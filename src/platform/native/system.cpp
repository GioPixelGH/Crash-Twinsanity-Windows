// The machine: start-up, the console's language and clock (the PS2's OSD settings), memory, time, the I/O processor, and the
// native build's settings and log
#include "graphics/renderthread.h"
#include "graphics/window.h"
#include "native.h"
#include "mods.h"
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
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
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

namespace
{
// The program's start, the log's seconds' zero
const std::chrono::steady_clock::time_point g_Started = std::chrono::steady_clock::now();

// A message formatted, the profile folder in its paths written %USERPROFILE% (the logs go to public issues)
std::string LogText(const char* format, va_list arguments)
{
    char text[1024];
    va_list again;
    va_copy(again, arguments);
    int length = std::vsnprintf(text, sizeof(text), format, arguments);
    std::string message;
    if (length >= static_cast<int>(sizeof(text)))
    {
        message.resize(static_cast<size_t>(length) + 1);
        std::vsnprintf(message.data(), message.size(), format, again);
        message.resize(static_cast<size_t>(length));
    }
    else if (length > 0)
    {
        message = text;
    }

    va_end(again);
    return Native::WithoutProfile(message);
}
}

double Native::SessionSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_Started).count();
}

void Native::Log(const char* format, ...)
{
    double seconds = SessionSeconds();
    va_list arguments;
    va_start(arguments, format);
    std::string message = LogText(format, arguments);
    va_end(arguments);
    // A line in one write (the threads' lines don't mix)
    std::fprintf(stderr, "[native] [%8.3f] %s\n", seconds, message.c_str());
}

void Native::Fatal(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    std::string message = LogText(format, arguments);
    va_end(arguments);
    // The session's summary, then the error as the session's last line
    EndSessionSummary();
    std::fprintf(stderr, "[native] fatal: %s\n", message.c_str());
    std::fflush(stderr);
    std::exit(1);
}

// ---------------------------------------------------------------------------------------------------- the disc and its data
// The build's data (build/native/data.cpp, native/tools/convert_data.py): the retail executable's bytes laid out into it, the PAL
// one's or another release's this build has a map for (native/discs, THE LIST 31: the North American v2.00's)
extern "C" int NativeLoadRetailData(const unsigned char* executable, uint64_t size);
extern "C" int NativeLoadRetailDataAs(const char* program, const unsigned char* executable, uint64_t size);
extern "C" const char* NativeOtherRelease(uint32_t index, const char** serial, const char** version);
extern "C" int NativeCheckRetailData(const char* program, const unsigned char* executable, uint64_t size);

namespace
{
std::string g_DiscImage;
// The release whose program the disc has (CheckDiscImage's): its serial, PAL's until a disc is checked
constexpr const char* PalSerial = "SLES-52568";
std::string g_DiscSerial = PalSerial;
// What the program plays, for the messages
constexpr const char* DiscsPlayed = "the PAL release (Europe/Australia, SLES-52568) or the North American version 2.00 (SLUS-20909)";

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

// What's wrong with an image, in the start's words and the options' (CheckDiscImage, CheckOtherDiscImage): not one this program
// plays, and another version of a release's program (or a changed copy)
std::string NotTheGameMessage(const std::string& image)
{
    return Format("%s isn't a Crash Twinsanity disc this program plays (%s): there's no SLES_525.68 or SLUS_209.09 on it.",
                  image.c_str(), DiscsPlayed);
}

std::string OtherVersionMessage(const std::string& image, bool pal, const char* serial, const char* version, const char* program)
{
    if (!pal)
    {
        return Format("%s has another version of %s's program, or a changed copy: this program plays its version %s, whose "
                      "%s has the SHA-1 9637769b520f137a2225eb1a24b35d602ca2c79d (redump.org's disc 13689), and the PAL "
                      "release (Europe/Australia, SLES-52568).",
                      image.c_str(), serial, version, program);
    }

    return Format("%s is another version of Crash Twinsanity, or a changed copy: this program plays %s; the PAL release's "
                  "SLES_525.68 has the SHA-1 d41b8d53f733f930cf03d8ea944bec24429fe57c.",
                  image.c_str(), DiscsPlayed);
}

// The disc image opened and checked, its data laid out: "" when it's the PAL disc with the executable this build is for, or another
// release's this build has a map for (the North American v2.00), else what's wrong (the image closed again)
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

