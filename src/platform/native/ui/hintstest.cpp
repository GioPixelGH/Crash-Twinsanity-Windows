// The game's menus' hints test (testing, $TWIN_HINTS_TEST=1 or a folder, with --quick): from the beach, a few seconds' play, then
// the pause menu. Its hints are checked as the menu hints option has them (the game's own drawn, or left out for the bar) and
// their lines logged as the controller in use labels them; with a folder and a window (no --headless) the window is saved there as
// it's shown (hints_pause.png, the pause menu; hints_levels.png, the next page). The mouse then clicks the pages' right arrow (the
// levels' page), the left one (the pause menu again) and back (play again). "hints test: pass" and exit 0, else what failed and 1;
// a step taking over 30 s fails it
#include "ui/bindings.h"

#include "graphics/presenter.h"
#include "native.h"

#include "game/gamecontroller.h"
#include "game/language.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace NativeUi
{
namespace
{
using Clock = std::chrono::steady_clock;

enum class Phase
{
    Playing,
    Pausing,
    Paused,
    PageRight,
    PageLeft,
    Back,
};

bool g_Read = false;
bool g_Active = false;
std::string g_Folder;
Phase g_Phase = Phase::Playing;
bool g_Playing = false;
bool g_PressStart = false;
u32 g_Clicks = 0;
Clock::time_point g_StepStart;

f32 Since(Clock::time_point start)
{
    return std::chrono::duration<f32>(Clock::now() - start).count();
}

bool Active()
{
    if (!g_Read)
    {
        g_Read = true;
        const char* test = std::getenv("TWIN_HINTS_TEST");
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
    Native::Log("hints test: %s", why);
    std::exit(status);
}

void Next(Phase phase)
{
    g_Phase = phase;
    g_Clicks = 0;
    g_StepStart = Clock::now();
}

// The step's hint clicked half a second into it, and once more 5 s into it when what it does hasn't happened (done: a press
// the game didn't take, as the pause doesn't read L1 and R1 while a save's result is to come or a menu appears; the log says
// it, so a click the menus lose shows there): whether it has been clicked
bool ClickHint(u32 text, bool done, const char* missing)
{
    if ((g_Clicks == 0 && Since(g_StepStart) > 0.5f) || (g_Clicks == 1 && !done && Since(g_StepStart) > 5.0f))
    {
        if (!ScriptMouseToHint(text, true))
        {
            Finish(1, missing);
        }

        if (g_Clicks == 1)
        {
            Native::Log("hints test: clicked again, the first click not taken in 5 s");
        }

        g_Clicks++;
    }

    return g_Clicks > 0;
}

void Shoot(const char* name)
{
    if (!g_Folder.empty() && !Native::GetSettings().headless)
    {
        NativeGraphicsShootWindow((g_Folder + "/" + name).c_str());
    }
}

// A game text for the log: its button glyphs by their PlayStation names (drawn as the pad's own art when it's another's), its
// arrows < and >
std::string Readable(const char* text)
{
    static const char* const Names[PromptGlyphCount] = {"(x)", "(o)", "(square)", "(triangle)", "(L1)", "(R1)", "(L2)", "(R2)"};
    std::string out;
    for (const char* at = text; *at != '\0'; at++)
    {
        const char* glyph = std::strchr(PromptGlyphs, *at);
        out += glyph != nullptr ? Names[glyph - PromptGlyphs] : std::string(1, *at);
    }

    return out;
}

// With a folder: the buttons drawn for the game's glyphs (buttonart.cpp) there too, button_xbox_0.png ... (ButtonArtLabel's order)
void WriteDrawnArt()
{
    if (g_Folder.empty())
    {
        return;
    }

    for (PromptStyle style : {PromptXbox, PromptSwitch})
    {
        for (u32 label = 0; label < ButtonArtLabels; label++)
        {
            const ButtonArt* art = DrawnButtonArt(style, static_cast<ButtonArtLabel>(label));
            if (art != nullptr)
            {
                std::string path = g_Folder + (style == PromptXbox ? "/button_xbox_" : "/button_switch_") + std::to_string(label) + ".png";
                WritePngFile(path, art->pixels.data(), static_cast<u32>(art->width), static_cast<u32>(art->height));
            }
        }
    }
}

// The pause menu's hints shown as the option has them: drawn by the game (original), or kept from drawing (the bar)
bool HintsAsTheOptionHasThem(const OLEG& oleg)
{
    bool bar = IsOn(OptionMenuHints);
    const Label* hints[] = {&oleg.selectHint, &oleg.backHint, &oleg.pagesLeftHint, &oleg.pagesRightHint};
    for (const Label* hint : hints)
    {
        if (hint->State() < Widget::StateAppearing || hint->flags.invisible != (bar ? 1u : 0u))
        {
            return false;
        }
    }

    return true;
}
}

void HintsTestFrame()
{
    if (!Active())
    {
        return;
    }

    GameController* controller = G_GameController;
    if (controller == nullptr)
    {
        return;
    }

    u32 state = controller->State();
    u32 reason = static_cast<u32>(controller->states.pauseReason);
    if (Since(g_StepStart) > 30.0f)
    {
        char why[64];
        std::snprintf(why, sizeof(why), "stuck (state %u, pause reason %u, step %u)", state, reason, static_cast<u32>(g_Phase));
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
                g_PressStart = true;
                Next(Phase::Pausing);
            }
        }

        break;
    case Phase::Pausing:
        if (state == GameController::StatePaused && reason == GameController::ReasonStart)
        {
            g_PressStart = false;
            Next(Phase::Paused);
        }

        break;
    case Phase::Paused:
        // Once the menu has faded in: the hints, a picture, then the right arrow clicked
        if (Since(g_StepStart) > 2.0f)
        {
            if (!HintsAsTheOptionHasThem(controller->oleg))
            {
                Finish(1, "the pause menu's hints aren't as the option has them");
            }

            static const char* const Styles[] = {"playstation", "xbox", "switch", "keyboard"};
            u32 style = static_cast<u32>(CurrentPromptStyle());
            Native::Log("hints test: %s, the hints \"%s\" \"%s\" \"%s\" \"%s\" (%s prompts)", IsOn(OptionMenuHints) ? "bar" : "original",
                        Readable(GameText(0x1C)).c_str(), Readable(GameText(0x1D)).c_str(), Readable(GameText(0x1E)).c_str(),
                        Readable(GameText(0x1F)).c_str(), style < 4 ? Styles[style] : "?");
            Shoot("hints_pause.png");
            WriteDrawnArt();
            Next(Phase::PageRight);
        }

        break;
    case Phase::PageRight:
        // The bar's hints aren't the game's: no arrows to click
        if (IsOn(OptionMenuHints))
        {
            Finish(0, "pass (the bar: nothing to click)");
        }

        if (ClickHint(0x1F, reason == GameController::ReasonFirstLevels, "no right arrow drawn") &&
            reason == GameController::ReasonFirstLevels && Since(g_StepStart) > 2.5f)
        {
            Native::Log("hints test: the right arrow clicked, the levels' page");
            Shoot("hints_levels.png");
            Next(Phase::PageLeft);
        }

        break;
    case Phase::PageLeft:
        if (ClickHint(0x1E, reason == GameController::ReasonStart, "no left arrow drawn") &&
            reason == GameController::ReasonStart && Since(g_StepStart) > 2.0f)
        {
            Native::Log("hints test: the left arrow clicked, the pause menu again");
            Next(Phase::Back);
        }

        break;
    case Phase::Back:
        if (ClickHint(0x1D, state == GameController::StatePlaying, "no back hint drawn") &&
            state == GameController::StatePlaying)
        {
            Finish(0, "pass (back clicked: playing again)");
        }

        break;
    }
}

void HintsTestInput(BoundPad* pad)
{
    if (Active() && g_PressStart)
    {
        pad->buttons |= PadBitStart;
    }
}
}
