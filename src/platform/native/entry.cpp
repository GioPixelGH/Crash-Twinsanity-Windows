// The native build's entry: its own arguments taken out, the retail data's pointers fixed up, then the game's Main as the
// PS2's entry calls it
#include "abi.h"
#include "debug.h"
#include "native.h"
#include "platform/saves.h"
#include "platform/system.h"
#include "ui/nativeui.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <timeapi.h>

#include <cstdio>
#endif

extern "C"
{
    int Main(u32 argc, char** argv);
    // build/native/data.cpp (native/tools/convert_data.py)
    void NativeApplyDataFixups();
}

namespace Native
{
void StartLowMemory();
#if defined(_WIN32)
// $TWIN_PROFILE (profiler_win32.cpp)
void StartProfiler();
#endif
}

namespace
{
// --selftest-lowmemory: the retail code's kinds of low accesses done once each, which must go on with the PS2's results
int SelfTestLowMemory()
{
    g_NativeLowMemory[0x10] = 0x5A;
    g_NativeLowMemory[0x18] = 0x80;
    // The addresses made at run time, as the retail code's are: a constant address is an operand with no register to move, which
    // the x86-64 emulation can't take, and the game's code has none
    volatile uiptr zero = 0;
    volatile u8* null = reinterpret_cast<volatile u8*>(zero);
    u8 byte = null[0x10];
    s8 signedByte = static_cast<s8>(null[0x18]);
    volatile u32* words = reinterpret_cast<volatile u32*>(zero + 0x40);
    words[1] = 0x12345678;
    u32 word = words[1];
    volatile u64* pair = reinterpret_cast<volatile u64*>(zero + 0x80);
    pair[0] = 1;
    pair[1] = 2;
    u64 sum = pair[0] + pair[1];
    volatile float* f = reinterpret_cast<volatile float*>(zero + 0x100);
    f[0] = 1.5f;
    float back = f[0];
    bool ok = byte == 0x5A && signedByte == -128 && word == 0x12345678 && sum == 3 && back == 1.5f;
    Native::Log("low memory self-test: %s (byte %#x, signed %d, word %#x, sum %llu, float %g)", ok ? "passed" : "FAILED", byte,
                signedByte, word, static_cast<unsigned long long>(sum), back);
    return ok ? 0 : 1;
}
}

// --selftest-saves: the game's own save manager (src/platform/ps2/saves.cpp) through the native memory card: a save made, a file
// written, found and read back, then the test's save removed
int SelfTestSaves()
{
    using namespace Platform::Saves;
    auto run = [](Operation operation)
    {
        StorageInfo info;
        for (int frames = 0; frames < 100; frames++)
        {
            if (Update(0, 0, &info) == Operation::None && GetResult(operation).succeeded != -1)
            {
                return GetResult(operation);
            }
        }
        return Result{-1, -1, -1};
    };
    StorageInfo info{};
    bool ready = Initialise("BE", "SLES-52568");
    for (int i = 0; i < 3; i++)
    {
        Update(0, 0, &info);
    }

    const char* save = "SELFTEST";
    const char* file = "TEST.DAT";
    char data[300];
    for (u32 i = 0; i < sizeof(data); i++)
    {
        data[i] = static_cast<char>(i * 7);
    }

    bool created = CreateSave(0, 0, save) && run(Operation::CreateSave).succeeded == 1;
    bool written = Write(0, 0, save, file, data, sizeof(data)) && run(Operation::Write).succeeded == 1;
    s32 size = -2;
    bool found = FindFile(0, 0, file, save, &size, nullptr) && run(Operation::FindFile).succeeded == 1;
    char back[300] = {};
    bool read = Read(0, 0, save, file, back, sizeof(back)) && run(Operation::Read).succeeded == 1;
    bool same = std::memcmp(data, back, sizeof(data)) == 0;
    std::error_code ignored;
    std::filesystem::remove_all(std::filesystem::path(Native::SettingsFolder()) / "memorycard" / "BESLES-52568SELFTEST", ignored);
    bool ok = ready && info.type == StorageMemoryCard && created && written && found && size == sizeof(data) && read && same;
    Native::Log("saves self-test: %s (card type %d, %d KB free, created %d, written %d, found %d size %d, read %d, same %d)",
                ok ? "passed" : "FAILED", info.type, info.freeKilobytes, created, written, found, size, read, same);
    return ok ? 0 : 1;
}

