// The fast renderer's prepared function for the scenery (THE LIST 75; native/GRAPHICS.md, "The fast renderer"): the type 01
// shader's VU1 program (0x2DF700 in the retail executable's .vutext), which draws the placed objects and the chunk's meshes with
// the source shader family's materials. The renderer's program 1 calls its first routine for each type 01 entry VIF1 unpacked;
// the routine goes through the instances' headers (2(vi00): a header's matrix, and whether its mesh is clipped), and for each
// one transforms the strip VIF1 unpacked at TOP (a GIF tag, the vertex count, then four quadwords a vertex: the position, the
// texture coordinates whose low bytes are also the colour, and two the lit shaders use), divides by w, adds the shader's
// scroll to the coordinates, writes the GIF packet (ST and Q, RGBA, XYZ a vertex) into one of the output buffers 1(vi00)
// rotates, and kicks the shader's registers (when it has them) and the packet.
//
// This is that routine's work for the instances wholly in view (a header's first word 0: the routine's second loop, at 0x4B),
// as native arithmetic giving the program's values bit for bit: the VU's float rules (vu1jit.h: operands clamped, results with
// their denormals zeroed, rounded toward zero as the run sets it, never fused), each value made in the program's order of
// operations, the registers, flags, Q, data memory and the kick left pending as the program leaves them at its return to
// program 1. The clipped instances (a header's first word not 0: the routine calls the clipping routine 4(vi00) names) are left
// to the program: the function declines the whole run when any header is one.
//
// The program's loop is software-pipelined: a vertex's position is transformed while the vertex before is divided and the one
// before that stored. What the pipelining leaves in the registers at the end is the last vertexes' values (written out below);
// the order of the stores doesn't matter (the input, the headers and the outputs are checked apart first).

#include "fastrenderer.h"

#include "ee/vu.h"
#include "ee/vu1jit.h"
#include "gs/gs.h"

#include <cstdlib>
#include <cstring>

#pragma clang fp contract(off)

