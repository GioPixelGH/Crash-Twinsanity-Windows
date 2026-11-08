#pragma once

// The hardware renderer: the GS's primitives drawn by the GPU (OpenGL 3.3 core) into render targets at the internal resolution,
// the way PCSX2's hardware renderers do. Local memory stays the PS2's and the GS front end (registers, kicks, transfers, the
// CLUT, the PCRTC) the same; the renderer keeps a render target for each frame and depth buffer it draws into and keeps them and
// local memory in step page by page. What it can't draw exactly (a frame in a Z format, blends GL's blending can't make, the
// destination alpha test, a texture's region clamp...) is left to the software rasteriser at the PS2's resolution, local memory
// made current for it first.

#include "common.h"

#include <vector>

namespace Gs
{
class Gs;
struct DisplayImage;
}

namespace NativeGraphics
{
// Switched on (with the GPU's context current, or one of its own made in a hidden window), at an internal scale (1-8). False when
// OpenGL 3.3 isn't there: the software rasteriser stays
bool StartHardwareRenderer(u32 scale);
void StopHardwareRenderer();
bool HardwareRendererActive();
// The shader programs, one for each combination of the GS's states drawn with (its bits): a hook told of each new one made while
// drawing (the frame waits for its compiling), and a list's made at once (a start's, before anything's drawn, so play doesn't wait
// for them: their compiling asked for and not waited for, a driver that compiles on threads of its own going on meanwhile; one
// that fails is made again when drawn with). The bits mean something for one source only (its hash). On the renderer's thread
void HardwareSetProgramHook(void (*made)(u32 sourceHash, u32 bits));
u32 HardwareProgramSourceHash();
u32 HardwareWarmPrograms(const std::vector<u32>& bits);
void HardwareSetScale(u32 scale);
u32 HardwareScale();

// The shown picture as a texture of the GPU (the presenter draws it): the texture, its picture's rectangle (0-1, top row first)
// and the picture's size in pixels. False when the shown buffer isn't a render target (the picture is read from local memory)
struct DisplayTexture
{
    u32 texture;
    float u0;
    float v0;
    float u1;
    float v1;
    u32 width;
    u32 height;
};
bool HardwareDisplayTexture(DisplayTexture* display);
// The shown picture read back at the internal resolution (RGBA, top row first)
bool HardwareReadDisplay(Gs::DisplayImage& image);
// The render thread's hand-over (renderthread.h): the shown picture copied into a texture of its own (one of three, by slot) for
// the window's context to draw while this one goes on drawing. The window's fence from its last drawing of the slot's copy is
// waited for first (null: none), and a fence the window's context waits for is made. False when the shown buffer isn't a
// render target (the picture is read from local memory)
struct ShownTexture
{
    u32 texture;
    u32 width;
    u32 height;
    void* fence;
};
bool HardwareCopyDisplay(u32 slot, void* drawnFence, ShownTexture* shown);

// The settings (settings.h): 16 bit frames' dithering (0 none, 1 at the PS2's pixels, 2 at the screen's) and the textures'
// filtering (0 the game's, 1 bilinear, 2 trilinear and anisotropic)
void HardwareSetDither(int mode);
void HardwareSetFiltering(int mode);
// Anisotropic filtering's most samples for the game's mipmapped textures (1: off, the PS2's sampling; up to 16, the card's most)
void HardwareSetAnisotropy(int samples);

// Statistics: draws done by the GPU and left to the software rasteriser since the start, and the last reason for leaving one
struct HardwareStats
{
    u64 drawn;
    u64 fallbacks;
    u64 downloads;
    u64 uploads;
    u64 batches;
    u64 replaced;
    u64 copies;
    u64 readbacks;
    u64 decodes;
    u64 contentHits;
    const char* lastFallback;
};
HardwareStats GetHardwareStats();
}
