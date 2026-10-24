// The machine: start-up, the console's language and clock (the PS2's OSD settings), memory, time, the I/O processor, and the
// native build's settings and log
#include "graphics/renderthread.h"
#include "graphics/window.h"
#include "native.h"
#include "ui/nativeui.h"
#include "platform/io.h"
#include "platform/memory.h"
#include "platform/sound.h"
#include "platform/system.h"
#include "platform/time.h"
#include "retail/libc.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------- settings and log
namespace
{
Native::Settings g_Settings = {nullptr, true, false};
std::string g_DiscImageSetting;
// The disc image is the one remembered (disc_image.txt) or picked now: one that turns out not to be the game is forgotten
bool g_DiscImageRemembered = false;

std::string SavedDiscImage()
{
    std::ifstream file(Native::SettingsFolder() + "/disc_image.txt");
    std::string path;
    std::getline(file, path);
    // Portable mode keeps an image in the program's folder relative to it (portable.cpp)
    return Native::DiscImageFromSetting(path);
}

// disc_image.txt written (an image in the program's folder relative to it in portable mode), unless it says that already
void RememberDiscImage(const std::string& image)
{
    std::string setting = Native::DiscImageSetting(image);
    std::string saved;
    std::getline(std::ifstream(Native::SettingsFolder() + "/disc_image.txt"), saved);
    if (saved != setting)
    {
        std::error_code ignored;
        std::filesystem::create_directories(Native::SettingsFolder(), ignored);
        std::ofstream(Native::SettingsFolder() + "/disc_image.txt") << setting << '\n';
    }
}

// local.json's "disc_image" (the decomp's own settings file, in the working folder)
std::string DiscImageFromLocalJson()
{
    std::ifstream file("local.json");
    if (!file)
    {
        return {};
    }

    std::stringstream text;
    text << file.rdbuf();
    std::string json = text.str();
    size_t key = json.find("\"disc_image\"");
    if (key == std::string::npos)
    {
        return {};
    }

    size_t open = json.find('"', json.find(':', key) + 1);
    size_t close = json.find('"', open + 1);
    return open == std::string::npos || close == std::string::npos ? std::string() : json.substr(open + 1, close - open - 1);
}
}

const Native::Settings& Native::GetSettings()
{
    return g_Settings;
}

void Native::ParseArguments(int& argc, char** argv)
{
    int kept = 1;
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--iso") == 0 && i + 1 < argc)
        {
            g_DiscImageSetting = argv[++i];
        }
        else if (std::strcmp(argv[i], "--quiet-stubs") == 0)
        {
            g_Settings.logStubs = false;
        }
        else if (std::strcmp(argv[i], "--snapshots") == 0 && i + 1 < argc)
        {
            Native::SetSnapshotFolder(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--quick") == 0)
        {
            Native::SetQuickStart(true);
        }
        else if (std::strcmp(argv[i], "--headless") == 0)
        {
            g_Settings.headless = true;
        }
        else
        {
            argv[kept++] = argv[i];
        }
    }

    argc = kept;
    argv[argc] = nullptr;
    if (g_DiscImageSetting.empty())
    {
        const char* environment = std::getenv("TWINSANITY_ISO");
        g_DiscImageSetting = environment != nullptr ? environment : DiscImageFromLocalJson();
    }

    if (g_DiscImageSetting.empty())
    {
        g_DiscImageSetting = SavedDiscImage();
        g_DiscImageRemembered = !g_DiscImageSetting.empty();
    }

    g_Settings.discImage = g_DiscImageSetting.empty() ? nullptr : g_DiscImageSetting.c_str();
}

// The disc image picked before (or now): the app keeps it in its settings (macOS: ~/Library/Application Support/Crash
// Twinsanity/disc_image.txt; elsewhere $XDG_CONFIG_HOME or ~/.config/crash-twinsanity/), and asks for it with the system's file
// dialog the first time it's launched without one
std::string Native::SettingsFolder()
{
#if defined(_WIN32)
    // Windows: %APPDATA%\Crash Twinsanity, or in portable mode the User folder beside the program (portable.cpp)
    return PortableMode() ? PortableUserFolder() : AppDataSettingsFolder();
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : ".") + "/Library/Application Support/Crash Twinsanity";
#else
    const char* home = std::getenv("HOME");
    const char* config = std::getenv("XDG_CONFIG_HOME");
    return config != nullptr ? std::string(config) + "/crash-twinsanity"
                             : std::string(home != nullptr ? home : ".") + "/.config/crash-twinsanity";
#endif
}

