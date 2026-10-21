// The hardware renderer (hwgs.h). The GS's semantics carried over to the GPU:
//
// - Coordinates: the GS draws a pixel when the point at its top-left corner is inside a primitive (left and top edges inside);
//   the GPU samples pixel centres. A vertex at x (12.4, less XYOFFSET) goes to x / 16 * scale + 0.5: a PS2 pixel's block of
//   scale x scale pixels has the GS's sample point as its first one's centre, the others at quarter (or half...) steps after
//   it. The positions are on the rasteriser's sixteenth of a pixel grid, so it snaps none of them: a nudge off the grid (as
//   this once had, 1/256 of a pixel) gets snapped a sixteenth away, losing thin primitives' pixels (shadow volumes' edges).
//   The draws' viewport is far bigger than the target (the scissor keeps them in it), so nothing reaching past the target is
//   clipped either (clipping makes new vertexes off the grid). At the PS2's resolution it's the GS's own sampling
// - Colours, texture coordinates, fog and depth are interpolated linearly in the screen (noperspective) as the GS's DDA does;
//   STQ is divided at each pixel. Colours are the GS's bytes (0-255, alpha 0x80 = 1.0) throughout the shader, truncated where the
//   GS truncates
// - Depth is the GS's value over 2^32 (exact below 2^24) in a 32 bit float buffer, written from the shader (truncated as the
//   GS's is); the tests are GL's. A 32 bit depth buffer first drawn with values in the top half (an effect's, made from
//   colours) is kept reversed, (2^32 - 1 - z) / 2^32, exact up there, its tests reversed
// - A target drawn into by a filterless sampling of a target at a higher scale reads each texel's first sample (the GS's
//   sample point), not the one nearest the coordinate
// - The blend ((A - B) * C >> 7) + D is GL's blending: dual source, the second output's alpha C (the source alpha / 128), or
//   the constant alpha (FIX / 128); the alpha written is the source's (FBA applied), as the GS doesn't blend alpha
// - A failed alpha test's AFAIL is a second pass over the batch's failing pixels with the writes AFAIL keeps
// - 16 bit frames: the colour dithered (DIMX, at the PS2's pixel) and truncated to 5 bits a channel before it's written, the
//   alpha its top bit (no blending into them: the blend would have to happen before the truncation)
//
// Render targets: a frame or depth buffer drawn into gets a GPU texture (at the scale) for its base, width and layout (32 bit
// colour and 24 bit share one, as do Z32 and Z24). Local memory stays the PS2's; each 8 KB page records where its newest content
// is (local memory, or one target), each target which of its pages it holds current. A target is brought up to date from local
// memory before the GPU uses it (uploads), and local memory from a target before the CPU side reads or writes the page
// (downloads: the target read back at the PS2's resolution). A texture that is a target's buffer in its own layout is sampled
// from the target; any other is decoded from local memory by the software GS's decoder (CLUT, TEXA, every format) and cached.

#include "hwgs.h"

#include "gl.h"
#include "replace.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include "../png.h"

#include "../hardware.h"
#include "../gs/backend.h"
#include "../gs/drawstate.h"
#include "../gs/gs.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace NativeGraphics
{
// The presenter's window and context (display.cpp), shared when there is one; the render thread's own (on it)
bool PresenterContext(SDL_Window** window, SDL_GLContext* context);
bool RenderContext(SDL_Window** window, SDL_GLContext* context);
}

namespace NativeGraphics
{
namespace
{
using namespace Hw;

// The layouts targets keep: 32 bit colour (and 24), 16 bit colour, 16 bit colour S, Z32 (and Z24), Z16, Z16S
enum TargetClass : u32
{
    ClassColour32,
    ClassColour16,
    ClassColour16S,
    ClassDepth32,
    ClassDepth16,
    ClassDepth16S,
    // A frame in a Z format: colour kept, laid out as the Z format
    ClassColourZ32,
    ClassColourZ16,
    ClassColourZ16S,
    ClassNone,
};


u32 ClassOf(u32 psm)
{
    switch (psm)
    {
    case Gs::PSMCT32:
    case Gs::PSMCT24:
        return ClassColour32;
    case Gs::PSMCT16:
        return ClassColour16;
    case Gs::PSMCT16S:
        return ClassColour16S;
    case Gs::PSMZ32:
    case Gs::PSMZ24:
        return ClassDepth32;
    case Gs::PSMZ16:
        return ClassDepth16;
    case Gs::PSMZ16S:
        return ClassDepth16S;
    default:
        return ClassNone;
    }
}

// A frame buffer's class: a Z format's frame is colour laid out as Z
u32 FrameClassOf(u32 psm)
{
    switch (psm)
    {
    case Gs::PSMZ32:
    case Gs::PSMZ24:
        return ClassColourZ32;
    case Gs::PSMZ16:
        return ClassColourZ16;
    case Gs::PSMZ16S:
        return ClassColourZ16S;
    default:
        return ClassOf(psm);
    }
}

// Colour classes stored as 32 bits (the others as 16)
bool Is32Class(u32 cls)
{
    return cls == ClassColour32 || cls == ClassColourZ32;
}

u32 StoredPsm(u32 cls)
{
    static constexpr u32 Formats[] = {Gs::PSMCT32, Gs::PSMCT16, Gs::PSMCT16S, Gs::PSMZ32, Gs::PSMZ16, Gs::PSMZ16S,
                                      Gs::PSMZ32,  Gs::PSMZ16,  Gs::PSMZ16S, 0};
    return Formats[cls];
}

bool IsDepthClass(u32 cls)
{
    return cls == ClassDepth32 || cls == ClassDepth16 || cls == ClassDepth16S;
}

// A page's size in pixels of a layout
void PageSize(u32 psm, s32* width, s32* height)
{
    switch (Gs::BitsPerPixel(psm))
    {
    case 16:
        *width = 64;
        *height = 64;
        break;
    case 8:
        *width = 128;
        *height = 64;
        break;
    case 4:
        *width = 128;
        *height = 128;
        break;
    default:
        *width = 64;
        *height = 32;
        break;
    }
}

struct Rect
{
    s32 left;
    s32 top;
    s32 right;
    s32 bottom;
};

struct Target
{
    u32 bp = 0;
    u32 bw = 0;
    u32 cls = 0;
    u32 psm = 0;
    bool depth = false;
    // A 32 bit depth buffer whose values are near the top (an effect's, made from colours): kept as (2^32 - 1 - z) / 2^32,
    // exact there (a float's steps near 1.0 are 256 of the GS's units)
    bool reversed = false;
    // Its size in the PS2's pixels, and its pixels a PS2 pixel each way (the internal scale; 1 for the effects' small buffers
    // under native scaling)
    u32 width = 0;
    u32 height = 0;
    u32 scale = 1;
    // Pages laid out as rectangles (its base at a page's start)
    bool aligned = false;
    GLuint texture = 0;
    GLuint fbo = 0;
    Gs::PageSet pages;
    Gs::PageSet valid;
    // The pages it holds newest with every other target's copy out of date (MarkDrawn has nothing to do there)
    Gs::PageSet owned;
    // A colour target's float buffer of wrapping sums not yet added to it (resolve), its framebuffers by depth-stencil buffer
    GLuint accum = 0;
    bool accumPending = false;
    // A colour target's alpha changes (a batch that can write it, an upload): a stencil made from it is good while it's the same
    u64 alphaVersion = 0;
    // A depth-stencil buffer's stencil: the destination alpha test it holds (its colour target, DATM, that one's alpha version
    // and the scissor), none when stencilFor is null
    const Target* stencilFor = nullptr;
    u32 stencilDatm = 0;
    u64 stencilAlphaVersion = 0;
    s32 stencilScissor[4] = {};
    // Each page's pixels (y << 11 | x) and their bounding rectangle
    std::vector<std::vector<u32>> pixels;
    std::vector<Rect> rects;
};

struct HwVertex
{
    f32 x;
    f32 y;
    u32 z;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    f32 s;
    f32 t;
    f32 q;
    f32 fog;
};

// What a batch draws with: everything that changes GL's state or the shader's uniforms (compared as bytes)
struct BatchKey
{
    Target* colour;
    Target* depth;
    GLuint texture;
    u32 primitive;
    u32 program;
    u32 minFilter;
    u32 magFilter;
    u32 wrapS;
    u32 wrapT;
    u32 maxLevel;
    f32 texWidth;
    f32 texHeight;
    f32 gsTexWidth;
    f32 gsTexHeight;
    u32 blend;
    u32 blendEquation;
    u32 blendSource;
    u32 blendDestination;
    u32 blendFix;
    u32 depthFunc;
    u32 depthWrite;
    u32 colourMask;
    s32 scissor[4];
    u32 aref;
    u32 afail;
    u32 alphaTested;
    u32 fogColour;
    u32 ta0;
    u32 ta1;
    u32 aem;
    f32 lodK;
    u32 lodL;
    u32 mxl;
    u64 dimx;
    f32 zMax;
    // The shader's blend (the GS's A, B, C, D, FIX, PABE, COLCLAMP), the frame mask as each channel's stored bits, DATM, and
    // the destination alpha test by a stencil
    u32 gsBlend[4];
    u32 gsFix;
    u32 pabe;
    u32 colclamp;
    u32 frameMask[4];
    u32 datm;
    u32 stencilDate;
    u32 ditherScale;
    u32 anisotropy;
    u32 wrapAdd;
    // A target sampled without a filter at a higher scale than the frame's: its scale (each texel read at its GS sample point)
    u32 texSnap;
    u32 depthReverse;
    u32 zMaxU;
    // The depth test at the GS's sample point (the block's first sample) in the shader: its ZTST, 0 for GL's own test
    u32 nativeDepthTest;
};

// Every primitive builds a key (zeroed, so its padding compares too) and compares it with the batch's: done inline, a word at a
// time (the C library's memset, memcmp and memcpy calls cost more than the work on Windows' runtime)
static_assert(sizeof(BatchKey) % sizeof(u64) == 0, "BatchKey compared a word at a time");

inline bool SameBatchKey(const BatchKey& a, const BatchKey& b)
{
    const u64* x = reinterpret_cast<const u64*>(&a);
    const u64* y = reinterpret_cast<const u64*>(&b);
    u64 difference = 0;
    for (size_t i = 0; i < sizeof(BatchKey) / sizeof(u64); i++)
    {
        difference |= x[i] ^ y[i];
    }

    return difference == 0;
}

// The shader's features (its variant's bits)
enum ProgramBits : u32
{
    BitTextured = 1u << 0,
    BitTfxShift = 1,
    BitTcc = 1u << 3,
    BitFog = 1u << 4,
    BitAtstShift = 5,
    BitAlphaTest = 1u << 8,
    BitUv = 1u << 9,
    BitTexAlphaShift = 10,
    BitFrame16 = 1u << 12,
    BitDither = 1u << 13,
    BitFba = 1u << 14,
    BitMipmap = 1u << 15,
    BitLodConstant = 1u << 16,
    BitFailPass = 1u << 17,
    BitDepth = 1u << 18,
    BitShaderBlend = 1u << 19,
    BitDate = 1u << 20,
    BitFrame24 = 1u << 21,
    BitWrapAdd = 1u << 22,
    BitNativeDepth = 1u << 23,
};

constexpr const char* VertexSource = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in uint aZ;
layout(location = 2) in vec4 aColour;
layout(location = 3) in vec3 aStq;
layout(location = 4) in float aFog;
uniform vec2 viewSize;
uniform vec2 viewOrigin;
uniform float zMax;
uniform uint zMaxU;
uniform int depthReverse;
uniform float pointSize;
noperspective out vec4 vColour;
noperspective out vec3 vStq;
noperspective out float vFog;
noperspective out float vDepth;
void main()
{
    gl_Position = vec4((aPos + viewOrigin) / viewSize * 2.0 - 1.0, 0.0, 1.0);
    gl_PointSize = pointSize;
    vColour = aColour;
    vStq = aStq;
    vFog = aFog;
    // Depth as a float of the GS's value over 2^32 (exact below 2^24), or for a reversed target 2^32 - 1 less it (exact near
    // the top)
    vDepth = depthReverse != 0 ? float(0xFFFFFFFFu - min(aZ, zMaxU)) * (1.0 / 4294967296.0) : min(float(aZ), zMax) * (1.0 / 4294967296.0);
}
)";

constexpr const char* FragmentSource = R"(
noperspective in vec4 vColour;
noperspective in vec3 vStq;
noperspective in float vFog;
noperspective in float vDepth;
layout(location = 0, index = 0) out vec4 outColour;
layout(location = 0, index = 1) out vec4 outFactor;
uniform sampler2D tex;
uniform vec2 texSize;
uniform vec2 texActual;
uniform float texSnap;
uniform float aref;
uniform vec3 fogColour;
uniform float ta0;
uniform float ta1;
uniform int aem;
uniform float lodK;
uniform float lodL;
uniform float lodMax;
uniform int dimx[16];
uniform float scale;
uniform sampler2D destination;
uniform int blendOn;
uniform int blendA;
uniform int blendB;
uniform int blendC;
uniform int blendD;
uniform float blendFix;
uniform int pabe;
uniform int colclamp;
uniform int datm;
uniform uvec4 frameMask;
uniform int depthReverse;
uniform sampler2D depthCopy;
uniform int nativeZtst;
uniform int targetScale;
vec3 Pick(int select, vec3 source, vec3 destination)
{
    return select == 0 ? source : select == 1 ? destination : vec3(0.0);
}
// The depth at the GS's sample point: the pixel's centre is 1/256 of a pixel past it (right and down, see the top), which on
// a steep surface is a unit or two of depth; the GS truncates it (exactly, while the value is below 2^24)
float GsDepth()
{
    float d = vDepth - (dFdx(vDepth) + dFdy(vDepth)) * SAMPLE_SHIFT;
    float z = d * 4294967296.0;
    if (z < 16777216.0)
    {
        // (reversed, the truncation is upwards)
        z = depthReverse != 0 ? ceil(max(z, 0.0) - 1.0 / 64.0) : floor(max(z, 0.0) + 1.0 / 64.0);
        d = z * (1.0 / 4294967296.0);
    }
    return d;
}
void main()
{
    // (before any discard: the derivatives need the whole quad)
    float depth = GsDepth();
#if NATIVEDEPTH
    // The depth test where the GS makes it: at the block's first sample, against the depth there
    {
        ivec2 block = ivec2(floor(gl_FragCoord.xy / float(targetScale))) * targetScale;
        vec2 back = gl_FragCoord.xy - (vec2(block) + 0.5);
        float here = depth - dFdx(depth) * back.x - dFdy(depth) * back.y;
        float there = texelFetch(depthCopy, block, 0).r;
        bool pass = depthReverse != 0 ? (nativeZtst == 2 ? here <= there : here < there) : (nativeZtst == 2 ? here >= there : here > there);
        if (!pass)
        {
            discard;
        }
    }
#endif
    vec4 c = vColour;
    vec3 rgb;
    float a;
#if TEXTURED
#if UVMODE
    vec2 uv = vStq.xy;
#else
    vec2 uv = vStq.xy / vStq.z * texSize;
#endif
#if NATIVEDEPTH
    // (and the texture read where the GS reads it: the block's first sample's coordinates)
    {
        ivec2 block = ivec2(floor(gl_FragCoord.xy / float(targetScale))) * targetScale;
        vec2 back = gl_FragCoord.xy - (vec2(block) + 0.5);
        uv = uv - dFdx(uv) * back.x - dFdy(uv) * back.y;
    }
#endif
    float lod = 0.0;
#if MIPMAP
#if LODCONST
    lod = lodK;
#else
    lod = log2(1.0 / abs(vStq.z)) * lodL + lodK;
#endif
    lod = min(lod, lodMax);
#endif
    // A target at a higher scale than the frame, unfiltered: the texel's first sample, the one at the GS's sample point (any
    // other sits up to a pixel's fraction away, across an edge the PS2's resolution doesn't have)
    if (texSnap > 0.0)
    {
        uv = floor(uv) + 0.5 / texSnap;
    }
    vec4 t = floor(textureLod(tex, uv / texActual, lod) * 255.0 + 0.5);
#if TEXALPHA == 1
    t.a = (aem != 0 && t.r + t.g + t.b == 0.0) ? 0.0 : ta0;
#elif TEXALPHA == 2
    // A 16 bit target's channels are 5 bits (GL's blending into it under a stencil DATE may have left the low bits)
    t.rgb = floor(t.rgb / 8.0) * 8.0;
    t.a = t.a >= 128.0 ? ta1 : ((aem != 0 && t.r + t.g + t.b == 0.0) ? 0.0 : ta0);
#endif
    float va = floor(c.a);
#if TFX == 0
    rgb = min(floor(t.rgb * c.rgb / 128.0), 255.0);
#if TCC
    a = min(floor(t.a * c.a / 128.0), 255.0);
#else
    a = va;
#endif
#elif TFX == 1
    rgb = t.rgb;
#if TCC
    a = t.a;
#else
    a = va;
#endif
#elif TFX == 2
    rgb = min(floor(t.rgb * c.rgb / 128.0) + va, 255.0);
#if TCC
    a = min(t.a + va, 255.0);
#else
    a = va;
#endif
#else
    rgb = min(floor(t.rgb * c.rgb / 128.0) + va, 255.0);
#if TCC
    a = t.a;
#else
    a = va;
#endif
#endif
#else
    rgb = floor(c.rgb);
    a = floor(c.a);
#endif
#if ALPHATEST
    bool pass;
#if ATST == 0
    pass = false;
#elif ATST == 1
    pass = true;
#elif ATST == 2
    pass = a < aref;
#elif ATST == 3
    pass = a <= aref;
#elif ATST == 4
    pass = a == aref;
#elif ATST == 5
    pass = a >= aref;
#elif ATST == 6
    pass = a > aref;
#else
    pass = a != aref;
#endif
#if FAILPASS
    if (pass)
    {
        discard;
    }
#else
    if (!pass)
    {
        discard;
    }
#endif
#endif
#if FOG
    float f = floor(vFog);
    rgb = floor((f * rgb + (255.0 - f) * fogColour) / 256.0);
#endif
#if WRAPADD
    // A wrapping additive blend (COLCLAMP off, D the destination): this primitive's part of the sum, as it adds to the 16 bit
    // frame's 5 bit channels (the destination's low bits are none, so the part's truncation is the sum's), summed by GL into a
    // float buffer and wrapped when it's resolved
    {
        float factor = blendC == 0 ? a : blendFix;
        vec3 part = floor((Pick(blendA, rgb, vec3(0.0)) - Pick(blendB, rgb, vec3(0.0))) * factor / 128.0);
#if DITHER
        ivec2 p = ivec2(floor(gl_FragCoord.xy / scale));
        part = part + float(dimx[(p.y & 3) * 4 + (p.x & 3)]);
#endif
        part = floor(part / 8.0) * 8.0;
        // As its signed equivalent modulo 256 (-8, not 248), in 256ths: a half float holds these and their sums exactly (in
        // 255ths, as large parts, the sums of the many layers of a shadow volume pass rounded off by a unit or two)
        part = mod(part, 256.0);
        part = mix(part, part - 256.0, greaterThanEqual(part, vec3(128.0)));
        outColour = vec4(part / 256.0, 0.0);
        outFactor = vec4(0.0);
#if DEPTH
        gl_FragDepth = depth;
#endif
        return;
    }
#endif
#if SHADERBLEND
    // The destination (a copy of the target from before the batch) and the GS's own blend, mask and tests on it
    vec4 d = floor(texelFetch(destination, ivec2(gl_FragCoord.xy), 0) * 255.0 + 0.5);
#if FRAME16
    d.rgb = floor(d.rgb / 8.0) * 8.0;
    d.a = d.a >= 128.0 ? 128.0 : 0.0;
#endif
    float da = d.a;
#if FRAME24
    da = 128.0;
#endif
#if DATE
    if ((da >= 128.0 ? 1 : 0) != datm)
    {
        discard;
    }
#endif
    if (blendOn != 0 && !(pabe != 0 && a < 128.0))
    {
        float factor = blendC == 0 ? a : blendC == 1 ? da : blendFix;
        rgb = floor((Pick(blendA, rgb, d.rgb) - Pick(blendB, rgb, d.rgb)) * factor / 128.0) + Pick(blendD, rgb, d.rgb);
    }
#endif
#if FRAME16
#if DITHER
    ivec2 p = ivec2(floor(gl_FragCoord.xy / scale));
    rgb = rgb + float(dimx[(p.y & 3) * 4 + (p.x & 3)]);
#endif
#endif
    rgb = colclamp != 0 ? clamp(rgb, 0.0, 255.0) : mod(rgb, 256.0);
#if FRAME16
    rgb = floor(rgb / 8.0) * 8.0;
#endif
    float written = a;
#if FBA
    written = written < 128.0 ? written + 128.0 : written;
#endif
#if FRAME16
    written = written >= 128.0 ? 128.0 : 0.0;
#endif
#if SHADERBLEND
    uvec4 merged = (uvec4(vec4(rgb, written)) & ~frameMask) | (uvec4(d) & frameMask);
    outColour = vec4(merged) / 255.0;
#else
    outColour = vec4(rgb / 255.0, written / 255.0);
#endif
    outFactor = vec4(0.0, 0.0, 0.0, min(a / 128.0, 1.0));
#if DEPTH
    gl_FragDepth = depth;
#endif
}
)";

// Uploads of depth: a staging texture's values (depth over 2^32) written over a rectangle
constexpr const char* DepthVertexSource = R"(#version 330 core
uniform vec4 rect;
uniform vec2 viewSize;
void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec2 at = mix(rect.xy, rect.zw, corner);
    gl_Position = vec4(at / viewSize * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* DepthFragmentSource = R"(#version 330 core
uniform sampler2D staging;
uniform float scale;
out vec4 unused;
void main()
{
    gl_FragDepth = texelFetch(staging, ivec2(floor(gl_FragCoord.xy / scale)), 0).r;
    unused = vec4(0.0);
}
)";

// The PCRTC's two circuits merged (circuit 1 over circuit 2 by ALP or its own alpha, over the background colour), at the scale
constexpr const char* MergeVertexSource = R"(#version 330 core
void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* MergeFragmentSource = R"(#version 330 core
uniform sampler2D circuit1;
uniform sampler2D circuit2;
// Each circuit: enabled, its place in the picture (pixels at the scale), its size, its corner in its buffer, its buffer's size
uniform int enabled1;
uniform int enabled2;
uniform vec4 place1;
uniform vec4 place2;
uniform vec2 corner1;
uniform vec2 corner2;
uniform vec2 size1;
uniform vec2 size2;
uniform float alp;
uniform int ownAlpha;
uniform vec3 background;
out vec4 colour;
bool Read(sampler2D source, vec4 place, vec2 corner, vec2 size, vec2 at, out vec4 c)
{
    vec2 local = at - place.xy;
    if (local.x < 0.0 || local.y < 0.0 || local.x >= place.z || local.y >= place.w)
    {
        return false;
    }

    c = texelFetch(source, ivec2(corner + local), 0);
    return true;
}
void main()
{
    vec2 at = floor(gl_FragCoord.xy);
    vec4 c1;
    vec4 c2;
    bool has1 = enabled1 != 0 && Read(circuit1, place1, corner1, size1, at, c1);
    bool has2 = enabled2 != 0 && Read(circuit2, place2, corner2, size2, at, c2);
    vec3 under = has2 ? c2.rgb : background;
    vec3 result = under;
    if (has1)
    {
        float a = ownAlpha != 0 ? min(c1.a * 2.0, 1.0) : alp;
        result = c1.rgb * a + under * (1.0 - a);
    }

    colour = vec4(result, 1.0);
}
)";

// The destination alpha test as a stencil: pixels whose destination alpha's top bit is DATM get 1
constexpr const char* DateFragmentSource = R"(#version 330 core
uniform sampler2D destination;
uniform int datm;
out vec4 unused;
void main()
{
    float a = floor(texelFetch(destination, ivec2(gl_FragCoord.xy), 0).a * 255.0 + 0.5);
    if ((a >= 128.0 ? 1 : 0) != datm)
    {
        discard;
    }

    unused = vec4(0.0);
}
)";

// A wrapping sum resolved: the target's colours plus the float buffer's sum, modulo 256
constexpr const char* ResolveFragmentSource = R"(#version 330 core
uniform sampler2D before;
uniform sampler2D sum;
out vec4 colour;
void main()
{
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec3 c = floor(texelFetch(before, at, 0).rgb * 255.0 + 0.5) + floor(texelFetch(sum, at, 0).rgb * 256.0 + 0.5);
    colour = vec4(mod(c, 256.0) / 255.0, 0.0);
}
)";


