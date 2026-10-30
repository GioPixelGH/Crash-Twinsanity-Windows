// The disc: the PS2's drive (Platform::Disc) and the native build's reading of the disc image (ISO 9660), which stands for it
#include "native.h"
#include "platform/disc.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
constexpr u32 SectorSize = 2048;
// The primary volume descriptor's sector, and its root directory record's place in it
constexpr u32 VolumeDescriptorSector = 16;
constexpr u32 RootRecordOffset = 156;

#if defined(_WIN32)
// The image's handle: reads give their offset (OVERLAPPED), so the threads reading it don't share a position
HANDLE g_Image = INVALID_HANDLE_VALUE;
#else
int g_Image = -1;
#endif
std::mutex g_Lock;
u32 g_RootSector = 0;
u32 g_RootSize = 0;

u32 Le32(const u8* bytes)
{
    return static_cast<u32>(bytes[0]) | static_cast<u32>(bytes[1]) << 8 | static_cast<u32>(bytes[2]) << 16 |
           static_cast<u32>(bytes[3]) << 24;
}

// A name as ISO 9660 compares it: upper case, without the version
std::string Normalised(const char* name, size_t length)
{
    std::string out;
    for (size_t i = 0; i < length && name[i] != ';'; i++)
    {
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(name[i])));
    }

    // A file without an extension is recorded with a trailing dot
    if (!out.empty() && out.back() == '.')
    {
        out.pop_back();
    }

    return out;
}

// A directory's entry by name: its extent and size, and whether it's a directory

// Mods: a file in the settings folder's mods folder at a disc file's path (any case: "mods/Levels/Earth/Hub/beach.rm2") is read
// instead of the disc's. Each is given a place past any image's end (16 GB on, a GB each: its sectors still 32 bit numbers for
// the I/O processor's reads), which Read takes from the file
constexpr u64 ModBase = 16ull << 30;
constexpr u32 ModSlotShift = 30;
constexpr u32 MostMods = 1024;
std::mutex g_ModLock;
bool g_ModsLooked = false;
bool g_ModsThere = false;
std::filesystem::path g_ModFolder;
std::vector<std::filesystem::path> g_ModFiles;
// By the path's normalised parts: the mod's slot, or -1 for none
std::map<std::string, s32> g_ModSlots;
// The archive's files' mods: each a place of its own past the image's end (and within 4 GB of the archive's start: its files'
// starts are 32 bit), whatever its file has past its size read as zeros
struct ArchiveMod
{
    u64 offset;
    u32 size;
    u32 room;
    std::filesystem::path file;
};
constexpr u32 ArchiveModAlignment = 2048;
std::vector<ArchiveMod> g_ArchiveMods;
std::map<std::string, s32> g_ArchiveModSlots;
u64 g_ImageSize = 0;
u64 g_ArchiveOffset = 0;
u64 g_ArchiveModNext = 0;

std::string Upper(std::string text)
{
    for (char& c : text)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    return text;
}

// The mod for the disc path's parts (upper case, no version), found a part at a time whatever their case
bool FindMod(const std::vector<std::string>& parts, std::filesystem::path* found)
{
    std::filesystem::path at = g_ModFolder;
    std::error_code error;
    for (size_t i = 0; i < parts.size(); i++)
    {
        bool matched = false;
        for (std::filesystem::directory_iterator it(at, error), end; !error && it != end; it.increment(error))
        {
            std::string name = Upper(it->path().filename().string());
            if (!name.empty() && name.back() == '.')
            {
                name.pop_back();
            }

            const bool last = i + 1 == parts.size();
            if (name == parts[i] && (last ? it->is_regular_file(error) : it->is_directory(error)))
            {
                at = it->path();
                matched = true;
                break;
            }
        }

        if (!matched)
        {
            return false;
        }
    }

    *found = at;
    return true;
}

// A disc or archive path's parts as the disc names them (upper case, no version), and their key
std::vector<std::string> PathParts(const char* path, std::string* key)
{
    std::vector<std::string> parts;
    for (const char* part = path; *part != '\0';)
    {
        while (*part == '\\' || *part == '/')
        {
            part++;
        }

        const char* end = part;
        while (*end != '\0' && *end != '\\' && *end != '/')
        {
            end++;
        }

        if (end != part)
        {
            parts.push_back(Normalised(part, static_cast<size_t>(end - part)));
            *key += '/' + parts.back();
        }

        part = end;
    }

    return parts;
}

