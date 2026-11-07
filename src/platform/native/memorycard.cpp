// The memory cards (libmc's interface, src/platform/ps2/memorycard.h), for the game's own save manager (src/platform/ps2/saves.cpp,
// which the native build compiles as it is). Port 0's card is always in, formatted, an 8 MB card whose files are the machine's: a
// folder in the app's settings (Native::SettingsFolder()/memorycard), "/BESLES-52568CRASH/..." its saves as on the PS2. Port 1 is
// empty. Each call does its work at once and leaves its result for the next Sync, which the save manager waits on or polls as it
// does a real card's
#include "../ps2/memorycard.h"
#include "native.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
using namespace Platform::MemoryCard;

// libmc's results and its functions' numbers (sceMcFunc*), as Sync reports them
constexpr s32 ResultSucceeded = 0;
constexpr s32 ResultNewCard = -1;
constexpr s32 ResultFull = -3;
constexpr s32 ResultNoEntry = -4;
constexpr s32 ResultNotEmpty = -6;
enum Function : s32
{
    FunctionGetInfo = 1,
    FunctionOpen = 2,
    FunctionClose = 3,
    FunctionRead = 5,
    FunctionWrite = 6,
    FunctionMakeDirectory = 0xB,
    FunctionChangeDirectory = 0xC,
    FunctionGetDirectory = 0xD,
    FunctionDelete = 0xF,
    FunctionFormat = 0x10,
};

// A PS2 card's 8 MB in kilobyte clusters, and what's free of it once its own file system takes its share
constexpr s32 CardClusters = 8192;
constexpr s32 SystemClusters = 192;
constexpr s32 TypePs2 = 2;
// The attributes of a directory entry (sceMcFileAttr*): readable, writable, executable, a file or a directory, closed, exists
constexpr u16 AttributeFile = 0x01 | 0x02 | 0x04 | 0x10 | 0x80 | 0x400 | 0x8000;
constexpr u16 AttributeDirectory = 0x01 | 0x02 | 0x04 | 0x20 | 0x400 | 0x8000;

bool g_Pending = false;
s32 g_PendingFunction = 0;
s32 g_PendingResult = 0;
bool g_CardSeen = false;
std::string g_CurrentDirectory = "/";

struct OpenFile
{
    std::fstream stream;
    fs::path path;
};
std::map<s32, OpenFile> g_Files;
s32 g_NextFile = 0;

s32 Finish(Function function, s32 result)
{
    // $TWIN_MC_TRACE: every call's function and result, for following the game's save code
    static const bool trace = std::getenv("TWIN_MC_TRACE") != nullptr;
    if (trace)
    {
        Native::Log("memory card: function %d result %d (directory %s)", static_cast<s32>(function), result,
                    g_CurrentDirectory.c_str());
    }

    g_Pending = true;
    g_PendingFunction = function;
    g_PendingResult = result;
    return 0;
}

bool CardThere(s32 port, s32 slot)
{
    return port == 0 && slot == 0;
}

// The card's folder in the settings folder: "memorycard", or another while a session mustn't touch the player's saves (the
// level select's: UseMemoryCard)
std::string g_CardName = "memorycard";

fs::path Root()
{
    fs::path root = fs::path(Native::SettingsFolder()) / g_CardName;
    std::error_code ignored;
    fs::create_directories(root, ignored);
    return root;
}

// The card's path on the machine: absolute ones from the card's root, the others from the current directory
fs::path HostPath(const char* name)
{
    std::string path = name[0] == '/' ? std::string(name) : g_CurrentDirectory + "/" + name;
    fs::path result = Root();
    size_t at = 0;
    while (at < path.size())
    {
        size_t next = path.find('/', at);
        std::string part = path.substr(at, next == std::string::npos ? std::string::npos : next - at);
        if (!part.empty() && part != ".")
        {
            result = part == ".." ? result.parent_path() : result / part;
        }

        if (next == std::string::npos)
        {
            break;
        }

        at = next + 1;
    }

    return result;
}

