// The overlay's drawing (OpenGL 3.3 core, on the presenter's thread through graphics/presenter.h's hook, or offscreen for the
// shots): the frame's commands in the 1280 x 720 reference space scaled to the window. One program: rounded boxes as a signed
// distance (a vertical gradient, a border, a soft edge for shadows), glyphs from atlases baked per font and pixel size
// (stb_truetype), the options' backdrop worked out per pixel (its gradient, the ring target, the Twinsanity spiral of
// native/design/options/spiral_grey.svg: two Archimedean arms r = 10 + 35.45 theta over 2.2 turns), images
#include "ui/overlayinternal.h"
#include "ui/nativeui.h"

#include "graphics/presenter.h"
#include "native.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <map>
#include <tuple>

#include "../../../../native/third_party/stb/stb_truetype.h"

namespace NativeUi
{
namespace
{
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLchar = char;
using GLbitfield = unsigned int;
using GLfloat = float;
// The pointer's width (long is 32 bits on Windows)
using GLsizeiptr = std::ptrdiff_t;

constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_ONE = 1;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_RED = 0x1903;
constexpr GLenum GL_R8 = 0x8229;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_STREAM_DRAW = 0x88E0;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum GL_PACK_ALIGNMENT = 0x0D05;

struct Gl
{
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void (*Scissor)(GLint, GLint, GLsizei, GLsizei);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*GenTextures)(GLsizei, GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*ActiveTexture)(GLenum);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*PixelStorei)(GLenum, GLint);
    void (*GenBuffers)(GLsizei, GLuint*);
    void (*BindBuffer)(GLenum, GLuint);
    void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    void (*GenVertexArrays)(GLsizei, GLuint*);
    void (*BindVertexArray)(GLuint);
    void (*EnableVertexAttribArray)(GLuint);
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (*CompileShader)(GLuint);
    void (*GetShaderiv)(GLuint, GLenum, GLint*);
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    GLuint (*CreateProgram)();
    void (*AttachShader)(GLuint, GLuint);
    void (*BindAttribLocation)(GLuint, GLuint, const GLchar*);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint, GLenum, GLint*);
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (*UseProgram)(GLuint);
    GLint (*GetUniformLocation)(GLuint, const GLchar*);
    void (*Uniform1i)(GLint, GLint);
    void (*Uniform1f)(GLint, GLfloat);
    void (*Uniform2f)(GLint, GLfloat, GLfloat);
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    void (*GenFramebuffers)(GLsizei, GLuint*);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Clear)(GLbitfield);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void (*DeleteTextures)(GLsizei, const GLuint*);
    void (*DeleteFramebuffers)(GLsizei, const GLuint*);
};

template <typename T> void Load(T& function, const char* name)
{
    function = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
}

