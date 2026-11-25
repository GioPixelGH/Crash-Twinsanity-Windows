#pragma once

#include <string>
#include <vector>

// The native build's own UI changes, called from the game's code under TWIN_NATIVE: the PC wording of the language files' console
// texts (pctext.cpp, native/PC_TEXT.md), the native menu texts the resolution setting uses, and the resolution setting itself
// (resolution.cpp)
#include "common.h"

class MenuPage;
class MenuWidget;
struct GccVTableEntry;
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
    TextTabGame,
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
    // Portable mode (settings.cpp's switch and its dialog)
    TextPortable,
    TextDescPortable,
    TextPortableAskOn,
    TextPortableAskOff,
    TextPortableCopying,
    TextPortableDone,
    TextPortableBackup,
    TextPortableRefused,
    TextPortableFailed,
    TextOk,
    // The dithering option (settings.cpp's Display page)
    TextDithering,
    TextDescDithering,
    TextDitherPs2,
    // Anti-aliasing's supersampling (settings.cpp's Display page)
    TextSsaa2,
    TextSsaa4,
    // The draw distance option (settings.cpp's Display page; its unlimited choice is the refresh rate's TextUnlimited)
    TextRenderDistance,
    TextDescRenderDistance,
    TextDistanceShort,
    TextDistanceOriginal,
    TextDistanceMedium,
    TextDistanceLong,
    // Quitting to the desktop (#33) and the credits (settings.cpp's Gameplay page, settingsview.cpp's dialogs)
    TextQuitDesktop,
    TextDescQuitDesktop,
    TextQuitAsk,
    TextCredits,
    TextDescCredits,
    TextCreditsWindows,
    TextCreditsDecompilation,
    TextCreditsMac,
    TextCreditsImproved,
    TextCreditsLinux,
    TextCreditsTesters,
    TextCreditsTextures,
    TextCreditsEvolved,
    // The speech language (settings.cpp's Gameplay page) and the languages a text bank can be in besides the five (languages.cpp)
    TextSpeechLanguage,
    TextDescSpeechLanguage,
    TextLanguageAsTexts,
    TextLanguagePortuguese,
    TextLanguageDutch,
    // SMAA (the Display page)
    TextSmaa,
    // Anisotropic filtering (the Display page)
    TextAnisotropic,
    TextDescAnisotropic,
    // The update check (the Gameplay page) and its notice in the main menu
    TextUpdateCheck,
    TextDescUpdateCheck,
    TextUpdatePage,
    TextDescUpdatePage,
    TextUpdateNotice,
    // The game options (settings.cpp's Gameplay page): infinite lives
    TextInfiniteLives,
    TextDescInfiniteLives,
    // The stronger Aku Aku mask
    TextStrongMask,
    TextDescStrongMask,
    // The automatic camera
    TextAutoCamera,
    TextDescAutoCamera,
    // The slide jump and its choices
    TextSlideJump,
    TextDescSlideJump,
    TextSlideJumpAnyMoment,
    TextSlideJumpEasier,
    // The level select: the option (its name the older TextLevelSelect), the level chosen, starting it, or where it starts from
    TextDescLevelSelect,
    TextLevelSelectLevel,
    TextDescLevelSelectLevel,
    TextLevelSelectStart,
    TextLevelSelectFromMainMenu,
    TextDescLevelSelectFromMainMenu,
    // The speedrun timer
    TextSpeedrunTimer,
    TextDescSpeedrunTimer,
    // The drop shadow
    TextDropShadow,
    TextDescDropShadow,
    // The shadows on crates
    TextShadowsOnCrates,
    TextDescShadowsOnCrates,
    // The credits' thanks (settingsview.cpp's Credits)
    TextCreditsThanks,
    // The Mods page (settings.cpp; mods.h)
    TextTabMods,
    TextModsImport,
    TextDescModsImport,
    TextModsOpenFolder,
    TextDescModsOpenFolder,
    TextModsLoose,
    TextModsImporting,
    TextModsImported,
    TextModsImportFailed,
    TextModRefused,
    TextModProgram,
    // The credits: the automatic camera's (THE LIST 55, PR #45)
    TextCreditsCamera,
    // The settings' tabs reordered (THE LIST 64): the Enhancements tab and its warning line, and the tabs' sections' titles
    TextTabEnhancements,
    TextNoteEnhancements,
    TextSectionProgram,
    TextSectionAssists,
    TextSectionMovement,
    TextSectionCamera,
    TextSectionLevels,
    TextSectionWindow,
    TextSectionPicture,
    TextSectionDevice,
    TextSectionMouse,
    // The master volume (THE LIST 81)
    TextMasterVolume,
    TextDescMasterVolume,
    // The live preview's prompt (THE LIST 66): the menu hidden while its button is held
    TextHideHold,
    // The drop shadow's third choice (THE LIST 52, #11): with a ring around it
    TextDropShadowOutlined,
    // Aku Aku's shadow (THE LIST 84, #51)
    TextMaskShadow,
    TextDescMaskShadow,
    // The double jump (THE LIST 53, #47): the row, its description and its "later" (its "any moment" the slide jump's)
    TextDoubleJump,
    TextDescDoubleJump,
    TextDoubleJumpLater,
    // Three masks' touch (THE LIST 54, #18)
    TextMaskTouch,
    TextDescMaskTouch,
    // The automatic camera's third choice (THE LIST 83, #42, #45)
    TextCameraAssisted,
    // The improved menus (THE LIST 79, #48): the row, its description and its section; the pause menu's return to title, its
    // row to the gems' pages and return to title's confirmation
    TextImprovedMenus,
    TextDescImprovedMenus,
    TextSectionMenus,
    TextReturnToTitle,
    TextGems,
    TextReturnToTitleAsk,
    // The level select's confirmation on the gems' pages (THE LIST 80, #48)
    TextPlayLevel,
    // The pause menu's restart checkpoint row as a game option (THE LIST 85): its description (its name the row's own text)
    TextDescRestartCheckpoint,
    // The screen the game is shown on (THE LIST 88): the row and its description
    TextScreen,
    TextDescScreen,
    // The graphics API (THE LIST 73): Display's renderer section, the row and its description
    TextSectionRenderer,
    TextGraphicsApi,
    TextDescGraphicsApi,
    // The improved menus' main menu: continue, the newest save loaded (THE LIST 63a, #48)
    TextContinue,
    // The longer invincibility (THE LIST 93, #56): the row and its description
    TextLongInvincibility,
    TextDescLongInvincibility,
    // The renderer's kind (THE LIST 74): the row, its description and its two choices
    TextRendererKind,
    TextDescRendererKind,
    TextAccurate,
    TextFastExperimental,
    // The Mods page's note on the North American disc (THE LIST 31): the mods aren't used there
    TextNoteModsOtherDisc,
    // The disc row (THE LIST 31d): the row, its description, and its dialogs' messages (chosen for the next start, the one in use,
    // not a disc image the game plays)
    TextDisc,
    TextDescDisc,
    TextDiscChosen,
    TextDiscInUse,
    TextDiscRefused,
    // The improved main menu's quit to desktop's confirmation (THE LIST 63c, #33; the row is TextQuitDesktop)
    TextQuitDesktopAsk,
    // The credits' part for the port's texts in Brazilian Portuguese (#25, NormanTheFox's translation)
    TextCreditsTranslation,
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
// The render scale (1 to 8 times the PS2's picture: the choices' scales) and the display modes: windows of fixed sizes, then
// fullscreen
constexpr s32 ScaleChoices = 6;
constexpr s32 Scales[ScaleChoices] = {1, 2, 3, 4, 6, 8};
// A scale as one of the choices': the biggest choice not past it (resolution.txt written by hand, or by another version)
s32 ChoiceScale(s32 scale);
constexpr s32 DisplayChoices = TextFullscreen - TextWindow960 + 1;