    // The PAL program, else another release's
    const char* program = "SLES_525.68";
    const char* serial = PalSerial;
    const char* version = "1.01";
    Native::Disc::Entry executable;
    bool found = Native::Disc::Find(program, &executable);
    for (uint32_t index = 0; !found; index++)
    {
        program = NativeOtherRelease(index, &serial, &version);
        if (program == nullptr)
        {
            break;
        }

        found = Native::Disc::Find(program, &executable);
    }

    if (!found)
    {
        Native::LogRefusedDisc(image);
        Native::Disc::Close();
        return NotTheGameMessage(image);
    }

    std::vector<u8> bytes(executable.size);
    if (Native::Disc::Read(executable.offset, bytes.data(), executable.size) != executable.size)
    {
        Native::Disc::Close();
        return Format("%s can't be read from %s.", program, image.c_str());
    }

    const bool pal = std::strcmp(serial, PalSerial) == 0;
    switch (pal ? NativeLoadRetailData(bytes.data(), bytes.size()) : NativeLoadRetailDataAs(program, bytes.data(), bytes.size()))
    {
    case 0:
        g_DiscSerial = serial;
        if (!pal)
        {
            Native::Log("disc: %s's program, %s v%s's, played as the PAL release's on its own data (native/discs' map)", program,
                        serial, version);
        }

        return {};
    case 1:
        Native::LogRefusedDisc(image);
        Native::Disc::Close();
        return OtherVersionMessage(image, pal, serial, version, program);
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
            Native::Log("portable mode: the disc image found in the program's folder: %s", Native::LogPath(image).c_str());
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
                Tell(SDL_MESSAGEBOX_WARNING, problem + "\n\nPick your disc image of Crash Twinsanity: " + DiscsPlayed + ".");
            }

