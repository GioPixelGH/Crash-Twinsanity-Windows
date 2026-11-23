// The level sweep (testing, native/LEVEL_SWEEP.md): $TWIN_SWEEP_LEVEL=Levels\Earth\Hub\Beach starts the game in that chunk the way
// the game's own command-line start does (the travellers' tales logo's branch: the progress's start chunk, a saved game's entry,
// the save controller taking the progress, the level loaded), then plays it with scripted input for $TWIN_SWEEP_SECONDS (30) of
// play and exits 0. The states it went through are logged ("sweep:"); it exits 3 when play hasn't started in $TWIN_SWEEP_TIMEOUT
// (180) seconds. The input: the left stick turned round a little each cycle, cross and square now and then, any held button
// skipping the cutscenes ("hold any button to skip": cross is held 1 s each cycle)
#include "ui/bindings.h"

#include "cutsceneskip.h"
#include "native.h"

#include "game/agentlab.h"
#include "game/agentparts.h"
#include "game/agents.h"
#include "game/behaviours.h"
#include "game/chunkdata.h"
#include "game/clock.h"
#include "game/commands.h"
#include "game/events.h"
#include "game/followcamera.h"
#include "game/memory.h"
#include "game/gamecontroller.h"
#include "game/instances.h"
#include "game/objectnode.h"
#include "game/objects.h"
#include "game/place.h"
#include "game/player.h"
#include "game/reference.h"
#include "game/string.h"
#include "graphics/presenter.h"
#include "platform/time.h"

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
// The sweep's clock: real time, or with $TWIN_SWEEP_GAME_TIME the game clock's ticks (with $TWIN_FIXED_TIME a frame's worth each
// frame), so a run's hops and waits come at the same frames every run and two runs' pictures can be compared frame by frame
struct Clock
{
    using duration = std::chrono::steady_clock::duration;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<Clock>;
    static constexpr bool is_steady = true;

    static time_point now()
    {
        static const bool gameTime = std::getenv("TWIN_SWEEP_GAME_TIME") != nullptr;
        if (gameTime)
        {
            return time_point(std::chrono::duration_cast<duration>(
                std::chrono::duration<double>(static_cast<double>(GameClockTicks()) / Platform::Time::TicksPerSecond)));
        }

        return time_point(std::chrono::steady_clock::now().time_since_epoch());
    }
};

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

// Whether an instance's object node is an object node (ObjectNodeBase: runners, a source node), not the smaller node of a pickup of
// subtype 16 or 17, nor a projectile's, whose base part's runners aren't its own (instancefactory.cpp's MakeAgentObjectNode;
// projectiles.cpp: on the PS2 its members overlay them)
bool IsSceneObjectNode(InstanceContext* instance)
{
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&instance->nodes, NodeObject));
    const GameObject* object = node != nullptr ? node->object : nullptr;
    return node != nullptr && !(object != nullptr && object->header.type == GameObject::TypePickup && object->header.subtype >= 0x10 &&
                                object->header.subtype < 0x12) &&
           !(object != nullptr && object->header.type == GameObject::TypeProjectile);
}

