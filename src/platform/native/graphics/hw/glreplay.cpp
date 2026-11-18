#include "gl.h"
#include "gpu.h"

#include "graphics/gputime.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <unordered_map>

// THE LIST 70: the hardware renderer's command lists played with OpenGL 3.3 (gpu.h): each command's GL calls, the state a draw,
// clear or copy carries set where it differs from what this playing last set (and gl.cpp leaves out what the context already has,
// THE LIST 69)

namespace Hw
{
namespace
{
constexpr GLenum Formats[][3] = {
    // Internal format, the pixels' format and type
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE},
    {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT},
    {GL_R32F, GL_RED, GL_FLOAT},
    {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT},
    {GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT},
    {GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV},
    {GL_R8, GL_RED, GL_UNSIGNED_BYTE},
    {GL_RG8, GL_RG, GL_UNSIGNED_BYTE},
    // (Uploaded by glCompressedTexImage2D: only the internal format)
    {GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0},
    {GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, 0},
    {GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0},
    {GL_COMPRESSED_RGBA_BPTC_UNORM, 0, 0},
};
constexpr GLenum Filters[] = {GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR,
                              GL_LINEAR_MIPMAP_LINEAR};
constexpr GLenum Wraps[] = {GL_CLAMP_TO_EDGE, GL_REPEAT};
constexpr GLenum Params[] = {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_BASE_LEVEL,
                             GL_TEXTURE_MAX_LEVEL, GL_TEXTURE_MAX_ANISOTROPY};
constexpr GLenum Compares[] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, 0x0205 /* GL_NOTEQUAL */, GL_GEQUAL, GL_ALWAYS};
constexpr GLenum Factors[] = {GL_ZERO,           GL_ONE,          0x0302 /* GL_SRC_ALPHA */, 0x0303 /* GL_ONE_MINUS_SRC_ALPHA */,
                              0x0304 /* GL_DST_ALPHA */, 0x0305 /* GL_ONE_MINUS_DST_ALPHA */, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA,
                              GL_SRC1_ALPHA,     GL_ONE_MINUS_SRC1_ALPHA};
constexpr GLenum BlendOps[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT};
constexpr GLenum StencilActions[] = {GL_KEEP, GL_ZERO, GL_REPLACE};
constexpr GLenum Primitives[] = {GL_POINTS, GL_LINES, GL_TRIANGLES, GL_TRIANGLE_STRIP};

GLbitfield GlBits(u32 bits)
{
    return ((bits & ClearColour) ? GL_COLOR_BUFFER_BIT : 0) | ((bits & ClearDepth) ? GL_DEPTH_BUFFER_BIT : 0) |
           ((bits & ClearStencil) ? GL_STENCIL_BUFFER_BIT : 0);
}

class GlBackend final : public Backend
{
public:
    bool Start(SDL_Window* window, SDL_GLContext context, bool shared, bool direct);
    ~GlBackend() override;
    const char* Name() const override { return "OpenGL"; }
    void MakeCurrent() override;
    void Play(const CommandList& list) override;
    void NewTextures(s32 count, u32* names) override { G().GenTextures(count, names); }
    void NewFramebuffers(s32 count, u32* names) override { G().GenFramebuffers(count, names); }
    u32 StartProgram(const char* vertex, const std::string& fragment, bool dualSource) override;
    bool FinishProgram(u32 program) override;
    s32 UniformLocation(u32 program, const char* name) override { return G().GetUniformLocation(program, name); }
    void Read(u32 framebuffer, s32 x, s32 y, s32 width, s32 height, Format format, s32 rowLength, s32 alignment, void* out) override;
    bool FramebufferComplete(u32 framebuffer) override;
    bool OutOfMemory() override;
    s32 MaxTextureSize() override;
    s32 MaxAnisotropy() override;
    bool SamplesFormat(u32 format) override;
    void* Fence() override;
    void WaitFence(void* fence) override;
    void DeleteFence(void* fence) override { G().DeleteSync(static_cast<GLsync>(fence)); }
    void ParallelCompiles() override
    {
        // As many of the driver's threads as it likes (0xFFFFFFFF), where it has them
        if (G().MaxShaderCompilerThreads != nullptr)
        {
            G().MaxShaderCompilerThreads(0xFFFFFFFFu);
        }
    }

