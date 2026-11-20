#include "fastrenderer.h"

#include "ee/gif.h"
#include "ee/vu.h"
#include "ee/vu1translate.h"
#include "gs/gs.h"
#include "ui/nativeui.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace NativeGraphics
{
namespace
{
// $TWIN_FAST_RENDERER: -1 unset (the setting's), else the mode it asks for
int AskedByVariable()
{
    static const int asked = [] {
        const char* value = std::getenv("TWIN_FAST_RENDERER");
        if (value == nullptr || value[0] == '\0')
        {
            return -1;
        }

        if (std::strcmp(value, "verify") == 0)
        {
            return static_cast<int>(FastMode::Verify);
        }

        return value[0] != '0' ? static_cast<int>(FastMode::Fast) : static_cast<int>(FastMode::Accurate);
    }();
    return asked;
}
}

FastMode FastRendererMode()
{
    const int asked = AskedByVariable();
    if (asked >= 0)
    {
        return static_cast<FastMode>(asked);
    }

    return NativeUi::GetOption(NativeUi::OptionFastRenderer) != 0 ? FastMode::Fast : FastMode::Accurate;
}

bool FastRendererHasKinds()
{
    // No kind of drawing has its prepared path yet (THE LIST 75 brings the scenery's)
    return false;
}

namespace
{
// $TWIN_FAST_KICKS and $TWIN_FAST_PREPARE read once: every program, or the programs at these addresses
struct FastKicks
{
    bool asked = false;
    bool all = false;
    std::vector<u32> addresses;

    bool Names(u32 address) const
    {
        if (all)
        {
            return true;
        }

        for (u32 asked : addresses)
        {
            if (asked == address)
            {
                return true;
            }
        }

        return false;
    }
};

FastKicks ReadPrograms(const char* variable)
{
    {
        FastKicks made;
        const char* value = std::getenv(variable);
        if (value == nullptr || value[0] == '\0')
        {
            return made;
        }

        made.asked = true;
        const std::string text = value;
        if (text == "all")
        {
            made.all = true;
            return made;
        }

        size_t at = 0;
        while (at < text.size())
        {
            size_t end = text.find(',', at);
            if (end == std::string::npos)
            {
                end = text.size();
            }

            const std::string item = text.substr(at, end - at);
            if (!item.empty())
            {
                made.addresses.push_back(static_cast<u32>(std::strtoul(item.c_str(), nullptr, 16)));
            }

            at = end + 1;
        }

        return made;
    }
}

const FastKicks& AskedKicks()
{
    static const FastKicks kicks = ReadPrograms("TWIN_FAST_KICKS");
    return kicks;
}
}

bool FastKicksAsked()
{
    return AskedKicks().asked;
}

bool FastKickRuns(u32 source)
{
    const FastKicks& kicks = AskedKicks();
    if (FastRendererMode() == FastMode::Accurate)
    {
        return false;
    }

    if (kicks.all)
    {
        return true;
    }

    // (The program's index + 1 in the source's bits 16 to 21: rendersplit.h)
    const u32 slot = (source >> 16) & 0x3F;
    if (slot == 0)
    {
        return false;
    }

    return kicks.Names(Ee::Vu1ProgramAddress(static_cast<s32>(slot) - 1));
}

namespace
{
// A program's run interpreted from st (at its entry) until control leaves its code, at an instruction with no branch pending, or
// the program ends (true). A run that goes on and on is stopped as ended (said once)
bool RunUntilLeaving(Ee::Vu& vu, Ee::VuMicroState& st, u32 base, u32 size)
{
    const u32 mask = vu.codeSize() - 1;
    for (u64 steps = 0;; steps++)
    {
        if (!vu.StepMicro(st))
        {
            return true;
        }

        if (st.branch == 0 && st.ebit == 0 && !st.delayBranch && ((st.pc - base) & mask) >= size)
        {
            return false;
        }

        if (steps > (1ull << 26))
        {
            static std::atomic<bool> said{false};
            if (!said.exchange(true))
            {
                std::fprintf(stderr, "graphics: a prepared stand-in's program at %#x ran on and on: stopped\n", base);
            }

            return true;
        }
    }
}

// A kick that can't be converted (IMAGE data): marked in the batch (verify's two sides alike: the kick's address in the
// marker), or, on VU1 itself, the batch so far sent and then the kick itself, in their order
constexpr u32 PreparedRawKick = 0x101;

void AddRawKick(std::vector<u8>& batch, u32 address)
{
    const Gs::PreparedWrite marker = {address, PreparedRawKick, 0};
    const u8* bytes = reinterpret_cast<const u8*>(&marker);
    batch.insert(batch.end(), bytes, bytes + sizeof(marker));
}

// $TWIN_FAST_PREPARE's stand-in prepared function: the program's own run, interpreted, its kicks converted into the batch. On
// VU1 itself (it has the GIF's xgkick) a kick that can't be converted sends the batch so far and itself; on verify's copy it's
// marked
bool RunConverted(PreparedRun& run)
{
    Ee::Vu& vu = run.vu;
    auto original = vu.xgkick;
    vu.xgkick = [&run, &original](u32 address) {
        if (Ee::Gif::ConvertKick(run.vu.data(), address, run.batch))
        {
            return;
        }

        if (!original)
        {
            AddRawKick(run.batch, address);
            return;
        }

        if (!run.batch.empty() && run.vu.prepared)
        {
            run.vu.prepared(run.batch.data(), static_cast<u32>(run.batch.size() / 16), run.base);
            run.batch.clear();
        }

        original(address);
    };
    run.ended = RunUntilLeaving(vu, run.st, run.base, run.size);
    vu.xgkick = original;
    return true;
}

const FastKicks& AskedPrepared()
{
    static const FastKicks programs = ReadPrograms("TWIN_FAST_PREPARE");
    return programs;
}

// Verify's counts, said at exit once verify has run
std::atomic<u64> g_Verified{0};
std::atomic<u64> g_Differences{0};

[[gnu::format(printf, 2, 3)]] void ReportDifference(u32 base, const char* format, ...)
{
    if (g_Differences.fetch_add(1) >= 50)
    {
        return;
    }

    std::fprintf(stderr, "fast verify: the program at %#x: ", base);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fprintf(stderr, "\n");
}

bool Verify(PreparedFunction function, Ee::Vu& vu, Ee::VuMicroState& st, u32 base, u32 size, bool& ended)
{
    static const bool counting = [] {
        std::atexit([] {
            std::fprintf(stderr, "fast verify: %llu runs of prepared functions compared, %llu differences\n",
                         static_cast<unsigned long long>(g_Verified.load()), static_cast<unsigned long long>(g_Differences.load()));
        });
        return true;
    }();
    (void)counting;

    // The function on a copy of VU1 (with the kick pending in it: the program sends that at its first XGKICK)
    static Ee::Vu shadow(1);
    shadow.CopyFrom(vu);
    shadow.executed = vu.executed;
    shadow.kickPc = vu.kickPc;
    shadow.kickPending = vu.kickPending;
    shadow.kickPendingAddress = vu.kickPendingAddress;
    shadow.kickPendingPc = vu.kickPendingPc;
    Ee::VuMicroState shadowState = st;
    std::vector<u8> made;
    PreparedRun run{shadow, shadowState, base, size, made, false};
    const bool ran = function(run);

    // The program on VU1 itself, interpreted: its kicks sent as ever, and converted
    std::vector<u8> expected;
    auto original = vu.xgkick;
    vu.xgkick = [&expected, &vu, &original](u32 address) {
        if (!Ee::Gif::ConvertKick(vu.data(), address, expected))
        {
            AddRawKick(expected, address);
        }

        if (original)
        {
            original(address);
        }
    };
    ended = RunUntilLeaving(vu, st, base, size);
    vu.xgkick = original;
    // (The counts said at the first run and every 16384 too: the level sweep's runs end with _Exit, which skips atexit)
    const u64 verified = ++g_Verified;
    if (verified == 1 || verified % 16384 == 0)
    {
        std::fprintf(stderr, "fast verify: %llu runs of prepared functions compared, %llu differences\n",
                     static_cast<unsigned long long>(verified), static_cast<unsigned long long>(g_Differences.load()));
    }

    if (!ran)
    {
        ReportDifference(base, "the function couldn't run");
        return true;
    }

    if (made != expected)
    {
        size_t at = 0;
        while (at < made.size() && at < expected.size() && made[at] == expected[at])
        {
            at++;
        }

        ReportDifference(base, "the GS's writes differ from entry %zu (%zu and %zu entries)", at / 16, made.size() / 16,
                         expected.size() / 16);
        return true;
    }

    const char* what = nullptr;
    if (std::memcmp(shadow.data(), vu.data(), vu.dataSize()) != 0)
    {
        what = "data memory";
    }
    else if (std::memcmp(shadow.vf, vu.vf, sizeof(vu.vf)) != 0)
    {
        what = "VF registers";
    }
    else if (std::memcmp(shadow.vi, vu.vi, sizeof(vu.vi)) != 0)
    {
        what = "VI registers";
    }
    else if (std::memcmp(&shadow.acc, &vu.acc, sizeof(vu.acc)) != 0)
    {
        what = "ACC";
    }
    else if (shadow.q != vu.q || shadow.p != vu.p || shadow.i != vu.i || shadow.r != vu.r)
    {
        what = "Q, P, I or R";
    }
    else if (shadow.statusFlag != vu.statusFlag || shadow.macFlag != vu.macFlag || shadow.clipFlag != vu.clipFlag)
    {
        what = "the flags";
    }
    else if (run.ended != ended || shadowState.pc != st.pc)
    {
        what = "where it left";
    }
    else if (shadow.kickPending != vu.kickPending || shadow.kickPendingAddress != vu.kickPendingAddress)
    {
        what = "the kick left pending";
    }

    if (what != nullptr)
    {
        ReportDifference(base, "%s differ (left at %#x/%#x, ended %d/%d)", what, shadowState.pc, st.pc, run.ended ? 1 : 0,
                         ended ? 1 : 0);
    }

    return true;
}
}

PreparedFunction PreparedFor(u32 programAddress)
{
    // No kind of drawing has its prepared function yet (THE LIST 75 brings the scenery's); the test's stand-in where asked
    return AskedPrepared().Names(programAddress) ? &RunConverted : nullptr;
}

bool RunPrepared(PreparedFunction function, Ee::Vu& vu, Ee::VuMicroState& st, u32 base, u32 size, bool& ended)
{
    if (FastRendererMode() == FastMode::Verify)
    {
        return Verify(function, vu, st, base, size, ended);
    }

    thread_local std::vector<u8> batch;
    batch.clear();
    PreparedRun run{vu, st, base, size, batch, false};
    if (!function(run))
    {
        return false;
    }

    if (!batch.empty() && vu.prepared)
    {
        vu.prepared(batch.data(), static_cast<u32>(batch.size() / 16), base);
    }

    ended = run.ended;
    return true;
}
}

bool NativeGraphicsFastRendererShown()
{
    return NativeGraphics::FastRendererHasKinds();
}
