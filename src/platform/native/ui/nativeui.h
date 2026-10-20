#pragma once

#include <vector>

// The native build's own UI changes, called from the game's code under TWIN_NATIVE: the PC wording of the language files' console
// texts (pctext.cpp, native/PC_TEXT.md), the native menu texts the resolution setting uses, and the resolution setting itself
// (resolution.cpp)
#include "common.h"

class MenuPage;
struct MenuInput;
struct MenuItem;
struct MenuSounds;
struct Renderer;

namespace NativeUi
{
// ---------------------------------------------------------------------------------------------------------------------- texts
// A language's text file just split into its lines (ReadTextFile): the lines with the console's wording replaced by the PC's, and,
// for the code's texts, the native texts added at NativeTextFirst on. The array returned is the native build's own (it stays);
// count is the file's line count and stays the same
const char** ApplyTextOverrides(u32 file, const char* language, const char** lines, u32 count);
// The lines ApplyTextOverrides keeps (for the prompts' labels) let go of: the self-test's, whose files it frees
void ForgetTextLines();

// The button prompts' glyphs in the game's texts (its font's codes): cross, circle, square, triangle, L1, R1, L2, R2. The texts'
// prompts are labelled as the input in use has them (bindings.cpp: a key's name, another pad's letters; the glyphs themselves for a
// PlayStation pad), every line that has a glyph made again from the line as it was read
constexpr const char PromptGlyphs[] = "\\][^{}\xA6\xAC";
constexpr u32 PromptGlyphCount = sizeof(PromptGlyphs) - 1;
// The labels in that order (nullptr: the glyphs), and the menus' select and back labels for the menus' own lines (nullptr: cross's
// and triangle's glyphs)
void SetPromptLabels(const char* const labels[PromptGlyphCount], const char* menuSelect, const char* menuBack);

// The prompts' style: the last input device's (prompts.cpp; $TWIN_UI_PROMPTS=playstation|xbox|switch|keyboard forces it)
enum PromptStyle : u32
{
    PromptPlayStation,
    PromptXbox,
    PromptSwitch,
    PromptKeyboard,
};
PromptStyle CurrentPromptStyle();
// The prompts labelled again (the style or the bindings changed)
void UpdatePrompts();
void ForgetPromptStyleForTest();

// The native texts: game text numbers past the file's own lines (the menu items' text field holds 12 bits)
enum NativeText : u32
{
    NativeTextFirst = 0x100,
    TextResolution = NativeTextFirst,
    TextDisplay,
    TextScale1,
    TextScale2,
    TextScale3,
    TextScale4,
    TextWindow960,
    TextWindow1280,
    TextWindow1600,
    TextWindow1920,
    TextWindow1280Wide,
    TextWindow1920Wide,
    TextFullscreen,
    TextFiltering,
    TextSmooth,
    TextSharp,
    TextCrt,
    TextVoiceVolume,
    TextSkipCutscenes,
    TextFastLoading,
    TextPauseInactive,
    TextDetailedLog,
    TextRefreshRate,
    Text50Hz,
    Text60Hz,
    Text120Hz,
    Text144Hz,
    TextUnlimited,
    TextGameLanguage,
    TextLanguageWindows,
    TextLanguageEnglish,
    TextLanguageFrench,
    TextLanguageGerman,
    TextLanguageItalian,
    TextLanguageSpanish,
    TextBugFixes,
    TextControls,
    TextResetControls,
    TextPressKey,
    TextL3,
    TextR3,
    TextStart,
    TextSelect,
    TextUp,
    TextDown,
    TextLeft,
    TextRight,
    TextLeftStick,
    TextJump,
    TextCrouch,
    TextStatus,
    TextSpin,
    TextNintendoLayout,
    TextNintendo,
    TextController,
    TextAutomatic,
    TextKeyboard,
    TextXbox,
    TextPlayStation,
    TextSwitch,
    TextMouse,
    TextTabDisplay,
    TextTabAudio,
    TextTabGameplay,
    TextDisplayMode,
    TextUpscale,
    TextTexturePack,
    TextAntiAliasing,
    TextVSync,
    TextColumnAction,
    TextColumnKey,
    TextColumnKey2,
    TextColumnPad,
    TextMoveUp,
    TextMoveDown,
    TextMoveLeft,
    TextMoveRight,
    TextCameraLeft,
    TextCameraRight,
    TextStrafeLeft,
    TextStrafeRight,
    TextPause,
    TextResetKeyboard,
    TextResetController,
    TextResetDefaults,
    TextDescDisplayMode,
    TextDescResolution,
    TextDescWidescreen,
    TextDescFiltering,
    TextDescUpscale,
    TextDescTexturePack,
    TextDescAntiAliasing,
    TextDescVSync,
    TextDescCrt,
    TextDescRefreshRate,
    TextDescGameLanguage,
    TextDescScreenPosition,
    TextDescMusic,
    TextDescEffects,
    TextDescVoices,
    TextDescOutput,
    TextDescController,
    TextDescLayout,
    TextDescVibration,
    TextDescMouse,
    TextDescBinding,
    TextDescResetKeyboard,
    TextDescResetController,
    TextDescSkip,
    TextDescFastLoading,
    TextDescPauseInactive,
    TextDescDetailedLog,
    TextDescBugFixes,
    TextDescResetDefaults,
    TextTabs,
    TextTabAccessibility,
    TextWindowSize,
    TextWindowed,
    TextKeyboardBindings,
    TextControllerBindings,
    TextColumnButton,
    TextColumnButton2,
    TextCameraShake,
    TextInvertX,
    TextInvertY,
    TextCameraSpeed,
    TextDeadZone,
    TextMuteInactive,
    TextShoulderLeft,
    TextShoulderRight,
    TextWalk,
    TextCameraUp,
    TextCameraDown,
    TextMouseLeft,
    TextMouseRight,
    TextKeepDisplay,
    TextClear,
    TextDescWindowSize,
    TextDescKeyboardBindings,
    TextDescControllerBindings,
    TextDescCameraShake,
    TextDescInvertX,
    TextDescInvertY,
    TextDescCameraSpeed,
    TextDescDeadZone,
    TextDescMuteInactive,
    TextNext,
    TextPositional,
    TextRestartCheckpoint,
    TextLevelSelect,
    TextLeftStickUp,
    TextLeftStickDown,
    TextLeftStickLeft,
    TextLeftStickRight,
    TextRightStickUp,
    TextRightStickDown,
    TextRightStickLeft,
    TextRightStickRight,
    TextMouseMiddle,
    TextMouse4,
    TextMouse5,
    TextWheelUp,
    TextWheelDown,
    TextMouseLook,
    TextMouseSensitivity,
    TextInvertMouseY,
    TextDescMouseLook,
    TextDescMouseSensitivity,
    TextDescInvertMouseY,
    TextFxaa,
    TextMenuHints,
    TextDescMenuHints,
    TextHintsOriginal,
    TextHintsBar,
    TextShowFps,
    TextDescShowFps,
    NativeTextEnd,
};

// The game's own texts the settings use (the code file's): options, widescreen, center screen, the volumes, output type and its
// first choice (mono), vibration, the footer's select and back prompts
enum GameTextId : u32
{
    GameTextNo = 2,
    GameTextYes = 3,
    GameTextBack = 4,
    GameTextCancel = 0x3D,
    GameTextMono = 6,
    GameTextOptions = 0xB,
    GameTextWidescreen = 0x10,
    GameTextCentreScreen = 0x11,
    GameTextEffectsVolume = 0x12,
    GameTextMusicVolume = 0x13,
    GameTextOutputType = 0x14,
    GameTextVibration = 0x15,
    GameTextSelectPrompt = 0x1C,
    GameTextBackPrompt = 0x1D,
};

// ----------------------------------------------------------------------------------------------------------------- resolution
// The render scale (1 to 4 times the PS2's picture) and the display modes: windows of fixed sizes, then fullscreen
constexpr s32 ScaleChoices = 4;
constexpr s32 DisplayChoices = TextFullscreen - TextWindow960 + 1;

struct Resolution
{
    s32 scale;
    s32 windowWidth;
    s32 windowHeight;
    bool fullscreen;
};

// The resolution in use: the saved one once read, else the graphics' own
Resolution CurrentResolution();
// The menu's choices for a resolution, and the resolution of the menu's choices (the window's size kept when the display choice
// is the one already in use, so a window resized by hand stays as it is)
s32 ScaleChoice(const Resolution& resolution);
s32 DisplayChoice(const Resolution& resolution);
Resolution FromChoices(s32 scaleChoice, s32 displayChoice);
// Applies a resolution through the graphics and saves it in the app's settings (resolution.txt)
void SetResolution(const Resolution& resolution);
// Whether the resolution goes to the graphics (the self-test turns it off: no window, the graphics not started)
extern bool g_ResolutionToGraphics;
// The options page's choices as shown, and whether the player changed them since (then they're noted): a window resized by hand
// while the page is shown isn't snapped to a choice's size
void NoteChoices(s32 scaleChoice, s32 displayChoice);
bool ChoicesChanged(s32 scaleChoice, s32 displayChoice);
// At start-up, once the window is there: the saved resolution applied, else the defaults (which aren't written: the file is made
// by the player's own choice)
void ApplySavedResolution();
// The defaults, the first start's and the display settings' reset alike: 2x the PS2's picture (1280 x 1024: sharp on a modern
// screen, light on a laptop's graphics) in a 1280 x 720 window (16:9 like the widescreen default, and it fits a 1366 x 768 laptop)
Resolution DefaultResolution();


// ---------------------------------------------------------------------------------------------------------- the native options
// The native build's options (options.cpp: kept in the settings folder's options.txt). Defaults (DefaultOption): the game as the
// PS2 had it, at 60 Hz, with the PC conveniences that change nothing of the game on
enum Option : u32
{
    // Hold any button to skip the cutscenes that have the retail game's disabled skip (the PS2 game couldn't skip them: off by
    // default); it stops the movies too
    OptionSkipCutscenes,
    // The disc's reads sent as soon as they're queued (the community patcher's faster level loads)
    OptionFastLoading,
    // The game's own pause (and the sound muted) when the window loses the focus
    OptionPauseOnFocusLoss,
    // The game's widescreen setting at start-up (a save's own setting still applies once loaded)
    OptionWidescreen,
    // The frame rate: PAL's 50 Hz (0), NTSC's 60 (1: the game's clock, its particles and the display's pacing), or NTSC's
    // timing at 120 (2), 144 (3) or as fast as the computer goes (4), the game's 60ths of a second kept where it counts in them
    OptionRefresh60,
    // The harmless retail bugs fixed (NATIVE.md lists them)
    OptionRetailFixes,
    // The picture's filtering when it's shown bigger: smooth (1) or sharp (0)
    OptionTextureFilter,
    // The CRT's scanlines over the picture
    OptionCrtFilter,
    // A Nintendo pad's menus in its own layout (A, the right button, selects; B, the bottom one, goes back), or by the places
    // (the game's: the bottom button selects); the play is by the places either way
    OptionNintendoLayout,
    // The voices' volume (0 to 10, as the game's volumes)
    OptionVoiceVolume,
    // The controller the prompts are for: automatic (the last device used), the keyboard, an Xbox pad, a PlayStation pad, a Switch
    // pad (ControllerChoice)
    OptionController,
    // The mouse in the menus (hover selects, click picks, right click goes back)
    OptionMouse,
    // The settings screen's display and audio settings the game doesn't keep itself (the graphics' texture upscale 0-2, not on
    // the screen until the graphics have it; the texture pack; anti-aliasing 0 off or 1 FXAA; v-sync; the music and effects volumes
    // 0-10, the output type, vibration, given to the game at start-up)
    OptionTextureUpscale,
    OptionTexturePack,
    OptionAntiAliasing,
    OptionVSync,
    OptionMusicVolume,
    OptionEffectsVolume,
    OptionOutputType,
    OptionVibration,
    // Accessibility: the camera's shake, its look axes inverted, its speed (1-10, 5 the game's), the sticks' dead zone (0-10:
    // 0 to 50% on top of the game's own); the sound muted in the background (apart from the pause)
    OptionCameraShake,
    OptionInvertCameraX,
    OptionInvertCameraY,
    OptionCameraSpeed,
    OptionStickDeadZone,
    OptionMuteInactive,
    // 21:9: the widescreen picture made 21:9 wide (the community mod's PCSX2 "Widescreen 21:9 Ultrawide", TechieSaru's fix): the
    // projection, the 2D's squeeze and the movies' pillarbox (NATIVE.md, "Bug fixes (optional)"); only while widescreen is on
    OptionUltrawide,
    // The mouse in play (bindings.cpp, mouselook.cpp): its movement the right stick (the camera, Crash's look) with the window
    // holding the mouse, its speed (1-10), its up and down inverted
    OptionMouseLook,
    OptionMouseSensitivity,
    OptionInvertMouseY,
    // The game's language (its texts, its voices, its movies' sound): the computer's (0, as the PS2 took its own), English,
    // French, German, Italian or Spanish; the game takes it when it starts (system.cpp's Platform::System::Language)
    OptionLanguage,
    // More of what the game does written to the log (trail.cpp), for bug reports
    OptionDetailedLog,
    // The game's menus' button hints: the game's own (0: OLEG's labels, their glyphs the controller's) or the native bar (1)
    OptionMenuHints,
    // A frames per second counter in the window's top left corner (fpscounter.cpp)
    OptionShowFps,
    OptionCount,
};

s32 GetOption(Option option);
// An option's default (options.cpp's table): the first start's value and the settings' reset's
s32 DefaultOption(Option option);
bool IsOn(Option option);
// The show FPS option's counter (fpscounter.cpp): shown or not (ApplyOption), each picture the window shows counted at its time
// (seconds, the presenter's thread: the overlay's hook), and the frames a second it shows (-1: none, the option off or the first
// half second not counted yet)
void SetFpsCounterShown(bool shown);
void CountShownFrame(double seconds);
s32 FpsCounterValue();
// The game's trail in the log, a frame at a time (trail.cpp)
void TrailFrame();
// Sets an option (kept, and applied at once where it applies at once)
void SetOption(Option option, s32 value);
// Applies what an option changes once it's set, and every option's start-up effect (features.cpp)
void ApplyOption(Option option);
void ApplyStartOptions();
void ApplyGameSettingsOnce();
// The options read and the window's events watched (from Main, once the window is there), and the work between the game's frames
// (GameController::Update): the fullscreen toggle, the mute, the 60 Hz pacing
void StartOptions();
void Frame();
// Once after the window lost the focus (the option on): the game pauses as for its start button
bool TakeFocusPause();
// With the cutscene skip option, "hold any button to skip" (features.cpp): any key, mouse button or pad button pressed after the
// scene began and held 0.6 s. A poll from a scene that can be skipped (the skip condition's, a movie's frame's): whether it's
// skipped; the hold's progress (0-1) while a counted button is held in a scene being polled (-1: none, no bar); the tests' inputs
// and clock (forced: the inputs held at the time in seconds; not forced: the real ones, the hold forgotten)
bool SkipHoldComplete();
// Any button pressed in a cutscene (its letterbox showing), for the game's own triangle skips
bool CutsceneSkipPressed();
f32 SkipHoldProgress();
bool MovieSkipHeld();
void SkipHoldForTest(bool forced, const std::vector<s32>& inputs, f32 seconds);
// How long a press has to be held to skip (the self-test's timings follow it)
f32 SkipHoldSecondsForTest();
// The game clock's frames a second for the retail value (the 60 Hz option's)
s32 FramesPerSecond(s32 retail);
// The frame rate the frames are paced at (50, 60, 120, 144; 0 as fast as they come)
s32 FrameRate();
// The game's work done a frame at a time that counts in 60ths of a second (the particles' emitters and events, the decals'
// lives): at 50 and 60 Hz once a frame, as the PS2 did it; faster, once for each 60th gone by (none some frames), the time
// left over kept in carried
u32 GameTicks(f32 seconds, f32* carried);
// The next key or gamepad button pressed, for the controls page (the pad reads nothing meanwhile): a key's SDL scancode, a
// button's SDL gamepad button (GamepadTriggerBase on: the triggers)
constexpr s32 GamepadTriggerBase = 0x100;
void BeginCapture();
bool Capturing();
bool TakeCapture(s32* key, s32* button);
void CancelCapture();

// $TWIN_UI_SHOTS (uishots.cpp): the menus driven by a script and their pictures saved; $TWIN_UI_RETAIL=1 leaves the native menu
// items out (to compare with the retail pages)
void UiShotsFrame();
u32 UiShotsButtons();
bool NativeItemsShown();

// The mouse in the menus (mouse.cpp): the window's mouse events watched, the frame's work, the lines the menus' drawer and the
// overlay queue noted, and a menu page's step given the mouse as the game's input
void StartMouse();
void MouseFrame();
void RecordMenuLine(const MenuPage* page, const MenuItem* item, const Renderer* renderer, const char* text, f32 x, f32 y);
void RecordQueuedText(const Renderer* renderer, const char* text, f32 x, f32 y);
// Whether a menu page stepped in the last quarter second (the camera keys navigate it)
bool MenuActiveRecently();
// An item's area (fractions of the picture) for the mouse, for items drawn as shapes rather than lines (the save slots)
void RecordMenuRect(const MenuPage* page, const MenuItem* item, f32 left, f32 top, f32 right, f32 bottom);
void MouseMenuStep(MenuPage* page, MenuInput* input, MenuSounds* sounds, u32 player);
// The pad's buttons a click on a hint presses (a pages' arrow: L1 or R1), for a few of the pad's reads (ReadBindings adds them)
u32 MouseMenuButtons();
void ScriptMouse(f32 x, f32 y, bool click);
// The shots' script: the mouse onto the middle of the game's hint of that text (0x1C select ...) drawn last frame, clicked or not
bool ScriptMouseToHint(u32 text, bool click);
// A window point (SDL's) in fractions of the game's picture; a text's width and height (fractions) in the renderer's font and scale
void WindowToPicture(f32 x, f32 y, f32* outX, f32* outY);
bool TextExtent(const Renderer* renderer, const char* text, f32* width, f32* height);
bool CursorShown();

// The settings screen (settings.cpp): the options' tabs (display, audio, controls, gameplay) drawn through the game's renderer in
// place of the old options pages, taking the keyboard, the pads and the mouse while it's open
void OpenSettings();
bool SettingsOpen();
void SettingsFrame();
// After OLEG's widgets are drawn (GameController::Draw): the settings' panels, and the game's menus' selection oval
void DrawAfterOleg(Renderer* renderer);
// The game's menus' selected line drawn this frame (mouse.cpp notes it as the drawer queues it): taken once
bool TakeSelectedMenuLine(f32* left, f32* top, f32* right, f32* bottom);
// The camera's shake scale (the accessibility setting: 1, or 0 with it off)
f32 CameraShakeScale();
// The follow camera's turn from the right stick (FollowCameraRig::Frame): its yaw's and pitch's inputs times the camera speed
// setting (5: the game's own) and the mouse's push past the stick's full deflection (bindings.cpp: a mouse sensitivity over 5),
// so the camera can turn faster than the game's fastest; its yaw and its clock's time noted for the mouse look test
void ScaleCameraTurn(f32* yaw, f32* pitch, s32 currentYaw, u32 time);
// The game's screen position page went back: true when the settings opened it (they're shown again)
bool ScreenPositionClosed();
void SettingsActForTest(u32 action);
void SettingsShotsFrame();
void SettingsMouseForTest(f32 x, f32 y, bool click);
void SettingsCursorForTest(f32 x, f32 y);
// Whether the mouse is over a line of the game's menus or one of their hints drawn last frame (the cursor's hover)
bool MouseOverAnyMenuLine();
u32 SettingsTabForTest();
s32 SettingsRowForTest();
bool ScriptMouseToLine(u32 index, bool click);
bool MouseOverLineForTest(const MenuPage* page);

// The tests' (--selftest-ui): options not saved, read again, events made up
void SetOptionSaving(bool saving);
void ResetOptionsForTest();
void SimulateFocus(bool focused);
void SimulateKey(s32 scancode, u32 modifiers);
bool FullscreenToggleDue();

// The audio's volumes (music, effects, voices) given again
void ApplyVolumes();


// --------------------------------------------------------------------------------------------------------------- the options
// The graphic options page's resolution items (olegpages.cpp): the IDs they're given, their values set when the page is entered and
// applied while it's shown
constexpr u32 ResolutionItem = 0x30;
constexpr u32 DisplayItem = 0x31;

// ------------------------------------------------------------------------------------------------------------------ commands
// The native build's UI commands on the command line (entry.cpp): --selftest-ui, --dump-texts, --pc-text-markdown. Whether the
// argument was one (and its exit status)
bool RunCommand(const char* argument, int* status);
}
