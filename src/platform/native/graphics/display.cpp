#include "display.h"

#include "capture.h"

#include "hardware.h"
#include "renderthread.h"
#include "ee/vu.h"
#include "ee/vu1translate.h"
#include "png.h"
#include "presenter.h"
#include "settings.h"
#include "hw/hwgs.h"
#include "hw/replace.h"
#include "resolution.h"
#include "window.h"
#include "gs/gs.h"

#include "game/renderer.h"
#include "fixes.h"
#include "native.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The window's OpenGL 3.3 core presenter: the PCRTC's picture is a texture drawn over the window at the TV's shape (4:3, or 16:9
// when the game's screen setting says the TV is wide), letterboxed. Its scaling is linear (the TV's analogue picture has no
// pixels of its own) unless TWIN_DISPLAY_FILTER=nearest. The GL entry points are loaded through SDL, so nothing here is
// macOS's or Linux's own

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
using GLsync = void*;

constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum GL_TRIANGLE_STRIP = 0x0005;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_TEXTURE0 = 0x84C0;

struct Gl
{
    void (*GenTextures)(GLsizei, GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Clear)(GLbitfield);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (*CompileShader)(GLuint);
    void (*GetShaderiv)(GLuint, GLenum, GLint*);
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    GLuint (*CreateProgram)();
    void (*AttachShader)(GLuint, GLuint);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint, GLenum, GLint*);
    void (*UseProgram)(GLuint);
    GLint (*GetUniformLocation)(GLuint, const GLchar*);
    void (*Uniform1i)(GLint, GLint);
    void (*GenVertexArrays)(GLsizei, GLuint*);
    void (*BindVertexArray)(GLuint);
    void (*ActiveTexture)(GLenum);
    void (*Uniform2f)(GLint, GLfloat, GLfloat);
    void (*Uniform1f)(GLint, GLfloat);
    void (*DeleteProgram)(GLuint);
    void (*DeleteShader)(GLuint);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    // The render thread's pictures: their fences
    void (*Flush)();
    GLsync (*FenceSync)(GLenum, GLbitfield);
    void (*WaitSync)(GLsync, GLbitfield, unsigned long long);
    void (*DeleteSync)(GLsync);
    // The window's shots (NativeGraphicsShootWindow)
    void (*PixelStorei)(GLenum, GLint);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
};

struct Presenter
{
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    Gl gl = {};
    GLuint texture = 0;
    GLuint program = 0;
    GLuint vertexArray = 0;
    bool linear = true;
    bool filterChanged = false;
    // The post filter asked for and the one made (0 none), its program
    int postFilter = 0;
    int madePostFilter = 0;
    GLuint postProgram = 0;
    // FXAA's program (made when it's first wanted), anti-aliasing (0 off, 1 FXAA), V-sync
    GLuint fxaaProgram = 0;
    int antiAliasing = 0;
    bool vsync = false;
    bool vsyncSet = false;
    // The picture's place in the window's pixels
    s32 viewX = 0;
    s32 viewY = 0;
    s32 viewWidth = 0;
    s32 viewHeight = 0;
};

Presenter g_Presenter;
void (*g_Overlay)(int, int, float) = nullptr;
// The window shot asked for (NativeGraphicsShootWindow): its file, written at the next picture shown
std::mutex g_WindowShotLock;
std::string g_WindowShotPath;
NativeGraphics::Picture g_Picture;
bool g_Pal = true;
bool g_Paced = true;
std::chrono::steady_clock::time_point g_NextBlank;
bool g_BlankStarted = false;
// The frame rate option's (0: the video mode's, -1: as fast as they come)
s32 g_FrameRate = 0;

template <typename T>
void Load(T& function, const char* name)
{
    function = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    if (function == nullptr)
    {
        std::fprintf(stderr, "graphics: OpenGL has no %s\n", name);
        std::abort();
    }
}

GLuint Compile(Gl& gl, GLenum kind, const char* source, bool required = true)
{
    GLuint shader = gl.CreateShader(kind);
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "graphics: shader: %s\n", log);
        if (required)
        {
            std::abort();
        }

        gl.DeleteShader(shader);
        return 0;
    }

    return shader;
}

// A quad over the viewport from the vertex number alone, the picture's rows top to bottom
constexpr const char* VertexSource = R"(#version 330 core
out vec2 place;
uniform vec4 uvRect;
void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    place = mix(uvRect.xy, uvRect.zw, vec2(corner.x, 1.0 - corner.y));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* FragmentSource = R"(#version 330 core
in vec2 place;
out vec4 colour;
uniform sampler2D picture;
void main()
{
    colour = vec4(texture(picture, place).rgb, 1.0);
}
)";

// The picture's place in a window of these pixels: the TV's shape, letterboxed or pillarboxed
void ViewRect(s32 width, s32 height, s32* x, s32* y, s32* viewWidth, s32* viewHeight)
{
    // A widescreen TV is 16:9, or 21:9 with the ultrawide option (the game projects for it); the frame buffers are the PS2's
    // either way (an anamorphic picture), only its shape on the window changes
    f32 aspect = g_WidescreenTv != 0 ? NativeFixes::WideTvAspect() : 4.0f / 3.0f;
    *viewWidth = width;
    *viewHeight = static_cast<s32>(static_cast<f32>(width) / aspect);
    if (*viewHeight > height)
    {
        *viewHeight = height;
        *viewWidth = static_cast<s32>(static_cast<f32>(height) * aspect);
    }

    *x = (width - *viewWidth) / 2;
    *y = (height - *viewHeight) / 2;
}

