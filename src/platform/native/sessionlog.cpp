// The session in the log, for the bug reports: what an issue's first questions ask, said by the log itself (native/WINDOWS.md,
// "The log"). At the start, after the version's line: the date, Windows (and Wine), the command line, the processor and memory;
// then the options and the resolution as their files name them, the threads, the TWIN_ variables, the mods, the texture pack and
// how the last session ended. While the game is played, a line of performance a minute of play: the frames' rate, the slowest
// frame, each thread's work a frame (the counters the threads keep anyway: nothing profiled). At the end the session's summary
// (its play's frame rate, the share of it below the rate chosen) and how it ended, whichever way: the window closed, the game's
// own exit, an error, a crash. Logs are attached to public issues: the profile folder in paths is written %USERPROFILE%, and
// nothing names the user, the computer, a serial number or an address
#include "native.h"

#include "graphics/renderthread.h"
#include "ui/nativeui.h"

#include "game/gamecontroller.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
extern char** environ;
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace
{
// ------------------------------------------------------------------------------------------------------------------ privacy
// What the log's text has replaced: the profile folder's path anywhere (as a whole path's start), a folder named as the user or
// the computer (a path's whole part)
struct Needle
{
    std::string text;
    const char* as;
    bool part;
};
// (Never destroyed: the threads may log while the program exits)
std::vector<Needle>& g_Needles = *new std::vector<Needle>();
bool g_PrivacyReady = false;

char Lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool Separator(char c)
{
    return c == '/' || c == '\\';
}

// A character a file's or folder's name goes on with
bool NameCharacter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
           static_cast<unsigned char>(c) >= 0x80;
}

// The text at a place is the needle's, whatever the case and the slashes
bool MatchesAt(const std::string& text, size_t at, const std::string& needle)
{
    for (size_t i = 0; i < needle.size(); i++)
    {
        char a = text[at + i];
        char b = needle[i];
        if (!(Separator(a) && Separator(b)) && Lower(a) != Lower(b))
        {
            return false;
        }
    }

    return true;
}

void AddNeedle(const std::string& text, const char* as, bool part)
{
    // Short ones would be found in ordinary words
    if (text.size() < (part ? 3u : 4u))
    {
        return;
    }

    for (const Needle& needle : g_Needles)
    {
        if (needle.text == text && std::strcmp(needle.as, as) == 0)
        {
            return;
        }
    }

    g_Needles.push_back({text, as, part});
}

#if defined(_WIN32)
// An environment variable's value as the system's code page has it (the settings' paths, getenv's) and as UTF-8 (SDL's), and its
// short form if it's a path (C:\Users\JOHNSM~1)
void AddVariable(const wchar_t* name, const char* as, bool part)
{
    wchar_t value[MAX_PATH * 2];
    DWORD length = GetEnvironmentVariableW(name, value, MAX_PATH * 2);
    if (length == 0 || length >= MAX_PATH * 2)
    {
        return;
    }

    auto add = [as, part](const wchar_t* wide)
    {
        for (UINT page : {CP_ACP, CP_UTF8})
        {
            char text[MAX_PATH * 4];
            int made = WideCharToMultiByte(page, 0, wide, -1, text, sizeof(text), nullptr, nullptr);
            if (made > 1)
            {
                AddNeedle(std::string(text, static_cast<size_t>(made - 1)), as, part);
            }
        }
    };
    add(value);
    wchar_t shortForm[MAX_PATH * 2];
    DWORD made = part ? 0 : GetShortPathNameW(value, shortForm, MAX_PATH * 2);
    if (made > 0 && made < MAX_PATH * 2)
    {
        add(shortForm);
    }
}
#endif

// ------------------------------------------------------------------------------------------------------------------ helpers
// "45 s", "16 min 05 s", "2 h 03 min"
void FormatDuration(double seconds, char* out, size_t size)
{
    long whole = seconds > 0.0 ? static_cast<long>(seconds + 0.5) : 0;
    if (whole < 90)
    {
        std::snprintf(out, size, "%ld s", whole);
    }
    else if (whole < 3600)
    {
        std::snprintf(out, size, "%ld min %02ld s", whole / 60, whole % 60);
    }
    else
    {
        std::snprintf(out, size, "%ld h %02ld min", whole / 3600, whole % 3600 / 60);
    }
}

