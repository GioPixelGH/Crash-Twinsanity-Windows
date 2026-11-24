#pragma once

#include "abi.h"
#include "common.h"

// A clock's flags: it runs, and the next advance sets the time instead of adding to it
union TimeClockFlags
{
    u32 value;
    struct
    {
        u32 running : 1;
        u32 restarts : 1;
        u32 unused2 : 30;
    };
};
CHECK_SIZE(TimeClockFlags, 4);

// The game's clocks: time in units of 1/9000 of a second (the platform's ticks >> 6), advanced every frame by the frame's time
// times their speed, while they run. A fixed step (g_FixedFrameSeconds above 0) takes the frame's time's place
struct TimeClock
{
    TimeClockFlags flags;
    u32 time;
    // The last advance
    u32 advance;
};
CHECK_SIZE(TimeClock, 0xC);

constexpr u32 GameClockCount = 8;

// The game clocks the code picks (instances keep the index of theirs): the first (the particles', the decals', the wind's and
// the follow camera's), most objects' (also the cutscenes', the dynamic scenery's and the timed play's countdown) and the
// characters' and graples' (also the time played)
enum GameClockIndex : u32
{
    FirstClock = 0,
    ObjectClock = 1,
    CharacterClock = 2,
};

// The frames a second the game's values per frame are given for (the particles' and decals' lives, the bodies' drags, the
// pickups' spin): NTSC's 60, whatever the TV's rate, and such a frame's seconds
constexpr f32 FramesPerSecond = 60.0f;
constexpr f32 SecondsPerFrame = Rounded(1.0 / 60.0);

// The frames' timing, and the game's clocks (the global clock is apart)
struct GameTimeController
{
    // The frame's start and length, in clock units
    s32 lastStamp;
    s32 frameTime;
    TimeClock clocks[GameClockCount];
    s32 framesPerSecond;
    // The platform's ticks >> shift are clock units
    u32 shift;
    u64 ticksPerSecond;
    // How far into the next frame a frame's work may go (GameTimeHasTimeLeft)
    u64 frameSlack;
    // In the platform's ticks
    u64 frameStart;
};
CHECK_SIZE(GameTimeController, 0x88);
CHECK_OFFSET(GameTimeController, ticksPerSecond, 0x70);

extern "C"
{
    extern TimeClock g_GlobalClock RETAIL(G_GlobalClock_);
    extern f32 g_ClockSpeeds[GameClockCount] RETAIL(G_ClockSpeedScales);
    extern f32 g_FixedFrameSeconds RETAIL(G_GlobalClockSpeedScale);
    extern f32 g_ClockUnitsPerSecond RETAIL(G_TICKS_TO_TIME);
    extern f32 g_SecondsPerClockUnit RETAIL(G_TIME_TO_TICKS);
    // The game clock's last frame in seconds (the scripts' movement commands scale by it)
    extern f32 g_FrameSeconds RETAIL(D_0030A0A4);
    // The frames rendered, which the renderer's double buffers go by
    extern u32 g_RenderedFrames RETAIL(D_00309B68);

    // A seconds' value of clock units (from a variable)
    f32 ClockUnitsToSeconds(const s32* units) RETAIL(GetTicksFromTime);

    void TimeClockReset(TimeClock* clock) RETAIL(FUN_00191d68);
    // Returns whether it runs
    bool TimeClockAdvance(TimeClock* clock, u32 units) RETAIL(FUN_00191d80);
    bool TimeClockAdvanceBy(f32 speed, s32 frameTime, TimeClock* clock) RETAIL_N32(FUN_00191dd8);
    // The controller's 8
    TimeClock* TimeClocksConstruct(TimeClock* clocks) RETAIL(FUN_00191e48);
    void TimeClocksDestroy(TimeClock* clocks, u32 flags) RETAIL(FUN_00191ea8);
    void TimeClocksStart(TimeClock* clocks) RETAIL(FUN_00191f70);
    void TimeClocksStop(TimeClock* clocks) RETAIL(FUN_00192038);
    void AdvanceClocks(TimeClock* clocks, GameTimeController* controller) RETAIL(AdvanceClocks);
    void ResetGlobalClock() RETAIL(FUN_00191f00);
    bool AdvanceGlobalClock(GameTimeController* controller) RETAIL(AdvanceGlobalClock);
    void ResetClockSpeeds() RETAIL(FUN_00191f38);

    GameTimeController* GameTimeConstruct(GameTimeController* controller, s32 framesPerSecond) RETAIL(FUN_001921c0);
    GameTimeController* GameTimeConstructStamp(GameTimeController* controller) RETAIL(FUN_00192068);
    // A frame starts: its length is the time since the last one's start, and the clocks advance by it
    void GameTimeTick(GameTimeController* controller) RETAIL(FUN_001922a0);
    // The time in clock units
    s32 GameTimeStamp(GameTimeController* controller) RETAIL(FUN_001922f8);
    void GameTimeSetStamp(GameTimeController* controller, s32 stamp) RETAIL(FUN_00192080);
    void GameTimeFrameRendered(GameTimeController* controller) RETAIL(FUN_00192330);
    void CountRenderedFrame() RETAIL(FUN_001920a0);
    // Whether the frame has seconds left for more work, counting from its start
    bool GameTimeHasTimeLeft(f32 seconds, GameTimeController* controller) RETAIL_N32(FUN_00192360);
}

#ifdef TWIN_NATIVE
// Natively, whether the frames are past the PS2's 60 a second (Options > Display's 120 Hz, 144 Hz and unlimited: the game clock's
// frames a second): the character's steps then make up for being shorter than 60 Hz's (CharacterAgent::Move)
bool FramesPast60();
// Natively, the game clock held at 60 Hz past 60 (THE LIST 29c, the two chases: NativeUi::ChaseRateFrame): a test run's fixed
// frames ($TWIN_FIXED_TIME's rates) are then 60ths, as the frames paced at 60 are
void HoldClockAt60(bool held);
// Whether what retail counts in frames is counted in 60ths of a second (THE LIST 29a): past 60 Hz, unless $TWIN_FRAME_STEPS=1
bool CountsSixtieths();
// Past 60 Hz, the 60ths of a second (the PS2's frames) a clock went through from one of its times to another, for what retail
// counts in frames (a rigid body's steps since its launch, since it last touched what it rides); at 60 Hz and below a frame is
// one, as on the PS2
u32 SixtiethsGoneBy(s32 from, s32 to);
// Past 60 Hz, what retail does to a value once a frame done for a frame of these seconds as at 60 Hz: a share kept (kept to the
// power of the 60ths gone by: a share never below 0 kept as it is) and a move (times the 60ths); at 60 Hz and below, as it is
f32 KeptOverSeconds(f32 kept, f32 seconds);
f32 MovedOverSeconds(f32 moved, f32 seconds);
// The game clock's ticks now (Platform::Time's): the test runs' under $TWIN_FIXED_TIME, a frame's worth more each frame
u64 GameClockTicks();
#endif
