// The cutscene skip's end of a scene (native/NATIVE.md, "What a skip leaves"): a skip's rule (condition 572 with a player) jumps
// the director's level out of the state that runs the scene, and the scene's levels are dropped where they are.
// The retail game never skipped these (572 returned 0): the developers' skip paths go to the director's ending and leave undone
// what the rest of the scene would have done. What broke the game (issue #9) is done here as the rest of the scene would do it:
// - the player's inputs it gives back (its last SetPlayerInput giving them all): given back as it skips (Cortex chased by the
//   bear stayed locked), and the character it gives the player (its last SwitchCharacter: Cortex after Rooftop Rampage's scene
//   from roofcor2 stayed locked);
// - its user messages 207 ("it's over", what the actors' scene behaviours wait for last): sent as it skips, and sent again to
//   an actor of the scene whenever it waits for one (the skunk reaches its wait after its animation);
// - the actors it gives back their own object (its StopTargetBehaviour and RestoreOwnObject: the actors made to run the
//   director's object's behaviours, their object node taking its object from the director's): given back once each has finished
//   the scene's behaviour, or past ActorSeconds (the player's own character past PlayerSeconds) with it stopped as when it
//   finishes (the skunk stayed the director: its own slots read past the director's models, drawn as Crash or a crash on what was
//   read as an OGI; Crash in the Iceberg Lab's scene stayed in its animations without controls).
// The actors the scene leaves as they are (the original leaves some taking the director's object, or running a behaviour on) are
// left so; the skip path's own steps (the developers' skip scripts, the level recipes' 244 handlers) run as they did, and an
// actor they take over and leave doing nothing is given back
#include "fixes.h"

#include "native.h"
#include "ui/nativeui.h"

