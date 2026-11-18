#pragma once

// THE LIST 70: what the hardware renderer asks of the graphics card, in no graphics API's terms. hwgs.cpp describes its work
// through Gpu the way it used to call OpenGL (the bound targets and textures, the switches, the blending, the uniforms), and Gpu
// records it as a list of commands, each whole by itself: a draw carries its target, its program, the textures on each unit and
// its state (blending, depth, stencil, masks, scissor, viewport: RenderState, the key a pipeline is made from), a clear and a copy
// theirs, an upload its bytes. A backend plays the list: glreplay.cpp's with OpenGL 3.3 (the calls hwgs.cpp made before, in the
// same order); THE LIST 71's with SDL's GPU API. hwgs.cpp plays the list after each batch (in bigger pieces OpenGL's calls cost
// more: native/WINDOWS.md, "The command list"), and it's played when its results are wanted (a read back, a target's check, a
// fence) and at the frame's end; the names of textures, framebuffers and programs are the backend's, made at once (with OpenGL,
// GL's own names).
// Modelled on Tenkaichi3Decomp's port/src/gs/gs_draw.h at its MIT commit 06e4671 (z3xox: the frame recorded by the GS's side, the
// draws replayed by a back end, OpenGL's or SDL GPU's); written anew for this renderer, whose list is GL's calls made neutral.

