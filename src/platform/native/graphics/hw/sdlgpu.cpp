#include "gpu.h"

// THE LIST 71: the hardware renderer's command lists played with SDL's GPU API on Vulkan (gpu.h). The device is SDL's "vulkan"
// driver with SPIR-V shaders only; the renderer's GLSL 3.30 programs are made SPIR-V while the game runs, with glslang as a
// library: their loose uniforms gathered into a std140 block a stage (set 1 the vertex shader's, set 3 the fragment shader's, SDL's
// layout), their float samplers given set 2's bindings in their order and their integer ones the storage textures' after them (SDL
// GPU samples no integer texture: a utexture2D's texelFetch), the varyings' locations matched by name, an array's elements found by
// their names too ("pageMask[3]"). What OpenGL does that SDL GPU doesn't is made here:
// - the GL calls' state (blending, depth, stencil, masks: RenderState) makes a pipeline, made when first drawn with and kept;
// - the vertex colour's bytes are OpenGL's floats of 0-255 (not normalised): read as integers and made floats in the shader;
// - a clear or a framebuffer's copy (a blit) is a draw (OpenGL's clear keeps to the scissor and the masks; a copy reaches past the
//   source's edges, and of depth: SDL's blits are of colour only), and so is a read back of depth (a copy into a float texture);
// - dual-source blending, which SDL GPU hasn't: a program drawn with its second output's alpha as the blend factor outputs that
//   factor as its alpha (the "factor" variant: the same blend) with the target's alpha left alone, then where the alpha is written
//   too the program draws it alone in a second pass (OpenGL's blending exactly: measured, the pictures the same as OpenGL's;
//   the shader's own blend instead, the GS's arithmetic, is a unit off OpenGL's rounding);
// - the depth range: GL's -1..1 made Vulkan's 0..1 at each vertex shader's end, so the depths are OpenGL's;
// - rows: SDL's Vulkan viewport is flipped (NDC +1 at its first row, OpenGL's -1), so each vertex shader's y is negated and a
//   texture's first row is OpenGL's y 0 in both (gl_FragCoord too);
// - new textures zeroed (OpenGL's drivers do, the renderer relies on it), their levels down to the short side's 1 (SDL sizes a
//   level without OpenGL's least of 1).
// Commands go into a command buffer submitted when a read back or fence wants them done, or after enough work (at most two in
// flight; with a window claimed, at the end of each playing: g_DeviceLock's comment). With $TWIN_VULKAN_DEBUG the validation
// layers' VUID-vkCmdDraw-None-09600 comes from SDL: its memory defragmentation at a submit leaves the moved textures' old images in
// TRANSFER_SRC after the draws that sampled them (SDL_gpu_vulkan.c 3.4.18, VULKAN_INTERNAL_DefragmentMemory), and the layers
// check a submit's sampled images against their last layouts.
// THE LIST 72: the window's backend is one of these too, on the same device (StartVulkanWindowBackend: the window claimed,
// framebuffer 0 a texture of its size copied into the swapchain's when shown, the renderer's picture taken as its own texture).

#if TWIN_VULKAN

#include <SDL3/SDL.h>

#include "graphics/renderwork.h"

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Hw
{
namespace
{
// Vulkan's formats of gpu.h's
constexpr SDL_GPUTextureFormat Formats[] = {
    SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
    SDL_GPU_TEXTUREFORMAT_R32_UINT,       SDL_GPU_TEXTUREFORMAT_R16_UINT,           SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
    SDL_GPU_TEXTUREFORMAT_R8_UNORM,       SDL_GPU_TEXTUREFORMAT_R8G8_UNORM,
    SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM, SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM,     SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM,
    SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM,
};
// (The block-compressed formats' bytes are CompressedBytes's)
constexpr u32 TexelBytes[] = {4, 8, 4, 4, 2, 8, 1, 2, 0, 0, 0, 0};
constexpr bool IntegerFormat[] = {false, false, false, true, true, false, false, false, false, false, false, false};
constexpr SDL_GPUCompareOp Compares[] = {SDL_GPU_COMPAREOP_NEVER,   SDL_GPU_COMPAREOP_LESS,      SDL_GPU_COMPAREOP_EQUAL,
                                         SDL_GPU_COMPAREOP_LESS_OR_EQUAL, SDL_GPU_COMPAREOP_GREATER, SDL_GPU_COMPAREOP_NOT_EQUAL,
                                         SDL_GPU_COMPAREOP_GREATER_OR_EQUAL, SDL_GPU_COMPAREOP_ALWAYS};
// (The constant alpha factors: the constant colour's, the constant set to the alpha in every channel; the second output's alpha:
// the first's, in the "factor" variant)
constexpr SDL_GPUBlendFactor Factors[] = {
    SDL_GPU_BLENDFACTOR_ZERO,           SDL_GPU_BLENDFACTOR_ONE,       SDL_GPU_BLENDFACTOR_SRC_ALPHA,
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_GPU_BLENDFACTOR_DST_ALPHA, SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA,
    SDL_GPU_BLENDFACTOR_CONSTANT_COLOR, SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR, SDL_GPU_BLENDFACTOR_SRC_ALPHA,
    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
};
constexpr SDL_GPUBlendOp BlendOps[] = {SDL_GPU_BLENDOP_ADD, SDL_GPU_BLENDOP_SUBTRACT, SDL_GPU_BLENDOP_REVERSE_SUBTRACT};
constexpr SDL_GPUStencilOp StencilOps[] = {SDL_GPU_STENCILOP_KEEP, SDL_GPU_STENCILOP_ZERO, SDL_GPU_STENCILOP_REPLACE};
constexpr SDL_GPUPrimitiveType Primitives[] = {SDL_GPU_PRIMITIVETYPE_POINTLIST, SDL_GPU_PRIMITIVETYPE_LINELIST,
                                               SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP};

// The time in SDL GPU's calls counted as the API's while the GS is emulated (renderwork.h: OpenGL's calls for the GL backend; the
// log's split names the API), a read back's wait as the card's
// A span of Vulkan's part counted to one of its parts (renderwork.h's VulkanPart)
class VulkanPartSpan
{
public:
    explicit VulkanPartSpan(NativeGraphics::VulkanPart part) : part_(part), start_(SDL_GetTicksNS())
    {
    }
    ~VulkanPartSpan()
    {
        NativeGraphics::CountVulkanPart(part_, SDL_GetTicksNS() - start_);
    }
    VulkanPartSpan(const VulkanPartSpan&) = delete;
    VulkanPartSpan& operator=(const VulkanPartSpan&) = delete;

private:
    NativeGraphics::VulkanPart part_;
    u64 start_;
};

class ApiSpan
{
public:
    explicit ApiSpan(NativeGraphics::Work kind)
    {
        if (NativeGraphics::t_Work == NativeGraphics::Work::Gs)
        {
            before_ = NativeGraphics::SwitchWork(kind);
            on_ = true;
        }
    }

    ~ApiSpan()
    {
        if (on_)
        {
            NativeGraphics::SwitchWork(before_);
        }
    }

    ApiSpan(const ApiSpan&) = delete;
    ApiSpan& operator=(const ApiSpan&) = delete;

private:
    NativeGraphics::Work before_ = NativeGraphics::Work::None;
    bool on_ = false;
};

bool UsesFactor(u8 factor)
{
    return factor == FactorSrc1Alpha || factor == FactorOneMinusSrc1Alpha;
}

bool UsesConstant(u8 factor)
{
    return factor == FactorConstantAlpha || factor == FactorOneMinusConstantAlpha;
}

// ---- GLSL 3.30 as Vulkan's GLSL ----

// A uniform of a block: its std140 place
struct Member
{
    std::string type;
    std::string name;
    u32 array;
    u32 offset;
};

struct Stage
{
    std::string text;
    std::vector<Member> members;
    u32 blockSize = 0;
    // Samplers in their order: name, an integer one (usampler2D: a storage texture, bound after the float ones)
    std::vector<std::pair<std::string, bool>> samplers;
    u32 Count(bool integer) const
    {
        u32 n = 0;
        for (const auto& s : samplers)
        {
            n += s.second == integer ? 1 : 0;
        }

        return n;
    }
};

bool Std140(const std::string& type, u32* size, u32* align)
{
    static const struct
    {
        const char* name;
        u32 size;
        u32 align;
    } types[] = {
        {"float", 4, 4}, {"int", 4, 4},    {"uint", 4, 4},   {"bool", 4, 4},   {"vec2", 8, 8},   {"ivec2", 8, 8},
        {"uvec2", 8, 8}, {"vec3", 12, 16}, {"ivec3", 12, 16}, {"uvec3", 12, 16}, {"vec4", 16, 16}, {"ivec4", 16, 16},
        {"uvec4", 16, 16}, {"mat2", 32, 16}, {"mat3", 48, 16}, {"mat4", 64, 16},
    };
    for (const auto& t : types)
    {
        if (type == t.name)
        {
            *size = t.size;
            *align = t.align;
            return true;
        }
    }

    return false;
}

std::string Trimmed(const std::string& line)
{
    size_t start = line.find_first_not_of(" \t");
    return start == std::string::npos ? std::string() : line.substr(start);
}

void ReplaceAll(std::string& text, const std::string& from, const std::string& to)
{
    for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
    {
        text.replace(at, from.size(), to);
    }
}

// A shader's GLSL made Vulkan's (VARYINGS: the vertex shader's outputs' locations by name, filled by it and read by the fragment
// shader). FACTOR: the fragment shader's blend factor (its second output's alpha) made its output's alpha. False with what's wrong
bool Translate(const std::string& source, bool vertex, bool factor, std::unordered_map<std::string, u32>& varyings, Stage& out,
               std::string& error)
{
    std::vector<std::string> lines;
    for (size_t start = 0; start <= source.size();)
    {
        size_t end = source.find('\n', start);
        end = end == std::string::npos ? source.size() : end;
        lines.push_back(source.substr(start, end - start));
        start = end + 1;
    }

    // The float samplers first (set 2's first bindings), then the integer ones: SDL GPU samples no integer texture, so those are its
    // read-only storage textures, which its Vulkan driver declares sampled images: a utexture2D's texelFetch (without a sampler)
    u32 floatSamplers = 0;
    for (const std::string& line : lines)
    {
        const std::string t = Trimmed(line);
        floatSamplers += t.rfind("uniform sampler", 0) == 0 ? 1 : 0;
    }

    u32 floatsSeen = 0;
    u32 integersSeen = 0;
    int depth = 0;
    size_t preamble = 0;
    bool inPreamble = true;
    bool hasFactor = false;
    std::string bytesInput;
    for (size_t i = 0; i < lines.size(); i++)
    {
        std::string& line = lines[i];
        const std::string t = Trimmed(line);
        if (inPreamble)
        {
            if (t.empty() || t.rfind("#version", 0) == 0 || t.rfind("#define", 0) == 0 || t.rfind("#extension", 0) == 0)
            {
                preamble = i + 1;
            }
            else
            {
                inPreamble = false;
            }
        }

        if (t.rfind("#version", 0) == 0)
        {
            line = "#version 450 core\n#extension GL_EXT_samplerless_texture_functions : require";
            continue;
        }

        if (depth == 0 && t.rfind("uniform ", 0) == 0)
        {
            // uniform TYPE NAME; or uniform TYPE NAME[N];
            size_t typeStart = 8;
            size_t typeEnd = t.find(' ', typeStart);
            size_t semicolon = t.find(';');
            if (typeEnd == std::string::npos || semicolon == std::string::npos)
            {
                error = "a uniform not understood: " + t;
                return false;
            }

            std::string type = t.substr(typeStart, typeEnd - typeStart);
            std::string name = Trimmed(t.substr(typeEnd, semicolon - typeEnd));
            u32 array = 0;
            size_t bracket = name.find('[');
            if (bracket != std::string::npos)
            {
                array = static_cast<u32>(std::strtoul(name.c_str() + bracket + 1, nullptr, 10));
                name = name.substr(0, bracket);
            }

            if (type.find("sampler") != std::string::npos)
            {
                if (vertex)
                {
                    error = "a vertex shader's sampler";
                    return false;
                }

                const bool integer = type[0] == 'u' || type[0] == 'i';
                const u32 binding = integer ? floatSamplers + integersSeen++ : floatsSeen++;
                line = "layout(set = 2, binding = " + std::to_string(binding) + ") uniform " +
                       (integer ? std::string(1, type[0]) + "texture2D " : type + " ") + name + ";";
                out.samplers.emplace_back(name, integer);
                continue;
            }

            u32 size = 0;
            u32 align = 0;
            if (!Std140(type, &size, &align))
            {
                error = "a uniform's type not known: " + t;
                return false;
            }

            bool known = false;
            for (const Member& m : out.members)
            {
                known = known || m.name == name;
            }

            if (!known)
            {
                Member m{type, name, array, 0};
                if (array != 0)
                {
                    align = 16;
                    size = ((size + 15) & ~15u) * array;
                }

                m.offset = (out.blockSize + align - 1) & ~(align - 1);
                out.blockSize = m.offset + size;
                out.members.push_back(m);
            }

            line.clear();
            continue;
        }

        if (vertex && depth == 0 && t.rfind("layout(location = 2) in vec4 ", 0) == 0)
        {
            // The vertex colour: OpenGL's bytes as floats of 0-255 (not normalised); SDL GPU's byte formats are normalised or
            // integers, so the integers, made floats here (exactly)
            const size_t start = std::strlen("layout(location = 2) in vec4 ");
            bytesInput = t.substr(start, t.find(';') - start);
            line = "layout(location = 2) in uvec4 twinBytes;\nvec4 " + bytesInput + ";";
            continue;
        }

        if (depth == 0 && t.find("index = 1)") != std::string::npos)
        {
            // The blend's second source: a variable of the shader's own
            size_t name = t.find(" out ");
            if (name == std::string::npos)
            {
                error = "a second output not understood: " + t;
                return false;
            }

            line = t.substr(name + 5);
            hasFactor = true;
            continue;
        }

        if (depth == 0 && t.find("index = 0)") != std::string::npos)
        {
            line = "layout(location = 0) " + t.substr(t.find(')') + 2);
            continue;
        }

        if (depth == 0 && t.rfind("layout", 0) != 0 && !t.empty() && t[0] != '#')
        {
            // [interpolation] in|out TYPE NAME;
            std::string rest = t;
            for (const char* q : {"noperspective ", "flat ", "smooth ", "centroid "})
            {
                if (rest.rfind(q, 0) == 0)
                {
                    rest = rest.substr(std::strlen(q));
                }
            }

            const bool isIn = rest.rfind("in ", 0) == 0;
            const bool isOut = rest.rfind("out ", 0) == 0;
            if ((isIn || isOut) && t.back() == ';')
            {
                size_t semicolon = t.rfind(';');
                size_t space = t.rfind(' ', semicolon);
                std::string name = t.substr(space + 1, semicolon - space - 1);
                if (vertex && isIn)
                {
                    error = "a vertex input without its location: " + t;
                    return false;
                }

                u32 location = 0;
                if (vertex || isIn)
                {
                    auto found = varyings.find(name);
                    if (found == varyings.end())
                    {
                        // (The next free location kept under a name no variable has: an array takes a location an element)
                        u32& next = varyings[" next"];
                        location = next;
                        const size_t bracket = name.find('[');
                        next += bracket == std::string::npos ? 1 : std::max<u32>(static_cast<u32>(std::strtoul(name.c_str() + bracket + 1, nullptr, 10)), 1);
                        varyings[name] = location;
                    }
                    else
                    {
                        location = found->second;
                    }
                }

                line = "layout(location = " + std::to_string(location) + ") " + t;
                continue;
            }
        }

        for (char c : line)
        {
            depth += c == '{' ? 1 : c == '}' ? -1 : 0;
        }
    }

    out.blockSize = (out.blockSize + 15) & ~15u;
    std::string block;
    if (!out.members.empty())
    {
        block = std::string("layout(std140, set = ") + (vertex ? "1" : "3") + ", binding = 0) uniform TwinUniforms\n{\n";
        for (const Member& m : out.members)
        {
            block += "    " + m.type + " " + m.name + (m.array != 0 ? "[" + std::to_string(m.array) + "]" : "") + ";\n";
        }

        block += "};";
    }

    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(preamble), block);
    std::string text;
    for (const std::string& line : lines)
    {
        text += line;
        text += '\n';
    }

    ReplaceAll(text, "gl_VertexID", "gl_VertexIndex");
    ReplaceAll(text, "gl_InstanceID", "gl_InstanceIndex");
    // The shader's main made a function of its own, then what Vulkan needs after it
    const size_t main = text.find("void main()");
    if (main == std::string::npos)
    {
        error = "no main";
        return false;
    }

    text.replace(main, 11, "void twinMain()");
    if (vertex)
    {
        text += "void main()\n{\n" + (bytesInput.empty() ? std::string() : "    " + bytesInput + " = vec4(twinBytes);\n") +
                "    twinMain();\n    gl_Position.y = -gl_Position.y;\n    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n}\n";
    }
    else if (factor && hasFactor)
    {
        text += "void main()\n{\n    twinMain();\n    outColour.a = outFactor.a;\n}\n";
    }
    else
    {
        text += "void main()\n{\n    twinMain();\n}\n";
    }

    out.text = std::move(text);
    return true;
}

bool CompileSpirv(bool vertex, const std::string& text, std::vector<u32>& spirv, std::string& log)
{
    const EShLanguage stage = vertex ? EShLangVertex : EShLangFragment;
    glslang::TShader shader(stage);
    const char* strings[] = {text.c_str()};
    shader.setStrings(strings, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, ECoreProfile, false, false, messages))
    {
        log = shader.getInfoLog();
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages))
    {
        log = program.getInfoLog();
        return false;
    }

    glslang::SpvOptions options;
    options.generateDebugInfo = false;
    options.disableOptimizer = true;
    options.validate = false;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv, &options);
    return !spirv.empty();
}

