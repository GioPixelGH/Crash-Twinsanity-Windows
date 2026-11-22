// The settings screen's logic: the options in tabs (game, enhancements, display, audio, controls, accessibility, mods) as the newer
// Crash games on a computer have them (native/research/MODERN_CRASH_UI.md), its view apart (settingsview.cpp, on a canvas), opened in place of
// the old options pages (olegpages.cpp's options item, under TWIN_NATIVE). Each row is a label and its value (a choice stepped left
// and right, a slider, a toggle), the highlighted row's description under the list, every change applied at once and kept. The
// controls tab is a table of the game's actions by the keyboard's two keys and the controller's button. It takes the keyboard
// (arrows, Enter, Escape, Q/E or Tab for the tabs), the pads (the D-pad or the stick, A and B, the shoulders for the tabs) and the
// mouse (hover, click, the tabs and the arrows, a slider dragged, the wheel and the scrollbar scrolling the list) while it's open;
// the game reads nothing meanwhile
#include "ui/pctext.h"
#include "ui/settingsmodel.h"
#include "ui/overlay.h"

#include "native.h"
#include "graphics/fastrenderer.h"
#include "graphics/resolution.h"
#include "graphics/window.h"
#include "mods.h"
#include "renderdistance.h"
#include "updatecheck.h"

#include "game/controllers.h"
#include "game/gamecontroller.h"
#include "game/language.h"
#include "game/oleg.h"
#include "game/pads.h"
#include "game/renderer.h"
#include "game/sound.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
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
constexpr u32 TabTexts[Pages] = {TextTabGame,   TextTabEnhancements,  TextTabDisplay, TextTabAudio,
                                 TextControls,  TextTabAccessibility, TextTabMods};
constexpr u32 Columns = BindingColumns;
// The draw distance's choices from the shortest to the farthest, so left is less and right more (options.txt keeps
// renderdistance.h's codes, whatever this order)
constexpr u32 DistanceOrder[] = {Native::DistanceShort, Native::DistanceOriginal, Native::DistanceMedium, Native::DistanceLong,
                                 Native::DistanceUnlimited};
constexpr u32 DistanceTexts[] = {TextDistanceShort, TextDistanceOriginal, TextDistanceMedium, TextDistanceLong, TextUnlimited};
constexpr s32 DistanceChoices = static_cast<s32>(std::size(DistanceOrder));

// The anti-aliasing's choices by cost, so right is smoother and slower (options.txt keeps its numbers: 4 is SMAA)
constexpr s32 AntiAliasingOrder[] = {0, 1, 4, 2, 3};
constexpr u32 AntiAliasingTexts[] = {0, TextFxaa, TextSmaa, TextSsaa2, TextSsaa4};
constexpr s32 AntiAliasingChoices = static_cast<s32>(std::size(AntiAliasingOrder));

// The resolution chosen while its row is stepped (THE LIST 41, #39): applied once the choice settles (the row left, the screen
// closed, or PendingSeconds after the last step), not at each step (stepping left from 1x wraps round to 8x: a laptop's 451 MB
// card drew 8x and back while its player went through the choices). -1: none waiting
s32 g_PendingScale = -1;
std::chrono::steady_clock::time_point g_PendingAt;
constexpr f32 PendingSeconds = 0.75f;

void ApplyPendingResolution()
{
    if (g_PendingScale < 0)
    {
        return;
    }

    Resolution resolution = CurrentResolution();
    resolution.scale = Scales[std::clamp(g_PendingScale, 0, ScaleChoices - 1)];
    g_PendingScale = -1;
    SetResolution(resolution);
}

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

// A section's title (THE LIST 64), and a line of text
Row Section(u32 label)
{
    Row row{RowSection, label, 0, {}, {}, 0, 0, {}, {}, 0, {}, false};
    return row;
}

Row Note(u32 label)
{
    Row row{RowNote, label, 0, {}, {}, 0, 0, {}, {}, 0, {}, false};
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
void OpenDialog(PortableDialog dialog);
void Close();

std::string OverlayWords(const std::string& utf8);

// The disc row's (THE LIST 31d): the file dialog asked for, and its answer taken while the settings are open (TakeDiscAnswer)
bool g_DiscAsked = false;
std::string g_DiscNote;

// A release by its serial as the row names it: PAL's, the North American v2.00's
std::string DiscName(const std::string& serial)
{
    return (serial == "SLES-52568" ? std::string("PAL") : std::string("NTSC-U")) + " \xB7 " + serial;
}

void AskDisc()
{
    g_DiscAsked = true;
    Native::AskForDiscImage();
}

// The file dialog's answer: nothing when cancelled; else checked as the start checks an image (its data left as they are) and
// remembered for the next start, or refused with what the start would have said; a dialog says which
void TakeDiscAnswer()
{
    std::string image;
    if (!g_DiscAsked || !Native::TakeAskedDiscImage(&image))
    {
        return;
    }

    g_DiscAsked = false;
    if (image.empty())
    {
        Native::Log("disc image: none chosen (the file dialog cancelled)");
        return;
    }

    std::string serial;
    const std::string problem = Native::DiscImageGiven() ? std::string("This start's disc image is given by --iso, $TWINSANITY_ISO or "
                                                                       "local.json: change it there.")
                                                         : Native::CheckOtherDiscImage(image, &serial);
    if (!problem.empty())
    {
        Native::Log("disc image: not chosen: %s", Native::WithoutProfile(problem).c_str());
        g_DiscNote = OverlayWords(problem);
        OpenDialog(DialogDiscRefused);
        return;
    }

    Native::ChooseDiscImage(image, serial);
    std::u8string name = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(image.data()), image.size())).filename().u8string();
    g_DiscNote = DiscName(serial) + ": " + OverlayWords(std::string(name.begin(), name.end()));
    OpenDialog(Native::NextDiscSerial().empty() ? DialogDiscInUse : DialogDiscChosen);
}
// The level select's level (its row's choice, kept while the program runs)
s32 g_LevelSelectChoice = 0;

// The screen's row (THE LIST 88): its name cut to this many characters
constexpr size_t ScreenNameLength = 24;

// The display mode (window or fullscreen) and the window's size: the windows' sizes are the display choices but the last
s32 WindowSizeChoice()
{
    Resolution current = CurrentResolution();
    current.fullscreen = false;
    return DisplayChoice(current);
}

// ------------------------------------------------------------------------------------------------------------------ the mods
// A mod's words for the overlay: its Windows-1252, the prompts' glyphs ([ ] { } \ ^ ...) made plain characters
std::string OverlayWords(const std::string& utf8)
{
    std::string text;
    ToGameText(utf8.c_str(), &text);
    for (char& c : text)
    {
        if (c == '[' || c == '{')
        {
            c = '(';
        }
        else if (c == ']' || c == '}')
        {
            c = ')';
        }
        else if (std::strchr(PromptGlyphs, c) != nullptr)
        {
            c = ' ';
        }
    }

    return text;
}

// A description cut to the view's one line
std::string OneLine(std::string text)
{
    if (DescriptionWidth(text) <= DescriptionRoom)
    {
        return text;
    }

    while (!text.empty() && DescriptionWidth(text + "...") > DescriptionRoom)
    {
        text.pop_back();
    }

    return text + "...";
}

// The import under way on a thread of its own: each zip and disc image in turn, its progress and how it ended
struct ImportState
{
    std::mutex lock;
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};
    bool ok = true;
    std::string source;
    std::string failure;
    double fraction = 0.0;
};
ImportState g_Import;

