// The level select (THE LIST 12; HAB3X's 944aa1c, made a game option): with the option on, Options > Gameplay starts any of the
// game's 16 levels (their names the code file's texts from 0x47) from the main menu, from its first chunk, the way a new game
// starts (the save controller's new game restored, the game reset), with the play's area the level's first (g_AreaLevels) and
// the story and the open areas brought up to it, then the level loaded as a save's would be (the save controller taking the
// progress).
//
// Such a play's progress is one the game never makes itself (the story raised past levels not played), so its saves never go on
// the player's card: from the level's start until the game is back at the title, the memory card is a folder of its own
// (memorycard-level-select, memorycard.cpp's UseMemoryCard). TwinsanityEvolved's level select warps through the game's own
// saved-level path from inside a level and keeps the progress; this one keeps the player's saves out of reach instead.
#include "ui/nativeui.h"

#include "native.h"
#include "mods.h"

#include "game/gamecontroller.h"
#include "game/oleg.h"
#include "game/string.h"

#include <algorithm>
#include <cstdlib>

namespace NativeUi
{
namespace
{
// Each level's first chunk: a chunk of the level's first area (the level sweep's areas: the area play is in once a chunk has
// started, g_AreaLevels its level)
constexpr const char* LevelChunks[LevelCount] = {
    "Levels\\Earth\\Hub\\beach",            // N. Sanity Island (area 0)
    "Levels\\Earth\\Hub\\huba",             // Jungle Bungle (1)
    "Levels\\Earth\\Cavern\\cavent",        // Cavern Catastrophe (3)
    "Levels\\Earth\\Totem\\l03beach",       // Totem Hokum (4)
    "Levels\\Ice\\Hub\\labext",             // Iceberg Lab (6)
    "Levels\\Ice\\IceClimb\\bergext",       // Ice Climb (7)
    "Levels\\Ice\\SlipSlide\\l05start",     // Slip Slide Icecapades (9)
    "Levels\\Ice\\HighSeas\\gpa01",         // High Seas Hi-Jinks (10)
    "Levels\\school\\Sch_Hub\\sch_hub",     // Academy of Evil (13)
    "Levels\\school\\Boiler\\boiler_1",     // Boiler Room Doom (15)
    "Levels\\school\\Crash\\crashent",      // Classroom Chaos (17)
    "Levels\\school\\RoofTop\\roof01",      // Rooftop Rampage (18)
    "Levels\\AltEarth\\Hub\\alta",          // Twinsanity Island (20)
    "Levels\\AltEarth\\RockSlid\\l10start", // Rockslide Rumble (21)
    "Levels\\AltEarth\\Hub\\altdoc",        // Bandicoot Pursuit (22)
    "Levels\\AltEarth\\Core\\corea",        // Ant Agony (23)
};

// The level select's play under way (its card in use) until the game is back at the title
bool g_Session = false;

// The level's first area (-1 none)
s32 FirstArea(u32 level)
{
    for (s32 area = 0; area < 25; area++)
    {
        if (g_AreaLevels[area] == static_cast<s32>(level))
        {
            return area;
        }
    }

    return -1;
}
}

const char* LevelChunk(u32 level)
{
    return level < LevelCount ? LevelChunks[level] : nullptr;
}

bool CanStartLevel()
{
    GameController* controller = G_GameController;
    return IsOn(OptionLevelSelect) && controller != nullptr && controller->State() == GameController::StateMainMenu;
}

void StartLevel(u32 level)
{
    GameController* controller = G_GameController;
    s32 area = FirstArea(level);
    if (level >= LevelCount || area < 0 || !CanStartLevel())
    {
        return;
    }

    // The play's saves on a card of their own from here (its progress isn't one a played game has)
    Native::UseMemoryCard(Native::Mods::AnyUsed() ? (Native::Mods::CardName() + "-level-select").c_str() : "memorycard-level-select");
    g_Session = true;
    controller->saveController.Restore(0, &controller->progress, controller->chunkManager);
    controller->ResetGame(EntryNewGame);
    StringAssign(&controller->progress.startChunk, LevelChunks[level]);
    PlayState play = controller->progress.play;
    play.area = static_cast<u32>(area);
    play.story = std::max<u32>(play.story, static_cast<u32>(area));
    play.open = std::max<u32>(play.open, static_cast<u32>(area));
    controller->progress.play = play;
    controller->states.entry = EntrySaved;
    controller->saveController.Take(&controller->progress, controller->chunkManager, nullptr);
    controller->SetNextState(GameController::StateLoadingLevel);
    Native::Log("level select: level %u (area %d) from %s, its saves on the level select's own card", level, area,
                LevelChunks[level]);
}

void LevelSelectFrame()
{
    GameController* controller = G_GameController;
    // $TWIN_LEVEL_SELECT_TEST=N (the checks; the option on in options.txt): from the Vivendi logo to the main menu, then level N
    // started from there once
    static const char* test = std::getenv("TWIN_LEVEL_SELECT_TEST");
    static bool tested = false;
    if (test != nullptr && controller != nullptr && !tested)
    {
        if (controller->State() == GameController::StateVivendiLogo)
        {
            controller->SetNextState(GameController::StateMainMenu);
        }
        else if (controller->State() == GameController::StateMainMenu)
        {
            tested = true;
            StartLevel(static_cast<u32>(std::atoi(test)));
        }
    }

    if (g_Session && controller != nullptr && controller->State() == GameController::StateTitle)
    {
        // Back at the title: the player's card again
        g_Session = false;
        Native::UseMemoryCard(Native::Mods::CardName().c_str());
        Native::Log("level select: its play over, the player's own saves again");
    }
}
}
