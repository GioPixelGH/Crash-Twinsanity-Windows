#pragma once

// THE LIST 74's first step, $TWIN_RENDER_SPLIT=1: the render thread's time and the GS's primitives by kind of drawing, logged
// with each performance line (sessionlog.cpp). Each record the render thread plays (renderthread.cpp) is timed and counted
// against what sent it: a kick against the VU1 program whose XGKICK it was (the dispatcher's, ee/vu1translate.h, by the
// program's address in the retail executable's .vutext), a transfer against its GIF path, the work queued for the GS's side
// as such; and against the renderer's frame bucket it was sent in (renderer/buckets.cpp: the frame's chain's tags tell where
// each bucket starts). It's the fast renderer's map: which kind of drawing the GS's emulation spends its time on. Needs the
// render thread (none with $TWIN_RENDER_THREAD=0); a batch's drawing is counted against the record whose primitive made it
// go out.

#include "common.h"

namespace NativeGraphics
{
bool RenderSplitOn();

// VU1's side: a kick's source (the XGKICK's address in bytes, as TWIN_GS_TRACE has it), with when the split is on its program's
// index + 1 in bits 16 to 21, the one VU1's run started at in bits 22 to 27, and bit 28 for a skin's part (a run of the
// renderer's program 1 after a run of a blend program: renderer/skins.cpp)
u32 SplitKickSource(u32 kickPc);
// VU1's side: a run of VU1 starts (its MSCAL's or MSCNT's address)
void SplitRunStarted(u32 start);

// The game's side: the frame's buckets' first tags (LinkRenderBuckets, before it sends the frame's chain; none after it)
void SplitFrameBuckets(const u8* const* firsts, u32 count);
// The game's side, VIF1's chains (hardware.cpp's SendVif1Chain): the chain's start, each tag (a bucket's first queues its
// number for the render thread, in its place among the data), its end
void SplitChainStart();
void SplitTag(const u8* tag);
void SplitChainEnd();

// The render thread: a record's work starts and ends (kind: renderthread.cpp's record kinds; value: the path, the kick's
// source)
struct SplitMark
{
    u64 nanoseconds;
    u64 primitives;
};
SplitMark SplitStart();
void SplitStop(const SplitMark& mark, u32 kind, u32 value);

// The lines since the last ones: by kind and by bucket, a frame's milliseconds, primitives and records
void LogRenderSplit(u64 frames);
}
