#include "gl.h"

#include "graphics/gputime.h"
#include "graphics/renderwork.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Hw
{
namespace
{
Gl g_Gl;
bool g_Loaded = false;

// The hardware renderer asks for its passes' uniforms by name every time it uses them (thousands a frame), and the driver looks
// each name up again: the answers kept, a thread's (each has its own context) by program and name. The renderer's programs live
// as long as it does, so an answer never goes stale
GLint (*g_GetUniformLocation)(GLuint, const GLchar*) = nullptr;

struct NameHash
{
    using is_transparent = void;
    size_t operator()(std::string_view name) const
    {
        return std::hash<std::string_view>()(name);
    }
};

GLint CachedUniformLocation(GLuint program, const GLchar* name)
{
    thread_local std::unordered_map<GLuint, std::unordered_map<std::string, GLint, NameHash, std::equal_to<>>> cache;
    auto& names = cache[program];
    auto found = names.find(std::string_view(name));
    if (found != names.end())
    {
        return found->second;
    }

    GLint location = g_GetUniformLocation(program, name);
    names.emplace(name, location);
    return location;
}

template <typename T>
bool Load(T& function, const char* name)
{
    function = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    if (function == nullptr)
    {
        std::fprintf(stderr, "graphics: OpenGL has no %s\n", name);
        return false;
    }

    // The work given the card timed (gputime.h)
    if (void* timed = NativeGraphics::TimedEntryPoint(name))
    {
        function = reinterpret_cast<T>(timed);
    }

    return true;
}

// The render thread's OpenGL calls counted as such while it emulates the GS (renderwork.h: the log's split of its time); a picture
// read back is a wait for the card. OpenGL's own entry points behind them
Gl g_Real;
// $TWIN_GL_CALLS (native/tools/perf.py, THE LIST 67): the same calls counted by entry point too. Only the render thread adds to a
// count (a plain add, no locked instruction); the game's thread reads them for the log
bool g_CountingCalls = false;
std::vector<GlCallCount> g_CallCounts;

template <auto Member>
struct Counted;

template <typename R, typename... A, R (*Gl::*Member)(A...)>
struct Counted<Member>
{
    static inline std::atomic<std::uint64_t> calls{0};

    template <NativeGraphics::Work Kind>
    static R Call(A... arguments)
    {
        if (NativeGraphics::t_Work != NativeGraphics::Work::Gs)
        {
            return (g_Real.*Member)(arguments...);
        }

        if (g_CountingCalls)
        {
            calls.store(calls.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        }

        NativeGraphics::WorkSpan span(Kind);
        return (g_Real.*Member)(arguments...);
    }
};

void CountCalls()
{
    using NativeGraphics::Work;
    g_Real = g_Gl;
    g_CountingCalls = std::getenv("TWIN_GL_CALLS") != nullptr;
#define TWIN_COUNTED(name, kind)                                                                                                        g_Gl.name = &Counted<&Gl::name>::Call<Work::kind>;                                                                                  g_CallCounts.push_back({#name, &Counted<&Gl::name>::calls})
    TWIN_COUNTED(GenTextures, OpenGl);
    TWIN_COUNTED(DeleteTextures, OpenGl);
    TWIN_COUNTED(BindTexture, OpenGl);
    TWIN_COUNTED(TexImage2D, OpenGl);
    TWIN_COUNTED(TexSubImage2D, OpenGl);
    TWIN_COUNTED(TexParameteri, OpenGl);
    TWIN_COUNTED(ActiveTexture, OpenGl);
    TWIN_COUNTED(PixelStorei, OpenGl);
    TWIN_COUNTED(GenFramebuffers, OpenGl);
    TWIN_COUNTED(DeleteFramebuffers, OpenGl);
    TWIN_COUNTED(BindFramebuffer, OpenGl);
    TWIN_COUNTED(FramebufferTexture2D, OpenGl);
    TWIN_COUNTED(CheckFramebufferStatus, OpenGl);
    TWIN_COUNTED(BlitFramebuffer, OpenGl);
    TWIN_COUNTED(ReadPixels, CardWait);
    TWIN_COUNTED(ReadBuffer, OpenGl);
    TWIN_COUNTED(DrawBuffer, OpenGl);
    TWIN_COUNTED(Viewport, OpenGl);
    TWIN_COUNTED(Scissor, OpenGl);
    TWIN_COUNTED(Enable, OpenGl);
    TWIN_COUNTED(Disable, OpenGl);
    TWIN_COUNTED(BlendFuncSeparate, OpenGl);
    TWIN_COUNTED(BlendEquationSeparate, OpenGl);
    TWIN_COUNTED(BlendColor, OpenGl);
    TWIN_COUNTED(ColorMask, OpenGl);
    TWIN_COUNTED(DepthMask, OpenGl);
    TWIN_COUNTED(DepthFunc, OpenGl);
    TWIN_COUNTED(Clear, OpenGl);
    TWIN_COUNTED(ClearColor, OpenGl);
    TWIN_COUNTED(CreateShader, OpenGl);
    TWIN_COUNTED(ShaderSource, OpenGl);
    TWIN_COUNTED(CompileShader, OpenGl);
    TWIN_COUNTED(GetShaderiv, OpenGl);
    TWIN_COUNTED(GetShaderInfoLog, OpenGl);
    TWIN_COUNTED(DeleteShader, OpenGl);
    TWIN_COUNTED(CreateProgram, OpenGl);
    TWIN_COUNTED(AttachShader, OpenGl);
    TWIN_COUNTED(BindFragDataLocationIndexed, OpenGl);
    TWIN_COUNTED(BindAttribLocation, OpenGl);
    TWIN_COUNTED(LinkProgram, OpenGl);
    TWIN_COUNTED(GetProgramiv, OpenGl);
    TWIN_COUNTED(GetProgramInfoLog, OpenGl);
    TWIN_COUNTED(UseProgram, OpenGl);
    TWIN_COUNTED(GetUniformLocation, OpenGl);
    TWIN_COUNTED(Uniform1i, OpenGl);
    TWIN_COUNTED(Uniform1f, OpenGl);
    TWIN_COUNTED(Uniform2f, OpenGl);
    TWIN_COUNTED(Uniform3f, OpenGl);
    TWIN_COUNTED(Uniform4f, OpenGl);
    TWIN_COUNTED(Uniform1iv, OpenGl);
    TWIN_COUNTED(GenVertexArrays, OpenGl);
    TWIN_COUNTED(BindVertexArray, OpenGl);
    TWIN_COUNTED(GenBuffers, OpenGl);
    TWIN_COUNTED(BindBuffer, OpenGl);
    TWIN_COUNTED(BufferData, OpenGl);
    TWIN_COUNTED(VertexAttribPointer, OpenGl);
    TWIN_COUNTED(VertexAttribIPointer, OpenGl);
    TWIN_COUNTED(EnableVertexAttribArray, OpenGl);
    TWIN_COUNTED(DrawArrays, OpenGl);
    TWIN_COUNTED(Finish, CardWait);
    TWIN_COUNTED(GetError, OpenGl);
    TWIN_COUNTED(GetIntegerv, OpenGl);
    TWIN_COUNTED(PointSize, OpenGl);
    TWIN_COUNTED(LineWidth, OpenGl);
    TWIN_COUNTED(Uniform4ui, OpenGl);
    TWIN_COUNTED(Uniform1ui, OpenGl);
    TWIN_COUNTED(StencilFunc, OpenGl);
    TWIN_COUNTED(TexParameterf, OpenGl);
    TWIN_COUNTED(GenerateMipmap, OpenGl);
    TWIN_COUNTED(StencilOp, OpenGl);
    TWIN_COUNTED(StencilMask, OpenGl);
    TWIN_COUNTED(ClearStencil, OpenGl);
    TWIN_COUNTED(Flush, OpenGl);
    TWIN_COUNTED(FenceSync, OpenGl);
    TWIN_COUNTED(WaitSync, OpenGl);
    TWIN_COUNTED(DeleteSync, OpenGl);
#undef TWIN_COUNTED
}

// THE LIST 69 (gl.h's CacheGlState): the hardware renderer sets every uniform and most of the context's state at every batch
// (63 calls a batch, 14,400 a frame at the beach), mostly to what they already are. Each call that would change nothing is
// left out here, above the counted entry points (so $TWIN_GL_CALLS counts the calls that reach the driver)
Gl g_Below;
bool g_Caching = false;

// A uniform's value by program and location: a program's own state (shared by the contexts, set only by the renderer, the
// only user of its programs). The program is the one last made current through here on the thread: the renderer makes its
// program current before setting its uniforms
struct UniformSlot
{
    std::uint32_t value[4];
    bool known;
};
std::vector<std::vector<UniformSlot>> g_Uniforms;
thread_local GLuint t_Program = 0;

// Whether the value is new (kept as the location's): a location of -1 (a uniform the program doesn't use) changes nothing
template <typename... T>
bool UniformChanged(GLint location, T... values)
{
    if (location == -1)
    {
        return false;
    }

    if (t_Program == 0 || location < 0 || location >= 4096)
    {
        return true;
    }

    if (g_Uniforms.size() <= t_Program)
    {
        g_Uniforms.resize(t_Program + 1);
    }

    std::vector<UniformSlot>& slots = g_Uniforms[t_Program];
    if (slots.size() <= static_cast<size_t>(location))
    {
        slots.resize(static_cast<size_t>(location) + 1);
    }

    UniformSlot& slot = slots[static_cast<size_t>(location)];
    const std::uint32_t words[4] = {std::bit_cast<std::uint32_t>(values)...};
    bool same = slot.known;
    for (size_t i = 0; i < sizeof...(T); i++)
    {
        same = same && slot.value[i] == words[i];
        slot.value[i] = words[i];
    }

    slot.known = true;
    return !same;
}

void CachedUniform1i(GLint location, GLint x)
{
    if (UniformChanged(location, x))
    {
        g_Below.Uniform1i(location, x);
    }
}

void CachedUniform1ui(GLint location, GLuint x)
{
    if (UniformChanged(location, x))
    {
        g_Below.Uniform1ui(location, x);
    }
}

void CachedUniform1f(GLint location, GLfloat x)
{
    if (UniformChanged(location, x))
    {
        g_Below.Uniform1f(location, x);
    }
}

void CachedUniform2f(GLint location, GLfloat x, GLfloat y)
{
    if (UniformChanged(location, x, y))
    {
        g_Below.Uniform2f(location, x, y);
    }
}

void CachedUniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z)
{
    if (UniformChanged(location, x, y, z))
    {
        g_Below.Uniform3f(location, x, y, z);
    }
}

void CachedUniform4f(GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    if (UniformChanged(location, x, y, z, w))
    {
        g_Below.Uniform4f(location, x, y, z, w);
    }
}

void CachedUniform4ui(GLint location, GLuint x, GLuint y, GLuint z, GLuint w)
{
    if (UniformChanged(location, x, y, z, w))
    {
        g_Below.Uniform4ui(location, x, y, z, w);
    }
}

void CachedUniform1iv(GLint location, GLsizei count, const GLint* values)
{
    // An array's elements are the locations after its first
    bool changed = false;
    for (GLsizei i = 0; i < count; i++)
    {
        changed |= UniformChanged(location < 0 ? location : location + i, values[i]);
    }

    if (changed)
    {
        g_Below.Uniform1iv(location, count, values);
    }
}

void CachedLinkProgram(GLuint program)
{
    // (Linking sets its uniforms to zero)
    if (program < g_Uniforms.size())
    {
        g_Uniforms[program].clear();
    }

    g_Below.LinkProgram(program);
}

// The context's state as last set through here on the thread (-1 or not known: unknown), while it's the renderer's own
constexpr GLenum CachedCaps[] = {GL_BLEND, GL_DEPTH_TEST, GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_CULL_FACE, GL_PROGRAM_POINT_SIZE};
constexpr int CachedUnits = 16;

struct TextureParams
{
    GLint value[5];
    std::uint8_t known;
};

struct ContextState
{
    bool on = false;
    signed char caps[std::size(CachedCaps)];
    GLint viewport[4];
    bool viewportKnown;
    GLint scissor[4];
    bool scissorKnown;
    int colorMask;
    int depthMask;
    long long depthFunc;
    GLenum blendFunc[4];
    bool blendFuncKnown;
    GLenum blendEquation[2];
    bool blendEquationKnown;
    std::uint32_t blendColor[4];
    bool blendColorKnown;
    GLenum stencilFunc;
    GLint stencilRef;
    GLuint stencilFuncMask;
    bool stencilFuncKnown;
    GLenum stencilOp[3];
    bool stencilOpKnown;
    long long stencilMask;
    int activeUnit;
    long long textures[CachedUnits];
    long long drawFramebuffer;
    long long readFramebuffer;
    long long vertexArray;
    long long arrayBuffer;
    long long program;
    // A texture's sampling by its name (a texture's own state; the window's code sets only the filters of the pictures it
    // shows, which the renderer never does)
    std::vector<TextureParams> params;
};
thread_local ContextState t_State;

void Forget(ContextState& s)
{
    for (signed char& cap : s.caps)
    {
        cap = -1;
    }

    s.viewportKnown = s.scissorKnown = s.blendFuncKnown = s.blendEquationKnown = s.blendColorKnown = false;
    s.stencilFuncKnown = s.stencilOpKnown = false;
    s.colorMask = s.depthMask = s.activeUnit = -1;
    s.depthFunc = s.stencilMask = s.drawFramebuffer = s.readFramebuffer = s.vertexArray = s.arrayBuffer = s.program = -1;
    for (long long& texture : s.textures)
    {
        texture = -1;
    }

    s.params.clear();
}

int CapIndex(GLenum cap)
{
    for (int i = 0; i < static_cast<int>(std::size(CachedCaps)); i++)
    {
        if (CachedCaps[i] == cap)
        {
            return i;
        }
    }

    return -1;
}

void CachedEnable(GLenum cap)
{
    ContextState& s = t_State;
    const int i = s.on ? CapIndex(cap) : -1;
    if (i >= 0 && s.caps[i] == 1)
    {
        return;
    }

    if (i >= 0)
    {
        s.caps[i] = 1;
    }

    g_Below.Enable(cap);
}

void CachedDisable(GLenum cap)
{
    ContextState& s = t_State;
    const int i = s.on ? CapIndex(cap) : -1;
    if (i >= 0 && s.caps[i] == 0)
    {
        return;
    }

    if (i >= 0)
    {
        s.caps[i] = 0;
    }

    g_Below.Disable(cap);
}

void CachedViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    ContextState& s = t_State;
    const GLint wanted[4] = {x, y, width, height};
    if (s.on && s.viewportKnown && std::equal(wanted, wanted + 4, s.viewport))
    {
        return;
    }

    std::copy(wanted, wanted + 4, s.viewport);
    s.viewportKnown = s.on;
    g_Below.Viewport(x, y, width, height);
}

void CachedScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    ContextState& s = t_State;
    const GLint wanted[4] = {x, y, width, height};
    if (s.on && s.scissorKnown && std::equal(wanted, wanted + 4, s.scissor))
    {
        return;
    }

    std::copy(wanted, wanted + 4, s.scissor);
    s.scissorKnown = s.on;
    g_Below.Scissor(x, y, width, height);
}

void CachedColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    ContextState& s = t_State;
    const int mask = (r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0);
    if (s.on && s.colorMask == mask)
    {
        return;
    }

    s.colorMask = s.on ? mask : -1;
    g_Below.ColorMask(r, g, b, a);
}

void CachedDepthMask(GLboolean on)
{
    ContextState& s = t_State;
    const int mask = on ? 1 : 0;
    if (s.on && s.depthMask == mask)
    {
        return;
    }

    s.depthMask = s.on ? mask : -1;
    g_Below.DepthMask(on);
}

void CachedDepthFunc(GLenum function)
{
    ContextState& s = t_State;
    if (s.on && s.depthFunc == static_cast<long long>(function))
    {
        return;
    }

    s.depthFunc = s.on ? static_cast<long long>(function) : -1;
    g_Below.DepthFunc(function);
}

void CachedBlendFuncSeparate(GLenum a, GLenum b, GLenum c, GLenum d)
{
    ContextState& s = t_State;
    const GLenum wanted[4] = {a, b, c, d};
    if (s.on && s.blendFuncKnown && std::equal(wanted, wanted + 4, s.blendFunc))
    {
        return;
    }

    std::copy(wanted, wanted + 4, s.blendFunc);
    s.blendFuncKnown = s.on;
    g_Below.BlendFuncSeparate(a, b, c, d);
}