// GS local memory on the GPU (a 1024x1024 R32UI texture of its words) and the conversions between it and the targets, by the
// GS's swizzles: a layout's table of where each pixel of a page is (its block in the page, its unit in the block) and the
// inverse (a page's units' pixels), made from gsmemory.cpp's own functions
constexpr const char* MemCommonSource = R"(
uniform usampler2D mem;
uniform usampler2D fwd;
uniform usampler2D inv;
uniform int unitsPerBlock;
uniform int unitShift;
uniform int unitBits;
uniform int pageWidth;
uniform int pageHeight;
uniform int pagesAcross;
uniform int pagesDown;
uniform int basePointer;
uniform int xorZ;
uint MemUnit(uint address)
{
    uint w = (address >> uint(unitShift)) & 0xFFFFFu;
    uint word = texelFetch(mem, ivec2(int(w & 1023u), int(w >> 10)), 0).r;
    uint sub = address & ((1u << uint(unitShift)) - 1u);
    uint mask = unitBits == 32 ? 0xFFFFFFFFu : ((1u << uint(unitBits)) - 1u);
    return (word >> (sub * uint(unitBits))) & mask;
}
uint UnitAddress(ivec2 p)
{
    uint page = uint((p.y / pageHeight) * pagesAcross + p.x / pageWidth);
    uint f = texelFetch(fwd, ivec2(p.x % pageWidth, p.y % pageHeight), 0).r;
    uint block = uint(basePointer) + page * 32u + (f >> 16);
    if (xorZ != 0)
    {
        block ^= 24u;
    }

    return (block & 16383u) * uint(unitsPerBlock) + (f & 0xFFFFu);
}
// The pixel a unit of memory is (false: none of the buffer's)
bool UnitPixel(uint address, out ivec2 p)
{
    uint block = address / uint(unitsPerBlock);
    uint inBlock = address % uint(unitsPerBlock);
    if (xorZ != 0)
    {
        block ^= 24u;
    }

    uint rel = (block - uint(basePointer)) & 16383u;
    uint page = rel >> 5;
    if (page >= uint(pagesAcross * pagesDown))
    {
        return false;
    }

    uint i = (rel & 31u) * uint(unitsPerBlock) + inBlock;
    uint v = texelFetch(inv, ivec2(int(i & 1023u), int(i >> 10)), 0).r;
    p = ivec2(int(page % uint(pagesAcross)) * pageWidth + int(v & 0xFFFFu), int(page / uint(pagesAcross)) * pageHeight + int(v >> 16));
    return true;
}
)";

constexpr const char* MemRectVertexSource = R"(#version 330 core
uniform vec4 rect;
uniform vec2 viewSize;
void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec2 at = mix(rect.xy, rect.zw, corner);
    gl_Position = vec4(at / viewSize * 2.0 - 1.0, 0.0, 1.0);
}
)";

// A target's pixels written into memory (the words of the pages drawn over; the target's sample point of each pixel)
constexpr const char* MemFromTargetSource = R"(
uniform sampler2D target;
uniform int targetScale;
// 0 colour 32, 1 colour 16, 2 depth
uniform int storage;
uniform int depthReverse;
out uint word;
uint Unit(uint address)
{
    ivec2 p;
    if (!UnitPixel(address, p))
    {
        discard;
    }

    vec4 c = texelFetch(target, p * targetScale, 0);
    if (storage == 2)
    {
        float z = floor(c.r * 4294967296.0 + 0.5);
        uint v = uint(min(z, 4294967040.0));
        v = depthReverse != 0 ? 0xFFFFFFFFu - v : v;
        return unitBits == 32 ? v : v & 0xFFFFu;
    }

    uvec4 b = uvec4(floor(c * 255.0 + 0.5));
    if (storage == 0)
    {
        return b.r | b.g << 8 | b.b << 16 | b.a << 24;
    }

    return (b.r >> 3) | (b.g >> 3) << 5 | (b.b >> 3) << 10 | (b.a >= 128u ? 0x8000u : 0u);
}
void main()
{
    uint w = uint(gl_FragCoord.y) * 1024u + uint(gl_FragCoord.x);
    uint units = 1u << uint(unitShift);
    uint value = 0u;
    for (uint k = 0u; k < units; k++)
    {
        value |= Unit((w << uint(unitShift)) | k) << (k * uint(unitBits));
    }

    word = value;
}
)";

// Memory's pixels written into a target (the pages asked for only: a page mask)
constexpr const char* TargetFromMemSource = R"(
uniform int targetScale;
uniform int storage;
uniform int depthReverse;
uniform uint pageMask[16];
out vec4 colour;
void main()
{
    ivec2 p = ivec2(floor(gl_FragCoord.xy)) / targetScale;
    uint address = UnitAddress(p);
    uint page = (address >> uint(unitShift)) >> 11;
    if ((pageMask[page >> 5] & (1u << (page & 31u))) == 0u)
    {
        discard;
    }

    uint v = MemUnit(address);
    if (storage == 2)
    {
        gl_FragDepth = float(depthReverse != 0 ? 0xFFFFFFFFu - v : v) * (1.0 / 4294967296.0);
        colour = vec4(0.0);
        return;
    }

    gl_FragDepth = 0.0;
    if (storage == 0)
    {
        colour = vec4(uvec4(v & 255u, (v >> 8) & 255u, (v >> 16) & 255u, v >> 24)) / 255.0;
    }
    else
    {
        colour = vec4(float((v & 31u) << 3), float(((v >> 5) & 31u) << 3), float(((v >> 10) & 31u) << 3), (v & 0x8000u) != 0u ? 128.0 : 0.0) / 255.0;
    }
}
)";

// A texture level decoded from memory's copy as the software GS decodes it (DecodeTexel: TEXA, the CLUT), into a texture's
// level: RGBA, the PS2's alpha (0x80 = 1.0) in the bytes
constexpr const char* DecodeSource = R"(
uniform int psm;
uniform usampler2D clut;
uniform int cpsm;
uniform int csa;
uniform uint ta0;
uniform uint ta1;
uniform int aem;
out vec4 colour;
uint Expand16(uint c)
{
    uint alpha = (c & 0x8000u) != 0u ? ta1 : ((aem != 0 && (c & 0x7FFFu) == 0u) ? 0u : ta0);
    return (c & 31u) << 3 | ((c >> 5) & 31u) << 11 | ((c >> 10) & 31u) << 19 | alpha << 24;
}
uint ClutEntry(int i)
{
    return texelFetch(clut, ivec2(i & 511, 0), 0).r;
}
uint ClutColour(uint index)
{
    if (cpsm == 0 || cpsm == 1)
    {
        int slot = int((index + uint(csa & 15) * 16u) & 255u);
        uint c = ClutEntry(slot) | ClutEntry(slot + 256) << 16;
        if (cpsm == 1)
        {
            uint alpha = (aem != 0 && (c & 0xFFFFFFu) == 0u) ? 0u : ta0;
            c = (c & 0xFFFFFFu) | alpha << 24;
        }

        return c;
    }

    return Expand16(ClutEntry(int((index + uint(csa) * 16u) & 511u)));
}
void main()
{
    ivec2 p = ivec2(floor(gl_FragCoord.xy));
    uint v = MemUnit(UnitAddress(p));
    uint c;
    // PSMCT32 0x00, CT24 0x01, CT16 0x02, CT16S 0x0A, T8 0x13, T4 0x14, T8H 0x1B, T4HL 0x24, T4HH 0x2C, Z32 0x30, Z24 0x31,
    // Z16 0x32, Z16S 0x3A
    if (psm == 0x00 || psm == 0x30)
    {
        c = v;
    }
    else if (psm == 0x01 || psm == 0x31)
    {
        c = v & 0xFFFFFFu;
        uint alpha = (aem != 0 && c == 0u) ? 0u : ta0;
        c = c | alpha << 24;
    }
    else if (psm == 0x02 || psm == 0x0A || psm == 0x32 || psm == 0x3A)
    {
        c = Expand16(v);
    }
    else if (psm == 0x1B)
    {
        c = ClutColour(v >> 24);
    }
    else if (psm == 0x24)
    {
        c = ClutColour((v >> 24) & 15u);
    }
    else if (psm == 0x2C)
    {
        c = ClutColour(v >> 28);
    }
    else
    {
        c = ClutColour(v);
    }

    colour = vec4(uvec4(c & 255u, (c >> 8) & 255u, (c >> 16) & 255u, c >> 24)) / 255.0;
}
)";

// Copies by drawing (a blit makes Apple's OpenGL submit its work and wait): a texture's texels at the same places
constexpr const char* CopyColourSource = R"(#version 330 core
uniform sampler2D source;
out vec4 colour;
void main()
{
    colour = texelFetch(source, ivec2(gl_FragCoord.xy), 0);
}
)";

constexpr const char* CopyWordSource = R"(#version 330 core
uniform usampler2D source;
out uint word;
void main()
{
    word = texelFetch(source, ivec2(gl_FragCoord.xy), 0).r;
}
)";

struct Program
{
    GLuint id = 0;
    GLint viewSize;
    GLint viewOrigin;
    GLint depthCopy;
    GLint nativeZtst;
    GLint targetScale;
    GLint zMax;
    GLint zMaxU;
    GLint depthReverse;
    GLint pointSize;
    GLint tex;
    GLint texSize;
    GLint texActual;
    GLint texSnap;
    GLint aref;
    GLint fogColour;
    GLint ta0;
    GLint ta1;
    GLint aem;
    GLint lodK;
    GLint lodL;
    GLint lodMax;
    GLint dimx;
    GLint scale;
    GLint destination;
    GLint blendOn;
    GLint blendA;
    GLint blendB;
    GLint blendC;
    GLint blendD;
    GLint blendFix;
    GLint pabe;
    GLint colclamp;
    GLint datm;
    GLint frameMask;
};

struct CachedTexture
{
    GLuint id = 0;
    u32 width = 0;
    u32 height = 0;
    u32 levels = 0;
    u32 versions[7] = {};
    u32 tilesVersions[7] = {};
    // Level 0's 8x8 tiles uploaded at its version (the decoder decodes what draws reach; the rest isn't read)
    std::vector<u8> tiles;
    // The replacement (a pack's texture) looked for at level 0's version: its texture (0: none), whether it has mipmaps
    u32 replacementVersion = ~0u;
    GLuint replacement = 0;
    bool replacementMipmaps = false;
    bool dumped = false;
};

// A texture decoded on the GPU: its levels, each level's 8x8 tiles' decode epochs (0: not decoded; good while every page under
// the tile is older and no target holds a newer copy) and pages
struct GpuTextureKey
{
    u32 psm;
    u32 cpsm;
    u32 csa;
    u32 clutVersion;
    u64 texa;
    u32 levels;
    u32 bp[7];
    u32 bw[7];
    u32 width;
    u32 height;
    bool operator==(const GpuTextureKey&) const = default;
};

struct GpuTextureKeyHash
{
    size_t operator()(const GpuTextureKey& k) const
    {
        u64 h = 1469598103934665603ull;
        auto mix = [&](u64 v) { h = (h ^ v) * 1099511628211ull; };
        mix(k.psm);
        mix(k.cpsm | k.csa << 8);
        mix(k.clutVersion);
        mix(k.texa);
        mix(k.levels);
        for (u32 i = 0; i < k.levels; i++)
        {
            mix(k.bp[i] | static_cast<u64>(k.bw[i]) << 32);
        }

        mix(k.width | static_cast<u64>(k.height) << 32);
        return static_cast<size_t>(h);
    }
};

struct GpuTexture
{
    GLuint id = 0;
    struct Level
    {
        u32 width;
        u32 height;
        u32 tilesWide;
        u32 tilesHigh;
        std::vector<u32> tileEpoch;
        std::vector<u16> tilePage;
    } levels[7];
    u32 levelCount = 0;
    Gs::PageSet pages;
    // Every tile decoded, checked good at this epoch
    bool complete = false;
    u32 checkedEpoch = 0;
    // Its oldest tile's decode, when it's complete: no page of it changed since then (and none a target's newer), every tile
    // is still good
    u32 oldestTile = 0;
    // A replacement (a pack's texture) looked for when the texture was last decoded whole from the CPU's data; 0: none
    GLuint replacement = 0;
    // The texture drawn with (its own, its replacement, or a decode of the same content made for another place)
    GLuint shown = 0;
    // Shown from the content cache: good while every page is older than this and no target holds a newer copy (0: not)
    u32 contentEpoch = 0;
    std::vector<u16> pageList;
};

// A texture's content decoded once (the game streams its textures through the same places of GS memory every frame): by the
// hash of its GS blocks, format, size, CLUT and TEXA
struct ContentTexture
{
    GLuint id = 0;
    GLuint replacement = 0;
};

GLuint CompileShader(GLenum kind, const std::string& source)
{
    Gl& gl = GetGl();
    GLuint shader = gl.CreateShader(kind);
    const char* text = source.c_str();
    gl.ShaderSource(shader, 1, &text, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048];
        gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "graphics: hardware shader: %s\n", log);
        std::abort();
    }

    return shader;
}

GLuint LinkProgram(GLuint vertex, GLuint fragment, bool dualSource)
{
    Gl& gl = GetGl();
    GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vertex);
    gl.AttachShader(program, fragment);
    if (dualSource)
    {
        gl.BindFragDataLocationIndexed(program, 0, 0, "outColour");
        gl.BindFragDataLocationIndexed(program, 0, 1, "outFactor");
    }

    gl.LinkProgram(program);
    GLint linked = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[2048];
        gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "graphics: hardware program: %s\n", log);
        std::abort();
    }

    return program;
}

class HwGs : public Gs::Backend
{
public:
    explicit HwGs(Gs::Gs& gs, u32 scale) : gs_(gs), scale_(scale) {}
    ~HwGs() override;

    bool Init();
    bool Draw(Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count) override;
    // A primitive drawn as the one before it was (its state, targets and texture as they were: HwGs::DrawLikeLast), and the
    // end of every primitive's drawing (its batch, its vertexes, the pages it draws)
    bool DrawLikeLast(const Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count, Rect rect);
    void AddPrimitive(const Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, const Rect& rect, Target* colour, Target* depth,
                      s32 n, bool shaderBlend, const Gs::PageSet& drawnPages, const Gs::PageSet& depthPages);
    void BeforeCpuRead(const Gs::PageSet& pages) override;
    void BeforeCpuWrite(const Gs::PageSet& pages) override { BeforeCpuRead(pages); }
    void Flush() override;
    bool PagesGpuNewer(const Gs::PageSet& pages) override
    {
        ProcessWrites();
        return (pages & (cpuStale_ | newestMask_)).any();
    }
    bool DebugPixel(u32 psm, u32 bp, u32 bw, u32 x, u32 y, u32* value) override;

    void SetScale(u32 scale);
    u32 scale() const { return scale_; }
    bool DisplayTexture(NativeGraphics::DisplayTexture* display, Target** shown, Rect* rect);
    bool ReadDisplay(Gs::DisplayImage& image);
    bool CopyDisplay(u32 slot, GLsync drawn, NativeGraphics::ShownTexture* shown);
    void MakeCurrent();

    HardwareStats stats = {};

private:
    Target* TargetFor(u32 bp, u32 bw, u32 psm, u32 height, bool frame = false);
    void DestroyTarget(Target* target);
    void ProcessWrites();
    void Download(Target* target);
    void Upload(Target* target, const Gs::PageSet& pages);
    void EnsureValid(Target* target, const Gs::PageSet& pages);
    void MarkDrawn(Target* target, const Gs::PageSet& pages);
    void RectPages(const Target* target, const Rect& rect, Gs::PageSet& pages) const;
    bool Fallback(const Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count, const Rect& rect, const char* why);
    const Program& ProgramFor(u32 bits);
    GLuint FramebufferFor(Target* colour, Target* depth);
    GLuint CopyOf(Target* target);
    GLuint CopyTarget(Target* target, const Rect* region = nullptr);
    Target* ScratchStencil(Target* colour);
    void Staging(u32 width, u32 height, bool depth, GLuint* texture, GLuint* fbo);

    Gs::Gs& gs_;
    u32 scale_;
    // The draw making a depth target has its depth in the top half (the target is made reversed)
    bool nextDepthHigh_ = false;
    bool nativeSmall_ = std::getenv("TWIN_HW_NATIVE_SMALL") == nullptr || std::getenv("TWIN_HW_NATIVE_SMALL")[0] != '0';
    std::vector<std::unique_ptr<Target>> targets_;
    Target* newest_[Gs::PageCount] = {};
    // The pages a target holds newer than local memory (newest_ not null)
    Gs::PageSet newestMask_;
    // Each texture level's whole pages (by base, width, format and size)
    struct LevelKeyHash
    {
        size_t operator()(const std::tuple<u32, u32, u32, u32, u32>& k) const
        {
            u64 h = std::get<0>(k) * 0x9E3779B97F4A7C15ull;
            h ^= (static_cast<u64>(std::get<1>(k)) << 20 | std::get<2>(k) << 8) + (h << 6) + (h >> 2);
            h ^= (static_cast<u64>(std::get<3>(k)) << 16 | std::get<4>(k)) + (h << 6) + (h >> 2);
            return static_cast<size_t>(h);
        }
    };
    std::unordered_map<std::tuple<u32, u32, u32, u32, u32>, Gs::PageSet, LevelKeyHash> levelPages_;
    std::map<std::pair<Target*, Target*>, GLuint> framebuffers_;
    std::unordered_map<u32, Program> programs_;
    std::unordered_map<const void*, CachedTexture> textures_;
    GLuint vertexArray_ = 0;
    GLuint vertexBuffer_ = 0;
    GLuint depthProgram_ = 0;
    GLuint mergeProgram_ = 0;
    GLuint mergeTexture_ = 0;
    GLuint mergeFbo_ = 0;
    u32 mergeWidth_ = 0;
    u32 mergeHeight_ = 0;
    GLuint emptyArray_ = 0;
    // Staging textures by size and kind (1x), and the copy of a target sampled while it's drawn into
    std::map<std::tuple<u32, u32, bool>, std::pair<GLuint, GLuint>> staging_;
    std::map<std::tuple<u32, u32>, std::pair<GLuint, GLuint>> copies_;
    BatchKey key_ = {};
    bool batching_ = false;
    // The last primitive drawn the whole way, for the next to be drawn the same way while nothing it depended on has changed:
    // the GS's state (its registers' generation, the CLUT), the targets (their list's changes), the settings, and the texture
    // DecodeOnGpu found (good as it was). Its key is the batch's (key_)
    struct LastDraw
    {
        bool valid = false;
        u64 generation = 0;
        u32 clut = 0;
        u64 targetsVersion = 0;
        int filterMode = 0;
        int ditherMode = 0;
        Target* colour = nullptr;
        Target* depth = nullptr;
        const GpuTexture* texture = nullptr;
        GLuint textureId = 0;
        bool shaderBlend = false;
    } last_;
    // Counts the targets made and destroyed
    u64 targetsVersion_ = 0;
    std::vector<HwVertex> vertexes_;
    // A batch blended by the shader: its primitives' rectangles (one overlapping an earlier one starts a new batch, as the
    // destination copy is from before the batch)
    std::vector<Rect> batchRects_;
    // Stencil buffers for the destination alpha test of draws without a depth buffer, by colour target
    std::map<Target*, std::unique_ptr<Target>> scratchStencils_;
    GLuint dateProgram_ = 0;
    // GS local memory on the GPU, the layouts' tables (32, 16, 16S, 8, 4 bit), the passes between it and the targets
    GLuint memTexture_ = 0;
    // The buffer CPU writes go to memory's copy through (the GPU copies from it: no wait for the texture's earlier use)
    GLuint uploadBuffer_ = 0;
    GLuint memFbo_ = 0;
    GLuint fwdTables_[5] = {};
    GLuint invTables_[5] = {};
    GLuint memFromTargetProgram_ = 0;
    GLuint copyColourProgram_ = 0;
    GLuint copyWordProgram_ = 0;
    // A rectangle of a texture copied into the framebuffer bound (its size given) at the same places
    void CopyRect(GLuint program, GLuint source, f32 viewWidth, f32 viewHeight, s32 x0, s32 y0, s32 x1, s32 y1);
    GLuint targetFromMemProgram_ = 0;
    // Each page's state: memory's copy out of date, local memory's (the CPU's) out of date, the shadow of what memory's copy
    // holds good (for re-uploads of the same bytes), and when its content last changed
    Gs::PageSet memStale_;
    // Pages whose content memory's copy has from a target at a higher scale than 1 (read back at the PS2's resolution)
    Gs::PageSet scaledPages_;
    // A depth target's copy as floats (the shader's native depth test reads it), by size
    std::map<std::pair<u32, u32>, std::pair<GLuint, GLuint>> depthCopies_;
    Gs::PageSet cpuStale_;
    Gs::PageSet shadowGood_;
    std::vector<u8> shadow_;
    u32 pageEpoch_[Gs::PageCount] = {};
    u32 epoch_ = 1;
    void InitMemory();
    // Textures decoded on the GPU, the decode's program and framebuffer, the CLUT as a texture
    std::unordered_map<GpuTextureKey, GpuTexture, GpuTextureKeyHash> gpuTextures_;
    // The previous draw's texture: its key, and the GS state's generation and CLUT it was found for (the same again: nothing to
    // make or look up)
    GpuTextureKey lastTextureKey_ = {};
    GpuTexture* lastTexture_ = nullptr;
    u64 lastTextureGeneration_ = 0;
    u32 lastTextureClut_ = 0;
    GLuint decodeProgram_ = 0;
    GLuint decodeFbo_ = 0;
    // TWIN_HW_STATS: the render passes begun (a draw into another framebuffer or attachment than the last), by cause
    u64 passIdentity_ = 0;
    std::map<std::string, u64> passCounts_;
    u64 passFrames_ = 0;
    // TWIN_HW_CONVERSION_TRACE=FIRST,LAST: each conversion of the draws numbered FIRST to LAST, with its pages
    void TraceConversion(const char* direction, const Target* target, const Gs::PageSet& pages)
    {
        static const char* range = std::getenv("TWIN_HW_CONVERSION_TRACE");
        if (range == nullptr)
        {
            return;
        }

        unsigned long long first = 0, last = 0;
        std::sscanf(range, "%llu,%llu", &first, &last);
        if (gs_.primitivesDrawn < first || gs_.primitivesDrawn > last)
        {
            return;
        }

        std::fprintf(stderr, "conversion: #%llu %s %x/%u class %u (%s) pages", static_cast<unsigned long long>(gs_.primitivesDrawn), direction,
                     target->bp, target->bw, target->cls, cpuReason);
        for (u32 page = 0; page < Gs::PageCount; page++)
        {
            if (pages.test(page) && target->pages.test(page))
            {
                const Target* held = newest_[page];
                std::fprintf(stderr, " %x(newest %x/%u/%u)", page, held ? held->bp : 0, held ? held->bw : 0, held ? held->cls : 99);
            }
        }

        std::fprintf(stderr, "\n");
    }

    void Pass(u64 identity, const char* cause)
    {
        static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
        if (counting && identity != passIdentity_)
        {
            passCounts_[cause]++;
        }

        passIdentity_ = identity;
    }
    GLuint clutTexture_ = 0;
    u32 clutUploaded_ = 0;
    // Rings of textures written by the CPU (a texture the GPU may still read is never changed: the driver would wait for it):
    // CLUTs, and staging for memory's pages (copied into memory's copy on the GPU)
    static constexpr u32 ClutRing = 64;
    GLuint clutRing_[ClutRing] = {};
    u32 clutNext_ = 0;
    static constexpr u32 StagingRing = 8;
    GLuint memStaging_[StagingRing] = {};
    GLuint stagingFbo_[StagingRing] = {};
    u32 stagingNext_ = 0;
    // Replacement images' textures, made once each
    std::map<NativeGraphics::ReplacementKey, GLuint> replacementTextures_;
    GLuint DecodeOnGpu(const Gs::Gs::DrawState& state, s32 left, s32 top, s32 right, s32 bottom);
    void DecodePasses(const Gs::Gs::DrawState& state, GLuint texture, const Rect* rects, u32 levels);
    u64 ContentHash(const Gs::Gs::DrawState& state);
    GLuint MakeLevels(const Gs::Gs::DrawState& state);
    std::unordered_map<u64, ContentTexture> contents_;
    void SyncMem(const Gs::PageSet& pages);
    void SyncCpu(const Gs::PageSet& pages);
    void MemFromTarget(Target* target, const Gs::PageSet& pages);
    void TargetFromMem(Target* target, const Gs::PageSet& pages);
    void UseLayout(GLuint program, u32 psm, u32 bp, u32 bw, u32 pagesDown);
    GLuint resolveProgram_ = 0;
    std::map<std::pair<Target*, Target*>, GLuint> accumFramebuffers_;
    void Resolve(Target* target);
    GLuint AccumFramebufferFor(Target* colour, Target* depth);
    const char* downloadReason_ = "";
    std::vector<u8> readback_;
    std::vector<u32> upload_;
    std::vector<f32> uploadDepth_;
    std::map<std::string, bool> reported_;
    SDL_Window* ownWindow_ = nullptr;
    SDL_GLContext context_ = nullptr;
    SDL_Window* window_ = nullptr;
    // The shown picture's copies for the render thread's hand-over
    struct ShownCopy
    {
        GLuint texture = 0;
        GLuint fbo = 0;
        u32 width = 0;
        u32 height = 0;
    };
    ShownCopy shownCopies_[3];
};

