// The resolution setting: the render scale (1 to 4 times the PS2's picture) and the display (a window of one of a few sizes, or
// fullscreen), chosen on the game's graphic options page (olegpages.cpp, under TWIN_NATIVE), applied through the graphics and kept
// in the app's settings (Native::SettingsFolder()/resolution.txt), then applied again at every start-up
#include "ui/nativeui.h"

#include "native.h"
#include "graphics/resolution.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace NativeUi
{
bool g_ResolutionToGraphics = true;

namespace
{
constexpr const char* SettingsFile = "resolution.txt";

struct WindowSize
{
    s32 width;
    s32 height;
};
// The display choices' windows (TextWindow960 on), the last choice fullscreen (the display's own size). The picture keeps the TV's
// shape in any of them (4:3, or 16:9 with the game's widescreen setting), letterboxed
constexpr WindowSize Windows[DisplayChoices - 1] = {
    {960, 720}, {1280, 960}, {1600, 1200}, {1920, 1440}, {1280, 720}, {1920, 1080},
};
static_assert(DisplayChoices - 1 == sizeof(Windows) / sizeof(Windows[0]));
constexpr s32 FullscreenChoice = DisplayChoices - 1;
// The defaults (nativeui.h's DefaultResolution): the platform's window is made at their size (system.cpp). A Steam Deck's
// (DefaultResolutionFor) start fullscreen instead, its own 1280 x 800 screen, at the same 2x (the PS2's 448 lines drawn at 896)
constexpr Resolution PcDefaults = {2, 1280, 720, false};

Resolution Defaults()
{
    return DefaultResolutionFor(Native::SteamDeckModel() != nullptr);
}

bool g_Known = false;
Resolution g_Current = PcDefaults;
// The log's changes (LogResolutionChanges): the resolution it last gave (the start's, then each change's), and whether it changed
Resolution g_Logged = PcDefaults;
bool g_LoggedKnown = false;
bool g_ChangeDue = false;

// "fullscreen", "a 1280x720 window"
std::string DisplayText(const Resolution& resolution)
{
    return resolution.fullscreen ? std::string("fullscreen")
                                 : "a " + std::to_string(resolution.windowWidth) + "x" + std::to_string(resolution.windowHeight) +
                                       " window";
}

std::string SettingsPath()
{
    return Native::SettingsFolder() + "/" + SettingsFile;
}

s32 Clamp(s32 value, s32 low, s32 high)
{
    return value < low ? low : value > high ? high : value;
}

// The saved resolution: false when there's none (or it can't be read)
bool ReadSaved(Resolution* resolution)
{
    std::ifstream file(SettingsPath());
    if (!file)
    {
        return false;
    }

    Resolution read = Defaults();
    bool any = false;
    std::string line;
    while (std::getline(file, line))
    {
        int a = 0;
        int b = 0;
        if (std::sscanf(line.c_str(), "scale=%d", &a) == 1)
        {
            read.scale = ChoiceScale(a);
            any = true;
        }
        else if (std::sscanf(line.c_str(), "window=%dx%d", &a, &b) == 2 && a > 0 && b > 0)
        {
            read.windowWidth = a;
            read.windowHeight = b;
            any = true;
        }
        else if (std::sscanf(line.c_str(), "fullscreen=%d", &a) == 1)
        {
            read.fullscreen = a != 0;
            any = true;
        }
    }

    if (any)
    {
        *resolution = read;
    }

    return any;
}

void Save(const Resolution& resolution)
{
    std::error_code error;
    std::filesystem::create_directories(Native::SettingsFolder(), error);
    std::ofstream file(SettingsPath(), std::ios::trunc);
    if (!file)
    {
        Native::Log("resolution: can't write %s", Native::LogPath(SettingsPath()).c_str());
        return;
    }

    file << "# Crash Twinsanity's resolution (the graphic options' resolution and display)\n";
    file << "scale=" << resolution.scale << "\n";
    file << "window=" << resolution.windowWidth << "x" << resolution.windowHeight << "\n";
    file << "fullscreen=" << (resolution.fullscreen ? 1 : 0) << "\n";
}
}

Resolution DefaultResolutionFor(bool steamDeck)
{
    Resolution defaults = PcDefaults;
    defaults.fullscreen = steamDeck;
    return defaults;
}

Resolution DefaultResolution()
{
    return Defaults();
}

Resolution CurrentResolution()
{
    if (!g_Known)
    {
        Resolution saved;
        if (ReadSaved(&saved))
        {
            g_Current = saved;
        }
        else
        {
            // The graphics' own
            int scale = g_Current.scale;
            int width = g_Current.windowWidth;
            int height = g_Current.windowHeight;
            bool fullscreen = g_Current.fullscreen;
            if (g_ResolutionToGraphics)
            {
                NativeGraphicsGetResolution(&scale, &width, &height, &fullscreen);
            }

            g_Current = {ChoiceScale(scale), width > 0 ? width : PcDefaults.windowWidth,
                         height > 0 ? height : PcDefaults.windowHeight, fullscreen};
        }

        g_Known = true;
    }

    // A window resized by hand keeps its size when only the scale changes
    if (g_ResolutionToGraphics && !g_Current.fullscreen && !Native::GetSettings().headless)
    {
        int width = 0;
        int height = 0;
        NativeGraphicsGetResolution(nullptr, &width, &height, nullptr);
        if (width > 0 && height > 0)
        {
            g_Current.windowWidth = width;
            g_Current.windowHeight = height;
        }
    }

    return g_Current;
}

s32 ChoiceScale(s32 scale)
{
    s32 chosen = Scales[0];
    for (s32 choice : Scales)
    {
        chosen = choice <= scale ? choice : chosen;
    }

    return chosen;
}

s32 ScaleChoice(const Resolution& resolution)
{
    s32 scale = ChoiceScale(resolution.scale);
    for (s32 choice = 0; choice < ScaleChoices; choice++)
    {
        if (Scales[choice] == scale)
        {
            return choice;
        }
    }

    return 0;
}

s32 DisplayChoice(const Resolution& resolution)
{
    if (resolution.fullscreen)
    {
        return FullscreenChoice;
    }

    // The window's size, or the nearest of the choices' (a window resized by hand)
    s32 best = 0;
    s64 bestDistance = -1;
    for (s32 choice = 0; choice < FullscreenChoice; choice++)
    {
        s64 dx = Windows[choice].width - resolution.windowWidth;
        s64 dy = Windows[choice].height - resolution.windowHeight;
        s64 distance = dx * dx + dy * dy;
        if (bestDistance < 0 || distance < bestDistance)
        {
            best = choice;
            bestDistance = distance;
        }
    }

    return best;
}

Resolution FromChoices(s32 scaleChoice, s32 displayChoice)
{
    Resolution current = CurrentResolution();
    Resolution resolution = current;
    resolution.scale = Scales[Clamp(scaleChoice, 0, ScaleChoices - 1)];
    displayChoice = Clamp(displayChoice, 0, FullscreenChoice);
    if (displayChoice == FullscreenChoice)
    {
        resolution.fullscreen = true;
    }
    else
    {
        resolution.fullscreen = false;
        // The same choice as the window's keeps its own size
        if (current.fullscreen || DisplayChoice(current) != displayChoice)
        {
            resolution.windowWidth = Windows[displayChoice].width;
            resolution.windowHeight = Windows[displayChoice].height;
        }
    }

    return resolution;
}

void SetResolution(const Resolution& resolution)
{
    Resolution current = CurrentResolution();
    if (current.scale == resolution.scale && current.windowWidth == resolution.windowWidth &&
        current.windowHeight == resolution.windowHeight && current.fullscreen == resolution.fullscreen)
    {
        return;
    }

    // (The log's line once the settings screen is closed: LogResolutionChanges)
    if (!g_LoggedKnown)
    {
        g_Logged = current;
        g_LoggedKnown = true;
    }

    g_Current = resolution;
    g_ChangeDue = true;
    if (g_ResolutionToGraphics)
    {
        NativeGraphicsSetResolution(resolution.scale, resolution.windowWidth, resolution.windowHeight, resolution.fullscreen);
    }

    Save(resolution);
}

namespace
{
s32 g_ShownScale = -1;
s32 g_ShownDisplay = -1;
}

void NoteChoices(s32 scaleChoice, s32 displayChoice)
{
    g_ShownScale = scaleChoice;
    g_ShownDisplay = displayChoice;
}

bool ChoicesChanged(s32 scaleChoice, s32 displayChoice)
{
    if (scaleChoice == g_ShownScale && displayChoice == g_ShownDisplay)
    {
        return false;
    }

    NoteChoices(scaleChoice, displayChoice);
    return true;
}

void ApplySavedResolution()
{
    Resolution saved;
    if (!ReadSaved(&saved))
    {
        saved = Defaults();
    }

    g_Current = saved;
    g_Known = true;
    if (!g_ResolutionToGraphics)
    {
        return;
    }

    if (!Native::GetSettings().headless)
    {
        NativeGraphicsSetResolution(saved.scale, saved.windowWidth, saved.windowHeight, saved.fullscreen);
    }
    else if (const char* scale = std::getenv("TWIN_SCALE"); scale != nullptr)
    {
        // $TWIN_SCALE: a headless run's internal scale (the checks' 6x and 8x; headless runs draw at the graphics' default)
        NativeGraphicsSetResolution(std::atoi(scale), saved.windowWidth, saved.windowHeight, false);
    }

    // The screen the window is on, for the log
    NativeGraphicsLogDisplay();
}

void LogResolution()
{
    Resolution saved;
    bool there = ReadSaved(&saved);
    if (!there)
    {
        saved = Defaults();
    }

    char window[48];
    std::snprintf(window, sizeof(window), "a %dx%d window", saved.windowWidth, saved.windowHeight);
    Native::Log("resolution (%s): %dx the PS2's picture, %s%s%s", there ? "resolution.txt's" : "the defaults: no resolution.txt yet",
                saved.scale, saved.fullscreen ? "fullscreen (" : "", window, saved.fullscreen ? " when windowed)" : "");
    // The changes are from this
    g_Logged = saved;
    g_LoggedKnown = true;
    g_ChangeDue = false;
}

void LogResolutionChanges()
{
    if (!g_ChangeDue)
    {
        return;
    }

    g_ChangeDue = false;
    const Resolution now = g_Current;
    if (now.scale != g_Logged.scale)
    {
        Native::Log("options: resolution %dx -> %dx the PS2's picture", g_Logged.scale, now.scale);
    }

    // (A fullscreen's window size isn't seen until it's windowed)
    if (now.fullscreen != g_Logged.fullscreen || (!now.fullscreen && (now.windowWidth != g_Logged.windowWidth ||
                                                                       now.windowHeight != g_Logged.windowHeight)))
    {
        Native::Log("options: display %s -> %s", DisplayText(g_Logged).c_str(), DisplayText(now).c_str());
    }

    g_Logged = now;
}
}
