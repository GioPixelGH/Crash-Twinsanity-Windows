#pragma once

// The graphics card's own time, for the log's performance lines (sessionlog.cpp): OpenGL's timer queries (GL_TIME_ELAPSED, core
// in 3.3) around each call that gives the card work (a draw, a clear, a copy between targets, a texture's upload), the hardware
// renderer's, the window's picture's and the UI's alike, so the time the card spends waiting for the next call (the render thread
// working, the game's thread) isn't counted. Each thread's context has queries of its own (they aren't shared between contexts);
// their results are read frames later, when GL_QUERY_RESULT_AVAILABLE says they're there: never waited for; each query's end is
// sent at once (glFlush), or a driver could keep it until the next call while the card waits. One frame in 8 is timed on each
// thread, counted 8 times (a query a call every frame cost the render thread 3%). See native/WINDOWS.md, "The log".
// $TWIN_GPU_TIME=0 times nothing (the entry points as they are). A query whose result isn't there 120 frames after it was begun,
// while the next one's is, is left out (its result would never come: the ones after it were held back, the time unknown after
// 8192 of them); $TWIN_GPU_TIME_TEST=stuck makes each thread's first query one (never begun), for the log's check

#include "common.h"

namespace NativeGraphics
{
// The entry point named, timed (glDrawArrays, glClear, glBlitFramebuffer, glTexImage2D, glTexSubImage2D, glGenerateMipmap),
// loaded with a context current: the loaders take it instead of OpenGL's own. nullptr for the others (and with $TWIN_GPU_TIME=0)
void* TimedEntryPoint(const char* name);
// A span of work timed on this thread's context (spans inside one count once)
void GpuTimeBegin();
void GpuTimeEnd();
// A frame's end on this thread (the render thread's picture made, the game's thread's shown): the results there read
void GpuTimeFrame();
// The nanoseconds the card has worked, read so far (a few frames behind), and whether they're known: not when the driver has no
// timer queries, never gives their results, or $TWIN_GPU_TIME=0
u64 GpuBusyNanoseconds();
bool GpuTimeKnown();
}