bool StartsWith(const char* text, const char* start)
{
    return std::strncmp(text, start, std::strlen(start)) == 0;
}

// Written as the crash report writes (no locks, no memory): the handle of the log itself
void RawWrite(const char* text)
{
#if defined(_WIN32)
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
#else
    ssize_t written = write(2, text, std::strlen(text));
    (void)written;
#endif
}

// ------------------------------------------------------------------------------------------------------------- the frames
using Clock = std::chrono::steady_clock;

// Frames timed while the game is played: their time, the slowest, and the threads' work in them
struct Span
{
    double seconds;
    u64 frames;
    double slowest;
    u64 gameNanoseconds;
    u64 waitedNanoseconds;
    u64 vuNanoseconds;
    u64 renderNanoseconds;
};
Span g_Minute = {};
Span g_Play = {};
// The share of the play below the rate chosen, a second at a time: the second being counted, the seconds counted (with a rate
// to keep) and those below 95% of it, and the rate last chosen
double g_SecondSeconds = 0.0;
u64 g_SecondFrames = 0;
double g_CountedSeconds = 0.0;
double g_SlowSeconds = 0.0;
s32 g_Rate = 0;
// The last frame's time, whether it was played and the counters then
Clock::time_point g_LastFrame{};
bool g_LastInPlay = false;
u64 g_LastGame = 0;
u64 g_LastWaited = 0;
u64 g_LastVu = 0;
u64 g_LastRender = 0;
// Seconds of play a performance line ($TWIN_PERFORMANCE_EVERY)
double g_PerformanceEvery = 60.0;
std::atomic<bool> g_Ended{false};
bool g_LogToFile = false;

// A frame's work, each thread's (as the threads are now)
void FormatWork(const Span& span, char* out, size_t size)
{
    double frames = span.frames != 0 ? static_cast<double>(span.frames) : 1.0;
    double waited = static_cast<double>(span.waitedNanoseconds) / frames / 1e6;
    double game = static_cast<double>(span.gameNanoseconds) / frames / 1e6 - waited;
    double vu = static_cast<double>(span.vuNanoseconds) / frames / 1e6;
    double render = static_cast<double>(span.renderNanoseconds) / frames / 1e6;
    switch (NativeGraphics::ThreadsNow())
    {
    case NativeGraphics::Threads::One:
        std::snprintf(out, size, "the game with VU1 and the GS %.1f ms", game + waited);
        break;
    case NativeGraphics::Threads::TwoVuOnGame:
        std::snprintf(out, size, "the game with VU1 %.1f ms (and %.1f ms waiting), the render thread %.1f ms", game, waited, render);
        break;
    case NativeGraphics::Threads::TwoVuOnRender:
        std::snprintf(out, size, "the game %.1f ms (and %.1f ms waiting), the render thread with VU1 %.1f ms", game, waited, render);
        break;
    case NativeGraphics::Threads::Three:
        std::snprintf(out, size, "the game %.1f ms (and %.1f ms waiting), VU1 %.1f ms, the render thread %.1f ms", game, waited, vu,
                      render);
        break;
    }
}

// The session's summary: its time, its play's frame rate, the share below the rate chosen, the slowest frame, the threads' work
void FormatSummary(char* out, size_t size)
{
    char total[32];
    FormatDuration(Native::SessionSeconds(), total, sizeof(total));
    if (g_Play.frames == 0 || g_Play.seconds < 1.0)
    {
        std::snprintf(out, size, "session: %s, none of it played", total);
        return;
    }

    char played[32];
    char below[96] = "";
    char work[192];
    FormatDuration(g_Play.seconds, played, sizeof(played));
    if (g_CountedSeconds > 0.0 && g_Rate > 0)
    {
        std::snprintf(below, sizeof(below), ", %.0f%% of it below %d fps (95%% of the %d chosen)",
                      100.0 * g_SlowSeconds / g_CountedSeconds, g_Rate * 95 / 100, g_Rate);
    }

    FormatWork(g_Play, work, sizeof(work));
    std::snprintf(out, size, "session: %s, %s of it played at %.1f fps on average%s, the slowest frame %.0f ms; a frame's work: %s",
                  total, played, static_cast<double>(g_Play.frames) / g_Play.seconds, below, g_Play.slowest * 1000.0, work);
}