// What the card's files take, counted once and again only after the card's files change (-1: to count): the save manager asks
// GetInfo every frame it has nothing else to do, and walking the folder each time cost the game's thread 0.2-0.4 ms a frame
s32 g_UsedClusters = -1;

s32 UsedClusters()
{
    if (g_UsedClusters >= 0)
    {
        return g_UsedClusters;
    }

    std::error_code error;
    s32 clusters = 0;
    for (auto it = fs::recursive_directory_iterator(Root(), error); !error && it != fs::recursive_directory_iterator();
         it.increment(error))
    {
        // A directory takes two clusters and one more for every two entries, a file what it holds in kilobytes
        clusters += it->is_directory() ? 2 : static_cast<s32>((it->file_size() + 1023) / 1024);
    }

    g_UsedClusters = clusters;
    return clusters;
}

// After a call that may have changed what the card's files take (a file made, emptied or written, a directory made, an entry
// deleted): the next GetInfo counts them again
void CardChanged()
{
    g_UsedClusters = -1;
}

// The PS2's directory entry time: a reserved byte, then second, minute, hour, day, month and the year in 16 bits
void EntryTime(u8* out, fs::file_time_type when)
{
    auto system = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        when - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t seconds = std::chrono::system_clock::to_time_t(system);
    std::tm local{};
    Native::LocalTime(seconds, &local);
    u16 year = static_cast<u16>(local.tm_year + 1900);
    u8 time[8] = {0,
                  static_cast<u8>(local.tm_sec),
                  static_cast<u8>(local.tm_min),
                  static_cast<u8>(local.tm_hour),
                  static_cast<u8>(local.tm_mday),
                  static_cast<u8>(local.tm_mon + 1),
                  static_cast<u8>(year & 0xFF),
                  static_cast<u8>(year >> 8)};
    std::memcpy(out, time, sizeof(time));
}

void FillEntry(DirectoryEntry* entry, const std::string& name, const fs::path& path, bool directory)
{
    std::memset(entry, 0, sizeof(*entry));
    std::error_code error;
    auto when = fs::last_write_time(path, error);
    if (!error)
    {
        EntryTime(entry->created, when);
        EntryTime(entry->modified, when);
    }

    entry->fileSizeByte = directory ? 0 : static_cast<u32>(fs::file_size(path, error));
    entry->attributes = directory ? AttributeDirectory : AttributeFile;
    std::strncpy(reinterpret_cast<char*>(entry->name), name.c_str(), sizeof(entry->name) - 1);
}

// A pattern's match the way the card's does: * any run, ? any character
bool Matches(const char* pattern, const char* name)
{
    if (*pattern == '\0')
    {
        return *name == '\0';
    }

    if (*pattern == '*')
    {
        return Matches(pattern + 1, name) || (*name != '\0' && Matches(pattern, name + 1));
    }

    return *name != '\0' && (*pattern == '?' || *pattern == *name) && Matches(pattern + 1, name + 1);
}
}

s32 Platform::MemoryCard::Initialise()
{
    Root();
    return 0;
}

s32 Platform::MemoryCard::GetInfo(s32 port, s32 slot, s32* type, s32* freeClusters, s32* format)
{
    if (!CardThere(port, slot))
    {
        *type = 0;
        *freeClusters = 0;
        *format = 0;
        return Finish(FunctionGetInfo, -10);
    }

    *type = TypePs2;
    *freeClusters = CardClusters - SystemClusters - UsedClusters();
    *format = 1;
    // A card is new the first time it's looked at (libmc's "changed card", formatted), the same one after
    s32 result = g_CardSeen ? ResultSucceeded : ResultNewCard;
    g_CardSeen = true;
    return Finish(FunctionGetInfo, result);
}

