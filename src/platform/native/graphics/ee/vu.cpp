// The vector units' micro mode, interpreted (vu.h). The instruction set and its timing are Sony's VU User's Manual's; the
// pipeline model (stalls, flag and Q/P latencies, the same-cycle and branch read rules) is the one PCSX2's interpreter
// established against the hardware.

#include "vu.h"

#include "rounding.h"
#include "../state.h"

#include <algorithm>
#include <bit>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>

#pragma STDC FENV_ACCESS ON
#pragma clang fp contract(off)

namespace Ee
{
namespace
{
enum class Upper : u8
{
    Nop,
    Arith,
    Itof,
    Ftoi,
    Abs,
    Clip,
    Opmula,
    Opmsub,
    Max,
    Mini,
    Unknown,
};

// What an arithmetic upper instruction does, where its second operand comes from and where its result goes
enum class Op : u8
{
    Add,
    Sub,
    Mul,
    Madd,
    Msub,
};

enum class Source : u8
{
    Vector,
    Broadcast,
    I,
    Q,
};

enum class Lower : u8
{
    Nop,
    Lq,
    Sq,
    Ilw,
    Isw,
    Iaddiu,
    Isubiu,
    Fceq,
    Fcset,
    Fcand,
    Fcor,
    Fseq,
    Fsset,
    Fsand,
    Fsor,
    Fmeq,
    Fmand,
    Fmor,
    Fcget,
    B,
    Bal,
    Jr,
    Jalr,
    Ibeq,
    Ibne,
    Ibltz,
    Ibgtz,
    Iblez,
    Ibgez,
    Iadd,
    Isub,
    Iaddi,
    Iand,
    Ior,
    Move,
    Mr32,
    Lqi,
    Sqi,
    Lqd,
    Sqd,
    Div,
    Sqrt,
    Rsqrt,
    Waitq,
    Mtir,
    Mfir,
    Ilwr,
    Iswr,
    Rnext,
    Rget,
    Rinit,
    Rxor,
    Mfp,
    Xtop,
    Xitop,
    Xgkick,
    Esadd,
    Ersadd,
    Eleng,
    Erleng,
    Eatanxy,
    Eatanxz,
    Esum,
    Ercpr,
    Esqrt,
    Ersqrt,
    Esin,
    Eatan,
    Eexp,
    Waitp,
    Unknown,
};

enum class Pipe : u8
{
    None,
    Fmac,
    Fdiv,
    Efu,
    Ialu,
    Branch,
};

constexpr u32 FlagStatus = 2;
constexpr u32 FlagClip = 4;

f32 AsFloat(u32 bits)
{
    return std::bit_cast<f32>(bits);
}

u32 AsBits(f32 value)
{
    return std::bit_cast<u32>(value);
}

// A VU operand: denormals read as zero, infinities and NaNs as the largest float
f32 VuFloat(u32 bits)
{
    switch (bits & 0x7F800000)
    {
    case 0:
        return AsFloat(bits & 0x80000000);
    case 0x7F800000:
        return AsFloat((bits & 0x80000000) | 0x7F7FFFFF);
    default:
        return AsFloat(bits);
    }
}

s32 Imm11(u32 word)
{
    return (word & 0x400) ? static_cast<s32>(word | 0xFFFFF800u) : static_cast<s32>(word & 0x3FF);
}
}

struct Vu::Decoded
{
    u32 lowerWord;
    u32 upperWord;
    bool iBit;
    bool eBit;
    // The upper instruction
    Upper upper;
    Op op;
    Source source;
    bool toAcc;
    u8 bc;
    u8 shift;
    u8 dest;
    u8 fd;
    u8 fs;
    u8 ft;
    // The lower instruction
    Lower lower;
    Pipe lowerPipe;
    u8 lowerDest;
    u8 lfs;
    u8 lft;
    u8 lfd;
    u8 fsf;
    u8 ftf;
    u8 latency;
    // What they read and write, for the stalls and the same-cycle rule: VF registers and their component masks
    u8 upperWriteVf;
    u8 upperWriteMask;
    u8 upperRead0;
    u8 upperRead0Mask;
    u8 upperRead1;
    u8 upperRead1Mask;
    u8 lowerWriteVf;
    u8 lowerWriteMask;
    u8 lowerRead0;
    u8 lowerRead0Mask;
    u8 lowerRead1;
    u8 lowerRead1Mask;
    // The integer register the lower writes (0 none) and the ones it reads (a mask), and whether its result is late (ILW)
    u8 lowerWriteVi;
    u16 lowerReadVi;
    // The flags the upper writes (FlagClip for CLIP), the lower (FCSET, FSSET)
    u8 upperFlags;
    u8 lowerFlags;
};

Vu::Vu(u32 index) : index_(index)
{
    dataSize_ = index == 0 ? 0x1000 : 0x4000;
    codeSize_ = index == 0 ? 0x200 : 0x800;
    data_ = static_cast<u8*>(std::calloc(1, dataSize_));
    vf[0].f[3] = 1.0f;
}

template <typename Io>
void Vu::Serialize(Io& io)
{
    io.Field(vf);
    io.Field(vi);
    io.Field(acc);
    io.Field(q);
    io.Field(p);
    io.Field(i);
    io.Field(r);
    io.Field(statusFlag);
    io.Field(macFlag);
    io.Field(clipFlag);
    io.Field(top);
    io.Field(itop);
    io.Field(pc_);
    io.Bytes(data_, dataSize_);
    io.Field(code_);
    if constexpr (Io::Loading)
    {
        codeVersion_++;
        codeChanges_.set();
        for (Decoded*& decoded : decoded_)
        {
            delete decoded;
            decoded = nullptr;
        }
    }
}

template void Vu::Serialize<NativeGraphics::Saver>(NativeGraphics::Saver&);
template void Vu::Serialize<NativeGraphics::Loader>(NativeGraphics::Loader&);

void Vu::WriteCode(u32 address, const u8* code, u32 instructions)
{
    for (u32 n = 0; n < instructions; n++)
    {
        u32 at = (address + n) & (codeSize_ - 1);
        if (std::memcmp(&code_[at], code + n * 8, 8) == 0)
        {
            continue;
        }

        std::memcpy(&code_[at], code + n * 8, 8);
        codeVersion_++;
        codeChanges_.set(at);
        if (decoded_[at] != nullptr)
        {
            delete decoded_[at];
            decoded_[at] = nullptr;
        }
    }
}

void Vu::CopyFrom(const Vu& other)
{
    WriteCode(0, reinterpret_cast<const u8*>(other.code_.data()), codeSize_);
    std::memcpy(data_, other.data_, dataSize_);
    std::memcpy(vf, other.vf, sizeof(vf));
    std::memcpy(vi, other.vi, sizeof(vi));
    acc = other.acc;
    q = other.q;
    p = other.p;
    i = other.i;
    r = other.r;
    statusFlag = other.statusFlag;
    macFlag = other.macFlag;
    clipFlag = other.clipFlag;
    top = other.top;
    itop = other.itop;
    pc_ = other.pc_;
}

namespace
{
u8 DestMask(u32 word)
{
    return static_cast<u8>(((word >> 24) & 1) | ((word >> 23) & 1) << 1 | ((word >> 22) & 1) << 2 | ((word >> 21) & 1) << 3);
}
}

void Vu::Decode(u32 address)
{
    auto* d = new Decoded{};
    u64 pair = code_[address];
    u32 lw = static_cast<u32>(pair);
    u32 uw = static_cast<u32>(pair >> 32);
    d->lowerWord = lw;
    d->upperWord = uw;
    d->iBit = (uw >> 31) & 1;
    d->eBit = (uw >> 30) & 1;
    d->dest = DestMask(uw);
    d->fd = (uw >> 6) & 0x1F;
    d->fs = (uw >> 11) & 0x1F;
    d->ft = (uw >> 16) & 0x1F;
    d->bc = uw & 3;

    // The upper instruction
    u32 op6 = uw & 0x3F;
    auto arith = [&](Op op, Source source, bool toAcc) {
        d->upper = Upper::Arith;
        d->op = op;
        d->source = source;
        d->toAcc = toAcc;
    };
    if (op6 < 0x3C)
    {
        switch (op6 >> 2)
        {
        case 0x0:
            arith(Op::Add, Source::Broadcast, false);
            break;
        case 0x1:
            arith(Op::Sub, Source::Broadcast, false);
            break;
        case 0x2:
            arith(Op::Madd, Source::Broadcast, false);
            break;
        case 0x3:
            arith(Op::Msub, Source::Broadcast, false);
            break;
        case 0x4:
            d->upper = Upper::Max;
            d->source = Source::Broadcast;
            break;
        case 0x5:
            d->upper = Upper::Mini;
            d->source = Source::Broadcast;
            break;
        case 0x6:
            arith(Op::Mul, Source::Broadcast, false);
            break;
        default:
            switch (op6)
            {
            case 0x1C:
                arith(Op::Mul, Source::Q, false);
                break;
            case 0x1D:
                d->upper = Upper::Max;
                d->source = Source::I;
                break;
            case 0x1E:
                arith(Op::Mul, Source::I, false);
                break;
            case 0x1F:
                d->upper = Upper::Mini;
                d->source = Source::I;
                break;
            case 0x20:
                arith(Op::Add, Source::Q, false);
                break;
            case 0x21:
                arith(Op::Madd, Source::Q, false);
                break;
            case 0x22:
                arith(Op::Add, Source::I, false);
                break;
            case 0x23:
                arith(Op::Madd, Source::I, false);
                break;
            case 0x24:
                arith(Op::Sub, Source::Q, false);
                break;
            case 0x25:
                arith(Op::Msub, Source::Q, false);
                break;
            case 0x26:
                arith(Op::Sub, Source::I, false);
                break;
            case 0x27:
                arith(Op::Msub, Source::I, false);
                break;
            case 0x28:
                arith(Op::Add, Source::Vector, false);
                break;
            case 0x29:
                arith(Op::Madd, Source::Vector, false);
                break;
            case 0x2A:
                arith(Op::Mul, Source::Vector, false);
                break;
            case 0x2B:
                d->upper = Upper::Max;
                d->source = Source::Vector;
                break;
            case 0x2C:
                arith(Op::Sub, Source::Vector, false);
                break;
            case 0x2D:
                arith(Op::Msub, Source::Vector, false);
                break;
            case 0x2E:
                d->upper = Upper::Opmsub;
                break;
            case 0x2F:
                d->upper = Upper::Mini;
                d->source = Source::Vector;
                break;
            default:
                d->upper = Upper::Unknown;
                break;
            }
            break;
        }
    }
    else
    {
        u32 index = (uw >> 6) & 0x1F;
        u32 bc = uw & 3;
        static constexpr u8 Shifts[4] = {0, 4, 12, 15};
        switch (index)
        {
        case 0:
            arith(Op::Add, Source::Broadcast, true);
            break;
        case 1:
            arith(Op::Sub, Source::Broadcast, true);
            break;
        case 2:
            arith(Op::Madd, Source::Broadcast, true);
            break;
        case 3:
            arith(Op::Msub, Source::Broadcast, true);
            break;
        case 4:
            d->upper = Upper::Itof;
            d->shift = Shifts[bc];
            break;
        case 5:
            d->upper = Upper::Ftoi;
            d->shift = Shifts[bc];
            break;
        case 6:
            arith(Op::Mul, Source::Broadcast, true);
            break;
        case 7:
            switch (bc)
            {
            case 0:
                arith(Op::Mul, Source::Q, true);
                break;
            case 1:
                d->upper = Upper::Abs;
                break;
            case 2:
                arith(Op::Mul, Source::I, true);
                break;
            default:
                d->upper = Upper::Clip;
                break;
            }
            break;
        case 8:
            switch (bc)
            {
            case 0:
                arith(Op::Add, Source::Q, true);
                break;
            case 1:
                arith(Op::Madd, Source::Q, true);
                break;
            case 2:
                arith(Op::Add, Source::I, true);
                break;
            default:
                arith(Op::Madd, Source::I, true);
                break;
            }
            break;
        case 9:
            switch (bc)
            {
            case 0:
                arith(Op::Sub, Source::Q, true);
                break;
            case 1:
                arith(Op::Msub, Source::Q, true);
                break;
            case 2:
                arith(Op::Sub, Source::I, true);
                break;
            default:
                arith(Op::Msub, Source::I, true);
                break;
            }
            break;
        case 10:
            switch (bc)
            {
            case 0:
                arith(Op::Add, Source::Vector, true);
                break;
            case 1:
                arith(Op::Madd, Source::Vector, true);
                break;
            case 2:
                arith(Op::Mul, Source::Vector, true);
                break;
            default:
                d->upper = Upper::Unknown;
                break;
            }
            break;
        case 11:
            switch (bc)
            {
            case 0:
                arith(Op::Sub, Source::Vector, true);
                break;
            case 1:
                arith(Op::Msub, Source::Vector, true);
                break;
            case 2:
                d->upper = Upper::Opmula;
                break;
            default:
                d->upper = Upper::Nop;
                break;
            }
            break;
        default:
            d->upper = Upper::Unknown;
            break;
        }
    }

    // What the upper reads and writes
    switch (d->upper)
    {
    case Upper::Arith:
    case Upper::Max:
    case Upper::Mini:
        d->upperRead0 = d->fs;
        d->upperRead0Mask = d->dest;
        if (d->source == Source::Vector)
        {
            d->upperRead1 = d->ft;
            d->upperRead1Mask = d->dest;
        }
        else if (d->source == Source::Broadcast)
        {
            d->upperRead1 = d->ft;
            d->upperRead1Mask = static_cast<u8>(1u << d->bc);
        }

        if (!(d->upper == Upper::Arith && d->toAcc))
        {
            d->upperWriteVf = d->fd;
            d->upperWriteMask = d->dest;
        }

        break;
    case Upper::Itof:
    case Upper::Ftoi:
    case Upper::Abs:
        d->upperRead0 = d->fs;
        d->upperRead0Mask = d->dest;
        d->upperWriteVf = d->ft;
        d->upperWriteMask = d->dest;
        break;
    case Upper::Clip:
        d->upperRead0 = d->fs;
        d->upperRead0Mask = 7;
        d->upperRead1 = d->ft;
        d->upperRead1Mask = 8;
        d->upperFlags = FlagClip;
        break;
    case Upper::Opmula:
    case Upper::Opmsub:
        d->upperRead0 = d->fs;
        d->upperRead0Mask = 7;
        d->upperRead1 = d->ft;
        d->upperRead1Mask = 7;
        if (d->upper == Upper::Opmsub)
        {
            d->upperWriteVf = d->fd;
            d->upperWriteMask = 7;
        }

        break;
    default:
        break;
    }

    if (d->iBit)
    {
        d->lower = Lower::Nop;
        d->lowerPipe = Pipe::None;
        decoded_[address] = d;
        return;
    }

    // The lower instruction
    d->lowerDest = DestMask(lw);
    d->lfs = (lw >> 11) & 0x1F;
    d->lft = (lw >> 16) & 0x1F;
    d->lfd = (lw >> 6) & 0x1F;
    d->fsf = (lw >> 21) & 3;
    d->ftf = (lw >> 23) & 3;
    u32 op7 = lw >> 25;
    Lower lower = Lower::Unknown;
    if (op7 == 0x40)
    {
        u32 f6 = lw & 0x3F;
        switch (f6)
        {
        case 0x30:
            lower = Lower::Iadd;
            break;
        case 0x31:
            lower = Lower::Isub;
            break;
        case 0x32:
            lower = Lower::Iaddi;
            break;
        case 0x34:
            lower = Lower::Iand;
            break;
        case 0x35:
            lower = Lower::Ior;
            break;
        case 0x3C:
        case 0x3D:
        case 0x3E:
        case 0x3F:
        {
            u32 index = (lw >> 6) & 0x1F;
            static constexpr Lower Table[4][32] = {
                {Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Move, Lower::Lqi, Lower::Div, Lower::Mtir, Lower::Rnext, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Mfp, Lower::Xtop, Lower::Xgkick, Lower::Esadd, Lower::Eatanxy, Lower::Esqrt, Lower::Esin},
                {Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Mr32, Lower::Sqi, Lower::Sqrt, Lower::Mfir, Lower::Rget, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Xitop, Lower::Unknown, Lower::Ersadd, Lower::Eatanxz, Lower::Ersqrt, Lower::Eatan},
                {Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Lqd, Lower::Rsqrt, Lower::Ilwr, Lower::Rinit, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Eleng, Lower::Esum, Lower::Ercpr, Lower::Eexp},
                {Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Sqd, Lower::Waitq, Lower::Iswr, Lower::Rxor, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Unknown,
                 Lower::Unknown, Lower::Unknown, Lower::Unknown, Lower::Erleng, Lower::Unknown, Lower::Waitp, Lower::Unknown},
            };
            lower = Table[lw & 3][index];
            break;
        }
        default:
            break;
        }
    }
    else
    {
        switch (op7)
        {
        case 0x00:
            lower = Lower::Lq;
            break;
        case 0x01:
            lower = Lower::Sq;
            break;
        case 0x04:
            lower = Lower::Ilw;
            break;
        case 0x05:
            lower = Lower::Isw;
            break;
        case 0x08:
            lower = Lower::Iaddiu;
            break;
        case 0x09:
            lower = Lower::Isubiu;
            break;
        case 0x10:
            lower = Lower::Fceq;
            break;
        case 0x11:
            lower = Lower::Fcset;
            break;
        case 0x12:
            lower = Lower::Fcand;
            break;
        case 0x13:
            lower = Lower::Fcor;
            break;
        case 0x14:
            lower = Lower::Fseq;
            break;
        case 0x15:
            lower = Lower::Fsset;
            break;
        case 0x16:
            lower = Lower::Fsand;
            break;
        case 0x17:
            lower = Lower::Fsor;
            break;
        case 0x18:
            lower = Lower::Fmeq;
            break;
        case 0x1A:
            lower = Lower::Fmand;
            break;
        case 0x1B:
            lower = Lower::Fmor;
            break;
        case 0x1C:
            lower = Lower::Fcget;
            break;
        case 0x20:
            lower = Lower::B;
            break;
        case 0x21:
            lower = Lower::Bal;
            break;
        case 0x24:
            lower = Lower::Jr;
            break;
        case 0x25:
            lower = Lower::Jalr;
            break;
        case 0x28:
            lower = Lower::Ibeq;
            break;
        case 0x29:
            lower = Lower::Ibne;
            break;
        case 0x2C:
            lower = Lower::Ibltz;
            break;
        case 0x2D:
            lower = Lower::Ibgtz;
            break;
        case 0x2E:
            lower = Lower::Iblez;
            break;
        case 0x2F:
            lower = Lower::Ibgez;
            break;
        default:
            break;
        }
    }

    d->lower = lower;
    u8 is = d->lfs & 0xF;
    u8 it = d->lft & 0xF;
    u8 id = d->lfd & 0xF;
    Pipe pipe = Pipe::None;
    switch (lower)
    {
    case Lower::Lq:
    case Lower::Lqi:
    case Lower::Lqd:
        pipe = Pipe::Fmac;
        d->lowerWriteVf = d->lft;
        d->lowerWriteMask = d->lowerDest;
        d->lowerReadVi = static_cast<u16>(1u << is);
        if (lower != Lower::Lq)
        {
            d->lowerWriteVi = is;
        }

        break;
    case Lower::Sq:
    case Lower::Sqi:
    case Lower::Sqd:
        pipe = Pipe::Fmac;
        d->lowerRead0 = d->lfs;
        d->lowerRead0Mask = d->lowerDest;
        d->lowerReadVi = static_cast<u16>(1u << it);
        if (lower != Lower::Sq)
        {
            d->lowerWriteVi = it;
        }

        break;
    case Lower::Move:
    case Lower::Mr32:
        pipe = Pipe::Fmac;
        d->lowerRead0 = d->lfs;
        d->lowerRead0Mask = lower == Lower::Move ? d->lowerDest : 0xF;
        d->lowerWriteVf = d->lft;
        d->lowerWriteMask = d->lowerDest;
        break;
    case Lower::Mfir:
        pipe = Pipe::Fmac;
        d->lowerWriteVf = d->lft;
        d->lowerWriteMask = d->lowerDest;
        d->lowerReadVi = static_cast<u16>(1u << is);
        break;
    case Lower::Mtir:
        pipe = Pipe::Fmac;
        d->lowerRead0 = d->lfs;
        d->lowerRead0Mask = static_cast<u8>(1u << d->fsf);
        d->lowerWriteVi = it;
        break;
    case Lower::Rget:
    case Lower::Rnext:
    case Lower::Mfp:
        pipe = Pipe::Fmac;
        d->lowerWriteVf = d->lft;
        d->lowerWriteMask = d->lowerDest;
        break;
    case Lower::Rinit:
    case Lower::Rxor:
        pipe = Pipe::Fmac;
        d->lowerRead0 = d->lfs;
        d->lowerRead0Mask = static_cast<u8>(1u << d->fsf);
        break;
    case Lower::Fcset:
        pipe = Pipe::Fmac;
        d->lowerFlags = FlagClip;
        break;
    case Lower::Fsset:
        pipe = Pipe::Fmac;
        d->lowerFlags = FlagStatus;
        break;
    case Lower::Div:
    case Lower::Sqrt:
    case Lower::Rsqrt:
        pipe = Pipe::Fdiv;
        d->latency = lower == Lower::Rsqrt ? 13 : 7;
        if (lower != Lower::Sqrt)
        {
            d->lowerRead0 = d->lfs;
            d->lowerRead0Mask = static_cast<u8>(1u << d->fsf);
        }

        d->lowerRead1 = d->lft;
        d->lowerRead1Mask = static_cast<u8>(1u << d->ftf);
        break;
    case Lower::Waitq:
        pipe = Pipe::Fdiv;
        break;
    case Lower::Esadd:
    case Lower::Ersadd:
    case Lower::Eleng:
    case Lower::Erleng:
    case Lower::Eatanxy:
    case Lower::Eatanxz:
    case Lower::Esum:
        pipe = Pipe::Efu;
        d->lowerRead0 = d->lfs;
        d->lowerRead0Mask = 0xF;
        break;
    case Lower::Ercpr:
    case Lower::Esqrt:
    case Lower::Ersqrt:
    case Lower::Esin:
    case Lower::Eatan:
    case Lower::Eexp:
        pipe = Pipe::Efu;
        d->lowerRead0 = d->lfs;
        d->lowerRead0Mask = static_cast<u8>(1u << d->fsf);
        break;
    case Lower::Waitp:
        pipe = Pipe::Efu;
        break;
    case Lower::Ilw:
    case Lower::Ilwr:
        pipe = Pipe::Ialu;
        d->latency = 4;
        d->lowerWriteVi = it;
        d->lowerReadVi = static_cast<u16>(1u << is);
        break;
    case Lower::Isw:
    case Lower::Iswr:
        pipe = Pipe::Ialu;
        d->lowerReadVi = static_cast<u16>(1u << is | 1u << it);
        break;
    case Lower::Iadd:
    case Lower::Isub:
    case Lower::Iand:
    case Lower::Ior:
        pipe = Pipe::Ialu;
        d->lowerWriteVi = id;
        d->lowerReadVi = static_cast<u16>(1u << is | 1u << it);
        break;
    case Lower::Iaddi:
    case Lower::Iaddiu:
    case Lower::Isubiu:
        pipe = Pipe::Ialu;
        d->lowerWriteVi = it;
        d->lowerReadVi = static_cast<u16>(1u << is);
        break;
    case Lower::Fseq:
    case Lower::Fsand:
    case Lower::Fsor:
    case Lower::Fcget:
    case Lower::Xtop:
    case Lower::Xitop:
        d->lowerWriteVi = it;
        break;
    case Lower::Fmeq:
    case Lower::Fmand:
    case Lower::Fmor:
        d->lowerWriteVi = it;
        d->lowerReadVi = static_cast<u16>(1u << is);
        break;
    case Lower::Fceq:
    case Lower::Fcand:
    case Lower::Fcor:
        d->lowerWriteVi = 1;
        break;
    case Lower::Ibeq:
    case Lower::Ibne:
        pipe = Pipe::Branch;
        d->lowerReadVi = static_cast<u16>(1u << is | 1u << it);
        break;
    case Lower::Ibltz:
    case Lower::Ibgtz:
    case Lower::Iblez:
    case Lower::Ibgez:
    case Lower::Jr:
        pipe = Pipe::Branch;
        d->lowerReadVi = static_cast<u16>(1u << is);
        break;
    case Lower::Jalr:
        pipe = Pipe::Branch;
        d->lowerReadVi = static_cast<u16>(1u << is);
        d->lowerWriteVi = it;
        break;
    case Lower::Bal:
        pipe = Pipe::Branch;
        d->lowerWriteVi = it;
        break;
    case Lower::B:
        pipe = Pipe::Branch;
        break;
    case Lower::Xgkick:
        d->lowerReadVi = static_cast<u16>(1u << is);
        break;
    default:
        break;
    }

    // The EFU's latencies
    switch (lower)
    {
    case Lower::Esadd:
        d->latency = 11;
        break;
    case Lower::Ersadd:
    case Lower::Eleng:
    case Lower::Ersqrt:
        d->latency = 18;
        break;
    case Lower::Erleng:
        d->latency = 24;
        break;
    case Lower::Eatanxy:
    case Lower::Eatanxz:
    case Lower::Eatan:
        d->latency = 54;
        break;
    case Lower::Esum:
    case Lower::Ercpr:
    case Lower::Esqrt:
        d->latency = 12;
        break;
    case Lower::Esin:
        d->latency = 29;
        break;
    case Lower::Eexp:
        d->latency = 44;
        break;
    default:
        break;
    }

    if (d->lowerWriteVf == 0)
    {
        d->lowerWriteMask = 0;
    }

    if (d->upperWriteVf == 0)
    {
        d->upperWriteMask = 0;
    }

    d->lowerPipe = pipe;
    decoded_[address] = d;
}

namespace
{
using FmacEntry = VuFmacEntry;
using Pipelines = VuPipelines;
using Pending = VuPending;

u32 MacUpdate(u32 shift, Pending& pending, f32 result)
{
    u32 v = AsBits(result);
    u32 exponent = (v >> 23) & 0xFF;
    u32 sign = v & 0x80000000;
    if (sign != 0)
    {
        pending.mac |= 0x0010u << shift;
    }
    else
    {
        pending.mac &= ~(0x0010u << shift);
    }

    if (result == 0.0f)
    {
        pending.mac = (pending.mac & ~(0x1100u << shift)) | (0x0001u << shift);
        return v;
    }

    switch (exponent)
    {
    case 0:
        pending.mac = (pending.mac & ~(0x1000u << shift)) | (0x0101u << shift);
        return sign;
    case 255:
        pending.mac = (pending.mac & ~(0x0101u << shift)) | (0x1000u << shift);
        return sign | 0x7F7FFFFF;
    default:
        pending.mac &= ~(0x1101u << shift);
        return v;
    }
}

u32 MacroResultFlags(Pending& pending, f32 value)
{
    return MacUpdate(0, pending, value);
}

void StatusUpdate(Pending& pending)
{
    u32 flags = 0;
    if (pending.mac & 0x000F)
    {
        flags |= 1;
    }

    if (pending.mac & 0x00F0)
    {
        flags |= 2;
    }

    if (pending.mac & 0x0F00)
    {
        flags |= 4;
    }

    if (pending.mac & 0xF000)
    {
        flags |= 8;
    }

    pending.status = (pending.status & ~0xFu) | flags;
}

u32 FpMax(u32 a, u32 b)
{
    s32 sa = static_cast<s32>(a);
    s32 sb = static_cast<s32>(b);
    return static_cast<u32>((sa < 0 && sb < 0) ? std::min(sa, sb) : std::max(sa, sb));
}

u32 FpMin(u32 a, u32 b)
{
    s32 sa = static_cast<s32>(a);
    s32 sb = static_cast<s32>(b);
    return static_cast<u32>((sa < 0 && sb < 0) ? std::max(sa, sb) : std::min(sa, sb));
}

f32 Eatan(f32 x)
{
    static const f32 Constants[9] = {0.999999344348907f, -0.333298563957214f, 0.199465364217758f, -0.139085337519646f,
                                     0.096420042216778f, -0.055909886956215f, 0.021861229091883f, -0.004054057877511f,
                                     0.785398185253143f};
    f32 result = (Constants[0] * x) + (Constants[1] * static_cast<f32>(std::pow(x, 3))) +
                 (Constants[2] * static_cast<f32>(std::pow(x, 5))) + (Constants[3] * static_cast<f32>(std::pow(x, 7))) +
                 (Constants[4] * static_cast<f32>(std::pow(x, 9))) + (Constants[5] * static_cast<f32>(std::pow(x, 11))) +
                 (Constants[6] * static_cast<f32>(std::pow(x, 13))) + (Constants[7] * static_cast<f32>(std::pow(x, 15)));
    result += Constants[8];
    return VuFloat(AsBits(result));
}
}

void Vu::Run(u32 start)
{
    if (!timing)
    {
        if (runner)
        {
            runner(start);
        }
        else
        {
            RunInterpreted(start);
        }

        FlushKick();
        return;
    }

    auto began = std::chrono::steady_clock::now();
    if (runner)
    {
        runner(start);
    }
    else
    {
        RunInterpreted(start);
    }

    FlushKick();
    runNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - began).count();
}