void LogPerformance()
{
    char played[32];
    char work[192];
    FormatDuration(g_Minute.seconds, played, sizeof(played));
    FormatWork(g_Minute, work, sizeof(work));
    const char* chunk = NativeUi::TrailChunk();
    Native::Log("performance: %.1f fps over %s of play, the slowest frame %.0f ms; a frame's work: %s; the player in %s",
                static_cast<double>(g_Minute.frames) / g_Minute.seconds, played, g_Minute.slowest * 1000.0, work,
                chunk != nullptr ? chunk : "?");
}

// --------------------------------------------------------------------------------------------------------------- the start
void LogSystem()
{
#if defined(_WIN32)
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    using GetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOEXW*);
    auto getVersion = reinterpret_cast<GetVersionFunction>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    OSVERSIONINFOEXW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    if (getVersion == nullptr || getVersion(&version) != 0)
    {
        version = {};
    }

    const unsigned long major = version.dwMajorVersion;
    const unsigned long minor = version.dwMinorVersion;
    const char* name = version.wProductType != VER_NT_WORKSTATION && major != 0 ? "Windows Server"
                       : major == 10 && version.dwBuildNumber >= 22000      ? "Windows 11"
                       : major == 10                                        ? "Windows 10"
                       : major == 6 && minor == 3                           ? "Windows 8.1"
                       : major == 6 && minor == 2                           ? "Windows 8"
                       : major == 6 && minor == 1                           ? "Windows 7"
                                                                            : "Windows";
    // An x64 program on an Arm64 Windows runs emulated
    using MachineFunction = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    auto machineOf = reinterpret_cast<MachineFunction>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2")));
    USHORT process = 0;
    USHORT machine = 0;
    bool arm = machineOf != nullptr && machineOf(GetCurrentProcess(), &process, &machine) && machine == 0xAA64;
    // Wine (and Proton, Wine's) says it's there in ntdll
    using WineVersionFunction = const char* (*)();
    using WineHostFunction = void (*)(const char**, const char**);
    auto wine = reinterpret_cast<WineVersionFunction>(reinterpret_cast<void*>(GetProcAddress(ntdll, "wine_get_version")));
    auto wineHost = reinterpret_cast<WineHostFunction>(reinterpret_cast<void*>(GetProcAddress(ntdll, "wine_get_host_version")));
    std::string under;
    if (wine != nullptr)
    {
        const char* system = nullptr;
        const char* release = nullptr;
        if (wineHost != nullptr)
        {
            wineHost(&system, &release);
        }

        under = std::string(", under Wine ") + (wine() != nullptr ? wine() : "?") +
                (system != nullptr ? std::string(" on ") + system : "") + (release != nullptr ? std::string(" ") + release : "");
    }
    else
    {
        under = ", not under Wine";
    }

    Native::Log("system: %s (%lu.%lu.%lu)%s%s", name, major, minor, static_cast<unsigned long>(version.dwBuildNumber),
                arm ? ", the program emulated on Arm64" : "", under.c_str());
#else
    Native::Log("system: %s", SDL_GetPlatform());
#endif
}

