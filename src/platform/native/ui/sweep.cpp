// The level sweep (testing, native/LEVEL_SWEEP.md): $TWIN_SWEEP_LEVEL=Levels\Earth\Hub\Beach starts the game in that chunk the way
// the game's own command-line start does (the travellers' tales logo's branch: the progress's start chunk, a saved game's entry,
// the save controller taking the progress, the level loaded), then plays it with scripted input for $TWIN_SWEEP_SECONDS (30) of
// play and exits 0. The states it went through are logged ("sweep:"); it exits 3 when play hasn't started in $TWIN_SWEEP_TIMEOUT
// (180) seconds. The input: the left stick turned round a little each cycle, cross and square now and then, any held button
// skipping the cutscenes ("hold any button to skip": cross is held 1 s each cycle)
#include "ui/bindings.h"

#include "native.h"

#include "game/agentlab.h"
#include "game/agents.h"
#include "game/behaviours.h"
#include "game/chunkdata.h"
#include "game/gamecontroller.h"
#include "game/instances.h"
#include "game/objectnode.h"
#include "game/objects.h"
#include "game/place.h"
#include "game/player.h"
#include "game/reference.h"
#include "game/string.h"
#include "graphics/presenter.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <string>
#include <thread>
#include <vector>

namespace NativeUi
{
namespace
{
using Clock = std::chrono::steady_clock;

bool g_Read = false;
std::string g_Level;
f32 g_Seconds = 30.0f;
f32 g_Timeout = 180.0f;
bool g_Warped = false;
u32 g_LastState = ~0u;
Clock::time_point g_Start;
Clock::time_point g_PlayStart;
bool g_Playing = false;
f32 g_Played = 0.0f;
Clock::time_point g_LastFrame;
u32 g_Frames = 0;
// The hops' "idle" step: no scripted input from then on (until an "input" step)
std::atomic<bool> g_HopIdle{false};

void ReadSettings()
{
    if (g_Read)
    {
        return;
    }

    g_Read = true;
    const char* level = std::getenv("TWIN_SWEEP_LEVEL");
    g_Level = level != nullptr ? level : "";
    if (const char* seconds = std::getenv("TWIN_SWEEP_SECONDS"))
    {
        g_Seconds = static_cast<f32>(std::atof(seconds));
    }

    if (const char* timeout = std::getenv("TWIN_SWEEP_TIMEOUT"))
    {
        g_Timeout = static_cast<f32>(std::atof(timeout));
    }

    g_Start = Clock::now();
    g_LastFrame = g_Start;
}

// The name of an instance's object ("" for none)
const char* ObjectNameOf(InstanceContext* instance)
{
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&instance->nodes, NodeObject));
    if (node == nullptr)
    {
        return "";
    }

    GameObject* object = node->sourceNode != nullptr ? SourceObject(node->sourceNode) : node->object;
    return object != nullptr && object->name.string != nullptr ? object->name.string : "";
}

// The live instances of a chunk: the objects step's list, or the nearest to a place of those whose object's name holds a text
// (any case)
struct InstanceSearch
{
    ChunkData* chunk;
    InstanceContext* player;
    // Empty: the instances logged
    std::string name;
    Vector4 from;
    InstanceContext* found;
    f32 distance;
};

void VisitInstance(InstanceContext* instance, void* user)
{
    auto* search = static_cast<InstanceSearch*>(user);
    if (instance->chunk != search->chunk || instance == search->player || instance->place == nullptr)
    {
        return;
    }

    std::string name = ObjectNameOf(instance);
    ObjectPlace* place = instance->place;
    place->SyncPosition();
    Vector4 position = place->position;
    if (search->name.empty())
    {
        Native::Log("hop:   %s at (%.1f, %.1f, %.1f)", name.c_str(), position.x, position.y, position.z);
        return;
    }

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower.find(search->name) == std::string::npos)
    {
        return;
    }

    f32 x = position.x - search->from.x;
    f32 y = position.y - search->from.y;
    f32 z = position.z - search->from.z;
    f32 distance = std::sqrt(x * x + y * y + z * z);
    if (search->found == nullptr || distance < search->distance)
    {
        search->found = instance;
        search->distance = distance;
    }
}

}

bool PutPlayerAbove(InstanceContext* player, const char* name, f32 height)
{
    std::string wanted = name;
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return std::tolower(c); });
    ObjectPlace* place = player->place;
    place->SyncPosition();
    InstanceSearch search = {player->chunk, player, wanted, place->position, nullptr, 0.0f};
    ForEachLiveInstance(VisitInstance, &search);
    if (search.found == nullptr)
    {
        return false;
    }

    Vector4 to = search.found->place->position;
    to.y += height;
    to.w = 1.0f;
    Native::Log("physics test: the player put %.1f above %s at (%.1f, %.1f, %.1f)", height, ObjectNameOf(search.found),
                search.found->place->position.x, search.found->place->position.y, search.found->place->position.z);
    if (place->MoveTo(&to))
    {
        QueueObject(player);
    }

    return true;
}