std::string Native::PickDiscImage()
{
    struct Pick
    {
        bool done = false;
        std::string path;
    } pick;
    // $TWIN_TEST_PICK: the tests' pick, without the dialog (empty: cancelled)
    if (const char* test = std::getenv("TWIN_TEST_PICK"))
    {
        pick.path = test;
        pick.done = true;
    }
    else if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        return {};
    }

    static const SDL_DialogFileFilter filters[] = {{"PS2 disc image", "iso;bin;img"}, {"Any file", "*"}};
    if (!pick.done)
    {
        EndPortableStart();
        SDL_ShowOpenFileDialog(
            [](void* user, const char* const* files, int)
            {
                auto* result = static_cast<Pick*>(user);
                if (files != nullptr && files[0] != nullptr)
                {
                    result->path = files[0];
                }
                result->done = true;
            },
            &pick, nullptr, filters, 2, nullptr, false);
    }

    while (!pick.done)
    {
        SDL_Event event;
        SDL_WaitEventTimeout(&event, 50);
    }

    if (!pick.path.empty())
    {
        RememberDiscImage(pick.path);
    }

    return pick.path;
}

void Native::LocalTime(std::time_t seconds, std::tm* local)
{
#if defined(_WIN32)
    localtime_s(local, &seconds);
#else
    localtime_r(&seconds, local);
#endif
}

void Native::SetEnvironment(const char* name, const char* value)
{
#if defined(_WIN32)
    // An empty value removes the variable
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr)
    {
        setenv(name, value, 1);
    }
    else
    {
        unsetenv(name);
    }
#endif
}

void Native::Log(const char* format, ...)
{
    static const auto started = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    va_list arguments;
    va_start(arguments, format);
    std::fprintf(stderr, "[native] [%8.3f] ", seconds);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    va_end(arguments);
}

void Native::Fatal(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::fputs("[native] fatal: ", stderr);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    va_end(arguments);
    std::exit(1);
}

// ---------------------------------------------------------------------------------------------------- the disc and its data
// The build's data (build/native/data.cpp, native/tools/convert_data.py): the retail executable's bytes laid out into it
extern "C" int NativeLoadRetailData(const unsigned char* executable, uint64_t size);

namespace
{
std::string g_DiscImage;

std::string Format(const char* format, ...) __attribute__((format(printf, 1, 2)));
std::string Format(const char* format, ...)
{
    char text[1024];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    return text;
}

// A message box (unless headless: the tests'), as the program has no window yet
void Tell(SDL_MessageBoxFlags kind, const std::string& message)
{
    if (!g_Settings.headless)
    {
        Native::EndPortableStart();
        SDL_ShowSimpleMessageBox(kind, "Crash Twinsanity", message.c_str(), nullptr);
    }
}

[[noreturn]] void DiscImageFatal(const std::string& message)
{
    Tell(SDL_MESSAGEBOX_ERROR, message);
    Native::Fatal("%s", message.c_str());
}

// The disc image opened and checked, its data laid out: "" when it's the PAL disc with the executable this build is for, else
// what's wrong (the image closed again)
std::string CheckDiscImage(const std::string& image)
{
    if (!Native::Disc::Open(image.c_str()))
    {
        std::error_code error;
        if (!std::filesystem::exists(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(image.data()), image.size())), error))
        {
            return Format("The disc image isn't where it was: %s (moved, renamed or deleted?).", image.c_str());
        }

        return Format("%s can't be read as a disc image.", image.c_str());
    }

    Native::Disc::Entry executable;
    if (!Native::Disc::Find("SLES_525.68", &executable))
    {
        Native::Disc::Close();
        return Format("%s isn't the PAL Crash Twinsanity disc (SLES-52568): there's no SLES_525.68 on it.", image.c_str());
    }

    std::vector<u8> bytes(executable.size);
    if (Native::Disc::Read(executable.offset, bytes.data(), executable.size) != executable.size)
    {
        Native::Disc::Close();
        return Format("SLES_525.68 can't be read from %s.", image.c_str());
    }

    switch (NativeLoadRetailData(bytes.data(), bytes.size()))
    {
    case 0:
        return {};
    case 1:
        Native::Disc::Close();
        return Format("%s is another version of Crash Twinsanity, or a changed copy: this program needs the PAL release "
                      "(Europe/Australia, SLES-52568), whose SLES_525.68 has the SHA-1 "
                      "d41b8d53f733f930cf03d8ea944bec24429fe57c.",
                      image.c_str());
    default:
        Native::Disc::Close();
        return Format("The game's data read from %s isn't what this program was built for.", image.c_str());
    }
}