HwGs* g_Hw = nullptr;
// Whether g_Hw is there, for the other threads (the UI's settings)
std::atomic<bool> g_HwActive{false};
// The draws' viewport (see Flush)
constexpr GLint BigViewSize = 16384;
constexpr GLint BigViewOrigin = 4096;
// Where a vertex goes past x / 16 * scale + 0.5, in pixels (TWIN_HW_VERTEX_OFFSET, 256ths)
f32 VertexNudge()
{
    static const f32 nudge = std::getenv("TWIN_HW_VERTEX_OFFSET") != nullptr ? static_cast<f32>(std::atof(std::getenv("TWIN_HW_VERTEX_OFFSET"))) / 256.0f
                                                                          : 0.0f;
    return nudge;
}
int g_DitherMode = 2;
int g_FilterMode = 0;

HwGs::~HwGs()
{
    gs_.SetBackend(nullptr);
}

void HwGs::MakeCurrent()
{
    if (SDL_GL_GetCurrentContext() != context_)
    {
        SDL_GL_MakeCurrent(window_, context_);
    }
}

bool HwGs::Init()
{
    if (!RenderContext(&window_, &context_) && !PresenterContext(&window_, &context_))
    {
        // No window: one of its own, hidden
        if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            std::fprintf(stderr, "graphics: hardware renderer: no video: %s\n", SDL_GetError());
            return false;
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        ownWindow_ = SDL_CreateWindow("graphics", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (ownWindow_ == nullptr)
        {
            std::fprintf(stderr, "graphics: hardware renderer: no window: %s\n", SDL_GetError());
            return false;
        }

        context_ = SDL_GL_CreateContext(ownWindow_);
        if (context_ == nullptr)
        {
            std::fprintf(stderr, "graphics: hardware renderer: no OpenGL 3.3: %s\n", SDL_GetError());
            return false;
        }

        window_ = ownWindow_;
    }

    MakeCurrent();
    if (!LoadGl())
    {
        return false;
    }

    Gl& gl = GetGl();
    gl.GenVertexArrays(1, &vertexArray_);
    gl.GenBuffers(1, &vertexBuffer_);
    gl.BindVertexArray(vertexArray_);
    gl.BindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(HwVertex), reinterpret_cast<void*>(offsetof(HwVertex, x)));
    gl.VertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(HwVertex), reinterpret_cast<void*>(offsetof(HwVertex, z)));
    gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(HwVertex), reinterpret_cast<void*>(offsetof(HwVertex, r)));
    gl.VertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(HwVertex), reinterpret_cast<void*>(offsetof(HwVertex, s)));
    gl.VertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(HwVertex), reinterpret_cast<void*>(offsetof(HwVertex, fog)));
    for (GLuint i = 0; i < 5; i++)
    {
        gl.EnableVertexAttribArray(i);
    }

    gl.GenVertexArrays(1, &emptyArray_);
    GLuint vertex = CompileShader(GL_VERTEX_SHADER, DepthVertexSource);
    GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, DepthFragmentSource);
    depthProgram_ = LinkProgram(vertex, fragment, false);
    resolveProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MergeVertexSource), CompileShader(GL_FRAGMENT_SHADER, ResolveFragmentSource),
                                  false);
    dateProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MergeVertexSource), CompileShader(GL_FRAGMENT_SHADER, DateFragmentSource),
                               false);
    mergeProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MergeVertexSource), CompileShader(GL_FRAGMENT_SHADER, MergeFragmentSource),
                                false);
    InitMemory();
    gs_.SetBackend(this);
    return true;
}

const Program& HwGs::ProgramFor(u32 bits)
{
    auto found = programs_.find(bits);
    if (found != programs_.end())
    {
        return found->second;
    }

    std::string header = "#version 330 core\n";
    auto define = [&](const char* name, u32 value) { header += "#define " + std::string(name) + " " + std::to_string(value) + "\n"; };
    define("TEXTURED", (bits & BitTextured) ? 1 : 0);
    define("TFX", (bits >> BitTfxShift) & 3);
    define("TCC", (bits & BitTcc) ? 1 : 0);
    define("FOG", (bits & BitFog) ? 1 : 0);
    define("ATST", (bits >> BitAtstShift) & 7);
    define("ALPHATEST", (bits & BitAlphaTest) ? 1 : 0);
    define("UVMODE", (bits & BitUv) ? 1 : 0);
    define("TEXALPHA", (bits >> BitTexAlphaShift) & 3);
    define("FRAME16", (bits & BitFrame16) ? 1 : 0);
    define("DITHER", (bits & BitDither) ? 1 : 0);
    header += "#define SAMPLE_SHIFT " + std::to_string(-VertexNudge()) + "\n";
    define("FBA", (bits & BitFba) ? 1 : 0);
    define("MIPMAP", (bits & BitMipmap) ? 1 : 0);
    define("LODCONST", (bits & BitLodConstant) ? 1 : 0);
    define("FAILPASS", (bits & BitFailPass) ? 1 : 0);
    define("DEPTH", (bits & BitDepth) ? 1 : 0);
    define("SHADERBLEND", (bits & BitShaderBlend) ? 1 : 0);
    define("DATE", (bits & BitDate) ? 1 : 0);
    define("FRAME24", (bits & BitFrame24) ? 1 : 0);
    define("WRAPADD", (bits & BitWrapAdd) ? 1 : 0);
    define("NATIVEDEPTH", (bits & BitNativeDepth) ? 1 : 0);
    GLuint vertex = CompileShader(GL_VERTEX_SHADER, VertexSource);
    GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, header + FragmentSource);
    Gl& gl = GetGl();
    Program p;
    p.id = LinkProgram(vertex, fragment, true);
    gl.DeleteShader(vertex);
    gl.DeleteShader(fragment);
    p.viewSize = gl.GetUniformLocation(p.id, "viewSize");
    p.viewOrigin = gl.GetUniformLocation(p.id, "viewOrigin");
    p.depthCopy = gl.GetUniformLocation(p.id, "depthCopy");
    p.nativeZtst = gl.GetUniformLocation(p.id, "nativeZtst");
    p.targetScale = gl.GetUniformLocation(p.id, "targetScale");
    p.zMax = gl.GetUniformLocation(p.id, "zMax");
    p.zMaxU = gl.GetUniformLocation(p.id, "zMaxU");
    p.depthReverse = gl.GetUniformLocation(p.id, "depthReverse");
    p.pointSize = gl.GetUniformLocation(p.id, "pointSize");
    p.tex = gl.GetUniformLocation(p.id, "tex");
    p.texSize = gl.GetUniformLocation(p.id, "texSize");
    p.texActual = gl.GetUniformLocation(p.id, "texActual");
    p.texSnap = gl.GetUniformLocation(p.id, "texSnap");
    p.aref = gl.GetUniformLocation(p.id, "aref");
    p.fogColour = gl.GetUniformLocation(p.id, "fogColour");
    p.ta0 = gl.GetUniformLocation(p.id, "ta0");
    p.ta1 = gl.GetUniformLocation(p.id, "ta1");
    p.aem = gl.GetUniformLocation(p.id, "aem");
    p.lodK = gl.GetUniformLocation(p.id, "lodK");
    p.lodL = gl.GetUniformLocation(p.id, "lodL");
    p.lodMax = gl.GetUniformLocation(p.id, "lodMax");
    p.dimx = gl.GetUniformLocation(p.id, "dimx");
    p.scale = gl.GetUniformLocation(p.id, "scale");
    p.destination = gl.GetUniformLocation(p.id, "destination");
    p.blendOn = gl.GetUniformLocation(p.id, "blendOn");
    p.blendA = gl.GetUniformLocation(p.id, "blendA");
    p.blendB = gl.GetUniformLocation(p.id, "blendB");
    p.blendC = gl.GetUniformLocation(p.id, "blendC");
    p.blendD = gl.GetUniformLocation(p.id, "blendD");
    p.blendFix = gl.GetUniformLocation(p.id, "blendFix");
    p.pabe = gl.GetUniformLocation(p.id, "pabe");
    p.colclamp = gl.GetUniformLocation(p.id, "colclamp");
    p.datm = gl.GetUniformLocation(p.id, "datm");
    p.frameMask = gl.GetUniformLocation(p.id, "frameMask");
    return programs_[bits] = p;
}

void HwGs::Staging(u32 width, u32 height, bool depth, GLuint* texture, GLuint* fbo)
{
    auto key = std::make_tuple(width, height, depth);
    auto found = staging_.find(key);
    if (found != staging_.end())
    {
        *texture = found->second.first;
        *fbo = found->second.second;
        return;
    }

    Gl& gl = GetGl();
    gl.GenTextures(1, texture);
    gl.BindTexture(GL_TEXTURE_2D, *texture);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl.GenFramebuffers(1, fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, *fbo);
    Pass(*fbo, "staging");
    if (depth)
    {
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH32F_STENCIL8), static_cast<GLsizei>(width),
                      static_cast<GLsizei>(height), 0, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, nullptr);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, *texture, 0);
        gl.DrawBuffer(GL_NONE);
        gl.ReadBuffer(GL_NONE);
    }
    else
    {
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
    }

    staging_[key] = {*texture, *fbo};
}

Target* HwGs::TargetFor(u32 bp, u32 bw, u32 psm, u32 height, bool frame)
{
    u32 cls = frame ? FrameClassOf(psm) : ClassOf(psm);
    s32 pageWidth = 0;
    s32 pageHeight = 0;
    PageSize(StoredPsm(cls), &pageWidth, &pageHeight);
    height = std::min<u32>((height + pageHeight - 1) / pageHeight * pageHeight, 2048);
    for (auto& known : targets_)
    {
        if (known->bp == bp && known->bw == bw && known->cls == cls)
        {
            if (known->height >= height)
            {
                return known.get();
            }

            // Taller than it was: a new one is made (the old one's newest pages go to memory's copy as it goes)
            height = std::max(height, known->height);
            DestroyTarget(known.get());
            break;
        }
    }

    auto made = std::make_unique<Target>();
    Target& t = *made;
    t.bp = bp;
    t.bw = bw;
    t.cls = cls;
    t.psm = StoredPsm(cls);
    t.depth = IsDepthClass(cls);
    t.reversed = cls == ClassDepth32 && nextDepthHigh_;
    t.width = bw * 64;
    t.height = height;
    // Native scaling: buffers narrower than the screen (the effects' half resolution buffers) stay at the PS2's resolution and
    // are filtered up where the scene reads them, as PCSX2's native scaling does (their sub-pixels would otherwise pick up the
    // frame's pixel patterns)
    t.scale = (nativeSmall_ && bw < 8) ? 1 : scale_;
    t.aligned = (bp % 32) == 0;
    t.pixels.resize(Gs::PageCount);
    t.rects.assign(Gs::PageCount, Rect{0x7FFF, 0x7FFF, -1, -1});
    for (u32 y = 0; y < t.height; y++)
    {
        for (u32 x = 0; x < t.width; x++)
        {
            u32 page = Gs::PixelPage(t.psm, x, y, bp, bw) & (Gs::PageCount - 1);
            t.pages.set(page);
            t.pixels[page].push_back(y << 11 | x);
            Rect& r = t.rects[page];
            r.left = std::min<s32>(r.left, static_cast<s32>(x));
            r.top = std::min<s32>(r.top, static_cast<s32>(y));
            r.right = std::max<s32>(r.right, static_cast<s32>(x));
            r.bottom = std::max<s32>(r.bottom, static_cast<s32>(y));
        }
    }

    Gl& gl = GetGl();
    gl.GenTextures(1, &t.texture);
    gl.BindTexture(GL_TEXTURE_2D, t.texture);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    GLsizei w = static_cast<GLsizei>(t.width * t.scale);
    GLsizei h = static_cast<GLsizei>(t.height * t.scale);
    gl.GenFramebuffers(1, &t.fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    if (t.depth)
    {
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH32F_STENCIL8), w, h, 0, GL_DEPTH_STENCIL,
                      GL_FLOAT_32_UNSIGNED_INT_24_8_REV, nullptr);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, t.texture, 0);
        gl.DrawBuffer(GL_NONE);
        gl.ReadBuffer(GL_NONE);
    }
    else
    {
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
    }

    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        std::fprintf(stderr, "graphics: hardware renderer: a target's framebuffer isn't complete\n");
    }

    targets_.push_back(std::move(made));
    targetsVersion_++;
    return targets_.back().get();
}

void HwGs::DestroyTarget(Target* target)
{
    Flush();
    {
        Gs::PageSet mine;
        for (u32 page = 0; page < Gs::PageCount; page++)
        {
            if (newest_[page] == target)
            {
                mine.set(page);
            }
        }

        if (mine.any())
        {
            SyncMem(mine);
        }
    }

    Gl& gl = GetGl();
    for (auto it = framebuffers_.begin(); it != framebuffers_.end();)
    {
        if (it->first.first == target || it->first.second == target)
        {
            gl.DeleteFramebuffers(1, &it->second);
            it = framebuffers_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (newest_[page] == target)
        {
            newest_[page] = nullptr;
            newestMask_.reset(page);
        }
    }

    auto scratch = scratchStencils_.find(target);
    if (scratch != scratchStencils_.end())
    {
        Target* stencil = scratch->second.get();
        for (auto it = framebuffers_.begin(); it != framebuffers_.end();)
        {
            if (it->first.second == stencil)
            {
                gl.DeleteFramebuffers(1, &it->second);
                it = framebuffers_.erase(it);
            }
            else
            {
                ++it;
            }
        }

        gl.DeleteTextures(1, &stencil->texture);
        scratchStencils_.erase(scratch);
    }

    for (auto it = accumFramebuffers_.begin(); it != accumFramebuffers_.end();)
    {
        if (it->first.first == target || it->first.second == target)
        {
            gl.DeleteFramebuffers(1, &it->second);
            it = accumFramebuffers_.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (target->accum != 0)
    {
        gl.DeleteTextures(1, &target->accum);
    }

    gl.DeleteFramebuffers(1, &target->fbo);
    gl.DeleteTextures(1, &target->texture);
    targets_.erase(std::remove_if(targets_.begin(), targets_.end(), [&](const auto& t) { return t.get() == target; }),
                   targets_.end());
    targetsVersion_++;
}

GLuint HwGs::FramebufferFor(Target* colour, Target* depth)
{
    if (depth == nullptr)
    {
        return colour->fbo;
    }

    auto key = std::make_pair(colour, depth);
    auto found = framebuffers_.find(key);
    if (found != framebuffers_.end())
    {
        return found->second;
    }

    Gl& gl = GetGl();
    GLuint fbo = 0;
    gl.GenFramebuffers(1, &fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour->texture, 0);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depth->texture, 0);
    framebuffers_[key] = fbo;
    return fbo;
}

void HwGs::RectPages(const Target* target, const Rect& rect, Gs::PageSet& pages) const
{
    if (target->aligned)
    {
        s32 pageWidth = 0;
        s32 pageHeight = 0;
        PageSize(target->psm, &pageWidth, &pageHeight);
        u32 base = target->bp / 32;
        // The bounds worked out once (the compiler can't: the page set might be the rectangle as far as it knows), every
        // primitive asks this
        const s32 firstRow = rect.top / pageHeight;
        const s32 lastRow = rect.bottom / pageHeight;
        const s32 firstColumn = rect.left / pageWidth;
        const s32 lastColumn = rect.right / pageWidth;
        const u32 bw = target->bw;
        for (s32 row = firstRow; row <= lastRow; row++)
        {
            const u32 rowBase = base + static_cast<u32>(row) * bw;
            for (s32 column = firstColumn; column <= lastColumn; column++)
            {
                pages.set((rowBase + static_cast<u32>(column)) & (Gs::PageCount - 1));
            }
        }

        return;
    }

    Gs::PagesOfRect(target->psm, target->bp, target->bw, rect.left, rect.top, rect.right, rect.bottom, pages);
}


namespace
{
// A layout's family (0 32 bit, 1 16, 2 16S, 3 8, 4 4) and whether it's a Z format's (its blocks XOR 0x18)
void LayoutOf(u32 psm, u32* family, bool* xorZ)
{
    *xorZ = false;
    switch (psm)
    {
    case Gs::PSMZ32:
    case Gs::PSMZ24:
        *xorZ = true;
        *family = 0;
        break;
    case Gs::PSMCT16:
        *family = 1;
        break;
    case Gs::PSMZ16:
        *xorZ = true;
        *family = 1;
        break;
    case Gs::PSMCT16S:
        *family = 2;
        break;
    case Gs::PSMZ16S:
        *xorZ = true;
        *family = 2;
        break;
    case Gs::PSMT8:
        *family = 3;
        break;
    case Gs::PSMT4:
        *family = 4;
        break;
    default:
        *family = 0;
        break;
    }
}

struct FamilyInfo
{
    u32 unitsPerBlock;
    u32 unitShift;
    u32 bits;
    u32 pageWidth;
    u32 pageHeight;
    bool halfWidth;
};

constexpr FamilyInfo Families[5] = {
    {64, 0, 32, 64, 32, false},
    {128, 1, 16, 64, 64, false},
    {128, 1, 16, 64, 64, false},
    {256, 2, 8, 128, 64, true},
    {512, 3, 4, 128, 128, true},
};
}

void HwGs::InitMemory()
{
    Gl& gl = GetGl();
    shadow_.assign(Gs::LocalMemoryBytes, 0);
    memStale_.set();
    cpuStale_.reset();
    shadowGood_.reset();
    gl.GenTextures(1, &memTexture_);
    gl.BindTexture(GL_TEXTURE_2D, memTexture_);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32UI), 1024, 1024, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
    gl.GenFramebuffers(1, &memFbo_);
    gl.BindFramebuffer(GL_FRAMEBUFFER, memFbo_);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, memTexture_, 0);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        std::fprintf(stderr, "graphics: hardware renderer: GS memory's framebuffer isn't complete\n");
    }

    // The tables, from the software GS's own layouts (a page at block 0)
    for (u32 f = 0; f < 5; f++)
    {
        const FamilyInfo& info = Families[f];
        u32 units = info.unitsPerBlock * 32;
        std::vector<u32> forward(info.pageWidth * info.pageHeight);
        std::vector<u32> inverse(units, 0xFFFFFFFFu);
        for (u32 y = 0; y < info.pageHeight; y++)
        {
            for (u32 x = 0; x < info.pageWidth; x++)
            {
                u32 a = 0;
                switch (f)
                {
                case 0:
                    a = Gs::PixelAddress32(x, y, 0, 1);
                    break;
                case 1:
                    a = Gs::PixelAddress16(x, y, 0, 1);
                    break;
                case 2:
                    a = Gs::PixelAddress16S(x, y, 0, 1);
                    break;
                case 3:
                    a = Gs::PixelAddress8(x, y, 0, 2);
                    break;
                default:
                    a = Gs::PixelAddress4(x, y, 0, 2);
                    break;
                }

                u32 block = a / info.unitsPerBlock;
                u32 unit = a % info.unitsPerBlock;
                forward[y * info.pageWidth + x] = block << 16 | unit;
                inverse[a] = x | y << 16;
            }
        }

        if (std::count(inverse.begin(), inverse.end(), 0xFFFFFFFFu) != 0)
        {
            std::fprintf(stderr, "graphics: hardware renderer: layout %u's table isn't whole\n", f);
        }

        gl.GenTextures(1, &fwdTables_[f]);
        gl.BindTexture(GL_TEXTURE_2D, fwdTables_[f]);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32UI), static_cast<GLsizei>(info.pageWidth),
                      static_cast<GLsizei>(info.pageHeight), 0, GL_RED_INTEGER, GL_UNSIGNED_INT, forward.data());
        gl.GenTextures(1, &invTables_[f]);
        gl.BindTexture(GL_TEXTURE_2D, invTables_[f]);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32UI), 1024, static_cast<GLsizei>(units / 1024), 0, GL_RED_INTEGER,
                      GL_UNSIGNED_INT, inverse.data());
    }

    std::string header = "#version 330 core\n";
    memFromTargetProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource),
                                        CompileShader(GL_FRAGMENT_SHADER, header + MemCommonSource + MemFromTargetSource), false);
    targetFromMemProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource),
                                        CompileShader(GL_FRAGMENT_SHADER, header + MemCommonSource + TargetFromMemSource), false);
    copyColourProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource), CompileShader(GL_FRAGMENT_SHADER, CopyColourSource),
                                     false);
    copyWordProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource), CompileShader(GL_FRAGMENT_SHADER, CopyWordSource),
                                   false);
    decodeProgram_ = LinkProgram(CompileShader(GL_VERTEX_SHADER, MemRectVertexSource),
                                 CompileShader(GL_FRAGMENT_SHADER, header + MemCommonSource + DecodeSource), false);
    gl.GenFramebuffers(1, &decodeFbo_);
    gl.GenTextures(1, &clutTexture_);
    gl.BindTexture(GL_TEXTURE_2D, clutTexture_);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R16UI), 512, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
    for (GLuint& clut : clutRing_)
    {
        gl.GenTextures(1, &clut);
        gl.BindTexture(GL_TEXTURE_2D, clut);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R16UI), 512, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
    }

    for (u32 i = 0; i < StagingRing; i++)
    {
        gl.GenTextures(1, &memStaging_[i]);
        gl.BindTexture(GL_TEXTURE_2D, memStaging_[i]);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32UI), 1024, 1024, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        gl.GenFramebuffers(1, &stagingFbo_[i]);
        gl.BindFramebuffer(GL_FRAMEBUFFER, stagingFbo_[i]);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, memStaging_[i], 0);
    }
}

