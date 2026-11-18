#include "gpu.h"

#include <algorithm>

// THE LIST 70: the commands recorded (gpu.h)

namespace Hw
{
size_t CommandList::AddData(const void* bytes, size_t size)
{
    const size_t at = (dataSize_ + 15) & ~size_t{15};
    if (at + size > dataCapacity_)
    {
        size_t capacity = std::max<size_t>(dataCapacity_ * 2, 256 * 1024);
        while (capacity < at + size)
        {
            capacity *= 2;
        }

        std::unique_ptr<u8[]> grown(new u8[capacity]);
        if (dataSize_ != 0)
        {
            std::memcpy(grown.get(), data_.get(), dataSize_);
        }

        data_ = std::move(grown);
        dataCapacity_ = capacity;
    }

    std::memcpy(data_.get() + at, bytes, size);
    dataSize_ = at + size;
    return at;
}

void CommandList::Grow(size_t least)
{
    size_t capacity = std::max<size_t>(capacity_ * 2, 64 * 1024);
    while (capacity < least)
    {
        capacity *= 2;
    }

    std::unique_ptr<u8[]> grown(new u8[capacity]);
    if (size_ != 0)
    {
        std::memcpy(grown.get(), commands_.get(), size_);
    }

    commands_ = std::move(grown);
    capacity_ = capacity;
}

void CommandList::Clear()
{
    size_ = 0;
    count_ = 0;
    dataSize_ = 0;
    if (batchStart != 0)
    {
        vertexes.erase(vertexes.begin(), vertexes.begin() + static_cast<std::ptrdiff_t>(batchStart));
        batchStart = 0;
    }
}

size_t PixelBytes(Format format, s32 width, s32 height, s32 rowLength, s32 alignment)
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }

    if (CompressedFormat(format))
    {
        return CompressedBytes(format, width, height);
    }

    size_t texel = 4;
    switch (format)
    {
    case FormatRgba16f:
        texel = 8;
        break;
    case FormatR16ui:
    case FormatRg8:
        texel = 2;
        break;
    case FormatR8:
        texel = 1;
        break;
    default:
        break;
    }

    const size_t align = alignment > 0 ? static_cast<size_t>(alignment) : 1;
    const size_t row = (static_cast<size_t>(rowLength > 0 ? rowLength : width) * texel + align - 1) / align * align;
    return row * static_cast<size_t>(height - 1) + static_cast<size_t>(width) * texel;
}

void Gpu::Start(Backend* backend)
{
    backend_ = backend;
    // OpenGL's starting state
    state_ = {};
    state_.blendOp = BlendAdd;
    state_.blendOpAlpha = BlendAdd;
    state_.blendSource = FactorOne;
    state_.blendDestination = FactorZero;
    state_.blendSourceAlpha = FactorOne;
    state_.blendDestinationAlpha = FactorZero;
    state_.colourMask = 0xF;
    state_.depthFunc = CompareLess;
    state_.depthWrite = 1;
    state_.stencilFunc = CompareAlways;
    state_.stencilReadMask = 0xFF;
    state_.stencilWriteMask = 0xFF;
    state_.stencilFail = StencilKeep;
    state_.stencilDepthFail = StencilKeep;
    state_.stencilPass = StencilKeep;
}

void Gpu::DeleteTextures(s32 count, const u32* names)
{
    if (count <= 0)
    {
        return;
    }

    auto& command = list_.Add<DeleteCommand>(Op::DeleteTextures, sizeof(DeleteCommand) + sizeof(u32) * static_cast<size_t>(count));
    command.count = static_cast<u32>(count);
    for (s32 i = 0; i < count; i++)
    {
        command.names[i] = names[i];
        if (names[i] < sampling_.size())
        {
            sampling_[names[i]] = {};
        }

        // (GL unbinds a deleted texture from the context's units)
        for (u32& unit : units_)
        {
            if (unit == names[i] && names[i] != 0)
            {
                unit = 0;
            }
        }
    }
}