s32 Platform::MemoryCard::Open(s32 port, s32 slot, const char* name, s32 flags)
{
    if (!CardThere(port, slot))
    {
        return Finish(FunctionOpen, ResultNoEntry);
    }

    fs::path path = HostPath(name);
    std::error_code error;
    bool exists = fs::is_regular_file(path, error);
    if (!exists && (flags & Platform::Files::OpenCreate) == 0)
    {
        return Finish(FunctionOpen, ResultNoEntry);
    }

    if (!fs::is_directory(path.parent_path(), error))
    {
        return Finish(FunctionOpen, ResultNoEntry);
    }

    std::ios::openmode mode = std::ios::binary;
    if ((flags & Platform::Files::OpenRead) != 0)
    {
        mode |= std::ios::in;
    }
    if ((flags & Platform::Files::OpenWrite) != 0)
    {
        mode |= std::ios::out;
        if (!exists || (flags & Platform::Files::OpenTruncate) != 0)
        {
            mode |= std::ios::trunc;
        }
    }

    OpenFile file;
    file.path = path;
    if (!exists)
    {
        std::ofstream(path, std::ios::binary).close();
    }

    if ((mode & std::ios::trunc) != 0 || !exists)
    {
        CardChanged();
    }

    file.stream.open(path, mode);
    if (!file.stream)
    {
        return Finish(FunctionOpen, ResultNoEntry);
    }

    s32 handle = g_NextFile++;
    g_Files[handle] = std::move(file);
    return Finish(FunctionOpen, handle);
}

s32 Platform::MemoryCard::Close(s32 file)
{
    auto found = g_Files.find(file);
    if (found == g_Files.end())
    {
        return Finish(FunctionClose, ResultNoEntry);
    }

    found->second.stream.close();
    g_Files.erase(found);
    return Finish(FunctionClose, ResultSucceeded);
}

s32 Platform::MemoryCard::Read(s32 file, void* buffer, s32 size)
{
    auto found = g_Files.find(file);
    if (found == g_Files.end())
    {
        return Finish(FunctionRead, ResultNoEntry);
    }

    found->second.stream.read(static_cast<char*>(buffer), size);
    s32 read = static_cast<s32>(found->second.stream.gcount());
    found->second.stream.clear();
    return Finish(FunctionRead, read);
}

s32 Platform::MemoryCard::Write(s32 file, const void* buffer, s32 size)
{
    auto found = g_Files.find(file);
    if (found == g_Files.end())
    {
        return Finish(FunctionWrite, ResultNoEntry);
    }

    if (UsedClusters() + (size + 1023) / 1024 > CardClusters - SystemClusters)
    {
        return Finish(FunctionWrite, ResultFull);
    }

    found->second.stream.write(static_cast<const char*>(buffer), size);
    found->second.stream.flush();
    CardChanged();
    Native::Log("memory card: %d bytes written to %s", size, found->second.path.filename().string().c_str());
    return Finish(FunctionWrite, found->second.stream ? size : ResultFull);
}

s32 Platform::MemoryCard::MakeDirectory(s32 port, s32 slot, const char* name)
{
    if (!CardThere(port, slot))
    {
        return Finish(FunctionMakeDirectory, ResultNoEntry);
    }

    std::error_code error;
    fs::path path = HostPath(name);
    if (fs::exists(path, error))
    {
        return Finish(FunctionMakeDirectory, ResultNoEntry);
    }

    CardChanged();
    return Finish(FunctionMakeDirectory, fs::create_directory(path, error) ? ResultSucceeded : ResultNoEntry);
}

s32 Platform::MemoryCard::ChangeDirectory(s32 port, s32 slot, const char* directory, char* currentDirectory)
{
    if (!CardThere(port, slot))
    {
        return Finish(FunctionChangeDirectory, ResultNoEntry);
    }

    if (currentDirectory != nullptr)
    {
        std::strncpy(currentDirectory, g_CurrentDirectory.c_str(), 1023);
    }

    std::error_code error;
    if (directory == nullptr || !fs::is_directory(HostPath(directory), error))
    {
        return Finish(FunctionChangeDirectory, ResultNoEntry);
    }

    g_CurrentDirectory = directory[0] == '/' ? std::string(directory) : g_CurrentDirectory + "/" + directory;
    return Finish(FunctionChangeDirectory, ResultSucceeded);
}

