// The improved menus (THE LIST 79, #48: ichagas7's BETTER MENUS.pdf, its part 2; the game option improved_menus, off) and the level
// select's gems' pages (THE LIST 80, #48's Level Select Menu.pdf; with the level select on, levelselect.cpp).
//
// The improved menus: the pause menu starts at its first row instead of resume, restart checkpoint comes second, the gems' pages
// (the pause menu's shoulder pages: each world's levels with their gems) and the extras are rows of it (back from them comes back
// to the pause menu, not to play; the extras' resume reads back), and quit game reads "return to title" (its confirmation asking
// so); resume goes (back and start resume play, as before).
// Disable autosave and the greyed resume go (THE LIST 63c, #48: ichagas7, so the pause menu fits without scrolling); the original's
// disable autosave stays as the PS2 has it, greyed but while an autosave waits. The pause page has both layouts' rows (olegpages.cpp's
// PausePage::Construct); each frame the layout chosen is shown. A row left out is neither drawn nor selectable (its enabled 0):
// one only not shown is drawn greyed (1.3.0's hidden rows were: the default menus showed the native rows greyed).
//
// Quit to desktop (THE LIST 63c, #33: ichagas7's ask, the main menu in the game's font and look): the improved main menu's last
// row, asking on the quit screen as the level select's confirmation does ("quit to desktop?", no selected first); yes ends the
// program as the window's close button does, no (or back) shows the main menu again on that row.
//
// The level select's gems' pages: with the level select on, every level's title shows on them and each level can be picked:
// "play this level?" yes / no on the quit's confirmation screen (its menu given a page of ours and its title our text while it
// asks), yes starting the level as the level select does (its own memory card: the campaign's saves never change). They open
// from a "level select" row in the main menu (this file runs their pages and their back there) and in the pause menu (the
// original's with the option on, the improved menus' gems row then named level select); from play, yes goes through the title
// (levelselect.cpp's StartLevelFromPlay).
//
// Continue (THE LIST 63a, #48's BETTER MENUS.pdf, its part 1's main menu): with the improved menus the main menu's rows are
// continue, level select (the option on), new game, load game, options, as the PDF has them, and continue is there while the card
// holds a save (selected first then; new game otherwise). It leads to the load game page as load game does, and the game's load
// takes the newest of the saves' slots (by the dates the game keeps in the card's folder file) where load game shows them to
// choose from (savecode.cpp): no saving or loading of ours, so nothing a save can't be.
#include "ui/nativeui.h"

#include "ui/bindings.h"
#include "native.h"

#include "game/clock.h"
#include "game/controllers.h"
#include "game/gamecontroller.h"
#include "game/language.h"
#include "game/memory.h"
#include "game/menus.h"
#include "game/oleg.h"
#include "game/olegpages.h"
#include "game/pads.h"
#include "game/widgets.h"

#include <cstdlib>
#include <cstring>
#include <string>