void CachedBlendEquationSeparate(GLenum a, GLenum b)
{
    ContextState& s = t_State;
    if (s.on && s.blendEquationKnown && s.blendEquation[0] == a && s.blendEquation[1] == b)
    {
        return;
    }

    s.blendEquation[0] = a;
    s.blendEquation[1] = b;
    s.blendEquationKnown = s.on;
    g_Below.BlendEquationSeparate(a, b);
}

void CachedBlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    ContextState& s = t_State;
    const std::uint32_t wanted[4] = {std::bit_cast<std::uint32_t>(r), std::bit_cast<std::uint32_t>(g), std::bit_cast<std::uint32_t>(b),
                                     std::bit_cast<std::uint32_t>(a)};
    if (s.on && s.blendColorKnown && std::equal(wanted, wanted + 4, s.blendColor))
    {
        return;
    }

    std::copy(wanted, wanted + 4, s.blendColor);
    s.blendColorKnown = s.on;
    g_Below.BlendColor(r, g, b, a);
}

void CachedStencilFunc(GLenum function, GLint reference, GLuint mask)
{
    ContextState& s = t_State;
    if (s.on && s.stencilFuncKnown && s.stencilFunc == function && s.stencilRef == reference && s.stencilFuncMask == mask)
    {
        return;
    }

    s.stencilFunc = function;
    s.stencilRef = reference;
    s.stencilFuncMask = mask;
    s.stencilFuncKnown = s.on;
    g_Below.StencilFunc(function, reference, mask);
}

