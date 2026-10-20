#pragma once

// The controls (bindings.cpp): the keyboard keys and gamepad button bound to each of the DualShock 2's functions, which the pad
// (pads.cpp) reads through ReadBindings
#include "ui/nativeui.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

struct InstanceContext;
class MenuPage;
struct MenuInput;

namespace NativeUi
{
// The PS2 report's button bits (pressed 1, before the report inverts them): pads.cpp's Button
enum PadBit : u32
{
    PadBitL2 = 0x1,
    PadBitR2 = 0x2,
    PadBitL1 = 0x4,
    PadBitR1 = 0x8,
    PadBitTriangle = 0x10,
    PadBitCircle = 0x20,
    PadBitCross = 0x40,
    PadBitSquare = 0x80,
    PadBitSelect = 0x100,
    PadBitL3 = 0x200,
    PadBitR3 = 0x400,
    PadBitStart = 0x800,
    PadBitUp = 0x1000,
    PadBitRight = 0x2000,
    PadBitDown = 0x4000,
    PadBitLeft = 0x8000,
};

// The game's actions bound (the modern Crash games' scheme, native/research/MODERN_CRASH_UI.md 3.4): what each sets of the PS2's
// report (buttons, or a stick's direction). The names of the PS2's buttons stay as aliases (the prompts' glyphs are those buttons')
enum BindingFunction : u32
{
    BindJump,
    BindSpin,
    BindCrouch,
    BindStatus,
    // The shoulders: L1 and L2 together (strafe, hover, the pause pages), R1 and R2 together
    BindShoulderLeft,
    BindShoulderRight,
    BindPause,
    BindSelect,
    BindL3,
    BindR3,
    // The left stick (moving), the right stick (the camera)
    BindMoveForward,
    BindMoveBack,
    BindMoveLeft,
    BindMoveRight,
    BindCameraUp,
    BindCameraDown,
    BindCameraLeft,
    BindCameraRight,
    // Walking (a toggle: keyboard play runs, the stick's push otherwise)
    BindWalk,
    // The D-pad (a pad's own, fixed; the menus' arrows)
    BindUp,
    BindDown,
    BindLeft,
    BindRight,
    BindingCount,

    BindCross = BindJump,
    BindSquare = BindSpin,
    BindCircle = BindCrouch,
    BindTriangle = BindStatus,
    BindL1 = BindShoulderLeft,
    BindR1 = BindShoulderRight,
    BindL2 = BindShoulderLeft,
    BindR2 = BindShoulderRight,
    BindStart = BindPause,
};

enum StickDirection : s32
{
    StickLeftUp,
    StickLeftDown,
    StickLeftLeft,
    StickLeftRight,
    StickRightUp,
    StickRightDown,
    StickRightLeft,
    StickRightRight,
};

// The mouse's buttons as keys (a binding's keys): left, right, middle, the side buttons (4 and 5), and the wheel's turns up and down
// (a turn holds its key for one of the pad's reads)
constexpr s32 KeyMouseLeft = 0x400;
constexpr s32 KeyMouseRight = 0x401;
constexpr s32 KeyMouseMiddle = 0x402;
constexpr s32 KeyMouse4 = 0x403;
constexpr s32 KeyMouse5 = 0x404;
constexpr s32 KeyWheelUp = 0x405;
constexpr s32 KeyWheelDown = 0x406;
constexpr bool IsMouseKey(s32 key)
{
    return key >= KeyMouseLeft && key <= KeyWheelDown;
}
// An SDL mouse button's key (none: -1)
s32 MouseButtonKey(u8 sdlButton);

// Two keys (SDL scancodes, or the mouse's) and two gamepad buttons (SDL gamepad buttons, GamepadTriggerBase + 0/1 the triggers);
// -1 none
struct Binding
{
    s32 keys[2];
    s32 button;
    s32 button2;
};

// What the bindings read of the keyboard and the gamepad: the report's button bits, the triggers' pressures when a trigger is
// bound (0 otherwise: the button's own full press), the sticks (0x80 the middle, up 0)
struct BoundPad
{
    static constexpr u8 StickMiddle = 0x80;

