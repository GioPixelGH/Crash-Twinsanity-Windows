#include "gl.h"

#include "graphics/gputime.h"
#include "graphics/renderwork.h"

#include <SDL3/SDL.h>

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

template <auto Member>
struct Counted;

template <typename R, typename... A, R (*Gl::*Member)(A...)>
struct Counted<Member>
{
    template <NativeGraphics::Work Kind>
    static R Call(A... arguments)
    {
        if (NativeGraphics::t_Work != NativeGraphics::Work::Gs)
        {
            return (g_Real.*Member)(arguments...);
        }

        NativeGraphics::WorkSpan span(Kind);
        return (g_Real.*Member)(arguments...);
    }
};

void CountCalls()
{
    using NativeGraphics::Work;
    g_Real = g_Gl;
#define TWIN_COUNTED(name, kind) g_Gl.name = &Counted<&Gl::name>::Call<Work::kind>
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
}

bool LoadGl()
{
    if (g_Loaded)
    {
        return true;
    }

    bool ok = true;
    ok &= Load(g_Gl.GenTextures, "glGenTextures");
    ok &= Load(g_Gl.DeleteTextures, "glDeleteTextures");
    ok &= Load(g_Gl.BindTexture, "glBindTexture");
    ok &= Load(g_Gl.TexImage2D, "glTexImage2D");
    ok &= Load(g_Gl.TexSubImage2D, "glTexSubImage2D");
    ok &= Load(g_Gl.TexParameteri, "glTexParameteri");
    ok &= Load(g_Gl.ActiveTexture, "glActiveTexture");
    ok &= Load(g_Gl.PixelStorei, "glPixelStorei");
    ok &= Load(g_Gl.GenFramebuffers, "glGenFramebuffers");
    ok &= Load(g_Gl.DeleteFramebuffers, "glDeleteFramebuffers");
    ok &= Load(g_Gl.BindFramebuffer, "glBindFramebuffer");
    ok &= Load(g_Gl.FramebufferTexture2D, "glFramebufferTexture2D");
    ok &= Load(g_Gl.CheckFramebufferStatus, "glCheckFramebufferStatus");
    ok &= Load(g_Gl.BlitFramebuffer, "glBlitFramebuffer");
    ok &= Load(g_Gl.ReadPixels, "glReadPixels");
    ok &= Load(g_Gl.ReadBuffer, "glReadBuffer");
    ok &= Load(g_Gl.DrawBuffer, "glDrawBuffer");
    ok &= Load(g_Gl.Viewport, "glViewport");
    ok &= Load(g_Gl.Scissor, "glScissor");
    ok &= Load(g_Gl.Enable, "glEnable");
    ok &= Load(g_Gl.Disable, "glDisable");
    ok &= Load(g_Gl.BlendFuncSeparate, "glBlendFuncSeparate");
    ok &= Load(g_Gl.BlendEquationSeparate, "glBlendEquationSeparate");
    ok &= Load(g_Gl.BlendColor, "glBlendColor");
    ok &= Load(g_Gl.ColorMask, "glColorMask");
    ok &= Load(g_Gl.DepthMask, "glDepthMask");
    ok &= Load(g_Gl.DepthFunc, "glDepthFunc");
    ok &= Load(g_Gl.Clear, "glClear");
    ok &= Load(g_Gl.ClearColor, "glClearColor");
    ok &= Load(g_Gl.CreateShader, "glCreateShader");
    ok &= Load(g_Gl.ShaderSource, "glShaderSource");
    ok &= Load(g_Gl.CompileShader, "glCompileShader");
    ok &= Load(g_Gl.GetShaderiv, "glGetShaderiv");
    ok &= Load(g_Gl.GetShaderInfoLog, "glGetShaderInfoLog");
    ok &= Load(g_Gl.DeleteShader, "glDeleteShader");
    ok &= Load(g_Gl.CreateProgram, "glCreateProgram");
    ok &= Load(g_Gl.AttachShader, "glAttachShader");
    ok &= Load(g_Gl.BindFragDataLocationIndexed, "glBindFragDataLocationIndexed");
    ok &= Load(g_Gl.BindAttribLocation, "glBindAttribLocation");
    ok &= Load(g_Gl.LinkProgram, "glLinkProgram");
    ok &= Load(g_Gl.GetProgramiv, "glGetProgramiv");
    ok &= Load(g_Gl.GetProgramInfoLog, "glGetProgramInfoLog");
    ok &= Load(g_Gl.UseProgram, "glUseProgram");
    ok &= Load(g_Gl.GetUniformLocation, "glGetUniformLocation");
    ok &= Load(g_Gl.Uniform1i, "glUniform1i");
    ok &= Load(g_Gl.Uniform1f, "glUniform1f");
    ok &= Load(g_Gl.Uniform2f, "glUniform2f");
    ok &= Load(g_Gl.Uniform3f, "glUniform3f");
    ok &= Load(g_Gl.Uniform4f, "glUniform4f");
    ok &= Load(g_Gl.Uniform1iv, "glUniform1iv");
    ok &= Load(g_Gl.GenVertexArrays, "glGenVertexArrays");
    ok &= Load(g_Gl.BindVertexArray, "glBindVertexArray");
    ok &= Load(g_Gl.GenBuffers, "glGenBuffers");
    ok &= Load(g_Gl.BindBuffer, "glBindBuffer");
    ok &= Load(g_Gl.BufferData, "glBufferData");
    ok &= Load(g_Gl.VertexAttribPointer, "glVertexAttribPointer");
    ok &= Load(g_Gl.VertexAttribIPointer, "glVertexAttribIPointer");
    ok &= Load(g_Gl.EnableVertexAttribArray, "glEnableVertexAttribArray");
    ok &= Load(g_Gl.DrawArrays, "glDrawArrays");
    ok &= Load(g_Gl.Finish, "glFinish");
    ok &= Load(g_Gl.GetError, "glGetError");
    ok &= Load(g_Gl.GetIntegerv, "glGetIntegerv");
    ok &= Load(g_Gl.PointSize, "glPointSize");
    ok &= Load(g_Gl.LineWidth, "glLineWidth");
    ok &= Load(g_Gl.Uniform4ui, "glUniform4ui");
    ok &= Load(g_Gl.Uniform1ui, "glUniform1ui");
    ok &= Load(g_Gl.StencilFunc, "glStencilFunc");
    ok &= Load(g_Gl.TexParameterf, "glTexParameterf");
    ok &= Load(g_Gl.GenerateMipmap, "glGenerateMipmap");
    ok &= Load(g_Gl.StencilOp, "glStencilOp");
    ok &= Load(g_Gl.StencilMask, "glStencilMask");
    ok &= Load(g_Gl.ClearStencil, "glClearStencil");
    ok &= Load(g_Gl.Flush, "glFlush");
    ok &= Load(g_Gl.FenceSync, "glFenceSync");
    ok &= Load(g_Gl.WaitSync, "glWaitSync");
    ok &= Load(g_Gl.DeleteSync, "glDeleteSync");
    // (Not needed: hwgs.cpp's programs made at the start compiled on the driver's threads where it has them)
    g_Gl.MaxShaderCompilerThreads = nullptr;
    if (SDL_GL_ExtensionSupported("GL_KHR_parallel_shader_compile"))
    {
        Load(g_Gl.MaxShaderCompilerThreads, "glMaxShaderCompilerThreadsKHR");
    }
    else if (SDL_GL_ExtensionSupported("GL_ARB_parallel_shader_compile"))
    {
        Load(g_Gl.MaxShaderCompilerThreads, "glMaxShaderCompilerThreadsARB");
    }

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

    g_Loaded = ok;
    return ok;
}

Gl& GetGl()
{
    return g_Gl;
}
}