// Threads that make programs' SPIR-V side by side (the renderer asks for a start's programs all at once)
class Compilers
{
public:
    void Start(u32 most)
    {
        const u32 count = std::min(std::clamp<u32>(std::thread::hardware_concurrency(), 2, 9) - 1, most);
        for (u32 i = 0; i < count; i++)
        {
            threads_.emplace_back([this] { Run(); });
        }
    }

    void Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }

        wake_.notify_all();
        for (std::thread& t : threads_)
        {
            t.join();
        }

        threads_.clear();
    }

    void Add(std::function<void()> job)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.push_back(std::move(job));
        }

        wake_.notify_one();
    }

    // Waits for FLAG set by a job (signalled through done())
    void WaitFor(const std::atomic<int>& flag)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [&] { return flag.load() != 0; });
    }

    void Signal()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
        }

        done_.notify_all();
    }

private:
    void Run()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [&] { return stop_ || !jobs_.empty(); });
                if (stop_ && jobs_.empty())
                {
                    return;
                }

                job = std::move(jobs_.front());
                jobs_.pop_front();
            }

            job();
        }
    }

    std::vector<std::thread> threads_;
    std::deque<std::function<void()>> jobs_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;
    bool stop_ = false;
};

// ---- The backend's objects ----

struct Texture
{
    SDL_GPUTexture* texture = nullptr;
    u32 format = 0;
    s32 width = 0;
    s32 height = 0;
    u32 levels = 0;
    bool live = false;
    // OpenGL's sampling (its defaults)
    u32 minFilter = FilterNearestMipLinear;
    u32 magFilter = FilterLinear;
    u32 wrapS = WrapRepeat;
    u32 wrapT = WrapRepeat;
    s32 maxLevel = 1000;
    f32 anisotropy = 1.0f;
    SDL_GPUSampler* sampler = nullptr;
    // Another backend's (THE LIST 72: the renderer's picture, taken by the window's backend): never released here
    bool taken = false;
};

struct Framebuffer
{
    u32 colour = 0;
    s32 colourLevel = 0;
    u32 depth = 0;
    bool noColour = false;
    bool live = false;
};

struct Slot
{
    s32 vertexOffset = -1;
    s32 fragmentOffset = -1;
    s32 sampler = -1;
    bool array = false;
};

struct Program
{
    bool live = false;
    bool dualSource = false;
    std::unordered_map<std::string, s32> locations;
    std::vector<Slot> slots;
    Stage vertex;
    Stage fragment;
    std::string fragmentSource;
    std::unordered_map<std::string, u32> varyings;
    std::vector<u8> vertexUniforms;
    std::vector<u8> fragmentUniforms;
    bool vertexDirty = true;
    bool fragmentDirty = true;
    // The samplers' texture units (Uniform1i on a sampler's location)
    std::vector<u32> units;
    // Made on the compilers' threads: 1 done, 2 failed
    std::atomic<int> state{0};
    std::vector<u32> vertexSpirv;
    std::vector<u32> fragmentSpirv;
    std::string log;
    SDL_GPUShader* vertexShader = nullptr;
    SDL_GPUShader* fragmentShader = nullptr;
    // The "factor" variant's fragment shader, made when first drawn with
    SDL_GPUShader* factorShader = nullptr;
    bool factorFailed = false;
};

// What a pipeline is made of (compared as bytes: made zeroed)
struct PipelineKey
{
    u32 program;
    u8 variant;
    u8 primitive;
    u8 vertexes;
    u8 colourFormat;
    u8 depthTarget;
    u8 blend;
    u8 blendOp;
    u8 blendOpAlpha;
    u8 blendSource;
    u8 blendDestination;
    u8 blendSourceAlpha;
    u8 blendDestinationAlpha;
    u8 colourMask;
    u8 depthTest;
    u8 depthFunc;
    u8 depthWrite;
    u8 stencilTest;
    u8 stencilFunc;
    u8 stencilFail;
    u8 stencilDepthFail;
    u8 stencilPass;
    u8 stencilReadMask;
    u8 stencilWriteMask;
    u8 cullFace;
    bool operator==(const PipelineKey& other) const { return std::memcmp(this, &other, sizeof(*this)) == 0; }
};

struct PipelineHash
{
    size_t operator()(const PipelineKey& key) const
    {
        const u8* bytes = reinterpret_cast<const u8*>(&key);
        u64 h = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(key); i++)
        {
            h = (h ^ bytes[i]) * 1099511628211ull;
        }

        return static_cast<size_t>(h);
    }
};

constexpr u8 NoColourFormat = 0xFF;

// The backend's own programs (their numbers past the renderer's)
enum Own : u32
{
    OwnClear,
    OwnBlit,
    OwnBlitUint,
    OwnBlitDepth,
    OwnCount,
};

constexpr const char* FullVertexSource = R"(#version 330 core
void main()
{
    vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";
constexpr const char* ClearFragmentSource = R"(#version 330 core
uniform vec4 colour;
out vec4 outColour;
void main()
{
    outColour = colour;
}
)";
// A framebuffer's rectangle copied (glBlitFramebuffer's sampling: each pixel's centre carried into the source's rectangle)
constexpr const char* BlitFragmentSource = R"(#version 330 core
uniform sampler2D source;
uniform vec4 sourceRect;
uniform vec4 destinationRect;
uniform vec2 sourceSize;
uniform int linear;
out vec4 outColour;
void main()
{
    vec2 s = sourceRect.xy + (gl_FragCoord.xy - destinationRect.xy) * ((sourceRect.zw - sourceRect.xy) / (destinationRect.zw - destinationRect.xy));
    outColour = linear != 0 ? textureLod(source, s / sourceSize, 0.0) : texelFetch(source, ivec2(floor(s)), 0);
}
)";
constexpr const char* BlitUintFragmentSource = R"(#version 330 core
uniform usampler2D source;
uniform vec4 sourceRect;
uniform vec4 destinationRect;
out uvec4 outColour;
void main()
{
    vec2 s = sourceRect.xy + (gl_FragCoord.xy - destinationRect.xy) * ((sourceRect.zw - sourceRect.xy) / (destinationRect.zw - destinationRect.xy));
    outColour = texelFetch(source, ivec2(floor(s)), 0);
}
)";
constexpr const char* BlitDepthFragmentSource = R"(#version 330 core
uniform sampler2D source;
uniform vec4 sourceRect;
uniform vec4 destinationRect;
void main()
{
    vec2 s = sourceRect.xy + (gl_FragCoord.xy - destinationRect.xy) * ((sourceRect.zw - sourceRect.xy) / (destinationRect.zw - destinationRect.xy));
    gl_FragDepth = texelFetch(source, ivec2(floor(s)), 0).r;
}
)";

// THE LIST 72: one device for the window's backend (the main thread's) and the renderer's (the render thread's), made by the first
// to start (the window's, at the window's making: the window is claimed for it there) and destroyed with the last (the overlay's
// shots, which share nothing, have one of their own); glslang's process started with the first device and ended with the last.
// SDL GPU's calls are safe from both threads (each records command buffers of its own; resources made and released anywhere); the
// queue runs the command buffers in their submitting's order, so a picture the renderer submitted is there for the window's command
// buffer submitted after it. One thing isn't: SDL defragments its memory at a submit (SDL_gpu_vulkan.c 3.4.18's VULKAN_Submit, with a
// window claimed only at the submit that presents it), moving the textures of a fragmented allocation into new images after a copy
// of them; it spares an allocation with transfers pending, not a texture drawn into or sampled by a command buffer still being
// recorded on another thread, whose draws before the move then land in the old image, after the copy: lost (the settings' shots
// headless, the renderer drawing beside them, lost a backdrop that way). So while a window is claimed the renderer's backend
// records only under g_RecordingLock (from a command buffer's start to its submit, which ends each call of it), and the window's
// presenting submit takes it too
std::mutex g_DeviceLock;
SDL_GPUDevice* g_Device = nullptr;
u32 g_DeviceUsers = 0;
u32 g_Devices = 0;
std::mutex g_RecordingLock;
std::atomic<bool> g_WindowClaimed{false};

// THE LIST 97 ($TWIN_VULKAN_STALLS): the command buffers submitted with a fence, both backends', and a watchdog thread that notes
// when each one is seen done (it looks every 20 ms); a wait on the card of a second or more then logs those not done when it
// began, in their submitting's order (the queue runs them in that order: the first not done is what the card was busy with or,
// a presenting one, waiting for the swapchain's picture), and the textures released since. A wait whose command buffers were all
// done long before it ended was SDL's own work after vkWaitForFences, under its submit lock: the textures' destroying (LetGo)
struct Flight
{
    u64 number = 0;
    u64 submitted = 0;
    u64 done = 0;
    SDL_GPUDevice* device = nullptr;
    bool window = false;
    bool presenting = false;
    u32 draws = 0;
    size_t uploaded = 0;
};
std::mutex g_FlightLock;
std::unordered_map<SDL_GPUFence*, Flight> g_Flights;
u64 g_FlightNumber = 0;
std::atomic<u64> g_AcquireStart{0};
std::atomic<u64> g_LastAcquire{0};

bool TracingStalls()
{
    static const bool tracing = std::getenv("TWIN_VULKAN_STALLS") != nullptr;
    return tracing;
}

// (The trace's count of SDL's framebuffers: the levels of each texture a render pass has begun on, cleared or drawn into; and the
// textures released, with their levels' views, since the last wait logged)
std::unordered_map<SDL_GPUTexture*, u64> g_TargetLevels;  // (levels cleared in the low half, drawn into in the high)
u64 g_ReleasedTextures = 0;
u64 g_ReleasedViews = 0;
u64 g_ReleasedTargets = 0;

void NoteTarget(SDL_GPUTexture* texture, u32 level, bool drawn)
{
    if (TracingStalls() && texture != nullptr)
    {
        std::lock_guard<std::mutex> lock(g_FlightLock);
        g_TargetLevels[texture] |= (1ull << std::min<u32>(level, 31)) << (drawn ? 32 : 0);
    }
}

void ReleaseTexture(SDL_GPUDevice* device, SDL_GPUTexture* texture, u32 levels)
{
    if (TracingStalls() && texture != nullptr)
    {
        std::lock_guard<std::mutex> lock(g_FlightLock);
        auto found = g_TargetLevels.find(texture);
        if (found != g_TargetLevels.end())
        {
            g_ReleasedTargets += static_cast<u64>(__builtin_popcount(static_cast<u32>(found->second | found->second >> 32)));
            g_TargetLevels.erase(found);
        }

        g_ReleasedTextures++;
        g_ReleasedViews += levels;
    }

    SDL_ReleaseGPUTexture(device, texture);
}

void Watchdog()
{
    for (;;)
    {
        SDL_Delay(20);
        std::lock_guard<std::mutex> lock(g_FlightLock);
        for (auto& entry : g_Flights)
        {
            if (entry.second.done == 0 && SDL_QueryGPUFence(entry.second.device, entry.first))
            {
                entry.second.done = SDL_GetTicksNS();
            }
        }
    }
}

void TrackFlight(SDL_GPUFence* fence, SDL_GPUDevice* device, bool window, bool presenting, u32 draws, size_t uploaded)
{
    if (fence == nullptr || !TracingStalls())
    {
        return;
    }

    static std::once_flag started;
    std::call_once(started, [] { std::thread(Watchdog).detach(); });
    std::lock_guard<std::mutex> lock(g_FlightLock);
    Flight& f = g_Flights[fence];
    f = Flight{};
    f.number = ++g_FlightNumber;
    f.submitted = SDL_GetTicksNS();
    f.device = device;
    f.window = window;
    f.presenting = presenting;
    f.draws = draws;
    f.uploaded = uploaded;
}

