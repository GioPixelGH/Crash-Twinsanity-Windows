// The resolution setting: the render scale (1 to 4 times the PS2's picture), the display (a window of one of a few sizes, or
// fullscreen) and the screen it's on (THE LIST 88: automatic, the main one, or a screen chosen by name), chosen in the settings
// (settings.cpp), applied through the graphics and kept in the app's settings (Native::SettingsFolder()/resolution.txt), then
// applied again at every start-up
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
const Resolution PcDefaults = {2, 1280, 720, false};

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
        else if (line.rfind("screen=", 0) == 0)
        {
            // (A file edited on Windows may end its lines with a carriage return)
            read.screen = line.substr(7);
            while (!read.screen.empty() && (read.screen.back() == '\r' || read.screen.back() == ' '))
            {
                read.screen.pop_back();
            }

            any = true;
        }
        else if (std::sscanf(line.c_str(), "screen_order=%d", &a) == 1)
        {
            read.screenOrder = a > 0 ? a : 1;
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
    // The screen chosen (THE LIST 88), by name; none: automatic
    if (!resolution.screen.empty())
    {
        file << "screen=" << resolution.screen << "\n";
        file << "screen_order=" << resolution.screenOrder << "\n";
    }
}

bool SameScreen(const Resolution& a, const Resolution& b)
{
    return a.screen == b.screen && (a.screen.empty() || a.screenOrder == b.screenOrder);
}

// "automatic", "\"VG27AQ1A\"", "screen 2 of those named \"DELL P2419H\""
std::string ScreenText(const Resolution& resolution)
{
    if (resolution.screen.empty())
    {
        return "automatic";
    }

    const std::string name = "\"" + resolution.screen + "\"";
    return resolution.screenOrder > 1 ? "screen " + std::to_string(resolution.screenOrder) + " of those named " + name : name;
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

s32 ScreenCount()
{
    return g_ResolutionToGraphics && !Native::GetSettings().headless ? NativeGraphicsScreenCount() : 0;
}

s32 ScreenIndex(const Resolution& resolution)
{
    if (resolution.screen.empty())
    {
        return -1;
    }

    // The screens of its name in SDL's order (two alike: the one in its place among them)
    s32 named = 0;
    const s32 count = ScreenCount();
    for (s32 screen = 0; screen < count; screen++)
    {
        if (NativeGraphicsScreenName(screen) == resolution.screen && ++named == resolution.screenOrder)
        {
            return screen;
        }
    }

    return -1;
}

s32 ScreenChoice(const Resolution& resolution)
{
    return ScreenIndex(resolution) + 1;
}

Resolution WithScreenChoice(const Resolution& resolution, s32 choice)
{
    Resolution chosen = resolution;
    chosen.screen.clear();
    chosen.screenOrder = 1;
    const s32 screen = choice - 1;
    if (screen < 0 || screen >= ScreenCount())
    {
        return chosen;
    }

    chosen.screen = NativeGraphicsScreenName(screen);
    for (s32 before = 0; before < screen; before++)
    {
        chosen.screenOrder += NativeGraphicsScreenName(before) == chosen.screen ? 1 : 0;
    }

    return chosen;
}

s32 StartScreen()
{
    Resolution saved;
    if (!ReadSaved(&saved) || saved.screen.empty())
    {
        return -1;
    }

    const s32 screen = ScreenIndex(saved);
    if (screen < 0)
    {
        // (Kept in resolution.txt: the window goes there again once it's connected)
        Native::Log("display: the screen chosen (%s) isn't connected, so the main screen instead", ScreenText(saved).c_str());
    }

    return screen;
}

void SetResolution(const Resolution& resolution)
{
    Resolution current = CurrentResolution();
    const bool sameScreen = SameScreen(current, resolution);
    if (current.scale == resolution.scale && current.windowWidth == resolution.windowWidth &&
        current.windowHeight == resolution.windowHeight && current.fullscreen == resolution.fullscreen && sameScreen)
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
        // The screen (THE LIST 88): a window changed to another screen goes there, and one made fullscreen goes to the chosen
        // screen first, so it fills that one (automatic: where it is, as before)
        const s32 screen = ScreenIndex(resolution);
        const bool move = !sameScreen || (resolution.fullscreen && !current.fullscreen && screen >= 0);
        if (move && resolution.fullscreen)
        {
            NativeGraphicsMoveToScreen(screen, false);
        }

        NativeGraphicsSetResolution(resolution.scale, resolution.windowWidth, resolution.windowHeight, resolution.fullscreen);
        if (move && !resolution.fullscreen)
        {
            NativeGraphicsMoveToScreen(screen, false);
        }
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
        // The chosen screen's window (made there, centred, at the defaults' size: system.cpp) centred again at its own size
        if (const s32 screen = ScreenIndex(saved); screen >= 0)
        {
            NativeGraphicsMoveToScreen(screen, !saved.fullscreen);
        }
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
    const std::string screen = saved.screen.empty() ? "" : ", the screen chosen " + ScreenText(saved);
    Native::Log("resolution (%s): %dx the PS2's picture, %s%s%s%s", there ? "resolution.txt's" : "the defaults: no resolution.txt yet",
                saved.scale, saved.fullscreen ? "fullscreen (" : "", window, saved.fullscreen ? " when windowed)" : "", screen.c_str());
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

    // The screen (THE LIST 88), then the one the window's on now
    if (!SameScreen(now, g_Logged))
    {
        Native::Log("options: screen %s -> %s", ScreenText(g_Logged).c_str(), ScreenText(now).c_str());
        if (g_ResolutionToGraphics)
        {
            NativeGraphicsLogDisplay();
        }
    }

    g_Logged = now;
}
}

namespace NativeUi
{
// $TWIN_SCREEN_TEST=1 (a window's run with two screens or more, THE LIST 88): every 120 frames from the first a change as the
// settings row makes it, then logged as the settings screen's closing logs it ("options: screen ..." and the "display: screen N
// of M" line): the last screen, fullscreen, the first screen, the last again, windowed, automatic. 60 frames after each, where the
// window is ("screen test: step N settled, ..."); "screen test: done" after the last
void ScreenTestFrame()
{
    static const bool on = [] {
        const char* test = std::getenv("TWIN_SCREEN_TEST");
        return test != nullptr && test[0] == '1';
    }();
    static u32 frame = 0;
    if (!on || frame > 7 * 120)
    {
        return;
    }

    frame++;
    const u32 step = frame / 120;
    if (frame % 120 == 60 && step >= 1 && step <= 6)
    {
        Native::Log("screen test: step %u settled, the window on %s", step, NativeGraphicsWindowPlace().c_str());
        return;
    }

    if (frame % 120 != 0)
    {
        return;
    }

    if (step > 6)
    {
        Native::Log("screen test: done");
        return;
    }

    Resolution resolution = CurrentResolution();
    const s32 count = ScreenCount();
    switch (step)
    {
    case 1:
        resolution = WithScreenChoice(resolution, count);
        break;
    case 2:
        resolution.fullscreen = true;
        break;
    case 3:
        resolution = WithScreenChoice(resolution, 1);
        break;
    case 4:
        resolution = WithScreenChoice(resolution, count);
        break;
    case 5:
        resolution.fullscreen = false;
        break;
    default:
        resolution = WithScreenChoice(resolution, 0);
        break;
    }

    Native::Log("screen test: step %u of 6, %d screens", step, count);
    SetResolution(resolution);
    LogResolutionChanges();
}
}
