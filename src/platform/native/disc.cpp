// The disc: the PS2's drive (Platform::Disc) and the native build's reading of the disc image (ISO 9660), which stands for it
#include "native.h"
#include "mods.h"
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

// Mods (mods.h, mods.cpp): a disc file's mod is given a place of its own past 16 GB (a GB each: its sectors still 32 bit numbers
// for the I/O processor's reads), which Read takes from its file. The archive's files' mods (CRASH6\CRASH.BD's) follow the
// archive's own data in a copy of it at 12 GB: while any mod has one, the archive's data file is found there, its size theirs
// too, and its table (CRASH6\CRASH.BH) is made again with their starts and sizes, the files they add among them, as Crate Mod
// Loader rebuilds it on the PS2. So the game and the I/O processor's STREAM.IRX read a mod's file as one of the archive's.
// STREAM.IRX refuses an offset past its file's size and reads on from where it was (#36's garbled sounds: each sound bank of a
// mod's level, placed past the image's end, came as the bank before it, cut short), and counts in signed 32 bits: the archive
// with its mods should stay under 2 GB
constexpr u64 ModBase = 16ull << 30;
constexpr u32 ModSlotShift = 30;
constexpr u32 MostMods = 1024;
constexpr u64 ArchiveCopyBase = 12ull << 30;
constexpr u32 ArchiveModAlignment = 2048;
constexpr u64 ArchiveSignedReach = 1ull << 31;
std::recursive_mutex g_ModLock;
// The disc files' mods by slot (a file's, or bytes made here: the archive's table), their slots by path key ("/FMV/H01_A.PSS")
struct ModFile
{
    std::filesystem::path file;
    std::string mod;
    std::vector<u8> bytes;
    bool made;
};
std::vector<ModFile> g_ModFiles;
std::map<std::string, s32> g_ModSlots;
// The archive's files' mods in its copy, by start; their indexes by path key ("/LEVELS/EARTH/HUB/BEACH.RM2")
struct ArchiveMod
{
    u64 start;
    u32 size;
    u32 room;
    std::filesystem::path file;
    std::string mod;
    bool logged;
};
std::vector<ArchiveMod> g_ArchiveMods;
std::map<std::string, size_t> g_ArchiveModKeys;
bool g_ArchivePlaced = false;
// The archive's data under the copy (the image's, or a mod's whole CRASH.BD) and the copy's size with the mods (0: no copy)
Native::Disc::Entry g_ArchiveBase{};
u64 g_ArchiveSpan = 0;

u64 Aligned(u64 value)
{
    return (value + ArchiveModAlignment - 1) / ArchiveModAlignment * ArchiveModAlignment;
}