// Whether there's a mods folder (looked for once)
bool ModsThere()
{
    if (!g_ModsLooked)
    {
        g_ModsLooked = true;
        g_ModFolder = std::filesystem::path(Native::SettingsFolder()) / "mods";
        std::error_code error;
        g_ModsThere = std::filesystem::is_directory(g_ModFolder, error);
        if (g_ModsThere)
        {
            Native::Log("mods: files in %s are read instead of the disc's", Native::LogPath(g_ModFolder.string()).c_str());
        }
    }

    return g_ModsThere;
}

// The disc path's mod, if there's one: its place and size
bool FindModEntry(const char* path, Native::Disc::Entry* entry)
{
    std::lock_guard lock(g_ModLock);
    if (!ModsThere())
    {
        return false;
    }

    std::string key;
    std::vector<std::string> parts = PathParts(path, &key);
    if (parts.empty())
    {
        return false;
    }

    auto known = g_ModSlots.find(key);
    if (known == g_ModSlots.end())
    {
        std::filesystem::path file;
        s32 slot = -1;
        if (g_ModFiles.size() < MostMods && FindMod(parts, &file))
        {
            slot = static_cast<s32>(g_ModFiles.size());
            g_ModFiles.push_back(file);
            Native::Log("mods: %s from %s", key.c_str(), Native::LogPath(file.string()).c_str());
        }

        known = g_ModSlots.emplace(key, slot).first;
    }

    if (known->second < 0)
    {
        return false;
    }

    std::error_code error;
    const u64 size = std::filesystem::file_size(g_ModFiles[static_cast<size_t>(known->second)], error);
    if (error || size >= (1ull << ModSlotShift))
    {
        return false;
    }

    entry->offset = ModBase + (static_cast<u64>(known->second) << ModSlotShift);
    entry->size = static_cast<u32>(size);
    return true;
}

// A read of an archive's file's mod: its file's bytes, zeros past them (to its place's end)
u32 ReadArchiveMod(u64 offset, void* destination, u32 size)
{
    ArchiveMod mod;
    {
        std::lock_guard lock(g_ModLock);
        auto it = std::upper_bound(g_ArchiveMods.begin(), g_ArchiveMods.end(), offset,
                                   [](u64 at, const ArchiveMod& m) { return at < m.offset; });
        if (it == g_ArchiveMods.begin())
        {
            return 0;
        }

        mod = *std::prev(it);
    }

    const u64 at = offset - mod.offset;
    if (at >= mod.room)
    {
        return 0;
    }

    size = static_cast<u32>(std::min<u64>(size, mod.room - at));
    std::memset(destination, 0, size);
    if (at < mod.size)
    {
#if defined(_WIN32)
        FILE* stream = _wfopen(mod.file.wstring().c_str(), L"rb");
#else
        FILE* stream = std::fopen(mod.file.string().c_str(), "rb");
#endif
        if (stream != nullptr)
        {
#if defined(_WIN32)
            const bool placed = _fseeki64(stream, static_cast<long long>(at), SEEK_SET) == 0;
#else
            const bool placed = fseeko(stream, static_cast<off_t>(at), SEEK_SET) == 0;
#endif
            if (placed)
            {
                std::fread(destination, 1, static_cast<size_t>(std::min<u64>(size, mod.size - at)), stream);
            }

            std::fclose(stream);
        }
    }

    return size;
}

// A read of a mod's place
u32 ReadMod(u64 offset, void* destination, u32 size)
{
    std::filesystem::path file;
    {
        std::lock_guard lock(g_ModLock);
        const u64 slot = (offset - ModBase) >> ModSlotShift;
        if (slot >= g_ModFiles.size())
        {
            return 0;
        }

        file = g_ModFiles[static_cast<size_t>(slot)];
    }

#if defined(_WIN32)
    FILE* stream = _wfopen(file.wstring().c_str(), L"rb");
#else
    FILE* stream = std::fopen(file.string().c_str(), "rb");
#endif
    if (stream == nullptr)
    {
        return 0;
    }

    const u64 at = (offset - ModBase) & ((1ull << ModSlotShift) - 1);
#if defined(_WIN32)
    const bool placed = _fseeki64(stream, static_cast<long long>(at), SEEK_SET) == 0;
#else
    const bool placed = fseeko(stream, static_cast<off_t>(at), SEEK_SET) == 0;
#endif
    const u32 read = placed ? static_cast<u32>(std::fread(destination, 1, size, stream)) : 0;
    std::fclose(stream);
    return read;
}

