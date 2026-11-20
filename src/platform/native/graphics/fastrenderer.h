#pragma once

// The fast renderer (THE LIST 24, 74-77; native/GRAPHICS.md, "The fast renderer"): a PC option beside the accurate renderer, the
// default, which emulates the GS a primitive at a time (hw/hwgs.cpp). Where the fast one knows a kind of drawing (a VU1 program),
// it draws it without the GS's emulation: the program's work done as native arithmetic giving the same values bit for bit, and
// the GS's state and vertexes handed to the hardware renderer as one batch. The kinds it doesn't know are drawn as before. The
// setting is options.txt's fast_renderer (Display > Renderer, shown once the fast renderer knows a kind), $TWIN_FAST_RENDERER
// over it: 0 accurate, 1 fast, verify (both ways, the results compared: the test of a kind's prepared path).

#include "common.h"

#include <vector>

namespace Ee
{
class Vu;
struct VuMicroState;
}

namespace NativeGraphics
{
enum class FastMode
{
    Accurate,
    Fast,
    Verify,
};

// The mode now (the setting is read at each call: it applies at once)
FastMode FastRendererMode();
// Whether the fast renderer knows any kind of drawing (the setting's row shown)
bool FastRendererHasKinds();

// THE LIST 74 step 3's GS side: when the mode isn't accurate every kick is drawn in runs (Gs::StartRun: the state made once, the
// backend called once for a run of primitives), or with $TWIN_FAST_KICKS=VU1 programs' addresses (hex, comma-separated: the
// retail executable's .vutext addresses $TWIN_RENDER_SPLIT's lines name them by) only those programs' kicks ("all": every one). Asked:
// whether the variable is set (the kicks' sources then carry their program, rendersplit.h's SplitKickSource); FastKickRuns is
// the GIF's hook (ee/gif.h's runsKick), on the render thread
bool FastKicksAsked();
bool FastKickRuns(u32 source);

// A kind's prepared path (THE LIST 74 step 3, the dispatcher's side: ee/vu1translate.cpp): VU1 at a program's entry handed to a
// function in the program's place. It runs from st (no branch pending) to where control leaves the program's code (base, size
// instructions) or the program ends (ended set), leaves VU1's registers, flags, memory and st as the program would, and appends
// to batch the GS's register writes the program's kicks would make (Gs::PreparedWrite entries: a kick pending when it starts is
// sent first, as the program's first XGKICK would; one pending when it leaves stays pending). False: it can't, and nothing
// changed (the program runs)
struct PreparedRun
{
    Ee::Vu& vu;
    Ee::VuMicroState& st;
    u32 base;
    u32 size;
    std::vector<u8>& batch;
    bool ended;
};
using PreparedFunction = bool (*)(PreparedRun& run);

// A prepared function's kick sent (the GIF reading the packet at address now, as the XGKICK after it or the program's stop
// would): converted into the batch (Gif::ConvertKick), or, a packet that can't be (IMAGE data), on VU1 itself the batch so far
// sent and then the packet (pc: its XGKICK's place, for the traces), on verify's copy a marker in the batch
void PreparedSend(PreparedRun& run, u32 address, u32 pc);

// The prepared functions the fast renderer knows, by program (fastscenery.cpp: the type 01 shader's, 0x2DF700), or null
PreparedFunction KnownPreparedFunction(u32 programAddress);

// The prepared function for a VU1 program (its address in the retail executable's .vutext), or null: the kinds the fast renderer
// knows (KnownPreparedFunction), and with $TWIN_FAST_PREPARE=all or programs' addresses (hex, comma-separated) the test's
// stand-in in their place for those, the program's own run (interpreted) with its kicks converted (Gif::ConvertKick)
PreparedFunction PreparedFor(u32 programAddress);
// The dispatcher's hook at the entry of a program that has a prepared function, when the mode isn't accurate. Fast: the function
// runs in the program's place and its batch is sent (Vu::prepared). Verify: the program runs (interpreted, its kicks sent as ever:
// the picture is the program's) and the function on a copy of VU1, their batches (the program's kicks converted) and VU1's state
// where each left compared; the differences logged (the first 50) and counted. False: nothing ran (the function couldn't)
bool RunPrepared(PreparedFunction function, Ee::Vu& vu, Ee::VuMicroState& st, u32 base, u32 size, bool& ended);
}

// The settings' row shown (ui/settings.cpp)
bool NativeGraphicsFastRendererShown();
