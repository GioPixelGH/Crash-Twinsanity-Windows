#include "window.h"

#include "hw/gpu.h"
#include "ui/nativeui.h"
#include "native.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

// The graphics API a start uses (THE LIST 73): the setting's (Display > Renderer > graphics API, options.txt's graphics_api: OpenGL
// 0, the default, or Vulkan 1), $TWIN_GPU_API over it for a test (opengl or vulkan), OpenGL in a build without Vulkan.
//
// A start can fail with one API in two ways. The window can't be drawn in with it (no driver, no OpenGL 3.3, no Vulkan): system.cpp
// takes the other at once. Or it starts and never shows a picture: a driver that hangs or crashes before the first frames (#12 and
// #16's black screens on some AMD cards, #4's hang). For that one, graphics_start.txt in the settings folder names the API before
// its window is made, and goes once the window has shown StartPictures pictures, or when the session ends in order (a fatal error's
// message, the window closed, the program's exit). Found at the next start for the same API, it means that start was killed or
// crashed before its pictures: the other API is taken. Either way the setting changes to the API taken, so the settings show the
// one in use and the next start begins with it; the log says why
namespace
{
constexpr const char* StartFile = "graphics_start.txt";
// The pictures a start must show for its API to count as working (half a second at 60 Hz: a driver that shows one and hangs counts
// as not)
constexpr unsigned StartPictures = 30;

std::atomic<bool> g_Marked{false};
unsigned g_Pictures = 0;

const char* ApiName(bool vulkan)
{
    return vulkan ? "Vulkan" : "OpenGL";
}

std::string StartPath()
{
    return Native::SettingsFolder() + "/" + StartFile;
}

// $TWIN_GPU_API: 1 vulkan, 0 opengl, -1 not set (a test's API is its own: no graphics_start.txt, the setting untouched)
int AskedByVariable()
{
    const char* api = std::getenv("TWIN_GPU_API");
    if (api == nullptr || api[0] == '\0')
    {
        return -1;
    }

    return std::strcmp(api, "vulkan") == 0 ? 1 : 0;
}

bool Decide()
{
    if (const int asked = AskedByVariable(); asked >= 0)
    {
        return asked == 1 && Hw::VulkanBuilt();
    }

    bool vulkan = NativeUi::GetOption(NativeUi::OptionGraphicsApi) == 1 && Hw::VulkanBuilt();
    // (Only a window's start shows pictures: a headless run's has nothing to watch)
    if (Native::GetSettings().headless)
    {
        return vulkan;
    }

    std::ifstream file(StartPath());
    std::string last;
    if (!file.is_open() || !std::getline(file, last))
    {
        return vulkan;
    }

    // The last start's API, its window made and no pictures shown: this one again only when it isn't the same (the setting changed
    // since, by hand in options.txt)
    const bool lastVulkan = last.rfind("vulkan", 0) == 0;
    if (lastVulkan != vulkan)
    {
        return vulkan;
    }

    if (!vulkan && !Hw::VulkanBuilt())
    {
        Native::Log("graphics: the last start with OpenGL never showed the game's pictures (%s left behind); this build has no "
                    "other graphics API",
                    StartFile);
        return vulkan;
    }

    Native::Log("graphics: the last start with %s never showed the game's pictures (%s left behind), so %s this time; the "
                "setting changed to it",
                ApiName(vulkan), StartFile, ApiName(!vulkan));
    vulkan = !vulkan;
    NativeUi::SetOption(NativeUi::OptionGraphicsApi, vulkan ? 1 : 0);
    return vulkan;
}
}

bool NativeGraphicsVulkanWanted()
{
    static const bool wanted = Decide();
    return wanted;
}

bool NativeGraphicsVulkanBuilt()
{
    return Hw::VulkanBuilt();
}

void NativeGraphicsStartingWith(bool vulkan)
{
    if (AskedByVariable() >= 0)
    {
        return;
    }

    std::ofstream file(StartPath(), std::ios::trunc);
    file << (vulkan ? "vulkan" : "opengl") << "\n"
         << "# The game's window was made with this graphics API and hadn't shown its pictures yet. Left here, the next start takes\n"
         << "# the other API (Display > Renderer > graphics API). Removed once the pictures show.\n";
    g_Marked = file.good();
    g_Pictures = 0;
}

void NativeGraphicsStartFellBack(bool vulkan)
{
    if (AskedByVariable() >= 0 || NativeUi::GetOption(NativeUi::OptionGraphicsApi) == (vulkan ? 1 : 0))
    {
        return;
    }

    NativeUi::SetOption(NativeUi::OptionGraphicsApi, vulkan ? 1 : 0);
    Native::Log("graphics: the setting changed to %s", ApiName(vulkan));
}

void NativeGraphicsPictureShown()
{
    if (!g_Marked || ++g_Pictures < StartPictures)
    {
        return;
    }

    NativeGraphicsStartEnded();
}

void NativeGraphicsStartEnded()
{
    if (!g_Marked.exchange(false))
    {
        return;
    }

    std::error_code ignored;
    std::filesystem::remove(StartPath(), ignored);
}