void ReleaseFence(SDL_GPUDevice* device, SDL_GPUFence* fence)
{
    if (TracingStalls())
    {
        std::lock_guard<std::mutex> lock(g_FlightLock);
        g_Flights.erase(fence);
    }

    SDL_ReleaseGPUFence(device, fence);
}

// SDL_WaitForGPUFences for one, with the stall's command buffers logged when it took a second or more
void WaitForCard(SDL_GPUDevice* device, SDL_GPUFence* fence, bool window)
{
    if (!TracingStalls())
    {
        SDL_WaitForGPUFences(device, true, &fence, 1);
        return;
    }

    const u64 start = SDL_GetTicksNS();
    SDL_WaitForGPUFences(device, true, &fence, 1);
    const u64 end = SDL_GetTicksNS();
    if (end - start < 1'000'000'000)
    {
        return;
    }

    std::vector<std::pair<u64, std::string>> lines;
    char released[200];
    {
        std::lock_guard<std::mutex> lock(g_FlightLock);
        u64 targets = 0;
        u64 drawn = 0;
        for (const auto& entry : g_TargetLevels)
        {
            targets += static_cast<u64>(__builtin_popcount(static_cast<u32>(entry.second | entry.second >> 32)));
            drawn += static_cast<u64>(__builtin_popcount(static_cast<u32>(entry.second >> 32)));
        }

        std::snprintf(released, sizeof(released),
                      "; since the last: %llu textures released, %llu levels, %llu of them render targets; %llu texture levels "
                      "render targets now (%llu drawn into, the others only cleared)",
                      static_cast<unsigned long long>(g_ReleasedTextures), static_cast<unsigned long long>(g_ReleasedViews),
                      static_cast<unsigned long long>(g_ReleasedTargets), static_cast<unsigned long long>(targets),
                      static_cast<unsigned long long>(drawn));
        g_ReleasedTextures = 0;
        g_ReleasedViews = 0;
        g_ReleasedTargets = 0;
        for (const auto& entry : g_Flights)
        {
            const Flight& f = entry.second;
            if (f.submitted > end || (f.done != 0 && f.done < start))
            {
                continue;
            }

            char line[200];
            std::snprintf(line, sizeof(line), "#%llu %s%s, %u draws, %.1f MB uploaded, submitted %.1f ms %s the wait, ",
                          static_cast<unsigned long long>(f.number), f.window ? "the window's" : "the renderer's",
                          f.presenting ? " presenting" : "", f.draws, static_cast<double>(f.uploaded) / 1e6,
                          std::abs(static_cast<double>(f.submitted) - static_cast<double>(start)) / 1e6,
                          f.submitted <= start ? "before" : "into");
            char done[48] = "not seen done";
            if (f.done != 0)
            {
                std::snprintf(done, sizeof(done), "seen done at +%.0f ms",
                              (static_cast<double>(f.done) - static_cast<double>(start)) / 1e6);
            }

            lines.emplace_back(f.number, std::string(line) + done);
        }
    }

    std::sort(lines.begin(), lines.end());
    std::string text;
    for (const auto& line : lines)
    {
        text += "; " + line.second;
    }

    const u64 acquiring = g_AcquireStart.load();
    char acquire[96];
    if (acquiring != 0)
    {
        std::snprintf(acquire, sizeof(acquire), "the window's thread taking a swapchain picture for %.0f ms",
                      static_cast<double>(end - acquiring) / 1e6);
    }
    else
    {
        std::snprintf(acquire, sizeof(acquire), "the window's last swapchain picture taken in %.1f ms",
                      static_cast<double>(g_LastAcquire.load()) / 1e6);
    }

    std::fprintf(stderr, "graphics: Vulkan: the %s's thread waited %.0f ms for the card (%s)%s%s\n",
                 window ? "window" : "renderer", static_cast<double>(end - start) / 1e6, acquire, text.c_str(), released);
}

// A device of SDL GPU's Vulkan (null when there's none: the reason printed)
SDL_GPUDevice* MakeDevice()
{
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "graphics: Vulkan: no video: %s\n", SDL_GetError());
        return nullptr;
    }

    // Vulkan's device with large points (the GS's points drawn as squares of the scale's size), else without
    SDL_GPUDevice* device = nullptr;
    for (int attempt = 0; attempt < 2 && device == nullptr; attempt++)
    {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "vulkan");
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, std::getenv("TWIN_VULKAN_DEBUG") != nullptr);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, false);
        // VkPhysicalDeviceFeatures' largePoints (the 17th of its 55 booleans)
        u32 features[55] = {};
        features[16] = 1;
        SDL_GPUVulkanOptions options = {};
        options.vulkan_api_version = (1u << 22);
        options.vulkan_10_physical_device_features = features;
        if (attempt == 0)
        {
            SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &options);
        }

        device = SDL_CreateGPUDeviceWithProperties(props);
        SDL_DestroyProperties(props);
        if (device == nullptr && attempt == 0)
        {
            std::fprintf(stderr, "graphics: Vulkan: no device with large points (%s): trying without\n", SDL_GetError());
        }
    }

    if (device == nullptr)
    {
        std::fprintf(stderr, "graphics: Vulkan: no device: %s\n", SDL_GetError());
        return nullptr;
    }

    SDL_PropertiesID info = SDL_GetGPUDeviceProperties(device);
    std::fprintf(stderr, "graphics: Vulkan: %s, driver %s %s\n", SDL_GetStringProperty(info, SDL_PROP_GPU_DEVICE_NAME_STRING, "?"),
                 SDL_GetStringProperty(info, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "?"),
                 SDL_GetStringProperty(info, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "?"));
    return device;
}

// The shared device (made the first time), or one of the caller's own (OWN)
SDL_GPUDevice* TakeDevice(bool own)
{
    std::lock_guard<std::mutex> lock(g_DeviceLock);
    SDL_GPUDevice* device = nullptr;
    if (own)
    {
        device = MakeDevice();
    }
    else
    {
        g_Device = g_Device != nullptr ? g_Device : MakeDevice();
        g_DeviceUsers += g_Device != nullptr ? 1 : 0;
        device = g_Device;
    }

    if (device != nullptr && g_Devices++ == 0)
    {
        glslang::InitializeProcess();
    }

    return device;
}

void LetGoOfDevice(SDL_GPUDevice* device)
{
    std::lock_guard<std::mutex> lock(g_DeviceLock);
    if (device != g_Device)
    {
        SDL_DestroyGPUDevice(device);
    }
    else if (--g_DeviceUsers == 0)
    {
        SDL_DestroyGPUDevice(g_Device);
        g_Device = nullptr;
    }

    if (--g_Devices == 0)
    {
        glslang::FinalizeProcess();
    }
}

class VulkanBackend final : public Backend
{
public:
    // A device of its own (OWN: the overlay's shots), or the shared one, with the window claimed for it (WINDOW)
    bool Start(SDL_Window* window, bool own);
    ~VulkanBackend() override;
    const char* Name() const override { return "Vulkan"; }
    std::string Describe() const override
    {
        SDL_PropertiesID info = SDL_GetGPUDeviceProperties(device_);
        return std::string("Vulkan: ") + SDL_GetStringProperty(info, SDL_PROP_GPU_DEVICE_NAME_STRING, "?") + ", driver " +
               SDL_GetStringProperty(info, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "?") + " " +
               SDL_GetStringProperty(info, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "?");
    }
    void MakeCurrent() override {}
    void Play(const CommandList& list) override;
    void NewTextures(s32 count, u32* names) override;
    void NewFramebuffers(s32 count, u32* names) override;
    u32 StartProgram(const char* vertex, const std::string& fragment, bool dualSource) override;
    bool FinishProgram(u32 program) override;
    s32 UniformLocation(u32 program, const char* name) override;
    void Read(u32 framebuffer, s32 x, s32 y, s32 width, s32 height, Format format, s32 rowLength, s32 alignment, void* out) override;
    bool FramebufferComplete(u32 framebuffer) override;
    bool OutOfMemory() override
    {
        const bool was = outOfMemory_;
        outOfMemory_ = false;
        return was;
    }
    // (SDL GPU tells neither: Vulkan's least on the cards it runs on)
    s32 MaxTextureSize() override { return 16384; }
    s32 MaxAnisotropy() override { return 16; }
    bool SamplesFormat(u32 format) override
    {
        return !CompressedFormat(format) ||
               SDL_GPUTextureSupportsFormat(device_, Formats[format], SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER);
    }
    void* Fence() override;
    void WaitFence(void* fence) override { (void)fence; }
    void DeleteFence(void* fence) override
    {
        if (fence != nullptr)
        {
            ReleaseFence(device_, static_cast<SDL_GPUFence*>(fence));
        }
    }
    void ParallelCompiles() override {}
    size_t ListBytes() const override { return listBytes_; }
    void Flush() override { SubmitWork(false); }
    void* NativeTexture(u32 texture) override
    {
        Texture* t = TextureOf(texture);
        return t != nullptr ? t->texture : nullptr;
    }
    u32 TakeTexture(void* native, s32 width, s32 height) override;
    bool WindowFrame(s32 width, s32 height) override;
    void ShowWindow() override;
    void SetVSync(bool on) override;

private:
    Texture* TextureOf(u32 name) { return name != 0 && name < textures_.size() && textures_[name].live ? &textures_[name] : nullptr; }
    Program* ProgramOf(u32 name) { return name != 0 && name < programs_.size() && programs_[name] != nullptr ? programs_[name].get() : nullptr; }
    SDL_GPUCommandBuffer* Commands();
    void EndPasses();
    SDL_GPUCopyPass* CopyPass();
    bool BeginRender(u32 framebuffer);
    // (PRESENTING: the window's presenting submit, under g_RecordingLock)
    void SubmitWork(bool wait, bool presenting = false);
    bool MakeTexture(Texture& t, u32 format, s32 width, s32 height, u32 levels);
    void EnsureLevels(Texture& t, u32 levels);
    // THE LIST 97: a texture let go of kept as a spare for the next one made of its format, size and levels, rather than released
    // (SDL destroys a colour target's views each looking through every framebuffer it keeps, SDL_gpu_vulkan.c 3.4.18's
    // VULKAN_INTERNAL_RemoveFramebuffersContainingView, about one a texture level ever cleared or drawn into: the renderer's 4097
    // textures decoded on the card, dropped at once by hwgs.cpp past 4096, took 4 s of the next wait or submit, under SDL's submit
    // lock, and the frame stopped). Kept up to SpareBytes, the rest released
    void LetGo(SDL_GPUTexture* texture, u32 format, s32 width, s32 height, u32 levels);
    SDL_GPUTexture* TakeSpare(u32 format, u32 width, u32 height, u32 levels);
    void ReleaseSpares();
    SDL_GPUSampler* SamplerFor(Texture& t);
    bool MakeShaders(Program& p);
    SDL_GPUShader* FactorShader(Program& p);
    SDL_GPUGraphicsPipeline* PipelineFor(const PipelineKey& key);
    void SetDynamic(const RenderState& s, bool blendConstants);
    void PushUniforms(Program& p, u32 name);
    void BindSamplers(Program& p, const u32* units);
    void SetUniform(Program& p, s32 location, const void* values, u32 count, bool array);
    u32 OwnProgram(u32 own) const { return ownPrograms_[own]; }
    void DrawCopy(u32 own, u32 framebuffer, u32 source, const s32* sourceRect, const s32* destinationRect, bool linear,
                  const RenderState& state, bool scissored);
    void Clear(const ClearCommand& c);
    void Blit(const BlitCommand& c);
    u32 TargetFormatOf(const Framebuffer& f);

    SDL_GPUDevice* device_ = nullptr;
    Compilers compilers_;
    // The window's backend's: the window claimed, its frame's texture (framebuffer 0's colour), the textures taken by their SDL
    // texture
    SDL_Window* window_ = nullptr;
    u32 windowTexture_ = 0;
    std::unordered_map<void*, u32> taken_;
    // The renderer's: g_RecordingLock held while its command buffer is open (a window claimed on the shared device)
    bool recordingLocked_ = false;
    std::vector<Texture> textures_;
    std::vector<u32> freeTextures_;
    // The spares (LetGo), by format, levels, width and height, and their size
    std::unordered_map<u64, std::vector<SDL_GPUTexture*>> spares_;
    size_t spareBytes_ = 0;
    size_t spareCount_ = 0;
    std::vector<Framebuffer> framebuffers_;
    std::vector<u32> freeFramebuffers_;
    std::vector<std::unique_ptr<Program>> programs_;
    u32 ownPrograms_[OwnCount] = {};
    std::unordered_map<PipelineKey, SDL_GPUGraphicsPipeline*, PipelineHash> pipelines_;
    std::unordered_map<u64, SDL_GPUSampler*> samplers_;
    // Bound where an integer or a float sampler wants a texture and none is
    u32 dummyFloat_ = 0;
    u32 dummyUint_ = 0;
    // A read back's float copy of depth
    u32 depthReadTexture_ = 0;
    u32 depthReadFramebuffer_ = 0;

    SDL_GPUCommandBuffer* commands_ = nullptr;
    SDL_GPURenderPass* render_ = nullptr;
    SDL_GPUCopyPass* copy_ = nullptr;
    u32 renderFramebuffer_ = 0;
    s32 renderWidth_ = 0;
    s32 renderHeight_ = 0;
    u32 renderColourFormat_ = NoColourFormat;
    bool renderDepth_ = false;
    // What the render pass has set
    SDL_GPUGraphicsPipeline* bound_ = nullptr;
    bool viewportKnown_ = false;
    s32 viewport_[4] = {};
    bool scissorKnown_ = false;
    s32 scissor_[4] = {};
    bool blendConstantKnown_ = false;
    f32 blendConstant_ = 0.0f;
    s32 stencilReference_ = -1;
    bool vertexesBound_ = false;
    u32 pushedVertex_ = 0;
    u32 pushedFragment_ = 0;
    // The work since the last submit, and the fences of the submitted (two at most in flight)
    u32 draws_ = 0;
    size_t uploaded_ = 0;
    std::deque<SDL_GPUFence*> inFlight_;

