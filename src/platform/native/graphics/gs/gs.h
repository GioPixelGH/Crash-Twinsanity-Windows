#pragma once

// The PS2's Graphics Synthesizer, emulated: its 4 MB of local memory in the GS's own swizzled layouts, its registers (both
// drawing contexts), the primitives its vertex kicks draw (rasterised in software by the GS's rules: 12.4 fixed point
// coordinates sampled at whole pixels with the top-left fill rule, the texture function, fog, the alpha, destination alpha and
// depth tests, the (A - B) * C >> 7 + D blend, dithering, colour clamping and the frame and depth masks), the transfers into
// and inside local memory, the colour lookup table's loads, and the PCRTC's read-out of the shown buffer through its two
// circuits. The renderer reaches it through the GIF (gif.h), the way the PS2's renderer reaches the real one.
//
// Nothing here is the game's: it is the hardware the game's packets are written for. native/GRAPHICS.md lists where it differs
// from the hardware.

#include "common.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Gs
{
// Pixel storage modes
enum Psm : u32
{
    PSMCT32 = 0x00,
    PSMCT24 = 0x01,
    PSMCT16 = 0x02,
    PSMCT16S = 0x0A,
    PSMT8 = 0x13,
    PSMT4 = 0x14,
    PSMT8H = 0x1B,
    PSMT4HL = 0x24,
    PSMT4HH = 0x2C,
    PSMZ32 = 0x30,
    PSMZ24 = 0x31,
    PSMZ16 = 0x32,
    PSMZ16S = 0x3A,
};

// The general purpose registers by address (the A+D writes' and the GIF tags' register numbers 0-0xE are the first ones)
enum Register : u32
{
    PRIM = 0x00,
    RGBAQ = 0x01,
    ST = 0x02,
    UV = 0x03,
    XYZF2 = 0x04,
    XYZ2 = 0x05,
    TEX0_1 = 0x06,
    TEX0_2 = 0x07,
    CLAMP_1 = 0x08,
    CLAMP_2 = 0x09,
    FOG = 0x0A,
    XYZF3 = 0x0C,
    XYZ3 = 0x0D,
    TEX1_1 = 0x14,
    TEX1_2 = 0x15,
    TEX2_1 = 0x16,
    TEX2_2 = 0x17,
    XYOFFSET_1 = 0x18,
    XYOFFSET_2 = 0x19,
    PRMODECONT = 0x1A,
    PRMODE = 0x1B,
    TEXCLUT = 0x1C,
    SCANMSK = 0x22,
    MIPTBP1_1 = 0x34,
    MIPTBP1_2 = 0x35,
    MIPTBP2_1 = 0x36,
    MIPTBP2_2 = 0x37,
    TEXA = 0x3B,
    FOGCOL = 0x3D,
    TEXFLUSH = 0x3F,
    SCISSOR_1 = 0x40,
    SCISSOR_2 = 0x41,
    ALPHA_1 = 0x42,
    ALPHA_2 = 0x43,
    DIMX = 0x44,
    DTHE = 0x45,
    COLCLAMP = 0x46,
    TEST_1 = 0x47,
    TEST_2 = 0x48,
    PABE = 0x49,
    FBA_1 = 0x4A,
    FBA_2 = 0x4B,
    FRAME_1 = 0x4C,
    FRAME_2 = 0x4D,
    ZBUF_1 = 0x4E,
    ZBUF_2 = 0x4F,
    BITBLTBUF = 0x50,
    TRXPOS = 0x51,
    TRXREG = 0x52,
    TRXDIR = 0x53,
    HWREG = 0x54,
    SIGNAL = 0x60,
    FINISH = 0x61,
    LABEL = 0x62,
};

union RegPrim
{
    u64 value;
    struct
    {
        u64 prim : 3;
        u64 iip : 1;
        u64 tme : 1;
        u64 fge : 1;
        u64 abe : 1;
        u64 aa1 : 1;
        u64 fst : 1;
        u64 ctxt : 1;
        u64 fix : 1;
        u64 unused11 : 53;
    };
};

union RegRgbaq
{
    u64 value;
    struct
    {
        u64 r : 8;
        u64 g : 8;
        u64 b : 8;
        u64 a : 8;
        u64 q : 32;
    };
};

union RegTex0
{
    u64 value;
    struct
    {
        u64 tbp0 : 14;
        u64 tbw : 6;
        u64 psm : 6;
        u64 tw : 4;
        u64 th : 4;
        u64 tcc : 1;
        u64 tfx : 2;
        u64 cbp : 14;
        u64 cpsm : 4;
        u64 csm : 1;
        u64 csa : 5;
        u64 cld : 3;
    };
};

union RegClamp
{
    u64 value;
    struct
    {
        u64 wms : 2;
        u64 wmt : 2;
        u64 minu : 10;
        u64 maxu : 10;
        u64 minv : 10;
        u64 maxv : 10;
        u64 unused44 : 20;
    };
};

union RegTex1
{
    u64 value;
    struct
    {
        u64 lcm : 1;
        u64 unused1 : 1;
        u64 mxl : 3;
        u64 mmag : 1;
        u64 mmin : 3;
        u64 mtba : 1;
        u64 unused10 : 9;
        u64 l : 2;
        u64 unused21 : 11;
        s64 k : 12;
        u64 unused44 : 20;
    };
};

union RegMiptbp
{
    u64 value;
    struct
    {
        u64 tbp1 : 14;
        u64 tbw1 : 6;
        u64 tbp2 : 14;
        u64 tbw2 : 6;
        u64 tbp3 : 14;
        u64 tbw3 : 6;
        u64 unused60 : 4;
    };
};

union RegXyoffset
{
    u64 value;
    struct
    {
        u64 ofx : 16;
        u64 unused16 : 16;
        u64 ofy : 16;
        u64 unused48 : 16;
    };
};

union RegTexclut
{
    u64 value;
    struct
    {
        u64 cbw : 6;
        u64 cou : 6;
        u64 cov : 10;
        u64 unused22 : 42;
    };
};

union RegTexa
{
    u64 value;
    struct
    {
        u64 ta0 : 8;
        u64 unused8 : 7;
        u64 aem : 1;
        u64 unused16 : 16;
        u64 ta1 : 8;
        u64 unused40 : 24;
    };
};

union RegScissor
{
    u64 value;
    struct
    {
        u64 scax0 : 11;
        u64 unused11 : 5;
        u64 scax1 : 11;
        u64 unused27 : 5;
        u64 scay0 : 11;
        u64 unused43 : 5;
        u64 scay1 : 11;
        u64 unused59 : 5;
    };
};

union RegAlpha
{
    u64 value;
    struct
    {
        u64 a : 2;
        u64 b : 2;
        u64 c : 2;
        u64 d : 2;
        u64 unused8 : 24;
        u64 fix : 8;
        u64 unused40 : 24;
    };
};

union RegTest
{
    u64 value;
    struct
    {
        u64 ate : 1;
        u64 atst : 3;
        u64 aref : 8;
        u64 afail : 2;
        u64 date : 1;
        u64 datm : 1;
        u64 zte : 1;
        u64 ztst : 2;
        u64 unused19 : 45;
    };
};

union RegFrame
{
    u64 value;
    struct
    {
        u64 fbp : 9;
        u64 unused9 : 7;
        u64 fbw : 6;
        u64 unused22 : 2;
        u64 psm : 6;
        u64 unused30 : 2;
        u64 fbmsk : 32;
    };
};

union RegZbuf
{
    u64 value;
    struct
    {
        u64 zbp : 9;
        u64 unused9 : 15;
        u64 psm : 4;
        u64 unused28 : 4;
        u64 zmsk : 1;
        u64 unused33 : 31;
    };
};

union RegBitbltbuf
{
    u64 value;
    struct
    {
        u64 sbp : 14;
        u64 unused14 : 2;
        u64 sbw : 6;
        u64 unused22 : 2;
        u64 spsm : 6;
        u64 unused30 : 2;
        u64 dbp : 14;
        u64 unused46 : 2;
        u64 dbw : 6;
        u64 unused54 : 2;
        u64 dpsm : 6;
        u64 unused62 : 2;
    };
};

union RegTrxpos
{
    u64 value;
    struct
    {
        u64 ssax : 11;
        u64 unused11 : 5;
        u64 ssay : 11;
        u64 unused27 : 5;
        u64 dsax : 11;
        u64 unused43 : 5;
        u64 dsay : 11;
        u64 dir : 2;
        u64 unused61 : 3;
    };
};

union RegTrxreg
{
    u64 value;
    struct
    {
        u64 rrw : 12;
        u64 unused12 : 20;
        u64 rrh : 12;
        u64 unused44 : 20;
    };
};

// The privileged registers the PCRTC reads
union RegPmode
{
    u64 value;
    struct
    {
        u64 en1 : 1;
        u64 en2 : 1;
        u64 crtmd : 3;
        u64 mmod : 1;
        u64 amod : 1;
        u64 slbg : 1;
        u64 alp : 8;
        u64 unused16 : 48;
    };
};

union RegDispfb
{
    u64 value;
    struct
    {
        u64 fbp : 9;
        u64 fbw : 6;
        u64 psm : 5;
        u64 unused20 : 12;
        u64 dbx : 11;
        u64 dby : 11;
        u64 unused54 : 10;
    };
};

union RegDisplay
{
    u64 value;
    struct
    {
        u64 dx : 12;
        u64 dy : 11;
        u64 magh : 4;
        u64 magv : 2;
        u64 unused29 : 3;
        u64 dw : 12;
        u64 dh : 11;
        u64 unused55 : 9;
    };
};

union RegSmode2
{
    u64 value;
    struct
    {
        u64 interlaced : 1;
        u64 ffmd : 1;
        u64 dpms : 2;
        u64 unused4 : 60;
    };
};

// A vertex as the GS keeps it at a kick: its place (12.4, before XYOFFSET), depth, colour, fog, and texture coordinates (STQ
// and UV both kept: the drawing's FST picks)
struct Vertex
{
    u32 x;
    u32 y;
    u32 z;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    u8 f;
    f32 s;
    f32 t;
    f32 q;
    u32 u;
    u32 v;
};

// One drawing context's registers
struct Context
{
    RegXyoffset xyoffset;
    RegTex0 tex0;
    RegTex1 tex1;
    RegClamp clamp;
    RegMiptbp miptbp1;
    RegMiptbp miptbp2;
    RegScissor scissor;
    RegAlpha alpha;
    RegTest test;
    u64 fba;
    RegFrame frame;
    RegZbuf zbuf;
};

constexpr u32 LocalMemoryBytes = 4 * 1024 * 1024;
constexpr u32 BlockCount = LocalMemoryBytes / 256;

}

