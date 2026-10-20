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

// The last picture shown: RGBA bytes, a row after another
struct Picture
{
    u32 width = 0;
    u32 height = 0;
    std::vector<u32> pixels;
};
const Picture& LastPicture();
}