// The post filter's program made when it's asked for (or dropped)
void MakePostFilter(Presenter& p)
{
    Gl& gl = p.gl;
    if (p.postProgram != 0)
    {
        gl.DeleteProgram(p.postProgram);
        p.postProgram = 0;
    }

    p.madePostFilter = p.postFilter;
    const char* source = p.postFilter == 1 ? NativeUiCrtShaderSource() : nullptr;
    if (source == nullptr)
    {
        return;
    }

    GLuint vertex = Compile(gl, GL_VERTEX_SHADER, VertexSource);
    GLuint fragment = Compile(gl, GL_FRAGMENT_SHADER, source, false);
    if (fragment == 0)
    {
        gl.DeleteShader(vertex);
        return;
    }

    GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vertex);
    gl.AttachShader(program, fragment);
    gl.LinkProgram(program);
    gl.DeleteShader(vertex);
    gl.DeleteShader(fragment);
    GLint linked = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        std::fprintf(stderr, "graphics: the post filter's program doesn't link\n");
        gl.DeleteProgram(program);
        return;
    }

    p.postProgram = program;
}

// FXAA (a short version of Timothy Lottes' FXAA 3 console algorithm): edges found by luma contrast with their neighbours and
// blended along them, on the shown picture
constexpr const char* FxaaSource = R"(#version 330 core
in vec2 place;
out vec4 colour;
uniform sampler2D picture;
uniform vec2 texel;
float Luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}
void main()
{
    vec3 m = texture(picture, place).rgb;
    vec3 nw = texture(picture, place + vec2(-1.0, -1.0) * texel).rgb;
    vec3 ne = texture(picture, place + vec2(1.0, -1.0) * texel).rgb;
    vec3 sw = texture(picture, place + vec2(-1.0, 1.0) * texel).rgb;
    vec3 se = texture(picture, place + vec2(1.0, 1.0) * texel).rgb;
    float lm = Luma(m);
    float lnw = Luma(nw);
    float lne = Luma(ne);
    float lsw = Luma(sw);
    float lse = Luma(se);
    float lowest = min(lm, min(min(lnw, lne), min(lsw, lse)));
    float highest = max(lm, max(max(lnw, lne), max(lsw, lse)));
    vec2 direction = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));
    float reduce = max((lnw + lne + lsw + lse) * 0.25 * (1.0 / 8.0), 1.0 / 128.0);
    float smallest = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * smallest, vec2(-8.0), vec2(8.0)) * texel;
    vec3 a = 0.5 * (texture(picture, place + direction * (1.0 / 3.0 - 0.5)).rgb + texture(picture, place + direction * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(picture, place - direction * 0.5).rgb + texture(picture, place + direction * 0.5).rgb);
    float lb = Luma(b);
    colour = vec4((lb < lowest || lb > highest) ? a : b, 1.0);
}
)";

// The window as it's about to be shown (the picture and the overlay over it) written as the PNG asked for, if one is
void WriteWindowShot(s32 width, s32 height)
{
    std::string path;
    {
        std::lock_guard<std::mutex> lock(g_WindowShotLock);
        path.swap(g_WindowShotPath);
    }

    if (path.empty() || width <= 0 || height <= 0)
    {
        return;
    }

    Gl& gl = g_Presenter.gl;
    std::vector<u32> rows(static_cast<size_t>(width) * static_cast<size_t>(height));
    gl.BindFramebuffer(0x8D40, 0);
    gl.PixelStorei(0x0D05, 4);
    gl.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
    std::vector<u32> pixels(rows.size());
    for (s32 y = 0; y < height; y++)
    {
        const u32* row = &rows[static_cast<size_t>(height - 1 - y) * static_cast<size_t>(width)];
        for (s32 x = 0; x < width; x++)
        {
            pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = row[x] | 0xFF000000u;
        }
    }

    bool written = WritePng(path, pixels.data(), static_cast<u32>(width), static_cast<u32>(height));
    Native::Log("window shot: %s (%dx%d)%s", path.c_str(), width, height, written ? "" : ", not written");
}

