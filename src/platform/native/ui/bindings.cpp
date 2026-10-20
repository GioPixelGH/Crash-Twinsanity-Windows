// The controls: which keyboard keys and gamepad button stand for each of the DualShock 2's functions, chosen on the options' controls
// page (olegpages.cpp, under TWIN_NATIVE), kept in the app's settings (Native::SettingsFolder()/controls.txt) and read by the pad
// (pads.cpp's Read, through ReadBindings)
#include "ui/bindings.h"

#include "native.h"

#include "game/gamecontroller.h"
#include "game/language.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace NativeUi
{
namespace
{
// A new file for the actions' scheme (the old controls.txt bound the PS2's buttons)
constexpr const char* SettingsFile = "bindings.txt";
// The keyboard's defaults' version: a file from before the mouse scheme (no "version=2") keeps its pad buttons, its keys the new
// defaults
constexpr int KeysVersion = 3;

struct BindingInfo
{
    const char* name;
    // The PS2 report's button bits it sets (none for the sticks' directions), or the stick and its direction
    u32 button;
    s32 stick;
    Binding defaults;
};

constexpr s32 NoStick = -1;
constexpr s32 None = -1;

// The defaults (the user's keyboard and mouse scheme, over MODERN_CRASH_UI.md 3.4's where they differ): WASD move (the left stick,
// diagonals a stick's), the mouse's movement looks and turns the camera (the right stick, mouselook.cpp) and the arrows do too,
// Q and E turn the camera left and right too, Space jumps, the mouse's left button spins (J too), Left Shift crouches, skids and
// slides (K too), Z and C the shoulders, Left Ctrl toggles walking, Tab
// the status (I too), Esc pauses (Enter too). A pad's buttons by their place (a PlayStation pad's own: south cross, west square,
// east circle, north triangle), its shoulders and triggers both the shoulders, its sticks and D-pad fixed
constexpr BindingInfo Infos[BindingCount] = {
    {"jump", PadBitCross, NoStick, {{SDL_SCANCODE_SPACE, None}, SDL_GAMEPAD_BUTTON_SOUTH, None}},
    {"spin", PadBitSquare, NoStick, {{KeyMouseLeft, SDL_SCANCODE_J}, SDL_GAMEPAD_BUTTON_WEST, None}},
    {"crouch", PadBitCircle, NoStick, {{SDL_SCANCODE_LSHIFT, SDL_SCANCODE_K}, SDL_GAMEPAD_BUTTON_EAST, None}},
    {"status", PadBitTriangle, NoStick, {{SDL_SCANCODE_TAB, SDL_SCANCODE_I}, SDL_GAMEPAD_BUTTON_NORTH, None}},
    {"shoulder_left", PadBitL1 | PadBitL2, NoStick, {{SDL_SCANCODE_Z, None}, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, GamepadTriggerBase + 0}},
    {"shoulder_right", PadBitR1 | PadBitR2, NoStick,
     {{SDL_SCANCODE_C, None}, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, GamepadTriggerBase + 1}},
    {"pause", PadBitStart, NoStick, {{SDL_SCANCODE_ESCAPE, SDL_SCANCODE_RETURN}, SDL_GAMEPAD_BUTTON_START, None}},
    {"select", PadBitSelect, NoStick, {{None, None}, SDL_GAMEPAD_BUTTON_BACK, None}},
    {"l3", PadBitL3, NoStick, {{None, None}, SDL_GAMEPAD_BUTTON_LEFT_STICK, None}},
    {"r3", PadBitR3, NoStick, {{None, None}, SDL_GAMEPAD_BUTTON_RIGHT_STICK, None}},
    {"move_forward", 0, StickLeftUp, {{SDL_SCANCODE_W, None}, None, None}},
    {"move_back", 0, StickLeftDown, {{SDL_SCANCODE_S, None}, None, None}},
    {"move_left", 0, StickLeftLeft, {{SDL_SCANCODE_A, None}, None, None}},
    {"move_right", 0, StickLeftRight, {{SDL_SCANCODE_D, None}, None, None}},
    {"camera_up", 0, StickRightUp, {{SDL_SCANCODE_UP, None}, None, None}},
    {"camera_down", 0, StickRightDown, {{SDL_SCANCODE_DOWN, None}, None, None}},
    {"camera_left", 0, StickRightLeft, {{SDL_SCANCODE_Q, SDL_SCANCODE_LEFT}, None, None}},
    {"camera_right", 0, StickRightRight, {{SDL_SCANCODE_E, SDL_SCANCODE_RIGHT}, None, None}},
    {"walk", 0, NoStick, {{SDL_SCANCODE_LCTRL, None}, None, None}},
    {"dpad_up", PadBitUp, NoStick, {{None, None}, SDL_GAMEPAD_BUTTON_DPAD_UP, None}},
    {"dpad_down", PadBitDown, NoStick, {{None, None}, SDL_GAMEPAD_BUTTON_DPAD_DOWN, None}},
    {"dpad_left", PadBitLeft, NoStick, {{None, None}, SDL_GAMEPAD_BUTTON_DPAD_LEFT, None}},
    {"dpad_right", PadBitRight, NoStick, {{None, None}, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, None}},
};

Binding g_Bindings[BindingCount];
bool g_Read = false;
bool g_Saving = true;
SDL_Gamepad* g_LastGamepad = nullptr;
// The mouse's push past the stick's full deflection this read, each axis (1: none): the camera's turn's boost (ScaleCameraTurn)
f32 g_MouseBoostX = 1.0f;
f32 g_MouseBoostY = 1.0f;

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
    for (u32 i = 0; i < BindingCount; i++)
    {
        g_Bindings[i] = Infos[i].defaults;
    }

    std::ifstream file(SettingsPath());
    std::string line;
    bool currentKeys = false;
    while (std::getline(file, line))
    {
        int version = 0;
        if (std::sscanf(line.c_str(), "version=%d", &version) == 1)
        {
            currentKeys = version >= KeysVersion;
            continue;
        }

        char name[64] = {};
        int first = None;
        int second = None;
        int button = None;
        int button2 = None;
        if (line.empty() || line[0] == '#' ||
            std::sscanf(line.c_str(), "%63[^=]=%d,%d,%d,%d", name, &first, &second, &button, &button2) < 4)
        {
            continue;
        }

        for (u32 i = 0; i < BindingCount; i++)
        {
            if (std::strcmp(name, Infos[i].name) == 0)
            {
                auto valid = [](int key)
                {
                    return (key > SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT) || IsMouseKey(key) ? key : None;
                };
                auto validButton = [](int pad)
                {
                    return (pad >= 0 && pad < SDL_GAMEPAD_BUTTON_COUNT) || pad == GamepadTriggerBase || pad == GamepadTriggerBase + 1
                               ? pad
                               : None;
                };
                g_Bindings[i].keys[0] = valid(first);
                g_Bindings[i].keys[1] = valid(second);
                g_Bindings[i].button = validButton(button);
                g_Bindings[i].button2 = validButton(button2);
            }
        }
    }

    if (!currentKeys)
    {
        for (u32 i = 0; i < BindingCount; i++)
        {
            g_Bindings[i].keys[0] = Infos[i].defaults.keys[0];
            g_Bindings[i].keys[1] = Infos[i].defaults.keys[1];
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
    file << "# Crash Twinsanity's controls: action=key,second key,pad button,second pad button (SDL scancodes, 1024 to 1030 the "
            "mouse's left, right, middle, 4 and 5 buttons and its wheel up and down; SDL gamepad buttons, 256 and 257 the triggers; "
            "-1 none)\n";
    file << "version=" << KeysVersion << "\n";
    for (u32 i = 0; i < BindingCount; i++)
    {
        file << Infos[i].name << "=" << g_Bindings[i].keys[0] << "," << g_Bindings[i].keys[1] << "," << g_Bindings[i].button << ","
             << g_Bindings[i].button2 << "\n";
    }
}

u8 Clamp255(int value)
{
    return static_cast<u8>(value < 0 ? 0 : value > 255 ? 255 : value);
}

u8 StickByte(s16 value)
{
    return Clamp255((value + 32768) >> 8);
}

bool GamepadHolds(SDL_Gamepad* gamepad, s32 button, u8* pressure)
{
    if (gamepad == nullptr || button < 0)
    {
        return false;
    }

    if (button >= GamepadTriggerBase)
    {
        s16 value = SDL_GetGamepadAxis(gamepad, button == GamepadTriggerBase ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                                                                             : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        // Pressed past a quarter, its pressure how far
        if (pressure != nullptr)
        {
            *pressure = Clamp255(value >> 7);
        }

        return value > 8192;
    }

    return SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(button));
}
}

const Binding& GetBinding(u32 function)
{
    Read();
    return g_Bindings[function];
}

const char* BindingName(u32 function)
{
    return Infos[function].name;
}

void SetBinding(u32 function, const Binding& binding)
{
    Read();
    g_Bindings[function] = binding;
    Save();
}

s32 BindKeySlot(u32 function, u32 slot, s32 key)
{
    Read();
    // A key stands for one function: where it was bound already, that place gets this one's old key (a swap)
    s32 old = g_Bindings[function].keys[slot];
    s32 swapped = -1;
    for (u32 other = 0; other < BindingCount; other++)
    {
        for (u32 otherSlot = 0; otherSlot < 2; otherSlot++)
        {
            if ((other != function || otherSlot != slot) && g_Bindings[other].keys[otherSlot] == key)
            {
                g_Bindings[other].keys[otherSlot] = old;
                swapped = static_cast<s32>(other);
            }
        }
    }

    g_Bindings[function].keys[slot] = key;
    Save();
    UpdatePrompts();
    return swapped;
}

s32 BindButtonSlot(u32 function, u32 slot, s32 button)
{
    Read();
    s32* cell = slot == 0 ? &g_Bindings[function].button : &g_Bindings[function].button2;
    s32 old = *cell;
    s32 swapped = -1;
    for (u32 other = 0; other < BindingCount; other++)
    {
        for (s32* otherCell : {&g_Bindings[other].button, &g_Bindings[other].button2})
        {
            if (otherCell != cell && *otherCell == button && ButtonsBindable(other))
            {
                *otherCell = old;
                swapped = static_cast<s32>(other);
            }
        }
    }

    *cell = button;
    Save();
    UpdatePrompts();
    return swapped;
}

void ClearKeySlot(u32 function, u32 slot)
{
    Read();
    g_Bindings[function].keys[slot] = None;
    Save();
    UpdatePrompts();
}

void ClearButtonSlot(u32 function, u32 slot)
{
    Read();
    (slot == 0 ? g_Bindings[function].button : g_Bindings[function].button2) = None;
    Save();
    UpdatePrompts();
}

bool KeysBindable(u32 function)
{
    return function < BindUp && function != BindSelect && function != BindL3 && function != BindR3;
}

bool ButtonsBindable(u32 function)
{
    return function <= BindPause;
}

void ResetKeyboardBindings()
{
    Read();
    for (u32 i = 0; i < BindingCount; i++)
    {
        g_Bindings[i].keys[0] = Infos[i].defaults.keys[0];
        g_Bindings[i].keys[1] = Infos[i].defaults.keys[1];
    }

    Save();
    UpdatePrompts();
}

void ResetControllerBindings()
{
    Read();
    for (u32 i = 0; i < BindingCount; i++)
    {
        g_Bindings[i].button = Infos[i].defaults.button;
        g_Bindings[i].button2 = Infos[i].defaults.button2;
    }

    Save();
    UpdatePrompts();
}

SDL_Gamepad* LastGamepad()
{
    return g_LastGamepad;
}

void ResetBindings()
{
    Read();
    for (u32 i = 0; i < BindingCount; i++)
    {
        g_Bindings[i] = Infos[i].defaults;
    }

    Save();
    UpdatePrompts();
}

void SetBindingsSaving(bool saving)
{
    g_Saving = saving;
}

void ResetBindingsForTest()
{
    g_Read = false;
    Read();
}

// Whether the game is being played (not in a menu, paused or the settings): the keys bound to play only reach it then, and the
// arrows (the camera in play) move the menus' selection otherwise
bool InPlay()
{
    GameController* controller = G_GameController;
    return controller != nullptr && controller->State() == GameController::StatePlaying && !SettingsOpen();
}

// A stick's bytes from the directions pushed by keys (-1, 0, 1 each): all the way, a diagonal normalised as a stick's (each axis
// 1/sqrt 2 of the way)
bool g_Walking = false;

void KeySticks(s32 x, s32 y, u8* outX, u8* outY)
{
    // A diagonal all the way on both axes, as a PS2 stick's corner reaches (each axis 1/sqrt 2 read as a half push: Crash walked);
    // walking (the walk toggle, Left Ctrl) a gentle push instead
    f32 reach = g_Walking ? 0.45f : 1.0f;
    *outX = Clamp255(static_cast<int>(std::lround(128.0f + static_cast<f32>(x) * reach * 127.0f)));
    *outY = Clamp255(static_cast<int>(std::lround(128.0f + static_cast<f32>(y) * reach * 127.0f)));
    if (x < 0 && reach == 1.0f)
    {
        *outX = 0x00;
    }
    if (y < 0 && reach == 1.0f)
    {
        *outY = 0x00;
    }
}

// A stick's two bytes as the DualShock 2's would be: a gamepad's stick reports a circle (a full diagonal is 0.71 of each axis's
// throw, some pads' a little less), the DualShock 2's reaches its square's corners, and the game takes its dead zones off each
// axis and runs past a push it measured on that: a circle's diagonal walked (an Xbox Series pad's; an Xbox One pad's, a little
// past the circle, ran). The point moved out along its direction to the square: as far as the circle's edge is the square's
// edge, the axes' own throws unchanged (PCSX2 scales its axes by 1.33 by default for the same reason)
void Squared(u8* x, u8* y)
{
    f32 fx = (static_cast<f32>(*x) - 128.0f) / 127.0f;
    f32 fy = (static_cast<f32>(*y) - 128.0f) / 127.0f;
    f32 biggest = std::max(std::fabs(fx), std::fabs(fy));
    if (biggest <= 0.0f)
    {
        return;
    }

    // (On an axis the stretch is 1: the byte as it was, 0x00 still reached)
    f32 stretch = std::sqrt(fx * fx + fy * fy) / biggest;
    if (stretch <= 1.0f)
    {
        return;
    }

    *x = Clamp255(static_cast<int>(std::lround(128.0f + fx * stretch * 127.0f)));
    *y = Clamp255(static_cast<int>(std::lround(128.0f + fy * stretch * 127.0f)));
}

// A stick's byte past a dead zone (a fraction of the throw: the throw past it stretched to the whole), and scaled
u8 Shaped(u8 value, f32 deadZone, f32 scale, bool inverted)
{
    f32 centred = (static_cast<f32>(value) - 128.0f) / 127.0f;
    f32 magnitude = centred < 0.0f ? -centred : centred;
    if (magnitude <= deadZone)
    {
        return BoundPad::StickMiddle;
    }

    f32 shaped = (magnitude - deadZone) / (1.0f - deadZone) * scale;
    shaped = shaped > 1.0f ? 1.0f : shaped;
    f32 signedValue = (centred < 0.0f ? -shaped : shaped) * (inverted ? -1.0f : 1.0f);
    return Clamp255(static_cast<int>(128.0f + signedValue * 127.0f + 0.5f));
}

void ReadBindings(const bool* keys, SDL_Gamepad* gamepad, BoundPad* pad)
{
    Read();
    g_LastGamepad = gamepad;
    *pad = {};
    pad->leftX = pad->leftY = pad->rightX = pad->rightY = BoundPad::StickMiddle;
    g_MouseBoostX = 1.0f;
    g_MouseBoostY = 1.0f;
    // Nothing reaches the game while the controls page waits for the key or button to bind
    if (Capturing() || SettingsOpen())
    {
        return;
    }

    pad->buttons = UiShotsButtons() | MouseMenuButtons();
    bool playing = InPlay();
    // The mouse's buttons and wheel as keys only in play (in the menus they're the mouse's own), with the window in front (or
    // holding the mouse)
    bool mouseHere = playing && (SDL_GetMouseFocus() != nullptr || MouseLookActive());
    SDL_MouseButtonFlags mouse = mouseHere ? SDL_GetMouseState(nullptr, nullptr) : 0;
    bool wheelUp = false;
    bool wheelDown = false;
    TakeWheel(&wheelUp, &wheelDown);
    // Alt+Enter is the fullscreen toggle, not a press of what Return stands for
    bool altHeld = keys != nullptr && (keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_RALT]);
    s32 leftX = 0;
    s32 leftY = 0;
    s32 rightX = 0;
    s32 rightY = 0;
    bool keyboardUsed = false;
    bool gamepadUsed = false;
    bool walkHeld = false;
    auto keyHeld = [&](s32 key)
    {
        switch (key)
        {
        case KeyMouseLeft:
            return (mouse & SDL_BUTTON_LMASK) != 0;
        case KeyMouseRight:
            return (mouse & SDL_BUTTON_RMASK) != 0;
        case KeyMouseMiddle:
            return (mouse & SDL_BUTTON_MMASK) != 0;
        case KeyMouse4:
            return (mouse & SDL_BUTTON_X1MASK) != 0;
        case KeyMouse5:
            return (mouse & SDL_BUTTON_X2MASK) != 0;
        case KeyWheelUp:
            return mouseHere && wheelUp;
        case KeyWheelDown:
            return mouseHere && wheelDown;
        default:
            break;
        }

        return keys != nullptr && key > SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT && keys[key] &&
               !(altHeld && key == SDL_SCANCODE_RETURN);
    };

    for (u32 i = 0; i < BindingCount; i++)
    {
        const BindingInfo& info = Infos[i];
        const Binding& binding = g_Bindings[i];
        bool held = false;
        for (s32 key : binding.keys)
        {
            if (keyHeld(key))
            {
                held = true;
                keyboardUsed = true;
            }
        }

        for (s32 button : {binding.button, binding.button2})
        {
            u8 pressure = 0;
            if (GamepadHolds(gamepad, button, &pressure))
            {
                held = true;
                gamepadUsed = true;
            }

            // A trigger bound gives the shoulder its pressure
            if (button >= GamepadTriggerBase && pressure != 0)
            {
                if ((info.button & PadBitL2) != 0)
                {
                    pad->l2Pressure = pressure;
                }
                else if ((info.button & PadBitR2) != 0)
                {
                    pad->r2Pressure = pressure;
                }
            }
        }

        if (!held)
        {
            continue;
        }

        if (i == BindWalk)
        {
            walkHeld = true;
        }

        pad->buttons |= info.button;
        switch (info.stick)
        {
        case StickLeftUp:
            leftY = -1;
            break;
        case StickLeftDown:
            leftY = leftY == -1 ? 0 : 1;
            break;
        case StickLeftLeft:
            leftX = -1;
            break;
        case StickLeftRight:
            leftX = leftX == -1 ? 0 : 1;
            break;
        case StickRightUp:
            rightY = -1;
            break;
        case StickRightDown:
            rightY = rightY == -1 ? 0 : 1;
            break;
        case StickRightLeft:
            rightX = -1;
            break;
        case StickRightRight:
            rightX = rightX == -1 ? 0 : 1;
            break;
        default:
            break;
        }
    }

    // The menus' arrows (fixed): out of play the camera's arrows move the selection as the D-pad does
    if (!playing)
    {
        pad->buttons |= (rightY < 0 ? PadBitUp : 0) | (rightY > 0 ? PadBitDown : 0) | (rightX < 0 ? PadBitLeft : 0) |
                        (rightX > 0 ? PadBitRight : 0);
        rightX = 0;
        rightY = 0;
    }

    if (gamepad != nullptr)
    {
        auto axis = [gamepad](SDL_GamepadAxis which) { return StickByte(SDL_GetGamepadAxis(gamepad, which)); };
        u8 rawLeftX = axis(SDL_GAMEPAD_AXIS_LEFTX);
        u8 rawLeftY = axis(SDL_GAMEPAD_AXIS_LEFTY);
        u8 rawRightX = axis(SDL_GAMEPAD_AXIS_RIGHTX);
        u8 rawRightY = axis(SDL_GAMEPAD_AXIS_RIGHTY);
        // A stick pushed past half is the pad in use too
        auto pushed = [](u8 value) { return value < 0x40 || value > 0xC0; };
        gamepadUsed = gamepadUsed || pushed(rawLeftX) || pushed(rawLeftY) || pushed(rawRightX) || pushed(rawRightY);
        // The accessibility settings: the sticks' dead zone (on top of the game's own) and the camera's inversion (its speed is
        // ScaleCameraTurn's, on the game's turn)
        f32 deadZone = static_cast<f32>(GetOption(OptionStickDeadZone)) * 0.05f;
        Squared(&rawLeftX, &rawLeftY);
        Squared(&rawRightX, &rawRightY);
        pad->leftX = Shaped(rawLeftX, deadZone, 1.0f, false);
        pad->leftY = Shaped(rawLeftY, deadZone, 1.0f, false);
        pad->rightX = Shaped(rawRightX, deadZone, 1.0f, IsOn(OptionInvertCameraX));
        pad->rightY = Shaped(rawRightY, deadZone, 1.0f, IsOn(OptionInvertCameraY));
    }

    // The prompts follow the device last used
    NoteInputDevice(keyboardUsed, gamepadUsed ? gamepad : nullptr, gamepad);

    // The mouse's movement as the right stick in play: with the gamepad's, whichever pushes further. The stick ends at its full
    // deflection: the mouse's push past it is the camera's turn's boost
    f32 mouseX = 0.0f;
    f32 mouseY = 0.0f;
    if (playing && MouseLookStick(&mouseX, &mouseY))
    {
        keyboardUsed = true;
        mouseX = IsOn(OptionInvertCameraX) ? -mouseX : mouseX;
        auto further = [](u8 pad, f32 mouse, f32* boost)
        {
            f32 padPush = (static_cast<f32>(pad) - 128.0f) / 127.0f;
            if (std::fabs(mouse) <= std::fabs(padPush))
            {
                return pad;
            }

            *boost = std::max(1.0f, std::fabs(mouse));
            f32 stick = std::clamp(mouse, -1.0f, 1.0f);
            return Clamp255(static_cast<int>(128.0f + stick * 127.0f + 0.5f));
        };
        pad->rightX = further(pad->rightX, mouseX, &g_MouseBoostX);
        pad->rightY = further(pad->rightY, mouseY, &g_MouseBoostY);
    }

    // Left Ctrl toggles walking for the keys' movement (keyboard play runs by default)
    static bool walkKeyWas = false;
    bool walkKey = walkHeld;
    if (walkKey && !walkKeyWas && playing)
    {
        g_Walking = !g_Walking;
    }

    walkKeyWas = walkKey;

    // In a menu (the game's, the pause menu's) the camera's keys are the D-pad too: the arrows, Q and E navigate it
    if (!playing || MenuActiveRecently())
    {
        pad->buttons |= (rightY < 0 ? PadBitUp : 0u) | (rightY > 0 ? PadBitDown : 0u) | (rightX < 0 ? PadBitLeft : 0u) |
                        (rightX > 0 ? PadBitRight : 0u);
    }

    // The keys' stick directions over the gamepad's sticks (and the mouse's)
    if (leftX != 0 || leftY != 0)
    {
        KeySticks(leftX, leftY, &pad->leftX, &pad->leftY);
    }

    if (rightX != 0 || rightY != 0)
    {
        bool walking = g_Walking;
        g_Walking = false;
        KeySticks(IsOn(OptionInvertCameraX) ? -rightX : rightX, IsOn(OptionInvertCameraY) ? -rightY : rightY, &pad->rightX,
                  &pad->rightY);
        g_Walking = walking;
        g_MouseBoostX = 1.0f;
        g_MouseBoostY = 1.0f;
    }
    // The level sweep's, the save test's, the hints test's, the FPS test's and the physics test's scripted input
    SweepInput(pad);
    SaveTestInput(pad);
    HintsTestInput(pad);
    FpsTestInput(pad);
    PhysicsTestInput(pad);
}

void ScaleCameraTurn(f32* yaw, f32* pitch, s32 currentYaw, u32 time)
{
    f32 speed = static_cast<f32>(GetOption(OptionCameraSpeed)) / static_cast<f32>(DefaultOption(OptionCameraSpeed));
    *yaw *= speed * g_MouseBoostX;
    *pitch *= speed * g_MouseBoostY;
    LookTestCamera(currentYaw, time);
}

void KeyName(s32 key, char* out, u32 size)
{
    if (IsMouseKey(key))
    {
        constexpr u32 Names[] = {TextMouseLeft, TextMouseRight, TextMouseMiddle, TextMouse4, TextMouse5, TextWheelUp, TextWheelDown};
        std::snprintf(out, size, "%s", GameText(Names[key - KeyMouseLeft]));
        return;
    }

    if (key <= SDL_SCANCODE_UNKNOWN || key >= SDL_SCANCODE_COUNT)
    {
        std::snprintf(out, size, "-");
        return;
    }

    // SDL's name for the key (its English name: the font has its letters), lower case as the menus are
    const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(key));
    std::snprintf(out, size, "%s", name != nullptr && name[0] != '\0' ? name : "?");
    for (char* c = out; *c != '\0'; c++)
    {
        *c = static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    }
}