            image = PickDiscImage();
            if (image.empty())
            {
                DiscImageFatal(problem.empty() ? std::string("No disc image was chosen. Crash Twinsanity needs an image of your own "
                                                             "disc (") + DiscsPlayed + "): pick it when asked, or give --iso PATH, "
                                                 "$TWINSANITY_ISO or local.json's \"disc_image\"."
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
    Log("disc image %s", LogPath(image).c_str());
    // What it is (discidentity.cpp): the whole image hashed once, on a thread of its own, in a player's start
    IdentifyDisc(image, GetSettings().headless ? DiscHashing::None : DiscHashing::Background);
    // Another release's saves on a card of its own (THE LIST 31)
    UseMemoryCard(DiscMemoryCard().c_str());
    // The mods folder's mods on (mods.cpp): read instead of the disc's files from here, their saves on a card of their own
    Mods::Scan();
    // Its texts' languages by their content, and the voices' (languages.cpp)
    NativeUi::LogLanguages();
}

const std::string& Native::DiscSerial()
{
    return g_DiscSerial;
}

bool Native::PalDisc()
{
    return g_DiscSerial == PalSerial;
}

// ------------------------------------------------------------------------- another disc image chosen in the options (THE LIST 31d)
namespace
{
std::mutex g_AskLock;
bool g_AskOpen = false;
bool g_AskAnswered = false;
std::string g_AskAnswer;
std::string g_NextDiscSerial;

u32 Le32(const u8* bytes)
{
    return static_cast<u32>(bytes[0]) | static_cast<u32>(bytes[1]) << 8 | static_cast<u32>(bytes[2]) << 16 |
           static_cast<u32>(bytes[3]) << 24;
}

std::filesystem::path Utf8Path(const std::string& text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

// A file of an image's root directory (an ISO 9660 image of 2048-byte sectors, as disc.cpp reads) read whole with a handle of
// its own: 1 read, 0 not on it, -1 the image can't be read as one
s32 ReadImageRootFile(const std::string& image, const char* name, std::vector<u8>* bytes)
{
    constexpr u32 Sector = 2048;
    std::ifstream file(Utf8Path(image), std::ios::binary);
    auto readAt = [&file](u64 offset, void* to, size_t size) {
        file.clear();
        file.seekg(static_cast<std::streamoff>(offset));
        file.read(static_cast<char*>(to), static_cast<std::streamsize>(size));
        return file.gcount() == static_cast<std::streamsize>(size);
    };
    u8 descriptor[Sector];
    if (!file || !readAt(16ull * Sector, descriptor, Sector) || descriptor[0] != 1 || std::memcmp(descriptor + 1, "CD001", 5) != 0)
    {
        return -1;
    }

    const u32 rootSize = Le32(descriptor + 156 + 10);
    std::vector<u8> root(rootSize);
    if (rootSize == 0 || rootSize > (1u << 20) || !readAt(static_cast<u64>(Le32(descriptor + 156 + 2)) * Sector, root.data(), rootSize))
    {
        return -1;
    }

    for (u32 at = 0; at < rootSize;)
    {
        const u8 length = root[at];
        if (length == 0)
        {
            // Records don't cross sectors: the rest of this one is empty
            at = (at / Sector + 1) * Sector;
            continue;
        }

        if (at + 33 > rootSize || at + length > rootSize || length < 33)
        {
            break;
        }

        const u8* record = &root[at];
        std::string recordName(reinterpret_cast<const char*>(record + 33), std::min<u32>(record[32], length - 33u));
        recordName = recordName.substr(0, recordName.find(';'));
        for (char& c : recordName)
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        if ((record[25] & 2) == 0 && recordName == name)
        {
            const u32 size = Le32(record + 10);
            bytes->resize(size);
            return size <= (64u << 20) && readAt(static_cast<u64>(Le32(record + 2)) * Sector, bytes->data(), size) ? 1 : -1;
        }

        at += length;
    }

    return 0;
}
}

void Native::AskForDiscImage()
{
    {
        std::lock_guard lock(g_AskLock);
        if (g_AskOpen)
        {
            return;
        }

        // $TWIN_TEST_PICK: the tests' answer, without the dialog (empty: cancelled)
        if (const char* test = std::getenv("TWIN_TEST_PICK"))
        {
            g_AskAnswer = test;
            g_AskAnswered = true;
            return;
        }

        g_AskOpen = true;
        g_AskAnswered = false;
    }

    // (The lock let go: SDL may answer at once, on this thread)
    static const SDL_DialogFileFilter filters[] = {{"PS2 disc image", "iso;bin;img"}, {"Any file", "*"}};
    static std::string folder;
    std::error_code ignored;
    folder = std::filesystem::is_directory(Utf8Path(g_DiscImage).parent_path(), ignored) ? g_DiscImage : std::string();
    Log("disc image: the file dialog asked for (Options > Game > Disc)");
    SDL_ShowOpenFileDialog(
        [](void*, const char* const* files, int) {
            std::lock_guard lock(g_AskLock);
            g_AskAnswer = files != nullptr && files[0] != nullptr ? files[0] : "";
            g_AskAnswered = true;
            g_AskOpen = false;
        },
        nullptr, SDL_GetKeyboardFocus(), filters, 2, folder.empty() ? nullptr : folder.c_str(), false);
}

bool Native::TakeAskedDiscImage(std::string* image)
{
    std::lock_guard lock(g_AskLock);
    if (!g_AskAnswered)
    {
        return false;
    }

    g_AskAnswered = false;
    *image = g_AskAnswer;
    return true;
}

std::string Native::CheckOtherDiscImage(const std::string& image, std::string* serial)
{
    // The PAL program, else another release's, as CheckDiscImage looks
    const char* program = "SLES_525.68";
    const char* found = PalSerial;
    const char* version = "1.01";
    std::vector<u8> bytes;
    s32 read = ReadImageRootFile(image, program, &bytes);
    for (uint32_t index = 0; read == 0; index++)
    {
        program = NativeOtherRelease(index, &found, &version);
        if (program == nullptr)
        {
            break;
        }

        read = ReadImageRootFile(image, program, &bytes);
    }

    if (read < 0)
    {
        return Format("%s can't be read as a disc image.", image.c_str());
    }

    if (read == 0)
    {
        return NotTheGameMessage(image);
    }

    const bool pal = std::strcmp(found, PalSerial) == 0;
    if (NativeCheckRetailData(program, bytes.data(), bytes.size()) != 0)
    {
        return OtherVersionMessage(image, pal, found, version, program);
    }

    *serial = found;
    return {};
}

bool Native::DiscImageGiven()
{
    return GetSettings().discImage != nullptr && !g_DiscImageRemembered;
}

void Native::ChooseDiscImage(const std::string& image, const std::string& serial)
{
    if (DiscImageGiven())
    {
        return;
    }

    RememberDiscImage(image);
    std::error_code error;
    const bool inUse = std::filesystem::equivalent(Utf8Path(image), Utf8Path(g_DiscImage), error) && !error;
    g_NextDiscSerial = inUse ? std::string() : serial;
    Log("disc image: %s chosen for the next start (%s%s)", LogPath(image).c_str(), serial.c_str(), inUse ? ", the one in use" : "");
}

const std::string& Native::NextDiscSerial()
{
    return g_NextDiscSerial;
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
        // On the screen chosen in the settings (THE LIST 88: resolution.txt's), centred; automatic, or that screen not connected
        // (the log says so), where SDL puts it, the main screen, as before
        int place = SDL_WINDOWPOS_UNDEFINED;
        if (const s32 screen = NativeUi::StartScreen(); screen >= 0)
        {
            int count = 0;
            SDL_DisplayID* displays = SDL_GetDisplays(&count);
            if (displays != nullptr && screen < count)
            {
                place = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(displays[screen]));
            }

            SDL_free(displays);
        }

        SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Crash Twinsanity");
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, place);
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, place);
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, defaults.windowWidth);
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, defaults.windowHeight);
        // Made for the graphics API its pictures are drawn with (THE LIST 72): the setting's (THE LIST 73, graphicsapi.cpp); one
        // that can't be drawn in with it made again for the other API, the setting changed to it (no driver, no OpenGL 3.3, no Vulkan)
        SDL_Window* window = nullptr;
        const bool wanted = NativeGraphicsVulkanWanted();
        std::string failed;
        for (const bool vulkan : {wanted, !wanted})
        {
            if (vulkan && !NativeGraphicsVulkanBuilt())
            {
                continue;
            }

            if (vulkan != wanted)
            {
                Native::Log("graphics: %s can't show the game here (%s): %s instead", wanted ? "Vulkan" : "OpenGL", failed.c_str(),
                            vulkan ? "Vulkan" : "OpenGL");
                NativeGraphicsStartFellBack(vulkan);
            }

            NativeGraphicsStartingWith(vulkan);
            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                                  (vulkan ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL) | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
            window = SDL_CreateWindowWithProperties(properties);
            if (window != nullptr && NativeGraphicsAttachWindow(window))
            {
                break;
            }

            failed = window == nullptr ? std::string("the window couldn't be made: ") + SDL_GetError() : std::string(SDL_GetError());
            if (window != nullptr)
            {
                SDL_DestroyWindow(window);
                window = nullptr;
            }
        }

        if (window == nullptr)
        {
            // (An end in order: graphics_start.txt goes, nothing to take at the next start that this one didn't try)
            Native::Fatal("the window can't show the game: %s (%s)",
                          NativeGraphicsVulkanBuilt() ? "neither OpenGL 3.3 nor Vulkan is there" : "OpenGL 3.3 isn't there",
                          failed.c_str());
        }

        SDL_DestroyProperties(properties);
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
    Native::EndSession("exit: the program ended (status %d)", status);
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
        // The game never exits itself: its C library's abort, a pure virtual call or an exception nothing caught (gcc2.cpp's
        // Terminate) end here. Where from, in the log
        Native::EndSession("exit: the game's C library ended the program (status %d: its abort, a pure virtual call or an "
                           "uncaught exception)",
                           status);
#if defined(_WIN32)
        Native::LogBacktraceHere();
#endif
        Platform::System::Exit(status);
    }

    int kputs(char* text)
    {
        std::fputs(text, stdout);
        std::fflush(stdout);
        return 0;
    }
}