// Portable mode: the first disc image in the program's folder (.iso, .bin, .img, by name) that's the PAL disc, checked and open; ""
// when there's none
std::string FindDiscImageBesideProgram()
{
    for (const std::string& image : Native::DiscImagesBesideProgram())
    {
        std::string problem = CheckDiscImage(image);
        if (problem.empty())
        {
            Native::Log("portable mode: the disc image found in the program's folder: %s", image.c_str());
            return image;
        }

        Native::Log("portable mode: not the game's disc image: %s", problem.c_str());
    }

    return {};
}
}

void Native::OpenDiscImage()
{
    if (!g_DiscImage.empty())
    {
        return;
    }

    // One given (--iso, $TWINSANITY_ISO, local.json) is the only one tried; one remembered or picked can be picked again: when
    // it's gone (the image moved) or wrong, the player is told why and asked for it with the file dialog
    const char* given = GetSettings().discImage;
    std::string image = given != nullptr ? given : "";
    bool askable = given == nullptr || g_DiscImageRemembered;
    // Portable mode: with none remembered (or the one remembered gone or wrong) the program's folder is looked in first, and an image
    // found there is used without asking
    bool searchable = askable && PortableMode();
    std::string problem;
    for (;;)
    {
        if (image.empty() && searchable)
        {
            searchable = false;
            image = FindDiscImageBesideProgram();
            if (!image.empty())
            {
                break;
            }
        }

        if (image.empty())
        {
            if (!problem.empty())
            {
                Tell(SDL_MESSAGEBOX_WARNING, problem + "\n\nPick your disc image of Crash Twinsanity's PAL release (SLES-52568).");
            }

            image = PickDiscImage();
            if (image.empty())
            {
                DiscImageFatal(problem.empty() ? "No disc image was chosen. Crash Twinsanity needs an image of your own PAL disc "
                                                 "(SLES-52568): pick it when asked, or give --iso PATH, $TWINSANITY_ISO or "
                                                 "local.json's \"disc_image\"."
                                               : problem);
            }
        }

        problem = CheckDiscImage(image);
        if (problem.empty())
        {
            break;
        }

        Log("%s", problem.c_str());
        if (!askable)
        {
            DiscImageFatal(problem);
        }

        // Not remembered any more (PickDiscImage remembers the next one)
        std::error_code ignored;
        std::filesystem::remove(SettingsFolder() + "/disc_image.txt", ignored);
        image.clear();
    }

    // Portable mode remembers it as it keeps it (an image in the program's folder relative to it), one found or remembered before
    if (askable && PortableMode())
    {
        RememberDiscImage(image);
    }

    g_DiscImage = image;
    Log("disc image %s", image.c_str());
}

// ---------------------------------------------------------------------------------------------------------- the system
void Platform::System::Initialise()
{
    Native::OpenDiscImage();
    Native::LogMovieLibraries();

    // The window the game is shown in, at the defaults' size (resolution.cpp; a saved size is applied once it's there); the
    // graphics make their context on it
    if (!Native::GetSettings().headless)
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        {
            Native::Fatal("SDL's video didn't start: %s", SDL_GetError());
        }

        const NativeUi::Resolution defaults = NativeUi::DefaultResolution();
        SDL_Window* window = SDL_CreateWindow("Crash Twinsanity", defaults.windowWidth, defaults.windowHeight,
                                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (window == nullptr)
        {
            Native::Fatal("the window couldn't be made: %s", SDL_GetError());
        }

        NativeGraphicsAttachWindow(window);
        // Portable mode's first start: its progress dialog goes once the window is there
        Native::EndPortableStart();
    }
}

void Platform::System::StartServices()
{
    Platform::Sound::InitialiseRemote();
}

void Platform::System::Exit(s32 status)
{
    // The render thread's work done and its context let go before SDL goes
    NativeGraphics::StopRenderThread();
    SDL_Quit();
    std::exit(status);
}