void StartImport()
{
    if (g_Import.running)
    {
        return;
    }

    g_Import.running = true;
    g_Import.finished = false;
    g_Import.ok = true;
    g_Import.failure.clear();
    // (Left to end on its own: quitting during an import leaves its .importing- folder, removed by the next import)
    std::thread([] {
        for (const std::filesystem::path& source : Native::Mods::Importables())
        {
            {
                std::lock_guard lock(g_Import.lock);
                std::u8string name = source.filename().u8string();
                g_Import.source = std::string(name.begin(), name.end());
                g_Import.fraction = 0.0;
            }

            std::string error;
            auto progress = [](const std::string&, double fraction) {
                std::lock_guard lock(g_Import.lock);
                g_Import.fraction = fraction;
            };
            if (!Native::Mods::Import(source, progress, &error))
            {
                std::lock_guard lock(g_Import.lock);
                g_Import.ok = false;
                g_Import.failure = g_Import.source + ": " + error;
            }
        }

        g_Import.finished = true;
        g_Import.running = false;
    }).detach();
}

void OpenModsFolder()
{
    const std::filesystem::path folder = Native::Mods::Folder();
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);
    std::u8string path = folder.generic_u8string();
    const std::string url = "file:///" + std::string(path.begin(), path.end());
    Native::Log("mods: the mods folder opened");
    SDL_OpenURL(url.c_str());
}

// The Mods page: import (while there's something to), each mod off or on in its place (the first wins: "on · 1"), the folder
void BuildMods(std::vector<Row>& rows)
{
    // (The North American disc: the mods are listed, never used, mods.cpp)
    Row other = Note(TextNoteModsOtherDisc);
    other.shown = [] { return !Native::PalDisc(); };
    rows.push_back(other);
    Row import = Action(TextModsImport, TextDescModsImport, [] {
        StartImport();
        OpenDialog(DialogImporting);
    });
    // (Built again when an import ends: hidden then unless something's left to import, a failed one say)
    const bool importable = !Native::Mods::Importables().empty();
    import.shown = [importable] { return importable; };
    rows.push_back(import);
    for (const Native::Mods::Mod& mod : Native::Mods::List())
    {
        const std::string id = mod.id;
        auto index = [id]() -> s32 {
            const auto& list = Native::Mods::List();
            for (size_t i = 0; i < list.size(); i++)
            {
                if (list[i].id == id)
                {
                    return static_cast<s32>(i);
                }
            }

            return -1;
        };
        const s32 count = static_cast<s32>(Native::Mods::List().size());
        Row row = Choice(
            TextModsLoose, 0,
            [index] {
                const s32 at = index();
                return at >= 0 && Native::Mods::List()[static_cast<size_t>(at)].on ? at + 1 : 0;
            },
            [index](s32 value) {
                const s32 at = index();
                if (at < 0)
                {
                    return;
                }

                Native::Mods::SetOn(static_cast<size_t>(at), value != 0);
                if (value != 0)
                {
                    Native::Mods::MoveTo(static_cast<size_t>(at), static_cast<size_t>(value - 1));
                }
            },
            count, [](s32 value) { return value == 0 ? Text(0) : Text(1) + " \xB7 " + std::to_string(value); });
        if (!id.empty())
        {
            const std::string name = OverlayWords(mod.name.empty() ? id : mod.name);
            row.labelText = [name] { return name; };
        }

        row.descriptionText = [index] {
            const s32 at = index();
            if (at < 0)
            {
                return std::string();
            }

            const Native::Mods::Mod& mod = Native::Mods::List()[static_cast<size_t>(at)];
            if (!mod.refusal.empty())
            {
                return OneLine(Text(TextModRefused) + " " + OverlayWords(mod.refusal));
            }

            std::string info;
            for (const std::string& part : {mod.author, mod.version})
            {
                info += part.empty() ? "" : (info.empty() ? "" : ", ") + OverlayWords(part);
            }

            info += mod.description.empty() ? "" : (info.empty() ? "" : ": ") + OverlayWords(mod.description);
            const bool program = std::any_of(mod.warnings.begin(), mod.warnings.end(),
                                             [](const std::string& warning) { return warning.find("SLES_525.68") != std::string::npos; });
            if (program)
            {
                info = Text(TextModProgram) + (info.empty() ? "" : " " + info);
            }

            return OneLine(info.empty() ? Text(TextDescModsOpenFolder) : info);
        };
        rows.push_back(row);
    }

    rows.push_back(Action(TextModsOpenFolder, TextDescModsOpenFolder, OpenModsFolder));
}