namespace
{
// The live instances of a chunk whose object's name holds a text (any case; none: every one): the scripts and state steps'
struct InstanceList
{
    ChunkData* chunk;
    std::string name;
    std::vector<InstanceContext*> found;
};

void ListInstance(InstanceContext* instance, void* user)
{
    auto* list = static_cast<InstanceList*>(user);
    std::string lower = ObjectNameOf(instance);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (instance->chunk == list->chunk && lower.find(list->name) != std::string::npos)
    {
        list->found.push_back(instance);
    }
}

// A state's index in its graph (-1 for none)
s32 StateIndex(GraphData* graph, GraphState* state)
{
    s32 index = 0;
    for (GraphState* at = graph != nullptr ? graph->states : nullptr; at != nullptr; at = at->next, index++)
    {
        if (at == state)
        {
            return index;
        }
    }

    return -1;
}

// An instance's behaviour runners logged: each level's graph and the state it's in, and the graph's states (a state's child
// behaviour and packet, each body's condition (its ID and parameter, ! inverted), the state it jumps to and its commands' vtables;
// the completion body first, marked)
void LogScripts(InstanceContext* instance)
{
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&instance->nodes, NodeObject));
    if (node == nullptr)
    {
        return;
    }

    for (u32 slot = 0; slot < 2; slot++)
    {
        BehaviourRunner* runner = node->runners[slot];
        if (runner == nullptr)
        {
            continue;
        }

        Native::Log("hop:   %s's runner %u, depth %u", ObjectNameOf(instance), slot, runner->depth);
        for (u32 at = 0; at < BehaviourRunner::Levels && (at == 0 || at <= runner->depth); at++)
        {
            BehaviourLevel* level = runner->levels[at];
            GraphData* graph = level != nullptr ? level->graph : nullptr;
            if (graph == nullptr)
            {
                continue;
            }

            Native::Log("hop:     level %u: graph %u %.*s in state %d%s", at, graph->bits.id, static_cast<int>(graph->name.length),
                        graph->name.string != nullptr ? graph->name.string : "", StateIndex(graph, level->state),
                        level->bits.finished != 0 ? " (finished)" : "");
            s32 index = 0;
            for (GraphState* state = graph->states; state != nullptr; state = state->next, index++)
            {
                std::string bodies;
                u32 body = 0;
                for (StateBody* item = state->bodies; item != nullptr; item = item->next, body++)
                {
                    char text[64];
                    bodies += body == 0 && state->bits.hasCompletion != 0 ? " [done" : " [";
                    if (item->condition != nullptr)
                    {
                        std::snprintf(text, sizeof(text), " if %s%u(%u)", item->condition->bits.inverted != 0 ? "!" : "",
                                      item->condition->bits.id, item->condition->bits.parameter);
                        bodies += text;
                    }

                    for (ScriptCommand* command = item->commands; command != nullptr; command = command->next)
                    {
                        // (The builder leaves the IDs unset: the vtable tells the command's class)
                        std::snprintf(text, sizeof(text), " %p", static_cast<const void*>(command->vtable));
                        bodies += text;
                    }

                    std::snprintf(text, sizeof(text), " -> %d]", item->jump != nullptr ? StateIndex(graph, item->jump) : -1);
                    bodies += text;
                }

                Native::Log("hop:       state %d%s%s:%s", index, state->packet != nullptr ? " packet" : "",
                            state->bits.child != NoScriptId ? (" child " + std::to_string(state->bits.child)).c_str() : "",
                            bodies.c_str());
            }
        }
    }
}

// Whether a runner's level of an instance runs a graph whose name holds a text (lower case): its slot and level
bool RunsGraph(InstanceContext* instance, const std::string& graphName, u32* slot, u32* at)
{
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&instance->nodes, NodeObject));
    for (u32 runnerSlot = 0; node != nullptr && runnerSlot < 2; runnerSlot++)
    {
        BehaviourRunner* runner = node->runners[runnerSlot];
        for (u32 level = 0; runner != nullptr && level < BehaviourRunner::Levels; level++)
        {
            GraphData* graph = runner->levels[level] != nullptr ? runner->levels[level]->graph : nullptr;
            if (graph == nullptr || graph->name.string == nullptr)
            {
                continue;
            }

            std::string lower(graph->name.string, graph->name.length);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            if (lower.find(graphName) != std::string::npos)
            {
                *slot = runnerSlot;
                *at = level;
                return true;
            }
        }
    }

    return false;
}

// A runner's level made to jump to a state of its graph, as a body's jump does (the levels above it unwound, or the packet ended;
// the state's packet started, the state entered at the runner's next frame)
bool JumpToState(InstanceContext* instance, u32 slot, u32 at, s32 wanted)
{
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&instance->nodes, NodeObject));
    BehaviourRunner* runner = node != nullptr && slot < 2 ? node->runners[slot] : nullptr;
    BehaviourLevel* level = runner != nullptr && at < BehaviourRunner::Levels ? runner->levels[at] : nullptr;
    GraphState* target = level != nullptr && level->graph != nullptr ? level->graph->states : nullptr;
    for (s32 index = 0; target != nullptr && index < wanted; index++)
    {
        target = target->next;
    }

    if (target == nullptr || wanted < 0)
    {
        return false;
    }

    if (level->state != nullptr && level->state->bits.child != NoScriptId)
    {
        runner->Unwind(level);
    }
    else
    {
        runner->EndPacket();
    }

    if (target->packet != nullptr)
    {
        runner->packet = target->packet;
        GameNode* agent = runner->agentNode;
        CallVirtual<void>(agent, agent->vtable, ObjectNode::PacketStartedSlot, runner);
        runner->flags.packetRuns = 1;
    }

    level->entered = level->state;
    level->state = target;
    level->bits.finished = 0;
    level->bits.restart = 0;
    runner->depth = static_cast<u8>(at);
    runner->flags.runLevels = 1;
    return true;
}