namespace
{
bool KicksAtOnce()
{
    static const bool atOnce = std::getenv("TWIN_VU1_KICK_AT_ONCE") != nullptr && std::getenv("TWIN_VU1_KICK_AT_ONCE")[0] != '0';
    return atOnce;
}

void Send(Vu& vu, u32 address)
{
    if (!vu.xgkick)
    {
        return;
    }

    if (!vu.timing)
    {
        vu.xgkick(address);
        return;
    }

    auto began = std::chrono::steady_clock::now();
    vu.xgkick(address);
    vu.kickNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - began).count();
}
}

void Vu::Kick(u32 address)
{
    if (KicksAtOnce())
    {
        Send(*this, address);
        return;
    }

    // The packet before goes first (a second XGKICK waits for the GIF to finish the first)
    FlushKick();
    kickPending = true;
    kickPendingAddress = address;
    kickPendingPc = kickPc;
}

void Vu::FlushKick()
{
    if (!kickPending)
    {
        return;
    }

    kickPending = false;
    const u32 pc = kickPc;
    kickPc = kickPendingPc;
    Send(*this, kickPendingAddress);
    kickPc = pc;
}

namespace
{
// $TWIN_VU_KICK_WRITES (the interpreter, TWIN_VU1=interpret): each instruction that writes into a packet XGKICK sent, before
// the GIF has it (the next XGKICK or the program's stop), told once: where in the packet and how many cycles after the XGKICK
struct KickWrites
{
    bool on = std::getenv("TWIN_VU_KICK_WRITES") != nullptr;
    bool pending = false;
    u32 start = 0;
    u32 end = 0;
    u64 cycle = 0;
    u32 pc = 0;
    std::set<std::pair<u32, u32>> told;
};
KickWrites g_KickWrites;

// A GIF packet's quadwords in VU memory, its tags' to the one with EOP
u32 PacketLength(const u8* data, u32 dataMask, u32 address)
{
    u32 at = address;
    for (u32 guard = 0; guard < 1024; guard++)
    {
        const u64 tag = *reinterpret_cast<const u64*>(data + ((at * 16) & dataMask));
        const u32 nloop = tag & 0x7FFF;
        const u32 flg = (tag >> 58) & 3;
        const u32 nreg = ((tag >> 60) & 0xF) != 0 ? (tag >> 60) & 0xF : 16;
        at += 1 + (flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop);
        if ((tag >> 15) & 1)
        {
            break;
        }
    }

    return at - address;
}
}