void Gpu::DeleteFramebuffers(s32 count, const u32* names)
{
    if (count <= 0)
    {
        return;
    }

    auto& command = list_.Add<DeleteCommand>(Op::DeleteFramebuffers, sizeof(DeleteCommand) + sizeof(u32) * static_cast<size_t>(count));
    command.count = static_cast<u32>(count);
    for (s32 i = 0; i < count; i++)
    {
        command.names[i] = names[i];
        if (names[i] != 0 && drawFramebuffer_ == names[i])
        {
            drawFramebuffer_ = 0;
        }

        if (names[i] != 0 && readFramebuffer_ == names[i])
        {
            readFramebuffer_ = 0;
        }
    }
}

void Gpu::SetCap(u32 cap, u8 on)
{
    switch (cap)
    {
    case CapBlend:
        state_.blend = on;
        break;
    case CapDepthTest:
        state_.depthTest = on;
        break;
    case CapScissorTest:
        state_.scissorTest = on;
        break;
    case CapStencilTest:
        state_.stencilTest = on;
        break;
    case CapCullFace:
        state_.cullFace = on;
        break;
    case CapPointSize:
        state_.pointSize = on;
        break;
    }
}

void Gpu::Viewport(s32 x, s32 y, s32 width, s32 height)
{
    state_.viewport[0] = x;
    state_.viewport[1] = y;
    state_.viewport[2] = width;
    state_.viewport[3] = height;
}

void Gpu::Scissor(s32 x, s32 y, s32 width, s32 height)
{
    state_.scissor[0] = x;
    state_.scissor[1] = y;
    state_.scissor[2] = width;
    state_.scissor[3] = height;
}

void Gpu::BlendFuncSeparate(u32 source, u32 destination, u32 sourceAlpha, u32 destinationAlpha)
{
    state_.blendSource = static_cast<u8>(source);
    state_.blendDestination = static_cast<u8>(destination);
    state_.blendSourceAlpha = static_cast<u8>(sourceAlpha);
    state_.blendDestinationAlpha = static_cast<u8>(destinationAlpha);
}

void Gpu::BlendEquationSeparate(u32 colour, u32 alpha)
{
    state_.blendOp = static_cast<u8>(colour);
    state_.blendOpAlpha = static_cast<u8>(alpha);
}

void Gpu::BlendColor(f32 r, f32 g, f32 b, f32 a)
{
    state_.blendColour[0] = r;
    state_.blendColour[1] = g;
    state_.blendColour[2] = b;
    state_.blendColour[3] = a;
}

void Gpu::ColorMask(bool r, bool g, bool b, bool a)
{
    state_.colourMask = static_cast<u8>((r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0));
}

void Gpu::StencilFunc(u32 function, s32 reference, u32 mask)
{
    state_.stencilFunc = static_cast<u8>(function);
    state_.stencilReference = static_cast<u8>(reference);
    state_.stencilReadMask = static_cast<u8>(mask);
}

void Gpu::StencilOp(u32 fail, u32 depthFail, u32 pass)
{
    state_.stencilFail = static_cast<u8>(fail);
    state_.stencilDepthFail = static_cast<u8>(depthFail);
    state_.stencilPass = static_cast<u8>(pass);
}

void Gpu::ClearColor(f32 r, f32 g, f32 b, f32 a)
{
    clearColour_[0] = r;
    clearColour_[1] = g;
    clearColour_[2] = b;
    clearColour_[3] = a;
}

bool Gpu::SamplingSame(u32 param, s32 value, f32 valueFloat)
{
    const u32 texture = units_[unit_];
    if (texture == 0 || texture >= (1u << 20) || param >= ParamCount)
    {
        return false;
    }

    if (sampling_.size() <= texture)
    {
        sampling_.resize(texture + 1);
    }

    Sampling& known = sampling_[texture];
    if (known.known[param] && known.value[param] == value && known.valueFloat[param] == valueFloat)
    {
        return true;
    }

    known.known[param] = 1;
    known.value[param] = value;
    known.valueFloat[param] = valueFloat;
    return false;
}