struct Resolution
{
    s32 scale;
    s32 windowWidth;
    s32 windowHeight;
    bool fullscreen;
    // The screen chosen (THE LIST 88): its name (SDL_GetDisplayName's) and which of the screens of that name it is (1 the first),
    // saved by name since a screen's number can change from one session to the next; no name: automatic, the main screen
    std::string screen;
    s32 screenOrder = 1;
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
// The screens (THE LIST 88): how many are connected (0 without a window: a test's run, or the self-test), a resolution's screen
// as their number (0 the first; -1 automatic, or its screen not connected), the settings row's choice of it (0 automatic, else
// the screen's number from 1), a resolution with a choice's screen, and the start's screen (resolution.txt's, the log told when
// it isn't connected), which the window is made on
s32 ScreenCount();
s32 ScreenIndex(const Resolution& resolution);
s32 ScreenChoice(const Resolution& resolution);
Resolution WithScreenChoice(const Resolution& resolution, s32 choice);
s32 StartScreen();
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
// screen, light on a laptop's graphics) in a 1280 x 720 window (16:9 like the widescreen default, and it fits a 1366 x 768 laptop);
// on a Steam Deck (Native::SteamDeckModel) fullscreen, its 1280 x 800 screen, at 2x too
Resolution DefaultResolution();
Resolution DefaultResolutionFor(bool steamDeck);


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
    // the screen until the graphics have it; the texture pack; anti-aliasing 0 off, 1 FXAA, 2 and 3 SSAA 2x and 4x, 4 SMAA; v-sync; the music and effects volumes
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
    // The game's language (its texts, and its voices and movies' sound unless OptionSpeechLanguage chooses another): the
    // computer's (0, as the PS2 took its own), English, French, German, Italian or Spanish; the game takes it when it starts
    // (system.cpp's Platform::System::Language)
    OptionLanguage,
    // More of what the game does written to the log (trail.cpp), for bug reports
    OptionDetailedLog,
    // The game's menus' button hints: the game's own (0: OLEG's labels, their glyphs the controller's) or the native bar (1)
    OptionMenuHints,
    // A frames per second counter in the window's top left corner (fpscounter.cpp)
    OptionShowFps,
    // The picture shown: the PS2's (0: the frame copied into a 16 bit buffer with the GS's 4x4 dither, 5 bits a channel), or
    // the frame's own full colour (1: the hardware renderer's dithered copy neither dithered nor truncated; the software
    // renderer draws into the PS2's 16 bits either way)
    OptionDithering,
    // How far the game draws (renderdistance.h, HAB3X's): the original (0), short (1: 0.6 times the distances), medium (2: twice),
    // long (3: three times) or unlimited (4: no level of detail's cut-off, the far plane four times); codes, not the menu's order
    OptionRenderDistance,
    // The voices' language (languages.cpp): as the texts' (0, the language option's), or English, French, German, Italian or
    // Spanish (the language option's numbers); the game takes it when it starts
    OptionSpeechLanguage,
    // Anisotropic filtering of the game's mipmapped textures (hwgs.cpp's ANISO): off (0, the PS2's sampling), 2x, 4x, 8x, 16x
    OptionAnisotropy,
    // 32:9: the widescreen picture made 32:9 wide (as 21:9's: the projection, the 2D's squeeze, the movies' pillarbox); only while
    // widescreen is on, the 21:9 option off
    OptionSuperUltrawide,
    // The update check (updatecheck.h): at each start, GitHub asked once for the latest release (off by default)
    OptionUpdateCheck,
    // The game options (gameoptions.h, all off by default): infinite lives
    OptionInfiniteLives,
    // The stronger Aku Aku mask (#30)
    OptionStrongMask,
    // The automatic camera: the look stick and the mouse no longer turn the follow camera, which swings behind the character itself
    OptionAutoCamera,
    // The slide jump (#11, #35): 0 the original, 1 any moment, 2 easier
    OptionSlideJump,
    // The level select (levelselect.cpp: from the main menu, a memory card of its own)
    OptionLevelSelect,
    // The speedrun timer (speedrun.cpp)
    OptionSpeedrunTimer,
    // The drop shadow under the player (shadows.cpp's CastDropShadow): 0 off, 1 on, 2 outlined (THE LIST 52)
    OptionDropShadow,
    // The player's shadow on crates' tops too
    OptionShadowsOnCrates,
    // The master volume (THE LIST 81): the whole output's gain, 0 to 100 (audio.cpp's stream)
    OptionMasterVolume,
    // Aku Aku's shadow (THE LIST 84, #51)
    OptionMaskShadow,
    // The double jump (THE LIST 53, #47): original, later, any moment
    OptionDoubleJump,
    // Three masks' touch (THE LIST 54, #18): what the invincible character touches hit as by a spin
    OptionMaskTouch,
    // The 16:10 TV (THE LIST 43): the game's widescreen picture made 16:10, the Steam Deck's screen (on by default there)
    OptionAspect1610,
    // The improved menus (THE LIST 79, #48: improvedmenus.cpp): the pause menu from its first row, restart checkpoint higher, the
    // gems' pages and the extras as rows, return to title
    OptionImprovedMenus,
    // The pause menu's restart checkpoint row (THE LIST 85: HAB3X's row, the PS2's pause menu has none; improvedmenus.cpp shows it)
    OptionRestartCheckpoint,
    // The graphics API (THE LIST 73: graphics/graphicsapi.cpp): OpenGL 0 (the default), Vulkan 1, from the next start; the other
    // one taken at a start where this one can't show the game or didn't at the last start
    OptionGraphicsApi,
    // The longer invincibility (THE LIST 93, #56): three masks' 20 s instead of 8
    OptionLongInvincibility,
    // The renderer's kind (THE LIST 74, graphics/fastrenderer.h): accurate 0 (the default), fast 1 (experimental: the kinds of
    // drawing it has drawn without the GS's emulation); $TWIN_FAST_RENDERER over it
    OptionFastRenderer,
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
// The game's trail in the log, a frame at a time (trail.cpp); the chunk the player was last seen in (nullptr: none yet), and where
// the game is now ("the game's state playing (12), the player in levels\earth\hub\beach at (x, y, z)"), for the log's end lines
void TrailFrame();
const char* TrailChunk();
std::string TrailNow();
// The camera shown and the player's follow camera, three lines in the log starting with WHY (sweep.cpp: $TWIN_CAMERA_TRACE's
// lines, and the detailed log's when the view stands still while the player moves)
void LogCamera(const char* why);
// The log's start (sessionlog.cpp): every option as options.txt names it, and whether they're options.txt's or the defaults (a
// first start), in two lines (options.cpp); the saved resolution or the defaults (resolution.cpp)
void LogOptions();
// The languages (languages.cpp): a text choice's bank's language by its content when it isn't the choice's name ("portuguese";
// nullptr when it is, or isn't recognised; option 1-5), and the log's line about them (the disc open)
const char* TextBankLanguage(s32 option);
// Whether the text bank the game loads by that name (its Language folder's: "English") is written in Portuguese (#25's disc's
// English slot): the port's own texts then shown in Brazilian Portuguese where they have it (pctext.cpp, THE LIST 56)
bool TextBankPortuguese(const char* language);
void LogLanguages();
void LogResolution();
// The settings changed since the log last gave them, a line each ("options: refresh_60hz 3 -> 1 (144 Hz -> 60 Hz)"): options.txt's
// (options.cpp), the resolution and the display (resolution.cpp), the controls (bindings.cpp). Each change's value is compared with
// the one logged before, so a value stepped through and left isn't one; logged once the settings screen is closed (options.cpp's
// Frame) and at the session's end
void LogSettingChanges();
void LogOptionChanges();
void LogResolutionChanges();
void LogBindingChanges();
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
// The level select (levelselect.cpp, the game option): the game's levels (their names the code file's texts from LevelNameTexts),
// each one's first chunk, whether a level can be started now (the option on, the main menu), a level started (its saves on a card
// of their own until the title), and its frame's work (the player's card again at the title)
constexpr u32 LevelCount = 16;
constexpr u32 LevelNameTexts = 0x47;
const char* LevelChunk(u32 level);
bool CanStartLevel();
void StartLevel(u32 level);
// From play (the level select's gems' pages in the pause menu, THE LIST 80): back to the title, then the level started from the
// main menu as StartLevel does
void StartLevelFromPlay(u32 level);
void LevelSelectFrame();
// The improved menus and the level select's gems' pages (improvedmenus.cpp, THE LIST 79 and 80, #48), called from the game's menus:
// the quit screen made the quit's again (it's also the level confirmation's); a pause menu row asking for a pause page (the
// gems' first or the extras: a reason) and the pause page asked for once (ReasonNone none); the main menu's level select opening
// the gems' pages; a level of them picked (its area: the confirmation, the level select on; the levels' vtable that asks it) and
// the level confirmed; the last
// area whose level's shown (every one with the level select on); back from the gems' pages or the extras to the pause menu (the
// improved menus: whether it went there); the main menu's frame while the gems' pages opened from it are up (whether they are);
// the frame's work (the rows each layout shows, the level select's hint); the menus test's buttons ($TWIN_MENUS_TEST)
void QuitScreenAsQuit();
void AskPausePage(u32 reason, u32 fromItem);
u32 TakePausePage();
void OpenLevelsFromMainMenu();
// The improved menus' main menu's continue (THE LIST 63a, #48): its row activated (the load it leads to takes the newest save:
// savecode.cpp's NativeTakeContinue)
void ContinueChosen();
// The improved main menu's quit to desktop (THE LIST 63c, #33): its row asks on the quit screen ("quit to desktop?", no first),
// its yes ends the program as the window's close button does
void AskQuitToDesktop();
void QuitToDesktop();
void PickLevel(u32 area);
// The gems' pages' levels' vtable (the link's, its activation PickLevel)
const GccVTableEntry* LevelItemVTable();
void PlayPickedLevel();
u32 LevelsOpen(u32 open);
bool BackToPauseMenuInstead(MenuWidget* menu);
bool MainMenuLevelsFrame();
void ImprovedMenusFrame();
u32 MenusTestButtons();
// The speedrun timer (speedrun.cpp, the game option): its frame's work (started at New Game, a split at each level's end, stopped
// at the credits) and its text queued in the game's font after OLEG's widgets
void SpeedrunFrame();
void SpeedrunDraw(Renderer* renderer);
// Once after the window lost the focus (the option on): the game pauses as for its start button
bool TakeFocusPause();
// The settings opened from play (THE LIST 65): F1, or Select+Start on a pad, asks for them (AskSettingsFromPlay, the ask kept half
// a second); the game's pause takes the ask once in play or a cutscene (TakeSettingsPause: it pauses as for its start button) and
// opens the settings over the pause (OpenSettingsFromPlay); closed, the pause resumes (TakeSettingsResume, once: the pause menu's
// resume) instead of showing the pause menu
void AskSettingsFromPlay();
bool TakeSettingsPause();
void OpenSettingsFromPlay();
bool TakeSettingsResume();
// With the cutscene skip option, "hold any button to skip" (features.cpp): any key, mouse button or pad button pressed after the
// scene began and held 1 s. A poll from a scene that can be skipped (the skip condition's, a movie's frame's): whether it's
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
// The frame rate the frames are paced at (50, 60, 120, 144; 0 as fast as they come): the option's, but 60 past 60 while the
// player is in a chase that still goes wrong there (ChaseRateFrame)
s32 FrameRate();
// THE LIST 29c (#44, #17): the frame rate held at 60 while the player is in Bandicoot Pursuit's chunks or the walrus chase's, if
// the option's is past 60, and the option's again once the player is elsewhere (a log line each way); every frame
void ChaseRateFrame();
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
// A test's script takes the mouse: the PC's own mouse is left out from then on (a hand on it can't click the menus)
void ScriptMouseTakeOver();
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
// A key pressed while the settings screen is open, as the keyboard's (a binding waiting for one takes it)
void SettingsKeyForTest(s32 scancode);
void SettingsShotsFrame();
// $TWIN_SCREEN_TEST=1, a window's run (THE LIST 88): the screen's choice changed while the game runs (resolution.cpp)
void ScreenTestFrame();
void SettingsMouseForTest(f32 x, f32 y, bool click);
void SettingsCursorForTest(f32 x, f32 y);
// Whether the mouse is over a line of the game's menus or one of their hints drawn last frame (the cursor's hover)
bool MouseOverAnyMenuLine();
u32 SettingsTabForTest();
s32 SettingsRowForTest();
// Portable mode's dialog shown (settingsmodel.h's PortableDialog), and its test ($TWIN_PORTABLE_TEST: portabletest.cpp)
u32 PortableDialogForTest();
void PortableTestFrame();
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