void Vu::RunInterpreted(u32 start)
{
    g_KickWrites.pending = false;
    RoundTowardZero rounding;
    VuMicroState state;
    BeginMicro(state, start);
    while (StepMicro(state))
    {
    }

    EndMicro(state);
}

void Vu::BeginMicro(VuMicroState& state, u32 start)
{
    state = VuMicroState{};
    state.pending.mac = macFlag;
    state.pending.status = statusFlag;
    state.pending.clip = clipFlag;
    state.startStatus = statusFlag;
    state.pc = start & (codeSize_ - 1);
    runStart = state.pc;
}

// The pipelines' finished results made visible
void Vu::FlushPipes(VuMicroState& state)
{
    VuPipelines& pipes = state.pipes;
    const u64 cycle = state.cycle;
    while (pipes.fmacCount > 0)
    {
        const FmacEntry& e = pipes.fmac[pipes.fmacRead];
        if (cycle - e.start < 4)
        {
            break;
        }

        if (e.flags & FlagClip)
        {
            clipFlag = e.clip;
        }

        if (e.flags & FlagStatus)
        {
            statusFlag = (statusFlag & 0x30) | (e.status & 0xFC0) | (e.status & 0xF);
        }
        else
        {
            statusFlag = (statusFlag & 0xFF0) | (e.status & 0xF) | ((e.status & 0xF) << 6);
        }

        macFlag = e.mac;
        pipes.fmacRead = (pipes.fmacRead + 1) & 3;
        pipes.fmacCount--;
    }

    if (pipes.fdiv && cycle - pipes.fdivStart >= pipes.fdivLatency)
    {
        pipes.fdiv = false;
        q = pipes.fdivValue;
        statusFlag = (statusFlag & 0xFCF) | (pipes.fdivStatus & 0xC30);
    }

    if (pipes.efu && cycle - pipes.efuStart >= pipes.efuLatency)
    {
        pipes.efu = false;
        p = pipes.efuValue;
    }
}

