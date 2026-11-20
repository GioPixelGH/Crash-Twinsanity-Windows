#pragma once

// A drawing backend for the GS other than its software rasteriser (the hardware renderer, hw/): the GS front end (registers,
// kicks, transfers, the CLUT, the PCRTC) stays the same and hands it each primitive. Local memory stays the PS2's: the backend
// keeps buffers of its own (render targets) and keeps them and local memory in step: before the CPU side reads or writes local
// memory (transfers, CLUT loads, texture decoding, the display read, a primitive it leaves to the software rasteriser) it's told
// the pages, and every page the CPU side writes is reported (CpuWrote, from any thread)

#include "drawstate.h"

#include <atomic>
#include <bitset>

namespace Gs
{
constexpr u32 PageCount = LocalMemoryBytes / 8192;
using PageSet = std::bitset<PageCount>;

// The pages a rectangle of a buffer (inclusive corners, in pixels of the format) is in
void PagesOfRect(u32 psm, u32 bp, u32 bw, s32 left, s32 top, s32 right, s32 bottom, PageSet& pages);

// The pages a rectangle covers whole (every byte of them in it)
void FullPagesOfRect(u32 psm, u32 bp, u32 bw, s32 left, s32 top, s32 right, s32 bottom, PageSet& pages);

// The pages a primitive's texture decode reads (the levels' regions its texture coordinates reach, or whole levels), and the
// region of the first level its coordinates reach (false: any of it)
void TexturePagesForDraw(const Gs::DrawState& state, const Vertex* vertexes, u32 count, PageSet& pages);
bool TextureRegionForDraw(const Gs::DrawState& state, const Vertex* vertexes, u32 count, s32* left, s32* top, s32* right,
                          s32* bottom);

// A texel of a level decoded as the software GS decodes it (RGBA, alpha 0x80 = 1.0; TEXA and the CLUT applied), and the CLUT's
// colour for an index (the texture's CPSM and CSA, TEXA for 16 bit entries)
u32 BackendTexel(const Gs& gs, const TextureLevel& level, u32 x, u32 y, const RegTex0& tex0);
u32 BackendClutEntry(const Gs& gs, u32 index, const RegTex0& tex0);

class Backend
{
public:
    virtual ~Backend() = default;

    // A primitive: true when the backend drew it; false leaves it to the software rasteriser, local memory made current for it
    virtual bool Draw(Gs::DrawState& state, const Vertex* vertexes, u32 count) = 0;
    // A run of primitives with the same state (Gs::DrawPrimitives: count vertexes each, one after another), each drawn as Draw
    // draws it and counted in the GS's primitivesDrawn before it is, as Gs::DrawPrimitive counts it. The primitives drawn in a
    // row from the first: it stops after one whose drawing changed the state (Gs::stateTouched: the state is made again for the
    // rest), or at one it leaves to the software rasteriser (refused set, local memory made current for it, as Draw's false)
    virtual u32 DrawRun(Gs::DrawState& state, const Vertex* vertexes, u32 count, u32 primitives, bool& refused) = 0;
    // A triangle strip's triangles from first to before end, with the same state (Gs::DrawStripPrimitives: triangle t is vertexes
    // t, t + 1 and t + 2, a primitive when kicks[t + 2] is set, as the GS's queue makes them), each drawn as DrawRun draws a run's
    // (counted in the GS's primitivesDrawn before it's drawn; drawn: how many the backend drew). Returns the triangle it stopped
    // before: end, the one after a primitive whose drawing changed the state, or one it leaves to the software rasteriser
    // (refused set, local memory made current for it)
    virtual u32 DrawStrip(Gs::DrawState& state, const Vertex* vertexes, const u8* kicks, u32 first, u32 end, u32& drawn,
                          bool& refused) = 0;
    // Local memory's pages about to be read, or written, by the CPU side: made current
    virtual void BeforeCpuRead(const PageSet& pages) = 0;
    virtual void BeforeCpuWrite(const PageSet& pages) = 0;
    // Why the next CPU access happens (statistics)
    const char* cpuReason = "";
    // Everything queued drawn
    virtual void Flush() = 0;
    // Whether any of the pages is newer on the GPU than in local memory
    virtual bool PagesGpuNewer(const PageSet& pages) = 0;
    // Debugging: a buffer's pixel as the backend has it (false: local memory's is current)
    virtual bool DebugPixel(u32 psm, u32 bp, u32 bw, u32 x, u32 y, u32* value) = 0;

    // A page the CPU side wrote (any thread): the backend's copies of it are out of date
    void CpuWrote(u32 page)
    {
        written_[page & (PageCount - 1)].store(1, std::memory_order_relaxed);
        anyWritten_.store(true, std::memory_order_release);
    }

protected:
    std::atomic<u8> written_[PageCount] = {};
    std::atomic<bool> anyWritten_{false};
};
}