GLuint HwGs::DecodeOnGpu(const Gs::Gs::DrawState& state, s32 left, s32 top, s32 right, s32 bottom)
{
    const Gs::RegTex0& tex0 = state.context->tex0;
    const Gs::TextureLevel& base = state.levels[0];
    GpuTexture* entry = nullptr;
    static const bool noGenerationCheck = std::getenv("TWIN_HW_NO_TEXTURE_GENERATION") != nullptr;
    if (!noGenerationCheck && lastTexture_ != nullptr && lastTextureGeneration_ == gs_.stateGeneration() &&
        lastTextureClut_ == gs_.clutVersion())
    {
        // No register written since the previous draw (its texture's, TEXA's) and the same CLUT: the same texture
        entry = lastTexture_;
    }
    else
    {
        const bool indexed = base.psm == Gs::PSMT8 || base.psm == Gs::PSMT4 || base.psm == Gs::PSMT8H || base.psm == Gs::PSMT4HL ||
                             base.psm == Gs::PSMT4HH;
        GpuTextureKey key = {};
        key.psm = base.psm;
        key.cpsm = indexed ? static_cast<u32>(tex0.cpsm) : 0;
        key.csa = indexed ? static_cast<u32>(tex0.csa) : 0;
        key.clutVersion = indexed ? gs_.clutVersion() : 0;
        key.texa = gs_.texa().value & 0x000000FF000080FFull;
        key.levels = state.levelCount;
        for (u32 i = 0; i < state.levelCount; i++)
        {
            key.bp[i] = state.levels[i].bp;
            key.bw[i] = state.levels[i].bw;
        }

        key.width = base.width;
        key.height = base.height;
        // The previous draw's texture again: nothing to look up
        if (lastTexture_ != nullptr && lastTextureKey_ == key)
        {
            entry = lastTexture_;
        }
        else
        {
            if (gpuTextures_.size() > 4096)
            {
                Flush();
                Gl& gl = GetGl();
                for (auto& e : gpuTextures_)
                {
                    gl.DeleteTextures(1, &e.second.id);
                }

                gpuTextures_.clear();
            }

            entry = &gpuTextures_[key];
            lastTextureKey_ = key;
            lastTexture_ = entry;
        }

        lastTextureGeneration_ = gs_.stateGeneration();
        lastTextureClut_ = gs_.clutVersion();
    }

    Gl& gl = GetGl();
    GpuTexture& t = *entry;
    if (t.id == 0)
    {
        gl.GenTextures(1, &t.id);
        gl.BindTexture(GL_TEXTURE_2D, t.id);
        t.levelCount = state.levelCount;
        for (u32 i = 0; i < state.levelCount; i++)
        {
            const Gs::TextureLevel& lv = state.levels[i];
            GpuTexture::Level& L = t.levels[i];
            L.width = lv.width;
            L.height = lv.height;
            L.tilesWide = (lv.width + 7) / 8;
            L.tilesHigh = (lv.height + 7) / 8;
            L.tileEpoch.assign(static_cast<size_t>(L.tilesWide) * L.tilesHigh, 0);
            L.tilePage.resize(L.tileEpoch.size());
            for (u32 ty = 0; ty < L.tilesHigh; ty++)
            {
                for (u32 tx = 0; tx < L.tilesWide; tx++)
                {
                    u32 page = Gs::PixelPage(lv.psm, tx * 8, ty * 8, lv.bp, lv.bw) & (Gs::PageCount - 1);
                    L.tilePage[ty * L.tilesWide + tx] = static_cast<u16>(page);
                    t.pages.set(page);
                }
            }

            gl.TexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(lv.width),
                          static_cast<GLsizei>(lv.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        }

        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(state.levelCount - 1));
        for (u32 page = 0; page < Gs::PageCount; page++)
        {
            if (t.pages.test(page))
            {
                t.pageList.push_back(static_cast<u16>(page));
            }
        }
    }

    // Good as it was: every tile decoded, nothing changed anywhere since it was checked, no target holding newer pages of it
    if (t.complete && t.checkedEpoch == epoch_ && (t.pages & newestMask_).none() && t.shown != 0)
    {
        return t.shown;
    }

    // Shown from the content cache: still good while no page of it changed
    if (t.contentEpoch != 0)
    {
        bool good = true;
        for (u16 page : t.pageList)
        {
            if (pageEpoch_[page] >= t.contentEpoch || newestMask_.test(page))
            {
                good = false;
                break;
            }
        }

        if (good)
        {
            t.checkedEpoch = epoch_;
            return t.shown;
        }

        t.contentEpoch = 0;
        t.complete = false;
    }

    // The tiles the draw reads (every level's whole for mipmaps and textures up to 256x256; else level 0's region)
    Rect want[7];
    const bool whole = state.levelCount > 1 || static_cast<u64>(base.width) * base.height <= 65536 || left > right || top > bottom;
    for (u32 i = 0; i < t.levelCount; i++)
    {
        want[i] = whole ? Rect{0, 0, static_cast<s32>(t.levels[i].width) - 1, static_cast<s32>(t.levels[i].height) - 1}
                        : Rect{left, top, right, bottom};
    }

    // Complete, and no page of it changed since its oldest tile was decoded: nothing stale, as the tiles' search would find
    // (a page at a time rather than a tile at a time; not when the content cache would take it)
    static const bool contentCache = std::getenv("TWIN_HW_CONTENT_CACHE") == nullptr || std::getenv("TWIN_HW_CONTENT_CACHE")[0] != '0';
    static const bool noPageCheck = std::getenv("TWIN_HW_NO_PAGE_CHECK") != nullptr;
    if (!noPageCheck && t.complete && t.oldestTile != 0 && t.shown != 0 &&
        !(contentCache && whole && (t.pages & (cpuStale_ | newestMask_)).none()))
    {
        bool good = true;
        for (u16 page : t.pageList)
        {
            if (pageEpoch_[page] >= t.oldestTile || newestMask_.test(page))
            {
                good = false;
                break;
            }
        }

        if (good)
        {
            if (whole)
            {
                t.checkedEpoch = epoch_;
            }

            t.shown = t.replacement != 0 && ReplacementsEnabled() ? t.replacement : t.id;
            return t.shown;
        }
    }

    // Stale tiles, their pages
    Gs::PageSet needed;
    bool anyStale = false;
    Rect stale[7];
    auto findStale = [&]() {
        needed.reset();
        anyStale = false;
        for (u32 i = 0; i < t.levelCount; i++)
        {
            GpuTexture::Level& L = t.levels[i];
            stale[i] = {0x7FFF, 0x7FFF, -1, -1};
            for (s32 ty = want[i].top / 8; ty <= want[i].bottom / 8; ty++)
            {
                for (s32 tx = want[i].left / 8; tx <= want[i].right / 8; tx++)
                {
                    u32 tile = static_cast<u32>(ty) * L.tilesWide + static_cast<u32>(tx);
                    u32 page = L.tilePage[tile];
                    if (L.tileEpoch[tile] != 0 && L.tileEpoch[tile] > pageEpoch_[page] && !newestMask_.test(page))
                    {
                        continue;
                    }

                    anyStale = true;
                    needed.set(page);
                    stale[i].left = std::min(stale[i].left, tx * 8);
                    stale[i].top = std::min(stale[i].top, ty * 8);
                    stale[i].right = std::max(stale[i].right, std::min(tx * 8 + 7, static_cast<s32>(L.width) - 1));
                    stale[i].bottom = std::max(stale[i].bottom, std::min(ty * 8 + 7, static_cast<s32>(L.height) - 1));
                }
            }
        }
    };
    findStale();
    // A big texture read from a render target a piece at a time (the effects' strips): the whole of it decoded at once,
    // so the next pieces find it decoded
    if (!whole && anyStale && (needed & newestMask_).any())
    {
        for (u32 i = 0; i < t.levelCount; i++)
        {
            want[i] = {0, 0, static_cast<s32>(t.levels[i].width) - 1, static_cast<s32>(t.levels[i].height) - 1};
        }

        findStale();
    }

    // Data the CPU wrote: decoded once for its content, wherever and whenever it's uploaded again
    // ($TWIN_HW_CONTENT_CACHE=0 turns it off)
    if (contentCache && whole && (t.pages & (cpuStale_ | newestMask_)).none())
    {
        const u64 hash = ContentHash(state);
        ContentTexture& c = contents_[hash];
        if (c.id == 0)
        {
            Flush();
            SyncMem(t.pages);
            c.id = MakeLevels(state);
            Rect all[7];
            for (u32 i = 0; i < t.levelCount; i++)
            {
                all[i] = {0, 0, static_cast<s32>(t.levels[i].width) - 1, static_cast<s32>(t.levels[i].height) - 1};
            }

            DecodePasses(state, c.id, all, t.levelCount);
            stats.decodes++;
            if (ReplacementsEnabled())
            {
                NativeGraphics::ReplacementKey replacementKey;
                const NativeGraphics::ReplacementImage* image = nullptr;
                if (NativeGraphics::ReplacementKeyFor(gs_, base, tex0, state.context->clamp, &replacementKey))
                {
                    image = NativeGraphics::FindReplacement(replacementKey);
                }

                if (image != nullptr)
                {
                    GLuint& made = replacementTextures_[replacementKey];
                    if (made == 0)
                    {
                        gl.GenTextures(1, &made);
                        gl.BindTexture(GL_TEXTURE_2D, made);
                        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
                        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(image->width),
                                      static_cast<GLsizei>(image->height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels.data());
                        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                        gl.GenerateMipmap(GL_TEXTURE_2D);
                    }

                    c.replacement = made;
                    stats.replaced++;
                }
            }
        }
        else
        {
            stats.contentHits++;
        }

        // The texture's own tiles stay as they are (none of this was decoded into it)
        t.contentEpoch = ++epoch_;
        t.complete = true;
        t.checkedEpoch = epoch_;
        t.shown = c.replacement != 0 && ReplacementsEnabled() ? c.replacement : c.id;
        return t.shown;
    }

    if (anyStale)
    {
        // The pages current in memory's copy, then the stale tiles' rectangles decoded from it into the texture's own
        Flush();
        SyncMem(needed);
        DecodePasses(state, t.id, stale, t.levelCount);
        const u32 decodeEpoch = ++epoch_;
        for (u32 i = 0; i < t.levelCount; i++)
        {
            GpuTexture::Level& L = t.levels[i];
            if (stale[i].right < stale[i].left)
            {
                continue;
            }

            for (s32 ty = stale[i].top / 8; ty <= stale[i].bottom / 8; ty++)
            {
                for (s32 tx = stale[i].left / 8; tx <= stale[i].right / 8; tx++)
                {
                    L.tileEpoch[static_cast<u32>(ty) * L.tilesWide + static_cast<u32>(tx)] = decodeEpoch;
                }
            }
        }

        stats.decodes++;
    }

    // A replacement looked for when it's decoded whole from data the CPU wrote (render targets' textures aren't replaced,
    // as in PCSX2): its hash is local memory's, which is current for these pages
    if (anyStale && whole && ReplacementsEnabled() && (t.pages & (cpuStale_ | newestMask_)).none())
    {
        NativeGraphics::ReplacementKey replacementKey;
        const NativeGraphics::ReplacementImage* image = nullptr;
        if (NativeGraphics::ReplacementKeyFor(gs_, base, tex0, state.context->clamp, &replacementKey))
        {
            image = NativeGraphics::FindReplacement(replacementKey);
        }

        t.replacement = 0;
        if (image != nullptr)
        {
            // Each replacement made a texture once (the textures that share it, and the decodes again, use that one)
            GLuint& made = replacementTextures_[replacementKey];
            if (made == 0)
            {
                gl.GenTextures(1, &made);
                gl.BindTexture(GL_TEXTURE_2D, made);
                gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
                gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(image->width),
                              static_cast<GLsizei>(image->height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels.data());
                gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                gl.GenerateMipmap(GL_TEXTURE_2D);
            }

            t.replacement = made;
            stats.replaced++;
        }
    }
    else if (anyStale && t.replacement != 0 && (t.pages & (cpuStale_ | newestMask_)).any())
    {
        // Its data is a render target's now: not replaced
        t.replacement = 0;
    }

    // Complete when every tile of every level is good now
    if (whole)
    {
        t.complete = true;
        t.oldestTile = ~0u;
        for (u32 i = 0; i < t.levelCount && t.complete; i++)
        {
            for (u32 e : t.levels[i].tileEpoch)
            {
                if (e == 0)
                {
                    t.complete = false;
                    break;
                }

                t.oldestTile = std::min(t.oldestTile, e);
            }
        }

        if (!t.complete)
        {
            t.oldestTile = 0;
        }

        t.checkedEpoch = epoch_;
    }

    t.shown = t.replacement != 0 && ReplacementsEnabled() ? t.replacement : t.id;
    return t.shown;
}

GLuint HwGs::MakeLevels(const Gs::Gs::DrawState& state)
{
    Gl& gl = GetGl();
    GLuint id = 0;
    gl.GenTextures(1, &id);
    gl.BindTexture(GL_TEXTURE_2D, id);
    for (u32 i = 0; i < state.levelCount; i++)
    {
        gl.TexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(state.levels[i].width),
                      static_cast<GLsizei>(state.levels[i].height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(state.levelCount - 1));
    return id;
}

u64 HwGs::ContentHash(const Gs::Gs::DrawState& state)
{
    const Gs::RegTex0& tex0 = state.context->tex0;
    const Gs::TextureLevel& base = state.levels[0];
    const bool indexed = base.psm == Gs::PSMT8 || base.psm == Gs::PSMT4 || base.psm == Gs::PSMT8H || base.psm == Gs::PSMT4HL ||
                         base.psm == Gs::PSMT4HH;
    XXH3_state_t* hash = XXH3_createState();
    XXH3_64bits_reset(hash);
    u64 header[4] = {static_cast<u64>(base.psm) | static_cast<u64>(state.levelCount) << 8 | static_cast<u64>(base.width) << 16 |
                         static_cast<u64>(base.height) << 32,
                     indexed ? (static_cast<u64>(gs_.clutVersion()) | static_cast<u64>(tex0.cpsm) << 32 | static_cast<u64>(tex0.csa) << 40)
                             : 0,
                     gs_.texa().value & 0x000000FF000080FFull, 0};
    XXH3_64bits_update(hash, header, sizeof(header));
    const u8* memory = gs_.memory();
    for (u32 i = 0; i < state.levelCount; i++)
    {
        const Gs::TextureLevel& lv = state.levels[i];
        u32 family = 0;
        bool xorZ = false;
        LayoutOf(lv.psm, &family, &xorZ);
        static constexpr u32 BlockWidth[5] = {8, 16, 16, 16, 32};
        static constexpr u32 BlockHeight[5] = {8, 8, 8, 16, 16};
        const FamilyInfo& info = Families[family];
        for (u32 y = 0; y < lv.height; y += BlockHeight[family])
        {
            for (u32 x = 0; x < lv.width; x += BlockWidth[family])
            {
                u32 unit = 0;
                switch (family)
                {
                case 0:
                    unit = xorZ ? Gs::PixelAddress32Z(x, y, lv.bp, lv.bw) : Gs::PixelAddress32(x, y, lv.bp, lv.bw);
                    break;
                case 1:
                    unit = xorZ ? Gs::PixelAddress16Z(x, y, lv.bp, lv.bw) : Gs::PixelAddress16(x, y, lv.bp, lv.bw);
                    break;
                case 2:
                    unit = xorZ ? Gs::PixelAddress16SZ(x, y, lv.bp, lv.bw) : Gs::PixelAddress16S(x, y, lv.bp, lv.bw);
                    break;
                case 3:
                    unit = Gs::PixelAddress8(x, y, lv.bp, lv.bw);
                    break;
                default:
                    unit = Gs::PixelAddress4(x, y, lv.bp, lv.bw);
                    break;
                }

                u32 block = (unit / info.unitsPerBlock) & (Gs::BlockCount - 1);
                XXH3_64bits_update(hash, memory + static_cast<size_t>(block) * 256, 256);
            }
        }
    }

    u64 result = XXH3_64bits_digest(hash);
    XXH3_freeState(hash);
    return result;
}

void HwGs::DecodePasses(const Gs::Gs::DrawState& state, GLuint texture, const Rect* rects, u32 levels)
{
    Gl& gl = GetGl();
    const Gs::RegTex0& tex0 = state.context->tex0;
    const Gs::TextureLevel& base = state.levels[0];
    const bool indexed = base.psm == Gs::PSMT8 || base.psm == Gs::PSMT4 || base.psm == Gs::PSMT8H || base.psm == Gs::PSMT4HL ||
                         base.psm == Gs::PSMT4HH;
    if (indexed && clutUploaded_ != gs_.clutVersion())
    {
        clutTexture_ = clutRing_[clutNext_++ % ClutRing];
        gl.BindTexture(GL_TEXTURE_2D, clutTexture_);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 2);
        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 1, GL_RED_INTEGER, GL_UNSIGNED_SHORT, gs_.clut());
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        clutUploaded_ = gs_.clutVersion();
    }

    gl.BindFramebuffer(GL_FRAMEBUFFER, decodeFbo_);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.BindVertexArray(emptyArray_);
    Gs::RegTexa texa = gs_.texa();
    for (u32 i = 0; i < levels; i++)
    {
        if (rects[i].right < rects[i].left)
        {
            continue;
        }

        const Gs::TextureLevel& lv = state.levels[i];
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, static_cast<GLint>(i));
        Pass((1ull << 40) | (static_cast<u64>(texture) << 4) | i, "texture decode");
        gl.Viewport(0, 0, static_cast<GLsizei>(lv.width), static_cast<GLsizei>(lv.height));
        UseLayout(decodeProgram_, lv.psm, lv.bp, lv.bw, 1);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, memTexture_);
        gl.ActiveTexture(GL_TEXTURE0 + 3);
        gl.BindTexture(GL_TEXTURE_2D, clutTexture_);
        gl.ActiveTexture(GL_TEXTURE0);
        auto uniform = [&](const char* name) { return gl.GetUniformLocation(decodeProgram_, name); };
        gl.Uniform1i(uniform("mem"), 0);
        gl.Uniform1i(uniform("clut"), 3);
        gl.Uniform1i(uniform("psm"), static_cast<GLint>(lv.psm));
        gl.Uniform1i(uniform("cpsm"), static_cast<GLint>(tex0.cpsm));
        gl.Uniform1i(uniform("csa"), static_cast<GLint>(tex0.csa));
        gl.Uniform1ui(uniform("ta0"), static_cast<GLuint>(texa.ta0));
        gl.Uniform1ui(uniform("ta1"), static_cast<GLuint>(texa.ta1));
        gl.Uniform1i(uniform("aem"), static_cast<GLint>(texa.aem));
        gl.Uniform2f(uniform("viewSize"), static_cast<f32>(lv.width), static_cast<f32>(lv.height));
        gl.Uniform4f(uniform("rect"), static_cast<f32>(rects[i].left), static_cast<f32>(rects[i].top), static_cast<f32>(rects[i].right + 1),
                     static_cast<f32>(rects[i].bottom + 1));
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
}

void HwGs::CopyRect(GLuint program, GLuint source, f32 viewWidth, f32 viewHeight, s32 x0, s32 y0, s32 x1, s32 y1)
{
    Gl& gl = GetGl();
    gl.Viewport(0, 0, static_cast<GLsizei>(viewWidth), static_cast<GLsizei>(viewHeight));
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.UseProgram(program);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, source);
    gl.Uniform1i(gl.GetUniformLocation(program, "source"), 0);
    gl.Uniform2f(gl.GetUniformLocation(program, "viewSize"), viewWidth, viewHeight);
    gl.Uniform4f(gl.GetUniformLocation(program, "rect"), static_cast<f32>(x0), static_cast<f32>(y0), static_cast<f32>(x1),
                 static_cast<f32>(y1));
    gl.BindVertexArray(emptyArray_);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void HwGs::UseLayout(GLuint program, u32 psm, u32 bp, u32 bw, u32 pagesDown)
{
    Gl& gl = GetGl();
    u32 family = 0;
    bool xorZ = false;
    LayoutOf(psm, &family, &xorZ);
    const FamilyInfo& info = Families[family];
    gl.UseProgram(program);
    auto uniform = [&](const char* name) { return gl.GetUniformLocation(program, name); };
    gl.Uniform1i(uniform("unitsPerBlock"), static_cast<GLint>(info.unitsPerBlock));
    gl.Uniform1i(uniform("unitShift"), static_cast<GLint>(info.unitShift));
    gl.Uniform1i(uniform("unitBits"), static_cast<GLint>(info.bits));
    gl.Uniform1i(uniform("pageWidth"), static_cast<GLint>(info.pageWidth));
    gl.Uniform1i(uniform("pageHeight"), static_cast<GLint>(info.pageHeight));
    gl.Uniform1i(uniform("pagesAcross"), static_cast<GLint>(std::max<u32>(info.halfWidth ? bw >> 1 : bw, 1)));
    gl.Uniform1i(uniform("pagesDown"), static_cast<GLint>(pagesDown));
    gl.Uniform1i(uniform("basePointer"), static_cast<GLint>(bp));
    gl.Uniform1i(uniform("xorZ"), xorZ ? 1 : 0);
    gl.ActiveTexture(GL_TEXTURE0 + 1);
    gl.BindTexture(GL_TEXTURE_2D, fwdTables_[family]);
    gl.ActiveTexture(GL_TEXTURE0 + 2);
    gl.BindTexture(GL_TEXTURE_2D, invTables_[family]);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.Uniform1i(uniform("fwd"), 1);
    gl.Uniform1i(uniform("inv"), 2);
}



void HwGs::MemFromTarget(Target* target, const Gs::PageSet& pages)
{
    Gl& gl = GetGl();
    stats.downloads++;
    TraceConversion("from", target, pages);
    if (target->scale > 1)
    {
        scaledPages_ |= pages & target->pages;
    }
    else
    {
        scaledPages_ &= ~(pages & target->pages);
    }

    u32 family = 0;
    bool xorZ = false;
    LayoutOf(target->psm, &family, &xorZ);
    UseLayout(memFromTargetProgram_, target->psm, target->bp, target->bw, target->height / Families[family].pageHeight);
    gl.BindFramebuffer(GL_FRAMEBUFFER, memFbo_);
    Pass(memFbo_, "target to memory");
    gl.Viewport(0, 0, 1024, 1024);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, target->texture);
    // (the memory sampler isn't read here: a table stands in for it, memory being the framebuffer)
    gl.ActiveTexture(GL_TEXTURE0 + 3);
    gl.BindTexture(GL_TEXTURE_2D, fwdTables_[0]);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.Uniform1i(gl.GetUniformLocation(memFromTargetProgram_, "target"), 0);
    gl.Uniform1i(gl.GetUniformLocation(memFromTargetProgram_, "mem"), 3);
    gl.Uniform1i(gl.GetUniformLocation(memFromTargetProgram_, "targetScale"), static_cast<GLint>(target->scale));
    gl.Uniform1i(gl.GetUniformLocation(memFromTargetProgram_, "depthReverse"), target->reversed ? 1 : 0);
    gl.Uniform1i(gl.GetUniformLocation(memFromTargetProgram_, "storage"),
                 target->depth ? 2 : (Is32Class(target->cls) ? 0 : 1));
    gl.Uniform2f(gl.GetUniformLocation(memFromTargetProgram_, "viewSize"), 1024.0f, 1024.0f);
    GLint rect = gl.GetUniformLocation(memFromTargetProgram_, "rect");
    gl.BindVertexArray(emptyArray_);
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        // Runs of pages: two rows of memory a page
        if (!pages.test(page))
        {
            continue;
        }

        u32 end = page;
        while (end + 1 < Gs::PageCount && pages.test(end + 1))
        {
            end++;
        }

        gl.Uniform4f(rect, 0.0f, static_cast<f32>(page * 2), 1024.0f, static_cast<f32>((end + 1) * 2));
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        page = end;
    }
}

void HwGs::TargetFromMem(Target* target, const Gs::PageSet& pages)
{
    Gl& gl = GetGl();
    stats.uploads++;
    TraceConversion("into", target, pages);
    static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
    if (counting)
    {
        static std::map<std::string, u64> counts;
        static u64 total = 0;
        char detail[128];
        // Who held the pages newest before (the last target drawn into them)
        std::snprintf(detail, sizeof(detail), "into %x/%u class %u: %zu pages (%s)", target->bp, target->bw, target->cls, (pages & target->pages).count(),
                      cpuReason);
        counts[detail]++;
        if (++total % 400 == 0)
        {
            for (auto& entry : counts)
            {
                std::fprintf(stderr, "conversions: %llu %s\n", static_cast<unsigned long long>(entry.second), entry.first.c_str());
            }
        }
    }
    u32 family = 0;
    bool xorZ = false;
    LayoutOf(target->psm, &family, &xorZ);
    UseLayout(targetFromMemProgram_, target->psm, target->bp, target->bw, target->height / Families[family].pageHeight);
    const GLint n = static_cast<GLint>(target->scale);
    gl.BindFramebuffer(GL_FRAMEBUFFER, target->fbo);
    Pass(target->fbo, "memory to target");
    gl.Viewport(0, 0, static_cast<GLsizei>(target->width) * n, static_cast<GLsizei>(target->height) * n);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    if (target->depth)
    {
        gl.Enable(GL_DEPTH_TEST);
        gl.DepthFunc(GL_ALWAYS);
        gl.DepthMask(GL_TRUE);
        gl.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    }
    else
    {
        gl.Disable(GL_DEPTH_TEST);
        gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, memTexture_);
    gl.Uniform1i(gl.GetUniformLocation(targetFromMemProgram_, "mem"), 0);
    gl.Uniform1i(gl.GetUniformLocation(targetFromMemProgram_, "targetScale"), n);
    gl.Uniform1i(gl.GetUniformLocation(targetFromMemProgram_, "depthReverse"), target->reversed ? 1 : 0);
    gl.Uniform1i(gl.GetUniformLocation(targetFromMemProgram_, "storage"), target->depth ? 2 : (Is32Class(target->cls) ? 0 : 1));
    gl.Uniform2f(gl.GetUniformLocation(targetFromMemProgram_, "viewSize"), static_cast<f32>(target->width * n),
                 static_cast<f32>(target->height * n));
    GLuint mask[16] = {};
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (pages.test(page))
        {
            mask[page >> 5] |= 1u << (page & 31);
        }
    }

    for (u32 i = 0; i < 16; i++)
    {
        char name[24];
        std::snprintf(name, sizeof(name), "pageMask[%u]", i);
        gl.Uniform1ui(gl.GetUniformLocation(targetFromMemProgram_, name), mask[i]);
    }

    // One draw over the pages' rectangle (the page mask leaves the others' pixels as they are)
    Rect all = {0x7FFF, 0x7FFF, -1, -1};
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (!pages.test(page) || !target->pages.test(page))
        {
            continue;
        }

        const Rect& r = target->rects[page];
        all.left = std::min(all.left, r.left);
        all.top = std::min(all.top, r.top);
        all.right = std::max(all.right, r.right);
        all.bottom = std::max(all.bottom, r.bottom);
    }

    if (all.right >= all.left)
    {
        gl.BindVertexArray(emptyArray_);
        gl.Uniform4f(gl.GetUniformLocation(targetFromMemProgram_, "rect"), static_cast<f32>(all.left * n), static_cast<f32>(all.top * n),
                     static_cast<f32>((all.right + 1) * n), static_cast<f32>((all.bottom + 1) * n));
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    target->alphaVersion++;
}