void Build()
{
    if (g_Built)
    {
        return;
    }

    g_Built = true;
    // The game (THE LIST 64): the languages, the skip, the loading, the pause, the timer; then the program's own rows
    std::vector<Row>& game = g_Rows[TabGame];
    // The disc in use (THE LIST 31d: its release and serial), and another image chosen with the file dialog, played from the next
    // start: stepping the row (or selecting it) asks for one
    game.push_back(Choice(
        TextDisc, TextDescDisc, [] { return 0; }, [](s32) { AskDisc(); }, 1, [](s32) { return DiscName(Native::DiscSerial()); }));
    // The texts' language (and the voices', unless the next row chooses theirs: THE LIST 7). A slot whose texts are another
    // language by their content (#25's Portuguese in the English slot: languages.cpp) is named by that language. Both rows only
    // on the PAL disc: the North American disc has one language, its English (THE LIST 31)
    Row language = Choice(
        TextGameLanguage, TextDescGameLanguage, [] { return GetOption(OptionLanguage); },
        [](s32 value) { SetOption(OptionLanguage, value); }, 5, [](s32 value) {
            constexpr u32 Names[] = {TextLanguageWindows, TextLanguageEnglish, TextLanguageFrench, TextLanguageGerman,
                                     TextLanguageItalian, TextLanguageSpanish};
            const char* content = TextBankLanguage(value);
            if (content != nullptr && std::strcmp(content, "portuguese") == 0)
            {
                return Text(TextLanguagePortuguese);
            }

            if (content != nullptr && std::strcmp(content, "dutch") == 0)
            {
                return Text(TextLanguageDutch);
            }

            return Text(Names[std::clamp(value, 0, 5)]);
        });
    language.shown = [] { return Native::PalDisc(); };
    game.push_back(language);
    Row speech = OptionChoice(TextSpeechLanguage, TextDescSpeechLanguage, OptionSpeechLanguage,
                              {TextLanguageAsTexts, TextLanguageEnglish, TextLanguageFrench, TextLanguageGerman,
                               TextLanguageItalian, TextLanguageSpanish});
    speech.shown = [] { return Native::PalDisc(); };
    game.push_back(speech);
    game.push_back(Toggle(TextSkipCutscenes, TextDescSkip, OptionSkipCutscenes));
    game.push_back(Toggle(TextFastLoading, TextDescFastLoading, OptionFastLoading));
    game.push_back(Toggle(TextPauseInactive, TextDescPauseInactive, OptionPauseOnFocusLoss));
    // The speedrun timer (THE LIST 13)
    game.push_back(Toggle(TextSpeedrunTimer, TextDescSpeedrunTimer, OptionSpeedrunTimer));
    game.push_back(Section(TextSectionProgram));
    // The update check (off by default; Windows' only: updatecheck.h), and the newer release's page once one's found
    Row updates = Toggle(TextUpdateCheck, TextDescUpdateCheck, OptionUpdateCheck);
    updates.shown = [] { return Native::UpdateCheckSupported(); };
    game.push_back(updates);
    Row page = Action(TextUpdatePage, TextDescUpdatePage, [] { Native::OpenNewerReleasePage(); });
    page.shown = [] { return Native::NewerRelease(nullptr, nullptr); };
    game.push_back(page);
    game.push_back(Toggle(TextDetailedLog, TextDescDetailedLog, OptionDetailedLog));
    // Portable mode (portable.cpp; not an option of options.txt, which is in the folder it chooses): the mode in use, and stepping
    // it asks to switch (the dialog), the switch taking effect from the next start
    Row portable = Choice(
        TextPortable, TextDescPortable, [] { return Native::PortableMode() ? 1 : 0; }, [](s32 value) { AskPortable(value != 0); }, 1,
        [](s32 value) { return Text(value != 0 ? GameTextYes : GameTextNo); });
    portable.shown = [] { return Native::PortableSupported(); };
    game.push_back(portable);
    // The credits, and quitting to the desktop (#33: asked first, then as the window's close button)
    game.push_back(Action(TextCredits, TextDescCredits, [] { OpenDialog(DialogCredits); }));
    game.push_back(Action(TextQuitDesktop, TextDescQuitDesktop, [] { OpenDialog(DialogQuit); }));

    // The enhancements (THE LIST 64): the game options (gameoptions.h), each off by default, under a line saying they change the
    // original; the bug fixes, infinite lives (THE LIST 6) and the stronger Aku Aku mask (THE LIST 10, #30) as assists
    std::vector<Row>& enhancements = g_Rows[TabEnhancements];
    enhancements.push_back(Note(TextNoteEnhancements));
    enhancements.push_back(Section(TextSectionAssists));
    enhancements.push_back(Toggle(TextBugFixes, TextDescBugFixes, OptionRetailFixes));
    enhancements.push_back(Toggle(TextInfiniteLives, TextDescInfiniteLives, OptionInfiniteLives));
    enhancements.push_back(Toggle(TextStrongMask, TextDescStrongMask, OptionStrongMask));
    // Three masks' touch (THE LIST 54, #18)
    enhancements.push_back(Toggle(TextMaskTouch, TextDescMaskTouch, OptionMaskTouch));
    // The longer invincibility (THE LIST 93, #56)
    enhancements.push_back(Toggle(TextLongInvincibility, TextDescLongInvincibility, OptionLongInvincibility));
    // The slide jump (THE LIST 11, #11, #35): original, any moment, easier; the double jump (THE LIST 53, #47): original, later, any moment
    enhancements.push_back(Section(TextSectionMovement));
    enhancements.push_back(OptionChoice(TextSlideJump, TextDescSlideJump, OptionSlideJump,
                                        {TextDistanceOriginal, TextSlideJumpAnyMoment, TextSlideJumpEasier}));
    enhancements.push_back(OptionChoice(TextDoubleJump, TextDescDoubleJump, OptionDoubleJump,
                                        {TextDistanceOriginal, TextDoubleJumpLater, TextSlideJumpAnyMoment}));
    // The automatic camera (THE LIST 55, #42, PR #45): free (0), behind the character by itself (1) or assisted (2, THE LIST 83)
    enhancements.push_back(Section(TextSectionCamera));
    enhancements.push_back(OptionChoice(TextAutoCamera, TextDescAutoCamera, OptionAutoCamera, {0, 1, TextCameraAssisted}));
    // The improved menus (THE LIST 79, #48, improvedmenus.cpp)
    enhancements.push_back(Section(TextSectionMenus));
    enhancements.push_back(Toggle(TextImprovedMenus, TextDescImprovedMenus, OptionImprovedMenus));
    // The pause menu's restart checkpoint row (THE LIST 85; with the improved menus it comes second)
    enhancements.push_back(Toggle(TextRestartCheckpoint, TextDescRestartCheckpoint, OptionRestartCheckpoint));
    // The level select (THE LIST 12, levelselect.cpp): the option, then (on) the level chosen and its start from the main menu, or
    // a line saying the main menu is where it starts from (THE LIST 80: also the gems' pages, in the main menu and the pause menu)
    enhancements.push_back(Section(TextSectionLevels));
    enhancements.push_back(Toggle(TextLevelSelect, TextDescLevelSelect, OptionLevelSelect));
    Row levelSelectLevel = Choice(
        TextLevelSelectLevel, TextDescLevelSelectLevel, [] { return g_LevelSelectChoice; },
        [](s32 value) { g_LevelSelectChoice = value; }, static_cast<s32>(LevelCount) - 1,
        [](s32 value) { return Text(LevelNameTexts + static_cast<u32>(value)); });
    levelSelectLevel.shown = [] { return IsOn(OptionLevelSelect); };
    enhancements.push_back(levelSelectLevel);
    Row levelSelectStart = Action(TextLevelSelectStart, TextDescLevelSelectLevel, [] {
        if (CanStartLevel())
        {
            Close();
            StartLevel(static_cast<u32>(g_LevelSelectChoice));
        }
    });
    levelSelectStart.shown = [] { return CanStartLevel(); };
    enhancements.push_back(levelSelectStart);
    Row levelSelectElsewhere = Action(TextLevelSelectFromMainMenu, TextDescLevelSelectFromMainMenu, [] {});
    levelSelectElsewhere.shown = [] { return IsOn(OptionLevelSelect) && !CanStartLevel(); };
    enhancements.push_back(levelSelectElsewhere);

    // The display (THE LIST 64): the window, then the picture
    std::vector<Row>& display = g_Rows[TabDisplay];
    display.push_back(Section(TextSectionWindow));
    // The screen (THE LIST 88): automatic (the main one) or a connected screen, its number and name; shown with two screens or
    // more, or one chosen (it may not be connected: automatic then, kept for when it is)
    Row screen = Choice(
        TextScreen, TextDescScreen, [] { return ScreenChoice(CurrentResolution()); },
        [](s32 value) { SetResolution(WithScreenChoice(CurrentResolution(), value)); }, 1, [](s32 value) {
            if (value <= 0)
            {
                return Text(TextAutomatic);
            }

            std::string name = OverlayWords(NativeGraphicsScreenName(value - 1));
            name = name.size() > ScreenNameLength ? name.substr(0, ScreenNameLength - 3) + "..." : name;
            return std::to_string(value) + "  \xB7  " + name;
        });
    screen.maximumNow = [] { return ScreenCount(); };
    screen.asksToKeep = true;
    screen.shown = [] { return ScreenCount() >= 2 || !CurrentResolution().screen.empty(); };
    display.push_back(screen);
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
    // (Its choice applied once it settles, not at each step: THE LIST 41, PendingScale)
    display.push_back(Choice(
        TextResolution, TextDescResolution,
        [] { return g_PendingScale >= 0 ? g_PendingScale : ScaleChoice(CurrentResolution()); },
        [](s32 value) {
            g_PendingScale = value;
            g_PendingAt = std::chrono::steady_clock::now();
        },
        ScaleChoices - 1, [](s32 value) {
            // The scale and the picture's size it draws at (the PAL frame's 640 x 512 times it): "2×  ·  1280 × 1024"
            s32 scale = Scales[value];
            return std::to_string(scale) + "x  \xB7  " + std::to_string(640 * scale) + "x" +
                   std::to_string(512 * scale);
        }));
    display.push_back(Choice(
        // The TV's shape: 4:3, 16:10 (THE LIST 43, the Steam Deck's), 16:9 (the game's widescreen), 21:9 or 32:9 (the game's
        // widescreen made narrower or wider, fixes.cpp's Widescreen1610, Ultrawide and SuperUltrawide)
        GameTextWidescreen, TextDescWidescreen,
        [] {
            return g_WidescreenTv == 0           ? 0
                   : IsOn(OptionSuperUltrawide) ? 4
                   : IsOn(OptionUltrawide)      ? 3
                   : IsOn(OptionAspect1610)     ? 1
                                                : 2;
        },
        [](s32 value) {
            g_WidescreenTv = static_cast<u8>(value != 0 ? 1 : 0);
            SetOption(OptionWidescreen, value != 0 ? 1 : 0);
            SetOption(OptionAspect1610, value == 1 ? 1 : 0);
            SetOption(OptionUltrawide, value == 3 ? 1 : 0);
            SetOption(OptionSuperUltrawide, value == 4 ? 1 : 0);
        },
        4, [](s32 value) {
            static const char* const Shapes[] = {"4:3", "16:10", "16:9", "21:9", "32:9"};
            return std::string(Shapes[value]);
        }));
    display.push_back(OptionChoice(TextRefreshRate, TextDescRefreshRate, OptionRefresh60,
                                   {Text50Hz, Text60Hz, Text120Hz, Text144Hz, TextUnlimited}));
    display.push_back(Toggle(TextVSync, TextDescVSync, OptionVSync));
    display.push_back(Toggle(TextShowFps, TextDescShowFps, OptionShowFps));
    display.push_back(Action(GameTextCentreScreen, TextDescScreenPosition, [] { OpenScreenPosition(); }));
    display.push_back(Section(TextSectionPicture));
    display.push_back(OptionChoice(TextFiltering, TextDescFiltering, OptionTextureFilter, {TextSharp, TextSmooth}));
    // (The texture upscale isn't shown until the graphics have it: NativeGraphicsSetTextureUpscale takes only off.) The
    // anti-aliasing is FXAA on the shown picture, or supersampling (the render scale 2 or 4 times the resolution's, up to 8x
    // the PS2's, the picture made smaller as it's shown); there's no MSAA: PCSX2 dropped its own
    display.push_back(Choice(
        TextAntiAliasing, TextDescAntiAliasing,
        [] {
            const s32 value = GetOption(OptionAntiAliasing);
            const auto* at = std::find(std::begin(AntiAliasingOrder), std::end(AntiAliasingOrder), value);
            return at != std::end(AntiAliasingOrder) ? static_cast<s32>(at - std::begin(AntiAliasingOrder)) : 0;
        },
        [](s32 value) { SetOption(OptionAntiAliasing, AntiAliasingOrder[value]); }, AntiAliasingChoices - 1,
        [](s32 value) { return Text(AntiAliasingTexts[value]); }));
    // Anisotropic filtering (THE LIST 9: off by default, the PS2's sampling)
    display.push_back(Choice(
        TextAnisotropic, TextDescAnisotropic, [] { return GetOption(OptionAnisotropy); },
        [](s32 value) { SetOption(OptionAnisotropy, value); }, 4,
        [](s32 value) { return value == 0 ? Text(0) : std::to_string(1 << value) + "x"; }));
    display.push_back(OptionChoice(TextDithering, TextDescDithering, OptionDithering, {TextDitherPs2, 0}));
    display.push_back(Choice(
        TextRenderDistance, TextDescRenderDistance,
        [] {
            const u32 code = static_cast<u32>(GetOption(OptionRenderDistance));
            const auto* at = std::find(std::begin(DistanceOrder), std::end(DistanceOrder), code);
            return at != std::end(DistanceOrder) ? static_cast<s32>(at - std::begin(DistanceOrder)) : 1;
        },
        [](s32 value) { SetOption(OptionRenderDistance, static_cast<s32>(DistanceOrder[value])); }, DistanceChoices - 1,
        [](s32 value) { return Text(DistanceTexts[value]); }));
    display.push_back(Toggle(TextTexturePack, TextDescTexturePack, OptionTexturePack));
    display.push_back(Toggle(TextCrt, TextDescCrt, OptionCrtFilter));
    // The renderer (THE LIST 73): the graphics API, OpenGL or Vulkan, from the next start (graphicsapi.cpp: the other one taken when
    // one can't show the game); in a build that has Vulkan
    Row renderer = Section(TextSectionRenderer);
    renderer.shown = [] { return NativeGraphicsVulkanBuilt() || NativeGraphicsFastRendererShown(); };
    display.push_back(renderer);
    Row api = Choice(
        TextGraphicsApi, TextDescGraphicsApi, [] { return GetOption(OptionGraphicsApi); },
        [](s32 value) { SetOption(OptionGraphicsApi, value); }, 1,
        [](s32 value) { return std::string(value != 0 ? "vulkan" : "opengl"); });
    api.shown = [] { return NativeGraphicsVulkanBuilt(); };
    display.push_back(api);
    // The renderer's kind (THE LIST 74): accurate (the GS emulated, the default) or fast (experimental), at once; hidden until the
    // fast one draws some kind of drawing itself (graphics/fastrenderer.h)
    Row kind = OptionChoice(TextRendererKind, TextDescRendererKind, OptionFastRenderer, {TextAccurate, TextFastExperimental});
    kind.shown = [] { return NativeGraphicsFastRendererShown(); };
    display.push_back(kind);

    std::vector<Row>& audio = g_Rows[TabAudio];
    // The master volume first (THE LIST 81): options.txt's 0 to 100 in steps of 5, the slider's 0 to 20 shown as the percent
    Row master = Slider(
        TextMasterVolume, TextDescMasterVolume, [] { return (GetOption(OptionMasterVolume) + 2) / 5; },
        [](s32 value) { SetOption(OptionMasterVolume, value * 5); }, 0, 20);
    master.valueText = [](s32 value) { return std::to_string(value * 5); };
    audio.push_back(master);
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

    // The controls (THE LIST 64): the device (the vibration, the prompts, the hints, the sticks' dead zone), the mouse, the camera
    // (from Accessibility), then the keys' table
    std::vector<Row>& controls = g_Rows[TabControls];
    controls.push_back(Section(TextSectionDevice));
    controls.push_back(Choice(
        GameTextVibration, TextDescVibration,
        [] {
            auto* pads = static_cast<GamePadController*>(G_GamePadController);
            return pads != nullptr && pads->flags.vibration != 0 ? 1 : 0;
        },
        SetVibration, 1, [](s32 value) { return Text(value != 0 ? 1 : 0); }));
    controls.push_back(OptionChoice(TextController, TextDescController, OptionController,
                                    {TextAutomatic, TextKeyboard, TextXbox, TextPlayStation, TextSwitch}));
    Row layout = OptionChoice(TextNintendoLayout, TextDescLayout, OptionNintendoLayout, {TextPositional, TextNintendo});
    layout.shown = [] { return CurrentPromptStyle() == PromptSwitch; };
    controls.push_back(layout);
    controls.push_back(OptionChoice(TextMenuHints, TextDescMenuHints, OptionMenuHints, {TextHintsOriginal, TextHintsBar}));
    // (Both sticks', so the device's rather than the camera's)
    controls.push_back(OptionSlider(TextDeadZone, TextDescDeadZone, OptionStickDeadZone, 0, 10));
    controls.push_back(Section(TextSectionMouse));
    controls.push_back(Toggle(TextMouse, TextDescMouse, OptionMouse));
    controls.push_back(Toggle(TextMouseLook, TextDescMouseLook, OptionMouseLook));
    Row sensitivity = OptionSlider(TextMouseSensitivity, TextDescMouseSensitivity, OptionMouseSensitivity, 1, 10);
    sensitivity.shown = [] { return IsOn(OptionMouseLook); };
    controls.push_back(sensitivity);
    Row invertMouse = Toggle(TextInvertMouseY, TextDescInvertMouseY, OptionInvertMouseY);
    invertMouse.shown = [] { return IsOn(OptionMouseLook); };
    controls.push_back(invertMouse);
    controls.push_back(Section(TextSectionCamera));
    controls.push_back(Toggle(TextInvertX, TextDescInvertX, OptionInvertCameraX));
    controls.push_back(Toggle(TextInvertY, TextDescInvertY, OptionInvertCameraY));
    controls.push_back(OptionSlider(TextCameraSpeed, TextDescCameraSpeed, OptionCameraSpeed, 1, 10));

    // The accessibility: the camera's shake, and the shadows that show where things are (the drop shadow, THE LIST 14, #11, #35,
    // off, on or outlined, THE LIST 52; Aku Aku's, THE LIST 84, #51; the shadows on crates, THE LIST 38)
    std::vector<Row>& access = g_Rows[TabAccessibility];
    access.push_back(Toggle(TextCameraShake, TextDescCameraShake, OptionCameraShake));
    access.push_back(OptionChoice(TextDropShadow, TextDescDropShadow, OptionDropShadow, {0, 1, TextDropShadowOutlined}));
    access.push_back(Toggle(TextMaskShadow, TextDescMaskShadow, OptionMaskShadow));
    access.push_back(Toggle(TextShadowsOnCrates, TextDescShadowsOnCrates, OptionShadowsOnCrates));

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
        if (tab != TabMods)
        {
            g_Rows[tab].push_back(Action(TextResetDefaults, TextDescResetDefaults, nullptr));
        }
    }

    BuildMods(g_Rows[TabMods]);
}