// A listing of what matches the name's last part in its directory: "." and ".." first, as the card's has them
s32 Platform::MemoryCard::GetDirectory(s32 port, s32 slot, const char* name, u32, s32 maxEntries, DirectoryEntry* table)
{
    if (!CardThere(port, slot))
    {
        return Finish(FunctionGetDirectory, ResultNoEntry);
    }

    std::string full = name;
    size_t slash = full.rfind('/');
    std::string directory = slash == std::string::npos ? std::string(".") : full.substr(0, slash == 0 ? 1 : slash);
    std::string pattern = slash == std::string::npos ? full : full.substr(slash + 1);
    fs::path path = HostPath(directory.c_str());
    std::error_code error;
    if (!fs::is_directory(path, error))
    {
        // A listing in a directory that isn't there: no such entry, as the card answers (the saves measure a save that isn't
        // there as none by it; 0 entries measured it as an empty save, and writing it failed)
        return Finish(FunctionGetDirectory, ResultNoEntry);
    }

    s32 count = 0;
    auto add = [&](const std::string& entryName, const fs::path& entryPath, bool isDirectory)
    {
        if (count < maxEntries && Matches(pattern.c_str(), entryName.c_str()))
        {
            FillEntry(&table[count++], entryName, entryPath, isDirectory);
        }
    };
    if (path != Root())
    {
        add(".", path, true);
        add("..", path.parent_path(), true);
    }

    for (const auto& entry : fs::directory_iterator(path, error))
    {
        add(entry.path().filename().string(), entry.path(), entry.is_directory());
    }

    return Finish(FunctionGetDirectory, count);
}

s32 Platform::MemoryCard::Delete(s32 port, s32 slot, const char* name)
{
    if (!CardThere(port, slot))
    {
        return Finish(FunctionDelete, ResultNoEntry);
    }

    std::error_code error;
    fs::path path = HostPath(name);
    if (!fs::exists(path, error))
    {
        return Finish(FunctionDelete, ResultNoEntry);
    }

    if (fs::is_directory(path, error) && !fs::is_empty(path, error))
    {
        return Finish(FunctionDelete, ResultNotEmpty);
    }

    CardChanged();
    return Finish(FunctionDelete, fs::remove(path, error) ? ResultSucceeded : ResultNoEntry);
}

// The card is never wiped: a format leaves the saves where they are (the card is the machine's folder, and the game only formats
// one the PS2 found unformatted, which this one never is)
s32 Platform::MemoryCard::Format(s32 port, s32 slot)
{
    return Finish(FunctionFormat, CardThere(port, slot) ? ResultSucceeded : ResultNoEntry);
}

s32 Platform::MemoryCard::Sync(SyncMode, s32* function, s32* result)
{
    if (!g_Pending)
    {
        return SyncNothing;
    }

    g_Pending = false;
    if (function != nullptr)
    {
        *function = g_PendingFunction;
    }
    if (result != nullptr)
    {
        *result = g_PendingResult;
    }

    return SyncFinished;
}

// $TWIN_SAVE_TRACE: the game's save code's steps (savecode.cpp)
void NativeSaveTrace(const char* format, ...)
{
    static const bool trace = std::getenv("TWIN_SAVE_TRACE") != nullptr;
    if (!trace)
    {
        return;
    }

    char line[256];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    Native::Log("save code: %s", line);
}

void Native::UseMemoryCard(const char* name)
{
    if (g_CardName == name)
    {
        return;
    }

    g_CardName = name;
    g_CurrentDirectory = "/";
    CardChanged();
    Log("memory card: the settings folder's %s from now on", name);
}

const char* Native::MemoryCardName()
{
    return g_CardName.c_str();
}