// A file's bytes from a place in it: how many were read
u32 ReadFileAt(const std::filesystem::path& path, u64 at, void* destination, u32 size)
{
#if defined(_WIN32)
    FILE* stream = _wfopen(path.wstring().c_str(), L"rb");
#else
    FILE* stream = std::fopen(path.string().c_str(), "rb");
#endif
    if (stream == nullptr)
    {
        return 0;
    }

#if defined(_WIN32)
    const bool placed = _fseeki64(stream, static_cast<long long>(at), SEEK_SET) == 0;
#else
    const bool placed = fseeko(stream, static_cast<off_t>(at), SEEK_SET) == 0;
#endif
    const u32 read = placed ? static_cast<u32>(std::fread(destination, 1, size, stream)) : 0;
    std::fclose(stream);
    return read;
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

bool FindInDirectory(u32 sector, u32 size, const std::string& name, u32* foundSector, u32* foundSize, bool* directory);

// A file of the image itself (no mod)
bool FindOnImage(const char* path, Native::Disc::Entry* entry)
{
    u32 sector = g_RootSector;
    u32 size = g_RootSize;
    bool directory = true;
    std::string key;
    for (const std::string& part : PathParts(path, &key))
    {
        if (!directory || !FindInDirectory(sector, size, part, &sector, &size, &directory))
        {
            return false;
        }
    }

    if (directory)
    {
        return false;
    }

    entry->offset = static_cast<u64>(sector) * SectorSize;
    entry->size = size;
    return true;
}

// A disc file's mod's slot (-1 for none), found once
s32 DiscModSlot(const std::string& key, const std::vector<std::string>& parts)
{
    auto known = g_ModSlots.find(key);
    if (known != g_ModSlots.end())
    {
        return known->second;
    }

    std::filesystem::path file;
    std::string mod;
    s32 slot = -1;
    if (g_ModFiles.size() < MostMods && Native::Mods::FindDiscFile(parts, &file, &mod))
    {
        slot = static_cast<s32>(g_ModFiles.size());
        g_ModFiles.push_back({file, mod, {}, false});
        Native::Log("mods: %s from %s (%s)", key.c_str(), Native::LogPath(file.string()).c_str(), Native::Mods::LogName(mod).c_str());
    }

    g_ModSlots.emplace(key, slot);
    return slot;
}

// A slot's place and size
bool SlotEntry(s32 slot, Native::Disc::Entry* entry)
{
    const ModFile& mod = g_ModFiles[static_cast<size_t>(slot)];
    std::error_code error;
    const u64 size = mod.made ? mod.bytes.size() : std::filesystem::file_size(mod.file, error);
    if (error || size >= (1ull << ModSlotShift))
    {
        return false;
    }

    entry->offset = ModBase + (static_cast<u64>(slot) << ModSlotShift);
    entry->size = static_cast<u32>(size);
    return true;
}

// The archive's files' mods placed after its data in its copy, and its table made again with them (once the mods are known)
void PlaceArchiveMods()
{
    if (g_ArchivePlaced || !Native::Mods::Scanned())
    {
        return;
    }

    g_ArchivePlaced = true;
    std::vector<Native::Mods::ArchiveFile> files = Native::Mods::ArchiveFiles();
    if (files.empty())
    {
        return;
    }

    // Under the copy: a mod's whole archive (CRASH6\CRASH.BD and .BH: an image's files copied into the mods folder), else the image's
    Native::Disc::Entry data{};
    Native::Disc::Entry table{};
    const s32 dataSlot = DiscModSlot("/CRASH6/CRASH.BD", {"CRASH6", "CRASH.BD"});
    const s32 tableSlot = DiscModSlot("/CRASH6/CRASH.BH", {"CRASH6", "CRASH.BH"});
    const bool haveData = dataSlot >= 0 ? SlotEntry(dataSlot, &data) : FindOnImage("CRASH6\\CRASH.BD", &data);
    const bool haveTable = tableSlot >= 0 ? SlotEntry(tableSlot, &table) : FindOnImage("CRASH6\\CRASH.BH", &table);
    std::vector<u8> bytes(haveTable && table.size <= (16u << 20) ? table.size : 0);
    if (!haveData || bytes.size() < 4 || Native::Disc::Read(table.offset, bytes.data(), table.size) != table.size)
    {
        Native::Log("mods: the archive's files from mods can't be used: no archive to put them in");
        return;
    }

    u64 next = Aligned(data.size);
    for (const Native::Mods::ArchiveFile& file : files)
    {
        std::error_code error;
        const u64 size = std::filesystem::file_size(file.file, error);
        const u64 room = Aligned(size) + ArchiveModAlignment;
        if (error || next + room > 0xffffffffull)
        {
            Native::Log("mods: %s from %s left out: %s", file.key.c_str(), Native::Mods::LogName(file.mod).c_str(),
                        error ? "it can't be read" : "the archive with its mods would pass 4 GB");
            continue;
        }

        g_ArchiveModKeys[file.key] = g_ArchiveMods.size();
        g_ArchiveMods.push_back({next, static_cast<u32>(size), static_cast<u32>(room), file.file, file.mod, false});
        next += room;
    }

    if (g_ArchiveMods.empty())
    {
        return;
    }

    g_ArchiveBase = data;
    g_ArchiveSpan = next;

    // The table again (its first word, then each file's name's length, its name, start and size): the archive's files with the mods'
    // starts and sizes where they have them, then the files they add
    std::vector<u8> made(bytes.begin(), bytes.begin() + 4);
    auto put = [&made](const std::string& name, u32 start, u32 size) {
        const u32 length = static_cast<u32>(name.size());
        const u8* l = reinterpret_cast<const u8*>(&length);
        made.insert(made.end(), l, l + 4);
        made.insert(made.end(), name.begin(), name.end());
        const u8* s = reinterpret_cast<const u8*>(&start);
        made.insert(made.end(), s, s + 4);
        const u8* z = reinterpret_cast<const u8*>(&size);
        made.insert(made.end(), z, z + 4);
    };
    std::map<std::string, bool> replaced;
    for (size_t at = 4; at + 4 <= bytes.size();)
    {
        const u32 length = Le32(&bytes[at]);
        if (length == 0 || length > 0x400 || at + 4 + length + 8 > bytes.size())
        {
            break;
        }

        std::string name(reinterpret_cast<const char*>(&bytes[at + 4]), length);
        std::string key;
        PathParts(name.c_str(), &key);
        auto mod = g_ArchiveModKeys.find(key);
        if (mod != g_ArchiveModKeys.end())
        {
            const ArchiveMod& placed = g_ArchiveMods[mod->second];
            put(name, static_cast<u32>(placed.start), placed.size);
            replaced[key] = true;
        }
        else
        {
            put(name, Le32(&bytes[at + 4 + length]), Le32(&bytes[at + 8 + length]));
        }

        at += 4 + length + 8;
    }

    u32 added = 0;
    for (const Native::Mods::ArchiveFile& file : files)
    {
        auto mod = g_ArchiveModKeys.find(file.key);
        if (mod != g_ArchiveModKeys.end() && !replaced.count(file.key))
        {
            put(file.name, static_cast<u32>(g_ArchiveMods[mod->second].start), g_ArchiveMods[mod->second].size);
            added++;
        }
    }

    const s32 slot = static_cast<s32>(g_ModFiles.size());
    g_ModFiles.push_back({{}, {}, std::move(made), true});
    g_ModSlots["/CRASH6/CRASH.BH"] = slot;
    Native::Log("mods: the archive's table made again: %zu of its files from mods, %u of them new; the archive with them %llu MB%s",
                g_ArchiveMods.size(), added, static_cast<unsigned long long>(next >> 20),
                next > ArchiveSignedReach ? " (past 2 GB: the sounds of the last ones may not be read)" : "");
}

// The disc path's mod, if there's one: its place and size
bool FindModEntry(const char* path, Native::Disc::Entry* entry)
{
    std::string key;
    std::vector<std::string> parts = PathParts(path, &key);
    if (parts.empty() || !Native::Mods::Scanned())
    {
        return false;
    }

    PlaceArchiveMods();
    const s32 slot = DiscModSlot(key, parts);
    return slot >= 0 && SlotEntry(slot, entry);
}

// A read of the archive's copy: its data, then its files' mods (each its file's bytes, zeros past them to its place's end)
u32 ReadArchiveCopy(u64 at, u8* destination, u32 size)
{
    u32 done = 0;
    while (done < size && at + done < g_ArchiveSpan)
    {
        const u64 here = at + done;
        u8* out = destination + done;
        const u32 want = size - done;
        if (here < g_ArchiveBase.size)
        {
            const u32 count = static_cast<u32>(std::min<u64>(want, g_ArchiveBase.size - here));
            const u32 read = Native::Disc::Read(g_ArchiveBase.offset + here, out, count);
            std::memset(out + read, 0, count - read);
            done += count;
            continue;
        }

        auto it = std::upper_bound(g_ArchiveMods.begin(), g_ArchiveMods.end(), here,
                                   [](u64 value, const ArchiveMod& mod) { return value < mod.start; });
        if (it == g_ArchiveMods.begin() || here >= std::prev(it)->start + std::prev(it)->room)
        {
            // Between the data's end and the first mod's place
            const u64 until = it == g_ArchiveMods.end() ? g_ArchiveSpan : it->start;
            const u32 count = static_cast<u32>(std::min<u64>(want, until - here));
            std::memset(out, 0, count);
            done += count;
            continue;
        }

        const ArchiveMod& mod = *std::prev(it);
        const u64 inside = here - mod.start;
        const u32 count = static_cast<u32>(std::min<u64>(want, mod.room - inside));
        std::memset(out, 0, count);
        if (inside < mod.size)
        {
            ReadFileAt(mod.file, inside, out, static_cast<u32>(std::min<u64>(count, mod.size - inside)));
        }

        done += count;
    }

    return done;
}

// A read of a disc file's mod's place
u32 ReadMod(u64 offset, void* destination, u32 size)
{
    std::filesystem::path file;
    const u64 at = (offset - ModBase) & ((1ull << ModSlotShift) - 1);
    {
        std::lock_guard lock(g_ModLock);
        const u64 slot = (offset - ModBase) >> ModSlotShift;
        if (slot >= g_ModFiles.size())
        {
            return 0;
        }

        const ModFile& found = g_ModFiles[static_cast<size_t>(slot)];
        if (found.made)
        {
            const u32 count = at < found.bytes.size() ? static_cast<u32>(std::min<u64>(size, found.bytes.size() - at)) : 0;
            std::memcpy(destination, found.bytes.data() + at, count);
            return count;
        }

        file = found.file;
    }

    return ReadFileAt(file, at, destination, size);
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
    std::lock_guard mods(g_ModLock);
    g_ModFiles.clear();
    g_ModSlots.clear();
    g_ArchiveMods.clear();
    g_ArchiveModKeys.clear();
    g_ArchivePlaced = false;
    g_ArchiveBase = {};
    g_ArchiveSpan = 0;
}

bool Native::Disc::Find(const char* path, Entry* entry)
{
    // The device and leading separators go ("cdrom0:\CRASH6\..." as the PS2 side would have made it)
    const char* colon = std::strchr(path, ':');
    if (colon != nullptr)
    {
        path = colon + 1;
    }

    {
        std::lock_guard lock(g_ModLock);
        std::string key;
        PathParts(path, &key);
        if (key == "/CRASH6/CRASH.BD" && Native::Mods::Scanned())
        {
            // The archive's copy, its files' mods after its data
            PlaceArchiveMods();
            if (g_ArchiveSpan != 0)
            {
                entry->offset = ArchiveCopyBase;
                entry->size = static_cast<u32>(g_ArchiveSpan);
                return true;
            }
        }

        if (FindModEntry(path, entry))
        {
            return true;
        }
    }

    if (!FindOnImage(path, entry))
    {
        return false;
    }

    if (Native::DetailedLog())
    {
        Native::Log("disc: %s (%u bytes)", path, entry->size);
    }

    return true;
}

bool Native::Disc::FindOriginal(const char* path, Entry* entry)
{
    const char* colon = std::strchr(path, ':');
    return FindOnImage(colon != nullptr ? colon + 1 : path, entry);
}

u32 Native::Disc::Read(u64 offset, void* destination, u32 size)
{
    if (offset >= ModBase)
    {
        return ReadMod(offset, destination, size);
    }

    if (offset >= ArchiveCopyBase)
    {
        return ReadArchiveCopy(offset - ArchiveCopyBase, static_cast<u8*>(destination), size);
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
    if (!Native::Mods::Scanned())
    {
        return false;
    }

    PlaceArchiveMods();
    std::string key;
    PathParts(path, &key);
    auto found = g_ArchiveModKeys.find(key);
    if (found == g_ArchiveModKeys.end())
    {
        return false;
    }

    ArchiveMod& mod = g_ArchiveMods[found->second];
    if (!mod.logged)
    {
        mod.logged = true;
        Native::Log("mods: the archive's %s from %s (%s)", key.c_str(), Native::LogPath(mod.file.string()).c_str(),
                    Native::Mods::LogName(mod.mod).c_str());
    }

    *start = static_cast<u32>(mod.start);
    *size = mod.size;
    return true;
}

bool Native::Disc::IsMod(u64 offset)
{
    return offset >= ArchiveCopyBase;
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