    // The window's (THE LIST 72): its own framebuffer, swapped
    void ShowWindow() override
    {
        if (ownWindow_ == nullptr)
        {
            SDL_GL_SwapWindow(window_);
        }
    }

    void SetVSync(bool on) override
    {
        MakeCurrent();
        SDL_GL_SetSwapInterval(on ? 1 : 0);
    }

private:
    // The entry points: gl.h's (the renderer's, counted and cached), or the window's own as they are
    Gl& G() { return *gl_; }
    void ApplyState(const RenderState& s);
    void BindDraw(u32 framebuffer);
    void BindRead(u32 framebuffer);
    void BindForChange(u32 texture);
    void UnpackStore(s32 rowLength, s32 alignment);
    void UnbindUnpackBuffer();

    SDL_Window* ownWindow_ = nullptr;
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool ownContext_ = false;
    Gl* gl_ = nullptr;
    Gl direct_ = {};
    // The context the window's code draws in too (one thread): what this playing set is forgotten at each
    bool shared_ = false;
    // Made current again: its state unknown
    bool forget_ = true;
    GLuint vertexArray_ = 0;
    GLuint vertexBuffer_ = 0;
    GLuint emptyArray_ = 0;
    GLuint uploadBuffer_ = 0;
    // A program's shaders until it's finished (their messages when it fails)
    std::unordered_map<u32, std::pair<GLuint, GLuint>> shaders_;
    // What this playing has set (none known when it starts: another thread's code may share the context, gl.h)
    struct Applied
    {
        bool state = false;
        RenderState renderState = {};
        u32 draw = ~0u;
        u32 read = ~0u;
        u32 program = ~0u;
        u32 units[TextureUnits];
        u32 unit = ~0u;
        u32 array = ~0u;
        s32 unpackRowLength = -1;
        s32 unpackAlignment = -1;
        bool unpackBuffer = true;
        f32 clearColour[4];
        bool clearColourKnown = false;
        s32 clearStencil = -1;
    } applied_;
    void Forget()
    {
        applied_ = Applied{};
        for (u32& unit : applied_.units)
        {
            unit = ~0u;
        }
    }
};

GlBackend::~GlBackend()
{
    if (ownContext_)
    {
        SDL_GL_DestroyContext(context_);
    }

    if (ownWindow_ != nullptr)
    {
        SDL_DestroyWindow(ownWindow_);
    }
}

bool GlBackend::Start(SDL_Window* window, SDL_GLContext context, bool shared, bool direct)
{
    shared_ = shared;
    window_ = window;
    context_ = context;
    if (window_ == nullptr || context_ == nullptr)
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

        ownContext_ = true;
        window_ = ownWindow_;
    }

    MakeCurrent();
    if (direct)
    {
        if (!LoadDirectGl(direct_))
        {
            return false;
        }

        gl_ = &direct_;
    }
    else
    {
        if (!LoadGl())
        {
            return false;
        }

        // The calls that would change nothing left out (THE LIST 69): the context's state only where the context is ours alone
        CacheGlState(!shared);
        gl_ = &GetGl();
    }

    Gl& gl = G();
    gl.GenVertexArrays(1, &vertexArray_);
    gl.GenBuffers(1, &vertexBuffer_);
    gl.BindVertexArray(vertexArray_);
    gl.BindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    gl.VertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, z)));
    gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
    gl.VertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, s)));
    gl.VertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, fog)));
    for (GLuint i = 0; i < 5; i++)
    {
        gl.EnableVertexAttribArray(i);
    }

    gl.GenVertexArrays(1, &emptyArray_);
    Forget();
    return true;
}

