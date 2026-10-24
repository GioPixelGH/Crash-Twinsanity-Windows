// Portable mode's test ($TWIN_PORTABLE_TEST=1, or a folder for the dialog's pictures; with --headless): from the main menu (past the
// logos, as the settings shots go) the settings opened, Gameplay's portable mode row found and stepped, and its dialog followed by its
// own input, as a player's select. Asked: select copies (portable.cpp), then done, and select closes the game as it would
// (QuitAfterPortableSwitch: exit 0). Refused (the program's folder can't be written to): select closes the dialog and the test ends
// (exit 0). Each step is logged ("portable test: ..."); check_windows.py's portable check looks at the folders after. A step over 60 s,
// or a copy that fails, fails it (exit 1). With a folder the screen is saved there at each step (portable_ask.png ...)
#include "ui/bindings.h"
#include "ui/overlay.h"
#include "ui/settingsmodel.h"

#include "native.h"

#include "game/gamecontroller.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace NativeUi
{
namespace
{
using Clock = std::chrono::steady_clock;

// The settings screen's actions (settings.cpp's UiAction)
enum : u32
{
    Down = 1,
    Right = 3,
    Confirm = 4,
    TabNext = 7,
};

enum class Phase
{
    Menu,
    Open,
    Find,
    Asked,
    Copying,
    Closing,
    Refused,
};

bool g_Read = false;
bool g_Active = false;
std::string g_Folder;
Phase g_Phase = Phase::Menu;
u32 g_Frames = 0;
Clock::time_point g_StepStart;
bool g_CopyingShot = false;

bool Active()
{
    if (!g_Read)
    {
        g_Read = true;
        const char* test = std::getenv("TWIN_PORTABLE_TEST");
        g_Active = test != nullptr && test[0] != '\0';
        if (g_Active && std::string(test) != "1")
        {
            g_Folder = test;
            std::error_code ignored;
            std::filesystem::create_directories(g_Folder, ignored);
        }
    }

    return g_Active;
}

[[noreturn]] void End(int status, const char* why)
{
    Native::Log("portable test: %s", why);
    std::exit(status);
}

void Next(Phase phase)
{
    g_Phase = phase;
    g_Frames = 0;
    g_StepStart = Clock::now();
}

// The screen as it's shown (the overlay at 1280 x 720), with a folder
void Shot(const char* name)
{
    std::vector<u32> pixels;
    std::string path = g_Folder + "/" + name + ".png";
    if (!g_Folder.empty() && RenderFrameOffscreen(1280, 720, &pixels) && WritePngFile(path, pixels.data(), 1280, 720))
    {
        Native::Log("portable test: %s", path.c_str());
    }
}

// Gameplay's portable mode row (-1: none)
s32 PortableRow()
{
    const std::vector<Row>& rows = SettingsRows(TabGameplay);
    for (s32 i = 0; i < static_cast<s32>(rows.size()); i++)
    {
        if (rows[static_cast<size_t>(i)].label == TextPortable)
        {
            return i;
        }
    }

    return -1;
}
}

void PortableTestFrame()
{
    GameController* controller = G_GameController;
    if (!Active() || controller == nullptr)
    {
        return;
    }

    if (g_Phase == Phase::Menu)
    {
        if (controller->State() == GameController::StateVivendiLogo)
        {
            controller->SetNextState(GameController::StateMainMenu);
        }
        else if (controller->State() == GameController::StateMainMenu)
        {
            Next(Phase::Open);
        }

        return;
    }

    g_Frames++;
    if (std::chrono::duration<f32>(Clock::now() - g_StepStart).count() > 60.0f)
    {
        End(1, "FAILED: a step took over 60 s");
    }

    u32 dialog = PortableDialogForTest();
    switch (g_Phase)
    {
    case Phase::Open:
        if (g_Frames >= 30)
        {
            OpenSettings();
            Next(Phase::Find);
        }

        break;
    case Phase::Find:
    {
        // An action every other frame, as a key's press
        if (g_Frames < 10 || g_Frames % 2 != 0)
        {
            break;
        }

        s32 row = PortableRow();
        if (row < 0)
        {
            End(1, "FAILED: Gameplay has no portable mode row");
        }

        if (SettingsTabForTest() != TabGameplay)
        {
            SettingsActForTest(TabNext);
        }
        else if (SettingsRowForTest() != row)
        {
            SettingsActForTest(Down);
        }
        else
        {
            Native::Log("portable test: the row selected (portable mode %s), stepped", Native::PortableMode() ? "on" : "off");
            SettingsActForTest(Right);
            Next(Phase::Asked);
        }

        break;
    }
    case Phase::Asked:
        if (g_Frames < 10)
        {
            break;
        }

        if (dialog == DialogAsk)
        {
            Shot("portable_ask");
            Native::Log("portable test: asked; select");
            SettingsActForTest(Confirm);
            Next(Phase::Copying);
        }
        else if (dialog == DialogRefused)
        {
            Shot("portable_refused");
            Native::Log("portable test: refused (the program's folder can't be written to); select");
            SettingsActForTest(Confirm);
            Next(Phase::Refused);
        }
        else
        {
            End(1, "FAILED: the row asked nothing");
        }

        break;
    case Phase::Copying:
        if (dialog == DialogCopying && !g_CopyingShot)
        {
            g_CopyingShot = true;
            Shot("portable_copying");
        }
        else if (dialog == DialogDone)
        {
            std::string backup;
            u64 copied = 0;
            Native::PortableSwitchState(&copied, nullptr, &backup, nullptr);
            Shot("portable_done");
            Native::Log("portable test: done, %llu bytes copied, %s; select (the game closes)", static_cast<unsigned long long>(copied),
                        backup.empty() ? "nothing set aside" : ("the old folder set aside as \"" + backup + "\"").c_str());
            SettingsActForTest(Confirm);
            Next(Phase::Closing);
        }
        else if (dialog == DialogFailed)
        {
            std::string reason;
            Native::PortableSwitchState(nullptr, nullptr, nullptr, &reason);
            Shot("portable_failed");
            End(1, ("FAILED: the copy failed: " + reason).c_str());
        }

        break;
    case Phase::Closing:
        if (g_Frames > 60)
        {
            End(1, "FAILED: the game didn't close");
        }

        break;
    case Phase::Refused:
        if (dialog == DialogNone && Native::PortableMode())
        {
            End(1, "FAILED: portable mode on after all");
        }
        else if (dialog == DialogNone)
        {
            End(0, "pass (refused, the settings where they were)");
        }

        break;
    default:
        break;
    }
}
}
