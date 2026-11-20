#include "gif.h"

#include <algorithm>
#include <bit>

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
                // A strip's tag (ST, RGBAQ, XYZ2 or XYZF2 a vertex), its quadwords all here: drawn at once while the GS collects a
                // run (the fast renderer's kicks: Gs::TakePackedStrip)
                const u32 registerList = high & 0xFFF;
                if (pre && path.format == Packed && path.registerCount == 3 && (registerList == 0x512 || registerList == 0x412) &&
                    path.loops * 3 <= quadwords - taken && gs_.TakePackedStrip(prim, registerList, at + 16, path.loops))
                {
                    taken += path.loops * 3;
                    path.loops = 0;
                    if (path.endOfPacket && stopAtEnd)
                    {
                        return taken;
                    }

                    continue;
                }

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
    const bool run = runsKick != nullptr && runsKick(source);
    if (run)
    {
        gs_.StartRun();
    }

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

    if (run)
    {
        gs_.EndRun();
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
    if (runsKick != nullptr && runsKick(source))
    {
        gs_.StartRun();
        TransferNow(0, quadwords, count, true);
        gs_.EndRun();
    }
    else
    {
        TransferNow(0, quadwords, count, true);
    }

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

namespace Ee
{
void Gif::PreparedNow(const u8* entries, u32 count, u32 source)
{
    if (tap != nullptr)
    {
        // A GS dump gets the batch as the A+D packet it stands for (its strips as their writes; the PACKED ST's Q left out: a
        // dump's next tag sets it again)
        std::vector<u8> expanded;
        Gs::ExpandPrepared(entries, count, expanded);
        const u32 expandedCount = static_cast<u32>(expanded.size() / 16);
        std::vector<u8> packet;
        packet.reserve((static_cast<size_t>(expandedCount) + 1) * 16);
        u32 writes = 0;
        for (u32 n = 0; n < expandedCount; n++)
        {
            Gs::PreparedWrite write;
            std::memcpy(&write, expanded.data() + static_cast<size_t>(n) * 16, 16);
            if (write.address != Gs::PreparedPackedQ)
            {
                writes++;
            }
        }

        const u64 tag[2] = {static_cast<u64>(writes) | 1ull << 15 | static_cast<u64>(Packed) << 58 | 1ull << 60, 0xE};
        packet.insert(packet.end(), reinterpret_cast<const u8*>(tag), reinterpret_cast<const u8*>(tag) + 16);
        for (u32 n = 0; n < expandedCount; n++)
        {
            Gs::PreparedWrite write;
            std::memcpy(&write, expanded.data() + static_cast<size_t>(n) * 16, 16);
            if (write.address != Gs::PreparedPackedQ)
            {
                const u64 quadword[2] = {write.value, write.address & 0xFF};
                packet.insert(packet.end(), reinterpret_cast<const u8*>(quadword), reinterpret_cast<const u8*>(quadword) + 16);
            }
        }

        tap(0, packet.data(), static_cast<u32>(packet.size() / 16));
    }

    inKick_ = true;
    gs_.traceSource = source;
    gs_.StartRun();
    gs_.PlayWrites(entries, count);
    gs_.EndRun();
    gs_.traceSource = ~0u;
    inKick_ = false;
}

bool Gif::ConvertKick(const u8* vuMemory, u32 address, std::vector<u8>& batch)
{
    const size_t kept = batch.size();
    auto add = [&batch](u32 reg, u64 value) {
        const Gs::PreparedWrite write = {value, reg, 0};
        const u8* bytes = reinterpret_cast<const u8*>(&write);
        batch.insert(batch.end(), bytes, bytes + sizeof(write));
    };
    auto quadword = [vuMemory](u32 at, u32 q[4]) { std::memcpy(q, vuMemory + (at & 0x3FF) * 16, 16); };

    // KickNow's walk through the packets: each tag, then its loops' quadwords (at most the ring)
    constexpr u32 Ring = 0x400;
    u32 at = address & (Ring - 1);
    u32 read = 0;
    bool tagged = false;
    f32 packedQ = 1.0f;
    while (read < Ring)
    {
        u64 low;
        u64 high;
        std::memcpy(&low, vuMemory + ((at + read) & (Ring - 1)) * 16, 8);
        std::memcpy(&high, vuMemory + ((at + read) & (Ring - 1)) * 16 + 8, 8);
        read++;
        const u32 loops = low & 0x7FFF;
        const bool endOfPacket = (low >> 15) & 1;
        const bool pre = (low >> 46) & 1;
        const u32 prim = (low >> 47) & 0x7FF;
        const u32 format = (low >> 58) & 3;
        u32 registers = (low >> 60) & 0xF;
        if (registers == 0)
        {
            registers = 16;
        }

        if (loops > 0)
        {
            // Gs::StartTag
            tagged = true;
            packedQ = 1.0f;
            if (pre && format == Packed)
            {
                add(Gs::PRIM, prim);
            }
        }

        if (format == Packed)
        {
            for (u32 loop = 0; loop < loops; loop++)
            {
                for (u32 r = 0; r < registers && read < Ring; r++, read++)
                {
                    u32 q[4];
                    quadword(at + read, q);
                    const u32 descriptor = (high >> (r * 4)) & 0xF;
                    switch (descriptor)
                    {
                    case Gs::PRIM:
                        add(Gs::PRIM, q[0] & 0x7FF);
                        break;
                    case Gs::RGBAQ:
                        add(Gs::RGBAQ, static_cast<u64>(q[0] & 0xFF) | static_cast<u64>(q[1] & 0xFF) << 8 |
                                           static_cast<u64>(q[2] & 0xFF) << 16 | static_cast<u64>(q[3] & 0xFF) << 24 |
                                           static_cast<u64>(std::bit_cast<u32>(packedQ)) << 32);
                        break;
                    case Gs::ST:
                        add(Gs::ST, static_cast<u64>(q[0]) | static_cast<u64>(q[1]) << 32);
                        packedQ = std::bit_cast<f32>(q[2]);
                        break;
                    case Gs::UV:
                        add(Gs::UV, static_cast<u64>(q[0] & 0x3FFF) | static_cast<u64>(q[1] & 0x3FFF) << 16);
                        break;
                    case Gs::XYZF2:
                        add((q[3] >> 15) & 1 ? Gs::XYZF3 : Gs::XYZF2,
                            static_cast<u64>(q[0] & 0xFFFF) | static_cast<u64>(q[1] & 0xFFFF) << 16 |
                                static_cast<u64>((q[2] >> 4) & 0xFFFFFF) << 32 | static_cast<u64>((q[3] >> 4) & 0xFF) << 56);
                        break;
                    case Gs::XYZ2:
                        add((q[3] >> 15) & 1 ? Gs::XYZ3 : Gs::XYZ2,
                            static_cast<u64>(q[0] & 0xFFFF) | static_cast<u64>(q[1] & 0xFFFF) << 16 | static_cast<u64>(q[2]) << 32);
                        break;
                    case Gs::FOG:
                        add(Gs::FOG, static_cast<u64>((q[3] >> 4) & 0xFF) << 56);
                        break;
                    case 0xE:
                        add(q[2] & 0xFF, static_cast<u64>(q[0]) | static_cast<u64>(q[1]) << 32);
                        break;
                    case 0xF:
                        break;
                    default:
                        add(descriptor, static_cast<u64>(q[0]) | static_cast<u64>(q[1]) << 32);
                        break;
                    }
                }
            }
        }
        else if (format == RegList)
        {
            u32 reg = 0;
            for (u32 n = 0; n < loops * registers; n++)
            {
                u64 value;
                std::memcpy(&value, vuMemory + ((at + read + n / 2) & (Ring - 1)) * 16 + (n & 1) * 8, 8);
                const u32 descriptor = (high >> (reg * 4)) & 0xF;
                if (descriptor < 0xE)
                {
                    add(descriptor, value);
                }

                reg = reg + 1 == registers ? 0 : reg + 1;
            }

            read += (loops * registers + 1) / 2;
        }
        else if (loops > 0)
        {
            // IMAGE data: not a register write
            batch.resize(kept);
            return false;
        }

        if (endOfPacket)
        {
            break;
        }
    }

    if (tagged)
    {
        add(Gs::PreparedPackedQ, std::bit_cast<u32>(packedQ));
    }

    return true;
}
}
