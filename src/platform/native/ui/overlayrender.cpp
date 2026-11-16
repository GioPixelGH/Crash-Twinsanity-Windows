// The overlay's drawing (through a graphics backend, hw/gpu.h: the window's on the presenter's thread through graphics/presenter.h's
// hook, or one of its own offscreen for the shots; OpenGL's or Vulkan's, THE LIST 72): the frame's commands in the 1280 x 720
// reference space scaled to the window. One program: rounded boxes as a signed distance (a vertical gradient, a border, a soft
// edge for shadows), glyphs from atlases baked per font and pixel size (stb_truetype), the options' backdrop worked out per pixel
// (its gradient, the ring target, the Twinsanity spiral of native/design/options/spiral_grey.svg: two Archimedean arms
// r = 10 + 35.45 theta over 2.2 turns), images
#include "ui/overlayinternal.h"
#include "ui/nativeui.h"

#include "graphics/display.h"
#include "graphics/hw/gpu.h"
#include "graphics/presenter.h"
#include "native.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <tuple>

#include "../../../../native/third_party/stb/stb_truetype.h"

namespace NativeUi
{
namespace
{
// (The backend's vertexes: the place at location 0, the texture's coordinates at 3)
constexpr const char* VertexSource = R"(#version 330 core
layout(location = 0) in vec2 position;
layout(location = 3) in vec3 uv;
uniform vec2 viewport;
out vec2 pixel;
out vec2 texel;
void main()
{
    pixel = position;
    texel = uv.xy;
    gl_Position = vec4(position.x / viewport.x * 2.0 - 1.0, 1.0 - position.y / viewport.y * 2.0, 0.0, 1.0);
}
)";

constexpr const char* FragmentSource = R"(#version 330 core
in vec2 pixel;
in vec2 texel;
out vec4 colour;
uniform int mode;
uniform sampler2D atlas;
uniform vec4 rect;
uniform float radius;
uniform vec4 fillTop;
uniform vec4 fillBottom;
uniform float border;
uniform vec4 borderColour;
uniform float blur;
uniform vec4 tint;
uniform float scale;
uniform vec2 offset;
uniform float time;

float RoundBox(vec2 p, vec2 halfSize, float r)
{
    vec2 q = abs(p) - halfSize + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

vec4 Over(vec4 under, vec3 c, float a)
{
    return vec4(mix(under.rgb, c, a), under.a);
}

// The distance (in the spiral's own units) to an arm r = a + b * theta, theta from 0 to its end, its phase
float ArmDistance(float r, float angle, float a, float b, float phase, float end)
{
    const float tau = 6.28318530718;
    float base = mod(angle - phase, tau);
    float best = 1e9;
    for (int k = 0; k < 4; k++)
    {
        float theta = base + tau * float(k);
        if (theta <= end)
        {
            best = min(best, abs(r - (a + b * theta)));
        }
    }

    return best;
}

void main()
{
    if (mode == 0)
    {
        // A rounded box: its fill (a vertical gradient), its border, an edge softened by blur (a shadow)
        vec2 centre = rect.xy + rect.zw * 0.5;
        float d = RoundBox(pixel - centre, rect.zw * 0.5, radius);
        float t = clamp((pixel.y - rect.y) / max(rect.w, 1.0), 0.0, 1.0);
        vec4 fill = fillBottom.a < 0.0 ? fillTop : mix(fillTop, fillBottom, t);
        if (blur > 0.0)
        {
            float a = 1.0 - smoothstep(-blur, blur, d);
            colour = vec4(fill.rgb, fill.a * a);
            return;
        }

        float coverage = clamp(0.5 - d, 0.0, 1.0);
        vec4 c = fill;
        if (border > 0.0)
        {
            float edge = clamp(d + border + 0.5, 0.0, 1.0);
            c = mix(fill, borderColour, edge);
        }

        colour = vec4(c.rgb, c.a * coverage);
    }
    else if (mode == 1)
    {
        colour = vec4(tint.rgb, tint.a * texture(atlas, texel).r);
    }
    else if (mode == 2)
    {
        // The options' backdrop in the reference space
        vec2 q = (pixel - offset) / scale;
        vec2 direction = normalize(vec2(1.0, 1.0));
        float lengthAlong = 1280.0 * 0.70710678 + 720.0 * 0.70710678;
        float g = clamp(dot(q - vec2(640.0, 360.0), direction) / lengthAlong + 0.5, 0.0, 1.0);
        vec3 c0 = vec3(14.0, 95.0, 184.0) / 255.0;
        vec3 c1 = vec3(10.0, 59.0, 122.0) / 255.0;
        vec3 c2 = vec3(7.0, 31.0, 69.0) / 255.0;
        vec3 base = g < 0.55 ? mix(c0, c1, g / 0.55) : mix(c1, c2, (g - 0.55) / 0.45);
        vec4 c = vec4(base, 1.0);
        // The ring target (bands of 70)
        float ringDistance = length(q - vec2(100.0, 460.0));
        if (ringDistance < 320.0)
        {
            bool light = mod(floor(ringDistance / 70.0), 2.0) < 1.0;
            vec3 band = light ? vec3(232.0, 238.0, 246.0) / 255.0 : vec3(22.0, 118.0, 210.0) / 255.0;
            float edge = clamp(320.0 - ringDistance, 0.0, 1.0);
            c = Over(c, band, 0.18 * edge);
        }

        // The spiral: the svg's 1000 units over 1300 reference pixels, its middle at (270, 350), turning a turn in 90 seconds
        vec2 s = (q - vec2(270.0, 350.0)) / 1.3;
        float r = length(s);
        float angle = atan(s.y, s.x) - time * 6.28318530718 / 90.0;
        float pixelInSvg = 1.0 / (scale * 1.3);
        float end = 2.2 * 6.28318530718;
        float white = min(ArmDistance(r, angle, 10.0, 35.4475, 0.0, end), ArmDistance(r, angle, 10.0, 35.4475, 3.14159265, end));
        float whiteCover = clamp((35.0 - white) / pixelInSvg + 0.5, 0.0, 1.0) * step(r, 520.0);
        c = Over(c, vec3(1.0), 0.18 * 0.7 * whiteCover);
        float blue = min(ArmDistance(r, angle, 9.854, 35.4477, 0.4456, end), ArmDistance(r, angle, 9.854, 35.4477, 3.5872, end));
        float blueCover = clamp((11.0 - blue) / pixelInSvg + 0.5, 0.0, 1.0) * step(r, 520.0);
        c = Over(c, vec3(159.0, 182.0, 216.0) / 255.0, 0.25 * 0.7 * blueCover);
        colour = c;
    }
    else if (mode == 3)
    {
        vec4 t = texture(atlas, texel);
        colour = vec4(t.rgb, t.a * tint.a);
    }
    else if (mode == 5)
    {
        // A PlayStation face button: a dark round button, its shape in its colour (radius: 0 cross, 1 circle, 2 square, 3 triangle)
        vec2 p = (texel - vec2(0.5)) * 2.0;
        float d = length(p);
        float aa = 2.0 / max(fwidth(p.x) * 100.0, 1.0);
        float button = clamp((0.96 - d) / fwidth(d), 0.0, 1.0);
        vec3 c = mix(vec3(0.07, 0.08, 0.12), vec3(0.18, 0.20, 0.26), clamp(0.6 - p.y * 0.5, 0.0, 1.0));
        int shape = int(radius + 0.5);
        float stroke;
        vec3 ink;
        if (shape == 0)
        {
            vec2 q = abs(p);
            stroke = min(abs(q.x - q.y), 1e9) * 0.7071 - 0.09;
            stroke = max(stroke, max(q.x, q.y) - 0.48);
            ink = vec3(0.49, 0.71, 0.95);
        }
        else if (shape == 1)
        {
            stroke = abs(d - 0.42) - 0.09;
            ink = vec3(1.0, 0.40, 0.42);
        }
        else if (shape == 2)
        {
            vec2 q = abs(p);
            stroke = abs(max(q.x, q.y) - 0.38) - 0.08;
            ink = vec3(0.98, 0.55, 0.85);
        }
        else
        {
            vec2 q = vec2(abs(p.x), p.y + 0.08);
            float tri = max(q.x * 0.866 + q.y * 0.5, -q.y) - 0.27;
            stroke = abs(tri) - 0.08;
            ink = vec3(0.30, 0.86, 0.70);
        }

        float inkCover = clamp(-stroke / fwidth(stroke), 0.0, 1.0);
        c = mix(c, ink, inkCover);
        colour = vec4(c, button * tint.a);
    }
    else
    {
        // A fruit drawn when there's no art: an orange ball, its dark edge and a shine
        vec2 p = (texel - vec2(0.5)) * 2.0;
        float d = length(p);
        float edge = clamp((1.0 - d) * 24.0, 0.0, 1.0);
        vec3 orange = mix(vec3(1.0, 0.72, 0.25), vec3(0.85, 0.32, 0.02), clamp(length(p - vec2(-0.35, -0.35)) * 0.8, 0.0, 1.0));
        vec3 c = mix(vec3(0.23, 0.07, 0.0), orange, clamp((0.88 - d) * 24.0, 0.0, 1.0));
        float shine = clamp((0.22 - length(p - vec2(-0.35, -0.4))) * 20.0, 0.0, 1.0);
        c = mix(c, vec3(1.0), shine * 0.8);
        colour = vec4(c, edge * tint.a);
    }
}
)";

struct Vertex
{
    f32 x;
    f32 y;
    f32 u;
    f32 v;
};

struct Atlas
{
    u32 texture = 0;
    s32 size = 0;
    std::vector<stbtt_packedchar> latin;
    std::vector<stbtt_packedchar> punctuation;
    std::vector<stbtt_packedchar> extras;
};

// The codepoints baked: Latin-1's, and the quotes, the ellipsis and the angle quotes
constexpr int LatinFirst = 32;
constexpr int LatinCount = 224;
constexpr int PunctuationFirst = 0x2018;
constexpr int PunctuationCount = 0x203B - 0x2018;
// And Windows-1252's others (the game's texts' codes 0x80 to 0x9F outside those)
int ExtraCodepoints[] = {0x0152, 0x0153, 0x0160, 0x0161, 0x0178, 0x017D, 0x017E, 0x0192, 0x02C6, 0x02DC, 0x20AC, 0x2122};
constexpr int ExtraCount = static_cast<int>(sizeof(ExtraCodepoints) / sizeof(ExtraCodepoints[0]));

// What's made for a backend's recording (the window's, or the shots' own)
struct Renderer
{
    Hw::Gpu* gpu = nullptr;
    bool ready = false;
    bool failed = false;
    u32 program = 0;
    // The images' textures (made the first time each is drawn; 0 none: drawn instead)
    u32 images[static_cast<u32>(OverlayImage::Count)] = {};
    bool imagesTried[static_cast<u32>(OverlayImage::Count)] = {};
    std::map<std::pair<u32, s32>, Atlas> atlases;
    std::map<std::string, s32> uniforms;
    f32 width = 0.0f;
    f32 height = 0.0f;
    f32 scale = 1.0f;
    f32 offsetX = 0.0f;
    f32 offsetY = 0.0f;
};

// The window's, and the offscreen shots'
Renderer g_Window;
Renderer g_Shots;

s32 Uniform(Renderer& r, const char* name)
{
    auto found = r.uniforms.find(name);
    if (found != r.uniforms.end())
    {
        return found->second;
    }

    s32 location = r.gpu->UniformLocation(r.program, name);
    r.uniforms[name] = location;
    return location;
}

bool Prepare(Renderer& r, Hw::Gpu& gpu)
{
    if (r.gpu == &gpu)
    {
        return r.ready;
    }

    // A recording of its own (the window's, or the shots'): everything made there
    r = Renderer();
    r.gpu = &gpu;
    r.program = gpu.StartProgram(VertexSource, FragmentSource, false);
    if (!gpu.FinishProgram(r.program))
    {
        Native::Log("overlay: the program wasn't made (the error output says why)");
        r.failed = true;
        return false;
    }

    r.ready = true;
    return true;
}

void DrawQuads(Renderer& r, const std::vector<Vertex>& vertexes)
{
    if (vertexes.empty())
    {
        return;
    }

    // As the backend's vertexes: the place, the texture's coordinates (s, t; q 1)
    Hw::Gpu& gpu = *r.gpu;
    std::vector<Hw::Vertex>& list = gpu.list().vertexes;
    for (const Vertex& v : vertexes)
    {
        Hw::Vertex made = {};
        made.x = v.x;
        made.y = v.y;
        made.s = v.u;
        made.t = v.v;
        made.q = 1.0f;
        list.push_back(made);
    }

    gpu.VertexData();
    gpu.BindVertexes(true);
    gpu.DrawArrays(Hw::PrimitiveTriangles, 0, static_cast<s32>(vertexes.size()));
}

void AddQuad(std::vector<Vertex>& vertexes, f32 x0, f32 y0, f32 x1, f32 y1, f32 u0 = 0, f32 v0 = 0, f32 u1 = 1, f32 v1 = 1)
{
    vertexes.push_back({x0, y0, u0, v0});
    vertexes.push_back({x1, y0, u1, v0});
    vertexes.push_back({x0, y1, u0, v1});
    vertexes.push_back({x1, y0, u1, v0});
    vertexes.push_back({x1, y1, u1, v1});
    vertexes.push_back({x0, y1, u0, v1});
}

void SetColour(Renderer& r, const char* name, const Rgba32& c)
{
    r.gpu->Uniform4f(Uniform(r, name), c.r, c.g, c.b, c.a);
}

// A rounded box in pixels
void DrawBox(Renderer& r, f32 x, f32 y, f32 w, f32 h, f32 radius, const Rgba32& top, const Rgba32& bottom, f32 border,
             const Rgba32& borderColour, f32 blur)
{
    Hw::Gpu& gpu = *r.gpu;
    gpu.Uniform1i(Uniform(r, "mode"), 0);
    gpu.Uniform4f(Uniform(r, "rect"), x, y, w, h);
    gpu.Uniform1f(Uniform(r, "radius"), std::min(radius, std::min(w, h) * 0.5f));
    SetColour(r, "fillTop", top);
    SetColour(r, "fillBottom", bottom);
    gpu.Uniform1f(Uniform(r, "border"), border);
    SetColour(r, "borderColour", borderColour);
    gpu.Uniform1f(Uniform(r, "blur"), blur);
    f32 grow = blur * 2.0f + 1.0f;
    std::vector<Vertex> quad;
    AddQuad(quad, x - grow, y - grow, x + w + grow, y + h + grow);
    DrawQuads(r, quad);
}

// A texture of the bound unit's made from rows of FORMAT (one level, linear, its edges clamped)
u32 MakeTexture(Hw::Gpu& gpu, Hw::Format format, s32 width, s32 height, const void* pixels, s32 alignment)
{
    u32 texture = 0;
    gpu.GenTextures(1, &texture);
    gpu.BindTexture(texture);
    gpu.PixelStore(Hw::StoreUnpackAlignment, alignment);
    gpu.PixelStore(Hw::StoreUnpackRowLength, 0);
    gpu.TexParameter(Hw::ParamMaxLevel, 0);
    gpu.TexImage(0, format, width, height, pixels);
    gpu.TexParameter(Hw::ParamMinFilter, static_cast<s32>(Hw::FilterLinear));
    gpu.TexParameter(Hw::ParamMagFilter, static_cast<s32>(Hw::FilterLinear));
    gpu.TexParameter(Hw::ParamWrapS, static_cast<s32>(Hw::WrapClamp));
    gpu.TexParameter(Hw::ParamWrapT, static_cast<s32>(Hw::WrapClamp));
    return texture;
}

Atlas& AtlasFor(Renderer& r, OverlayFont face, s32 pixelSize)
{
    Atlas& atlas = r.atlases[{static_cast<u32>(face), pixelSize}];
    if (atlas.texture != 0)
    {
        return atlas;
    }

    const stbtt_fontinfo* info = OverlayFontInfo(face);
    if (info == nullptr)
    {
        return atlas;
    }

    for (s32 size = 512; size <= 4096; size *= 2)
    {
        std::vector<unsigned char> pixels(static_cast<size_t>(size) * size);
        stbtt_pack_context pack;
        if (!stbtt_PackBegin(&pack, pixels.data(), size, size, 0, 1, nullptr))
        {
            continue;
        }

        stbtt_PackSetOversampling(&pack, 2, 2);
        atlas.latin.assign(LatinCount, {});
        atlas.punctuation.assign(PunctuationCount, {});
        atlas.extras.assign(ExtraCount, {});
        stbtt_pack_range ranges[3] = {};
        f32 scale = stbtt_ScaleForMappingEmToPixels(info, static_cast<f32>(pixelSize));
        int ascent;
        int descent;
        int gap;
        stbtt_GetFontVMetrics(info, &ascent, &descent, &gap);
        // PackFontRanges takes a pixel height (ascent to descent): the em's size given as that
        f32 height = scale * static_cast<f32>(ascent - descent);
        ranges[0] = {height, LatinFirst, nullptr, LatinCount, atlas.latin.data(), 0, 0};
        ranges[1] = {height, PunctuationFirst, nullptr, PunctuationCount, atlas.punctuation.data(), 0, 0};
        ranges[2] = {height, 0, ExtraCodepoints, ExtraCount, atlas.extras.data(), 0, 0};
        const unsigned char* data = info->data;
        bool packed = stbtt_PackFontRanges(&pack, data, info->fontstart, ranges, 3) != 0;
        stbtt_PackEnd(&pack);
        if (!packed)
        {
            continue;
        }

        atlas.texture = MakeTexture(*r.gpu, Hw::FormatR8, size, size, pixels.data(), 1);
        atlas.size = size;
        break;
    }

    return atlas;
}

// A run of text's glyphs in pixels at a place (its left, its baseline), drawn in a colour
void DrawGlyphs(Renderer& r, const std::u32string& text, Atlas& atlas, f32 left, f32 baseline, f32 letterSpacing, const Rgba32& colour)
{
    if (atlas.texture == 0)
    {
        return;
    }

    std::vector<Vertex> vertexes;
    f32 x = left;
    f32 y = baseline;
    for (char32_t c : text)
    {
        const stbtt_packedchar* chars = nullptr;
        int index = 0;
        if (c >= static_cast<char32_t>(LatinFirst) && c < static_cast<char32_t>(LatinFirst + LatinCount))
        {
            chars = atlas.latin.data();
            index = static_cast<int>(c) - LatinFirst;
        }
        else if (c >= static_cast<char32_t>(PunctuationFirst) && c < static_cast<char32_t>(PunctuationFirst + PunctuationCount))
        {
            chars = atlas.punctuation.data();
            index = static_cast<int>(c) - PunctuationFirst;
        }
        else
        {
            int* end = ExtraCodepoints + ExtraCount;
            int* found = std::find(ExtraCodepoints, end, static_cast<int>(c));
            if (found == end)
            {
                continue;
            }
            chars = atlas.extras.data();
            index = static_cast<int>(found - ExtraCodepoints);
        }

        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(chars, atlas.size, atlas.size, index, &x, &y, &q, 0);
        AddQuad(vertexes, q.x0, q.y0, q.x1, q.y1, q.s0, q.t0, q.s1, q.t1);
        x += letterSpacing;
    }

    Hw::Gpu& gpu = *r.gpu;
    gpu.Uniform1i(Uniform(r, "mode"), 1);
    SetColour(r, "tint", colour);
    gpu.ActiveTexture(0);
    gpu.BindTexture(atlas.texture);
    gpu.Uniform1i(Uniform(r, "atlas"), 0);
    DrawQuads(r, vertexes);
}

void DrawText(Renderer& r, const OverlayFrame::Command& command)
{
    const TextStyle& style = command.textStyle;
    f32 pixelSize = style.size * r.scale;
    Atlas& atlas = AtlasFor(r, style.font, static_cast<s32>(std::lround(pixelSize)));
    f32 ascent;
    f32 descent;
    f32 gap;
    OverlayFontMetrics(style.font, style.size, &ascent, &descent, &gap);
    // The baseline from the line's middle (a CSS line box of the normal height)
    f32 left = r.offsetX + command.x * r.scale;
    f32 baseline = r.offsetY + (command.y + (ascent + descent) * 0.5f) * r.scale;
    f32 spacing = style.letterSpacing * r.scale;
    auto layer = [&](f32 dx, f32 dy, const Rgba32& colour, bool outlined)
    {
        if (outlined && style.outline > 0.0f)
        {
            // The outline's visible half all round, then the fill over it
            f32 o = style.outline * 0.5f * r.scale;
            for (s32 i = 0; i < 12; i++)
            {
                f32 a = static_cast<f32>(i) * 6.28318530718f / 12.0f;
                DrawGlyphs(r, command.text, atlas, left + dx + std::cos(a) * o, baseline + dy + std::sin(a) * o, spacing,
                           outlined && dx == 0.0f && dy == 0.0f ? style.outlineColour : colour);
            }
        }

        DrawGlyphs(r, command.text, atlas, left + dx, baseline + dy, spacing, colour);
    };

    if (style.dropColour.a > 0.0f && (style.dropX != 0.0f || style.dropY != 0.0f))
    {
        layer(style.dropX * r.scale, style.dropY * r.scale, style.dropColour, true);
    }

    layer(0.0f, 0.0f, style.colour, true);
}

void DrawBoxCommand(Renderer& r, const OverlayFrame::Command& command)
{
    const BoxStyle& s = command.box;
    f32 x = r.offsetX + command.x * r.scale;
    f32 y = r.offsetY + command.y * r.scale;
    f32 w = command.width * r.scale;
    f32 h = command.height * r.scale;
    f32 radius = s.radius * r.scale;
    Rgba32 none = {0, 0, 0, -1};
    if (s.shadowColour.a > 0.0f)
    {
        f32 blur = s.shadowBlur * 0.5f * r.scale;
        DrawBox(r, x, y + s.shadowOffset * r.scale, w, h, radius, s.shadowColour, none, 0.0f, none, std::max(blur, 0.5f));
    }

    if (s.lip > 0.0f && s.lipColour.a > 0.0f)
    {
        DrawBox(r, x, y + s.lip * r.scale, w, h, radius, s.lipColour, none, 0.0f, none, 0.0f);
    }

    DrawBox(r, x, y, w, h, radius, s.fill, s.fillBottom, s.border * r.scale, s.borderColour, 0.0f);
}

u32 ImageTexture(Renderer& r, OverlayImage image)
{
    u32 index = static_cast<u32>(image);
    if (r.imagesTried[index])
    {
        return r.images[index];
    }

    r.imagesTried[index] = true;
    std::vector<u32> pixels;
    s32 width = 0;
    s32 height = 0;
    if (!ImageArt(image, &pixels, &width, &height))
    {
        return 0;
    }

    r.images[index] = MakeTexture(*r.gpu, Hw::FormatRgba8, width, height, pixels.data(), 4);
    return r.images[index];
}

void DrawImage(Renderer& r, const OverlayFrame::Command& command)
{
    Hw::Gpu& gpu = *r.gpu;
    u32 texture = ImageTexture(r, command.image);
    f32 x = r.offsetX + command.x * r.scale;
    f32 y = r.offsetY + command.y * r.scale;
    std::vector<Vertex> quad;
    AddQuad(quad, x, y, x + command.width * r.scale, y + command.height * r.scale);
    s32 mode = 3;
    if (texture == 0)
    {
        // Drawn: the fruit, or a PlayStation button's shape (5 + its shape)
        u32 shape = static_cast<u32>(command.image) - static_cast<u32>(OverlayImage::PlayStationCross);
        mode = command.image == OverlayImage::Cursor ? 4 : shape < 4 ? 5 : 4;
        gpu.Uniform1f(Uniform(r, "radius"), static_cast<f32>(shape));
    }

    gpu.Uniform1i(Uniform(r, "mode"), mode);
    SetColour(r, "tint", {1, 1, 1, command.alpha});
    gpu.ActiveTexture(0);
    gpu.BindTexture(texture);
    gpu.Uniform1i(Uniform(r, "atlas"), 0);
    DrawQuads(r, quad);
}

// A frame's commands drawn by a backend's recording on its bound framebuffer, of a size
void DrawFrame(Renderer& r, Hw::Gpu& gpu, const std::vector<OverlayFrame::Command>& commands, int pixelWidth, int pixelHeight)
{
    if (commands.empty() || !Prepare(r, gpu))
    {
        return;
    }

    r.width = static_cast<f32>(pixelWidth);
    r.height = static_cast<f32>(pixelHeight);
    ReferenceMapping(r.width, r.height, &r.scale, &r.offsetX, &r.offsetY);
    gpu.Viewport(0, 0, pixelWidth, pixelHeight);
    gpu.Enable(Hw::CapBlend);
    gpu.BlendFuncSeparate(Hw::FactorSrcAlpha, Hw::FactorOneMinusSrcAlpha, Hw::FactorOne, Hw::FactorOneMinusSrcAlpha);
    gpu.BlendEquationSeparate(Hw::BlendAdd, Hw::BlendAdd);
    gpu.Disable(Hw::CapScissorTest);
    gpu.UseProgram(r.program);
    gpu.Uniform2f(Uniform(r, "viewport"), r.width, r.height);
    for (const OverlayFrame::Command& command : commands)
    {
        switch (command.kind)
        {
        case CommandKind::Backdrop:
        {
            gpu.Uniform1i(Uniform(r, "mode"), 2);
            gpu.Uniform1f(Uniform(r, "scale"), r.scale);
            gpu.Uniform2f(Uniform(r, "offset"), r.offsetX, r.offsetY);
            gpu.Uniform1f(Uniform(r, "time"), command.seconds);
            std::vector<Vertex> quad;
            AddQuad(quad, 0.0f, 0.0f, r.width, r.height);
            DrawQuads(r, quad);
            break;
        }
        case CommandKind::Box:
            DrawBoxCommand(r, command);
            break;
        case CommandKind::Text:
            DrawText(r, command);
            break;
        case CommandKind::Image:
            DrawImage(r, command);
            break;
        case CommandKind::Clip:
        {
            f32 x = r.offsetX + command.x * r.scale;
            f32 y = r.offsetY + command.y * r.scale;
            f32 w = command.width * r.scale;
            f32 h = command.height * r.scale;
            gpu.Enable(Hw::CapScissorTest);
            gpu.Scissor(static_cast<s32>(x), static_cast<s32>(r.height - y - h), static_cast<s32>(w), static_cast<s32>(h));
            break;
        }
        case CommandKind::NoClip:
            gpu.Disable(Hw::CapScissorTest);
            break;
        }
    }

    gpu.Disable(Hw::CapScissorTest);
    gpu.Disable(Hw::CapBlend);
}

// The presenter's hook: the last frame handed over, over the game's picture, and the FPS counter (the picture counted)
void OverlayHook(int pixelWidth, int pixelHeight, float pixelScale)
{
    NoteWindow(pixelWidth, pixelHeight, pixelScale);
    CountShownFrame(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
    std::vector<OverlayFrame::Command> commands = SubmittedFrame();
    AddFpsCounter(&commands, pixelWidth, pixelHeight);
    DrawFrame(g_Window, NativeGraphicsWindowGpu(), commands, pixelWidth, pixelHeight);
}
}

void StartOverlay()
{
    NativeGraphicsSetOverlay(OverlayHook);
}

bool RenderFrameOffscreen(u32 width, u32 height, std::vector<u32>* pixels)
{
    // A backend of its own (the renderer's API: a Vulkan device of its own, or OpenGL in a hidden window that's never shown) and a
    // framebuffer of the size
    // (Kept to the end: the exit quits SDL before static objects go)
    static Hw::Backend* backend = nullptr;
    static Hw::Gpu gpu;
    static bool tried = false;
    if (!tried)
    {
        tried = true;
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            return false;
        }

        backend = NativeGraphics::RendererOnVulkan() ? Hw::StartVulkanBackend(true) : nullptr;
        if (backend == nullptr)
        {
            backend = Hw::StartGlWindowBackend(nullptr, nullptr);
        }

        if (backend == nullptr)
        {
            Native::Log("overlay: no graphics for the shots: %s", SDL_GetError());
            return false;
        }

        gpu.Start(backend);
    }