// $TWIN_HOP=cryscave,escape: while playing, the player taken through its chunk's link to each of those chunks in turn (the tests'
// way into chunks only entered from a neighbour), placed as the game's WarpToChunkLinkTowardsPlayer places an instance: half a
// unit in front of the link's load wall, its middle, where the link takes it. The player's chunk changes are logged
void HopFrame()
{
    static const char* hops = std::getenv("TWIN_HOP");
    static std::vector<std::string> targets;
    static size_t next = 0;
    static std::string lastChunk;
    static Clock::time_point lastHop;
    if (hops == nullptr || G_GameController == nullptr || G_GameController->State() != GameController::StatePlaying ||
        g_PlayerInstance == nullptr || g_PlayerInstance->object == nullptr)
    {
        return;
    }

    if (targets.empty())
    {
        std::string list = hops;
        for (size_t start = 0; start <= list.size();)
        {
            size_t comma = list.find(',', start);
            comma = comma == std::string::npos ? list.size() : comma;
            if (comma > start)
            {
                targets.push_back(list.substr(start, comma - start));
            }
            start = comma + 1;
        }
    }

    auto* player = static_cast<InstanceContext*>(g_PlayerInstance->object);
    ChunkData* chunk = player->chunk;
    if (chunk == nullptr || chunk->name.string == nullptr)
    {
        return;
    }

    std::string name = chunk->name.string;
    if (name != lastChunk)
    {
        Native::Log("hop: the player in %s", name.c_str());
        for (ChunkLinkData* link = chunk->links; link != nullptr; link = link->next)
        {
            const char* linked = link->linkedData != nullptr && link->linkedData->name.string != nullptr
                                     ? link->linkedData->name.string
                                     : "?";
            Native::Log("hop:   linked to %s%s%s", linked, link->loadWall != nullptr ? "" : " (no wall)",
                        link->flags.linkedRm2Loaded != 0 ? "" : " (not loaded)");
        }

        lastChunk = name;
    }

    if (next < targets.size() && name.size() >= targets[next].size() &&
        name.compare(name.size() - targets[next].size(), std::string::npos, targets[next]) == 0)
    {
        next++;
        return;
    }

    Clock::time_point now = Clock::now();
    if (next >= targets.size() || now - lastHop < std::chrono::seconds(2))
    {
        return;
    }

    // "goto:X:Y:Z": the player put there in its chunk
    if (targets[next].rfind("goto:", 0) == 0)
    {
        Vector4 to = {0.0f, 0.0f, 0.0f, 1.0f};
        std::sscanf(targets[next].c_str() + 5, "%f:%f:%f", &to.x, &to.y, &to.z);
        Native::Log("hop: the player put at (%.1f, %.1f, %.1f) in %s", to.x, to.y, to.z, name.c_str());
        ObjectPlace* place = player->place;
        place->SyncPosition();
        if (place->MoveTo(&to))
        {
            QueueObject(player);
        }

        next++;
        lastHop = now;
        return;
    }

    // "objects": the live instances of the player's chunk logged, their objects' names and places
    if (targets[next] == "objects")
    {
        Native::Log("hop: the instances in %s", name.c_str());
        InstanceSearch search = {chunk, player, "", {}, nullptr, 0.0f};
        ForEachLiveInstance(VisitInstance, &search);
        next++;
        lastHop = now;
        return;
    }

    // "scripts:NAME": the behaviour runners of the instances of the player's chunk whose object's name holds NAME logged (each
    // level's graph and state, the graph's states). "state:GRAPH:N": the runner's level that runs a graph whose name holds GRAPH
    // (the first of the player's chunk) made to jump to the graph's state N (where playing it out can't take a test: a boss
    // beaten)
    bool jump = targets[next].rfind("state:", 0) == 0;
    if (jump || targets[next].rfind("scripts:", 0) == 0)
    {
        std::string wanted = targets[next].substr(jump ? 6 : 8);
        s32 state = -1;
        size_t colon = wanted.find(':');
        if (colon != std::string::npos)
        {
            state = std::atoi(wanted.c_str() + colon + 1);
            wanted.resize(colon);
        }

        std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return std::tolower(c); });
        InstanceList list = {chunk, jump ? std::string() : wanted, {}};
        ForEachLiveInstance(ListInstance, &list);
        if (jump)
        {
            InstanceContext* runs = nullptr;
            u32 slot = 0;
            u32 at = 0;
            for (InstanceContext* instance : list.found)
            {
                if (RunsGraph(instance, wanted, &slot, &at))
                {
                    runs = instance;
                    break;
                }
            }

            bool jumped = runs != nullptr && JumpToState(runs, slot, at, state);
            Native::Log("hop: %s's runner %u, level %u, %s state %d of %s in %s",
                        runs != nullptr ? ObjectNameOf(runs) : "no instance", slot, at,
                        jumped ? "made to jump to" : "not made to jump to", state, wanted.c_str(), name.c_str());
        }
        else if (list.found.empty())
        {
            Native::Log("hop: no instance of an object named like %s in %s", wanted.c_str(), name.c_str());
        }
        else
        {
            for (InstanceContext* instance : list.found)
            {
                LogScripts(instance);
            }
        }

        next++;
        lastHop = now;
        return;
    }

    // "on:NAME" (or "on:NAME:HEIGHT"): the player put HEIGHT (2) units above the instance nearest it in its chunk whose object's
    // name holds NAME. "bring:NAME" (or "bring:NAME:HEIGHT"): that instance put HEIGHT (0) units above the player instead (where
    // the player can't be put: down a slide, its physics keep it on its way)
    bool bring = targets[next].rfind("bring:", 0) == 0;
    if (bring || targets[next].rfind("on:", 0) == 0)
    {
        std::string wanted = targets[next].substr(bring ? 6 : 3);
        f32 height = bring ? 0.0f : 2.0f;
        size_t colon = wanted.find(':');
        if (colon != std::string::npos)
        {
            height = static_cast<f32>(std::atof(wanted.c_str() + colon + 1));
            wanted.resize(colon);
        }

        std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return std::tolower(c); });
        ObjectPlace* place = player->place;
        place->SyncPosition();
        InstanceSearch search = {chunk, player, wanted, place->position, nullptr, 0.0f};
        ForEachLiveInstance(VisitInstance, &search);
        if (search.found == nullptr)
        {
            Native::Log("hop: no instance of an object named like %s in %s", wanted.c_str(), name.c_str());
        }
        else if (bring)
        {
            Vector4 to = place->position;
            to.y += height;
            to.w = 1.0f;
            Native::Log("hop: %s from (%.1f, %.1f, %.1f) put %.1f above the player at (%.1f, %.1f, %.1f) in %s",
                        ObjectNameOf(search.found), search.found->place->position.x, search.found->place->position.y,
                        search.found->place->position.z, height, to.x, to.y, to.z, name.c_str());
            if (search.found->place->MoveTo(&to))
            {
                QueueObject(search.found);
            }
        }
        else
        {
            Vector4 to = search.found->place->position;
            to.y += height;
            to.w = 1.0f;
            Native::Log("hop: the player put %.1f above %s at (%.1f, %.1f, %.1f) in %s", height, ObjectNameOf(search.found),
                        search.found->place->position.x, search.found->place->position.y, search.found->place->position.z,
                        name.c_str());
            if (place->MoveTo(&to))
            {
                QueueObject(player);
            }
        }

        next++;
        lastHop = now;
        return;
    }

    // "idle", "input": the scripted input stopped (the player left to go on by itself: a slide, say) or started again
    if (targets[next] == "idle" || targets[next] == "input")
    {
        g_HopIdle = targets[next] == "idle";
        Native::Log("hop: scripted input %s at (%.1f, %.1f, %.1f) in %s", g_HopIdle ? "stopped" : "started",
                    player->place->position.x, player->place->position.y, player->place->position.z, name.c_str());
        next++;
        lastHop = now - std::chrono::seconds(2);
        return;
    }

    // "waitN": N seconds where it is before the next step
    if (targets[next].rfind("wait", 0) == 0)
    {
        s32 seconds = std::atoi(targets[next].c_str() + 4);
        Native::Log("hop: waiting %d s in %s", seconds, name.c_str());
        next++;
        lastHop = now + std::chrono::seconds(seconds) - std::chrono::seconds(2);
        return;
    }

    // "die": the player dropped 200 units below where it is (it falls to its death; play starts again at its checkpoint)
    if (targets[next] == "die")
    {
        ObjectPlace* place = player->place;
        place->SyncPosition();
        Vector4 below = place->position;
        below.y -= 200.0f;
        Native::Log("hop: the player dropped to its death from (%.1f, %.1f, %.1f) in %s", place->position.x, place->position.y,
                    place->position.z, name.c_str());
        if (place->MoveTo(&below))
        {
            QueueObject(player);
        }

        next++;
        lastHop = now + std::chrono::seconds(8);
        return;
    }

    for (ChunkLinkData* link = chunk->links; link != nullptr; link = link->next)
    {
        ChunkData* linked = link->linkedData;
        if (linked == nullptr || linked->name.string == nullptr || link->loadWall == nullptr || link->flags.linkedRm2Loaded == 0)
        {
            continue;
        }

        std::string linkedName = linked->name.string;
        if (linkedName.size() < targets[next].size() ||
            linkedName.compare(linkedName.size() - targets[next].size(), std::string::npos, targets[next]) != 0)
        {
            continue;
        }

        const LoadWall* wall = link->loadWall;
        Vector4 position = {0.0f, 0.0f, 0.0f, 1.0f};
        for (const Vector4& corner : wall->corners)
        {
            position.x += corner.x * 0.25f;
            position.y += corner.y * 0.25f;
            position.z += corner.z * 0.25f;
        }

        position.x += wall->plane.x * 0.5f;
        position.y += wall->plane.y * 0.5f;
        position.z += wall->plane.z * 0.5f;
        ObjectPlace* place = player->place;
        place->SyncPosition();
        Native::Log("hop: the player from (%.1f, %.1f, %.1f) in %s to (%.1f, %.1f, %.1f), the link to %s", place->position.x,
                    place->position.y, place->position.z, name.c_str(), position.x, position.y, position.z, linkedName.c_str());
        if (place->MoveTo(&position))
        {
            QueueObject(player);
        }

        lastHop = now;
        return;
    }

    // Not linked yet (a link whose chunk isn't loaded has no name): the player taken near the wall of each such link in turn,
    // three units in front of it (out of its reach), where the chunk loads
    static u32 approach = 0;
    u32 index = 0;
    u32 count = 0;
    for (ChunkLinkData* link = chunk->links; link != nullptr; link = link->next)
    {
        count += link->loadWall != nullptr && (link->linkedData == nullptr || link->linkedData->name.string == nullptr) ? 1 : 0;
    }

    for (ChunkLinkData* link = chunk->links; link != nullptr && count != 0; link = link->next)
    {
        if (link->loadWall == nullptr || (link->linkedData != nullptr && link->linkedData->name.string != nullptr) ||
            index++ != approach % count)
        {
            continue;
        }

        const LoadWall* wall = link->loadWall;
        Vector4 position = {0.0f, 0.0f, 0.0f, 1.0f};
        for (const Vector4& corner : wall->corners)
        {
            position.x += corner.x * 0.25f;
            position.y += corner.y * 0.25f;
            position.z += corner.z * 0.25f;
        }

        position.x += wall->plane.x * 3.0f;
        position.y += wall->plane.y * 3.0f;
        position.z += wall->plane.z * 3.0f;
        Native::Log("hop: the player near an unloaded link of %s, at (%.1f, %.1f, %.1f)", name.c_str(), position.x, position.y,
                    position.z);
        ObjectPlace* place = player->place;
        place->SyncPosition();
        if (place->MoveTo(&position))
        {
            QueueObject(player);
        }

        approach++;
        lastHop = now;
        return;
    }
}