#include "gsaddress.h"

namespace Gs
{

// The 8 KB page of local memory a pixel of a format is in
u32 PixelPage(u32 psm, u32 x, u32 y, u32 bp, u32 bw);

// The size of a pixel of a format in bits (0 for an unknown one)
u32 BitsPerPixel(u32 psm);

// The picture the PCRTC sends to the TV: RGBA bytes, a row after another
struct DisplayImage
{
    u32 width = 0;
    u32 height = 0;
    std::vector<u32> pixels;
};

class Backend;

class Gs
{
public:
    Gs();
    ~Gs();

    // A register written (an A+D write, a GIF tag's REGLIST or PACKED register): the vertex kicks draw
    void WriteRegister(u32 address, u64 value);
    // A PACKED quadword for one of the GIF tag's register descriptors (0-0xF; 0xE is A+D)
    void WritePacked(u32 descriptor, const u32* quadword);
    // A GIF tag of loops starts: the Q that PACKED RGBAQ writes take is 1 again until an ST brings one
    void StartTag() { packedQ_ = 1.0f; }
    // IMAGE data of a host to local memory transfer (whole quadwords)
    void WriteImage(const u8* data, u32 bytes);

    // The privileged registers (the PCRTC's: PMODE, SMODE2, DISPFB1/2, DISPLAY1/2, BGCOLOR)
    RegPmode pmode = {};
    RegSmode2 smode2 = {};
    RegDispfb dispfb[2] = {};
    RegDisplay display[2] = {};
    u64 bgcolor = 0;