// Memory's copy made current for the pages: targets' newer pages written into it, local memory's uploaded
void HwGs::SyncMem(const Gs::PageSet& pages)
{
    ProcessWrites();
    Gs::PageSet fromTargets = pages & newestMask_;
    if (fromTargets.any())
    {
        Flush();
        for (auto& target : targets_)
        {
            Gs::PageSet mine;
            for (u32 page = 0; page < Gs::PageCount; page++)
            {
                if (fromTargets.test(page) && newest_[page] == target.get())
                {
                    mine.set(page);
                }
            }

            if (mine.none())
            {
                continue;
            }

            Resolve(target.get());
            MemFromTarget(target.get(), mine);
            for (u32 page = 0; page < Gs::PageCount; page++)
            {
                if (mine.test(page))
                {
                    newest_[page] = nullptr;
                    pageEpoch_[page] = ++epoch_;
                }
            }

            newestMask_ &= ~mine;
            memStale_ &= ~mine;
            shadowGood_ &= ~mine;
            target->owned &= ~mine;
        }
    }

    Gs::PageSet uploads = pages & memStale_ & ~newestMask_;
    if (uploads.none())
    {
        return;
    }

    Flush();
    Gl& gl = GetGl();
    static const bool staged = std::getenv("TWIN_HW_STAGED_UPLOADS") != nullptr;
    if (!staged)
    {
        // Each run of pages into a fresh buffer's storage (the old storage stays the GPU's until it's done with it), copied
        // into memory's copy by the GPU
        if (uploadBuffer_ == 0)
        {
            gl.GenBuffers(1, &uploadBuffer_);
        }

        gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, uploadBuffer_);
        gl.BindTexture(GL_TEXTURE_2D, memTexture_);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        const u8* memory = gs_.memory();
        for (u32 page = 0; page < Gs::PageCount; page++)
        {
            if (!uploads.test(page))
            {
                continue;
            }

            u32 end = page;
            while (end + 1 < Gs::PageCount && uploads.test(end + 1))
            {
                end++;
            }

            const size_t bytes = static_cast<size_t>(end - page + 1) * 8192;
            gl.BufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(bytes), memory + static_cast<size_t>(page) * 8192, GL_STREAM_DRAW);
            gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(page * 2), 1024, static_cast<GLsizei>((end - page + 1) * 2),
                             GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
            std::memcpy(shadow_.data() + static_cast<size_t>(page) * 8192, memory + static_cast<size_t>(page) * 8192, bytes);
            page = end;
        }

        gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        Pass(1ull << 50, "CPU writes to memory");
        memStale_ &= ~uploads;
        shadowGood_ |= uploads;
        return;
    }

    // Into the next staging texture (at the pages' own rows), then copied into memory's copy by the GPU
    const u32 slot = stagingNext_++ % StagingRing;
    gl.BindTexture(GL_TEXTURE_2D, memStaging_[slot]);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    const u8* memory = gs_.memory();
    std::vector<std::pair<u32, u32>> runs;
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (!uploads.test(page))
        {
            continue;
        }

        u32 end = page;
        while (end + 1 < Gs::PageCount && uploads.test(end + 1))
        {
            end++;
        }

        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, static_cast<GLint>(page * 2), 1024, static_cast<GLsizei>((end - page + 1) * 2),
                         GL_RED_INTEGER, GL_UNSIGNED_INT, memory + static_cast<size_t>(page) * 8192);
        runs.emplace_back(page, end);
        std::memcpy(shadow_.data() + static_cast<size_t>(page) * 8192, memory + static_cast<size_t>(page) * 8192,
                    static_cast<size_t>(end - page + 1) * 8192);
        page = end;
    }

    // The staging rows copied into memory's copy by drawing
    gl.BindFramebuffer(GL_FRAMEBUFFER, memFbo_);
    Pass(memFbo_, "CPU writes to memory");
    for (const auto& run : runs)
    {
        CopyRect(copyWordProgram_, memStaging_[slot], 1024.0f, 1024.0f, 0, static_cast<s32>(run.first * 2), 1024,
                 static_cast<s32>((run.second + 1) * 2));
    }

    memStale_ &= ~uploads;
    shadowGood_ |= uploads;
}

// Local memory made current for the pages (read back from memory's copy: the GPU's work waited for)
void HwGs::SyncCpu(const Gs::PageSet& pages)
{
    ProcessWrites();
    if ((pages & (cpuStale_ | newestMask_)).none())
    {
        return;
    }

    MakeCurrent();
    SyncMem(pages);
    Gs::PageSet reads = pages & cpuStale_;
    if (reads.none())
    {
        return;
    }

    Gl& gl = GetGl();
    stats.readbacks++;
    static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
    if (counting)
    {
        static std::map<std::string, u64> counts;
        static u64 total = 0;
        char detail[128];
        u32 first = 0;
        while (first < Gs::PageCount && !reads.test(first))
        {
            first++;
        }

        std::snprintf(detail, sizeof(detail), "%s: %zu pages from %x", downloadReason_[0] ? downloadReason_ : cpuReason, reads.count(), first);
        counts[detail]++;
        if (++total % 50 == 0)
        {
            for (auto& entry : counts)
            {
                std::fprintf(stderr, "readbacks: %llu %s\n", static_cast<unsigned long long>(entry.second), entry.first.c_str());
            }
        }
    }
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, memFbo_);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_PACK_ROW_LENGTH, 0);
    gs_.NextGeneration();
    gs_.SetBackendQuiet(true);
    u8* memory = gs_.memory();
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (!reads.test(page))
        {
            continue;
        }

        u32 end = page;
        while (end + 1 < Gs::PageCount && reads.test(end + 1))
        {
            end++;
        }

        u8* to = shadow_.data() + static_cast<size_t>(page) * 8192;
        gl.ReadPixels(0, static_cast<GLint>(page * 2), 1024, static_cast<GLsizei>((end - page + 1) * 2), GL_RED_INTEGER,
                      GL_UNSIGNED_INT, to);
        std::memcpy(memory + static_cast<size_t>(page) * 8192, to, static_cast<size_t>(end - page + 1) * 8192);
        for (u32 p = page; p <= end; p++)
        {
            gs_.MarkWritten(p);
        }

        page = end;
    }

    gs_.SetBackendQuiet(false);
    gs_.NextGeneration();
    cpuStale_ &= ~reads;
    shadowGood_ |= reads;
}

void HwGs::ProcessWrites()
{
    if (!anyWritten_.exchange(false, std::memory_order_acquire))
    {
        return;
    }

    // Pages the CPU side wrote: changed unless their bytes are what memory's copy already holds (the game uploads its textures
    // again every frame)
    Gs::PageSet written;
    const u8* memory = gs_.memory();
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (written_[page].load(std::memory_order_relaxed) == 0)
        {
            continue;
        }

        written_[page].store(0, std::memory_order_relaxed);
        if (!memStale_.test(page) && shadowGood_.test(page) && newest_[page] == nullptr &&
            std::memcmp(memory + static_cast<size_t>(page) * 8192, shadow_.data() + static_cast<size_t>(page) * 8192, 8192) == 0)
        {
            continue;
        }

        written.set(page);
        newest_[page] = nullptr;
        pageEpoch_[page] = ++epoch_;
    }

    if (written.none())
    {
        return;
    }

    newestMask_ &= ~written;
    memStale_ |= written;
    scaledPages_ &= ~written;
    cpuStale_ &= ~written;
    shadowGood_ &= ~written;
    for (auto& target : targets_)
    {
        target->valid &= ~written;
        target->owned &= ~written;
    }
}

void HwGs::BeforeCpuRead(const Gs::PageSet& pages)
{
    SyncCpu(pages);
}

void HwGs::Download(Target* target)
{
    Gs::PageSet pages;
    Gs::PageSet candidates = newestMask_ & target->pages;
    if (candidates.none())
    {
        return;
    }

    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (candidates.test(page) && newest_[page] == target)
        {
            pages.set(page);
        }
    }

    if (pages.none())
    {
        return;
    }

    Flush();
    Resolve(target);
    stats.downloads++;
    static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
    if (counting)
    {
        static std::map<std::string, u64> counts;
        static u64 total = 0;
        char detail[96];
        std::snprintf(detail, sizeof(detail), "download %x/%u class %u (%s)", target->bp, target->bw, target->cls, downloadReason_);
        counts[detail]++;
        if (++total % 100 == 0)
        {
            for (auto& entry : counts)
            {
                std::fprintf(stderr, "downloads: %llu %s\n", static_cast<unsigned long long>(entry.second), entry.first.c_str());
            }
        }
    }
    Gl& gl = GetGl();
    // The target at the PS2's resolution (a sample of each pixel's), read back
    GLuint small = 0;
    GLuint smallFbo = 0;
    Staging(target->width, target->height, target->depth, &small, &smallFbo);
    gl.Disable(GL_SCISSOR_TEST);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, target->fbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, smallFbo);
    Pass(smallFbo, "download");
    GLint w = static_cast<GLint>(target->width);
    GLint h = static_cast<GLint>(target->height);
    // Each pixel's sample is its block's first (the GS's sample point): the blit's nearest sample moved there
    const GLint back = static_cast<GLint>(target->scale / 2);
    gl.BlitFramebuffer(-back, -back, w * static_cast<GLint>(target->scale) - back, h * static_cast<GLint>(target->scale) - back, 0, 0, w, h,
                       target->depth ? GL_DEPTH_BUFFER_BIT : GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, smallFbo);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_PACK_ROW_LENGTH, 0);
    readback_.resize(static_cast<size_t>(w) * h * 4);
    if (target->depth)
    {
        gl.ReadPixels(0, 0, w, h, GL_DEPTH_COMPONENT, GL_FLOAT, readback_.data());
    }
    else
    {
        gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, readback_.data());
    }

    // Written into local memory without telling the backend (local memory catches up), a new generation each side so the
    // texture cache sees the pages change
    gs_.NextGeneration();
    gs_.SetBackendQuiet(true);
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (!pages.test(page))
        {
            continue;
        }

        for (u32 packed : target->pixels[page])
        {
            u32 x = packed & 2047;
            u32 y = packed >> 11;
            size_t at = (static_cast<size_t>(y) * static_cast<size_t>(w) + x) * 4;
            u32 value;
            if (target->depth)
            {
                f32 d;
                std::memcpy(&d, &readback_[at], 4);
                double z = std::floor(static_cast<double>(d) * 4294967296.0 + 0.5);
                value = static_cast<u32>(std::min(z, 4294967295.0));
                value = target->reversed ? 0xFFFFFFFFu - value : value;
            }
            else
            {
                u32 r = readback_[at];
                u32 g = readback_[at + 1];
                u32 b = readback_[at + 2];
                u32 a = readback_[at + 3];
                if (target->cls == ClassColour32)
                {
                    value = r | g << 8 | b << 16 | a << 24;
                }
                else
                {
                    value = (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 | (a >= 0x80 ? 0x8000u : 0u);
                }
            }

            gs_.WritePixel(target->psm, x, y, target->bp, target->bw, value);
        }

        newest_[page] = nullptr;
        newestMask_.reset(page);
        target->owned.reset(page);
    }

    gs_.SetBackendQuiet(false);
    gs_.NextGeneration();
}

void HwGs::Upload(Target* target, const Gs::PageSet& pages)
{
    Flush();
    stats.uploads++;
    static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
    if (counting)
    {
        static std::map<std::string, u64> counts;
        static u64 total = 0;
        char detail[96];
        std::snprintf(detail, sizeof(detail), "upload %x/%u class %u, %zu pages", target->bp, target->bw, target->cls, (pages & target->pages).count());
        counts[detail]++;
        if (++total % 100 == 0)
        {
            for (auto& entry : counts)
            {
                std::fprintf(stderr, "uploads: %llu %s\n", static_cast<unsigned long long>(entry.second), entry.first.c_str());
            }
        }
    }
    Gl& gl = GetGl();
    const u32 w = target->width;
    const u32 h = target->height;
    GLuint small = 0;
    GLuint smallFbo = 0;
    if (target->depth)
    {
        // A float staging texture of the depths over 2^32, written by the depth program
        static std::map<std::pair<u32, u32>, GLuint> floats;
        auto key = std::make_pair(w, h);
        if (floats.find(key) == floats.end())
        {
            GLuint texture = 0;
            gl.GenTextures(1, &texture);
            gl.BindTexture(GL_TEXTURE_2D, texture);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32F), static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0,
                          GL_RED, GL_FLOAT, nullptr);
            floats[key] = texture;
        }

        small = floats[key];
        uploadDepth_.assign(static_cast<size_t>(w) * h, 0.0f);
    }
    else
    {
        Staging(w, h, false, &small, &smallFbo);
        upload_.assign(static_cast<size_t>(w) * h, 0);
    }

    Rect all = {0x7FFF, 0x7FFF, -1, -1};
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (!pages.test(page) || !target->pages.test(page))
        {
            continue;
        }

        for (u32 packed : target->pixels[page])
        {
            u32 x = packed & 2047;
            u32 y = packed >> 11;
            u32 value = gs_.ReadPixel(target->psm, x, y, target->bp, target->bw);
            size_t at = static_cast<size_t>(y) * w + x;
            if (target->depth)
            {
                uploadDepth_[at] = static_cast<f32>(target->reversed ? 0xFFFFFFFFu - value : value) * (1.0f / 4294967296.0f);
            }
            else if (target->cls == ClassColour32)
            {
                upload_[at] = value;
            }
            else
            {
                upload_[at] = (value & 0x1F) << 3 | ((value >> 5) & 0x1F) << 11 | ((value >> 10) & 0x1F) << 19 |
                              (value & 0x8000 ? 0x80000000u : 0u);
            }
        }

        const Rect& r = target->rects[page];
        all.left = std::min(all.left, r.left);
        all.top = std::min(all.top, r.top);
        all.right = std::max(all.right, r.right);
        all.bottom = std::max(all.bottom, r.bottom);
    }

    if (all.right < all.left)
    {
        return;
    }

    // Each page's rectangle (its own pixels only, on targets laid out in pages; otherwise the pages' bounding rectangle)
    std::vector<Rect> rects;
    if (target->aligned)
    {
        for (u32 page = 0; page < Gs::PageCount; page++)
        {
            if (pages.test(page) && target->pages.test(page))
            {
                rects.push_back(target->rects[page]);
            }
        }
    }
    else
    {
        rects.push_back(all);
    }

    gl.Disable(GL_SCISSOR_TEST);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(w));
    gl.BindTexture(GL_TEXTURE_2D, small);
    for (const Rect& r : rects)
    {
        size_t at = static_cast<size_t>(r.top) * w + static_cast<size_t>(r.left);
        GLsizei rw = r.right - r.left + 1;
        GLsizei rh = r.bottom - r.top + 1;
        if (target->depth)
        {
            gl.TexSubImage2D(GL_TEXTURE_2D, 0, r.left, r.top, rw, rh, GL_RED, GL_FLOAT, &uploadDepth_[at]);
        }
        else
        {
            gl.TexSubImage2D(GL_TEXTURE_2D, 0, r.left, r.top, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, &upload_[at]);
        }
    }

    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    const GLint n = static_cast<GLint>(target->scale);
    if (target->depth)
    {
        gl.BindFramebuffer(GL_FRAMEBUFFER, target->fbo);
        Pass(target->fbo, "upload");
        gl.Viewport(0, 0, static_cast<GLsizei>(w) * n, static_cast<GLsizei>(h) * n);
        gl.UseProgram(depthProgram_);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, small);
        gl.Uniform1i(gl.GetUniformLocation(depthProgram_, "staging"), 0);
        gl.Uniform1f(gl.GetUniformLocation(depthProgram_, "scale"), static_cast<f32>(target->scale));
        gl.Uniform2f(gl.GetUniformLocation(depthProgram_, "viewSize"), static_cast<f32>(w * target->scale),
                     static_cast<f32>(h * target->scale));
        gl.Disable(GL_BLEND);
        gl.Disable(GL_STENCIL_TEST);
        gl.Enable(GL_DEPTH_TEST);
        gl.DepthFunc(GL_ALWAYS);
        gl.DepthMask(GL_TRUE);
        gl.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl.BindVertexArray(emptyArray_);
        GLint rectLocation = gl.GetUniformLocation(depthProgram_, "rect");
        for (const Rect& r : rects)
        {
            gl.Uniform4f(rectLocation, static_cast<f32>(r.left * n), static_cast<f32>(r.top * n), static_cast<f32>((r.right + 1) * n),
                         static_cast<f32>((r.bottom + 1) * n));
            gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    else
    {
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, smallFbo);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, target->fbo);
        Pass(target->fbo, "upload");
        for (const Rect& r : rects)
        {
            gl.BlitFramebuffer(r.left, r.top, r.right + 1, r.bottom + 1, r.left * n, r.top * n, (r.right + 1) * n, (r.bottom + 1) * n,
                               GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
    }

    Gs::PageSet uploaded = pages & target->pages;
    target->valid |= uploaded;
    target->alphaVersion++;
    // Another target holding these pages newest has a copy elsewhere now
    for (auto& other : targets_)
    {
        if (other.get() != target)
        {
            other->owned &= ~uploaded;
        }
    }
}

void HwGs::EnsureValid(Target* target, const Gs::PageSet& pages)
{
    Gs::PageSet missing = pages & target->pages & ~target->valid;
    if (missing.none())
    {
        return;
    }

    // With the target's other pages that come the same way: those another target holding one of these newest holds newest
    // too, and those memory's copy already holds current. All in the same passes, so a buffer drawn into a page or two at a
    // time (a model drawn into an effect's buffer, over another view of it) isn't converted a pass a draw
    static const bool narrow = std::getenv("TWIN_HW_NO_WIDE_LOAD") != nullptr;
    if (!narrow)
    {
        ProcessWrites();
        const Gs::PageSet wanted = target->pages & ~target->valid;
        Gs::PageSet fromTargets = missing & newestMask_;
        if (fromTargets.any())
        {
            const Target* holders[4] = {};
            u32 holderCount = 0;
            for (u32 page = 0; page < Gs::PageCount; page++)
            {
                if (fromTargets.test(page))
                {
                    const Target* held = newest_[page];
                    if (std::find(holders, holders + holderCount, held) == holders + holderCount && holderCount < 4)
                    {
                        holders[holderCount++] = held;
                    }
                }
            }

            const Gs::PageSet candidates = wanted & newestMask_;
            for (u32 page = 0; page < Gs::PageCount; page++)
            {
                if (candidates.test(page) && std::find(holders, holders + holderCount, newest_[page]) != holders + holderCount)
                {
                    missing.set(page);
                }
            }
        }

        missing |= wanted & ~newestMask_ & ~memStale_;
    }

    // From memory's copy (other targets' newer pages written into it first), on the GPU
    SyncMem(missing);

    Flush();
    TargetFromMem(target, missing);
    target->valid |= missing;
}

void HwGs::MarkDrawn(Target* target, const Gs::PageSet& pages)
{
    Gs::PageSet drawn = pages & target->pages;
    // Already its newest, every other target's copy out of date: nothing changes
    if ((drawn & ~target->owned).none())
    {
        return;
    }

    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        if (drawn.test(page))
        {
            newest_[page] = target;
        }
    }

    newestMask_ |= drawn;
    memStale_ |= drawn;
    cpuStale_ |= drawn;
    for (auto& other : targets_)
    {
        if (other.get() != target)
        {
            other->valid &= ~drawn;
            other->owned &= ~drawn;
        }
    }

    target->owned |= drawn;
}

bool HwGs::Fallback(const Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count, const Rect& rect, const char* why)
{
    stats.fallbacks++;
    stats.lastFallback = why;
    if (!reported_[why])
    {
        reported_[why] = true;
        std::fprintf(stderr, "graphics: hardware renderer: left to the software GS: %s\n", why);
    }

    static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
    if (counting)
    {
        static std::map<std::string, u64> counts;
        static u64 total = 0;
        char detail[160];
        std::snprintf(detail, sizeof(detail), "%s (frame %x/%u/%x msk %08x z %x test %llx)", why, state.fbp, state.fbw, state.fpsm, state.fbmsk, state.zbp,
                      static_cast<unsigned long long>(state.context->test.value));
        counts[detail]++;
        if (++total % 2000 == 0)
        {
            for (auto& entry : counts)
            {
                std::fprintf(stderr, "fallbacks: %llu %s\n", static_cast<unsigned long long>(entry.second), entry.first.c_str());
            }
        }
    }

    Gs::PageSet pages;
    if (rect.right >= rect.left && rect.bottom >= rect.top)
    {
        Gs::PagesOfRect(state.fpsm, state.fbp, state.fbw, rect.left, rect.top, rect.right, rect.bottom, pages);
        if (state.ztst != Gs::ZtstAlways || state.zWritten)
        {
            Gs::PagesOfRect(state.zpsm, state.zbp, state.fbw, rect.left, rect.top, rect.right, rect.bottom, pages);
        }
    }

    if (state.textured)
    {
        Gs::TexturePagesForDraw(state, vertexes, count, pages);
    }

    downloadReason_ = "fallback";
    BeforeCpuWrite(pages);
    downloadReason_ = "";
    return false;
}

// The GL blend for the GS's ((A - B) * C >> 7) + D (A, B, D: 0 source, 1 destination, 2 zero); false when GL can't make it
bool BlendFor(u32 a, u32 b, u32 d, GLenum factor, GLenum inverse, u32* equation, u32* source, u32* destination)
{
    auto set = [&](GLenum e, GLenum s, GLenum dst) {
        *equation = e;
        *source = s;
        *destination = dst;
        return true;
    };
    if (a == b)
    {
        return d == 0 ? set(GL_FUNC_ADD, GL_ONE, GL_ZERO) : d == 1 ? set(GL_FUNC_ADD, GL_ZERO, GL_ONE) : set(GL_FUNC_ADD, GL_ZERO, GL_ZERO);
    }

    u32 key = a * 9 + b * 3 + d;
    switch (key)
    {
    case 0 * 9 + 1 * 3 + 1:
        return set(GL_FUNC_ADD, factor, inverse);
    case 0 * 9 + 1 * 3 + 2:
        return set(GL_FUNC_SUBTRACT, factor, factor);
    case 0 * 9 + 2 * 3 + 1:
        return set(GL_FUNC_ADD, factor, GL_ONE);
    case 0 * 9 + 2 * 3 + 2:
        return set(GL_FUNC_ADD, factor, GL_ZERO);
    case 1 * 9 + 0 * 3 + 0:
        return set(GL_FUNC_ADD, inverse, factor);
    case 1 * 9 + 0 * 3 + 2:
        return set(GL_FUNC_REVERSE_SUBTRACT, factor, factor);
    case 1 * 9 + 2 * 3 + 0:
        return set(GL_FUNC_ADD, GL_ONE, factor);
    case 1 * 9 + 2 * 3 + 2:
        return set(GL_FUNC_ADD, GL_ZERO, factor);
    case 2 * 9 + 0 * 3 + 1:
        return set(GL_FUNC_REVERSE_SUBTRACT, factor, GL_ONE);
    case 2 * 9 + 0 * 3 + 0:
        return set(GL_FUNC_ADD, inverse, GL_ZERO);
    case 2 * 9 + 0 * 3 + 2:
        return set(GL_FUNC_ADD, GL_ZERO, GL_ZERO);
    case 2 * 9 + 1 * 3 + 0:
        return set(GL_FUNC_SUBTRACT, GL_ONE, factor);
    case 2 * 9 + 1 * 3 + 1:
        return set(GL_FUNC_ADD, GL_ZERO, inverse);
    case 2 * 9 + 1 * 3 + 2:
        return set(GL_FUNC_ADD, GL_ZERO, GL_ZERO);
    default:
        return false;
    }
}

GLuint HwGs::AccumFramebufferFor(Target* colour, Target* depth)
{
    auto key = std::make_pair(colour, depth);
    auto found = accumFramebuffers_.find(key);
    if (found != accumFramebuffers_.end())
    {
        return found->second;
    }

    Gl& gl = GetGl();
    if (colour->accum == 0)
    {
        gl.GenTextures(1, &colour->accum);
        gl.BindTexture(GL_TEXTURE_2D, colour->accum);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA16F), static_cast<GLsizei>(colour->width * colour->scale),
                      static_cast<GLsizei>(colour->height * colour->scale), 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    }

    GLuint fbo = 0;
    gl.GenFramebuffers(1, &fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour->accum, 0);
    if (depth != nullptr)
    {
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depth->texture, 0);
    }

    accumFramebuffers_[key] = fbo;
    return fbo;
}

// The wrapping sums added to the target (its colour channels, modulo 256), once before anything else uses it
void HwGs::Resolve(Target* target)
{
    if (target == nullptr || !target->accumPending)
    {
        return;
    }

    target->accumPending = false;
    Gl& gl = GetGl();
    GLuint before = CopyTarget(target);
    const GLsizei w = static_cast<GLsizei>(target->width * target->scale);
    const GLsizei h = static_cast<GLsizei>(target->height * target->scale);
    gl.BindFramebuffer(GL_FRAMEBUFFER, target->fbo);
    Pass(target->fbo, "wrap-add resolve");
    gl.Viewport(0, 0, w, h);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    gl.UseProgram(resolveProgram_);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, before);
    gl.ActiveTexture(GL_TEXTURE0 + 1);
    gl.BindTexture(GL_TEXTURE_2D, target->accum);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.Uniform1i(gl.GetUniformLocation(resolveProgram_, "before"), 0);
    gl.Uniform1i(gl.GetUniformLocation(resolveProgram_, "sum"), 1);
    gl.BindVertexArray(emptyArray_);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

