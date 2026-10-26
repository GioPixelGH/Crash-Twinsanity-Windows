// The native build's own options (the PC conveniences and the opt-in changes to the retail game), kept in the app's settings
// (Native::SettingsFolder()/options.txt, "name=value" lines), and what happens between the game's frames for them: the window's
// focus (the game's own pause, the sound muted), Alt+Enter / Cmd+F (fullscreen), the 60 Hz mode's pacing. SDL's events are watched
// (SDL_AddEventWatch), so the platform's own event handling stays as it is
#include "ui/bindings.h"
#include "ui/nativeui.h"
#include "ui/overlay.h"

#include "native.h"
#include "audio/volumes.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace NativeUi
{
namespace
{
constexpr const char* SettingsFile = "options.txt";

struct OptionInfo
{
    Option option;
    const char* name;
    s32 defaultValue;
    s32 maximum;
};
// The defaults, the first start's and the settings' reset alike (DefaultOption): the game as the PS2 had it (no skipping its
// cutscenes, as it couldn't; its own settings' defaults), at 60 Hz (the NTSC release's rate: 50 judders on a 60 Hz display), with
// the PC conveniences that change nothing of the game on (fast loading, pausing and muting in the background, the mouse)
constexpr OptionInfo Infos[OptionCount] = {
    {OptionSkipCutscenes, "skip_cutscenes", 0, 1},
    {OptionFastLoading, "fast_loading", 1, 1},
    {OptionPauseOnFocusLoss, "pause_on_focus_loss", 1, 1},
    {OptionWidescreen, "widescreen", 1, 1},
    {OptionRefresh60, "refresh_60hz", 1, 4},
    {OptionRetailFixes, "retail_fixes", 0, 1},
    {OptionTextureFilter, "texture_filter", 1, 1},
    {OptionCrtFilter, "crt_filter", 0, 1},
    {OptionNintendoLayout, "nintendo_layout", 1, 1},
    {OptionVoiceVolume, "voice_volume", 10, 10},
    {OptionController, "controller", 0, 4},
    {OptionMouse, "mouse", 1, 1},
    {OptionTextureUpscale, "texture_upscale", 0, 2},
    {OptionTexturePack, "texture_pack", 1, 1},
    {OptionAntiAliasing, "anti_aliasing", 0, 1},
    {OptionVSync, "vsync", 1, 1},
    {OptionMusicVolume, "music_volume", 10, 10},
    {OptionEffectsVolume, "effects_volume", 10, 10},
    {OptionOutputType, "output_type", 1, 2},
    {OptionVibration, "vibration", 1, 1},
    {OptionCameraShake, "camera_shake", 1, 1},
    {OptionInvertCameraX, "invert_camera_x", 0, 1},
    {OptionInvertCameraY, "invert_camera_y", 0, 1},
    {OptionCameraSpeed, "camera_speed", 5, 10},
    {OptionStickDeadZone, "stick_dead_zone", 0, 10},
    {OptionMuteInactive, "mute_in_background", 1, 1},
    {OptionUltrawide, "ultrawide", 0, 1},
    {OptionMouseLook, "mouse_look", 1, 1},
    {OptionMouseSensitivity, "mouse_sensitivity", 5, 10},
    {OptionInvertMouseY, "invert_mouse_y", 0, 1},
    {OptionLanguage, "language", 0, 5},
    {OptionDetailedLog, "detailed_log", 0, 1},
    {OptionMenuHints, "menu_hints", 0, 1},
    {OptionShowFps, "show_fps", 0, 1},
};

s32 g_Values[OptionCount];
bool g_Read = false;
// Whether options.txt was there when they were read (else the defaults: a first start)
bool g_FromFile = false;
bool g_Saving = true;

std::atomic<bool> g_FocusLost{false};
std::atomic<bool> g_FocusPauseDue{false};
std::atomic<bool> g_FullscreenToggleDue{false};
std::atomic<s32> g_CapturedKey{-1};
std::atomic<s32> g_CapturedButton{-1};
std::atomic<bool> g_Capturing{false};
bool g_Watching = false;
bool g_Muted = false;

std::string SettingsPath()
{
    return Native::SettingsFolder() + "/" + SettingsFile;
}

void Read()
{
    if (g_Read)
    {
        return;
    }

    g_Read = true;
    for (const OptionInfo& info : Infos)
    {
        g_Values[info.option] = info.defaultValue;
    }

    std::ifstream file(SettingsPath());
    g_FromFile = file.is_open();
    std::string line;
    while (std::getline(file, line))
    {
        size_t equals = line.find('=');
        if (line.empty() || line[0] == '#' || equals == std::string::npos)
        {
            continue;
        }

        std::string name = line.substr(0, equals);
        int value = std::atoi(line.c_str() + equals + 1);
        for (const OptionInfo& info : Infos)
        {
            if (name == info.name)
            {
                g_Values[info.option] = value < 0 ? 0 : value > info.maximum ? info.maximum : value;
            }
        }
    }
}

void Save()
{
    if (!g_Saving)
    {
        return;
    }

    std::error_code error;
    std::filesystem::create_directories(Native::SettingsFolder(), error);
    std::ofstream file(SettingsPath(), std::ios::trunc);
    file << "# Crash Twinsanity's native options (the options menu's PC settings)\n";
    for (const OptionInfo& info : Infos)
    {
        file << info.name << "=" << g_Values[info.option] << "\n";
    }
}

bool WatchEvent(void*, SDL_Event* event)
{
    switch (event->type)
    {
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        g_FocusLost = true;
        g_FocusPauseDue = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        g_FocusLost = false;
        g_FocusPauseDue = false;
        break;
    case SDL_EVENT_KEY_DOWN:
        if (event->key.repeat)
        {
            break;
        }

        if (g_Capturing)
        {
            g_CapturedKey = static_cast<s32>(event->key.scancode);
            break;
        }

        // Alt+Enter, and Cmd+F on macOS: fullscreen or the window
        if ((event->key.scancode == SDL_SCANCODE_RETURN && (event->key.mod & SDL_KMOD_ALT) != 0) ||
            (event->key.scancode == SDL_SCANCODE_F && (event->key.mod & SDL_KMOD_GUI) != 0))
        {
            g_FullscreenToggleDue = true;
        }

        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (g_Capturing)
        {
            g_CapturedButton = static_cast<s32>(event->gbutton.button);
        }

        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        // The triggers bound like buttons: an axis pushed past half
        if (g_Capturing && event->gaxis.value > 16384 &&
            (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
        {
            g_CapturedButton = GamepadTriggerBase + event->gaxis.axis - SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        }

        break;
    default:
        break;
    }

    return true;
}
}

s32 DefaultOption(Option option)
{
    for (const OptionInfo& info : Infos)
    {
        if (info.option == option)
        {
            return info.defaultValue;
        }
    }

    return 0;
}

s32 GetOption(Option option)
{
    Read();
    return g_Values[option];
}

bool IsOn(Option option)
{
    return GetOption(option) != 0;
}

void SetOption(Option option, s32 value)
{
    Read();
    if (g_Values[option] == value)
    {
        return;
    }

    g_Values[option] = value;
    Save();
    ApplyOption(option);
}

void LogOptions()
{
    Read();
    // Two lines of options.txt's names and values, in its order
    std::string halves[2];
    for (u32 i = 0; i < OptionCount; i++)
    {
        std::string& half = halves[i < OptionCount / 2 ? 0 : 1];
        half += (half.empty() ? "" : " ") + std::string(Infos[i].name) + "=" + std::to_string(g_Values[Infos[i].option]);
    }

    Native::Log("options (%s): %s", g_FromFile ? "options.txt's" : "the defaults: no options.txt yet", halves[0].c_str());
    Native::Log("options, the rest: %s", halves[1].c_str());
}

void SetOptionSaving(bool saving)
{
    g_Saving = saving;
}

void ResetOptionsForTest()
{
    g_Read = false;
    Read();
}

void StartOptions()
{
    Read();
    if (!g_Watching)
    {
        g_Watching = true;
        SDL_AddEventWatch(WatchEvent, nullptr);
        StartMouse();
        // The mouse in play: the camera's stick, the wheel's keys
        StartMouseLook();
        // The UI's overlay at the window's resolution (the options screen, the menus' bar of prompts, the cursor)
        StartOverlay();
    }

    ApplyStartOptions();
}

bool TakeFocusPause()
{
    return IsOn(OptionPauseOnFocusLoss) && g_FocusPauseDue.exchange(false);
}

void BeginCapture()
{
    g_CapturedKey = -1;
    g_CapturedButton = -1;
    g_Capturing = true;
}

bool Capturing()
{
    return g_Capturing;
}

bool TakeCapture(s32* key, s32* button)
{
    *key = g_CapturedKey.exchange(-1);
    *button = g_CapturedButton.exchange(-1);
    if (*key < 0 && *button < 0)
    {
        return false;
    }

    g_Capturing = false;
    return true;
}

void CancelCapture()
{
    g_Capturing = false;
}

void Frame()
{
    UiShotsFrame();
    TrailFrame();
    SweepFrame();
    SaveTestFrame();
    HintsTestFrame();
    FpsTestFrame();
    LookTestFrame();
    PhysicsTestFrame();
    SettingsShotsFrame();
    PortableTestFrame();
    MouseFrame();
    ApplyGameSettingsOnce();
    SettingsFrame();
    // The window holds the mouse while the game is played (mouse look)
    MouseLookFrame();

    // Fullscreen asked for from the keyboard
    if (g_FullscreenToggleDue.exchange(false))
    {
        Resolution resolution = CurrentResolution();
        resolution.fullscreen = !resolution.fullscreen;
        SetResolution(resolution);
    }

    // The sound muted while the window isn't the one in front (only while the option pauses)
    bool mute = g_FocusLost && IsOn(OptionMuteInactive);
    if (mute != g_Muted)
    {
        g_Muted = mute;
        NativeAudioSetMuted(mute);
    }

    // The 60 Hz mode: the clock's budget and the display's pacing, every frame (the renderer sets PAL's again when it sets the
    // display up)
    if (IsOn(OptionRefresh60))
    {
        ApplyOption(OptionRefresh60);
    }
}

void SimulateFocus(bool focused)
{
    SDL_Event event = {};
    event.type = focused ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST;
    WatchEvent(nullptr, &event);
}

void SimulateKey(s32 scancode, u32 modifiers)
{
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = static_cast<SDL_Scancode>(scancode);
    event.key.mod = static_cast<SDL_Keymod>(modifiers);
    WatchEvent(nullptr, &event);
}

bool FullscreenToggleDue()
{
    return g_FullscreenToggleDue;
}
}

// The disc read at the computer's speed rather than a PS2 drive's (iop.cpp's sceCdRead), with fast loading on
bool NativeFastDisc()
{
    return NativeUi::IsOn(NativeUi::OptionFastLoading);
}