// $TWIN_SCENE_STATE (testing): what a cutscene leaves behind, logged 1.5 s into the play that follows it (=SECONDS: that long), to
// compare a scene skipped with the same played out: the player's locked inputs, place and runners; every instance that takes its
// object from another's node (a scene's actor not given its own back); every instance that runs a scene's script (a graph named
// CUTSCENE or _CS). The game's states are the sweep's log
namespace
{
InstanceContext* ScenePlayer()
{
    return g_PlayerInstance != nullptr ? static_cast<InstanceContext*>(g_PlayerInstance->object) : nullptr;
}

CharacterAgent* ScenePlayerAgent()
{
    InstanceContext* instance = ScenePlayer();
    auto* node = instance != nullptr ? static_cast<AgentNode*>(GetGameNode(&instance->nodes, NodeCharacter)) : nullptr;
    return node != nullptr ? static_cast<CharacterAgent*>(node->agent) : nullptr;
}

// Whether an instance's object node is an object node (ObjectNodeBase: runners, a source node), not the smaller node of a pickup of
// subtype 16 or 17 (instancefactory.cpp's MakeAgentObjectNode)
bool IsSceneObjectNode(InstanceContext* instance)
{
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&instance->nodes, NodeObject));
    const GameObject* object = node != nullptr ? node->object : nullptr;
    return node != nullptr && !(object != nullptr && object->header.type == GameObject::TypePickup && object->header.subtype >= 0x10 &&
                                object->header.subtype < 0x12);
}