void GlBackend::MakeCurrent()
{
    if (SDL_GL_GetCurrentContext() != context_)
    {
        SDL_GL_MakeCurrent(window_, context_);
        // (Its state may not be as this thread last set it: gl.h)
        ForgetGlState();
        forget_ = true;
    }
}

void GlBackend::BindDraw(u32 framebuffer)
{
    if (applied_.draw != framebuffer)
    {
        G().BindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        applied_.draw = framebuffer;
    }
}

void GlBackend::BindRead(u32 framebuffer)
{
    if (applied_.read != framebuffer)
    {
        G().BindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        applied_.read = framebuffer;
    }
}

void GlBackend::BindForChange(u32 texture)
{
    // On the unit made active last (as the renderer's calls did)
    Gl& gl = G();
    if (applied_.unit == ~0u)
    {
        gl.ActiveTexture(GL_TEXTURE0);
        applied_.unit = 0;
    }

    if (applied_.units[applied_.unit] != texture)
    {
        gl.BindTexture(GL_TEXTURE_2D, texture);
        applied_.units[applied_.unit] = texture;
    }
}

void GlBackend::UnpackStore(s32 rowLength, s32 alignment)
{
    Gl& gl = G();
    if (applied_.unpackRowLength != rowLength)
    {
        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
        applied_.unpackRowLength = rowLength;
    }

    if (applied_.unpackAlignment != alignment)
    {
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        applied_.unpackAlignment = alignment;
    }
}

void GlBackend::UnbindUnpackBuffer()
{
    if (applied_.unpackBuffer)
    {
        G().BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        applied_.unpackBuffer = false;
    }
}

void GlBackend::ApplyState(const RenderState& s)
{
    Gl& gl = G();
    RenderState& a = applied_.renderState;
    const bool all = !applied_.state;
    auto cap = [&](GLenum name, u8 on, u8 was) {
        if (all || on != was)
        {
            on ? gl.Enable(name) : gl.Disable(name);
        }
    };
    cap(GL_BLEND, s.blend, a.blend);
    cap(GL_DEPTH_TEST, s.depthTest, a.depthTest);
    cap(GL_SCISSOR_TEST, s.scissorTest, a.scissorTest);
    cap(GL_STENCIL_TEST, s.stencilTest, a.stencilTest);
    cap(GL_CULL_FACE, s.cullFace, a.cullFace);
    cap(GL_PROGRAM_POINT_SIZE, s.pointSize, a.pointSize);
    if (all || s.blendOp != a.blendOp || s.blendOpAlpha != a.blendOpAlpha)
    {
        gl.BlendEquationSeparate(BlendOps[s.blendOp], BlendOps[s.blendOpAlpha]);
    }

    if (all || s.blendSource != a.blendSource || s.blendDestination != a.blendDestination || s.blendSourceAlpha != a.blendSourceAlpha ||
        s.blendDestinationAlpha != a.blendDestinationAlpha)
    {
        gl.BlendFuncSeparate(Factors[s.blendSource], Factors[s.blendDestination], Factors[s.blendSourceAlpha], Factors[s.blendDestinationAlpha]);
    }

    if (all || std::memcmp(s.blendColour, a.blendColour, sizeof(s.blendColour)) != 0)
    {
        gl.BlendColor(s.blendColour[0], s.blendColour[1], s.blendColour[2], s.blendColour[3]);
    }

    if (all || s.colourMask != a.colourMask)
    {
        gl.ColorMask((s.colourMask & 1) ? GL_TRUE : GL_FALSE, (s.colourMask & 2) ? GL_TRUE : GL_FALSE, (s.colourMask & 4) ? GL_TRUE : GL_FALSE,
                     (s.colourMask & 8) ? GL_TRUE : GL_FALSE);
    }

    if (all || s.depthFunc != a.depthFunc)
    {
        gl.DepthFunc(Compares[s.depthFunc]);
    }

    if (all || s.depthWrite != a.depthWrite)
    {
        gl.DepthMask(s.depthWrite ? GL_TRUE : GL_FALSE);
    }

    if (all || s.stencilFunc != a.stencilFunc || s.stencilReference != a.stencilReference || s.stencilReadMask != a.stencilReadMask)
    {
        gl.StencilFunc(Compares[s.stencilFunc], s.stencilReference, s.stencilReadMask);
    }

    if (all || s.stencilFail != a.stencilFail || s.stencilDepthFail != a.stencilDepthFail || s.stencilPass != a.stencilPass)
    {
        gl.StencilOp(StencilActions[s.stencilFail], StencilActions[s.stencilDepthFail], StencilActions[s.stencilPass]);
    }

    if (all || s.stencilWriteMask != a.stencilWriteMask)
    {
        gl.StencilMask(s.stencilWriteMask);
    }

    if (all || std::memcmp(s.scissor, a.scissor, sizeof(s.scissor)) != 0)
    {
        gl.Scissor(s.scissor[0], s.scissor[1], s.scissor[2], s.scissor[3]);
    }

    if (all || std::memcmp(s.viewport, a.viewport, sizeof(s.viewport)) != 0)
    {
        gl.Viewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    }

    a = s;
    applied_.state = true;
}

