#pragma once

// The render thread: the GS's side of the hardware (the GIF, the GS's registers and local memory, the hardware renderer and its
// OpenGL context) runs on a thread of its own, a frame behind the game. The game's thread runs the game, VU0, VIF1 and VU1 as
// before; what they send the GIF (VU1's XGKICK packets, VIF1's DIRECT data, the GIF channel's chains) is copied into a queue the
// render thread takes it from in the same order, so the GS gets exactly what it got before. Everything else that touches the GS's
// side (the PCRTC's registers, a movie's picture, the glyph images, the renderer's settings, reading the picture back) goes
// through the same queue as work to run there, in its place among the data.
//
// The frames: Present queues the frame's picture to be made (the shown buffer copied out of its render target), then waits for
// the one before it and shows that in the window on the main thread (SDL's window and swap calls are the main thread's). So the
// game builds frame N + 1 while the render thread draws frame N, and the window shows N - 1 then: one frame later than the game
// made it, as on the PS2, whose DMA sent a frame's chain while the game built the next.
//
// Three stages by default: VIF1 and VU1 run on a thread of their own between the game's and the render thread (the VU stage, a
// queue like the render thread's: the game's thread queues VIF1's words, the GIF's transfers and the work for the GS's side, in
// their order; the VU stage runs VIF1 and VU1 and hands everything on, what VU1 and VIF1 send the GIF in its place, to the render
// thread's queue). The GS gets exactly what it got before, in the same order. $TWIN_RENDER_THREAD=gs keeps VIF1 and VU1 on the
// game's thread (two stages), =vif puts them on the render thread, =0 keeps everything on the game's thread (as before the
// render thread). See native/GRAPHICS.md, "Threads".

#include "common.h"

#include <functional>

namespace NativeGraphics
{
// Wanted unless $TWIN_RENDER_THREAD=0, or the harness says not (before the renderer starts)
void SetRenderThreadWanted(bool wanted);
bool RenderThreadWanted();

// Started from the main thread (its window's context current, if it has one, for the render thread's own context to share its
// textures with), and stopped with everything queued done first (at exit too)
void StartRenderThread();
void StopRenderThread();
bool RenderThreadActive();
bool OnRenderThread();
// Wanted and not started yet: the GS's side isn't made until it is (the settings wait for it)
bool RenderThreadPending();

// Work for the GS's side, after everything queued before it: queued (run at once when there's no render thread, or on it)
void RenderCall(std::function<void()> work);
// The same, waited for
void RenderCallAndWait(const std::function<void()>& work);
// Everything queued done: the render thread waits for more, so the GS's side can be read or written from here until something
// else is queued (not OpenGL: the render thread's context is its own)
void RenderSync();
// What's queued so far handed over (the end of a chain), for the render thread to start on
void RenderFlush();

// A frame's picture to be made (queued, given its number from 1); its number
u64 RenderQueueFrame(std::function<void(u64 frame)> show);
// Waits for a frame's picture to be made
void RenderWaitFrame(u64 frame);

// TWIN_RENDER_THREAD=vif: VIF1's words go to the render thread, which runs VIF1 and VU1 there too (measured slower: the
// render thread is the busier one), instead of what they send the GIF
bool RenderThreadRunsVu1();
void RenderQueueVif1(const u32* words, u32 count);

// Statistics: nanoseconds the render thread has worked (not waited) since it started, and the game's thread has waited for it;
// the VU stage's thread's work (VIF1's and VU1's) with three stages
u64 RenderBusyNanoseconds();
u64 RenderWaitNanoseconds();
u64 VuStageBusyNanoseconds();

// The threads the frames are made on: as $TWIN_RENDER_THREAD asks (for the log's start: "three: ... (the default)"), and as they
// run now (the render thread may not have started)
const char* ThreadsAskedText();
enum class Threads
{
    // Everything on the game's thread
    One,
    // The render thread, VIF1 and VU1 on the game's thread ($TWIN_RENDER_THREAD=gs)
    TwoVuOnGame,
    // The render thread with VIF1 and VU1 ($TWIN_RENDER_THREAD=vif)
    TwoVuOnRender,
    // The game's, the VU stage's and the render thread
    Three,
};
Threads ThreadsNow();
}
