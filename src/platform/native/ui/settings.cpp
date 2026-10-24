// The settings screen's logic: the options in tabs (gameplay, display, audio, controls, accessibility) as the newer Crash games on a
// computer have them (native/research/MODERN_CRASH_UI.md), its view apart (settingsview.cpp, on a canvas), opened in place of
// the old options pages (olegpages.cpp's options item, under TWIN_NATIVE). Each row is a label and its value (a choice stepped left
// and right, a slider, a toggle), the highlighted row's description under the list, every change applied at once and kept. The
// controls tab is a table of the game's actions by the keyboard's two keys and the controller's button. It takes the keyboard
// (arrows, Enter, Escape, Q/E or Tab for the tabs), the pads (the D-pad or the stick, A and B, the shoulders for the tabs) and the
// mouse (hover, click, the wheel, the tabs and the arrows, a slider dragged) while it's open; the game reads nothing meanwhile
#include "ui/settingsmodel.h"
#include "ui/overlay.h"

#include "native.h"

#include "game/controllers.h"
#include "game/gamecontroller.h"
#include "game/language.h"
#include "game/oleg.h"
#include "game/pads.h"
#include "game/renderer.h"
#include "game/sound.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// The graphics' texture and picture settings (src/platform/native/graphics/): until they're there these weak
// stand-ins do nothing (the settings are kept and given to them once they are)
// TODO(native-graphics): drop the stand-ins once these are on macos-support
__attribute__((weak)) void NativeGraphicsSetTextureUpscale(int)
{
}

__attribute__((weak)) void NativeGraphicsSetTexturePack(bool)
{
}

__attribute__((weak)) void NativeGraphicsSetAntiAliasing(int)
{
}

__attribute__((weak)) void NativeGraphicsSetVSync(bool)
{
}