// The console's language is the machine's: the user's first preferred language the game has, English otherwise (the PS2's
// OSD setting, which the game goes by the same way)
Platform::System::ConsoleLanguage Platform::System::Language()
{
    // The options' language when one's chosen (Options > Gameplay > language)
    switch (NativeUi::GetOption(NativeUi::OptionLanguage))
    {
    case 1:
        return LanguageEnglish;
    case 2:
        return LanguageFrench;
    case 3:
        return LanguageGerman;
    case 4:
        return LanguageItalian;
    case 5:
        return LanguageSpanish;
    default:
        break;
    }

    int count = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&count);
    ConsoleLanguage language = LanguageEnglish;
    for (int i = 0; locales != nullptr && i < count; i++)
    {
        const char* code = locales[i]->language;
        ConsoleLanguage found = std::strcmp(code, "en") == 0   ? LanguageEnglish
                                : std::strcmp(code, "fr") == 0 ? LanguageFrench
                                : std::strcmp(code, "es") == 0 ? LanguageSpanish
                                : std::strcmp(code, "de") == 0 ? LanguageGerman
                                : std::strcmp(code, "it") == 0 ? LanguageItalian
                                : std::strcmp(code, "nl") == 0 ? LanguageDutch
                                : std::strcmp(code, "pt") == 0 ? LanguagePortuguese
                                                               : LanguageJapanese;
        if (found != LanguageJapanese)
        {
            language = found;
            break;
        }
    }

    SDL_free(locales);
    return language;
}

Platform::System::DateTime Platform::System::LocalTime()
{
    std::time_t now = std::time(nullptr);
    std::tm local{};
    Native::LocalTime(now, &local);
    return {static_cast<u16>(local.tm_year + 1900), static_cast<u8>(local.tm_mon + 1), static_cast<u8>(local.tm_mday),
            static_cast<u8>(local.tm_hour),       static_cast<u8>(local.tm_min),     static_cast<u8>(local.tm_sec)};
}

// ---------------------------------------------------------------------------------------------------------- memory
namespace
{
// The PS2 gave the game's pools what its 32 MB left past the executable and the C library's reserve (about 28 MB). Natively
// structs that hold pointers are bigger, so the pools get twice that (native/NATIVE.md)
constexpr u32 Ps2PoolSpace = 0x2000000 - (0x3DDA00 + 0x2800);
constexpr u32 NativePoolScale = 4;
}

u32 Platform::Memory::PoolSpace()
{
    return Ps2PoolSpace * NativePoolScale;
}

void* Platform::Memory::AllocatePool(u32 size)
{
    return RetailLibc::Malloc(size);
}

void Platform::Memory::Synchronise()
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void Platform::Memory::BeforeDeviceWrite(void*, u32)
{
}

void Platform::Memory::WriteBackCache()
{
}

// The retail C library's malloc and free (Sony's dlmalloc on sbrk on the PS2): the host's, 16 byte aligned like the PS2's
// quadwords. The game's own heap manager (game/memory.cpp) works inside what AllocatePool gives it, as on the PS2
void* RetailLibc::Malloc(u32 size)
{
#if defined(_WIN32)
    // The C runtime's malloc is 16 byte aligned on 64 bit Windows, and std::free frees it (posix_memalign isn't there)
    return std::malloc(size != 0 ? size : 16);
#else
    void* memory = nullptr;
    return posix_memalign(&memory, 16, size != 0 ? size : 16) == 0 ? memory : nullptr;
#endif
}

void RetailLibc::Free(void* memory)
{
    std::free(memory);
}

void* RetailLibc::Sbrk(s32)
{
    // Only the PS2's test runs move the break (AllocatePool): the native build has none
    return reinterpret_cast<void*>(-1);
}

// ---------------------------------------------------------------------------------------------------------- time
namespace
{
std::chrono::steady_clock::time_point g_Start;
}

void Platform::Time::Initialise()
{
    g_Start = std::chrono::steady_clock::now();
}

// The PS2's timer 0 at the bus clock / 256 (576000 a second)
u64 Platform::Time::Ticks()
{
    auto elapsed = std::chrono::steady_clock::now() - g_Start;
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) * TicksPerSecond /
           1000000000ull;
}

// ---------------------------------------------------------------------------------------------------------- the I/O processor
// Its modules and heap: the native build's services need none of it
void Platform::Io::Initialise()
{
}

void Platform::Io::Restart(const char*)
{
}

s32 Platform::Io::LoadDriver(const char*)
{
    return 0;
}

void Platform::Io::InitialiseHeap()
{
}

void* Platform::Io::AllocateHeap(s32 size)
{
    return std::malloc(static_cast<size_t>(size));
}

s32 Platform::Io::FreeHeap(void* address)
{
    std::free(address);
    return 0;
}

// The PS2 side's test hook (src/platform/ps2/pads.cpp): buttons held for tools/run_pcsx2.py's --press. Nothing sets it natively
extern "C" volatile u32 g_TestPadButtons = 0;

// The retail C library's way out and the debugger's console (deci2's kputs), which the game's printf goes to a line at a time
extern "C"
{
    [[noreturn]] void ProgramExit(int status)
    {
        Platform::System::Exit(status);
    }

    int kputs(char* text)
    {
        std::fputs(text, stdout);
        std::fflush(stdout);
        return 0;
    }
}