// The picture drawn over the window: a texture's rectangle (0-1, top row first) of a picture of these pixels, at the TV's shape
void PresentTexture(GLuint texture, f32 u0, f32 v0, f32 u1, f32 v1, u32 pictureWidth, u32 pictureHeight, u32 texelsPerPixel)
{
    Presenter& p = g_Presenter;
    Gl& gl = p.gl;
    s32 width = 0;
    s32 height = 0;
    SDL_GetWindowSizeInPixels(p.window, &width, &height);
    // The hardware renderer shares the context: its state isn't the presenter's
    gl.BindFramebuffer(0x8D40, 0);
    gl.Disable(0x0BE2);
    gl.Disable(0x0B71);
    gl.Disable(0x0C11);
    gl.Disable(0x0B90);
    gl.ColorMask(1, 1, 1, 1);
    gl.Viewport(0, 0, width, height);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT);

    ViewRect(width, height, &p.viewX, &p.viewY, &p.viewWidth, &p.viewHeight);
    gl.Viewport(p.viewX, p.viewY, p.viewWidth, p.viewHeight);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, texture);
    GLint scaling = static_cast<GLint>(p.linear ? GL_LINEAR : GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, scaling);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, scaling);
    if (p.postFilter != p.madePostFilter)
    {
        MakePostFilter(p);
    }

    if (p.antiAliasing == 1 && p.fxaaProgram == 0)
    {
        GLuint vertex = Compile(gl, GL_VERTEX_SHADER, VertexSource);
        GLuint fragment = Compile(gl, GL_FRAGMENT_SHADER, FxaaSource);
        p.fxaaProgram = gl.CreateProgram();
        gl.AttachShader(p.fxaaProgram, vertex);
        gl.AttachShader(p.fxaaProgram, fragment);
        gl.LinkProgram(p.fxaaProgram);
    }

    if (!p.vsyncSet)
    {
        SDL_GL_SetSwapInterval(p.vsync ? 1 : 0);
        p.vsyncSet = true;
    }

    GLuint program = p.postProgram != 0 ? p.postProgram : (p.antiAliasing == 1 && p.fxaaProgram != 0) ? p.fxaaProgram : p.program;
    gl.UseProgram(program);
    gl.Uniform4f(gl.GetUniformLocation(program, "uvRect"), u0, v0, u1, v1);
    if (program == p.fxaaProgram)
    {
        gl.Uniform1i(gl.GetUniformLocation(program, "picture"), 0);
        gl.Uniform2f(gl.GetUniformLocation(program, "texel"), (u1 - u0) / static_cast<f32>(pictureWidth),
                     (v1 - v0) / static_cast<f32>(pictureHeight));
    }
    if (p.postProgram != 0)
    {
        static const auto started = std::chrono::steady_clock::now();
        gl.Uniform1i(gl.GetUniformLocation(p.postProgram, "picture"), 0);
        // The picture's size in the PS2's pixels (its texture may hold several texels a pixel), over the whole texture: the
        // filter's scanlines are the PS2's lines
        f32 spanU = u1 - u0;
        f32 spanV = v1 - v0;
        gl.Uniform2f(gl.GetUniformLocation(p.postProgram, "pictureSize"),
                     static_cast<f32>(pictureWidth) / static_cast<f32>(texelsPerPixel) / spanU,
                     static_cast<f32>(pictureHeight) / static_cast<f32>(texelsPerPixel) / spanV);
        gl.Uniform2f(gl.GetUniformLocation(p.postProgram, "viewSize"), static_cast<f32>(p.viewWidth),
                     static_cast<f32>(p.viewHeight));
        gl.Uniform1f(gl.GetUniformLocation(p.postProgram, "time"),
                     std::chrono::duration<f32>(std::chrono::steady_clock::now() - started).count());
        NativeUiCrtShaderUniforms(p.postProgram, [](const char* name) {
            return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
        });
    }

    gl.BindVertexArray(p.vertexArray);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (g_Overlay != nullptr)
    {
        s32 points = 0;
        s32 pointsHigh = 0;
        SDL_GetWindowSize(p.window, &points, &pointsHigh);
        gl.Viewport(0, 0, width, height);
        g_Overlay(width, height, points > 0 ? static_cast<f32>(width) / static_cast<f32>(points) : 1.0f);
    }

    WriteWindowShot(width, height);
    SDL_GL_SwapWindow(p.window);
}

void Present(const NativeGraphics::Picture& picture)
{
    Presenter& p = g_Presenter;
    if (p.window == nullptr || picture.width == 0)
    {
        return;
    }

    Gl& gl = p.gl;
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, p.texture);
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), static_cast<GLsizei>(picture.width),
                  static_cast<GLsizei>(picture.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, picture.pixels.data());
    PresentTexture(p.texture, 0.0f, 0.0f, 1.0f, 1.0f, picture.width, picture.height, 1);
}
}

namespace NativeGraphics
{
bool PresenterContext(SDL_Window** window, SDL_GLContext* context)
{
    if (g_Presenter.window == nullptr || g_Presenter.context == nullptr)
    {
        return false;
    }

    *window = g_Presenter.window;
    *context = g_Presenter.context;
    return true;
}

namespace
{
// The render thread's context (renderthread.h): made on the main thread, sharing the window's context's objects (the shown
// picture's copies, their fences) when there's a window, with a hidden window of its own to be current with
SDL_Window* g_RenderWindow = nullptr;
SDL_GLContext g_RenderContext = nullptr;
}

bool MakeRenderContext()
{
    if (g_RenderContext != nullptr)
    {
        return true;
    }

    if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "graphics: render thread: no video: %s\n", SDL_GetError());
        return false;
    }

    Presenter& p = g_Presenter;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    if (p.context != nullptr)
    {
        SDL_GL_MakeCurrent(p.window, p.context);
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    }

    g_RenderWindow = SDL_CreateWindow("graphics", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (g_RenderWindow != nullptr)
    {
        g_RenderContext = SDL_GL_CreateContext(g_RenderWindow);
    }

    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    if (g_RenderContext == nullptr)
    {
        std::fprintf(stderr, "graphics: render thread: no OpenGL 3.3 context: %s\n", SDL_GetError());
        if (g_RenderWindow != nullptr)
        {
            SDL_DestroyWindow(g_RenderWindow);
            g_RenderWindow = nullptr;
        }

        return false;
    }

    // Making it made it current here: the window's again (the render thread makes its own current there)
    if (p.context != nullptr)
    {
        SDL_GL_MakeCurrent(p.window, p.context);
    }
    else
    {
        SDL_GL_MakeCurrent(g_RenderWindow, nullptr);
    }

    return true;
}

void UseRenderContext()
{
    SDL_GL_MakeCurrent(g_RenderWindow, g_RenderContext);
}

void ReleaseRenderContext()
{
    SDL_GL_MakeCurrent(g_RenderWindow, nullptr);
}

bool RenderContext(SDL_Window** window, SDL_GLContext* context)
{
    if (!OnRenderThread() || g_RenderContext == nullptr)
    {
        return false;
    }

    *window = g_RenderWindow;
    *context = g_RenderContext;
    return true;
}
}

__attribute__((weak)) const char* NativeUiCrtShaderSource()
{
    return nullptr;
}

__attribute__((weak)) void NativeUiCrtShaderUniforms(unsigned int, void* (*)(const char*))
{
}

