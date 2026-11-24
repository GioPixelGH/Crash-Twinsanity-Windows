#include "game/clock.h"

#include "debug.h"
#include "game/memory.h"
#include "platform/time.h"

#ifdef TWIN_NATIVE
#include "game/controllers.h"

#include <cmath>
#include <cstdlib>
#endif

namespace
{
// Clock units are the platform's ticks halved until there are at most this many a second
constexpr u32 MaxClockUnitsPerSecond = 9000;
// Ticks a frame may go past its time and still count as that frame
constexpr u64 FrameSlack = 0x4E;

// Test runs' time (g_DebugFixedTime): the ticks, a frame's worth more each frame, and the time checks a frame's work has left
constexpr u32 TestTimeChecks = 16;
u64 g_TestTicks;
u32 g_TestTimeChecks;
#ifdef TWIN_NATIVE
// The game clock held at 60 Hz past 60 (HoldClockAt60: THE LIST 29c's chases)
bool g_HeldAt60 = false;
#endif

u64 Now()
{
    return g_DebugFixedTime != 0 ? g_TestTicks : Platform::Time::Ticks();
}

// A test run's next frame's ticks: the frame rate's, or $TWIN_FIXED_TIME's rates in turn ("240,60": a 240th of a second, then a
// 60th...; the physics test's uneven frames, as a computer's past 60 Hz)
u64 TestFrameTicks(const GameTimeController* controller)
{
    u64 ticks = controller->ticksPerSecond / static_cast<u32>(controller->framesPerSecond);
#ifdef TWIN_NATIVE
    constexpr u32 MostRates = 16;
    static u32 rates[MostRates];
    static u32 count = 0;
    static u32 next = 0;
    static bool read = false;
    if (!read)
    {
        read = true;
        const char* at = std::getenv("TWIN_FIXED_TIME");
        while (at != nullptr && *at != '\0' && count < MostRates)
        {
            char* end = nullptr;
            unsigned long rate = std::strtoul(at, &end, 10);
            if (end == at)
            {
                break;
            }

            if (rate > 1)
            {
                rates[count++] = static_cast<u32>(rate);
            }

            at = *end == ',' ? end + 1 : end;
        }
    }

    // (held at 60 Hz the frames are the clock's own 60ths, the rates' turn kept for after)
    if (count != 0 && !g_HeldAt60)
    {
        ticks = controller->ticksPerSecond / rates[next];
        next = (next + 1) % count;
    }
#endif
    return ticks;
}
}

#ifdef TWIN_NATIVE
u64 GameClockTicks()
{
    return Now();
}
#endif

EABI_EXPORT(FUN_00191dd8, TimeClockAdvanceBy);
EABI_EXPORT(FUN_00192360, GameTimeHasTimeLeft);

