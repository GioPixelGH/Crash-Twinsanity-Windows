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
#include <cstring>
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

// An entry of a fast renderer's prepared batch (Gs::PlayWrites): a register written as an A+D write writes it (the quadword's
// layout: the value, then the register's address), or with the address PreparedPackedQ the Q a PACKED ST leaves for the next
// PACKED RGBAQ (the value's low word, a float's bits)
struct PreparedWrite
{
    u64 value;
    u32 address;
    u32 unused;
};
static_assert(sizeof(PreparedWrite) == 16);
constexpr u32 PreparedPackedQ = 0x100;

// A strip in a prepared batch (THE LIST 75 step 2): a GIF packet whose tag's PRE writes PRIM and whose loops are PACKED ST,
// RGBAQ and XYZ2 (or XYZF2) a vertex, as the scenery's VU1 programs write theirs, made by the program's prepared function
// with the packet. A PreparedWrite entry with the address PreparedStrip, its value the PRIM (bit 32 set: the vertexes are
// XYZF2's) and its unused word the vertex count, then the vertexes, a StripVertex (two quadwords) each. Played as the packet's
// register writes would be (Gs::PlayWrites), its triangles drawn at once
constexpr u32 PreparedStrip = 0x102;
struct StripVertex
{
    // XYZ's: x and y (the PACKED quadword's low 16 bits), z (XYZ2's 32 bits, XYZF2's 24)
    u32 x;
    u32 y;
    u32 z;
    u8 r;
    u8 g;
    u8 b;
    u8 a;
    // ST's S, T and Q (floats' bits): RGBAQ's Q too
    u32 s;
    u32 t;
    u32 q;
    // XYZF2's fog; whether the vertex draws (XYZ2 or XYZF2: 1) or only goes in the queue (the ADC bit's XYZ3 or XYZF3: 0)
    u8 fog;
    u8 kick;
    u8 unused[2];
};
static_assert(sizeof(StripVertex) == 32);
// A prepared batch's entry's quadwords (a strip's with its vertexes)
inline u32 PreparedEntryQuadwords(const PreparedWrite& entry)
{
    return entry.address == PreparedStrip ? 1 + entry.unused * 2 : 1;
}
// The register writes a prepared batch stands for, appended to writes (PreparedWrite entries: its strips as the writes Gif's
// ConvertKick makes of their packets, the PRIM, then ST, RGBAQ and XYZ a vertex, then the PACKED ST's Q): a GS dump's packet
// of a batch, and the fast renderer's verify
void ExpandPrepared(const u8* entries, u32 count, std::vector<u8>& writes);

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
    // A PACKED tag's quadwords as far as there are (at most available): each written as WritePacked writes it, from the tag's
    // descriptors (registers, registerCount of them) at reg, loops counting down. The quadwords taken. (Here rather than a
    // call a quadword from the GIF: the writes inlined into one loop)
    u32 WritePackedRun(u64 registers, u32 registerCount, u32& reg, u32& loops, const u8* data, u32 available);
    // A GIF tag of loops starts: the Q that PACKED RGBAQ writes take is 1 again until an ST brings one
    void StartTag() { packedQ_ = 1.0f; }
    // IMAGE data of a host to local memory transfer (whole quadwords)
    void WriteImage(const u8* data, u32 bytes);

    // The fast renderer's GS side (THE LIST 74 step 3, graphics/fastrenderer.h): between StartRun and EndRun (a kick of a kind
    // the fast renderer takes, or a prepared batch) the primitives the vertexes make are collected into a run instead of drawn
    // one by one, and the run is drawn at once (DrawPrimitives: the draw's state made once, Backend::DrawRun) before any register
    // other than a vertex's is written, before image data, when the run is full, and at EndRun. The same primitives in the same
    // order with the same state: only the calls between them are fewer
    void StartRun();
    void EndRun();
    // The fast renderer's prepared batch (fastrenderer.h's PreparedWrite entries, in order): each a register write as an A+D
    // write makes it (vertexes included), or the PACKED ST's Q (packedQ_) the next PACKED RGBAQ takes, or a strip (PreparedStrip:
    // PlayStrip). count: quadwords
    void PlayWrites(const u8* entries, u32 count);
    // A raw kick's strip while a run is being collected (THE LIST 75): a PACKED tag with PRE whose loops are ST, RGBAQ and XYZ2
    // (registerList 0x512) or XYZF2 (0x412) a vertex, count vertexes, all of them in data (count * 3 quadwords). Played as the
    // tag's PRIM and quadwords would be (WritePacked's), a triangle strip's triangles drawn at once (DrawStripPrimitives). False:
    // not a triangle strip, no run, or too long, and nothing written (the tag goes the usual way)
    bool TakePackedStrip(u32 prim, u32 registerList, const u8* data, u32 count);
    // Whether the backend changed the draw's state while drawing the last primitive (DrawRun stops after such a one)
    bool stateTouched() const { return stateTouched_; }

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
    // For the backend: the draw's textures decoded (vertexes null: the whole levels), and a glyph image's sprite made the image's.
    // (DrawState: what a primitive draws with, gsdraw.cpp)
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
    // A primitive drawn now, or added to the run (StartRun)
    void Emit(const Vertex* vertexes, u32 count)
    {
        if (!collecting_)
        {
            DrawPrimitive(vertexes, count);
            return;
        }

        if (runPrimitives_ == RunCapacity || (runPrimitives_ != 0 && runCount_ != count))
        {
            FlushRun();
        }

        std::memcpy(&run_[runPrimitives_ * count], vertexes, sizeof(Vertex) * count);
        runCount_ = count;
        runPrimitives_++;
    }
    void FlushRun()
    {
        if (runPrimitives_ != 0)
        {
            const u32 primitives = runPrimitives_;
            runPrimitives_ = 0;
            DrawPrimitives(run_, runCount_, primitives);
        }
    }
    void DrawPrimitives(const Vertex* vertexes, u32 count, u32 primitives);
    // A prepared strip (PreparedStrip: its PRIM entry's value, the vertexes' StripVertex entries, count of them) played: the
    // registers, the vertex queue and the drawing as the packet's writes leave them. A triangle strip's triangles made from the
    // vertexes in place and drawn at once (DrawStripPrimitives) while a run is being collected; else (another primitive's type,
    // the debugging switches) the writes made one by one
    void PlayStrip(u64 prim, const u8* vertexes, u32 count);
    // A strip's end (PlayStrip's and TakePackedStrip's, the vertexes in strip_ and stripKicks_): the registers as the last
    // vertex's writes leave them, the queue as the strip's kicks leave it, and its triangles drawn
    void FinishStrip(u32 count);
    // A triangle strip's triangles (triangle t: vertexes t to t + 2, drawn when kicks[t + 2] is set), with the state the
    // registers make now: DrawPrimitives' work, the backend's DrawStrip
    void DrawStripPrimitives(const Vertex* vertexes, const u8* kicks, u32 count);
    void DrawSoftware(DrawState& state, const Vertex* vertexes, u32 count);
    [[gnu::noinline]] void DebugBeforeDraw(const DrawState& state, const Vertex* vertexes, u32 count, s32 watch[2]);
    [[gnu::noinline]] void ReportWatch(const DrawState& state, const Vertex* vertexes, u32 count, const s32 watch[2]);
    [[gnu::noinline]] void DrawOwnState(const DrawState& state, const Vertex* vertexes, u32 count, bool touched);
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

    // The state as PCSX2's GS dumps begin with it (its GSState::Freeze, version 9: the registers, local memory made current from
    // the backend, the GIF paths idle) and its privileged registers' 8 KB (capture.cpp's $TWIN_GS_DUMP)
    void WritePcsx2State(std::vector<u8>& out);
    void WritePcsx2PrivilegedRegisters(std::vector<u8>& out) const;

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

    // The run being collected (StartRun): its primitives' vertexes one after another, count of them each
    static constexpr u32 RunCapacity = 512;
    bool collecting_ = false;
    u32 runCount_ = 0;
    u32 runPrimitives_ = 0;
    Vertex run_[RunCapacity * 3];
    // A prepared strip's vertexes as the queue makes them, and whether each draws (PlayStrip; longer strips are written one by one)
    static constexpr u32 StripCapacity = 1024;
    Vertex strip_[StripCapacity];
    u8 stripKicks_[StripCapacity];

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
