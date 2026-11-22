#include "renderer.h"
#include "gsvalues.h"

#include <bit>
#include <cmath>
#include "gsregs.h"

namespace
{
u64 Zeroed(u32 value)
{
    return value;
}

// The half-size buffer's pixels are 16 bit (PSMCT16S), and past it (at 11 bytes a pixel of the screen) is a buffer the depth goes
// into. The shadows' sprites and strips are drawn at a depth nothing's behind, and a strip is 32 pixels wide (narrow ones half
// that in the half-size buffer)
constexpr s32 PastHalfSizePixelOffset = 11;
const u64 HalfSizeFormat = std::bit_cast<u64>(GS_FRAME{.psm = GS_PIXMODE_16S});
const u64 ShadowDepth = std::bit_cast<u64>(GS_XYZ{.z = 0xFFFF});
constexpr u32 NarrowStripShift = 5;
constexpr u32 WideStripShift = 6;
constexpr u32 StripStep = 0x200;
constexpr u32 StripShift = 9;
constexpr u32 NarrowStep = 0x100;
constexpr u32 NarrowShift = 8;
u64 DepthBuffer(u32 page)
{
    return Zeroed(page) | ZbufZ32Format | DepthNotWritten;
}
}

extern "C"
{
    // The view's matrix to the screen while the shadows are drawn (the half-size buffer's), and the matrix kept to put back
    extern Matrix4x4 g_ShadowToCamera RETAIL(D_003B0770);
    extern Matrix4x4 g_ShadowToScreen RETAIL(D_003B07B0);
    extern Matrix4x4 g_SavedToScreen RETAIL(D_003BDBC0);
    // The shadows' own set-up packet, and the GS memory the frame's alpha blends with (the first texture slot, let go of)
    extern u8 g_ShadowSetUp[] RETAIL(D_003D6840);
    extern u32 g_ShadowTextureAddress RETAIL(D_0030ABA8);

    void UseHalfSizeScreen(RenderView* view, const RenderTargetDescription* target)
    {
        g_SavedToScreen = view->toScreen;
        Matrix4x4 projection;
        HalfSizeScreenMatrix(target, &projection);
        VuMultiplyMatrices(&view->toClip, &projection, &view->toScreen);
    }

    void RestoreViewScreenMatrix(RenderView* view)
    {
        view->toScreen = g_SavedToScreen;
    }

    void StartShadows(void*)
    {
        // 43 settings, 2 a wide strip of the clear, 4 a narrow strip of the two shrinks and 4 a wide one of the blend
        constexpr u32 SettingWrites = 0x2B;
        // TEXA's alpha of 0x80 for 16 bit texels without their alpha bit (TA0), CLAMP clamping both ways, the clear's grey (alpha
        // 0x78 too), the shrinks' frames (only alpha written; the depth as 32 bit depth), the frame and the depth as textures,
        // the depth's copy as 8 bit indexes (its pixels' top bytes) of the texture slot's palette as highlighting (HIGHLIGHT2),
        // ALPHA's Cs - Cd (FIX 128) and Cs + Cd (FIX 128), the shadows' strips in UV of the second context blended, their test
        // of the destination alpha (its top bit 0: drawn) and depth GEQUAL, and the half-size buffer's alpha not written
        const u64 FirstAlpha = std::bit_cast<u64>(GS_TEXA{.alpha_0 = 0x80});
        const u64 ClampBoth = std::bit_cast<u64>(GS_CLAMP{.wrap_mode_s = 1, .wrap_mode_t = 1});
        const u64 ClearGrey = std::bit_cast<u64>(GS_RGBAQ{.r = 0x78, .g = 0x78, .b = 0x78, .a = 0x78, .q = 1.0f});
        const u64 AlphaOnly = std::bit_cast<u64>(GS_FRAME{.psm = GS_PIXMODE_16S, .draw_mask = 0xFFFFFF});
        constexpr u32 DepthAsFrame = u32{GS_ZBUFF_32} << 24;
        const u64 FrameAsTexture = std::bit_cast<u64>(GS_TEX0{.tex_width = 10, .tex_height = 10, .tex_cc = 1});
        const u64 DepthAsTexture =
            std::bit_cast<u64>(GS_TEX0{.psm = GS_ZBUFF_32, .tex_width = 10, .tex_height = 10, .tex_cc = 1});
        const u64 IndexesAsTexture = std::bit_cast<u64>(
            GS_TEX0{.psm = GS_TEX_8H, .tex_width = 10, .tex_height = 10, .tex_cc = 1, .tex_funtion = 3, .clut_loadmode = 1});
        const u64 SourceMinusFrame = std::bit_cast<u64>(GS_ALPHA{.b = 1, .c = 2, .d = 2, .alpha = 0x80});
        const u64 Grey = std::bit_cast<u64>(GS_RGBAQ{.r = 0x80, .g = 0x80, .b = 0x80});
        const u64 AddedStrips = std::bit_cast<u64>(GS_PRIM{.prim_type = GS_PRIM_TRI_STRIP, .abe = 1, .fst = 1, .ctxt = 1});
        const u64 Added = std::bit_cast<u64>(GS_ALPHA{.b = 2, .c = 2, .d = 1, .alpha = 0x80});
        const u64 UndrawnAndNearer = std::bit_cast<u64>(
            GS_TEST{.datest_enable = 1, .ztest_enable = 1, .ztest_method = GS_ZBUFF_GEQUAL});
        const u64 Attributes = std::bit_cast<u64>(GS_PRMODE{.abe = 1, .fst = 1, .ctxt = 1});
        const u64 ColourOnly = std::bit_cast<u64>(GS_FRAME{.psm = GS_PIXMODE_16S, .draw_mask = 0xFF000000});
        UseHalfSizeScreen(g_RenderView, g_RenderTarget);
        RenderBucket& bucket = g_FrameBuckets.buckets[BucketShadows];
        u32 width = g_DisplayWidth;
        u32 height = g_DisplayHeight;
        u32 narrowStrips = width >> NarrowStripShift;
        u32* at = BeginPacket(bucket);
        g_ShadowTextureAddress = ClaimFirstTextureSlot();
        u32 wideStrips = width >> WideStripShift;
        // The asm counts the third set of strips as wide ones (the same at 512 pixels)
        u32 pairCount = 6 * wideStrips + 8 * narrowStrips + SettingWrites;
        u32 pixels = width * height;
        u32 halfSizePage = pixels * HalfSizeBufferPixelOffset >> GsPageShift;
        u64 halfWidth = static_cast<u64>(width >> (GsWidthShift + 1)) << GsFrameWidthShift;
        at[0] = CallTag;
        at[1] = Address(g_ShadowSetUp);
        at[2] = 0;
        at[3] = 0;
        at[4] = (pairCount + 1) | CountTag;
        at[5] = 0;
        at[6] = VifFlushA;
        at[7] = (pairCount + 1) | VifDirect;
        GsWriter pairs{reinterpret_cast<GsWrite*>(at + 8)};
        pairs.Write(AddressDataTag(pairCount), GifAddressData);

        // The half-size buffer cleared to grey
        pairs.Write(0, GsTexFlush);
        pairs.Write(AttributesFromPrim, GsPrmodeCont);
        pairs.Write(0, SecondContext(GsXyOffset));
        pairs.Write(Zeroed(halfSizePage) | halfWidth | HalfSizeFormat, SecondContext(GsFrame));
        pairs.Write(DepthBuffer(pixels * DepthBufferPixelOffset >> GsPageShift), SecondContext(GsZbuf));
        pairs.Write(static_cast<u64>(width >> 1) << GsScissorRightShift | static_cast<u64>(height >> 1) << GsScissorBottomShift,
                    SecondContext(GsScissor));
        pairs.Write(ClampBoth, SecondContext(GsClamp));
        pairs.Write(ClampColours, GsColClamp);
        pairs.Write(0, SecondContext(GsFba));
        pairs.Write(FirstAlpha, GsTexa);
        pairs.Write(0, GsDither);
        pairs.Write(DepthAlways, SecondContext(GsTest));
        pairs.Write(FlatSprite, GsPrim);
        pairs.Write(ClearGrey, GsRgbaq);
        u64 halfBottom = static_cast<u64>(height << (GsSubpixelShift - 1)) << GsXyzYShift;
        for (u32 strip = 0; strip < wideStrips; strip++)
        {
            pairs.Write(Zeroed(strip << StripShift) | ShadowDepth, GsXyz2);
            pairs.Write(Zeroed(StripStep + strip * StripStep) | halfBottom | ShadowDepth, GsXyz2);
        }

        // The frame shrunk into it, its colour masked, then the depth buffer into the buffer past it (as 32 bit depth)
        u64 frameBottom = static_cast<u64>((height << GsSubpixelShift) + GsHalfTexel) << GsUvVShift;
        auto shrink = [&]()
        {
            for (u32 strip = 0; strip < narrowStrips; strip++)
            {
                pairs.Write(Zeroed(GsHalfTexel + strip * StripStep) | HalfTexelDown, GsUv);
                pairs.Write(Zeroed(strip << NarrowShift) | ShadowDepth, GsXyzf2);
                pairs.Write(Zeroed(StripStep + GsHalfTexel + strip * StripStep) | frameBottom, GsUv);
                pairs.Write(Zeroed(NarrowStep + strip * NarrowStep) | halfBottom | ShadowDepth, GsXyzf2);
            }
        };
        pairs.Write(0, GsTexFlush);
        pairs.Write(Zeroed(halfSizePage) | halfWidth | AlphaOnly, SecondContext(GsFrame));
        pairs.Write(TexturedSprite, GsPrim);
        pairs.Write(Zeroed(pixels >> FrameBufferBlocksShift) |
                        static_cast<u64>(width >> GsWidthShift) << GsTextureWidthShift | FrameAsTexture,
                    SecondContext(GsTex0));
        pairs.Write(0, SecondContext(GsTex1));
        pairs.Write(White, GsRgbaq);
        shrink();
        pairs.Write(0, GsTexFlush);
        pairs.Write(Zeroed(pixels * PastHalfSizePixelOffset >> GsPageShift |
                           static_cast<u32>(width >> (GsWidthShift + 1)) << GsFrameWidthShift | DepthAsFrame),
                    SecondContext(GsFrame));
        pairs.Write(TexturedSprite, GsPrim);
        pairs.Write(Zeroed(pixels * DepthBufferPixelOffset >> GsBlockShift) |
                        static_cast<u64>(width >> GsWidthShift) << GsTextureWidthShift | DepthAsTexture,
                    SecondContext(GsTex0));
        pairs.Write(0, SecondContext(GsTex1));
        pairs.Write(White, GsRgbaq);
        shrink();

        // The half-size buffer blended with the texture slot's memory through the depth's palette
        u32 pastHalfSizePage = pixels * PastHalfSizePixelOffset >> GsPageShift;
        pairs.Write(0, GsTexFlush);
        pairs.Write(Zeroed(pastHalfSizePage) | halfWidth, SecondContext(GsFrame));
        pairs.Write(BlendedSprite, GsPrim);
        pairs.Write(Zeroed(pixels * PastHalfSizePixelOffset >> GsBlockShift) |
                        static_cast<u64>(width >> (GsWidthShift + 1)) << GsTextureWidthShift |
                        static_cast<u64>(g_ShadowTextureAddress) << GsPaletteShift | IndexesAsTexture,
                    SecondContext(GsTex0));
        pairs.Write(SourceMinusFrame, SecondContext(GsAlpha));
        pairs.Write(Grey, GsRgbaq);
        u64 copyBottom = static_cast<u64>((height << (GsSubpixelShift - 1)) + GsHalfTexel) << GsUvVShift;
        for (u32 strip = 0; strip < narrowStrips >> 1; strip++)
        {
            pairs.Write(Zeroed(GsHalfTexel + strip * StripStep) | HalfTexelDown, GsUv);
            pairs.Write(Zeroed(strip << StripShift) | ShadowDepth, GsXyzf2);
            pairs.Write(Zeroed(StripStep + GsHalfTexel + strip * StripStep) | copyBottom, GsUv);
            pairs.Write(Zeroed(StripStep + strip * StripStep) | halfBottom | ShadowDepth, GsXyzf2);
        }

        // Drawing set up for the shadows into the half-size buffer
        pairs.Write(0, GsTexFlush);
        pairs.Write(DepthBuffer(pastHalfSizePage), SecondContext(GsZbuf));
        pairs.Write(AddedStrips, GsPrim);
        pairs.Write(Zeroed((GsScreenMiddle - (width >> 2)) << GsSubpixelShift) |
                        static_cast<u64>((GsScreenMiddle - (height >> 2)) << GsSubpixelShift) << GsOffsetYShift,
                    SecondContext(GsXyOffset));
        pairs.Write(static_cast<u64>((width >> 1) - 1) << GsScissorRightShift |
                        static_cast<u64>((height >> 1) - 1) << GsScissorBottomShift,
                    SecondContext(GsScissor));
        pairs.Write(0, GsColClamp);
        pairs.Write(Added, SecondContext(GsAlpha));
        pairs.Write(UndrawnAndNearer, SecondContext(GsTest));
        pairs.Write(AttributesFromPrmode, GsPrmodeCont);
        pairs.Write(Attributes, GsPrmode);
        pairs.Write(Zeroed(halfSizePage) | halfWidth | ColourOnly, SecondContext(GsFrame));
        EndPacket(bucket, reinterpret_cast<u8*>(pairs.at));

        // The shadows' matrix to the half-size screen
        g_ShadowToCamera = g_RenderView->toClip;
        Matrix4x4 projection;
        HalfSizeScreenMatrix(g_RenderTarget, &projection);
        VuMultiplyMatrices(&g_ShadowToCamera, &projection, &g_ShadowToScreen);
    }

    void ApplyShadows(void*)
    {
        // 22 settings, 2 a wide strip of the clear and 4 a narrow one of the darkening
        constexpr u32 SettingWrites = 0x16;
        // The clear's frame (the alpha and the colours' top bits not written) and black; the half-size buffer as a 512 by 512
        // texture of its alpha, bilinear; ALPHA's Cd - Cs * 32 / 128
        const u64 ClearMask = std::bit_cast<u64>(GS_FRAME{.psm = GS_PIXMODE_16S, .draw_mask = 0xFF808080});
        const u64 Black = std::bit_cast<u64>(GS_RGBAQ{.q = 1.0f});
        const u64 HalfSizeTexture = std::bit_cast<u64>(GS_TEX0{.psm = GS_TEX_16S, .tex_width = 9, .tex_height = 9, .tex_cc = 1});
        const u64 QuarterTakenAway = std::bit_cast<u64>(GS_ALPHA{.a = 2, .c = 2, .d = 1, .alpha = 0x20});
        RestoreViewScreenMatrix(g_RenderView);
        RenderBucket& bucket = g_FrameBuckets.buckets[BucketShadows];
        u32 width = g_DisplayWidth;
        u32 height = g_DisplayHeight;
        u32 narrowStrips = width >> NarrowStripShift;
        u32 wideStrips = width >> WideStripShift;
        u32 pairCount = (wideStrips << 1) + (narrowStrips << 2) + SettingWrites;
        u32 pixels = width * height;
        u32* tag = BeginPacket(bucket);
        // The GIF's data and a FLUSHA after it (the DIRECT's count the GIF tag's quadword ORed in)
        tag[0] = (pairCount + 2) | CountTag;
        tag[1] = 0;
        tag[2] = VifFlushA;
        tag[3] = pairCount | VifDirect | 1;
        GsWriter pairs{reinterpret_cast<GsWrite*>(tag + 4)};
        pairs.Write(AddressDataTag(pairCount), GifAddressData);

        // The half-size buffer (16 bit, past ten bytes a pixel of the screen) cleared to black in strips of 32 pixels
        pairs.Write(AttributesFromPrim, GsPrmodeCont);
        pairs.Write(0, GsTexFlush);
        pairs.Write(0, SecondContext(GsXyOffset));
        pairs.Write(static_cast<u64>(width >> 1) << GsScissorRightShift | static_cast<u64>(height >> 1) << GsScissorBottomShift,
                    SecondContext(GsScissor));
        pairs.Write(Zeroed(pixels * HalfSizeBufferPixelOffset >> GsPageShift) |
                        static_cast<u64>(width >> (GsWidthShift + 1)) << GsFrameWidthShift | ClearMask,
                    SecondContext(GsFrame));
        pairs.Write(DepthAlways, SecondContext(GsTest));
        pairs.Write(FlatSprite, GsPrim);
        pairs.Write(Black, GsRgbaq);
        u64 halfBottom = static_cast<u64>(height << (GsSubpixelShift - 1)) << GsXyzYShift;
        for (u32 strip = 0; strip < wideStrips; strip++)
        {
            pairs.Write(Zeroed(strip << StripShift) | ShadowDepth, GsXyzf2);
            pairs.Write(Zeroed(StripStep + strip * StripStep) | halfBottom | ShadowDepth, GsXyzf2);
        }

        // The screen again, a quarter of the buffer taken away from it (Cd - Cs * 32 / 128) through bilinear sprites
        pairs.Write(0, GsTexFlush);
        pairs.Write(AttributesFromPrim, GsPrmodeCont);
        pairs.Write(Zeroed(pixels >> FrameBufferPagesShift) | static_cast<u64>(width >> GsWidthShift) << GsFrameWidthShift,
                    SecondContext(GsFrame));
        pairs.Write(DepthBuffer(pixels * DepthBufferPixelOffset >> GsPageShift), SecondContext(GsZbuf));
        pairs.Write(0, SecondContext(GsXyOffset));
        pairs.Write(WholeScissor, SecondContext(GsScissor));
        pairs.Write(ClampColours, GsColClamp);
        pairs.Write(Zeroed(pixels * HalfSizeBufferPixelOffset >> GsBlockShift) |
                        static_cast<u64>(width >> (GsWidthShift + 1)) << GsTextureWidthShift | HalfSizeTexture,
                    SecondContext(GsTex0));
        pairs.Write(Bilinear, SecondContext(GsTex1));
        pairs.Write(DepthAlways, SecondContext(GsTest));
        pairs.Write(BlendedSprite, GsPrim);
        pairs.Write(QuarterTakenAway, SecondContext(GsAlpha));
        pairs.Write(White, GsRgbaq);
        u64 textureBottom = static_cast<u64>((height << (GsSubpixelShift - 1)) + GsHalfTexel) << GsUvVShift;
        u64 screenBottom = static_cast<u64>(height << GsSubpixelShift) << GsXyzYShift;
        for (u32 strip = 0; strip < narrowStrips; strip++)
        {
            pairs.Write(Zeroed(GsHalfTexel + strip * NarrowStep) | HalfTexelDown, GsUv);
            pairs.Write(Zeroed(strip << StripShift) | ShadowDepth, GsXyz2);
            pairs.Write(Zeroed(NarrowStep + GsHalfTexel + strip * NarrowStep) | textureBottom, GsUv);
            pairs.Write(Zeroed(StripStep + strip * StripStep) | screenBottom | ShadowDepth, GsXyz2);
        }

        pairs.Write(AttributesFromPrmode, GsPrmodeCont);
        auto* flush = reinterpret_cast<u32*>(pairs.at);
        flush[0] = VifFlushA;
        flush[1] = 0;
        flush[2] = 0;
        flush[3] = 0;
        EndPacket(bucket, reinterpret_cast<u8*>(flush + 4));
    }
}

