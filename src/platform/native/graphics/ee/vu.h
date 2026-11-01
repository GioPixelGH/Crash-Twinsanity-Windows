#pragma once

// A vector unit (VU0 or VU1) in micro mode, interpreted: its micro memory's instruction pairs (the upper FMAC instruction and
// the lower one, or an immediate for I) run until the E bit's delay slot, with the pipelines whose timing the game's programs
// are scheduled around: results of the FMAC pipeline stall readers until they're ready (4 cycles), its MAC, status and clip
// flags become visible 4 cycles after the instruction, Q 7 cycles after DIV and SQRT (13 after RSQRT) and P after each EFU
// instruction's latency (until then the old value is read), a branch reads the old value of an integer register the instruction
// just before it wrote, and a lower instruction reads the old value of a register its upper instruction writes in the same
// cycle. Floats follow the VU's: denormals are zero, overflow clamps to the largest float, results round toward zero.
//
// The programs themselves are the game's (its .vutext, loaded by the renderer's MPG uploads); this is only the processor.
// VU1's programs mostly run translated (vu1translate.h, native/GRAPHICS.md's "VU1 translation"): the same state, the same
// results, from C++ made of the game's microcode at build time; the interpreter runs what isn't translated.

#include "common.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <functional>
#include <vector>

namespace Ee
{
union Vf
{
    f32 f[4];
    u32 u[4];
    s32 s[4];
};

// The micro mode's state between two instructions besides the registers: the pipelines' results in flight and the flags they
// carry, the branch and E bit delay slots, and the branch read rule's old value. The interpreter keeps it for a program's run;
// the translated programs hand it over (vu1translate.cpp)
struct VuFmacEntry
{
    u64 start;
    u8 upperReg;
    u8 upperMask;
    u8 lowerReg;
    u8 lowerMask;
    u8 flags;
    u32 mac;
    u32 status;
    u32 clip;
};

struct VuPipelines
{
    VuFmacEntry fmac[4];
    u32 fmacRead = 0;
    u32 fmacCount = 0;
    bool fdiv = false;
    u64 fdivStart = 0;
    u32 fdivLatency = 0;
    u32 fdivValue = 0;
    u32 fdivStatus = 0;
    bool efu = false;
    u64 efuStart = 0;
    u32 efuLatency = 0;
    u32 efuValue = 0;
    // ILW's results (a register and when it's ready), for the branches' stalls
    u8 ialuReg[4] = {};
    u64 ialuReady[4] = {};
    u32 ialuNext = 0;
};

// The pending results of the instruction being run (what goes into its pipeline entries)
struct VuPending
{
    u32 mac = 0;
    u32 status = 0;
    u32 clip = 0;
    u32 q = 0;
    u32 qStatus = 0;
    u32 p = 0;
};

struct VuMicroState
{
    VuPipelines pipes;
    VuPending pending;
    u64 cycle = 0;
    u32 pc = 0;
    u32 ebit = 0;
    u32 branch = 0;
    u32 branchTarget = 0;
    bool delayBranch = false;
    u32 delayTarget = 0;
    // The branch read rule: the integer register the last instruction wrote and its old value, for 2 cycles
    u32 backupCycles = 0;
    u32 backupReg = 0;
    u16 backupValue = 0;
    // The status flag when the program started, and whether an FMAC instruction has run since (for the translations)
    u32 startStatus = 0;
    bool anyEntry = false;
};

class Vu
{
public:
    // VU0 (4 KB of data and of code) or VU1 (16 KB each)
    explicit Vu(u32 index);

    u32 index() const { return index_; }
    u8* data() { return data_; }
    u32 dataSize() const { return dataSize_; }
    u32 codeSize() const { return codeSize_; }
    const u64* code() const { return code_.data(); }

    // Code written into micro memory (an MPG's instructions, 8 bytes each) at an instruction address
    void WriteCode(u32 address, const u8* code, u32 instructions);