void GlBackend::Play(const CommandList& list)
{
    MakeCurrent();
    if (shared_ || forget_)
    {
        Forget();
        forget_ = false;
    }

    Gl& gl = G();
    const u8* at = list.commands();
    const u8* end = at + list.size();
    while (at < end)
    {
        const Header& header = *reinterpret_cast<const Header*>(at);
        switch (header.op)
        {
        case Op::Draw:
        {
            const auto& c = *reinterpret_cast<const DrawCommand*>(at);
            BindDraw(c.framebuffer);
            if (applied_.program != c.program)
            {
                gl.UseProgram(c.program);
                applied_.program = c.program;
            }

            for (u32 unit = 0; unit < TextureUnits; unit++)
            {
                if (applied_.units[unit] != c.textures[unit])
                {
                    if (applied_.unit != unit)
                    {
                        gl.ActiveTexture(GL_TEXTURE0 + unit);
                        applied_.unit = unit;
                    }

                    gl.BindTexture(GL_TEXTURE_2D, c.textures[unit]);
                    applied_.units[unit] = c.textures[unit];
                }
            }

            const GLuint array = c.vertexes ? vertexArray_ : emptyArray_;
            if (applied_.array != array)
            {
                gl.BindVertexArray(array);
                applied_.array = array;
            }

            ApplyState(c.state);
            gl.DrawArrays(Primitives[c.primitive], c.first, c.count);
            break;
        }
        case Op::Clear:
        {
            const auto& c = *reinterpret_cast<const ClearCommand*>(at);
            BindDraw(c.framebuffer);
            ApplyState(c.state);
            if ((c.bits & ClearColour) &&
                (!applied_.clearColourKnown || std::memcmp(applied_.clearColour, c.colour, sizeof(c.colour)) != 0))
            {
                gl.ClearColor(c.colour[0], c.colour[1], c.colour[2], c.colour[3]);
                std::memcpy(applied_.clearColour, c.colour, sizeof(c.colour));
                applied_.clearColourKnown = true;
            }

            if ((c.bits & ClearStencil) && applied_.clearStencil != c.stencil)
            {
                gl.ClearStencil(c.stencil);
                applied_.clearStencil = c.stencil;
            }

            gl.Clear(GlBits(c.bits));
            break;
        }
        case Op::Blit:
        {
            const auto& c = *reinterpret_cast<const BlitCommand*>(at);
            BindRead(c.readFramebuffer);
            BindDraw(c.drawFramebuffer);
            ApplyState(c.state);
            gl.BlitFramebuffer(c.source[0], c.source[1], c.source[2], c.source[3], c.destination[0], c.destination[1], c.destination[2],
                               c.destination[3], GlBits(c.bits), c.filter == FilterLinear ? GL_LINEAR : GL_NEAREST);
            break;
        }
        case Op::Uniform:
        {
            const auto& c = *reinterpret_cast<const UniformCommand*>(at);
            if (applied_.program != c.program)
            {
                gl.UseProgram(c.program);
                applied_.program = c.program;
            }

            const s32* i = reinterpret_cast<const s32*>(c.values);
            const f32* f = reinterpret_cast<const f32*>(c.values);
            if (c.array)
            {
                gl.Uniform1iv(c.location, c.count, i);
            }
            else if (c.kind == UniformInt)
            {
                gl.Uniform1i(c.location, i[0]);
            }
            else if (c.kind == UniformUint)
            {
                c.count == 4 ? gl.Uniform4ui(c.location, c.values[0], c.values[1], c.values[2], c.values[3]) : gl.Uniform1ui(c.location, c.values[0]);
            }
            else
            {
                switch (c.count)
                {
                case 1:
                    gl.Uniform1f(c.location, f[0]);
                    break;
                case 2:
                    gl.Uniform2f(c.location, f[0], f[1]);
                    break;
                case 3:
                    gl.Uniform3f(c.location, f[0], f[1], f[2]);
                    break;
                default:
                    gl.Uniform4f(c.location, f[0], f[1], f[2], f[3]);
                    break;
                }
            }

            break;
        }
        case Op::TexImage:
        case Op::TexSubImage:
        {
            const auto& c = *reinterpret_cast<const TexImageCommand*>(at);
            BindForChange(c.texture);
            UnpackStore(c.rowLength, c.alignment);
            const GLenum* format = Formats[c.format];
            const void* pixels = c.bytes != 0 ? list.data() + c.data : nullptr;
            if (c.streamed && pixels != nullptr)
            {
                // Into a fresh buffer's storage (the old storage stays the GPU's until it's done with it), copied by the GPU
                if (uploadBuffer_ == 0)
                {
                    gl.GenBuffers(1, &uploadBuffer_);
                }

                gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, uploadBuffer_);
                applied_.unpackBuffer = true;
                gl.BufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(c.bytes), pixels, GL_STREAM_DRAW);
                pixels = nullptr;
            }
            else
            {
                UnbindUnpackBuffer();
            }

            if (header.op == Op::TexImage && CompressedFormat(c.format))
            {
                gl.CompressedTexImage2D(GL_TEXTURE_2D, c.level, format[0], c.width, c.height, 0, static_cast<GLsizei>(c.bytes), pixels);
            }
            else if (header.op == Op::TexImage)
            {
                gl.TexImage2D(GL_TEXTURE_2D, c.level, static_cast<GLint>(format[0]), c.width, c.height, 0, format[1], format[2], pixels);
            }
            else
            {
                gl.TexSubImage2D(GL_TEXTURE_2D, c.level, c.x, c.y, c.width, c.height, format[1], format[2], pixels);
            }

            break;
        }
        case Op::TexParam:
        {
            const auto& c = *reinterpret_cast<const TexParamCommand*>(at);
            BindForChange(c.texture);
            switch (c.param)
            {
            case ParamMinFilter:
            case ParamMagFilter:
                gl.TexParameteri(GL_TEXTURE_2D, Params[c.param], static_cast<GLint>(Filters[c.value]));
                break;
            case ParamWrapS:
            case ParamWrapT:
                gl.TexParameteri(GL_TEXTURE_2D, Params[c.param], static_cast<GLint>(Wraps[c.value]));
                break;
            case ParamMaxAnisotropy:
                gl.TexParameterf(GL_TEXTURE_2D, Params[c.param], c.valueFloat);
                break;
            default:
                gl.TexParameteri(GL_TEXTURE_2D, Params[c.param], c.value);
                break;
            }

            break;
        }
        case Op::Mipmaps:
        {
            const auto& c = *reinterpret_cast<const TextureCommand*>(at);
            BindForChange(c.texture);
            gl.GenerateMipmap(GL_TEXTURE_2D);
            break;
        }
        case Op::Attach:
        {
            const auto& c = *reinterpret_cast<const AttachCommand*>(at);
            BindDraw(c.framebuffer);
            gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, c.slot == 0 ? GL_COLOR_ATTACHMENT0 : GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                                    c.texture, c.level);
            break;
        }
        case Op::NoColour:
        {
            const auto& c = *reinterpret_cast<const AttachCommand*>(at);
            BindDraw(c.framebuffer);
            BindRead(c.framebuffer);
            gl.DrawBuffer(GL_NONE);
            gl.ReadBuffer(GL_NONE);
            break;
        }
        case Op::Vertices:
        {
            const auto& c = *reinterpret_cast<const VerticesCommand*>(at);
            if (applied_.array != vertexArray_)
            {
                gl.BindVertexArray(vertexArray_);
                applied_.array = vertexArray_;
            }

            gl.BindBuffer(GL_ARRAY_BUFFER, vertexBuffer_);
            gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(c.count * sizeof(Vertex)), list.vertexes.data() + c.first, GL_STREAM_DRAW);
            break;
        }
        case Op::DeleteTextures:
        {
            const auto& c = *reinterpret_cast<const DeleteCommand*>(at);
            gl.DeleteTextures(static_cast<GLsizei>(c.count), c.names);
            for (u32 i = 0; i < c.count; i++)
            {
                for (u32& unit : applied_.units)
                {
                    if (unit == c.names[i])
                    {
                        unit = 0;
                    }
                }
            }

            break;
        }
        case Op::DeleteFramebuffers:
        {
            const auto& c = *reinterpret_cast<const DeleteCommand*>(at);
            gl.DeleteFramebuffers(static_cast<GLsizei>(c.count), c.names);
            for (u32 i = 0; i < c.count; i++)
            {
                applied_.draw = applied_.draw == c.names[i] ? 0 : applied_.draw;
                applied_.read = applied_.read == c.names[i] ? 0 : applied_.read;
            }

            break;
        }
        case Op::Label:
            NativeGraphics::GpuTimeLabel(reinterpret_cast<const LabelCommand*>(at)->label);
            break;
        }

        at += header.bytes;
    }

    UnbindUnpackBuffer();
}