std::string OwnNameOf(InstanceContext* instance)
{
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&instance->nodes, NodeObject));
    GameObject* object = node != nullptr ? node->object : nullptr;
    return object != nullptr && object->name.string != nullptr ? object->name.string : "";
}

// An instance's runners' graphs and states ("" none)
std::string RunnersOf(InstanceContext* instance, bool* scene)
{
    std::string text;
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&instance->nodes, NodeObject));
    for (u32 slot = 0; node != nullptr && slot < 2; slot++)
    {
        BehaviourRunner* runner = node->runners[slot];
        for (u32 at = 0; runner != nullptr && at < BehaviourRunner::Levels && at <= runner->depth; at++)
        {
            BehaviourLevel* level = runner->levels[at];
            GraphData* graph = level != nullptr ? level->graph : nullptr;
            if (graph == nullptr || graph->name.string == nullptr)
            {
                continue;
            }

            std::string name(graph->name.string, graph->name.length);
            *scene = *scene || name.find("CUTSCENE") != std::string::npos || name.find("_CS") != std::string::npos;
            char part[160];
            std::snprintf(part, sizeof(part), " [%u.%u %s %d%s]", slot, at, name.c_str(), StateIndex(graph, level->state),
                          level->bits.finished != 0 ? " finished" : "");
            text += part;
        }
    }

    return text;
}

void LogSceneInstance(InstanceContext* instance, void*)
{
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&instance->nodes, NodeObject));
    if (!IsSceneObjectNode(instance) || instance == ScenePlayer())
    {
        return;
    }

    bool scene = false;
    std::string runners = RunnersOf(instance, &scene);
    if (node->sourceNode != nullptr)
    {
        Native::Log("scene state: %s takes its object from %s:%s", OwnNameOf(instance).c_str(), ObjectNameOf(instance),
                    runners.c_str());
    }
    else if (scene)
    {
        Native::Log("scene state: %s runs a scene:%s", OwnNameOf(instance).c_str(), runners.c_str());
    }
}
}

void SceneStateFrame()
{
    // $TWIN_SCENE_STATE=SECONDS: that long into the play after it (1.5 s by default)
    static const char* setting = std::getenv("TWIN_SCENE_STATE");
    static const bool wanted = setting != nullptr;
    static const f32 after = setting != nullptr && std::atof(setting) > 0.0 ? static_cast<f32>(std::atof(setting)) : 1.5f;
    static bool inScene = false;
    static bool pending = false;
    static Clock::time_point ended;
    GameController* controller = G_GameController;
    if (!wanted || controller == nullptr)
    {
        return;
    }

    u32 state = controller->State();
    Clock::time_point now = Clock::now();
    if (state == GameController::StateWatching)
    {
        inScene = true;
        pending = false;
        return;
    }

    if (inScene && state == GameController::StatePlaying)
    {
        inScene = false;
        pending = true;
        ended = now;
    }

    InstanceContext* player = ScenePlayer();
    if (!pending || state != GameController::StatePlaying || std::chrono::duration<f32>(now - ended).count() < after ||
        player == nullptr)
    {
        return;
    }

    pending = false;
    CharacterAgent* agent = ScenePlayerAgent();
    ObjectPlace* place = player->place;
    place->SyncPosition();
    bool scene = false;
    std::string runners = RunnersOf(player, &scene);
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&player->nodes, NodeObject));
    Native::Log("scene state: the player %s%s%s, inputs locked %08x, at (%.1f, %.1f, %.1f):%s", OwnNameOf(player).c_str(),
                node != nullptr && node->sourceNode != nullptr ? " taking its object from " : "",
                node != nullptr && node->sourceNode != nullptr ? ObjectNameOf(player) : "",
                agent != nullptr ? agent->buttons.locked.value : 0u, place->position.x, place->position.y, place->position.z,
                runners.c_str());
    ForEachLiveInstance(LogSceneInstance, nullptr);
    Native::Log("scene state: end");
}