void CachedStencilOp(GLenum fail, GLenum depthFail, GLenum pass)
{
    ContextState& s = t_State;
    if (s.on && s.stencilOpKnown && s.stencilOp[0] == fail && s.stencilOp[1] == depthFail && s.stencilOp[2] == pass)
    {
        return;
    }

    s.stencilOp[0] = fail;
    s.stencilOp[1] = depthFail;
    s.stencilOp[2] = pass;
    s.stencilOpKnown = s.on;
    g_Below.StencilOp(fail, depthFail, pass);
}

void CachedStencilMask(GLuint mask)
{
    ContextState& s = t_State;
    if (s.on && s.stencilMask == static_cast<long long>(mask))
    {
        return;
    }

    s.stencilMask = s.on ? static_cast<long long>(mask) : -1;
    g_Below.StencilMask(mask);
}

void CachedActiveTexture(GLenum unit)
{
    ContextState& s = t_State;
    const int index = static_cast<int>(unit - GL_TEXTURE0);
    if (s.on && s.activeUnit == index)
    {
        return;
    }

    s.activeUnit = s.on && index >= 0 && index < CachedUnits ? index : -1;
    g_Below.ActiveTexture(unit);
}

void CachedBindTexture(GLenum target, GLuint texture)
{
    ContextState& s = t_State;
    const bool tracked = s.on && target == GL_TEXTURE_2D && s.activeUnit >= 0;
    if (tracked && s.textures[s.activeUnit] == static_cast<long long>(texture))
    {
        return;
    }

    if (tracked)
    {
        s.textures[s.activeUnit] = texture;
    }
    else if (s.activeUnit >= 0 && target == GL_TEXTURE_2D)
    {
        s.textures[s.activeUnit] = -1;
    }

    g_Below.BindTexture(target, texture);
}

