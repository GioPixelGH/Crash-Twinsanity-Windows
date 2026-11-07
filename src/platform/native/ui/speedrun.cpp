// The speedrun timer (THE LIST 13, the game option speedrun_timer, off): real time from New Game, drawn in the game's own font
// at the top of the picture (the game's text queue, as "press start" is), and a split at each level's end: when play moves on
// to another of the game's 16 levels (g_AreaLevels of the play's area), the level just left and its time are shown under the
// timer for a few seconds and written to the log; the end credits stop it. Real time: the pauses, the loadings and the movies
// count, as a speedrun's timer does. Back at the title it's reset. TwinsanityEvolved's speedrun mode gave the idea; the code is
// ours.
#include "ui/nativeui.h"

#include "native.h"

#include "game/colour.h"
#include "game/gamecontroller.h"
#include "game/language.h"
#include "game/oleg.h"
#include "game/overlay.h"
#include "game/renderer.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace NativeUi
{
namespace
{
using Clock = std::chrono::steady_clock;

// Where the timer and the split are drawn (fractions of the picture: the top's middle, above the HUD's row) and their sizes
constexpr f32 TimerX = 0.5f;
constexpr f32 TimerY = Rounded(0.07);
constexpr f32 TimerScale = 0.5f;
constexpr f32 SplitY = Rounded(0.12);
constexpr f32 SplitScale = Rounded(0.4);
constexpr f32 SplitSeconds = 5.0f;

struct Run
{
    bool running = false;
    bool finished = false;
    Clock::time_point start;
    Clock::time_point stopped;
    // The level being played (-1 none yet) and when it was entered
    s32 level = -1;
    Clock::time_point levelStart;
    // The last split's words and when it was made
    std::string split;
    Clock::time_point splitAt;
    u32 lastState = ~0u;
};
Run g_Run;

std::string Format(double seconds)
{
    const s32 hundredths = static_cast<s32>(seconds * 100.0);
    const s32 minutes = hundredths / 6000;
    char text[32];
    if (minutes >= 60)
    {
        std::snprintf(text, sizeof(text), "%d:%02d:%02d.%02d", minutes / 60, minutes % 60, (hundredths / 100) % 60, hundredths % 100);
    }
    else
    {
        std::snprintf(text, sizeof(text), "%d:%02d.%02d", minutes, (hundredths / 100) % 60, hundredths % 100);
    }

    return text;
}

double Since(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double>(to - from).count();
}

void Split(Clock::time_point now)
{
    if (g_Run.level < 0)
    {
        return;
    }

    const char* name = GameText(LevelNameTexts + static_cast<u32>(g_Run.level));
    const std::string took = Format(Since(g_Run.levelStart, now));
    g_Run.split = std::string(name != nullptr ? name : "?") + "  " + took;
    g_Run.splitAt = now;
    Native::Log("speedrun: %s in %s (the run %s)", name != nullptr ? name : "?", took.c_str(),
                Format(Since(g_Run.start, now)).c_str());
}
}

void SpeedrunFrame()
{
    GameController* controller = G_GameController;
    if (controller == nullptr)
    {
        return;
    }

    const u32 state = controller->State();
    const Clock::time_point now = Clock::now();
    if (state != g_Run.lastState)
    {
        if (state == GameController::StateNewGame && IsOn(OptionSpeedrunTimer))
        {
            g_Run = {};
            g_Run.running = true;
            g_Run.start = now;
            g_Run.levelStart = now;
            Native::Log("speedrun: the timer started (New Game)");
        }
        else if (state == GameController::StateTitle)
        {
            g_Run = {};
        }
        else if (state == GameController::StateCredits && g_Run.running && !g_Run.finished)
        {
            Split(now);
            g_Run.finished = true;
            g_Run.stopped = now;
            Native::Log("speedrun: the run ended at the credits in %s", Format(Since(g_Run.start, now)).c_str());
        }

        g_Run.lastState = state;
    }

    if (!g_Run.running || g_Run.finished)
    {
        return;
    }

    // A level's end: play in another of the game's levels
    const u32 area = controller->progress.play.area;
    const s32 level = area < 25 ? g_AreaLevels[area] : -1;
    if ((state == GameController::StatePlaying || state == GameController::StateWatching) && level >= 0 && level != g_Run.level)
    {
        Split(now);
        g_Run.level = level;
        g_Run.levelStart = now;
    }
}

void SpeedrunDraw(Renderer* renderer)
{
    GameController* controller = G_GameController;
    if (!g_Run.running || renderer == nullptr || controller == nullptr || !IsOn(OptionSpeedrunTimer))
    {
        return;
    }

    const Clock::time_point now = Clock::now();
    const std::string time = Format(Since(g_Run.start, g_Run.finished ? g_Run.stopped : now));
    u32 colour;
    GetColor(&colour, ColourWhite);
    renderer->font = &controller->font;
    renderer->colour = colour;
    renderer->textScale.x = TimerScale;
    renderer->textScale.y = TimerScale;
    renderer->textAlignment.value = TextAlignment::Centred;
    QueueText(renderer, time.c_str(), TimerX, TimerY);
    if (!g_Run.split.empty() && Since(g_Run.splitAt, now) < SplitSeconds)
    {
        renderer->textScale.x = SplitScale;
        renderer->textScale.y = SplitScale;
        QueueText(renderer, g_Run.split.c_str(), TimerX, SplitY);
    }
}
}
