#pragma once

// The picture's resolution, for the native build's settings: how many times the PS2's resolution the GS draws at (1 to 4: every
// primitive drawn by the GS's own rules with that many samples a pixel across and down, the frame shown at that size), and the
// window's size or full screen. The picture keeps the TV's shape (4:3, or 16:9 with the game's widescreen setting) in the window,
// letterboxed. Nothing is saved here: the settings' side keeps the values and sets them at start-up
struct NativeGraphicsResolution
{
    int internalScale;
    int windowWidth;
    int windowHeight;
    bool fullscreen;
};

// Sets them (an internal scale outside 1-4 is clamped; a window size of 0 keeps the window's; fullscreen takes the display's
// own size). Call from the thread the game runs on (SDL's windows are the main thread's)
void NativeGraphicsSetResolution(int internalScale, int windowWidth, int windowHeight, bool fullscreen);
// What they are now (the window's size as it is, the user may have resized it)
void NativeGraphicsGetResolution(int* internalScale, int* windowWidth, int* windowHeight, bool* fullscreen);
NativeGraphicsResolution NativeGraphicsCurrentResolution();
// The screen the window is on in the log, once at start-up (its name, size, refresh rate, scale and HDR; "none" without a window)
void NativeGraphicsLogDisplay();