GLuint HwGs::CopyOf(Target* target)
{
    Flush();
    Resolve(target);
    return CopyTarget(target);
}

// A copy of a colour target (the destination a batch's shader reads, the texture of a target drawn into)
GLuint HwGs::CopyTarget(Target* target, const Rect* region)
{
    auto key = std::make_tuple(target->width * target->scale, target->height * target->scale);
    auto found = copies_.find(key);
    GLuint texture = 0;
    GLuint fbo = 0;
    Gl& gl = GetGl();
    if (found == copies_.end())
    {
        gl.GenTextures(1, &texture);
        gl.BindTexture(GL_TEXTURE_2D, texture);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(target->width * target->scale),
                      static_cast<GLsizei>(target->height * target->scale), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.GenFramebuffers(1, &fbo);
        gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        copies_[key] = {texture, fbo};
    }
    else
    {
        texture = found->second.first;
        fbo = found->second.second;
    }

    GLint w = static_cast<GLint>(target->width * target->scale);
    GLint h = static_cast<GLint>(target->height * target->scale);
    GLint x0 = 0;
    GLint y0 = 0;
    GLint x1 = w;
    GLint y1 = h;
    if (region != nullptr)
    {
        // Only the part the batch reads (its primitives' rectangle, a pixel more each way)
        const GLint n = static_cast<GLint>(target->scale);
        x0 = std::clamp((region->left - 1) * n, 0, w);
        y0 = std::clamp((region->top - 1) * n, 0, h);
        x1 = std::clamp((region->right + 2) * n, 0, w);
        y1 = std::clamp((region->bottom + 2) * n, 0, h);
    }

    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    Pass(fbo, "target copy");
    CopyRect(copyColourProgram_, target->texture, static_cast<f32>(w), static_cast<f32>(h), x0, y0, x1, y1);
    return texture;
}

Target* HwGs::ScratchStencil(Target* colour)
{
    auto found = scratchStencils_.find(colour);
    if (found != scratchStencils_.end())
    {
        return found->second.get();
    }

    auto made = std::make_unique<Target>();
    made->width = colour->width;
    made->height = colour->height;
    made->scale = colour->scale;
    made->depth = true;
    Gl& gl = GetGl();
    gl.GenTextures(1, &made->texture);
    gl.BindTexture(GL_TEXTURE_2D, made->texture);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH32F_STENCIL8), static_cast<GLsizei>(colour->width * colour->scale),
                  static_cast<GLsizei>(colour->height * colour->scale), 0, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, nullptr);
    Target* raw = made.get();
    scratchStencils_[colour] = std::move(made);
    return raw;
}

bool HwGs::Draw(Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count)
{
    MakeCurrent();
    ProcessWrites();
    // The colour target's pixels a PS2 pixel (set when it's known)
    s32 n = 1;

    // The rectangle the primitive can draw in (the PS2's pixels, inclusive)
    Rect rect;
    {
        s32 minX = 0x7FFFFFFF;
        s32 minY = 0x7FFFFFFF;
        s32 maxX = -0x7FFFFFFF;
        s32 maxY = -0x7FFFFFFF;
        for (u32 i = 0; i < count; i++)
        {
            s32 x = static_cast<s32>(vertexes[i].x) - state.offsetX;
            s32 y = static_cast<s32>(vertexes[i].y) - state.offsetY;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }

        // The GS's pixels: from the ceiling of the start to before the ceiling of the end (points and lines: the pixels their
        // ends are in)
        rect.left = std::max((minX + 15) >> 4, state.scissorLeft);
        rect.top = std::max((minY + 15) >> 4, state.scissorTop);
        rect.right = std::min(((maxX + 15) >> 4) - 1, state.scissorRight);
        rect.bottom = std::min(((maxY + 15) >> 4) - 1, state.scissorBottom);
        if (state.type == Gs::PrimPoint || state.type == Gs::PrimLine || state.type == Gs::PrimLineStrip)
        {
            rect.left = std::max(minX >> 4, state.scissorLeft);
            rect.top = std::max(minY >> 4, state.scissorTop);
            rect.right = std::min(maxX >> 4, state.scissorRight);
            rect.bottom = std::min(maxY >> 4, state.scissorBottom);
        }
    }

    if (rect.right < rect.left || rect.bottom < rect.top || state.ztst == Gs::ZtstNever)
    {
        stats.drawn++;
        return true;
    }

    // A mesh's next triangle: drawn as the last was
    if (DrawLikeLast(state, vertexes, count, rect))
    {
        return true;
    }

    // Else made the whole way (and the next drawn the same way if this one can be: not a glyph, a texture from a target or the
    // CPU's decode, or one read from upscaled pages)
    last_.valid = false;
    bool repeatable = true;

    // What the GPU can't draw exactly is the software GS's
    const bool depthUsed = state.ztst != Gs::ZtstAlways || state.zWritten;
    const u32 frameClass = FrameClassOf(state.fpsm);
    const bool frame16 = frameClass == ClassColour16 || frameClass == ClassColour16S || frameClass == ClassColourZ16 ||
                         frameClass == ClassColourZ16S;
    if (frameClass == ClassNone || IsDepthClass(frameClass))
    {
        return Fallback(state, vertexes, count, rect, "a frame in a depth or indexed format");
    }

    if (depthUsed && !IsDepthClass(ClassOf(state.zpsm)))
    {
        return Fallback(state, vertexes, count, rect, "a depth buffer in a colour format");
    }

    if (state.textured && (state.wms >= Gs::WrapRegionClamp || state.wmt >= Gs::WrapRegionClamp))
    {
        return Fallback(state, vertexes, count, rect, "a texture's region clamp or repeat");
    }

    BatchKey key;
    __builtin_memset_inline(&key, 0, sizeof(key));

    // The frame mask: whole channels by GL's colour mask; part of a channel by the shader on a copy of the destination
    // (a 16 bit frame's channels are its 5 bits and its alpha bit)
    u32 channelMasks[4];
    if (frame16)
    {
        channelMasks[0] = state.fbmsk & 0xF8;
        channelMasks[1] = (state.fbmsk >> 8) & 0xF8;
        channelMasks[2] = (state.fbmsk >> 16) & 0xF8;
        channelMasks[3] = (state.fbmsk >> 24) & 0x80;
    }
    else
    {
        for (u32 i = 0; i < 4; i++)
        {
            channelMasks[i] = (state.fbmsk >> (i * 8)) & 0xFF;
        }
    }

    const u32 channelFull[4] = {frame16 ? 0xF8u : 0xFFu, frame16 ? 0xF8u : 0xFFu, frame16 ? 0xF8u : 0xFFu, frame16 ? 0x80u : 0xFFu};
    bool shaderBlend = false;
    for (u32 i = 0; i < 4; i++)
    {
        if (channelMasks[i] != 0 && channelMasks[i] != channelFull[i])
        {
            shaderBlend = true;
        }
    }

    // The destination alpha test: by a stencil when the draw can't change the alpha bit it tests (every primitive of a batch
    // tests what was there before it), else in the shader
    const bool alphaKept = (channelMasks[3] & 0x80) != 0 || !state.frameWritten;
    bool stencilDate = false;
    if (state.date)
    {
        static const bool noStencilDate = std::getenv("TWIN_HW_NO_STENCIL_DATE") != nullptr;
        if (alphaKept && !noStencilDate)
        {
            stencilDate = true;
        }
        else
        {
            shaderBlend = true;
        }
    }

    // A wrapping additive blend into a 16 bit frame (the shadow volumes' counting: COLCLAMP off, D the destination, A, B and C
    // the source's or FIX, the alpha not written): summed in a float buffer, wrapped when it's resolved (its order doesn't
    // matter, so the batch needn't break where its primitives overlap)
    bool wrapAdd = false;
    static const bool noWrapAdd = std::getenv("TWIN_HW_NO_WRAPADD") != nullptr;
    if (!noWrapAdd && state.blend && !state.colclamp && !state.pabe && frame16 && state.blendD == 1 && state.blendA != 1 && state.blendB != 1 &&
        state.blendC != 1 && channelMasks[0] == 0 && channelMasks[1] == 0 && channelMasks[2] == 0 && channelMasks[3] == 0x80 &&
        !shaderBlend)
    {
        wrapAdd = true;
        key.wrapAdd = 1;
        key.gsBlend[0] = state.blendA;
        key.gsBlend[1] = state.blendB;
        key.gsBlend[2] = state.blendC;
        key.gsBlend[3] = state.blendD;
        key.gsFix = state.blendFix;
    }

    // The blend: GL's blending when it can make it exactly (a 16 bit frame's blend under a stencil DATE too: its truncation
    // to 5 bits is then left to the reads), else the shader
    if (state.blend && !wrapAdd)
    {
        GLenum factor = GL_SRC1_ALPHA;
        GLenum inverse = GL_ONE_MINUS_SRC1_ALPHA;
        bool glCan = state.colclamp && !state.pabe && (!frame16 || stencilDate);
        if (state.blendC == 2 && state.blendFix <= 0x80)
        {
            factor = GL_CONSTANT_ALPHA;
            inverse = GL_ONE_MINUS_CONSTANT_ALPHA;
            key.blendFix = state.blendFix;
        }
        else if (state.blendC != 0)
        {
            glCan = false;
        }

        if (glCan && !shaderBlend &&
            BlendFor(state.blendA, state.blendB, state.blendD, factor, inverse, &key.blendEquation, &key.blendSource,
                     &key.blendDestination))
        {
            key.blend = 1;
        }
        else
        {
            shaderBlend = true;
            key.blendEquation = 0;
            key.blendSource = 0;
            key.blendDestination = 0;
            key.blendFix = 0;
        }
    }

    u32 maskBytes[4];
    for (u32 i = 0; i < 4; i++)
    {
        // GL's colour mask: a channel wholly masked (the shader merges the partly masked ones)
        maskBytes[i] = channelMasks[i] == channelFull[i] ? 0xFF : 0;
    }

    static const bool why = std::getenv("TWIN_HW_WHY") != nullptr;
    if (why && shaderBlend && state.fbw == 4)
    {
        static int told = 0;
        if (told++ < 5)
        {
            std::fprintf(stderr, "shader blend: frame %x/%u/%x fbmsk %08x date %d datm %u alphaKept %d stencil %d blend %d abcd %u%u%u%u fix %x colclamp %d pabe %d masks %x %x %x %x\n",
                         state.fbp, state.fbw, state.fpsm, state.fbmsk, state.date, state.datm, alphaKept, stencilDate, state.blend, state.blendA,
                         state.blendB, state.blendC, state.blendD, state.blendFix, state.colclamp, state.pabe, channelMasks[0], channelMasks[1],
                         channelMasks[2], channelMasks[3]);
        }
    }

    if (shaderBlend)
    {
        key.gsBlend[0] = state.blend ? state.blendA : 0;
        key.gsBlend[1] = state.blend ? state.blendB : 0;
        key.gsBlend[2] = state.blend ? state.blendC : 0;
        key.gsBlend[3] = state.blend ? state.blendD : 0;
        key.gsFix = state.blend ? state.blendFix : 0;
        key.pabe = state.blend && state.pabe ? 1 : 0;
        key.blend = 0;
        for (u32 i = 0; i < 4; i++)
        {
            key.frameMask[i] = channelMasks[i];
        }
    }

    key.colclamp = state.colclamp ? 1 : 0;
    key.datm = state.datm;
    key.stencilDate = stencilDate ? 1 : 0;

    // A glyph image's sprite (the UI's glyphs)
    Gs::Vertex glyphed[3];
    if (state.textured && state.uv && state.type == Gs::PrimSprite)
    {
        repeatable = false;
        std::copy(vertexes, vertexes + count, glyphed);
        if (gs_.GlyphForBackend(state, glyphed, count))
        {
            vertexes = glyphed;
        }
    }

    // The targets
    // As tall as the primitive draws (a scissor taller than the buffer, as the game's full screen passes set, would make the
    // target run over the buffers past it)
    const u32 height = static_cast<u32>(rect.bottom + 1);
    Target* colour = TargetFor(state.fbp, state.fbw, state.fpsm, height, true);
    u32 highestZ = 0;
    for (u32 i = 0; i < count; i++)
    {
        highestZ = std::max(highestZ, vertexes[i].z);
    }

    nextDepthHigh_ = highestZ >= 0x80000000u;
    Target* depth = depthUsed ? TargetFor(state.zbp, state.fbw, state.zpsm, height) : nullptr;
    nextDepthHigh_ = false;
    n = static_cast<s32>(colour->scale);
    if (static_cast<u32>(rect.right) >= colour->width)
    {
        rect.right = static_cast<s32>(colour->width) - 1;
    }

    if (rect.right < rect.left)
    {
        stats.drawn++;
        return true;
    }

    // The texture
    u32 program = 0;
    bool textureFromScaled = false;
    GLuint texture = 0;
    u32 texAlpha = 0;
    if (state.textured)
    {
        const Gs::TextureLevel& level = state.levels[0];
        program |= BitTextured | (state.tfx << BitTfxShift) | (state.tcc ? BitTcc : 0) | (state.uv ? BitUv : 0);
        key.gsTexWidth = static_cast<f32>(level.width);
        key.gsTexHeight = static_cast<f32>(level.height);
        key.texWidth = key.gsTexWidth;
        key.texHeight = key.gsTexHeight;
        key.wrapS = state.wms == Gs::WrapClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        key.wrapT = state.wmt == Gs::WrapClamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        key.magFilter = state.magLinear ? GL_LINEAR : GL_NEAREST;
        key.minFilter = key.magFilter;
        if (state.mipmapped)
        {
            static constexpr GLenum Minified[] = {GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR,
                                                  GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR};
            key.minFilter = Minified[std::min<u32>(state.mmin, 5)];
            key.maxLevel = state.levelCount - 1;
            key.lodK = state.k;
            key.lodL = state.l;
            key.mxl = state.mxl;
            program |= BitMipmap | (state.lodConstant ? BitLodConstant : 0);
        }

        s32 left = 0;
        s32 top = 0;
        s32 right = static_cast<s32>(level.width) - 1;
        s32 bottom = static_cast<s32>(level.height) - 1;
        s32 l = 0;
        s32 t = 0;
        s32 r = 0;
        s32 b = 0;
        if (Gs::TextureRegionForDraw(state, vertexes, count, &l, &t, &r, &b))
        {
            // As the decoder takes it: a clamped coordinate's range clamped, a repeating one's whole unless it stays inside
            s32 w = static_cast<s32>(level.width);
            s32 h = static_cast<s32>(level.height);
            if (state.wms == Gs::WrapClamp || (l >= 0 && r < w))
            {
                left = std::clamp(l, 0, w - 1);
                right = std::clamp(r, 0, w - 1);
            }

            if (state.wmt == Gs::WrapClamp || (t >= 0 && b < h))
            {
                top = std::clamp(t, 0, h - 1);
                bottom = std::clamp(b, 0, h - 1);
            }
        }

        // The software GS's decoder (on the CPU) for replacement textures and their dumps
        static const bool cpuTextures = std::getenv("TWIN_HW_DUMP_TEXTURES") != nullptr || std::getenv("TWIN_HW_CPU_TEXTURES") != nullptr;
        // A target's own buffer in its own layout (no mipmaps; its rectangle within the target)
        Target* source = nullptr;
        u32 sourceClass = ClassOf(level.psm);
        if (level.cacheId == nullptr && !state.mipmapped && sourceClass != ClassNone && !IsDepthClass(sourceClass) &&
            right >= left && bottom >= top)
        {
            for (auto& known : targets_)
            {
                // The region the draw reads inside the target (a repeating texture whose coordinates stay inside it doesn't
                // repeat)
                // (two texels past its edge allowed: a sprite's end coordinate is past its last pixel's and the region reaches
                // one more for the filter; the filter's weight there is nothing and the sampler clamps)
                if (known->bp == level.bp && known->bw == level.bw && known->cls == sourceClass && left >= 0 && top >= 0 &&
                    static_cast<u32>(right) <= known->width + 1 && static_cast<u32>(bottom) <= known->height + 1 &&
                    (state.wms == Gs::WrapClamp || static_cast<u32>(right) < level.width) &&
                    (state.wmt == Gs::WrapClamp || static_cast<u32>(bottom) < level.height))
                {
                    source = known.get();
                }
            }
        }

        if (source != nullptr)
        {
            repeatable = false;
            Gs::PageSet pages;
            RectPages(source, Rect{left, top, std::min(right, static_cast<s32>(source->width) - 1),
                                   std::min(bottom, static_cast<s32>(source->height) - 1)}, pages);
            cpuReason = "texture";
            EnsureValid(source, pages);
            if (source != colour && source->accumPending)
            {
                Flush();
                Resolve(source);
            }

            texture = source == colour ? CopyOf(source) : source->texture;
            key.texWidth = static_cast<f32>(source->width);
            key.texHeight = static_cast<f32>(source->height);
            static const bool noSnap = std::getenv("TWIN_HW_NO_SNAP") != nullptr;
            if (!noSnap && source->scale > colour->scale && key.magFilter == GL_NEAREST && key.minFilter == GL_NEAREST)
            {
                key.texSnap = source->scale;
            }
            // Its edge isn't the texture's: a filter reaching past the region stops at the target's edge
            key.wrapS = GL_CLAMP_TO_EDGE;
            key.wrapT = GL_CLAMP_TO_EDGE;
            if (level.psm == Gs::PSMCT24)
            {
                texAlpha = 1;
            }
            else if (sourceClass != ClassColour32)
            {
                texAlpha = 2;
            }
        }
        else if (level.cacheId == nullptr && !cpuTextures)
        {
            // Decoded on the GPU from memory's copy
            texture = DecodeOnGpu(state, left, top, right, bottom);
            // Decoded from memory read back from an upscaled target: its texels are the blocks' first samples
            Gs::PageSet texturePages;
            Gs::PagesOfRect(level.psm, level.bp, level.bw, left, top, right, bottom, texturePages);
            textureFromScaled = (texturePages & scaledPages_).any();
            // Repeated while it's the texture DecodeOnGpu has at once, as long as no target can be its source (whatever the
            // next primitive's region) and no page of it is upscaled
            bool sourceless = true;
            if (!state.mipmapped && sourceClass != ClassNone && !IsDepthClass(sourceClass))
            {
                for (auto& known : targets_)
                {
                    if (known->bp == level.bp && known->bw == level.bw && known->cls == sourceClass)
                    {
                        sourceless = false;
                        break;
                    }
                }
            }

            repeatable = repeatable && sourceless && !textureFromScaled;
            if (g_FilterMode >= 1)
            {
                key.magFilter = GL_LINEAR;
                key.minFilter = state.mipmapped ? (g_FilterMode == 2 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST) : GL_LINEAR;
                key.anisotropy = g_FilterMode == 2 && state.mipmapped ? 16 : 1;
            }
        }
        else
        {
            repeatable = false;
            // Decoded by the software GS from local memory (made current for the pages it reads)
            if (level.cacheId == nullptr)
            {
                // Local memory current for the levels' whole pages: nothing to sync (the usual case); else the pages the
                // decode reads
                ProcessWrites();
                bool synced = true;
                for (u32 i = 0; i < state.levelCount && synced; i++)
                {
                    const Gs::TextureLevel& lv = state.levels[i];
                    auto levelKey = std::make_tuple(lv.bp, lv.bw, lv.psm, lv.width, lv.height);
                    auto found = levelPages_.find(levelKey);
                    if (found == levelPages_.end())
                    {
                        Gs::PageSet whole;
                        Gs::PagesOfRect(lv.psm, lv.bp, lv.bw, 0, 0, static_cast<s32>(lv.width) - 1, static_cast<s32>(lv.height) - 1,
                                        whole);
                        found = levelPages_.emplace(levelKey, whole).first;
                    }

                    synced = (found->second & newestMask_).none();
                }

                if (!synced)
                {
                    Gs::PageSet pages;
                    Gs::TexturePagesForDraw(state, vertexes, count, pages);
                    static char reasonText[128];
                    std::snprintf(reasonText, sizeof(reasonText), "texture %x/%u/%x %ux%u for frame %x/%u/%x", level.bp, level.bw, level.psm,
                                  level.width, level.height, state.fbp, state.fbw, state.fpsm);
                    downloadReason_ = reasonText;
                    BeforeCpuRead(pages);
                    downloadReason_ = "";
                }

                gs_.DecodeForBackend(state, vertexes, count);
            }

            CachedTexture& cached = textures_[state.levels[0].cacheId];
            Gl& gl = GetGl();
            const u32 levels = state.levelCount;
            if (cached.id == 0 || cached.width != level.width || cached.height != level.height || cached.levels != levels)
            {
                Flush();
                if (cached.id == 0)
                {
                    gl.GenTextures(1, &cached.id);
                }

                gl.BindTexture(GL_TEXTURE_2D, cached.id);
                for (u32 i = 0; i < levels; i++)
                {
                    gl.TexImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), static_cast<GLint>(GL_RGBA8),
                                  static_cast<GLsizei>(state.levels[i].width), static_cast<GLsizei>(state.levels[i].height), 0, GL_RGBA,
                                  GL_UNSIGNED_BYTE, nullptr);
                }

                gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(levels - 1));
                cached.width = level.width;
                cached.height = level.height;
                cached.levels = levels;
                std::fill(std::begin(cached.versions), std::end(cached.versions), ~0u);
                cached.tiles.assign(static_cast<size_t>((level.width + 7) / 8) * ((level.height + 7) / 8), 0);
            }

            // Pending draws may sample the texture: they're drawn before texels they read change (its content version), not
            // for tiles added (they read none of those)
            bool prepared = false;
            auto prepare = [&]() {
                if (!prepared)
                {
                    bool changed = false;
                    for (u32 i = 0; i < levels; i++)
                    {
                        changed |= cached.versions[i] != state.levels[i].version && cached.versions[i] != ~0u;
                    }

                    if (changed)
                    {
                        Flush();
                    }

                    gl.BindTexture(GL_TEXTURE_2D, cached.id);
                    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
                    prepared = true;
                }
            };
            for (u32 i = 0; i < levels; i++)
            {
                const Gs::TextureLevel& lv = state.levels[i];
                if (lv.texels == nullptr)
                {
                    continue;
                }

                // Level 0's region (the tiles of it not uploaded at this version); the others whole when they change
                Rect want = {0, 0, static_cast<s32>(lv.width) - 1, static_cast<s32>(lv.height) - 1};
                if (i == 0)
                {
                    if (cached.versions[0] != lv.version)
                    {
                        std::fill(cached.tiles.begin(), cached.tiles.end(), 0);
                    }

                    if (left <= right && top <= bottom)
                    {
                        want = {left, top, right, bottom};
                    }

                    const u32 tilesWide = (lv.width + 7) / 8;
                    Rect missing = {0x7FFF, 0x7FFF, -1, -1};
                    for (s32 ty = want.top / 8; ty <= want.bottom / 8; ty++)
                    {
                        for (s32 tx = want.left / 8; tx <= want.right / 8; tx++)
                        {
                            u8& tile = cached.tiles[static_cast<size_t>(ty) * tilesWide + static_cast<size_t>(tx)];
                            if (tile == 0)
                            {
                                tile = 1;
                                missing.left = std::min(missing.left, tx * 8);
                                missing.top = std::min(missing.top, ty * 8);
                                missing.right = std::max(missing.right, std::min(tx * 8 + 7, static_cast<s32>(lv.width) - 1));
                                missing.bottom = std::max(missing.bottom, std::min(ty * 8 + 7, static_cast<s32>(lv.height) - 1));
                            }
                        }
                    }

                    cached.versions[0] = lv.version;
                    if (missing.right < missing.left)
                    {
                        continue;
                    }

                    // The missing tiles' rectangle: any other tile in it is either uploaded already (decoded, the same texels)
                    // or not reached by a draw yet
                    want = missing;
                }
                else if (cached.versions[i] == lv.version && cached.tilesVersions[i] == lv.tilesVersion)
                {
                    continue;
                }

                prepare();
                gl.PixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(lv.stride));
                const u32* from = lv.texels + static_cast<size_t>(want.top) * lv.stride + static_cast<size_t>(want.left);
                gl.TexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(i), want.left, want.top, want.right - want.left + 1,
                                 want.bottom - want.top + 1, GL_RGBA, GL_UNSIGNED_BYTE, from);
                cached.versions[i] = lv.version;
                cached.tilesVersions[i] = lv.tilesVersion;
            }

            gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            texture = cached.id;

            // A replacement of level 0 (PCSX2's packs), looked for when its texels change; and the dump of it in PCSX2's
            // names (TWIN_HW_DUMP_TEXTURES=folder)
            static const char* dumpFolder = std::getenv("TWIN_HW_DUMP_TEXTURES");
            const Gs::TextureLevel& base = state.levels[0];
            if (base.cacheId != nullptr && base.texels != nullptr && (ReplacementsEnabled() || dumpFolder != nullptr) &&
                cached.replacementVersion != base.version)
            {
                cached.replacementVersion = base.version;
                NativeGraphics::ReplacementKey replacementKey;
                bool keyed = NativeGraphics::ReplacementKeyFor(gs_, base, state.context->tex0, state.context->clamp, &replacementKey);
                if (keyed && dumpFolder != nullptr && !cached.dumped && base.stride == base.width &&
                    (left <= 0 && top <= 0 && right >= static_cast<s32>(base.width) - 1 && bottom >= static_cast<s32>(base.height) - 1))
                {
                    // The whole texture decoded (a draw that read all of it): written as PCSX2 dumps it, alpha in the PS2's range
                    cached.dumped = true;
                    std::string path = std::string(dumpFolder) + "/" + NativeGraphics::ReplacementName(replacementKey) + ".png";
                    WritePng(path, base.texels, base.width, base.height);
                }

                const NativeGraphics::ReplacementImage* image = keyed ? NativeGraphics::FindReplacement(replacementKey) : nullptr;
                if (image != nullptr)
                {
                    Flush();
                    if (cached.replacement == 0)
                    {
                        gl.GenTextures(1, &cached.replacement);
                    }

                    gl.BindTexture(GL_TEXTURE_2D, cached.replacement);
                    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
                    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(image->width),
                                  static_cast<GLsizei>(image->height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image->pixels.data());
                    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
                    gl.GenerateMipmap(GL_TEXTURE_2D);
                    cached.replacementMipmaps = true;
                }
                else if (cached.replacement != 0)
                {
                    Flush();
                    gl.DeleteTextures(1, &cached.replacement);
                    cached.replacement = 0;
                }
            }

            if (cached.replacement != 0 && ReplacementsEnabled())
            {
                // Sampled with the original's normalised coordinates; its mipmaps for the original's levels
                texture = cached.replacement;
                stats.replaced++;
            }
            // The filtering setting (the game's textures, not its buffers)
            if (g_FilterMode >= 1)
            {
                key.magFilter = GL_LINEAR;
                key.minFilter = state.mipmapped ? (g_FilterMode == 2 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST) : GL_LINEAR;
                key.anisotropy = g_FilterMode == 2 && state.mipmapped ? 16 : 1;
            }
            if (textures_.size() > 4096)
            {
                // Too many: they're made again as they're needed
                Flush();
                for (auto& entry : textures_)
                {
                    gl.DeleteTextures(1, &entry.second.id);
                }

                textures_.clear();
                return Draw(state, vertexes, count);
            }
        }

        program |= texAlpha << BitTexAlphaShift;
        key.texture = texture;
        if (texAlpha != 0)
        {
            Gs::RegTexa texa = gs_.texa();
            key.ta0 = texa.ta0;
            key.ta1 = texa.ta1;
            key.aem = texa.aem;
        }
    }

    // The targets current for the pages the primitive draws in
    Gs::PageSet drawnPages;
    RectPages(colour, rect, drawnPages);
    cpuReason = "frame";
    EnsureValid(colour, drawnPages);
    Gs::PageSet depthPages;
    if (depth != nullptr)
    {
        RectPages(depth, rect, depthPages);
        cpuReason = "depth";
        EnsureValid(depth, depthPages);
    }

    // The rest of the key
    if (state.fog)
    {
        program |= BitFog;
        key.fogColour = state.fogR | state.fogG << 8 | state.fogB << 16;
    }

    if (frame16)
    {
        program |= BitFrame16;
        if (state.dither && g_DitherMode != 0)
        {
            program |= BitDither;
            key.dimx = gs_.dimx();
            key.ditherScale = g_DitherMode == 1 ? static_cast<u32>(n) : 1;
        }
    }

    if (state.fba)
    {
        program |= BitFba;
    }

    if (wrapAdd)
    {
        program |= BitWrapAdd;
    }

    if (shaderBlend)
    {
        program |= BitShaderBlend;
        if (state.date && !stencilDate)
        {
            program |= BitDate;
        }

        if (state.fpsm == Gs::PSMCT24 || state.fpsm == Gs::PSMZ24)
        {
            program |= BitFrame24;
        }
    }

    if (depth != nullptr)
    {
        program |= BitDepth;
        key.depthReverse = depth->reversed ? 1 : 0;
        key.zMaxU = state.zmax;
        if (depth->reversed)
        {
            key.depthFunc = state.ztst == Gs::ZtstAlways ? GL_ALWAYS : state.ztst == Gs::ZtstGequal ? GL_LEQUAL : GL_LESS;
        }
        else
        {
            key.depthFunc = state.ztst == Gs::ZtstAlways ? GL_ALWAYS : state.ztst == Gs::ZtstGequal ? GL_GEQUAL : GL_GREATER;
        }

        // A texture at the PS2's resolution made from upscaled buffers (an effect's lookup of the depth, say) tested against
        // the upscaled depth: tested at the GS's sample point instead, as the texture was made there (else the samples near
        // where the test changes pass with the texel of the other side: a line along it)
        static const bool noNativeDepth = std::getenv("TWIN_HW_NO_NATIVE_DEPTH") != nullptr;
        if (!noNativeDepth && textureFromScaled && depth->scale > 1 && !state.zWritten &&
            (state.ztst == Gs::ZtstGequal || state.ztst == Gs::ZtstGreater))
        {
            key.nativeDepthTest = state.ztst;
            key.depthFunc = GL_ALWAYS;
            program |= BitNativeDepth;
        }
        key.depthWrite = state.zWritten ? 1 : 0;
        key.zMax = static_cast<f32>(state.zmax);
    }

    if (state.alphaTest && state.atst != Gs::AtstAlways)
    {
        program |= BitAlphaTest | (state.atst << BitAtstShift);
        key.aref = state.aref;
        key.afail = state.afail;
        key.alphaTested = 1;
    }

    key.colourMask = (maskBytes[0] == 0 ? 1 : 0) | (maskBytes[1] == 0 ? 2 : 0) | (maskBytes[2] == 0 ? 4 : 0) |
                     (maskBytes[3] == 0 ? 8 : 0);
    if (frame16)
    {
        // A 16 bit frame's alpha is its top bit: FBMSK's top bit masks it
        key.colourMask = (key.colourMask & 7) | ((state.fbmsk & 0x80000000u) == 0 ? 8 : 0);
    }

    key.scissor[0] = state.scissorLeft;
    key.scissor[1] = state.scissorTop;
    key.scissor[2] = state.scissorRight;
    key.scissor[3] = state.scissorBottom;
    key.colour = colour;
    key.depth = depth;
    key.program = program;
    key.primitive = state.type == Gs::PrimPoint ? GL_POINTS
                    : (state.type == Gs::PrimLine || state.type == Gs::PrimLineStrip) ? GL_LINES
                                                                                       : GL_TRIANGLES;
    // The batch's key kept when it's the same (most primitives: a mesh's run): copied only when it changes
    const bool sameKey = batching_ && SameBatchKey(key, key_);
    if (batching_ && !sameKey)
    {
        Flush();
    }

    static const char* tracePrim = std::getenv("TWIN_HW_TRACE_PRIM");
    if (tracePrim != nullptr && std::strtoull(tracePrim, nullptr, 10) == gs_.primitivesDrawn)
    {
        std::fprintf(stderr, "hwprim: #%llu program %x depthFunc %x depthWrite %u colourMask %x stencilDate %u datm %u wrapAdd %u "
                     "blend %u gsBlend %u,%u,%u,%u fix %u colclamp %u depth %p scissor %d,%d,%d,%d rect %d,%d,%d,%d\n",
                     static_cast<unsigned long long>(gs_.primitivesDrawn), key.program, key.depthFunc, key.depthWrite, key.colourMask,
                     key.stencilDate, key.datm, key.wrapAdd, key.blend, key.gsBlend[0], key.gsBlend[1], key.gsBlend[2], key.gsBlend[3],
                     key.gsFix, key.colclamp, static_cast<void*>(key.depth), key.scissor[0], key.scissor[1],
                     key.scissor[2], key.scissor[3], rect.left, rect.top, rect.right, rect.bottom);
    }

    // (A new key is the batch's after the flush above: the overlap's flush in AddPrimitive has nothing of it to draw then)
    if (!sameKey)
    {
        __builtin_memcpy_inline(&key_, &key, sizeof(key));
    }

    AddPrimitive(state, vertexes, rect, colour, depth, n, shaderBlend, drawnPages, depthPages);
    if (repeatable && tracePrim == nullptr)
    {
        last_.valid = true;
        last_.generation = gs_.stateGeneration();
        last_.clut = gs_.clutVersion();
        last_.targetsVersion = targetsVersion_;
        last_.filterMode = g_FilterMode;
        last_.ditherMode = g_DitherMode;
        last_.colour = colour;
        last_.depth = depth;
        last_.texture = state.textured ? lastTexture_ : nullptr;
        last_.textureId = texture;
        last_.shaderBlend = shaderBlend;
    }

    return true;
}

