#pragma once

// SMAA 1x (iryoku's SMAA, MIT: native/third_party/smaa) over the picture shown: the anti-aliasing option's SMAA choice. The
// picture's part given (a texture's rectangle) is copied into a texture of its own, then SMAA's three passes run at its size:
// the edges found by luma, the blending weights (its area and search textures), the neighbours blended. The result is a texture
// of the picture's size the window then draws as it draws the picture (made smaller when it's larger than the window). On the
// thread whose context draws the window, with it current; the framebuffer, viewport and program it leaves are the window's to
// set again.

#include "common.h"

namespace NativeGraphics
{
// The picture (texture's rectangle u0..u1, v0..v1, width x height texels) anti-aliased: the result's texture (0 when SMAA can't
// run here: its shaders didn't compile, a texture couldn't be made; the log says why once)
unsigned int SmaaPicture(unsigned int texture, f32 u0, f32 v0, f32 u1, f32 v1, u32 width, u32 height);
}