bool LoadGl(Gl& gl)
{
    Load(gl.Enable, "glEnable");
    Load(gl.Disable, "glDisable");
    Load(gl.BlendFuncSeparate, "glBlendFuncSeparate");
    Load(gl.Scissor, "glScissor");
    Load(gl.Viewport, "glViewport");
    Load(gl.GenTextures, "glGenTextures");
    Load(gl.BindTexture, "glBindTexture");
    Load(gl.ActiveTexture, "glActiveTexture");
    Load(gl.TexParameteri, "glTexParameteri");
    Load(gl.TexImage2D, "glTexImage2D");
    Load(gl.PixelStorei, "glPixelStorei");
    Load(gl.GenBuffers, "glGenBuffers");
    Load(gl.BindBuffer, "glBindBuffer");
    Load(gl.BufferData, "glBufferData");
    Load(gl.GenVertexArrays, "glGenVertexArrays");
    Load(gl.BindVertexArray, "glBindVertexArray");
    Load(gl.EnableVertexAttribArray, "glEnableVertexAttribArray");
    Load(gl.VertexAttribPointer, "glVertexAttribPointer");
    Load(gl.CreateShader, "glCreateShader");
    Load(gl.ShaderSource, "glShaderSource");
    Load(gl.CompileShader, "glCompileShader");
    Load(gl.GetShaderiv, "glGetShaderiv");
    Load(gl.GetShaderInfoLog, "glGetShaderInfoLog");
    Load(gl.CreateProgram, "glCreateProgram");
    Load(gl.AttachShader, "glAttachShader");
    Load(gl.BindAttribLocation, "glBindAttribLocation");
    Load(gl.LinkProgram, "glLinkProgram");
    Load(gl.GetProgramiv, "glGetProgramiv");
    Load(gl.GetProgramInfoLog, "glGetProgramInfoLog");
    Load(gl.UseProgram, "glUseProgram");
    Load(gl.GetUniformLocation, "glGetUniformLocation");
    Load(gl.Uniform1i, "glUniform1i");
    Load(gl.Uniform1f, "glUniform1f");
    Load(gl.Uniform2f, "glUniform2f");
    Load(gl.Uniform4f, "glUniform4f");
    Load(gl.DrawArrays, "glDrawArrays");
    Load(gl.GenFramebuffers, "glGenFramebuffers");
    Load(gl.BindFramebuffer, "glBindFramebuffer");
    Load(gl.FramebufferTexture2D, "glFramebufferTexture2D");
    Load(gl.ClearColor, "glClearColor");
    Load(gl.Clear, "glClear");
    Load(gl.ReadPixels, "glReadPixels");
    Load(gl.DeleteTextures, "glDeleteTextures");
    Load(gl.DeleteFramebuffers, "glDeleteFramebuffers");
    return gl.Enable != nullptr && gl.CreateProgram != nullptr && gl.DrawArrays != nullptr && gl.GenVertexArrays != nullptr;
}

constexpr const char* VertexSource = R"(#version 330 core
in vec2 position;
in vec2 uv;
uniform vec2 viewport;
out vec2 pixel;
out vec2 texel;
void main()
{
    pixel = position;
    texel = uv;
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
    GLuint texture = 0;
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

struct Renderer
{
    SDL_GLContext context = nullptr;
    bool ready = false;
    bool failed = false;
    Gl gl{};
    GLuint program = 0;
    GLuint vertexArray = 0;
    GLuint buffer = 0;
    // The images' textures (made the first time each is drawn; 0 none: drawn instead)
    GLuint images[static_cast<u32>(OverlayImage::Count)] = {};
    bool imagesTried[static_cast<u32>(OverlayImage::Count)] = {};
    std::map<std::pair<u32, s32>, Atlas> atlases;
    std::map<std::string, GLint> uniforms;
    f32 width = 0.0f;
    f32 height = 0.0f;
    f32 scale = 1.0f;
    f32 offsetX = 0.0f;
    f32 offsetY = 0.0f;
};

Renderer g_Renderer;

GLint Uniform(Renderer& r, const char* name)
{
    auto found = r.uniforms.find(name);
    if (found != r.uniforms.end())
    {
        return found->second;
    }

    GLint location = r.gl.GetUniformLocation(r.program, name);
    r.uniforms[name] = location;
    return location;
}

GLuint Compile(Gl& gl, GLenum kind, const char* source)
{
    GLuint shader = gl.CreateShader(kind);
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[2048] = {};
        gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        Native::Log("overlay: a shader didn't compile: %s", log);
        return 0;
    }

    return shader;
}