void Gpu::TexParameter(u32 param, s32 value)
{
    if (SamplingSame(param, value, static_cast<f32>(value)))
    {
        return;
    }

    auto& command = list_.Add<TexParamCommand>(Op::TexParam);
    command.texture = units_[unit_];
    command.param = param;
    command.value = value;
    command.valueFloat = static_cast<f32>(value);
}

void Gpu::TexParameterf(u32 param, f32 value)
{
    if (SamplingSame(param, static_cast<s32>(value), value))
    {
        return;
    }

    auto& command = list_.Add<TexParamCommand>(Op::TexParam);
    command.texture = units_[unit_];
    command.param = param;
    command.value = static_cast<s32>(value);
    command.valueFloat = value;
}

void Gpu::TexData(s32 level, u32 format, s32 x, s32 y, s32 width, s32 height, const void* pixels, bool sub, bool streamed)
{
    auto& command = list_.Add<TexImageCommand>(sub ? Op::TexSubImage : Op::TexImage);
    command.texture = units_[unit_];
    command.level = level;
    command.format = format;
    command.x = x;
    command.y = y;
    command.width = width;
    command.height = height;
    command.rowLength = store_[StoreUnpackRowLength];
    command.alignment = store_[StoreUnpackAlignment];
    command.streamed = streamed ? 1 : 0;
    command.bytes = pixels != nullptr ? PixelBytes(static_cast<Format>(format), width, height, command.rowLength, command.alignment) : 0;
    // (The bytes are kept apart from the commands: the command stays where it is)
    command.data = command.bytes != 0 ? list_.AddData(pixels, command.bytes) : 0;
}

void Gpu::TexImage(s32 level, u32 format, s32 width, s32 height, const void* pixels)
{
    TexData(level, format, 0, 0, width, height, pixels, false, false);
}

void Gpu::TexSubImage(s32 level, s32 x, s32 y, s32 width, s32 height, u32 format, const void* pixels, bool streamed)
{
    TexData(level, format, x, y, width, height, pixels, true, streamed);
}

void Gpu::GenerateMipmap()
{
    auto& command = list_.Add<TextureCommand>(Op::Mipmaps);
    command.texture = units_[unit_];
}

void Gpu::AttachColour(u32 texture, s32 level)
{
    auto& command = list_.Add<AttachCommand>(Op::Attach);
    command.framebuffer = drawFramebuffer_;
    command.slot = 0;
    command.texture = texture;
    command.level = level;
}

void Gpu::AttachDepthStencil(u32 texture)
{
    auto& command = list_.Add<AttachCommand>(Op::Attach);
    command.framebuffer = drawFramebuffer_;
    command.slot = 1;
    command.texture = texture;
    command.level = 0;
}

void Gpu::NoColour()
{
    auto& command = list_.Add<AttachCommand>(Op::NoColour);
    command.framebuffer = drawFramebuffer_;
}

void Gpu::Uniform(s32 location, UniformKind kind, u32 count, const void* values, bool array)
{
    if (location < 0)
    {
        return;
    }

    if (!array && program_ != 0 && program_ < (1u << 20) && location < 4096)
    {
        if (uniforms_.size() <= program_)
        {
            uniforms_.resize(program_ + 1);
        }

        std::vector<UniformSlot>& slots = uniforms_[program_];
        if (slots.size() <= static_cast<size_t>(location))
        {
            slots.resize(static_cast<size_t>(location) + 1);
        }

        UniformSlot& slot = slots[static_cast<size_t>(location)];
        u32 words[4] = {};
        std::memcpy(words, values, count * sizeof(u32));
        bool same = slot.known;
        for (u32 i = 0; i < count; i++)
        {
            same = same && slot.value[i] == words[i];
            slot.value[i] = words[i];
        }

        slot.known = true;
        if (same)
        {
            return;
        }
    }

    const size_t extra = count > 4 ? (count - 4) * sizeof(u32) : 0;
    auto& command = list_.Add<UniformCommand>(Op::Uniform, sizeof(UniformCommand) + extra);
    command.program = program_;
    command.location = location;
    command.kind = kind;
    command.count = static_cast<u8>(count);
    command.array = array ? 1 : 0;
    std::memcpy(command.values, values, count * sizeof(u32));
}