bool FindInDirectory(u32 sector, u32 size, const std::string& name, u32* foundSector, u32* foundSize, bool* directory)
{
    std::string buffer(size, '\0');
    if (Native::Disc::Read(static_cast<u64>(sector) * SectorSize, buffer.data(), size) != size)
    {
        return false;
    }

    const u8* bytes = reinterpret_cast<const u8*>(buffer.data());
    for (u32 at = 0; at < size;)
    {
        u8 length = bytes[at];
        if (length == 0)
        {
            // Records don't cross sectors: the rest of this one is empty
            at = (at / SectorSize + 1) * SectorSize;
            continue;
        }

        const u8* record = bytes + at;
        u8 nameLength = record[32];
        const char* recordName = reinterpret_cast<const char*>(record + 33);
        bool special = nameLength == 1 && (recordName[0] == 0 || recordName[0] == 1);
        if (!special && Normalised(recordName, nameLength) == name)
        {
            *foundSector = Le32(record + 2);
            *foundSize = Le32(record + 10);
            *directory = (record[25] & 2) != 0;
            return true;
        }

        at += length;
    }

    return false;
}
}

bool Native::Disc::Open(const char* path)
{
    std::lock_guard lock(g_Lock);
#if defined(_WIN32)
    if (g_Image != INVALID_HANDLE_VALUE)
    {
        return true;
    }

    // The path is UTF-8 (the command line's, the settings' and SDL's file dialog's)
    int length = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    std::wstring wide(length > 0 ? static_cast<size_t>(length) : 0, wchar_t{});
    if (length <= 0 || MultiByteToWideChar(CP_UTF8, 0, path, -1, wide.data(), length) != length)
    {
        return false;
    }

    g_Image = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (g_Image == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    u8 descriptor[SectorSize];
    if (Native::Disc::Read(static_cast<u64>(VolumeDescriptorSector) * SectorSize, descriptor, SectorSize) != SectorSize ||
        descriptor[0] != 1 || std::memcmp(descriptor + 1, "CD001", 5) != 0)
    {
        CloseHandle(g_Image);
        g_Image = INVALID_HANDLE_VALUE;
        return false;
    }
#else
    if (g_Image >= 0)
    {
        return true;
    }

    g_Image = ::open(path, O_RDONLY);
    if (g_Image < 0)
    {
        return false;
    }

    u8 descriptor[SectorSize];
    if (::pread(g_Image, descriptor, SectorSize, static_cast<off_t>(VolumeDescriptorSector) * SectorSize) != SectorSize ||
        descriptor[0] != 1 || std::memcmp(descriptor + 1, "CD001", 5) != 0)
    {
        ::close(g_Image);
        g_Image = -1;
        return false;
    }
#endif

    // The image's size: the archive's files' mods are placed past it
#if defined(_WIN32)
    LARGE_INTEGER imageSize = {};
    g_ImageSize = GetFileSizeEx(g_Image, &imageSize) ? static_cast<u64>(imageSize.QuadPart) : 0;
#else
    g_ImageSize = static_cast<u64>(::lseek(g_Image, 0, SEEK_END));
#endif
    g_RootSector = Le32(descriptor + RootRecordOffset + 2);
    g_RootSize = Le32(descriptor + RootRecordOffset + 10);
    return true;
}

void Native::Disc::Close()
{
    std::lock_guard lock(g_Lock);
#if defined(_WIN32)
    if (g_Image != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_Image);
        g_Image = INVALID_HANDLE_VALUE;
    }
#else
    if (g_Image >= 0)
    {
        ::close(g_Image);
        g_Image = -1;
    }
#endif
    g_RootSector = 0;
    g_RootSize = 0;
    g_ImageSize = 0;
    g_ArchiveOffset = 0;
    g_ArchiveModNext = 0;
}

