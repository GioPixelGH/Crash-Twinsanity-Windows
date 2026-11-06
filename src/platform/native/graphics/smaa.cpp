#include "smaa.h"

#include "gputime.h"
#include "native.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>

// iryoku's SMAA (MIT): the shader and its two textures, as published (native/third_party/smaa/README.md)
#include "../../../../native/third_party/smaa/AreaTex.h"
#include "../../../../native/third_party/smaa/SearchTex.h"
#include "../../../../native/third_party/smaa/SMAA.hlsl.inc"

namespace NativeGraphics
{
namespace
{
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLchar = char;
using GLbitfield = unsigned int;
using GLfloat = float;

constexpr GLenum GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800,
                 GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803, GL_CLAMP_TO_EDGE = 0x812F, GL_LINEAR = 0x2601,
                 GL_NEAREST = 0x2600, GL_RGBA = 0x1908, GL_RGBA8 = 0x8058, GL_RG = 0x8227, GL_RG8 = 0x822B, GL_RED = 0x1903,
                 GL_R8 = 0x8229, GL_UNSIGNED_BYTE = 0x1401, GL_FRAMEBUFFER = 0x8D40, GL_COLOR_ATTACHMENT0 = 0x8CE0,
                 GL_FRAMEBUFFER_COMPLETE = 0x8CD5, GL_COLOR_BUFFER_BIT = 0x4000, GL_TEXTURE0 = 0x84C0,
                 GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30, GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82,
                 GL_UNPACK_ALIGNMENT = 0x0CF5, GL_UNPACK_ROW_LENGTH = 0x0CF2, GL_BLEND = 0x0BE2, GL_DEPTH_TEST = 0x0B71,
                 GL_SCISSOR_TEST = 0x0C11, GL_STENCIL_TEST = 0x0B90, GL_TRIANGLE_STRIP = 0x0005;

struct Gl
{
    void (*GenTextures)(GLsizei, GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*ActiveTexture)(GLenum);
    void (*PixelStorei)(GLenum, GLint);
    void (*GenFramebuffers)(GLsizei, GLuint*);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (*CheckFramebufferStatus)(GLenum);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*Disable)(GLenum);
    void (*ColorMask)(unsigned char, unsigned char, unsigned char, unsigned char);
    void (*Clear)(GLbitfield);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (*CompileShader)(GLuint);
    void (*GetShaderiv)(GLuint, GLenum, GLint*);
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (*DeleteShader)(GLuint);
    GLuint (*CreateProgram)();
    void (*AttachShader)(GLuint, GLuint);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint, GLenum, GLint*);
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (*UseProgram)(GLuint);
    GLint (*GetUniformLocation)(GLuint, const GLchar*);
    void (*Uniform1i)(GLint, GLint);
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*GenVertexArrays)(GLsizei, GLuint*);
    void (*BindVertexArray)(GLuint);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
};

template <typename T>
bool Load(T& function, const char* name)
{
    // (The card's work timed as the presenter's is: gputime.h)
    void* timed = TimedEntryPoint(name);
    function = reinterpret_cast<T>(timed != nullptr ? timed : reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)));
    return function != nullptr;
}

bool LoadGl(Gl& gl)
{
    bool ok = Load(gl.GenTextures, "glGenTextures");
    ok &= Load(gl.BindTexture, "glBindTexture");
    ok &= Load(gl.TexImage2D, "glTexImage2D");
    ok &= Load(gl.TexParameteri, "glTexParameteri");
    ok &= Load(gl.ActiveTexture, "glActiveTexture");
    ok &= Load(gl.PixelStorei, "glPixelStorei");
    ok &= Load(gl.GenFramebuffers, "glGenFramebuffers");
    ok &= Load(gl.BindFramebuffer, "glBindFramebuffer");
    ok &= Load(gl.FramebufferTexture2D, "glFramebufferTexture2D");
    ok &= Load(gl.CheckFramebufferStatus, "glCheckFramebufferStatus");
    ok &= Load(gl.Viewport, "glViewport");
    ok &= Load(gl.Disable, "glDisable");
    ok &= Load(gl.ColorMask, "glColorMask");
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
    ok &= Load(gl.LinkProgram, "glLinkProgram");
    ok &= Load(gl.GetProgramiv, "glGetProgramiv");
    ok &= Load(gl.GetProgramInfoLog, "glGetProgramInfoLog");
    ok &= Load(gl.UseProgram, "glUseProgram");
    ok &= Load(gl.GetUniformLocation, "glGetUniformLocation");
    ok &= Load(gl.Uniform1i, "glUniform1i");
    ok &= Load(gl.Uniform4f, "glUniform4f");
    ok &= Load(gl.GenVertexArrays, "glGenVertexArrays");
    ok &= Load(gl.BindVertexArray, "glBindVertexArray");
    ok &= Load(gl.DrawArrays, "glDrawArrays");
    return ok;
}