bool Prepare(Renderer& r)
{
    SDL_GLContext context = SDL_GL_GetCurrentContext();
    if (r.ready && r.context == context)
    {
        return true;
    }

    if (r.failed && r.context == context)
    {
        return false;
    }

    // A context of its own (the presenter's, or the shots'): everything made again
    r = Renderer();
    r.context = context;
    if (!LoadGl(r.gl))
    {
        r.failed = true;
        return false;
    }

    Gl& gl = r.gl;
    GLuint vertex = Compile(gl, GL_VERTEX_SHADER, VertexSource);
    GLuint fragment = Compile(gl, GL_FRAGMENT_SHADER, FragmentSource);
    if (vertex == 0 || fragment == 0)
    {
        r.failed = true;
        return false;
    }

    r.program = gl.CreateProgram();
    gl.AttachShader(r.program, vertex);
    gl.AttachShader(r.program, fragment);
    gl.BindAttribLocation(r.program, 0, "position");
    gl.BindAttribLocation(r.program, 1, "uv");
    gl.LinkProgram(r.program);
    GLint linked = 0;
    gl.GetProgramiv(r.program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[2048] = {};
        gl.GetProgramInfoLog(r.program, sizeof(log), nullptr, log);
        Native::Log("overlay: the program didn't link: %s", log);
        r.failed = true;
        return false;
    }

    gl.GenVertexArrays(1, &r.vertexArray);
    gl.BindVertexArray(r.vertexArray);
    gl.GenBuffers(1, &r.buffer);
    gl.BindBuffer(GL_ARRAY_BUFFER, r.buffer);
    gl.EnableVertexAttribArray(0);
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(Vertex), reinterpret_cast<const void*>(0));
    gl.VertexAttribPointer(1, 2, GL_FLOAT, 0, sizeof(Vertex), reinterpret_cast<const void*>(8));
    r.ready = true;
    return true;
}

void DrawQuads(Renderer& r, const std::vector<Vertex>& vertexes)
{
    if (vertexes.empty())
    {
        return;
    }

    r.gl.BindBuffer(GL_ARRAY_BUFFER, r.buffer);
    r.gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexes.size() * sizeof(Vertex)), vertexes.data(), GL_STREAM_DRAW);
    r.gl.DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexes.size()));
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
    r.gl.Uniform4f(Uniform(r, name), c.r, c.g, c.b, c.a);
}

// A rounded box in pixels
void DrawBox(Renderer& r, f32 x, f32 y, f32 w, f32 h, f32 radius, const Rgba32& top, const Rgba32& bottom, f32 border,
             const Rgba32& borderColour, f32 blur)
{
    Gl& gl = r.gl;
    gl.Uniform1i(Uniform(r, "mode"), 0);
    gl.Uniform4f(Uniform(r, "rect"), x, y, w, h);
    gl.Uniform1f(Uniform(r, "radius"), std::min(radius, std::min(w, h) * 0.5f));
    SetColour(r, "fillTop", top);
    SetColour(r, "fillBottom", bottom);
    gl.Uniform1f(Uniform(r, "border"), border);
    SetColour(r, "borderColour", borderColour);
    gl.Uniform1f(Uniform(r, "blur"), blur);
    f32 grow = blur * 2.0f + 1.0f;
    std::vector<Vertex> quad;
    AddQuad(quad, x - grow, y - grow, x + w + grow, y + h + grow);
    DrawQuads(r, quad);
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

        Gl& gl = r.gl;
        gl.GenTextures(1, &atlas.texture);
        gl.BindTexture(GL_TEXTURE_2D, atlas.texture);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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

    Gl& gl = r.gl;
    gl.Uniform1i(Uniform(r, "mode"), 1);
    SetColour(r, "tint", colour);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, atlas.texture);
    gl.Uniform1i(Uniform(r, "atlas"), 0);
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

GLuint ImageTexture(Renderer& r, OverlayImage image)
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

    Gl& gl = r.gl;
    gl.GenTextures(1, &r.images[index]);
    gl.BindTexture(GL_TEXTURE_2D, r.images[index]);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return r.images[index];
}

void DrawImage(Renderer& r, const OverlayFrame::Command& command)
{
    Gl& gl = r.gl;
    GLuint texture = ImageTexture(r, command.image);
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
        gl.Uniform1f(Uniform(r, "radius"), static_cast<f32>(shape));
    }

    gl.Uniform1i(Uniform(r, "mode"), mode);
    SetColour(r, "tint", {1, 1, 1, command.alpha});
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, texture);
    gl.Uniform1i(Uniform(r, "atlas"), 0);
    DrawQuads(r, quad);
}

