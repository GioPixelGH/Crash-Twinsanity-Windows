#pragma once

// The TV's side: the vertical blanks the frames are paced by (50 a second for PAL, 59.94 for NTSC), and the picture the PCRTC
// sends, shown in the window (window.h) or kept to be read back

#include "common.h"

#include <vector>

namespace NativeGraphics
{
// The video mode the renderer set the display up in
void SetVideoMode(bool pal);
// Waits for the next vertical blank (pacing can be turned off, for the tests that run frames as fast as they can)
void WaitForVBlank();
void SetPacing(bool paced);
// The frames' rate past the video mode's (120, 144; 0 as fast as they come; 50 or 60: the video mode's own blanks)
void SetFrameRate(s32 rate);
// The PCRTC's picture read out of the GS and shown
void ShowFrame();
// The renderer and internal resolution settings (settings.h, resolution.h) put into effect: at the start and after each picture
void ApplyGraphicsSettings();
// Windows: every graphics adapter written to the log with its driver's version and its memory, once the window is there
// (adapters_win32.cpp)
void LogAdapters();
// The video memory the program uses on the adapter where it uses the most (Windows' own count, whatever the graphics API), the budget
// Windows gives it there and what of it Windows moved to shared memory (the log's memory line, #55: Vulkan stalls of seconds on a
// 4 GB card); false where it isn't known (Linux, Windows before 10)
struct VideoMemory
{
    unsigned long long used = 0;
    unsigned long long budget = 0;
    unsigned long long shared = 0;
    char adapter[128] = {};
};
bool VideoMemoryNow(VideoMemory* memory);
// Whether the hardware renderer draws with Vulkan (THE LIST 71-72): as the window's backend does, or as $TWIN_GPU_API asks without
// a window
bool RendererOnVulkan();

// The last picture shown: RGBA bytes, a row after another
struct Picture
{
    u32 width = 0;
    u32 height = 0;
    std::vector<u32> pixels;
};
const Picture& LastPicture();
}
