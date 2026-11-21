// The save and load test (testing, $TWIN_SAVE_TEST, with --headless): the game's own menus driven by what's on them (each wanted
// item found by its text), every page's items logged ("savetest:").
//   save   from --quick's beach: play a few seconds, pause, "save game", the first slot (and "yes" when it asks to overwrite);
//          once the save's file is written, the saved chunk is logged and the run exits 0
//   load   past the logos and the title to the main menu: "load game", the first slot (and "yes"/"continue" when asked); once
//          play starts, the chunk it started in is logged, it plays a few seconds and exits 0
// $TWIN_SAVE_TEST_SECONDS: how long to play first (save) or after (load), default 6. It exits 5 when a step takes over 120 s
#include "ui/bindings.h"

#include "native.h"

#include "game/gamecontroller.h"
#include "game/menus.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace NativeUi
{
namespace
{
using Clock = std::chrono::steady_clock;

enum class Mode
{
    None,
    Save,
    Load,
};

// A menu item wanted: by its text in the text table, or the first save slot (the slots page's items: their text an empty string,
// the slot's summary drawn in its place); an optional one is skipped when no page shows it for a few seconds
struct Want
{
    s32 text;
    bool optional;
};
constexpr s32 FirstSlot = -1;

bool g_Read = false;
Mode g_Mode = Mode::None;
f32 g_Seconds = 6.0f;
std::vector<Want> g_Wants;
size_t g_Next = 0;
const MenuPage* g_LastPage = nullptr;
const MenuPage* g_PageWhenWanted = nullptr;
Clock::time_point g_Start;
Clock::time_point g_StepStart;
Clock::time_point g_PlayStart;
bool g_Playing = false;
u32 g_Phase = 0;
bool g_PressStart = false;

void ReadSettings()
{
    if (g_Read)
    {
        return;
    }

    g_Read = true;
    const char* mode = std::getenv("TWIN_SAVE_TEST");
    std::string text = mode != nullptr ? mode : "";
    g_Mode = text == "save" ? Mode::Save : text == "load" ? Mode::Load : Mode::None;
    if (const char* seconds = std::getenv("TWIN_SAVE_TEST_SECONDS"))
    {
        g_Seconds = static_cast<f32>(std::atof(seconds));
    }

    // The game's texts: save game 0x19, load game 0x0A, yes 0x03, continue 0x05
    if (g_Mode == Mode::Save)
    {
        g_Wants = {{0x19, false}, {FirstSlot, false}, {0x03, true}};
    }
    else if (g_Mode == Mode::Load)
    {
        g_Wants = {{0x0A, false}, {FirstSlot, false}, {0x03, true}, {0x05, true}};
    }

    g_Start = Clock::now();
    g_StepStart = g_Start;
}

f32 Since(Clock::time_point from)
{
    return std::chrono::duration<f32>(Clock::now() - from).count();
}

[[noreturn]] void Finish(int status, const char* why)
{
    Native::Log("savetest: %s (%.1f s)", why, Since(g_Start));
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(status);
}

bool Selectable(const MenuItem* item, u32 player)
{
    return item != nullptr && ((item->shown >> player) & 1) != 0 && ((item->enabled >> player) & 1) != 0;
}

void LogPage(const MenuPage* page, u32 player)
{
    std::string items;
    for (u32 i = 0; i < page->items.count; i++)
    {
        const MenuItem* item = page->items.data[i];
        char one[96];
        std::snprintf(one, sizeof(one), " [%u: text %#x%s%s%s]", i, item->bits.text, item->text != nullptr ? " \"" : "",
                      item->text != nullptr ? item->text : "", item->text != nullptr ? "\"" : "");
        items += one;
        if (!Selectable(item, player))
        {
            items += "(off)";
        }
    }

    Native::Log("savetest: page %p, selected %u:%s", static_cast<const void*>(page), page->selections[player], items.c_str());
}

// The memory card folder's files' latest write
std::filesystem::file_time_type LatestSave()
{
    std::filesystem::file_time_type latest{};
    std::error_code error;
    std::filesystem::path folder = std::filesystem::path(Native::SettingsFolder()) / Native::MemoryCardName();
    for (auto it = std::filesystem::recursive_directory_iterator(folder, error); !error && it != std::filesystem::recursive_directory_iterator();
         it.increment(error))
    {
        if (it->is_regular_file(error))
        {
            latest = std::max(latest, it->last_write_time(error));
        }
    }

    return latest;
}

std::filesystem::file_time_type g_SavesBefore{};
// The pause menu's page (the save is over once it's back)
const MenuPage* g_PausePage = nullptr;
}

bool SaveTestActive()
{
    ReadSettings();
    return g_Mode != Mode::None;
}

bool SaveTestMenuStep(MenuPage* page, MenuInput* input, u32 player)
{
    if (!SaveTestActive() || input == nullptr)
    {
        return false;
    }

    if (page != g_LastPage)
    {
        g_LastPage = page;
        LogPage(page, player);
    }

    if (g_Next >= g_Wants.size())
    {
        return false;
    }

    const Want& want = g_Wants[g_Next];
    s32 found = -1;
    for (u32 i = 0; i < page->items.count && found < 0; i++)
    {
        const MenuItem* item = page->items.data[i];
        if (!Selectable(item, player))
        {
            continue;
        }

        bool slot = item->bits.text == 0 && item->text != nullptr && item->text[0] == '\0';
        if (want.text == FirstSlot ? slot : static_cast<s32>(item->bits.text) == want.text)
        {
            found = static_cast<s32>(i);
        }
    }

    if (found < 0)
    {
        if (want.optional && Since(g_StepStart) > 4.0f)
        {
            Native::Log("savetest: no %#x shown, going on", want.text);
            g_Next++;
            g_StepStart = Clock::now();
        }

        return false;
    }

    // Selected once the page has been up a moment (its fade)
    if (Since(g_StepStart) < 0.6f)
    {
        return false;
    }

    page->selections[player] = static_cast<u8>(found);
    input->pressed.value = static_cast<u8>(input->pressed.value | (1u << MenuInput::ActionSelect));
    input->held.value = static_cast<u8>(input->held.value | (1u << MenuInput::ActionSelect));
    Native::Log("savetest: selected item %d (text %#x) on page %p", found, page->items.data[found]->bits.text,
                static_cast<const void*>(page));
    g_PageWhenWanted = page;
    if (g_Next == 0)
    {
        g_PausePage = page;
    }

    g_Next++;
    g_StepStart = Clock::now();
    return true;
}

void SaveTestFrame()
{
    if (!SaveTestActive())
    {
        return;
    }

    GameController* controller = G_GameController;
    if (controller == nullptr)
    {
        return;
    }

    u32 state = controller->State();
    if (Since(g_StepStart) > 120.0f)
    {
        char why[64];
        std::snprintf(why, sizeof(why), "stuck (state %u, step %zu)", state, g_Next);
        Finish(5, why);
    }

    if (g_Mode == Mode::Save)
    {
        switch (g_Phase)
        {
        case 0:
            // Playing: a few seconds, then start pressed
            if (state == GameController::StatePlaying)
            {
                if (!g_Playing)
                {
                    g_Playing = true;
                    g_PlayStart = Clock::now();
                    Native::Log("savetest: playing in %s", controller->progress.startChunk.string != nullptr ? controller->progress.startChunk.string : "?");
                }
                else if (Since(g_PlayStart) > g_Seconds)
                {
                    g_SavesBefore = LatestSave();
                    g_PressStart = true;
                    g_Phase = 1;
                    g_StepStart = Clock::now();
                }
            }

            break;
        case 1:
            if (state == GameController::StatePaused)
            {
                g_PressStart = false;
                g_Phase = 2;
                g_StepStart = Clock::now();
                Native::Log("savetest: paused");
            }

            break;
        case 2:
            // The wanted items picked (SaveTestMenuStep); done once a file was written and the pause menu is back (the save's
            // screens over: the bank, then the folder's summaries written)
            if (g_Next >= 2 && LatestSave() > g_SavesBefore && g_LastPage == g_PausePage)
            {
                g_Phase = 3;
                g_StepStart = Clock::now();
                Native::Log("savetest: written, back at the pause menu");
            }

            break;
        case 3:
            if (Since(g_StepStart) > 3.0f)
            {
                Native::Log("savetest: saved chunk %s, place %u", controller->saveController.chunk.string != nullptr ? controller->saveController.chunk.string : "?",
                            controller->saveController.place);
                Finish(0, "saved");
            }

            break;
        default:
            break;
        }

        return;
    }

    // Load: past the logos and the title (as --quick does) to the main menu
    switch (state)
    {
    case GameController::StateVivendiLogo:
        controller->SetNextState(GameController::StateLoadingTitle);
        break;
    case GameController::StateTitle:
        if (g_Phase == 0)
        {
            g_Phase = 1;
            controller->SetNextState(GameController::StateMainMenu);
        }

        break;
    case GameController::StatePlaying:
        if (!g_Playing)
        {
            g_Playing = true;
            g_PlayStart = Clock::now();
            Native::Log("savetest: playing, loaded chunk %s, place %u",
                        controller->progress.startChunk.string != nullptr ? controller->progress.startChunk.string : "?",
                        controller->saveController.place);
        }
        else if (Since(g_PlayStart) > g_Seconds)
        {
            Finish(0, "loaded and played");
        }

        controller->hudDelay = 0;
        break;
    default:
        break;
    }
}

void SaveTestInput(BoundPad* pad)
{
    if (SaveTestActive() && g_PressStart)
    {
        pad->buttons |= PadBitStart;
    }
}
}