    // Runs from an instruction address until the program ends (the E bit). The VIF's TOP and ITOP are read by XTOP and XITOP
    void Run(u32 start);
    // The interpreter's run in steps, with the rounding (toward zero) already set: the state made for a start, one instruction
    // (false once the program has ended: its E bit's delay slot run), the end (the pipelines' results landed)
    void BeginMicro(VuMicroState& state, u32 start);
    bool StepMicro(VuMicroState& state);
    void EndMicro(VuMicroState& state);
    // The pipelines' results due at the state's cycle made visible
    void FlushPipes(VuMicroState& state);
    // The interpreter alone (TWIN_VU1=interpret makes every run this)
    void RunInterpreted(u32 start);
    // Where the last program stopped (MSCNT goes on from there)
    u32 pc() const { return pc_; }

    // VU1's XGKICK: the GIF packet at a data address (a quadword index) sent on path 1. The GIF reads the packet over the
    // cycles after XGKICK while the program goes on, and a program may finish its packet meanwhile (the skinned meshes' writes
    // their last two vertices' colours 2 and 5 cycles after): the packet is sent at the next XGKICK or when the program stops
    // (FlushKick), with what the program wrote into it by then ($TWIN_VU1_KICK_AT_ONCE=1: at the XGKICK, as before)
    std::function<void(u32 address)> xgkick;
    void Kick(u32 address);
    void FlushKick();
    // What runs a program instead of the interpreter (VU1's translations, vu1translate.h)
    std::function<void(u32 start)> runner;
    u32 top = 0;
    u32 itop = 0;

    // Macro mode (the EE's COP2 instructions, each done at once): loads and stores of VF registers, the FMAC instructions the
    // renderer's macro code uses (bc: the broadcast component, 0-3; masks: x is bit 0), CLIPw, and VCALLMS/VCALLMSR
    void LoadVf(u32 reg, const void* from);
    void StoreVf(u32 reg, void* to) const;
    void MacroMove(u32 ft, u32 fs, u32 mask);
    void MacroMulBc(u32 fd, u32 fs, u32 ft, u32 bc, bool toAcc, u32 mask = 0xF);
    void MacroMaddBc(u32 fd, u32 fs, u32 ft, u32 bc, bool toAcc, u32 mask = 0xF);
    void MacroMul(u32 fd, u32 fs, u32 ft, u32 mask = 0xF);
    void MacroClipW(u32 fs, u32 ft);
    // VCALLMS's immediate is the program's byte address in micro memory
    void CallMicro(u32 byteAddress) { Run(byteAddress >> 3); }

    // The registers
    Vf vf[32] = {};
    u16 vi[16] = {};
    Vf acc = {};
    u32 q = 0;
    u32 p = 0;
    u32 i = 0;
    u32 r = 0;
    u32 statusFlag = 0;
    u32 macFlag = 0;
    u32 clipFlag = 0;

    // Everything a program sees (registers, flags, memories, TOP and ITOP) made the same as another unit's
    void CopyFrom(const Vu& other);

    // The instructions of a program the interpreter doesn't know (an address each, upper or lower), for the tests
    std::vector<u32> UnknownInstructions(u32 start, u32 count);

    // Instructions run (statistics), and the address the running program started at (for traces)
    u64 executed = 0;
    // With timing on (TWIN_FRAME_STATS): the time in programs, and the part of it in XGKICK's packets (the GIF and the GS)
    bool timing = false;
    u64 runNanoseconds = 0;
    u64 kickNanoseconds = 0;
    // The XGKICK not sent yet: its packet's address and the instruction's
    bool kickPending = false;
    u32 kickPendingAddress = 0;
    u32 kickPendingPc = 0;
    // Bumped when code written into micro memory changes it, and the instructions changed since TakeCodeChanges last took them
    u64 codeVersion() const { return codeVersion_; }
    std::bitset<2048> TakeCodeChanges()
    {
        std::bitset<2048> changes = codeChanges_;
        codeChanges_.reset();
        return changes;
    }
    u32 runStart = 0;
    u32 kickPc = 0;

    // The registers and memories for a recording (capture.h)
    template <typename Io>
    void Serialize(Io& io);

private:
    struct Decoded;

    void Decode(u32 address);

    u32 index_;
    u8* data_;
    u32 dataSize_;
    u32 codeSize_;
    std::array<u64, 2048> code_ = {};
    std::array<Decoded*, 2048> decoded_ = {};
    u32 pc_ = 0;
    u64 codeVersion_ = 0;
    std::bitset<2048> codeChanges_;
};
}
