#pragma once

// The presenter's settings for the native UI (display.cpp). None of them changes the PS2's picture, only how the window shows it.

// The rectangle the game's picture is shown in, in the window's points (not pixels: HiDPI windows have more pixels than points),
// after the letterbox or pillarbox for the TV's shape. All zero before a window is attached
void NativeGraphicsPictureRect(float* x, float* y, float* w, float* h);

// The picture's scaling to the window: linear (the default) or nearest
void NativeGraphicsSetPresentFilter(bool linear);

// A shader drawing the picture in place of the plain one: 0 none, 1 the UI's CRT filter (NativeUiCrtShaderSource). Its fragment
// shader (GLSL 330 core, as the window's backend takes it: hw/gpu.h) gets `in vec2 place` (the picture's coordinates, 0-1, top row
// first) and writes `out vec4 colour`; the presenter sets `uniform sampler2D picture` (unit 0), `uniform vec2 pictureSize` (the
// picture's pixels), `uniform vec2 viewSize` (the pixels it's drawn over) and `uniform float time` (seconds). A shader that doesn't
// compile is reported and the plain one stays
void NativeGraphicsSetPostFilter(int which);
// The post filter drawing now (0 none: none asked for, or it didn't compile); it's made at the next picture shown
int NativeGraphicsPostFilterInUse();

// The UI's side (src/platform/native/ui/crtfilter.cpp); a weak fallback here: no source
const char* NativeUiCrtShaderSource();

// A drawing over the window after each picture is presented (the UI's screens drawn at the window's own resolution): called on
// the presenter's thread to record through the window's backend (NativeGraphicsWindowGpu, hw/gpu.h: OpenGL's or Vulkan's), its
// framebuffer 0 the window's and bound, the viewport the whole window (its pixels), depth, stencil and scissor tests off and the
// blend state left to it; pixelScale is the window's pixels a point. It leaves the state as it likes: the presenter sets what it
// needs. Null: none
void NativeGraphicsSetOverlay(void (*draw)(int windowPixelWidth, int windowPixelHeight, float pixelScale));
namespace Hw
{
class Gpu;
}
// The window's backend's recording (THE LIST 72), on the presenter's thread
Hw::Gpu& NativeGraphicsWindowGpu();

// Testing: the window as the next picture shows it (the picture and the overlay over it, at the window's pixels) written to a PNG
// (none without a window: a headless run)
void NativeGraphicsShootWindow(const char* path);
