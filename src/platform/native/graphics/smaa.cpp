#include "smaa.h"

#include "hw/gpu.h"
#include "native.h"

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
    // The window's backend's recording it was made for
    Hw::Gpu* gpu = nullptr;
    bool tried = false;
    bool usable = false;
    u32 copyProgram = 0;
    u32 edgesProgram = 0;
    u32 weightsProgram = 0;
    u32 blendProgram = 0;
    u32 areaTexture = 0;
    u32 searchTexture = 0;
    // The picture's copy, the edges, the weights and the result, and a framebuffer for each
    u32 textures[4] = {};
    u32 framebuffers[4] = {};
    u32 width = 0;
    u32 height = 0;
};
Smaa g_Smaa;

u32 Link(Hw::Gpu& gpu, const std::string& vertex, const std::string& fragment)
{
    const u32 program = gpu.StartProgram(vertex.c_str(), fragment, false);
    if (!gpu.FinishProgram(program))
    {
        Native::Log("graphics: SMAA's program wasn't made (the error output says why)");
        return 0;
    }

    return program;
}

u32 PassProgram(Hw::Gpu& gpu, const Pass& pass)
{
    std::string vertex = std::string(Prelude) + "#define SMAA_INCLUDE_VS 1\n#define SMAA_INCLUDE_PS 0\n" + SmaaHlsl + "\n" +
                         pass.vertexDeclarations + "void main()\n{\n" + QuadVertex + pass.vertexCall + "}\n";
    std::string fragment = std::string(Prelude) + "#define SMAA_INCLUDE_VS 0\n#define SMAA_INCLUDE_PS 1\n" + SmaaHlsl + "\n" +
                           pass.fragmentDeclarations + "void main()\n{\n" + pass.fragmentBody + "}\n";
    return Link(gpu, vertex, fragment);
}

// The bound texture's sampling: FILTER, its edges clamped, one level
void Sampling(Hw::Gpu& gpu, Hw::Filter filter)
{
    gpu.TexParameter(Hw::ParamMaxLevel, 0);
    gpu.TexParameter(Hw::ParamMinFilter, static_cast<s32>(filter));
    gpu.TexParameter(Hw::ParamMagFilter, static_cast<s32>(filter));
    gpu.TexParameter(Hw::ParamWrapS, static_cast<s32>(Hw::WrapClamp));
    gpu.TexParameter(Hw::ParamWrapT, static_cast<s32>(Hw::WrapClamp));
}

bool Prepare(Smaa& s, Hw::Gpu& gpu)
{
    if (s.tried && s.gpu == &gpu)
    {
        return s.usable;
    }

    // (Another backend's recording: everything made again there)
    s = Smaa();
    s.gpu = &gpu;
    s.tried = true;
    s.copyProgram = Link(gpu, CopyVertex, CopyFragment);
    s.edgesProgram = PassProgram(gpu, EdgesPass);
    s.weightsProgram = PassProgram(gpu, WeightsPass);
    s.blendProgram = PassProgram(gpu, BlendPass);
    if (s.copyProgram == 0 || s.edgesProgram == 0 || s.weightsProgram == 0 || s.blendProgram == 0)
    {
        return false;
    }

    gpu.PixelStore(Hw::StoreUnpackAlignment, 1);
    gpu.PixelStore(Hw::StoreUnpackRowLength, 0);
    gpu.GenTextures(1, &s.areaTexture);
    gpu.BindTexture(s.areaTexture);
    Sampling(gpu, Hw::FilterLinear);
    gpu.TexImage(0, Hw::FormatRg8, AREATEX_WIDTH, AREATEX_HEIGHT, areaTexBytes);
    gpu.GenTextures(1, &s.searchTexture);
    gpu.BindTexture(s.searchTexture);
    Sampling(gpu, Hw::FilterNearest);
    gpu.TexImage(0, Hw::FormatR8, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, searchTexBytes);
    gpu.PixelStore(Hw::StoreUnpackAlignment, 4);
    gpu.GenTextures(4, s.textures);
    gpu.GenFramebuffers(4, s.framebuffers);
    s.usable = true;
    Native::Log("graphics: SMAA 1x (iryoku's, the high preset) ready");
    return true;
}