void NativeGraphicsPictureRect(float* x, float* y, float* w, float* h)
{
    *x = *y = *w = *h = 0.0f;
    Presenter& p = g_Presenter;
    if (p.window == nullptr)
    {
        return;
    }

    s32 pixelsWide = 0;
    s32 pixelsHigh = 0;
    s32 pointsWide = 0;
    s32 pointsHigh = 0;
    SDL_GetWindowSizeInPixels(p.window, &pixelsWide, &pixelsHigh);
    SDL_GetWindowSize(p.window, &pointsWide, &pointsHigh);
    if (pixelsWide <= 0 || pixelsHigh <= 0)
    {
        return;
    }

    s32 vx = 0;
    s32 vy = 0;
    s32 vw = 0;
    s32 vh = 0;
    ViewRect(pixelsWide, pixelsHigh, &vx, &vy, &vw, &vh);
    f32 sx = static_cast<f32>(pointsWide) / static_cast<f32>(pixelsWide);
    f32 sy = static_cast<f32>(pointsHigh) / static_cast<f32>(pixelsHigh);
    *x = static_cast<f32>(vx) * sx;
    *y = static_cast<f32>(vy) * sy;
    *w = static_cast<f32>(vw) * sx;
    *h = static_cast<f32>(vh) * sy;
}

void NativeGraphicsSetPresentFilter(bool linear)
{
    g_Presenter.linear = linear;
    g_Presenter.filterChanged = true;
}

void NativeGraphicsSetPostFilter(int which)
{
    g_Presenter.postFilter = which;
}

namespace
{
// The renderer wanted (settings.h) and the internal scale setting (0: the window's), set by the UI and applied on the GS's side
std::atomic<int> g_RendererWanted{-1};
// The internal resolution: 4x by default (the UI's resolution setting sets it at start-up), sharp as the overlay's menus
std::atomic<int> g_ScaleSetting{4};
// The window's height in pixels, as the main thread last saw it (the scale setting 0 goes by it)
std::atomic<int> g_WindowPixelHeight{0};
}

namespace NativeGraphics
{
// The window's size noted for the settings (the main thread's: SDL's window calls are its)
void NoteWindowSize()
{
    s32 width = 0;
    s32 height = 0;
    if (g_Presenter.window != nullptr)
    {
        SDL_GetWindowSizeInPixels(g_Presenter.window, &width, &height);
    }

    g_WindowPixelHeight = height;
}

// The renderer and scale settings applied (at the start, and after each picture), on the GS's side. Before the render thread
// starts they're only kept (the UI sets the saved ones before the renderer starts): its start applies them there, where the
// hardware renderer's context is
void ApplyGraphicsSettings()
{
    if (RenderThreadPending())
    {
        return;
    }

    int wanted = g_RendererWanted;
    if (wanted < 0)
    {
        const char* chosen = std::getenv("TWIN_GS");
        int fromEnvironment = (chosen != nullptr && std::strcmp(chosen, "software") == 0) ? 0 : 1;
        g_RendererWanted.compare_exchange_strong(wanted, fromEnvironment);
        wanted = g_RendererWanted;
    }

    if (!OnRenderThread())
    {
        NoteWindowSize();
    }

    // The scale the setting asks for: the window's (its height over the shown picture's, rounded) for 0
    int setting = g_ScaleSetting;
    u32 scale = static_cast<u32>(std::max(setting, 1));
    if (setting == 0)
    {
        s32 height = g_WindowPixelHeight;
        scale = static_cast<u32>(std::clamp((height + 256) / 512, 1, 8));
    }

    if (wanted == 1 && !HardwareRendererActive())
    {
        if (StartHardwareRenderer(scale))
        {
            GetHardware().gs->SetScale(1);
        }
        else
        {
            std::fprintf(stderr, "graphics: no hardware renderer: the software GS draws\n");
            g_RendererWanted = 0;
        }
    }
    else if (wanted == 0 && HardwareRendererActive())
    {
        StopHardwareRenderer();
    }

    if (HardwareRendererActive())
    {
        HardwareSetScale(scale);
    }
    else
    {
        GetHardware().gs->SetScale(std::min<u32>(scale, 4));
    }
}
}

bool NativeGraphicsSetRenderer(int which)
{
    if (which != 0 && which != 1)
    {
        return false;
    }

    g_RendererWanted = which;
    return true;
}

int NativeGraphicsRenderer()
{
    return NativeGraphics::HardwareRendererActive() ? 1 : 0;
}

void NativeGraphicsSetDither(int mode)
{
    NativeGraphics::RenderCall([mode] { NativeGraphics::HardwareSetDither(mode); });
}

void NativeGraphicsSetTextureFiltering(int mode)
{
    NativeGraphics::RenderCall([mode] { NativeGraphics::HardwareSetFiltering(mode); });
}

bool NativeGraphicsSetTextureUpscale(int mode)
{
    // Not yet: only off
    return mode == 0;
}

bool NativeGraphicsSetTexturePack(bool on)
{
    NativeGraphics::RenderCall([on] { NativeGraphics::ReplacementsSetEnabled(on); });
    return true;
}

bool NativeGraphicsSetAntiAliasing(int mode)
{
    if (mode < 0 || mode > 1)
    {
        return false;
    }

    g_Presenter.antiAliasing = mode;
    return true;
}

void NativeGraphicsSetVSync(bool on)
{
    g_Presenter.vsync = on;
    g_Presenter.vsyncSet = false;
}

void NativeGraphicsSetOverlay(void (*draw)(int windowPixelWidth, int windowPixelHeight, float pixelScale))
{
    g_Overlay = draw;
}