u32 GlBackend::StartProgram(const char* vertex, const std::string& fragment, bool dualSource)
{
    Gl& gl = G();
    GLuint v = gl.CreateShader(GL_VERTEX_SHADER);
    gl.ShaderSource(v, 1, &vertex, nullptr);
    gl.CompileShader(v);
    GLuint f = gl.CreateShader(GL_FRAGMENT_SHADER);
    const char* fragmentText = fragment.c_str();
    gl.ShaderSource(f, 1, &fragmentText, nullptr);
    gl.CompileShader(f);
    GLuint program = gl.CreateProgram();
    gl.AttachShader(program, v);
    gl.AttachShader(program, f);
    if (dualSource)
    {
        gl.BindFragDataLocationIndexed(program, 0, 0, "outColour");
        gl.BindFragDataLocationIndexed(program, 0, 1, "outFactor");
    }

    gl.LinkProgram(program);
    shaders_[program] = {v, f};
    return program;
}

bool GlBackend::FinishProgram(u32 program)
{
    Gl& gl = G();
    auto found = shaders_.find(program);
    GLint linked = 0;
    gl.GetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[2048];
        if (found != shaders_.end())
        {
            for (GLuint shader : {found->second.first, found->second.second})
            {
                GLint compiled = 0;
                gl.GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
                if (!compiled)
                {
                    gl.GetShaderInfoLog(shader, sizeof(log), nullptr, log);
                    std::fprintf(stderr, "graphics: hardware shader: %s\n", log);
                }
            }
        }

        gl.GetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::fprintf(stderr, "graphics: hardware program: %s\n", log);
        return false;
    }

    if (found != shaders_.end())
    {
        gl.DeleteShader(found->second.first);
        gl.DeleteShader(found->second.second);
        shaders_.erase(found);
    }

    return true;
}