#if defined(_WIN32)
namespace
{
// Where the log goes on Windows: a windowed program (native/CMakeLists.txt's TWIN_WINDOWS_CONSOLE off) has no console. What
// started it may have given it its output (a pipe, a file): kept. Else the console of the terminal it was started from, else
// log.txt in the settings folder
void SetUpLog()
{
    HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
    if (error != nullptr && error != INVALID_HANDLE_VALUE && GetFileType(error) != FILE_TYPE_UNKNOWN)
    {
        return;
    }

    if (AttachConsole(ATTACH_PARENT_PROCESS))
    {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
    else
    {
        std::error_code ignored;
        std::filesystem::create_directories(Native::SettingsFolder(), ignored);
        std::string log = Native::SettingsFolder() + "/log.txt";
        // The last run's log kept (a run that hung or crashed, started again)
        std::filesystem::rename(log, Native::SettingsFolder() + "/log.previous.txt", ignored);
        std::freopen(log.c_str(), "w", stderr);
        std::freopen(log.c_str(), "a", stdout);
        std::setvbuf(stdout, nullptr, _IONBF, 0);
    }

    // The crash report writes to the handle itself (lowmemory_win32.cpp)
    SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stderr))));
}
}
#endif

int main(int argc, char** argv)
{
    // Where the settings are (portable mode or not: portable.cpp) before anything reads or writes them, the log first
    bool headless = false;
    for (int i = 1; i < argc; i++)
    {
        headless = headless || std::string_view(argv[i]) == "--headless";
    }

    Native::StartPortable(headless);
#if defined(_WIN32)
    SetUpLog();
    Native::StartProfiler();
    // The system timer at 1 ms: the threads' waits (the renderer's, the audio's, the disc's) are as coarse as it otherwise
    timeBeginPeriod(1);
#endif
    Native::ParseArguments(argc, argv);
    if (Native::GetSettings().headless)
    {
        // A headless run (the tests) shows nothing: no Dock icon, no menu bar, never the active app
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
    }
    Native::StartLowMemory();
    Native::StartHangWatch();
    for (int i = 1; i < argc; i++)
    {
        if (std::string_view(argv[i]) == "--selftest-lowmemory")
        {
            return SelfTestLowMemory();
        }
        if (std::string_view(argv[i]) == "--selftest-saves")
        {
            return SelfTestSaves();
        }
#if defined(_WIN32)
        // The calls through the retail vtables and pointers in both conventions (src/platform/native/abi_win32.cpp)
        if (std::string_view(argv[i]) == "--selftest-abi")
        {
            return Abi::SelfTest();
        }
#endif
        // --selftest-ui, --dump-texts (src/platform/native/ui/commands.cpp)
        if (int status = 0; NativeUi::RunCommand(argv[i], &status))
        {
            return status;
        }
    }

    // The port's version first (native/CMakeLists.txt's), for the bug reports' logs
    Native::Log("Crash Twinsanity %s", TWIN_PORT_VERSION);
    Native::LogPortableStart();
    Native::OpenDiscImage();
    NativeApplyDataFixups();
    // $TWIN_FIXED_TIME: the test runs' time (debug.h's g_DebugFixedTime): every frame takes exactly a frame's time and reads
    // finish when asked about, so what a frame draws doesn't depend on how fast the machine is (two builds compared frame by frame)
    if (const char* fixed = std::getenv("TWIN_FIXED_TIME"); fixed != nullptr && fixed[0] != '0')
    {
        g_DebugFixedTime = 1;
    }

    Platform::System::Exit(Main(static_cast<u32>(argc), argv));
}