namespace Platform::Graphics
{
// The palette's upload: a RET tag with VIF1's FLUSHA and DIRECT of the GIF packet: BITBLTBUF (to the slot, 64 pixels a row, 32
// bit), TRXPOS, TRXREG (16 by 16), TRXDIR, then the image of 64 quadwords (alpha from 255 down, the colour white)
void SetUpShadowPass()
{
    constexpr u32 PaletteEntries = 0x100;
    constexpr u32 SetUpWrites = 4;
    constexpr u32 ImageQuadwords = PaletteEntries * sizeof(u32) / 0x10;
    constexpr u32 Quadwords = 1 + SetUpWrites + 1 + ImageQuadwords;
    constexpr u32 WhiteColour = 0xFFFFFF;
    // Adding 255 to the alpha byte takes one away
    constexpr u32 FullAlpha = 0xFF000000;
    const u64 ToSlot = std::bit_cast<u64>(GS_BITBLTBUF{.dest_width = 1, .dest_pixmode = GS_PIXMODE_32});
    constexpr u32 BitBltDestinationShift = 32;
    g_ShadowTextureAddress = ClaimFirstTextureSlot();
    auto* words = reinterpret_cast<u32*>(g_ShadowSetUp);
    words[0] = ReturnTag | Quadwords;
    words[1] = 0;
    words[2] = VifFlushA;
    words[3] = VifDirect | Quadwords;
    GifTag setUp = {};
    setUp.loops = SetUpWrites;
    setUp.registerCount = 1;
    GsWriter pairs{reinterpret_cast<GsWrite*>(g_ShadowSetUp + 0x10)};
    pairs.Write(setUp.value, GifAddressData);
    pairs.Write(static_cast<u64>(g_ShadowTextureAddress) << BitBltDestinationShift | ToSlot, GsBitBltBuf);
    pairs.Write(0, GsTrxPos);
    pairs.Write(PaletteSize, GsTrxReg);
    pairs.Write(0, GsTrxDir);
    pairs.Write(ImageDataTag(ImageQuadwords), 0);
    auto* palette = reinterpret_cast<u32*>(pairs.at);
    u32 alpha = FullAlpha;
    for (u32 index = 0; index < PaletteEntries; index++)
    {
        palette[PaletteSlot(index)] = alpha | WhiteColour;
        alpha = alpha + FullAlpha;
    }
}

void DrawShadowMesh(RigidModel* mesh, const Matrix4x4* toScreen, const Matrix4x4* toCamera, const Matrix4x4* world)
{
    SetDefaultMeshDMA(mesh, toScreen, toCamera, world);
}
}

