#pragma once

// What the translated VU1 programs (build/native/vu1programs.cpp, made by native/tools/vu1_translate.py) are written with:
// four-lane vectors of the VU's registers, its float rules on them, and the hand-over of the pipelines' state to and from the
// interpreter's form (vu.h's VuMicroState). Every operation gives the bits the interpreter's (vu.cpp) gives; the arithmetic is
// the host's IEEE single precision under the rounding the run sets (toward zero), lane by lane, never fused.

#include "common.h"

#include "vu.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace Ee::Vu1Jit
{
typedef u32 V4u __attribute__((vector_size(16)));
typedef s32 V4i __attribute__((vector_size(16)));
typedef f32 V4f __attribute__((vector_size(16)));

#if defined(__clang__)
#define VU1_SHUFFLE(v, a, b, c, d) __builtin_shufflevector((v), (v), a, b, c, d)
#else
#define VU1_SHUFFLE(v, a, b, c, d) __builtin_shuffle((v), V4i{a, b, c, d})
#endif

inline V4f AsF(V4u v)
{
    return reinterpret_cast<V4f&>(v);
}

inline V4u AsU(V4f v)
{
    return reinterpret_cast<V4u&>(v);
}

inline V4u Splat(u32 x)
{
    return V4u{x, x, x, x};
}

inline V4u Select(V4u mask, V4u a, V4u b)
{
    return (mask & a) | (~mask & b);
}

inline V4u Min(V4u a, V4u b)
{
    return Select(reinterpret_cast<V4u>(a < b), a, b);
}

inline V4u Max(V4u a, V4u b)
{
    return Select(reinterpret_cast<V4u>(a > b), a, b);
}

inline V4u Load(const u8* at)
{
    V4u v;
    std::memcpy(&v, at, 16);
    return v;
}

inline void Store(u8* at, V4u v)
{
    std::memcpy(at, &v, 16);
}

inline V4u LoadVf(const Vf& r)
{
    V4u v;
    std::memcpy(&v, r.u, 16);
    return v;
}

inline void StoreVf(Vf& r, V4u v)
{
    std::memcpy(r.u, &v, 16);
}

// A constant the compiler mustn't fold into float arithmetic (it would round to nearest)
inline u32 Opaque(u32 x)
{
    asm volatile("" : "+r"(x));
    return x;
}

// Lanes of a destination mask (x is bit 0): new in the mask's lanes, old elsewhere
template <u32 Mask>
inline V4u Blend(V4u old, V4u neu)
{
    if constexpr ((Mask & 0xF) == 0xF)
    {
        return neu;
    }
    else if constexpr ((Mask & 0xF) == 0)
    {
        return old;
    }
    else
    {
        constexpr V4u m = {(Mask & 1) ? ~0u : 0u, (Mask & 2) ? ~0u : 0u, (Mask & 4) ? ~0u : 0u, (Mask & 8) ? ~0u : 0u};
        return (neu & m) | (old & ~m);
    }
}

// An operand as the VU reads it (vu.cpp's VuFloat): an exponent of 0 a zero of its sign, of 255 the largest float of its sign.
// The same rule makes an FMAC result (MacUpdate's): a denormal result a zero of its sign
inline V4u Clamp(V4u x)
{
    V4u magnitude = x & 0x7FFFFFFFu;
    V4u sign = x ^ magnitude;
    magnitude = Min(magnitude, Splat(0x7F7FFFFFu));
    magnitude &= reinterpret_cast<V4u>(magnitude > 0x007FFFFFu);
    return magnitude | sign;
}

// An FMAC result as MacUpdate leaves it: a denormal a zero of its sign. (An exponent of 255 can't come out of arithmetic on
// clamped operands rounded toward zero; Clamp would make it the largest float, as MacUpdate does)
inline V4u Result(V4u raw)
{
    V4u magnitude = raw & 0x7FFFFFFFu;
    V4u keep = reinterpret_cast<V4u>(magnitude > 0x007FFFFFu) | 0x80000000u;
    return raw & keep;
}

inline u32 ClampScalar(u32 x)
{
    switch (x & 0x7F800000)
    {
    case 0:
        return x & 0x80000000;
    case 0x7F800000:
        return (x & 0x80000000) | 0x7F7FFFFF;
    default:
        return x;
    }
}

template <int Lane>
inline V4u SplatLane(V4u v)
{
    return VU1_SHUFFLE(v, Lane, Lane, Lane, Lane);
}

// MAX and MINI: the bits compared as signed magnitudes (vu.cpp's FpMax and FpMin)
inline V4u FpMax(V4u a, V4u b)
{
    V4i sa = reinterpret_cast<V4i>(a);
    V4i sb = reinterpret_cast<V4i>(b);
    V4u bothNegative = reinterpret_cast<V4u>((sa & sb) < 0);
    V4u greater = reinterpret_cast<V4u>(sa > sb);
    V4u larger = Select(greater, a, b);
    V4u smaller = Select(greater, b, a);
    return Select(bothNegative, smaller, larger);
}

inline V4u FpMin(V4u a, V4u b)
{
    V4i sa = reinterpret_cast<V4i>(a);
    V4i sb = reinterpret_cast<V4i>(b);
    V4u bothNegative = reinterpret_cast<V4u>((sa & sb) < 0);
    V4u greater = reinterpret_cast<V4u>(sa > sb);
    V4u larger = Select(greater, a, b);
    V4u smaller = Select(greater, b, a);
    return Select(bothNegative, larger, smaller);
}

// ITOF: the integer to a float (rounded as the run rounds), scaled down by 2^shift
template <u32 Shift>
inline V4u Itof(V4u v)
{
    V4f f = __builtin_convertvector(reinterpret_cast<V4i>(v), V4f);
    if constexpr (Shift != 0)
    {
        f = f * AsF(Splat(Opaque(0x3F800000u - (Shift << 23))));
    }

    return AsU(f);
}

// FTOI: the float (as it is, not clamped) scaled up by 2^shift and truncated, saturated past 2^31
template <u32 Shift>
inline V4u Ftoi(V4u v)
{
    V4f f = AsF(v);
    if constexpr (Shift != 0)
    {
        f = f * AsF(Splat(Opaque(0x3F800000u + (Shift << 23))));
    }

    V4u bits = AsU(f);
    V4u saturate = reinterpret_cast<V4u>((bits & 0x7F800000u) >= 0x4F000000u);
    V4u saturated = Select(reinterpret_cast<V4u>(reinterpret_cast<V4i>(bits) < 0), Splat(0x80000000u), Splat(0x7FFFFFFFu));
    V4f safe = AsF(bits & ~saturate);
    V4u converted = reinterpret_cast<V4u>(__builtin_convertvector(safe, V4i));
    return Select(saturate, saturated, converted);
}

// A MAC flag's source: the raw results (before the result rule) of the lanes an instruction set, 1.0 in the lanes it cleared.
// MacOf gives the flag from it: each lane's sign, zero (and underflow for a denormal) and overflow bits as MacUpdate sets them
inline u32 MacOf(V4u v)
{
    u32 mac = 0;
    for (int c = 0; c < 4; c++)
    {
        u32 x = v[c];
        u32 shift = 3 - c;
        if (x & 0x80000000)
        {
            mac |= 0x10u << shift;
        }

        if ((x & 0x7FFFFFFF) == 0)
        {
            mac |= 0x1u << shift;
        }
        else if ((x & 0x7F800000) == 0)
        {
            mac |= 0x101u << shift;
        }
        else if ((x & 0x7F800000) == 0x7F800000)
        {
            mac |= 0x1000u << shift;
        }
    }

    return mac;
}

// A MAC flag as a source (false when no result gives it)
inline bool MacSource(u32 mac, V4u& out)
{
    for (int c = 0; c < 4; c++)
    {
        u32 shift = 3 - c;
        u32 zero = (mac >> shift) & 1;
        u32 sign = (mac >> (4 + shift)) & 1;
        u32 under = (mac >> (8 + shift)) & 1;
        u32 over = (mac >> (12 + shift)) & 1;
        u32 x;
        if (over)
        {
            if (zero || under)
            {
                return false;
            }

            x = 0x7F800000;
        }
        else if (under)
        {
            if (!zero)
            {
                return false;
            }

            x = 1;
        }
        else if (zero)
        {
            x = 0;
        }
        else
        {
            x = 0x3F800000;
        }

        out[c] = x | (sign << 31);
    }

    return (mac >> 16) == 0;
}

// The status flag's low bits from a MAC flag (StatusUpdate)
inline u32 StatusLow(u32 mac)
{
    return ((mac & 0x000F) ? 1u : 0u) | ((mac & 0x00F0) ? 2u : 0u) | ((mac & 0x0F00) ? 4u : 0u) | ((mac & 0xF000) ? 8u : 0u);
}

// The sticky status bits of every FMAC result of the run: their sources' signs ORed, the smallest magnitude (a zero exponent:
// zero), the smallest magnitude less one (a denormal: underflow) and the largest (overflow)
struct Sticky
{
    V4u signs = {0, 0, 0, 0};
    V4u smallest = {~0u, ~0u, ~0u, ~0u};
    V4u smallestNonzero = {~0u, ~0u, ~0u, ~0u};
    V4u largest = {0, 0, 0, 0};
};

inline void Accumulate(Sticky& sticky, V4u source)
{
    V4u magnitude = source & 0x7FFFFFFFu;
    sticky.signs |= source;
    sticky.smallest = Min(sticky.smallest, magnitude);
    sticky.smallestNonzero = Min(sticky.smallestNonzero, magnitude - 1u);
    sticky.largest = Max(sticky.largest, magnitude);
}

// The same for an arithmetic instruction's source, whose lanes are raw results or 1.0: no overflow can be among them
// (arithmetic on clamped operands rounded toward zero never reaches an exponent of 255)
inline void AccumulateResults(Sticky& sticky, V4u source)
{
    V4u magnitude = source & 0x7FFFFFFFu;
    sticky.signs |= source;
    sticky.smallest = Min(sticky.smallest, magnitude);
    sticky.smallestNonzero = Min(sticky.smallestNonzero, magnitude - 1u);
}

inline u32 StickyLow(const Sticky& sticky)
{
    u32 low = 0;
    for (int c = 0; c < 4; c++)
    {
        low |= (sticky.signs[c] & 0x80000000) ? 2u : 0u;
        low |= sticky.smallest[c] <= 0x007FFFFF ? 1u : 0u;
        low |= sticky.smallestNonzero[c] < 0x007FFFFF ? 4u : 0u;
        low |= sticky.largest[c] >= 0x7F800000 ? 8u : 0u;
    }

    return low;
}

// DIV, SQRT and RSQRT as vu.cpp's interpreter does them: Q's value, and the status bits they set (0x10 invalid, 0x20 divide
// by zero)
inline u32 Divide(u32 fsBits, u32 ftBits, u32& flags)
{
    f32 ft = std::bit_cast<f32>(ClampScalar(ftBits));
    f32 fs = std::bit_cast<f32>(ClampScalar(fsBits));
    bool signs = ((ftBits ^ fsBits) & 0x80000000) != 0;
    flags = 0;
    if (ft == 0.0f)
    {
        flags = fs == 0.0f ? 0x10 : 0x20;
        return signs ? 0xFF7FFFFFu : 0x7F7FFFFFu;
    }

    return ClampScalar(std::bit_cast<u32>(fs / ft));
}

inline u32 SquareRoot(u32 ftBits, u32& flags)
{
    f32 ft = std::bit_cast<f32>(ClampScalar(ftBits));
    flags = ft < 0.0f ? 0x10 : 0;
    return ClampScalar(std::bit_cast<u32>(std::sqrt(std::fabs(ft))));
}

inline u32 ReciprocalSquareRoot(u32 fsBits, u32 ftBits, u32& flags)
{
    f32 ft = std::bit_cast<f32>(ClampScalar(ftBits));
    f32 fs = std::bit_cast<f32>(ClampScalar(fsBits));
    bool signs = ((ftBits ^ fsBits) & 0x80000000) != 0;
    flags = 0;
    if (ft == 0.0f)
    {
        flags = 0x20;
        if (fs != 0.0f)
        {
            return signs ? 0xFF7FFFFFu : 0x7F7FFFFFu;
        }

        flags |= 0x10;
        return signs ? 0x80000000u : 0;
    }

    if (ft < 0.0f)
    {
        flags = 0x10;
    }

    f32 root = std::sqrt(std::fabs(ft));
    return ClampScalar(std::bit_cast<u32>(fs / root));
}

// The static part of the pipelines' state between two instructions: what's in flight relative to the next instruction's cycle.
// A translated program's code is made for one of these at each place it can be entered (vu1translate.cpp matches the
// interpreter's state against them)
struct Signature
{
    struct Fmac
    {
        s8 start;
        u8 upperReg;
        u8 upperMask;
        u8 lowerReg;
        u8 lowerMask;
        u8 flags;
    };
    struct Ialu
    {
        s8 ready;
        u8 reg;
    };
    u8 fmacCount;
    u8 ialuCount;
    u8 fdivBusy;
    s8 fdivReady;
    u8 backupCycles;
    u8 backupReg;
    u8 anyEntry;
    u8 pad;
    Fmac fmac[3];
    Ialu ialu[4];
};

// A variant: where it starts, the code's label in its program's function, and the state it's made for (a signature's
// number in g_Signatures; sorted by offset)
struct Variant
{
    u16 offset;
    u16 label;
    u32 signature;
    // The instruction at offset's key (EntryKey): the dispatcher's index of where programs can be entered
    u32 entryKey;
};

// The registers and the pipelines' values a translated program keeps in its own variables, handed over at its entries and exits
struct Slots
{
    V4u pendingMac;
    V4u visibleMac;
    V4u entryMac[3];
    u32 pendingClip;
    u32 visibleClip;
    u32 entryClip[3];
    u32 visibleDivFlags;
    u32 fdivValue;
    u32 fdivFlags;
    u32 backupValue;
    u32 stickyBits;
    Sticky sticky;
    // Where a translated program stopped: the signature of the state it left (its number)
    u32 exitSignature;
};

// FNV-1a (64 bits) of size instructions' bytes from base in micro memory (wrapping at mask): a translated program is told by it
// (the build keeps no copy of the microcode, vu1_translate.py's code_hash)
inline u64 CodeHash(const u64* code, u32 mask, u32 base, u32 size)
{
    u64 hash = 0xCBF29CE484222325ull;
    for (u32 n = 0; n < size; n++)
    {
        u64 word = code[(base + n) & mask];
        for (u32 i = 0; i < 8; i++)
        {
            hash = (hash ^ ((word >> (8 * i)) & 0xFF)) * 0x100000001B3ull;
        }
    }

    return hash;
}

// An instruction's key (its hash's top 32 bits): the index of the places programs can be entered
inline u32 EntryKey(u64 word)
{
    return static_cast<u32>(CodeHash(&word, 0, 0, 1) >> 32);
}

// A translated program: its code's hash (checked against micro memory before it runs), its function and the places it can be
// entered
struct Program
{
    u64 codeHash;
    u32 size;
    // The function: runs from a variant (its label) until an exit, with the slots' values. Returns 1 when the program ended
    // (its E bit), 0 when the run goes on at state.pc; either way the slots and slots.exitSignature are the state it left,
    // which LeaveTranslation makes the interpreter's (state.pc, the registers and memory are already up to date)
    int (*run)(Vu& vu, VuMicroState& state, Slots& slots, u32 base, u32 label);
    const Variant* variants;
    u32 variantCount;
};

// A translated program's exit: the pipelines' state in the interpreter's form, the flags made visible (vu1translate.cpp)
void LeaveTranslation(Vu& vu, VuMicroState& state, const Slots& slots, const Signature& signature);

extern const Signature g_Signatures[];
extern const u32 g_SignatureCount;
extern const Program g_Programs[];
extern const u32 g_ProgramCount;
// Each program's address in the retail executable's .vutext
extern const u32 g_ProgramAddresses[];
// VU0's (its decal program: vu1_translate.py --vu0)
extern const Signature g_SignaturesVu0[];
extern const u32 g_SignatureCountVu0;
extern const Program g_ProgramsVu0[];
extern const u32 g_ProgramCountVu0;
}
