// Debugging aid ($TWIN_UI_SETTINGS_SHOTS=DIR, with --headless): the settings screen opened from the main menu (straight to it past
// the logos, as $TWIN_UI_SHOTS goes) and driven through its tabs by its own input (keyboard actions and the mouse), a PNG saved of
// each step, then the program ends. $TWIN_SETTINGS_LOG_TEST=1 drives it the same way through changes the log must give once each,
// as they're left when the screen is closed (check_windows.py's log check): the refresh rate stepped from 144 Hz down to 50 and up
// to 60, the resolution from 2x to 3x, back and to 3x again, a key bound (F8); then the program ends
#include "ui/bindings.h"
#include "ui/overlay.h"

#include "graphics/display.h"
#include "mods.h"
#include "native.h"

#include "game/gamecontroller.h"

#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>

namespace NativeUi
{
namespace
{
enum Kind
{
    Open,
    Key,
    Mouse,
    Click,
    Shot,
    // A key pressed (its SDL scancode in action), as the keyboard's
    Press,
    Quit,
};

struct Step
{
    u32 frame;
    Kind kind;
    u32 action;
    f32 x;
    f32 y;
    const char* name;
};

// The settings screen's actions (settings.cpp's UiAction): up, down, left, right, confirm, back, previous tab, next tab
enum : u32
{
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Back,
    TabPrevious,
    TabNext,
};

constexpr Step Script[] = {
    {60, Open, 0, 0, 0, nullptr},
    {100, Shot, 0, 0, 0, "settings_gameplay"},
    {105, Key, TabNext, 0, 0, nullptr},
    {125, Shot, 0, 0, 0, "settings_display"},
    {130, Key, Down, 0, 0, nullptr},
    {132, Key, Down, 0, 0, nullptr},
    {145, Shot, 0, 0, 0, "settings_display_resolution"},
    // Down to the display tab's lower rows: v-sync and the FPS counter (show FPS)
    {147, Key, Down, 0, 0, nullptr},
    {149, Key, Down, 0, 0, nullptr},
    {151, Key, Down, 0, 0, nullptr},
    {153, Key, Down, 0, 0, nullptr},
    {155, Key, Down, 0, 0, nullptr},
    {157, Key, Down, 0, 0, nullptr},
    {159, Key, Down, 0, 0, nullptr},
    {161, Key, Down, 0, 0, nullptr},
    {175, Shot, 0, 0, 0, "settings_display_show_fps"},
    {180, Key, TabNext, 0, 0, nullptr},
    {195, Shot, 0, 0, 0, "settings_audio"},
    {200, Mouse, 0, 0.70f, 0.312f, nullptr},
    {210, Shot, 0, 0, 0, "settings_audio_mouse_hover"},
    {215, Key, TabNext, 0, 0, nullptr},
    {230, Shot, 0, 0, 0, "settings_controls"},
    {235, Key, Down, 0, 0, nullptr},
    {237, Key, Down, 0, 0, nullptr},
    {239, Key, Down, 0, 0, nullptr},
    {241, Key, Down, 0, 0, nullptr},
    {243, Key, Down, 0, 0, nullptr},
    {245, Key, Down, 0, 0, nullptr},
    {247, Key, Down, 0, 0, nullptr},
    {249, Key, Down, 0, 0, nullptr},
    {251, Key, Down, 0, 0, nullptr},
    {265, Shot, 0, 0, 0, "settings_controls_table"},
    {267, Key, Down, 0, 0, nullptr},
    {269, Key, Down, 0, 0, nullptr},
    {271, Key, Down, 0, 0, nullptr},
    {273, Key, Down, 0, 0, nullptr},
    {275, Key, Down, 0, 0, nullptr},
    {277, Key, Down, 0, 0, nullptr},
    {292, Shot, 0, 0, 0, "settings_controls_actions"},
    {294, Key, Up, 0, 0, nullptr},
    {296, Key, Up, 0, 0, nullptr},
    {298, Key, Up, 0, 0, nullptr},
    {300, Key, Right, 0, 0, nullptr},
    {302, Key, Confirm, 0, 0, nullptr},
    {315, Shot, 0, 0, 0, "settings_controls_press_a_key"},
    {320, Key, Back, 0, 0, nullptr},
    {350, Key, TabNext, 0, 0, nullptr},
    {365, Shot, 0, 0, 0, "settings_accessibility"},
    {370, Key, TabPrevious, 0, 0, nullptr},
    {372, Key, TabPrevious, 0, 0, nullptr},
    {374, Key, TabPrevious, 0, 0, nullptr},
    {380, Key, Right, 0, 0, nullptr},
    {395, Shot, 0, 0, 0, "settings_display_keep_countdown"},
    {400, Key, Back, 0, 0, nullptr},
    {405, Key, Back, 0, 0, nullptr},
    {440, Shot, 0, 0, 0, "settings_closed_main_menu"},
    // The Mods page (from the display tab, back round to the last): its import confirmed (only with a zip or disc image in the
    // mods folder to import: the import row first; else the step's skipped), the dialog left, a step down
    {450, Open, 0, 0, 0, nullptr},
    {455, Key, TabPrevious, 0, 0, nullptr},
    {457, Key, TabPrevious, 0, 0, nullptr},
    {470, Shot, 0, 0, 0, "settings_mods"},
    {472, Key, Confirm, 0, 0, "import"},
    {560, Shot, 0, 0, 0, "settings_mods_imported"},
    {562, Key, Back, 0, 0, nullptr},
    {580, Shot, 0, 0, 0, "settings_mods_after_import"},
    {582, Key, Down, 0, 0, nullptr},
    {600, Shot, 0, 0, 0, "settings_mods_down"},
    {605, Quit, 0, 0, 0, nullptr},
};

// $TWIN_SETTINGS_LOG_TEST's (options.txt's refresh_60hz=3 given by the check)
constexpr Step LogScript[] = {
    {30, Open, 0, 0, 0, nullptr},
    // The display tab's refresh rate (its fifth row): 144 Hz to 120, 60, 50 and 60 again
    {40, Key, TabNext, 0, 0, nullptr},
    {44, Key, Down, 0, 0, nullptr},
    {46, Key, Down, 0, 0, nullptr},
    {48, Key, Down, 0, 0, nullptr},
    {50, Key, Down, 0, 0, nullptr},
    {54, Key, Left, 0, 0, nullptr},
    {56, Key, Left, 0, 0, nullptr},
    {58, Key, Left, 0, 0, nullptr},
    {60, Key, Right, 0, 0, nullptr},
    // The resolution (the third): 2x to 3x, back to 2x, to 3x
    {64, Key, Up, 0, 0, nullptr},
    {66, Key, Up, 0, 0, nullptr},
    {70, Key, Right, 0, 0, nullptr},
    {72, Key, Left, 0, 0, nullptr},
    {74, Key, Right, 0, 0, nullptr},
    // The controls tab's table: an action's second key bound to F8
    {80, Key, TabNext, 0, 0, nullptr},
    {82, Key, TabNext, 0, 0, nullptr},
    {84, Key, Down, 0, 0, nullptr},
    {86, Key, Down, 0, 0, nullptr},
    {88, Key, Down, 0, 0, nullptr},
    {90, Key, Down, 0, 0, nullptr},
    {92, Key, Down, 0, 0, nullptr},
    {94, Key, Down, 0, 0, nullptr},
    {96, Key, Down, 0, 0, nullptr},
    {98, Key, Down, 0, 0, nullptr},
    {100, Key, Down, 0, 0, nullptr},
    {102, Key, Down, 0, 0, nullptr},
    {104, Key, Down, 0, 0, nullptr},
    {106, Key, Down, 0, 0, nullptr},
    {108, Key, Right, 0, 0, nullptr},
    {110, Key, Confirm, 0, 0, nullptr},
    {116, Press, SDL_SCANCODE_F8, 0, 0, nullptr},
    // Closed: the changes logged
    {130, Key, Back, 0, 0, nullptr},
    {170, Quit, 0, 0, 0, nullptr},
};

std::string g_Folder;
bool g_LogTest = false;
bool g_Checked = false;
bool g_InMenu = false;
u32 g_Frame = 0;
}

void SettingsShotsFrame()
{
    if (!g_Checked)
    {
        g_Checked = true;
        const char* folder = std::getenv("TWIN_UI_SETTINGS_SHOTS");
        if (folder != nullptr && folder[0] != '\0')
        {
            g_Folder = folder;
            std::error_code ignored;
            std::filesystem::create_directories(g_Folder, ignored);
        }

        const char* logTest = std::getenv("TWIN_SETTINGS_LOG_TEST");
        g_LogTest = g_Folder.empty() && logTest != nullptr && logTest[0] == '1';
    }

    GameController* controller = G_GameController;
    if ((g_Folder.empty() && !g_LogTest) || controller == nullptr)
    {
        return;
    }

    if (!g_InMenu)
    {
        if (controller->State() == GameController::StateVivendiLogo)
        {
            controller->SetNextState(GameController::StateMainMenu);
        }
        else if (controller->State() == GameController::StateMainMenu)
        {
            g_InMenu = true;
        }

        return;
    }

    g_Frame++;
    const Step* begin = g_LogTest ? std::begin(LogScript) : std::begin(Script);
    const Step* end = g_LogTest ? std::end(LogScript) : std::end(Script);
    for (const Step* at = begin; at != end; at++)
    {
        const Step& step = *at;
        if (step.frame != g_Frame)
        {
            continue;
        }

        switch (step.kind)
        {
        case Open:
            OpenSettings();
            break;
        case Key:
            // (A Key step named "import" only when there's something to import: never the mods folder opened instead)
            if (step.name != nullptr && Native::Mods::Importables().empty())
            {
                break;
            }

            // Escape while waiting for a key cancels it, as the keyboard's does
            SettingsActForTest(step.action);
            break;
        case Mouse:
        case Click:
            SettingsMouseForTest(step.x, step.y, step.kind == Click);
            break;
        case Shot:
        {
            // The overlay's frame (the settings screen at the window's resolution) drawn offscreen at 1280 x 720 and 2560 x 1440
            for (u32 scale = 1; scale <= 2; scale++)
            {
                std::vector<u32> pixels;
                u32 width = 1280 * scale;
                u32 height = 720 * scale;
                std::string path = g_Folder + "/" + step.name + (scale == 1 ? "_1280x720" : "_2560x1440") + ".png";
                if (RenderFrameOffscreen(width, height, &pixels) && WritePngFile(path, pixels.data(), width, height))
                {
                    Native::Log("settings shots: %s", path.c_str());
                }
            }

            break;
        }
        case Press:
            SettingsKeyForTest(static_cast<s32>(step.action));
            break;
        case Quit:
            Native::Log("%s: done", g_LogTest ? "settings log test" : "settings shots");
            std::exit(0);
        }
    }
}
}