// The player put at a position (the tests' tools), as the game's warps put an instance
void PutPlayer(InstanceContext* player, const Vector4& position)
{
    ObjectPlace* place = player->place;
    place->SyncPosition();
    if (place->MoveTo(&position))
    {
        QueueObject(player);
    }
}

// $TWIN_TRIGGER_PLAN=list or a list of steps (5,die,5): the message triggers of the player's chunk listed once play starts in
// it (by position: their index, place and box), then a step every 8 s of play: a number puts the player in that trigger, "die" drops
// it 200 units below where it is (it falls to its death). The game's states are the sweep's log
void TriggerPlanFrame()
{
    static const char* plan = std::getenv("TWIN_TRIGGER_PLAN");
    static std::vector<std::string> steps;
    static size_t next = 0;
    static std::string listedChunk;
    static std::vector<InstanceContext*> triggers;
    static Clock::time_point lastStep = Clock::now();
    if (plan != nullptr && G_GameController != nullptr && G_GameController->State() != GameController::StatePlaying)
    {
        // 8 s of play between two steps: a cutscene (or a death's restart) a step started plays out first
        lastStep = Clock::now();
    }

    if (plan == nullptr || G_GameController == nullptr || G_GameController->State() != GameController::StatePlaying ||
        g_PlayerInstance == nullptr || g_PlayerInstance->object == nullptr)
    {
        return;
    }

    auto* player = static_cast<InstanceContext*>(g_PlayerInstance->object);
    ChunkData* chunk = player->chunk;
    if (chunk == nullptr || chunk->name.string == nullptr)
    {
        return;
    }

    // The plan's deaths never end the game
    G_GameController->progress.counts.lives = 50;
    if (steps.empty())
    {
        std::string list = plan;
        for (size_t start = 0; start <= list.size();)
        {
            size_t comma = list.find(',', start);
            comma = comma == std::string::npos ? list.size() : comma;
            if (comma > start)
            {
                steps.push_back(list.substr(start, comma - start));
            }
            start = comma + 1;
        }
    }

    if (listedChunk != chunk->name.string)
    {
        listedChunk = chunk->name.string;
        struct Found
        {
            ChunkData* chunk;
            std::vector<InstanceContext*>* triggers;
        } found = {chunk, &triggers};
        triggers.clear();
        ForEachLiveInstance(
            [](InstanceContext* instance, void* user)
            {
                auto* f = static_cast<Found*>(user);
                if (instance->chunk == f->chunk && instance->place != nullptr &&
                    GetGameNode(&instance->nodes, NodeMessageTrigger) != nullptr)
                {
                    f->triggers->push_back(instance);
                }
            },
            &found);
        for (InstanceContext* trigger : triggers)
        {
            trigger->place->SyncPosition();
        }

        std::sort(triggers.begin(), triggers.end(),
                  [](const InstanceContext* a, const InstanceContext* b)
                  {
                      const Vector4& p = a->place->position;
                      const Vector4& q = b->place->position;
                      return p.x != q.x ? p.x < q.x : p.z != q.z ? p.z < q.z : p.y < q.y;
                  });
        player->place->SyncPosition();
        Native::Log("triggers: %zu in %s, the player at (%.1f, %.1f, %.1f)", triggers.size(), listedChunk.c_str(),
                    player->place->position.x, player->place->position.y, player->place->position.z);
        for (size_t i = 0; i < triggers.size(); i++)
        {
            const Vector4& p = triggers[i]->place->position;
            const Box& box = triggers[i]->collision.box;
            // The clock units between two checks of its box (0x7FFFFFFF: in effect never), and whether it's checked at all
            const auto* node = static_cast<const TriggerNode*>(GetGameNode(&triggers[i]->nodes, NodeMessageTrigger));
            Native::Log("triggers: %zu at (%.1f, %.1f, %.1f), box (%.1f, %.1f, %.1f)-(%.1f, %.1f, %.1f), checked every %d clock "
                        "units%s",
                        i, p.x, p.y, p.z, box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z, node->checkTicks,
                        node->bits.neverPolled != 0 ? " (never polled)" : "");
        }

        // The camera triggers too (not steps of a plan), with the kinds of nodes their events go to
        std::vector<InstanceContext*> cameras;
        found.triggers = &cameras;
        ForEachLiveInstance(
            [](InstanceContext* instance, void* user)
            {
                auto* f = static_cast<Found*>(user);
                if (instance->chunk == f->chunk && instance->place != nullptr &&
                    GetGameNode(&instance->nodes, NodeCameraTrigger) != nullptr)
                {
                    f->triggers->push_back(instance);
                }
            },
            &found);
        for (size_t i = 0; i < cameras.size(); i++)
        {
            cameras[i]->place->SyncPosition();
            const Vector4& p = cameras[i]->place->position;
            const Box& box = cameras[i]->collision.box;
            const auto* node = static_cast<const TriggerNode*>(GetGameNode(&cameras[i]->nodes, NodeCameraTrigger));
            Native::Log("triggers: camera %zu at (%.1f, %.1f, %.1f), box (%.1f, %.1f, %.1f)-(%.1f, %.1f, %.1f), checked every %d "
                        "clock units%s, events to 0x%X",
                        i, p.x, p.y, p.z, box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z, node->checkTicks,
                        node->bits.neverPolled != 0 ? " (never polled)" : "", node->eventKinds);
        }
    }

    Clock::time_point now = Clock::now();
    // $TWIN_TRIGGER_EVERY=SECONDS of play between two steps (8 by default)
    static const char* every = std::getenv("TWIN_TRIGGER_EVERY");
    static const f32 stepSeconds = every != nullptr && std::atof(every) > 0.0 ? static_cast<f32>(std::atof(every)) : 8.0f;
    if (next >= steps.size() || std::chrono::duration<f32>(now - lastStep).count() < stepSeconds)
    {
        return;
    }

    lastStep = now;
    const std::string& step = steps[next++];
    player->place->SyncPosition();
    Vector4 position = player->place->position;
    if (step == "die")
    {
        position.y -= 200.0f;
        Native::Log("triggers: step %s: the player dropped from (%.1f, %.1f, %.1f)", step.c_str(), player->place->position.x,
                    player->place->position.y, player->place->position.z);
        PutPlayer(player, position);
        return;
    }

    size_t index = static_cast<size_t>(std::atoi(step.c_str()));
    if (step == "list" || index >= triggers.size())
    {
        return;
    }

    triggers[index]->place->SyncPosition();
    position = triggers[index]->place->position;
    position.w = 1.0f;
    Native::Log("triggers: step %s: the player into trigger %zu at (%.1f, %.1f, %.1f)", step.c_str(), index, position.x, position.y,
                position.z);
    PutPlayer(player, position);
}