namespace NativeGraphics
{
namespace
{
using namespace Ee::Vu1Jit;

// VU1's data memory in quadwords (16 KB): its addresses wrap there
constexpr u32 Quadwords = 0x400;
constexpr u32 QuadwordMask = Quadwords - 1;
constexpr u32 One = 0x3F800000u;

u8* At(u8* memory, u32 quadword)
{
    return memory + (quadword & QuadwordMask) * 16;
}

// ILW: a word's low 16 bits
u16 Word(u8* memory, u32 quadword, u32 lane)
{
    u32 value;
    std::memcpy(&value, At(memory, quadword) + lane * 4, 4);
    return static_cast<u16>(value);
}

// Whether two ranges of quadwords (wrapping) share any
bool Overlap(u32 a, u32 aSize, u32 b, u32 bSize)
{
    return ((b - a) & QuadwordMask) < aSize || ((a - b) & QuadwordMask) < bSize;
}

// The routine's instructions: where its kicks are (for the traces, as the program's XGKICKs) and how many it runs
constexpr u32 RegistersKickOffset = 0x7F;
constexpr u32 PacketKickOffset = 0x81;

bool KicksAtOnce()
{
    static const bool atOnce = std::getenv("TWIN_VU1_KICK_AT_ONCE") != nullptr && std::getenv("TWIN_VU1_KICK_AT_ONCE")[0] != '0';
    return atOnce;
}

// The registers the last vertexes leave (the pipelined loop's): see Type01 below
struct LastVertexes
{
    V4u position;
    V4u colour;
    V4u divided;
    V4u coordinates;
    V4u screen;
    V4u accumulator;
    V4u wholeTransformed;
    u32 q;
    u32 divideFlags;
    V4u macSource;
    V4u thirdLastScreen;
    V4u thirdLastColour;
};

// One instance's strip: the packet written at out (count vertexes from top), the sticky flags of every FMAC result added; with
// strip set, also the vertexes as the GIF would take them from the packet (Gs::StripVertex: XYZF2's when fogged)
void TransformStrip(u8* mem, u16 top, u16 out, u32 count, V4u m0, V4u m1, V4u m2, V4u m3, V4u scroll, Sticky& sticky,
                    LastVertexes& last, u8* strip, bool fogged)
{
    const V4u one = Splat(One);
    Store(At(mem, out), Load(At(mem, top)));
    // MULAw ACC, vf04, vf00w: the matrix's fourth row times 1, the same each vertex
    AccumulateResults(sticky, m3);
    for (u32 k = 0; k < count; k++)
    {
        const u32 vertex = top + 3u + 4u * k;
        const V4u positionRaw = Load(At(mem, vertex));
        const V4u colourRaw = Load(At(mem, vertex + 1));
        const V4u position = Clamp(positionRaw);
        // MADDAy, MADDAz, MADDx: ((row 3 + row 1 * y) + row 2 * z) + row 0 * x
        const V4u second = AsU(AsF(m3) + AsF(m1) * AsF(SplatLane<1>(position)));
        const V4u secondResult = Result(second);
        const V4u third = AsU(AsF(secondResult) + AsF(m2) * AsF(SplatLane<2>(position)));
        const V4u thirdResult = Result(third);
        const V4u transformed = AsU(AsF(thirdResult) + AsF(m0) * AsF(SplatLane<0>(position)));
        const V4u transformedResult = Result(transformed);
        // DIV Q, vf00w, w
        u32 divideFlags;
        const u32 q = Divide(One, transformedResult[3], divideFlags);
        const V4u qv = Splat(q);
        // ADD.xyz: the coordinates and the scroll; MULq.xyz both ways
        const V4u coordinates = AsU(AsF(Clamp(colourRaw)) + AsF(scroll));
        const V4u coordinatesResult = Result(coordinates);
        const V4u textured = AsU(AsF(coordinatesResult) * AsF(qv));
        const V4u texturedResult = Result(textured);
        const V4u divided = AsU(AsF(transformedResult) * AsF(qv));
        const V4u dividedResult = Result(divided);
        // FTOI0.xyz
        const V4u screen = Ftoi<0>(dividedResult);
        AccumulateResults(sticky, second);
        AccumulateResults(sticky, third);
        AccumulateResults(sticky, transformed);
        const V4u coordinatesSource = Blend<7>(one, coordinates);
        const V4u dividedSource = Blend<7>(one, divided);
        const V4u texturedSource = Blend<7>(one, textured);
        AccumulateResults(sticky, coordinatesSource);
        AccumulateResults(sticky, dividedSource);
        AccumulateResults(sticky, texturedSource);
        // The vertex's packet: ST and Q (its w left), RGBA (the coordinates as they came), XYZ with the coordinates' w
        u8* stq = At(mem, out + 1u + 3u * k);
        Store(stq, Blend<7>(Load(stq), texturedResult));
        Store(At(mem, out + 2u + 3u * k), colourRaw);
        const V4u xyz = Blend<7>(colourRaw, screen);
        Store(At(mem, out + 3u + 3u * k), xyz);
        if (strip != nullptr)
        {
            // The GIF's reading of the vertex's PACKED ST, RGBAQ and XYZ2 or XYZF2 (Gif::ConvertKick's)
            Gs::StripVertex made;
            made.x = xyz[0] & 0xFFFF;
            made.y = xyz[1] & 0xFFFF;
            made.z = fogged ? (xyz[2] >> 4) & 0xFFFFFF : xyz[2];
            made.r = static_cast<u8>(colourRaw[0]);
            made.g = static_cast<u8>(colourRaw[1]);
            made.b = static_cast<u8>(colourRaw[2]);
            made.a = static_cast<u8>(colourRaw[3]);
            made.s = texturedResult[0];
            made.t = texturedResult[1];
            made.q = texturedResult[2];
            made.fog = fogged ? static_cast<u8>(xyz[3] >> 4) : 0;
            made.kick = (xyz[3] >> 15) & 1 ? 0 : 1;
            made.unused[0] = 0;
            made.unused[1] = 0;
            std::memcpy(strip + static_cast<size_t>(k) * sizeof(made), &made, sizeof(made));
        }

        if (k + 3 == count)
        {
            last.thirdLastScreen = xyz;
        }

        if (k + 1 == count)
        {
            last.position = positionRaw;
            last.colour = colourRaw;
            last.divided = Blend<7>(transformedResult, dividedResult);
            last.coordinates = texturedResult;
            last.screen = xyz;
            last.accumulator = thirdResult;
            last.wholeTransformed = transformedResult;
            last.q = q;
            last.divideFlags = divideFlags;
            last.macSource = texturedSource;
        }
    }
}

// The type 01 program's first routine, for runs whose instances are all wholly in view
bool Type01(PreparedRun& run)
{
    Ee::Vu& vu = run.vu;
    Ee::VuMicroState& st = run.st;
    u8* const mem = vu.data();
    if (KicksAtOnce() || st.pipes.efu || vu.dataSize() != Quadwords * 16)
    {
        return false;
    }

    // The headers (ILW.x vi13, 2(vi00); ILW.y vi10, 0(vi13); each next one ILW.w's quadwords on), all wholly in view
    const u16 first = Word(mem, 2, 0);
    const u16 end = Word(mem, first, 1);
    constexpr u32 MaxHeaders = 64;
    u16 headers[MaxHeaders];
    u32 headerCount = 0;
    u16 header = first;
    do
    {
        if (headerCount == MaxHeaders || Word(mem, header, 0) != 0)
        {
            return false;
        }

        headers[headerCount++] = header;
        header = static_cast<u16>(header + Word(mem, header, 3));
    } while (header != end);

    // The strip at TOP: its vertex count from 1(TOP).x (four quadwords a vertex: the loop ends on an equal address, after the
    // first two vertexes)
    const u16 top = static_cast<u16>(vu.top);
    const u16 vi14 = vu.vi[14];
    const u32 inputQuadwords = Word(mem, top + 1u, 0);
    const u32 count = inputQuadwords / 4;
    if (inputQuadwords % 4 != 0 || count < 3)
    {
        return false;
    }

    // The output buffers (1(vi00)'s words, one a header in turn) apart from what the routine reads after writing one
    const u32 inputSize = 3 + inputQuadwords;
    const u32 outputSize = 1 + 3 * count;
    if (inputSize + outputSize > Quadwords)
    {
        return false;
    }

    for (u32 n = 0; n < headerCount && n < 4; n++)
    {
        const u16 out = Word(mem, 1, n);
        if (Overlap(out, outputSize, top, inputSize) || Overlap(out, outputSize, 0, 3) || Overlap(out, outputSize, vi14, 3))
        {
            return false;
        }

        for (u32 h = 0; h < headerCount; h++)
        {
            if (Overlap(out, outputSize, headers[h], 5))
            {
                return false;
            }
        }
    }

    // The strips' prepared form (Gs::PreparedStrip) when their GIF tag (the strip's at TOP, copied into each packet) is the one it
    // stands for: NLOOP the vertexes, EOP, PRE, PACKED, three registers ST, RGBAQ and XYZ2 or XYZF2; and when no packet is written
    // over before the GIF reads it (at the next kick: the next instance's packet is written first), the output buffers one after
    // another apart, the first apart from the pending kick's prepared packet
    u64 tag[2];
    std::memcpy(tag, At(mem, top), 16);
    const u32 registerList = tag[1] & 0xFFF;
    const bool fogged = registerList == 0x412;
    bool strips = (tag[0] & 0x7FFF) == count && ((tag[0] >> 15) & 1) != 0 && ((tag[0] >> 46) & 1) != 0 && ((tag[0] >> 58) & 3) == 0 &&
                  ((tag[0] >> 60) & 0xF) == 3 && (registerList == 0x512 || fogged);
    for (u32 n = 0; strips && n + 1 < headerCount && n < 4; n++)
    {
        strips = !Overlap(Word(mem, 1, n), outputSize, Word(mem, 1, (n + 1) & 3), outputSize);
    }

    if (vu.kickPending && !vu.kickPendingBatch.empty())
    {
        Gs::PreparedWrite pending;
        std::memcpy(&pending, vu.kickPendingBatch.data(), sizeof(pending));
        if (Overlap(vu.kickPendingAddress, 1 + 3 * pending.unused, Word(mem, 1, 0), outputSize))
        {
            // (Read from memory when sent, as the GIF would, after this run's first packet is written)
            vu.kickPendingBatch.clear();
        }
    }

    // The run goes ahead: what's in flight lands first (it would before any of the routine's own results)
    st.cycle += 16;
    vu.FlushPipes(st);
    const u32 pendingStatus = st.pending.status;
    const u32 codeMask = vu.codeSize() - 1;
    auto kick = [&](u32 address, u32 offset) {
        const u32 pc = (run.base + offset) & codeMask;
        if (vu.kickPending)
        {
            // The pending kick sent: its prepared form, or its packet read from memory
            vu.kickPending = false;
            if (!vu.kickPendingBatch.empty())
            {
                run.batch.insert(run.batch.end(), vu.kickPendingBatch.begin(), vu.kickPendingBatch.end());
                vu.kickPendingBatch.clear();
            }
            else
            {
                PreparedSend(run, vu.kickPendingAddress, vu.kickPendingPc);
            }
        }

        vu.kickPending = true;
        vu.kickPendingAddress = address & QuadwordMask;
        vu.kickPendingPc = pc;
        vu.kickPc = pc;
    };

    Sticky sticky;
    LastVertexes last;
    const V4u scrollRaw = Load(At(mem, vi14 + 2u));
    const V4u scroll = Clamp(scrollRaw);
    const bool registers = Word(mem, vi14, 3) != 0;
    thread_local std::vector<u8> strip;
    const u64 stripPrim = ((tag[0] >> 47) & 0x7FF) | (fogged ? 1ull << 32 : 0);
    V4u rows[4];
    V4u rotated = {};
    u16 out = 0;
    for (u32 h = 0; h < headerCount; h++)
    {
        for (u32 r = 0; r < 4; r++)
        {
            rows[r] = Load(At(mem, headers[h] + 1u + r));
        }

        out = Word(mem, 1, 0);
        u8* stripVertexes = nullptr;
        if (strips)
        {
            strip.resize(sizeof(Gs::PreparedWrite) + static_cast<size_t>(count) * sizeof(Gs::StripVertex));
            const Gs::PreparedWrite header = {stripPrim, Gs::PreparedStrip, count};
            std::memcpy(strip.data(), &header, sizeof(header));
            stripVertexes = strip.data() + sizeof(header);
        }

        TransformStrip(mem, top, out, count, Clamp(rows[0]), Clamp(rows[1]), Clamp(rows[2]), Clamp(rows[3]), scroll, sticky,
                       last, stripVertexes, fogged);
        if (registers)
        {
            kick(vi14 + 3u, RegistersKickOffset);
        }

        kick(out, PacketKickOffset);
        if (strips)
        {
            // The packet's prepared form kept with the pending kick (its batch empty after kick sent the one before)
            vu.kickPendingBatch.swap(strip);
        }
        // LQ vf06, 1(vi00); MR32; SQ: the next output buffer
        rotated = VU1_SHUFFLE(Load(At(mem, 1)), 1, 2, 3, 0);
        Store(At(mem, 1), rotated);
    }

    // The registers as the routine leaves them: the last header's, the pipelined loop's last vertexes'
    StoreVf(vu.vf[1], rows[0]);
    StoreVf(vu.vf[2], rows[1]);
    StoreVf(vu.vf[3], rows[2]);
    StoreVf(vu.vf[4], rows[3]);
    StoreVf(vu.vf[5], scrollRaw);
    StoreVf(vu.vf[6], rotated);
    StoreVf(vu.vf[7], last.screen);
    StoreVf(vu.vf[8], last.colour);
    StoreVf(vu.vf[9], last.divided);
    StoreVf(vu.vf[10], Blend<7>(LoadVf(vu.vf[10]), last.coordinates));
    StoreVf(vu.vf[11], Blend<7>(LoadVf(vu.vf[11]), last.position));
    StoreVf(vu.vf[12], last.thirdLastScreen);
    StoreVf(vu.acc, last.accumulator);
    vu.q = last.q;
    vu.vi[3] = Word(mem, headers[headerCount - 1], 3);
    vu.vi[5] = out;
    vu.vi[6] = static_cast<u16>(top + inputSize);
    vu.vi[7] = static_cast<u16>(vi14 + 3u);
    vu.vi[8] = top;
    vu.vi[9] = end;
    vu.vi[10] = end;
    vu.vi[11] = static_cast<u16>(out + outputSize);
    vu.vi[13] = end;

    // The flags: MAC the last arithmetic result's (MULq.xyz of the last coordinates), the status flag's low bits from it, its
    // sticky ones every result's (and the flags' before the first, which the loads' entries carry), the division's bits the
    // last DIV's
    const u32 mac = MacOf(last.macSource);
    u32 status = vu.statusFlag | ((pendingStatus & 0xF) << 6) | (StickyLow(sticky) << 6);
    status = (status & ~0x3Fu) | StatusLow(mac) | last.divideFlags | (pendingStatus & 0xC00);
    vu.statusFlag = status;
    vu.macFlag = mac;
    st.pending.mac = mac;
    st.pending.status = (pendingStatus & ~0xFu) | StatusLow(mac);

    // JR vi15: back to program 1
    const u32 instructions = 8 + headerCount * (61 + 13 * (count - 2) + (registers ? 2 : 0));
    vu.executed += instructions;
    st.cycle += instructions;
    st.pc = vu.vi[15] & codeMask;
    st.backupCycles = 0;
    st.anyEntry = true;
    // The routine's last empty MOVEs (before the branch to the next header and in JR's delay slot) still in the FMAC pipeline:
    // no register, the pending flags, 3 cycles and 1 cycle before (the routine's tail runs without stalls)
    st.pipes.fmacRead = 0;
    st.pipes.fmacCount = 2;
    for (u32 n = 0; n < 2; n++)
    {
        Ee::VuFmacEntry& e = st.pipes.fmac[n];
        e = {};
        e.start = st.cycle - (n == 0 ? 3 : 1);
        e.mac = st.pending.mac;
        e.status = st.pending.status;
        e.clip = st.pending.clip;
    }
    run.ended = false;
    return true;
}
}

PreparedFunction KnownPreparedFunction(u32 programAddress)
{
    switch (programAddress)
    {
    case 0x2DF700:
        return &Type01;
    default:
        return nullptr;
    }
}
}
