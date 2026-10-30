#include "gl.h"

#include "graphics/gputime.h"

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