    SDL_GPUBuffer* vertexBuffer_ = nullptr;
    u32 vertexBufferSize_ = 0;
    u32 vertexBase_ = 0;
    SDL_GPUTransferBuffer* upload_ = nullptr;
    u32 uploadSize_ = 0;
    SDL_GPUTransferBuffer* download_ = nullptr;
    u32 downloadSize_ = 0;
    // Zeros, for the levels MakeTexture can't clear by drawing
    SDL_GPUTransferBuffer* zeros_ = nullptr;
    u32 zerosSize_ = 0;
    std::vector<u32> uploadOffsets_;
    bool outOfMemory_ = false;
    size_t listBytes_ = 0;
    u32 told_ = 0;
};

VulkanBackend::~VulkanBackend()
{
    if (device_ != nullptr)
    {
        if (commands_ != nullptr)
        {
            EndPasses();
            SDL_SubmitGPUCommandBuffer(commands_);
            commands_ = nullptr;
            if (recordingLocked_)
            {
                recordingLocked_ = false;
                g_RecordingLock.unlock();
            }
        }

        SDL_WaitForGPUIdle(device_);
        if (window_ != nullptr)
        {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
            g_WindowClaimed = false;
        }

        for (SDL_GPUFence* f : inFlight_)
        {
            ReleaseFence(device_, f);
        }

        for (auto& entry : pipelines_)
        {
            SDL_ReleaseGPUGraphicsPipeline(device_, entry.second);
        }

        for (auto& entry : samplers_)
        {
            SDL_ReleaseGPUSampler(device_, entry.second);
        }

        for (Texture& t : textures_)
        {
            if (t.texture != nullptr && !t.taken)
            {
                SDL_ReleaseGPUTexture(device_, t.texture);
            }
        }

        ReleaseSpares();
    }

    compilers_.Stop();
    if (device_ != nullptr)
    {
        for (auto& p : programs_)
        {
            if (p == nullptr)
            {
                continue;
            }

            for (SDL_GPUShader* s : {p->vertexShader, p->fragmentShader, p->factorShader})
            {
                if (s != nullptr)
                {
                    SDL_ReleaseGPUShader(device_, s);
                }
            }
        }

        for (SDL_GPUBuffer* b : {vertexBuffer_})
        {
            if (b != nullptr)
            {
                SDL_ReleaseGPUBuffer(device_, b);
            }
        }

        for (SDL_GPUTransferBuffer* b : {upload_, download_, zeros_})
        {
            if (b != nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device_, b);
            }
        }

        LetGoOfDevice(device_);
    }
}

