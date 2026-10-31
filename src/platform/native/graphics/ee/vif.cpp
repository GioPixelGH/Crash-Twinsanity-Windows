#include "vif.h"

#include "gif.h"
#include "vu.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Ee
{
namespace
{
enum VifCommand : u32
{
    Nop = 0x00,
    Stcycl = 0x01,
    Offset = 0x02,
    Base = 0x03,
    Itop = 0x04,
    Stmod = 0x05,
    Mskpath3 = 0x06,
    Mark = 0x07,
    Flushe = 0x10,
    Flush = 0x11,
    Flusha = 0x13,
    Mscal = 0x14,
    Mscalf = 0x15,
    Mscnt = 0x17,
    Stmask = 0x20,
    Strow = 0x30,
    Stcol = 0x31,
    Mpg = 0x4A,
    Direct = 0x50,
    Directhl = 0x51,
};

// The bytes of an UNPACK element of each VN/VL: S-32, S-16, S-8, -, V2-32, ..., V4-5
constexpr u8 ElementBytes[16] = {4, 2, 1, 0, 8, 4, 2, 0, 12, 6, 3, 0, 16, 8, 4, 2};

bool IsUnpack(u32 command)
{
    return (command & 0x60) == 0x60;
}
}

u32 Vif::DataWords(u32 code) const
{
    u32 command = (code >> 24) & 0x7F;
    u32 imm = code & 0xFFFF;
    u32 num = (code >> 16) & 0xFF;
    if (IsUnpack(command))
    {
        u32 count = num == 0 ? 256 : num;
        u32 size = ElementBytes[command & 0xF];
        u32 wl = writeLength != 0 ? writeLength : 256;
        u32 elements = count;
        if (cycleLength < wl)
        {
            // Filling writes: CL elements of data for every WL quadwords
            elements = cycleLength * (count / wl) + std::min(count % wl, cycleLength);
        }

        return (elements * size + 3) / 4;
    }

    switch (command)
    {
    case Stmask:
        return 1;
    case Strow:
    case Stcol:
        return 4;
    case Mpg:
        return (num == 0 ? 256 : num) * 2;
    case Direct:
    case Directhl:
        return (imm == 0 ? 0x10000 : imm) * 4;
    default:
        return 0;
    }
}

void Vif::Process(const u32* words, u32 count)
{
    u32 at = 0;
    while (at < count)
    {
        if (pending_)
        {
            u32 take = std::min(pendingWords_ - static_cast<u32>(pendingData_.size()), count - at);
            pendingData_.insert(pendingData_.end(), words + at, words + at + take);
            at += take;
            if (pendingData_.size() == pendingWords_)
            {
                pending_ = false;
                Execute(pendingCode_, pendingData_.data());
                pendingData_.clear();
            }

            continue;
        }

        u32 code = words[at++];
        u32 needed = DataWords(code);
        if (needed == 0)
        {
            Execute(code, nullptr);
            continue;
        }

        if (count - at >= needed)
        {
            Execute(code, words + at);
            at += needed;
            continue;
        }

        pending_ = true;
        pendingCode_ = code;
        pendingWords_ = needed;
        pendingData_.assign(words + at, words + count);
        at = count;
    }
}

void Vif::StartProgram(u32 address, bool resume)
{
    // The double buffer: the program gets the buffer just filled as TOP (and ITOP), the next data goes to the other one
    if (index_ == 1)
    {
        top = tops & 0x3FF;
        if (doubleBuffer)
        {
            tops = base;
            doubleBuffer = false;
        }
        else
        {
            tops = base + offset;
            doubleBuffer = true;
        }
    }

    itop = itops;
    vu_.top = top;
    vu_.itop = itop;
    programs++;
    vu_.Run(resume ? vu_.pc() : address);
}

namespace
{
// $TWIN_VIF_HISTORY: VIF1's last codes kept (each with the registers before it, the row after it and its data's first words),
// written out when a VU1 program runs away (DumpVifHistory, from vu1translate.cpp's report)
struct VifRecord
{
    u64 serial;
    u32 code;
    u32 mode;
    u32 mask;
    u32 cycleLength;
    u32 writeLength;
    u32 tops;
    u32 rowBefore[4];
    u32 rowAfter[4];
    u32 words;
    u32 data[8];
};
constexpr u32 HistorySize = 256;
VifRecord g_History[HistorySize];
u64 g_HistorySerial = 0;

bool HistoryOn()
{
    static const bool on = std::getenv("TWIN_VIF_HISTORY") != nullptr;
    return on;
}
}

void DumpVifHistory(const char* path)
{
    if (!HistoryOn())
    {
        return;
    }

    FILE* file = std::fopen(path, "w");
    if (file == nullptr)
    {
        return;
    }

    u64 first = g_HistorySerial > HistorySize ? g_HistorySerial - HistorySize : 0;
    for (u64 serial = first; serial < g_HistorySerial; serial++)
    {
        const VifRecord& r = g_History[serial % HistorySize];
        std::fprintf(file, "#%llu code %08x (cmd %02x num %u addr %#x%s) mode %u mask %08x cl %u wl %u tops %#x row %x %x %x %x -> %x %x %x %x;"
                           " %u words:",
                     static_cast<unsigned long long>(r.serial), r.code, (r.code >> 24) & 0x7F, (r.code >> 16) & 0xFF, r.code & 0x3FF,
                     (r.code >> 15) & 1 ? "+tops" : "", r.mode, r.mask, r.cycleLength, r.writeLength, r.tops, r.rowBefore[0],
                     r.rowBefore[1], r.rowBefore[2], r.rowBefore[3], r.rowAfter[0], r.rowAfter[1], r.rowAfter[2], r.rowAfter[3], r.words);
        for (u32 i = 0; i < std::min(r.words, 8u); i++)
        {
            std::fprintf(file, " %08x", r.data[i]);
        }

        std::fprintf(file, "\n");
    }

    std::fclose(file);
}

void Vif::Execute(u32 code, const u32* data)
{
    VifRecord* record = nullptr;
    if (index_ == 1 && HistoryOn())
    {
        record = &g_History[g_HistorySerial % HistorySize];
        record->serial = g_HistorySerial++;
        record->code = code;
        record->mode = mode;
        record->mask = mask;
        record->cycleLength = cycleLength;
        record->writeLength = writeLength;
        record->tops = tops;
        std::memcpy(record->rowBefore, row, 16);
        record->words = DataWords(code);
        for (u32 i = 0; i < std::min(record->words, 8u); i++)
        {
            record->data[i] = data[i];
        }
    }

    ExecuteNow(code, data);
    if (record != nullptr)
    {
        std::memcpy(record->rowAfter, row, 16);
    }
}

void Vif::ExecuteNow(u32 code, const u32* data)
{
    u32 command = (code >> 24) & 0x7F;
    u32 imm = code & 0xFFFF;
    u32 num = (code >> 16) & 0xFF;
    if (IsUnpack(command))
    {
        Unpack(code, data, DataWords(code));
        return;
    }

    switch (command)
    {
    case Nop:
    case Mskpath3:
    case Flushe:
    case Flush:
    case Flusha:
        break;
    case Stcycl:
        cycleLength = imm & 0xFF;
        writeLength = (imm >> 8) & 0xFF;
        break;
    case Offset:
        offset = imm & 0x3FF;
        doubleBuffer = false;
        tops = base;
        break;
    case Base:
        base = imm & 0x3FF;
        break;
    case Itop:
        itops = imm & 0x3FF;
        break;
    case Stmod:
        mode = imm & 3;
        break;
    case Mark:
        mark = imm;
        break;
    case Mscal:
    case Mscalf:
        if (imm >= vu_.codeSize() && !warnedAddress_)
        {
            // A stream gone wrong (data read as VIF codes): the PS2 would run whatever is there too
            std::fprintf(stderr, "graphics: VIF%u MSCAL %#x past the program memory\n", index_, imm);
            warnedAddress_ = true;
        }

        StartProgram(imm, false);
        break;
    case Mscnt:
        StartProgram(0, true);
        break;
    case Stmask:
        mask = data[0];
        break;
    case Strow:
        std::memcpy(row, data, 16);
        break;
    case Stcol:
        std::memcpy(col, data, 16);
        break;
    case Mpg:
        vu_.WriteCode(imm, reinterpret_cast<const u8*>(data), num == 0 ? 256 : num);
        break;
    case Direct:
    case Directhl:
        if (gif_ != nullptr)
        {
            gif_->Transfer(1, reinterpret_cast<const u8*>(data), imm == 0 ? 0x10000 : imm);
        }

        break;
    default:
        break;
    }
}

namespace
{
// An unmasked unpack writing every cycle's quadword, its data all there: the elements read straight (as Vif::Unpack's own
// loop reads them), through the mode
template <u32 Vn, u32 Vl, bool Unsigned>
void UnpackStraight(u8* memory, u32 memoryQuadwords, u32 address, const u8* bytes, u32 count, u32 elementSize, u32 cycleLength,
                    u32 wl, u32 mode, u32* row)
{
    constexpr u32 Size = Vl == 0 ? 4 : Vl == 1 ? 2 : 1;
    auto read = [&](const u8* at) -> u32 {
        if constexpr (Vl == 0)
        {
            u32 value;
            std::memcpy(&value, at, 4);
            return value;
        }
        else if constexpr (Vl == 1)
        {
            u16 value;
            std::memcpy(&value, at, 2);
            return Unsigned ? value : static_cast<u32>(static_cast<s32>(static_cast<s16>(value)));
        }
        else
        {
            return Unsigned ? *at : static_cast<u32>(static_cast<s32>(static_cast<s8>(*at)));
        }
    };

    u32 cycle = 0;
    const u8* at = bytes;
    for (u32 written = 0; written < count; written++, at += elementSize)
    {
        u32 values[4];
        if constexpr (Vn == 0)
        {
            values[0] = values[1] = values[2] = values[3] = read(at);
        }
        else if constexpr (Vn == 1)
        {
            values[0] = values[2] = read(at);
            values[1] = values[3] = read(at + Size);
        }
        else
        {
            for (u32 c = 0; c < 4; c++)
            {
                values[c] = read(at + c * Size);
            }
        }

        auto* destination = reinterpret_cast<u32*>(memory + (address & (memoryQuadwords - 1)) * 16);
        switch (mode)
        {
        case 1:
            for (u32 c = 0; c < 4; c++)
            {
                destination[c] = values[c] + row[c];
            }

            break;
        case 2:
            for (u32 c = 0; c < 4; c++)
            {
                row[c] = row[c] + values[c];
                destination[c] = row[c];
            }

            break;
        case 3:
            for (u32 c = 0; c < 4; c++)
            {
                row[c] = values[c];
                destination[c] = values[c];
            }

            break;
        default:
            std::memcpy(destination, values, 16);
            break;
        }

        address++;
        if (++cycle >= wl)
        {
            address += cycleLength - wl;
            cycle = 0;
        }
    }
}

using StraightUnpack = void (*)(u8*, u32, u32, const u8*, u32, u32, u32, u32, u32, u32*);
template <u32 Format, bool Unsigned>
constexpr StraightUnpack StraightFor()
{
    return &UnpackStraight<(Format >> 2) & 3, Format & 3, Unsigned>;
}

constexpr StraightUnpack StraightUnpacks[2][16] = {
    {StraightFor<0, false>(), StraightFor<1, false>(), StraightFor<2, false>(), nullptr, StraightFor<4, false>(), StraightFor<5, false>(),
     StraightFor<6, false>(), nullptr, StraightFor<8, false>(), StraightFor<9, false>(), StraightFor<10, false>(), nullptr,
     StraightFor<12, false>(), StraightFor<13, false>(), StraightFor<14, false>(), nullptr},
    {StraightFor<0, true>(), StraightFor<1, true>(), StraightFor<2, true>(), nullptr, StraightFor<4, true>(), StraightFor<5, true>(),
     StraightFor<6, true>(), nullptr, StraightFor<8, true>(), StraightFor<9, true>(), StraightFor<10, true>(), nullptr,
     StraightFor<12, true>(), StraightFor<13, true>(), StraightFor<14, true>(), nullptr},
};
}

// An UNPACK: NUM quadwords written from ADDR (plus TOPS with FLG) in the write cycle's pattern, each from an element of the
// data (S broadcast to xyzw, V2 written x y x y, V3 read as V4 (w is the next element's first value), V4-5 as 5:5:5:1
// colours), 16 and 8 bit values sign extended unless USN, each value through the mask (data, the row, the column, nothing)
// and the mode (data plus the row, the row accumulating, the row set)
void Vif::Unpack(u32 code, const u32* data, u32 words)
{
    u32 command = (code >> 24) & 0x7F;
    u32 num = (code >> 16) & 0xFF;
    u32 count = num == 0 ? 256 : num;
    bool masked = (command & 0x10) != 0;
    u32 format = command & 0xF;
    u32 vn = (format >> 2) & 3;
    u32 vl = format & 3;
    bool unsignedValues = (code >> 14) & 1;
    u32 address = code & 0x3FF;
    if (index_ == 1 && ((code >> 15) & 1))
    {
        address += tops;
    }

    // (VU0's 256 quadwords, VU1's 1024: the address wraps by a mask, not a division an element)
    u32 memoryQuadwords = vu_.dataSize() / 16;
    u32 wl = writeLength != 0 ? writeLength : 256;
    bool fill = cycleLength < wl;
    u32 elementSize = ElementBytes[format];
    const u8* bytes = reinterpret_cast<const u8*>(data);
    u32 totalBytes = words * 4;
    u32 dataOffset = 0;
    u32 cycle = 0;
    u32 modeUsed = format == 0xF ? 0 : mode;

    // Unmasked, every cycle written, every element's values inside the data: straight
    {
        const u32 componentBytes = vl == 0 ? 4 : vl == 1 ? 2 : 1;
        const u32 readBytes = vn == 0 ? componentBytes : vn == 1 ? 2 * componentBytes : 4 * componentBytes;
        if (!masked && !fill && format != 0xF && StraightUnpacks[unsignedValues][format] != nullptr &&
            static_cast<u64>(count - 1) * elementSize + readBytes <= totalBytes)
        {
            StraightUnpacks[unsignedValues][format](vu_.data(), memoryQuadwords, address, bytes, count, elementSize, cycleLength, wl,
                                                    modeUsed, row);
            return;
        }
    }

    auto readValue = [&](u32 offset, u32 component) -> u32 {
        // A value of the element at offset: 32, 16 or 8 bits (beyond the data: 0)
        u32 size = vl == 0 ? 4 : vl == 1 ? 2 : 1;
        u32 at = offset + component * size;
        if (at + size > totalBytes)
        {
            return 0;
        }

        switch (vl)
        {
        case 0:
        {
            u32 value;
            std::memcpy(&value, bytes + at, 4);
            return value;
        }
        case 1:
        {
            u16 value;
            std::memcpy(&value, bytes + at, 2);
            return unsignedValues ? value : static_cast<u32>(static_cast<s32>(static_cast<s16>(value)));
        }
        default:
        {
            u8 value = bytes[at];
            return unsignedValues ? value : static_cast<u32>(static_cast<s32>(static_cast<s8>(value)));
        }
        }
    };

    for (u32 written = 0; written < count; written++)
    {
        u32 values[4];
        if (format == 0xF)
        {
            u16 colour = 0;
            if (dataOffset + 2 <= totalBytes)
            {
                std::memcpy(&colour, bytes + dataOffset, 2);
            }

            values[0] = (colour & 0x001F) << 3;
            values[1] = (colour & 0x03E0) >> 2;
            values[2] = (colour & 0x7C00) >> 7;
            values[3] = (colour & 0x8000) >> 8;
        }
        else
        {
            switch (vn)
            {
            case 0:
            {
                u32 value = readValue(dataOffset, 0);
                values[0] = value;
                values[1] = value;
                values[2] = value;
                values[3] = value;
                break;
            }
            case 1:
                values[0] = readValue(dataOffset, 0);
                values[1] = readValue(dataOffset, 1);
                values[2] = values[0];
                values[3] = values[1];
                break;
            default:
                for (u32 c = 0; c < 4; c++)
                {
                    values[c] = readValue(dataOffset, c);
                }

                break;
            }
        }

        auto* destination = reinterpret_cast<u32*>(vu_.data() + (address & (memoryQuadwords - 1)) * 16);
        u32 maskRow = std::min(cycle, 3u);
        for (u32 c = 0; c < 4; c++)
        {
            u32 selection = masked ? (mask >> (maskRow * 8 + c * 2)) & 3 : 0;
            switch (selection)
            {
            case 0:
                switch (modeUsed)
                {
                case 1:
                    destination[c] = values[c] + row[c];
                    break;
                case 2:
                    row[c] = row[c] + values[c];
                    destination[c] = row[c];
                    break;
                case 3:
                    row[c] = values[c];
                    destination[c] = values[c];
                    break;
                default:
                    destination[c] = values[c];
                    break;
                }

                break;
            case 1:
                destination[c] = row[c];
                break;
            case 2:
                destination[c] = col[maskRow];
                break;
            default:
                break;
            }
        }

        address++;
        cycle++;
        if (fill)
        {
            if (cycle <= cycleLength)
            {
                dataOffset += elementSize;
            }
            else if (cycle == wl)
            {
                cycle = 0;
            }
        }
        else
        {
            dataOffset += elementSize;
            if (cycle >= wl)
            {
                address += cycleLength - wl;
                cycle = 0;
            }
        }
    }
}
}
