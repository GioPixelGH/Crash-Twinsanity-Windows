// The physics test (testing, $TWIN_PHYSICS_TEST): the player's moves measured by the game's own clock, so that runs at different
// frame rates ($TWIN_FIXED_TIME's frames: "144,144,60" a 144 Hz screen's with a slow frame now and then) can be compared with 60 Hz's.
//
// jumps (with --quick, or $TWIN_START_CHUNK): once play has gone on 3 s, where the player stands and the way the camera looks are
// kept ($TWIN_PHYSICS_WAY=DEGREES: that far round from it), and each of the first five moves below ($TWIN_PHYSICS_MOVES=013: those,
// by their numbers from 0; tied characters have no slide; 5 and 6 only so) is made from there: the player put back at
// that place, a second and a half standing still, then the move's input by the game clock's time, the stick pushed the kept way in
// the world whatever the camera does (every run takes the same path). Each jump is measured from the last place on the ground to the
// landing: "physics test: MOVE: S s in the air, D along the ground, H high, landed U up", then "physics test: done" and exit 0; a
// move that never leaves the ground or never lands fails it (exit 1).
//
// watch, or step[:SECONDS] (the stick pushed $TWIN_PHYSICS_WAY's way from 3 s that long, 0.3 by default, then let go): every time the
// player leaves the ground from 3 s on, logged as it lands ("physics test: off the ground S s, H high, D along at T s").
// $TWIN_PHYSICS_ON=NAME puts the player 2 units above the nearest instance whose object's name holds NAME 2 s into play (a lift),
// $TWIN_PHYSICS_TIED Cortex tied to it as play starts (the progress's pairing, as the levels' scripts set it), $TWIN_PHYSICS_SECONDS
// ends the watch then ("physics test: watched", exit 0). With the level sweep to start a chunk ($TWIN_SWEEP_LEVEL, and
// $TWIN_SWEEP_IDLE: no scripted input of its own).
//
// $TWIN_PHYSICS_TRACE=FILE: every frame's time and seconds, the move, the player's place, its velocity, whether it's on the ground,
// the velocity its movement node gives, its height offset and its height state
#include "ui/bindings.h"

#include "native.h"