// Each pass's program: SMAA.hlsl as GLSL 3.30 (SMAA_GLSL_3, the high preset), its vertex part (a quad over the target from the
// vertex number, the texture's coordinates as the target's rows: row 0 the picture's top in both) and its pixel part
constexpr const char* Prelude = "#version 330 core\n"
                                "#define SMAA_GLSL_3 1\n"
                                "#define SMAA_PRESET_HIGH 1\n"
                                "#define SMAA_RT_METRICS rtMetrics\n"
                                "uniform vec4 rtMetrics;\n";

constexpr const char* QuadVertex = "    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
                                   "    vTexcoord = corner;\n"
                                   "    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n";

struct Pass
{
    const char* vertexDeclarations;
    const char* vertexCall;
    const char* fragmentDeclarations;
    const char* fragmentBody;
};

constexpr Pass EdgesPass = {
    "out vec2 vTexcoord;\nout vec4 vOffset[3];\n",
    "    SMAAEdgeDetectionVS(vTexcoord, vOffset);\n",
    "in vec2 vTexcoord;\nin vec4 vOffset[3];\nuniform sampler2D colorTex;\nlayout(location = 0) out vec4 fragColor;\n",
    "    fragColor = vec4(SMAALumaEdgeDetectionPS(vTexcoord, vOffset, colorTex), 0.0, 0.0);\n",
};

constexpr Pass WeightsPass = {
    "out vec2 vTexcoord;\nout vec2 vPixcoord;\nout vec4 vOffset[3];\n",
    "    SMAABlendingWeightCalculationVS(vTexcoord, vPixcoord, vOffset);\n",
    "in vec2 vTexcoord;\nin vec2 vPixcoord;\nin vec4 vOffset[3];\nuniform sampler2D edgesTex;\nuniform sampler2D areaTex;\n"
    "uniform sampler2D searchTex;\nlayout(location = 0) out vec4 fragColor;\n",
    "    fragColor = SMAABlendingWeightCalculationPS(vTexcoord, vPixcoord, vOffset, edgesTex, areaTex, searchTex, vec4(0.0));\n",
};

constexpr Pass BlendPass = {
    "out vec2 vTexcoord;\nout vec4 vOffset;\n",
    "    SMAANeighborhoodBlendingVS(vTexcoord, vOffset);\n",
    "in vec2 vTexcoord;\nin vec4 vOffset;\nuniform sampler2D colorTex;\nuniform sampler2D blendTex;\n"
    "layout(location = 0) out vec4 fragColor;\n",
    "    fragColor = SMAANeighborhoodBlendingPS(vTexcoord, vOffset, colorTex, blendTex);\n",
};

// The picture's rectangle copied into a texture of its own (rows as they are)
constexpr const char* CopyVertex = "#version 330 core\n"
                                   "out vec2 vTexcoord;\n"
                                   "uniform vec4 uvRect;\n"
                                   "void main()\n"
                                   "{\n"
                                   "    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);\n"
                                   "    vTexcoord = mix(uvRect.xy, uvRect.zw, corner);\n"
                                   "    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
                                   "}\n";
constexpr const char* CopyFragment = "#version 330 core\n"
                                     "in vec2 vTexcoord;\n"
                                     "uniform sampler2D picture;\n"
                                     "layout(location = 0) out vec4 fragColor;\n"
                                     "void main()\n"
                                     "{\n"
                                     "    fragColor = vec4(texture(picture, vTexcoord).rgb, 1.0);\n"
                                     "}\n";