// The texture bound to the active unit, as far as it's known (0: none or unknown)
GLuint BoundTexture()
{
    const ContextState& s = t_State;
    if (!s.on || s.activeUnit < 0 || s.textures[s.activeUnit] <= 0)
    {
        return 0;
    }

    return static_cast<GLuint>(s.textures[s.activeUnit]);
}

int ParamIndex(GLenum name)
{
    switch (name)
    {
    case GL_TEXTURE_MIN_FILTER:
        return 0;
    case GL_TEXTURE_MAG_FILTER:
        return 1;
    case GL_TEXTURE_WRAP_S:
        return 2;
    case GL_TEXTURE_WRAP_T:
        return 3;
    case GL_TEXTURE_MAX_ANISOTROPY:
        return 4;
    default:
        return -1;
    }
}

// Whether a texture's parameter changes (kept as its own): unknown textures and other parameters always do
bool ParamChanged(GLenum target, GLenum name, GLint value)
{
    const GLuint texture = target == GL_TEXTURE_2D ? BoundTexture() : 0;
    const int index = ParamIndex(name);
    if (texture == 0 || index < 0 || texture >= (1u << 20))
    {
        return true;
    }

    std::vector<TextureParams>& params = t_State.params;
    if (params.size() <= texture)
    {
        params.resize(texture + 1);
    }

    TextureParams& p = params[texture];
    const bool same = (p.known >> index & 1) != 0 && p.value[index] == value;
    p.value[index] = value;
    p.known |= static_cast<std::uint8_t>(1u << index);
    return !same;
}

