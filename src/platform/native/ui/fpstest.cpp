// The FPS counter's test (testing, $TWIN_FPS_TEST=1 or a folder, with --quick and a window: the counter counts the pictures the
// window shows, and a headless run shows none): from the beach, a few seconds' play, then the status button held (the HUD shows),
// then the pause menu, the counter checked after each. With the show FPS option on, what it shows has to be the game's own frames a
// second (its frames counted here over the same step, within 8% or 2), and at 50 or 60 Hz the paced rate too (within 5% or 2: the
// game holds it); faster, the rate the computer reaches is logged. With the option off, nothing shown. With a folder the window is
// saved there as it's shown, whatever the checks found (fps_play.png, the HUD and the counter; fps_pause.png, the pause menu's
// hints and the counter). "fps test: pass" and exit 0, else what failed and 1; a step taking over 30 s fails it
#include "ui/bindings.h"

#include "graphics/presenter.h"
#include "native.h"

#include "game/gamecontroller.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace NativeUi
{
namespace
{
using Clock = std::chrono::steady_clock;

enum class Phase
{
    Playing,
    Status,
    Pausing,
    Paused,
    Done,
};

bool g_Read = false;
bool g_Active = false;
std::string g_Folder;
Phase g_Phase = Phase::Playing;
bool g_Playing = false;
Clock::time_point g_StepStart;
// The game's frames since the step began (this test's frame is one a game frame)
u32 g_StepFrames = 0;
// The first check that failed (none: all passed so far)
std::string g_Failed;

f32 Since(Clock::time_point start)
{
    return std::chrono::duration<f32>(Clock::now() - start).count();
}

bool Active()
{
    if (!g_Read)
    {
        g_Read = true;
        const char* test = std::getenv("TWIN_FPS_TEST");
        g_Active = test != nullptr && test[0] != '\0';
        if (g_Active && std::string(test) != "1")
        {
            g_Folder = test;
        }

        g_StepStart = Clock::now();
    }

    return g_Active;
}

[[noreturn]] void Finish(int status, const char* why)
{
    Native::Log("fps test: %s", why);
    std::exit(status);
}

void Next(Phase phase)
{
    g_Phase = phase;
    g_StepStart = Clock::now();
    g_StepFrames = 0;
}

void Shoot(const char* name)
{
    if (!g_Folder.empty())
    {
        NativeGraphicsShootWindow((g_Folder + "/" + name).c_str());
    }
}

bool Near(f32 value, f32 target, f32 share)
{
    return std::fabs(value - target) <= std::max(2.0f, target * share);
}

// The counter as the option has it, against the game's frames a second over the step so far: logged, and the first failure kept
void CheckCounter(const char* where)
{
    s32 shown = FpsCounterValue();
    s32 paced = FrameRate();
    f32 game = static_cast<f32>(g_StepFrames) / Since(g_StepStart);
    char what[192];
    bool right;
    if (!IsOn(OptionShowFps))
    {
        std::snprintf(what, sizeof(what), "%s, the option off: the counter %s", where, shown < 0 ? "not shown" : "SHOWN");
        right = shown < 0;
    }
    else
    {
        bool holds = paced == 0 || paced > 60 || Near(game, static_cast<f32>(paced), 0.05f);
        right = shown >= 0 && Near(static_cast<f32>(shown), game, 0.08f) && holds;
        std::snprintf(what, sizeof(what), "%s, the option on: the counter %d, the game's frames %.1f a second (paced at %s)%s", where,
                      shown, game, paced == 0 ? "as fast as they come" : std::to_string(paced).c_str(),
                      right ? "" : holds ? ": NOT the game's" : ": the game doesn't hold its rate");
    }

    Native::Log("fps test: %s", what);
    if (!right && g_Failed.empty())
    {
        g_Failed = what;
    }
}
}

void FpsTestFrame()
{
    if (!Active())
    {
        return;
    }

    if (Native::GetSettings().headless)
    {
        Finish(1, "it needs a window (the counter counts the pictures shown)");
    }

    GameController* controller = G_GameController;
    if (controller == nullptr)
    {
        return;
    }

    g_StepFrames++;
    u32 state = controller->State();
    if (Since(g_StepStart) > 30.0f)
    {
        char why[64];
        std::snprintf(why, sizeof(why), "stuck (state %u, step %u)", state, static_cast<u32>(g_Phase));
        Finish(1, why);
    }

    switch (g_Phase)
    {
    case Phase::Playing:
        if (state == GameController::StatePlaying)
        {
            if (!g_Playing)
            {
                g_Playing = true;
                g_StepStart = Clock::now();
            }
            else if (Since(g_StepStart) > 4.0f)
            {
                Next(Phase::Status);
            }
        }

        break;
    case Phase::Status:
        // Once the HUD has slid in
        if (Since(g_StepStart) > 2.0f)
        {
            CheckCounter("playing");
            Shoot("fps_play.png");
            Next(Phase::Pausing);
        }

        break;
    case Phase::Pausing:
        if (state == GameController::StatePaused && Since(g_StepStart) > 0.5f)
        {
            Next(Phase::Paused);
        }

        break;
    case Phase::Paused:
        // Once the menu has faded in
        if (Since(g_StepStart) > 2.0f)
        {
            CheckCounter("paused");
            Shoot("fps_pause.png");
            Next(Phase::Done);
        }

        break;
    case Phase::Done:
        // The shot written at the next picture shown
        if (Since(g_StepStart) > 1.0f)
        {
            Finish(g_Failed.empty() ? 0 : 1, g_Failed.empty() ? "pass" : ("FAILED " + g_Failed).c_str());
        }

        break;
    }
}

void FpsTestInput(BoundPad* pad)
{
    if (!Active())
    {
        return;
    }

    // (Not in the pause menu: triangle goes back there)
    if (g_Phase == Phase::Status)
    {
        pad->buttons |= PadBitTriangle;
    }

    if (g_Phase == Phase::Pausing && G_GameController != nullptr && G_GameController->State() != GameController::StatePaused)
    {
        pad->buttons |= PadBitStart;
    }
}
}