// The pass targets at the picture's size: its copy (RGBA), the edges (RG), the weights (RGBA) and the result (RGBA)
bool Size(Smaa& s, Hw::Gpu& gpu, u32 width, u32 height)
{
    if (s.width == width && s.height == height)
    {
        return true;
    }

    const Hw::Format formats[4] = {Hw::FormatRgba8, Hw::FormatRg8, Hw::FormatRgba8, Hw::FormatRgba8};
    for (u32 i = 0; i < 4; i++)
    {
        gpu.BindTexture(s.textures[i]);
        Sampling(gpu, Hw::FilterLinear);
        gpu.TexImage(0, formats[i], static_cast<s32>(width), static_cast<s32>(height), nullptr);
        gpu.BindFramebuffer(s.framebuffers[i]);
        gpu.AttachColour(s.textures[i]);
        if (!gpu.FramebufferComplete())
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

void Bind(Hw::Gpu& gpu, u32 program, const char* name, u32 unit, u32 texture)
{
    gpu.ActiveTexture(unit);
    gpu.BindTexture(texture);
    gpu.Uniform1i(gpu.UniformLocation(program, name), static_cast<s32>(unit));
}

void Target(Smaa& s, Hw::Gpu& gpu, u32 index, u32 program, bool clear)
{
    gpu.BindFramebuffer(s.framebuffers[index]);
    gpu.Viewport(0, 0, static_cast<s32>(s.width), static_cast<s32>(s.height));
    if (clear)
    {
        gpu.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gpu.Clear(Hw::ClearColour);
    }

    gpu.UseProgram(program);
    gpu.Uniform4f(gpu.UniformLocation(program, "rtMetrics"), 1.0f / static_cast<f32>(s.width), 1.0f / static_cast<f32>(s.height),
                  static_cast<f32>(s.width), static_cast<f32>(s.height));
}
}

unsigned int SmaaPicture(Hw::Gpu& gpu, unsigned int texture, f32 u0, f32 v0, f32 u1, f32 v1, u32 width, u32 height)
{
    Smaa& s = g_Smaa;
    if (width == 0 || height == 0 || !Prepare(s, gpu) || !s.usable || !Size(s, gpu, width, height))
    {
        return 0;
    }

    gpu.Disable(Hw::CapBlend);
    gpu.Disable(Hw::CapDepthTest);
    gpu.Disable(Hw::CapScissorTest);
    gpu.Disable(Hw::CapStencilTest);
    gpu.ColorMask(true, true, true, true);
    gpu.BindVertexes(false);
    enum
    {
        Copy,
        Edges,
        Weights,
        Result,
    };

    // The picture's rectangle into a texture of its own
    Target(s, gpu, Copy, s.copyProgram, false);
    gpu.Uniform4f(gpu.UniformLocation(s.copyProgram, "uvRect"), u0, v0, u1, v1);
    Bind(gpu, s.copyProgram, "picture", 0, texture);
    gpu.DrawArrays(Hw::PrimitiveTriangleStrip, 0, 4);
    // The edges (the pixels without one discarded: cleared first), the blending weights, the neighbours blended
    Target(s, gpu, Edges, s.edgesProgram, true);
    Bind(gpu, s.edgesProgram, "colorTex", 0, s.textures[Copy]);
    gpu.DrawArrays(Hw::PrimitiveTriangleStrip, 0, 4);
    Target(s, gpu, Weights, s.weightsProgram, true);
    Bind(gpu, s.weightsProgram, "edgesTex", 0, s.textures[Edges]);
    Bind(gpu, s.weightsProgram, "areaTex", 1, s.areaTexture);
    Bind(gpu, s.weightsProgram, "searchTex", 2, s.searchTexture);
    gpu.DrawArrays(Hw::PrimitiveTriangleStrip, 0, 4);
    Target(s, gpu, Result, s.blendProgram, false);
    Bind(gpu, s.blendProgram, "colorTex", 0, s.textures[Copy]);
    Bind(gpu, s.blendProgram, "blendTex", 1, s.textures[Weights]);
    gpu.DrawArrays(Hw::PrimitiveTriangleStrip, 0, 4);
    gpu.ActiveTexture(0);
    gpu.BindFramebuffer(0);
    return s.textures[Result];
}
}