// (Only those with an object node whose runners are its own: the steps read them)
void ListInstance(InstanceContext* instance, void* user)
{
    auto* list = static_cast<InstanceList*>(user);
    std::string lower = ObjectNameOf(instance);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if ((list->chunk == nullptr || instance->chunk == list->chunk) && lower.find(list->name) != std::string::npos &&
        IsSceneObjectNode(instance))
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
            // (the load wall's middle: where a fly step goes through the link as a player walking would)
            Vector4 middle = {0.0f, 0.0f, 0.0f, 1.0f};
            if (link->loadWall != nullptr)
            {
                for (const Vector4& corner : link->loadWall->corners)
                {
                    middle.x += corner.x * 0.25f;
                    middle.y += corner.y * 0.25f;
                    middle.z += corner.z * 0.25f;
                }
            }

            Native::Log("hop:   linked to %s%s%s, its wall's middle at (%.1f, %.1f, %.1f)", linked,
                        link->loadWall != nullptr ? "" : " (no wall)", link->flags.linkedRm2Loaded != 0 ? "" : " (not loaded)",
                        middle.x, middle.y, middle.z);
        }

        lastChunk = name;
    }

    // $TWIN_WATCH=NAME[,NAME...]: every frame, the live instances whose object's name holds a NAME (any case, any chunk) logged
    // with the sweep's clock (the game clock's with $TWIN_SWEEP_GAME_TIME), their place and the player's ("watch: frame F t T
    // NAME#ID at (...), the player at (...)", ID the instance's address's low bits): the chases past 60 Hz (THE LIST 29a)
    {
        static const char* watched = std::getenv("TWIN_WATCH");
        static u32 frame = 0;
        frame++;
        std::string names = watched != nullptr ? watched : "";
        for (size_t start = 0; start < names.size();)
        {
            size_t comma = names.find(',', start);
            comma = comma == std::string::npos ? names.size() : comma;
            std::string lower = names.substr(start, comma - start);
            start = comma + 1;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            InstanceList list = {nullptr, lower, {}};
            ForEachLiveInstance(ListInstance, &list);
            player->place->SyncPosition();
            for (InstanceContext* instance : list.found)
            {
                instance->place->SyncPosition();
                Native::Log("watch: frame %u t %.4f %s#%04x at (%.2f, %.3f, %.2f), the player at (%.2f, %.2f, %.2f)", frame,
                            std::chrono::duration<double>(Clock::now() - g_Start).count(), ObjectNameOf(instance),
                            static_cast<u32>(reinterpret_cast<uiptr>(instance) & 0xFFFF),
                            instance->place->position.x, instance->place->position.y, instance->place->position.z,
                            player->place->position.x, player->place->position.y, player->place->position.z);
            }
        }
    }

    if (next < targets.size() && name.size() >= targets[next].size() &&
        name.compare(name.size() - targets[next].size(), std::string::npos, targets[next]) == 0)
    {
        next++;
        return;
    }

    // A "fly" step under way: the player put a little further along its line each frame (at most a tenth of a second's way: after a
    // cutscene or a restart it goes on from where it was), until it's there, its chunk changes (a link took it: the next chunk's
    // places are its own) or the player is another (a cutscene switched the character)
    static bool flying = false;
    static Vector4 flyAt;
    static Vector4 flyTo;
    static f32 flySpeed = 10.0f;
    static Clock::time_point flyLast;
    static std::string flyChunk;
    static const InstanceContext* flyPlayer = nullptr;
    Clock::time_point now = Clock::now();
    if (flying)
    {
        f32 step = flySpeed * std::min(std::chrono::duration<f32>(now - flyLast).count(), 0.1f);
        flyLast = now;
        f32 dx = flyTo.x - flyAt.x;
        f32 dy = flyTo.y - flyAt.y;
        f32 dz = flyTo.z - flyAt.z;
        f32 left = std::sqrt(dx * dx + dy * dy + dz * dz);
        bool stopped = name != flyChunk || player != flyPlayer;
        bool there = stopped || left <= step;
        if (!there)
        {
            flyAt.x += dx * step / left;
            flyAt.y += dy * step / left;
            flyAt.z += dz * step / left;
        }
        else if (!stopped)
        {
            flyAt = flyTo;
        }

        ObjectPlace* place = player->place;
        place->SyncPosition();
        if (!stopped && place->MoveTo(&flyAt))
        {
            QueueObject(player);
        }

        if (there)
        {
            flying = false;
            Native::Log("hop: the player %s at (%.1f, %.1f, %.1f) in %s", stopped ? "stopped flying" : "flown", place->position.x,
                        place->position.y, place->position.z, name.c_str());
            next++;
            lastHop = now;
        }

        return;
    }

    if (next >= targets.size() || now - lastHop < std::chrono::seconds(2))
    {
        return;
    }

    // Where the player was as the last step was taken, the player then and its chunk
    static Vector4 stepAt;
    static const InstanceContext* stepPlayer = nullptr;
    static std::string stepChunk;
    ObjectPlace* here = player->place;
    here->SyncPosition();
    Vector4 from = stepAt;
    bool same = stepPlayer == player && stepChunk == name;
    stepAt = here->position;
    stepPlayer = player;
    stepChunk = name;

    // "moved:N": how far the player went since the last step (in its chunk), logged with whether that's N units or more (a player
    // the scripted input moves is a player that can move: Cortex after Rooftop Rampage's scene skipped)
    if (targets[next].rfind("moved:", 0) == 0)
    {
        f32 least = static_cast<f32>(std::atof(targets[next].c_str() + 6));
        f32 dx = stepAt.x - from.x;
        f32 dz = stepAt.z - from.z;
        f32 moved = std::sqrt(dx * dx + dz * dz);
        Native::Log("hop: the player %s moved %.1f units since the last step, %s %.1f, to (%.1f, %.1f, %.1f) in %s",
                    ObjectNameOf(player), moved, same && moved >= least ? "at least" : "NOT", least, stepAt.x, stepAt.y,
                    stepAt.z, name.c_str());
        next++;
        lastHop = now;
        return;
    }

    // "fly:X:Y:Z" (or "fly:X:Y:Z:SPEED", 10 units a second): the player taken there in a straight line, a little further each
    // frame. A goto's long way is undone by the player's physics (they move it from its last place: a wall on the way stops it, a
    // gap drops it); flown, it doesn't fall on its way, and the chunk's triggers and links see it go by as they would a player's
    if (targets[next].rfind("fly:", 0) == 0)
    {
        flyTo = {0.0f, 0.0f, 0.0f, 1.0f};
        flySpeed = 10.0f;
        std::sscanf(targets[next].c_str() + 4, "%f:%f:%f:%f", &flyTo.x, &flyTo.y, &flyTo.z, &flySpeed);
        ObjectPlace* place = player->place;
        place->SyncPosition();
        flyAt = place->position;
        flyAt.w = 1.0f;
        flyChunk = name;
        flyPlayer = player;
        flyLast = now;
        flying = flySpeed > 0.0f;
        Native::Log("hop: the player flying from (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f) in %s", flyAt.x, flyAt.y, flyAt.z,
                    flyTo.x, flyTo.y, flyTo.z, name.c_str());
        next += flying ? 0 : 1;
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

    // "tell:NAME" (or "tell:NAME:MESSAGE"): user message MESSAGE (87) sent to the instances of the player's chunk whose object's
    // name holds NAME, from none, as a trigger tells it (a scene's director started where its trigger is out of a test's reach:
    // TwinsanityEvolved's tests start them so, native/tools/check_cutscene_skips.py)
    if (targets[next].rfind("tell:", 0) == 0)
    {
        std::string wanted = targets[next].substr(5);
        u32 message = 87;
        size_t colon = wanted.find(':');
        if (colon != std::string::npos)
        {
            message = static_cast<u32>(std::atoi(wanted.c_str() + colon + 1));
            wanted.resize(colon);
        }

        std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return std::tolower(c); });
        InstanceList list = {chunk, wanted, {}};
        ForEachLiveInstance(ListInstance, &list);
        for (InstanceContext* instance : list.found)
        {
            Reference* argument = nullptr;
            auto* event = GameEvent::Construct(static_cast<GameEvent*>(MemoryAllocate(sizeof(GameEvent))), message, &argument,
                                               ObjectNodeKinds);
            Reference* handle = AddEventReference(event);
            QueueEvent(instance, &handle);
            Native::Log("hop: message %u sent to %s in %s", message, ObjectNameOf(instance), name.c_str());
        }

        if (list.found.empty())
        {
            Native::Log("hop: no instance of an object named like %s in %s", wanted.c_str(), name.c_str());
        }

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

    // "states:NAME": the instances of the player's chunk whose object's name holds NAME logged with their places, their context's
    // flags (reference.h), their agent's state flags (properties.h's InstanceState) and their part's bits (agentparts.h)
    if (targets[next].rfind("states:", 0) == 0)
    {
        std::string wanted = targets[next].substr(7);
        std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return std::tolower(c); });
        InstanceList list = {chunk, wanted, {}};
        ForEachLiveInstance(ListInstance, &list);
        for (InstanceContext* instance : list.found)
        {
            AgentNode* node = AgentNodeOf(instance);
            Agent* agent = node != nullptr ? node->agent : nullptr;
            ObjectPlace* place = instance->place;
            place->SyncPosition();
            Native::Log("hop:   %s at (%.1f, %.1f, %.1f): flags %08x, state %08x, part %08x", ObjectNameOf(instance),
                        place->position.x, place->position.y, place->position.z, instance->flags.value,
                        agent != nullptr && agent->properties != nullptr ? agent->properties->state.value : 0u,
                        agent != nullptr && agent->part != nullptr ? static_cast<BasicAgentPart*>(agent->part)->bits.value : 0u);
        }

        if (list.found.empty())
        {
            Native::Log("hop: no instance of an object named like %s in %s", wanted.c_str(), name.c_str());
        }

        next++;
        lastHop = now;
        return;
    }

    // "damage:NAME": the instance nearest the player in its chunk whose object's name holds NAME sent an explosion's contact message
    // of 1 damage from the player, as the scripts' CreateDamage sends one (#58). "knock:NAME": hit by a body as a rigid body's
    // collision tells it (its agent's Collided, an impulse of 6 sideways: over a crate's breaking 5). "trigger:NAME": its trigger
    // event run, as a "!" crate's TriggerLinkedObjects runs its linked objects' (an outline crate made solid)
    bool knock = targets[next].rfind("knock:", 0) == 0;
    bool trigger = targets[next].rfind("trigger:", 0) == 0;
    if (knock || trigger || targets[next].rfind("damage:", 0) == 0)
    {
        std::string wanted = targets[next].substr(knock ? 6 : trigger ? 8 : 7);
        std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c) { return std::tolower(c); });
        ObjectPlace* place = player->place;
        place->SyncPosition();
        InstanceSearch search = {chunk, player, wanted, place->position, nullptr, 0.0f};
        ForEachLiveInstance(VisitInstance, &search);
        AgentNode* node = search.found != nullptr ? AgentNodeOf(search.found) : nullptr;
        if (node == nullptr)
        {
            Native::Log("hop: no agent of an object named like %s in %s", wanted.c_str(), name.c_str());
        }
        else
        {
            Agent* agent = node->agent;
            search.found->place->SyncPosition();
            Vector4 at = search.found->place->position;
            Native::Log("hop: %s at (%.1f, %.1f, %.1f), flags %08x, %s", ObjectNameOf(search.found), at.x, at.y, at.z,
                        search.found->flags.value,
                        knock ? "knocked by a body" : trigger ? "its trigger event run" : "sent an explosion's damage");
            if (trigger)
            {
                RunAgentEvent(agent, OnTrigger, reinterpret_cast<uiptr>(player), 0, 0);
            }
            else if (knock)
            {
                Vector4 impulse = {6.0f, 0.0f, 0.0f, 1.0f};
                CallVirtual<u32>(agent, agent->vtable, Agent::CollidedSlot, player, &at, &impulse);
            }
            else
            {
                ContactMessage message;
                ContactMessage::Construct(&message);
                message.point = at;
                message.hitKinds = HitExplosion;
                message.damage = 1;
                CallVirtual<void>(agent, agent->vtable, Agent::ContactSlot, &message, player, 1u);
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

    // "waitN": N seconds where it is before the next step (N may have a fraction: "wait2.5")
    if (targets[next].rfind("wait", 0) == 0)
    {
        double seconds = std::atof(targets[next].c_str() + 4);
        Native::Log("hop: waiting %g s in %s", seconds, name.c_str());
        next++;
        lastHop = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds)) - std::chrono::seconds(2);
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

    // "load:Levels\AltEarth\Core\corea": that chunk loaded as $TWIN_START_CHUNK loads one (the game's saved-level path), the
    // chunks played before it staying the renderer's history (THE LIST 16: Ant Agony after bossarea's sky)
    if (targets[next].rfind("load:", 0) == 0)
    {
        GameController* controller = G_GameController;
        const std::string chunkName = targets[next].substr(5);
        StringAssign(&controller->progress.startChunk, chunkName.c_str());
        controller->states.entry = EntrySaved;
        controller->saveController.Take(&controller->progress, controller->chunkManager, nullptr);
        controller->SetNextState(GameController::StateLoadingLevel);
        Native::Log("hop: loading %s from %s", chunkName.c_str(), name.c_str());
        next++;
        lastHop = now + std::chrono::seconds(2);
        return;
    }

    // "movie:H03_F": the game's movie with that in its file's name played as the game asks for one in play (RequestMovie), play
    // going on after it (THE LIST 58: #49's crash decoding that movie, $TWIN_MOVIE_GUARD)
    if (targets[next].rfind("movie:", 0) == 0)
    {
        auto upper = [](std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return text;
        };
        const std::string wanted = upper(targets[next].substr(6));
        u32 index = GameMovie::Count;
        std::string files;
        for (u32 i = 0; i < GameMovie::Count && index == GameMovie::Count; i++)
        {
            const std::string file = g_Movies[i].file != nullptr ? g_Movies[i].file : "";
            files += " " + file;
            if (upper(file).find(wanted) != std::string::npos)
            {
                index = i;
            }
        }

        if (index == GameMovie::Count)
        {
            Native::Log("hop: no movie's file is like %s:%s", wanted.c_str(), files.c_str());
        }
        else
        {
            G_GameController->RequestMovie(0, index);
            Native::Log("hop: the movie %s (%u) asked for in %s", g_Movies[index].file, index, name.c_str());
        }

        next++;
        lastHop = now;
        return;
    }

    // "hurt": the player hurt as a hit that isn't kept out leaves it, a hit point fewer and 2 s of blinking unless it's invulnerable
    // already (THE LIST 46, #18: three masks' invincibility going on into a hurt's blinking)
    if (targets[next] == "hurt")
    {
        auto* agentNode = static_cast<AgentNode*>(GetGameNode(&player->nodes, NodeCharacter));
        auto* agent = agentNode != nullptr ? static_cast<CharacterAgent*>(agentNode->agent) : nullptr;
        if (agent != nullptr)
        {
            auto* part = static_cast<CharacterPart*>(agent->part);
            u32 points = part->flags.hitPoints;
            part->flags.hitPoints = points > 1 ? points - 1 : points;
            agent->StartInvincibility(0);
            Native::Log("hop: the player hurt in %s, hit points %u to %u, invulnerable %u", name.c_str(), points,
                        static_cast<u32>(part->flags.hitPoints), static_cast<u32>(part->bits.invulnerable));
        }

        next++;
        lastHop = now - std::chrono::seconds(2);
        return;
    }

    // "unfollow": the player's follow camera stopped as FollowNode::Update stops it for another character's instance (its node
    // out of the chunk's updates, its camera asleep): the view stands still while the player moves, as #52's video shows, for
    // the detailed log's "camera:" lines (trail.cpp's CameraStillFrame)
    if (targets[next] == "unfollow")
    {
        auto* follow = static_cast<FollowNode*>(GetGameNode(&player->nodes, NodeFollow));
        if (follow != nullptr)
        {
            follow->StopFollowing();
            Native::Log("hop: the player's follow camera stopped in %s", name.c_str());
        }

        next++;
        lastHop = now - std::chrono::seconds(2);
        return;
    }

    // "credits": the end credits rolled as the game's end asks for them (PlayCredits), the chunk after them loaded and played
    // (THE LIST 44: #20's game that closed instead)
    if (targets[next] == "credits")
    {
        G_GameController->PlayCredits();
        Native::Log("hop: the credits asked for in %s", name.c_str());
        next++;
        lastHop = now;
        return;
    }

    // "mask": a mask picked up as an Aku Aku crate gives it (a hit point; the fourth makes the player invincible: THE LIST 15's
    // trails, $TWIN_MASK_TRACE)
    if (targets[next] == "mask")
    {
        alignas(PickUpHealthCommand) unsigned char command[sizeof(PickUpHealthCommand)] = {};
        reinterpret_cast<PickUpHealthCommand*>(command)->ExecuteOn(nullptr);
        Native::Log("hop: a mask picked up in %s", name.c_str());
        next++;
        lastHop = now - std::chrono::seconds(2);
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
// A place's point without changing anything of the game's: its matrix's when that's newer than its position (trail.cpp's Where)
Vector4 PlacePoint(const ObjectPlace* place)
{
    if ((place->bits.value & PlaceBits::MatrixMoved) != 0)
    {
        return {place->matrix.m[3][0], place->matrix.m[3][1], place->matrix.m[3][2], 1.0f};
    }

    return place->position;
}

std::string ChunkOf(const InstanceContext* instance)
{
    if (instance == nullptr)
    {
        return "none";
    }

    return instance->chunk == nullptr || instance->chunk->name.string == nullptr ? "no chunk" : instance->chunk->name.string;
}
}

// The camera in the log (THE LIST 59, #23's and #52's camera that stopped following Crash after a scene), changing nothing of the
// game's: the camera shown (the state word's: 0 the follow camera, 3 the game's rig, 4 the cutscenes'), where the view's camera is
// (its instance's place), the player and how far apart they are, how far round from the player's back the camera is seen from above
// (0 right behind it, 180 in front: THE LIST 83's assisted camera); then the player's follow camera: the chunks its instance and the
// player are in (a lens node in a chunk whose instances aren't updated leaves the view where it was) and the rig its lens shows; the
// character the progress plays (FollowNode::Update stops following another's), the rig's place, what its target follows and looks
// at, and the positioner's bits that hold it still
void LogCameraLines(const char* why)
{
    GameController* controller = G_GameController;
    if (controller == nullptr)
    {
        return;
    }

    InstanceContext* player = ScenePlayer();
    Vector4 at = {};
    const Vector4* facing = nullptr;
    if (player != nullptr && player->place != nullptr)
    {
        at = PlacePoint(player->place);
        facing = RowOf(&player->place->matrix, 2);
    }

    Reference* shown = controller->view.cameraObject;
    auto* shownInstance = shown != nullptr ? static_cast<InstanceContext*>(shown->object) : nullptr;
    Vector4 camera = {};
    if (shownInstance != nullptr && shownInstance->place != nullptr)
    {
        camera = PlacePoint(shownInstance->place);
    }

    f32 apart = std::sqrt((camera.x - at.x) * (camera.x - at.x) + (camera.y - at.y) * (camera.y - at.y) +
                          (camera.z - at.z) * (camera.z - at.z));
    // The way from the camera to the player against the player's facing, on the ground's plane
    f32 lookX = at.x - camera.x;
    f32 lookZ = at.z - camera.z;
    f32 off = facing == nullptr ? 0.0f
                                : std::atan2(lookX * facing->z - lookZ * facing->x, lookX * facing->x + lookZ * facing->z) * 180.0f /
                                      3.14159265f;
    Native::Log("%s: state %u, camera %u at (%.1f, %.1f, %.1f), the player %s at (%.1f, %.1f, %.1f), %.1f units apart, "
                "%.0f degrees off its back", why, controller->State(), static_cast<u32>(controller->states.camera), camera.x, camera.y,
                camera.z, player != nullptr ? OwnNameOf(player).c_str() : "none", at.x, at.y, at.z, apart, std::fabs(off));
    auto* follow = player != nullptr ? static_cast<FollowNode*>(GetGameNode(&player->nodes, NodeFollow)) : nullptr;
    if (follow == nullptr)
    {
        return;
    }

    InstanceContext* played = controller->progress.Instance(controller->progress.play.character);
    FollowCameraRig& rig = follow->camera.rig;
    FollowCameraPositioner& positioner = rig.ownPositioner;
    auto* followed = rig.ownTarget.followed != nullptr ? static_cast<InstanceContext*>(rig.ownTarget.followed->object) : nullptr;
    auto* own = follow->cameraInstance != nullptr ? static_cast<InstanceContext*>(follow->cameraInstance->object) : nullptr;
    auto* lens = own != nullptr ? static_cast<CameraLensNode*>(GetGameNode(&own->nodes, NodeCameraLens)) : nullptr;
    const char* lensRig = lens == nullptr                                                   ? "nothing"
                          : lens->rig == &rig && follow->camera.lensRig == &rig             ? "the follow rig"
                          : lens->rig == static_cast<CameraRig*>(&controller->camera)       ? "the game's rig"
                          : lens->rig == static_cast<CameraRig*>(&controller->cutsceneCamera) ? "the cutscenes' rig"
                                                                                            : "another rig";
    Native::Log("%s:   the follow camera's instance %s, in %s (the player in %s), its lens on %s%s", why,
                own == nullptr ? "none" : own == shownInstance ? "shown" : "not shown", ChunkOf(own).c_str(), ChunkOf(player).c_str(),
                lensRig, lens != nullptr && lens->next != nullptr ? " blending to another" : "");
    Native::Log("%s:   the character played %s, the rig following %s, at (%.1f, %.1f, %.1f) looking at (%.1f, %.1f, %.1f); keyed %u, "
                "holding still %u, looking only %u, the target still %u, at the trigger's rate %u (%.2f), pushed off the collision "
                "%u; a trigger's camera %s; the last update with the clock stopped %u", why,
                played == player ? "the player" : played != nullptr ? OwnNameOf(played).c_str() : "none",
                followed == player ? "the player" : followed != nullptr ? OwnNameOf(followed).c_str() : "nothing", positioner.position.x,
                positioner.position.y, positioner.position.z, rig.ownTarget.point.x, rig.ownTarget.point.y, rig.ownTarget.point.z,
                static_cast<u32>(positioner.keyed), static_cast<u32>(positioner.bits.holdsStill),
                static_cast<u32>(positioner.bits.onlyLooksAtTarget), static_cast<u32>(positioner.bits.targetStill),
                static_cast<u32>(positioner.bits.atTriggerRate), positioner.triggerRate,
                static_cast<u32>(positioner.bits.pushedOffCollision), follow->camera.current != nullptr ? "on" : "none",
                static_cast<u32>(follow->bits.unused0));
}

// $TWIN_CAMERA_TRACE=1 (the sweep): the camera (LogCameraLines) every second of play or of a scene
void CameraTraceFrame()
{
    static const bool wanted = std::getenv("TWIN_CAMERA_TRACE") != nullptr;
    static Clock::time_point last;
    GameController* controller = G_GameController;
    if (!wanted || controller == nullptr)
    {
        return;
    }

    u32 state = controller->State();
    Clock::time_point now = Clock::now();
    if ((state != GameController::StatePlaying && state != GameController::StateWatching) || now - last < std::chrono::seconds(1))
    {
        return;
    }

    last = now;
    LogCameraLines("camera trace");
}

// $TWIN_SCENE_SCRIPTS=SECONDS:NAME[:NAME...]: SECONDS into each cutscene watched (game frames, 60 a second), the behaviour scripts
// of the instances whose object's name holds a NAME logged, any chunk's (as the hop's scripts step logs them: the hop waits while a
// cutscene plays): what a cutscene that never ends waits for (check_cutscene_skips.py's stalls)
void SceneScriptsFrame()
{
    static const char* setting = std::getenv("TWIN_SCENE_SCRIPTS");
    static u32 frames = 0;
    GameController* controller = G_GameController;
    if (setting == nullptr || controller == nullptr)
    {
        return;
    }

    if (controller->State() != GameController::StateWatching)
    {
        frames = 0;
        return;
    }

    std::string text = setting;
    size_t colon = text.find(':');
    if (++frames != static_cast<u32>(std::atof(text.c_str()) * 60.0f) || colon == std::string::npos)
    {
        return;
    }

    for (size_t start = colon + 1; start <= text.size();)
    {
        size_t end = text.find(':', start);
        end = end == std::string::npos ? text.size() : end;
        std::string name = text.substr(start, end - start);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
        InstanceList list = {nullptr, name, {}};
        ForEachLiveInstance(ListInstance, &list);
        Native::Log("scene scripts: %u instances named like %s, %u frames into the cutscene", static_cast<u32>(list.found.size()),
                    name.c_str(), frames);
        for (InstanceContext* instance : list.found)
        {
            LogScripts(instance);
        }

        start = end + 1;
    }
}

void SceneStateFrame()
{
    // $TWIN_SCENE_STATE=SECONDS: that long into the play after it (1.5 s by default), counted in game frames (60 a second), so
    // two runs with $TWIN_FIXED_TIME take it at the same frame (native/tools/check_cutscene_skips.py)
    static const char* setting = std::getenv("TWIN_SCENE_STATE");
    static const bool wanted = setting != nullptr;
    static const f32 after = setting != nullptr && std::atof(setting) > 0.0 ? static_cast<f32>(std::atof(setting)) : 1.5f;
    static bool inScene = false;
    static bool pending = false;
    static u32 frames = 0;
    GameController* controller = G_GameController;
    if (!wanted || controller == nullptr)
    {
        return;
    }

    u32 state = controller->State();
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
        frames = 0;
    }

    // Twice, a second apart: what changes between the two goes on by itself (an idle loop, a patrol), what doesn't is what the
    // scene left
    static u32 taken = 0;
    taken = frames == 0 ? 0 : taken;
    InstanceContext* player = ScenePlayer();
    frames += pending && state == GameController::StatePlaying ? 1 : 0;
    if (!pending || state != GameController::StatePlaying || static_cast<f32>(frames) < (after + static_cast<f32>(taken)) * 60.0f ||
        player == nullptr)
    {
        return;
    }

    taken++;
    pending = taken < 2;
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
    // The last cutscene's agents (cutsceneskip.cpp's), where they are and what they run (the skip's check: watched and skipped
    // compared, native/tools/check_cutscene_skips.py)
    const InstanceContext* agents[128];
    u32 count = LastCutsceneAgents(agents, 128);
    for (u32 index = 0; index < count; index++)
    {
        auto* agent = const_cast<InstanceContext*>(agents[index]);
        if (agent == player || agent->place == nullptr)
        {
            continue;
        }

        ObjectPlace* at = agent->place;
        at->SyncPosition();
        bool runsScene = false;
        std::string agentRunners = RunnersOf(agent, &runsScene);
        Native::Log("scene state: agent %s at (%.2f, %.2f, %.2f), visible %u:%s", OwnNameOf(agent).c_str(), at->position.x,
                    at->position.y, at->position.z, static_cast<u32>(agent->flags.visible), agentRunners.c_str());
    }

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
    Native::EndSession("exit: the level sweep's run ended (status %d)", status);
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(status);
}
}