void CachedTexParameteri(GLenum target, GLenum name, GLint value)
{
    if (ParamChanged(target, name, value))
    {
        g_Below.TexParameteri(target, name, value);
    }
}

void CachedTexParameterf(GLenum target, GLenum name, GLfloat value)
{
    if (ParamChanged(target, name, std::bit_cast<GLint>(value)))
    {
        g_Below.TexParameterf(target, name, value);
    }
}

void CachedGenTextures(GLsizei count, GLuint* textures)
{
    g_Below.GenTextures(count, textures);
    std::vector<TextureParams>& params = t_State.params;
    for (GLsizei i = 0; i < count; i++)
    {
        if (textures[i] < params.size())
        {
            params[textures[i]] = {};
        }
    }
}

void CachedDeleteTextures(GLsizei count, const GLuint* textures)
{
    // (A bound texture deleted leaves its unit's binding 0)
    ContextState& s = t_State;
    for (GLsizei i = 0; i < count; i++)
    {
        if (textures[i] < s.params.size())
        {
            s.params[textures[i]] = {};
        }

        for (long long& bound : s.textures)
        {
            if (bound == static_cast<long long>(textures[i]))
            {
                bound = 0;
            }
        }
    }

    g_Below.DeleteTextures(count, textures);
}

void CachedBindFramebuffer(GLenum target, GLuint framebuffer)
{
    ContextState& s = t_State;
    const long long wanted = framebuffer;
    const bool draw = target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER;
    const bool read = target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER;
    if (s.on && (!draw || s.drawFramebuffer == wanted) && (!read || s.readFramebuffer == wanted))
    {
        return;
    }

    if (draw)
    {
        s.drawFramebuffer = s.on ? wanted : -1;
    }

    if (read)
    {
        s.readFramebuffer = s.on ? wanted : -1;
    }

    g_Below.BindFramebuffer(target, framebuffer);
}

void CachedDeleteFramebuffers(GLsizei count, const GLuint* framebuffers)
{
    ContextState& s = t_State;
    for (GLsizei i = 0; i < count; i++)
    {
        if (s.drawFramebuffer == static_cast<long long>(framebuffers[i]))
        {
            s.drawFramebuffer = 0;
        }

        if (s.readFramebuffer == static_cast<long long>(framebuffers[i]))
        {
            s.readFramebuffer = 0;
        }
    }

    g_Below.DeleteFramebuffers(count, framebuffers);
}

void CachedBindVertexArray(GLuint array)
{
    ContextState& s = t_State;
    if (s.on && s.vertexArray == static_cast<long long>(array))
    {
        return;
    }

    s.vertexArray = s.on ? static_cast<long long>(array) : -1;
    g_Below.BindVertexArray(array);
}

void CachedBindBuffer(GLenum target, GLuint buffer)
{
    ContextState& s = t_State;
    const bool tracked = s.on && target == GL_ARRAY_BUFFER;
    if (tracked && s.arrayBuffer == static_cast<long long>(buffer))
    {
        return;
    }

    if (target == GL_ARRAY_BUFFER)
    {
        s.arrayBuffer = tracked ? static_cast<long long>(buffer) : -1;
    }

    g_Below.BindBuffer(target, buffer);
}

void CachedUseProgram(GLuint program)
{
    ContextState& s = t_State;
    t_Program = program;
    if (s.on && s.program == static_cast<long long>(program))
    {
        return;
    }

    s.program = s.on ? static_cast<long long>(program) : -1;
    g_Below.UseProgram(program);
}