#include "game/agentlab.h"
#include "game/attachments.h"
#include "game/behaviours.h"
#include "game/commands.h"
#include "game/events.h"
#include "game/gamecontroller.h"
#include "game/instances.h"
#include "game/memory.h"
#include "game/objectnode.h"
#include "game/objects.h"
#include "game/player.h"
#include "game/reference.h"
#include "game/resources.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace NativeFixes
{
namespace
{
using SkipClock = std::chrono::steady_clock;
constexpr u32 SkipCondition = 572;
constexpr u32 GotUserMessageCondition = 51;
// A scene's "it's over" (the scenes' last SendUserMessage, the actors' last wait)
constexpr u32 SceneOverMessage = 207;
constexpr u32 SendUserMessageId = 54;
constexpr u32 StopTargetBehaviourId = 172;
constexpr u32 RestoreOwnObjectId = 184;
constexpr u32 SetPlayerInputId = 521;
constexpr u32 SwitchCharacterId = 625;
// The commands that set the designators the sends and the give-backs go to (the focus, AgentRef1 and AgentRef2): commands.h's
// SetFocus*, Request*Focus*, CopyDesignator, ClearFocus, Clear/SetAgentRef*
constexpr u32 DesignatorIds[] = {45,  52,  57,  59,  67,  90,  91,  97,  98,  99,  100, 101, 102,
                                 104, 106, 122, 134, 146, 154, 155, 174, 175, 556, 557, 564, 565,
                                 566, 576, 637};
// How long an actor may go on with the scene's behaviour before it's stopped, and how long a skip's actors are looked after
constexpr f32 ActorSeconds = 8.0f;
// The player's own character sooner: the "over" messages' steps run (a warp to its end mark), not the rest of its scene's
// animations (Crash in the Iceberg Lab's scene waited on one 8 s with no controls)
constexpr f32 PlayerSeconds = 1.0f;
constexpr f32 PendingSeconds = 10.0f;
constexpr f32 SendSeconds = 0.1f;
// How long the skip path's own steps take before an actor it took over and left doing nothing is given back
constexpr f32 SkipPathSeconds = 1.0f;

struct CommandClasses
{
    std::set<const GccVTableEntry*> designators;
    const GccVTableEntry* send = nullptr;
    const GccVTableEntry* stopTarget = nullptr;
    const GccVTableEntry* restoreOwn = nullptr;
    const GccVTableEntry* input = nullptr;
    const GccVTableEntry* switchCharacter = nullptr;
};

const GccVTableEntry* VTableOf(u32 id)
{
    auto* command = static_cast<ScriptCommand*>(g_ObjectBuilder->Build(id, ObjectBuilder::CommandKind));
    if (command == nullptr)
    {
        return nullptr;
    }

    const GccVTableEntry* vtable = command->vtable;
    command->next = nullptr;
    CallVirtual<void>(command, command->vtable, ScriptCommand::DestroySlot, u32{DestroyAndFree});
    return vtable;
}

// The commands' classes by their vtables (the builder leaves the commands' IDs unset)
const CommandClasses* Classes()
{
    static CommandClasses classes;
    static bool made = false;
    if (!made && g_ObjectBuilder != nullptr)
    {
        made = true;
        for (u32 id : DesignatorIds)
        {
            if (const GccVTableEntry* vtable = VTableOf(id))
            {
                classes.designators.insert(vtable);
            }
        }

        classes.send = VTableOf(SendUserMessageId);
        classes.stopTarget = VTableOf(StopTargetBehaviourId);
        classes.restoreOwn = VTableOf(RestoreOwnObjectId);
        classes.input = VTableOf(SetPlayerInputId);
        classes.switchCharacter = VTableOf(SwitchCharacterId);
    }

    return made ? &classes : nullptr;
}

bool SendsSceneOver(const ScriptCommand* command, const CommandClasses& classes)
{
    return command->vtable == classes.send &&
           static_cast<const SendUserMessageCommand*>(command)->target.message == SceneOverMessage;
}

bool GivesInputsBack(const ScriptCommand* command, const CommandClasses& classes)
{
    if (command->vtable != classes.input)
    {
        return false;
    }

    auto* input = static_cast<const SetPlayerInputCommand*>(command);
    u32 all = 1u << SetPlayerInputCommand::SettingAllInputs;
    return (input->settings.given & all) != 0 && (input->settings.values & all) != 0;
}

bool GivesObjectBack(const ScriptCommand* command, const CommandClasses& classes)
{
    return command->vtable == classes.stopTarget || command->vtable == classes.restoreOwn;
}

// The rest of a scene: its bodies that send 207 or give actors back their own object, in the order the scene would reach them,
// its last SetPlayerInput giving the inputs back and its last SwitchCharacter (the character the player has after it)
struct Ending
{
    std::vector<const StateBody*> bodies;
    const ScriptCommand* inputs = nullptr;
    const ScriptCommand* character = nullptr;
};

// A graph's states from one (its bodies' jumps followed), and the graphs their states run but the first's (the level above's,
// walked on its own)
void Walk(GraphState* from, void* object, const CommandClasses& classes, Ending* ending, std::set<const GraphState*>* seen,
          s32 depth)
{
    std::vector<GraphState*> todo{from};
    for (size_t at = 0; at < todo.size(); at++)
    {
        GraphState* state = todo[at];
        if (state == nullptr || !seen->insert(state).second)
        {
            continue;
        }

        if (state != from && state->bits.child != NoScriptId && depth < 3)
        {
            GraphData* child = ChildGraphOf(state, object);
            if (child != nullptr)
            {
                Walk(child->start, object, classes, ending, seen, depth + 1);
            }
        }

        for (StateBody* body = state->bodies; body != nullptr; body = body->next)
        {
            bool wanted = false;
            for (ScriptCommand* command = body->commands; command != nullptr; command = command->next)
            {
                wanted = wanted || SendsSceneOver(command, classes) || GivesObjectBack(command, classes);
                if (GivesInputsBack(command, classes))
                {
                    ending->inputs = command;
                }

                if (command->vtable == classes.switchCharacter)
                {
                    ending->character = command;
                }
            }

            if (wanted)
            {
                ending->bodies.push_back(body);
            }

            if (body->bits.hasJump != 0)
            {
                todo.push_back(body->jump);
            }
        }
    }
}

std::string NameOf(const GameObject* object)
{
    return object != nullptr && object->name.string != nullptr ? object->name.string : "?";
}

// Whether an instance's object node is an object node (ObjectNodeBase: runners, a source node), not the smaller node of a pickup of
// subtype 16 or 17 (instancefactory.cpp's MakeAgentObjectNode)
bool IsObjectNode(InstanceContext* instance)
{
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&instance->nodes, NodeObject));
    const GameObject* object = node != nullptr ? node->object : nullptr;
    return node != nullptr && !(object != nullptr && object->header.type == GameObject::TypePickup && object->header.subtype >= 0x10 &&
                                object->header.subtype < 0x12);
}