namespace
{
// The drop shadow's outline (drop_shadow 2, THE LIST 52, #11: the shadow alone hardly shows on dark ground), asked for by the game's
// shadows while they're drawn (OutlineDropShadow) and drawn once they've been taken away from the screen: a yellow ring around
// the ground's point (a strip of flat triangles, its colour only), depth tested at a height above the ground so the ground doesn't
// hide it and nearer things (the character over it, a ledge in front) do. While it grows or shrinks (THE LIST 101: amount below 1)
// it's that much of its size (eased out) and blended over the screen by that much (its colour's alpha, ALPHA Cs - Cd by As, put
// back as ApplyShadows leaves it after); whole it's drawn as before, unblended
struct DropOutline
{
    bool due;
    f32 radius;
    f32 amount;
    Vector4 ground;
    Matrix4x4 chunkToWorld;
};
DropOutline g_DropOutline{};
constexpr u32 OutlineSegments = 32;
constexpr f32 OutlineWidth = 0.12f;
constexpr f32 OutlineDepthLift = 0.4f;
constexpr f32 TwoPi = 6.2831853f;
// The ring left out when a corner is behind the camera, too far off the screen for the GS's 16 bit coordinates, or when it's all
// off the screen; drawn with the scissor at the screen's edges (ApplyShadows leaves it at 1024 by 1024: a ring below the screen
// would draw into the GS memory past the frame, the depth and the textures there)
constexpr f32 OutlineNearest = 0.05f;
constexpr f32 OutlineMostOff = 1536.0f;
constexpr u64 GsXyzZShift = 32;
constexpr u32 OutlineSettingWrites = 14;
const u64 OutlineColour = std::bit_cast<u64>(GS_RGBAQ{.r = 0xFF, .g = 0xD8, .b = 0x20, .a = 0x80, .q = 1.0f});
const u64 OutlineStrip = std::bit_cast<u64>(GS_PRIM{.prim_type = GS_PRIM_TRI_STRIP, .ctxt = 1});
const u64 FadingStrip = std::bit_cast<u64>(GS_PRIM{.prim_type = GS_PRIM_TRI_STRIP, .abe = 1, .ctxt = 1});
const u64 SourceOverFrame = std::bit_cast<u64>(GS_ALPHA{.b = 1, .d = 1});
// (ApplyShadows' QuarterTakenAway, the second context's ALPHA it leaves)
const u64 ShadowsLeftAlpha = std::bit_cast<u64>(GS_ALPHA{.a = 2, .c = 2, .d = 1, .alpha = 0x20});
constexpr u32 FadingWrites = 2;
const u64 NearerOrSame = std::bit_cast<u64>(GS_TEST{.ztest_enable = 1, .ztest_method = GS_ZBUFF_GEQUAL});
const u64 NoAlphaWritten = std::bit_cast<u64>(GS_FRAME{.draw_mask = 0xFF000000});

struct OutlineBounds
{
    f32 left = OutlineMostOff;
    f32 top = OutlineMostOff;
    f32 right = -OutlineMostOff;
    f32 bottom = -OutlineMostOff;
};

// A point of the ring in the chunk: its XYZ2 on the screen (the screen's pixels' bounds kept), the depth the lifted point's
bool OutlineVertex(const Vector4& local, u64* xyz, OutlineBounds* bounds)
{
    Vector4 world;
    Vector4 screen;
    VuTransformPoint(&g_DropOutline.chunkToWorld, &local, &world);
    VuTransformPoint(&g_RenderView->toScreen, &world, &screen);
    Vector4 lifted = local;
    lifted.y = lifted.y + OutlineDepthLift;
    Vector4 liftedWorld;
    Vector4 liftedScreen;
    VuTransformPoint(&g_DropOutline.chunkToWorld, &lifted, &liftedWorld);
    VuTransformPoint(&g_RenderView->toScreen, &liftedWorld, &liftedScreen);
    if (!(screen.w > OutlineNearest) || !(liftedScreen.w > OutlineNearest))
    {
        return false;
    }

    f32 x = screen.x / screen.w;
    f32 y = screen.y / screen.w;
    f32 z = liftedScreen.z / liftedScreen.w;
    f32 pixelX = x / GsSubpixels - static_cast<f32>(GsScreenMiddle - static_cast<s32>(g_DisplayWidth >> 1));
    f32 pixelY = y / GsSubpixels - static_cast<f32>(GsScreenMiddle - static_cast<s32>(g_DisplayHeight >> 1));
    if (!(std::fabs(pixelX) < OutlineMostOff) || !(std::fabs(pixelY) < OutlineMostOff))
    {
        return false;
    }

    bounds->left = pixelX < bounds->left ? pixelX : bounds->left;
    bounds->right = pixelX > bounds->right ? pixelX : bounds->right;
    bounds->top = pixelY < bounds->top ? pixelY : bounds->top;
    bounds->bottom = pixelY > bounds->bottom ? pixelY : bounds->bottom;
    z = z < 0.0f ? 0.0f : (z > 2.0f * GsDepthRange ? 2.0f * GsDepthRange : z);
    *xyz = static_cast<u64>(static_cast<u32>(static_cast<s32>(x)) & 0xFFFF) |
           static_cast<u64>(static_cast<u32>(static_cast<s32>(y)) & 0xFFFF) << GsXyzYShift |
           static_cast<u64>(static_cast<u32>(z)) << GsXyzZShift;
    return true;
}

// Drawn in the second context as ApplyShadows left it (the frame, the depth not written), its offset the scene's, the scissor the
// screen's, then what it changed put back as ApplyShadows left it (the whole scissor, its sprite and white among them)
void DrawDropOutline()
{
    constexpr u32 Vertices = 2 * (OutlineSegments + 1);
    u64 xyz[Vertices];
    OutlineBounds bounds;
    const f32 amount = g_DropOutline.amount;
    const bool fading = amount < 1.0f;
    const f32 grown = fading ? amount * (2.0f - amount) : 1.0f;
    if (!(amount > 0.0f))
    {
        return;
    }

    f32 inner = g_DropOutline.radius * grown;
    f32 outer = inner + OutlineWidth * grown;
    for (u32 step = 0; step <= OutlineSegments; step++)
    {
        f32 angle = static_cast<f32>(step) * (TwoPi / static_cast<f32>(OutlineSegments));
        f32 cosine = std::cos(angle);
        f32 sine = std::sin(angle);
        for (u32 side = 0; side < 2; side++)
        {
            f32 radius = side == 0 ? outer : inner;
            Vector4 local = g_DropOutline.ground;
            local.x = local.x + cosine * radius;
            local.z = local.z + sine * radius;
            if (!OutlineVertex(local, &xyz[step * 2 + side], &bounds))
            {
                return;
            }
        }
    }

    u32 width = g_DisplayWidth;
    u32 height = g_DisplayHeight;
    if (bounds.right < 0.0f || bounds.bottom < 0.0f || bounds.left >= static_cast<f32>(width) ||
        bounds.top >= static_cast<f32>(height))
    {
        return;
    }

    RenderBucket& bucket = g_FrameBuckets.buckets[BucketShadows];
    u32 pixels = width * height;
    u32 pairCount = OutlineSettingWrites + Vertices + (fading ? FadingWrites : 0);
    u32* tag = BeginPacket(bucket);
    tag[0] = (pairCount + 2) | CountTag;
    tag[1] = 0;
    tag[2] = VifFlushA;
    tag[3] = pairCount | VifDirect | 1;
    GsWriter pairs{reinterpret_cast<GsWrite*>(tag + 4)};
    pairs.Write(AddressDataTag(pairCount), GifAddressData);
    u64 frame = Zeroed(pixels >> FrameBufferPagesShift) | static_cast<u64>(width >> GsWidthShift) << GsFrameWidthShift;
    u64 offset = Zeroed((GsScreenMiddle - (width >> 1)) << GsSubpixelShift) |
                 static_cast<u64>((GsScreenMiddle - (height >> 1)) << GsSubpixelShift) << GsOffsetYShift;
    u64 screen = static_cast<u64>(width - 1) << GsScissorRightShift | static_cast<u64>(height - 1) << GsScissorBottomShift;
    pairs.Write(AttributesFromPrim, GsPrmodeCont);
    pairs.Write(offset, SecondContext(GsXyOffset));
    pairs.Write(screen, SecondContext(GsScissor));
    pairs.Write(frame | NoAlphaWritten, SecondContext(GsFrame));
    pairs.Write(NearerOrSame, SecondContext(GsTest));
    if (fading)
    {
        GS_RGBAQ colour = std::bit_cast<GS_RGBAQ>(OutlineColour);
        colour.a = static_cast<u8>(static_cast<f32>(colour.a) * amount + 0.5f);
        pairs.Write(SourceOverFrame, SecondContext(GsAlpha));
        pairs.Write(FadingStrip, GsPrim);
        pairs.Write(std::bit_cast<u64>(colour), GsRgbaq);
    }
    else
    {
        pairs.Write(OutlineStrip, GsPrim);
        pairs.Write(OutlineColour, GsRgbaq);
    }

    for (u64 vertex : xyz)
    {
        pairs.Write(vertex, GsXyz2);
    }

    if (fading)
    {
        pairs.Write(ShadowsLeftAlpha, SecondContext(GsAlpha));
    }

    pairs.Write(DepthAlways, SecondContext(GsTest));
    pairs.Write(frame, SecondContext(GsFrame));
    pairs.Write(WholeScissor, SecondContext(GsScissor));
    pairs.Write(0, SecondContext(GsXyOffset));
    pairs.Write(BlendedSprite, GsPrim);
    pairs.Write(White, GsRgbaq);
    pairs.Write(AttributesFromPrmode, GsPrmodeCont);
    auto* flush = reinterpret_cast<u32*>(pairs.at);
    flush[0] = VifFlushA;
    flush[1] = 0;
    flush[2] = 0;
    flush[3] = 0;
    EndPacket(bucket, reinterpret_cast<u8*>(flush + 4));
}
}

namespace Platform::Graphics
{
void BeginShadows()
{
    g_DropOutline.due = false;
    StartShadows(nullptr);
}

void EndShadows()
{
    ApplyShadows(nullptr);
    if (g_DropOutline.due)
    {
        g_DropOutline.due = false;
        DrawDropOutline();
    }
}

void OutlineDropShadow(const Vector4* ground, f32 radius, f32 amount, const Matrix4x4* chunkToWorld)
{
    g_DropOutline.due = true;
    g_DropOutline.radius = radius;
    g_DropOutline.amount = amount;
    g_DropOutline.ground = *ground;
    g_DropOutline.chunkToWorld = *chunkToWorld;
}
}
