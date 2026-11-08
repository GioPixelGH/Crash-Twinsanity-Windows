#pragma once

// The render thread's time split by its kind of work, for the log's performance lines (sessionlog.cpp; native/WINDOWS.md, "The
// log"): the GS emulated on the processor (the GIF's data and the work queued with it: hwgs.cpp's drawing; with two threads and
// VU1 on the render thread, VIF1 and VU1 too), OpenGL's calls (the driver's work: hw/gl.cpp's entry points, counted when the GS
// emulation calls them), waiting for the card (a picture or GS memory read back: glReadPixels, glFinish), and the picture handed
// to the window (display.cpp's MakeShown: its copy into a slot, its wait for the window to have drawn the slot before). Only the
// render thread counts; the window's present on the main thread is timed apart (display.cpp). The processor's time stamp counter
// is read at each change (a few nanoseconds; twice an OpenGL call), its ticks made nanoseconds against
// the steady clock over the render thread's first 2 s (nothing's counted before).

#include "common.h"

namespace NativeGraphics
{
enum class Work : u32
{
    None,
    Gs,
    OpenGl,
    CardWait,
    Picture,
    Count,
};

// The calling thread's work now (None on a thread that doesn't count)
extern thread_local Work t_Work;
// The render thread counts from now on (its own call, at its start)
void CountWorkOnThisThread();
// The thread's work from now on (the time since the last change counted to the one before, which is returned); nothing on a thread
// that doesn't count
Work SwitchWork(Work to);
// The nanoseconds counted to a kind of work so far
u64 WorkNanoseconds(Work kind);
// The GS's shader programs made so far (hwgs.cpp's ProgramFor: one for each new combination of the GS's states, compiled and
// linked when first drawn with) and their time (part of OpenGL's), for the log's slow frames
void CountProgramMade(u64 nanoseconds);
u64 ProgramsMade();
u64 ProgramNanoseconds();
// The longest program's time since the last call (the performance lines')
u64 TakeLongestProgram();

class WorkSpan
{
public:
    explicit WorkSpan(Work work) : before_(SwitchWork(work))
    {
    }
    ~WorkSpan()
    {
        SwitchWork(before_);
    }
    WorkSpan(const WorkSpan&) = delete;
    WorkSpan& operator=(const WorkSpan&) = delete;

private:
    Work before_;
};
}
