// The improved menus (THE LIST 79, #48: ichagas7's BETTER MENUS.pdf, its part 2; the game option improved_menus, off).
//
// The improved menus: the pause menu starts at its first row instead of resume, restart checkpoint comes second, the gems' pages
// (the pause menu's shoulder pages: each world's levels with their gems) and the extras are rows of it (back from them comes back
// to the pause menu, not to play; the extras' resume reads back), and quit game reads "return to title" (its confirmation asking
// so); resume goes (back and start resume play, as before).
// Disable autosave stays the original's action, shown while an autosave waits. The pause page has both layouts' rows
// (olegpages.cpp's PausePage::Construct); each frame the layout chosen is shown.
#include "ui/nativeui.h"

#include "ui/bindings.h"
#include "native.h"

#include "game/gamecontroller.h"
#include "game/language.h"
#include "game/menus.h"
#include "game/oleg.h"
#include "game/olegpages.h"
#include "game/widgets.h"

#include <cstdlib>
#include <string>

namespace NativeUi
{
namespace
{
constexpr u32 Worlds = 4;

// The pause page a pause menu row asked for (a reason, ReasonNone none), the row (its ID), and the pause menu shown again from
// its page (the row the one selected first)
u32 g_PausePageAsked = GameController::ReasonNone;
u32 g_PauseRow = 0;
bool g_PauseReturning = false;
bool g_Captured = false;

void SetShown(MenuItem* item, bool shown)
{
    if (item != nullptr)
    {
        item->shown = shown ? EveryPlayer : 0;
    }
}

void SetText(MenuItem* item, u32 text)
{
    if (item != nullptr && item->bits.text != text)
    {
        item->bits.text = text;
        item->text = nullptr;
    }
}

bool Shown(const Widget& widget)
{
    return widget.State() == Widget::StateShown;
}

// The pause page's rows of the layout chosen, and the one selected first (options, resume, or the row its page was asked from)
void LayOutPause(OLEG& oleg)
{
    MenuPage* page = oleg.pauseMenu.firstPage;
    if (page == nullptr)
    {
        return;
    }

    const bool improved = IsOn(OptionImprovedMenus);
    SetShown(page->Find(NativeItems::RestartCheckpointHigher), improved);
    SetShown(page->Find(NativeItems::LevelsHigher), improved);
    SetShown(page->Find(NativeItems::RestartCheckpoint), !improved);
    SetShown(page->Find(NativeItems::Extras), improved);
    SetText(page->Find(NativeItems::QuitGame), improved ? TextReturnToTitle : NativeItems::QuitGameText);
    SetShown(page->Find(NativeItems::Resume), !improved);
    const u32 first = g_PauseReturning && g_PauseRow != 0 ? g_PauseRow : improved ? NativeItems::Options : NativeItems::Resume;
    page->SetFirstItem(first);
    // (The extras' resume reads back with the improved menus: it goes to the pause menu as their back does)
    MenuPage* extras = oleg.extrasMenu.firstPage;
    if (extras != nullptr)
    {
        SetText(extras->Find(NativeItems::ExtrasResume), improved ? NativeItems::BackText : NativeItems::ResumeText);
    }
}

// ------------------------------------------------------------------------------------------------------------- the menus test
// $TWIN_MENUS_TEST=1 (improved_menus=1 and level_select=1 in options.txt; headless): from the Vivendi logo to the main menu, the
// Iceberg Lab started by the level select; in play the pause menu (its rows and the first selected), the gems' pages from their
// row and back, the extras from theirs and their back row, return to title's confirmation and its no. Each step logged ("menus
// test:"), "menus test: done" at the end, "menus test: stuck" if a step waits a minute
struct Test
{
    bool on = false;
    u32 step = 0;
    u32 frame = 0;
    u32 stepFrame = 0;
    u32 buttons = 0;
    u32 buttonsUntil = 0;
    u32 playFrames = 0;
};
Test g_Test;

std::string ItemName(MenuItem* item)
{
    if (item->text != nullptr)
    {
        return item->text;
    }

    return GameText(item->bits.text);
}

MenuItem* Selected(MenuPage* page)
{
    return page != nullptr && page->selections != nullptr ? page->Item(page->selections[0]) : nullptr;
}

// A page's rows shown, the selected one in brackets
std::string Rows(MenuPage* page)
{
    std::string rows;
    MenuItem* selected = Selected(page);
    for (u32 i = 0; i < page->items.count; i++)
    {
        MenuItem* item = page->Item(static_cast<s32>(i));
        if (!item->IsShownTo(0))
        {
            continue;
        }

        rows += rows.empty() ? "" : ", ";
        rows += item == selected ? "[" + ItemName(item) + "]" : ItemName(item);
    }

    return rows;
}

void Press(u32 buttons)
{
    g_Test.buttons = buttons;
    g_Test.buttonsUntil = g_Test.frame + 4;
}

void Next()
{
    g_Test.step++;
    g_Test.stepFrame = g_Test.frame;
}

// On a shown menu's page: the item of an ID selected (a press up or down a frame) and then activated. Returns whether it was
bool Choose(MenuWidget& menu, u32 id)
{
    MenuPage* page = menu.current;
    MenuItem* selected = Selected(page);
    if (selected == nullptr)
    {
        return false;
    }

    if (selected->Id() == id)
    {
        Press(MenuSelectBit());
        return true;
    }

    Press(page->IndexOf(id) < page->selections[0] ? PadBitUp : PadBitDown);
    return false;
}

MenuWidget* ShownLevels(OLEG& oleg, u32* world)
{
    for (u32 w = 0; w < Worlds; w++)
    {
        if (Shown(oleg.levelsMenus[w]))
        {
            *world = w;
            return &oleg.levelsMenus[w];
        }
    }

    return nullptr;
}

void TestFrame(GameController* controller)
{
    Test& test = g_Test;
    test.frame++;
    if (test.frame < test.buttonsUntil + 15 || test.frame < test.stepFrame + 15)
    {
        return;
    }

    if (test.frame > test.stepFrame + 3000)
    {
        Native::Log("menus test: stuck at step %u", test.step);
        std::exit(1);
    }

    OLEG& oleg = controller->oleg;
    const u32 state = controller->State();
    u32 world = 0;
    MenuWidget* levels = ShownLevels(oleg, &world);
    const bool pause = Shown(oleg.pauseMenu);
    switch (test.step)
    {
    case 0:
        if (state == GameController::StateVivendiLogo)
        {
            controller->SetNextState(GameController::StateMainMenu);
        }
        else if (state == GameController::StateMainMenu && Shown(oleg.mainMenu))
        {
            StartLevel(4);
            Next();
        }
        break;
    case 1:
        // In play for 3 seconds, then paused
        test.playFrames = state == GameController::StatePlaying ? test.playFrames + 1 : 0;
        if (test.playFrames >= 150)
        {
            Press(PadBitStart);
            Next();
        }
        break;
    case 2:
    case 5:
    case 8:
        if (pause)
        {
            Native::Log("menus test: the pause menu: %s", Rows(oleg.pauseMenu.current).c_str());
            Next();
        }
        break;
    case 3:
        if (pause && Choose(oleg.pauseMenu, NativeItems::LevelsHigher))
        {
            Next();
        }
        break;
    case 4:
        if (levels != nullptr)
        {
            Native::Log("menus test: the gems' pages from the pause menu, world %u: back", world + 1);
            Press(MenuBackBit());
            Next();
        }
        break;
    case 6:
        if (pause && Choose(oleg.pauseMenu, NativeItems::Extras))
        {
            Next();
        }
        break;
    case 7:
        // Its back row (the original's resume) chosen
        if (Shown(oleg.extrasMenu) && Choose(oleg.extrasMenu, NativeItems::ExtrasResume))
        {
            Native::Log("menus test: the extras: %s", Rows(oleg.extrasMenu.current).c_str());
            Next();
        }
        break;
    case 9:
        if (pause && Choose(oleg.pauseMenu, NativeItems::QuitGame))
        {
            Next();
        }
        break;
    case 10:
        if (Shown(oleg.quitMenu))
        {
            // Its no, selected first
            Native::Log("menus test: return to title asks: %s %s: no", GameText(oleg.quitTitle.text),
                        Rows(oleg.quitMenu.current).c_str());
            Press(MenuSelectBit());
            Next();
        }
        break;
    case 11:
        if (pause)
        {
            Native::Log("menus test: the pause menu again: %s", Rows(oleg.pauseMenu.current).c_str());
            Native::Log("menus test: done");
            std::exit(0);
        }
        break;
    default:
        break;
    }
}
}

void QuitScreenAsQuit()
{
    GameController* controller = G_GameController;
    if (controller != nullptr)
    {
        controller->oleg.quitTitle.text = IsOn(OptionImprovedMenus) ? TextReturnToTitleAsk : NativeItems::QuitTitleText;
    }
}

void AskPausePage(u32 reason, u32 fromItem)
{
    g_PausePageAsked = reason;
    g_PauseRow = fromItem;
}

u32 TakePausePage()
{
    const u32 asked = g_PausePageAsked;
    g_PausePageAsked = GameController::ReasonNone;
    return asked;
}

bool BackToPauseMenuInstead(MenuWidget* menu)
{
    GameController* controller = G_GameController;
    if (controller == nullptr || menu == nullptr || !IsOn(OptionImprovedMenus))
    {
        return false;
    }

    OLEG& oleg = controller->oleg;
    const u32 reason = static_cast<u32>(controller->states.pauseReason);
    const bool page = menu == &oleg.extrasMenu || (menu >= &oleg.levelsMenus[0] && menu <= &oleg.levelsMenus[Worlds - 1]);
    if (!page || reason < GameController::ReasonFirstLevels || reason > GameController::ReasonExtras)
    {
        return false;
    }

    // The pause menu again, on the row that led here (the row of the page shown, when it came by the shoulders)
    if (g_PauseRow == 0 || (reason == GameController::ReasonExtras) != (g_PauseRow == NativeItems::Extras))
    {
        g_PauseRow = reason == GameController::ReasonExtras ? NativeItems::Extras : NativeItems::LevelsHigher;
    }

    g_PauseReturning = true;
    LayOutPause(oleg);
    controller->states.pauseReason = GameController::ReasonStart;
    controller->BackToPauseMenu();
    return true;
}

void ImprovedMenusFrame()
{
    GameController* controller = G_GameController;
    if (controller == nullptr || controller->oleg.pauseMenu.firstPage == nullptr)
    {
        return;
    }

    OLEG& oleg = controller->oleg;
    if (!g_Captured)
    {
        g_Captured = true;
        static const char* test = std::getenv("TWIN_MENUS_TEST");
        g_Test.on = test != nullptr && test[0] == '1';
    }

    // Back on the pause menu: its first row as usual again
    if (g_PauseReturning && oleg.pauseMenu.current == oleg.pauseMenu.firstPage)
    {
        g_PauseReturning = false;
    }

    LayOutPause(oleg);
    if (g_Test.on)
    {
        TestFrame(controller);
    }
}

u32 MenusTestButtons()
{
    return g_Test.on && g_Test.frame < g_Test.buttonsUntil ? g_Test.buttons : 0;
}
}