namespace NativeUi
{
namespace
{
// As olegpages.cpp's ShowScreen: every other screen hidden and the screen shown, over half a second
constexpr f32 ScreenChangeSeconds = 0.5f;
constexpr u32 Worlds = 4;
// Past every area: each level's title shown and each levels page reached
constexpr u32 EveryArea = 24;

// The gems' pages opened from the main menu, the world's page shown
bool g_FromMainMenu = false;
u32 g_World = 0;
// The level picked on them (-1 none)
s32 g_Picked = -1;
// The pause page a pause menu row asked for (a reason, ReasonNone none), the row (its ID), and the pause menu shown again from
// its page (the row the one selected first); the main menu likewise from the gems' pages or quit to desktop's confirmation (the
// row it came from: 0 none)
u32 g_PausePageAsked = GameController::ReasonNone;
u32 g_PauseRow = 0;
bool g_PauseReturning = false;
u32 g_MainMenuReturning = 0;
// Quit to desktop's confirmation up from the main menu (until the main menu's shown again), its page and vtable (the quit page's,
// but its back), in storage of our own as the native rows are
bool g_QuitAsking = false;
OlegPage* g_QuitConfirmPage = nullptr;
GccVTableEntry g_QuitConfirmVTable[10];
alignas(16) u8 g_QuitConfirmStorage[sizeof(OlegPage)];
alignas(16) u8 g_QuitYesStorage[sizeof(ActionItem)];
alignas(16) u8 g_QuitNoStorage[sizeof(BackItem)];
// The game's own: the quit screen's page, the main menu's first item, the levels screens' widgets (the select hint added with
// the level select on)
MenuPage* g_QuitPage = nullptr;
u32 g_MainMenuFirst = 0;
u64 g_LevelsScreens[Worlds] = {};
bool g_Captured = false;
// The level confirmation's page and its vtable (the quit page's, but its back); the gems' pages' levels' vtable (the link's, but
// its activation)
OlegPage* g_ConfirmPage = nullptr;
GccVTableEntry g_ConfirmVTable[10];
GccVTableEntry g_LevelItemVTable[15];
// Continue asked for the load it leads to (savecode.cpp takes it)
bool g_Continue = false;
// The pause page's rows in each layout's order (all of them: the hidden ones too), and the main menu's
constexpr u32 PauseRows = 9;
constexpr u32 MainMenuRows = 6;
constexpr u32 ImprovedMainOrder[MainMenuRows] = {NativeItems::MainMenuContinue, NativeItems::MainMenuLevels, NativeItems::NewGame,
                                                 NativeItems::LoadGame,          NativeItems::Options,        NativeItems::MainMenuQuit};
constexpr u32 OriginalMainOrder[MainMenuRows] = {NativeItems::NewGame,          NativeItems::LoadGame, NativeItems::MainMenuLevels,
                                                 NativeItems::MainMenuContinue, NativeItems::Options,  NativeItems::MainMenuQuit};
constexpr u32 ImprovedOrder[PauseRows] = {NativeItems::Options,     NativeItems::RestartCheckpoint, NativeItems::Levels,
                                          NativeItems::SaveGame,    NativeItems::LoadGame,          NativeItems::DisableAutosave,
                                          NativeItems::Extras,      NativeItems::QuitGame,          NativeItems::Resume};
constexpr u32 OriginalOrder[PauseRows] = {NativeItems::Options,         NativeItems::SaveGame, NativeItems::LoadGame,
                                          NativeItems::DisableAutosave, NativeItems::RestartCheckpoint, NativeItems::Levels,
                                          NativeItems::Extras,          NativeItems::QuitGame, NativeItems::Resume};

void ShowScreen(OLEG& oleg, u32 screen)
{
    oleg.Hide(oleg.screens[OLEG::ScreenAllButOverlays], static_cast<s32>(g_ClockUnitsPerSecond * ScreenChangeSeconds), 0);
    oleg.Show(oleg.screens[screen], static_cast<s32>(g_ClockUnitsPerSecond * ScreenChangeSeconds), 0);
}

// A row shown, or left out: neither drawn nor selectable (THE LIST 63c: only shown 0 draws it greyed)
void SetShown(MenuItem* item, bool shown)
{
    if (item != nullptr)
    {
        item->shown = shown ? EveryPlayer : 0;
        item->enabled = shown ? EveryPlayer : 0;
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

// The quit screen's menu given a page and its title a text (the menu entered again if it was on another page)
void PointQuitScreen(OLEG& oleg, MenuPage* page, u32 title)
{
    MenuWidget& menu = oleg.quitMenu;
    menu.home = page;
    menu.firstPage = page;
    oleg.quitTitle.text = title;
    if (menu.current != nullptr && menu.current != page)
    {
        menu.current->Leave();
        menu.current = nullptr;
    }
}

// The confirmation's back (its no too): the gems' page it came from again
u32 ConfirmBack(MenuPage*, u32)
{
    GameController* controller = G_GameController;
    if (controller != nullptr)
    {
        if (g_FromMainMenu)
        {
            ShowScreen(controller->oleg, OLEG::ScreenLevels + g_World);
        }
        else
        {
            controller->BackToPauseMenu();
        }
    }

    return 1;
}

// A level of the gems' pages activated: the confirmation (the level select on), leading nowhere as the link
MenuPage* LevelActivate(MenuItem* item, u32, MenuPage*)
{
    PickLevel(item->Id());
    return item->target;
}

OlegPage* ConfirmPage()
{
    if (g_ConfirmPage != nullptr)
    {
        return g_ConfirmPage;
    }

    std::memcpy(g_ConfirmVTable, g_QuitPageVTable, sizeof(g_ConfirmVTable));
    g_ConfirmVTable[MenuPage::BackSlot].function = reinterpret_cast<void*>(&ConfirmBack);
    auto* page = static_cast<OlegPage*>(MemoryAllocate(sizeof(OlegPage)));
    OlegPage::ConstructTitled(page, TextPlayLevel, nullptr);
    page->vtable = g_ConfirmVTable;
    ActionItem* yes = ActionItem::Construct(static_cast<ActionItem*>(MemoryAllocate(sizeof(ActionItem))), NativeItems::YesText,
                                            NativeItems::PlayLevelYes, ActionItem::ActionPlayLevel);
    BackItem* no = BackItem::Construct(static_cast<BackItem*>(MemoryAllocate(sizeof(BackItem))), NativeItems::NoText,
                                       NativeItems::No, nullptr, MenuPlayers);
    page->Add(yes);
    page->Add(no);
    page->SetFirstItem(no->Id());
    g_ConfirmPage = page;
    return page;
}

void LayOutMainMenu(OLEG& oleg);

// Quit to desktop's confirmation's back (its no too): the main menu again, on its quit row
u32 QuitConfirmBack(MenuPage*, u32)
{
    GameController* controller = G_GameController;
    if (controller != nullptr)
    {
        g_MainMenuReturning = NativeItems::MainMenuQuit;
        LayOutMainMenu(controller->oleg);
        ShowScreen(controller->oleg, OLEG::ScreenMainMenu);
    }

    return 1;
}

OlegPage* QuitConfirmPage()
{
    if (g_QuitConfirmPage != nullptr)
    {
        return g_QuitConfirmPage;
    }

    std::memcpy(g_QuitConfirmVTable, g_QuitPageVTable, sizeof(g_QuitConfirmVTable));
    g_QuitConfirmVTable[MenuPage::BackSlot].function = reinterpret_cast<void*>(&QuitConfirmBack);
    auto* page = reinterpret_cast<OlegPage*>(g_QuitConfirmStorage);
    OlegPage::ConstructTitled(page, TextQuitDesktopAsk, nullptr);
    page->vtable = g_QuitConfirmVTable;
    ActionItem* yes = ActionItem::Construct(reinterpret_cast<ActionItem*>(g_QuitYesStorage), NativeItems::YesText,
                                            NativeItems::QuitDesktopYes, ActionItem::ActionQuitToDesktop);
    BackItem* no = BackItem::Construct(reinterpret_cast<BackItem*>(g_QuitNoStorage), NativeItems::NoText, NativeItems::No, nullptr,
                                       MenuPlayers);
    page->Add(yes);
    page->Add(no);
    page->SetFirstItem(no->Id());
    g_QuitConfirmPage = page;
    return page;
}

// A page's items put in an order (by their IDs; nothing done unless it has exactly those)
void Order(MenuPage* page, const u32* ids, u32 count)
{
    if (page->items.count != count)
    {
        return;
    }

    MenuItem* ordered[PauseRows];
    if (count > PauseRows)
    {
        return;
    }

    bool same = true;
    for (u32 i = 0; i < count; i++)
    {
        ordered[i] = page->Find(ids[i]);
        if (ordered[i] == nullptr)
        {
            return;
        }

        same = same && ordered[i] == page->items.data[i];
    }

    for (u32 i = 0; !same && i < count; i++)
    {
        page->items.data[i] = ordered[i];
    }
}

// The pause page's rows of the layout chosen in its order, and the one selected first (options, resume, or the row its page was
// asked from)
void LayOutPause(OLEG& oleg)
{
    MenuPage* page = oleg.pauseMenu.firstPage;
    if (page == nullptr)
    {
        return;
    }

    const bool improved = IsOn(OptionImprovedMenus);
    const bool levelSelect = IsOn(OptionLevelSelect);
    Order(page, improved ? ImprovedOrder : OriginalOrder, PauseRows);
    MenuItem* levels = page->Find(NativeItems::Levels);
    SetShown(levels, improved || levelSelect);
    SetText(levels, levelSelect ? TextLevelSelect : TextGems);
    SetShown(page->Find(NativeItems::Extras), improved);
    // Restart checkpoint only with its option on (THE LIST 85: HAB3X's row, the PS2's pause menu has none)
    SetShown(page->Find(NativeItems::RestartCheckpoint), IsOn(OptionRestartCheckpoint));
    SetText(page->Find(NativeItems::QuitGame), improved ? TextReturnToTitle : NativeItems::QuitGameText);
    SetShown(page->Find(NativeItems::Resume), !improved);
    // Disable autosave left out with the improved menus (THE LIST 63c, #48); the original's greyed but while an autosave waits
    // (PausePage::Frame sets its shown each frame: only its enabled here)
    if (MenuItem* disableAutosave = page->Find(NativeItems::DisableAutosave))
    {
        disableAutosave->enabled = improved ? 0 : EveryPlayer;
    }

    const u32 first = g_PauseReturning && g_PauseRow != 0 ? g_PauseRow : improved ? NativeItems::Options : NativeItems::Resume;
    page->SetFirstItem(first);
    // (The extras' resume reads back with the improved menus: it goes to the pause menu as their back does)
    MenuPage* extras = oleg.extrasMenu.firstPage;
    if (extras != nullptr)
    {
        SetText(extras->Find(NativeItems::ExtrasResume), improved ? NativeItems::BackText : NativeItems::ResumeText);
    }
}

void LayOutMainMenu(OLEG& oleg)
{
    MenuPage* page = oleg.mainMenu.firstPage;
    if (page == nullptr)
    {
        return;
    }

    const bool improved = IsOn(OptionImprovedMenus);
    const bool continueShown = improved && Native::MemoryCardHasSave();
    Order(page, improved ? ImprovedMainOrder : OriginalMainOrder, MainMenuRows);
    SetShown(page->Find(NativeItems::MainMenuLevels), IsOn(OptionLevelSelect));
    SetShown(page->Find(NativeItems::MainMenuContinue), continueShown);
    SetShown(page->Find(NativeItems::MainMenuQuit), improved);
    if (g_MainMenuReturning != 0)
    {
        page->SetFirstItem(g_MainMenuReturning);
    }
    else if (improved)
    {
        page->SetFirstItem(continueShown ? NativeItems::MainMenuContinue : NativeItems::NewGame);
    }
    else
    {
        page->flags.firstItem = g_MainMenuFirst;
    }
}

// ------------------------------------------------------------------------------------------------------------- the menus test
// $TWIN_MENUS_TEST=1 (improved_menus=1 and level_select=1 in options.txt; headless): from the Vivendi logo to the main menu, its
// level select row, the gems' pages, the second world's second level picked and played (the confirmation's yes); in play the
// pause menu (its rows and the first selected), the gems' pages from their row and back, the extras from theirs and their back
// row, return to title's confirmation and its no, then from the gems' pages Jungle Bungle played from play (through the title). Each step
// logged ("menus test:"), "menus test: done" at the end, "menus test: stuck" if a step waits a minute. $TWIN_MENUS_TEST=2 ends
// at the first pause menu (the original menus' rows: improved_menus=0 and level_select=1, restart_checkpoint on or off; THE LIST 85).
// $TWIN_MENUS_TEST=3 (improved_menus=1, a card with saves; THE LIST 63a): the main menu's rows, continue chosen, the notice a load
// brings answered, then 3 seconds of play and "menus test: done" (the slot it loaded is continue's own line).
// $TWIN_MENUS_TEST=4 (improved_menus=1 and level_select=1, a card with saves; THE LIST 108, #38): the level select from a loaded
// save as a player would. Continue's load, 3 seconds of play, the gems' pages from the pause menu and Jungle Bungle played
// (through the title, the level select's card), an autosave there as a checkpoint's, then return to title, continue again and
// 3 seconds of play. The progress logged at each ("menus test: the progress"); the player's card's files compared by the caller
struct Test
{
    bool on = false;
    bool toFirstPause = false;
    bool toContinue = false;
    bool fromSave = false;
    // (fromSave: 0 the save loaded, 1 the level select's play, 2 the save loaded again)
    u32 round = 0;
    // (the improved menus' test 1: quit to desktop 0 not chosen yet, 1 its confirmation waited for, 2 the main menu again, 3 done)
    u32 quitRound = 0;
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

// A page's rows drawn, the selected one in brackets, a greyed one (drawn, not selectable) in parentheses
std::string Rows(MenuPage* page)
{
    std::string rows;
    MenuItem* selected = Selected(page);
    for (u32 i = 0; i < page->items.count; i++)
    {
        MenuItem* item = page->Item(static_cast<s32>(i));
        if (!item->CanSelect(0))
        {
            continue;
        }

        rows += rows.empty() ? "" : ", ";
        rows += item == selected ? "[" + ItemName(item) + "]" : !item->IsShownTo(0) ? "(" + ItemName(item) + ")" : ItemName(item);
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

// (fromSave) The progress the game has: its gems and crystals, the story, the card in use and the autosave's slot
void LogProgress(GameController* controller, const char* when)
{
    const GameProgress& progress = controller->progress;
    u32 gems = 0;
    u32 crystals = 0;
    for (u32 level = 0; level < GameProgress::Levels; level++)
    {
        for (u32 gem = 0; gem < GameProgress::Gems; gem++)
        {
            gems += (progress.levels[level].gems >> gem) & 1;
        }

        crystals += progress.levels[level].crystal;
    }

    auto* saves = static_cast<SaveCode*>(g_SaveManager);
    Native::Log("menus test: the progress %s: %u gems, %u crystals, story %u, the card %s, autosave %s, slot %u", when, gems, crystals,
                static_cast<u32>(progress.play.story), Native::MemoryCardName(), saves->bits.savingDue != 0 ? "on" : "off",
                static_cast<u32>(saves->results.slot) + 1);
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
    const bool quit = Shown(oleg.quitMenu);
    switch (test.step)
    {
    case 0:
        if (state == GameController::StateVivendiLogo)
        {
            controller->SetNextState(GameController::StateMainMenu);
        }
        else if (state == GameController::StateMainMenu && Shown(oleg.mainMenu))
        {
            Native::Log("menus test: the main menu: %s", Rows(oleg.mainMenu.current).c_str());
            Next();
        }
        break;
    case 1:
        if (test.toContinue)
        {
            if (Shown(oleg.mainMenu) && Choose(oleg.mainMenu, NativeItems::MainMenuContinue))
            {
                // Then in play (step 19's 3 seconds) and the end
                Native::Log("menus test: continue chosen");
                test.step = 18;
                Next();
            }

            break;
        }

        // (The improved menus' first: quit to desktop asked, its confirmation's no, the main menu again on that row; THE LIST 63c)
        if (IsOn(OptionImprovedMenus) && !test.toFirstPause && test.quitRound < 3)
        {
            if (test.quitRound == 0 && Shown(oleg.mainMenu) && Choose(oleg.mainMenu, NativeItems::MainMenuQuit))
            {
                test.quitRound = 1;
                test.stepFrame = test.frame;
            }
            else if (test.quitRound == 1 && quit)
            {
                Native::Log("menus test: quit to desktop asks: %s %s", GameText(oleg.quitTitle.text), Rows(oleg.quitMenu.current).c_str());
                Press(MenuSelectBit());
                test.quitRound = 2;
                test.stepFrame = test.frame;
            }
            else if (test.quitRound == 2 && Shown(oleg.mainMenu))
            {
                Native::Log("menus test: the main menu again: %s", Rows(oleg.mainMenu.current).c_str());
                test.quitRound = 3;
                test.stepFrame = test.frame;
            }

            break;
        }

        if (Shown(oleg.mainMenu) && Choose(oleg.mainMenu, NativeItems::MainMenuLevels))
        {
            Next();
        }
        break;
    case 2:
        if (levels != nullptr)
        {
            Native::Log("menus test: the gems' pages from the main menu, world %u, on %s", world + 1, GameText(oleg.levelName.text));
            Press(PadBitR1);
            Next();
        }
        break;
    case 3:
    case 17:
        // From the main menu the second world's second level (Ice Climb, area 7); from play the first world's second (Jungle
        // Bungle, area 1)
        if (levels != nullptr && world == (test.step == 3 ? 1u : 0u) && Choose(*levels, test.step == 3 ? 7 : 1))
        {
            Native::Log("menus test: world %u, %s picked", world + 1, GameText(oleg.levelName.text));
            Next();
        }
        break;
    case 4:
    case 18:
        if (quit)
        {
            MenuItem* selected = Selected(oleg.quitMenu.current);
            if (selected != nullptr && selected->Id() == NativeItems::No)
            {
                Native::Log("menus test: the confirmation: %s %s", GameText(oleg.quitTitle.text), Rows(oleg.quitMenu.current).c_str());
            }

            if (Choose(oleg.quitMenu, NativeItems::PlayLevelYes))
            {
                Next();
            }
        }
        break;
    case 5:
    case 19:
        // (After continue's load: the game's notice that follows a load, answered as a player would)
        if (test.toContinue && state == GameController::StatePaused && test.frame > test.buttonsUntil + 30)
        {
            Native::Log("menus test: paused after the load (pause reason %u), answered",
                        static_cast<u32>(controller->states.pauseReason));
            Press(MenuSelectBit());
            break;
        }

        // In play for 3 seconds, then paused (or the end)
        test.playFrames = state == GameController::StatePlaying ? test.playFrames + 1 : 0;
        if (test.playFrames >= 150)
        {
            test.playFrames = 0;
            if (test.step == 19 && test.fromSave)
            {
                LogProgress(controller, test.round == 0 ? "of the save loaded" : test.round == 1 ? "of the level select's play"
                                                                                                    : "of the save loaded again");
                if (test.round == 0)
                {
                    // To the gems' pages from the pause menu (steps 16 to 18), Jungle Bungle played, then here again
                    test.round = 1;
                    Press(PadBitStart);
                    test.step = 15;
                    Next();
                    break;
                }

                if (test.round == 1)
                {
                    // An autosave as a checkpoint's (commandscharacters.cpp's), then return to title (steps 20 to 23)
                    test.round = 2;
                    Native::Log("menus test: an autosave asked for: %s", controller->Autosave(nullptr) != 0 ? "saving" : "autosave off");
                    Next();
                    break;
                }
            }

            if (test.step == 19)
            {
                Native::Log("menus test: done");
                std::exit(0);
            }

            Press(PadBitStart);
            Next();
        }

        break;
    case 6:
    case 9:
    case 12:
        if (pause)
        {
            Native::Log("menus test: the pause menu: %s", Rows(oleg.pauseMenu.current).c_str());
            if (test.toFirstPause)
            {
                Native::Log("menus test: done");
                std::exit(0);
            }

            Next();
        }
        break;
    case 7:
    case 16:
        if (pause && Choose(oleg.pauseMenu, NativeItems::Levels))
        {
            Next();
        }
        break;
    case 8:
        if (levels != nullptr)
        {
            Native::Log("menus test: the gems' pages from the pause menu, world %u: back", world + 1);
            Press(MenuBackBit());
            Next();
        }
        break;
    case 10:
        if (pause && Choose(oleg.pauseMenu, NativeItems::Extras))
        {
            Next();
        }
        break;
    case 11:
        // Its back row (the original's resume) chosen
        if (Shown(oleg.extrasMenu) && Choose(oleg.extrasMenu, NativeItems::ExtrasResume))
        {
            Native::Log("menus test: the extras: %s", Rows(oleg.extrasMenu.current).c_str());
            Next();
        }
        break;
    case 13:
        if (pause && Choose(oleg.pauseMenu, NativeItems::QuitGame))
        {
            Next();
        }
        break;
    case 14:
        if (quit)
        {
            // Its no, selected first
            Native::Log("menus test: return to title asks: %s %s: no", GameText(oleg.quitTitle.text),
                        Rows(oleg.quitMenu.current).c_str());
            Press(MenuSelectBit());
            Next();
        }
        break;
    case 15:
        if (pause)
        {
            Native::Log("menus test: the pause menu again: %s", Rows(oleg.pauseMenu.current).c_str());
            Next();
        }
        break;
    case 20:
    {
        // (fromSave) The autosave's end: the save code idle and play going on (a notice it brought answered), then paused
        auto* saves = static_cast<SaveCode*>(g_SaveManager);
        if (state == GameController::StatePaused && !pause && test.frame > test.buttonsUntil + 30)
        {
            Native::Log("menus test: paused after the autosave (pause reason %u), answered", static_cast<u32>(controller->states.pauseReason));
            Press(MenuSelectBit());
            break;
        }

        // (A save done leaves the step waiting: SavingWait, the storage not polled on a computer)
        const u32 saving = static_cast<u32>(controller->states.savingStep);
        const bool idle = (saving == GameController::SavingNone || saving == GameController::SavingWait) && saves->bits.operation == 0;
        test.playFrames = idle && state == GameController::StatePlaying ? test.playFrames + 1 : 0;
        if (test.playFrames >= 150)
        {
            test.playFrames = 0;
            LogProgress(controller, "after the autosave");
            Press(PadBitStart);
            Next();
        }
        break;
    }
    case 21:
        if (pause && Choose(oleg.pauseMenu, NativeItems::QuitGame))
        {
            Next();
        }
        break;
    case 22:
        // Return to title's yes (the quit page's, OlegPages' QuitYesItem)
        if (quit && Choose(oleg.quitMenu, 0xB))
        {
            Native::Log("menus test: return to title: yes");
            Next();
        }
        break;
    case 23:
        // At the title on to the main menu, there continue again (step 19 the last time)
        if (state == GameController::StateTitle && test.frame > test.stepFrame + 60)
        {
            controller->SetNextState(GameController::StateMainMenu);
        }
        else if (state == GameController::StateMainMenu && Shown(oleg.mainMenu) &&
                 Choose(oleg.mainMenu, NativeItems::MainMenuContinue))
        {
            Native::Log("menus test: continue chosen again: %s", Rows(oleg.mainMenu.current).c_str());
            test.step = 18;
            Next();
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
    if (controller != nullptr && g_QuitPage != nullptr)
    {
        PointQuitScreen(controller->oleg, g_QuitPage, IsOn(OptionImprovedMenus) ? TextReturnToTitleAsk : NativeItems::QuitTitleText);
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

void OpenLevelsFromMainMenu()
{
    GameController* controller = G_GameController;
    if (controller == nullptr || controller->State() != GameController::StateMainMenu)
    {
        return;
    }

    g_FromMainMenu = true;
    g_World = 0;
    ShowScreen(controller->oleg, OLEG::ScreenLevels);
}

void ContinueChosen()
{
    g_Continue = true;
    Native::Log("continue: the newest save asked for, through load game's load");
}

void AskQuitToDesktop()
{
    GameController* controller = G_GameController;
    if (controller == nullptr || controller->State() != GameController::StateMainMenu || g_QuitPage == nullptr)
    {
        return;
    }

    g_QuitAsking = true;
    PointQuitScreen(controller->oleg, QuitConfirmPage(), TextQuitDesktopAsk);
    ShowScreen(controller->oleg, OLEG::ScreenQuit);
}

void QuitToDesktop()
{
    // As the window's close button does (pads.cpp): the saves and settings are written as they're made
    Native::EndSession("quit: to the desktop from the main menu");
    std::_Exit(0);
}

void PickLevel(u32 area)
{
    GameController* controller = G_GameController;
    if (controller == nullptr || !IsOn(OptionLevelSelect) || area >= 25 || g_QuitPage == nullptr)
    {
        return;
    }

    g_Picked = g_AreaLevels[area];
    PointQuitScreen(controller->oleg, ConfirmPage(), TextPlayLevel);
    ShowScreen(controller->oleg, OLEG::ScreenQuit);
}

const GccVTableEntry* LevelItemVTable()
{
    static bool made = false;
    if (!made)
    {
        made = true;
        std::memcpy(g_LevelItemVTable, g_LinkItemVTable, sizeof(g_LevelItemVTable));
        g_LevelItemVTable[MenuItem::ActivateSlot].function = reinterpret_cast<void*>(&LevelActivate);
    }

    return g_LevelItemVTable;
}

void PlayPickedLevel()
{
    GameController* controller = G_GameController;
    if (controller == nullptr || g_Picked < 0)
    {
        return;
    }

    const u32 level = static_cast<u32>(g_Picked);
    g_Picked = -1;
    if (controller->State() == GameController::StateMainMenu)
    {
        g_FromMainMenu = false;
        StartLevel(level);
    }
    else
    {
        StartLevelFromPlay(level);
    }
}

u32 LevelsOpen(u32 open)
{
    return IsOn(OptionLevelSelect) && open < EveryArea ? EveryArea : open;
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
        g_PauseRow = reason == GameController::ReasonExtras ? NativeItems::Extras : NativeItems::Levels;
    }

    g_PauseReturning = true;
    LayOutPause(oleg);
    controller->states.pauseReason = GameController::ReasonStart;
    controller->BackToPauseMenu();
    return true;
}

bool MainMenuLevelsFrame()
{
    GameController* controller = G_GameController;
    // Quit to desktop's confirmation up (its no fading back to the main menu): the main menu's back to the title not taken meanwhile
    if (g_QuitAsking && controller != nullptr)
    {
        g_QuitAsking = controller->State() == GameController::StateMainMenu && !Shown(controller->oleg.mainMenu);
        if (g_QuitAsking)
        {
            return true;
        }
    }

    if (!g_FromMainMenu || controller == nullptr)
    {
        return false;
    }

    OLEG& oleg = controller->oleg;
    MenuWidget& levels = oleg.levelsMenus[g_World];
    if (!Shown(levels))
    {
        // Fading, or the confirmation up
        return true;
    }

    if (levels.current == &g_ResumePage)
    {
        // Back: the main menu again, on its level select row
        g_FromMainMenu = false;
        g_MainMenuReturning = NativeItems::MainMenuLevels;
        LayOutMainMenu(oleg);
        ShowScreen(oleg, OLEG::ScreenMainMenu);
        return true;
    }

    // The shoulders: the worlds' pages round (as in the pause menu, but without the pause and extras pages)
    const bool back = GetButtonState(controller->pad, PadL1, true) || GetButtonState(controller->pad, PadL2, true);
    const bool on = GetButtonState(controller->pad, PadR1, true) || GetButtonState(controller->pad, PadR2, true);
    if (back || on)
    {
        g_World = (g_World + (on ? 1 : Worlds - 1)) % Worlds;
        ShowScreen(oleg, OLEG::ScreenLevels + g_World);
    }

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
        g_QuitPage = oleg.quitMenu.firstPage;
        g_MainMenuFirst = oleg.mainMenu.firstPage->flags.firstItem;
        for (u32 w = 0; w < Worlds; w++)
        {
            g_LevelsScreens[w] = oleg.screens[OLEG::ScreenLevels + w];
        }

        static const char* test = std::getenv("TWIN_MENUS_TEST");
        g_Test.on = test != nullptr && (test[0] == '1' || test[0] == '2' || test[0] == '3' || test[0] == '4');
        g_Test.toFirstPause = g_Test.on && test[0] == '2';
        g_Test.fromSave = g_Test.on && test[0] == '4';
        g_Test.toContinue = g_Test.on && (test[0] == '3' || g_Test.fromSave);
    }

    // Back on the pause menu or the main menu: their first rows as usual again
    if (g_PauseReturning && oleg.pauseMenu.current == oleg.pauseMenu.firstPage)
    {
        g_PauseReturning = false;
    }

    if (g_MainMenuReturning != 0 && oleg.mainMenu.current == oleg.mainMenu.firstPage)
    {
        g_MainMenuReturning = 0;
    }

    if (g_FromMainMenu && controller->State() != GameController::StateMainMenu)
    {
        g_FromMainMenu = false;
    }

    LayOutPause(oleg);
    LayOutMainMenu(oleg);
    // The levels screens' select hint while a level can be picked
    for (u32 w = 0; w < Worlds; w++)
    {
        oleg.screens[OLEG::ScreenLevels + w] =
            IsOn(OptionLevelSelect) ? g_LevelsScreens[w] | oleg.screens[OLEG::ScreenSelectHint] : g_LevelsScreens[w];
    }

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

// The save code's (savecode.cpp): whether continue asked for the load whose slots were just read (then forgotten; also when an
// operation ends or the player answers a screen, so a later load game asks as usual), and the slot it takes logged
bool NativeTakeContinue()
{
    const bool asked = NativeUi::g_Continue;
    NativeUi::g_Continue = false;
    return asked;
}

void NativeContinueSlot(u32 slot, u32 saves, u32 day, u32 month, u32 year, u32 hour, u32 minute, u32 second)
{
    Native::Log("continue: slot %u loaded, saved %u/%u/%02u %u:%02u:%02u, the newest of %u save%s", slot + 1, day, month, year, hour,
                minute, second, saves, saves == 1 ? "" : "s");
}