// A frame's commands drawn on the current context's framebuffer of a size
void DrawFrame(const std::vector<OverlayFrame::Command>& commands, int pixelWidth, int pixelHeight)
{
    Renderer& r = g_Renderer;
    if (commands.empty() || !Prepare(r))
    {
        return;
    }

    Gl& gl = r.gl;
    r.width = static_cast<f32>(pixelWidth);
    r.height = static_cast<f32>(pixelHeight);
    ReferenceMapping(r.width, r.height, &r.scale, &r.offsetX, &r.offsetY);
    gl.Viewport(0, 0, pixelWidth, pixelHeight);
    gl.Enable(GL_BLEND);
    gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl.Disable(GL_SCISSOR_TEST);
    gl.UseProgram(r.program);
    gl.BindVertexArray(r.vertexArray);
    gl.Uniform2f(Uniform(r, "viewport"), r.width, r.height);
    for (const OverlayFrame::Command& command : commands)
    {
        switch (command.kind)
        {
        case CommandKind::Backdrop:
        {
            gl.Uniform1i(Uniform(r, "mode"), 2);
            gl.Uniform1f(Uniform(r, "scale"), r.scale);
            gl.Uniform2f(Uniform(r, "offset"), r.offsetX, r.offsetY);
            gl.Uniform1f(Uniform(r, "time"), command.seconds);
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
            gl.Enable(GL_SCISSOR_TEST);
            gl.Scissor(static_cast<GLint>(x), static_cast<GLint>(r.height - y - h), static_cast<GLsizei>(w), static_cast<GLsizei>(h));
            break;
        }
        case CommandKind::NoClip:
            gl.Disable(GL_SCISSOR_TEST);
            break;
        }
    }

    gl.Disable(GL_SCISSOR_TEST);
}

// The presenter's hook: the last frame handed over, over the game's picture, and the FPS counter (the picture counted)
void OverlayHook(int pixelWidth, int pixelHeight, float pixelScale)
{
    NoteWindow(pixelWidth, pixelHeight, pixelScale);
    CountShownFrame(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
    std::vector<OverlayFrame::Command> commands = SubmittedFrame();
    AddFpsCounter(&commands, pixelWidth, pixelHeight);
    DrawFrame(commands, pixelWidth, pixelHeight);
}
}

void StartOverlay()
{
    NativeGraphicsSetOverlay(OverlayHook);
}

bool RenderFrameOffscreen(u32 width, u32 height, std::vector<u32>* pixels)
{
    // A hidden window's context and a framebuffer of the size (the window is never shown)
    static SDL_Window* window = nullptr;
    static SDL_GLContext context = nullptr;
    if (window == nullptr)
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            return false;
        }

        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        window = SDL_CreateWindow("overlay", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (window == nullptr)
        {
            Native::Log("overlay: no hidden window for the shots: %s", SDL_GetError());
            return false;
        }

        context = SDL_GL_CreateContext(window);
        if (context == nullptr)
        {
            Native::Log("overlay: no GL context for the shots: %s", SDL_GetError());
            return false;
        }
    }

    SDL_GL_MakeCurrent(window, context);
    if (!Prepare(g_Renderer))
    {
        return false;
    }

    Gl& gl = g_Renderer.gl;
    GLuint texture = 0;
    GLuint framebuffer = 0;
    gl.GenTextures(1, &texture);
    gl.BindTexture(GL_TEXTURE_2D, texture);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                  nullptr);
    gl.GenFramebuffers(1, &framebuffer);
    gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    gl.Viewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    // The frame recorded last (the game's thread is this one in headless runs)
    NoteWindow(static_cast<int>(width), static_cast<int>(height), 1.0f);
    DrawFrame(RecordingFrame().Commands(), static_cast<int>(width), static_cast<int>(height));
    std::vector<u32> rows(static_cast<size_t>(width) * height);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl.ReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
    pixels->resize(rows.size());
    for (u32 y = 0; y < height; y++)
    {
        std::memcpy(&(*pixels)[static_cast<size_t>(y) * width], &rows[static_cast<size_t>(height - 1 - y) * width], width * 4);
    }

    for (u32& pixel : *pixels)
    {
        pixel |= 0xFF000000u;
    }

    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.DeleteFramebuffers(1, &framebuffer);
    gl.DeleteTextures(1, &texture);
    return true;
}
}