[[noreturn]] void Finish(int status, const char* why)
{
    Native::Log("sweep: %s: %s (%.1f s played, %u frames)", g_Level.c_str(), why, g_Played, g_Frames);
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(status);
}
}

bool SweepActive()
{
    ReadSettings();
    return !g_Level.empty();
}

void SweepFrame(bool fromPads)
{
    // $TWIN_START_CHUNK=Levels\Earth\Hub\bossarea: the game started in that chunk as the sweep starts it, then played by hand
    static const char* startChunk = std::getenv("TWIN_START_CHUNK");
    static bool started = false;
    if (startChunk != nullptr && !started && G_GameController != nullptr)
    {
        GameController* controller = G_GameController;
        u32 state = controller->State();
        if (state == GameController::StateVivendiLogo || state == GameController::StateTravellersTalesLogo)
        {
            started = true;
            StringAssign(&controller->progress.startChunk, startChunk);
            controller->states.entry = EntrySaved;
            controller->saveController.Take(&controller->progress, controller->chunkManager, nullptr);
            controller->SetNextState(GameController::StateLoadingLevel);
            Native::Log("start: loading %s", startChunk);
        }
    }

    if (!SweepActive())
    {
        return;
    }

    GameController* controller = G_GameController;
    if (controller == nullptr)
    {
        return;
    }

    if (!fromPads)
    {
        HopFrame();
        TriggerPlanFrame();
        SceneStateFrame();
        // $TWIN_TEST_HANG=SECONDS: the game's thread stopped that long once play starts (the hang watch's test)
        static const char* hang = std::getenv("TWIN_TEST_HANG");
        static bool hung = false;
        if (hang != nullptr && !hung && controller->State() == GameController::StatePlaying)
        {
            hung = true;
            std::this_thread::sleep_for(std::chrono::seconds(std::atoi(hang)));
        }
    }

    // Called from the game controller's update and the pads' read (pads.cpp), both every frame while the game plays: the time
    // is what passed since either's last call, the frames the game controller's
    Clock::time_point now = Clock::now();
    f32 delta = std::chrono::duration<f32>(now - g_LastFrame).count();
    g_LastFrame = now;
    u32 state = controller->State();
    static f32 lastTick = 0.0f;
    f32 sinceStart = std::chrono::duration<f32>(now - g_Start).count();
    if (std::getenv("TWIN_SWEEP_TRACE") != nullptr && sinceStart - lastTick > 5.0f)
    {
        lastTick = sinceStart;
        Native::Log("sweep: tick at %.1f s: state %u, next %u, played %.1f s, %u frames", sinceStart, state,
                    controller->NextState(), g_Played, g_Frames);
    }
    if (state != g_LastState)
    {
        Native::Log("sweep: state %u at %.1f s", state, std::chrono::duration<f32>(now - g_Start).count());
        g_LastState = state;
    }

    // At the first logo: the level, as the command line's start chunk
    if (!g_Warped && (state == GameController::StateVivendiLogo || state == GameController::StateTravellersTalesLogo))
    {
        g_Warped = true;
        StringAssign(&controller->progress.startChunk, g_Level.c_str());
        controller->states.entry = EntrySaved;
        controller->saveController.Take(&controller->progress, controller->chunkManager, nullptr);
        controller->SetNextState(GameController::StateLoadingLevel);
        Native::Log("sweep: loading %s", g_Level.c_str());
        return;
    }

    // A chunk with no character's instance in it (a hub's part or a tunnel, only ever entered from a neighbour, whose links bring
    // the characters): play would start with none (FollowCameraRig::Restart through a null vtable)
    if (controller->NextState() == GameController::StateStartingPlay)
    {
        bool any = false;
        for (u32 character = 0; character < GameProgress::Characters; character++)
        {
            any = any || controller->progress.Instance(character) != nullptr;
        }

        if (!any)
        {
            Finish(4, "no character in the chunk");
        }
    }

    // Play counted while the game runs (playing, watching a cutscene or a movie)
    bool running = state == GameController::StatePlaying || state == GameController::StateWatching ||
                   state == GameController::StateMovie;
    if (running)
    {
        if (!g_Playing)
        {
            g_Playing = true;
            g_PlayStart = now;
        }

        if (!fromPads)
        {
            g_Frames++;
        }

        g_Played += std::min(delta, 0.25f);
        controller->hudDelay = 0;
        // $TWIN_SWEEP_PICTURES=FIRST: every picture OLEG reads from a file from the FIRSTth (the levels' titles, then the screens'
        // tiles but the gallery's named two), one each 20 frames from 3 s in, read at once
        static const char* pictures = std::getenv("TWIN_SWEEP_PICTURES");
        static u32 nextPicture = pictures != nullptr ? static_cast<u32>(std::atoi(pictures)) : 0;
        if (pictures && !fromPads && g_Played > 3.0f && g_Frames % 20 == 0)
        {
            const u32 titles = OLEG::NoLevelTitle;
            u32 picture = nextPicture < titles ? OLEG::PictureLevelTitle : OLEG::PictureTiles;
            u32 wanted = nextPicture < titles ? nextPicture : nextPicture - titles;
            if (picture == OLEG::PictureTiles && (wanted == OLEG::TilesNamed || wanted == OLEG::TilesNamed + 1))
            {
                wanted = OLEG::TilesCredits;
                nextPicture = titles + OLEG::TilesCredits;
            }

            if (picture == OLEG::PictureTiles && wanted >= OLEG::TilesNone)
            {
                Finish(0, "pictures read");
            }

            Native::Log("sweep: picture %u %u", picture, wanted);
            controller->oleg.pictures[picture].wanted = wanted;
            controller->oleg.LoadPicture(picture, 1);
            nextPicture++;
        }

        // $TWIN_SWEEP_SHOTS=FOLDER:SECONDS: the window saved every SECONDS of play as it's shown (shot_000.png on; with a window,
        // not --headless): a place's pictures, the README's
        static const char* shots = std::getenv("TWIN_SWEEP_SHOTS");
        static const char* every = shots != nullptr ? std::strrchr(shots, ':') : nullptr;
        static f32 nextShot = every != nullptr ? static_cast<f32>(std::atof(every + 1)) : 0.0f;
        static u32 shot = 0;
        if (every != nullptr && nextShot > 0.0f && !fromPads && g_Played >= nextShot)
        {
            char name[32];
            std::snprintf(name, sizeof(name), "/shot_%03u.png", shot++);
            NativeGraphicsShootWindow((std::string(shots, every) + name).c_str());
            Native::Log("sweep: shot %s at %.1f s", name + 1, g_Played);
            nextShot += static_cast<f32>(std::atof(every + 1));
        }

        if (g_Played >= g_Seconds)
        {
            Finish(0, "ran");
        }
    }
    else if (state == GameController::StateGameOver)
    {
        // The scripted input lost every life (the game's own game over screen, waiting for a choice): played as far as it goes
        Finish(0, "game over");
    }
    else if (state == GameController::StatePaused)
    {
        // Out of a pause the scripted start may have made
        controller->SetNextState(GameController::StatePlaying);
    }

    if (!g_Playing && std::chrono::duration<f32>(now - g_Start).count() > g_Timeout)
    {
        char why[64];
        std::snprintf(why, sizeof(why), "never played (state %u)", state);
        Finish(3, why);
    }
}