// The primitive drawn as the last one was: nothing it depended on changed but its vertexes. Its key would be the batch's, its
// targets those TargetFor gave (the primitive no taller than they are), its texture what DecodeOnGpu gives at once (the region
// it reads doesn't matter then); its pages are made current and drawn as every primitive's ($TWIN_HW_NO_FAST_DRAW: never)
bool HwGs::DrawLikeLast(const Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, u32 count, Rect rect)
{
    static const bool off = std::getenv("TWIN_HW_NO_FAST_DRAW") != nullptr;
    const LastDraw& last = last_;
    if (off || !last.valid || last.generation != gs_.stateGeneration() || last.clut != gs_.clutVersion() ||
        last.targetsVersion != targetsVersion_ || last.filterMode != g_FilterMode || last.ditherMode != g_DitherMode)
    {
        return false;
    }

    // (The targets' heights are whole pages, as TargetFor rounds the height it's asked for)
    Target* colour = last.colour;
    Target* depth = last.depth;
    const u32 height = static_cast<u32>(rect.bottom + 1);
    if (height > colour->height || (depth != nullptr && height > depth->height))
    {
        return false;
    }

    // DecodeOnGpu's first test, and no upscaled page in any region of the texture
    if (last.texture != nullptr)
    {
        const GpuTexture& t = *last.texture;
        if (!t.complete || t.checkedEpoch != epoch_ || t.shown == 0 || t.shown != last.textureId || (t.pages & newestMask_).any() ||
            (t.pages & scaledPages_).any())
        {
            return false;
        }
    }

    const s32 n = static_cast<s32>(colour->scale);
    if (static_cast<u32>(rect.right) >= colour->width)
    {
        rect.right = static_cast<s32>(colour->width) - 1;
    }

    if (rect.right < rect.left)
    {
        stats.drawn++;
        return true;
    }

    Gs::PageSet drawnPages;
    RectPages(colour, rect, drawnPages);
    cpuReason = "frame";
    EnsureValid(colour, drawnPages);
    Gs::PageSet depthPages;
    if (depth != nullptr)
    {
        RectPages(depth, rect, depthPages);
        cpuReason = "depth";
        EnsureValid(depth, depthPages);
    }

    AddPrimitive(state, vertexes, rect, colour, depth, n, last.shaderBlend, drawnPages, depthPages);
    return true;
}

void HwGs::AddPrimitive(const Gs::Gs::DrawState& state, const Gs::Vertex* vertexes, const Rect& rect, Target* colour, Target* depth,
                        s32 n, bool shaderBlend, const Gs::PageSet& drawnPages, const Gs::PageSet& depthPages)
{
    if (batching_ && shaderBlend)
    {
        for (const Rect& earlier : batchRects_)
        {
            if (rect.left <= earlier.right && earlier.left <= rect.right && rect.top <= earlier.bottom && earlier.top <= rect.bottom)
            {
                Flush();
                break;
            }
        }
    }

    batching_ = true;
    if (shaderBlend)
    {
        batchRects_.push_back(rect);
    }

    // The vertexes
    auto place = [&](const Gs::Vertex& v, HwVertex& out) {
        const f32 nudge = VertexNudge();
        out.x = static_cast<f32>(static_cast<s32>(v.x) - state.offsetX) / 16.0f * static_cast<f32>(n) + 0.5f + nudge;
        out.y = static_cast<f32>(static_cast<s32>(v.y) - state.offsetY) / 16.0f * static_cast<f32>(n) + 0.5f + nudge;
        out.z = v.z;
        out.r = v.r;
        out.g = v.g;
        out.b = v.b;
        out.a = v.a;
        if (state.uv)
        {
            out.s = static_cast<f32>(v.u) / 16.0f;
            out.t = static_cast<f32>(v.v) / 16.0f;
            out.q = 1.0f;
        }
        else
        {
            out.s = v.s;
            out.t = v.t;
            out.q = v.q;
        }

        out.fog = static_cast<f32>(v.f);
    };

    switch (state.type)
    {
    case Gs::PrimSprite:
    {
        // Two corners: the quad between them, its texture coordinates the corners', everything else the second vertex's
        const Gs::Vertex& a = vertexes[0];
        const Gs::Vertex& b = vertexes[1];
        HwVertex corners[4];
        Gs::Vertex v = b;
        for (u32 i = 0; i < 4; i++)
        {
            const Gs::Vertex& xs = (i & 1) ? b : a;
            const Gs::Vertex& ys = (i & 2) ? b : a;
            v.x = xs.x;
            v.u = xs.u;
            v.s = xs.s;
            v.y = ys.y;
            v.v = ys.v;
            v.t = ys.t;
            v.q = b.q;
            place(v, corners[i]);
        }

        if (!state.uv)
        {
            // STQ sprites: S over Q and T over Q across, with the second vertex's Q
            for (u32 i = 0; i < 4; i++)
            {
                const Gs::Vertex& xs = (i & 1) ? b : a;
                const Gs::Vertex& ys = (i & 2) ? b : a;
                corners[i].s = xs.s;
                corners[i].t = ys.t;
                corners[i].q = b.q;
            }
        }

        static constexpr u32 Order[6] = {0, 1, 2, 1, 3, 2};
        for (u32 i : Order)
        {
            vertexes_.push_back(corners[i]);
        }

        break;
    }
    case Gs::PrimPoint:
    {
        HwVertex out;
        place(vertexes[0], out);
        vertexes_.push_back(out);
        break;
    }
    case Gs::PrimLine:
    case Gs::PrimLineStrip:
    {
        HwVertex out[2];
        place(vertexes[0], out[0]);
        place(vertexes[1], out[1]);
        if (!state.gouraud)
        {
            out[0].r = out[1].r;
            out[0].g = out[1].g;
            out[0].b = out[1].b;
            out[0].a = out[1].a;
        }

        vertexes_.push_back(out[0]);
        vertexes_.push_back(out[1]);
        break;
    }
    default:
    {
        HwVertex out[3];
        for (u32 i = 0; i < 3; i++)
        {
            place(vertexes[i], out[i]);
        }

        if (!state.gouraud)
        {
            for (u32 i = 0; i < 2; i++)
            {
                out[i].r = out[2].r;
                out[i].g = out[2].g;
                out[i].b = out[2].b;
                out[i].a = out[2].a;
            }
        }

        for (const HwVertex& v : out)
        {
            vertexes_.push_back(v);
        }

        break;
    }
    }

    MarkDrawn(colour, drawnPages);
    if (depth != nullptr && state.zWritten)
    {
        MarkDrawn(depth, depthPages);
    }

    static const char* tracePrim = std::getenv("TWIN_HW_TRACE_PRIM");
    if (tracePrim != nullptr && std::strtoull(tracePrim, nullptr, 10) == gs_.primitivesDrawn)
    {
        for (size_t i = vertexes_.size() >= 3 ? vertexes_.size() - 3 : 0; i < vertexes_.size(); i++)
        {
            std::fprintf(stderr, "hwprim:   vertex %.6f,%.6f z %x (of %zu in the batch; colour target %ux%u scale %u)\n", vertexes_[i].x,
                         vertexes_[i].y, vertexes_[i].z, vertexes_.size(), colour->width, colour->height, colour->scale);
        }
    }

    stats.drawn++;
}

void HwGs::Flush()
{
    if (!batching_ || vertexes_.empty())
    {
        batching_ = false;
        vertexes_.clear();
        batchRects_.clear();
        return;
    }

    MakeCurrent();
    stats.batches++;
    const BatchKey& k = key_;
    Gl& gl = GetGl();
    const GLsizei n = static_cast<GLsizei>(k.colour->scale);
    const GLsizei width = static_cast<GLsizei>(k.colour->width) * n;
    const GLsizei height = static_cast<GLsizei>(k.colour->height) * n;
    // The destination as it was before the batch, for the shader's blend and the destination alpha test
    // A batch other than a wrapping sum sees the sums added first
    if (!k.wrapAdd && k.colour->accumPending)
    {
        Resolve(k.colour);
    }

    Target* depthStencil = k.depth;
    if (k.stencilDate && depthStencil == nullptr)
    {
        depthStencil = ScratchStencil(k.colour);
    }

    const bool stencilKept = k.stencilDate && depthStencil->stencilFor == k.colour && depthStencil->stencilDatm == k.datm &&
                             depthStencil->stencilAlphaVersion == k.colour->alphaVersion &&
                             std::memcmp(depthStencil->stencilScissor, k.scissor, sizeof(k.scissor)) == 0;
    GLuint destination = 0;
    if ((k.program & BitShaderBlend) || (k.stencilDate && !stencilKept))
    {
        // The shader's blend reads the batch's rectangle; the stencil's pass the whole scissor
        Rect region = {0x7FFF, 0x7FFF, -1, -1};
        for (const Rect& r : batchRects_)
        {
            region.left = std::min(region.left, r.left);
            region.top = std::min(region.top, r.top);
            region.right = std::max(region.right, r.right);
            region.bottom = std::max(region.bottom, r.bottom);
        }

        bool regional = (k.program & BitShaderBlend) && !k.stencilDate && region.right >= region.left;
        destination = CopyTarget(k.colour, regional ? &region : nullptr);
        stats.copies++;
        static const bool counting = std::getenv("TWIN_HW_STATS") != nullptr;
        if (counting)
        {
            static std::map<std::string, u64> counts;
            static u64 total = 0;
            char detail[160];
            std::snprintf(detail, sizeof(detail), "copy of %x/%u class %u: %s, %zu vertexes", k.colour->bp, k.colour->bw, k.colour->cls,
                          (k.program & BitShaderBlend) ? "shader blend" : "stencil DATE", vertexes_.size());
            counts[detail]++;
            if (++total % 400 == 0)
            {
                for (auto& entry : counts)
                {
                    std::fprintf(stderr, "copies: %llu %s\n", static_cast<unsigned long long>(entry.second), entry.first.c_str());
                }
            }
        }
    }

    // The depth target as floats, for the shader's native depth test
    GLuint depthCopy = 0;
    if ((k.program & BitNativeDepth) && k.depth != nullptr)
    {
        const u32 w = k.depth->width * k.depth->scale;
        const u32 h = k.depth->height * k.depth->scale;
        auto& copy = depthCopies_[{w, h}];
        if (copy.first == 0)
        {
            gl.GenTextures(1, &copy.first);
            gl.BindTexture(GL_TEXTURE_2D, copy.first);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32F), static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RED, GL_FLOAT, nullptr);
            gl.GenFramebuffers(1, &copy.second);
            gl.BindFramebuffer(GL_FRAMEBUFFER, copy.second);
            gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, copy.first, 0);
        }

        gl.BindFramebuffer(GL_FRAMEBUFFER, copy.second);
        Pass(copy.second, "depth copy");
        CopyRect(copyColourProgram_, k.depth->texture, static_cast<f32>(w), static_cast<f32>(h), 0, 0, static_cast<s32>(w), static_cast<s32>(h));
        depthCopy = copy.first;
    }

    if (k.wrapAdd)
    {
        // Into the target's float buffer of sums (cleared when a run of them starts)
        gl.BindFramebuffer(GL_FRAMEBUFFER, AccumFramebufferFor(k.colour, depthStencil));
        Pass(AccumFramebufferFor(k.colour, depthStencil), "draw (wrap-add sums)");
        if (!k.colour->accumPending)
        {
            gl.Disable(GL_SCISSOR_TEST);
            gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            gl.Clear(GL_COLOR_BUFFER_BIT);
            k.colour->accumPending = true;
        }
    }
    else
    {
        gl.BindFramebuffer(GL_FRAMEBUFFER, FramebufferFor(k.colour, depthStencil));
        Pass(FramebufferFor(k.colour, depthStencil), "draw");
    }

    gl.Viewport(0, 0, width, height);
    gl.Enable(GL_SCISSOR_TEST);
    gl.Scissor(k.scissor[0] * n, k.scissor[1] * n, (k.scissor[2] - k.scissor[0] + 1) * n, (k.scissor[3] - k.scissor[1] + 1) * n);
    gl.Disable(GL_STENCIL_TEST);
    // The stencil the destination alpha test made last time on this buffer is good while the target's alpha, DATM and the
    // scissor are the same: no copy, no prepass
    bool stencilReady = k.stencilDate && depthStencil->stencilFor == k.colour && depthStencil->stencilDatm == k.datm &&
                        depthStencil->stencilAlphaVersion == k.colour->alphaVersion &&
                        std::memcmp(depthStencil->stencilScissor, k.scissor, sizeof(k.scissor)) == 0;
    if (k.stencilDate && stencilReady)
    {
        gl.Enable(GL_STENCIL_TEST);
        gl.StencilFunc(GL_EQUAL, 1, 0xFF);
        gl.StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        gl.StencilMask(0);
    }
    else if (k.stencilDate)
    {
        depthStencil->stencilFor = k.colour;
        depthStencil->stencilDatm = k.datm;
        depthStencil->stencilAlphaVersion = k.colour->alphaVersion;
        std::memcpy(depthStencil->stencilScissor, k.scissor, sizeof(k.scissor));
        // Stencil 1 where the destination's alpha bit is DATM, then the draw only there
        gl.Disable(GL_DEPTH_TEST);
        gl.Disable(GL_BLEND);
        gl.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        gl.DepthMask(GL_FALSE);
        gl.Enable(GL_STENCIL_TEST);
        gl.StencilMask(0xFF);
        gl.ClearStencil(0);
        gl.Clear(GL_STENCIL_BUFFER_BIT);
        gl.StencilFunc(GL_ALWAYS, 1, 0xFF);
        gl.StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        gl.UseProgram(dateProgram_);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, destination);
        gl.Uniform1i(gl.GetUniformLocation(dateProgram_, "destination"), 0);
        gl.Uniform1i(gl.GetUniformLocation(dateProgram_, "datm"), static_cast<GLint>(k.datm));
        gl.BindVertexArray(emptyArray_);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl.StencilFunc(GL_EQUAL, 1, 0xFF);
        gl.StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        gl.StencilMask(0);
    }

    gl.Disable(GL_CULL_FACE);
    gl.Enable(GL_PROGRAM_POINT_SIZE);
    if (k.depth != nullptr)
    {
        gl.Enable(GL_DEPTH_TEST);
        gl.DepthFunc(k.depthFunc);
    }
    else
    {
        gl.Disable(GL_DEPTH_TEST);
    }

    if (k.wrapAdd)
    {
        gl.Enable(GL_BLEND);
        gl.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        gl.BlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
    }
    else if (k.blend)
    {
        gl.Enable(GL_BLEND);
        gl.BlendEquationSeparate(k.blendEquation, GL_FUNC_ADD);
        gl.BlendFuncSeparate(k.blendSource, k.blendDestination, GL_ONE, GL_ZERO);
        f32 fix = std::min(static_cast<f32>(k.blendFix) / 128.0f, 1.0f);
        gl.BlendColor(0.0f, 0.0f, 0.0f, fix);
    }
    else
    {
        gl.Disable(GL_BLEND);
    }

    if (k.texture != 0)
    {
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, k.texture);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(k.minFilter));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(k.magFilter));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(k.wrapS));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(k.wrapT));
        gl.TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, static_cast<f32>(k.anisotropy != 0 ? k.anisotropy : 1));
    }

    gl.BindVertexArray(vertexArray_);
    gl.BindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexes_.size() * sizeof(HwVertex)), vertexes_.data(), GL_STREAM_DRAW);

    const Gs::Gs& gs = gs_;
    (void)gs;
    auto draw = [&](bool failPass, bool colourOn, bool alphaOn, bool depthOn) {
        const Program& p = ProgramFor(k.program | (failPass ? BitFailPass : 0));
        gl.UseProgram(p.id);
        // A viewport far bigger than the target (the scissor keeps the drawing in it): primitives reaching past the target
        // aren't clipped, which would move their edges by the rasteriser's snapping of the new vertexes (a sixteenth of a
        // pixel, enough to lose a thin primitive's pixels); powers of two, so the positions stay exact
        static const bool smallViewport = std::getenv("TWIN_HW_SMALL_VIEWPORT") != nullptr;
        if (smallViewport)
        {
            gl.Viewport(0, 0, width, height);
            gl.Uniform2f(p.viewSize, static_cast<f32>(width), static_cast<f32>(height));
            gl.Uniform2f(p.viewOrigin, 0.0f, 0.0f);
        }
        else
        {
            gl.Viewport(-BigViewOrigin, -BigViewOrigin, BigViewSize, BigViewSize);
            gl.Uniform2f(p.viewSize, static_cast<f32>(BigViewSize), static_cast<f32>(BigViewSize));
            gl.Uniform2f(p.viewOrigin, static_cast<f32>(BigViewOrigin), static_cast<f32>(BigViewOrigin));
        }

        gl.Uniform1f(p.zMax, k.zMax);
        gl.Uniform1ui(p.zMaxU, k.zMaxU);
        gl.Uniform1i(p.depthReverse, static_cast<GLint>(k.depthReverse));
        if (k.program & BitNativeDepth)
        {
            gl.ActiveTexture(GL_TEXTURE0 + 4);
            gl.BindTexture(GL_TEXTURE_2D, depthCopy);
            gl.ActiveTexture(GL_TEXTURE0);
            gl.Uniform1i(p.depthCopy, 4);
            gl.Uniform1i(p.nativeZtst, static_cast<GLint>(k.nativeDepthTest));
            gl.Uniform1i(p.targetScale, static_cast<GLint>(n));
        }
        gl.Uniform1f(p.pointSize, static_cast<f32>(n));
        gl.Uniform1i(p.tex, 0);
        gl.Uniform2f(p.texSize, k.gsTexWidth, k.gsTexHeight);
        gl.Uniform2f(p.texActual, k.texWidth, k.texHeight);
        gl.Uniform1f(p.texSnap, static_cast<f32>(k.texSnap));
        gl.Uniform1f(p.aref, static_cast<f32>(k.aref));
        gl.Uniform3f(p.fogColour, static_cast<f32>(k.fogColour & 0xFF), static_cast<f32>((k.fogColour >> 8) & 0xFF),
                     static_cast<f32>((k.fogColour >> 16) & 0xFF));
        gl.Uniform1f(p.ta0, static_cast<f32>(k.ta0));
        gl.Uniform1f(p.ta1, static_cast<f32>(k.ta1));
        gl.Uniform1i(p.aem, static_cast<GLint>(k.aem));
        gl.Uniform1f(p.lodK, k.lodK);
        gl.Uniform1f(p.lodL, std::exp2(static_cast<f32>(k.lodL)));
        gl.Uniform1f(p.lodMax, static_cast<f32>(k.mxl));
        GLint dimx[16];
        for (u32 i = 0; i < 16; i++)
        {
            s32 value = static_cast<s32>((k.dimx >> (i * 4)) & 7);
            dimx[i] = value & 4 ? value - 8 : value;
        }

        gl.Uniform1iv(p.dimx, 16, dimx);
        gl.Uniform1f(p.scale, static_cast<f32>(k.ditherScale != 0 ? k.ditherScale : 1));
        gl.Uniform1i(p.colclamp, static_cast<GLint>(k.colclamp));
        if (k.program & BitWrapAdd)
        {
            gl.Uniform1i(p.blendA, static_cast<GLint>(k.gsBlend[0]));
            gl.Uniform1i(p.blendB, static_cast<GLint>(k.gsBlend[1]));
            gl.Uniform1i(p.blendC, static_cast<GLint>(k.gsBlend[2]));
            gl.Uniform1f(p.blendFix, static_cast<f32>(k.gsFix));
        }

        if (k.program & BitShaderBlend)
        {
            gl.ActiveTexture(GL_TEXTURE0 + 1);
            gl.BindTexture(GL_TEXTURE_2D, destination);
            gl.ActiveTexture(GL_TEXTURE0);
            gl.Uniform1i(p.destination, 1);
            bool blending = k.gsBlend[0] != 0 || k.gsBlend[1] != 0 || k.gsBlend[2] != 0 || k.gsBlend[3] != 0 || k.gsFix != 0;
            gl.Uniform1i(p.blendOn, blending ? 1 : 0);
            gl.Uniform1i(p.blendA, static_cast<GLint>(k.gsBlend[0]));
            gl.Uniform1i(p.blendB, static_cast<GLint>(k.gsBlend[1]));
            gl.Uniform1i(p.blendC, static_cast<GLint>(k.gsBlend[2]));
            gl.Uniform1i(p.blendD, static_cast<GLint>(k.gsBlend[3]));
            gl.Uniform1f(p.blendFix, static_cast<f32>(k.gsFix));
            gl.Uniform1i(p.pabe, static_cast<GLint>(k.pabe));
            gl.Uniform1i(p.datm, static_cast<GLint>(k.datm));
            gl.Uniform4ui(p.frameMask, k.frameMask[0], k.frameMask[1], k.frameMask[2], k.frameMask[3]);
        }
        u32 mask = k.colourMask;
        gl.ColorMask(colourOn && (mask & 1) ? GL_TRUE : GL_FALSE, colourOn && (mask & 2) ? GL_TRUE : GL_FALSE,
                     colourOn && (mask & 4) ? GL_TRUE : GL_FALSE, colourOn && alphaOn && (mask & 8) ? GL_TRUE : GL_FALSE);
        gl.DepthMask(depthOn && k.depthWrite ? GL_TRUE : GL_FALSE);
        gl.DrawArrays(k.primitive, 0, static_cast<GLsizei>(vertexes_.size()));
    };

    // The pixels passing the alpha test with every write; then a failed test's AFAIL: the failing pixels with the frame only
    // (FB_ONLY), the depth only (ZB_ONLY) or the colour without alpha (RGB_ONLY)
    bool fails = k.alphaTested != 0;
    u32 atst = (k.program >> BitAtstShift) & 7;
    if (!fails || atst != Gs::AtstNever)
    {
        draw(false, true, true, true);
    }

    if (fails && k.afail != Gs::AfailKeep)
    {
        switch (k.afail)
        {
        case Gs::AfailFbOnly:
            draw(true, true, true, false);
            break;
        case Gs::AfailZbOnly:
            draw(true, false, false, true);
            break;
        default:
            draw(true, true, false, false);
            break;
        }
    }

    gl.Viewport(0, 0, width, height);
    // Debugging: TWIN_HW_TRACK=BP,X,Y (hex BP, the target's own pixels): the pixel after each batch into it, when it changes
    static const char* track = std::getenv("TWIN_HW_TRACK");
    if (track != nullptr)
    {
        u32 bp = 0;
        int tx = 0, ty = 0;
        static u32 last = ~0u;
        if (std::sscanf(track, "%x,%d,%d", &bp, &tx, &ty) == 3 && k.colour->bp == bp && !k.wrapAdd)
        {
            u8 c[4] = {};
            gl.BindFramebuffer(GL_READ_FRAMEBUFFER, k.colour->fbo);
            gl.ReadPixels(tx, ty, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
            u32 v = c[0] | c[1] << 8 | c[2] << 16 | static_cast<u32>(c[3]) << 24;
            if (v != last)
            {
                std::fprintf(stderr, "track: #%llu class %u program %x -> %08x\n", static_cast<unsigned long long>(gs_.primitivesDrawn), k.colour->cls,
                             k.program, v);
                last = v;
            }
        }
    }

    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.DepthMask(GL_TRUE);
    gl.Disable(GL_STENCIL_TEST);
    gl.StencilMask(0xFF);
    // A batch that can write the colour target's alpha makes its stencils out of date
    if ((k.colourMask & 8) != 0 || (k.program & BitShaderBlend) != 0)
    {
        k.colour->alphaVersion++;
    }

    vertexes_.clear();
    batchRects_.clear();
    batching_ = false;
}