struct Smaa
{
    Gl gl = {};
    bool tried = false;
    bool usable = false;
    GLuint copyProgram = 0;
    GLuint edgesProgram = 0;
    GLuint weightsProgram = 0;
    GLuint blendProgram = 0;
    GLuint vertexArray = 0;
    GLuint areaTexture = 0;
    GLuint searchTexture = 0;
    // The picture's copy, the edges, the weights and the result, and a framebuffer for each
    GLuint textures[4] = {};
    GLuint framebuffers[4] = {};
    u32 width = 0;
    u32 height = 0;
};
Smaa g_Smaa;

GLuint Compile(Gl& gl, GLenum kind, const std::string& source)
{
    GLuint shader = gl.CreateShader(kind);
    const char* text = source.c_str();
    gl.ShaderSource(shader, 1, &text, nullptr);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = "";
        gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        Native::Log("graphics: SMAA's %s shader didn't compile: %s", kind == GL_VERTEX_SHADER ? "vertex" : "pixel", log);
        gl.DeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint Link(Gl& gl, const std::string& vertex, const std::string& fragment)
{
    GLuint vertexShader = Compile(gl, GL_VERTEX_SHADER, vertex);
    GLuint fragmentShader = Compile(gl, GL_FRAGMENT_SHADER, fragment);
    if (vertexShader == 0 || fragmentShader == 0)
    {
        return 0;
    }

    GLuint program = gl.CreateProgram();
    gl.AttachShader(program, vertexShader);
    gl.AttachShader(program, fragmentShader);
    gl.LinkProgram(program);
    GLint ok = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &ok);
    gl.DeleteShader(vertexShader);
    gl.DeleteShader(fragmentShader);
    if (!ok)
    {
        char log[1024] = "";
        gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
        Native::Log("graphics: SMAA's program didn't link: %s", log);
        return 0;
    }

    return program;
}

GLuint PassProgram(Gl& gl, const Pass& pass)
{
    std::string vertex = std::string(Prelude) + "#define SMAA_INCLUDE_VS 1\n#define SMAA_INCLUDE_PS 0\n" + SmaaHlsl + "\n" +
                         pass.vertexDeclarations + "void main()\n{\n" + QuadVertex + pass.vertexCall + "}\n";
    std::string fragment = std::string(Prelude) + "#define SMAA_INCLUDE_VS 0\n#define SMAA_INCLUDE_PS 1\n" + SmaaHlsl + "\n" +
                           pass.fragmentDeclarations + "void main()\n{\n" + pass.fragmentBody + "}\n";
    return Link(gl, vertex, fragment);
}

void Sampling(Gl& gl, GLint filter)
{
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
}

bool Prepare(Smaa& s)
{
    if (s.tried)
    {
        return s.usable;
    }

    s.tried = true;
    Gl& gl = s.gl;
    if (!LoadGl(gl))
    {
        Native::Log("graphics: SMAA can't run: OpenGL's entry points missing");
        return false;
    }

    s.copyProgram = Link(gl, CopyVertex, CopyFragment);
    s.edgesProgram = PassProgram(gl, EdgesPass);
    s.weightsProgram = PassProgram(gl, WeightsPass);
    s.blendProgram = PassProgram(gl, BlendPass);
    if (s.copyProgram == 0 || s.edgesProgram == 0 || s.weightsProgram == 0 || s.blendProgram == 0)
    {
        return false;
    }

    gl.GenVertexArrays(1, &s.vertexArray);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl.GenTextures(1, &s.areaTexture);
    gl.BindTexture(GL_TEXTURE_2D, s.areaTexture);
    Sampling(gl, static_cast<GLint>(GL_LINEAR));
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RG8), AREATEX_WIDTH, AREATEX_HEIGHT, 0, GL_RG, GL_UNSIGNED_BYTE,
                  areaTexBytes);
    gl.GenTextures(1, &s.searchTexture);
    gl.BindTexture(GL_TEXTURE_2D, s.searchTexture);
    Sampling(gl, static_cast<GLint>(GL_NEAREST));
    gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R8), SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 0, GL_RED, GL_UNSIGNED_BYTE,
                  searchTexBytes);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.GenTextures(4, s.textures);
    gl.GenFramebuffers(4, s.framebuffers);
    s.usable = true;
    Native::Log("graphics: SMAA 1x (iryoku's, the high preset) ready");
    return true;
}