void SweepInput(BoundPad* pad)
{
    // $TWIN_AUTOPLAY: the same input in any play (a saved game loaded by $TWIN_SAVE_TEST=load, say), from when play starts
    static const bool autoplay = std::getenv("TWIN_AUTOPLAY") != nullptr;
    f32 t = g_Played;
    if (autoplay && !SweepActive())
    {
        GameController* controller = G_GameController;
        if (controller == nullptr || controller->State() != GameController::StatePlaying)
        {
            return;
        }

        static const Clock::time_point began = Clock::now();
        t = std::chrono::duration<f32>(Clock::now() - began).count();
    }
    else if (!SweepActive() || !g_Playing)
    {
        return;
    }

    // $TWIN_SWEEP_IDLE: no scripted input (the cutscenes play out, nothing is skipped)
    static const bool idle = std::getenv("TWIN_SWEEP_IDLE") != nullptr;
    if (idle || g_HopIdle)
    {
        return;
    }

    // A 4 s cycle: the stick pushed in a direction that turns 100 degrees a cycle, cross held for the first second (a jump, and
    // the cutscenes' skip), square tapped at 2.5 s
    s32 cycle = static_cast<s32>(t / 4.0f);
    f32 inCycle = t - static_cast<f32>(cycle) * 4.0f;
    f32 angle = static_cast<f32>(cycle) * 1.745f;
    pad->leftX = static_cast<u8>(128.0f + std::sin(angle) * 127.0f);
    pad->leftY = static_cast<u8>(128.0f - std::cos(angle) * 127.0f);
    if (inCycle < 1.0f)
    {
        pad->buttons |= PadBitCross;
    }

    if (inCycle > 2.5f && inCycle < 2.7f)
    {
        pad->buttons |= PadBitSquare;
    }
}
}