#include "game/agentparts.h"
#include "game/characters.h"
#include "game/chunkdata.h"
#include "game/clock.h"
#include "game/gamecontroller.h"
#include "game/instances.h"
#include "game/place.h"
#include "game/player.h"
#include "game/progress.h"
#include "game/reference.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace NativeUi
{
namespace
{
// A move's input: a button held from a time to another (seconds since the move's input starts)
struct Press
{
    u32 buttons;
    f32 from;
    f32 to;
};

struct Move
{
    const char* name;
    // The stick pushed the kept way from 0 to this, and the move's input's length
    f32 forward;
    f32 seconds;
    Press presses[3];
};

// The standing jump with cross held through it (the highest), the running jump, the knee slide's jump (circle while running, cross
// in the slide), the jump and spin in quick succession and the double jump; then, only when asked, the double jump option's (THE
// LIST 53): a double jump pressed in the fall (weaker the later but at its "any moment") and one after a slide jump (none at 0)
constexpr Move Moves[] = {
    {"standing jump", 0.0f, 1.0f, {{PadBitCross, 0.0f, 0.9f}}},
    {"running jump", 1.6f, 1.6f, {{PadBitCross, 0.8f, 1.6f}}},
    {"slide jump", 1.3f, 1.3f, {{PadBitCircle, 0.4f, 0.5f}, {PadBitCross, 0.65f, 1.1f}}},
    {"jump and spin", 1.2f, 1.2f, {{PadBitCross, 0.6f, 1.2f}, {PadBitSquare, 0.7f, 0.8f}}},
    {"double jump", 1.2f, 1.4f, {{PadBitCross, 0.6f, 0.75f}, {PadBitCross, 0.95f, 1.4f}}},
    {"falling double jump", 0.0f, 1.0f, {{PadBitCross, 0.0f, 0.45f}, {PadBitCross, 0.55f, 1.0f}}},
    {"slide and double jump", 1.3f, 1.6f, {{PadBitCircle, 0.4f, 0.5f}, {PadBitCross, 0.65f, 1.1f}, {PadBitCross, 1.2f, 1.6f}}},
};
constexpr u32 MoveCount = sizeof(Moves) / sizeof(Moves[0]);
constexpr u32 DefaultMoves = 5;
// Play before the first move, standing still before each move's input, and the most a move may take past its input
constexpr f32 PlaySeconds = 3.0f;
constexpr f32 SettleSeconds = 1.5f;
constexpr f32 LandSeconds = 4.0f;
// Watching: the player put on something then, and watched (and stepped) from then
constexpr f32 PutSeconds = 2.0f;
constexpr f32 WatchSeconds = 3.0f;
constexpr f32 PutHeight = 2.0f;
constexpr f32 DefaultStepSeconds = 0.3f;
constexpr f32 Pi = 3.14159265f;

enum class Mode
{
    Off,
    Jumps,
    Watch,
};

bool g_Read = false;
Mode g_Mode = Mode::Off;
// The settings: the way round from the camera's (radians), and watching the stick's push first, what the player is put on (none:
// nullptr), Cortex tied and when the watch ends (0: never)
f32 g_WayAngle = 0.0f;
// The moves made, a bit each (the first five; $TWIN_PHYSICS_MOVES=013: their numbers, from 0)
u32 g_MovesMade = (1u << DefaultMoves) - 1;
f32 g_StepSeconds = 0.0f;
const char* g_On = nullptr;
bool g_Tied = false;
f32 g_WatchEnd = 0.0f;
std::FILE* g_Trace = nullptr;
// The game clock's time while playing (seconds), whether the player was put on something, the move under way and its start (its
// input SettleSeconds later)
double g_Time = 0.0;
bool g_Put = false;
u32 g_Move = 0;
bool g_Moving = false;
double g_StepStart = 0.0;
// Where the moves start and the way they go (flat, unit long)
Vector4 g_Start = {};
f32 g_WayX = 0.0f;
f32 g_WayZ = 1.0f;
// The jump measured: whether in the air, the take off's place and time, the highest, and the last frame's place and time
bool g_HaveGround = false;
bool g_WasOnGround = true;
bool g_Airborne = false;
bool g_Measured = false;
Vector4 g_Last = {};
double g_LastTime = 0.0;
Vector4 g_TakeOff = {};
double g_TakeOffTime = 0.0;
f32 g_Highest = 0.0f;

f32 FloatSetting(const char* name, f32 none)
{
    const char* value = std::getenv(name);
    return value != nullptr ? static_cast<f32>(std::atof(value)) : none;
}

void ReadSettings()
{
    if (g_Read)
    {
        return;
    }

    g_Read = true;
    const char* test = std::getenv("TWIN_PHYSICS_TEST");
    if (test == nullptr)
    {
        return;
    }

    if (std::strcmp(test, "jumps") == 0)
    {
        g_Mode = Mode::Jumps;
    }
    else if (std::strcmp(test, "watch") == 0)
    {
        g_Mode = Mode::Watch;
    }
    else if (std::strncmp(test, "step", 4) == 0)
    {
        g_Mode = Mode::Watch;
        g_StepSeconds = test[4] == ':' ? static_cast<f32>(std::atof(test + 5)) : DefaultStepSeconds;
    }

    g_WayAngle = FloatSetting("TWIN_PHYSICS_WAY", 0.0f) * Pi / 180.0f;
    if (const char* moves = std::getenv("TWIN_PHYSICS_MOVES"))
    {
        g_MovesMade = 0;
        for (const char* at = moves; *at != '\0'; at++)
        {
            if (*at >= '0' && *at < static_cast<char>('0' + MoveCount))
            {
                g_MovesMade |= 1u << (*at - '0');
            }
        }
    }
    g_On = std::getenv("TWIN_PHYSICS_ON");
    g_Tied = std::getenv("TWIN_PHYSICS_TIED") != nullptr;
    g_WatchEnd = FloatSetting("TWIN_PHYSICS_SECONDS", 0.0f);
    if (const char* trace = std::getenv("TWIN_PHYSICS_TRACE"); trace != nullptr && g_Mode != Mode::Off)
    {
        g_Trace = std::fopen(trace, "w");
        if (g_Trace != nullptr)
        {
            std::fprintf(g_Trace, "time,seconds,move,x,y,z,vx,vy,vz,ground,nodex,nodey,nodez,offset,heightstate\n");
        }
    }
}

[[noreturn]] void Finish(int status, const char* why)
{
    Native::Log("physics test: %s", why);
    if (g_Trace != nullptr)
    {
        std::fclose(g_Trace);
    }

    std::exit(status);
}

// The player's character while it plays
PlayerCharacter* Character()
{
    GameController* controller = G_GameController;
    if (controller == nullptr || controller->State() != GameController::StatePlaying || g_PlayerCharacter == nullptr ||
        g_PlayerPart == nullptr || g_PlayerInstance == nullptr || g_PlayerInstance->object == nullptr)
    {
        return nullptr;
    }

    auto* instance = static_cast<InstanceContext*>(g_PlayerInstance->object);
    return InstanceLive(instance) && instance->place != nullptr && instance->chunk != nullptr ? g_PlayerCharacter : nullptr;
}

Vector4 PlaceOf(PlayerCharacter* character)
{
    const ObjectPlace* place = character->instance->place;
    if ((place->bits.value & PlaceBits::MatrixMoved) != 0)
    {
        return {place->matrix.m[3][0], place->matrix.m[3][1], place->matrix.m[3][2], 1.0f};
    }

    return place->position;
}

f32 Along(const Vector4& from, const Vector4& to)
{
    f32 x = to.x - from.x;
    f32 z = to.z - from.z;
    return std::sqrt(x * x + z * z);
}

// Leaving the ground and landing: a jump's measure (from the last place on the ground)
void Measure(const Vector4& at)
{
    bool onGround = g_PlayerPart->flags.onGround != 0;
    if (!g_HaveGround)
    {
        g_HaveGround = true;
        g_WasOnGround = onGround;
        g_Last = at;
        g_LastTime = g_Time;
        return;
    }

    if (g_WasOnGround && !onGround && !g_Airborne)
    {
        g_Airborne = true;
        g_TakeOff = g_Last;
        g_TakeOffTime = g_LastTime;
        g_Highest = 0.0f;
    }

    if (g_Airborne)
    {
        g_Highest = std::fmax(g_Highest, at.y - g_TakeOff.y);
    }

    if (g_Airborne && onGround)
    {
        g_Airborne = false;
        f32 air = static_cast<f32>(g_Time - g_TakeOffTime);
        if (g_Mode == Mode::Jumps && g_Moving && !g_Measured && g_Time - g_StepStart > SettleSeconds)
        {
            g_Measured = true;
            Native::Log("physics test: %s: %.2f s in the air, %.2f along the ground, %.2f high, landed %.2f up", Moves[g_Move].name,
                        air, Along(g_TakeOff, at), g_Highest, at.y - g_TakeOff.y);
        }
        else if (g_Mode == Mode::Watch && g_TakeOffTime >= WatchSeconds)
        {
            Native::Log("physics test: off the ground %.2f s, %.2f high, %.2f along at %.1f s", air, g_Highest,
                        Along(g_TakeOff, at), g_Time);
        }
    }

    g_WasOnGround = onGround;
    g_Last = at;
    g_LastTime = g_Time;
}

// The first of the moves made from one on (MoveCount: none)
u32 NextWanted(u32 from)
{
    while (from < MoveCount && (g_MovesMade & (1u << from)) == 0)
    {
        from++;
    }

    return from;
}

// The next move: the player put back where the moves start
void NextMove(PlayerCharacter* character)
{
    if (g_Moving && !g_Measured)
    {
        char why[96];
        std::snprintf(why, sizeof(why), "%s: %s", Moves[g_Move].name, g_Airborne ? "never landed" : "never left the ground");
        Finish(1, why);
    }

    if (g_Moving)
    {
        g_Move = NextWanted(g_Move + 1);
        if (g_Move >= MoveCount)
        {
            Finish(0, "done");
        }
    }
    else
    {
        g_Move = NextWanted(0);
        // The first: the place and the way the camera looks (flat, turned the setting's angle about the up axis) kept
        g_Start = PlaceOf(character);
        const Matrix4x4& view = character->instance->chunk->matrix;
        f32 length = std::sqrt(view.m[2][0] * view.m[2][0] + view.m[2][2] * view.m[2][2]);
        f32 x = length > 0.0f ? view.m[2][0] / length : 0.0f;
        f32 z = length > 0.0f ? view.m[2][2] / length : 1.0f;
        g_WayX = x * std::cos(g_WayAngle) + z * std::sin(g_WayAngle);
        g_WayZ = z * std::cos(g_WayAngle) - x * std::sin(g_WayAngle);
        Native::Log("physics test: the moves from (%.2f, %.2f, %.2f) toward (%.2f, %.2f)", g_Start.x, g_Start.y, g_Start.z,
                    g_WayX, g_WayZ);
    }

    InstanceContext* instance = character->instance;
    Vector4 to = g_Start;
    to.w = 1.0f;
    if (instance->place->MoveTo(&to))
    {
        QueueObject(instance);
    }

    g_Moving = true;
    g_Measured = false;
    g_StepStart = g_Time;
}

// Watching: the player put on what the setting names, and the watch's end
void WatchFrame(PlayerCharacter* character)
{
    if (g_On != nullptr && !g_Put && g_Time >= PutSeconds)
    {
        g_Put = true;
        if (!PutPlayerAbove(character->instance, g_On, PutHeight))
        {
            Finish(1, "nothing to put the player on");
        }

        Native::Log("physics test: the player %s", character->Linked() != 0 ? "tied to the second" : "alone");
    }

    if (g_WatchEnd > 0.0f && g_Time >= g_WatchEnd && !g_Airborne)
    {
        Finish(0, "watched");
    }
}
}

void PhysicsTestFrame()
{
    ReadSettings();
    if (g_Mode == Mode::Off)
    {
        return;
    }

    GameController* controller = G_GameController;
    if (g_Tied && controller != nullptr && controller->NextState() == GameController::StateStartingPlay)
    {
        controller->progress.play.pairing = PairingTied;
        controller->progress.play.second = CharacterCortex;
    }

    PlayerCharacter* character = Character();
    if (character == nullptr)
    {
        g_HaveGround = false;
        return;
    }

    f32 seconds = static_cast<f32>(g_GlobalClock.advance) * g_SecondsPerClockUnit;
    g_Time += seconds;
    Vector4 at = PlaceOf(character);
    Measure(at);
    if (g_Trace != nullptr)
    {
        const Vector4& velocity = character->velocity;
        // The velocity its movement node gives (MovementVelocity's, read without capturing)
        Vector4 node = {};
        auto* movement = static_cast<MovementNode*>(GetGameNode(&character->instance->nodes, NodeMovement));
        if (movement != nullptr && movement->bits.captured != 0 && movement->seconds > 0.0f)
        {
            node.x = (movement->matrix.m[3][0] - movement->previousMatrix.m[3][0]) / movement->seconds;
            node.y = (movement->matrix.m[3][1] - movement->previousMatrix.m[3][1]) / movement->seconds;
            node.z = (movement->matrix.m[3][2] - movement->previousMatrix.m[3][2]) / movement->seconds;
        }

        std::fprintf(g_Trace, "%.4f,%.5f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%.3f,%.3f,%.3f,%.3f,%d\n", g_Time, seconds,
                     g_Moving ? static_cast<s32>(g_Move) : -1, at.x, at.y, at.z, velocity.x, velocity.y, velocity.z,
                     g_PlayerPart->flags.onGround, node.x, node.y, node.z, character->heightOffset, character->heightState);
    }

    if (g_Mode == Mode::Watch)
    {
        WatchFrame(character);
        return;
    }

    if (!g_Moving ? g_Time >= PlaySeconds
                  : g_Measured && !g_Airborne && g_Time - g_StepStart >= SettleSeconds + Moves[g_Move].seconds + 0.5)
    {
        NextMove(character);
    }
    else if (g_Moving && g_Time - g_StepStart > SettleSeconds + Moves[g_Move].seconds + LandSeconds)
    {
        NextMove(character);
    }
}

void PhysicsTestInput(BoundPad* pad)
{
    PlayerCharacter* character = Character();
    if (g_Mode == Mode::Watch)
    {
        // The step: the stick pushed the setting's way round from the camera's
        if (character != nullptr && g_Time >= WatchSeconds && g_Time < WatchSeconds + g_StepSeconds)
        {
            pad->leftX = static_cast<u8>(std::lround(128.0f + std::sin(g_WayAngle) * 127.0f));
            pad->leftY = static_cast<u8>(std::lround(128.0f - std::cos(g_WayAngle) * 127.0f));
        }

        return;
    }

    if (g_Mode != Mode::Jumps || !g_Moving || character == nullptr)
    {
        return;
    }

    const Move& move = Moves[g_Move];
    f32 t = static_cast<f32>(g_Time - g_StepStart) - SettleSeconds;
    if (t < 0.0f)
    {
        return;
    }

    if (t < move.forward)
    {
        // The stick that the camera turns the kept way (ControlsHandler::StickInWorld: the stick through the view's rows, made
        // flat): x times its first row plus z times its third, solved for the way
        const Matrix4x4& view = character->instance->chunk->matrix;
        f32 a = view.m[0][0], b = view.m[2][0], c = view.m[0][2], d = view.m[2][2];
        f32 determinant = a * d - b * c;
        if (std::fabs(determinant) > 1e-6f)
        {
            f32 x = (d * g_WayX - b * g_WayZ) / determinant;
            f32 z = (a * g_WayZ - c * g_WayX) / determinant;
            // Pushed all the way, out to the DualShock 2's square
            f32 most = std::fmax(std::fabs(x), std::fabs(z));
            x /= most;
            z /= most;
            pad->leftX = static_cast<u8>(std::lround(128.0f + x * 127.0f));
            pad->leftY = static_cast<u8>(std::lround(128.0f - z * 127.0f));
        }
    }

    for (const Press& press : move.presses)
    {
        if (press.buttons != 0 && press.from <= t && t < press.to)
        {
            pad->buttons |= press.buttons;
        }
    }
}
}
