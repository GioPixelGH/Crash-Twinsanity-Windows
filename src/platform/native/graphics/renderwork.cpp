#include "renderwork.h"

#include <atomic>
#include <chrono>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <x86intrin.h>
#define TWIN_WORK_TSC 1
#endif

namespace NativeGraphics
{
thread_local Work t_Work = Work::None;

namespace
{
using Clock = std::chrono::steady_clock;

thread_local bool t_Counting = false;
thread_local u64 t_Since = 0;
// Each kind's ticks (the render thread's alone writes them)
std::atomic<u64> g_Ticks[static_cast<u32>(Work::Count)] = {};
// The ticks' clock against the steady clock from the render thread's start: nanoseconds a tick, worked out again until 2 s have
// passed, then kept
std::atomic<u64> g_StartTicks{0};
std::atomic<s64> g_StartNanoseconds{0};
std::atomic<double> g_NanosecondsPerTick{0.0};
std::atomic<bool> g_RateKept{false};

s64 NowNanoseconds()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

u64 Ticks()
{
#if defined(TWIN_WORK_TSC)
    return __rdtsc();
#else
    return static_cast<u64>(NowNanoseconds());
#endif
}

double NanosecondsPerTick()
{
#if defined(TWIN_WORK_TSC)
    if (g_RateKept.load(std::memory_order_relaxed))
    {
        return g_NanosecondsPerTick.load(std::memory_order_relaxed);
    }

    // (Nothing until the rate's kept: the counts only ever grow, and the play's lines start long after)
    const u64 start = g_StartTicks.load(std::memory_order_relaxed);
    const s64 nanoseconds = NowNanoseconds() - g_StartNanoseconds.load(std::memory_order_relaxed);
    const u64 ticks = Ticks() - start;
    if (start == 0 || ticks == 0 || nanoseconds < 2'000'000'000)
    {
        return 0.0;
    }

    g_NanosecondsPerTick = static_cast<double>(nanoseconds) / static_cast<double>(ticks);
    g_RateKept = true;
    return g_NanosecondsPerTick.load(std::memory_order_relaxed);
#else
    return 1.0;
#endif
}
}

void CountWorkOnThisThread()
{
    if (g_StartTicks.load() == 0)
    {
        g_StartNanoseconds = NowNanoseconds();
        g_StartTicks = Ticks();
    }

    t_Counting = true;
    t_Work = Work::None;
    t_Since = Ticks();
}

Work SwitchWork(Work to)
{
    if (!t_Counting)
    {
        return Work::None;
    }

    const u64 now = Ticks();
    const Work before = t_Work;
    if (before != Work::None)
    {
        std::atomic<u64>& ticks = g_Ticks[static_cast<u32>(before)];
        ticks.store(ticks.load(std::memory_order_relaxed) + (now - t_Since), std::memory_order_relaxed);
    }

    t_Since = now;
    t_Work = to;
    return before;
}

u64 WorkNanoseconds(Work kind)
{
    return static_cast<u64>(static_cast<double>(g_Ticks[static_cast<u32>(kind)].load(std::memory_order_relaxed)) *
                            NanosecondsPerTick());
}

namespace
{
std::atomic<u64> g_ProgramsMade{0};
std::atomic<u64> g_ProgramNanoseconds{0};
std::atomic<u64> g_LongestProgram{0};
}

void CountProgramMade(u64 nanoseconds)
{
    g_ProgramsMade.fetch_add(1, std::memory_order_relaxed);
    g_ProgramNanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
    u64 longest = g_LongestProgram.load(std::memory_order_relaxed);
    while (nanoseconds > longest && !g_LongestProgram.compare_exchange_weak(longest, nanoseconds, std::memory_order_relaxed))
    {
    }
}

u64 TakeLongestProgram()
{
    return g_LongestProgram.exchange(0, std::memory_order_relaxed);
}

u64 ProgramsMade()
{
    return g_ProgramsMade.load(std::memory_order_relaxed);
}

u64 ProgramNanoseconds()
{
    return g_ProgramNanoseconds.load(std::memory_order_relaxed);
}
}