void NativeGraphicsShootWindow(const char* path)
{
    std::lock_guard<std::mutex> lock(g_WindowShotLock);
    g_WindowShotPath = path != nullptr ? path : "";
}

int NativeGraphicsPostFilterInUse()
{
    return g_Presenter.postProgram != 0 ? g_Presenter.madePostFilter : 0;
}

void NativeGraphicsAttachWindow(SDL_Window* window)
{
    Presenter& p = g_Presenter;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    p.context = SDL_GL_CreateContext(window);
    if (p.context == nullptr)
    {
        std::fprintf(stderr, "graphics: no OpenGL 3.3 context: %s\n", SDL_GetError());
        return;
    }

    SDL_GL_MakeCurrent(window, p.context);
    // The frames are paced by the vertical blank's own wait
    SDL_GL_SetSwapInterval(0);
    // The graphics card and its driver in the log, for the bug reports: OpenGL's renderer, vendor and version (the driver's own
    // number in it: NVIDIA's "3.3.0 NVIDIA 617.14", AMD's "... Core Profile Context 24.10.1...", Intel's "... Build 31.0.101...",
    // Mesa's), then Windows' adapters and their drivers' versions
    auto getString = reinterpret_cast<const unsigned char* (*)(GLenum)>(SDL_GL_GetProcAddress("glGetString"));
    if (getString != nullptr)
    {
        auto text = [getString](GLenum name) {
            const unsigned char* value = getString(name);
            return value != nullptr ? reinterpret_cast<const char*>(value) : "?";
        };
        Native::Log("graphics: %s (%s), OpenGL %s", text(0x1F01), text(0x1F00), text(0x1F02));
    }
#if defined(_WIN32)
    NativeGraphics::LogAdapters();
#endif
    Gl& gl = p.gl;
    Load(gl.GenTextures, "glGenTextures");
    Load(gl.BindTexture, "glBindTexture");
    Load(gl.TexParameteri, "glTexParameteri");
    Load(gl.TexImage2D, "glTexImage2D");
    Load(gl.Viewport, "glViewport");
    Load(gl.ClearColor, "glClearColor");
    Load(gl.Clear, "glClear");
    Load(gl.DrawArrays, "glDrawArrays");
    Load(gl.CreateShader, "glCreateShader");
    Load(gl.ShaderSource, "glShaderSource");
    Load(gl.CompileShader, "glCompileShader");
    Load(gl.GetShaderiv, "glGetShaderiv");
    Load(gl.GetShaderInfoLog, "glGetShaderInfoLog");
    Load(gl.CreateProgram, "glCreateProgram");
    Load(gl.AttachShader, "glAttachShader");
    Load(gl.LinkProgram, "glLinkProgram");
    Load(gl.GetProgramiv, "glGetProgramiv");
    Load(gl.UseProgram, "glUseProgram");
    Load(gl.GetUniformLocation, "glGetUniformLocation");
    Load(gl.Uniform1i, "glUniform1i");
    Load(gl.GenVertexArrays, "glGenVertexArrays");
    Load(gl.BindVertexArray, "glBindVertexArray");
    Load(gl.ActiveTexture, "glActiveTexture");
    Load(gl.Uniform2f, "glUniform2f");
    Load(gl.Uniform1f, "glUniform1f");
    Load(gl.DeleteProgram, "glDeleteProgram");
    Load(gl.DeleteShader, "glDeleteShader");
    Load(gl.BindFramebuffer, "glBindFramebuffer");
    Load(gl.Enable, "glEnable");
    Load(gl.Disable, "glDisable");
    Load(gl.ColorMask, "glColorMask");
    Load(gl.Uniform4f, "glUniform4f");
    Load(gl.Flush, "glFlush");
    Load(gl.FenceSync, "glFenceSync");
    Load(gl.WaitSync, "glWaitSync");
    Load(gl.DeleteSync, "glDeleteSync");
    Load(gl.PixelStorei, "glPixelStorei");
    Load(gl.ReadPixels, "glReadPixels");

    const char* filter = std::getenv("TWIN_DISPLAY_FILTER");
    if (filter != nullptr)
    {
        p.linear = std::strcmp(filter, "nearest") != 0;
    }
    gl.GenTextures(1, &p.texture);
    gl.BindTexture(GL_TEXTURE_2D, p.texture);
    GLint scaling = static_cast<GLint>(p.linear ? GL_LINEAR : GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, scaling);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, scaling);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    GLuint vertex = Compile(gl, GL_VERTEX_SHADER, VertexSource);
    GLuint fragment = Compile(gl, GL_FRAGMENT_SHADER, FragmentSource);
    p.program = gl.CreateProgram();
    gl.AttachShader(p.program, vertex);
    gl.AttachShader(p.program, fragment);
    gl.LinkProgram(p.program);
    GLint linked = 0;
    gl.GetProgramiv(p.program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        std::fprintf(stderr, "graphics: the presenter's program doesn't link\n");
        std::abort();
    }

    gl.UseProgram(p.program);
    gl.Uniform1i(gl.GetUniformLocation(p.program, "picture"), 0);
    gl.GenVertexArrays(1, &p.vertexArray);
    p.window = window;
}

namespace NativeGraphics
{
void SetVideoMode(bool pal)
{
    g_Pal = pal;
}

void SetPacing(bool paced)
{
    g_Paced = paced;
}

void SetFrameRate(s32 rate)
{
    g_FrameRate = rate == 0 ? -1 : rate > 60 ? rate : 0;
}

// The next vertical blank: a field's time after the last one (50 Hz PAL, 60000/1001 Hz NTSC); late frames start counting again
// The game thread's work between its waits for the vertical blank (TWIN_FRAME_STATS' "game")
std::atomic<u64> g_GameWorkNanoseconds{0};
std::chrono::steady_clock::time_point g_GameWorkFrom;

void WaitForVBlank()
{
    Native::GameFrameDone();
    const auto arrived = std::chrono::steady_clock::now();
    if (g_GameWorkFrom.time_since_epoch().count() != 0)
    {
        g_GameWorkNanoseconds += static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(arrived - g_GameWorkFrom).count());
    }

