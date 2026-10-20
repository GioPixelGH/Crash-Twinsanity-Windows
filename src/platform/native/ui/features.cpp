// What the native options change in the game: applied once they're set and at start-up
#include "ui/nativeui.h"
#include "ui/bindings.h"

#include "native.h"
#include "graphics/presenter.h"
#include "audio/volumes.h"
#include "graphics/display.h"

#include "game/clock.h"
#include "game/controllers.h"
#include "game/gamecontroller.h"
#include "game/pads.h"
#include "game/renderer.h"
#include "game/sound.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <vector>

// The graphics' texture settings (settings.cpp has their weak stand-ins until the graphics define them)
void NativeGraphicsSetTextureUpscale(int);
void NativeGraphicsSetTexturePack(bool);
void NativeGraphicsSetAntiAliasing(int);
void NativeGraphicsSetVSync(bool);

namespace NativeUi
{
namespace
{
// The game clock's frames a second, which sizes the frame's work budget (GameTimeHasTimeLeft): main.cpp's PAL 50, NTSC 60
constexpr s32 PalFramesPerSecond = 50;
constexpr s32 NtscFramesPerSecond = 60;
// The option's rates past 60 (as fast as the computer goes: the clock's work budget a 240th of a second)
constexpr s32 FrameRates[] = {PalFramesPerSecond, NtscFramesPerSecond, 120, 144, 0};
constexpr s32 UnlimitedBudgetRate = 240;
constexpr f32 GameTick = 1.0f / 60.0f;
}

void ApplyOption(Option option)
{
    switch (option)
    {
    case OptionRefresh60:
    {
        bool ntsc = IsOn(OptionRefresh60);
        if (G_GameClockController != nullptr)
        {
            G_GameClockController->framesPerSecond = FrameRate() != 0 ? FrameRate() : UnlimitedBudgetRate;
        }

        NativeGraphics::SetVideoMode(!ntsc);
        NativeGraphics::SetFrameRate(FrameRate());
        break;
    }
    case OptionTextureFilter:
        NativeGraphicsSetPresentFilter(IsOn(OptionTextureFilter));
        break;
    case OptionCrtFilter:
        NativeGraphicsSetPostFilter(IsOn(OptionCrtFilter) ? 1 : 0);
        break;
    case OptionTextureUpscale:
        NativeGraphicsSetTextureUpscale(GetOption(OptionTextureUpscale));
        break;
    case OptionTexturePack:
        NativeGraphicsSetTexturePack(IsOn(OptionTexturePack));
        break;
    case OptionAntiAliasing:
        NativeGraphicsSetAntiAliasing(GetOption(OptionAntiAliasing));
        break;
    case OptionVSync:
        NativeGraphicsSetVSync(IsOn(OptionVSync));
        break;
    case OptionShowFps:
        SetFpsCounterShown(IsOn(OptionShowFps));
        break;
    case OptionVoiceVolume:
        ApplyVolumes();
        break;
    case OptionNintendoLayout:
        UpdatePrompts();
        break;
    case OptionController:
        ForgetPromptStyleForTest();
        break;
    default:
        break;
    }
}

void ApplyStartOptions()
{
    // The game's widescreen setting (the retail game starts with it off; a save's own setting applies once it's loaded)
    g_WidescreenTv = IsOn(OptionWidescreen) ? 1 : 0;
    ApplyOption(OptionTextureFilter);
    ApplyOption(OptionCrtFilter);
    ApplyOption(OptionTextureUpscale);
    ApplyOption(OptionTexturePack);
    ApplyOption(OptionAntiAliasing);
    ApplyOption(OptionVSync);
    ApplyOption(OptionShowFps);
    ApplyVolumes();
}

void ApplyGameSettingsOnce()
{
    // The kept volumes, output type and vibration given to the game once its sound and pads are there (a loaded save's own
    // settings still apply over them, as on the PS2)
    static bool applied = false;
    auto* pads = static_cast<GamePadController*>(G_GamePadController);
    if (applied || G_GameController == nullptr || pads == nullptr)
    {
        return;
    }

    applied = true;
    f32 music = static_cast<f32>(GetOption(OptionMusicVolume)) * 0.1f;
    f32 effects = static_cast<f32>(GetOption(OptionEffectsVolume)) * 0.1f;
    SetGroupVolume(music, music, MusicGroup);
    SetGroupVolume(effects, effects, EffectsGroup);
    SetGroupVolume(effects, effects, SecondEffectsGroup);
    SetGroupVolume(effects, effects, MovieGroup);
    SetMusicStereo(static_cast<u32>(GetOption(OptionOutputType)));
    if (IsOn(OptionVibration))
    {
        pads->flags.vibration = 1;
    }
    else
    {
        pads->DisableVibration();
    }
}

// ------------------------------------------------------------------------------------------------------- hold any button to skip
// The cutscenes' and the movies' skip (the cutscene skip option): any key, mouse button or pad button held SkipHoldSeconds skips,
// counted only when it's pressed after the scene began (what was held as it began, running or jumping into it, is ignored until
// it's let go). A scene is the polls in a row (the skip condition's, a movie's frames'): a gap of SceneGap starts another
namespace
{
using SkipClock = std::chrono::steady_clock;
// A press (just long enough not to be a stray key bounce): "press any button to skip"
constexpr f32 SkipHoldSeconds = 0.1f;
constexpr f32 SceneGap = 0.25f;
// The skip reported for this long once the hold completes (a scene's scripts may look a few frames later), then what's held is
// ignored until it's let go (a scene right after isn't skipped too)
constexpr f32 SkipReported = 0.15f;

struct SkipHold
{
    bool scene = false;
    SkipClock::time_point lastPoll;
    std::vector<s32> ignored;
    bool holding = false;
    SkipClock::time_point since;
    bool done = false;
    SkipClock::time_point doneAt;
    f32 progress = 0.0f;
};
SkipHold g_Skip;
bool g_SkipInputsForced = false;
std::vector<s32> g_SkipForcedInputs;
bool g_SkipClockForced = false;
SkipClock::time_point g_SkipForcedNow;

// The inputs held: keys (SDL scancodes), the mouse's buttons (0x10000 + SDL's), the pad's buttons (0x20000 + SDL's) and triggers
// (0x30000 + 0 or 1)
std::vector<s32> HeldInputs()
{
    if (g_SkipInputsForced)
    {
        return g_SkipForcedInputs;
    }

    std::vector<s32> held;
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    for (int key = 0; keys != nullptr && key < count; key++)
    {
        if (keys[key])
        {
            held.push_back(key);
        }
    }

    if (SDL_GetMouseFocus() != nullptr || MouseLookActive())
    {
        SDL_MouseButtonFlags mouse = SDL_GetMouseState(nullptr, nullptr);
        for (int button = SDL_BUTTON_LEFT; button <= SDL_BUTTON_X2; button++)
        {
            if ((mouse & SDL_BUTTON_MASK(button)) != 0)
            {
                held.push_back(0x10000 + button);
            }
        }
    }

    SDL_Gamepad* gamepad = LastGamepad();
    if (gamepad != nullptr)
    {
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; button++)
        {
            if (SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(button)))
            {
                held.push_back(0x20000 + button);
            }
        }