    // The picture of the PCRTC's circuits merged, as the TV gets it: each enabled circuit's buffer read at its place, the first
    // blended over the second by PMODE's alpha
    void ReadDisplay(DisplayImage& image);

    // The drawing backend (null: the software rasteriser draws everything)
    void SetBackend(Backend* backend) { backend_ = backend; }
    Backend* backend() const { return backend_; }
    // Local memory written without telling the backend (the backend's own copies going back to local memory)
    void SetBackendQuiet(bool quiet) { backendQuiet_ = quiet; }
    // For the backend: the draw's textures decoded (vertexes null: the whole levels), and a glyph image's sprite made the image's
    struct DrawState;
    void DecodeForBackend(DrawState& state, const Vertex* vertexes, u32 count);
    bool GlyphForBackend(DrawState& state, Vertex* vertexes, u32 count) const;
    u64 dimx() const { return dimx_; }
    RegTexa texa() const { return texa_; }
    // The local memory's pages of a host to local transfer, a CLUT load and the display, for the backend's syncs
    void PrepareCpuRect(bool write, u32 psm, u32 bp, u32 bw, s32 left, s32 top, s32 right, s32 bottom, const char* why = "");

    // The state (registers, local memory, the CLUT, a transfer in progress) for a recording; a loaded state counts as every
    // page written (decoded textures are checked against it again) and drops the shadows
    template <typename Io>
    void Serialize(Io& io);