void Vu::EndMicro(VuMicroState& state)
{
    // The program's end: everything in the pipelines lands
    state.cycle += 64;
    FlushPipes(state);
    pc_ = state.pc;
}

bool Vu::StepMicro(VuMicroState& state)
{
    Pipelines& pipes = state.pipes;
    Pending& pending = state.pending;
    u64& cycle = state.cycle;
    u32& pc = state.pc;
    u32& ebit = state.ebit;
    u32& branch = state.branch;
    u32& branchTarget = state.branchTarget;
    bool& delayBranch = state.delayBranch;
    u32& delayTarget = state.delayTarget;
    u32& backupCycles = state.backupCycles;
    u32& backupReg = state.backupReg;
    u16& backupValue = state.backupValue;
    const u32 dataMask = dataSize_ - 1;
    const u32 codeMask = codeSize_ - 1;

    auto fmacStall = [&](u8 reg, u8 mask) {
        if (reg == 0 || mask == 0)
        {
            return;
        }

        for (u32 n = 0, at = pipes.fmacRead; n < pipes.fmacCount; n++, at = (at + 1) & 3)
        {
            const FmacEntry& e = pipes.fmac[at];
            if (cycle - e.start >= 4)
            {
                continue;
            }

            if ((e.upperReg == reg && (e.upperMask & mask)) || (e.lowerReg == reg && (e.lowerMask & mask)))
            {
                cycle = std::max(cycle, e.start + 4);
            }
        }
    };

    // The pipelines' finished results made visible
    auto flushPipes = [&]() { FlushPipes(state); };

    auto setBranch = [&](u32 target) {
        if (branch == 1)
        {
            delayTarget = target & codeMask;
            delayBranch = true;
        }
        else
        {
            branch = 2;
            branchTarget = target & codeMask;
        }
    };

    auto viRead = [&](u32 reg) -> s16 {
        if (backupCycles > 0 && backupReg == reg)
        {
            return static_cast<s16>(backupValue);
        }

        return static_cast<s16>(vi[reg]);
    };

    // An integer register written; the integer ALU's instructions (IADD, ISUB, IAND, IOR, IADDI, IADDIU, ISUBIU) keep its old
    // value for the branch rule, the others (loads' increments, flag reads, MTIR, XTOP...) don't
    auto writeVi = [&](u32 reg, u16 value, bool alu = false) {
        if (reg == 0)
        {
            return;
        }

        if (alu)
        {
            if (!(backupCycles > 0 && backupReg == reg))
            {
                backupReg = reg;
                backupValue = vi[reg];
            }

            backupCycles = 2;
        }

        vi[reg] = value;
    };

    {
        if (decoded_[pc] == nullptr)
        {
            Decode(pc);
        }

        const Decoded& d = *decoded_[pc];
        u32 thisPc = pc;
        pc = (pc + 1) & codeMask;
        executed++;
        if (d.eBit)
        {
            ebit = 2;
        }

        u64 before = cycle;
        // Stalls: the upper's and lower's reads of FMAC results in flight, FDIV and EFU instructions on their busy units,
        // branches on ILW's results
        fmacStall(d.upperRead0, d.upperRead0Mask);
        fmacStall(d.upperRead1, d.upperRead1Mask);
        if (!d.iBit)
        {
            fmacStall(d.lowerRead0, d.lowerRead0Mask);
            fmacStall(d.lowerRead1, d.lowerRead1Mask);
            if (d.lowerPipe == Pipe::Fdiv && pipes.fdiv)
            {
                cycle = std::max(cycle, pipes.fdivStart + pipes.fdivLatency);
            }
            else if (d.lowerPipe == Pipe::Efu && pipes.efu)
            {
                cycle = std::max(cycle, pipes.efuStart + pipes.efuLatency - 1);
            }
            else if (d.lowerPipe == Pipe::Branch)
            {
                for (u32 n = 0; n < 4; n++)
                {
                    if (pipes.ialuReady[n] > cycle && (d.lowerReadVi >> pipes.ialuReg[n]) & 1)
                    {
                        cycle = pipes.ialuReady[n];
                    }
                }
            }
        }

        flushPipes();
        u32 elapsed = static_cast<u32>(cycle - before) + 1;
        backupCycles = backupCycles > elapsed ? backupCycles - elapsed : 0;

        // The same cycle: the lower reads the registers the upper writes as they were
        Vf savedUpperTarget = {};
        bool restoreUpperTarget = false;
        bool discardLower = false;
        if (!d.iBit && d.upperWriteVf != 0)
        {
            if (d.lowerWriteVf == d.upperWriteVf)
            {
                discardLower = true;
            }
            else if (d.lowerRead0 == d.upperWriteVf || d.lowerRead1 == d.upperWriteVf)
            {
                savedUpperTarget = vf[d.upperWriteVf];
                restoreUpperTarget = true;
            }
        }

        // The upper instruction
        u32 upperFlags = 0;
        bool upperFmac = d.upper != Upper::Nop && d.upper != Upper::Unknown;
        switch (d.upper)
        {
        case Upper::Arith:
        {
            Vf* dst = d.toAcc ? &acc : &vf[d.fd];
            Vf result;
            const Vf& fs = vf[d.fs];
            const Vf& ft = vf[d.ft];
            u32 scalar = 0;
            if (d.source == Source::Broadcast)
            {
                scalar = ft.u[d.bc];
            }
            else if (d.source == Source::I)
            {
                scalar = i;
            }
            else if (d.source == Source::Q)
            {
                scalar = q;
            }

            for (u32 c = 0; c < 4; c++)
            {
                u32 shift = 3 - c;
                if (!(d.dest & (1u << c)))
                {
                    pending.mac &= ~(0x1111u << shift);
                    continue;
                }

                f32 a = VuFloat(fs.u[c]);
                f32 b = VuFloat(d.source == Source::Vector ? ft.u[c] : scalar);
                f32 value;
                switch (d.op)
                {
                case Op::Add:
                    value = a + b;
                    break;
                case Op::Sub:
                    value = a - b;
                    break;
                case Op::Mul:
                    value = a * b;
                    break;
                case Op::Madd:
                {
                    f32 product = a * b;
                    value = VuFloat(acc.u[c]) + product;
                    break;
                }
                default:
                {
                    f32 product = a * b;
                    value = VuFloat(acc.u[c]) - product;
                    break;
                }
                }

                result.u[c] = MacUpdate(shift, pending, value);
            }

            StatusUpdate(pending);
            if (d.toAcc || d.fd != 0)
            {
                for (u32 c = 0; c < 4; c++)
                {
                    if (d.dest & (1u << c))
                    {
                        dst->u[c] = result.u[c];
                    }
                }
            }

            break;
        }
        case Upper::Max:
        case Upper::Mini:
        {
            if (d.fd == 0)
            {
                break;
            }

            const Vf& fs = vf[d.fs];
            const Vf& ft = vf[d.ft];
            Vf result = vf[d.fd];
            for (u32 c = 0; c < 4; c++)
            {
                if (!(d.dest & (1u << c)))
                {
                    continue;
                }

                u32 b = d.source == Source::Vector ? ft.u[c] : d.source == Source::Broadcast ? ft.u[d.bc] : i;
                result.u[c] = d.upper == Upper::Max ? FpMax(fs.u[c], b) : FpMin(fs.u[c], b);
            }

            vf[d.fd] = result;
            break;
        }
        case Upper::Itof:
        {
            if (d.ft == 0)
            {
                break;
            }

            Vf result = vf[d.ft];
            for (u32 c = 0; c < 4; c++)
            {
                if (d.dest & (1u << c))
                {
                    f32 value = static_cast<f32>(vf[d.fs].s[c]);
                    if (d.shift != 0)
                    {
                        value *= AsFloat(0x3F800000u - (static_cast<u32>(d.shift) << 23));
                    }

                    result.u[c] = AsBits(value);
                }
            }

            vf[d.ft] = result;
            break;
        }
        case Upper::Ftoi:
        {
            if (d.ft == 0)
            {
                break;
            }

            Vf result = vf[d.ft];
            for (u32 c = 0; c < 4; c++)
            {
                if (d.dest & (1u << c))
                {
                    f32 value = vf[d.fs].f[c];
                    if (d.shift != 0)
                    {
                        value *= AsFloat(0x3F800000u + (static_cast<u32>(d.shift) << 23));
                    }

                    u32 bits = AsBits(value);
                    if ((bits & 0x7F800000) >= 0x4F000000)
                    {
                        result.u[c] = (bits & 0x80000000) ? 0x80000000u : 0x7FFFFFFFu;
                    }
                    else
                    {
                        result.s[c] = static_cast<s32>(value);
                    }
                }
            }

            vf[d.ft] = result;
            break;
        }
        case Upper::Abs:
            if (d.ft != 0)
            {
                Vf result = vf[d.ft];
                for (u32 c = 0; c < 4; c++)
                {
                    if (d.dest & (1u << c))
                    {
                        result.u[c] = vf[d.fs].u[c] & 0x7FFFFFFF;
                    }
                }

                vf[d.ft] = result;
            }

            break;
        case Upper::Clip:
        {
            s32 w = static_cast<s32>(vf[d.ft].u[3]);
            w = (w & 0x7F800000) ? (w & 0x7FFFFFFF) : 0x007FFFFF;
            u32 flags = (pending.clip << 6) & 0xFFFFFF;
            const Vf& fs = vf[d.fs];
            if (static_cast<s32>(fs.u[0]) > w)
            {
                flags |= 0x01;
            }

            if (static_cast<s32>(fs.u[0] ^ 0x80000000u) > w)
            {
                flags |= 0x02;
            }

            if (static_cast<s32>(fs.u[1]) > w)
            {
                flags |= 0x04;
            }

            if (static_cast<s32>(fs.u[1] ^ 0x80000000u) > w)
            {
                flags |= 0x08;
            }

            if (static_cast<s32>(fs.u[2]) > w)
            {
                flags |= 0x10;
            }

            if (static_cast<s32>(fs.u[2] ^ 0x80000000u) > w)
            {
                flags |= 0x20;
            }

            pending.clip = flags;
            upperFlags = FlagClip;
            break;
        }
        case Upper::Opmula:
        {
            const Vf& fs = vf[d.fs];
            const Vf& ft = vf[d.ft];
            acc.u[0] = MacUpdate(3, pending, VuFloat(fs.u[1]) * VuFloat(ft.u[2]));
            acc.u[1] = MacUpdate(2, pending, VuFloat(fs.u[2]) * VuFloat(ft.u[0]));
            acc.u[2] = MacUpdate(1, pending, VuFloat(fs.u[0]) * VuFloat(ft.u[1]));
            StatusUpdate(pending);
            break;
        }
        case Upper::Opmsub:
        {
            const Vf& fs = vf[d.fs];
            const Vf& ft = vf[d.ft];
            Vf result;
            f32 p0 = VuFloat(fs.u[1]) * VuFloat(ft.u[2]);
            f32 p1 = VuFloat(fs.u[2]) * VuFloat(ft.u[0]);
            f32 p2 = VuFloat(fs.u[0]) * VuFloat(ft.u[1]);
            result.u[0] = MacUpdate(3, pending, VuFloat(acc.u[0]) - p0);
            result.u[1] = MacUpdate(2, pending, VuFloat(acc.u[1]) - p1);
            result.u[2] = MacUpdate(1, pending, VuFloat(acc.u[2]) - p2);
            StatusUpdate(pending);
            if (d.fd != 0)
            {
                vf[d.fd].u[0] = result.u[0];
                vf[d.fd].u[1] = result.u[1];
                vf[d.fd].u[2] = result.u[2];
            }

            break;
        }
        default:
            break;
        }

        vf[0].u[0] = 0;
        vf[0].u[1] = 0;
        vf[0].u[2] = 0;
        vf[0].u[3] = 0x3F800000;

        bool lowerFmac = false;
        u32 lowerFlags = 0;
        if (d.iBit)
        {
            i = d.lowerWord;
        }
        else if (!discardLower)
        {
            Vf upperResult = {};
            if (restoreUpperTarget)
            {
                upperResult = vf[d.upperWriteVf];
                vf[d.upperWriteVf] = savedUpperTarget;
            }

            const u32 lw = d.lowerWord;
            const u32 is = d.lfs & 0xF;
            const u32 it = d.lft & 0xF;
            const u32 id = d.lfd & 0xF;
            lowerFmac = d.lowerPipe == Pipe::Fmac;
            auto memory = [&](u32 address) -> u32* {
                return reinterpret_cast<u32*>(data_ + ((address * 16) & dataMask));
            };
            auto load = [&](u32 reg, const u32* from) {
                if (reg == 0)
                {
                    return;
                }

                for (u32 c = 0; c < 4; c++)
                {
                    if (d.lowerDest & (1u << c))
                    {
                        vf[reg].u[c] = from[c];
                    }
                }
            };
            auto store = [&](const Vf& value, u32* to) {
                if (g_KickWrites.on && g_KickWrites.pending && index_ == 1)
                {
                    const u32 at = static_cast<u32>((reinterpret_cast<u8*>(to) - data_) / 16);
                    if (at >= g_KickWrites.start && at < g_KickWrites.end && g_KickWrites.told.insert({g_KickWrites.pc, thisPc}).second)
                    {
                        std::fprintf(stderr, "vu1: the XGKICK at %#x's packet written at %#x, quadword %u of %u, %llu cycles after it\n",
                                     g_KickWrites.pc, thisPc, at - g_KickWrites.start, g_KickWrites.end - g_KickWrites.start,
                                     static_cast<unsigned long long>(cycle - g_KickWrites.cycle));
                    }
                }

                for (u32 c = 0; c < 4; c++)
                {
                    if (d.lowerDest & (1u << c))
                    {
                        to[c] = value.u[c];
                    }
                }
            };
            auto broadcast = [&](u32 reg, u32 value) {
                if (reg == 0)
                {
                    return;
                }

                for (u32 c = 0; c < 4; c++)
                {
                    if (d.lowerDest & (1u << c))
                    {
                        vf[reg].u[c] = value;
                    }
                }
            };
            auto advanceR = [&]() {
                u32 x = (r >> 4) & 1;
                u32 y = (r >> 22) & 1;
                r <<= 1;
                r ^= x ^ y;
                r = (r & 0x7FFFFF) | 0x3F800000;
            };
            u16 imm15 = static_cast<u16>(((lw >> 10) & 0x7800) | (lw & 0x7FF));
            u16 imm12 = static_cast<u16>(((lw >> 21) & 1) << 11 | (lw & 0x7FF));
            switch (d.lower)
            {
            case Lower::Nop:
            case Lower::Unknown:
            case Lower::Waitq:
            case Lower::Waitp:
                break;
            case Lower::Lq:
                load(d.lft, memory(static_cast<u32>(static_cast<s32>(static_cast<s16>(vi[is])) + Imm11(lw))));
                break;
            case Lower::Sq:
                store(vf[d.lfs], memory(static_cast<u32>(static_cast<s32>(static_cast<s16>(vi[it])) + Imm11(lw))));
                break;
            case Lower::Lqi:
                load(d.lft, memory(vi[is]));
                if (is != 0)
                {
                    writeVi(is, static_cast<u16>(vi[is] + 1));
                }

                break;
            case Lower::Sqi:
                store(vf[d.lfs], memory(vi[it]));
                if (it != 0)
                {
                    writeVi(it, static_cast<u16>(vi[it] + 1));
                }

                break;
            case Lower::Lqd:
                if (is != 0)
                {
                    writeVi(is, static_cast<u16>(vi[is] - 1));
                }

                load(d.lft, memory(vi[is]));
                break;
            case Lower::Sqd:
                if (it != 0)
                {
                    writeVi(it, static_cast<u16>(vi[it] - 1));
                }

                store(vf[d.lfs], memory(vi[it]));
                break;
            case Lower::Ilw:
            case Lower::Ilwr:
            {
                u32 address = d.lower == Lower::Ilw
                                  ? static_cast<u32>(static_cast<s32>(static_cast<s16>(vi[is])) + Imm11(lw))
                                  : vi[is];
                const u32* at = memory(address);
                u16 value = vi[it];
                for (u32 c = 0; c < 4; c++)
                {
                    if (d.lowerDest & (1u << c))
                    {
                        value = static_cast<u16>(at[c]);
                    }
                }

                if (it != 0)
                {
                    writeVi(it, value);
                    pipes.ialuReg[pipes.ialuNext] = static_cast<u8>(it);
                    pipes.ialuReady[pipes.ialuNext] = cycle + 4;
                    pipes.ialuNext = (pipes.ialuNext + 1) & 3;
                }

                break;
            }
            case Lower::Isw:
            case Lower::Iswr:
            {
                u32 address = d.lower == Lower::Isw
                                  ? static_cast<u32>(static_cast<s32>(static_cast<s16>(vi[is])) + Imm11(lw))
                                  : vi[is];
                u32* at = memory(address);
                for (u32 c = 0; c < 4; c++)
                {
                    if (d.lowerDest & (1u << c))
                    {
                        at[c] = vi[it];
                    }
                }

                break;
            }
            case Lower::Iaddiu:
                writeVi(it, static_cast<u16>(vi[is] + imm15), true);
                break;
            case Lower::Isubiu:
                writeVi(it, static_cast<u16>(vi[is] - imm15), true);
                break;
            case Lower::Iaddi:
            {
                s16 imm = static_cast<s16>((lw >> 6) & 0x1F);
                if (imm & 0x10)
                {
                    imm = static_cast<s16>(imm | 0xFFE0);
                }

                writeVi(it, static_cast<u16>(vi[is] + imm), true);
                break;
            }
            case Lower::Iadd:
                writeVi(id, static_cast<u16>(vi[is] + vi[it]), true);
                break;
            case Lower::Isub:
                writeVi(id, static_cast<u16>(vi[is] - vi[it]), true);
                break;
            case Lower::Iand:
                writeVi(id, static_cast<u16>(vi[is] & vi[it]), true);
                break;
            case Lower::Ior:
                writeVi(id, static_cast<u16>(vi[is] | vi[it]), true);
                break;
            case Lower::Fceq:
                writeVi(1, (clipFlag & 0xFFFFFF) == (lw & 0xFFFFFF) ? 1 : 0);
                break;
            case Lower::Fcand:
                writeVi(1, ((clipFlag & 0xFFFFFF) & (lw & 0xFFFFFF)) != 0 ? 1 : 0);
                break;
            case Lower::Fcor:
                writeVi(1, ((clipFlag & 0xFFFFFF) | (lw & 0xFFFFFF)) == 0xFFFFFF ? 1 : 0);
                break;
            case Lower::Fcset:
                pending.clip = lw & 0xFFFFFF;
                lowerFlags = FlagClip;
                break;
            case Lower::Fcget:
                writeVi(it, static_cast<u16>(clipFlag & 0xFFF));
                break;
            case Lower::Fseq:
                writeVi(it, (statusFlag & 0xFFF) == imm12 ? 1 : 0);
                break;
            case Lower::Fsand:
                writeVi(it, static_cast<u16>((statusFlag & 0xFFF) & imm12));
                break;
            case Lower::Fsor:
                writeVi(it, static_cast<u16>((statusFlag & 0xFFF) | imm12));
                break;
            case Lower::Fsset:
                pending.status = (imm12 & 0xFC0) | (pending.status & 0x3F);
                lowerFlags = FlagStatus;
                break;
            case Lower::Fmeq:
                writeVi(it, (macFlag & 0xFFFF) == vi[is] ? 1 : 0);
                break;
            case Lower::Fmand:
                writeVi(it, static_cast<u16>(vi[is] & (macFlag & 0xFFFF)));
                break;
            case Lower::Fmor:
                writeVi(it, static_cast<u16>(vi[is] | (macFlag & 0xFFFF)));
                break;
            case Lower::B:
                setBranch(thisPc + 1 + static_cast<u32>(Imm11(lw)));
                break;
            case Lower::Bal:
                if (it != 0)
                {
                    writeVi(it, static_cast<u16>(branch == 1 ? branchTarget + 1 : thisPc + 2));
                }

                setBranch(thisPc + 1 + static_cast<u32>(Imm11(lw)));
                break;
            case Lower::Jr:
                setBranch(vi[is]);
                break;
            case Lower::Jalr:
            {
                u32 target = vi[is];
                if (it != 0)
                {
                    writeVi(it, static_cast<u16>(branch == 1 ? branchTarget + 1 : thisPc + 2));
                }

                setBranch(target);
                break;
            }
            case Lower::Ibeq:
                if (viRead(it) == viRead(is))
                {
                    setBranch(thisPc + 1 + static_cast<u32>(Imm11(lw)));
                }

                break;
            case Lower::Ibne:
                if (viRead(it) != viRead(is))
                {
                    setBranch(thisPc + 1 + static_cast<u32>(Imm11(lw)));
                }

                break;
            case Lower::Ibltz:
                if (viRead(is) < 0)
                {
                    setBranch(thisPc + 1 + static_cast<u32>(Imm11(lw)));
                }

                break;
            case Lower::Ibgtz:
                if (viRead(is) > 0)
                {
                    setBranch(thisPc + 1 + static_cast<u32>(Imm11(lw)));
                }

                break;
            case Lower::Iblez:
                if (viRead(is) <= 0)
                {
                    setBranch(thisPc + 1 + static_cast<u32>(Imm11(lw)));
                }

                break;
            case Lower::Ibgez:
                if (viRead(is) >= 0)
                {
                    setBranch(thisPc + 1 + static_cast<u32>(Imm11(lw)));
                }

                break;
            case Lower::Move:
                if (d.lft != 0)
                {
                    for (u32 c = 0; c < 4; c++)
                    {
                        if (d.lowerDest & (1u << c))
                        {
                            vf[d.lft].u[c] = vf[d.lfs].u[c];
                        }
                    }
                }

                break;
            case Lower::Mr32:
                if (d.lft != 0)
                {
                    Vf source = vf[d.lfs];
                    u32 rotated[4] = {source.u[1], source.u[2], source.u[3], source.u[0]};
                    for (u32 c = 0; c < 4; c++)
                    {
                        if (d.lowerDest & (1u << c))
                        {
                            vf[d.lft].u[c] = rotated[c];
                        }
                    }
                }

                break;
            case Lower::Mtir:
                writeVi(it, static_cast<u16>(vf[d.lfs].u[d.fsf]));
                break;
            case Lower::Mfir:
                broadcast(d.lft, static_cast<u32>(static_cast<s32>(static_cast<s16>(vi[is]))));
                break;
            case Lower::Rinit:
                r = 0x3F800000 | (vf[d.lfs].u[d.fsf] & 0x007FFFFF);
                break;
            case Lower::Rxor:
                r = 0x3F800000 | ((r ^ vf[d.lfs].u[d.fsf]) & 0x007FFFFF);
                break;
            case Lower::Rget:
                broadcast(d.lft, r);
                break;
            case Lower::Rnext:
                if (d.lft != 0)
                {
                    advanceR();
                    broadcast(d.lft, r);
                }

                break;
            case Lower::Mfp:
                broadcast(d.lft, p);
                break;
            case Lower::Xtop:
                writeVi(it, static_cast<u16>(top));
                break;
            case Lower::Xitop:
                writeVi(it, static_cast<u16>(itop));
                break;
            case Lower::Xgkick:
                kickPc = thisPc;
                if (g_KickWrites.on && index_ == 1)
                {
                    const u32 address = vi[is] & (dataMask >> 4);
                    g_KickWrites.pending = true;
                    g_KickWrites.start = address;
                    g_KickWrites.end = address + PacketLength(data_, dataMask, address);
                    g_KickWrites.cycle = cycle;
                    g_KickWrites.pc = thisPc;
                }

                Kick(vi[is] & (dataMask >> 4));

                break;
            case Lower::Div:
            case Lower::Sqrt:
            case Lower::Rsqrt:
            {
                f32 ft = VuFloat(vf[d.lft].u[d.ftf]);
                f32 fs = VuFloat(vf[d.lfs].u[d.fsf]);
                u32 status = pending.status & ~0x30u;
                u32 result;
                bool signs = ((vf[d.lft].u[d.ftf] ^ vf[d.lfs].u[d.fsf]) & 0x80000000) != 0;
                if (d.lower == Lower::Div)
                {
                    if (ft == 0.0f)
                    {
                        status |= fs == 0.0f ? 0x10 : 0x20;
                        result = signs ? 0xFF7FFFFFu : 0x7F7FFFFFu;
                    }
                    else
                    {
                        result = AsBits(VuFloat(AsBits(fs / ft)));
                    }
                }
                else if (d.lower == Lower::Sqrt)
                {
                    if (ft < 0.0f)
                    {
                        status |= 0x10;
                    }

                    result = AsBits(VuFloat(AsBits(std::sqrt(std::fabs(ft)))));
                }
                else
                {
                    if (ft == 0.0f)
                    {
                        status |= 0x20;
                        if (fs != 0.0f)
                        {
                            result = signs ? 0xFF7FFFFFu : 0x7F7FFFFFu;
                        }
                        else
                        {
                            result = signs ? 0x80000000u : 0;
                            status |= 0x10;
                        }
                    }
                    else
                    {
                        if (ft < 0.0f)
                        {
                            status |= 0x10;
                        }

                        f32 root = std::sqrt(std::fabs(ft));
                        result = AsBits(VuFloat(AsBits(fs / root)));
                    }
                }

                pipes.fdiv = true;
                pipes.fdivStart = cycle;
                pipes.fdivLatency = d.latency;
                pipes.fdivValue = result;
                pipes.fdivStatus = status;
                break;
            }
            case Lower::Esadd:
            case Lower::Ersadd:
            case Lower::Eleng:
            case Lower::Erleng:
            {
                const Vf& fs = vf[d.lfs];
                f32 sum = VuFloat(fs.u[0]) * VuFloat(fs.u[0]) + VuFloat(fs.u[1]) * VuFloat(fs.u[1]) +
                          VuFloat(fs.u[2]) * VuFloat(fs.u[2]);
                if (d.lower == Lower::Ersadd)
                {
                    if (sum != 0.0f)
                    {
                        sum = 1.0f / sum;
                    }
                }
                else if (d.lower == Lower::Eleng)
                {
                    if (sum >= 0.0f)
                    {
                        sum = std::sqrt(sum);
                    }
                }
                else if (d.lower == Lower::Erleng)
                {
                    if (sum >= 0.0f)
                    {
                        sum = std::sqrt(sum);
                        if (sum != 0.0f)
                        {
                            sum = 1.0f / sum;
                        }
                    }
                }

                pending.p = AsBits(sum);
                break;
            }
            case Lower::Eatanxy:
            case Lower::Eatanxz:
            {
                const Vf& fs = vf[d.lfs];
                f32 x = VuFloat(fs.u[0]);
                f32 value = 0.0f;
                if (x != 0.0f)
                {
                    value = Eatan(VuFloat(fs.u[d.lower == Lower::Eatanxy ? 1 : 2]) / x);
                }

                pending.p = AsBits(value);
                break;
            }
            case Lower::Esum:
            {
                const Vf& fs = vf[d.lfs];
                pending.p = AsBits(VuFloat(fs.u[0]) + VuFloat(fs.u[1]) + VuFloat(fs.u[2]) + VuFloat(fs.u[3]));
                break;
            }
            case Lower::Ercpr:
            {
                f32 value = VuFloat(vf[d.lfs].u[d.fsf]);
                if (value != 0.0f)
                {
                    value = static_cast<f32>(1.0 / value);
                }

                pending.p = AsBits(value);
                break;
            }
            case Lower::Esqrt:
            case Lower::Ersqrt:
            {
                f32 value = VuFloat(vf[d.lfs].u[d.fsf]);
                if (value >= 0.0f)
                {
                    value = std::sqrt(value);
                    if (d.lower == Lower::Ersqrt && value != 0.0f)
                    {
                        value = 1.0f / value;
                    }
                }

                pending.p = AsBits(value);
                break;
            }
            case Lower::Esin:
            {
                static const f32 Constants[5] = {1.0f, -0.166666567325592f, 0.008333025500178f, -0.000198074136279f,
                                                 0.000002601886990f};
                f32 x = VuFloat(vf[d.lfs].u[d.fsf]);
                f32 value = static_cast<f32>((Constants[0] * x) + (Constants[1] * std::pow(x, 3)) +
                                             (Constants[2] * std::pow(x, 5)) + (Constants[3] * std::pow(x, 7)) +
                                             (Constants[4] * std::pow(x, 9)));
                pending.p = AsBits(VuFloat(AsBits(value)));
                break;
            }
            case Lower::Eatan:
                pending.p = AsBits(Eatan(VuFloat(vf[d.lfs].u[d.fsf])));
                break;
            case Lower::Eexp:
            {
                static const f32 Constants[6] = {0.249998688697815f, 0.031257584691048f, 0.002591371303424f,
                                                 0.000171562001924f, 0.000005430199963f, 0.000000690600018f};
                f32 x = VuFloat(vf[d.lfs].u[d.fsf]);
                f32 value = static_cast<f32>(1.0f + (Constants[0] * x) + (Constants[1] * std::pow(x, 2)) +
                                             (Constants[2] * std::pow(x, 3)) + (Constants[3] * std::pow(x, 4)) +
                                             (Constants[4] * std::pow(x, 5)) + (Constants[5] * std::pow(x, 6)));
                value = static_cast<f32>(std::pow(value, 4));
                value = VuFloat(AsBits(value));
                value = 1.0f / value;
                pending.p = AsBits(value);
                break;
            }
            }

            if (d.lowerPipe == Pipe::Efu && d.lower != Lower::Waitp)
            {
                pipes.efu = true;
                pipes.efuStart = cycle;
                pipes.efuLatency = d.latency;
                pipes.efuValue = pending.p;
            }

            if (restoreUpperTarget)
            {
                // The lower may have written the same register's other components (MOVE of another register): keep its
                // writes to them and the upper's result in its own
                Vf lowerView = vf[d.upperWriteVf];
                for (u32 c = 0; c < 4; c++)
                {
                    lowerView.u[c] = (d.upperWriteMask & (1u << c)) ? upperResult.u[c] : lowerView.u[c];
                }

                vf[d.upperWriteVf] = lowerView;
            }

            vf[0].u[0] = 0;
            vf[0].u[1] = 0;
            vf[0].u[2] = 0;
            vf[0].u[3] = 0x3F800000;
            vi[0] = 0;
        }

        // The FMAC pipeline's entry for this cycle
        if (upperFmac || lowerFmac)
        {
            u32 at = (pipes.fmacRead + pipes.fmacCount) & 3;
            if (pipes.fmacCount == 4)
            {
                // A full pipeline can't happen with 4 cycle latencies and a cycle per instruction: drop the oldest
                pipes.fmacRead = (pipes.fmacRead + 1) & 3;
                pipes.fmacCount--;
                at = (pipes.fmacRead + pipes.fmacCount) & 3;
            }

            FmacEntry& e = pipes.fmac[at];
            e.start = cycle;
            e.upperReg = upperFmac ? d.upperWriteVf : 0;
            e.upperMask = upperFmac ? d.upperWriteMask : 0;
            e.lowerReg = lowerFmac ? d.lowerWriteVf : 0;
            e.lowerMask = lowerFmac ? d.lowerWriteMask : 0;
            e.flags = static_cast<u8>(upperFlags | lowerFlags);
            e.mac = pending.mac;
            e.status = pending.status;
            e.clip = pending.clip;
            pipes.fmacCount++;
            state.anyEntry = true;
        }

        // Branches go after their delay slot; the E bit ends the program after its own
        if (branch > 0)
        {
            if (branch-- == 1)
            {
                pc = branchTarget;
                if (delayBranch)
                {
                    branch = 1;
                    branchTarget = delayTarget;
                    delayBranch = false;
                }
            }
        }

        cycle++;
        if (ebit > 0)
        {
            if (ebit-- == 1)
            {
                return false;
            }
        }
    }

    return true;
}