#include "common.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Hw
{
// A primitive's vertex, as every backend takes it (hwgs.cpp's AddPrimitive makes them)
struct Vertex
{
    f32 x;
    f32 y;
    u32 z;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    f32 s;
    f32 t;
    f32 q;
    f32 fog;
};

// Textures' formats (an upload's bytes and a read back's are in the texture's own: RGBA8 4 bytes, the integer and float ones one
// value a texel, R8 and RG8 a byte a channel; a depth-stencil buffer read back as its depth's floats)
enum Format : u32
{
    FormatRgba8,
    FormatRgba16f,
    FormatR32f,
    FormatR32ui,
    FormatR16ui,
    FormatDepth32fStencil8,
    // The window's code's (SMAA's tables and targets, the overlay's glyphs)
    FormatR8,
    FormatRg8,
    // Block-compressed RGBA (a texture pack's DDS files as they are, THE LIST 25a): blocks of 4x4 texels, BC1 8 bytes, BC2, BC3
    // and BC7 16; sampled only, uploaded a whole level at a time (its blocks in rows, a level smaller than a block one block)
    FormatBc1,
    FormatBc2,
    FormatBc3,
    FormatBc7,
};

inline bool CompressedFormat(u32 format)
{
    return format >= FormatBc1 && format <= FormatBc7;
}

// A block-compressed level's bytes
inline size_t CompressedBytes(u32 format, s32 width, s32 height)
{
    const size_t blocks = static_cast<size_t>((std::max(width, 1) + 3) / 4) * static_cast<size_t>((std::max(height, 1) + 3) / 4);
    return blocks * (format == FormatBc1 ? 8 : 16);
}

enum Filter : u32
{
    FilterNearest,
    FilterLinear,
    FilterNearestMipNearest,
    FilterLinearMipNearest,
    FilterNearestMipLinear,
    FilterLinearMipLinear,
};

enum Wrap : u32
{
    WrapClamp,
    WrapRepeat,
};

// A texture's sampling (kept by the texture, as GL keeps it)
enum Param : u32
{
    ParamMinFilter,
    ParamMagFilter,
    ParamWrapS,
    ParamWrapT,
    ParamBaseLevel,
    ParamMaxLevel,
    ParamMaxAnisotropy,
    ParamCount,
};

enum Cap : u32
{
    CapBlend,
    CapDepthTest,
    CapScissorTest,
    CapStencilTest,
    CapCullFace,
    CapPointSize,
};

enum Compare : u32
{
    CompareNever,
    CompareLess,
    CompareEqual,
    CompareLessEqual,
    CompareGreater,
    CompareNotEqual,
    CompareGreaterEqual,
    CompareAlways,
};

enum Factor : u32
{
    FactorZero,
    FactorOne,
    FactorSrcAlpha,
    FactorOneMinusSrcAlpha,
    FactorDstAlpha,
    FactorOneMinusDstAlpha,
    FactorConstantAlpha,
    FactorOneMinusConstantAlpha,
    // The fragment shader's second output's alpha (dual-source blending: OpenGL's, not SDL GPU's)
    FactorSrc1Alpha,
    FactorOneMinusSrc1Alpha,
};

enum BlendOp : u32
{
    BlendAdd,
    BlendSubtract,
    BlendReverseSubtract,
};

enum StencilAction : u32
{
    StencilKeep,
    StencilZero,
    StencilReplace,
};

enum Primitive : u32
{
    PrimitivePoints,
    PrimitiveLines,
    PrimitiveTriangles,
    PrimitiveTriangleStrip,
};

enum ClearBits : u32
{
    ClearColour = 1,
    ClearDepth = 2,
    ClearStencil = 4,
};

// Pixel rows' layout of the uploads (unpack) and read backs (pack): rows this many texels long (0: the rectangle's width), rows
// starting at multiples of the alignment
enum Store : u32
{
    StoreUnpackRowLength,
    StoreUnpackAlignment,
    StorePackRowLength,
    StorePackAlignment,
};

constexpr u32 TextureUnits = 8;

// Everything fixed a draw (and a clear, a copy) is made with: what a pipeline is made from, with the scissor and viewport.
// Compared as bytes (made zeroed)
struct RenderState
{
    u8 blend;
    u8 blendOp;
    u8 blendOpAlpha;
    u8 blendSource;
    u8 blendDestination;
    u8 blendSourceAlpha;
    u8 blendDestinationAlpha;
    // Bits: red 1, green 2, blue 4, alpha 8
    u8 colourMask;
    u8 depthTest;
    u8 depthFunc;
    u8 depthWrite;
    u8 stencilTest;
    u8 stencilFunc;
    u8 stencilFail;
    u8 stencilDepthFail;
    u8 stencilPass;
    u8 stencilReference;
    u8 stencilReadMask;
    u8 stencilWriteMask;
    u8 scissorTest;
    u8 cullFace;
    u8 pointSize;
    u8 spare[2];
    f32 blendColour[4];
    s32 scissor[4];
    s32 viewport[4];
};

enum class Op : u16
{
    Draw,
    Clear,
    Blit,
    Uniform,
    TexImage,
    TexSubImage,
    TexParam,
    Mipmaps,
    Attach,
    NoColour,
    Vertices,
    DeleteTextures,
    DeleteFramebuffers,
    Label,
};

// The commands, each after a header saying which and how long (8 byte aligned)
struct Header
{
    Op op;
    u16 bytes;
    u32 spare;
};

struct DrawCommand
{
    Header header;
    RenderState state;
    u32 framebuffer;
    u32 program;
    u32 textures[TextureUnits];
    u32 primitive;
    // The vertexes: the list's (the last Vertices command's) or none (the program makes its own from the vertex's number)
    u32 vertexes;
    s32 first;
    s32 count;
};

struct ClearCommand
{
    Header header;
    RenderState state;
    u32 framebuffer;
    u32 bits;
    f32 colour[4];
    s32 stencil;
    u32 spare;
};

struct BlitCommand
{
    Header header;
    RenderState state;
    u32 readFramebuffer;
    u32 drawFramebuffer;
    s32 source[4];
    s32 destination[4];
    u32 bits;
    u32 filter;
};

enum UniformKind : u8
{
    UniformInt,
    UniformUint,
    UniformFloat,
};

// A uniform of a program (its value kept by the program): COUNT values of its kind (a vector's, or an array's of ints)
struct UniformCommand
{
    Header header;
    u32 program;
    s32 location;
    u8 kind;
    u8 count;
    u8 array;
    u8 spare;
    u32 values[4];
};

// A level's storage given (its bytes, or none: undefined), or a rectangle of it written. With STREAMED the backend may pass the
// bytes through a buffer of its own (OpenGL: a pixel unpack buffer orphaned each time)
struct TexImageCommand
{
    Header header;
    u32 texture;
    s32 level;
    u32 format;
    s32 x;
    s32 y;
    s32 width;
    s32 height;
    s32 rowLength;
    s32 alignment;
    u32 streamed;
    size_t data;
    size_t bytes;
};

struct TexParamCommand
{
    Header header;
    u32 texture;
    u32 param;
    s32 value;
    f32 valueFloat;
};

struct TextureCommand
{
    Header header;
    u32 texture;
    u32 spare;
};

// A framebuffer's attachment: colour (slot 0) or depth-stencil (slot 1); NoColour: drawn and read without a colour buffer
struct AttachCommand
{
    Header header;
    u32 framebuffer;
    u32 slot;
    u32 texture;
    s32 level;
};

// The vertexes the next draws take: a range of the list's
struct VerticesCommand
{
    Header header;
    size_t first;
    size_t count;
};

// Objects deleted (their names after the header)
struct DeleteCommand
{
    Header header;
    u32 count;
    u32 names[1];
};

// The card's time counted to LABEL from here (gputime.h)
struct LabelCommand
{
    Header header;
    const char* label;
};

// A frame's commands, their bytes (uploads') and vertexes
class CommandList
{
public:
    template <typename T>
    T& Add(Op op, size_t bytes = sizeof(T))
    {
        bytes = (bytes + 7) & ~size_t{7};
        if (size_ + bytes > capacity_)
        {
            Grow(size_ + bytes);
        }

        T* command = reinterpret_cast<T*>(commands_.get() + size_);
        command->header.op = op;
        command->header.bytes = static_cast<u16>(bytes);
        size_ += bytes;
        count_++;
        return *command;
    }

    // Bytes kept for the list's playing: where they start in data()
    size_t AddData(const void* bytes, size_t size);
    const u8* data() const { return data_.get(); }
    const u8* commands() const { return commands_.get(); }
    size_t size() const { return size_; }
    size_t dataSize() const { return dataSize_; }
    u32 count() const { return count_; }
    bool empty() const { return size_ == 0; }

    // The vertexes: the batch being made is after the last Vertices command's (kept when the list is cleared)
    std::vector<Vertex> vertexes;
    size_t batchStart = 0;

    // After playing: the commands and bytes gone, the batch being made moved to the start
    void Clear();

private:
    void Grow(size_t least);

    std::unique_ptr<u8[]> commands_;
    size_t size_ = 0;
    size_t capacity_ = 0;
    u32 count_ = 0;
    std::unique_ptr<u8[]> data_;
    size_t dataSize_ = 0;
    size_t dataCapacity_ = 0;
};

// A backend: plays the lists, makes the names and programs, reads back
class Backend
{
public:
    virtual ~Backend() = default;
    virtual const char* Name() const = 0;
    // The card and its driver, for the log
    virtual std::string Describe() const { return Name(); }
    // Its context or device current on the calling thread (with OpenGL, the context; made current when it isn't)
    virtual void MakeCurrent() = 0;
    virtual void Play(const CommandList& list) = 0;
    virtual void NewTextures(s32 count, u32* names) = 0;
    virtual void NewFramebuffers(s32 count, u32* names) = 0;
    // A program from GLSL 3.30's vertex and fragment shaders (DUALSOURCE: the fragment's outColour and outFactor its blend's two
    // sources), its compiling asked for; Finish waits for it and says whether it compiled and linked (its messages printed)
    virtual u32 StartProgram(const char* vertex, const std::string& fragment, bool dualSource) = 0;
    virtual bool FinishProgram(u32 program) = 0;
    virtual s32 UniformLocation(u32 program, const char* name) = 0;
    // A rectangle of a framebuffer's colour (or depth, for a depth-stencil one's FormatDepth32fStencil8) read back
    virtual void Read(u32 framebuffer, s32 x, s32 y, s32 width, s32 height, Format format, s32 rowLength, s32 alignment, void* out) = 0;
    virtual bool FramebufferComplete(u32 framebuffer) = 0;
    // Whether the card ran out of memory since last asked
    virtual bool OutOfMemory() = 0;
    virtual s32 MaxTextureSize() = 0;
    virtual s32 MaxAnisotropy() = 0;
    // Whether the card samples a block-compressed format (gpu.h's FormatBc1 to FormatBc7)
    virtual bool SamplesFormat(u32 format) { return !CompressedFormat(format); }
    // A fence after everything played; a fence waited for by the card before what's played next; one deleted
    virtual void* Fence() = 0;
    virtual void WaitFence(void* fence) = 0;
    virtual void DeleteFence(void* fence) = 0;
    // The card's driver compiling programs on threads of its own, where it can
    virtual void ParallelCompiles() = 0;
    // The list played once it's this many bytes long (0: after each batch, OpenGL's best; $TWIN_HW_LIST_BYTES says otherwise)
    virtual size_t ListBytes() const { return 0; }
    // What's played so far sent to the card (another backend's work after it uses its results: THE LIST 72's window)
    virtual void Flush() {}

    // THE LIST 72, the window's backend (the presenter's, display.cpp) and the renderer's of the same API: a texture handed from
    // one to the other (its API's own object: OpenGL's name, SDL GPU's texture), and taken as a name of the other's, valid until it's
    // handed over again (an RGBA8 texture of this size; its sampling set by the taker)
    virtual void* NativeTexture(u32 texture) { return reinterpret_cast<void*>(static_cast<uintptr_t>(texture)); }
    virtual u32 TakeTexture(void* native, s32 width, s32 height)
    {
        (void)width;
        (void)height;
        return static_cast<u32>(reinterpret_cast<uintptr_t>(native));
    }
    // The window's frame: framebuffer 0 is the window's pixels, WIDTH x HEIGHT (rows as OpenGL's: the first the bottom one), shown by
    // ShowWindow once drawn (OpenGL: the window's own framebuffer, swapped; Vulkan: a texture of the window's size, copied into
    // the swapchain's). False when the window can't be drawn now (minimised)
    virtual bool WindowFrame(s32 width, s32 height)
    {
        (void)width;
        (void)height;
        return true;
    }
    virtual void ShowWindow() {}
    // The window's frames shown at the screen's refresh (V-sync) or as soon as they're drawn
    virtual void SetVSync(bool on) { (void)on; }
};

// The GL backend (glreplay.cpp): the window's or the render thread's context (WINDOW and CONTEXT: SDL's), or one of its own made in
// a hidden window when both are null; SHARED: the context is the window's code's too (one thread). Null when OpenGL 3.3 isn't there
Backend* StartGlBackend(void* window, void* context, bool shared);
// The window's GL backend (THE LIST 72: display.cpp's presenter, the overlay's): the window's context, or one of its own in a hidden
// window when both are null (the overlay's shots). OpenGL's entry points called as they are: not counted nor left out (gl.h's
// cache is the renderer's, on its own thread); the state it sets forgotten at each playing (one thread: the renderer's too)
Backend* StartGlWindowBackend(void* window, void* context);
// The SDL GPU backend on Vulkan (sdlgpu.cpp, THE LIST 71): the device the window's backend made (made here when there's none: no
// window), or one of its own (OWN: the overlay's shots, which share nothing). Null when Vulkan isn't there, or the build has no
// glslang
Backend* StartVulkanBackend(bool own = false);
// The window's Vulkan backend (THE LIST 72): the device made and the window claimed for it (from the window's thread). Null when
// Vulkan isn't there (or can't show in this window), or the build has no glslang
Backend* StartVulkanWindowBackend(void* window);
// Whether the build has the Vulkan backend
bool VulkanBuilt();

// What hwgs.cpp calls: the state as GL's calls set it, the commands recorded with it
class Gpu
{
public:
    void Start(Backend* backend);
    Backend& backend() { return *backend_; }
    CommandList& list() { return list_; }

    // Names, made at once
    void GenTextures(s32 count, u32* names) { backend_->NewTextures(count, names); }
    void GenFramebuffers(s32 count, u32* names) { backend_->NewFramebuffers(count, names); }
    void DeleteTextures(s32 count, const u32* names);
    void DeleteFramebuffers(s32 count, const u32* names);

    // The state the next commands are recorded with
    void ActiveTexture(u32 unit) { unit_ = unit; }
    void BindTexture(u32 texture) { units_[unit_] = texture; }
    u32 BoundTexture() const { return units_[unit_]; }
    void BindFramebuffer(u32 framebuffer)
    {
        drawFramebuffer_ = framebuffer;
        readFramebuffer_ = framebuffer;
    }
    void BindDrawFramebuffer(u32 framebuffer) { drawFramebuffer_ = framebuffer; }
    void BindReadFramebuffer(u32 framebuffer) { readFramebuffer_ = framebuffer; }
    void UseProgram(u32 program) { program_ = program; }
    // Draws with the list's vertexes (true) or none (false)
    void BindVertexes(bool vertexes) { vertexes_ = vertexes; }
    // (The enums' values as u32: Cap, Factor, BlendOp, Compare, StencilAction, Store, Param, Format, Primitive, Filter, ClearBits)
    void Enable(u32 cap) { SetCap(cap, 1); }
    void Disable(u32 cap) { SetCap(cap, 0); }
    void Viewport(s32 x, s32 y, s32 width, s32 height);
    void Scissor(s32 x, s32 y, s32 width, s32 height);
    void BlendFuncSeparate(u32 source, u32 destination, u32 sourceAlpha, u32 destinationAlpha);
    void BlendEquationSeparate(u32 colour, u32 alpha);
    void BlendColor(f32 r, f32 g, f32 b, f32 a);
    void ColorMask(bool r, bool g, bool b, bool a);
    void DepthMask(bool on) { state_.depthWrite = on ? 1 : 0; }
    void DepthFunc(u32 function) { state_.depthFunc = static_cast<u8>(function); }
    void StencilFunc(u32 function, s32 reference, u32 mask);
    void StencilOp(u32 fail, u32 depthFail, u32 pass);
    void StencilMask(u32 mask) { state_.stencilWriteMask = static_cast<u8>(mask); }
    void ClearColor(f32 r, f32 g, f32 b, f32 a);
    void ClearStencil(s32 value) { clearStencil_ = value; }
    void PixelStore(u32 store, s32 value) { store_[store] = value; }
    // A texture's sampling not known here (THE LIST 72: one taken from another backend, set there too): set again when asked
    void ForgetSampling(u32 texture)
    {
        if (texture < sampling_.size())
        {
            sampling_[texture] = {};
        }
    }

    // The commands. The bound texture's: its sampling, a level's storage or a rectangle of it (the bytes copied into the list),
    // its smaller levels made from level 0
    void TexParameter(u32 param, s32 value);
    void TexParameterf(u32 param, f32 value);
    void TexImage(s32 level, u32 format, s32 width, s32 height, const void* pixels);
    void TexSubImage(s32 level, s32 x, s32 y, s32 width, s32 height, u32 format, const void* pixels, bool streamed = false);
    void GenerateMipmap();
    // The draw framebuffer's attachments
    void AttachColour(u32 texture, s32 level = 0);
    void AttachDepthStencil(u32 texture);
    void NoColour();
    // The bound program's uniforms
    void Uniform1i(s32 location, s32 x);
    void Uniform1ui(s32 location, u32 x);
    void Uniform1f(s32 location, f32 x);
    void Uniform2f(s32 location, f32 x, f32 y);
    void Uniform3f(s32 location, f32 x, f32 y, f32 z);
    void Uniform4f(s32 location, f32 x, f32 y, f32 z, f32 w);
    void Uniform4ui(s32 location, u32 x, u32 y, u32 z, u32 w);
    void Uniform1iv(s32 location, s32 count, const s32* values);
    void Clear(u32 bits);
    // The read framebuffer's rectangle into the draw framebuffer's (BITS: colour, or depth and stencil)
    void BlitFramebuffer(s32 sx0, s32 sy0, s32 sx1, s32 sy1, s32 dx0, s32 dy0, s32 dx1, s32 dy1, u32 bits, u32 filter);
    // The batch being made (list().vertexes after batchStart): the next draws' vertexes
    void VertexData();
    void DrawArrays(u32 primitive, s32 first, s32 count);
    void Label(const char* label);

    // At once: the list played first
    void Submit();
    void ReadPixels(s32 x, s32 y, s32 width, s32 height, u32 format, void* out);
    bool FramebufferComplete();
    bool OutOfMemory();
    void* Fence();
    void WaitFence(void* fence);

    // Programs (made at once: nothing recorded depends on them before)
    u32 StartProgram(const char* vertex, const std::string& fragment, bool dualSource)
    {
        return backend_->StartProgram(vertex, fragment, dualSource);
    }
    bool FinishProgram(u32 program) { return backend_->FinishProgram(program); }
    s32 UniformLocation(u32 program, const char* name) { return backend_->UniformLocation(program, name); }

private:
    void SetCap(u32 cap, u8 on);
    void Uniform(s32 location, UniformKind kind, u32 count, const void* values, bool array = false);
    void TexData(s32 level, u32 format, s32 x, s32 y, s32 width, s32 height, const void* pixels, bool sub, bool streamed);

    Backend* backend_ = nullptr;
    CommandList list_;
    RenderState state_ = {};
    u32 units_[TextureUnits] = {};
    u32 unit_ = 0;
    u32 drawFramebuffer_ = 0;
    u32 readFramebuffer_ = 0;
    u32 program_ = 0;
    bool vertexes_ = false;
    f32 clearColour_[4] = {};
    s32 clearStencil_ = 0;
    s32 store_[4] = {0, 4, 0, 4};
    // What the list already sets (a program's uniforms by location, a texture's sampling): a value set again isn't recorded
    // (the programs are the renderer's alone; a deleted texture's name forgotten)
    struct UniformSlot
    {
        u32 value[4];
        bool known;
    };
    std::vector<std::vector<UniformSlot>> uniforms_;
    struct Sampling
    {
        s32 value[ParamCount];
        f32 valueFloat[ParamCount];
        u8 known[ParamCount];
    };
    std::vector<Sampling> sampling_;
    bool SamplingSame(u32 param, s32 value, f32 valueFloat);
};

// The bytes of a rectangle of WIDTH x HEIGHT texels of FORMAT with rows ROWLENGTH texels long (0: WIDTH) at ALIGNMENT
size_t PixelBytes(Format format, s32 width, s32 height, s32 rowLength, s32 alignment);
}
