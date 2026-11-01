#include "gif.h"

#include <algorithm>

#include "../gs/gs.h"

#include <cstring>

namespace Ee
{
namespace
{
enum GifFormat : u32
{
    Packed = 0,
    RegList = 1,
    Image = 2,
};
}

u32 Gif::TransferTaken(u32 index, const u8* data, u32 quadwords, bool stopAtEnd)
{
    Path& path = paths_[index];
    u32 taken = 0;
    while (taken < quadwords)
    {
        const u8* at = data + taken * 16;
        if (path.loops == 0)
        {
            // A tag: NLOOP, EOP, PRE and PRIM, FLG, NREG (0 is 16), the registers
            u64 low;
            u64 high;
            std::memcpy(&low, at, 8);
            std::memcpy(&high, at + 8, 8);
            taken++;
            path.loops = low & 0x7FFF;
            path.endOfPacket = (low >> 15) & 1;
            bool pre = (low >> 46) & 1;
            u32 prim = (low >> 47) & 0x7FF;
            path.format = (low >> 58) & 3;
            path.registerCount = (low >> 60) & 0xF;
            if (path.registerCount == 0)
            {
                path.registerCount = 16;
            }

            path.registers = high;
            path.reg = 0;
            if (path.loops > 0)
            {
                gs_.StartTag();
                if (pre && path.format == Packed)
                {
                    gs_.WriteRegister(Gs::PRIM, prim);
                }
            }
            else if (path.endOfPacket && stopAtEnd)
            {
                return taken;
            }

            continue;
        }

        switch (path.format)
        {
        case Packed:
            // The tag's quadwords there written in one go (as many as it has, up to the data's end)
            taken += gs_.WritePackedRun(path.registers, path.registerCount, path.reg, path.loops, at, quadwords - taken);
            break;
        case RegList:
        {
            // Two registers a quadword; an odd count leaves the last quadword's upper half unused
            for (u32 half = 0; half < 2 && path.loops > 0; half++)
            {
                u64 value;
                std::memcpy(&value, at + half * 8, 8);
                u32 descriptor = (path.registers >> (path.reg * 4)) & 0xF;
                // REGLIST's descriptors are register addresses, A+D and NOP write nothing
                if (descriptor < 0xE)
                {
                    gs_.WriteRegister(descriptor, value);
                }

                if (++path.reg == path.registerCount)
                {
                    path.reg = 0;
                    path.loops--;
                }
            }

            taken++;
            break;
        }
        default:
        {
            u32 count = quadwords - taken < path.loops ? quadwords - taken : path.loops;
            gs_.WriteImage(at, count * 16);
            taken += count;
            path.loops -= count;
            break;
        }
        }

        if (path.loops == 0 && path.endOfPacket && stopAtEnd)
        {
            return taken;
        }
    }

    return taken;
}

void Gif::KickNow(const u8* vuMemory, u32 address, u32 source)
{
    if (tap != nullptr)
    {
        // The kick's packets in a row, as the GS takes them
        u8 row[0x400 * 16];
        u32 length = KickLength(vuMemory, address);
        for (u32 i = 0; i < length; i++)
        {
            std::memcpy(row + i * 16, vuMemory + ((address + i) & 0x3FF) * 16, 16);
        }

        tap(0, row, length);
    }

    inKick_ = true;
    gs_.traceSource = source;
    // Path 1 starts a packet fresh at each kick
    paths_[0] = Path{};
    // (as far as the memory's end at a time: Transfer stops at the packet's end)
    u32 at = address & 0x3FF;
    u32 left = 0x400;
    while (left > 0)
    {
        u32 run = std::min(left, 0x400 - at);
        u32 taken = TransferNow(0, vuMemory + at * 16, run, true);
        at = (at + taken) & 0x3FF;
        left -= taken;
        const Path& path = paths_[0];
        if ((path.loops == 0 && path.endOfPacket) || taken == 0)
        {
            break;
        }
    }

    gs_.traceSource = ~0u;
    inKick_ = false;
}

void Gif::KickRow(const u8* quadwords, u32 count, u32 source)
{
    if (tap != nullptr)
    {
        tap(0, quadwords, count);
    }

    inKick_ = true;
    gs_.traceSource = source;
    paths_[0] = Path{};
    TransferNow(0, quadwords, count, true);
    gs_.traceSource = ~0u;
    inKick_ = false;
}

u32 Gif::KickLength(const u8* vuMemory, u32 address)
{
    // TransferNow's walk through the packets with nothing written: each tag, then its loops' quadwords
    constexpr u32 Ring = 0x400;
    u32 at = address & (Ring - 1);
    u32 length = 0;
    while (length < Ring)
    {
        u64 low;
        std::memcpy(&low, vuMemory + ((at + length) & (Ring - 1)) * 16, 8);
        length++;
        u32 loops = low & 0x7FFF;
        bool endOfPacket = (low >> 15) & 1;
        u32 format = (low >> 58) & 3;
        u32 registers = (low >> 60) & 0xF;
        if (registers == 0)
        {
            registers = 16;
        }

        u32 data = format == Packed ? loops * registers : format == RegList ? (loops * registers + 1) / 2 : loops;
        length += data;
        if (endOfPacket)
        {
            break;
        }
    }

    return std::min(length, Ring);
}
}