extern "C"
{
    void TimeClockReset(TimeClock* clock)
    {
        clock->advance = 0;
        clock->flags.value = 0;
        clock->flags.restarts = 1;
        clock->time = 0;
    }

    bool TimeClockAdvance(TimeClock* clock, u32 units)
    {
        if (clock->flags.running == 0)
        {
            clock->advance = 0;
            return false;
        }

        if (clock->flags.restarts != 0)
        {
            clock->time = units;
            clock->advance = 0;
            clock->flags.restarts = 0;
        }
        else
        {
            clock->advance = units;
            clock->time += units;
        }

        return true;
    }

    bool TimeClockAdvanceBy(f32 speed, s32 frameTime, TimeClock* clock)
    {
        f32 seconds = g_FixedFrameSeconds;
        if (seconds <= 0.0f)
        {
            seconds = static_cast<f32>(frameTime) * g_SecondsPerClockUnit;
        }

        return TimeClockAdvance(clock, static_cast<u32>(static_cast<s32>(seconds * speed * g_ClockUnitsPerSecond)));
    }

    TimeClock* TimeClocksConstruct(TimeClock* clocks)
    {
        for (u32 i = 0; i < GameClockCount; i++)
        {
            TimeClockReset(&clocks[i]);
        }

        return clocks;
    }

    void TimeClocksDestroy(TimeClock* clocks, u32 flags)
    {
        if ((flags & 1) != 0)
        {
            MemoryDeallocate2_(clocks);
        }
    }

    void TimeClocksStart(TimeClock* clocks)
    {
        for (u32 i = 0; i < GameClockCount; i++)
        {
            clocks[i].flags.running = 1;
        }
    }

    void TimeClocksStop(TimeClock* clocks)
    {
        for (u32 i = 0; i < GameClockCount; i++)
        {
            clocks[i].flags.running = 0;
        }
    }

    void AdvanceClocks(TimeClock* clocks, GameTimeController* controller)
    {
        s32 frameTime = controller->frameTime;
        for (u32 i = 0; i < GameClockCount; i++)
        {
            TimeClockAdvanceBy(g_ClockSpeeds[i], frameTime, &clocks[i]);
        }
    }

    void ResetGlobalClock()
    {
        TimeClockReset(&g_GlobalClock);
        g_GlobalClock.flags.running = 1;
    }

    bool AdvanceGlobalClock(GameTimeController* controller)
    {
        return TimeClockAdvanceBy(1.0f, controller->frameTime, &g_GlobalClock);
    }

    void ResetClockSpeeds()
    {
        for (u32 i = 0; i < GameClockCount; i++)
        {
            g_ClockSpeeds[i] = 1.0f;
        }
    }

    GameTimeController* GameTimeConstructStamp(GameTimeController* controller)
    {
        controller->lastStamp = 0;
        return controller;
    }

    GameTimeController* GameTimeConstruct(GameTimeController* controller, s32 framesPerSecond)
    {
        GameTimeConstructStamp(controller);
        TimeClocksConstruct(controller->clocks);
        controller->framesPerSecond = framesPerSecond;
        controller->frameStart = 0;
        Platform::Time::Initialise();
        controller->ticksPerSecond = Platform::Time::TicksPerSecond;
        controller->frameSlack = FrameSlack;
        controller->shift = 0;
        s32 unitsPerSecond = static_cast<s32>(Platform::Time::TicksPerSecond);
        do
        {
            unitsPerSecond >>= 1;
            controller->shift++;
        } while (unitsPerSecond > static_cast<s32>(MaxClockUnitsPerSecond));

        g_ClockUnitsPerSecond = static_cast<f32>(unitsPerSecond);
        g_SecondsPerClockUnit = 1.0f / g_ClockUnitsPerSecond;
        ResetGlobalClock();
        ResetClockSpeeds();
        TimeClocksStart(controller->clocks);
        return controller;
    }

    void GameTimeTick(GameTimeController* controller)
    {
        if (g_DebugFixedTime != 0)
        {
            g_TestTicks += TestFrameTicks(controller);
            g_TestTimeChecks = TestTimeChecks;
        }

        s32 stamp = GameTimeStamp(controller);
        controller->frameStart = Now();
        GameTimeSetStamp(controller, stamp);
        AdvanceGlobalClock(controller);
        AdvanceClocks(controller->clocks, controller);
    }

    s32 GameTimeStamp(GameTimeController* controller)
    {
        return static_cast<s32>(Now() >> controller->shift);
    }

    // A stamp before the last one (the clock wrapped) leaves the frame's length as it was
    void GameTimeSetStamp(GameTimeController* controller, s32 stamp)
    {
        if (controller->lastStamp <= stamp)
        {
            controller->frameTime = stamp - controller->lastStamp;
#ifdef TWIN_NATIVE
            // A frame's time at most two of the game's frames: a frame far slower than the PS2 ever ran (the native renderer at
            // its slowest) makes the game slower rather than taking steps its physics wasn't made for (jumps cut short). Two of
            // its 60 Hz frames at the frame rates past 60 (a slower frame among faster ones isn't cut short)
            u32 rate = static_cast<u32>(controller->framesPerSecond) > 60 ? 60 : static_cast<u32>(controller->framesPerSecond);
            s32 most = static_cast<s32>((static_cast<u32>(controller->ticksPerSecond) / rate * 2) >> controller->shift);
            if (most > 0 && controller->frameTime > most)
            {
                controller->frameTime = most;
            }
#endif
        }

        controller->lastStamp = stamp;
    }

    void GameTimeFrameRendered(GameTimeController* controller)
    {
        GameTimeStamp(controller);
        CountRenderedFrame();
    }

    void CountRenderedFrame()
    {
        g_RenderedFrames++;
    }

    // Past the frame's time (and its slack), the time is taken within the frames since. The comparison is unsigned: in the
    // slack, the time left wraps around to plenty
    bool GameTimeHasTimeLeft(f32 seconds, GameTimeController* controller)
    {
        if (g_DebugFixedTime != 0)
        {
            if (g_TestTimeChecks == 0)
            {
                return false;
            }

            g_TestTimeChecks--;
            return true;
        }

        u32 ticksPerSecond = static_cast<u32>(controller->ticksPerSecond);
        s32 wanted = static_cast<s32>(seconds * static_cast<f32>(static_cast<s32>(ticksPerSecond)));
        u64 frameTicks = ticksPerSecond / static_cast<u32>(controller->framesPerSecond);
        u64 elapsed = Platform::Time::Ticks() - controller->frameStart;
        u64 limit = frameTicks + controller->frameSlack;
        while (limit < elapsed)
        {
            elapsed -= frameTicks;
        }

        return static_cast<u64>(static_cast<s64>(wanted)) < frameTicks - elapsed;
    }
}

#ifdef TWIN_NATIVE
bool FramesPast60()
{
    constexpr s32 NtscFramesPerSecond = 60;
    return G_GameClockController != nullptr && G_GameClockController->framesPerSecond > NtscFramesPerSecond;
}

void HoldClockAt60(bool held)
{
    g_HeldAt60 = held;
}

bool CountsSixtieths()
{
    // ($TWIN_FRAME_STEPS=1: retail's counts and shares a frame past 60 Hz too, the tests' way to see what they did)
    static const bool frames = std::getenv("TWIN_FRAME_STEPS") != nullptr;
    return FramesPast60() && !frames;
}

u32 SixtiethsGoneBy(s32 from, s32 to)
{
    if (!CountsSixtieths())
    {
        return 1;
    }

    // (the clock's units counted in 60ths of a second, rounded down: a 60th's boundary passed is one)
    const double sixtieths = 60.0 / static_cast<double>(g_ClockUnitsPerSecond);
    const s64 before = static_cast<s64>(std::floor(static_cast<double>(from) * sixtieths));
    const s64 after = static_cast<s64>(std::floor(static_cast<double>(to) * sixtieths));
    return after > before ? static_cast<u32>(after - before) : 0;
}

f32 KeptOverSeconds(f32 kept, f32 seconds)
{
    if (!CountsSixtieths() || !(0.0f < kept))
    {
        return kept;
    }

    return static_cast<f32>(std::pow(static_cast<double>(kept), static_cast<double>(seconds) * 60.0));
}

f32 MovedOverSeconds(f32 moved, f32 seconds)
{
    return CountsSixtieths() ? moved * seconds * 60.0f : moved;
}
#endif