namespace NativeUi
{
namespace
{
// ------------------------------------------------------------------------------------------------------------------- the rows
constexpr u32 TabTexts[Pages] = {TextTabGameplay, TextTabDisplay, TextTabAudio, TextControls, TextTabAccessibility};
constexpr u32 Columns = BindingColumns;

std::vector<Row> g_Rows[Pages];
bool g_Built = false;

std::string Text(u32 id)
{
    return GameText(id);
}

Row Choice(u32 label, u32 description, std::function<s32()> get, std::function<void(s32)> set, s32 maximum,
           std::function<std::string(s32)> valueText)
{
    Row row{RowChoice, label, description, std::move(get), std::move(set), 0, maximum, std::move(valueText), {}, 0, {}, false};
    return row;
}

Row Toggle(u32 label, u32 description, Option option)
{
    return Choice(label, description, [option] { return GetOption(option); }, [option](s32 value) { SetOption(option, value); }, 1,
                  [](s32 value) { return Text(value != 0 ? 1 : 0); });
}

Row OptionChoice(u32 label, u32 description, Option option, std::vector<u32> texts)
{
    s32 maximum = static_cast<s32>(texts.size()) - 1;
    return Choice(label, description, [option] { return GetOption(option); }, [option](s32 value) { SetOption(option, value); },
                  maximum, [texts](s32 value) { return Text(texts[static_cast<size_t>(value)]); });
}

Row Slider(u32 label, u32 description, std::function<s32()> get, std::function<void(s32)> set, s32 minimum = 0, s32 maximum = 10)
{
    Row row{RowSlider, label, description, std::move(get), std::move(set), minimum, maximum, {}, {}, 0, {}, false};
    return row;
}

Row OptionSlider(u32 label, u32 description, Option option, s32 minimum, s32 maximum)
{
    return Slider(label, description, [option] { return GetOption(option); }, [option](s32 value) { SetOption(option, value); },
                  minimum, maximum);
}

Row Action(u32 label, u32 description, std::function<void()> activate)
{
    Row row{RowAction, label, description, {}, {}, 0, 0, {}, std::move(activate), 0, {}, false};
    return row;
}

Row Binding(u32 label, u32 function)
{
    Row row{RowBinding, label, TextDescBinding, {}, {}, 0, 0, {}, {}, function, {}, false};
    return row;
}

Row Header()
{
    Row row{RowHeader, 0, TextDescBinding, {}, {}, 0, 0, {}, {}, 0, {}, false};
    return row;
}

// The game's volume groups (the sound options page's): effects 0, 1 and 3, music 2; 0 to 10
s32 GroupLevel(s32 group)
{
    return static_cast<s32>(GroupVolumeLevel(group) * 10.0f + 0.5f);
}

void SetEffects(s32 value)
{
    f32 level = static_cast<f32>(value) * 0.1f;
    SetGroupVolume(level, level, EffectsGroup);
    SetGroupVolume(level, level, SecondEffectsGroup);
    SetGroupVolume(level, level, MovieGroup);
    SetOption(OptionEffectsVolume, value);
}

void SetMusic(s32 value)
{
    f32 level = static_cast<f32>(value) * 0.1f;
    SetGroupVolume(level, level, MusicGroup);
    SetOption(OptionMusicVolume, value);
}

// The game's vibration setting (its pads' flag) and the option that gives it to the game at start-up
void SetVibration(s32 value)
{
    auto* pads = static_cast<GamePadController*>(G_GamePadController);
    if (pads != nullptr)
    {
        if (value != 0)
        {
            pads->flags.vibration = 1;
        }
        else
        {
            pads->DisableVibration();
        }
    }

    SetOption(OptionVibration, value);
}

void OpenScreenPosition();
void SelectTab(u32 tab);
void AskPortable(bool portable);

// The display mode (window or fullscreen) and the window's size: the windows' sizes are the display choices but the last
s32 WindowSizeChoice()
{
    Resolution current = CurrentResolution();
    current.fullscreen = false;
    return DisplayChoice(current);
}

void Build()
{
    if (g_Built)
    {
        return;
    }

    g_Built = true;
    std::vector<Row>& gameplay = g_Rows[TabGameplay];
    gameplay.push_back(Choice(
        GameTextVibration, TextDescVibration,
        [] {
            auto* pads = static_cast<GamePadController*>(G_GamePadController);
            return pads != nullptr && pads->flags.vibration != 0 ? 1 : 0;
        },
        SetVibration, 1, [](s32 value) { return Text(value != 0 ? 1 : 0); }));
    gameplay.push_back(OptionChoice(TextGameLanguage, TextDescGameLanguage, OptionLanguage,
                                    {TextLanguageWindows, TextLanguageEnglish, TextLanguageFrench, TextLanguageGerman,
                                     TextLanguageItalian, TextLanguageSpanish}));
    gameplay.push_back(Toggle(TextSkipCutscenes, TextDescSkip, OptionSkipCutscenes));
    gameplay.push_back(Toggle(TextFastLoading, TextDescFastLoading, OptionFastLoading));
    gameplay.push_back(Toggle(TextBugFixes, TextDescBugFixes, OptionRetailFixes));
    gameplay.push_back(Toggle(TextPauseInactive, TextDescPauseInactive, OptionPauseOnFocusLoss));
    gameplay.push_back(Toggle(TextDetailedLog, TextDescDetailedLog, OptionDetailedLog));
    // Portable mode (portable.cpp; not an option of options.txt, which is in the folder it chooses): the mode in use, and stepping
    // it asks to switch (the dialog), the switch taking effect from the next start
    Row portable = Choice(
        TextPortable, TextDescPortable, [] { return Native::PortableMode() ? 1 : 0; }, [](s32 value) { AskPortable(value != 0); }, 1,
        [](s32 value) { return Text(value != 0 ? GameTextYes : GameTextNo); });
    portable.shown = [] { return Native::PortableSupported(); };
    gameplay.push_back(portable);

    std::vector<Row>& display = g_Rows[TabDisplay];
    Row mode = Choice(
        TextDisplayMode, TextDescDisplayMode, [] { return CurrentResolution().fullscreen ? 1 : 0; },
        [](s32 value) {
            Resolution resolution = CurrentResolution();
            resolution.fullscreen = value != 0;
            SetResolution(resolution);
        },
        1, [](s32 value) { return Text(value != 0 ? TextFullscreen : TextWindowed); });
    mode.asksToKeep = true;
    display.push_back(mode);
    Row size = Choice(
        TextWindowSize, TextDescWindowSize, [] { return WindowSizeChoice(); },
        [](s32 value) { SetResolution(FromChoices(ScaleChoice(CurrentResolution()), value)); }, DisplayChoices - 2,
        [](s32 value) { return Text(TextWindow960 + static_cast<u32>(value)); });
    size.asksToKeep = true;
    size.shown = [] { return !CurrentResolution().fullscreen; };
    display.push_back(size);
    display.push_back(Choice(
        TextResolution, TextDescResolution, [] { return ScaleChoice(CurrentResolution()); },
        [](s32 value) {
            Resolution resolution = CurrentResolution();
            resolution.scale = value + 1;
            SetResolution(resolution);
        },
        ScaleChoices - 1, [](s32 value) {
            // The scale and the picture's size it draws at (the PAL frame's 640 x 512 times it): "2×  ·  1280 × 1024"
            s32 scale = value + 1;
            return Text(TextScale1 + static_cast<u32>(value)) + "  \xB7  " + std::to_string(640 * scale) + "x" +
                   std::to_string(512 * scale);
        }));
    display.push_back(Choice(
        // The TV's shape: 4:3, 16:9 (the game's widescreen) or 21:9 (the game's widescreen made wider, fixes.cpp's Ultrawide)
        GameTextWidescreen, TextDescWidescreen,
        [] { return g_WidescreenTv == 0 ? 0 : IsOn(OptionUltrawide) ? 2 : 1; },
        [](s32 value) {
            g_WidescreenTv = static_cast<u8>(value != 0 ? 1 : 0);
            SetOption(OptionWidescreen, value != 0 ? 1 : 0);
            SetOption(OptionUltrawide, value == 2 ? 1 : 0);
        },
        2, [](s32 value) { return std::string(value == 0 ? "4:3" : value == 1 ? "16:9" : "21:9"); }));
    display.push_back(OptionChoice(TextRefreshRate, TextDescRefreshRate, OptionRefresh60,
                                   {Text50Hz, Text60Hz, Text120Hz, Text144Hz, TextUnlimited}));
    display.push_back(OptionChoice(TextFiltering, TextDescFiltering, OptionTextureFilter, {TextSharp, TextSmooth}));
    display.push_back(Toggle(TextCrt, TextDescCrt, OptionCrtFilter));
    display.push_back(Toggle(TextTexturePack, TextDescTexturePack, OptionTexturePack));
    // (The texture upscale isn't shown until the graphics have it: NativeGraphicsSetTextureUpscale takes only off.) The
    // anti-aliasing is FXAA on the shown picture; there's no MSAA: PCSX2 dropped its own, and the render scale is the
    // supersampling
    display.push_back(OptionChoice(TextAntiAliasing, TextDescAntiAliasing, OptionAntiAliasing, {0, TextFxaa}));
    display.push_back(Toggle(TextVSync, TextDescVSync, OptionVSync));
    display.push_back(Toggle(TextShowFps, TextDescShowFps, OptionShowFps));
    display.push_back(Action(GameTextCentreScreen, TextDescScreenPosition, [] { OpenScreenPosition(); }));

    std::vector<Row>& audio = g_Rows[TabAudio];
    audio.push_back(Slider(GameTextMusicVolume, TextDescMusic, [] { return GroupLevel(MusicGroup); }, SetMusic));
    audio.push_back(Slider(GameTextEffectsVolume, TextDescEffects, [] { return GroupLevel(EffectsGroup); }, SetEffects));
    audio.push_back(OptionSlider(TextVoiceVolume, TextDescVoices, OptionVoiceVolume, 0, 10));
    audio.push_back(Choice(
        GameTextOutputType, TextDescOutput, [] { return static_cast<s32>(g_MusicStereo); },
        [](s32 value) {
            SetMusicStereo(static_cast<u32>(value));
            SetOption(OptionOutputType, value);
        },
        2, [](s32 value) { return Text(GameTextMono + static_cast<u32>(value)); }));
    audio.push_back(Toggle(TextMuteInactive, TextDescMuteInactive, OptionMuteInactive));

    std::vector<Row>& controls = g_Rows[TabControls];
    controls.push_back(OptionChoice(TextController, TextDescController, OptionController,
                                    {TextAutomatic, TextKeyboard, TextXbox, TextPlayStation, TextSwitch}));
    Row layout = OptionChoice(TextNintendoLayout, TextDescLayout, OptionNintendoLayout, {TextPositional, TextNintendo});
    layout.shown = [] { return CurrentPromptStyle() == PromptSwitch; };
    controls.push_back(layout);
    controls.push_back(OptionChoice(TextMenuHints, TextDescMenuHints, OptionMenuHints, {TextHintsOriginal, TextHintsBar}));
    controls.push_back(Toggle(TextMouse, TextDescMouse, OptionMouse));
    controls.push_back(Toggle(TextMouseLook, TextDescMouseLook, OptionMouseLook));
    Row sensitivity = OptionSlider(TextMouseSensitivity, TextDescMouseSensitivity, OptionMouseSensitivity, 1, 10);
    sensitivity.shown = [] { return IsOn(OptionMouseLook); };
    controls.push_back(sensitivity);
    Row invertMouse = Toggle(TextInvertMouseY, TextDescInvertMouseY, OptionInvertMouseY);
    invertMouse.shown = [] { return IsOn(OptionMouseLook); };
    controls.push_back(invertMouse);

    std::vector<Row>& access = g_Rows[TabAccessibility];
    access.push_back(Toggle(TextCameraShake, TextDescCameraShake, OptionCameraShake));
    access.push_back(Toggle(TextInvertX, TextDescInvertX, OptionInvertCameraX));
    access.push_back(Toggle(TextInvertY, TextDescInvertY, OptionInvertCameraY));
    access.push_back(OptionSlider(TextCameraSpeed, TextDescCameraSpeed, OptionCameraSpeed, 1, 10));
    access.push_back(OptionSlider(TextDeadZone, TextDescDeadZone, OptionStickDeadZone, 0, 10));

    // The table: the actions in the research's order by key, alt key and the controller's button
    constexpr struct
    {
        u32 label;
        u32 function;
    } Actions[] = {
        {TextMoveUp, BindMoveForward},     {TextMoveDown, BindMoveBack},       {TextMoveLeft, BindMoveLeft},
        {TextMoveRight, BindMoveRight},    {TextCameraUp, BindCameraUp},       {TextCameraDown, BindCameraDown},
        {TextCameraLeft, BindCameraLeft},  {TextCameraRight, BindCameraRight}, {TextJump, BindJump},
        {TextSpin, BindSpin},              {TextCrouch, BindCrouch},           {TextShoulderLeft, BindShoulderLeft},
        {TextShoulderRight, BindShoulderRight}, {TextStatus, BindStatus},     {TextPause, BindPause},
        {TextWalk, BindWalk},
    };
    controls.push_back(Header());
    for (const auto& action : Actions)
    {
        controls.push_back(Binding(action.label, action.function));
    }

    controls.push_back(Action(TextResetKeyboard, TextDescResetKeyboard, ResetKeyboardBindings));
    controls.push_back(Action(TextResetController, TextDescResetController, ResetControllerBindings));

    for (u32 tab = 0; tab < TabCount; tab++)
    {
        if (tab != TabControls)
        {
            g_Rows[tab].push_back(Action(TextResetDefaults, TextDescResetDefaults, nullptr));
        }
    }
}

bool Shown(const Row& row)
{
    return !row.shown || row.shown();
}

bool Selectable(const Row& row)
{
    return row.kind != RowHeader && Shown(row);
}


// ------------------------------------------------------------------------------------------------------------------ the state
bool g_Open = false;
u32 g_Tab = TabGameplay;
s32 g_Row = 0;
u32 g_Column = 0;
s32 g_Scroll = 0;
// Waiting for a key or button for the binding row's column (g_Row, g_Column)
bool g_Capturing = false;
// The rows swapped last (flashed for a moment)
s32 g_FlashRows[2] = {-1, -1};
std::chrono::steady_clock::time_point g_FlashStart;
// The screen to show again when it's closed
u32 g_ReturnScreen = 0;
bool g_ScreenPosition = false;
// A display change waiting to be kept (the revert countdown: display mode and window size only)
bool g_Keeping = false;
Resolution g_KeepPrevious = {};
std::chrono::steady_clock::time_point g_KeepStart;
constexpr f32 KeepSeconds = 15.0f;
// Portable mode's dialog, whether it's turning it on, and when the copy was done (the game closes on select, or by itself)
PortableDialog g_Dialog = DialogNone;
bool g_DialogPortable = false;
std::chrono::steady_clock::time_point g_DoneAt;
constexpr f32 CloseSeconds = 10.0f;

// Input since the last frame
enum UiAction : u32
{
    UiUp,
    UiDown,
    UiLeft,
    UiRight,
    UiConfirm,
    UiBack,
    UiTabPrevious,
    UiTabNext,
    UiClear,
    UiActions,
};
u32 g_Actions = 0;
// Captured for a binding
s32 g_CapturedKey = -1;
s32 g_CapturedButton = -1;
// The mouse
f32 g_MouseX = -1.0f;
f32 g_MouseY = -1.0f;
bool g_MouseMoved = false;
bool g_MouseDown = false;
bool g_MouseClicked = false;
bool g_MouseRightClicked = false;
s32 g_Wheel = 0;
bool g_Dragging = false;
// The stick's and the D-pad's repeat (400 ms, then every 80 ms)
std::chrono::steady_clock::time_point g_RepeatAt;
s32 g_HeldDirection = -1;

void Act(UiAction action)
{
    g_Actions |= 1u << action;
}

bool Took(UiAction action)
{
    return (g_Actions & (1u << action)) != 0;
}

// The selected cell is the controller's (else a key's)
bool OnPadColumn()
{
    return g_Column == ColumnPad;
}

// Changes of the page (the view's cross-fade)
u32 g_PageChanges = 0;

bool WatchSettings(void*, SDL_Event* event)
{
    if (!g_Open)
    {
        return true;
    }

    switch (event->type)
    {
    case SDL_EVENT_KEY_DOWN:
    {
        if (g_Capturing)
        {
            if (!event->key.repeat)
            {
                g_CapturedKey = static_cast<s32>(event->key.scancode);
            }

            break;
        }

        NoteInputDevice(true, nullptr, LastGamepad());
        switch (event->key.scancode)
        {
        case SDL_SCANCODE_UP:
        case SDL_SCANCODE_W:
            Act(UiUp);
            break;
        case SDL_SCANCODE_DOWN:
        case SDL_SCANCODE_S:
            Act(UiDown);
            break;
        case SDL_SCANCODE_LEFT:
        case SDL_SCANCODE_A:
            Act(UiLeft);
            break;
        case SDL_SCANCODE_RIGHT:
        case SDL_SCANCODE_D:
            Act(UiRight);
            break;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER:
        case SDL_SCANCODE_SPACE:
            if (!event->key.repeat && (event->key.mod & SDL_KMOD_ALT) == 0)
            {
                Act(UiConfirm);
            }

            break;
        case SDL_SCANCODE_ESCAPE:
            if (!event->key.repeat)
            {
                Act(UiBack);
            }

            break;
        case SDL_SCANCODE_BACKSPACE:
        case SDL_SCANCODE_DELETE:
            // On a binding's cell it clears it; elsewhere Backspace goes back
            if (!event->key.repeat)
            {
                Act(g_Tab == TabControls ? UiClear : event->key.scancode == SDL_SCANCODE_BACKSPACE ? UiBack : UiClear);
            }

            break;
        case SDL_SCANCODE_Q:
        case SDL_SCANCODE_PAGEUP:
            Act(UiTabPrevious);
            break;
        case SDL_SCANCODE_E:
        case SDL_SCANCODE_PAGEDOWN:
            Act(UiTabNext);
            break;
        case SDL_SCANCODE_TAB:
            Act((event->key.mod & SDL_KMOD_SHIFT) != 0 ? UiTabPrevious : UiTabNext);
            break;
        default:
            break;
        }

        break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    {
        SDL_Gamepad* gamepad = SDL_GetGamepadFromID(event->gbutton.which);
        if (g_Capturing)
        {
            g_CapturedButton = static_cast<s32>(event->gbutton.button);
            break;
        }

        NoteInputDevice(false, gamepad, gamepad);
        // Select on the bottom button and back on the right one (triangle's place too, the original's back), or a Nintendo pad's
        // own layout: A, the right button, selects and B, the bottom one, goes back
        bool nintendo = CurrentPromptStyle() == PromptSwitch && GetOption(OptionNintendoLayout) != 0;
        switch (event->gbutton.button)
        {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            Act(UiUp);
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            Act(UiDown);
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            Act(UiLeft);
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            Act(UiRight);
            break;
        case SDL_GAMEPAD_BUTTON_SOUTH:
            Act(nintendo ? UiBack : UiConfirm);
            break;
        case SDL_GAMEPAD_BUTTON_EAST:
            Act(nintendo ? UiConfirm : UiBack);
            break;
        case SDL_GAMEPAD_BUTTON_NORTH:
            Act(UiBack);
            break;
        case SDL_GAMEPAD_BUTTON_WEST:
            Act(UiClear);
            break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            Act(UiTabPrevious);
            break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            Act(UiTabNext);
            break;
        case SDL_GAMEPAD_BUTTON_START:
            Act(UiBack);
            break;
        default:
            break;
        }

        g_HeldDirection = -1;
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (g_Capturing && event->gaxis.value > 16384 &&
            (event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
        {
            g_CapturedButton = GamepadTriggerBase + event->gaxis.axis - SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
        }

        break;
    case SDL_EVENT_MOUSE_MOTION:
        WindowToReference(event->motion.x, event->motion.y, &g_MouseX, &g_MouseY);
        g_MouseMoved = true;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        WindowToReference(event->button.x, event->button.y, &g_MouseX, &g_MouseY);
        if (g_Capturing)
        {
            // A mouse button (1 to 5) binds a key cell (the mouse's buttons as keys); elsewhere a click cancels
            if (!OnPadColumn() && MouseButtonKey(event->button.button) >= 0)
            {
                g_CapturedKey = MouseButtonKey(event->button.button);
            }
            else
            {
                g_Capturing = false;
            }

            break;
        }

        if (event->button.button == SDL_BUTTON_LEFT)
        {
            g_MouseClicked = true;
            g_MouseDown = true;
        }
        else if (event->button.button == SDL_BUTTON_RIGHT)
        {
            g_MouseRightClicked = true;
        }

        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT)
        {
            g_MouseDown = false;
            g_Dragging = false;
        }

        break;
    case SDL_EVENT_MOUSE_WHEEL:
    {
        f32 y = event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
        // A turn binds a key cell to the wheel's up or down
        if (g_Capturing)
        {
            if (!OnPadColumn() && y != 0.0f)
            {
                g_CapturedKey = y > 0.0f ? KeyWheelUp : KeyWheelDown;
            }

            break;
        }

        g_Wheel += event->wheel.y > 0 ? 1 : event->wheel.y < 0 ? -1 : 0;
        break;
    }
    default:
        break;
    }

    return true;
}

// The stick and a held D-pad step as the keyboard's repeat does
void PollHeldDirections()
{
    SDL_Gamepad* gamepad = LastGamepad();
    if (gamepad == nullptr)
    {
        g_HeldDirection = -1;
        return;
    }

    s16 x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    s16 y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
    constexpr s16 Pushed = 20000;
    bool up = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    bool down = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    bool left = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    bool right = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    s32 direction = y < -Pushed || up ? UiUp : y > Pushed || down ? UiDown : x < -Pushed || left ? UiLeft : x > Pushed || right ? UiRight : -1;
    auto now = std::chrono::steady_clock::now();
    if (direction < 0)
    {
        g_HeldDirection = -1;
        return;
    }

    if (direction != g_HeldDirection)
    {
        // The D-pad's first press is its event's; the stick's first push is here
        if (!(up || down || left || right))
        {
            NoteInputDevice(false, gamepad, gamepad);
            Act(static_cast<UiAction>(direction));
        }

        g_HeldDirection = direction;
        g_RepeatAt = now + std::chrono::milliseconds(400);
        return;
    }

    if (now >= g_RepeatAt)
    {
        Act(static_cast<UiAction>(direction));
        g_RepeatAt = now + std::chrono::milliseconds(80);
    }
}

// The hit regions the view recorded last frame (reference pixels: the overlay's 1280 x 720)
std::vector<HitRegion> g_Regions;

// ----------------------------------------------------------------------------------------------------------- the rows' values
std::vector<s32> VisibleIndexes()
{
    std::vector<s32> indexes;
    std::vector<Row>& rows = g_Rows[g_Tab];
    for (s32 i = 0; i < static_cast<s32>(rows.size()); i++)
    {
        if (Shown(rows[static_cast<size_t>(i)]))
        {
            indexes.push_back(i);
        }
    }

    return indexes;
}

// Options back to their defaults (the first start's: options.cpp's table)
void ResetOptions(std::initializer_list<Option> options)
{
    for (Option option : options)
    {
        SetOption(option, DefaultOption(option));
    }
}

// A tab's settings back to the defaults, the same as a first start's
void ResetTab(u32 tab)
{
    switch (tab)
    {
    case TabDisplay:
        SetResolution(DefaultResolution());
        g_WidescreenTv = static_cast<u8>(DefaultOption(OptionWidescreen) != 0 ? 1 : 0);
        ResetOptions({OptionWidescreen, OptionUltrawide, OptionRefresh60, OptionTextureFilter, OptionCrtFilter, OptionTexturePack,
                      OptionTextureUpscale, OptionAntiAliasing, OptionVSync});
        break;
    case TabAudio:
        SetMusic(DefaultOption(OptionMusicVolume));
        SetEffects(DefaultOption(OptionEffectsVolume));
        SetMusicStereo(static_cast<u32>(DefaultOption(OptionOutputType)));
        ResetOptions({OptionVoiceVolume, OptionOutputType, OptionMuteInactive});
        break;
    case TabGameplay:
        SetVibration(DefaultOption(OptionVibration));
        ResetOptions({OptionLanguage, OptionSkipCutscenes, OptionFastLoading, OptionRetailFixes, OptionPauseOnFocusLoss,
                      OptionDetailedLog});
        break;
    case TabAccessibility:
        ResetOptions({OptionCameraShake, OptionInvertCameraX, OptionInvertCameraY, OptionCameraSpeed, OptionStickDeadZone});
        break;
    default:
        break;
    }
}

void StartKeeping(const Resolution& previous)
{
    g_Keeping = true;
    g_KeepPrevious = previous;
    g_KeepStart = std::chrono::steady_clock::now();
}

void EndKeeping(bool keep)
{
    if (!keep)
    {
        SetResolution(g_KeepPrevious);
    }

    g_Keeping = false;
}

void Step(Row& row, s32 by)
{
    if (row.kind != RowChoice && row.kind != RowSlider)
    {
        return;
    }

    s32 value = row.get();
    s32 next = value + by;
    if (row.kind == RowChoice)
    {
        // Choices wrap round
        next = next > row.maximum ? row.minimum : next < row.minimum ? row.maximum : next;
    }
    else
    {
        next = next > row.maximum ? row.maximum : next < row.minimum ? row.minimum : next;
    }

    if (next != value)
    {
        Resolution before = CurrentResolution();
        row.set(next);
        if (row.asksToKeep)
        {
            StartKeeping(before);
        }
    }
}

void Activate(Row& row)
{
    switch (row.kind)
    {
    case RowChoice:
        Step(row, 1);
        break;
    case RowAction:
        if (row.activate)
        {
            row.activate();
        }
        else
        {
            ResetTab(g_Tab);
        }

        break;
    case RowBinding:
        if (BindingCellBindable(row.function, g_Column))
        {
            g_Capturing = true;
            g_CapturedKey = -1;
            g_CapturedButton = -1;
        }

        break;
    default:
        break;
    }
}

void MoveRow(s32 by)
{
    std::vector<s32> visible = VisibleIndexes();
    if (visible.empty())
    {
        return;
    }

    s32 at = 0;
    for (s32 i = 0; i < static_cast<s32>(visible.size()); i++)
    {
        if (visible[static_cast<size_t>(i)] == g_Row)
        {
            at = i;
        }
    }

    // Wrapping round at the ends
    for (s32 tries = 0; tries < static_cast<s32>(visible.size()); tries++)
    {
        at = (at + by + static_cast<s32>(visible.size())) % static_cast<s32>(visible.size());
        if (Selectable(g_Rows[g_Tab][static_cast<size_t>(visible[static_cast<size_t>(at)])]))
        {
            g_Row = visible[static_cast<size_t>(at)];
            return;
        }
    }
}

void SelectTab(u32 tab)
{
    if (tab != g_Tab)
    {
        g_PageChanges++;
    }

    g_Tab = tab;
    g_Row = 0;
    g_Scroll = 0;
    g_Column = 0;
    g_Capturing = false;
    if (!Selectable(g_Rows[g_Tab][0]))
    {
        MoveRow(1);
    }
}

void Close()
{
    if (g_Keeping)
    {
        EndKeeping(false);
    }

    g_Open = false;
    g_Capturing = false;
    GameController* controller = G_GameController;
    OLEG& oleg = controller->oleg;
    s32 fade = static_cast<s32>(g_ClockUnitsPerSecond * 0.5f);
    oleg.Hide(oleg.screens[OLEG::ScreenAllButOverlays], fade, 0);
    oleg.Show(oleg.screens[g_ReturnScreen], fade, 0);
}

void OpenScreenPosition()
{
    // The game's own screen position page (its back comes here again: ScreenPositionClosed)
    g_Open = false;
    g_ScreenPosition = true;
    GameController* controller = G_GameController;
    OLEG& oleg = controller->oleg;
    s32 fade = static_cast<s32>(g_ClockUnitsPerSecond * 0.5f);
    oleg.Hide(oleg.screens[OLEG::ScreenAllButOverlays], fade, 0);
    oleg.Show(oleg.screens[OLEG::ScreenScreenPosition], fade, 0);
}

// ------------------------------------------------------------------------------------------------------------- the frame's input
void TakeCapture()
{
    Row& row = g_Rows[g_Tab][static_cast<size_t>(g_Row)];
    s32 swapped = -1;
    if (g_CapturedKey == SDL_SCANCODE_ESCAPE)
    {
        g_Capturing = false;
        return;
    }

    if (OnPadColumn())
    {
        if (g_CapturedButton < 0)
        {
            return;
        }

        swapped = BindButtonSlot(row.function, 0, g_CapturedButton);
    }
    else
    {
        if (g_CapturedKey < 0)
        {
            return;
        }

        swapped = BindKeySlot(row.function, g_Column, g_CapturedKey);
    }

    g_Capturing = false;
    g_FlashRows[0] = -1;
    g_FlashRows[1] = -1;
    if (swapped >= 0)
    {
        // The two rows flash
        g_FlashRows[0] = g_Row;
        for (s32 i = 0; i < static_cast<s32>(g_Rows[g_Tab].size()); i++)
        {
            const Row& other = g_Rows[g_Tab][static_cast<size_t>(i)];
            if (other.kind == RowBinding && static_cast<s32>(other.function) == swapped)
            {
                g_FlashRows[1] = i;
            }
        }

        g_FlashStart = std::chrono::steady_clock::now();
    }
}

struct Hit
{
    s32 row = -1;
    s32 column = -1;
    s32 tab = -1;
    // -1 the left arrow, 1 the right arrow, 2 the slider's bar
    s32 part = 0;
    const HitRegion* region = nullptr;
};

// The mouse against the regions the view recorded (the last one drawn over the others wins)
Hit HitTest()
{
    Hit hit;
    f32 x = g_MouseX;
    f32 y = g_MouseY;
    for (const HitRegion& region : g_Regions)
    {
        if (!region.Contains(x, y))
        {
            continue;
        }

        switch (region.kind)
        {
        case HitKind::Tab:
            hit.tab = region.tab;
            break;
        case HitKind::Row:
            hit.row = region.row;
            break;
        case HitKind::Cell:
            hit.row = region.row;
            hit.column = region.column;
            break;
        case HitKind::ArrowLeft:
            hit.row = region.row;
            hit.part = -1;
            break;
        case HitKind::ArrowRight:
            hit.row = region.row;
            hit.part = 1;
            break;
        case HitKind::Slider:
            hit.row = region.row;
            hit.part = 2;
            break;
        default:
            break;
        }

        hit.region = &region;
    }

    return hit;
}

// The slider's region of a row (its bar's ends), for a drag
const HitRegion* SliderRegion(s32 row)
{
    for (const HitRegion& region : g_Regions)
    {
        if (region.kind == HitKind::Slider && region.row == row)
        {
            return &region;
        }
    }

    return nullptr;
}

void Back()
{
    Close();
}

// ------------------------------------------------------------------------------------------------------------- portable mode
// The switch asked about (Gameplay's row stepped), or refused at once when the program's folder can't be written to
void AskPortable(bool portable)
{
    g_DialogPortable = portable;
    g_Dialog = DialogAsk;
    std::string why;
    if (portable && !Native::PortableFolderUsable(&why))
    {
        Native::Log("portable mode can't be turned on: the program's folder %s", why.c_str());
        g_Dialog = DialogRefused;
    }
}

// The dialog's input (select or its yes, back or its no) and the copy followed: asked, yes copies; refused and failed go away on
// either; done closes the game on either, or by itself after a few seconds (the old folder gets nothing more either way)
void TakeDialogInput()
{
    Hit hit = HitTest();
    bool clicked = g_MouseClicked && hit.region != nullptr;
    bool yes = Took(UiConfirm) || (clicked && hit.region->kind == HitKind::DialogYes);
    bool no = Took(UiBack) || g_MouseRightClicked || (clicked && hit.region->kind == HitKind::DialogNo);
    switch (g_Dialog)
    {
    case DialogAsk:
        if (yes)
        {
            Native::StartPortableSwitch(g_DialogPortable);
            g_Dialog = DialogCopying;
        }
        else if (no)
        {
            g_Dialog = DialogNone;
        }

        break;
    case DialogCopying:
        switch (Native::PortableSwitchState(nullptr, nullptr, nullptr, nullptr))
        {
        case Native::PortableSwitchDone:
            g_Dialog = DialogDone;
            g_DoneAt = std::chrono::steady_clock::now();
            break;
        case Native::PortableSwitchFailed:
            g_Dialog = DialogFailed;
            break;
        default:
            break;
        }

        break;
    case DialogDone:
        if (yes || no || std::chrono::duration<f32>(std::chrono::steady_clock::now() - g_DoneAt).count() >= CloseSeconds)
        {
            Native::QuitAfterPortableSwitch();
        }

        break;
    case DialogRefused:
    case DialogFailed:
        if (yes || no)
        {
            g_Dialog = DialogNone;
        }

        break;
    default:
        break;
    }
}

void TakeInput()
{
    std::vector<Row>& rows = g_Rows[g_Tab];
    if (g_Capturing)
    {
        if (g_CapturedKey >= 0 || g_CapturedButton >= 0)
        {
            TakeCapture();
            g_CapturedKey = -1;
            g_CapturedButton = -1;
        }

        g_Actions = 0;
        return;
    }

    // The display's countdown: keep (select), revert (back, or the time's up)
    if (g_Keeping)
    {
        f32 waited = std::chrono::duration<f32>(std::chrono::steady_clock::now() - g_KeepStart).count();
        Hit hit = HitTest();
        bool yes = g_MouseClicked && hit.region != nullptr && hit.region->kind == HitKind::KeepYes;
        bool no = g_MouseClicked && hit.region != nullptr && hit.region->kind == HitKind::KeepNo;
        if (Took(UiConfirm) || yes)
        {
            EndKeeping(true);
        }
        else if (Took(UiBack) || no || g_MouseRightClicked || waited >= KeepSeconds)
        {
            EndKeeping(false);
        }

        g_Actions = 0;
        g_MouseClicked = false;
        g_MouseRightClicked = false;
        g_MouseMoved = false;
        g_Wheel = 0;
        return;
    }

    // Portable mode's dialog: the list takes nothing meanwhile
    if (g_Dialog != DialogNone)
    {
        TakeDialogInput();
        g_Actions = 0;
        g_MouseClicked = false;
        g_MouseRightClicked = false;
        g_MouseMoved = false;
        g_Wheel = 0;
        return;
    }

    PollHeldDirections();
    // The mouse: hovering selects, a click picks (or steps a choice by its arrow, sets a slider, picks a tab), the wheel steps the
    // hovered value or scrolls, a right click goes back
    if (IsOn(OptionMouse) && (g_MouseMoved || g_MouseClicked || g_Wheel != 0 || g_MouseDown || g_MouseRightClicked))
    {
        Hit hit = HitTest();
        if (g_MouseMoved && hit.row >= 0 && Selectable(rows[static_cast<size_t>(hit.row)]))
        {
            g_Row = hit.row;
            if (hit.column >= 0)
            {
                g_Column = static_cast<u32>(hit.column);
            }
        }

        if (g_MouseClicked)
        {
            if (hit.tab >= 0)
            {
                SelectTab(static_cast<u32>(hit.tab));
            }
            else if (hit.row >= 0 && Selectable(rows[static_cast<size_t>(hit.row)]))
            {
                Row& row = rows[static_cast<size_t>(hit.row)];
                g_Row = hit.row;
                if (row.kind == RowChoice && hit.part != 0)
                {
                    Step(row, hit.part);
                }
                else if (row.kind == RowSlider && hit.part == 2)
                {
                    g_Dragging = true;
                }
                else if (row.kind == RowBinding)
                {
                    if (hit.column >= 0)
                    {
                        g_Column = static_cast<u32>(hit.column);
                        Activate(row);
                    }
                }
                else
                {
                    Activate(row);
                }
            }
            else if (hit.region != nullptr)
            {
                // The bar's prompts
                switch (hit.region->kind)
                {
                case HitKind::FooterSelect:
                    Act(UiConfirm);
                    break;
                case HitKind::FooterBack:
                    Act(UiBack);
                    break;
                case HitKind::FooterTabs:
                    Act(UiTabNext);
                    break;
                case HitKind::FooterClear:
                    Act(UiClear);
                    break;
                default:
                    break;
                }
            }
        }

        if (g_Dragging && g_MouseDown && g_Row >= 0)
        {
            Row& row = rows[static_cast<size_t>(g_Row)];
            const HitRegion* slider = SliderRegion(g_Row);
            if (row.kind == RowSlider && slider != nullptr)
            {
                f32 fraction = (g_MouseX - slider->x) / slider->width;
                fraction = fraction < 0.0f ? 0.0f : fraction > 1.0f ? 1.0f : fraction;
                s32 value = row.minimum + static_cast<s32>(fraction * static_cast<f32>(row.maximum - row.minimum) + 0.5f);
                if (value != row.get())
                {
                    row.set(value);
                }
            }
        }

        if (g_Wheel != 0)
        {
            if (hit.row >= 0 && (rows[static_cast<size_t>(hit.row)].kind == RowChoice || rows[static_cast<size_t>(hit.row)].kind == RowSlider))
            {
                g_Row = hit.row;
                Step(rows[static_cast<size_t>(hit.row)], g_Wheel > 0 ? 1 : -1);
            }
            else
            {
                MoveRow(g_Wheel > 0 ? -1 : 1);
            }
        }

        if (g_MouseRightClicked)
        {
            Act(UiBack);
        }
    }

    g_MouseMoved = false;
    g_MouseClicked = false;
    g_MouseRightClicked = false;
    g_Wheel = 0;

    if (Took(UiTabPrevious))
    {
        SelectTab((g_Tab + TabCount - 1) % TabCount);
    }
    else if (Took(UiTabNext))
    {
        SelectTab((g_Tab + 1) % TabCount);
    }

    if (Took(UiUp))
    {
        MoveRow(-1);
    }
    else if (Took(UiDown))
    {
        MoveRow(1);
    }

    Row& row = g_Rows[g_Tab][static_cast<size_t>(g_Row)];
    if (row.kind == RowBinding)
    {
        if (Took(UiLeft) && g_Column > 0)
        {
            g_Column--;
        }
        else if (Took(UiRight) && g_Column + 1 < Columns)
        {
            g_Column++;
        }
        else if (Took(UiClear))
        {
            if (BindingCellBindable(row.function, g_Column))
            {
                if (OnPadColumn())
                {
                    ClearButtonSlot(row.function, 0);
                }
                else
                {
                    ClearKeySlot(row.function, g_Column);
                }
            }
        }
    }
    else if (Took(UiLeft))
    {
        Step(row, -1);
    }
    else if (Took(UiRight))
    {
        Step(row, 1);
    }

    if (Took(UiConfirm))
    {
        Activate(row);
    }
    else if (Took(UiBack))
    {
        Back();
    }

    g_Actions = 0;
}

}

// The model as the view sees it
SettingsView CurrentSettingsView()
{
    SettingsView view{};
    view.page = g_Tab;
    view.row = g_Row;
    view.column = g_Column;
    view.capturing = g_Capturing;
    view.keeping = g_Keeping;
    f32 waited = std::chrono::duration<f32>(std::chrono::steady_clock::now() - g_KeepStart).count();
    view.keepSecondsLeft = g_Keeping ? static_cast<s32>(KeepSeconds - waited + 0.999f) : 0;
    f32 flash = std::chrono::duration<f32>(std::chrono::steady_clock::now() - g_FlashStart).count();
    view.flashRows[0] = g_FlashRows[0];
    view.flashRows[1] = g_FlashRows[1];
    view.flashOn = flash < 0.6f && (static_cast<s32>(flash * 10.0f) & 1) == 0;
    view.pageChanges = g_PageChanges;
    view.dialog = g_Dialog;
    view.dialogPortable = g_DialogPortable;
    if (g_Dialog != DialogNone)
    {
        Native::PortableSwitchState(&view.copied, &view.copyTotal, &view.backup, &view.failure);
    }

    f32 done = std::chrono::duration<f32>(std::chrono::steady_clock::now() - g_DoneAt).count();
    view.closeSecondsLeft = g_Dialog == DialogDone ? static_cast<s32>(CloseSeconds - done + 0.999f) : 0;
    return view;
}

const std::vector<Row>& SettingsRows(u32 page)
{
    return g_Rows[page];
}

std::vector<s32> VisibleRowIndexes(u32 page)
{
    std::vector<s32> indexes;
    for (s32 i = 0; i < static_cast<s32>(g_Rows[page].size()); i++)
    {
        if (Shown(g_Rows[page][static_cast<size_t>(i)]))
        {
            indexes.push_back(i);
        }
    }

    return indexes;
}

bool RowSelectable(const Row& row)
{
    return Selectable(row);
}

u32 PageTitleText(u32 page)
{
    return TabTexts[page];
}

std::string BindingCellText(u32 function, u32 column)
{
    const NativeUi::Binding& binding = GetBinding(function);
    if (column == ColumnPad)
    {
        // The sticks' directions (fixed): "left stick up" ...
        if (function >= BindMoveForward && function <= BindCameraRight)
        {
            return Text(TextLeftStickUp + (function - BindMoveForward));
        }

        std::string label = ButtonsBindable(function) ? PadButtonLabel(binding.button) : "";
        return label.empty() ? "-" : label;
    }

    char name[64];
    KeyName(binding.keys[column], name, sizeof(name));
    return name;
}

bool BindingCellBindable(u32 function, u32 column)
{
    return column == ColumnPad ? ButtonsBindable(function) : KeysBindable(function);
}

s32& SettingsScroll()
{
    return g_Scroll;
}

void OpenSettings()
{
    Build();
    GameController* controller = G_GameController;
    if (controller == nullptr)
    {
        return;
    }

    static bool watching = false;
    if (!watching)
    {
        watching = true;
        SDL_AddEventWatch(WatchSettings, nullptr);
    }

    g_Open = true;
    g_Actions = 0;
    g_Capturing = false;
    g_Regions.clear();
    g_ReturnScreen = controller->State() == GameController::StateMainMenu ? OLEG::ScreenMainMenu : OLEG::ScreenPauseMenu;
    OLEG& oleg = controller->oleg;
    s32 fade = static_cast<s32>(g_ClockUnitsPerSecond * 0.5f);
    oleg.Hide(oleg.screens[OLEG::ScreenAllButOverlays], fade, 0);
    if (!Selectable(g_Rows[g_Tab][static_cast<size_t>(g_Row)]))
    {
        MoveRow(1);
    }
}

bool SettingsOpen()
{
    return g_Open;
}

bool ScreenPositionClosed()
{
    // The game's screen position page went back: to the settings, not the old options page
    if (!g_ScreenPosition)
    {
        return false;
    }

    g_ScreenPosition = false;
    OpenSettings();
    return true;
}

// The update's part: the input taken against last frame's regions
void SettingsFrame()
{
    if (!g_Open || G_GameController == nullptr)
    {
        return;
    }

    TakeInput();
}

namespace
{
bool g_ScriptedCursor = false;
f32 g_ScriptedCursorX = 0.0f;
f32 g_ScriptedCursorY = 0.0f;
std::vector<HitRegion> g_MenuRegions;
}

// The draw phase's part, after OLEG's widgets: the overlay's frame recorded (the settings screen, its regions kept for the next
// frame's input; or the game's menus' bar of prompts) with the cursor over what can be clicked, and handed to the presenter
void DrawAfterOleg(Renderer*)
{
    OverlayFrame& frame = RecordingFrame();
    frame.Clear();
    f32 mouseX = -1.0f;
    f32 mouseY = -1.0f;
    if (g_ScriptedCursor)
    {
        mouseX = g_ScriptedCursorX * ReferenceWidth;
        mouseY = g_ScriptedCursorY * ReferenceHeight;
    }
    else
    {
        f32 windowX = 0.0f;
        f32 windowY = 0.0f;
        SDL_GetMouseState(&windowX, &windowY);
        WindowToReference(windowX, windowY, &mouseX, &mouseY);
    }

    bool overClickable = false;
    if (g_Open)
    {
        g_Regions.clear();
        DrawSettingsView(frame, g_Regions);
        for (const HitRegion& region : g_Regions)
        {
            overClickable = overClickable || region.Contains(mouseX, mouseY);
        }
    }
    else
    {
        g_MenuRegions.clear();
        DrawGameMenuChrome(frame, g_MenuRegions);
        overClickable = MouseOverAnyMenuLine();
    }

    // The skip's hold bar while a button is held in a scene that can be skipped
    f32 skip = SkipHoldProgress();
    if (skip >= 0.0f && !g_Open)
    {
        DrawSkipProgress(frame, skip);
    }

    // The pointer is the system's; only the scripted shots draw one (where there's no system pointer to see)
    if (g_ScriptedCursor)
    {
        DrawCursor(frame, mouseX, mouseY, overClickable);
    }

    SubmitFrame();
}

void SettingsCursorForTest(f32 x, f32 y)
{
    g_ScriptedCursor = true;
    g_ScriptedCursorX = x;
    g_ScriptedCursorY = y;
}

// The tests' and the shots': the screen driven without the window's events
void SettingsActForTest(u32 action)
{
    if (g_Capturing && action == UiBack)
    {
        g_Capturing = false;
        return;
    }

    Act(static_cast<UiAction>(action));
}

void SettingsMouseForTest(f32 x, f32 y, bool click)
{
    // Fractions of the reference space
    SettingsCursorForTest(x, y);
    g_MouseX = x * ReferenceWidth;
    g_MouseY = y * ReferenceHeight;
    g_MouseMoved = true;
    g_MouseClicked = click;
}

u32 SettingsTabForTest()
{
    return g_Tab;
}

s32 SettingsRowForTest()
{
    return g_Row;
}

u32 PortableDialogForTest()
{
    return g_Dialog;
}
}