bool Shown(const Row& row)
{
    return !row.shown || row.shown();
}

bool Selectable(const Row& row)
{
    return row.kind != RowHeader && row.kind != RowSection && row.kind != RowNote && Shown(row);
}


// ------------------------------------------------------------------------------------------------------------------ the state
bool g_Open = false;
// The settings opened from play (THE LIST 65): the ask's time (steady clock ticks, 0 none), to resume when they close, and the
// resume due for the game's pause
std::atomic<s64> g_SettingsAskedAt{0};
bool g_ResumeOnClose = false;
std::atomic<bool> g_ResumeDue{false};
u32 g_Tab = TabGame;
s32 g_Row = 0;
u32 g_Column = 0;
f32 g_Scroll = 0.0f;
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
// The asked dialog's button the keys and the controller are on (left yes, right no): no at first, so a press by chance
// doesn't switch
bool g_DialogOnYes = false;
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
// The wheel turned since the last frame (notches, fractions from a fine wheel or a touchpad; away from the player positive)
f32 g_Wheel = 0.0f;
bool g_Dragging = false;
// The list's scroll asked for the view (THE LIST 107: SettingsScrollAsk), the thumb held (the pointer's distance from its top,
// the track's top and the thumb's travel, reference pixels), and until when the row under the pointer is highlighted again as the
// rows move under it after a turn of the wheel
SettingsScrollAsk g_ScrollAsk = {0.0f, 0, -1.0f};
bool g_ThumbHeld = false;
f32 g_ThumbGrab = 0.0f;
f32 g_ThumbTrackTop = 0.0f;
f32 g_ThumbTravel = 0.0f;
std::chrono::steady_clock::time_point g_HoverAgainUntil;
// Changes by the keys or the controller (SettingsView's keyMoves)
u32 g_KeyMoves = 0;
// The live preview's hide held by the mouse on its prompt (until the button's let go)
bool g_MouseHiding = false;
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
            g_ThumbHeld = false;
            g_MouseHiding = false;
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

        g_Wheel += event->wheel.y;
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
                      OptionTextureUpscale, OptionAntiAliasing, OptionVSync, OptionRenderDistance});
        ResetOptions({OptionAnisotropy});
        ResetOptions({OptionSuperUltrawide, OptionAspect1610, OptionGraphicsApi});
        break;
    case TabAudio:
        SetMusic(DefaultOption(OptionMusicVolume));
        SetEffects(DefaultOption(OptionEffectsVolume));
        SetMusicStereo(static_cast<u32>(DefaultOption(OptionOutputType)));
        ResetOptions({OptionVoiceVolume, OptionOutputType, OptionMuteInactive, OptionMasterVolume});
        break;
    case TabGame:
        ResetOptions({OptionLanguage, OptionSpeechLanguage, OptionSkipCutscenes, OptionFastLoading, OptionPauseOnFocusLoss,
                      OptionSpeedrunTimer, OptionUpdateCheck, OptionDetailedLog});
        break;
    case TabEnhancements:
        ResetOptions({OptionRetailFixes, OptionInfiniteLives, OptionStrongMask, OptionMaskTouch, OptionLongInvincibility,
                      OptionSlideJump, OptionDoubleJump, OptionAutoCamera, OptionImprovedMenus, OptionRestartCheckpoint,
                      OptionLevelSelect});
        break;
    case TabControls:
        // The options, and the keys and buttons as the table's own two resets put them back
        SetVibration(DefaultOption(OptionVibration));
        ResetOptions({OptionController, OptionNintendoLayout, OptionMenuHints, OptionStickDeadZone, OptionMouse, OptionMouseLook,
                      OptionMouseSensitivity, OptionInvertMouseY, OptionInvertCameraX, OptionInvertCameraY, OptionCameraSpeed});
        ResetKeyboardBindings();
        ResetControllerBindings();
        break;
    case TabAccessibility:
        ResetOptions({OptionCameraShake, OptionDropShadow, OptionMaskShadow, OptionShadowsOnCrates});
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
        const s32 maximum = row.maximumNow ? row.maximumNow() : row.maximum;
        next = next > maximum ? row.minimum : next < row.minimum ? maximum : next;
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

    // From the selected row's line; a hidden one's (the Mods page's import, once done) is between the lines round it, so the next
    // step lands on the line after it (or before), not one further
    s32 before = 0;
    s32 at = -1;
    for (s32 i = 0; i < static_cast<s32>(visible.size()); i++)
    {
        if (visible[static_cast<size_t>(i)] == g_Row)
        {
            at = i;
        }
        else if (visible[static_cast<size_t>(i)] < g_Row)
        {
            before++;
        }
    }

    if (at < 0)
    {
        at = by > 0 ? before - 1 : before;
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

// The Mods page's rows made again from the list (an import ended); the selection on the page's first mod if it was on the import
void RebuildMods()
{
    std::vector<Row>& rows = g_Rows[TabMods];
    rows.clear();
    BuildMods(rows);
    if (g_Tab == TabMods)
    {
        g_Row = std::min(g_Row, static_cast<s32>(rows.size()) - 1);
        if (!Selectable(rows[static_cast<size_t>(g_Row)]))
        {
            MoveRow(1);
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
    ApplyPendingResolution();
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
    // Opened from play (F1, Select+Start: THE LIST 65): back to play, as the pause menu's resume (the game's Paused takes it)
    if (g_ResumeOnClose && controller->State() == GameController::StatePaused)
    {
        g_ResumeOnClose = false;
        g_ResumeDue = true;
        return;
    }

    g_ResumeOnClose = false;
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
        case HitKind::ScrollTrack:
        case HitKind::ScrollThumb:
            // (over the rows' right end: the scrollbar's, not the row's)
            hit.row = -1;
            hit.column = -1;
            hit.part = 0;
            break;
        default:
            break;
        }

        hit.region = &region;
    }

    // (The regions are last frame's: the Mods page's rows may have been made again since, fewer)
    if (hit.row >= static_cast<s32>(g_Rows[g_Tab].size()))
    {
        hit.row = -1;
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

// The scrollbar's track or thumb (last frame's; none when the rows fit)
const HitRegion* KindRegion(HitKind kind)
{
    for (const HitRegion& region : g_Regions)
    {
        if (region.kind == kind)
        {
            return &region;
        }
    }

    return nullptr;
}

// A click on the scrollbar (THE LIST 107): on the thumb it's held (dragged while the button is), on the track above or below it
// a page that way
void ClickScrollbar(const HitRegion& clicked)
{
    const HitRegion* track = KindRegion(HitKind::ScrollTrack);
    const HitRegion* thumb = KindRegion(HitKind::ScrollThumb);
    if (track == nullptr || thumb == nullptr)
    {
        return;
    }

    if (clicked.kind == HitKind::ScrollThumb)
    {
        g_ThumbHeld = true;
        g_ThumbGrab = g_MouseY - thumb->y;
        g_ThumbTrackTop = track->y;
        g_ThumbTravel = track->height - thumb->height;
    }
    else
    {
        g_ScrollAsk.pages += g_MouseY < thumb->y ? -1 : 1;
    }
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
    g_DialogOnYes = false;
    std::string why;
    if (portable && !Native::PortableFolderUsable(&why))
    {
        Native::Log("portable mode can't be turned on: the program's folder %s", why.c_str());
        g_Dialog = DialogRefused;
    }
}

// The quit's question (no chosen first) or the credits
void OpenDialog(PortableDialog dialog)
{
    g_Dialog = dialog;
    g_DialogOnYes = false;
}

// The dialog's input and the copy followed. Asked: left and right (or the mouse over a button) choose yes or no, select takes the
// chosen one, back is no, a click takes its button; yes copies. Refused and failed go away on select or back; done closes the
// game on either, or by itself after a few seconds (the old folder gets nothing more either way). The quit's question as asked
// (yes closes the game as the window's close button does: the saves and settings are written as they're made); the credits go
// away on select or back
void TakeDialogInput()
{
    Hit hit = HitTest();
    bool asked = g_Dialog == DialogAsk || g_Dialog == DialogQuit;
    if (asked && hit.region != nullptr && (g_MouseMoved || g_MouseClicked) &&
        (hit.region->kind == HitKind::DialogYes || hit.region->kind == HitKind::DialogNo))
    {
        g_DialogOnYes = hit.region->kind == HitKind::DialogYes;
    }

    if (asked && (Took(UiLeft) || Took(UiRight)))
    {
        g_DialogOnYes = Took(UiLeft);
    }

    bool clicked = g_MouseClicked && hit.region != nullptr;
    bool selected = Took(UiConfirm);
    bool yes = (selected && (!asked || g_DialogOnYes)) || (clicked && hit.region->kind == HitKind::DialogYes);
    bool no = (selected && asked && !g_DialogOnYes) || Took(UiBack) || g_MouseRightClicked ||
              (clicked && hit.region->kind == HitKind::DialogNo);
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
    case DialogQuit:
        if (yes)
        {
            LogSettingChanges();
            Native::EndSession("quit: to the desktop from the options");
            std::_Exit(0);
        }
        else if (no)
        {
            g_Dialog = DialogNone;
        }

        break;
    case DialogImporting:
        if (g_Import.finished)
        {
            {
                std::lock_guard lock(g_Import.lock);
                g_Dialog = g_Import.ok ? DialogImported : DialogImportFailed;
            }

            // The imported mods on the page at once (used from the next start)
            Native::Mods::Refresh();
            RebuildMods();
        }

        break;
    case DialogRefused:
    case DialogFailed:
    case DialogCredits:
    case DialogImported:
    case DialogImportFailed:
    case DialogDiscChosen:
    case DialogDiscInUse:
    case DialogDiscRefused:
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
    TakeDiscAnswer();
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

    // Portable mode's dialog: the list takes nothing meanwhile (the stick's left and right choose its button)
    if (g_Dialog != DialogNone)
    {
        PollHeldDirections();
        TakeDialogInput();
        g_Actions = 0;
        g_MouseClicked = false;
        g_MouseRightClicked = false;
        g_MouseMoved = false;
        g_Wheel = 0;
        return;
    }

    PollHeldDirections();
    // The mouse: hovering selects, a click picks (or steps a choice by its arrow, sets a slider, picks a tab, pages the list or
    // holds its scrollbar's thumb), the wheel scrolls the list and never changes a value (THE LIST 107: the row under the
    // pointer highlighted again as the rows move under it), a right click goes back
    const bool hoverAgain = std::chrono::steady_clock::now() < g_HoverAgainUntil;
    if (IsOn(OptionMouse) &&
        (g_MouseMoved || g_MouseClicked || g_Wheel != 0 || g_MouseDown || g_MouseRightClicked || hoverAgain))
    {
        Hit hit = HitTest();
        if ((g_MouseMoved || hoverAgain) && !g_ThumbHeld && hit.row >= 0 && Selectable(rows[static_cast<size_t>(hit.row)]))
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
                case HitKind::FooterHide:
                    g_MouseHiding = g_MouseDown;
                    break;
                case HitKind::ScrollTrack:
                case HitKind::ScrollThumb:
                    ClickScrollbar(*hit.region);
                    break;
                default:
                    break;
                }
            }
        }

        if (g_ThumbHeld && g_MouseDown && g_ThumbTravel > 0.0f)
        {
            f32 at = (g_MouseY - g_ThumbGrab - g_ThumbTrackTop) / g_ThumbTravel;
            g_ScrollAsk.thumbAt = at < 0.0f ? 0.0f : at > 1.0f ? 1.0f : at;
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
            g_ScrollAsk.notches += g_Wheel;
            g_HoverAgainUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
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

    // (a key's or the controller's action brings the selected row into view: the view's keyMoves)
    if (g_Actions != 0)
    {
        g_KeyMoves++;
    }

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

// The live preview (THE LIST 66): the tabs whose changes show on the game's picture (the paused game's scene is drawn again each
// frame: GameContext::GameEndFrame; the main menu's is its own)
static bool PreviewTab(u32 tab)
{
    return tab == TabDisplay || tab == TabAccessibility;
}

// Its hide held: H, the last pad's right trigger past half, or the mouse on its prompt (never over a question)
static bool PreviewHidden()
{
    if (g_Capturing || g_Keeping || g_Dialog != DialogNone)
    {
        return false;
    }

    const bool* keys = SDL_GetKeyboardState(nullptr);
    SDL_Gamepad* gamepad = LastGamepad();
    return g_MouseHiding || (keys != nullptr && keys[SDL_SCANCODE_H]) ||
           (gamepad != nullptr && SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16384);
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
    view.keyMoves = g_KeyMoves;
    view.preview = PreviewTab(g_Tab);
    view.hidden = view.preview && PreviewHidden();
    view.dialog = g_Dialog;
    view.dialogPortable = g_DialogPortable;
    view.dialogOnYes = g_DialogOnYes;
    if (g_Dialog == DialogImporting || g_Dialog == DialogImported || g_Dialog == DialogImportFailed)
    {
        std::lock_guard lock(g_Import.lock);
        view.copied = static_cast<u64>(g_Import.fraction * 1000.0);
        view.copyTotal = 1000;
        view.backup = OverlayWords(g_Import.source);
        view.failure = g_Import.failure;
    }
    else if (g_Dialog == DialogDiscChosen || g_Dialog == DialogDiscInUse || g_Dialog == DialogDiscRefused)
    {
        view.backup = g_Dialog == DialogDiscRefused ? std::string() : g_DiscNote;
        view.failure = g_Dialog == DialogDiscRefused ? g_DiscNote : std::string();
    }
    else if (g_Dialog != DialogNone)
    {
        Native::PortableSwitchState(&view.copied, &view.copyTotal, &view.backup, &view.failure);
    }

    f32 done = std::chrono::duration<f32>(std::chrono::steady_clock::now() - g_DoneAt).count();
    view.closeSecondsLeft = g_Dialog == DialogDone ? static_cast<s32>(CloseSeconds - done + 0.999f) : 0;
    return view;
}

const std::vector<Row>& SettingsRows(u32 page)
{
    Build();
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

f32& SettingsScroll()
{
    return g_Scroll;
}

SettingsScrollAsk TakeSettingsScroll()
{
    SettingsScrollAsk ask = g_ScrollAsk;
    g_ScrollAsk = {0.0f, 0, -1.0f};
    return ask;
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
    g_ThumbHeld = false;
    g_ScrollAsk = {0.0f, 0, -1.0f};
    // (the selected row brought into view, as a key would)
    g_KeyMoves++;
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

void AskSettingsFromPlay()
{
    g_SettingsAskedAt = std::chrono::steady_clock::now().time_since_epoch().count();
}

bool TakeSettingsPause()
{
    const s64 asked = g_SettingsAskedAt.exchange(0);
    if (asked == 0 || g_Open)
    {
        return false;
    }

    // An ask left from a menu or a load (no pause took it then) is forgotten
    const auto since = std::chrono::steady_clock::now().time_since_epoch() - std::chrono::steady_clock::duration(asked);
    return since < std::chrono::milliseconds(500);
}

void OpenSettingsFromPlay()
{
    OpenSettings();
    g_ResumeOnClose = g_Open;
    Native::Log("settings: opened from play (F1 or Select+Start), the game paused");
}

bool TakeSettingsResume()
{
    return g_ResumeDue.exchange(false);
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

// $TWIN_DISC_TEST (the checks, THE LIST 31d; $TWIN_TEST_PICK the image the file dialog answers): from the Vivendi logo to the
// main menu, there the settings opened on the game's tab and the disc row stepped; the dialog its answer brings logged ("disc
// test:"), then the program ends
void DiscTestFrame()
{
    static const bool test = std::getenv("TWIN_DISC_TEST") != nullptr;
    static u32 step = 0;
    GameController* controller = G_GameController;
    if (!test || controller == nullptr)
    {
        return;
    }

    if (step == 0 && controller->State() == GameController::StateVivendiLogo)
    {
        controller->SetNextState(GameController::StateMainMenu);
    }
    else if (step == 0 && controller->State() == GameController::StateMainMenu)
    {
        OpenSettings();
        SelectTab(TabGame);
        for (Row& row : g_Rows[TabGame])
        {
            if (row.label == TextDisc)
            {
                Native::Log("disc test: the disc row stepped, the disc in use %s", Native::DiscSerial().c_str());
                Step(row, 1);
                step = 1;
            }
        }
    }
    else if (step == 1 && g_Dialog != DialogNone)
    {
        Native::Log("disc test: %s, the next start's disc %s",
                    g_Dialog == DialogDiscChosen    ? "chosen"
                    : g_Dialog == DialogDiscInUse   ? "the one in use chosen"
                    : g_Dialog == DialogDiscRefused ? "refused"
                                                    : "another dialog",
                    Native::NextDiscSerial().empty() ? "the one in use" : Native::NextDiscSerial().c_str());
        std::exit(0);
    }
}

// $TWIN_SETTINGS_MOUSE_TEST (the checks, THE LIST 107): from the Vivendi logo to the main menu, there the settings opened on the
// Enhancements tab (its list longer than the panel) and the mouse driven as the window's events would: the pointer on a row's
// arrow, the wheel turned down and up, the track clicked below the thumb, the thumb dragged past both ends, then the keys down
// the list; on the audio tab the wheel over a slider and a click on its bar. Every row's value compared after each, the list's
// scroll and the highlighted row checked; "settings mouse test: pass" or what failed, then the program ends
static std::vector<s32> RowValues()
{
    std::vector<s32> values;
    for (const Row& row : g_Rows[g_Tab])
    {
        values.push_back((row.kind == RowChoice || row.kind == RowSlider) && row.get ? row.get() : 0);
    }

    return values;
}

// The first region of a kind (of a row, or any row's with row -1) from a height down, or none
static const HitRegion* RegionPast(HitKind kind, f32 below, s32 row = -1)
{
    for (const HitRegion& region : g_Regions)
    {
        if (region.kind == kind && region.y >= below && (row < 0 || region.row == row))
        {
            return &region;
        }
    }

    return nullptr;
}

static void MouseTestFrame()
{
    // (the view's row pitch)
    constexpr f32 RowPitchForTest = 48.0f;
    static const bool test = std::getenv("TWIN_SETTINGS_MOUSE_TEST") != nullptr;
    static u32 step = 0;
    static std::chrono::steady_clock::time_point at;
    static std::vector<s32> values;
    static std::string failed;
    static std::string said;
    static s32 presses = 0;
    GameController* controller = G_GameController;
    if (!test || controller == nullptr)
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    auto fail = [&](const std::string& what) {
        failed += (failed.empty() ? "" : "; ") + what;
    };
    auto settle = [&](u32 next) {
        step = next;
        at = now;
    };
    auto settled = [&]() {
        return now - at > std::chrono::milliseconds(600);
    };
    auto put = [&](f32 x, f32 y, bool click) {
        SettingsMouseForTest(x / ReferenceWidth, y / ReferenceHeight, click);
    };
    // (the wheel and the left button's release as the window's events, through the screen's event watch)
    auto send = [&](SDL_EventType type, f32 wheel) {
        SDL_Event event = {};
        event.type = type;
        if (type == SDL_EVENT_MOUSE_WHEEL)
        {
            event.wheel.y = wheel;
            event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        }
        else
        {
            event.button.button = SDL_BUTTON_LEFT;
        }

        WatchSettings(nullptr, &event);
    };
    auto sameValues = [&](const char* after) {
        if (RowValues() != values)
        {
            fail(std::string("a value changed after ") + after);
        }
    };
    auto underPointer = [&](const char* after) {
        Hit hit = HitTest();
        if (hit.row >= 0 && Selectable(g_Rows[g_Tab][static_cast<size_t>(hit.row)]) && g_Row != hit.row)
        {
            fail(std::string("the row under the pointer not highlighted after ") + after);
        }
    };

    if (step == 0 && controller->State() == GameController::StateVivendiLogo)
    {
        controller->SetNextState(GameController::StateMainMenu);
        return;
    }

    if (step == 0 && controller->State() == GameController::StateMainMenu)
    {
        OpenSettings();
        SelectTab(TabEnhancements);
        settle(1);
        return;
    }

    // The keys: down ten rows, one a frame as the keyboard's
    if (step == 14)
    {
        if (presses++ < 10)
        {
            Act(UiDown);
            at = now;
        }
        else if (settled())
        {
            settle(8);
        }

        return;
    }

    if (step == 0 || !settled())
    {
        return;
    }

    const HitRegion* track = KindRegion(HitKind::ScrollTrack);
    switch (step)
    {
    case 1:
    {
        // The pointer on the right arrow of a choice two rows down (or the row itself)
        values = RowValues();
        const HitRegion* row = track != nullptr ? RegionPast(HitKind::Row, track->y + RowPitchForTest * 2.0f) : nullptr;
        if (track == nullptr || row == nullptr)
        {
            fail("the Enhancements tab's list fits the panel (no scrollbar)");
            settle(9);
            break;
        }

        const HitRegion* arrow = RegionPast(HitKind::ArrowRight, row->y, row->row);
        put(arrow != nullptr ? arrow->x + arrow->width * 0.5f : row->x + row->width * 0.5f, row->y + row->height * 0.5f, false);
        said = arrow != nullptr ? "over a choice's arrow" : "over a row";
        settle(2);
        break;
    }
    case 2:
        if (g_Row != HitTest().row)
        {
            fail("the hovered row not highlighted");
        }

        send(SDL_EVENT_MOUSE_WHEEL, -2.0f);
        settle(3);
        break;
    case 3:
        sameValues("the wheel turned down");
        underPointer("the wheel turned down");
        if (SettingsScroll() <= 0.0f)
        {
            fail("the wheel turned down didn't scroll");
        }

        said += ", down " + std::to_string(static_cast<s32>(SettingsScroll()));
        send(SDL_EVENT_MOUSE_WHEEL, 2.0f);
        settle(4);
        break;
    case 4:
        sameValues("the wheel turned up");
        underPointer("the wheel turned up");
        if (SettingsScroll() != 0.0f)
        {
            fail("the wheel turned up didn't scroll back to the top");
        }

        put(track->x + track->width * 0.5f, track->y + track->height - 4.0f, true);
        settle(5);
        break;
    case 5:
    {
        sameValues("a click on the track");
        said += ", a page " + std::to_string(static_cast<s32>(SettingsScroll()));
        if (SettingsScroll() <= 0.0f)
        {
            fail("a click on the track didn't page");
        }

        const HitRegion* thumb = KindRegion(HitKind::ScrollThumb);
        if (thumb == nullptr)
        {
            fail("no thumb");
            settle(9);
            break;
        }

        put(thumb->x + thumb->width * 0.5f, thumb->y + thumb->height * 0.5f, true);
        g_MouseDown = true;
        settle(6);
        break;
    }
    case 6:
    {
        // Held: dragged past the track's bottom, the list at its end
        put(track->x + track->width * 0.5f, track->y + track->height + 60.0f, false);
        g_MouseDown = true;
        if (now - at > std::chrono::milliseconds(1200))
        {
            said += ", the thumb to the end " + std::to_string(static_cast<s32>(SettingsScroll()));
            settle(7);
        }

        break;
    }
    case 7:
    {
        put(track->x + track->width * 0.5f, track->y - 60.0f, false);
        g_MouseDown = true;
        if (now - at > std::chrono::milliseconds(1200))
        {
            send(SDL_EVENT_MOUSE_BUTTON_UP, 0.0f);
            if (g_ThumbHeld)
            {
                fail("the thumb still held after the button's release");
            }

            sameValues("the thumb dragged");
            if (SettingsScroll() != 0.0f)
            {
                fail("the thumb dragged to the top didn't scroll to the top");
            }

            said += " and back " + std::to_string(static_cast<s32>(SettingsScroll()));
            settle(14);
        }

        break;
    }
    case 8:
    {
        // The keys' row in view (its region inside the track's height)
        const HitRegion* row = RegionPast(HitKind::Row, -1.0f, g_Row);
        if (row == nullptr || row->y < track->y - 4.0f || row->y + row->height > track->y + track->height + 4.0f)
        {
            fail("the keys' row not brought into view");
        }

        said += ", the keys' row in view at " + std::to_string(static_cast<s32>(SettingsScroll()));
        SelectTab(TabAudio);
        settle(10);
        break;
    }
    case 10:
    {
        const HitRegion* slider = RegionPast(HitKind::Slider, -1.0f);
        if (slider == nullptr)
        {
            fail("no slider on the audio tab");
            settle(9);
            break;
        }

        values = RowValues();
        put(slider->x + slider->width * 0.5f, slider->y + slider->height * 0.5f, false);
        send(SDL_EVENT_MOUSE_WHEEL, 1.0f);
        settle(11);
        break;
    }
    case 11:
        send(SDL_EVENT_MOUSE_WHEEL, -1.0f);
        settle(12);
        break;
    case 12:
    {
        sameValues("the wheel over a slider");
        const HitRegion* slider = RegionPast(HitKind::Slider, -1.0f);
        Row& row = g_Rows[g_Tab][static_cast<size_t>(slider->row)];
        put(slider->x + slider->width * 0.25f, slider->y + slider->height * 0.5f, true);
        g_MouseDown = true;
        said += ", the slider's value " + std::to_string(row.get());
        settle(13);
        break;
    }
    case 13:
    {
        send(SDL_EVENT_MOUSE_BUTTON_UP, 0.0f);
        const HitRegion* slider = RegionPast(HitKind::Slider, -1.0f);
        Row& row = g_Rows[g_Tab][static_cast<size_t>(slider->row)];
        const s32 wanted = row.minimum + static_cast<s32>(0.25f * static_cast<f32>(row.maximum - row.minimum) + 0.5f);
        if (row.get() != wanted)
        {
            fail("a click on a slider's bar didn't set it (" + std::to_string(row.get()) + ", not " + std::to_string(wanted) + ")");
        }

        said += ", clicked at a quarter " + std::to_string(row.get());
        settle(9);
        break;
    }
    default:
        Native::Log("settings mouse test: %s (%s)", failed.empty() ? "pass" : ("FAILED: " + failed).c_str(), said.c_str());
        std::exit(0);
    }
}

// The update's part: the input taken against last frame's regions
void SettingsFrame()
{
    DiscTestFrame();
    MouseTestFrame();
    if (!g_Open || G_GameController == nullptr)
    {
        return;
    }

    TakeInput();
    // The resolution stepped to: applied once the row's left or the steps stop (THE LIST 41)
    if (g_PendingScale >= 0)
    {
        const std::vector<Row>& rows = g_Rows[g_Tab];
        const bool onRow = g_Open && g_Row >= 0 && static_cast<size_t>(g_Row) < rows.size() && rows[g_Row].label == TextResolution;
        const f32 since = std::chrono::duration<f32>(std::chrono::steady_clock::now() - g_PendingAt).count();
        if (!onRow || since >= PendingSeconds)
        {
            ApplyPendingResolution();
        }
    }
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
void DrawAfterOleg(Renderer* renderer)
{
    // The speedrun timer (the game option) in the game's own font
    SpeedrunDraw(renderer);
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

void SettingsKeyForTest(s32 scancode)
{
    // As the keyboard's key goes (a binding waiting for one takes it)
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = static_cast<SDL_Scancode>(scancode);
    WatchSettings(nullptr, &event);
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
