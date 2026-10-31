// The GS's registers, vertex queue, transfers, colour lookup table and PCRTC (the drawing itself is gsdraw.cpp)

#include "gs.h"
#include "backend.h"
#include "workers.h"

#include <atomic>
#include <functional>

#include "../state.h"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>

namespace Gs
{
namespace
{

f32 AsFloat(u32 bits)
{
    return std::bit_cast<f32>(bits);
}
}

Gs::Gs()
{
    // Each GS's generations its own (DrawPrimitive's cache keeps the GS it was made for, which a new one may reuse the place of)
    static std::atomic<u64> instances{0};
    stateGeneration_ = (instances.fetch_add(1) + 1) << 40;
    vm_ = static_cast<u8*>(std::calloc(1, LocalMemoryBytes));
    prmodecont_ = 1;
    rgbaq_.q = std::bit_cast<u32>(1.0f);
}

void Gs::BackendWrote(u32 page)
{
    backend_->CpuWrote(page);
}

void Gs::PrepareCpuRect(bool write, u32 psm, u32 bp, u32 bw, s32 left, s32 top, s32 right, s32 bottom, const char* why)
{
    if (backend_ == nullptr)
    {
        return;
    }

    PageSet pages;
    PagesOfRect(psm, bp, bw, left, top, right, bottom, pages);
    backend_->cpuReason = why;
    if (write)
    {
        // Pages the write covers whole needn't be current first (their GPU copies just go out of date as they're written)
        PageSet full;
        FullPagesOfRect(psm, bp, bw, left, top, right, bottom, full);
        pages &= ~full;
    }

    if (pages.none())
    {
        return;
    }

    if (write)
    {
        backend_->BeforeCpuWrite(pages);
    }
    else
    {
        backend_->BeforeCpuRead(pages);
    }
}

template <typename Io>
void Gs::Serialize(Io& io)
{
    stateGeneration_++;
    io.Bytes(vm_, LocalMemoryBytes);
    io.Field(pmode);
    io.Field(smode2);
    io.Field(dispfb);
    io.Field(display);
    io.Field(bgcolor);
    io.Field(prim_);
    io.Field(prmode_);
    io.Field(prmodecont_);
    io.Field(rgbaq_);
    io.Field(s_);
    io.Field(t_);
    io.Field(packedQ_);
    io.Field(u_);
    io.Field(v_);
    io.Field(fog_);
    io.Field(context_);
    io.Field(texclut_);
    io.Field(scanmsk_);
    io.Field(texa_);
    io.Field(fogcol_);
    io.Field(dimx_);
    io.Field(dthe_);
    io.Field(colclamp_);
    io.Field(pabe_);
    io.Field(bitbltbuf_);
    io.Field(trxpos_);
    io.Field(trxreg_);
    io.Field(queue_);
    io.Field(queued_);
    io.Field(fanFirst_);
    io.Field(clut_);
    io.Field(clutVersion_);
    io.Field(cbp_);
    io.Field(transferring_);
    io.Field(transferX_);
    io.Field(transferY_);
    io.Vector(transferRest_);
    if constexpr (Io::Loading)
    {
        NextGeneration();
        for (u32 page = 0; page < LocalMemoryBytes / 8192; page++)
        {
            MarkWritten(page);
        }

        NextGeneration();
        clutVersion_++;
        u32 scale = scale_;
        scale_ = 0;
        SetScale(scale);
    }
}

template void Gs::Serialize<NativeGraphics::Saver>(NativeGraphics::Saver&);
template void Gs::Serialize<NativeGraphics::Loader>(NativeGraphics::Loader&);

Gs::~Gs()
{
    std::free(vm_);
}

void Gs::ResetQueue()
{
    queued_ = 0;
}

void Gs::WriteRegister(u32 address, u64 value)
{
    // Every register but the vertexes' and the transfers' is part of a primitive's DrawState (SetUpState): its cache made stale
    switch (address)
    {
    case RGBAQ:
    case ST:
    case UV:
    case XYZF2:
    case XYZF3:
    case XYZ2:
    case XYZ3:
    case FOG:
    case BITBLTBUF:
    case TRXPOS:
    case TRXREG:
    case TRXDIR:
    case TEXFLUSH:
        break;
    default:
        stateGeneration_++;
        break;
    }

    switch (address)
    {
    case PRIM:
        prim_.value = value & 0x7FF;
        ResetQueue();
        break;
    case RGBAQ:
        rgbaq_.value = value;
        break;
    case ST:
        s_ = AsFloat(static_cast<u32>(value));
        t_ = AsFloat(static_cast<u32>(value >> 32));
        break;
    case UV:
        u_ = value & 0x3FFF;
        v_ = (value >> 16) & 0x3FFF;
        break;
    case XYZF2:
    case XYZF3:
    {
        // XYZF writes the fog value along with a 24 bit depth
        fog_ = static_cast<u8>(value >> 56);
        Vertex next = {};
        next.x = value & 0xFFFF;
        next.y = (value >> 16) & 0xFFFF;
        next.z = (value >> 32) & 0xFFFFFF;
        next.r = static_cast<u8>(rgbaq_.r);
        next.g = static_cast<u8>(rgbaq_.g);
        next.b = static_cast<u8>(rgbaq_.b);
        next.a = static_cast<u8>(rgbaq_.a);
        next.q = AsFloat(static_cast<u32>(rgbaq_.q));
        next.s = s_;
        next.t = t_;
        next.u = u_;
        next.v = v_;
        next.f = fog_;
        queue_[queued_ < 3 ? queued_ : 2] = next;
        Kick(address == XYZF2, true);
        break;
    }
    case XYZ2:
    case XYZ3:
    {
        Vertex next = {};
        next.x = value & 0xFFFF;
        next.y = (value >> 16) & 0xFFFF;
        next.z = static_cast<u32>(value >> 32);
        next.r = static_cast<u8>(rgbaq_.r);
        next.g = static_cast<u8>(rgbaq_.g);
        next.b = static_cast<u8>(rgbaq_.b);
        next.a = static_cast<u8>(rgbaq_.a);
        next.q = AsFloat(static_cast<u32>(rgbaq_.q));
        next.s = s_;
        next.t = t_;
        next.u = u_;
        next.v = v_;
        next.f = fog_;
        queue_[queued_ < 3 ? queued_ : 2] = next;
        Kick(address == XYZ2, false);
        break;
    }
    case TEX0_1:
    case TEX0_2:
    {
        Context& context = context_[address - TEX0_1];
        RegTex0 tex0;
        tex0.value = value;
        context.tex0 = tex0;
        LoadClut(tex0);
        break;
    }
    case TEX2_1:
    case TEX2_2:
    {
        // TEX2 sets TEX0's PSM and palette fields (CBP, CPSM, CSM, CSA, CLD) only
        Context& context = context_[address - TEX2_1];
        constexpr u64 Tex2Mask = 0xFFFFFFE003F00000ull;
        context.tex0.value = (context.tex0.value & ~Tex2Mask) | (value & Tex2Mask);
        LoadClut(context.tex0);
        break;
    }
    case CLAMP_1:
    case CLAMP_2:
        context_[address - CLAMP_1].clamp.value = value;
        break;
    case FOG:
        fog_ = static_cast<u8>(value >> 56);
        break;
    case TEX1_1:
    case TEX1_2:
        context_[address - TEX1_1].tex1.value = value;
        break;
    case XYOFFSET_1:
    case XYOFFSET_2:
        context_[address - XYOFFSET_1].xyoffset.value = value;
        break;
    case PRMODECONT:
        prmodecont_ = value & 1;
        break;
    case PRMODE:
        prmode_.value = value & 0x7F8;
        break;
    case TEXCLUT:
        texclut_.value = value;
        break;
    case SCANMSK:
        scanmsk_ = value & 3;
        break;
    case MIPTBP1_1:
    case MIPTBP1_2:
        context_[address - MIPTBP1_1].miptbp1.value = value;
        break;
    case MIPTBP2_1:
    case MIPTBP2_2:
        context_[address - MIPTBP2_1].miptbp2.value = value;
        break;
    case TEXA:
        texa_.value = value;
        break;
    case FOGCOL:
        fogcol_ = value & 0xFFFFFF;
        break;
    case TEXFLUSH:
        break;
    case SCISSOR_1:
    case SCISSOR_2:
        context_[address - SCISSOR_1].scissor.value = value;
        break;
    case ALPHA_1:
    case ALPHA_2:
        context_[address - ALPHA_1].alpha.value = value;
        break;
    case DIMX:
        dimx_ = value;
        break;
    case DTHE:
        dthe_ = value & 1;
        break;
    case COLCLAMP:
        colclamp_ = value & 1;
        break;
    case TEST_1:
    case TEST_2:
        context_[address - TEST_1].test.value = value;
        break;
    case PABE:
        pabe_ = value & 1;
        break;
    case FBA_1:
    case FBA_2:
        context_[address - FBA_1].fba = value & 1;
        break;
    case FRAME_1:
    case FRAME_2:
        context_[address - FRAME_1].frame.value = value;
        break;
    case ZBUF_1:
    case ZBUF_2:
        context_[address - ZBUF_1].zbuf.value = value;
        break;
    case BITBLTBUF:
        bitbltbuf_.value = value;
        break;
    case TRXPOS:
        trxpos_.value = value;
        break;
    case TRXREG:
        trxreg_.value = value;
        break;
    case TRXDIR:
        switch (value & 3)
        {
        case 0:
            // Host to local: the IMAGE data that follows fills the rectangle
            PrepareCpuRect(true, bitbltbuf_.dpsm, bitbltbuf_.dbp, bitbltbuf_.dbw, static_cast<s32>(trxpos_.dsax),
                           static_cast<s32>(trxpos_.dsay), static_cast<s32>(trxpos_.dsax + trxreg_.rrw) - 1,
                           static_cast<s32>(trxpos_.dsay + trxreg_.rrh) - 1, "host to local transfer");
            transferring_ = true;
            transferX_ = 0;
            transferY_ = 0;
            transferRest_.clear();
            break;
        case 2:
            TransferLocalToLocal();
            break;
        default:
            // Local to host reads nothing the game uses
            transferring_ = false;
            break;
        }
        break;
    default:
        break;
    }
}

void Gs::WritePacked(u32 descriptor, const u32* q)
{
    switch (descriptor)
    {
    case PRIM:
        WriteRegister(PRIM, q[0] & 0x7FF);
        break;
    case RGBAQ:
    {
        // The colour, and the Q the last ST brought
        RegRgbaq value = {};
        value.r = q[0] & 0xFF;
        value.g = q[1] & 0xFF;
        value.b = q[2] & 0xFF;
        value.a = q[3] & 0xFF;
        value.q = std::bit_cast<u32>(packedQ_);
        rgbaq_ = value;
        break;
    }
    case ST:
        s_ = AsFloat(q[0]);
        t_ = AsFloat(q[1]);
        packedQ_ = AsFloat(q[2]);
        break;
    case UV:
        u_ = q[0] & 0x3FFF;
        v_ = q[1] & 0x3FFF;
        break;
    case XYZF2:
    {
        bool noKick = (q[3] >> 15) & 1;
        u64 value = static_cast<u64>(q[0] & 0xFFFF) | static_cast<u64>(q[1] & 0xFFFF) << 16 |
                    static_cast<u64>((q[2] >> 4) & 0xFFFFFF) << 32 | static_cast<u64>((q[3] >> 4) & 0xFF) << 56;
        WriteRegister(noKick ? XYZF3 : XYZF2, value);
        break;
    }
    case XYZ2:
    {
        bool noKick = (q[3] >> 15) & 1;
        u64 value = static_cast<u64>(q[0] & 0xFFFF) | static_cast<u64>(q[1] & 0xFFFF) << 16 | static_cast<u64>(q[2]) << 32;
        WriteRegister(noKick ? XYZ3 : XYZ2, value);
        break;
    }
    case FOG:
        fog_ = static_cast<u8>((q[3] >> 4) & 0xFF);
        break;
    case 0xE:
    {
        u64 value = static_cast<u64>(q[0]) | static_cast<u64>(q[1]) << 32;
        WriteRegister(q[2] & 0xFF, value);
        break;
    }
    case 0xF:
        break;
    default:
        // The other registers take the quadword's low doubleword
        WriteRegister(descriptor, static_cast<u64>(q[0]) | static_cast<u64>(q[1]) << 32);
        break;
    }
}

u32 Gs::WritePackedRun(u64 registers, u32 registerCount, u32& reg, u32& loops, const u8* data, u32 available)
{
    u32 taken = 0;
    while (taken < available && loops > 0)
    {
        u32 q[4];
        std::memcpy(q, data + taken * 16, 16);
        WritePacked((registers >> (reg * 4)) & 0xF, q);
        taken++;
        if (++reg == registerCount)
        {
            reg = 0;
            loops--;
        }
    }

    return taken;
}

// A vertex went in the queue at queued_ (or its last place): draw when the primitive has its vertexes
void Gs::Kick(bool draw, bool)
{
    u32 type = prim_.prim;
    queued_++;
    Vertex drawn[3];
    switch (type)
    {
    case PrimPoint:
        if (draw)
        {
            DrawPrimitive(queue_, 1);
        }

        queued_ = 0;
        break;
    case PrimLine:
    case PrimSprite:
        if (queued_ == 2)
        {
            if (draw)
            {
                DrawPrimitive(queue_, 2);
            }

            queued_ = 0;
        }

        break;
    case PrimLineStrip:
        if (queued_ == 2)
        {
            if (draw)
            {
                DrawPrimitive(queue_, 2);
            }

            queue_[0] = queue_[1];
            queued_ = 1;
        }

        break;
    case PrimTriangle:
        if (queued_ == 3)
        {
            if (draw)
            {
                DrawPrimitive(queue_, 3);
            }

            queued_ = 0;
        }

        break;
    case PrimTriangleStrip:
        if (queued_ == 3)
        {
            if (draw)
            {
                DrawPrimitive(queue_, 3);
            }

            queue_[0] = queue_[1];
            queue_[1] = queue_[2];
            queued_ = 2;
        }

        break;
    case PrimTriangleFan:
        if (queued_ == 3)
        {
            if (draw)
            {
                drawn[0] = queue_[0];
                drawn[1] = queue_[1];
                drawn[2] = queue_[2];
                DrawPrimitive(drawn, 3);
            }

            queue_[1] = queue_[2];
            queued_ = 2;
        }

        break;
    default:
        // Type 7 is reserved: nothing is drawn
        queued_ = 0;
        break;
    }
}

// The CLUT buffer loaded from local memory at a TEX0 write when its CLD says so (CLD 2 and 3 also keep CBP for 4 and 5)
void Gs::LoadClut(const RegTex0& tex0)
{
    u32 psm = tex0.psm;
    bool indexed = psm == PSMT8 || psm == PSMT4 || psm == PSMT8H || psm == PSMT4HL || psm == PSMT4HH;
    if (!indexed)
    {
        return;
    }

    switch (tex0.cld)
    {
    case 1:
        break;
    case 2:
        cbp_[0] = tex0.cbp;
        break;
    case 3:
        cbp_[1] = tex0.cbp;
        break;
    case 4:
        if (cbp_[0] == tex0.cbp)
        {
            return;
        }

        cbp_[0] = tex0.cbp;
        break;
    case 5:
        if (cbp_[1] == tex0.cbp)
        {
            return;
        }

        cbp_[1] = tex0.cbp;
        break;
    default:
        return;
    }

    bool eightBit = psm == PSMT8 || psm == PSMT8H;
    u32 entries = eightBit ? 256 : 16;
    u32 cpsm = tex0.cpsm;
    bool thirtyTwo = cpsm == PSMCT32 || cpsm == PSMCT24;
    // The same CLUT loaded again with nothing written to its pages since: the buffer would come out the same
    const u64 source0 = static_cast<u64>(tex0.cbp) | static_cast<u64>(cpsm) << 14 | static_cast<u64>(tex0.csm) << 18 |
                        static_cast<u64>(tex0.csa) << 19 | static_cast<u64>(entries) << 24;
    const u64 source1 = tex0.csm != 0 ? texclut_.value : 0;
    // (nothing written anywhere since: no page of it to look at)
    const bool sameSource = lastClutSource_[0] == source0 && lastClutSource_[1] == source1;
    if (sameSource && newestWrite_ <= lastClutGeneration_ && (backend_ == nullptr || !backend_->PagesGpuNewer(lastClutPages_)))
    {
        return;
    }

    {
        PageSet pages;
        u32 clutPsm = thirtyTwo ? PSMCT32 : cpsm == PSMCT16S ? PSMCT16S : PSMCT16;
        if (tex0.csm == 0)
        {
            PagesOfRect(clutPsm, tex0.cbp, 1, 0, 0, 15, 15, pages);
        }
        else
        {
            s32 x = static_cast<s32>(texclut_.cou * 16);
            PagesOfRect(clutPsm, tex0.cbp, texclut_.cbw, x, static_cast<s32>(texclut_.cov), x + 255, static_cast<s32>(texclut_.cov), pages);
        }

        bool unchanged = sameSource;
        for (u32 page = 0; unchanged && page < PageCount; page++)
        {
            if (pages.test(page) && pageWritten_[page] > lastClutGeneration_)
            {
                unchanged = false;
            }
        }

        if (unchanged && (backend_ == nullptr || !backend_->PagesGpuNewer(pages)))
        {
            return;
        }

        lastClutPages_ = pages;
    }

    lastClutSource_[0] = source0;
    lastClutSource_[1] = source1;
    if (backend_ != nullptr)
    {
        u32 clutPsm = thirtyTwo ? PSMCT32 : cpsm == PSMCT16S ? PSMCT16S : PSMCT16;
        if (tex0.csm == 0)
        {
            PrepareCpuRect(false, clutPsm, tex0.cbp, 1, 0, 0, 15, 15, "CLUT");
        }
        else
        {
            s32 x = static_cast<s32>(texclut_.cou * 16);
            PrepareCpuRect(false, clutPsm, tex0.cbp, texclut_.cbw, x, static_cast<s32>(texclut_.cov), x + 255,
                           static_cast<s32>(texclut_.cov), "CLUT");
        }
    }

    if (tex0.csm == 0)
    {
        // CSM1: the entries in 8 by 2 pixel groups of the palette's 16 pixel wide area at CBP (a buffer 64 pixels wide), the
        // 8 bit textures' second and third groups of eight in each 32 traded (the PS2's palettes are stored with bits 3 and 4 of
        // the index swapped)
        for (u32 i = 0; i < entries; i++)
        {
            u32 x = (i & 7) | ((i >> 4) & 1) << 3;
            u32 y = ((i >> 3) & 1) | (i >> 5) << 1;
            if (!eightBit)
            {
                x = i & 7;
                y = i >> 3;
            }

            if (thirtyTwo)
            {
                u32 colour = ReadPixel(PSMCT32, x, y, tex0.cbp, 1);
                u32 slot = ((tex0.csa & 15) * 16 + i) & 255;
                clut_[slot] = static_cast<u16>(colour);
                clut_[slot + 256] = static_cast<u16>(colour >> 16);
            }
            else
            {
                u32 colour = ReadPixel(cpsm == PSMCT16S ? PSMCT16S : PSMCT16, x, y, tex0.cbp, 1);
                clut_[(tex0.csa * 16 + i) & 511] = static_cast<u16>(colour);
            }
        }
    }
    else
    {
        // CSM2: a line of entries at (COU * 16, COV) of a buffer of TEXCLUT's width
        for (u32 i = 0; i < entries; i++)
        {
            u32 x = texclut_.cou * 16 + i;
            u32 y = texclut_.cov;
            if (thirtyTwo)
            {
                u32 colour = ReadPixel(PSMCT32, x, y, tex0.cbp, texclut_.cbw);
                u32 slot = ((tex0.csa & 15) * 16 + i) & 255;
                clut_[slot] = static_cast<u16>(colour);
                clut_[slot + 256] = static_cast<u16>(colour >> 16);
            }
            else
            {
                u32 colour = ReadPixel(cpsm == PSMCT16S ? PSMCT16S : PSMCT16, x, y, tex0.cbp, texclut_.cbw);
                clut_[(tex0.csa * 16 + i) & 511] = static_cast<u16>(colour);
            }
        }
    }

    // The buffer's contents as a number (the texture cache's key)
    u64 hash = 1469598103934665603ull;
    for (u16 entry : clut_)
    {
        hash = (hash ^ entry) * 1099511628211ull;
    }

    lastClutGeneration_ = generation_;
    NextGeneration();
    clutVersion_ = static_cast<u32>(hash ^ (hash >> 32));
}

// Host to local transfer data: pixels of the destination format, left to right and top to bottom in TRXREG's rectangle at
// TRXPOS's destination corner (wrapping at 2048)
void Gs::WriteImage(const u8* data, u32 bytes)
{
    NextGeneration();
    if (!transferring_)
    {
        return;
    }

    u32 psm = bitbltbuf_.dpsm;
    u32 bits = psm == PSMCT24 || psm == PSMZ24 ? 24 : BitsPerPixel(psm);
    if (bits == 0)
    {
        return;
    }

    u32 width = trxreg_.rrw;
    u32 height = trxreg_.rrh;
    u32 bp = bitbltbuf_.dbp;
    u32 bw = bitbltbuf_.dbw;
    u32 left = trxpos_.dsax;
    u32 top = trxpos_.dsay;
    if (width == 0 || height == 0)
    {
        transferring_ = false;
        return;
    }

    // The bytes not making a whole pixel wait for the next data
    std::vector<u8> buffer;
    const u8* at = data;
    u32 size = bytes;
    if (!transferRest_.empty())
    {
        buffer = transferRest_;
        buffer.insert(buffer.end(), data, data + bytes);
        at = buffer.data();
        size = static_cast<u32>(buffer.size());
        transferRest_.clear();
    }

    u32 offset = 0;
    auto put = [&](u32 value) {
        if (transferY_ >= height)
        {
            return;
        }

        WritePixel(psm, (left + transferX_) & 2047, (top + transferY_) & 2047, bp, bw, value);
        if (++transferX_ == width)
        {
            transferX_ = 0;
            transferY_++;
        }
    };

    // The common formats written straight into local memory (the same addresses WritePixel takes), each page marked once a run
    u32 lastPage = ~0u;
    auto mark = [&](u32 page) {
        if (page != lastPage)
        {
            MarkWritten(page);
            lastPage = page;
        }
    };
    auto advance = [&]() {
        if (++transferX_ == width)
        {
            transferX_ = 0;
            transferY_++;
        }
    };
    if (psm == PSMCT32 || psm == PSMT8 || psm == PSMT4)
    {
        u32* vm32 = reinterpret_cast<u32*>(vm_);
        // A row's pixels a block's run at a time (a block is 8 pixels across in 32 bit, 16 in 8 bit): the block's address and
        // its page worked out once a run, each pixel's place in it by the column's layout (the render thread's busiest
        // function, the textures streamed every frame)
        auto endRun = [&](u32 run) {
            transferX_ += run;
            if (transferX_ == width)
            {
                transferX_ = 0;
                transferY_++;
            }
        };
        if (psm == PSMCT32)
        {
            while (offset + 4 <= size && transferY_ < height)
            {
                const u32 x = (left + transferX_) & 2047;
                const u32 y = (top + transferY_) & 2047;
                const u32 run = std::min({8 - (x & 7), width - transferX_, (size - offset) / 4});
                const u32 first = PixelAddress32(x, y, bp, bw);
                mark(first >> 11);
                const u32 block = first & ~63u;
                for (u32 i = 0; i < run; i++)
                {
                    std::memcpy(&vm32[block | Column32((x + i) & 7, y & 7)], at + offset + i * 4, 4);
                }

                offset += run * 4;
                endRun(run);
            }
        }
        else if (psm == PSMT8)
        {
            while (offset < size && transferY_ < height)
            {
                const u32 x = (left + transferX_) & 2047;
                const u32 y = (top + transferY_) & 2047;
                const u32 run = std::min({16 - (x & 15), width - transferX_, size - offset});
                const u32 first = PixelAddress8(x, y, bp, bw);
                mark(first >> 13);
                const u32 block = first & ~255u;
                const u8* columns = Column8Table[y & 15];
                for (u32 i = 0; i < run; i++)
                {
                    vm_[block | columns[(x + i) & 15]] = at[offset + i];
                }

                offset += run;
                endRun(run);
            }
        }
        else
        {
            auto put4 = [&](u32 value) {
                if (transferY_ >= height)
                {
                    return;
                }

                u32 a = PixelAddress4((left + transferX_) & 2047, (top + transferY_) & 2047, bp, bw);
                u8& byte = vm_[a >> 1];
                byte = (a & 1) ? static_cast<u8>((byte & 0x0F) | value << 4) : static_cast<u8>((byte & 0xF0) | value);
                mark(a >> 14);
                advance();
            };
            for (; offset < size; offset++)
            {
                put4(at[offset] & 0xF);
                put4(at[offset] >> 4);
            }
        }

        // Past the rectangle's end the data is dropped, as put drops it (a part pixel at the data's end waits for the next)
        if (transferY_ >= height)
        {
            offset = size;
        }
    }

    switch (psm == PSMCT32 || psm == PSMT8 || psm == PSMT4 ? 0 : bits)
    {
    case 32:
        for (; offset + 4 <= size; offset += 4)
        {
            u32 value;
            std::memcpy(&value, at + offset, 4);
            put(value);
        }

        break;
    case 24:
        for (; offset + 3 <= size; offset += 3)
        {
            put(static_cast<u32>(at[offset]) | static_cast<u32>(at[offset + 1]) << 8 | static_cast<u32>(at[offset + 2]) << 16);
        }

        break;
    case 16:
        for (; offset + 2 <= size; offset += 2)
        {
            put(static_cast<u32>(at[offset]) | static_cast<u32>(at[offset + 1]) << 8);
        }

        break;
    case 8:
        for (; offset < size; offset++)
        {
            put(at[offset]);
        }

        break;
    case 4:
        for (; offset < size; offset++)
        {
            put(at[offset] & 0xF);
            put(at[offset] >> 4);
        }

        break;
    default:
        break;
    }

    if (offset < size)
    {
        transferRest_.assign(at + offset, at + size);
    }

    if (transferY_ >= height)
    {
        transferring_ = false;
    }
}

// A local to local copy of TRXREG's rectangle from the source buffer's corner to the destination's, a pixel at a time in the
// source format (the formats are taken to be the same size, as the GS requires)
void Gs::TransferLocalToLocal()
{
    NextGeneration();
    u32 width = trxreg_.rrw;
    u32 height = trxreg_.rrh;
    if (width != 0 && height != 0)
    {
        PrepareCpuRect(false, bitbltbuf_.spsm, bitbltbuf_.sbp, bitbltbuf_.sbw, static_cast<s32>(trxpos_.ssax),
                       static_cast<s32>(trxpos_.ssay), static_cast<s32>(trxpos_.ssax + width) - 1,
                       static_cast<s32>(trxpos_.ssay + height) - 1, "local to local source");
        PrepareCpuRect(true, bitbltbuf_.dpsm, bitbltbuf_.dbp, bitbltbuf_.dbw, static_cast<s32>(trxpos_.dsax),
                       static_cast<s32>(trxpos_.dsay), static_cast<s32>(trxpos_.dsax + width) - 1,
                       static_cast<s32>(trxpos_.dsay + height) - 1, "local to local destination");
    }

    u32 dir = trxpos_.dir;
    for (u32 row = 0; row < height; row++)
    {
        u32 y = dir & 2 ? height - 1 - row : row;
        for (u32 column = 0; column < width; column++)
        {
            u32 x = dir & 1 ? width - 1 - column : column;
            u32 value = ReadPixel(bitbltbuf_.spsm, (trxpos_.ssax + x) & 2047, (trxpos_.ssay + y) & 2047, bitbltbuf_.sbp,
                                  bitbltbuf_.sbw);
            WritePixel(bitbltbuf_.dpsm, (trxpos_.dsax + x) & 2047, (trxpos_.dsay + y) & 2047, bitbltbuf_.dbp, bitbltbuf_.dbw,
                       value);
        }
    }
}

// The PCRTC: each enabled circuit reads its buffer (DISPFB: base, width, format, corner) over DISPLAY's size in pixels (its
// width in video clocks divided by the magnification), placed by DISPLAY's offset relative to the other circuit's (an
// interlaced display's offsets counting half lines); circuit 1 is blended over circuit 2 by PMODE's ALP (or its own alpha),
// the background colour under both
void Gs::ReadDisplay(DisplayImage& image)
{
    struct Circuit
    {
        bool enabled;
        u32 width;
        u32 height;
        s32 x;
        s32 y;
    } circuits[2] = {};

    bool interlaced = smode2.interlaced != 0;
    for (u32 i = 0; i < 2; i++)
    {
        bool enabled = i == 0 ? pmode.en1 != 0 : pmode.en2 != 0;
        const RegDisplay& d = display[i];
        if (!enabled || (dispfb[i].fbw == 0 && d.dw == 0 && d.dh == 0 && d.magh == 0))
        {
            continue;
        }

        circuits[i].enabled = true;
        circuits[i].width = static_cast<u32>(d.dw + 1) / static_cast<u32>(d.magh + 1);
        circuits[i].height = static_cast<u32>(d.dh + 1) / static_cast<u32>(d.magv + 1);
        if (smode2.ffmd != 0 && interlaced)
        {
            circuits[i].height = (circuits[i].height + 1) >> 1;
        }

        circuits[i].x = static_cast<s32>(d.dx) / static_cast<s32>(d.magh + 1);
        circuits[i].y = static_cast<s32>(d.dy) / (interlaced ? 2 : 1);
    }

    if (!circuits[0].enabled && !circuits[1].enabled)
    {
        image.width = 0;
        image.height = 0;
        image.pixels.clear();
        return;
    }

    // The picture is the enabled circuits' union, from the nearest one's corner
    s32 originX = 0x7FFFFFFF;
    s32 originY = 0x7FFFFFFF;
    s32 endX = -0x7FFFFFFF;
    s32 endY = -0x7FFFFFFF;
    for (const Circuit& c : circuits)
    {
        if (!c.enabled)
        {
            continue;
        }

        originX = std::min(originX, c.x);
        originY = std::min(originY, c.y);
        endX = std::max(endX, c.x + static_cast<s32>(c.width));
        endY = std::max(endY, c.y + static_cast<s32>(c.height));
    }

    if (backend_ != nullptr)
    {
        backend_->Flush();
        for (u32 i = 0; i < 2; i++)
        {
            if (circuits[i].enabled)
            {
                PrepareCpuRect(false, dispfb[i].psm, dispfb[i].fbp * 32, dispfb[i].fbw, static_cast<s32>(dispfb[i].dbx),
                               static_cast<s32>(dispfb[i].dby), static_cast<s32>(dispfb[i].dbx + circuits[i].width) - 1,
                               static_cast<s32>(dispfb[i].dby + circuits[i].height) - 1, "display");
            }
        }
    }

    // Drawing at a scale, the picture is the shown buffers' shadows (scale times the size)
    const u32 n = backend_ != nullptr ? 1 : scale_;
    image.width = static_cast<u32>(endX - originX) * n;
    image.height = static_cast<u32>(endY - originY) * n;
    image.pixels.assign(static_cast<size_t>(image.width) * image.height, 0);
    Shadow* shown[2] = {};
    for (u32 i = 0; n > 1 && i < 2; i++)
    {
        if (circuits[i].enabled)
        {
            shown[i] = ShadowFor(dispfb[i].psm, dispfb[i].fbp * 32, dispfb[i].fbw, false);
            if (shown[i] != nullptr)
            {
                SyncShadow(*shown[i]);
            }
        }
    }

    auto readCircuit = [&](u32 index, s32 px, s32 py, u32& colour) -> bool {
        const Circuit& c = circuits[index];
        if (!c.enabled)
        {
            return false;
        }

        s32 sx = px - (c.x - originX) * static_cast<s32>(n);
        s32 sy = py - (c.y - originY) * static_cast<s32>(n);
        if (sx < 0 || sy < 0 || sx >= static_cast<s32>(c.width * n) || sy >= static_cast<s32>(c.height * n))
        {
            return false;
        }

        const RegDispfb& fb = dispfb[index];
        u32 bx = (static_cast<u32>(sx) / n + fb.dbx) & 2047;
        u32 by = (static_cast<u32>(sy) / n + fb.dby) & 2047;
        u32 bp = fb.fbp * 32;
        const Shadow* shadow = shown[index];
        auto read = [&](u32 psm) -> u32 {
            if (shadow != nullptr)
            {
                u32 sampleX = fb.dbx * n + static_cast<u32>(sx);
                u32 sampleY = fb.dby * n + static_cast<u32>(sy);
                if (sampleX < shadow->width * n && sampleY < shadow->height * n)
                {
                    u32 value = shadow->samples[static_cast<size_t>(sampleY) * shadow->width * n + sampleX];
                    return psm == PSMCT24 ? value & 0xFFFFFF : value;
                }
            }

            return ReadPixel(psm, bx, by, bp, fb.fbw);
        };
        switch (fb.psm)
        {
        case PSMCT32:
            colour = read(PSMCT32);
            break;
        case PSMCT24:
            colour = read(PSMCT24) | 0x80000000u;
            break;
        case PSMCT16:
        case PSMCT16S:
        {
            u32 c16 = read(fb.psm);
            colour = (c16 & 0x1F) << 3 | ((c16 >> 5) & 0x1F) << 11 | ((c16 >> 10) & 0x1F) << 19 | (c16 & 0x8000 ? 0x80000000u : 0);
            break;
        }
        default:
            colour = 0;
            break;
        }

        return true;
    };

    u32 background = static_cast<u32>(bgcolor & 0xFFFFFF);
    // Both circuits reading the same buffer at the same place blend a colour with itself: the colour (a * alp + a * (255 - alp))
    // / 255 is a), so one read does
    if (circuits[0].enabled && circuits[1].enabled && dispfb[0].value == dispfb[1].value && circuits[0].x == circuits[1].x &&
        circuits[0].y == circuits[1].y && circuits[0].width == circuits[1].width && circuits[0].height == circuits[1].height)
    {
        // The rows read by the worker threads (each pixel's read is independent)
        std::function<void(int, int)> rows = [&](int first, int last) {
            for (u32 y = static_cast<u32>(first); y < static_cast<u32>(last); y++)
            {
                for (u32 x = 0; x < image.width; x++)
                {
                    u32 c = 0;
                    readCircuit(0, static_cast<s32>(x), static_cast<s32>(y), c);
                    image.pixels[static_cast<size_t>(y) * image.width + x] = (c & 0xFFFFFF) | 0xFF000000u;
                }
            }
        };
        GetWorkers().ForRows(0, static_cast<int>(image.height), rows);
        return;
    }

    std::function<void(int, int)> rows = [&](int first, int last) {
    for (u32 y = static_cast<u32>(first); y < static_cast<u32>(last); y++)
    {
        for (u32 x = 0; x < image.width; x++)
        {
            u32 c1 = 0;
            u32 c2 = 0;
            bool has1 = readCircuit(0, static_cast<s32>(x), static_cast<s32>(y), c1);
            bool has2 = readCircuit(1, static_cast<s32>(x), static_cast<s32>(y), c2);
            u32 under = has2 ? c2 : background;
            u32 out = under;
            if (has1)
            {
                // ALP or circuit 1's own alpha (doubled to a full byte)
                u32 alpha = pmode.mmod != 0 ? static_cast<u32>(pmode.alp) : std::min<u32>((c1 >> 24) * 2, 0xFF);
                out = 0;
                for (u32 shift = 0; shift < 24; shift += 8)
                {
                    u32 a = (c1 >> shift) & 0xFF;
                    u32 b = (under >> shift) & 0xFF;
                    u32 blended = (a * alpha + b * (0xFF - alpha)) / 0xFF;
                    out |= std::min<u32>(blended, 0xFF) << shift;
                }
            }

            image.pixels[static_cast<size_t>(y) * image.width + x] = (out & 0xFFFFFF) | 0xFF000000u;
        }
    }
    };
    GetWorkers().ForRows(0, static_cast<int>(image.height), rows);
}
}
