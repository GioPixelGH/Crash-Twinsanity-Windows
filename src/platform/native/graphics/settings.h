#pragma once

// The graphics settings for the native UI. Nothing is saved here: the settings' side keeps the values and sets them at start-up.
// The internal resolution is NativeGraphicsSetResolution's (resolution.h): 1-8 times the PS2's (the software GS 1-4), 0 to match
// the window. A setter returning false can't do what it's asked on this machine (or yet): the setting stays as it was.

// Which draws the GS's primitives: 0 the software GS (the PS2's own rules, pixel for pixel; slow at higher resolutions), 1 the
// hardware renderer (OpenGL; the default). Taken at the next picture. $TWIN_GS=software or hardware overrides it at start-up
bool NativeGraphicsSetRenderer(int which);
int NativeGraphicsRenderer();

// 16 bit frames' dithering (the hardware renderer): 0 none, 1 as the PS2 (its 4x4 pattern at its pixels: at higher resolutions
// a coarse grain), 2 fine (the pattern at the screen's pixels). Default 2
void NativeGraphicsSetDither(int mode);

// Textures' filtering (the hardware renderer): 0 as the game asks (the PS2's), 1 bilinear everywhere, 2 trilinear with 16x
// anisotropic filtering where the game's textures have mipmaps
void NativeGraphicsSetTextureFiltering(int mode);
// Anisotropic filtering of the game's mipmapped textures: 1 off (the PS2's sampling), 2, 4, 8 or 16 samples (the card's most at
// most)
void NativeGraphicsSetAnisotropy(int samples);

// Textures upscaled when there's no replacement (the hardware renderer): 0 off, 1 2x, 2 4x
bool NativeGraphicsSetTextureUpscale(int mode);

// Replacement textures (PCSX2's packs: textures/<serial>/replacements in the settings folder) on or off
bool NativeGraphicsSetTexturePack(bool on);

// Anti-aliasing: 0 off, 1 FXAA (on the shown picture), 2 MSAA
bool NativeGraphicsSetAntiAliasing(int mode);

// The window's swaps wait for the display's refresh
void NativeGraphicsSetVSync(bool on);
