#pragma once

// The fast renderer (THE LIST 24, 74-77; native/GRAPHICS.md, "The fast renderer"): a PC option beside the accurate renderer, the
// default, which emulates the GS a primitive at a time (hw/hwgs.cpp). Where the fast one knows a kind of drawing (a VU1 program),
// it draws it without the GS's emulation: the program's work done as native arithmetic giving the same values bit for bit, and
// the GS's state and vertexes handed to the hardware renderer as one batch. The kinds it doesn't know are drawn as before. The
// setting is options.txt's fast_renderer (Display > Renderer, shown once the fast renderer knows a kind), $TWIN_FAST_RENDERER
// over it: 0 accurate, 1 fast, verify (both ways, the results compared: the test of a kind's prepared path).

#include "common.h"

namespace NativeGraphics
{
enum class FastMode
{
    Accurate,
    Fast,
    Verify,
};

// The mode now (the setting is read at each call: it applies at once)
FastMode FastRendererMode();
// Whether the fast renderer knows any kind of drawing (the setting's row shown)
bool FastRendererHasKinds();
}

// The settings' row shown (ui/settings.cpp)
bool NativeGraphicsFastRendererShown();
