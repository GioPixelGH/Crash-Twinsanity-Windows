// The controllers: the machine's gamepad (SDL3) and keyboard, presented to the game as a DualShock 2 on port 0 the way the PS2's
// libpad does (analog mode, two actuators, pressure sensitive mode), reporting what the PS2's report has: the button word
// (pressed 0), the sticks (0x80 the middle, up 0) and the buttons' pressures (0-255). See native/NATIVE.md for the mapping
#include "native.h"
#include "fixes.h"
#include "platform/pads.h"
#include "ui/bindings.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace
{
using namespace Platform::Pads;

// The report's button word, pressed 1 before it's inverted (the PS2's bits: high byte left, down, right, up, start, R3, L3,
// select; low byte square, cross, circle, triangle, R1, L1, R2, L2)
enum Button : u32
{
    ButtonL2 = 0x1,
    ButtonR2 = 0x2,
    ButtonL1 = 0x4,
    ButtonR1 = 0x8,
    ButtonTriangle = 0x10,
    ButtonCircle = 0x20,
    ButtonCross = 0x40,
    ButtonSquare = 0x80,
    ButtonSelect = 0x100,
    ButtonL3 = 0x200,
    ButtonR3 = 0x400,
    ButtonStart = 0x800,
    ButtonUp = 0x1000,
    ButtonRight = 0x2000,
    ButtonDown = 0x4000,
    ButtonLeft = 0x8000,
};

// A DualShock 2 in pressure sensitive mode: type 7 (analog), 9 halfwords of data after the header
constexpr u8 ModeAnalog = 0x73;
constexpr u8 ModePressure = 0x79;
constexpr s32 PressureReportBytes = 20;
constexpr s32 AnalogReportBytes = 8;
constexpr u8 StickMiddle = 0x80;
constexpr u8 Pressed = 0xFF;

bool g_Initialised = false;
bool g_Open = false;
bool g_PressureMode = false;
SDL_Gamepad* g_Gamepad = nullptr;

// What each button's pressure byte is (the PS2's ReportByte order)
struct PressureOf
{
    Button button;
    ReportByte byte;
};
constexpr PressureOf Pressures[] = {
    {ButtonRight, PressureRight}, {ButtonLeft, PressureLeft},     {ButtonUp, PressureUp},       {ButtonDown, PressureDown},
    {ButtonTriangle, PressureTriangle}, {ButtonCircle, PressureCircle}, {ButtonCross, PressureCross},
    {ButtonSquare, PressureSquare}, {ButtonL1, PressureL1},       {ButtonR1, PressureR1},       {ButtonL2, PressureL2},
    {ButtonR2, PressureR2},
};

void StartSdl()
{
    if (!g_Initialised)
    {
        g_Initialised = true;
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_EVENTS))
        {
            Native::Log("pads: SDL's gamepads didn't start: %s", SDL_GetError());
        }
    }
}

// The first gamepad plugged in, kept until it's unplugged
void FindGamepad()
{
    if (g_Gamepad != nullptr && SDL_GamepadConnected(g_Gamepad))
    {
        return;
    }

    if (g_Gamepad != nullptr)
    {
        SDL_CloseGamepad(g_Gamepad);
        g_Gamepad = nullptr;
    }

    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids != nullptr && count > 0)
    {
        g_Gamepad = SDL_OpenGamepad(ids[0]);
        if (g_Gamepad != nullptr)
        {
            Native::Log("pads: %s", SDL_GetGamepadName(g_Gamepad));
        }
    }

    SDL_free(ids);
}
}

s32 Platform::Pads::Initialise()
{
    StartSdl();
    return 1;
}

s32 Platform::Pads::Open(s32 port, s32 slot, void*)
{
    StartSdl();
    if (port != 0 || slot != 0)
    {
        return 0;
    }

    g_Open = true;
    return 1;
}

s32 Platform::Pads::Close(s32 port, s32 slot)
{
    if (port == 0 && slot == 0)
    {
        g_Open = false;
        g_PressureMode = false;
    }

    return 1;
}

Platform::Pads::State Platform::Pads::GetState(s32 port, s32 slot)
{
    return port == 0 && slot == 0 && g_Open ? StateStable : StateDisconnected;
}

Platform::Pads::RequestState Platform::Pads::GetRequestState(s32, s32)
{
    return RequestComplete;
}

// A DualShock 2's modes: digital (4) and analog (7), in analog mode
s32 Platform::Pads::InfoMode(s32 port, s32 slot, ModeInfo info, s32 index)
{
    if (GetState(port, slot) != StateStable)
    {
        return 0;
    }

    switch (info)
    {
    case InfoCurrentId:
    case InfoCurrentExtendedId:
        return ModeIdAnalog;
    case InfoCurrentIndex:
        return 1;
    case InfoModeTable:
        return index == ModeTableLength ? 2 : index == 0 ? ModeIdDigital : index == 1 ? ModeIdAnalog : 0;
    }

    return 0;
}