// The instances a give-back command of the director's would give back: its designator's, else every linked instance's when
// asked (StopTargetBehaviour); its designator's (RestoreOwnObject, its "own" being the director)
void GivenBack(const ScriptCommand* command, const CommandClasses& classes, ObjectNodeBase* director,
               std::set<const InstanceContext*>* actors)
{
    u32 designator = BehaviourRunner::OriginatorDesignator;
    bool everyLinked = false;
    if (command->vtable == classes.stopTarget)
    {
        const StopTargetRequest& request = static_cast<const StopTargetBehaviourCommand*>(command)->request;
        designator = request.designator;
        everyLinked = request.everyLinked != 0;
    }
    else
    {
        designator = static_cast<const RestoreOwnObjectCommand*>(command)->request.designator;
    }

    if (designator != DesignatesNone)
    {
        auto* instance = CallVirtual<InstanceContext*>(director, director->vtable, ObjectNode::GetDesignatorSlot, designator);
        if (instance != nullptr)
        {
            actors->insert(instance);
        }

        return;
    }

    auto* attachments = static_cast<AttachmentsNode*>(GetGameNode(&director->owner->nodes, NodeAttachments));
    for (u32 index = 0; everyLinked && attachments != nullptr && index < attachments->LinkedCount(); index++)
    {
        if (attachments->linked[index] != nullptr)
        {
            actors->insert(attachments->linked[index]);
        }
    }
}

// A skip: the director's node and instance (compared, never read: they may be gone by the time they're looked for), the starters
// of its object's behaviours, when it skipped, the actors the rest of its scene gives back and the director's behaviours each of
// them ran then (the scene's)
struct Takeover
{
    const GameNode* director;
    const InstanceContext* sender;
    std::set<const ScriptStarter*> starters;
    SkipClock::time_point when;
    std::set<const InstanceContext*> givenBack;
    std::map<const InstanceContext*, std::set<const ScriptStarter*>> sceneRuns;
    // The actors taking the director's object as it skipped (those the skip path takes over after are its own)
    std::set<const InstanceContext*> takenAtSkip;
    // The last 207 sent to a waiting actor (one at most every SendSeconds: the waits check every 0.2 s for one that recent)
    SkipClock::time_point lastSend;
};

// The skips lately: their actors given back as each finishes the scene's behaviour it runs. Forgotten when another cutscene
// starts or a level loads
std::vector<Takeover> g_Pending;

struct ActorSearch
{
    const GameNode* director;
    std::vector<InstanceContext*> actors;
};

void FindActor(InstanceContext* instance, void* user)
{
    auto* search = static_cast<ActorSearch*>(user);
    auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&instance->nodes, NodeObject));
    if (IsObjectNode(instance) && node->sourceNode == search->director)
    {
        search->actors.push_back(instance);
    }
}

// Whether a runner's levels wait for a user message (their state's bodies), the message
bool WaitsFor(const BehaviourRunner* runner, u32 message)
{
    for (u32 at = 0; at < BehaviourRunner::Levels && at <= runner->depth; at++)
    {
        const BehaviourLevel* level = runner->levels[at];
        const GraphState* state = level != nullptr && level->bits.finished == 0 ? level->state : nullptr;
        for (const StateBody* body = state != nullptr ? state->bodies : nullptr; body != nullptr; body = body->next)
        {
            const ScriptCondition* condition = body->condition;
            if (condition != nullptr && condition->bits.id == GotUserMessageCondition && condition->bits.inverted == 0 &&
                condition->Parameter() == message)
            {
                return true;
            }
        }
    }

    return false;
}

// A user message from an instance to another, as SendUserMessage sends it
void Send(u32 message, const InstanceContext* sender, InstanceContext* to)
{
    Reference* argument = sender != nullptr ? AddReference(const_cast<InstanceContext*>(sender)) : nullptr;
    auto* event = GameEvent::Construct(static_cast<GameEvent*>(MemoryAllocate(sizeof(GameEvent))), message, &argument,
                                       ObjectNodeKinds);
    Reference* handle = AddEventReference(event);
    QueueEvent(to, &handle);
}