bool VulkanBackend::Start(SDL_Window* window, bool own)
{
    device_ = TakeDevice(own);
    if (device_ == nullptr)
    {
        return false;
    }

    // The formats drawn and sampled (the renderer's, the window's code's)
    for (u32 f = 0; f < 8; f++)
    {
        if (f == FormatDepth32fStencil8)
        {
            continue;
        }

        const SDL_GPUTextureUsageFlags colour = (IntegerFormat[f] ? SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ : SDL_GPU_TEXTUREUSAGE_SAMPLER) |
                                                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        if (!SDL_GPUTextureSupportsFormat(device_, Formats[f], SDL_GPU_TEXTURETYPE_2D, colour))
        {
            std::fprintf(stderr, "graphics: Vulkan: format %u can't be drawn into and sampled\n", f);
            return false;
        }
    }

    if (!SDL_GPUTextureSupportsFormat(device_, Formats[FormatDepth32fStencil8], SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
    {
        std::fprintf(stderr, "graphics: Vulkan: no 32 bit float depth with stencil\n");
        return false;
    }

    // (The window's code makes a few programs: a thread for them)
    compilers_.Start(window != nullptr ? 1 : 8);
    textures_.resize(1);
    framebuffers_.resize(1);
    programs_.resize(1);
    if (window != nullptr)
    {
        // The window claimed (its swapchain made): framebuffer 0 is its frame's texture, copied into the swapchain's when shown
        if (!SDL_ClaimWindowForGPUDevice(device_, window))
        {
            std::fprintf(stderr, "graphics: Vulkan: the window can't show its pictures: %s\n", SDL_GetError());
            return false;
        }

        window_ = window;
        g_WindowClaimed = true;
        NewTextures(1, &windowTexture_);
        framebuffers_[0].colour = windowTexture_;
        framebuffers_[0].live = true;
    }

    // The lists played in pieces of this many bytes (OpenGL's after each batch: here each piece's vertexes are one upload and its
    // draws into one target one render pass)
    const char* bytes = std::getenv("TWIN_VULKAN_LIST_BYTES");
    listBytes_ = bytes != nullptr ? std::strtoull(bytes, nullptr, 10) : (4u << 20);

    // The own programs
    const char* fragments[OwnCount] = {ClearFragmentSource, BlitFragmentSource, BlitUintFragmentSource, BlitDepthFragmentSource};
    for (u32 i = 0; i < OwnCount; i++)
    {
        ownPrograms_[i] = StartProgram(FullVertexSource, fragments[i], false);
        if (!FinishProgram(ownPrograms_[i]))
        {
            return false;
        }
    }

    // The textures bound where nothing is
    NewTextures(1, &dummyFloat_);
    NewTextures(1, &dummyUint_);
    Texture& f = textures_[dummyFloat_];
    Texture& u = textures_[dummyUint_];
    f.maxLevel = 0;
    u.maxLevel = 0;
    f.minFilter = u.minFilter = FilterNearest;
    f.magFilter = u.magFilter = FilterNearest;
    if (!MakeTexture(f, FormatRgba8, 1, 1, 1) || !MakeTexture(u, FormatR32ui, 1, 1, 1))
    {
        SubmitWork(false);
        return false;
    }

    SubmitWork(false);

    std::fprintf(stderr, "graphics: Vulkan: %s\n", window != nullptr ? "the window's pictures shown" : "drawing");
    return true;
}

void VulkanBackend::NewTextures(s32 count, u32* names)
{
    for (s32 i = 0; i < count; i++)
    {
        u32 name;
        if (!freeTextures_.empty())
        {
            name = freeTextures_.back();
            freeTextures_.pop_back();
        }
        else
        {
            name = static_cast<u32>(textures_.size());
            textures_.emplace_back();
        }

        textures_[name] = Texture{};
        textures_[name].live = true;
        names[i] = name;
    }
}

void VulkanBackend::NewFramebuffers(s32 count, u32* names)
{
    for (s32 i = 0; i < count; i++)
    {
        u32 name;
        if (!freeFramebuffers_.empty())
        {
            name = freeFramebuffers_.back();
            freeFramebuffers_.pop_back();
        }
        else
        {
            name = static_cast<u32>(framebuffers_.size());
            framebuffers_.emplace_back();
        }

        framebuffers_[name] = Framebuffer{};
        framebuffers_[name].live = true;
        names[i] = name;
    }
}

u32 VulkanBackend::StartProgram(const char* vertex, const std::string& fragment, bool dualSource)
{
    const u32 name = static_cast<u32>(programs_.size());
    programs_.push_back(std::make_unique<Program>());
    Program& p = *programs_.back();
    p.live = true;
    p.dualSource = dualSource;
    p.fragmentSource = fragment;
    std::string error;
    if (!Translate(vertex, true, false, p.varyings, p.vertex, error) || !Translate(fragment, false, false, p.varyings, p.fragment, error))
    {
        p.log = "made Vulkan's: " + error;
        p.state = 2;
        return name;
    }

    // The uniforms' locations: by name, both stages' places
    auto add = [&](const std::string& n) -> Slot& {
        auto found = p.locations.find(n);
        if (found != p.locations.end())
        {
            return p.slots[static_cast<size_t>(found->second)];
        }

        p.locations[n] = static_cast<s32>(p.slots.size());
        p.slots.emplace_back();
        return p.slots.back();
    };
    for (const Member& m : p.vertex.members)
    {
        Slot& s = add(m.name);
        s.vertexOffset = static_cast<s32>(m.offset);
        s.array = m.array != 0;
    }

    for (const Member& m : p.fragment.members)
    {
        Slot& s = add(m.name);
        s.fragmentOffset = static_cast<s32>(m.offset);
        s.array = m.array != 0;
    }

    for (size_t i = 0; i < p.fragment.samplers.size(); i++)
    {
        add(p.fragment.samplers[i].first).sampler = static_cast<s32>(i);
    }

    p.vertexUniforms.assign(p.vertex.blockSize, 0);
    p.fragmentUniforms.assign(p.fragment.blockSize, 0);
    p.units.assign(p.fragment.samplers.size(), 0);
    Program* program = &p;
    compilers_.Add([this, program] {
        std::string log;
        const bool made = CompileSpirv(true, program->vertex.text, program->vertexSpirv, log) &&
                          CompileSpirv(false, program->fragment.text, program->fragmentSpirv, log);
        program->log = log;
        program->state = made ? 1 : 2;
        compilers_.Signal();
    });
    return name;
}

bool VulkanBackend::FinishProgram(u32 name)
{
    Program* p = ProgramOf(name);
    if (p == nullptr)
    {
        return false;
    }

    compilers_.WaitFor(p->state);
    if (p->state != 1 || !MakeShaders(*p))
    {
        std::fprintf(stderr, "graphics: Vulkan shader: %s\n", p->log.c_str());
        return false;
    }

    return true;
}

bool VulkanBackend::MakeShaders(Program& p)
{
    if (p.vertexShader != nullptr)
    {
        return true;
    }

    auto make = [&](const std::vector<u32>& spirv, bool vertex, const Stage& stage) {
        SDL_GPUShaderCreateInfo info = {};
        info.code_size = spirv.size() * sizeof(u32);
        info.code = reinterpret_cast<const Uint8*>(spirv.data());
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        info.num_samplers = stage.Count(false);
        info.num_storage_textures = stage.Count(true);
        info.num_uniform_buffers = stage.members.empty() ? 0 : 1;
        return SDL_CreateGPUShader(device_, &info);
    };
    p.vertexShader = make(p.vertexSpirv, true, p.vertex);
    p.fragmentShader = make(p.fragmentSpirv, false, p.fragment);
    if (p.vertexShader == nullptr || p.fragmentShader == nullptr)
    {
        p.log = std::string("SDL: ") + SDL_GetError();
        return false;
    }

    p.vertexSpirv.clear();
    p.vertexSpirv.shrink_to_fit();
    return true;
}

SDL_GPUShader* VulkanBackend::FactorShader(Program& p)
{
    if (p.factorShader != nullptr || p.factorFailed)
    {
        return p.factorShader;
    }

    std::unordered_map<std::string, u32> varyings = p.varyings;
    Stage stage;
    std::string error;
    std::vector<u32> spirv;
    if (!Translate(p.fragmentSource, false, true, varyings, stage, error) || !CompileSpirv(false, stage.text, spirv, error))
    {
        std::fprintf(stderr, "graphics: Vulkan shader (blend factor): %s\n", error.c_str());
        p.factorFailed = true;
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info = {};
    info.code_size = spirv.size() * sizeof(u32);
    info.code = reinterpret_cast<const Uint8*>(spirv.data());
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_SPIRV;
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = stage.Count(false);
        info.num_storage_textures = stage.Count(true);
    info.num_uniform_buffers = stage.members.empty() ? 0 : 1;
    p.factorShader = SDL_CreateGPUShader(device_, &info);
    p.factorFailed = p.factorShader == nullptr;
    return p.factorShader;
}

s32 VulkanBackend::UniformLocation(u32 program, const char* name)
{
    Program* p = ProgramOf(program);
    if (p == nullptr)
    {
        return -1;
    }

    auto found = p->locations.find(name);
    if (found != p->locations.end())
    {
        return found->second;
    }

    // An array's element by name ("pageMask[3]"): a slot of its own at the element's place (std140: 16 bytes apart)
    const char* bracket = std::strchr(name, '[');
    if (bracket == nullptr)
    {
        return -1;
    }

    auto base = p->locations.find(std::string(name, bracket));
    if (base == p->locations.end() || !p->slots[static_cast<size_t>(base->second)].array)
    {
        return -1;
    }

    const s32 element = std::atoi(bracket + 1);
    Slot s = p->slots[static_cast<size_t>(base->second)];
    s.vertexOffset = s.vertexOffset >= 0 ? s.vertexOffset + element * 16 : -1;
    s.fragmentOffset = s.fragmentOffset >= 0 ? s.fragmentOffset + element * 16 : -1;
    s.array = false;
    const s32 location = static_cast<s32>(p->slots.size());
    p->slots.push_back(s);
    p->locations[name] = location;
    return location;
}

void VulkanBackend::SetUniform(Program& p, s32 location, const void* values, u32 count, bool array)
{
    if (location < 0 || static_cast<size_t>(location) >= p.slots.size())
    {
        return;
    }

    const Slot& s = p.slots[static_cast<size_t>(location)];
    const u32* words = static_cast<const u32*>(values);
    if (s.sampler >= 0)
    {
        p.units[static_cast<size_t>(s.sampler)] = words[0];
        return;
    }

    auto write = [&](std::vector<u8>& block, s32 offset, bool& dirty) {
        if (offset < 0)
        {
            return;
        }

        for (u32 i = 0; i < count; i++)
        {
            const size_t at = static_cast<size_t>(offset) + (array ? i * 16 : i * 4);
            if (at + 4 <= block.size())
            {
                std::memcpy(&block[at], &words[i], 4);
            }
        }

        dirty = true;
    };
    write(p.vertexUniforms, s.vertexOffset, p.vertexDirty);
    write(p.fragmentUniforms, s.fragmentOffset, p.fragmentDirty);
}

SDL_GPUCommandBuffer* VulkanBackend::Commands()
{
    if (commands_ == nullptr)
    {
        if (window_ == nullptr && device_ == g_Device && g_WindowClaimed)
        {
            g_RecordingLock.lock();
            recordingLocked_ = true;
        }

        commands_ = SDL_AcquireGPUCommandBuffer(device_);
        pushedVertex_ = 0;
        pushedFragment_ = 0;
    }

    return commands_;
}

void VulkanBackend::EndPasses()
{
    if (render_ != nullptr)
    {
        SDL_EndGPURenderPass(render_);
        render_ = nullptr;
    }

    if (copy_ != nullptr)
    {
        SDL_EndGPUCopyPass(copy_);
        copy_ = nullptr;
    }
}

SDL_GPUCopyPass* VulkanBackend::CopyPass()
{
    if (copy_ == nullptr)
    {
        EndPasses();
        copy_ = SDL_BeginGPUCopyPass(Commands());
    }

    return copy_;
}

u32 VulkanBackend::TargetFormatOf(const Framebuffer& f)
{
    const Texture* c = f.noColour ? nullptr : TextureOf(f.colour);
    return c != nullptr && c->texture != nullptr ? c->format : NoColourFormat;
}

bool VulkanBackend::BeginRender(u32 framebuffer)
{
    if (render_ != nullptr && renderFramebuffer_ == framebuffer)
    {
        return true;
    }

    if (framebuffer >= framebuffers_.size() || !framebuffers_[framebuffer].live)
    {
        return false;
    }

    const Framebuffer& f = framebuffers_[framebuffer];
    Texture* colour = f.noColour ? nullptr : TextureOf(f.colour);
    Texture* depth = TextureOf(f.depth);
    colour = colour != nullptr && colour->texture != nullptr ? colour : nullptr;
    depth = depth != nullptr && depth->texture != nullptr ? depth : nullptr;
    if (colour == nullptr && depth == nullptr)
    {
        return false;
    }

    EndPasses();
    SDL_GPUColorTargetInfo target = {};
    SDL_GPUDepthStencilTargetInfo depthTarget = {};
    if (colour != nullptr)
    {
        target.texture = colour->texture;
        target.mip_level = static_cast<Uint32>(f.colourLevel);
        target.load_op = SDL_GPU_LOADOP_LOAD;
        target.store_op = SDL_GPU_STOREOP_STORE;
        renderWidth_ = std::max(colour->width >> f.colourLevel, 1);
        renderHeight_ = std::max(colour->height >> f.colourLevel, 1);
    }

    if (depth != nullptr)
    {
        depthTarget.texture = depth->texture;
        depthTarget.load_op = SDL_GPU_LOADOP_LOAD;
        depthTarget.store_op = SDL_GPU_STOREOP_STORE;
        depthTarget.stencil_load_op = SDL_GPU_LOADOP_LOAD;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        if (colour == nullptr)
        {
            renderWidth_ = depth->width;
            renderHeight_ = depth->height;
        }
        else
        {
            // (Vulkan draws where both are)
            renderWidth_ = std::min(renderWidth_, depth->width);
            renderHeight_ = std::min(renderHeight_, depth->height);
        }
    }

    render_ = SDL_BeginGPURenderPass(Commands(), colour != nullptr ? &target : nullptr, colour != nullptr ? 1 : 0,
                                     depth != nullptr ? &depthTarget : nullptr);
    NoteTarget(colour != nullptr ? colour->texture : nullptr, static_cast<u32>(f.colourLevel), true);
    NoteTarget(depth != nullptr ? depth->texture : nullptr, 0, true);
    renderFramebuffer_ = framebuffer;
    renderColourFormat_ = colour != nullptr ? colour->format : NoColourFormat;
    renderDepth_ = depth != nullptr;
    bound_ = nullptr;
    viewportKnown_ = false;
    scissorKnown_ = false;
    blendConstantKnown_ = false;
    stencilReference_ = -1;
    vertexesBound_ = false;
    return render_ != nullptr;
}

void VulkanBackend::SubmitWork(bool wait, bool presenting)
{
    if (commands_ == nullptr)
    {
        return;
    }

    EndPasses();
    SDL_GPUFence* fence = nullptr;
    if (presenting)
    {
        // (Where SDL may move textures: no other command buffer of the device open meanwhile, g_DeviceLock's comment)
        std::lock_guard<std::mutex> lock(g_RecordingLock);
        VulkanPartSpan part(NativeGraphics::VulkanPart::Submits);
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands_);
    }
    else
    {
        VulkanPartSpan part(NativeGraphics::VulkanPart::Submits);
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands_);
    }

    TrackFlight(fence, device_, window_ != nullptr, presenting, draws_, uploaded_);
    commands_ = nullptr;
    draws_ = 0;
    uploaded_ = 0;
    if (recordingLocked_)
    {
        recordingLocked_ = false;
        g_RecordingLock.unlock();
    }

    if (fence == nullptr)
    {
        return;
    }

    if (wait)
    {
        {
            VulkanPartSpan part(NativeGraphics::VulkanPart::InFlight);
            WaitForCard(device_, fence, window_ != nullptr);
        }

        ReleaseFence(device_, fence);
        for (SDL_GPUFence* f : inFlight_)
        {
            ReleaseFence(device_, f);
        }

        inFlight_.clear();
        return;
    }

    inFlight_.push_back(fence);
    while (inFlight_.size() > 2)
    {
        VulkanPartSpan part(NativeGraphics::VulkanPart::InFlight);
        WaitForCard(device_, inFlight_.front(), window_ != nullptr);
        ReleaseFence(device_, inFlight_.front());
        inFlight_.pop_front();
    }
}

// The spares' key: format, levels, width, height
u64 SpareKey(u32 format, u32 width, u32 height, u32 levels)
{
    return (static_cast<u64>(format) << 56) | (static_cast<u64>(levels & 0xFF) << 48) | (static_cast<u64>(width & 0xFFFFFF) << 24) |
           (height & 0xFFFFFF);
}

// Its levels' bytes
size_t TextureBytes(u32 format, u32 width, u32 height, u32 levels)
{
    size_t bytes = 0;
    for (u32 level = 0; level < levels; level++)
    {
        bytes += static_cast<size_t>(std::max(width >> level, 1u)) * std::max(height >> level, 1u) * TexelBytes[format];
    }

    return bytes;
}

// (The most kept: the 4097 textures decoded on the card the renderer drops at once came to 181-196 MB)
constexpr size_t SpareBytes = 256u << 20;

void VulkanBackend::LetGo(SDL_GPUTexture* texture, u32 format, s32 width, s32 height, u32 levels)
{
    if (texture == nullptr)
    {
        return;
    }

    // (A block-compressed texture, a pack's, has no colour target views: released at no cost)
    const u32 w = static_cast<u32>(std::max(width, 1));
    const u32 h = static_cast<u32>(std::max(height, 1));
    const size_t bytes = TextureBytes(format, w, h, levels);
    if (CompressedFormat(format) || spareBytes_ + bytes > SpareBytes)
    {
        ReleaseTexture(device_, texture, levels);
        return;
    }

    spares_[SpareKey(format, w, h, levels)].push_back(texture);
    spareBytes_ += bytes;
    spareCount_++;
    if (TracingStalls() && spareCount_ % 1024 == 0)
    {
        std::fprintf(stderr, "graphics: Vulkan: %zu spare textures, %.1f MB\n", spareCount_, static_cast<double>(spareBytes_) / 1048576.0);
    }
}

SDL_GPUTexture* VulkanBackend::TakeSpare(u32 format, u32 width, u32 height, u32 levels)
{
    auto found = spares_.find(SpareKey(format, width, height, levels));
    if (found == spares_.end() || found->second.empty())
    {
        return nullptr;
    }

    SDL_GPUTexture* texture = found->second.back();
    found->second.pop_back();
    spareBytes_ -= TextureBytes(format, width, height, levels);
    spareCount_--;
    return texture;
}

void VulkanBackend::ReleaseSpares()
{
    for (auto& entry : spares_)
    {
        const u32 levels = static_cast<u32>((entry.first >> 48) & 0xFF);
        for (SDL_GPUTexture* texture : entry.second)
        {
            ReleaseTexture(device_, texture, levels);
        }
    }

    spares_.clear();
    spareBytes_ = 0;
    spareCount_ = 0;
}

bool VulkanBackend::MakeTexture(Texture& t, u32 format, s32 width, s32 height, u32 levels)
{
    VulkanPartSpan part(NativeGraphics::VulkanPart::Textures);
    if (t.texture != nullptr && !t.taken)
    {
        LetGo(t.texture, t.format, t.width, t.height, t.levels);
    }

    t.texture = nullptr;
    t.taken = false;

    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = Formats[format];
    // (Integer textures are read as storage textures: SDL GPU samples none)
    info.usage = CompressedFormat(format)          ? SDL_GPU_TEXTUREUSAGE_SAMPLER
                 : format == FormatDepth32fStencil8 ? (SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
                 : IntegerFormat[format]          ? (SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)
                                                  : (SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
    info.width = static_cast<Uint32>(std::max(width, 1));
    info.height = static_cast<Uint32>(std::max(height, 1));
    info.layer_count_or_depth = 1;
    info.num_levels = std::max<u32>(levels, 1);
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    // (With the validation layers on, named as the renderer knows it: their messages say which)
    static const bool debug = std::getenv("TWIN_VULKAN_DEBUG") != nullptr;
    char name[64] = {};
    if (debug)
    {
        std::snprintf(name, sizeof(name), "texture %u %dx%d format %u levels %u", static_cast<u32>(&t - textures_.data()), width, height, format,
                      info.num_levels);
    }

    // A spare of its kind (LetGo), else a new one; with no room for a new one, the spares released first
    t.texture = TakeSpare(format, info.width, info.height, info.num_levels);
    if (t.texture != nullptr && debug)
    {
        SDL_SetGPUTextureName(device_, t.texture, name);
    }

    for (int attempt = 0; attempt < 2 && t.texture == nullptr; attempt++)
    {
        if (attempt == 1)
        {
            if (spareCount_ == 0)
            {
                break;
            }

            ReleaseSpares();
        }

        if (debug)
        {
            info.props = SDL_CreateProperties();
            SDL_SetStringProperty(info.props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name);
        }

        t.texture = SDL_CreateGPUTexture(device_, &info);
        if (info.props != 0)
        {
            SDL_DestroyProperties(info.props);
            info.props = 0;
        }
    }

    t.format = format;
    t.width = static_cast<s32>(info.width);
    t.height = static_cast<s32>(info.height);
    t.levels = info.num_levels;
    if (t.texture == nullptr)
    {
        std::fprintf(stderr, "graphics: Vulkan: no texture %dx%d (format %u): %s\n", width, height, format, SDL_GetError());
        outOfMemory_ = true;
        return false;
    }

    // Its levels cleared to zero: OpenGL's drivers give a new texture's storage zeroed and the renderer has come to rely on it (a depth
    // buffer's first draws tested against 0), Vulkan's are undefined. (A block-compressed one, a texture pack's, has every level
    // uploaded at once and can't be drawn into)
    if (CompressedFormat(format))
    {
        return true;
    }

    EndPasses();
    for (u32 level = 0; level < t.levels; level++)
    {
        if ((t.width >> level) == 0 || (t.height >> level) == 0)
        {
            // (A level past the short side's: SDL sizes a level without OpenGL's least of 1, so it can't be drawn into; zeros
            // uploaded instead)
            const u32 w = static_cast<u32>(std::max(t.width >> level, 1));
            const u32 h = static_cast<u32>(std::max(t.height >> level, 1));
            const u32 bytes = w * h * TexelBytes[format];
            if (bytes > zerosSize_)
            {
                if (zeros_ != nullptr)
                {
                    SDL_ReleaseGPUTransferBuffer(device_, zeros_);
                }

                SDL_GPUTransferBufferCreateInfo zeroInfo = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes, 0};
                zeros_ = SDL_CreateGPUTransferBuffer(device_, &zeroInfo);
                void* mapped = zeros_ != nullptr ? SDL_MapGPUTransferBuffer(device_, zeros_, false) : nullptr;
                if (mapped == nullptr)
                {
                    zerosSize_ = 0;
                    continue;
                }

                std::memset(mapped, 0, bytes);
                SDL_UnmapGPUTransferBuffer(device_, zeros_);
                zerosSize_ = bytes;
            }

            SDL_GPUTextureTransferInfo from = {zeros_, 0, w, h};
            SDL_GPUTextureRegion to = {t.texture, level, 0, 0, 0, 0, w, h, 1};
            SDL_UploadToGPUTexture(CopyPass(), &from, &to, false);
            continue;
        }

        EndPasses();
        SDL_GPUColorTargetInfo colour = {};
        SDL_GPUDepthStencilTargetInfo depth = {};
        const bool isDepth = format == FormatDepth32fStencil8;
        if (isDepth)
        {
            depth.texture = t.texture;
            depth.clear_depth = 0.0f;
            depth.load_op = SDL_GPU_LOADOP_CLEAR;
            depth.store_op = SDL_GPU_STOREOP_STORE;
            depth.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
            depth.stencil_store_op = SDL_GPU_STOREOP_STORE;
            depth.clear_stencil = 0;
            depth.mip_level = static_cast<Uint8>(level);
        }
        else
        {
            colour.texture = t.texture;
            colour.mip_level = level;
            colour.load_op = SDL_GPU_LOADOP_CLEAR;
            colour.store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPURenderPass* clear = SDL_BeginGPURenderPass(Commands(), isDepth ? nullptr : &colour, isDepth ? 0 : 1, isDepth ? &depth : nullptr);
        SDL_EndGPURenderPass(clear);
        NoteTarget(t.texture, level, false);
    }

    return true;
}

// A texture given more levels (OpenGL's level made past those it has): made again, its levels copied
void VulkanBackend::EnsureLevels(Texture& t, u32 levels)
{
    // (A block-compressed texture is made with the levels it has: its levels past SDL's chain are left out)
    if (t.texture == nullptr || levels <= t.levels || CompressedFormat(t.format))
    {
        return;
    }

    SDL_GPUTexture* old = t.texture;
    const u32 oldLevels = t.levels;
    t.texture = nullptr;
    if (!MakeTexture(t, t.format, t.width, t.height, levels))
    {
        return;
    }

    SDL_GPUCopyPass* copy = CopyPass();
    for (u32 level = 0; level < oldLevels; level++)
    {
        SDL_GPUTextureLocation from = {old, level, 0, 0, 0, 0};
        SDL_GPUTextureLocation to = {t.texture, level, 0, 0, 0, 0};
        SDL_CopyGPUTextureToTexture(copy, &from, &to, static_cast<Uint32>(std::max(t.width >> level, 1)),
                                    static_cast<Uint32>(std::max(t.height >> level, 1)), 1, false);
    }

    LetGo(old, t.format, t.width, t.height, oldLevels);
}

SDL_GPUSampler* VulkanBackend::SamplerFor(Texture& t)
{
    // Integer textures are only fetched (no filtering)
    const bool integer = IntegerFormat[t.format];
    const u32 minFilter = integer ? FilterNearest : t.minFilter;
    const u32 magFilter = integer ? FilterNearest : t.magFilter;
    const bool mipmapped = minFilter >= FilterNearestMipNearest;
    const s32 maxLevel = mipmapped ? std::clamp<s32>(t.maxLevel, 0, 15) : 0;
    const u32 anisotropy = mipmapped && t.anisotropy > 1.0f ? static_cast<u32>(t.anisotropy) : 1;
    const u64 key = static_cast<u64>(minFilter) | static_cast<u64>(magFilter) << 4 | static_cast<u64>(t.wrapS) << 8 |
                    static_cast<u64>(t.wrapT) << 10 | static_cast<u64>(maxLevel) << 12 | static_cast<u64>(anisotropy) << 20;
    auto found = samplers_.find(key);
    if (found != samplers_.end())
    {
        return found->second;
    }

    SDL_GPUSamplerCreateInfo info = {};
    const bool minLinear = minFilter == FilterLinear || minFilter == FilterLinearMipNearest || minFilter == FilterLinearMipLinear;
    info.min_filter = minLinear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    info.mag_filter = magFilter == FilterLinear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    info.mipmap_mode = (minFilter == FilterNearestMipLinear || minFilter == FilterLinearMipLinear) ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR
                                                                                                  : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = t.wrapS == WrapRepeat ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = t.wrapT == WrapRepeat ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.min_lod = 0.0f;
    // (Without mipmaps OpenGL samples the first level: 0.25 keeps the choice between magnifying and minifying, Vulkan's advice)
    info.max_lod = mipmapped ? static_cast<f32>(maxLevel) : 0.25f;
    info.enable_anisotropy = anisotropy > 1;
    info.max_anisotropy = static_cast<f32>(anisotropy);
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(device_, &info);
    samplers_[key] = sampler;
    return sampler;
}

SDL_GPUGraphicsPipeline* VulkanBackend::PipelineFor(const PipelineKey& key)
{
    auto found = pipelines_.find(key);
    if (found != pipelines_.end())
    {
        return found->second;
    }

    Program* p = ProgramOf(key.program);
    if (p == nullptr || p->vertexShader == nullptr)
    {
        pipelines_[key] = nullptr;
        return nullptr;
    }

    SDL_GPUShader* fragment = key.variant != 0 ? FactorShader(*p) : p->fragmentShader;
    if (fragment == nullptr)
    {
        pipelines_[key] = nullptr;
        return nullptr;
    }

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = p->vertexShader;
    info.fragment_shader = fragment;
    SDL_GPUVertexBufferDescription buffer = {0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
    SDL_GPUVertexAttribute attributes[5] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, static_cast<Uint32>(offsetof(Vertex, x))},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UINT, static_cast<Uint32>(offsetof(Vertex, z))},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, static_cast<Uint32>(offsetof(Vertex, r))},
        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, static_cast<Uint32>(offsetof(Vertex, s))},
        {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, static_cast<Uint32>(offsetof(Vertex, fog))},
    };
    if (key.vertexes)
    {
        info.vertex_input_state.vertex_buffer_descriptions = &buffer;
        info.vertex_input_state.num_vertex_buffers = 1;
        info.vertex_input_state.vertex_attributes = attributes;
        info.vertex_input_state.num_vertex_attributes = 5;
    }

    info.primitive_type = Primitives[key.primitive];
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    // (OpenGL's counter-clockwise front with its y up is Vulkan's clockwise with the same rows)
    info.rasterizer_state.cull_mode = key.cullFace ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.depth_stencil_state.enable_depth_test = key.depthTarget && key.depthTest;
    info.depth_stencil_state.enable_depth_write = key.depthTarget && key.depthTest && key.depthWrite;
    info.depth_stencil_state.compare_op = Compares[key.depthFunc];
    info.depth_stencil_state.enable_stencil_test = key.depthTarget && key.stencilTest;
    SDL_GPUStencilOpState stencil = {StencilOps[key.stencilFail], StencilOps[key.stencilPass], StencilOps[key.stencilDepthFail],
                                     Compares[key.stencilFunc]};
    info.depth_stencil_state.front_stencil_state = stencil;
    info.depth_stencil_state.back_stencil_state = stencil;
    info.depth_stencil_state.compare_mask = key.stencilReadMask;
    info.depth_stencil_state.write_mask = key.stencilWriteMask;
    SDL_GPUColorTargetDescription target = {};
    if (key.colourFormat != NoColourFormat)
    {
        target.format = Formats[key.colourFormat];
        SDL_GPUColorTargetBlendState& b = target.blend_state;
        b.enable_blend = key.blend && !IntegerFormat[key.colourFormat];
        b.color_blend_op = BlendOps[key.blendOp];
        b.alpha_blend_op = BlendOps[key.blendOpAlpha];
        b.src_color_blendfactor = Factors[key.blendSource];
        b.dst_color_blendfactor = Factors[key.blendDestination];
        b.src_alpha_blendfactor = Factors[key.blendSourceAlpha];
        b.dst_alpha_blendfactor = Factors[key.blendDestinationAlpha];
        b.enable_color_write_mask = true;
        b.color_write_mask = key.colourMask;
        info.target_info.color_target_descriptions = &target;
        info.target_info.num_color_targets = 1;
    }

    info.target_info.has_depth_stencil_target = key.depthTarget != 0;
    info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device_, &info);
    if (pipeline == nullptr)
    {
        std::fprintf(stderr, "graphics: Vulkan: no pipeline: %s\n", SDL_GetError());
    }

    pipelines_[key] = pipeline;
    return pipeline;
}