    // The log's frame counts (sessionlog.cpp: the performance lines)
    Native::SessionFrame(g_GameWorkNanoseconds.load(std::memory_order_relaxed));
    struct Restart
    {
        ~Restart()
        {
            g_GameWorkFrom = std::chrono::steady_clock::now();
        }
    } restart;
    if (!g_Paced || g_FrameRate < 0)
    {
        return;
    }

    using namespace std::chrono;
    auto field = g_FrameRate > 0 ? duration_cast<steady_clock::duration>(nanoseconds(1000000000ll / g_FrameRate))
                 : g_Pal         ? duration_cast<steady_clock::duration>(microseconds(20000))
                                 : duration_cast<steady_clock::duration>(nanoseconds(16683333));
    auto now = steady_clock::now();
    if (!g_BlankStarted || now > g_NextBlank + field)
    {
        g_BlankStarted = true;
        g_NextBlank = now + field;
        return;
    }

#if defined(_WIN32)
    // Windows' sleeps are as coarse as its timer (up to 15.6 ms late, a 50 Hz field could take 31): SDL's precise delay, a high
    // resolution waitable timer and a short spin
    auto left = duration_cast<nanoseconds>(g_NextBlank - steady_clock::now()).count();
    if (left > 0)
    {
        SDL_DelayPrecise(static_cast<Uint64>(left));
    }
#else
    std::this_thread::sleep_until(g_NextBlank);
#endif
    g_NextBlank += field;
}

namespace
{
bool g_PictureStale = false;

void ReadPictureInto(Picture& picture)
{
    Gs::DisplayImage image;
    if (!HardwareReadDisplay(image))
    {
        GetHardware().gs->ReadDisplay(image);
    }

    picture.width = image.width;
    picture.height = image.height;
    picture.pixels = std::move(image.pixels);
}

void ReadPicture()
{
    ReadPictureInto(g_Picture);
    g_PictureStale = false;
}

// TWIN_DUMP_FRAMES=FOLDER[:EVERY]: every (default 50th) picture also written there as frame_NNNNNN.png
void DumpPicture(const Picture& picture, u32 number)
{
    static const char* dump = std::getenv("TWIN_DUMP_FRAMES");
    if (dump == nullptr || picture.width == 0)
    {
        return;
    }

    std::string folder = dump;
    u32 every = 50;
    size_t colon = folder.rfind(':');
    if (colon != std::string::npos)
    {
        every = static_cast<u32>(std::max(1, std::atoi(folder.c_str() + colon + 1)));
        folder.resize(colon);
    }

    if (number % every == 0)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "/frame_%06u.png", number);
        WritePng(folder + name, picture.pixels.data(), picture.width, picture.height);
    }
}

// With the render thread: each frame's picture, made there into a slot (three: the window draws one while the next ones are
// made) and shown from the main thread a frame later
struct Shown
{
    // The hardware renderer's copy of the picture (and the fence the window's context waits for), or the picture read out
    bool gpu = false;
    ShownTexture texture = {};
    u32 scale = 1;
    Picture picture;
    // The window's fence after it drew the copy (the render thread waits for it before it copies into the slot again)
    void* drawn = nullptr;
    // The GS's counts when it was made (statistics)
    u64 primitives = 0;
    u64 boxPixels = 0;
};
Shown g_Shown[3];
u32 g_ShownCount = 0;
// LastPicture's copy for the game's side
Picture g_PictureForCaller;

// On the render thread: the frame's picture into its slot, then the settings applied (as ShowFrameHere does)
void MakeShown(u64 frame)
{
    static const bool dumping = std::getenv("TWIN_DUMP_FRAMES") != nullptr;
    Shown& shown = g_Shown[frame % 3];
    g_PictureStale = true;
    shown.gpu = false;
    if (HardwareRendererActive() && HardwareCopyDisplay(static_cast<u32>(frame % 3), shown.drawn, &shown.texture))
    {
        shown.drawn = nullptr;
        shown.gpu = true;
        shown.scale = HardwareScale();
    }

    if ((!shown.gpu && g_Presenter.window != nullptr) || dumping)
    {
        ReadPictureInto(shown.picture);
    }

    Hardware& h = GetHardware();
    shown.primitives = h.gs->primitivesDrawn;
    shown.boxPixels = h.gs->boxPixels;
    DumpPicture(shown.picture, g_ShownCount++);
    ApplyGraphicsSettings();
}

// On the main thread: a slot's picture shown in the window
void PresentShown(Shown& shown)
{
    Presenter& p = g_Presenter;
    if (p.window == nullptr)
    {
        return;
    }

    if (!shown.gpu)
    {
        Present(shown.picture);
        return;
    }

    Gl& gl = p.gl;
    if (shown.texture.fence != nullptr)
    {
        gl.WaitSync(shown.texture.fence, 0, ~0ull);
        gl.DeleteSync(shown.texture.fence);
        shown.texture.fence = nullptr;
    }

    PresentTexture(shown.texture.texture, 0.0f, 0.0f, 1.0f, 1.0f, shown.texture.width, shown.texture.height, shown.scale);
    if (shown.drawn != nullptr)
    {
        gl.DeleteSync(shown.drawn);
    }

    shown.drawn = gl.FenceSync(0x9117, 0);
    gl.Flush();
}