    if (backend == nullptr)
    {
        return false;
    }

    backend->MakeCurrent();
    if (!Prepare(g_Shots, gpu))
    {
        return false;
    }

    // (A target for each size, kept: the shots come at two)
    static std::map<std::pair<u32, u32>, std::pair<u32, u32>> targets;
    auto& [texture, framebuffer] = targets[{width, height}];
    if (texture == 0)
    {
        gpu.GenTextures(1, &texture);
        gpu.BindTexture(texture);
        gpu.TexParameter(Hw::ParamMaxLevel, 0);
        gpu.TexImage(0, Hw::FormatRgba8, static_cast<s32>(width), static_cast<s32>(height), nullptr);
        gpu.GenFramebuffers(1, &framebuffer);
        gpu.BindFramebuffer(framebuffer);
        gpu.AttachColour(texture);
    }

    gpu.BindFramebuffer(framebuffer);
    gpu.Disable(Hw::CapBlend);
    gpu.Disable(Hw::CapScissorTest);
    gpu.ColorMask(true, true, true, true);
    gpu.Viewport(0, 0, static_cast<s32>(width), static_cast<s32>(height));
    gpu.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gpu.Clear(Hw::ClearColour);
    // The frame recorded last (the game's thread is this one in headless runs)
    NoteWindow(static_cast<int>(width), static_cast<int>(height), 1.0f);
    DrawFrame(g_Shots, gpu, RecordingFrame().Commands(), static_cast<int>(width), static_cast<int>(height));
    std::vector<u32> rows(static_cast<size_t>(width) * height);
    gpu.BindReadFramebuffer(framebuffer);
    gpu.PixelStore(Hw::StorePackAlignment, 4);
    gpu.PixelStore(Hw::StorePackRowLength, 0);
    gpu.ReadPixels(0, 0, static_cast<s32>(width), static_cast<s32>(height), Hw::FormatRgba8, rows.data());
    pixels->resize(rows.size());
    for (u32 y = 0; y < height; y++)
    {
        std::memcpy(&(*pixels)[static_cast<size_t>(y) * width], &rows[static_cast<size_t>(height - 1 - y) * width], width * 4);
    }

    for (u32& pixel : *pixels)
    {
        pixel |= 0xFF000000u;
    }

    gpu.BindFramebuffer(0);
    gpu.Submit();
    return true;
}
}