void VulkanBackend::SetDynamic(const RenderState& s, bool blendConstants)
{
    // The viewport (Vulkan's within its bounds: OpenGL clamps them)
    if (!viewportKnown_ || std::memcmp(viewport_, s.viewport, sizeof(viewport_)) != 0)
    {
        SDL_GPUViewport v = {static_cast<f32>(s.viewport[0]), static_cast<f32>(s.viewport[1]), static_cast<f32>(std::max(s.viewport[2], 1)),
                             static_cast<f32>(std::max(s.viewport[3], 1)), 0.0f, 1.0f};
        SDL_SetGPUViewport(render_, &v);
        std::memcpy(viewport_, s.viewport, sizeof(viewport_));
        viewportKnown_ = true;
    }

    s32 scissor[4] = {0, 0, renderWidth_, renderHeight_};
    if (s.scissorTest)
    {
        const s32 left = std::clamp(s.scissor[0], 0, renderWidth_);
        const s32 top = std::clamp(s.scissor[1], 0, renderHeight_);
        const s32 right = std::clamp(s.scissor[0] + s.scissor[2], left, renderWidth_);
        const s32 bottom = std::clamp(s.scissor[1] + s.scissor[3], top, renderHeight_);
        scissor[0] = left;
        scissor[1] = top;
        scissor[2] = right - left;
        scissor[3] = bottom - top;
    }

    if (!scissorKnown_ || std::memcmp(scissor_, scissor, sizeof(scissor_)) != 0)
    {
        SDL_Rect r = {scissor[0], scissor[1], scissor[2], scissor[3]};
        SDL_SetGPUScissor(render_, &r);
        std::memcpy(scissor_, scissor, sizeof(scissor_));
        scissorKnown_ = true;
    }

    if (blendConstants && (!blendConstantKnown_ || blendConstant_ != s.blendColour[3]))
    {
        const f32 a = s.blendColour[3];
        SDL_SetGPUBlendConstants(render_, SDL_FColor{a, a, a, a});
        blendConstant_ = a;
        blendConstantKnown_ = true;
    }

    if (s.stencilTest && stencilReference_ != s.stencilReference)
    {
        SDL_SetGPUStencilReference(render_, s.stencilReference);
        stencilReference_ = s.stencilReference;
    }
}

void VulkanBackend::PushUniforms(Program& p, u32 name)
{
    if (!p.vertexUniforms.empty() && (pushedVertex_ != name || p.vertexDirty))
    {
        SDL_PushGPUVertexUniformData(commands_, 0, p.vertexUniforms.data(), static_cast<Uint32>(p.vertexUniforms.size()));
        pushedVertex_ = name;
        p.vertexDirty = false;
    }

    if (!p.fragmentUniforms.empty() && (pushedFragment_ != name || p.fragmentDirty))
    {
        SDL_PushGPUFragmentUniformData(commands_, 0, p.fragmentUniforms.data(), static_cast<Uint32>(p.fragmentUniforms.size()));
        pushedFragment_ = name;
        p.fragmentDirty = false;
    }
}

void VulkanBackend::BindSamplers(Program& p, const u32* units)
{
    const size_t count = p.fragment.samplers.size();
    if (count == 0)
    {
        return;
    }

    // The float textures with their samplers, the integer ones as storage textures (their bindings after)
    SDL_GPUTextureSamplerBinding bindings[16] = {};
    SDL_GPUTexture* storage[16] = {};
    u32 floats = 0;
    u32 integers = 0;
    const Framebuffer& f = framebuffers_[renderFramebuffer_];
    for (size_t i = 0; i < count && i < 16; i++)
    {
        const bool integer = p.fragment.samplers[i].second;
        const u32 unit = p.units[i];
        Texture* t = TextureOf(unit < TextureUnits ? units[unit] : 0);
        // None, of the other kind, or drawn into now (OpenGL leaves those undefined): the dummy
        if (t == nullptr || t->texture == nullptr || IntegerFormat[t->format] != integer ||
            (!f.noColour && units[unit] == f.colour) || units[unit] == f.depth)
        {
            t = &textures_[integer ? dummyUint_ : dummyFloat_];
        }

        if (integer)
        {
            storage[integers++] = t->texture;
        }
        else
        {
            bindings[floats].texture = t->texture;
            bindings[floats].sampler = SamplerFor(*t);
            floats++;
        }
    }

    if (floats != 0)
    {
        SDL_BindGPUFragmentSamplers(render_, 0, bindings, floats);
    }

    if (integers != 0)
    {
        SDL_BindGPUFragmentStorageTextures(render_, 0, storage, integers);
    }
}

// A rectangle of SOURCE (a texture) drawn over a rectangle of FRAMEBUFFER by one of the own programs
void VulkanBackend::DrawCopy(u32 own, u32 framebuffer, u32 source, const s32* sourceRect, const s32* destinationRect, bool linear,
                             const RenderState& state, bool scissored)
{
    if (!BeginRender(framebuffer))
    {
        return;
    }

    Program& p = *programs_[ownPrograms_[own]];
    Texture* t = TextureOf(source);
    if (t == nullptr || t->texture == nullptr)
    {
        return;
    }

    PipelineKey key;
    std::memset(&key, 0, sizeof(key));
    key.program = ownPrograms_[own];
    key.primitive = PrimitiveTriangles;
    key.colourFormat = static_cast<u8>(renderColourFormat_);
    key.depthTarget = renderDepth_ ? 1 : 0;
    key.blendOp = BlendAdd;
    key.blendOpAlpha = BlendAdd;
    key.blendSource = FactorOne;
    key.blendSourceAlpha = FactorOne;
    key.colourMask = own == OwnBlitDepth ? 0 : 0xF;
    key.depthTest = own == OwnBlitDepth ? 1 : 0;
    key.depthFunc = CompareAlways;
    key.depthWrite = own == OwnBlitDepth ? 1 : 0;
    key.stencilFunc = CompareAlways;
    key.stencilReadMask = 0xFF;
    SDL_GPUGraphicsPipeline* pipeline = PipelineFor(key);
    if (pipeline == nullptr)
    {
        return;
    }

    if (bound_ != pipeline)
    {
        SDL_BindGPUGraphicsPipeline(render_, pipeline);
        bound_ = pipeline;
    }

    RenderState s = {};
    const s32 left = std::min(destinationRect[0], destinationRect[2]);
    const s32 top = std::min(destinationRect[1], destinationRect[3]);
    s.viewport[0] = left;
    s.viewport[1] = top;
    s.viewport[2] = std::max(destinationRect[0], destinationRect[2]) - left;
    s.viewport[3] = std::max(destinationRect[1], destinationRect[3]) - top;
    s.scissorTest = 1;
    s.scissor[0] = left;
    s.scissor[1] = top;
    s.scissor[2] = s.viewport[2];
    s.scissor[3] = s.viewport[3];
    if (scissored && state.scissorTest)
    {
        const s32 l = std::max(left, state.scissor[0]);
        const s32 tp = std::max(top, state.scissor[1]);
        const s32 r = std::min(left + s.viewport[2], state.scissor[0] + state.scissor[2]);
        const s32 b = std::min(top + s.viewport[3], state.scissor[1] + state.scissor[3]);
        s.scissor[0] = l;
        s.scissor[1] = tp;
        s.scissor[2] = std::max(r - l, 0);
        s.scissor[3] = std::max(b - tp, 0);
    }

    SetDynamic(s, false);
    if (scissor_[2] <= 0 || scissor_[3] <= 0)
    {
        return;
    }

    f32 values[4];
    for (int i = 0; i < 4; i++)
    {
        values[i] = static_cast<f32>(sourceRect[i]);
    }

    SetUniform(p, p.locations["sourceRect"], values, 4, false);
    for (int i = 0; i < 4; i++)
    {
        values[i] = static_cast<f32>(destinationRect[i]);
    }

    SetUniform(p, p.locations["destinationRect"], values, 4, false);
    if (own == OwnBlit)
    {
        const f32 size[2] = {static_cast<f32>(t->width), static_cast<f32>(t->height)};
        SetUniform(p, p.locations["sourceSize"], size, 2, false);
        const s32 on = linear ? 1 : 0;
        SetUniform(p, p.locations["linear"], &on, 1, false);
    }

    PushUniforms(p, ownPrograms_[own]);
    if (IntegerFormat[t->format])
    {
        SDL_BindGPUFragmentStorageTextures(render_, 0, &t->texture, 1);
    }
    else
    {
        SDL_GPUTextureSamplerBinding binding = {t->texture, nullptr};
        Texture probe = *t;
        probe.minFilter = linear ? FilterLinear : FilterNearest;
        probe.magFilter = probe.minFilter;
        probe.wrapS = WrapClamp;
        probe.wrapT = WrapClamp;
        binding.sampler = SamplerFor(probe);
        SDL_BindGPUFragmentSamplers(render_, 0, &binding, 1);
    }
    SDL_DrawGPUPrimitives(render_, 3, 1, 0, 0);
    draws_++;
}