// A skip's actors still taking the director's object: those the scene gives back given back once they run none of the director's
// object's behaviours (sent 207 whenever one waits for it); past ActorSeconds the scene's behaviours they still run stopped as when
// one finishes, no longer busy (the scene's SetObject would have cleared it). Their names
std::string GiveActorsBack(Takeover& takeover, f32 since, f32 actorSeconds)
{
    InstanceContext* player = g_PlayerInstance != nullptr ? static_cast<InstanceContext*>(g_PlayerInstance->object) : nullptr;
    ActorSearch search = {takeover.director, {}};
    ForEachLiveInstance(FindActor, &search);
    std::string actors;
    for (InstanceContext* actor : search.actors)
    {
        // An actor the skip path took over (the developers' skip scripts run the director's behaviours on actors too) and left
        // doing nothing: given back as the scene played out would leave it
        auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&actor->nodes, NodeObject));
        bool skipPaths = takeover.takenAtSkip.count(actor) == 0;
        if (takeover.givenBack.count(actor) == 0 && !(skipPaths && since >= SkipPathSeconds))
        {
            continue;
        }

        auto scene = takeover.sceneRuns.find(actor);
        bool force = since >= (actor == player ? std::min(PlayerSeconds, actorSeconds) : actorSeconds);
        bool stopped = false;
        bool runs = false;
        for (BehaviourRunner* runner : node->runners)
        {
            // What it runs, or is to run from its next frame (a behaviour started this frame)
            const ScriptStarter* starter = runner != nullptr && runner->receivers != nullptr ? runner->receivers->starter : nullptr;
            const ScriptStarter* next = runner != nullptr ? runner->nextStarter : nullptr;
            starter = next != nullptr && takeover.starters.count(next) != 0 ? next : starter;
            if (starter == nullptr || takeover.starters.count(starter) == 0)
            {
                continue;
            }

            // (The player's own character: any of the director's, as it may have started one more as the skip came)
            bool sceneBehaviour = (scene != takeover.sceneRuns.end() && scene->second.count(starter) != 0) ||
                                  (actor == player && takeover.givenBack.count(actor) != 0);
            if (force && sceneBehaviour)
            {
                runner->nextStarter = nullptr;
                runner->nextOriginator = nullptr;
                if (runner->receivers != nullptr)
                {
                    runner->Stop(1);
                    CallVirtual<void>(node, node->vtable, ObjectNode::RunnerFinishedSlot);
                }

                actor->flags.value &= ~ReferencedObjectFlags::Busy;
                stopped = true;
                continue;
            }

            SkipClock::time_point now = SkipClock::now();
            if (sceneBehaviour && std::chrono::duration<f32>(now - takeover.lastSend).count() >= SendSeconds &&
                WaitsFor(runner, SceneOverMessage))
            {
                Send(SceneOverMessage, takeover.sender, actor);
                takeover.lastSend = now;
            }

            runs = true;
        }

        if (!runs)
        {
            node->sourceNode = nullptr;
            actors += (actors.empty() ? "" : ", ") + NameOf(node->object) + (stopped ? " (its scene stopped)" : "");
        }
    }

    return actors;
}
}

bool IsSkipRule(const StateBody* body)
{
    const ScriptCondition* condition = body != nullptr ? body->condition : nullptr;
    if (condition == nullptr || condition->bits.inverted != 0 || !NativeUi::IsOn(NativeUi::OptionSkipCutscenes))
    {
        return false;
    }

    // (The game's own Triangle rules, 575, are in no state that runs a scene: TWIN_FIXES_TEST=skips)
    return condition->bits.id == SkipCondition && condition->Parameter() != 0;
}