// TWIN_FRAME_STATS with the render thread: the time between pictures shown, VU1's and VU0's work on the game's thread for the
// frame just made, the GS's primitives of the frame shown, and the render thread's time working and the game's waiting for it
void ThreadedFrameStats(const Shown* shown)
{
    static const bool stats = std::getenv("TWIN_FRAME_STATS") != nullptr;
    if (!stats || shown == nullptr)
    {
        return;
    }

    static auto last = std::chrono::steady_clock::now();
    static u64 vu1 = 0, vu0 = 0, prims = 0, pixels = 0, vu1Ns = 0, translated = 0, busy = 0, waited = 0;
    Hardware& h = GetHardware();
    auto now = std::chrono::steady_clock::now();
    // (VU1's counts are the render thread's when it runs VU1: not read from here)
    const bool vu1Here = !RenderThreadRunsVu1();
    u64 ns = vu1Here ? h.vu1->runNanoseconds - h.vu1->kickNanoseconds : vu1Ns;
    u64 executed = vu1Here ? h.vu1->executed - vu1 : 0;
    u64 translatedNow = vu1Here ? Ee::GetVu1TranslationStats().translatedInstructions : translated;
    u64 busyNow = RenderBusyNanoseconds();
    u64 waitedNow = RenderWaitNanoseconds();
    // The GPU renderer's work for the frame: its batches, the textures it decoded, memory's pages sent to it and read back,
    // its copies of a target
    static HardwareStats hw = {};
    const HardwareStats hwNow = GetHardwareStats();
    static u64 game = 0;
    const u64 gameNow = g_GameWorkNanoseconds.load();
    std::fprintf(stderr,
                 "frame: %.1f ms, vu1 %llu (%.1f ms, %.0f%% translated), vu0 %llu, primitives %llu, box pixels %llu, render %.1f ms, "
                 "waited %.1f ms; gpu batches %llu, decodes %llu, uploads %llu, downloads %llu, copies %llu; game %.1f ms\n",
                 std::chrono::duration<double, std::milli>(now - last).count(), (unsigned long long)executed, (ns - vu1Ns) / 1e6,
                 executed != 0 ? 100.0 * (translatedNow - translated) / executed : 0.0, (unsigned long long)(h.vu0->executed - vu0),
                 (unsigned long long)(shown->primitives - prims), (unsigned long long)(shown->boxPixels - pixels),
                 (busyNow - busy) / 1e6, (waitedNow - waited) / 1e6, (unsigned long long)(hwNow.batches - hw.batches),
                 (unsigned long long)(hwNow.decodes - hw.decodes), (unsigned long long)(hwNow.uploads - hw.uploads),
                 (unsigned long long)(hwNow.downloads - hw.downloads), (unsigned long long)(hwNow.copies - hw.copies),
                 (gameNow - game) / 1e6);
    hw = hwNow;
    game = gameNow;
    if (vu1Here)
    {
        h.vu1->timing = true;
        vu1 = h.vu1->executed;
    }

    vu1Ns = ns;
    translated = translatedNow;
    last = now;
    vu0 = h.vu0->executed;
    prims = shown->primitives;
    pixels = shown->boxPixels;
    busy = busyNow;
    waited = waitedNow;
}

// Without the render thread: the picture read out and shown at once
void ShowFrameHere();
}

void ShowFrame()
{
    NoteWindowSize();
    if (!RenderThreadActive())
    {
        ShowFrameHere();
        return;
    }

    CaptureShown();
    // This frame's picture made on the render thread; the one before it, made by now or waited for, shown
    u64 frame = RenderQueueFrame([](u64 made) { MakeShown(made); });
    const Shown* shown = nullptr;
    if (frame > 1)
    {
        RenderWaitFrame(frame - 1);
        Shown& before = g_Shown[(frame - 1) % 3];
        PresentShown(before);
        shown = &before;
    }

    ThreadedFrameStats(shown);
}