void VulkanBackend::Clear(const ClearCommand& c)
{
    if (!BeginRender(c.framebuffer))
    {
        return;
    }

    if ((c.bits & ClearDepth) != 0 && told_++ < 3)
    {
        std::fprintf(stderr, "graphics: Vulkan: a depth clear (left out)\n");
    }

    if (renderColourFormat_ != NoColourFormat && IntegerFormat[renderColourFormat_] && (c.bits & ClearColour) != 0)
    {
        if (told_++ < 3)
        {
            std::fprintf(stderr, "graphics: Vulkan: an integer target's clear (left out)\n");
        }

        return;
    }

    Program& p = *programs_[ownPrograms_[OwnClear]];
    PipelineKey key;
    std::memset(&key, 0, sizeof(key));
    key.program = ownPrograms_[OwnClear];
    key.primitive = PrimitiveTriangles;
    key.colourFormat = static_cast<u8>(renderColourFormat_);
    key.depthTarget = renderDepth_ ? 1 : 0;
    key.blendOp = BlendAdd;
    key.blendOpAlpha = BlendAdd;
    key.blendSource = FactorOne;
    key.blendSourceAlpha = FactorOne;
    key.colourMask = (c.bits & ClearColour) != 0 ? c.state.colourMask : 0;
    key.depthFunc = CompareAlways;
    key.stencilFunc = CompareAlways;
    key.stencilReadMask = 0xFF;
    const bool stencil = (c.bits & ClearStencil) != 0 && renderDepth_;
    if (stencil)
    {
        key.stencilTest = 1;
        key.stencilFail = StencilReplace;
        key.stencilDepthFail = StencilReplace;
        key.stencilPass = StencilReplace;
        key.stencilWriteMask = c.state.stencilWriteMask;
    }

    if (key.colourMask == 0 && !stencil)
    {
        return;
    }

    SDL_GPUGraphicsPipeline* pipeline = PipelineFor(key);
    if (pipeline == nullptr)
    {
        return;
    }

    if (bound_ != pipeline)
    {
        SDL_BindGPUGraphicsPipeline(render_, pipeline);
        bound_ = pipeline;
    }

    RenderState s = c.state;
    s.viewport[0] = 0;
    s.viewport[1] = 0;
    s.viewport[2] = renderWidth_;
    s.viewport[3] = renderHeight_;
    s.stencilTest = stencil ? 1 : 0;
    s.stencilReference = static_cast<u8>(c.stencil);
    SetDynamic(s, false);
    if (scissor_[2] <= 0 || scissor_[3] <= 0)
    {
        return;
    }

    SetUniform(p, p.locations["colour"], c.colour, 4, false);
    PushUniforms(p, ownPrograms_[OwnClear]);
    SDL_DrawGPUPrimitives(render_, 3, 1, 0, 0);
    draws_++;
}

void VulkanBackend::Blit(const BlitCommand& c)
{
    if (c.readFramebuffer >= framebuffers_.size() || !framebuffers_[c.readFramebuffer].live)
    {
        return;
    }

    const Framebuffer& from = framebuffers_[c.readFramebuffer];
    const s32 destination[4] = {c.destination[0], c.destination[1], c.destination[2], c.destination[3]};
    if ((c.bits & ClearColour) != 0)
    {
        Texture* t = TextureOf(from.colour);
        if (t != nullptr && t->texture != nullptr)
        {
            DrawCopy(IntegerFormat[t->format] ? OwnBlitUint : OwnBlit, c.drawFramebuffer, from.colour, c.source, destination,
                     c.filter == FilterLinear, c.state, true);
        }
    }

    if ((c.bits & ClearDepth) != 0)
    {
        DrawCopy(OwnBlitDepth, c.drawFramebuffer, from.depth, c.source, destination, false, c.state, true);
    }

    if ((c.bits & ClearStencil) != 0 && told_++ < 3)
    {
        std::fprintf(stderr, "graphics: Vulkan: a stencil copy (left out)\n");
    }
}

void VulkanBackend::Play(const CommandList& list)
{
    ApiSpan span(NativeGraphics::Work::OpenGl);
    // The bytes the list uploads (its vertexes, then its textures' rows, tightly), into the upload buffer at once
    uploadOffsets_.clear();
    size_t total = 0;
    size_t vertexBytes = 0;
    for (const u8* at = list.commands(); at < list.commands() + list.size(); at += reinterpret_cast<const Header*>(at)->bytes)
    {
        const Header& header = *reinterpret_cast<const Header*>(at);
        if (header.op == Op::Vertices)
        {
            vertexBytes += reinterpret_cast<const VerticesCommand*>(at)->count * sizeof(Vertex);
        }
        else if ((header.op == Op::TexImage || header.op == Op::TexSubImage) && reinterpret_cast<const TexImageCommand*>(at)->bytes != 0)
        {
            const auto& c = *reinterpret_cast<const TexImageCommand*>(at);
            total += (CompressedFormat(c.format) ? c.bytes : static_cast<size_t>(c.width) * c.height * TexelBytes[c.format]) + 16;
        }
    }

    total += vertexBytes;
    if (total != 0)
    {
        if (total > uploadSize_)
        {
            if (upload_ != nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device_, upload_);
            }

            uploadSize_ = static_cast<u32>(std::max<size_t>(total + total / 2, 1u << 20));
            SDL_GPUTransferBufferCreateInfo info = {SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, uploadSize_, 0};
            upload_ = SDL_CreateGPUTransferBuffer(device_, &info);
        }

        if (vertexBytes > vertexBufferSize_)
        {
            if (vertexBuffer_ != nullptr)
            {
                SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
            }

            vertexBufferSize_ = static_cast<u32>(std::max<size_t>(vertexBytes + vertexBytes / 2, 1u << 20));
            SDL_GPUBufferCreateInfo info = {SDL_GPU_BUFFERUSAGE_VERTEX, vertexBufferSize_, 0};
            vertexBuffer_ = SDL_CreateGPUBuffer(device_, &info);
        }

        u8* mapped = static_cast<u8*>(SDL_MapGPUTransferBuffer(device_, upload_, true));
        size_t place = 0;
        if (vertexBytes != 0)
        {
            for (const u8* at = list.commands(); at < list.commands() + list.size(); at += reinterpret_cast<const Header*>(at)->bytes)
            {
                if (reinterpret_cast<const Header*>(at)->op == Op::Vertices)
                {
                    const auto& c = *reinterpret_cast<const VerticesCommand*>(at);
                    std::memcpy(mapped + place, list.vertexes.data() + c.first, c.count * sizeof(Vertex));
                    place += c.count * sizeof(Vertex);
                }
            }
        }

        for (const u8* at = list.commands(); at < list.commands() + list.size(); at += reinterpret_cast<const Header*>(at)->bytes)
        {
            const Header& header = *reinterpret_cast<const Header*>(at);
            if ((header.op == Op::TexImage || header.op == Op::TexSubImage) && reinterpret_cast<const TexImageCommand*>(at)->bytes != 0)
            {
                const auto& c = *reinterpret_cast<const TexImageCommand*>(at);
                place = (place + 15) & ~size_t{15};
                uploadOffsets_.push_back(static_cast<u32>(place));
                if (CompressedFormat(c.format))
                {
                    // (A whole level's blocks, as they are)
                    std::memcpy(mapped + place, list.data() + c.data, c.bytes);
                    place += c.bytes;
                    continue;
                }

                const size_t row = static_cast<size_t>(c.width) * TexelBytes[c.format];
                const s32 rowLength = c.rowLength != 0 ? c.rowLength : c.width;
                size_t stride = static_cast<size_t>(rowLength) * TexelBytes[c.format];
                const size_t alignment = static_cast<size_t>(std::max(c.alignment, 1));
                stride = (stride + alignment - 1) / alignment * alignment;
                const u8* from = list.data() + c.data;
                for (s32 y = 0; y < c.height; y++)
                {
                    std::memcpy(mapped + place, from + static_cast<size_t>(y) * stride, row);
                    place += row;
                }
            }
        }

        SDL_UnmapGPUTransferBuffer(device_, upload_);
        if (vertexBytes != 0)
        {
            SDL_GPUTransferBufferLocation from = {upload_, 0};
            SDL_GPUBufferRegion to = {vertexBuffer_, 0, static_cast<Uint32>(vertexBytes)};
            SDL_UploadToGPUBuffer(CopyPass(), &from, &to, true);
        }

        uploaded_ += total;
    }

    size_t nextUpload = 0;
    u32 vertexPlace = 0;
    size_t verticesFirst = 0;
    for (const u8* at = list.commands(); at < list.commands() + list.size(); at += reinterpret_cast<const Header*>(at)->bytes)
    {
        const Header& header = *reinterpret_cast<const Header*>(at);
        switch (header.op)
        {
        case Op::Draw:
        {
            const auto& c = *reinterpret_cast<const DrawCommand*>(at);
            Program* p = ProgramOf(c.program);
            if (p == nullptr || p->vertexShader == nullptr || c.count <= 0 || !BeginRender(c.framebuffer))
            {
                break;
            }

            const RenderState& s = c.state;
            PipelineKey key;
            std::memset(&key, 0, sizeof(key));
            key.program = c.program;
            key.primitive = static_cast<u8>(c.primitive);
            key.vertexes = c.vertexes ? 1 : 0;
            key.colourFormat = static_cast<u8>(renderColourFormat_);
            key.depthTarget = renderDepth_ ? 1 : 0;
            key.colourMask = s.colourMask;
            if (s.depthTest)
            {
                key.depthTest = 1;
                key.depthFunc = s.depthFunc;
                key.depthWrite = s.depthWrite;
            }

            if (s.stencilTest)
            {
                key.stencilTest = 1;
                key.stencilFunc = s.stencilFunc;
                key.stencilFail = s.stencilFail;
                key.stencilDepthFail = s.stencilDepthFail;
                key.stencilPass = s.stencilPass;
                key.stencilReadMask = s.stencilReadMask;
                key.stencilWriteMask = s.stencilWriteMask;
            }

            key.cullFace = s.cullFace;
            key.blend = s.blend;
            // A blend by the second output's alpha that writes the alpha too: drawn twice, the colour blended by the "factor" variant
            // with the alpha left alone, then the alpha alone by the program itself where the first drew (the same tests; the depth
            // the first wrote passes as equal), blended by the alpha's own factors (OpenGL's here: the source's, One and Zero)
            PipelineKey alphaKey;
            bool alphaPass = false;
            if (s.blend)
            {
                key.blendOp = s.blendOp;
                key.blendOpAlpha = s.blendOpAlpha;
                key.blendSource = s.blendSource;
                key.blendDestination = s.blendDestination;
                key.blendSourceAlpha = s.blendSourceAlpha;
                key.blendDestinationAlpha = s.blendDestinationAlpha;
                if (p->dualSource && (UsesFactor(s.blendSource) || UsesFactor(s.blendDestination) || UsesFactor(s.blendSourceAlpha) ||
                                      UsesFactor(s.blendDestinationAlpha)))
                {
                    key.variant = 1;
                    if ((s.colourMask & 8) != 0)
                    {
                        alphaPass = true;
                        alphaKey = key;
                        key.colourMask = s.colourMask & 7;
                        alphaKey.variant = 0;
                        alphaKey.colourMask = 8;
                        alphaKey.blend = !(s.blendOpAlpha == BlendAdd && s.blendSourceAlpha == FactorOne && s.blendDestinationAlpha == FactorZero);
                        if (alphaKey.depthWrite)
                        {
                            alphaKey.depthWrite = 0;
                            alphaKey.depthFunc = CompareEqual;
                        }

                        alphaKey.stencilFail = StencilKeep;
                        alphaKey.stencilDepthFail = StencilKeep;
                        alphaKey.stencilPass = StencilKeep;
                        alphaKey.stencilWriteMask = 0;
                    }
                }
            }

            SDL_GPUGraphicsPipeline* pipeline = key.colourMask != 0 || key.depthWrite || key.stencilWriteMask != 0 || !alphaPass ? PipelineFor(key) : nullptr;
            SDL_GPUGraphicsPipeline* alphaPipeline = alphaPass ? PipelineFor(alphaKey) : nullptr;
            if ((pipeline == nullptr && !alphaPass) || (alphaPass && alphaPipeline == nullptr))
            {
                break;
            }

            SetDynamic(s, s.blend && (UsesConstant(s.blendSource) || UsesConstant(s.blendDestination) || UsesConstant(s.blendSourceAlpha) ||
                                      UsesConstant(s.blendDestinationAlpha)));
            if (scissor_[2] <= 0 || scissor_[3] <= 0)
            {
                break;
            }

            if (c.vertexes && !vertexesBound_)
            {
                SDL_GPUBufferBinding binding = {vertexBuffer_, 0};
                SDL_BindGPUVertexBuffers(render_, 0, &binding, 1);
                vertexesBound_ = true;
            }

            for (SDL_GPUGraphicsPipeline* each : {pipeline, alphaPipeline})
            {
                if (each == nullptr)
                {
                    continue;
                }

                if (bound_ != each)
                {
                    SDL_BindGPUGraphicsPipeline(render_, each);
                    bound_ = each;
                }

                BindSamplers(*p, c.textures);
                PushUniforms(*p, c.program);
                SDL_DrawGPUPrimitives(render_, static_cast<Uint32>(c.count), 1, static_cast<Uint32>(c.first) + (c.vertexes ? vertexBase_ : 0), 0);
                draws_++;
            }

            break;
        }
        case Op::Clear:
            Clear(*reinterpret_cast<const ClearCommand*>(at));
            break;
        case Op::Blit:
            Blit(*reinterpret_cast<const BlitCommand*>(at));
            break;
        case Op::Uniform:
        {
            const auto& c = *reinterpret_cast<const UniformCommand*>(at);
            if (Program* p = ProgramOf(c.program))
            {
                SetUniform(*p, c.location, c.values, c.count, c.array != 0);
            }

            break;
        }
        case Op::TexImage:
        case Op::TexSubImage:
        {
            const auto& c = *reinterpret_cast<const TexImageCommand*>(at);
            Texture* t = TextureOf(c.texture);
            const bool data = c.bytes != 0;
            const size_t offset = data ? uploadOffsets_[nextUpload++] : 0;
            if (t == nullptr)
            {
                break;
            }

            if (header.op == Op::TexImage)
            {
                if (c.level == 0)
                {
                    if (t->texture == nullptr || t->format != c.format || t->width != c.width || t->height != c.height)
                    {
                        // As many levels as its most level asks, down to the short side's 1 (SDL sizes a level as the size
                        // shifted, without OpenGL's least of 1: the levels past that it can neither draw into nor make;
                        // a level uploaded there later is made by EnsureLevels)
                        u32 chain = 1;
                        while ((std::min(c.width, c.height) >> chain) > 0)
                        {
                            chain++;
                        }

                        const u32 levels = IntegerFormat[c.format] || c.format == FormatDepth32fStencil8
                                               ? 1
                                               : std::min<u32>(chain, static_cast<u32>(std::clamp(t->maxLevel, 0, 15)) + 1);
                        // (Its render passes end: the texture they drew into is another now)
                        EndPasses();
                        if (!MakeTexture(*t, c.format, c.width, c.height, levels))
                        {
                            break;
                        }
                    }
                }
                else if (t->texture != nullptr)
                {
                    EnsureLevels(*t, static_cast<u32>(c.level) + 1);
                }
            }

            if (!data || t->texture == nullptr || static_cast<u32>(c.level) >= t->levels)
            {
                break;
            }

            if (c.format == FormatDepth32fStencil8)
            {
                if (told_++ < 3)
                {
                    std::fprintf(stderr, "graphics: Vulkan: a depth upload (left out)\n");
                }

                break;
            }

            // (A block-compressed level's rows and height in whole blocks' texels)
            const s32 blocksAcross = CompressedFormat(c.format) ? (c.width + 3) / 4 * 4 : c.width;
            const s32 blocksDown = CompressedFormat(c.format) ? (c.height + 3) / 4 * 4 : c.height;
            SDL_GPUTextureTransferInfo from = {upload_, static_cast<Uint32>(offset), static_cast<Uint32>(blocksAcross), static_cast<Uint32>(blocksDown)};
            SDL_GPUTextureRegion to = {t->texture,
                                       static_cast<Uint32>(c.level),
                                       0,
                                       static_cast<Uint32>(c.x),
                                       static_cast<Uint32>(c.y),
                                       0,
                                       static_cast<Uint32>(c.width),
                                       static_cast<Uint32>(c.height),
                                       1};
            SDL_UploadToGPUTexture(CopyPass(), &from, &to, false);
            break;
        }
        case Op::TexParam:
        {
            const auto& c = *reinterpret_cast<const TexParamCommand*>(at);
            Texture* t = TextureOf(c.texture);
            if (t == nullptr)
            {
                break;
            }

            switch (c.param)
            {
            case ParamMinFilter:
                t->minFilter = static_cast<u32>(c.value);
                break;
            case ParamMagFilter:
                t->magFilter = static_cast<u32>(c.value);
                break;
            case ParamWrapS:
                t->wrapS = static_cast<u32>(c.value);
                break;
            case ParamWrapT:
                t->wrapT = static_cast<u32>(c.value);
                break;
            case ParamMaxLevel:
                t->maxLevel = c.value;
                break;
            case ParamMaxAnisotropy:
                t->anisotropy = c.valueFloat;
                break;
            default:
                break;
            }

            break;
        }
        case Op::Mipmaps:
        {
            const auto& c = *reinterpret_cast<const TextureCommand*>(at);
            Texture* t = TextureOf(c.texture);
            if (t == nullptr || t->texture == nullptr || IntegerFormat[t->format] || CompressedFormat(t->format))
            {
                break;
            }

            // (SDL makes each level from the one before by a blit of the shifted sizes: down to the short side's 1)
            u32 chain = 1;
            while ((std::min(t->width, t->height) >> chain) > 0)
            {
                chain++;
            }

            EnsureLevels(*t, std::min<u32>(chain, static_cast<u32>(std::clamp(t->maxLevel, 0, 15)) + 1));
            if (t->levels > 1)
            {
                EndPasses();
                SDL_GenerateMipmapsForGPUTexture(Commands(), t->texture);
            }

            break;
        }
        case Op::Attach:
        {
            const auto& c = *reinterpret_cast<const AttachCommand*>(at);
            if (c.framebuffer == 0 || c.framebuffer >= framebuffers_.size())
            {
                break;
            }

            if (render_ != nullptr && renderFramebuffer_ == c.framebuffer)
            {
                EndPasses();
            }

            Framebuffer& f = framebuffers_[c.framebuffer];
            if (c.slot == 0)
            {
                f.colour = c.texture;
                f.colourLevel = c.level;
            }
            else
            {
                f.depth = c.texture;
            }

            break;
        }
        case Op::NoColour:
        {
            const auto& c = *reinterpret_cast<const AttachCommand*>(at);
            if (c.framebuffer != 0 && c.framebuffer < framebuffers_.size())
            {
                if (render_ != nullptr && renderFramebuffer_ == c.framebuffer)
                {
                    EndPasses();
                }

                framebuffers_[c.framebuffer].noColour = true;
            }

            break;
        }
        case Op::Vertices:
        {
            const auto& c = *reinterpret_cast<const VerticesCommand*>(at);
            vertexBase_ = vertexPlace;
            vertexPlace += static_cast<u32>(c.count);
            verticesFirst = c.first;
            break;
        }
        case Op::DeleteTextures:
        {
            const auto& c = *reinterpret_cast<const DeleteCommand*>(at);
            for (u32 i = 0; i < c.count; i++)
            {
                Texture* t = TextureOf(c.names[i]);
                if (t == nullptr || c.names[i] == dummyFloat_ || c.names[i] == dummyUint_)
                {
                    continue;
                }

                if (render_ != nullptr)
                {
                    const Framebuffer& f = framebuffers_[renderFramebuffer_];
                    if (f.colour == c.names[i] || f.depth == c.names[i])
                    {
                        EndPasses();
                    }
                }

                if (t->texture != nullptr && !t->taken)
                {
                    LetGo(t->texture, t->format, t->width, t->height, t->levels);
                }

                if (t->taken)
                {
                    taken_.erase(t->texture);
                }

                *t = Texture{};
                freeTextures_.push_back(c.names[i]);
            }

            break;
        }
        case Op::DeleteFramebuffers:
        {
            const auto& c = *reinterpret_cast<const DeleteCommand*>(at);
            for (u32 i = 0; i < c.count; i++)
            {
                const u32 name = c.names[i];
                if (name == 0 || name >= framebuffers_.size() || !framebuffers_[name].live)
                {
                    continue;
                }

                if (render_ != nullptr && renderFramebuffer_ == name)
                {
                    EndPasses();
                }

                framebuffers_[name] = Framebuffer{};
                freeFramebuffers_.push_back(name);
            }

            break;
        }
        case Op::Label:
            break;
        }
    }

    // (Each playing's passes end with it: the next starts with its vertexes' upload; its command buffer submitted when it's under the
    // recording lock, or after enough work)
    EndPasses();
    if (recordingLocked_ || draws_ >= 8192 || uploaded_ >= (64u << 20))
    {
        SubmitWork(false);
    }
}