void LogProcessor()
{
    char brand[49] = "?";
#if defined(__x86_64__) || defined(__i386__)
    unsigned int registers[4] = {};
    if (__get_cpuid(0x80000000u, &registers[0], &registers[1], &registers[2], &registers[3]) && registers[0] >= 0x80000004u)
    {
        unsigned int words[12] = {};
        for (unsigned int leaf = 0; leaf < 3; leaf++)
        {
            __get_cpuid(0x80000002u + leaf, &words[leaf * 4], &words[leaf * 4 + 1], &words[leaf * 4 + 2], &words[leaf * 4 + 3]);
        }

        std::memcpy(brand, words, 48);
        brand[48] = '\0';
    }
#endif
    // The brand's padding taken off
    std::string name = brand;
    name.erase(0, name.find_first_not_of(' '));
    name.erase(name.find_last_not_of(' ') + 1);
    int threads = SDL_GetNumLogicalCPUCores();
    char cores[64] = "";
    double totalGb = SDL_GetSystemRAM() / 1024.0;
    double freeGb = -1.0;
#if defined(_WIN32)
    // The cores (a hybrid processor's performance cores: the highest efficiency class) and their threads
    DWORD size = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
    std::vector<u8> buffer(size);
    auto* processors = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
    if (size != 0 && GetLogicalProcessorInformationEx(RelationProcessorCore, processors, &size))
    {
        int count = 0;
        int counted = 0;
        int fastest = 0;
        BYTE most = 0;
        for (int pass = 0; pass < 2; pass++)
        {
            for (DWORD at = 0; at < size;)
            {
                const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + at);
                if (pass == 0)
                {
                    count++;
                    most = std::max(most, info->Processor.EfficiencyClass);
                    for (WORD group = 0; group < info->Processor.GroupCount; group++)
                    {
                        counted += __builtin_popcountll(static_cast<unsigned long long>(info->Processor.GroupMask[group].Mask));
                    }
                }
                else if (info->Processor.EfficiencyClass == most)
                {
                    fastest++;
                }

                at += info->Size;
            }
        }

        threads = counted > 0 ? counted : threads;
        if (most > 0)
        {
            std::snprintf(cores, sizeof(cores), "%d cores (%d of them performance cores)", count, fastest);
        }
        else
        {
            std::snprintf(cores, sizeof(cores), "%d cores", count);
        }
    }

    MEMORYSTATUSEX memory = {};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
    {
        totalGb = static_cast<double>(memory.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
        freeGb = static_cast<double>(memory.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
#endif
    char freeText[32] = "";
    if (freeGb >= 0.0)
    {
        std::snprintf(freeText, sizeof(freeText), ", %.1f GB free", freeGb);
    }

    Native::Log("processor: %s, %s%s%d threads, %s; memory %.1f GB%s", name.c_str(), cores, cores[0] != '\0' ? ", " : "", threads,
                SDL_HasAVX2() ? "AVX2" : SDL_HasAVX() ? "AVX, no AVX2" : "no AVX", totalGb, freeText);
}

// The environment's TWIN_ and TWINSANITY_ variables (the test switches, the one-thread .bat's), in order
void LogVariables()
{
#if defined(_WIN32)
    char** variables = _environ;
#else
    char** variables = environ;
#endif
    auto startsWith = [](const char* text, const char* start)
    {
        for (; *start != '\0'; text++, start++)
        {
            if (Lower(*text) != Lower(*start))
            {
                return false;
            }
        }

        return true;
    };
    std::vector<std::string> set;
    for (char** variable = variables; variable != nullptr && *variable != nullptr; variable++)
    {
        const char* text = *variable;
        if (startsWith(text, "twin_") || startsWith(text, "twinsanity_"))
        {
            set.push_back(std::strchr(text, ' ') != nullptr ? std::string("\"") + text + "\"" : text);
        }
    }

    std::sort(set.begin(), set.end());
    std::string line;
    for (const std::string& one : set)
    {
        line += (line.empty() ? "" : " ") + one;
    }

    Native::Log("variables: %s", set.empty() ? "no TWIN_ ones set" : line.c_str());
}

// The mods folder's files (their paths in it, the first ones)
void LogMods()
{
    namespace fs = std::filesystem;
    constexpr size_t MostNamed = 20;
    std::error_code error;
    fs::path folder = fs::path(Native::SettingsFolder()) / "mods";
    if (!fs::is_directory(folder, error))
    {
        Native::Log("mods: none (no mods folder)");
        return;
    }

    std::vector<std::string> files;
    fs::recursive_directory_iterator it(folder, error);
    for (; !error && it != fs::recursive_directory_iterator(); it.increment(error))
    {
        if (it->is_regular_file(error))
        {
            files.push_back(it->path().lexically_relative(folder).generic_string());
        }
    }

    std::sort(files.begin(), files.end());
    std::string named;
    for (size_t i = 0; i < files.size() && i < MostNamed; i++)
    {
        named += (i == 0 ? ": " : ", ") + files[i];
    }

    if (files.size() > MostNamed)
    {
        named += ", and " + std::to_string(files.size() - MostNamed) + " more";
    }

    Native::Log("mods: %zu file%s in the mods folder%s", files.size(), files.size() == 1 ? "" : "s", named.c_str());
}

void LogTexturePack()
{
    namespace fs = std::filesystem;
    std::error_code error;
    const char* overridden = std::getenv("TWIN_TEXTURES_DIR");
    fs::path root = overridden != nullptr ? fs::path(overridden) : fs::path(Native::SettingsFolder()) / "textures";
    bool there = fs::is_directory(root, error);
    // (Its replacements are counted as it's read: "texture pack: N replacement textures", hw/replace.cpp)
    Native::Log("texture pack: %s, %s", NativeUi::IsOn(NativeUi::OptionTexturePack) ? "on" : "off",
                there ? "a textures folder is there" : "none installed (no textures folder)");
}

// How the last session ended, from the end of its log: its last line of quitting, exiting, an error, a crash or a hang (none:
// closed by force, the computer stopped, or a hang the hang watch didn't see)
void LogLastSession()
{
    const char* name = g_LogToFile ? "log.previous.txt" : "log.txt";
    std::ifstream file(Native::SettingsFolder() + "/" + name, std::ios::binary);
    if (!file)
    {
        Native::Log("the last session: none in the settings folder's %s (the first start%s)", name,
                    g_LogToFile ? "" : ", or its logs went elsewhere");
        return;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    constexpr std::streamoff HeadBytes = 4096;
    constexpr std::streamoff TailBytes = 256 * 1024;
    std::string head(static_cast<size_t>(std::min(size, HeadBytes)), '\0');
    file.seekg(0);
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    const std::streamoff from = size > TailBytes ? size - TailBytes : 0;
    std::string tail(static_cast<size_t>(size - from), '\0');
    file.seekg(from);
    file.read(tail.data(), static_cast<std::streamsize>(tail.size()));

    std::string version = "an unknown version";
    const char* VersionLine = "] Crash Twinsanity ";
    if (size_t at = head.find(VersionLine); at != std::string::npos)
    {
        size_t start = at + std::strlen(VersionLine);
        version = head.substr(start, head.find_first_of("\r\n", start) - start);
    }

    enum class End
    {
        None,
        Quit,
        Exit,
        Crash,
        Error,
        Hang,
    } end = End::None;
    std::string said;
    double seconds = -1.0;
    for (size_t at = 0; at < tail.size();)
    {
        size_t next = tail.find('\n', at);
        std::string line = tail.substr(at, next == std::string::npos ? std::string::npos : next - at);
        at = next == std::string::npos ? tail.size() : next + 1;
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        // "[native] [  12.345] text", or the crash report's "[native] text"
        const char* text = line.c_str();
        double time = 0.0;
        int skipped = 0;
        if (std::sscanf(text, "[native] [%lf] %n", &time, &skipped) == 1 && skipped > 0)
        {
            seconds = time;
            text += skipped;
        }
        else if (StartsWith(text, "[native] "))
        {
            text += std::strlen("[native] ");
        }
        else
        {
            continue;
        }

        // The hang watch's "went on" takes its hang back
        if (StartsWith(text, "hang: the game's thread went on"))
        {
            end = End::None;
            said.clear();
            continue;
        }

        End kind = StartsWith(text, "quit")                             ? End::Quit
                   : StartsWith(text, "exit: ")                         ? End::Exit
                   : StartsWith(text, "fatal exception")                ? End::Crash
                   : StartsWith(text, "fatal: ")                        ? End::Error
                   : StartsWith(text, "hang: the game's thread hasn't") ? End::Hang
                                                                        : End::None;
        if (kind != End::None)
        {
            end = kind;
            said = text;
        }
    }

    constexpr size_t MostSaid = 180;
    if (said.size() > MostSaid)
    {
        said = said.substr(0, MostSaid) + "...";
    }

    char lasted[32] = "?";
    if (seconds >= 0.0)
    {
        FormatDuration(seconds, lasted, sizeof(lasted));
    }

    const char* how = end == End::Quit    ? "ended by its window"
                      : end == End::Exit  ? "ended"
                      : end == End::Crash ? "ended in a crash"
                      : end == End::Error ? "ended on an error"
                      : end == End::Hang  ? "hung"
                                          : "ended with no last line: closed by force, a hang, or the computer stopped";
    Native::Log("the last session (%s): %s for %s, %s%s%s%s", name, version.c_str(), lasted, how, said.empty() ? "" : ": \"",
                said.c_str(), said.empty() ? "" : "\"");
}

// abort(): the program ends there (the C library's own abort, a failed check's)
void OnAbort(int)
{
    RawWrite("[native] fatal: the program aborted itself\n");
    Native::WriteCrashSessionEnd(RawWrite);
}
}

// ----------------------------------------------------------------------------------------------------------- the interface
void Native::StartLogPrivacy()
{
    if (g_PrivacyReady)
    {
        return;
    }

    g_PrivacyReady = true;
#if defined(_WIN32)
    AddVariable(L"USERPROFILE", "%USERPROFILE%", false);
    AddVariable(L"USERNAME", "%USERNAME%", true);
    AddVariable(L"COMPUTERNAME", "%COMPUTERNAME%", true);
#else
    if (const char* home = std::getenv("HOME"))
    {
        AddNeedle(home, "$HOME", false);
    }

    if (const char* user = std::getenv("USER"))
    {
        AddNeedle(user, "$USER", true);
    }
#endif
    // The longest first: the profile folder's path before a name in it
    std::stable_sort(g_Needles.begin(), g_Needles.end(),
                     [](const Needle& a, const Needle& b) { return a.text.size() > b.text.size(); });
}

std::string Native::WithoutProfile(const std::string& text)
{
    StartLogPrivacy();
    std::string result = text;
    for (const Needle& needle : g_Needles)
    {
        const std::string& find = needle.text;
        for (size_t at = 0; at + find.size() <= result.size();)
        {
            if (!MatchesAt(result, at, find))
            {
                at++;
                continue;
            }

            const size_t end = at + find.size();
            const char before = at > 0 ? result[at - 1] : ' ';
            const char after = end < result.size() ? result[end] : ' ';
            // The profile folder as a path's start (not inside a longer name), a user's or computer's name as a path's whole part
            bool whole = needle.part ? Separator(before) && !NameCharacter(after)
                                     : !NameCharacter(before) && !NameCharacter(after);
            if (!whole)
            {
                at++;
                continue;
            }

            result.replace(at, find.size(), needle.as);
            at += std::strlen(needle.as);
        }
    }

    return result;
}

std::string Native::DeviceNameForLog(const char* name)
{
    if (name == nullptr)
    {
        return "?";
    }

    // "Ann's AirPods", "Ann’s": the word before a possessive is a person's name more often than not
    std::string text = name;
    for (size_t at = 0; at < text.size(); at++)
    {
        size_t length = text.compare(at, 2, "'s") == 0 ? 2 : text.compare(at, 4, "\xE2\x80\x99s") == 0 ? 4 : 0;
        if (length == 0 || (at + length < text.size() && NameCharacter(text[at + length])))
        {
            continue;
        }

        size_t start = at;
        while (start > 0 && text[start - 1] != ' ' && text[start - 1] != '(' && text[start - 1] != '"')
        {
            start--;
        }

        if (start < at)
        {
            text.replace(start, at - start, "someone");
            at = start + std::strlen("someone") + length - 1;
        }
    }

    return WithoutProfile(text);
}

void Native::SetLogToFile(bool toFile)
{
    g_LogToFile = toFile;
}

void Native::LogSessionStart(int argc, char** argv)
{
    // Every way out says how the session ended: the C library's exit without a line of its own (a test's), abort()
    std::atexit([] { Native::EndSession("exit: the program ended"); });
    std::signal(SIGABRT, OnAbort);
    if (const char* every = std::getenv("TWIN_PERFORMANCE_EVERY"); every != nullptr && std::atof(every) > 0.0)
    {
        g_PerformanceEvery = std::atof(every);
    }

    std::time_t now = std::time(nullptr);
    std::tm local{};
    LocalTime(now, &local);
    char date[64];
    std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &local);
    Log("started %s (local time)", date);
    LogSystem();
    // The arguments (not the program's own path)
    std::string arguments;
    for (int i = 1; i < argc; i++)
    {
        std::string argument = argv[i];
        bool quoted = argument.empty() || argument.find(' ') != std::string::npos;
        arguments += (i > 1 ? " " : "") + (quoted ? "\"" + argument + "\"" : argument);
    }

    Log("command line: %s", argc > 1 ? arguments.c_str() : "none");
    LogProcessor();
}

void Native::LogSessionSettings()
{
    NativeUi::LogOptions();
    NativeUi::LogResolution();
    Log("threads: %s", NativeGraphics::ThreadsAskedText());
    LogVariables();
    LogMods();
    LogTexturePack();
    LogLastSession();
}

void Native::SessionFrame(u64 gameWorkNanoseconds)
{
    const Clock::time_point now = Clock::now();
    GameController* controller = G_GameController;
    const u32 state = controller != nullptr ? controller->State() : ~0u;
    // Played: the play itself and the cutscenes (not the menus, the pause, the loadings, the movies)
    const bool inPlay = state == GameController::StatePlaying || state == GameController::StateWatching;
    const u64 waited = NativeGraphics::RenderWaitNanoseconds();
    const u64 vu = NativeGraphics::VuStageBusyNanoseconds();
    const u64 render = NativeGraphics::RenderBusyNanoseconds();
    if (inPlay && g_LastInPlay && g_LastFrame != Clock::time_point{})
    {
        const double seconds = std::chrono::duration<double>(now - g_LastFrame).count();
        for (Span* span : {&g_Minute, &g_Play})
        {
            span->seconds += seconds;
            span->frames++;
            span->slowest = std::max(span->slowest, seconds);
            span->gameNanoseconds += gameWorkNanoseconds - g_LastGame;
            span->waitedNanoseconds += waited - g_LastWaited;
            span->vuNanoseconds += vu - g_LastVu;
            span->renderNanoseconds += render - g_LastRender;
        }

        g_SecondSeconds += seconds;
        g_SecondFrames++;
        if (g_SecondSeconds >= 1.0)
        {
            g_Rate = NativeUi::FrameRate();
            if (g_Rate > 0)
            {
                g_CountedSeconds += g_SecondSeconds;
                if (static_cast<double>(g_SecondFrames) / g_SecondSeconds < g_Rate * 0.95)
                {
                    g_SlowSeconds += g_SecondSeconds;
                }
            }

            g_SecondSeconds = 0.0;
            g_SecondFrames = 0;
        }

        if (g_Minute.seconds >= g_PerformanceEvery)
        {
            LogPerformance();
            g_Minute = {};
        }
    }

    g_LastFrame = now;
    g_LastInPlay = inPlay;
    g_LastGame = gameWorkNanoseconds;
    g_LastWaited = waited;
    g_LastVu = vu;
    g_LastRender = render;
}

void Native::EndSession(const char* format, ...)
{
    if (g_Ended.exchange(true))
    {
        return;
    }

    char how[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(how, sizeof(how), format, arguments);
    va_end(arguments);
    char summary[600];
    FormatSummary(summary, sizeof(summary));
    Log("%s", summary);
    char lasted[32];
    FormatDuration(SessionSeconds(), lasted, sizeof(lasted));
    Log("%s after %s; %s", how, lasted, NativeUi::TrailNow().c_str());
    std::fflush(stdout);
    std::fflush(stderr);
}

void Native::EndSessionSummary()
{
    if (g_Ended.exchange(true))
    {
        return;
    }

    char summary[600];
    FormatSummary(summary, sizeof(summary));
    Log("%s", summary);
}

void Native::WriteCrashSessionEnd(void (*write)(const char* text))
{
    if (g_Ended.exchange(true))
    {
        return;
    }

    char summary[600];
    char line[640];
    FormatSummary(summary, sizeof(summary));
    std::snprintf(line, sizeof(line), "[native] %s; it ended there\n", summary);
    write(line);
}