bool Native::Disc::Find(const char* path, Entry* entry)
{
    // The device and leading separators go ("cdrom0:\CRASH6\..." as the PS2 side would have made it)
    const char* colon = std::strchr(path, ':');
    if (colon != nullptr)
    {
        path = colon + 1;
    }

    if (FindModEntry(path, entry))
    {
        return true;
    }

    u32 sector = g_RootSector;
    u32 size = g_RootSize;
    bool directory = true;
    const char* part = path;
    while (*part != '\0')
    {
        while (*part == '\\' || *part == '/')
        {
            part++;
        }

        const char* end = part;
        while (*end != '\0' && *end != '\\' && *end != '/')
        {
            end++;
        }

        if (end == part)
        {
            break;
        }

        if (!directory || !FindInDirectory(sector, size, Normalised(part, static_cast<size_t>(end - part)), &sector, &size,
                                           &directory))
        {
            return false;
        }

        part = end;
    }

    if (directory)
    {
        return false;
    }

    entry->offset = static_cast<u64>(sector) * SectorSize;
    entry->size = size;
    if (Native::DetailedLog())
    {
        Native::Log("disc: %s (%u bytes)", path, size);
    }

    return true;
}

u32 Native::Disc::Read(u64 offset, void* destination, u32 size)
{
    if (offset >= ModBase)
    {
        return ReadMod(offset, destination, size);
    }

    if (g_ArchiveModNext != 0 && offset >= g_ImageSize)
    {
        return ReadArchiveMod(offset, destination, size);
    }

    u32 done = 0;
    while (done < size)
    {
#if defined(_WIN32)
        OVERLAPPED at = {};
        u64 position = offset + done;
        at.Offset = static_cast<DWORD>(position);
        at.OffsetHigh = static_cast<DWORD>(position >> 32);
        DWORD read = 0;
        if (!ReadFile(g_Image, static_cast<u8*>(destination) + done, size - done, &read, &at) || read == 0)
        {
            break;
        }
#else
        ssize_t read = ::pread(g_Image, static_cast<u8*>(destination) + done, size - done, static_cast<off_t>(offset + done));
        if (read <= 0)
        {
            break;
        }
#endif

        done += static_cast<u32>(read);
    }

    return done;
}

bool Native::Disc::FindArchiveMod(const char* path, u32* start, u32* size)
{
    std::lock_guard lock(g_ModLock);
    if (!ModsThere())
    {
        return false;
    }

    std::string key;
    std::vector<std::string> parts = PathParts(path, &key);
    if (parts.empty())
    {
        return false;
    }

    // The archive's own place in the image (not a mod's), and the image's end the places start from
    if (g_ArchiveOffset == 0)
    {
        u32 sector = g_RootSector;
        u32 bytes = g_RootSize;
        bool directory = true;
        if (!FindInDirectory(sector, bytes, "CRASH6", &sector, &bytes, &directory) ||
            !FindInDirectory(sector, bytes, "CRASH.BD", &sector, &bytes, &directory))
        {
            return false;
        }

        g_ArchiveOffset = static_cast<u64>(sector) * SectorSize;
        g_ArchiveModNext = (g_ImageSize + ArchiveModAlignment - 1) / ArchiveModAlignment * ArchiveModAlignment;
    }

    auto known = g_ArchiveModSlots.find(key);
    if (known == g_ArchiveModSlots.end())
    {
        std::filesystem::path file;
        s32 slot = -1;
        std::error_code error;
        if (FindMod(parts, &file))
        {
            const u64 bytes = std::filesystem::file_size(file, error);
            const u64 room = (bytes + ArchiveModAlignment - 1) / ArchiveModAlignment * ArchiveModAlignment + ArchiveModAlignment;
            if (!error && g_ArchiveModNext + room - g_ArchiveOffset < (1ull << 32))
            {
                slot = static_cast<s32>(g_ArchiveMods.size());
                g_ArchiveMods.push_back({g_ArchiveModNext, static_cast<u32>(bytes), static_cast<u32>(room), file});
                g_ArchiveModNext += room;
                Native::Log("mods: the archive's %s from %s", key.c_str(), Native::LogPath(file.string()).c_str());
            }
        }

        known = g_ArchiveModSlots.emplace(key, slot).first;
    }

    if (known->second < 0)
    {
        return false;
    }

    const ArchiveMod& mod = g_ArchiveMods[static_cast<size_t>(known->second)];
    *start = static_cast<u32>(mod.offset - g_ArchiveOffset);
    *size = mod.size;
    return true;
}

bool Native::Disc::IsMod(u64 offset)
{
    return offset >= ModBase || (g_ArchiveModNext != 0 && offset >= g_ImageSize);
}

// The PS2's drive: the native build's image is always in and ready
s32 Platform::Disc::Initialise()
{
    return 1;
}

s32 Platform::Disc::SetMedia(Media)
{
    return 1;
}

Platform::Disc::Readiness Platform::Disc::WaitReady()
{
    return Ready;
}