void VulkanBackend::Read(u32 framebuffer, s32 x, s32 y, s32 width, s32 height, Format format, s32 rowLength, s32 alignment, void* out)
{
    ApiSpan span(NativeGraphics::Work::CardWait);
    if (framebuffer >= framebuffers_.size() || !framebuffers_[framebuffer].live || width <= 0 || height <= 0)
    {
        return;
    }

    const Framebuffer& f = framebuffers_[framebuffer];
    Texture* t = nullptr;
    u32 level = 0;
    s32 fromX = x;
    s32 fromY = y;
    if (format == FormatDepth32fStencil8)
    {
        // Depth: copied as floats into a texture of its own first (SDL copies depth-stencil textures whole)
        Texture* depth = TextureOf(f.depth);
        if (depth == nullptr || depth->texture == nullptr)
        {
            return;
        }

        if (depthReadTexture_ == 0)
        {
            NewTextures(1, &depthReadTexture_);
            NewFramebuffers(1, &depthReadFramebuffer_);
            framebuffers_[depthReadFramebuffer_].colour = depthReadTexture_;
        }

        Texture& r = textures_[depthReadTexture_];
        if (r.texture == nullptr || r.width < width || r.height < height)
        {
            EndPasses();
            if (!MakeTexture(r, FormatR32f, std::max(width, r.width), std::max(height, r.height), 1))
            {
                return;
            }
        }

        const s32 source[4] = {x, y, x + width, y + height};
        const s32 destination[4] = {0, 0, width, height};
        DrawCopy(OwnBlit, depthReadFramebuffer_, f.depth, source, destination, false, RenderState{}, false);
        t = &r;
        fromX = 0;
        fromY = 0;
    }
    else
    {
        t = TextureOf(f.colour);
        level = static_cast<u32>(f.colourLevel);
    }

    if (t == nullptr || t->texture == nullptr)
    {
        return;
    }

    const u32 texel = TexelBytes[t->format == FormatR32f && format == FormatDepth32fStencil8 ? FormatR32f : t->format];
    const u32 bytes = static_cast<u32>(width) * static_cast<u32>(height) * texel;
    if (bytes > downloadSize_)
    {
        if (download_ != nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device_, download_);
        }

        downloadSize_ = std::max<u32>(bytes, 1u << 20);
        SDL_GPUTransferBufferCreateInfo info = {SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, downloadSize_, 0};
        download_ = SDL_CreateGPUTransferBuffer(device_, &info);
    }

    SDL_GPUTextureRegion from = {t->texture, level, 0, static_cast<Uint32>(fromX), static_cast<Uint32>(fromY), 0,
                                 static_cast<Uint32>(width), static_cast<Uint32>(height), 1};
    SDL_GPUTextureTransferInfo to = {download_, 0, static_cast<Uint32>(width), static_cast<Uint32>(height)};
    SDL_DownloadFromGPUTexture(CopyPass(), &from, &to);
    SubmitWork(true);
    const u8* mapped = static_cast<const u8*>(SDL_MapGPUTransferBuffer(device_, download_, false));
    if (mapped == nullptr)
    {
        return;
    }

    // Into OUT with OpenGL's pack rows
    const size_t row = static_cast<size_t>(width) * texel;
    size_t stride = static_cast<size_t>(rowLength != 0 ? rowLength : width) * texel;
    const size_t align = static_cast<size_t>(std::max(alignment, 1));
    stride = (stride + align - 1) / align * align;
    for (s32 i = 0; i < height; i++)
    {
        std::memcpy(static_cast<u8*>(out) + static_cast<size_t>(i) * stride, mapped + static_cast<size_t>(i) * row, row);
    }

    SDL_UnmapGPUTransferBuffer(device_, download_);
}

bool VulkanBackend::FramebufferComplete(u32 framebuffer)
{
    if (framebuffer >= framebuffers_.size() || !framebuffers_[framebuffer].live)
    {
        return false;
    }

    const Framebuffer& f = framebuffers_[framebuffer];
    const Texture* colour = f.noColour ? nullptr : TextureOf(f.colour);
    const Texture* depth = TextureOf(f.depth);
    return (colour != nullptr && colour->texture != nullptr) || (depth != nullptr && depth->texture != nullptr);
}

void* VulkanBackend::Fence()
{
    ApiSpan span(NativeGraphics::Work::OpenGl);
    // (Nothing recorded since the last submit: no fence needed, the queue keeps the order)
    if (commands_ == nullptr)
    {
        return nullptr;
    }

    EndPasses();
    SDL_GPUFence* fence = nullptr;
    {
        VulkanPartSpan part(NativeGraphics::VulkanPart::Submits);
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands_);
    }

    TrackFlight(fence, device_, window_ != nullptr, false, draws_, uploaded_);
    commands_ = nullptr;
    draws_ = 0;
    uploaded_ = 0;
    if (recordingLocked_)
    {
        recordingLocked_ = false;
        g_RecordingLock.unlock();
    }

    return fence;
}
}

u32 VulkanBackend::TakeTexture(void* native, s32 width, s32 height)
{
    if (native == nullptr)
    {
        return 0;
    }

    u32 name = 0;
    auto found = taken_.find(native);
    if (found != taken_.end())
    {
        name = found->second;
    }
    else
    {
        NewTextures(1, &name);
        taken_[native] = name;
    }

    // (The renderer's picture: RGBA8, one level, clamped at its edges as the renderer's copy of it is)
    Texture& t = textures_[name];
    if (!t.taken)
    {
        t.wrapS = WrapClamp;
        t.wrapT = WrapClamp;
    }

    t.texture = static_cast<SDL_GPUTexture*>(native);
    t.taken = true;
    t.format = FormatRgba8;
    t.width = width;
    t.height = height;
    t.levels = 1;
    t.maxLevel = 0;
    return name;
}

bool VulkanBackend::WindowFrame(s32 width, s32 height)
{
    if (window_ == nullptr || width <= 0 || height <= 0)
    {
        return false;
    }

    Texture& t = textures_[windowTexture_];
    if (t.texture == nullptr || t.width != width || t.height != height)
    {
        t.maxLevel = 0;
        EndPasses();
        if (!MakeTexture(t, FormatRgba8, width, height, 1))
        {
            return false;
        }
    }

    return true;
}

void VulkanBackend::ShowWindow()
{
    if (window_ == nullptr)
    {
        return;
    }

    // The frame's texture copied into the swapchain's (rows turned over: the texture's are OpenGL's, the first the bottom one),
    // which is shown when the command buffer is submitted. None while the window is minimised: nothing to show
    SDL_GPUCommandBuffer* commands = Commands();
    EndPasses();
    const Texture* t = TextureOf(windowTexture_);
    SDL_GPUTexture* swapchain = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    const u64 acquireStart = SDL_GetTicksNS();
    g_AcquireStart = acquireStart;
    const bool acquired = SDL_WaitAndAcquireGPUSwapchainTexture(commands, window_, &swapchain, &width, &height);
    g_AcquireStart = 0;
    g_LastAcquire = SDL_GetTicksNS() - acquireStart;
    if (!acquired)
    {
        if (told_++ < 3)
        {
            std::fprintf(stderr, "graphics: Vulkan: no swapchain picture: %s\n", SDL_GetError());
        }
    }
    else if (swapchain != nullptr && t != nullptr && t->texture != nullptr)
    {
        SDL_GPUBlitInfo blit = {};
        blit.source.texture = t->texture;
        blit.source.w = static_cast<Uint32>(t->width);
        blit.source.h = static_cast<Uint32>(t->height);
        blit.destination.texture = swapchain;
        blit.destination.w = width;
        blit.destination.h = height;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.flip_mode = SDL_FLIP_VERTICAL;
        blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(commands, &blit);
    }

    SubmitWork(false, true);
}

void VulkanBackend::SetVSync(bool on)
{
    if (window_ == nullptr)
    {
        return;
    }

    // Off: as soon as drawn (tearing, as OpenGL's swap interval 0), else the next blank without tearing, else at the blank
    SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!on && SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_IMMEDIATE))
    {
        mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
    }
    else if (!on && SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_MAILBOX))
    {
        mode = SDL_GPU_PRESENTMODE_MAILBOX;
    }

    if (!SDL_SetGPUSwapchainParameters(device_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
    {
        std::fprintf(stderr, "graphics: Vulkan: the window's presenting not changed: %s\n", SDL_GetError());
    }
}

Backend* StartVulkanBackend(bool own)
{
    auto* backend = new VulkanBackend;
    if (!backend->Start(nullptr, own))
    {
        delete backend;
        return nullptr;
    }

    return backend;
}

Backend* StartVulkanWindowBackend(void* window)
{
    auto* backend = new VulkanBackend;
    if (!backend->Start(static_cast<SDL_Window*>(window), false))
    {
        delete backend;
        return nullptr;
    }

    return backend;
}

bool VulkanBuilt()
{
    return true;
}
}

#else

namespace Hw
{
Backend* StartVulkanBackend(bool)
{
    return nullptr;
}

Backend* StartVulkanWindowBackend(void*)
{
    return nullptr;
}

bool VulkanBuilt()
{
    return false;
}
}

#endif