        for (int trigger = 0; trigger < 2; trigger++)
        {
            if (SDL_GetGamepadAxis(gamepad, trigger == 0 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16384)
            {
                held.push_back(0x30000 + trigger);
            }
        }
    }

    return held;
}

SkipClock::time_point SkipNow()
{
    return g_SkipClockForced ? g_SkipForcedNow : SkipClock::now();
}

f32 Seconds(SkipClock::time_point from, SkipClock::time_point to)
{
    return std::chrono::duration<f32>(to - from).count();
}

bool Contains(const std::vector<s32>& list, s32 value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

// A poll of the skip (a frame of a scene that can be skipped): whether the hold completed
bool PollSkipHold()
{
    SkipHold& skip = g_Skip;
    SkipClock::time_point now = SkipNow();
    std::vector<s32> held = HeldInputs();
    if (!skip.scene || Seconds(skip.lastPoll, now) > SceneGap)
    {
        // A scene begins: what's held now doesn't count
        skip = {};
        skip.scene = true;
        skip.ignored = held;
    }

    skip.lastPoll = now;
    // What was let go counts again once it's pressed
    std::vector<s32> stillIgnored;
    for (s32 input : skip.ignored)
    {
        if (Contains(held, input))
        {
            stillIgnored.push_back(input);
        }
    }

    skip.ignored.swap(stillIgnored);
    if (skip.done)
    {
        if (Seconds(skip.doneAt, now) <= SkipReported)
        {
            return true;
        }

        // Reported: what's held now is ignored until it's let go
        skip.done = false;
        skip.holding = false;
        skip.progress = 0.0f;
        skip.ignored = held;
        return false;
    }

    bool pressed = false;
    for (s32 input : held)
    {
        pressed = pressed || !Contains(skip.ignored, input);
    }

    if (!pressed)
    {
        skip.holding = false;
        skip.progress = 0.0f;
        return false;
    }

    if (!skip.holding)
    {
        skip.holding = true;
        skip.since = now;
    }

    skip.progress = std::min(1.0f, Seconds(skip.since, now) / SkipHoldSeconds);
    if (skip.progress >= 1.0f)
    {
        skip.done = true;
        skip.doneAt = now;
        return true;
    }

    return false;
}
}

bool SkipHoldComplete()
{
    return IsOn(OptionSkipCutscenes) && PollSkipHold();
}

// A cutscene's bars (the letterbox) showing
bool LetterboxShowing()
{
    GameController* controller = G_GameController;
    if (controller == nullptr)
    {
        return false;
    }

    u32 bars = controller->oleg.letterboxBottom.flags.state;
    return bars == Widget::StateAppearing || bars == Widget::StateShown || bars == Widget::StateDisappearing;
}

// The game's own skippable scenes (they skip on triangle) skip on any button too while a cutscene's letterbox shows
bool CutsceneSkipPressed()
{
    return IsOn(OptionSkipCutscenes) && LetterboxShowing() && PollSkipHold();
}

f32 SkipHoldProgress()
{
    // While a scene is polled and a counted button is held (its skip's bar), else none
    if (!g_Skip.scene || Seconds(g_Skip.lastPoll, SkipNow()) > SceneGap || !g_Skip.holding || !IsOn(OptionSkipCutscenes))
    {
        return -1.0f;
    }

    return g_Skip.done ? 1.0f : g_Skip.progress;
}

bool MovieSkipHeld()
{
    return SkipHoldComplete();
}

f32 SkipHoldSecondsForTest()
{
    return SkipHoldSeconds;
}

void SkipHoldForTest(bool forced, const std::vector<s32>& inputs, f32 seconds)
{
    static SkipClock::time_point base = SkipClock::now();
    g_SkipInputsForced = forced;
    g_SkipForcedInputs = inputs;
    g_SkipClockForced = forced;
    g_SkipForcedNow = base + std::chrono::duration_cast<SkipClock::duration>(std::chrono::duration<f32>(seconds));
    if (!forced)
    {
        g_Skip = {};
    }
}

void ApplyVolumes()
{
    // The player's volumes go on top of the game's own (the music and effects volumes are the game's groups, which its options
    // set): the voices' alone is the native option's
    NativeAudioSetVolumes(1.0f, 1.0f, static_cast<f32>(GetOption(OptionVoiceVolume)) / 10.0f);
}

f32 CameraShakeScale()
{
    return IsOn(OptionCameraShake) ? 1.0f : 0.0f;
}

s32 FramesPerSecond(s32 retail)
{
    return IsOn(OptionRefresh60) ? NtscFramesPerSecond : retail;
}

s32 FrameRate()
{
    return FrameRates[std::clamp(GetOption(OptionRefresh60), 0, 4)];
}

u32 GameTicks(f32 seconds, f32* carried)
{
    const s32 rate = FrameRate();
    if (rate == PalFramesPerSecond || rate == NtscFramesPerSecond)
    {
        *carried = 0.0f;
        return 1;
    }

    // At most a few at once (a long frame: the game's own steps are capped at two frames' time too)
    *carried += std::max(seconds, 0.0f);
    u32 ticks = 0;
    while (*carried >= GameTick && ticks < 4)
    {
        *carried -= GameTick;
        ticks++;
    }

    if (ticks == 4)
    {
        *carried = 0.0f;
    }

    return ticks;
}
}
