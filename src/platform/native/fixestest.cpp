// The bug fixes' checks in the running game ($TWIN_FIXES_TEST, with --headless --quick): each one sets up the state a fix is
// about on the playing character, runs the game's own code on it and logs what came out, then the game exits (0 when the result is
// the one expected for the bug fixes option as it is: fixed with it on, retail's with it off). $TWIN_RETAIL_FIXES sets the option
// for the run.
//   recipes   the level recipes' self-test (SelfTestRecipes)
//   skips     the skippable scenes and what their skips leave undone (SurveySkips)
//   aku       an invincible Crash hit by an explosion (TNT's 100 damage), then by a generic instant death
//   reset     a Crash blinking after a hit (hidden at that moment) reset as a cutscene does
//   prompt    the cutscene skip's prompt (its BottomTextDisplay, the AgentLab texts' line 0x18) shown in a cutscene's letterbox
//             for a few seconds (with --snapshots, to look at)
//   units     the retail bugs' fixes that can be called on their own: WalkController::AskTurn turning 90 degrees either way, and
//             FollowCameraRig::RollerbrawlYaw past the ball's top speed
#include "fixes.h"

#include "native.h"

#include "game/agentparts.h"
#include "game/agentlab.h"
#include "game/agents.h"
#include "game/commands.h"
#include "game/characters.h"
#include "game/clock.h"
#include "game/followcamera.h"
#include "game/gamecontroller.h"
#include "game/instances.h"
#include "game/layout.h"
#include "game/player.h"
#include "platform/system.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace NativeFixes
{
namespace
{
constexpr u32 SettleFrames = 90;

CharacterAgent* PlayerAgent()
{
    if (g_PlayerInstance == nullptr || g_PlayerInstance->object == nullptr)
    {
        return nullptr;
    }

    auto* instance = static_cast<InstanceContext*>(g_PlayerInstance->object);
    auto* node = static_cast<AgentNode*>(GetGameNode(&instance->nodes, NodeCharacter));
    return node != nullptr ? static_cast<CharacterAgent*>(node->agent) : nullptr;
}

int TestAku(CharacterAgent* agent)
{
    auto* part = static_cast<CharacterPart*>(agent->part);
    auto* instance = agent->instance;
    // Aku Aku's three masks: the part invulnerable (StartInvincibility kind 1)
    part->bits.invulnerable = 1;
    u32 before = part->flags.hitPoints;
    ContactMessage explosion;
    ContactMessage::Construct(&explosion);
    explosion.hitKinds = HitExplosion;
    explosion.damage = 100;
    agent->Contact(&explosion, instance, 0);
    u32 afterExplosion = part->flags.hitPoints;
    bool diedToExplosion = agent->state.dead != 0;
    // A generic instant death (kind 9, as the fall-through and crush deaths) still goes through
    ContactMessage generic;
    ContactMessage::Construct(&generic);
    generic.hitKinds = 1u << 9;
    generic.damage = 100;
    part->bits.invulnerable = 1;
    agent->Contact(&generic, instance, 0);
    u32 afterGeneric = part->flags.hitPoints;
    bool fixes = RetailFixes();
    bool ok = fixes ? (afterExplosion == before && !diedToExplosion && afterGeneric == 0)
                    : (afterExplosion == 0 && diedToExplosion && afterGeneric == 0);
    Native::Log("fixes test aku: %s (bug fixes %s; hit points %u, after TNT's explosion while invincible %u%s, after an instant "
                "death %u)",
                ok ? "passed" : "FAILED", fixes ? "on" : "off", before, afterExplosion, diedToExplosion ? " (died)" : "",
                afterGeneric);
    return ok ? 0 : 1;
}

int TestReset(CharacterAgent* agent)
{
    auto* part = static_cast<CharacterPart*>(agent->part);
    auto* instance = agent->instance;
    // Hurt: StartHurtInvincibility's state, then the blink's hidden fifth (HurtFrame running: mode hurt, previous invincible)
    part->bits.invulnerable = 1;
    agent->state.previousMode = CharacterAgent::ModeInvincible;
    agent->state.mode = CharacterAgent::ModeHurt;
    instance->flags.visible = 0;
    agent->Reset();
    u32 visible = instance->flags.visible;
    u32 invulnerable = part->bits.invulnerable;
    // Requested but not started yet (the hit's frame)
    part->bits.invulnerable = 1;
    agent->state.mode = CharacterAgent::ModeInvincible;
    instance->flags.visible = 0;
    agent->Reset();
    u32 visibleRequested = instance->flags.visible;
    u32 invulnerableRequested = part->bits.invulnerable;
    bool fixes = RetailFixes();
    bool ok = fixes ? (visible == 1 && invulnerable == 0 && visibleRequested == 1 && invulnerableRequested == 0)
                    : (visible == 0 && invulnerable == 1 && visibleRequested == 0 && invulnerableRequested == 1);
    instance->flags.visible = 1;
    part->bits.invulnerable = 0;
    Native::Log("fixes test reset: %s (bug fixes %s; blinking: visible %u, invincible %u after the reset; about to blink: visible "
                "%u, invincible %u)",
                ok ? "passed" : "FAILED", fixes ? "on" : "off", visible, invulnerable, visibleRequested, invulnerableRequested);
    return ok ? 0 : 1;
}

// An object of the game's laid over cleared memory (the two calls only read and write their own members)
template <typename T>
T* Cleared(std::vector<u8>& memory)
{
    memory.assign(sizeof(T), 0);
    return reinterpret_cast<T*>(memory.data());
}

int TestUnits()
{
    bool fixes = RetailFixes();
    // AskTurn: the stick a half turn's half (90 degrees) to either side, a frame of 1/50 s, a turn rate that doesn't clamp
    std::vector<u8> walkMemory;
    auto* walk = Cleared<WalkController>(walkMemory);
    TimeClock clock = {};
    clock.advance = static_cast<u32>(g_ClockUnitsPerSecond / 50.0f);
    s32 rate = 0x7FFFFFFF / 4;
    walk->AskTurn(0.5f, &clock, &rate);
    s32 left = walk->turn;
    walk->AskTurn(-0.5f, &clock, &rate);
    s32 right = walk->turn;
    bool symmetric = left == -right;
    // RollerbrawlYaw: at the top speed (50) and past it (100)
    std::vector<u8> rigMemory;
    auto* rig = Cleared<FollowCameraRig>(rigMemory);
    rig->RollerbrawlYaw(nullptr, 50.0f);
    s32 atTop = rig->ownPositioner.yaw.speed;
    rig->RollerbrawlYaw(nullptr, 100.0f);
    s32 pastTop = rig->ownPositioner.yaw.speed;
    bool ok = fixes ? (symmetric && pastTop == atTop) : (!symmetric && pastTop < atTop);
    Native::Log("fixes test units: %s (bug fixes %s; AskTurn 90 degrees: %d one way, %d the other; RollerbrawlYaw's yaw speed at "
                "50 u/s %d, at 100 u/s %d)",
                ok ? "passed" : "FAILED", fixes ? "on" : "off", left, right, atTop, pastTop);
    return ok ? 0 : 1;
}
}

void Frame()
{
    static const char* test = std::getenv("TWIN_FIXES_TEST");
    static u32 frames = 0;
    static u32 promptFrames = 0;
    GameController* controller = G_GameController;
    // (Once the prompt test started its cutscene, the game's state is the cutscene's)
    if (test == nullptr || controller == nullptr || (controller->State() != GameController::StatePlaying && promptFrames == 0))
    {
        return;
    }

    if (++frames < SettleFrames)
    {
        return;
    }

    if (std::strcmp(test, "prompt") == 0)
    {
        // A cutscene's letterbox, then the prompt as the recipes' command shows it; the game goes on for 40 frames
        constexpr u32 PromptLine = 0x18;
        constexpr u32 ShownFrames = 40;
        if (promptFrames++ == 0)
        {
            controller->StartCutscene(static_cast<s32>(0.5f * g_ClockUnitsPerSecond));
            auto* command = static_cast<DisplayBottomTextCommand*>(g_ObjectBuilder->Build(603, ObjectBuilder::CommandKind));
            command->text = PromptLine;
            command->x = 0.5f;
            command->y = 0.92f;
            command->red = 1.0f;
            command->green = 1.0f;
            command->blue = 1.0f;
            command->seconds = 0.0f;
            command->Execute(nullptr, nullptr, nullptr);
            Native::Log("fixes test prompt: shown");
        }

        if (promptFrames < ShownFrames)
        {
            return;
        }

        Platform::System::Exit(0);
    }

    int status = 1;
    CharacterAgent* agent = PlayerAgent();
    if (std::strcmp(test, "recipes") == 0)
    {
        status = SelfTestRecipes();
    }
    else if (std::strcmp(test, "skips") == 0)
    {
        status = SurveySkips();
    }
    else if (std::strcmp(test, "units") == 0)
    {
        status = TestUnits();
    }
    else if (agent == nullptr)
    {
        Native::Log("fixes test: no playing character");
    }
    else if (std::strcmp(test, "aku") == 0)
    {
        status = TestAku(agent);
    }
    else if (std::strcmp(test, "reset") == 0)
    {
        status = TestReset(agent);
    }
    else
    {
        Native::Log("fixes test: no test \"%s\"", test);
    }

    Platform::System::Exit(status);
}
}