void SceneSkipped(BehaviourRunner* runner, BehaviourLevel* level, TimeClock* clock)
{
    const CommandClasses* classes = Classes();
    auto* director = static_cast<ObjectNodeBase*>(runner->agentNode);
    if (classes == nullptr || director == nullptr || level->bits.index >= runner->depth)
    {
        return;
    }

    void* object = director->sourceNode != nullptr ? SourceObject(director->sourceNode) : director->object;
    GraphData* scene = runner->levels[level->bits.index + 1] != nullptr ? runner->levels[level->bits.index + 1]->graph : nullptr;

    // The rest of the scene: each of its levels from the state it's in, the deepest first (it ends first)
    Ending ending;
    std::set<const GraphState*> seen;
    for (s32 at = runner->depth; at > static_cast<s32>(level->bits.index); at--)
    {
        BehaviourLevel* sceneLevel = runner->levels[at];
        if (sceneLevel != nullptr && sceneLevel->state != nullptr)
        {
            Walk(sceneLevel->state, object, *classes, &ending, &seen, 0);
        }
    }

    // Its bodies in turn: the designators they set, its 207s sent, the actors its give-backs name kept for later
    Takeover takeover = {director, director->owner, {}, SkipClock::now(), {}, {}, {}, {}};
    BehaviourLevel* deepest = runner->levels[runner->depth];
    u32 sends = 0;
    for (const StateBody* body : ending.bodies)
    {
        for (ScriptCommand* command = body->commands; command != nullptr; command = command->next)
        {
            if (classes->designators.count(command->vtable) != 0 || SendsSceneOver(command, *classes))
            {
                sends += SendsSceneOver(command, *classes) ? 1 : 0;
                CallVirtual<void>(command, command->vtable, ScriptCommand::ExecuteSlot, clock, runner, deepest);
            }
            else if (GivesObjectBack(command, *classes))
            {
                GivenBack(command, *classes, director, &takeover.givenBack);
            }
        }
    }

    // Its last SwitchCharacter (the character it gives the player: with its controls), then the inputs it gives back
    for (const ScriptCommand* last : {ending.character, ending.inputs})
    {
        if (last != nullptr)
        {
            auto* command = const_cast<ScriptCommand*>(last);
            CallVirtual<void>(command, command->vtable, ScriptCommand::ExecuteSlot, clock, runner, deepest);
        }
    }

    // The director's behaviours, and those its actors run now (the scene's)
    auto* directorObject = static_cast<GameObject*>(object);
    ResourceTable* scripts = g_ScriptTable;
    for (u32 slot = 0; directorObject != nullptr && slot < directorObject->behaviours.count; slot++)
    {
        u16 id = directorObject->behaviours.items[slot];
        if (id != NoScriptId && (id & ResourceIndexMask) < scripts->bits.capacity)
        {
            takeover.starters.insert(static_cast<const ScriptStarter*>(scripts->items[id & ResourceIndexMask]));
        }
    }

    ActorSearch search = {director, {}};
    ForEachLiveInstance(FindActor, &search);
    std::string kept;
    std::string left;
    for (InstanceContext* actor : search.actors)
    {
        auto* node = static_cast<ObjectNodeBase*>(GetGameNode(&actor->nodes, NodeObject));
        takeover.takenAtSkip.insert(actor);
        for (BehaviourRunner* actorRunner : node->runners)
        {
            if (actorRunner != nullptr && actorRunner->receivers != nullptr &&
                takeover.starters.count(actorRunner->receivers->starter) != 0)
            {
                takeover.sceneRuns[actor].insert(actorRunner->receivers->starter);
            }

            if (actorRunner != nullptr && takeover.starters.count(actorRunner->nextStarter) != 0)
            {
                takeover.sceneRuns[actor].insert(actorRunner->nextStarter);
            }
        }

        if (takeover.givenBack.count(actor) != 0)
        {
            kept += (kept.empty() ? "" : ", ") + NameOf(node->object);
        }
        else
        {
            left += (left.empty() ? "" : ", ") + NameOf(node->object);
        }
    }

    g_Pending.push_back(std::move(takeover));
    Native::Log("skip: %s's scene %.*s skipped: %u \"over\" messages sent, %s%s; its actors given back as they finish: %s; "
                "left as it leaves them: %s",
                NameOf(directorObject).c_str(), scene != nullptr ? static_cast<int>(scene->name.length) : 1,
                scene != nullptr && scene->name.string != nullptr ? scene->name.string : "?", sends,
                ending.inputs != nullptr ? "the inputs given back" : "no inputs to give back",
                ending.character != nullptr ? ", the character switched as it ends" : "", kept.empty() ? "none" : kept.c_str(),
                left.empty() ? "none" : left.c_str());
}

void GiveBackFrame()
{
    // Forgotten when another cutscene starts (the scene again after a death, say: its actors are its own then) or a level loads
    static u32 lastState = GameController::NoState;
    GameController* controller = G_GameController;
    u32 state = controller != nullptr ? controller->State() : GameController::NoState;
    bool another = (state == GameController::StateWatching && lastState != GameController::StateWatching) ||
                   state == GameController::StateLoadingLevel || state == GameController::StateGameOver;
    lastState = state;
    if (g_Pending.empty())
    {
        return;
    }

    if (another)
    {
        g_Pending.clear();
        return;
    }

    // $TWIN_SKIP_ACTOR_SECONDS (testing): another time before the scene's behaviours are stopped
    static const char* setting = std::getenv("TWIN_SKIP_ACTOR_SECONDS");
    static const f32 actorSeconds = setting != nullptr ? static_cast<f32>(std::atof(setting)) : ActorSeconds;
    SkipClock::time_point now = SkipClock::now();
    for (Takeover& takeover : g_Pending)
    {
        f32 since = std::chrono::duration<f32>(now - takeover.when).count();
        std::string actors = GiveActorsBack(takeover, since, actorSeconds);
        if (!actors.empty())
        {
            Native::Log("skip: %.1f s after the skip, actors given their own object back: %s", since, actors.c_str());
        }
    }

    g_Pending.erase(std::remove_if(g_Pending.begin(), g_Pending.end(),
                                   [now](const Takeover& takeover)
                                   { return std::chrono::duration<f32>(now - takeover.when).count() >= PendingSeconds; }),
                    g_Pending.end());
}
}
