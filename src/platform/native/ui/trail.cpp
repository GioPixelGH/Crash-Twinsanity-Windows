// The game's trail in the log, for bug reports: always the game's states as they change (by name, with where the player is), the
// chunk the player is in, the chunk a level loads at and how long the loading took; with the detailed log (Options > Gameplay, or
// $TWIN_DETAILED_LOG) also where the player is every 5 s of play, every sound played (game/sound.cpp) and every file looked up on
// the disc (disc.cpp)
#include "ui/bindings.h"
#include "ui/nativeui.h"

#include "native.h"

#include "game/chunkdata.h"
#include "game/gamecontroller.h"
#include "game/instances.h"
#include "game/place.h"
#include "game/player.h"
#include "game/reference.h"
#include "game/sound.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
using Clock = std::chrono::steady_clock;

const char* StateName(u32 state)
{
    static const char* const Names[] = {
        "waiting for the loader", "starting",          "waiting for a pad",   "checking the memory card", "the Vivendi logo",
        "the Traveller's Tales logo", "loading the title", "the title",        "the main menu",            "loading a level",
        "a new game",             "starting play",     "playing",             "a cutscene",               "a movie",
        "paused",                 "the gallery",       "waiting",             "game over",                "the credits",
        "back to the title",      "fading out",        "restarting",
    };
    return state < sizeof(Names) / sizeof(Names[0]) ? Names[state] : "?";
}

// The player's instance while it's made (none between levels)
InstanceContext* Player()
{
    if (g_PlayerInstance == nullptr || g_PlayerInstance->object == nullptr)
    {
        return nullptr;
    }

    auto* player = static_cast<InstanceContext*>(g_PlayerInstance->object);
    return InstanceLive(player) && player->place != nullptr ? player : nullptr;
}

// " at (x, y, z)" of the player (its matrix's or its position, whichever is newer: nothing of the game's is changed), or nothing
std::string Where(InstanceContext* player)
{
    if (player == nullptr)
    {
        return {};
    }

    const ObjectPlace* place = player->place;
    Vector4 position = place->position;
    if ((place->bits.value & PlaceBits::MatrixMoved) != 0)
    {
        position = {place->matrix.m[3][0], place->matrix.m[3][1], place->matrix.m[3][2], 1.0f};
    }

    char text[64];
    std::snprintf(text, sizeof(text), " at (%.1f, %.1f, %.1f)", position.x, position.y, position.z);
    return text;
}

// The chunk the player was last seen in (the game's thread's)
std::string g_LastChunk;
}

bool Native::DetailedLog()
{
    static const bool asked = std::getenv("TWIN_DETAILED_LOG") != nullptr;
    return asked || NativeUi::IsOn(NativeUi::OptionDetailedLog);
}

void NativeUi::TrailFrame()
{
    GameController* controller = G_GameController;
    if (controller == nullptr)
    {
        return;
    }

    static u32 lastState = ~0u;
    static Clock::time_point lastPlace = Clock::now();
    // A loading's start (the title's, a level's) and what it loads
    static Clock::time_point loadingFrom{};
    static std::string loading;
    u32 state = controller->State();
    InstanceContext* player = Player();
    Clock::time_point now = Clock::now();
    if (state != lastState)
    {
        if ((lastState == GameController::StateLoadingTitle || lastState == GameController::StateLoadingLevel) &&
            loadingFrom != Clock::time_point{})
        {
            Native::Log("game: %s loaded in %.2f s", loading.c_str(), std::chrono::duration<double>(now - loadingFrom).count());
            loadingFrom = {};
        }

        Native::Log("game: %s (state %u)%s", StateName(state), state, Where(player).c_str());
        if (state == GameController::StateLoadingLevel && controller->progress.startChunk.string != nullptr)
        {
            Native::Log("game: loading at %s", controller->progress.startChunk.string);
        }

        if (state == GameController::StateLoadingTitle || state == GameController::StateLoadingLevel)
        {
            loadingFrom = now;
            const char* chunk = controller->progress.startChunk.string;
            loading = state == GameController::StateLoadingTitle ? "the title" : chunk != nullptr ? chunk : "a level";
        }

        lastState = state;
    }

    ChunkData* chunk = player != nullptr ? player->chunk : nullptr;
    const char* name = chunk != nullptr ? chunk->name.string : nullptr;
    if (name != nullptr && g_LastChunk != name)
    {
        g_LastChunk = name;
        Native::Log("game: the player in %s%s", name, Where(player).c_str());
    }

    // $TWIN_TEST_SOUND=0xae,0x63: those sounds played (as the game plays one, unplaced) every 3 s of play from 5 s in (the
    // sound's tests)
    static const char* testSounds = std::getenv("TWIN_TEST_SOUND");
    static Clock::time_point playing{};
    static Clock::time_point lastTest{};
    if (testSounds != nullptr && state == GameController::StatePlaying)
    {
        if (playing == Clock::time_point{})
        {
            playing = now;
        }

        // ($TWIN_TEST_SOUND_DELAY: from so many seconds of play instead)
        static const s32 delay = std::getenv("TWIN_TEST_SOUND_DELAY") != nullptr ? std::atoi(std::getenv("TWIN_TEST_SOUND_DELAY")) : 5;
        if (now - playing >= std::chrono::seconds(delay) && now - lastTest >= std::chrono::seconds(3))
        {
            lastTest = now;
            for (const char* at = testSounds; *at != '\0';)
            {
                char* end = nullptr;
                u32 id = static_cast<u32>(std::strtoul(at, &end, 0));
                if (end == at)
                {
                    break;
                }

                u32 channel = PlaySoundById(1.0f, -1.0f, id, 0, ListenerVoiceKind(), -1);
                Native::Log("test sound: %#x played (%#x)", id, channel);
                at = *end == ',' ? end + 1 : end;
            }
        }
    }

    if (Native::DetailedLog() && state == GameController::StatePlaying && now - lastPlace >= std::chrono::seconds(5))
    {
        lastPlace = now;
        Native::Log("game: the player%s in %s", Where(player).c_str(), name != nullptr ? name : "?");
    }
}

const char* NativeUi::TrailChunk()
{
    return g_LastChunk.empty() ? nullptr : g_LastChunk.c_str();
}

std::string NativeUi::TrailNow()
{
    GameController* controller = G_GameController;
    if (controller == nullptr)
    {
        return "the game not started";
    }

    u32 state = controller->State();
    InstanceContext* player = Player();
    ChunkData* chunk = player != nullptr ? player->chunk : nullptr;
    char text[256];
    std::snprintf(text, sizeof(text), "the game's state %s (%u), the player in %s%s", StateName(state), state,
                  chunk != nullptr && chunk->name.string != nullptr ? chunk->name.string : "no chunk", Where(player).c_str());
    return text;
}