void Vu::LoadVf(u32 reg, const void* from)
{
    if (reg != 0)
    {
        std::memcpy(vf[reg].u, from, 16);
    }
}

void Vu::StoreVf(u32 reg, void* to) const
{
    std::memcpy(to, vf[reg].u, 16);
}

void Vu::MacroMove(u32 ft, u32 fs, u32 mask)
{
    if (ft == 0)
    {
        return;
    }

    Vf source = vf[fs];
    for (u32 c = 0; c < 4; c++)
    {
        if (mask & (1u << c))
        {
            vf[ft].u[c] = source.u[c];
        }
    }
}

namespace
{
// A macro mode FMAC result: the VU's operand and result rules, rounding toward zero
using MacroRounding = RoundTowardZero;

u32 MacroResult(f32 value)
{
    Pending ignored;
    return MacroResultFlags(ignored, value);
}
}

void Vu::MacroMulBc(u32 fd, u32 fs, u32 ft, u32 bc, bool toAcc, u32 mask)
{
    MacroRounding rounding;
    f32 b = VuFloat(vf[ft].u[bc]);
    Vf result = toAcc ? acc : vf[fd];
    for (u32 c = 0; c < 4; c++)
    {
        if (mask & (1u << c))
        {
            result.u[c] = MacroResult(VuFloat(vf[fs].u[c]) * b);
        }
    }

    if (toAcc)
    {
        acc = result;
    }
    else if (fd != 0)
    {
        vf[fd] = result;
    }
}