void LogCamera(const char* why)
{
    LogCameraLines(why);
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
        SceneScriptsFrame();
        CameraTraceFrame();
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
    // ($TWIN_SETTINGS_PLAY_TEST pauses and resumes the game itself: settingsshots.cpp)
    static const bool settingsPlayTest = std::getenv("TWIN_SETTINGS_PLAY_TEST") != nullptr;
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
        // $TWIN_SWEEP_STORY=N: the story got to area N (and the areas open up to it), as a played game's progress would be there:
        // the cutscenes a script starts only at a point of the story (StoryAreaCondition; check_cutscene_skips.py)
        if (const char* story = std::getenv("TWIN_SWEEP_STORY"))
        {
            PlayState play = controller->progress.play;
            play.story = static_cast<u32>(std::atoi(story));
            play.open = std::max<u32>(play.open, play.story);
            controller->progress.play = play;
            Native::Log("sweep: the story at area %u", static_cast<u32>(play.story));
        }

        // $TWIN_SWEEP_PAIRING=PAIRING[:FIRST:SECOND]: the characters paired as a played game's progress would have them there (3 the
        // Rollerbrawl, FIRST driving: 0 Crash, 1 Cortex), so play starts on it as from a checkpoint (THE LIST 90b: the ball's end)
        if (const char* pairing = std::getenv("TWIN_SWEEP_PAIRING"))
        {
            u32 kind = 0;
            u32 first = 0;
            u32 second = 1;
            std::sscanf(pairing, "%u:%u:%u", &kind, &first, &second);
            PlayState play = controller->progress.play;
            play.pairing = kind;
            play.character = first;
            play.second = second;
            controller->progress.play = play;
            Native::Log("sweep: the characters paired %u (%u first, %u second)", kind, first, second);
        }

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
    else if (state == GameController::StatePaused && !settingsPlayTest)
    {
        // Out of a pause the scripted start may have made (not the settings from play's test's: its own pause and resume)
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

// $TWIN_PAD_SCRIPT=FROM-TO:WHAT[,WHAT...][;...]: from the pads' FROMth read to before their TOth (a game frame each since the
// start; with $TWIN_FIXED_TIME the same frames every run) the pad holds WHAT: rx=N, ry=N, lx=N, ly=N (a stick's byte, 0x80 the
// middle, up 0) or a button's name (cross, square, circle, triangle, l1, r1, l2, r2, l3, r3, start, select, up, down, left,
// right). "FROM:WHAT" is one read. Each span logged as it starts. With $TWIN_PAD_SCRIPT_TIME=1 FROM and TO are seconds of the game
// clock since play started ("10.5-11:cross"), the same moments at any frame rate (THE LIST 29a's chases at 60, 144 and 240 Hz)
void PadScriptInput(BoundPad* pad)
{
    struct Span
    {
        double from;
        double to;
        s32 sticks[4];
        u32 buttons;
        bool logged;
    };
    static const char* script = std::getenv("TWIN_PAD_SCRIPT");
    // ($TWIN_PAD_SCRIPT_TIME=1: FROM and TO are seconds of the game clock since play started, with fractions, the same moments at
    // any frame rate; a span's read past play's start)
    static const bool timed = std::getenv("TWIN_PAD_SCRIPT_TIME") != nullptr;
    if (script == nullptr)
    {
        return;
    }

    static std::vector<Span> spans;
    static bool parsed = false;
    if (!parsed)
    {
        parsed = true;
        static const struct
        {
            const char* name;
            u32 bit;
        } Buttons[] = {{"cross", PadBitCross},   {"square", PadBitSquare}, {"circle", PadBitCircle}, {"triangle", PadBitTriangle},
                       {"l1", PadBitL1},         {"r1", PadBitR1},         {"l2", PadBitL2},         {"r2", PadBitR2},
                       {"l3", PadBitL3},         {"r3", PadBitR3},         {"start", PadBitStart},   {"select", PadBitSelect},
                       {"up", PadBitUp},         {"down", PadBitDown},     {"left", PadBitLeft},     {"right", PadBitRight}};
        std::string all = script;
        size_t at = 0;
        while (at < all.size())
        {
            size_t end = all.find(';', at);
            std::string step = all.substr(at, end == std::string::npos ? std::string::npos : end - at);
            at = end == std::string::npos ? all.size() : end + 1;
            size_t colon = step.find(':');
            if (colon == std::string::npos)
            {
                continue;
            }

            Span span = {0.0, 0.0, {-1, -1, -1, -1}, 0, false};
            span.from = std::strtod(step.c_str(), nullptr);
            size_t dash = step.find('-');
            span.to = dash != std::string::npos && dash < colon ? std::strtod(step.c_str() + dash + 1, nullptr) : span.from + 1;
            std::string what = step.substr(colon + 1);
            size_t w = 0;
            while (w < what.size())
            {
                size_t comma = what.find(',', w);
                std::string item = what.substr(w, comma == std::string::npos ? std::string::npos : comma - w);
                w = comma == std::string::npos ? what.size() : comma + 1;
                static const char* const Sticks[] = {"lx=", "ly=", "rx=", "ry="};
                for (u32 s = 0; s < 4; s++)
                {
                    if (item.rfind(Sticks[s], 0) == 0)
                    {
                        span.sticks[s] = std::clamp(std::atoi(item.c_str() + 3), 0, 255);
                    }
                }

                for (const auto& button : Buttons)
                {
                    if (item == button.name)
                    {
                        span.buttons |= button.bit;
                    }
                }
            }

            spans.push_back(span);
        }

        Native::Log("pad script: %zu spans", spans.size());
    }

    static u32 read = 0;
    u32 now = read++;
    // (where play and the first cutscene start in reads: PCSX2's runs time their keys and screenshots from there)
    static bool played = false;
    static bool watched = false;
    GameController* controller = G_GameController;
    static double playedAt = 0.0;
    const double clock = static_cast<double>(GameClockTicks()) / static_cast<double>(Platform::Time::TicksPerSecond);
    if (!played && controller != nullptr && controller->State() == GameController::StatePlaying)
    {
        played = true;
        playedAt = clock;
        Native::Log("pad script: play starts at read %u", now);
    }

    const double at = timed ? (played ? clock - playedAt : -1.0) : static_cast<double>(now);

    if (played && !watched && controller != nullptr && controller->State() == GameController::StateWatching)
    {
        watched = true;
        Native::Log("pad script: a cutscene starts at read %u", now);
    }

    for (Span& span : spans)
    {
        if (at < span.from || at >= span.to)
        {
            continue;
        }

        if (!span.logged)
        {
            span.logged = true;
            Native::Log("pad script: from %g to %g (read %u)", span.from, span.to, now);
        }

        u8* sticks[4] = {&pad->leftX, &pad->leftY, &pad->rightX, &pad->rightY};
        for (u32 s = 0; s < 4; s++)
        {
            if (span.sticks[s] >= 0)
            {
                *sticks[s] = static_cast<u8>(span.sticks[s]);
            }
        }

        pad->buttons |= span.buttons;
    }
}
}
