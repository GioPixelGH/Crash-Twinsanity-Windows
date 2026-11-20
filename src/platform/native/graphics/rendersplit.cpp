#include "rendersplit.h"

#include "fastrenderer.h"
#include "hardware.h"
#include "renderthread.h"

#include "ee/vu1translate.h"
#include "gs/gs.h"
#include "native.h"
#include "renderer/renderer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace NativeGraphics
{
namespace
{
// The render thread's counts: kicks by the program VU1's run started at and the program whose XGKICK it was (each 0 when not
// told, else 1 + the program's index: entry * ProgramSlots + kicker), then the GIF's paths' transfers, the work queued, VIF1's
// words played there ($TWIN_RENDER_THREAD=vif)
constexpr u32 ProgramSlots = 64;
// (and the same again for a skin's parts' kicks)
constexpr u32 KickSlots = ProgramSlots * ProgramSlots * 2;
constexpr u32 TransferSlot = KickSlots;
constexpr u32 CallSlot = TransferSlot + 4;
constexpr u32 Vif1Slot = CallSlot + 1;
constexpr u32 SlotCount = Vif1Slot + 1;
// The frame's 28 buckets, then a chain of VIF1's other than the frame's, then outside VIF1's chains
constexpr u32 FrameBuckets = 28;
constexpr u32 OtherChain = FrameBuckets;
constexpr u32 NoChain = FrameBuckets + 1;
constexpr u32 BucketCount = NoChain + 1;

// One writer (the render thread), read by the log's: relaxed loads and stores, no locked instruction
struct Count
{
    std::atomic<u64> nanoseconds{0};
    std::atomic<u64> primitives{0};
    std::atomic<u64> records{0};

    void Add(u64 time, u64 drawn)
    {
        nanoseconds.store(nanoseconds.load(std::memory_order_relaxed) + time, std::memory_order_relaxed);
        primitives.store(primitives.load(std::memory_order_relaxed) + drawn, std::memory_order_relaxed);
        records.store(records.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }
};

struct Seen
{
    u64 nanoseconds = 0;
    u64 primitives = 0;
    u64 records = 0;
};

Count g_BySlot[SlotCount];
Count g_ByBucket[BucketCount];
Seen g_SeenSlot[SlotCount];
Seen g_SeenBucket[BucketCount];
u64 g_SeenBusy = 0;

// The render thread's bucket now (set by the work the game's side queues where a bucket starts)
u32 g_Bucket = NoChain;

// VU1's side: a blend program's run seen since program 1's last, and whether this run is a skin's part's
bool g_SawBlend = false;
bool g_SkinRun = false;

// The game's side: the frame's buckets' first tags while its chain is sent
const u8* g_Firsts[FrameBuckets] = {};
u32 g_FirstCount = 0;

u64 Now()
{
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void QueueBucket(u32 bucket)
{
    RenderCall([bucket] { g_Bucket = bucket; });
}

// The renderer's names of the VU1 programs (renderer/vuprograms.cpp's registration), by their address in .vutext
struct KnownProgram
{
    u32 address;
    const char* name;
};

const KnownProgram KnownPrograms[] = {
    {0x2DB0E0, "program 1"},
    {0x2DA060, "program 2"},
    {0x2E4430, "program A"},
    {0x2E47C0, "program D"},
    {0x2E2C60, "program B"},
    {0x2E28D0, "parameter"},
    {0x2E21F0, "blend parameter"},
    {0x2E2450, "blend no shapes"},
    {0x2E25B0, "blend first shape"},
    {0x2E2750, "blend next shape"},
    {0x2E3B40, "type 04"},
    {0x2DF700, "type 01/1B"},
    {0x2DB3E0, "type 03"},
    {0x2DBEB0, "type 02"},
    {0x2E3EC0, "type 0A"},
    {0x2E1A90, "type 0B"},
    {0x2DA080, "type 0C"},
    {0x2E2DE0, "type 0F"},
    {0x2E1000, "type 10"},
    {0x2E3720, "type 11"},
    {0x2DC5D0, "type 15"},
    {0x2DE590, "type 16"},
    {0x2DD780, "type 17"},
    {0x2DDF40, "type 19"},
    {0x2DCFB0, "type 1A"},
    {0x2DB170, "type 1C"},
    {0x2DF230, "type 1E"},
    {0x2DB690, "type 1F"},
    {0x2E3400, "type 20"},
    {0x2DAE70, "type 0D"},
    {0x2DCBD0, "type 12"},
    {0x2D9D90, "type 13"},
    {0x2E0960, "type 18"},
};

// A program's name (its slot: 1 + its index, 0 none told)
std::string ProgramName(u32 slot)
{
    char text[64];
    if (slot == 0)
    {
        return "an untold program";
    }

    const u32 address = Ee::Vu1ProgramAddress(static_cast<s32>(slot - 1));
    for (const KnownProgram& known : KnownPrograms)
    {
        if (known.address == address)
        {
            std::snprintf(text, sizeof(text), "%s (%X)", known.name, address);
            return text;
        }
    }

    std::snprintf(text, sizeof(text), "program at %X", address);
    return text;
}

std::string SlotName(u32 slot)
{
    char text[64];
    if (slot < KickSlots)
    {
        // The kicking program, and the one its run started at when that's another
        const bool skin = slot >= ProgramSlots * ProgramSlots;
        const u32 entry = slot / ProgramSlots % ProgramSlots;
        const u32 kicker = slot % ProgramSlots;
        return (entry == kicker ? ProgramName(kicker) : ProgramName(kicker) + " from " + ProgramName(entry)) + (skin ? ", skins" : "");
    }

    if (slot < CallSlot)
    {
        std::snprintf(text, sizeof(text), "path %u", slot - TransferSlot + 1);
        return text;
    }

    return slot == CallSlot ? "work queued" : "VIF1 played here";
}

std::string BucketName(u32 bucket)
{
    static const char* const names[FrameBuckets] = {
        "sky",         "1",         "opaque",    "global opaque", "screen effects", "5",       "blended 6",
        "blended 7",   "blended 8", "blended 9", "blended 10",    "blended 11",     "blended 12", "blended 13",
        "blended 14",  "blended 15", "blended 16", "blended 17",  "blended 18",     "blended 19", "decals",
        "shadows",     "particles", "distortion", "UI",           "25",             "fonts",   "movies",
    };
    if (bucket < FrameBuckets)
    {
        return names[bucket];
    }

    return bucket == OtherChain ? "other chains" : "outside VIF1's chains";
}

// A line of the counts since the last, the busiest first, as long as they're over a hundredth of a millisecond a frame
void LogCounts(const char* title, Count* counts, Seen* seen, u32 count, std::string (*name)(u32), double perFrame, double busy)
{
    struct Row
    {
        u32 index;
        u64 nanoseconds;
        u64 primitives;
        u64 records;
    };
    std::vector<Row> rows;
    u64 total = 0;
    for (u32 i = 0; i < count; i++)
    {
        const u64 nanoseconds = counts[i].nanoseconds.load(std::memory_order_relaxed);
        const u64 primitives = counts[i].primitives.load(std::memory_order_relaxed);
        const u64 records = counts[i].records.load(std::memory_order_relaxed);
        Row row = {i, nanoseconds - seen[i].nanoseconds, primitives - seen[i].primitives, records - seen[i].records};
        seen[i] = {nanoseconds, primitives, records};
        total += row.nanoseconds;
        if (row.records != 0)
        {
            rows.push_back(row);
        }
    }

    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.nanoseconds > b.nanoseconds; });
    char part[160];
    std::snprintf(part, sizeof(part), "%s (a frame: ms, primitives, records; %.2f ms counted of the render thread's %.2f):", title,
                  static_cast<double>(total) * perFrame / 1e6, busy);
    std::string line = part;
    bool first = true;
    for (const Row& row : rows)
    {
        const double ms = static_cast<double>(row.nanoseconds) * perFrame / 1e6;
        if (ms < 0.01)
        {
            continue;
        }

        std::snprintf(part, sizeof(part), "%s %s %.2f %.0f %.0f", first ? "" : ";", name(row.index).c_str(), ms,
                      static_cast<double>(row.primitives) * perFrame, static_cast<double>(row.records) * perFrame);
        line += part;
        first = false;
    }

    Native::Log("%s", line.c_str());
}
}

bool RenderSplitOn()
{
    static const bool on = [] {
        const char* value = std::getenv("TWIN_RENDER_SPLIT");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return on;
}

u32 SplitKickSource(u32 kickPc)
{
    const u32 source = kickPc * 8;
    // (Also for the fast renderer's $TWIN_FAST_KICKS, which picks kicks by their program)
    static const bool programs = RenderSplitOn() || FastKicksAsked();
    if (!programs)
    {
        return source;
    }

    auto slot = [](s32 program) {
        return program >= 0 && static_cast<u32>(program) + 1 < ProgramSlots ? static_cast<u32>(program) + 1 : 0u;
    };
    return source | slot(Ee::Vu1ProgramAt(kickPc)) << 16 | slot(Ee::Vu1RunEntryProgram()) << 22 | (g_SkinRun ? 1u << 28 : 0u);
}

void SplitRunStarted(u32 start)
{
    // renderer/skins.cpp: a skin's part runs program 1 after a blend program's run (or several), and EndProgram's between
    const u32 address = start & 0x7FF;
    if (address == g_VuPrograms[6].address || address == g_VuPrograms[7].address || address == g_VuPrograms[8].address)
    {
        g_SawBlend = true;
        g_SkinRun = false;
    }
    else if (address == g_VuPrograms[1].address)
    {
        g_SkinRun = g_SawBlend;
        g_SawBlend = false;
    }
    else if (address != g_VuPrograms[EndProgram].address)
    {
        g_SawBlend = false;
        g_SkinRun = false;
    }
}

void SplitFrameBuckets(const u8* const* firsts, u32 count)
{
    g_FirstCount = std::min(count, FrameBuckets);
    for (u32 i = 0; i < g_FirstCount; i++)
    {
        g_Firsts[i] = firsts[i];
    }
}

void SplitChainStart()
{
    QueueBucket(OtherChain);
}

void SplitTag(const u8* tag)
{
    for (u32 i = 0; i < g_FirstCount; i++)
    {
        if (g_Firsts[i] == tag)
        {
            QueueBucket(i);
            return;
        }
    }
}

void SplitChainEnd()
{
    QueueBucket(NoChain);
}

SplitMark SplitStart()
{
    return {Now(), GetHardware().gs->primitivesDrawn};
}

void SplitStop(const SplitMark& mark, u32 kind, u32 value)
{
    const u64 time = Now() - mark.nanoseconds;
    const u64 drawn = GetHardware().gs->primitivesDrawn - mark.primitives;
    u32 slot;
    switch (kind)
    {
    case 0:
        slot = TransferSlot + std::min(value, 3u);
        break;
    case 1:
    case 4:
        // (A kick, or the fast renderer's prepared batch in a kick's place: renderthread.cpp's kinds)
        slot = (value >> 28 & 1) * ProgramSlots * ProgramSlots + (value >> 22 & 0x3F) * ProgramSlots + (value >> 16 & 0x3F);
        break;
    case 2:
        slot = CallSlot;
        break;
    default:
        slot = Vif1Slot;
        break;
    }

    g_BySlot[slot].Add(time, drawn);
    g_ByBucket[std::min(g_Bucket, BucketCount - 1)].Add(time, drawn);
}

void LogRenderSplit(u64 frames)
{
    if (!RenderSplitOn() || frames == 0)
    {
        return;
    }

    const double perFrame = 1.0 / static_cast<double>(frames);
    const u64 busy = RenderBusyNanoseconds();
    const double busyMs = static_cast<double>(busy - g_SeenBusy) * perFrame / 1e6;
    g_SeenBusy = busy;
    LogCounts("render split by kind", g_BySlot, g_SeenSlot, SlotCount, SlotName, perFrame, busyMs);
    LogCounts("render split by bucket", g_ByBucket, g_SeenBucket, BucketCount, BucketName, perFrame, busyMs);
}
}