void GlBackend::Read(u32 framebuffer, s32 x, s32 y, s32 width, s32 height, Format format, s32 rowLength, s32 alignment, void* out)
{
    MakeCurrent();
    Gl& gl = G();
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    applied_.read = framebuffer;
    gl.PixelStorei(GL_PACK_ALIGNMENT, alignment);
    gl.PixelStorei(GL_PACK_ROW_LENGTH, rowLength);
    if (format == FormatDepth32fStencil8)
    {
        gl.ReadPixels(x, y, width, height, GL_DEPTH_COMPONENT, GL_FLOAT, out);
    }
    else
    {
        gl.ReadPixels(x, y, width, height, Formats[format][1], Formats[format][2], out);
    }
}

bool GlBackend::FramebufferComplete(u32 framebuffer)
{
    MakeCurrent();
    Gl& gl = G();
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    applied_.draw = framebuffer;
    return gl.CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

bool GlBackend::OutOfMemory()
{
    MakeCurrent();
    Gl& gl = G();
    bool failed = false;
    while (gl.GetError() != GL_NO_ERROR)
    {
        failed = true;
    }

    return failed;
}

s32 GlBackend::MaxTextureSize()
{
    GLint biggest = 0;
    G().GetIntegerv(GL_MAX_TEXTURE_SIZE, &biggest);
    return biggest;
}

// S3TC (BC1-3) by EXT_texture_compression_s3tc, BPTC (BC7) by ARB_texture_compression_bptc (core since 4.2); asked once, with the
// context current
bool GlBackend::SamplesFormat(u32 format)
{
    if (!CompressedFormat(format))
    {
        return true;
    }

    static const bool s3tc = SDL_GL_ExtensionSupported("GL_EXT_texture_compression_s3tc");
    static const bool bptc = SDL_GL_ExtensionSupported("GL_ARB_texture_compression_bptc");
    return format == FormatBc7 ? bptc : s3tc;
}

s32 GlBackend::MaxAnisotropy()
{
    // GL_MAX_TEXTURE_MAX_ANISOTROPY, core since 4.6 and EXT_texture_filter_anisotropic's before; 1 when it has none
    GLfloat value = 0.0f;
    auto getFloat = reinterpret_cast<void (*)(GLenum, GLfloat*)>(SDL_GL_GetProcAddress("glGetFloatv"));
    Gl& gl = G();
    while (gl.GetError() != GL_NO_ERROR)
    {
    }

    if (getFloat != nullptr)
    {
        getFloat(0x84FF, &value);
    }

    return gl.GetError() == GL_NO_ERROR && value >= 1.0f ? static_cast<s32>(value) : 1;
}

void* GlBackend::Fence()
{
    Gl& gl = G();
    GLsync fence = gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    gl.Flush();
    return fence;
}

void GlBackend::WaitFence(void* fence)
{
    G().WaitSync(static_cast<GLsync>(fence), 0, GL_TIMEOUT_IGNORED);
}
}

Backend* StartGlBackend(void* window, void* context, bool shared)
{
    auto* backend = new GlBackend;
    if (!backend->Start(static_cast<SDL_Window*>(window), static_cast<SDL_GLContext>(context), shared, false))
    {
        delete backend;
        return nullptr;
    }

    return backend;
}

Backend* StartGlWindowBackend(void* window, void* context)
{
    auto* backend = new GlBackend;
    if (!backend->Start(static_cast<SDL_Window*>(window), static_cast<SDL_GLContext>(context), true, true))
    {
        delete backend;
        return nullptr;
    }

    return backend;
}
}