    u32 buttons;
    u8 l2Pressure;
    u8 r2Pressure;
    u8 leftX;
    u8 leftY;
    u8 rightX;
    u8 rightY;
};

void ReadBindings(const bool* keys, SDL_Gamepad* gamepad, BoundPad* pad);
// A gamepad stick's bytes (its circle) moved out to the DualShock 2's square (bindings.cpp)
void Squared(u8* x, u8* y);
// The level sweep (sweep.cpp, $TWIN_SWEEP_LEVEL): whether it runs, its frame (the warp, the time played, the exit) and its scripted
// input over the pad's
bool SweepActive();
// fromPads: the pads' read's call (time only; the game controller's counts the frames)
void SweepFrame(bool fromPads = false);
void SweepInput(BoundPad* pad);
// The player put a height above the nearest live instance of its chunk whose object's name holds a text (any case; the physics
// test's): whether there's one
bool PutPlayerAbove(InstanceContext* player, const char* name, f32 height);
// The save and load test (savetest.cpp, $TWIN_SAVE_TEST=save|load): whether it runs, its frame, its menu step (a wanted item
// selected: true) and its input
bool SaveTestActive();
void SaveTestFrame();
bool SaveTestMenuStep(MenuPage* page, MenuInput* input, u32 player);
void SaveTestInput(BoundPad* pad);
// The game's menus' hints test ($TWIN_HINTS_TEST, hintstest.cpp): its frame and its scripted start
void HintsTestFrame();
void HintsTestInput(BoundPad* pad);
// The FPS counter's test ($TWIN_FPS_TEST, fpstest.cpp): its frame and its scripted status button and start
void FpsTestFrame();
void FpsTestInput(BoundPad* pad);
// The mouse look test ($TWIN_LOOK_TEST, looktest.cpp): whether it runs (mouse look then holds the mouse without a window), its
// frame (the scripted motion) and the follow camera's yaw and clock (ScaleCameraTurn's)
bool LookTestActive();
void LookTestFrame();
void LookTestCamera(s32 yaw, u32 time);
// The physics test ($TWIN_PHYSICS_TEST, physicstest.cpp): its frame (the measures, the trace) and its scripted moves
void PhysicsTestFrame();
void PhysicsTestInput(BoundPad* pad);

// The mouse in play (mouselook.cpp): its events watched (once), the window holding the mouse in relative mode while the game is
// played with mouse look on (a game frame's check), whether it holds it, the right stick's deflection from the motion since the
// last read (-1 to 1 each, past 1 with a sensitivity over 5: the camera's turn's boost; false: none), the wheel's keys held this
// read; and the tests' motion and relative mode
void StartMouseLook();
void MouseLookFrame();
bool MouseLookActive();
bool MouseLookStick(f32* x, f32* y);
void TakeWheel(bool* up, bool* down);
void MouseLookTestMotion(f32 dx, f32 dy);
void MouseLookForceForTest(bool relative);

const Binding& GetBinding(u32 function);
const char* BindingName(u32 function);
void SetBinding(u32 function, const Binding& binding);
// A key bound to a function (taken from any other; the function's first key becomes its second), a gamepad button (swapped with
// the function that had it)
void ResetBindings();
// A key bound to a function's first or second key (slot 0 or 1), a pad button to its first or second: where the key or button
// was bound already gets the old one (a swap). The function swapped with (-1 none)
s32 BindKeySlot(u32 function, u32 slot, s32 key);
s32 BindButtonSlot(u32 function, u32 slot, s32 button);
// A cell cleared (Backspace or Delete)
void ClearKeySlot(u32 function, u32 slot);
void ClearButtonSlot(u32 function, u32 slot);
// Whether a function's keys and buttons can be bound (the pad's sticks and D-pad are fixed)
bool KeysBindable(u32 function);
bool ButtonsBindable(u32 function);
void ResetKeyboardBindings();
void ResetControllerBindings();
// The gamepad the pad reads (none: none plugged in)
SDL_Gamepad* LastGamepad();
// A key's name and a gamepad button's (the connected pad's labels: the game's glyphs for a PlayStation pad), in the game's text
// encoding, lower case
void KeyName(s32 key, char* out, u32 size);
void ButtonName(s32 button, char* out, u32 size);

// The last input's device (ReadBindings): the prompts follow it (prompts.cpp)
void NoteInputDevice(bool keyboard, SDL_Gamepad* gamepad, SDL_Gamepad* connected);
// A pad button's label as the pad in use (or last used) names it (the game's glyphs for a PlayStation pad); empty for none
std::string PadButtonLabel(s32 button);
// The pad's style the pad's labels are in (the prompts' style, or with the keyboard's prompts the pad last plugged in)
PromptStyle PadPromptStyle();




// The menus' select and back and the tabs' prompts for the input in use (Enter, Esc, Q and E for the keyboard; the glyphs, or the
// pad's labels)
std::string MenuSelectPrompt();
std::string MenuBackPrompt();
std::string TabPrompt(bool next);
std::string PromptOfButton(s32 button);
// A text's prompt glyph code (PromptGlyphs) as the input in use labels it (the glyph for a PlayStation pad)
std::string GlyphPrompt(char glyph);

// The report bits of the menus' select and back for the prompts' style (the shots' script)
u32 MenuSelectBit();
u32 MenuBackBit();

// The Xbox's button glyphs (xboxart.cpp), read from the user's Xbox disc (xbox_disc.txt, local.json's "xbox_disc_image" or
// $TWINSANITY_XBOX_ISO) when there is one, and the Switch's made from them (grey): an image (RGBA, red the low byte) by label
struct ButtonArt
{
    s32 width = 0;
    s32 height = 0;
    std::vector<u32> pixels;
};
enum ButtonArtLabel : u32
{
    ArtA,
    ArtB,
    ArtX,
    ArtY,
    ArtLeftShoulder,
    ArtRightShoulder,
    ArtLeftTrigger,
    ArtRightTrigger,
    ButtonArtLabels,
};
bool HaveButtonArt();
const ButtonArt* GetButtonArt(PromptStyle style, ButtonArtLabel label);
// The buttons drawn in the game's glyphs' style (buttonart.cpp), and the art the game's texts' glyphs are drawn as: the disc's
// when there is one, else the drawn
const ButtonArt* DrawnButtonArt(PromptStyle style, ButtonArtLabel label);
const ButtonArt* GlyphButtonArt(PromptStyle style, ButtonArtLabel label);
void ForgetButtonArtForTest();
// An RGBA picture (red the low byte) written as a PNG (uishots.cpp)
bool WritePngFile(const std::string& path, const u32* pixels, u32 width, u32 height);

// The tests'
void SetBindingsSaving(bool saving);
void ResetBindingsForTest();
}