    // The local memory
    u8* memory() { return vm_; }
    const u8* memory() const { return vm_; }

    // Pixels of local memory read and written in a format (32 bit values; the colour formats' bits as they're stored)
    u32 ReadPixel(u32 psm, u32 x, u32 y, u32 bp, u32 bw) const;
    void WritePixel(u32 psm, u32 x, u32 y, u32 bp, u32 bw, u32 value);

    // Which drawing or transfer last wrote each 8 KB page of local memory (the texture cache's check)
    void MarkWritten(u32 page)
    {
        // Read first: the workers drawing a primitive's rows would fight over the cache line otherwise
        u32& written = pageWritten_[page & (LocalMemoryBytes / 8192 - 1)];
        if (written != generation_)
        {
            written = generation_;
            newestWrite_ = generation_;
            if (backend_ != nullptr && !backendQuiet_)
            {
                BackendWrote(page);
            }
        }
    }
    u32 PageWritten(u32 page) const { return pageWritten_[page & (LocalMemoryBytes / 8192 - 1)]; }
    u32 generation() const { return generation_; }
    void NextGeneration() { generation_++; }
    // Changes with every register write but the vertexes' and the transfers' (the draw state's: Gs::DrawPrimitive's cache)
    u64 stateGeneration() const { return stateGeneration_; }
    // The CLUT buffer's contents (they change at each load)
    u32 clutVersion() const { return clutVersion_; }
    const u16* clut() const { return clut_; }

    // Statistics of the drawing since the last reset (primitives, pixels tested)
    u64 primitives = 0;
    u64 pixels = 0;

    // What a primitive draws with (gsdraw.cpp)
    struct DrawState;

    // Drawing at a multiple of the PS2's resolution (1 to 4): every primitive is drawn as the PS2 draws it into local memory,
    // and drawn again with its places scaled into a buffer of scale times scale samples a pixel kept beside each frame and depth
    // buffer it draws into (a shadow). A texture read from a buffer that has a shadow of its format reads the shadow; the
    // PCRTC reads the shown buffer's shadow. A shadow takes local memory's pixels again (each pixel's samples the same) where
    // something other than its own drawing wrote local memory (transfers, drawing into the memory in another format)
    void SetScale(u32 scale);
    u32 scale() const { return scale_; }

    struct Shadow
    {
        u32 bp;
        u32 bw;
        u32 kind;
        u32 psm;
        u32 width;
        u32 height;
        u32 scale;
        std::vector<u32> samples;
        // The generation local memory's pages were taken at, and each page's pixels (y << 11 | x)
        std::vector<u32> synced;
        std::vector<std::vector<u32>> pagePixels;
        // Its samples decoded as a texture (TEXA applied) and when
        std::vector<u32> texels;
        u64 texelsTexa = ~0ull;
        u32 texelsVersion = 0;
        u32 version = 1;
    };

    // The shadow of a buffer (made when asked to and there is none), its samples made to match local memory
    Shadow* ShadowFor(u32 psm, u32 bp, u32 bw, bool make);
    void SyncShadow(Shadow& shadow);

private:

    void Kick(bool draw, bool fog);
    void ResetQueue();
    void DrawPrimitive(const Vertex* vertexes, u32 count);
    void DrawSoftware(DrawState& state, const Vertex* vertexes, u32 count);
    void DrawSprite(const DrawState& state, const Vertex& v0, const Vertex& v1);
    void DrawTriangle(const DrawState& state, const Vertex& v0, const Vertex& v1, const Vertex& v2);
    void DrawLine(const DrawState& state, const Vertex& v0, const Vertex& v1);
    void DrawPoint(const DrawState& state, const Vertex& v0);
    void SetUpState(DrawState& state, const Vertex* last);
    // The draw's texture levels decoded (or found decoded in the cache, unless their pages were written since)
    void DecodeTextures(DrawState& state, const Vertex* vertexes, u32 count);
    // A glyph image's sprite: the state's texture made the image and the vertexes' UVs its own (false: none)
    bool GlyphImage(DrawState& state, Vertex* vertexes, u32 count) const;

public:
    // Images drawn in place of a texture's rectangle (the UI's glyph images, glyphs.h): a sprite with UV coordinates (sixteenths of
    // a texel) of exactly the rectangle's corners, u0 v0 the first, draws the image instead, its top row at v0's end. texels: RGBA,
    // alpha 0x80 = 1.0. An empty image takes the rectangle's back
    struct GlyphOverride
    {
        s32 u0;
        s32 v0;
        s32 u1;
        s32 v1;
        u32 width;
        u32 height;
        std::vector<u32> texels;
        u32 version = 0;
    };
    void SetGlyphOverride(GlyphOverride glyph);

    // Where the packets come from, for TWIN_GS_TRACE (VU1's program's address for path 1, ~0 otherwise)
    u32 traceSource = ~0u;
    // Counts for TWIN_FRAME_STATS: primitives drawn and their bounding boxes' pixels
    u64 primitivesDrawn = 0;
    u64 boxPixels = 0;

private:
    void LoadClut(const RegTex0& tex0);
    void TextureFlush();
    void TransferLocalToLocal();

    u8* vm_;

    // The registers
    RegPrim prim_ = {};
    RegPrim prmode_ = {};
    u32 prmodecont_ = 1;
    // Changed whenever a register a primitive's DrawState is made from is written (or the state restored): DrawPrimitive makes
    // the state again only then (gsdraw.cpp)
    u64 stateGeneration_ = 1;
    // Whether the backend changed the draw's state (DecodeForBackend, GlyphForBackend): DrawPrimitive's cache made again
    mutable bool stateTouched_ = false;
    RegRgbaq rgbaq_ = {};
    f32 s_ = 0.0f;
    f32 t_ = 0.0f;
    f32 packedQ_ = 1.0f;
    u32 u_ = 0;
    u32 v_ = 0;
    u8 fog_ = 0;
    Context context_[2] = {};
    RegTexclut texclut_ = {};
    u64 scanmsk_ = 0;
    RegTexa texa_ = {};
    u32 fogcol_ = 0;
    u64 dimx_ = 0;
    u32 dthe_ = 0;
    u32 colclamp_ = 0;
    u32 pabe_ = 0;
    RegBitbltbuf bitbltbuf_ = {};
    RegTrxpos trxpos_ = {};
    RegTrxreg trxreg_ = {};

    // The vertex queue
    Vertex queue_[3] = {};
    u32 queued_ = 0;
    u32 fanFirst_ = 0;

    // The colour lookup table's buffer (16 bit halves: a 32 bit entry's low half at i, its high one at i + 256) and its
    // load condition's saved bases
    u16 clut_[512] = {};
    u32 clutVersion_ = 1;
    u32 scale_ = 1;
    std::vector<Shadow*> shadows_;
    void DrawScaled(const Vertex* vertexes, u32 count, u32 drawGeneration);
    u32 pageWritten_[LocalMemoryBytes / 8192] = {};
    u32 generation_ = 1;
    // The latest generation any page was written in
    u32 newestWrite_ = 0;
    u32 cbp_[2] = {};
    // The last CLUT load (its source's fields and the generation it was read at): the same one again, its pages not written
    // since, leaves the buffer as it is
    u64 lastClutSource_[2] = {~0ull, ~0ull};
    u32 lastClutGeneration_ = 0;
    // The pages the last CLUT load read
    std::bitset<LocalMemoryBytes / 8192> lastClutPages_;

    // A host to local transfer in progress: where its next pixel goes, how many bits are left over from the last data
    bool transferring_ = false;
    u32 transferX_ = 0;
    u32 transferY_ = 0;
    std::vector<u8> transferRest_;
    std::vector<GlyphOverride> glyphs_;
    Backend* backend_ = nullptr;
    bool backendQuiet_ = false;
    void BackendWrote(u32 page);
};
}