void Gpu::Uniform1i(s32 location, s32 x)
{
    Uniform(location, UniformInt, 1, &x);
}

void Gpu::Uniform1ui(s32 location, u32 x)
{
    Uniform(location, UniformUint, 1, &x);
}

void Gpu::Uniform1f(s32 location, f32 x)
{
    Uniform(location, UniformFloat, 1, &x);
}

void Gpu::Uniform2f(s32 location, f32 x, f32 y)
{
    const f32 v[2] = {x, y};
    Uniform(location, UniformFloat, 2, v);
}

void Gpu::Uniform3f(s32 location, f32 x, f32 y, f32 z)
{
    const f32 v[3] = {x, y, z};
    Uniform(location, UniformFloat, 3, v);
}

void Gpu::Uniform4f(s32 location, f32 x, f32 y, f32 z, f32 w)
{
    const f32 v[4] = {x, y, z, w};
    Uniform(location, UniformFloat, 4, v);
}

void Gpu::Uniform4ui(s32 location, u32 x, u32 y, u32 z, u32 w)
{
    const u32 v[4] = {x, y, z, w};
    Uniform(location, UniformUint, 4, v);
}

void Gpu::Uniform1iv(s32 location, s32 count, const s32* values)
{
    Uniform(location, UniformInt, static_cast<u32>(count), values, true);
}

void Gpu::Clear(u32 bits)
{
    auto& command = list_.Add<ClearCommand>(Op::Clear);
    command.state = state_;
    command.framebuffer = drawFramebuffer_;
    command.bits = bits;
    std::memcpy(command.colour, clearColour_, sizeof(clearColour_));
    command.stencil = clearStencil_;
}

void Gpu::BlitFramebuffer(s32 sx0, s32 sy0, s32 sx1, s32 sy1, s32 dx0, s32 dy0, s32 dx1, s32 dy1, u32 bits, u32 filter)
{
    auto& command = list_.Add<BlitCommand>(Op::Blit);
    command.state = state_;
    command.readFramebuffer = readFramebuffer_;
    command.drawFramebuffer = drawFramebuffer_;
    command.source[0] = sx0;
    command.source[1] = sy0;
    command.source[2] = sx1;
    command.source[3] = sy1;
    command.destination[0] = dx0;
    command.destination[1] = dy0;
    command.destination[2] = dx1;
    command.destination[3] = dy1;
    command.bits = bits;
    command.filter = filter;
}

void Gpu::VertexData()
{
    auto& command = list_.Add<VerticesCommand>(Op::Vertices);
    command.first = list_.batchStart;
    command.count = list_.vertexes.size() - list_.batchStart;
    list_.batchStart = list_.vertexes.size();
}

void Gpu::DrawArrays(u32 primitive, s32 first, s32 count)
{
    auto& command = list_.Add<DrawCommand>(Op::Draw);
    command.state = state_;
    command.framebuffer = drawFramebuffer_;
    command.program = program_;
    std::memcpy(command.textures, units_, sizeof(units_));
    command.primitive = primitive;
    command.vertexes = vertexes_ ? 1 : 0;
    command.first = first;
    command.count = count;
}

void Gpu::Label(const char* label)
{
    auto& command = list_.Add<LabelCommand>(Op::Label);
    command.label = label;
}

void Gpu::Submit()
{
    if (list_.empty())
    {
        return;
    }

    backend_->Play(list_);
    list_.Clear();
}

void Gpu::ReadPixels(s32 x, s32 y, s32 width, s32 height, u32 format, void* out)
{
    Submit();
    backend_->Read(readFramebuffer_, x, y, width, height, static_cast<Format>(format), store_[StorePackRowLength], store_[StorePackAlignment], out);
}

bool Gpu::FramebufferComplete()
{
    Submit();
    return backend_->FramebufferComplete(drawFramebuffer_);
}

bool Gpu::OutOfMemory()
{
    Submit();
    return backend_->OutOfMemory();
}

void* Gpu::Fence()
{
    Submit();
    return backend_->Fence();
}

void Gpu::WaitFence(void* fence)
{
    Submit();
    backend_->WaitFence(fence);
}
}