void CacheCalls()
{
    const char* caching = std::getenv("TWIN_HW_STATE_CACHE");
    g_Caching = caching == nullptr || caching[0] != '0';
    if (!g_Caching)
    {
        return;
    }

    g_Below = g_Gl;
    g_Gl.Uniform1i = CachedUniform1i;
    g_Gl.Uniform1ui = CachedUniform1ui;
    g_Gl.Uniform1f = CachedUniform1f;
    g_Gl.Uniform2f = CachedUniform2f;
    g_Gl.Uniform3f = CachedUniform3f;
    g_Gl.Uniform4f = CachedUniform4f;
    g_Gl.Uniform4ui = CachedUniform4ui;
    g_Gl.Uniform1iv = CachedUniform1iv;
    g_Gl.LinkProgram = CachedLinkProgram;
    g_Gl.Enable = CachedEnable;
    g_Gl.Disable = CachedDisable;
    g_Gl.Viewport = CachedViewport;
    g_Gl.Scissor = CachedScissor;
    g_Gl.ColorMask = CachedColorMask;
    g_Gl.DepthMask = CachedDepthMask;
    g_Gl.DepthFunc = CachedDepthFunc;
    g_Gl.BlendFuncSeparate = CachedBlendFuncSeparate;
    g_Gl.BlendEquationSeparate = CachedBlendEquationSeparate;
    g_Gl.BlendColor = CachedBlendColor;
    g_Gl.StencilFunc = CachedStencilFunc;
    g_Gl.StencilOp = CachedStencilOp;
    g_Gl.StencilMask = CachedStencilMask;
    g_Gl.ActiveTexture = CachedActiveTexture;
    g_Gl.BindTexture = CachedBindTexture;
    g_Gl.TexParameteri = CachedTexParameteri;
    g_Gl.TexParameterf = CachedTexParameterf;
    g_Gl.GenTextures = CachedGenTextures;
    g_Gl.DeleteTextures = CachedDeleteTextures;
    g_Gl.BindFramebuffer = CachedBindFramebuffer;
    g_Gl.DeleteFramebuffers = CachedDeleteFramebuffers;
    g_Gl.BindVertexArray = CachedBindVertexArray;
    g_Gl.BindBuffer = CachedBindBuffer;
    g_Gl.UseProgram = CachedUseProgram;
}