void Vu::MacroMaddBc(u32 fd, u32 fs, u32 ft, u32 bc, bool toAcc, u32 mask)
{
    MacroRounding rounding;
    f32 b = VuFloat(vf[ft].u[bc]);
    Vf result = toAcc ? acc : vf[fd];
    for (u32 c = 0; c < 4; c++)
    {
        if (mask & (1u << c))
        {
            f32 product = VuFloat(vf[fs].u[c]) * b;
            result.u[c] = MacroResult(VuFloat(acc.u[c]) + product);
        }
    }

    if (toAcc)
    {
        acc = result;
    }
    else if (fd != 0)
    {
        vf[fd] = result;
    }
}

void Vu::MacroMul(u32 fd, u32 fs, u32 ft, u32 mask)
{
    MacroRounding rounding;
    Vf result = vf[fd];
    for (u32 c = 0; c < 4; c++)
    {
        if (mask & (1u << c))
        {
            result.u[c] = MacroResult(VuFloat(vf[fs].u[c]) * VuFloat(vf[ft].u[c]));
        }
    }

    if (fd != 0)
    {
        vf[fd] = result;
    }
}

void Vu::MacroClipW(u32 fs, u32 ft)
{
    s32 w = static_cast<s32>(vf[ft].u[3]);
    w = (w & 0x7F800000) ? (w & 0x7FFFFFFF) : 0x007FFFFF;
    u32 flags = (clipFlag << 6) & 0xFFFFFF;
    const Vf& v = vf[fs];
    for (u32 c = 0; c < 3; c++)
    {
        if (static_cast<s32>(v.u[c]) > w)
        {
            flags |= 1u << (c * 2);
        }

        if (static_cast<s32>(v.u[c] ^ 0x80000000u) > w)
        {
            flags |= 2u << (c * 2);
        }
    }

    clipFlag = flags;
}

std::vector<u32> Vu::UnknownInstructions(u32 start, u32 count)
{
    std::vector<u32> unknown;
    for (u32 n = 0; n < count; n++)
    {
        u32 address = (start + n) & (codeSize_ - 1);
        if (decoded_[address] == nullptr)
        {
            Decode(address);
        }

        const Decoded& d = *decoded_[address];
        if (d.upper == Upper::Unknown || d.lower == Lower::Unknown)
        {
            unknown.push_back(address);
        }
    }

    return unknown;
}
}
