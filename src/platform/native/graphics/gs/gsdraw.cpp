// The GS's drawing: a primitive's pixels found by the GS's sampling rule and each one taken through the pixel pipeline the GS
// User's Manual describes (texture function, fog, alpha test, depth test, destination alpha test, alpha blending, dithering,
// colour clamp, FBA, the frame and depth masks), reading and writing local memory in the frame's and depth buffer's formats.
//
// Coordinates are the GS's 12.4 fixed point less XYOFFSET; a pixel is drawn when the point at its top-left corner (whole pixel
// coordinates) is inside the primitive, points on a left or top edge counting as inside (the GS's DDA: rows and columns from
// the ceiling of the start to before the ceiling of the end). Colours are interpolated with 7 fraction bits and truncated, texture
// coordinates in 16.16 texels with 4 bit bilinear weights (the GS's 12.4 texel coordinates), depths as doubles truncated.

#include "backend.h"
#include "drawstate.h"
#include "gs.h"
#include "workers.h"

#include <functional>

#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <type_traits>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace Gs
{
namespace
{
s32 CeilPixel(s32 subpixels)
{
    return (subpixels + 15) >> 4;
}

u32 Clamp255(s32 value)
{
    return static_cast<u32>(value < 0 ? 0 : value > 255 ? 255 : value);
}

}

namespace
{
// What a pixel's interpolated values are
struct PixelInput
{
    u32 z;
    // Colours with 7 fraction bits
    s32 r;
    s32 g;
    s32 b;
    s32 a;
    u32 f;
    // Texture coordinates in 16.16 texels (before any level's shift), and Q for the level of detail
    s32 u;
    s32 v;
    f32 q;
};
}

void Gs::SetUpState(DrawState& state, const Vertex*)
{
    RegPrim attributes = prmodecont_ != 0 ? prim_ : prmode_;
    const Context& context = context_[attributes.ctxt];
    state.context = &context;
    state.frameShadow = nullptr;
    state.depthShadow = nullptr;
    state.scale = 1;
    state.type = prim_.prim;
    state.gouraud = attributes.iip != 0;
    state.textured = attributes.tme != 0;
    state.fog = attributes.fge != 0;
    state.blend = attributes.abe != 0;
    state.uv = attributes.fst != 0;

    state.fbp = context.frame.fbp * 32;
    state.fbw = context.frame.fbw;
    state.fpsm = context.frame.psm;
    state.fbmsk = static_cast<u32>(context.frame.fbmsk);
    if (state.fpsm == PSMCT24 || state.fpsm == PSMZ24)
    {
        state.fbmsk |= 0xFF000000u;
    }

    state.frameWritten = state.fbmsk != 0xFFFFFFFFu;
    state.zbp = context.zbuf.zbp * 32;
    state.zpsm = context.zbuf.psm | 0x30;
    state.zmax = state.zpsm == PSMZ32 ? 0xFFFFFFFFu : state.zpsm == PSMZ24 ? 0xFFFFFFu : 0xFFFFu;
    const RegTest& test = context.test;
    state.ztst = test.zte != 0 ? static_cast<u32>(test.ztst) : static_cast<u32>(ZtstAlways);
    state.zWritten = context.zbuf.zmsk == 0;
    state.alphaTest = test.ate != 0;
    state.atst = test.atst;
    state.aref = test.aref;
    state.afail = test.afail;
    state.date = test.date != 0 && (state.fpsm == PSMCT32 || state.fpsm == PSMCT16 || state.fpsm == PSMCT16S);
    state.datm = test.datm;

    const RegAlpha& alpha = context.alpha;
    state.blendA = alpha.a;
    state.blendB = alpha.b;
    state.blendC = alpha.c;
    state.blendD = alpha.d;
    state.blendFix = alpha.fix;
    state.pabe = pabe_ != 0;
    state.colclamp = colclamp_ != 0;
    state.fba = context.fba != 0;
    state.dither = dthe_ != 0 && (state.fpsm == PSMCT16 || state.fpsm == PSMCT16S);

    state.scissorLeft = static_cast<s32>(context.scissor.scax0);
    state.scissorRight = static_cast<s32>(context.scissor.scax1);
    state.scissorTop = static_cast<s32>(context.scissor.scay0);
    state.scissorBottom = static_cast<s32>(context.scissor.scay1);
    state.offsetX = static_cast<s32>(context.xyoffset.ofx);
    state.offsetY = static_cast<s32>(context.xyoffset.ofy);

    state.fogR = fogcol_ & 0xFF;
    state.fogG = (fogcol_ >> 8) & 0xFF;
    state.fogB = (fogcol_ >> 16) & 0xFF;

    if (state.textured)
    {
        const RegTex0& tex0 = context.tex0;
        const RegTex1& tex1 = context.tex1;
        state.tfx = tex0.tfx;
        state.tcc = tex0.tcc != 0;
        state.wms = context.clamp.wms;
        state.wmt = context.clamp.wmt;
        state.magLinear = tex1.mmag != 0;
        state.mmin = tex1.mmin;
        state.mxl = std::min<u32>(tex1.mxl, 6);
        state.k = static_cast<f32>(static_cast<s32>(tex1.k)) / 16.0f;
        state.l = tex1.l;
        state.lodConstant = tex1.lcm != 0 || state.uv;
        // MXL 0 leaves MMIN out (the magnification filter is used throughout)
        state.mipmapped = state.mxl > 0 && state.mmin >= 2 && state.mmin <= 5;
        u32 tw = std::min<u32>(tex0.tw, 10);
        u32 th = std::min<u32>(tex0.th, 10);
        const RegClamp& clamp = context.clamp;
        state.levelCount = state.mipmapped ? state.mxl + 1 : 1;
        for (u32 level = 0; level < state.levelCount; level++)
        {
            TextureLevel& t = state.levels[level];
            t.psm = tex0.psm;
            t.scale = 1;
            t.texels = nullptr;
            t.cacheId = nullptr;
            t.version = 0;
            t.tilesVersion = 0;
            t.width = std::max<u32>((1u << tw) >> level, 1);
            t.height = std::max<u32>((1u << th) >> level, 1);
            t.minu = clamp.minu >> level;
            t.maxu = clamp.maxu >> level;
            t.minv = clamp.minv >> level;
            t.maxv = clamp.maxv >> level;
            switch (level)
            {
            case 0:
                t.bp = tex0.tbp0;
                t.bw = tex0.tbw;
                break;
            case 1:
                t.bp = context.miptbp1.tbp1;
                t.bw = context.miptbp1.tbw1;
                break;
            case 2:
                t.bp = context.miptbp1.tbp2;
                t.bw = context.miptbp1.tbw2;
                break;
            case 3:
                t.bp = context.miptbp1.tbp3;
                t.bw = context.miptbp1.tbw3;
                break;
            case 4:
                t.bp = context.miptbp2.tbp1;
                t.bw = context.miptbp2.tbw1;
                break;
            case 5:
                t.bp = context.miptbp2.tbp2;
                t.bw = context.miptbp2.tbw2;
                break;
            default:
                t.bp = context.miptbp2.tbp3;
                t.bw = context.miptbp2.tbw3;
                break;
            }
        }
    }
}

void Gs::DrawPrimitive(const Vertex* vertexes, u32 count)
{
    // The state made from the registers kept while none of them is written (a strip's triangles, a mesh's): the GS's of this
    // thread, by generation (Gs::WriteRegister)
    struct StateCache
    {
        const Gs* gs = nullptr;
        u64 generation = 0;
        DrawState state;
    };
    thread_local StateCache cache;
    static const bool caching = std::getenv("TWIN_GS_STATE_CACHE") == nullptr || std::getenv("TWIN_GS_STATE_CACHE")[0] != '0';
    // The backend draws from the cache's own state, not a copy (copying its 800 bytes a primitive was a twelfth of the render
    // thread's time); a backend that changes it (a glyph's texture swapped in, the texture's levels decoded: stateTouched_)
    // leaves the cache to be made again, and the software rasteriser draws from a copy of its own (below)
    if (!(caching && cache.gs == this && cache.generation == stateGeneration_))
    {
        SetUpState(cache.state, &vertexes[count - 1]);
        cache.gs = this;
        cache.generation = stateGeneration_;
    }

    DrawState& state = cache.state;

    primitivesDrawn++;
    // Debugging: TWIN_GS_VERTEX_CHECK counts the vertexes VU1 and the game send that a GPU could take differently (ST or Q not
    // finite, Q zero, ST/Q past 64 texture widths, a place more than 2048 pixels off), the first 40 of them each a line and the
    // counts every 3 million vertexes
    {
        static const bool vertexCheck = std::getenv("TWIN_GS_VERTEX_CHECK") != nullptr;
        if (vertexCheck)
        {
            static u64 checked = 0, nonFinite = 0, zeroQ = 0, farXy = 0, farSt = 0, shown = 0;
            for (u32 i = 0; i < count; i++)
            {
                const Vertex& v = vertexes[i];
                checked++;
                const s32 sx = (static_cast<s32>(v.x) - static_cast<s32>(state.offsetX)) / 16;
                const s32 sy = (static_cast<s32>(v.y) - static_cast<s32>(state.offsetY)) / 16;
                bool odd = false;
                const char* why = "";
                if (sx < -2048 || sx > 2048 || sy < -2048 || sy > 2048)
                {
                    farXy++;
                    odd = true;
                    why = "xy";
                }
                if (state.textured && !state.uv)
                {
                    if (!std::isfinite(v.s) || !std::isfinite(v.t) || !std::isfinite(v.q))
                    {
                        nonFinite++;
                        odd = true;
                        why = "nonfinite";
                    }
                    else if (v.q == 0.0f)
                    {
                        zeroQ++;
                        odd = true;
                        why = "q0";
                    }
                    else if (std::fabs(v.s / v.q) > 64.0f || std::fabs(v.t / v.q) > 64.0f)
                    {
                        farSt++;
                        odd = true;
                        why = "st";
                    }
                }
                if (odd && shown < 40)
                {
                    shown++;
                    std::fprintf(stderr, "gs vertex check: %s prim %llu type %u vertex %u of %u: screen %d,%d z %u stq %g %g %g tex0 %x\n", why,
                                 (unsigned long long)primitivesDrawn, (unsigned)state.type, i, count, sx, sy, v.z, v.s, v.t, v.q,
                                 state.textured ? (unsigned)state.context->tex0.tbp0 : 0u);
                }
            }
            if (checked % 3000000 < count)
            {
                std::fprintf(stderr, "gs vertex check: %llu vertexes, %llu not finite, %llu Q 0, %llu far XY, %llu ST/Q past 64\n",
                             (unsigned long long)checked, (unsigned long long)nonFinite, (unsigned long long)zeroQ,
                             (unsigned long long)farXy, (unsigned long long)farSt);
            }
        }
    }
    // Debugging: TWIN_GS_DUMP_AT=PRIM,BP,BW,PSM,W,H[;...] (hex BP and PSM) writes that buffer's values, as local memory holds
    // them just before primitive PRIM, to gsdump_PRIM_BP.bin (a u32 a pixel, row by row) in
    // $TWIN_HW_DUMP_DIR (or the system's temporary folder)
    static const char* dumpAt = std::getenv("TWIN_GS_DUMP_AT");
    if (dumpAt != nullptr)
    {
        for (const char* spec = dumpAt; spec != nullptr && *spec != 0;)
        {
            unsigned long long prim = 0;
            u32 bp = 0, bw = 0, psm = 0, w = 0, h = 0;
            if (std::sscanf(spec, "%llu,%x,%u,%x,%u,%u", &prim, &bp, &bw, &psm, &w, &h) == 6 && prim == primitivesDrawn)
            {
                PrepareCpuRect(false, psm, bp, bw, 0, 0, static_cast<s32>(w) - 1, static_cast<s32>(h) - 1, "dump");
                std::vector<u32> values(static_cast<size_t>(w) * h);
                for (u32 y = 0; y < h; y++)
                {
                    for (u32 x = 0; x < w; x++)
                    {
                        values[static_cast<size_t>(y) * w + x] = ReadPixel(psm, x, y, bp, bw);
                    }
                }

                const char* folder = std::getenv("TWIN_HW_DUMP_DIR");
                folder = folder != nullptr ? folder : std::getenv("TMPDIR");
                folder = folder != nullptr ? folder : "/tmp";
                char name[1024];
                std::snprintf(name, sizeof(name), "%s/gsdump_%llu_%x.bin", folder, prim, bp);
                if (FILE* f = std::fopen(name, "wb"))
                {
                    std::fwrite(values.data(), 4, values.size(), f);
                    std::fclose(f);
                }
            }

            spec = std::strchr(spec, ';');
            spec = spec != nullptr ? spec + 1 : nullptr;
        }
    }

    static const bool trace = std::getenv("TWIN_GS_TRACE") != nullptr;
    if (trace)
    {
        const Context& c = *state.context;
        std::fprintf(stderr, "gs: from %x prim %u tme %d fst %d abe %d frame %x/%u/%x zbuf %x/%x test %llx alpha %llx tex0 %llx clamp %llx tex1 %llx ofs %u,%u sc %llx\n",
                     traceSource, state.type, state.textured, state.uv, state.blend, c.frame.fbp, static_cast<u32>(c.frame.fbw), static_cast<u32>(c.frame.psm),
                     static_cast<u32>(c.zbuf.zbp), static_cast<u32>(c.zbuf.psm), static_cast<unsigned long long>(c.test.value),
                     static_cast<unsigned long long>(c.alpha.value), static_cast<unsigned long long>(c.tex0.value),
                     static_cast<unsigned long long>(c.clamp.value), static_cast<unsigned long long>(c.tex1.value),
                     static_cast<u32>(c.xyoffset.ofx), static_cast<u32>(c.xyoffset.ofy), static_cast<unsigned long long>(c.scissor.value));
        for (u32 i = 0; i < count; i++)
        {
            const Vertex& v = vertexes[i];
            std::fprintf(stderr, "gs:   v %.4f,%.4f z %x rgba %02x%02x%02x%02x uv %.3f,%.3f stq %g,%g,%g\n", v.x / 16.0, v.y / 16.0, v.z,
                         v.r, v.g, v.b, v.a, v.u / 16.0, v.v / 16.0, v.s, v.t, v.q);
        }
    }

    // TWIN_PIXEL_WATCH=X,Y: each primitive whose box covers the pixel (of the frame drawn into), with the frame's and the depth
    // buffer's values there after it
    static const char* watch = std::getenv("TWIN_PIXEL_WATCH");
    s32 watchX = -1;
    s32 watchY = -1;
    if (watch != nullptr)
    {
        std::sscanf(watch, "%d,%d", &watchX, &watchY);
        s32 x0 = 0x7FFFFFFF, x1 = -0x7FFFFFFF, y0 = 0x7FFFFFFF, y1 = -0x7FFFFFFF;
        for (u32 i = 0; i < count; i++)
        {
            x0 = std::min(x0, static_cast<s32>(vertexes[i].x) - state.offsetX);
            x1 = std::max(x1, static_cast<s32>(vertexes[i].x) - state.offsetX);
            y0 = std::min(y0, static_cast<s32>(vertexes[i].y) - state.offsetY);
            y1 = std::max(y1, static_cast<s32>(vertexes[i].y) - state.offsetY);
        }

        if (!(x0 <= watchX * 16 + 16 && x1 >= watchX * 16 - 16 && y0 <= watchY * 16 + 16 && y1 >= watchY * 16 - 16))
        {
            watchX = -1;
        }
    }

    auto reportWatch = [&]() {
        if (watchX < 0)
        {
            return;
        }

        u32 frame = 0;
        u32 depth = 0;
        if (backend_ == nullptr || !backend_->DebugPixel(state.fpsm, state.fbp, state.fbw, watchX, watchY, &frame))
        {
            frame = ReadPixel(state.fpsm, watchX, watchY, state.fbp, state.fbw);
        }

        if (backend_ == nullptr || !backend_->DebugPixel(state.zpsm, state.zbp, state.fbw, watchX, watchY, &depth))
        {
            depth = ReadPixel(state.zpsm, watchX, watchY, state.zbp, state.fbw);
        }

        const Context& c = *state.context;
        std::fprintf(stderr, "watch: #%llu prim %u frame %x/%u/%x tex0 %llx test %llx alpha %llx -> frame %08x z %08x\n",
                     static_cast<unsigned long long>(primitivesDrawn), state.type, state.fbp, state.fbw, state.fpsm,
                     static_cast<unsigned long long>(state.textured ? c.tex0.value : 0), static_cast<unsigned long long>(c.test.value),
                     static_cast<unsigned long long>(state.blend ? c.alpha.value : 0), frame, depth);
        static const bool printVertexes = std::getenv("TWIN_PIXEL_WATCH_VERTEXES") != nullptr;
        for (u32 i = 0; printVertexes && i < count; i++)
        {
            const Vertex& v = vertexes[i];
            std::fprintf(stderr, "watch:   v %.4f,%.4f z %x rgba %02x%02x%02x%02x uv %.3f,%.3f stq %g,%g,%g\n", v.x / 16.0,
                         v.y / 16.0, v.z, v.r, v.g, v.b, v.a, v.u / 16.0, v.v / 16.0, v.s, v.t, v.q);
        }
    };

    // A backend draws it, or leaves it to the software rasteriser (having made local memory current for it)
    stateTouched_ = false;
    if (backend_ != nullptr && backend_->Draw(state, vertexes, count))
    {
        if (stateTouched_)
        {
            cache.generation = ~0ull;
        }

        primitives++;
        reportWatch();
        return;
    }

    // (Copied inline: the C library's memcpy, called for a struct this big, cost more than the state's making saved)
    static_assert(std::is_trivially_copyable_v<DrawState>);
    DrawState own;
    if (stateTouched_)
    {
        cache.generation = ~0ull;
        SetUpState(own, &vertexes[count - 1]);
    }
    else
    {
        __builtin_memcpy_inline(&own, &state, sizeof(DrawState));
    }

    DrawSoftware(own, vertexes, count);
    reportWatch();
}

// The software rasteriser's part of DrawPrimitive, on a state of its own (it fills its texture levels)
void Gs::DrawSoftware(DrawState& state, const Vertex* vertexes, u32 count)
{
    {
        u32 x0 = vertexes[0].x, x1 = vertexes[0].x, y0 = vertexes[0].y, y1 = vertexes[0].y;
        for (u32 i = 1; i < count; i++)
        {
            x0 = std::min(x0, vertexes[i].x);
            x1 = std::max(x1, vertexes[i].x);
            y0 = std::min(y0, vertexes[i].y);
            y1 = std::max(y1, vertexes[i].y);
        }

        boxPixels += static_cast<u64>((x1 - x0) >> 4) * ((y1 - y0) >> 4);
    }
    if (state.textured)
    {
        DecodeTextures(state, vertexes, count);
    }

    const Vertex* original = vertexes;
    Vertex glyphVertexes[3];
    if (!glyphs_.empty() && state.textured && state.uv && state.type == PrimSprite)
    {
        std::copy(vertexes, vertexes + count, glyphVertexes);
        if (GlyphImage(state, glyphVertexes, count))
        {
            vertexes = glyphVertexes;
        }
    }

    // Drawing at a scale: the shadows the draw goes into and reads made to match local memory before local memory is drawn into
    if (scale_ > 1 && backend_ == nullptr)
    {
        bool depthUsed = state.ztst == 2 || state.ztst == 3 || state.zWritten;
        if (Shadow* frame = ShadowFor(state.fpsm, state.fbp, state.fbw, true))
        {
            SyncShadow(*frame);
        }

        if (depthUsed)
        {
            if (Shadow* depth = ShadowFor(state.zpsm, state.zbp, state.fbw, true))
            {
                SyncShadow(*depth);
            }
        }

        for (u32 level = 0; state.textured && level < state.levelCount; level++)
        {
            const TextureLevel& t = state.levels[level];
            if (Shadow* texture = ShadowFor(t.psm, t.bp, t.bw, false))
            {
                SyncShadow(*texture);
            }
        }
    }

    NextGeneration();
    const u32 drawGeneration = generation_;
    primitives++;
    switch (state.type)
    {
    case PrimPoint:
        DrawPoint(state, vertexes[0]);
        break;
    case PrimLine:
    case PrimLineStrip:
        DrawLine(state, vertexes[0], vertexes[1]);
        break;
    case PrimTriangle:
    case PrimTriangleStrip:
    case PrimTriangleFan:
        DrawTriangle(state, vertexes[0], vertexes[1], vertexes[2]);
        break;
    case PrimSprite:
        DrawSprite(state, vertexes[0], vertexes[1]);
        break;
    default:
        break;
    }

    if (scale_ > 1 && backend_ == nullptr)
    {
        DrawScaled(original, count, drawGeneration);
    }
}


namespace
{
// The pixel pipeline's pieces that read local memory, through the GS
struct Pipeline
{
    Gs* gs;
    const u16* clut;
    u32 texa0;
    u32 texa1;
    bool aem;
    u64 dimx;
};

// A texel of a level as RGBA (bytes, alpha 0x80 = 1.0): direct colour formats expanded through TEXA, indexed ones through the
// CLUT (its 32 bit entries, or 16 bit ones expanded)
u32 Expand16(u32 c, const Pipeline& p)
{
    u32 alpha = (c & 0x8000) ? p.texa1 : (p.aem && (c & 0x7FFF) == 0) ? 0 : p.texa0;
    return (c & 0x1F) << 3 | ((c >> 5) & 0x1F) << 11 | ((c >> 10) & 0x1F) << 19 | alpha << 24;
}

u32 ClutColour(u32 index, u32 cpsm, u32 csa, const Pipeline& p)
{
    if (cpsm == PSMCT32 || cpsm == PSMCT24)
    {
        u32 slot = (index + (csa & 15) * 16) & 255;
        u32 colour = static_cast<u32>(p.clut[slot]) | static_cast<u32>(p.clut[slot + 256]) << 16;
        if (cpsm == PSMCT24)
        {
            u32 alpha = (p.aem && (colour & 0xFFFFFF) == 0) ? 0 : p.texa0;
            colour = (colour & 0xFFFFFF) | alpha << 24;
        }

        return colour;
    }

    return Expand16(p.clut[(index + csa * 16) & 511], p);
}
}

namespace
{
inline u32 Wrap(s32 coordinate, u32 mode, u32 size, u32 minimum, u32 maximum)
{
    switch (mode)
    {
    case WrapRepeat:
        return static_cast<u32>(coordinate) & (size - 1);
    case WrapClamp:
        return static_cast<u32>(std::clamp<s32>(coordinate, 0, static_cast<s32>(size) - 1));
    case WrapRegionClamp:
        return static_cast<u32>(std::clamp<s32>(coordinate, static_cast<s32>(minimum), static_cast<s32>(maximum)));
    default:
        return (static_cast<u32>(coordinate) & minimum) | maximum;
    }
}
}

// The level's texel at whole texel coordinates (wrapped by CLAMP)
// A texel of a level read from local memory and expanded: direct colour formats through TEXA, indexed ones through the CLUT
// (its 32 bit entries, or 16 bit ones expanded)
static u32 DecodeTexel(const Gs& gs, const TextureLevel& level, u32 x, u32 y, const Pipeline& p, const RegTex0& tex0)
{
    switch (level.psm)
    {
    case PSMCT32:
        return gs.ReadPixel(PSMCT32, x, y, level.bp, level.bw);
    case PSMCT24:
    {
        u32 c = gs.ReadPixel(PSMCT24, x, y, level.bp, level.bw);
        u32 alpha = (p.aem && c == 0) ? 0 : p.texa0;
        return c | alpha << 24;
    }
    case PSMCT16:
    case PSMCT16S:
        return Expand16(gs.ReadPixel(level.psm, x, y, level.bp, level.bw), p);
    case PSMT8:
    case PSMT8H:
    case PSMT4:
    case PSMT4HL:
    case PSMT4HH:
        return ClutColour(gs.ReadPixel(level.psm, x, y, level.bp, level.bw), tex0.cpsm, tex0.csa, p);
    case PSMZ32:
    case PSMZ24:
    case PSMZ16:
    case PSMZ16S:
    {
        // Depth read as colour: its bits as the colour format of its size
        u32 c = gs.ReadPixel(level.psm, x, y, level.bp, level.bw);
        if (level.psm == PSMZ16 || level.psm == PSMZ16S)
        {
            return Expand16(c, p);
        }

        if (level.psm == PSMZ24)
        {
            u32 alpha = (p.aem && c == 0) ? 0 : p.texa0;
            return c | alpha << 24;
        }

        return c;
    }
    default:
        return 0;
    }
}


namespace
{
// Bilinear: the four texels round (u, v) (16.16, half a texel taken off), weighted by 4 bit fractions as the GS does:
// a + (b - a) * f >> 4 across, then down
inline u32 Lerp4(u32 a, u32 b, u32 f)
{
    if (f == 0)
    {
        return a;
    }

    u32 out = 0;
    for (u32 shift = 0; shift < 32; shift += 8)
    {
        s32 ca = static_cast<s32>((a >> shift) & 0xFF);
        s32 cb = static_cast<s32>((b >> shift) & 0xFF);
        s32 c = ca + (((cb - ca) * static_cast<s32>(f)) >> 4);
        out |= static_cast<u32>(c & 0xFF) << shift;
    }

    return out;
}
}

// A texture read from a shadow, drawing at a scale: coordinates in samples, CLAMP's modes in the texture's own texels (a sample
// of a texel repeats or clamps with its texel), and past the shadow's rows local memory's texel
static u32 ShadowTexel(const Gs& gs, const Gs::DrawState& state, const TextureLevel& t, s64 x, s64 y, const Pipeline& p,
                       const RegTex0& tex0)
{
    const s64 n = t.scale;
    auto wrap = [&](s64 c, u32 mode, u32 size, u32 minimum, u32 maximum) -> u32 {
        s64 texel = c >= 0 ? c / n : -((-c + n - 1) / n);
        s64 sample = c - texel * n;
        u32 wrapped = Wrap(static_cast<s32>(texel), mode, size, minimum, maximum);
        if (mode == WrapClamp || mode == WrapRegionClamp)
        {
            // A clamped texel's samples clamp to its edge's
            if (static_cast<s64>(wrapped) != texel)
            {
                sample = texel < static_cast<s64>(wrapped) ? 0 : n - 1;
            }
        }

        return static_cast<u32>(wrapped * n + sample);
    };
    u32 sx = wrap(x, state.wms, t.width / t.scale, t.minu, t.maxu);
    u32 sy = wrap(y, state.wmt, t.height / t.scale, t.minv, t.maxv);
    if (sx < t.stride && sy < t.rows)
    {
        return t.texels[static_cast<size_t>(sy) * t.stride + sx];
    }

    return DecodeTexel(gs, t, sx / t.scale, sy / t.scale, p, tex0);
}

static u32 SampleScaledLevel(const Gs& gs, const Gs::DrawState& state, const TextureLevel& t, s32 u, s32 v, bool linear,
                             const Pipeline& p, const RegTex0& tex0)
{
    s64 lu = static_cast<s64>(u) * t.scale;
    s64 lv = static_cast<s64>(v) * t.scale;
    if (!linear)
    {
        return ShadowTexel(gs, state, t, lu >> 16, lv >> 16, p, tex0);
    }

    lu -= 0x8000;
    lv -= 0x8000;
    s64 u0 = lu >> 16;
    s64 v0 = lv >> 16;
    u32 uf = (static_cast<u32>(lu) >> 12) & 0xF;
    u32 vf = (static_cast<u32>(lv) >> 12) & 0xF;
    u32 c00 = ShadowTexel(gs, state, t, u0, v0, p, tex0);
    u32 c01 = ShadowTexel(gs, state, t, u0 + 1, v0, p, tex0);
    u32 c10 = ShadowTexel(gs, state, t, u0, v0 + 1, p, tex0);
    u32 c11 = ShadowTexel(gs, state, t, u0 + 1, v0 + 1, p, tex0);
    return Lerp4(Lerp4(c00, c01, uf), Lerp4(c10, c11, uf), vf);
}

static u32 SampleLevel(Gs& gs, const Gs::DrawState& state, u32 level, s32 u, s32 v, bool linear, const Pipeline& p,
                       const RegTex0& tex0)
{
    const TextureLevel& t = state.levels[level];
    if (t.scale != 1)
    {
        return SampleScaledLevel(gs, state, t, u >> level, v >> level, linear, p, tex0);
    }

    s32 lu = u >> level;
    s32 lv = v >> level;
    if (!linear)
    {
        u32 x = Wrap(lu >> 16, state.wms, t.width, t.minu, t.maxu);
        u32 y = Wrap(lv >> 16, state.wmt, t.height, t.minv, t.maxv);
        return t.texels[y * t.width + x];
    }

    lu -= 0x8000;
    lv -= 0x8000;
    s32 u0 = lu >> 16;
    s32 v0 = lv >> 16;
    u32 uf = (static_cast<u32>(lu) >> 12) & 0xF;
    u32 vf = (static_cast<u32>(lv) >> 12) & 0xF;
    u32 x0 = Wrap(u0, state.wms, t.width, t.minu, t.maxu);
    u32 x1 = Wrap(u0 + 1, state.wms, t.width, t.minu, t.maxu);
    const u32* row0 = t.texels + Wrap(v0, state.wmt, t.height, t.minv, t.maxv) * t.width;
    const u32* row1 = t.texels + Wrap(v0 + 1, state.wmt, t.height, t.minv, t.maxv) * t.width;
    return Lerp4(Lerp4(row0[x0], row0[x1], uf), Lerp4(row1[x0], row1[x1], uf), vf);
}

// The texture's colour at the pixel: the level of detail picks the magnification filter (LOD <= 0) or the minification one,
// and with mipmaps the level (rounded, or the two round it blended by the fraction)
static u32 SampleTexture(Gs& gs, const Gs::DrawState& state, s32 u, s32 v, f32 q, const Pipeline& p, const RegTex0& tex0)
{
    // MXL 0: the magnification filter throughout, no level of detail to work out
    if (state.mxl == 0)
    {
        return SampleLevel(gs, state, 0, u, v, state.magLinear, p, tex0);
    }

    f32 lod = state.k;
    if (!state.lodConstant)
    {
        lod = -std::log2(std::fabs(q)) * static_cast<f32>(1u << state.l) + state.k;
    }

    if (lod <= 0.0f || state.levelCount == 1)
    {
        bool linear = lod <= 0.0f ? state.magLinear : (state.mmin & 1) != 0;
        if (state.mxl == 0)
        {
            linear = state.magLinear;
        }

        return SampleLevel(gs, state, 0, u, v, linear, p, tex0);
    }

    // MMIN: 2 nearest of the nearest level, 3 nearest of two levels, 4 linear of the nearest level, 5 linear of two levels
    bool linear = state.mmin >= 4;
    f32 maxLevel = static_cast<f32>(state.levelCount - 1);
    if ((state.mmin & 1) == 0)
    {
        u32 level = static_cast<u32>(std::min(std::floor(lod + 0.5f), maxLevel));
        return SampleLevel(gs, state, level, u, v, linear, p, tex0);
    }

    f32 clamped = std::min(lod, maxLevel);
    u32 level = static_cast<u32>(std::floor(clamped));
    u32 fraction = static_cast<u32>((clamped - static_cast<f32>(level)) * 16.0f) & 0xF;
    u32 c0 = SampleLevel(gs, state, level, u, v, linear, p, tex0);
    if (level + 1 >= state.levelCount || fraction == 0)
    {
        return c0;
    }

    u32 c1 = SampleLevel(gs, state, level + 1, u, v, linear, p, tex0);
    return Lerp4(c0, c1, fraction);
}

// A pixel of the frame or depth buffer: local memory's, or drawing at a scale its shadow's sample (24 bit formats' top byte
// kept, 16 bit formats' low half)
static u32 ReadTarget(const Gs& gs, const Gs::Shadow* shadow, u32 psm, s32 x, s32 y, u32 bp, u32 bw)
{
    if (shadow == nullptr)
    {
        return gs.ReadPixel(psm, static_cast<u32>(x), static_cast<u32>(y), bp, bw);
    }

    u32 rowSamples = shadow->width * shadow->scale;
    if (static_cast<u32>(x) >= rowSamples || static_cast<u32>(y) >= shadow->height * shadow->scale)
    {
        return 0;
    }

    u32 value = shadow->samples[static_cast<size_t>(y) * rowSamples + static_cast<u32>(x)];
    return psm == PSMCT24 || psm == PSMZ24 ? value & 0xFFFFFF : value;
}

static void WriteTarget(Gs& gs, Gs::Shadow* shadow, u32 psm, s32 x, s32 y, u32 bp, u32 bw, u32 value)
{
    if (shadow == nullptr)
    {
        gs.WritePixel(psm, static_cast<u32>(x), static_cast<u32>(y), bp, bw, value);
        return;
    }

    u32 rowSamples = shadow->width * shadow->scale;
    if (static_cast<u32>(x) >= rowSamples || static_cast<u32>(y) >= shadow->height * shadow->scale)
    {
        return;
    }

    u32& sample = shadow->samples[static_cast<size_t>(y) * rowSamples + static_cast<u32>(x)];
    switch (psm)
    {
    case PSMCT24:
    case PSMZ24:
        sample = (sample & 0xFF000000) | (value & 0xFFFFFF);
        break;
    case PSMCT16:
    case PSMCT16S:
    case PSMZ16:
    case PSMZ16S:
        sample = value & 0xFFFF;
        break;
    default:
        sample = value;
        break;
    }
}

// One pixel through the pipeline
static void ShadePixel(Gs& gs, const Gs::DrawState& state, const Pipeline& p, s32 x, s32 y, const PixelInput& in,
                       const RegTex0& tex0)
{
    // The depth test
    u32 z = std::min(in.z, state.zmax);
    u32 zd = 0;
    bool readsDepth = state.ztst == ZtstGequal || state.ztst == ZtstGreater;
    if (state.ztst == ZtstNever)
    {
        return;
    }

    if (readsDepth)
    {
        zd = ReadTarget(gs, state.depthShadow, state.zpsm, x, y, state.zbp, state.fbw);
        if (state.ztst == ZtstGequal ? z < zd : z <= zd)
        {
            return;
        }
    }

    // The texture function
    u32 vr = static_cast<u32>(in.r >> 7);
    u32 vg = static_cast<u32>(in.g >> 7);
    u32 vb = static_cast<u32>(in.b >> 7);
    u32 va = static_cast<u32>(in.a >> 7);
    u32 r = vr;
    u32 g = vg;
    u32 b = vb;
    u32 a = va;
    if (state.textured)
    {
        u32 texel = SampleTexture(gs, state, in.u, in.v, in.q, p, tex0);
        u32 tr = texel & 0xFF;
        u32 tg = (texel >> 8) & 0xFF;
        u32 tb = (texel >> 16) & 0xFF;
        u32 ta = texel >> 24;
        switch (state.tfx)
        {
        case TfxModulate:
            r = std::min<u32>((tr * static_cast<u32>(in.r)) >> 14, 255);
            g = std::min<u32>((tg * static_cast<u32>(in.g)) >> 14, 255);
            b = std::min<u32>((tb * static_cast<u32>(in.b)) >> 14, 255);
            a = state.tcc ? std::min<u32>((ta * static_cast<u32>(in.a)) >> 14, 255) : va;
            break;
        case TfxDecal:
            r = tr;
            g = tg;
            b = tb;
            a = state.tcc ? ta : va;
            break;
        case TfxHighlight:
            r = std::min<u32>(((tr * static_cast<u32>(in.r)) >> 14) + va, 255);
            g = std::min<u32>(((tg * static_cast<u32>(in.g)) >> 14) + va, 255);
            b = std::min<u32>(((tb * static_cast<u32>(in.b)) >> 14) + va, 255);
            a = state.tcc ? std::min<u32>(ta + va, 255) : va;
            break;
        default:
            r = std::min<u32>(((tr * static_cast<u32>(in.r)) >> 14) + va, 255);
            g = std::min<u32>(((tg * static_cast<u32>(in.g)) >> 14) + va, 255);
            b = std::min<u32>(((tb * static_cast<u32>(in.b)) >> 14) + va, 255);
            a = state.tcc ? ta : va;
            break;
        }
    }

    // The alpha test: a failure keeps nothing, or keeps the frame, the depth or the frame's colour
    bool writeFrame = state.frameWritten;
    bool writeDepth = state.zWritten;
    u32 alphaMask = 0;
    if (state.alphaTest)
    {
        bool pass;
        switch (state.atst)
        {
        case AtstNever:
            pass = false;
            break;
        case AtstAlways:
            pass = true;
            break;
        case AtstLess:
            pass = a < state.aref;
            break;
        case AtstLequal:
            pass = a <= state.aref;
            break;
        case AtstEqual:
            pass = a == state.aref;
            break;
        case AtstGequal:
            pass = a >= state.aref;
            break;
        case AtstGreater:
            pass = a > state.aref;
            break;
        default:
            pass = a != state.aref;
            break;
        }

        if (!pass)
        {
            switch (state.afail)
            {
            case AfailKeep:
                return;
            case AfailFbOnly:
                writeDepth = false;
                break;
            case AfailZbOnly:
                writeFrame = false;
                break;
            default:
                writeDepth = false;
                alphaMask = 0xFF000000u;
                break;
            }
        }
    }

    // Fog
    if (state.fog)
    {
        u32 f = in.f;
        r = (f * r + (255 - f) * state.fogR) >> 8;
        g = (f * g + (255 - f) * state.fogG) >> 8;
        b = (f * b + (255 - f) * state.fogB) >> 8;
    }

    // The destination: its colour as 32 bits (16 bit pixels expanded, their alpha bit 0x80)
    u32 fpsm = state.fpsm;
    u32 fd = 0;
    bool needsDestination = writeFrame && (state.blend || state.date || state.fbmsk != 0 || alphaMask != 0 ||
                                           fpsm == PSMCT24 || fpsm == PSMZ24);
    if (needsDestination || state.date)
    {
        fd = ReadTarget(gs, state.frameShadow, fpsm, x, y, state.fbp, state.fbw);
    }

    if (state.date)
    {
        u32 bit = (fpsm == PSMCT32) ? fd >> 31 : (fd >> 15) & 1;
        if (bit != state.datm)
        {
            return;
        }
    }

    if (writeDepth)
    {
        WriteTarget(gs, state.depthShadow, state.zpsm, x, y, state.zbp, state.fbw, z);
    }

    if (!writeFrame)
    {
        return;
    }

    u32 dr;
    u32 dg;
    u32 db;
    u32 da;
    if (fpsm == PSMCT16 || fpsm == PSMCT16S)
    {
        dr = (fd & 0x1F) << 3;
        dg = ((fd >> 5) & 0x1F) << 3;
        db = ((fd >> 10) & 0x1F) << 3;
        da = (fd & 0x8000) ? 0x80 : 0;
    }
    else
    {
        dr = fd & 0xFF;
        dg = (fd >> 8) & 0xFF;
        db = (fd >> 16) & 0xFF;
        da = (fpsm == PSMCT24 || fpsm == PSMZ24) ? 0x80 : fd >> 24;
    }

    // The blend: ((A - B) * C >> 7) + D on each colour channel, A, B and D the source, the destination or 0, C the source's
    // alpha, the destination's or FIX (the alpha isn't blended). PABE blends only pixels whose alpha's top bit is set
    s32 sr = static_cast<s32>(r);
    s32 sg = static_cast<s32>(g);
    s32 sb = static_cast<s32>(b);
    if (state.blend && !(state.pabe && (a & 0x80) == 0))
    {
        auto pick = [](u32 select, s32 source, s32 destination) { return select == 0 ? source : select == 1 ? destination : 0; };
        s32 c = state.blendC == 0 ? static_cast<s32>(a) : state.blendC == 1 ? static_cast<s32>(da) : static_cast<s32>(state.blendFix);
        sr = (((pick(state.blendA, sr, static_cast<s32>(dr)) - pick(state.blendB, sr, static_cast<s32>(dr))) * c) >> 7) +
             pick(state.blendD, sr, static_cast<s32>(dr));
        sg = (((pick(state.blendA, sg, static_cast<s32>(dg)) - pick(state.blendB, sg, static_cast<s32>(dg))) * c) >> 7) +
             pick(state.blendD, sg, static_cast<s32>(dg));
        sb = (((pick(state.blendA, sb, static_cast<s32>(db)) - pick(state.blendB, sb, static_cast<s32>(db))) * c) >> 7) +
             pick(state.blendD, sb, static_cast<s32>(db));
    }

    // Dithering of 16 bit frames, then the clamp (or the low byte kept)
    if (state.dither)
    {
        // Drawing at a scale, the matrix's place is the PS2's pixel's
        u32 shift = (((static_cast<u32>(y) / state.scale) & 3) * 4 + ((static_cast<u32>(x) / state.scale) & 3)) * 4;
        s32 dm = static_cast<s32>((p.dimx >> shift) & 7);
        if (dm & 4)
        {
            dm -= 8;
        }

        sr += dm;
        sg += dm;
        sb += dm;
    }

    u32 fr;
    u32 fg;
    u32 fb;
    if (state.colclamp)
    {
        fr = Clamp255(sr);
        fg = Clamp255(sg);
        fb = Clamp255(sb);
    }
    else
    {
        fr = static_cast<u32>(sr) & 0xFF;
        fg = static_cast<u32>(sg) & 0xFF;
        fb = static_cast<u32>(sb) & 0xFF;
    }

    u32 fa = a | (state.fba ? 0x80u : 0u);
    u32 colour = fr | fg << 8 | fb << 16 | fa << 24;
    u32 mask = state.fbmsk | alphaMask;
    if (fpsm == PSMCT16 || fpsm == PSMCT16S)
    {
        u32 c16 = (fr >> 3) | (fg >> 3) << 5 | (fb >> 3) << 10 | (fa & 0x80) << 8;
        u32 m16 = ((mask >> 3) & 0x1F) | ((mask >> 6) & 0x3E0) | ((mask >> 9) & 0x7C00) | ((mask >> 16) & 0x8000);
        c16 = (c16 & ~m16) | (fd & m16);
        WriteTarget(gs, state.frameShadow, fpsm, x, y, state.fbp, state.fbw, c16);
        return;
    }

    if (fpsm == PSMCT24 || fpsm == PSMZ24)
    {
        colour = (colour & ~mask) | (fd & mask);
        WriteTarget(gs, state.frameShadow, fpsm, x, y, state.fbp, state.fbw, colour);
        return;
    }

    colour = (colour & ~mask) | (fd & mask);
    WriteTarget(gs, state.frameShadow, fpsm, x, y, state.fbp, state.fbw, colour);
}

namespace
{
// Decoded texture levels by what they're decoded from: the place and format in local memory, the size, the palette (the CLUT's
// contents, format and offset) and TEXA. An entry stays good while none of the pages it was read from is written
struct TextureKey
{
    u32 bp;
    u32 bw;
    u32 psm;
    u32 width;
    u32 height;
    u32 cpsm;
    u32 csa;
    u32 clutVersion;
    u64 texa;

    bool operator==(const TextureKey&) const = default;
};

struct TextureKeyHash
{
    size_t operator()(const TextureKey& key) const
    {
        u64 h = 1469598103934665603ull;
        const u32 words[] = {key.bp, key.bw, key.psm, key.width, key.height, key.cpsm, key.csa, key.clutVersion,
                             static_cast<u32>(key.texa), static_cast<u32>(key.texa >> 32)};
        for (u32 word : words)
        {
            h = (h ^ word) * 1099511628211ull;
        }

        return static_cast<size_t>(h);
    }
};

// A level decoded 8x8 texels at a time, as primitives need them (a frame buffer read as a 1024x1024 texture by a screen effect
// is decoded where the effect reads it). Every page a tile was read from is kept with its bytes then and the tiles it gave: a page
// written since with other bytes takes its tiles back (one written with the same bytes, the game uploading its textures again
// every frame, leaves them good)
constexpr u32 TileTexels = 8;

struct DecodedTexture
{
    std::vector<u32> texels;
    u32 tilesWide = 0;
    std::vector<u8> tileGood;
    std::vector<u32> pages;
    std::vector<u8> bytes;
    std::vector<std::vector<u32>> pageTiles;
    u32 generation = 0;
    // Changed when its texels change (made, or tiles taken back; not when tiles are added), and with every tile decoded
    u32 version = 0;
    u32 tilesVersion = 0;
    // Its tiles decoded so far, and whether that's all of them (draws then skip the tiles' check)
    u32 goodTiles = 0;
};

std::unordered_map<TextureKey, DecodedTexture, TextureKeyHash> g_TextureCache;
constexpr size_t MostCachedTextures = 512;

bool Indexed(u32 psm)
{
    return psm == PSMT8 || psm == PSMT8H || psm == PSMT4 || psm == PSMT4HL || psm == PSMT4HH;
}
}

namespace
{
// A primitive's rows drawn here, or shared out to the workers when there are enough of its pixels to be worth it
constexpr s64 ThreadedPixels = 16384;

template <typename Rows>
void DrawRows(s32 begin, s32 end, s32 width, const Rows& rows)
{
    if (static_cast<s64>(end - begin) * width < ThreadedPixels)
    {
        rows(begin, end);
        return;
    }

    std::function<void(int, int)> work = [&](int first, int last) { rows(first, last); };
    GetWorkers().ForRows(begin, end, work);
}

Pipeline MakePipeline(Gs* gs, const u16* clut, const RegTexa& texa, u64 dimx)
{
    Pipeline p;
    p.gs = gs;
    p.clut = clut;
    p.texa0 = texa.ta0;
    p.texa1 = texa.ta1;
    p.aem = texa.aem != 0;
    p.dimx = dimx;
    return p;
}

}

namespace
{
// The texels a primitive can read of its texture's first level (inclusive; a texel more around them for the bilinear filter and
// rounding), or false when it can read any (coordinates past the texture's edges that repeat, or behind the eye)
bool TexelRegion(const Gs::DrawState& state, const Vertex* vertexes, u32 count, s32* left, s32* top, s32* right, s32* bottom)
{
    if (vertexes == nullptr || count == 0)
    {
        return false;
    }

    f32 minS = 1e30f;
    f32 minT = 1e30f;
    f32 maxS = -1e30f;
    f32 maxT = -1e30f;
    const f32 width = static_cast<f32>(state.levels[0].width);
    const f32 height = static_cast<f32>(state.levels[0].height);
    for (u32 i = 0; i < count; i++)
    {
        const Vertex& v = vertexes[i];
        f32 s;
        f32 t;
        if (state.uv)
        {
            s = static_cast<f32>(v.u) / 16.0f;
            t = static_cast<f32>(v.v) / 16.0f;
        }
        else
        {
            if (!(v.q > 0.0f) || !std::isfinite(v.s / v.q) || !std::isfinite(v.t / v.q))
            {
                return false;
            }

            s = v.s / v.q * width;
            t = v.t / v.q * height;
        }

        minS = std::min(minS, s);
        maxS = std::max(maxS, s);
        minT = std::min(minT, t);
        maxT = std::max(maxT, t);
    }

    if (!(maxS - minS < 65536.0f) || !(maxT - minT < 65536.0f) || std::fabs(minS) > 65536.0f || std::fabs(minT) > 65536.0f)
    {
        return false;
    }

    // The texels the filter reads: a bilinear sample at u reads floor(u - 0.5) and the one after it (a nearest one floor(u),
    // within that); a little more each way for STQ's division at the pixels
    const f32 slack = state.uv ? 0.0f : 1.0f / 64.0f;
    *left = static_cast<s32>(std::floor(minS - 0.5f - slack));
    *right = static_cast<s32>(std::floor(maxS - 0.5f + slack)) + 1;
    *top = static_cast<s32>(std::floor(minT - 0.5f - slack));
    *bottom = static_cast<s32>(std::floor(maxT - 0.5f + slack)) + 1;
    return true;
}

// The region at a level, made to fit the level by its wrap mode; false when it has to be the whole level
bool FitRegion(u32 mode, u32 size, u32 minimum, u32 maximum, s32* low, s32* high)
{
    if (*low >= 0 && *high < static_cast<s32>(size))
    {
        return true;
    }

    switch (mode)
    {
    case WrapClamp:
        *low = std::clamp<s32>(*low, 0, static_cast<s32>(size) - 1);
        *high = std::clamp<s32>(*high, 0, static_cast<s32>(size) - 1);
        return true;
    case WrapRegionClamp:
        *low = std::clamp<s32>(*low, static_cast<s32>(minimum), static_cast<s32>(maximum));
        *high = std::clamp<s32>(*high, static_cast<s32>(minimum), static_cast<s32>(maximum));
        return *high < static_cast<s32>(size);
    default:
        return false;
    }
}
}

void Gs::DecodeTextures(DrawState& state, const Vertex* vertexes, u32 count)
{
    const RegTex0& tex0 = state.context->tex0;
    Pipeline p = MakePipeline(this, clut_, texa_, dimx_);
    s32 regionLeft = 0;
    s32 regionTop = 0;
    s32 regionRight = 0;
    s32 regionBottom = 0;
    bool regional = TexelRegion(state, vertexes, count, &regionLeft, &regionTop, &regionRight, &regionBottom);
    auto keyOf = [&](const TextureLevel& level) {
        TextureKey key = {level.bp, level.bw, level.psm, level.width, level.height, 0, 0, 0, 0};
        if (Indexed(level.psm))
        {
            key.cpsm = tex0.cpsm;
            key.csa = tex0.csa;
            key.clutVersion = clutVersion_;
        }

        key.texa = texa_.value & 0x000000FF000080FFull;
        return key;
    };
    // The same texture as the last draw's at a level, nothing written since (the same generation): its decode as it was
    static TextureKey memoKey[7];
    static DecodedTexture* memoDecoded[7] = {};
    static u32 memoGeneration[7] = {};
    // A full cache emptied before the draw's levels are decoded, never between them: a mipmapped draw's first levels' texels,
    // taken already, went with it, and the rasteriser read freed memory (the software GS drawing for the hardware renderer, which
    // fills the same cache, crashed so in SampleLevel)
    {
        size_t missing = 0;
        for (u32 index = 0; index < state.levelCount; index++)
        {
            missing += g_TextureCache.find(keyOf(state.levels[index])) == g_TextureCache.end() ? 1 : 0;
        }

        if (missing > 0 && g_TextureCache.size() + missing > MostCachedTextures)
        {
            g_TextureCache.clear();
            std::fill(std::begin(memoDecoded), std::end(memoDecoded), nullptr);
        }
    }

    for (u32 index = 0; index < state.levelCount; index++)
    {
        TextureLevel& level = state.levels[index];
        const TextureKey key = keyOf(level);
        const bool memoHit = memoDecoded[index] != nullptr && memoGeneration[index] == generation_ && memoKey[index] == key;
        DecodedTexture& decoded = memoHit ? *memoDecoded[index] : g_TextureCache[key];
        const u32 tilesWide = (level.width + TileTexels - 1) / TileTexels;
        const u32 tilesHigh = (level.height + TileTexels - 1) / TileTexels;
        // The decode's content version: new when it's made and when tiles are taken back (tiles added leave it: the texels a
        // draw already read stay as they were)
        static u32 contentVersions = 0;
        if (decoded.texels.empty())
        {
            decoded.version = ++contentVersions;
            decoded.texels.resize(static_cast<size_t>(level.width) * level.height);
            decoded.tilesWide = tilesWide;
            decoded.tileGood.assign(static_cast<size_t>(tilesWide) * tilesHigh, 0);
            decoded.generation = generation_;
        }
        else if (!memoHit)
        {
            // The pages written since: their tiles taken back where the bytes changed
            bool written = false;
            for (size_t n = 0; n < decoded.pages.size();)
            {
                u32 page = decoded.pages[n];
                if (PageWritten(page) > decoded.generation)
                {
                    written = true;
                    if (std::memcmp(decoded.bytes.data() + n * 8192, vm_ + static_cast<size_t>(page) * 8192, 8192) != 0)
                    {
                        for (u32 tile : decoded.pageTiles[n])
                        {
                            decoded.goodTiles -= decoded.tileGood[tile];
                            decoded.tileGood[tile] = 0;
                        }

                        decoded.version = ++contentVersions;

                        size_t last = decoded.pages.size() - 1;
                        decoded.pages[n] = decoded.pages[last];
                        std::memcpy(decoded.bytes.data() + n * 8192, decoded.bytes.data() + last * 8192, 8192);
                        decoded.pageTiles[n] = std::move(decoded.pageTiles[last]);
                        decoded.pages.pop_back();
                        decoded.bytes.resize(last * 8192);
                        decoded.pageTiles.pop_back();
                        continue;
                    }
                }

                n++;
            }

            if (written)
            {
                decoded.generation = generation_;
                NextGeneration();
            }
        }

        // The tiles the primitive reads
        s32 left = 0;
        s32 top = 0;
        s32 right = static_cast<s32>(level.width) - 1;
        s32 bottom = static_cast<s32>(level.height) - 1;
        if (regional)
        {
            s32 l = regionLeft >> index;
            s32 r = (regionRight >> index) + 1;
            s32 t = regionTop >> index;
            s32 b = (regionBottom >> index) + 1;
            if (FitRegion(state.wms, level.width, level.minu, level.maxu, &l, &r) &&
                FitRegion(state.wmt, level.height, level.minv, level.maxv, &t, &b))
            {
                left = l;
                right = r;
                top = t;
                bottom = b;
            }
        }

        bool added = false;
        const bool allGood = decoded.goodTiles == static_cast<u32>(decoded.tileGood.size());
        for (u32 ty = static_cast<u32>(top) / TileTexels; !allGood && ty <= static_cast<u32>(bottom) / TileTexels; ty++)
        {
            for (u32 tx = static_cast<u32>(left) / TileTexels; tx <= static_cast<u32>(right) / TileTexels; tx++)
            {
                u32 tile = ty * tilesWide + tx;
                if (decoded.tileGood[tile])
                {
                    continue;
                }

                // A tile is within a page: every format's page is a whole number of tiles
                u32 x0 = tx * TileTexels;
                u32 y0 = ty * TileTexels;
                u32 page = PixelPage(level.psm, x0, y0, level.bp, level.bw) & (LocalMemoryBytes / 8192 - 1);
                size_t at = 0;
                while (at < decoded.pages.size() && decoded.pages[at] != page)
                {
                    at++;
                }

                if (at == decoded.pages.size())
                {
                    decoded.pages.push_back(page);
                    decoded.bytes.resize(decoded.pages.size() * 8192);
                    std::memcpy(decoded.bytes.data() + at * 8192, vm_ + static_cast<size_t>(page) * 8192, 8192);
                    decoded.pageTiles.emplace_back();
                }

                decoded.pageTiles[at].push_back(tile);
                u32 x1 = std::min(x0 + TileTexels, level.width);
                u32 y1 = std::min(y0 + TileTexels, level.height);
                for (u32 y = y0; y < y1; y++)
                {
                    u32* row = decoded.texels.data() + static_cast<size_t>(y) * level.width;
                    for (u32 x = x0; x < x1; x++)
                    {
                        row[x] = DecodeTexel(*this, level, x, y, p, tex0);
                    }
                }

                decoded.tileGood[tile] = 1;
                decoded.goodTiles++;
                added = true;
                static u32 tileVersions = 0;
                decoded.tilesVersion = ++tileVersions;

            }
        }

        if (added && decoded.pages.size() > 0)
        {
            // The pages read now are as they were when the tiles were
            decoded.generation = std::max(decoded.generation, generation_);
            NextGeneration();
        }

        level.texels = decoded.texels.data();
        level.stride = level.width;
        level.rows = level.height;
        level.cacheId = &decoded;
        level.version = decoded.version;
        level.tilesVersion = decoded.tilesVersion;
        memoKey[index] = key;
        memoDecoded[index] = &decoded;
        memoGeneration[index] = generation_;
    }
}

namespace
{
// A vertex's texture coordinates in 16.16 texels, or STQ scaled by the texture's size for a division at each pixel
struct TexCoords
{
    f32 s;
    f32 t;
    f32 q;
};

TexCoords TextureCoordinates(const Gs::DrawState& state, const Vertex& v)
{
    if (state.uv)
    {
        return {static_cast<f32>(v.u) * 4096.0f, static_cast<f32>(v.v) * 4096.0f, 1.0f};
    }

    f32 width = static_cast<f32>(state.levels[0].width) * 65536.0f;
    f32 height = static_cast<f32>(state.levels[0].height) * 65536.0f;
    return {v.s * width, v.t * height, v.q};
}

s32 ToFixed(f32 value)
{
    if (!(value > -2147483648.0f))
    {
        return static_cast<s32>(0x80000000u);
    }

    if (value >= 2147483647.0f)
    {
        return 0x7FFFFFFF;
    }

    return static_cast<s32>(value);
}
}

#include "gsspan.inl"

void Gs::DrawSprite(const DrawState& state, const Vertex& v0, const Vertex& v1)
{
    Pipeline p = MakePipeline(this, clut_, texa_, dimx_);
    const RegTex0& tex0 = state.context->tex0;
    s32 x0 = static_cast<s32>(v0.x) - state.offsetX;
    s32 y0 = static_cast<s32>(v0.y) - state.offsetY;
    s32 x1 = static_cast<s32>(v1.x) - state.offsetX;
    s32 y1 = static_cast<s32>(v1.y) - state.offsetY;
    // A sprite's texture coordinates: its two corners', Q the second vertex's for both
    TexCoords t0 = {};
    TexCoords t1 = {};
    if (state.textured)
    {
        t0 = TextureCoordinates(state, v0);
        t1 = TextureCoordinates(state, v1);
        if (!state.uv)
        {
            t0.s /= v1.q;
            t0.t /= v1.q;
            t1.s /= v1.q;
            t1.t /= v1.q;
            t0.q = v1.q;
            t1.q = v1.q;
        }
    }

    if (x1 < x0)
    {
        std::swap(x0, x1);
        std::swap(t0.s, t1.s);
    }

    if (y1 < y0)
    {
        std::swap(y0, y1);
        std::swap(t0.t, t1.t);
    }

    s32 left = std::max(CeilPixel(x0), state.scissorLeft);
    s32 right = std::min(CeilPixel(x1) - 1, state.scissorRight);
    s32 top = std::max(CeilPixel(y0), state.scissorTop);
    s32 bottom = std::min(CeilPixel(y1) - 1, state.scissorBottom);
    if (left > right || top > bottom)
    {
        return;
    }

    f32 dsdx = x1 != x0 ? (t1.s - t0.s) / static_cast<f32>(x1 - x0) : 0.0f;
    f32 dtdy = y1 != y0 ? (t1.t - t0.t) / static_cast<f32>(y1 - y0) : 0.0f;
    PixelInput in = {};
    in.z = v1.z;
    in.r = static_cast<s32>(v1.r) << 7;
    in.g = static_cast<s32>(v1.g) << 7;
    in.b = static_cast<s32>(v1.b) << 7;
    in.a = static_cast<s32>(v1.a) << 7;
    in.f = v1.f;
    in.q = state.uv ? 1.0f : v1.q;
    pixels += static_cast<u64>(bottom - top + 1) * static_cast<u64>(right - left + 1);
    const BufferLayout& frameLayout = Layout(state.fpsm, state.fbp, state.fbw);
    const BufferLayout& depthLayout = Layout(state.zpsm, state.zbp, state.fbw);
    SpanShader shader = SpanShaderFor(state, frameLayout, depthLayout);
    auto rows = [&](s32 first, s32 end) {
        PixelInput row = in;
        for (s32 y = first; y < end; y++)
        {
            f32 tv = t0.t + dtdy * static_cast<f32>((y << 4) - y0);
            if (shader != nullptr)
            {
                Span span = {};
                span.y = y;
                span.x0 = left;
                span.x1 = right;
                span.z = v1.z;
                span.colour[0] = v1.r;
                span.colour[1] = v1.g;
                span.colour[2] = v1.b;
                span.colour[3] = v1.a;
                span.f = v1.f;
                span.s = t0.s + dsdx * static_cast<f32>((left << 4) - x0);
                span.ds = dsdx * 16.0f;
                span.t = tv;
                span.q = in.q;
                span.divide = false;
                shader(*this, state, p, span, tex0, frameLayout, depthLayout);
                continue;
            }

            row.v = ToFixed(tv);
            for (s32 x = left; x <= right; x++)
            {
                f32 tu = t0.s + dsdx * static_cast<f32>((x << 4) - x0);
                row.u = ToFixed(tu);
                ShadePixel(*this, state, p, x, y, row, tex0);
            }
        }
    };
    DrawRows(top, bottom + 1, right - left + 1, rows);
}

void Gs::DrawTriangle(const DrawState& state, const Vertex& a, const Vertex& b, const Vertex& c)
{
    Pipeline p = MakePipeline(this, clut_, texa_, dimx_);
    const RegTex0& tex0 = state.context->tex0;
    const Vertex* v[3] = {&a, &b, &c};
    s64 x[3];
    s64 y[3];
    for (u32 i = 0; i < 3; i++)
    {
        x[i] = static_cast<s64>(v[i]->x) - state.offsetX;
        y[i] = static_cast<s64>(v[i]->y) - state.offsetY;
    }

    s64 area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
    if (area == 0)
    {
        return;
    }

    // The interior on the edges' positive side
    if (area < 0)
    {
        std::swap(v[1], v[2]);
        std::swap(x[1], x[2]);
        std::swap(y[1], y[2]);
        area = -area;
    }

    s64 minX = std::min({x[0], x[1], x[2]});
    s64 maxX = std::max({x[0], x[1], x[2]});
    s64 minY = std::min({y[0], y[1], y[2]});
    s64 maxY = std::max({y[0], y[1], y[2]});
    s32 left = std::max(static_cast<s32>((minX + 15) >> 4), state.scissorLeft);
    s32 right = std::min(static_cast<s32>((maxX + 15) >> 4) - 1, state.scissorRight);
    s32 top = std::max(static_cast<s32>((minY + 15) >> 4), state.scissorTop);
    s32 bottom = std::min(static_cast<s32>((maxY + 15) >> 4) - 1, state.scissorBottom);
    if (left > right || top > bottom)
    {
        return;
    }

    // The edge from vertex i to the next: E(P) = (xb - xa)(Py - ya) - (yb - ya)(Px - xa), positive inside. A point on an edge is
    // inside when the edge is a left one (going up) or a top one (horizontal, going right)
    struct Edge
    {
        s64 dx;
        s64 dy;
        s64 x0;
        s64 y0;
        bool inclusive;
    } edges[3];
    for (u32 i = 0; i < 3; i++)
    {
        u32 j = (i + 1) % 3;
        Edge& e = edges[i];
        e.dx = x[j] - x[i];
        e.dy = y[j] - y[i];
        e.x0 = x[i];
        e.y0 = y[i];
        e.inclusive = e.dy < 0 || (e.dy == 0 && e.dx > 0);
    }

    // The attributes' planes over the 12.4 plane: value = v0 + ddx * (Px - x0) + ddy * (Py - y0)
    auto plane = [&](double a0, double a1, double a2, double& ddx, double& ddy) {
        double d1 = a1 - a0;
        double d2 = a2 - a0;
        double ex1 = static_cast<double>(x[1] - x[0]);
        double ey1 = static_cast<double>(y[1] - y[0]);
        double ex2 = static_cast<double>(x[2] - x[0]);
        double ey2 = static_cast<double>(y[2] - y[0]);
        double det = static_cast<double>(area);
        ddx = (d1 * ey2 - d2 * ey1) / det;
        ddy = (d2 * ex1 - d1 * ex2) / det;
    };

    double zx;
    double zy;
    plane(v[0]->z, v[1]->z, v[2]->z, zx, zy);
    // Flat shading takes the last vertex's colour (the one that drew)
    const Vertex& last = c;
    double cx[4] = {};
    double cy[4] = {};
    double c0[4];
    if (state.gouraud)
    {
        plane(v[0]->r, v[1]->r, v[2]->r, cx[0], cy[0]);
        plane(v[0]->g, v[1]->g, v[2]->g, cx[1], cy[1]);
        plane(v[0]->b, v[1]->b, v[2]->b, cx[2], cy[2]);
        plane(v[0]->a, v[1]->a, v[2]->a, cx[3], cy[3]);
        c0[0] = v[0]->r;
        c0[1] = v[0]->g;
        c0[2] = v[0]->b;
        c0[3] = v[0]->a;
    }
    else
    {
        c0[0] = last.r;
        c0[1] = last.g;
        c0[2] = last.b;
        c0[3] = last.a;
    }

    double fx;
    double fy;
    plane(v[0]->f, v[1]->f, v[2]->f, fx, fy);
    TexCoords t[3] = {};
    double sx = 0;
    double sy = 0;
    double tx = 0;
    double ty = 0;
    double qx = 0;
    double qy = 0;
    if (state.textured)
    {
        for (u32 i = 0; i < 3; i++)
        {
            t[i] = TextureCoordinates(state, *v[i]);
        }

        plane(t[0].s, t[1].s, t[2].s, sx, sy);
        plane(t[0].t, t[1].t, t[2].t, tx, ty);
        plane(t[0].q, t[1].q, t[2].q, qx, qy);
    }

    pixels += static_cast<u64>(bottom - top + 1) * static_cast<u64>(right - left + 1);
    const BufferLayout& frameLayout = Layout(state.fpsm, state.fbp, state.fbw);
    const BufferLayout& depthLayout = Layout(state.zpsm, state.zbp, state.fbw);
    SpanShader shader = SpanShaderFor(state, frameLayout, depthLayout);
    auto inside = [&](s64 sampleX, s64 sampleY) {
        for (const Edge& e : edges)
        {
            s64 value = e.dx * (sampleY - e.y0) - e.dy * (sampleX - e.x0);
            if (value < 0 || (value == 0 && !e.inclusive))
            {
                return false;
            }
        }

        return true;
    };
    auto rows = [&](s32 first, s32 end) {
    for (s32 py = first; py < end; py++)
    {
        s64 sampleY = static_cast<s64>(py) << 4;
        double dy = static_cast<double>(sampleY - y[0]);
        if (shader != nullptr)
        {
            // The row's run: the triangle covers one run of each row (it's convex)
            s32 x0 = left;
            while (x0 <= right && !inside(static_cast<s64>(x0) << 4, sampleY))
            {
                x0++;
            }

            if (x0 > right)
            {
                continue;
            }

            s32 x1 = x0;
            while (x1 + 1 <= right && inside(static_cast<s64>(x1 + 1) << 4, sampleY))
            {
                x1++;
            }

            double dx = static_cast<double>((static_cast<s64>(x0) << 4) - x[0]);
            Span span = {};
            span.y = py;
            span.x0 = x0;
            span.x1 = x1;
            span.z = static_cast<double>(v[0]->z) + zx * dx + zy * dy;
            span.dz = zx * 16.0;
            for (u32 n = 0; n < 4; n++)
            {
                if (state.gouraud)
                {
                    span.colour[n] = static_cast<f32>(c0[n] + cx[n] * dx + cy[n] * dy);
                    span.dcolour[n] = static_cast<f32>(cx[n] * 16.0);
                }
                else
                {
                    span.colour[n] = static_cast<f32>(c0[n]);
                }
            }

            span.f = static_cast<f32>(static_cast<double>(v[0]->f) + fx * dx + fy * dy);
            span.df = static_cast<f32>(fx * 16.0);
            if (state.textured)
            {
                span.s = static_cast<f32>(t[0].s + sx * dx + sy * dy);
                span.t = static_cast<f32>(t[0].t + tx * dx + ty * dy);
                span.q = static_cast<f32>(t[0].q + qx * dx + qy * dy);
                span.ds = static_cast<f32>(sx * 16.0);
                span.dt = static_cast<f32>(tx * 16.0);
                span.dq = static_cast<f32>(qx * 16.0);
                span.divide = !state.uv;
            }

            shader(*this, state, p, span, tex0, frameLayout, depthLayout);
            continue;
        }

        for (s32 px = left; px <= right; px++)
        {
            s64 sampleX = static_cast<s64>(px) << 4;
            if (!inside(sampleX, sampleY))
            {
                continue;
            }

            double dx = static_cast<double>(sampleX - x[0]);
            PixelInput in;
            double z = static_cast<double>(v[0]->z) + zx * dx + zy * dy;
            in.z = z <= 0.0 ? 0u : z >= 4294967295.0 ? 0xFFFFFFFFu : static_cast<u32>(z);
            if (state.gouraud)
            {
                in.r = static_cast<s32>((c0[0] + cx[0] * dx + cy[0] * dy) * 128.0);
                in.g = static_cast<s32>((c0[1] + cx[1] * dx + cy[1] * dy) * 128.0);
                in.b = static_cast<s32>((c0[2] + cx[2] * dx + cy[2] * dy) * 128.0);
                in.a = static_cast<s32>((c0[3] + cx[3] * dx + cy[3] * dy) * 128.0);
            }
            else
            {
                in.r = static_cast<s32>(c0[0]) << 7;
                in.g = static_cast<s32>(c0[1]) << 7;
                in.b = static_cast<s32>(c0[2]) << 7;
                in.a = static_cast<s32>(c0[3]) << 7;
            }

            in.r = std::clamp(in.r, 0, 255 << 7);
            in.g = std::clamp(in.g, 0, 255 << 7);
            in.b = std::clamp(in.b, 0, 255 << 7);
            in.a = std::clamp(in.a, 0, 255 << 7);
            double f = static_cast<double>(v[0]->f) + fx * dx + fy * dy;
            in.f = static_cast<u32>(std::clamp(f, 0.0, 255.0));
            in.u = 0;
            in.v = 0;
            in.q = 1.0f;
            if (state.textured)
            {
                double s = t[0].s + sx * dx + sy * dy;
                double tt = t[0].t + tx * dx + ty * dy;
                double q = t[0].q + qx * dx + qy * dy;
                if (!state.uv)
                {
                    s /= q;
                    tt /= q;
                }

                in.u = ToFixed(static_cast<f32>(s));
                in.v = ToFixed(static_cast<f32>(tt));
                in.q = static_cast<f32>(q);
            }

            ShadePixel(*this, state, p, px, py, in, tex0);
        }
    }
    };
    DrawRows(top, bottom + 1, right - left + 1, rows);
}

// Lines: a pixel for each step along the major axis from the first end, the attributes interpolated along it (the GS's line
// DDA; the game doesn't draw lines)
void Gs::DrawLine(const DrawState& state, const Vertex& a, const Vertex& b)
{
    Pipeline p = MakePipeline(this, clut_, texa_, dimx_);
    const RegTex0& tex0 = state.context->tex0;
    double x0 = (static_cast<double>(a.x) - state.offsetX) / 16.0;
    double y0 = (static_cast<double>(a.y) - state.offsetY) / 16.0;
    double x1 = (static_cast<double>(b.x) - state.offsetX) / 16.0;
    double y1 = (static_cast<double>(b.y) - state.offsetY) / 16.0;
    double length = std::max(std::fabs(x1 - x0), std::fabs(y1 - y0));
    s32 steps = static_cast<s32>(std::ceil(length));
    TexCoords ta = state.textured ? TextureCoordinates(state, a) : TexCoords{};
    TexCoords tb = state.textured ? TextureCoordinates(state, b) : TexCoords{};
    for (s32 i = 0; i < steps; i++)
    {
        double f = steps > 0 ? static_cast<double>(i) / steps : 0.0;
        s32 px = static_cast<s32>(std::floor(x0 + (x1 - x0) * f + 0.5));
        s32 py = static_cast<s32>(std::floor(y0 + (y1 - y0) * f + 0.5));
        if (px < state.scissorLeft || px > state.scissorRight || py < state.scissorTop || py > state.scissorBottom)
        {
            continue;
        }

        const Vertex& colour = state.gouraud ? a : b;
        PixelInput in;
        in.z = static_cast<u32>(a.z + (static_cast<double>(b.z) - a.z) * f);
        auto lerp = [&](u8 ca, u8 cb) {
            return state.gouraud ? static_cast<s32>((ca + (static_cast<double>(cb) - ca) * f) * 128.0) : static_cast<s32>(cb) << 7;
        };
        in.r = lerp(colour.r, b.r);
        in.g = lerp(colour.g, b.g);
        in.b = lerp(colour.b, b.b);
        in.a = lerp(colour.a, b.a);
        in.f = static_cast<u32>(a.f + (static_cast<double>(b.f) - a.f) * f);
        double s = ta.s + (tb.s - ta.s) * f;
        double t = ta.t + (tb.t - ta.t) * f;
        double q = ta.q + (tb.q - ta.q) * f;
        if (state.textured && !state.uv)
        {
            s /= q;
            t /= q;
        }

        in.u = ToFixed(static_cast<f32>(s));
        in.v = ToFixed(static_cast<f32>(t));
        in.q = static_cast<f32>(q);
        ShadePixel(*this, state, p, px, py, in, tex0);
    }
}

void Gs::DrawPoint(const DrawState& state, const Vertex& a)
{
    Pipeline p = MakePipeline(this, clut_, texa_, dimx_);
    s32 px = (static_cast<s32>(a.x) - state.offsetX + 8) >> 4;
    s32 py = (static_cast<s32>(a.y) - state.offsetY + 8) >> 4;
    if (px < state.scissorLeft || px > state.scissorRight || py < state.scissorTop || py > state.scissorBottom)
    {
        return;
    }

    PixelInput in;
    in.z = a.z;
    in.r = static_cast<s32>(a.r) << 7;
    in.g = static_cast<s32>(a.g) << 7;
    in.b = static_cast<s32>(a.b) << 7;
    in.a = static_cast<s32>(a.a) << 7;
    in.f = a.f;
    TexCoords t = state.textured ? TextureCoordinates(state, a) : TexCoords{};
    if (state.textured && !state.uv)
    {
        t.s /= t.q;
        t.t /= t.q;
    }

    in.u = ToFixed(t.s);
    in.v = ToFixed(t.t);
    in.q = t.q;
    ShadePixel(*this, state, p, px, py, in, state.context->tex0);
}

namespace
{
bool DirectColour(u32 psm)
{
    return psm == PSMCT32 || psm == PSMCT24 || psm == PSMCT16 || psm == PSMCT16S || psm == PSMZ32 || psm == PSMZ24 ||
           psm == PSMZ16 || psm == PSMZ16S;
}
}

// The primitive again into the shadows, its places and the scissor scaled; textures read from shadows where they have them.
// Then the shadows' pages local memory's drawing wrote are the shadows' own again
void Gs::DrawScaled(const Vertex* vertexes, u32 count, u32 drawGeneration)
{
    const s32 n = static_cast<s32>(scale_);
    DrawState state;
    SetUpState(state, &vertexes[count - 1]);
    bool depthUsed = state.ztst == 2 || state.ztst == 3 || state.zWritten;
    state.scale = scale_;
    state.frameShadow = ShadowFor(state.fpsm, state.fbp, state.fbw, false);
    state.depthShadow = depthUsed ? ShadowFor(state.zpsm, state.zbp, state.fbw, false) : nullptr;
    if (state.frameShadow == nullptr)
    {
        return;
    }

    if (state.textured)
    {
        DecodeTextures(state, vertexes, count);
        Pipeline p = MakePipeline(this, clut_, texa_, dimx_);
        for (u32 index = 0; index < state.levelCount; index++)
        {
            TextureLevel& level = state.levels[index];
            Shadow* shadow = DirectColour(level.psm) ? ShadowFor(level.psm, level.bp, level.bw, false) : nullptr;
            if (shadow == nullptr)
            {
                continue;
            }

            // The shadow's samples decoded as the texture's format (TEXA for 24 and 16 bit pixels)
            u64 texa = texa_.value & 0x000000FF000080FFull;
            if (shadow->texelsVersion != shadow->version || shadow->texelsTexa != texa || shadow->texels.size() != shadow->samples.size())
            {
                shadow->texels.resize(shadow->samples.size());
                for (size_t i = 0; i < shadow->samples.size(); i++)
                {
                    u32 c = shadow->samples[i];
                    switch (level.psm)
                    {
                    case PSMCT24:
                    case PSMZ24:
                    {
                        c &= 0xFFFFFF;
                        u32 alpha = (p.aem && c == 0) ? 0 : p.texa0;
                        c |= alpha << 24;
                        break;
                    }
                    case PSMCT16:
                    case PSMCT16S:
                    case PSMZ16:
                    case PSMZ16S:
                        c = Expand16(c & 0xFFFF, p);
                        break;
                    default:
                        break;
                    }

                    shadow->texels[i] = c;
                }

                shadow->texelsVersion = shadow->version;
                shadow->texelsTexa = texa;
            }

            level.texels = shadow->texels.data();
            level.cacheId = shadow;
            level.version = shadow->version;
            level.tilesVersion = shadow->version;
            level.stride = shadow->width * shadow->scale;
            level.rows = shadow->height * shadow->scale;
            level.scale = shadow->scale;
            level.width *= shadow->scale;
            level.height *= shadow->scale;
        }
    }

    state.offsetX *= n;
    state.offsetY *= n;
    state.scissorLeft *= n;
    state.scissorTop *= n;
    state.scissorRight = state.scissorRight * n + n - 1;
    state.scissorBottom = state.scissorBottom * n + n - 1;
    Vertex scaled[3];
    for (u32 i = 0; i < count; i++)
    {
        scaled[i] = vertexes[i];
        // The samples of a pixel centred on the point the PS2 samples it at (its corner): the places moved half the samples
        // less one sample along
        scaled[i].x = vertexes[i].x * scale_ + (scale_ - 1) * 8;
        scaled[i].y = vertexes[i].y * scale_ + (scale_ - 1) * 8;
    }

    if (!glyphs_.empty() && state.textured && state.uv && state.type == PrimSprite)
    {
        GlyphImage(state, scaled, count);
    }

    u64 counted = pixels;
    switch (state.type)
    {
    case PrimPoint:
        DrawPoint(state, scaled[0]);
        break;
    case PrimLine:
    case PrimLineStrip:
        DrawLine(state, scaled[0], scaled[1]);
        break;
    case PrimTriangle:
    case PrimTriangleStrip:
    case PrimTriangleFan:
        DrawTriangle(state, scaled[0], scaled[1], scaled[2]);
        break;
    case PrimSprite:
        DrawSprite(state, scaled[0], scaled[1]);
        break;
    default:
        break;
    }

    pixels = counted;
    for (Shadow* shadow : {state.frameShadow, state.depthShadow})
    {
        if (shadow == nullptr)
        {
            continue;
        }

        for (u32 page = 0; page < LocalMemoryBytes / 8192; page++)
        {
            if (pageWritten_[page] == drawGeneration && shadow->synced[page] < drawGeneration)
            {
                shadow->synced[page] = drawGeneration;
            }
        }

        shadow->version++;
    }
}
}

