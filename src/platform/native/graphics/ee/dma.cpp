#include "dma.h"

#include "address.h"

#include <cstdio>
#include <cstring>

namespace Ee
{
namespace
{
enum TagId : u32
{
    Refe = 0,
    Cnt = 1,
    Next = 2,
    Ref = 3,
    Refs = 4,
    Call = 5,
    Ret = 6,
    End = 7,
};

// A runaway chain (a tag that points nowhere) stops after this many tags
constexpr u32 MaxTags = 4000000;
}

void RunDmaChain(const u8* firstTag, bool tagWords, const std::function<void(const u32* words, u32 count)>& sink)
{
    RunDmaChain(firstTag, tagWords, sink, {});
}

void RunDmaChain(const u8* firstTag, bool tagWords, const std::function<void(const u32* words, u32 count)>& sink,
                 const std::function<void(const u8* tag)>& onTag)
{
    const u8* tag = firstTag;
    const u8* stack[2] = {};
    u32 depth = 0;
    for (u32 n = 0; n < MaxTags && tag != nullptr; n++)
    {
        u32 words[4];
        std::memcpy(words, tag, 16);
        u32 quadwords = words[0] & 0xFFFF;
        u32 id = (words[0] >> 28) & 7;
        u32 address = words[1];
        if (onTag)
        {
            onTag(tag);
        }

        if (tagWords)
        {
            sink(words + 2, 2);
        }

        const u8* data = tag + 16;
        const u8* next = nullptr;
        bool ends = false;
        switch (id)
        {
        case Refe:
            data = HostAddress(address);
            ends = true;
            break;
        case Cnt:
            next = tag + 16 + quadwords * 16;
            break;
        case Next:
            next = HostAddress(address);
            break;
        case Ref:
        case Refs:
            data = HostAddress(address);
            next = tag + 16;
            break;
        case Call:
            if (depth < 2)
            {
                stack[depth++] = tag + 16 + quadwords * 16;
            }

            next = HostAddress(address);
            break;
        case Ret:
            if (depth > 0)
            {
                next = stack[--depth];
            }
            else
            {
                ends = true;
            }

            break;
        default:
            ends = true;
            break;
        }

        if (quadwords > 0)
        {
            if (data == nullptr)
            {
                std::fprintf(stderr, "graphics: DMA tag %08x %08x (tag %u of the chain at %p, at %p) reads from no memory\n", words[0], words[1], n, static_cast<const void*>(firstTag), static_cast<const void*>(tag));
                return;
            }

            sink(reinterpret_cast<const u32*>(data), quadwords * 4);
        }

        if (ends)
        {
            return;
        }

        if (next == nullptr)
        {
            std::fprintf(stderr, "graphics: DMA tag %08x %08x goes to no memory\n", words[0], words[1]);
            return;
        }

        tag = next;
    }
}
}