namespace
{
void ShowFrameHere()
{
    CaptureShown();
    // The picture is read out when something shows or keeps it: a window, a frame dump, or LastPicture's caller later (its
    // buffer and the PCRTC's registers stay as they are until the next frame's drawing)
    static const bool dumping = std::getenv("TWIN_DUMP_FRAMES") != nullptr;
    g_PictureStale = true;
    // The hardware renderer's picture is drawn straight from its target
    DisplayTexture gpu;
    if (HardwareRendererActive() && HardwareDisplayTexture(&gpu))
    {
        if (g_Presenter.window != nullptr)
        {
            PresentTexture(gpu.texture, gpu.u0, gpu.v0, gpu.u1, gpu.v1, gpu.width, gpu.height, HardwareScale());
        }

        if (dumping)
        {
            ReadPicture();
        }
    }
    else
    {
        if (g_Presenter.window != nullptr || dumping)
        {
            ReadPicture();
        }

        Present(g_Picture);
    }

    // TWIN_FRAME_STATS: each frame's time, VU1's and VU0's instructions, the GS's primitives and their boxes' pixels
    static const bool stats = std::getenv("TWIN_FRAME_STATS") != nullptr;
    if (stats)
    {
        static auto last = std::chrono::steady_clock::now();
        static u64 vu1 = 0, vu0 = 0, prims = 0, pixels = 0, vu1Ns = 0, translated = 0;
        Hardware& h = GetHardware();
        auto now = std::chrono::steady_clock::now();
        // VU1's time: its programs' less their XGKICKs' (the GIF and the GS drawing the packets)
        u64 ns = h.vu1->runNanoseconds - h.vu1->kickNanoseconds;
        u64 executed = h.vu1->executed - vu1;
        u64 translatedNow = Ee::GetVu1TranslationStats().translatedInstructions;
        std::fprintf(stderr, "frame: %.1f ms, vu1 %llu (%.1f ms, %.0f%% translated), vu0 %llu, primitives %llu, box pixels %llu\n",
                     std::chrono::duration<double, std::milli>(now - last).count(), (unsigned long long)executed,
                     (ns - vu1Ns) / 1e6, executed != 0 ? 100.0 * (translatedNow - translated) / executed : 0.0,
                     (unsigned long long)(h.vu0->executed - vu0), (unsigned long long)(h.gs->primitivesDrawn - prims),
                     (unsigned long long)(h.gs->boxPixels - pixels));
        h.vu1->timing = true;
        vu1Ns = ns;
        translated = translatedNow;
        last = now;
        vu1 = h.vu1->executed;
        vu0 = h.vu0->executed;
        prims = h.gs->primitivesDrawn;
        pixels = h.gs->boxPixels;
    }

    // TWIN_DUMP_FRAMES=FOLDER[:EVERY]: every (default 50th) picture also written there as frame_NNNNNN.png
    static const char* dump = std::getenv("TWIN_DUMP_FRAMES");
    static u32 shown = 0;
    if (dump != nullptr && g_Picture.width != 0)
    {
        std::string folder = dump;
        u32 every = 50;
        size_t colon = folder.rfind(':');
        if (colon != std::string::npos)
        {
            every = static_cast<u32>(std::max(1, std::atoi(folder.c_str() + colon + 1)));
            folder.resize(colon);
        }

        if (shown % every == 0)
        {
            char name[32];
            std::snprintf(name, sizeof(name), "/frame_%06u.png", shown);
            WritePng(folder + name, g_Picture.pixels.data(), g_Picture.width, g_Picture.height);
        }
    }

    shown++;
    ApplyGraphicsSettings();
}
}

const Picture& LastPicture()
{
    // With the render thread, read there once everything queued is drawn
    if (RenderThreadActive())
    {
        RenderCallAndWait([] {
            if (g_PictureStale)
            {
                ReadPicture();
            }

            g_PictureForCaller = g_Picture;
        });
        return g_PictureForCaller;
    }

    if (g_PictureStale)
    {
        ReadPicture();
    }

    return g_Picture;
}
}

void NativeGraphicsSetResolution(int internalScale, int windowWidth, int windowHeight, bool fullscreen)
{
    g_ScaleSetting = std::clamp(internalScale, 0, 8);
    NativeGraphics::NoteWindowSize();
    NativeGraphics::RenderCall([] { NativeGraphics::ApplyGraphicsSettings(); });
    SDL_Window* window = g_Presenter.window;
    if (window == nullptr)
    {
        return;
    }

    // Only what changes, without waiting for the window manager (a window in the background never finishes a sync)
    bool isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (isFullscreen != fullscreen)
    {
        SDL_SetWindowFullscreen(window, fullscreen);
    }

    if (!fullscreen && windowWidth > 0 && windowHeight > 0)
    {
        s32 width = 0;
        s32 height = 0;
        SDL_GetWindowSize(window, &width, &height);
        if (width != windowWidth || height != windowHeight)
        {
            SDL_SetWindowSize(window, windowWidth, windowHeight);
        }
    }
}

void NativeGraphicsGetResolution(int* internalScale, int* windowWidth, int* windowHeight, bool* fullscreen)
{
    NativeGraphicsResolution resolution = NativeGraphicsCurrentResolution();
    if (internalScale != nullptr)
    {
        *internalScale = resolution.internalScale;
    }

    if (windowWidth != nullptr)
    {
        *windowWidth = resolution.windowWidth;
    }

    if (windowHeight != nullptr)
    {
        *windowHeight = resolution.windowHeight;
    }

    if (fullscreen != nullptr)
    {
        *fullscreen = resolution.fullscreen;
    }
}

NativeGraphicsResolution NativeGraphicsCurrentResolution()
{
    NativeGraphicsResolution resolution = {};
    resolution.internalScale = g_ScaleSetting;
    if (SDL_Window* window = g_Presenter.window)
    {
        SDL_GetWindowSize(window, &resolution.windowWidth, &resolution.windowHeight);
        resolution.fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    }

    return resolution;
}

void NativeGraphicsLogDisplay()
{
    SDL_Window* window = g_Presenter.window;
    if (window == nullptr)
    {
        Native::Log("display: none (no window: a test's run)");
        return;
    }

    // The screen the window is on (its number among the screens), its desktop mode in pixels, the desktop's scale, HDR
    SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    int count = 0;
    int number = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    for (int i = 0; displays != nullptr && i < count; i++)
    {
        number = displays[i] == display ? i + 1 : number;
    }

    SDL_free(displays);
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display);
    const char* name = SDL_GetDisplayName(display);
    bool hdr = SDL_GetBooleanProperty(SDL_GetDisplayProperties(display), SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN, false);
    if (mode == nullptr)
    {
        Native::Log("display: screen %d of %d, its mode unknown (%s)", number, count, SDL_GetError());
        return;
    }

    Native::Log("display: screen %d of %d (%s), %dx%d at %.2f Hz, scale %.0f%%, HDR %s", number, count, name != nullptr ? name : "?",
                static_cast<int>(mode->w * mode->pixel_density + 0.5f), static_cast<int>(mode->h * mode->pixel_density + 0.5f),
                static_cast<double>(mode->refresh_rate), static_cast<double>(SDL_GetDisplayContentScale(display) * 100.0f),
                hdr ? "on" : "off");
}
