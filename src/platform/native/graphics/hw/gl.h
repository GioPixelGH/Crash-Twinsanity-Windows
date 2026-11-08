#pragma once

// The OpenGL 3.3 core entry points the hardware renderer uses, loaded through SDL (nothing of macOS's or Linux's own)

#include <cstddef>
#include <cstdint>

namespace Hw
{
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLchar = char;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLsizeiptr = std::ptrdiff_t;
using GLuint64 = std::uint64_t;
using GLsync = struct GLsyncObject*;
constexpr GLenum GL_SYNC_GPU_COMMANDS_COMPLETE = 0x9117;
constexpr GLuint64 GL_TIMEOUT_IGNORED = ~GLuint64{0};
constexpr GLenum GL_NO_ERROR = 0, GL_OUT_OF_MEMORY = 0x0505, GL_MAX_TEXTURE_SIZE = 0x0D33;

constexpr GLenum GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800,
                 GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803, GL_CLAMP_TO_EDGE = 0x812F, GL_REPEAT = 0x2901,
                 GL_LINEAR = 0x2601, GL_NEAREST = 0x2600, GL_NEAREST_MIPMAP_NEAREST = 0x2700, GL_LINEAR_MIPMAP_NEAREST = 0x2701,
                 GL_NEAREST_MIPMAP_LINEAR = 0x2702, GL_LINEAR_MIPMAP_LINEAR = 0x2703, GL_TEXTURE_BASE_LEVEL = 0x813C,
                 GL_TEXTURE_MAX_LEVEL = 0x813D, GL_RGBA = 0x1908, GL_RGBA8 = 0x8058, GL_RED = 0x1903, GL_R32F = 0x822E,
                 GL_UNSIGNED_BYTE = 0x1401, GL_FLOAT = 0x1406, GL_UNSIGNED_INT = 0x1405, GL_DEPTH_STENCIL = 0x84F9,
                 GL_DEPTH_COMPONENT = 0x1902, GL_DEPTH32F_STENCIL8 = 0x8CAD, GL_FLOAT_32_UNSIGNED_INT_24_8_REV = 0x8DAD,
                 GL_FRAMEBUFFER = 0x8D40, GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9,
                 GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_DEPTH_STENCIL_ATTACHMENT = 0x821A, GL_FRAMEBUFFER_COMPLETE = 0x8CD5,
                 GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x100, GL_STENCIL_BUFFER_BIT = 0x400, GL_NONE = 0,
                 GL_BLEND = 0x0BE2, GL_DEPTH_TEST = 0x0B71, GL_SCISSOR_TEST = 0x0C11, GL_STENCIL_TEST = 0x0B90,
                 GL_CULL_FACE = 0x0B44, GL_PROGRAM_POINT_SIZE = 0x8642, GL_FUNC_ADD = 0x8006, GL_FUNC_SUBTRACT = 0x800A,
                 GL_FUNC_REVERSE_SUBTRACT = 0x800B, GL_ZERO = 0, GL_ONE = 1, GL_SRC1_ALPHA = 0x8589,
                 GL_ONE_MINUS_SRC1_ALPHA = 0x88FB, GL_CONSTANT_ALPHA = 0x8003, GL_ONE_MINUS_CONSTANT_ALPHA = 0x8004,
                 GL_ALWAYS = 0x0207, GL_GEQUAL = 0x0206, GL_GREATER = 0x0204, GL_NEVER = 0x0200, GL_LESS = 0x0201, GL_LEQUAL = 0x0203,
                 GL_TRIANGLES = 0x0004, GL_LINES = 0x0001, GL_POINTS = 0x0000, GL_TRIANGLE_STRIP = 0x0005,
                 GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30, GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82,
                 GL_TEXTURE0 = 0x84C0, GL_ARRAY_BUFFER = 0x8892, GL_PIXEL_UNPACK_BUFFER = 0x88EC, GL_STREAM_DRAW = 0x88E0, GL_UNPACK_ROW_LENGTH = 0x0CF2,
                 GL_PACK_ROW_LENGTH = 0x0D02, GL_UNPACK_ALIGNMENT = 0x0CF5, GL_PACK_ALIGNMENT = 0x0D05, GL_BACK = 0x0405,
                 GL_TRUE = 1, GL_FALSE = 0, GL_KEEP = 0x1E00, GL_R32UI = 0x8236, GL_R16UI = 0x8234, GL_RED_INTEGER = 0x8D94, GL_UNSIGNED_SHORT = 0x1403, GL_RGBA16F = 0x881A, GL_HALF_FLOAT = 0x140B, GL_TEXTURE_MAX_ANISOTROPY = 0x84FE, GL_REPLACE = 0x1E01, GL_EQUAL = 0x0202;

struct Gl
{
    void (*GenTextures)(GLsizei, GLuint*);
    void (*DeleteTextures)(GLsizei, const GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*ActiveTexture)(GLenum);
    void (*PixelStorei)(GLenum, GLint);
    void (*GenFramebuffers)(GLsizei, GLuint*);
    void (*DeleteFramebuffers)(GLsizei, const GLuint*);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (*CheckFramebufferStatus)(GLenum);
    void (*BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void (*ReadBuffer)(GLenum);
    void (*DrawBuffer)(GLenum);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*Scissor)(GLint, GLint, GLsizei, GLsizei);
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum);
    void (*BlendEquationSeparate)(GLenum, GLenum);
    void (*BlendColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*ColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
    void (*DepthMask)(GLboolean);
    void (*DepthFunc)(GLenum);
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
    void (*BindFragDataLocationIndexed)(GLuint, GLuint, GLuint, const GLchar*);
    void (*BindAttribLocation)(GLuint, GLuint, const GLchar*);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint, GLenum, GLint*);
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (*UseProgram)(GLuint);
    GLint (*GetUniformLocation)(GLuint, const GLchar*);
    void (*Uniform1i)(GLint, GLint);
    void (*Uniform1f)(GLint, GLfloat);
    void (*Uniform2f)(GLint, GLfloat, GLfloat);
    void (*Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Uniform1iv)(GLint, GLsizei, const GLint*);
    void (*GenVertexArrays)(GLsizei, GLuint*);
    void (*BindVertexArray)(GLuint);
    void (*GenBuffers)(GLsizei, GLuint*);
    void (*BindBuffer)(GLenum, GLuint);
    void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    void (*VertexAttribIPointer)(GLuint, GLint, GLenum, GLsizei, const void*);
    void (*EnableVertexAttribArray)(GLuint);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    void (*Finish)();
    GLenum (*GetError)();
    void (*GetIntegerv)(GLenum, GLint*);
    void (*PointSize)(GLfloat);
    void (*LineWidth)(GLfloat);
    void (*Uniform4ui)(GLint, GLuint, GLuint, GLuint, GLuint);
    void (*Uniform1ui)(GLint, GLuint);
    void (*StencilFunc)(GLenum, GLint, GLuint);
    void (*TexParameterf)(GLenum, GLenum, GLfloat);
    void (*GenerateMipmap)(GLenum);
    void (*StencilOp)(GLenum, GLenum, GLenum);
    void (*StencilMask)(GLuint);
    void (*ClearStencil)(GLint);
    // The render thread's hand-over of the shown picture to the window's context
    void (*Flush)();
    GLsync (*FenceSync)(GLenum, GLbitfield);
    void (*WaitSync)(GLsync, GLbitfield, GLuint64);
    void (*DeleteSync)(GLsync);
    // KHR_ or ARB_parallel_shader_compile's (null without either): the driver compiling the shader programs on threads of its own
    void (*MaxShaderCompilerThreads)(GLuint);
};

// The entry points (loaded the first time, with a context current); false when one is missing
bool LoadGl();
Gl& GetGl();
}