s32 Platform::Pads::SetMainMode(s32, s32, MainMode, ModeLock)
{
    return 1;
}

// Two actuators, as a DualShock 2 has: the small motor (on or off) and the large one (0-255)
s32 Platform::Pads::InfoActuator(s32 port, s32 slot, s32 actuator, s32)
{
    if (GetState(port, slot) != StateStable)
    {
        return 0;
    }

    return actuator == CountActuators ? 2 : 1;
}

s32 Platform::Pads::SetActuatorAlign(s32, s32, const u8*)
{
    return 1;
}

s32 Platform::Pads::SetActuatorDirect(s32 port, s32 slot, const u8 values[6])
{
    if (port != 0 || slot != 0)
    {
        return 0;
    }

    if (g_Gamepad != nullptr)
    {
        // The small motor on or off, the large one by its strength; the game sets them again every frame it wants them
        u16 small = values[0] != 0 ? 0xFFFF : 0;
        u16 large = static_cast<u16>(values[1] * 0x101);
        SDL_RumbleGamepad(g_Gamepad, large, small, 100);
    }

    return 1;
}

s32 Platform::Pads::InfoPressureMode(s32 port, s32 slot)
{
    return GetState(port, slot) == StateStable ? 1 : 0;
}

s32 Platform::Pads::EnterPressureMode(s32 port, s32 slot)
{
    if (GetState(port, slot) != StateStable)
    {
        return 0;
    }

    g_PressureMode = true;
    return 1;
}

namespace
{
// Keys pressed since the last read: a press shorter than a frame (the native frames can be slow) still counts for one
bool g_Latched[SDL_SCANCODE_COUNT];
bool g_Watching = false;
bool g_QuitAsked = false;

bool WatchPadEvents(void*, SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode > SDL_SCANCODE_UNKNOWN &&
        event->key.scancode < SDL_SCANCODE_COUNT)
    {
        g_Latched[event->key.scancode] = true;
    }
    else if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        // The window's close button, the Dock's or the menu's quit, Cmd+Q
        g_QuitAsked = true;
    }

    return true;
}
}

s32 Platform::Pads::Read(s32 port, s32 slot, u8* data)
{
    if (!g_Watching)
    {
        g_Watching = true;
        SDL_AddEventWatch(WatchPadEvents, nullptr);
    }

    if (port != 0 || slot != 0 || !g_Open)
    {
        return 0;
    }

    SDL_PumpEvents();
    if (g_QuitAsked)
    {
        // Quitting: the saves and settings are written as they're made, so nothing is left to keep
        // Straight out: SDL's shutdown under the sound thread still feeding its stream crashed on the way out
        Native::Log("quit");
        std::_Exit(0);
    }

    Native::SnapshotFrame();
    Native::QuickStartFrame();
    NativeFixes::Frame();
    NativeFixes::GiveBackFrame();
    // The level sweep's frame here too: GameController::Update (its other caller) doesn't run while some cutscenes play, the pads'
    // reads do (their skip polls them)
    NativeUi::SweepFrame(true);
    FindGamepad();
    // The keyboard's keys and the gamepad's buttons by the controls' bindings (src/platform/native/ui/bindings.cpp: the defaults
    // are NATIVE.md's), the sticks the gamepad's (the keys bound to the left stick's directions over it)
    NativeUi::BoundPad bound;
    static bool keys[SDL_SCANCODE_COUNT];
    const bool* state = SDL_GetKeyboardState(nullptr);
    for (int key = 0; key < SDL_SCANCODE_COUNT; key++)
    {
        keys[key] = state[key] || g_Latched[key];
        g_Latched[key] = false;
    }

    NativeUi::ReadBindings(keys, g_Gamepad, &bound);
    u32 buttons = bound.buttons;
    u8 pressure[PressureReportBytes] = {};
    pressure[PressureL2] = bound.l2Pressure;
    pressure[PressureR2] = bound.r2Pressure;
    u8 rightX = bound.rightX, rightY = bound.rightY, leftX = bound.leftX, leftY = bound.leftY;

    // The digital buttons' pressures: fully pressed (the triggers have their own when a gamepad gives them)
    for (const PressureOf& of : Pressures)
    {
        if ((buttons & of.button) != 0 && pressure[of.byte] == 0)
        {
            pressure[of.byte] = Pressed;
        }
    }

    std::memset(data, 0, PressureReportBytes);
    data[0] = 0;
    data[ReportMode] = g_PressureMode ? ModePressure : ModeAnalog;
    data[ReportButtonsHigh] = static_cast<u8>(~buttons >> 8);
    data[ReportButtonsLow] = static_cast<u8>(~buttons);
    data[StickRightX] = rightX;
    data[StickRightY] = rightY;
    data[StickLeftX] = leftX;
    data[StickLeftY] = leftY;
    if (g_PressureMode)
    {
        for (u32 byte = PressureRight; byte <= PressureR2; byte++)
        {
            data[byte] = pressure[byte];
        }
    }

    return g_PressureMode ? PressureReportBytes : AnalogReportBytes;
}
