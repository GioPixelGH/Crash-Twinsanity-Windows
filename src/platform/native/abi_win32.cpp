// Which functions take the System V convention on Windows (include/abi.h's TWIN_EABI_CC, RETAIL_N32's): the calls through
// the retail vtables and function pointers go each function's way (Abi::CallEabi). The list is made from the declarations
// (native/tools/sysv_functions.py) and kept here in an open addressing table of their addresses, made before main (the game's
// calls through its vtables all come after)
#if defined(_WIN32)
#include "abi.h"

#include "native.h"

#include <cstdlib>
#include <mutex>
#include <set>

namespace Abi
{
extern const void* const g_SysvFunctionList[];
extern const u32 g_SysvFunctionCount;

const void* g_SysvTable[SysvTableSize] = {};
bool g_TraceCrossCalls = std::getenv("TWIN_ABI_TRACE") != nullptr;

void NoteCrossCall(const void* function, bool mixed)
{
    static std::mutex lock;
    static std::set<const void*> told;
    std::lock_guard guard(lock);
    if (told.insert(function).second)
    {
        Native::Log("abi: %s call to %p (CrashTwinsanity.exe+0x%llx)", mixed ? "a mixed" : "an unmixed", function,
                    static_cast<unsigned long long>(reinterpret_cast<u64>(function) - 0x10000000ull));
    }
}

namespace
{
// The self-test's (--selftest-abi): a function taking a float among its integers (System V's), and one of a slot like it that
// leaves the float out (Windows')
int g_Marker = 0;
}
}

extern "C"
{
    void AbiTestMixed(int* self, f32 value, int* out) RETAIL_N32(AbiTestMixed);
    void AbiTestPlain(int* self, int* out) RETAIL(AbiTestPlain);
}

void AbiTestMixed(int* self, f32 value, int* out)
{
    out[0] = self == &Abi::g_Marker ? 1 : 0;
    out[1] = value == 2.5f ? 1 : 0;
}

void AbiTestPlain(int* self, int* out)
{
    out[0] = self == &Abi::g_Marker ? 1 : 0;
    out[1] = 1;
}

namespace Abi
{
// The calls through a pointer going each function's way: a mixed call to System V's, the same call to Windows' own (the float
// left out), and a call without the float to System V's (its integers where it takes them)
int SelfTest()
{
    int mixed[2] = {};
    int plain[2] = {};
    int unmixed[2] = {};
    CallEabi<void>(reinterpret_cast<const void*>(&AbiTestMixed), &g_Marker, 2.5f, mixed);
    CallEabi<void>(reinterpret_cast<const void*>(&AbiTestPlain), &g_Marker, 2.5f, plain);
    CallEabi<void>(reinterpret_cast<const void*>(&AbiTestMixed), &g_Marker, unmixed);
    // Floats made s32s out of range saturate as the R5900's cvt.w.s does (-fno-strict-float-cast-overflow, native/CMakeLists.txt)
    volatile f32 above = 4294967296.0f;
    volatile f32 below = -4294967296.0f;
    u32 high = static_cast<u32>(static_cast<s32>(above));
    u32 low = static_cast<u32>(static_cast<s32>(below));
    bool good = mixed[0] == 1 && mixed[1] == 1 && plain[0] == 1 && plain[1] == 1 && unmixed[0] == 1 && high == 0x7FFFFFFFu &&
                low == 0x80000000u;
    Native::Log("selftest-abi: mixed to System V's %d %d, mixed to Windows' %d %d, unmixed to System V's %d, 2^32 and -2^32 made "
                "s32s 0x%X 0x%X: %s",
                mixed[0], mixed[1], plain[0], plain[1], unmixed[0], high, low, good ? "pass" : "FAIL");
    return good ? 0 : 1;
}

namespace
{
struct TableMaker
{
    TableMaker()
    {
        for (u32 index = 0; index < g_SysvFunctionCount; index++)
        {
            const void* function = g_SysvFunctionList[index];
            if (function == nullptr)
            {
                continue;
            }

            u32 slot = SysvSlot(function);
            while (g_SysvTable[slot] != nullptr && g_SysvTable[slot] != function)
            {
                slot = (slot + 1) & (SysvTableSize - 1);
            }

            g_SysvTable[slot] = function;
        }
    }
} g_TableMaker;
}
}
#endif