namespace Gs
{
void Gs::SetGlyphOverride(GlyphOverride glyph)
{
    static u32 versions = 0;
    glyph.version = ++versions;
    for (size_t i = 0; i < glyphs_.size(); i++)
    {
        GlyphOverride& known = glyphs_[i];
        if (known.u0 == glyph.u0 && known.v0 == glyph.v0 && known.u1 == glyph.u1 && known.v1 == glyph.v1)
        {
            if (glyph.texels.empty())
            {
                glyphs_.erase(glyphs_.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else
            {
                known = std::move(glyph);
            }

            return;
        }
    }

    if (!glyph.texels.empty())
    {
        glyphs_.push_back(std::move(glyph));
    }
}

bool Gs::GlyphImage(DrawState& state, Vertex* vertexes, u32 count) const
{
    if (count < 2)
    {
        return false;
    }

    // The sprite's corners: either vertex may be the rectangle's first
    auto near = [](u32 a, s32 b) { return std::abs(static_cast<s32>(a) - b) <= 1; };
    for (const GlyphOverride& glyph : glyphs_)
    {
        const Vertex& a = vertexes[0];
        const Vertex& b = vertexes[1];
        bool forward = near(a.u, glyph.u0) && near(a.v, glyph.v0) && near(b.u, glyph.u1) && near(b.v, glyph.v1);
        bool backward = near(b.u, glyph.u0) && near(b.v, glyph.v0) && near(a.u, glyph.u1) && near(a.v, glyph.v1);
        if (!forward && !backward)
        {
            continue;
        }

        // The image's texels at the rectangle's: its left at u0, its top row at v0
        const s64 du = glyph.u1 - glyph.u0;
        const s64 dv = glyph.v1 - glyph.v0;
        for (u32 i = 0; i < 2; i++)
        {
            Vertex& v = vertexes[i];
            const bool first = (i == 0) == forward;
            v.u = static_cast<u32>(first ? 0 : glyph.width * 16);
            v.v = static_cast<u32>(first ? 0 : glyph.height * 16);
        }

        (void)du;
        (void)dv;
        TextureLevel& level = state.levels[0];
        level.bp = 0;
        level.bw = 0;
        level.psm = PSMCT32;
        level.width = glyph.width;
        level.height = glyph.height;
        level.minu = 0;
        level.maxu = glyph.width - 1;
        level.minv = 0;
        level.maxv = glyph.height - 1;
        level.texels = glyph.texels.data();
        level.cacheId = &glyph;
        level.version = glyph.version;
        level.tilesVersion = glyph.version;
        level.scale = 1;
        level.stride = glyph.width;
        level.rows = glyph.height;
        state.levelCount = 1;
        state.mxl = 0;
        state.mipmapped = false;
        state.wms = WrapClamp;
        state.wmt = WrapClamp;
        state.tcc = true;
        return true;
    }

    return false;
}
}

namespace Gs
{
void Gs::DecodeForBackend(DrawState& state, const Vertex* vertexes, u32 count)
{
    stateTouched_ = true;
    DecodeTextures(state, vertexes, count);
}

bool Gs::GlyphForBackend(DrawState& state, Vertex* vertexes, u32 count) const
{
    if (!glyphs_.empty() && GlyphImage(state, vertexes, count))
    {
        stateTouched_ = true;
        return true;
    }

    return false;
}
}

namespace Gs
{
void TexturePagesForDraw(const Gs::DrawState& state, const Vertex* vertexes, u32 count, PageSet& pages)
{
    s32 regionLeft = 0;
    s32 regionTop = 0;
    s32 regionRight = 0;
    s32 regionBottom = 0;
    bool regional = TexelRegion(state, vertexes, count, &regionLeft, &regionTop, &regionRight, &regionBottom);
    for (u32 index = 0; index < state.levelCount; index++)
    {
        const TextureLevel& level = state.levels[index];
        s32 left = 0;
        s32 top = 0;
        s32 right = static_cast<s32>(level.width) - 1;
        s32 bottom = static_cast<s32>(level.height) - 1;
        if (regional)
        {
            s32 l = regionLeft >> index;
            s32 r = (regionRight >> index) + 1;
            s32 t = regionTop >> index;
            s32 b = (regionBottom >> index) + 1;
            if (FitRegion(state.wms, level.width, level.minu, level.maxu, &l, &r) &&
                FitRegion(state.wmt, level.height, level.minv, level.maxv, &t, &b))
            {
                // The decode's tiles round out to 8 texels
                left = l & ~7;
                top = t & ~7;
                right = std::min(r | 7, static_cast<s32>(level.width) - 1);
                bottom = std::min(b | 7, static_cast<s32>(level.height) - 1);
            }
        }

        PagesOfRect(level.psm, level.bp, level.bw, left, top, right, bottom, pages);
    }
}

bool TextureRegionForDraw(const Gs::DrawState& state, const Vertex* vertexes, u32 count, s32* left, s32* top, s32* right,
                          s32* bottom)
{
    return TexelRegion(state, vertexes, count, left, top, right, bottom);
}
}

namespace Gs
{
u32 BackendTexel(const Gs& gs, const TextureLevel& level, u32 x, u32 y, const RegTex0& tex0)
{
    Pipeline p = MakePipeline(const_cast<Gs*>(&gs), gs.clut(), gs.texa(), gs.dimx());
    return DecodeTexel(gs, level, x, y, p, tex0);
}

u32 BackendClutEntry(const Gs& gs, u32 index, const RegTex0& tex0)
{
    Pipeline p = MakePipeline(const_cast<Gs*>(&gs), gs.clut(), gs.texa(), gs.dimx());
    return ClutColour(index, tex0.cpsm, tex0.csa, p);
}
}
