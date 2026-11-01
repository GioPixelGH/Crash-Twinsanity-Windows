#pragma once

// The GIF: GS packets (a GIF tag, then its loops of PACKED registers, REGLIST registers or IMAGE data) taken to the GS on its
// three paths: path 1 from VU1's memory by XGKICK, path 2 through VIF1's DIRECT, path 3 from main memory by the GIF's DMA
// channel. Each path keeps its place in a packet between transfers.

#include "common.h"

namespace Gs
{
class Gs;
}

namespace Ee
{
class Gif
{
public:
    explicit Gif(Gs::Gs& gs) : gs_(gs) {}

    // Where the paths' data goes instead of the GS while one is set (the render thread's queue, graphics/renderthread.h): it's
    // handed over as it comes, and given back to TransferNow and KickNow in the same order on the GS's side
    class Forward
    {
    public:
        // Quadwords on a path (0-2)
        virtual void Transfer(u32 path, const u8* data, u32 quadwords) = 0;
        // Path 1's packets of a kick, the quadwords KickNow would read, laid out in a row (KickLength of them); the VU1 code's
        // address it was kicked from (for TWIN_GS_TRACE)
        virtual void Kick(const u8* vuMemory, u32 address, u32 quadwords, u32 source) = 0;
    };
    void SetForward(Forward* forward) { forward_ = forward; }
    bool forwarding() const { return forward_ != nullptr; }

    // Quadwords on a path (0-2 for paths 1-3); returns how many were taken (all, unless the path ends a packet with EOP and
    // stopAtEnd asks to stop there)
    u32 Transfer(u32 path, const u8* data, u32 quadwords, bool stopAtEnd = false)
    {
        if (forward_ != nullptr)
        {
            forward_->Transfer(path, data, quadwords);
            return quadwords;
        }

        return TransferNow(path, data, quadwords, stopAtEnd);
    }
    u32 TransferNow(u32 path, const u8* data, u32 quadwords, bool stopAtEnd = false)
    {
        u32 taken = TransferTaken(path, data, quadwords, stopAtEnd);
        if (tap != nullptr && !inKick_)
        {
            tap(path, data, taken);
        }

        return taken;
    }
    // Given every path's quadwords as the GS takes them (a path 1 kick's in a row, once): capture.cpp's GS dump while it records
    using Tap = void (*)(u32 path, const u8* data, u32 quadwords);
    static inline Tap tap = nullptr;
    // Path 1's XGKICK: packets read from VU1's memory (a 16 KB ring) from a quadword address until one ends with EOP; source is
    // the kicking VU1 code's address for TWIN_GS_TRACE
    void Kick(const u8* vuMemory, u32 address, u32 source = ~0u)
    {
        if (forward_ != nullptr)
        {
            forward_->Kick(vuMemory, address, KickLength(vuMemory, address), source);
            return;
        }

        KickNow(vuMemory, address, source);
    }
    void KickNow(const u8* vuMemory, u32 address, u32 source = ~0u);
    // A kick's quadwords given in a row (Forward::Kick's): the same as KickNow on the ring they came from
    void KickRow(const u8* quadwords, u32 count, u32 source);
    // How many quadwords from the address KickNow reads: its packets' tags and data up to the one with EOP (at most the ring)
    static u32 KickLength(const u8* vuMemory, u32 address);

    Gs::Gs& gs() { return gs_; }

    // The paths' places in their packets, for a recording (capture.h)
    template <typename Io>
    void Serialize(Io& io)
    {
        io.Field(paths_);
    }

private:
    struct Path
    {
        u32 loops = 0;
        u32 registerCount = 0;
        u64 registers = 0;
        u32 format = 0;
        bool endOfPacket = false;
        u32 reg = 0;
    };

    u32 TransferTaken(u32 path, const u8* data, u32 quadwords, bool stopAtEnd);

    Gs::Gs& gs_;
    Path paths_[3];
    bool inKick_ = false;
    Forward* forward_ = nullptr;
};
}