void ButtonName(s32 button, char* out, u32 size)
{
    // By the connected pad's own labels: a PlayStation pad's the game's glyphs (cross \\, circle ], square [, triangle ^, L1 {,
    // R1 }, L2 ¦, R2 ¬), another's its letters (A B X Y, LB RB LT RT)
    // (no pad: the PlayStation's, as the game's own prompts)
    SDL_GamepadType type = g_LastGamepad != nullptr ? SDL_GetGamepadType(g_LastGamepad) : SDL_GAMEPAD_TYPE_PS4;
    bool playStation = type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
    const char* name = nullptr;
    if (button >= 0 && button < SDL_GAMEPAD_BUTTON_COUNT && button <= SDL_GAMEPAD_BUTTON_NORTH)
    {
        switch (SDL_GetGamepadButtonLabelForType(type, static_cast<SDL_GamepadButton>(button)))
        {
        case SDL_GAMEPAD_BUTTON_LABEL_A:
            name = "a";
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_B:
            name = "b";
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_X:
            name = "x";
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_Y:
            name = "y";
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
            name = "\\";
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:
            name = "]";
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:
            name = "[";
            break;
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
            name = "^";
            break;
        default:
            break;
        }
    }
    else
    {
        switch (button)
        {
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            name = playStation ? "{" : "lb";
            break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            name = playStation ? "}" : "rb";
            break;
        case GamepadTriggerBase:
            name = playStation ? "\xA6" : "lt";
            break;
        case GamepadTriggerBase + 1:
            name = playStation ? "\xAC" : "rt";
            break;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            name = playStation ? "l3" : "ls";
            break;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            name = playStation ? "r3" : "rs";
            break;
        case SDL_GAMEPAD_BUTTON_START:
            name = "start";
            break;
        case SDL_GAMEPAD_BUTTON_BACK:
            name = playStation ? "select" : "back";
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            name = "d-pad up";
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            name = "d-pad down";
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            name = "d-pad left";
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            name = "d-pad right";
            break;
        default:
            break;
        }
    }

    if (name != nullptr)
    {
        std::snprintf(out, size, "%s", name);
    }
    else if (button < 0)
    {
        std::snprintf(out, size, "-");
    }
    else
    {
        std::snprintf(out, size, "%d", button);
    }
}
}