bool HwGs::DebugPixel(u32 psm, u32 bp, u32 bw, u32 x, u32 y, u32* value)
{
    u32 cls = ClassOf(psm);
    for (auto& t : targets_)
    {
        if (t->bp != bp || t->bw != bw || t->cls != cls || x >= t->width || y >= t->height)
        {
            continue;
        }

        u32 page = Gs::PixelPage(t->psm, x, y, bp, bw) & (Gs::PageCount - 1);
        if (newest_[page] != t.get())
        {
            return false;
        }

        Flush();
        Resolve(t.get());
        Gl& gl = GetGl();
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, t->fbo);
        gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
        GLint sx = static_cast<GLint>(x * t->scale);
        GLint sy = static_cast<GLint>(y * t->scale);
        if (t->depth)
        {
            f32 d = 0.0f;
            gl.ReadPixels(sx, sy, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &d);
            *value = static_cast<u32>(std::min(std::floor(static_cast<double>(d) * 4294967296.0 + 0.5), 4294967295.0));
            *value = t->reversed ? 0xFFFFFFFFu - *value : *value;
        }
        else
        {
            u8 c[4];
            gl.ReadPixels(sx, sy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
            *value = static_cast<u32>(c[0]) | static_cast<u32>(c[1]) << 8 | static_cast<u32>(c[2]) << 16 | static_cast<u32>(c[3]) << 24;
        }

        return true;
    }

    return false;
}

void HwGs::SetScale(u32 scale)
{
    scale = std::clamp<u32>(scale, 1, 8);
    if (scale == scale_)
    {
        return;
    }

    MakeCurrent();
    Flush();
    SyncMem(newestMask_);

    while (!targets_.empty())
    {
        DestroyTarget(targets_.back().get());
    }

    Gl& gl = GetGl();
    for (auto& copy : copies_)
    {
        gl.DeleteTextures(1, &copy.second.first);
        gl.DeleteFramebuffers(1, &copy.second.second);
    }

    copies_.clear();
    scale_ = scale;
}

bool HwGs::DisplayTexture(NativeGraphics::DisplayTexture* display, Target** shown, Rect* shownRect)
{
    MakeCurrent();
    Flush();
    if (!passCounts_.empty() && ++passFrames_ % 60 == 0)
    {
        u64 total = 0;
        for (auto& entry : passCounts_)
        {
            total += entry.second;
        }

        std::fprintf(stderr, "passes: %.1f a frame:", static_cast<double>(total) / 60.0);
        for (auto& entry : passCounts_)
        {
            std::fprintf(stderr, " %s %.1f;", entry.first.c_str(), static_cast<double>(entry.second) / 60.0);
        }

        std::fprintf(stderr, "\n");
        passCounts_.clear();
    }
    ProcessWrites();
    for (auto& t : targets_)
    {
        Resolve(t.get());
    }

    static const char* dumpTarget = std::getenv("TWIN_HW_DUMP_TARGET");
    if (const char* dump = dumpTarget)
    {
        u32 bp = static_cast<u32>(std::strtoul(dump, nullptr, 16));
        for (auto& t : targets_)
        {
            if (t->bp == bp && !t->depth)
            {
                Gl& gl = GetGl();
                std::vector<u32> pixels(static_cast<size_t>(t->width * t->scale) * t->height * t->scale);
                gl.BindFramebuffer(GL_READ_FRAMEBUFFER, t->fbo);
                gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
                gl.ReadPixels(0, 0, static_cast<GLsizei>(t->width * t->scale), static_cast<GLsizei>(t->height * t->scale), GL_RGBA,
                              GL_UNSIGNED_BYTE, pixels.data());
                for (u32& p : pixels)
                {
                    p |= 0xFF000000u;
                }

                const char* folder = std::getenv("TWIN_HW_DUMP_DIR");
                folder = folder != nullptr ? folder : std::getenv("TMPDIR");
                folder = folder != nullptr ? folder : "/tmp";
                char name[1024];
                std::snprintf(name, sizeof(name), "%s/target_%x_%u.png", folder, t->bp, t->cls);
                WritePng(name, pixels.data(), t->width * t->scale, t->height * t->scale);
            }
        }
    }

    const Gs::Gs& gs = gs_;
    // The PCRTC's circuits, as Gs::ReadDisplay reads them
    struct Circuit
    {
        bool enabled;
        u32 width;
        u32 height;
        s32 x;
        s32 y;
        Target* target;
    } circuits[2] = {};
    bool interlaced = gs.smode2.interlaced != 0;
    for (u32 i = 0; i < 2; i++)
    {
        bool enabled = i == 0 ? gs.pmode.en1 != 0 : gs.pmode.en2 != 0;
        const Gs::RegDisplay& d = gs.display[i];
        if (!enabled || (gs.dispfb[i].fbw == 0 && d.dw == 0 && d.dh == 0 && d.magh == 0))
        {
            continue;
        }

        Circuit& c = circuits[i];
        c.enabled = true;
        c.width = static_cast<u32>(d.dw + 1) / static_cast<u32>(d.magh + 1);
        c.height = static_cast<u32>(d.dh + 1) / static_cast<u32>(d.magv + 1);
        if (gs.smode2.ffmd != 0 && interlaced)
        {
            c.height = (c.height + 1) >> 1;
        }

        c.x = static_cast<s32>(d.dx) / static_cast<s32>(d.magh + 1);
        c.y = static_cast<s32>(d.dy) / (interlaced ? 2 : 1);

        // Its buffer's target (made as tall as it's shown), current for the shown rectangle
        const Gs::RegDispfb& fb = gs.dispfb[i];
        u32 cls = ClassOf(fb.psm);
        for (auto& known : targets_)
        {
            if (known->bp == fb.fbp * 32 && known->bw == fb.fbw && known->cls == cls && fb.dbx + c.width <= known->width)
            {
                c.target = known.get();
            }
        }

        if (c.target == nullptr)
        {
            return false;
        }

        if (fb.dby + c.height > c.target->height)
        {
            c.target = TargetFor(c.target->bp, c.target->bw, c.target->psm, fb.dby + c.height);
        }

        if (std::getenv("TWIN_HW_DISPLAY_TRACE") != nullptr)
        {
            std::fprintf(stderr, "display: circuit %u fb %x/%u/%x db %u,%u size %ux%u at %d,%d -> target %x/%u class %u %ux%u scale %u alp %u mmod %u\n", i,
                         static_cast<u32>(fb.fbp * 32), static_cast<u32>(fb.fbw), static_cast<u32>(fb.psm), static_cast<u32>(fb.dbx),
                         static_cast<u32>(fb.dby), c.width, c.height, c.x, c.y, c.target->bp, c.target->bw, c.target->cls, c.target->width,
                         c.target->height, c.target->scale, static_cast<u32>(gs.pmode.alp), static_cast<u32>(gs.pmode.mmod));
        }

        Rect rect = {static_cast<s32>(fb.dbx), static_cast<s32>(fb.dby), static_cast<s32>(fb.dbx + c.width) - 1,
                     static_cast<s32>(fb.dby + c.height) - 1};
        Gs::PageSet pages;
        RectPages(c.target, rect, pages);
        EnsureValid(c.target, pages);
    }

    if (!circuits[0].enabled && !circuits[1].enabled)
    {
        return false;
    }

    // One circuit, or both showing the same thing: its target's rectangle is the picture
    s32 only = -1;
    if (circuits[0].enabled != circuits[1].enabled)
    {
        only = circuits[0].enabled ? 0 : 1;
    }
    else if (gs.dispfb[0].value == gs.dispfb[1].value && circuits[0].x == circuits[1].x && circuits[0].y == circuits[1].y &&
             circuits[0].width == circuits[1].width && circuits[0].height == circuits[1].height)
    {
        only = 0;
    }

    if (only >= 0)
    {
        const Circuit& c = circuits[only];
        const Gs::RegDispfb& fb = gs.dispfb[only];
        display->texture = c.target->texture;
        display->u0 = static_cast<f32>(fb.dbx) / static_cast<f32>(c.target->width);
        display->v0 = static_cast<f32>(fb.dby) / static_cast<f32>(c.target->height);
        display->u1 = static_cast<f32>(fb.dbx + c.width) / static_cast<f32>(c.target->width);
        display->v1 = static_cast<f32>(fb.dby + c.height) / static_cast<f32>(c.target->height);
        display->width = c.width * c.target->scale;
        display->height = c.height * c.target->scale;
        if (shown != nullptr)
        {
            *shown = c.target;
            *shownRect = {static_cast<s32>(fb.dbx * c.target->scale), static_cast<s32>(fb.dby * c.target->scale), 0, 0};
        }

        return true;
    }

    // Merged: the circuits' union from the nearest one's corner
    s32 originX = std::min(circuits[0].x, circuits[1].x);
    s32 originY = std::min(circuits[0].y, circuits[1].y);
    s32 endX = std::max(circuits[0].x + static_cast<s32>(circuits[0].width), circuits[1].x + static_cast<s32>(circuits[1].width));
    s32 endY = std::max(circuits[0].y + static_cast<s32>(circuits[0].height), circuits[1].y + static_cast<s32>(circuits[1].height));
    const u32 n = std::max(circuits[0].enabled ? circuits[0].target->scale : 1, circuits[1].enabled ? circuits[1].target->scale : 1);
    u32 width = static_cast<u32>(endX - originX) * n;
    u32 height = static_cast<u32>(endY - originY) * n;
    Gl& gl = GetGl();
    if (mergeTexture_ == 0 || mergeWidth_ != width || mergeHeight_ != height)
    {
        if (mergeTexture_ == 0)
        {
            gl.GenTextures(1, &mergeTexture_);
            gl.GenFramebuffers(1, &mergeFbo_);
        }

        gl.BindTexture(GL_TEXTURE_2D, mergeTexture_);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.BindFramebuffer(GL_FRAMEBUFFER, mergeFbo_);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mergeTexture_, 0);
        mergeWidth_ = width;
        mergeHeight_ = height;
    }

    gl.BindFramebuffer(GL_FRAMEBUFFER, mergeFbo_);
    gl.Viewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.UseProgram(mergeProgram_);
    auto uniform = [&](const char* name) { return gl.GetUniformLocation(mergeProgram_, name); };
    const char* names[2][6] = {{"circuit1", "enabled1", "place1", "corner1", "size1", ""}, {"circuit2", "enabled2", "place2", "corner2", "size2", ""}};
    for (u32 i = 0; i < 2; i++)
    {
        const Circuit& c = circuits[i];
        gl.ActiveTexture(GL_TEXTURE0 + i);
        gl.BindTexture(GL_TEXTURE_2D, c.enabled ? c.target->texture : circuits[i ^ 1].target->texture);
        gl.Uniform1i(uniform(names[i][0]), static_cast<GLint>(i));
        gl.Uniform1i(uniform(names[i][1]), c.enabled ? 1 : 0);
        gl.Uniform4f(uniform(names[i][2]), static_cast<f32>((c.x - originX) * static_cast<s32>(n)),
                     static_cast<f32>((c.y - originY) * static_cast<s32>(n)), static_cast<f32>(c.width * n), static_cast<f32>(c.height * n));
        gl.Uniform2f(uniform(names[i][3]), static_cast<f32>(gs.dispfb[i].dbx * n), static_cast<f32>(gs.dispfb[i].dby * n));
        gl.Uniform2f(uniform(names[i][4]), c.enabled ? static_cast<f32>(c.target->width * n) : 1.0f,
                     c.enabled ? static_cast<f32>(c.target->height * n) : 1.0f);
    }

    gl.ActiveTexture(GL_TEXTURE0);
    gl.Uniform1f(uniform("alp"), static_cast<f32>(gs.pmode.alp) / 255.0f);
    gl.Uniform1i(uniform("ownAlpha"), gs.pmode.mmod != 0 ? 0 : 1);
    u32 bg = static_cast<u32>(gs.bgcolor & 0xFFFFFF);
    gl.Uniform3f(uniform("background"), static_cast<f32>(bg & 0xFF) / 255.0f, static_cast<f32>((bg >> 8) & 0xFF) / 255.0f,
                 static_cast<f32>((bg >> 16) & 0xFF) / 255.0f);
    gl.BindVertexArray(emptyArray_);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    display->texture = mergeTexture_;
    display->u0 = 0.0f;
    display->v0 = 0.0f;
    display->u1 = 1.0f;
    display->v1 = 1.0f;
    display->width = width;
    display->height = height;
    if (shown != nullptr)
    {
        *shown = nullptr;
        *shownRect = {0, 0, 0, 0};
    }

    return true;
}

bool HwGs::ReadDisplay(Gs::DisplayImage& image)
{
    NativeGraphics::DisplayTexture display;
    Target* target = nullptr;
    Rect rect;
    if (!DisplayTexture(&display, &target, &rect))
    {
        return false;
    }

    Gl& gl = GetGl();
    image.width = display.width;
    image.height = display.height;
    image.pixels.resize(static_cast<size_t>(image.width) * image.height);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, target != nullptr ? target->fbo : mergeFbo_);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_PACK_ROW_LENGTH, 0);
    gl.ReadPixels(rect.left, rect.top, static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), GL_RGBA,
                  GL_UNSIGNED_BYTE, image.pixels.data());
    for (u32& pixel : image.pixels)
    {
        pixel |= 0xFF000000u;
    }

    return true;
}

bool HwGs::CopyDisplay(u32 slot, GLsync drawn, NativeGraphics::ShownTexture* shown)
{
    NativeGraphics::DisplayTexture display;
    Target* target = nullptr;
    Rect rect;
    if (!DisplayTexture(&display, &target, &rect))
    {
        return false;
    }

    Gl& gl = GetGl();
    if (drawn != nullptr)
    {
        gl.WaitSync(drawn, 0, GL_TIMEOUT_IGNORED);
        gl.DeleteSync(drawn);
    }

    ShownCopy& copy = shownCopies_[slot % 3];
    if (copy.texture == 0 || copy.width != display.width || copy.height != display.height)
    {
        if (copy.texture == 0)
        {
            gl.GenTextures(1, &copy.texture);
            gl.GenFramebuffers(1, &copy.fbo);
        }

        gl.BindTexture(GL_TEXTURE_2D, copy.texture);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(display.width),
                      static_cast<GLsizei>(display.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        gl.BindFramebuffer(GL_FRAMEBUFFER, copy.fbo);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, copy.texture, 0);
        copy.width = display.width;
        copy.height = display.height;
    }

    // The picture's rectangle of its target (or the circuits merged), rows as they are: the copy's first row is the top one
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, target != nullptr ? target->fbo : mergeFbo_);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, copy.fbo);
    gl.Disable(GL_SCISSOR_TEST);
    gl.BlitFramebuffer(rect.left, rect.top, rect.left + static_cast<GLint>(display.width), rect.top + static_cast<GLint>(display.height),
                       0, 0, static_cast<GLint>(display.width), static_cast<GLint>(display.height), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    shown->texture = copy.texture;
    shown->width = display.width;
    shown->height = display.height;
    shown->fence = gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    gl.Flush();
    return true;
}
}

bool StartHardwareRenderer(u32 scale)
{
    if (g_Hw != nullptr)
    {
        g_Hw->SetScale(scale);
        return true;
    }

    auto* hw = new HwGs(*GetHardware().gs, std::clamp<u32>(scale, 1, 8));
    if (!hw->Init())
    {
        delete hw;
        return false;
    }

    g_Hw = hw;
    g_HwActive = true;
    return true;
}

void StopHardwareRenderer()
{
    if (g_Hw == nullptr)
    {
        return;
    }

    g_Hw->MakeCurrent();
    g_Hw->Flush();
    for (u32 page = 0; page < Gs::PageCount; page++)
    {
        Gs::PageSet all;
        all.set();
        g_Hw->BeforeCpuRead(all);
        break;
    }

    delete g_Hw;
    g_Hw = nullptr;
    g_HwActive = false;
}

bool HardwareRendererActive()
{
    return g_HwActive.load(std::memory_order_relaxed);
}

void HardwareSetScale(u32 scale)
{
    if (g_Hw != nullptr)
    {
        g_Hw->SetScale(scale);
    }
}

u32 HardwareScale()
{
    return g_Hw != nullptr ? g_Hw->scale() : 1;
}

bool HardwareDisplayTexture(DisplayTexture* display)
{
    return g_Hw != nullptr && g_Hw->DisplayTexture(display, nullptr, nullptr);
}

bool HardwareReadDisplay(Gs::DisplayImage& image)
{
    return g_Hw != nullptr && g_Hw->ReadDisplay(image);
}

bool HardwareCopyDisplay(u32 slot, void* drawnFence, ShownTexture* shown)
{
    return g_Hw != nullptr && g_Hw->CopyDisplay(slot, static_cast<GLsync>(drawnFence), shown);
}

void HardwareSetDither(int mode)
{
    if (g_Hw != nullptr)
    {
        g_Hw->Flush();
    }

    g_DitherMode = std::clamp(mode, 0, 2);
}

void HardwareSetFiltering(int mode)
{
    if (g_Hw != nullptr)
    {
        g_Hw->Flush();
    }

    g_FilterMode = std::clamp(mode, 0, 2);
}

HardwareStats GetHardwareStats()
{
    return g_Hw != nullptr ? g_Hw->stats : HardwareStats{};
}
}