// The pass targets at the picture's size: its copy (RGBA), the edges (RG), the weights (RGBA) and the result (RGBA)
bool Size(Smaa& s, u32 width, u32 height)
{
    if (s.width == width && s.height == height)
    {
        return true;
    }

    Gl& gl = s.gl;
    const GLenum formats[4][2] = {{GL_RGBA8, GL_RGBA}, {GL_RG8, GL_RG}, {GL_RGBA8, GL_RGBA}, {GL_RGBA8, GL_RGBA}};
    for (u32 i = 0; i < 4; i++)
    {
        gl.BindTexture(GL_TEXTURE_2D, s.textures[i]);
        Sampling(gl, static_cast<GLint>(GL_LINEAR));
        gl.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(formats[i][0]), static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                      formats[i][1], GL_UNSIGNED_BYTE, nullptr);
        gl.BindFramebuffer(GL_FRAMEBUFFER, s.framebuffers[i]);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.textures[i], 0);
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            Native::Log("graphics: SMAA's targets can't be made at %ux%u", width, height);
            s.usable = false;
            return false;
        }
    }

    s.width = width;
    s.height = height;
    return true;
}

void Bind(Gl& gl, GLuint program, const char* name, u32 unit, GLuint texture)
{
    gl.ActiveTexture(GL_TEXTURE0 + unit);
    gl.BindTexture(GL_TEXTURE_2D, texture);
    gl.Uniform1i(gl.GetUniformLocation(program, name), static_cast<GLint>(unit));
}

void Target(Smaa& s, u32 index, GLuint program, bool clear)
{
    Gl& gl = s.gl;
    gl.BindFramebuffer(GL_FRAMEBUFFER, s.framebuffers[index]);
    gl.Viewport(0, 0, static_cast<GLsizei>(s.width), static_cast<GLsizei>(s.height));
    if (clear)
    {
        gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gl.Clear(GL_COLOR_BUFFER_BIT);
    }

    gl.UseProgram(program);
    GLint metrics = gl.GetUniformLocation(program, "rtMetrics");
    if (metrics >= 0)
    {
        gl.Uniform4f(metrics, 1.0f / static_cast<f32>(s.width), 1.0f / static_cast<f32>(s.height), static_cast<f32>(s.width),
                     static_cast<f32>(s.height));
    }
}
}

unsigned int SmaaPicture(unsigned int texture, f32 u0, f32 v0, f32 u1, f32 v1, u32 width, u32 height)
{
    Smaa& s = g_Smaa;
    if (width == 0 || height == 0 || !Prepare(s) || !s.usable || !Size(s, width, height))
    {
        return 0;
    }

    Gl& gl = s.gl;
    gl.Disable(GL_BLEND);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.ColorMask(1, 1, 1, 1);
    gl.BindVertexArray(s.vertexArray);
    enum
    {
        Copy,
        Edges,
        Weights,
        Result,
    };

    // The picture's rectangle into a texture of its own
    Target(s, Copy, s.copyProgram, false);
    gl.Uniform4f(gl.GetUniformLocation(s.copyProgram, "uvRect"), u0, v0, u1, v1);
    Bind(gl, s.copyProgram, "picture", 0, texture);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    // The edges (the pixels without one discarded: cleared first), the blending weights, the neighbours blended
    Target(s, Edges, s.edgesProgram, true);
    Bind(gl, s.edgesProgram, "colorTex", 0, s.textures[Copy]);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    Target(s, Weights, s.weightsProgram, true);
    Bind(gl, s.weightsProgram, "edgesTex", 0, s.textures[Edges]);
    Bind(gl, s.weightsProgram, "areaTex", 1, s.areaTexture);
    Bind(gl, s.weightsProgram, "searchTex", 2, s.searchTexture);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    Target(s, Result, s.blendProgram, false);
    Bind(gl, s.blendProgram, "colorTex", 0, s.textures[Copy]);
    Bind(gl, s.blendProgram, "blendTex", 1, s.textures[Weights]);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    return s.textures[Result];
}
}