// The entry points into a table (with a context current)
bool LoadTable(Gl& gl)
{
    bool ok = true;
    ok &= Load(gl.GenTextures, "glGenTextures");
    ok &= Load(gl.DeleteTextures, "glDeleteTextures");
    ok &= Load(gl.BindTexture, "glBindTexture");
    ok &= Load(gl.TexImage2D, "glTexImage2D");
    ok &= Load(gl.TexSubImage2D, "glTexSubImage2D");
    ok &= Load(gl.TexParameteri, "glTexParameteri");
    ok &= Load(gl.ActiveTexture, "glActiveTexture");
    ok &= Load(gl.PixelStorei, "glPixelStorei");
    ok &= Load(gl.GenFramebuffers, "glGenFramebuffers");
    ok &= Load(gl.DeleteFramebuffers, "glDeleteFramebuffers");
    ok &= Load(gl.BindFramebuffer, "glBindFramebuffer");
    ok &= Load(gl.FramebufferTexture2D, "glFramebufferTexture2D");
    ok &= Load(gl.CheckFramebufferStatus, "glCheckFramebufferStatus");
    ok &= Load(gl.BlitFramebuffer, "glBlitFramebuffer");
    ok &= Load(gl.ReadPixels, "glReadPixels");
    ok &= Load(gl.ReadBuffer, "glReadBuffer");
    ok &= Load(gl.DrawBuffer, "glDrawBuffer");
    ok &= Load(gl.Viewport, "glViewport");
    ok &= Load(gl.Scissor, "glScissor");
    ok &= Load(gl.Enable, "glEnable");
    ok &= Load(gl.Disable, "glDisable");
    ok &= Load(gl.BlendFuncSeparate, "glBlendFuncSeparate");
    ok &= Load(gl.BlendEquationSeparate, "glBlendEquationSeparate");
    ok &= Load(gl.BlendColor, "glBlendColor");
    ok &= Load(gl.ColorMask, "glColorMask");
    ok &= Load(gl.DepthMask, "glDepthMask");
    ok &= Load(gl.DepthFunc, "glDepthFunc");
    ok &= Load(gl.Clear, "glClear");
    ok &= Load(gl.ClearColor, "glClearColor");
    ok &= Load(gl.CreateShader, "glCreateShader");
    ok &= Load(gl.ShaderSource, "glShaderSource");
    ok &= Load(gl.CompileShader, "glCompileShader");
    ok &= Load(gl.GetShaderiv, "glGetShaderiv");
    ok &= Load(gl.GetShaderInfoLog, "glGetShaderInfoLog");
    ok &= Load(gl.DeleteShader, "glDeleteShader");
    ok &= Load(gl.CreateProgram, "glCreateProgram");
    ok &= Load(gl.AttachShader, "glAttachShader");
    ok &= Load(gl.BindFragDataLocationIndexed, "glBindFragDataLocationIndexed");
    ok &= Load(gl.BindAttribLocation, "glBindAttribLocation");
    ok &= Load(gl.LinkProgram, "glLinkProgram");
    ok &= Load(gl.GetProgramiv, "glGetProgramiv");
    ok &= Load(gl.GetProgramInfoLog, "glGetProgramInfoLog");
    ok &= Load(gl.UseProgram, "glUseProgram");
    ok &= Load(gl.GetUniformLocation, "glGetUniformLocation");
    ok &= Load(gl.Uniform1i, "glUniform1i");
    ok &= Load(gl.Uniform1f, "glUniform1f");
    ok &= Load(gl.Uniform2f, "glUniform2f");
    ok &= Load(gl.Uniform3f, "glUniform3f");
    ok &= Load(gl.Uniform4f, "glUniform4f");
    ok &= Load(gl.Uniform1iv, "glUniform1iv");
    ok &= Load(gl.GenVertexArrays, "glGenVertexArrays");
    ok &= Load(gl.BindVertexArray, "glBindVertexArray");
    ok &= Load(gl.GenBuffers, "glGenBuffers");
    ok &= Load(gl.BindBuffer, "glBindBuffer");
    ok &= Load(gl.BufferData, "glBufferData");
    ok &= Load(gl.VertexAttribPointer, "glVertexAttribPointer");
    ok &= Load(gl.VertexAttribIPointer, "glVertexAttribIPointer");
    ok &= Load(gl.EnableVertexAttribArray, "glEnableVertexAttribArray");
    ok &= Load(gl.DrawArrays, "glDrawArrays");
    ok &= Load(gl.Finish, "glFinish");
    ok &= Load(gl.GetError, "glGetError");
    ok &= Load(gl.GetIntegerv, "glGetIntegerv");
    ok &= Load(gl.PointSize, "glPointSize");
    ok &= Load(gl.LineWidth, "glLineWidth");
    ok &= Load(gl.Uniform4ui, "glUniform4ui");
    ok &= Load(gl.Uniform1ui, "glUniform1ui");
    ok &= Load(gl.StencilFunc, "glStencilFunc");
    ok &= Load(gl.TexParameterf, "glTexParameterf");
    ok &= Load(gl.GenerateMipmap, "glGenerateMipmap");
    ok &= Load(gl.StencilOp, "glStencilOp");
    ok &= Load(gl.StencilMask, "glStencilMask");
    ok &= Load(gl.ClearStencil, "glClearStencil");
    ok &= Load(gl.Flush, "glFlush");
    ok &= Load(gl.FenceSync, "glFenceSync");
    ok &= Load(gl.WaitSync, "glWaitSync");
    ok &= Load(gl.DeleteSync, "glDeleteSync");
    // (Not needed: hwgs.cpp's programs made at the start compiled on the driver's threads where it has them)
    gl.MaxShaderCompilerThreads = nullptr;
    if (SDL_GL_ExtensionSupported("GL_KHR_parallel_shader_compile"))
    {
        Load(gl.MaxShaderCompilerThreads, "glMaxShaderCompilerThreadsKHR");
    }
    else if (SDL_GL_ExtensionSupported("GL_ARB_parallel_shader_compile"))
    {
        Load(gl.MaxShaderCompilerThreads, "glMaxShaderCompilerThreadsARB");
    }

    ok &= Load(gl.CompressedTexImage2D, "glCompressedTexImage2D");
    return ok;
}
}

bool LoadGl()
{
    if (g_Loaded)
    {
        return true;
    }

    bool ok = LoadTable(g_Gl);
    if (ok)
    {
        CountCalls();
    }

    const char* caching = std::getenv("TWIN_HW_UNIFORM_CACHE");
    if (ok && (caching == nullptr || caching[0] != '0'))
    {
        g_GetUniformLocation = g_Gl.GetUniformLocation;
        g_Gl.GetUniformLocation = CachedUniformLocation;
    }

    if (ok)
    {
        CacheCalls();
    }

    g_Loaded = ok;
    return ok;
}

Gl& GetGl()
{
    return g_Gl;
}

bool LoadDirectGl(Gl& gl)
{
    return LoadTable(gl);
}

void CacheGlState(bool own)
{
    t_State.on = g_Caching && own;
    Forget(t_State);
}

void ForgetGlState()
{
    Forget(t_State);
}

bool CountingGlCalls()
{
    return g_CountingCalls;
}

const std::vector<GlCallCount>& GlCallCounts()
{
    return g_CallCounts;
}
}
