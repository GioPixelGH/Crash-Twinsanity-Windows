// Debugging aid ($TWIN_UI_SETTINGS_SHOTS=DIR, with --headless): the settings screen opened from the main menu (straight to it past
// the logos, as $TWIN_UI_SHOTS goes) and driven through its tabs by its own input (keyboard actions and the mouse), a PNG saved of
// each step, then the program ends
#include "ui/bindings.h"
#include "ui/overlay.h"

#include "graphics/display.h"
#include "native.h"

#include "game/gamecontroller.h"

#include <cstdlib>
#include <filesystem>
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
    {445, Quit, 0, 0, 0, nullptr},
};

std::string g_Folder;
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
    }

    GameController* controller = G_GameController;
    if (g_Folder.empty() || controller == nullptr)
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
    for (const Step& step : Script)
    {
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
        case Quit:
            Native::Log("settings shots: done");
            std::exit(0);
        }
    }
}
}
