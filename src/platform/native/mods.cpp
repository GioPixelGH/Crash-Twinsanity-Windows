// The mods (mods.h): the mods folder's mods, their order and switches (mods.txt), the files they give, the memory card they play on
#include "mods.h"

#include "native.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
// mods.txt: a mod a line, the first first, "on" or "off", a tab, its folder's name ("*" the loose files)
constexpr const char* OrderFile = "mods.txt";
constexpr const char* LooseName = "*";
// The disc's top files and folders, and the archive's: the mods folder's own of these names are the loose files
const char* const DiscTops[] = {"CRASH6", "FMV", "SLES_525.68", "SYSTEM.CNF"};
const char* const ArchiveTops[] = {"EXTRAS", "LANGUAGE", "LEVELS", "STARTUP"};
// The executable, never a mod's (the port's own program, the game's data from the player's disc)
constexpr const char* Executable = "SLES_525.68";

std::mutex g_Lock;
bool g_Scanned = false;
// The Options page's list (changed by the player and by an import: from the next start), and the mods this session uses, in the
// order it started with (never changed until the next start: the game reads their files)
std::vector<Native::Mods::Mod> g_Mods;
std::vector<Native::Mods::Mod> g_Used;
// The disc's own archive names (a mod's files not among them are new ones), read once
std::set<std::string> g_ArchiveNames;
bool g_ArchiveRead = false;
std::string g_Card = "memorycard";

std::string Utf8(const fs::path& path)
{
    std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path FromUtf8(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string Upper(std::string text)
{
    for (char& c : text)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    return text;
}

std::string Trimmed(const std::string& text)
{
    size_t start = 0;
    size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start])))
    {
        start++;
    }

    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        end--;
    }

    return text.substr(start, end - start);
}

// A name as the disc compares it: upper case, no version, no trailing dot
std::string Compared(const fs::path& name)
{
    std::string text = Upper(Utf8(name));
    if (size_t version = text.find(';'); version != std::string::npos)
    {
        text.resize(version);
    }

    if (!text.empty() && text.back() == '.')
    {
        text.pop_back();
    }

    return text;
}

bool IsTop(const std::string& upper, bool disc, bool archive)
{
    if (disc)
    {
        for (const char* top : DiscTops)
        {
            if (upper == top)
            {
                return true;
            }
        }
    }

    if (archive)
    {
        for (const char* top : ArchiveTops)
        {
            if (upper == top)
            {
                return true;
            }
        }
    }

    return false;
}

// A folder's child by its compared name, whatever its case on the disk: a file or a folder
bool Child(const fs::path& folder, const std::string& name, bool file, fs::path* found)
{
    std::error_code error;
    for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
    {
        if (Compared(it->path().filename()) == name && (file ? it->is_regular_file(error) : it->is_directory(error)))
        {
            *found = it->path();
            return true;
        }
    }

    return false;
}

bool FindIn(const fs::path& root, const std::vector<std::string>& parts, fs::path* found)
{
    if (root.empty() || parts.empty())
    {
        return false;
    }

    fs::path at = root;
    for (size_t i = 0; i < parts.size(); i++)
    {
        if (!Child(at, parts[i], i + 1 == parts.size(), &at))
        {
            return false;
        }
    }

    *found = at;
    return true;
}

// A Mod Crate's ModCrateInfo.txt: Name=, Description=, Author=, Version=, Game= ("CrashTS" for this game)
void ReadCrateInfo(const fs::path& file, Native::Mods::Mod* mod)
{
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line))
    {
        line = Trimmed(line);
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xef && static_cast<unsigned char>(line[1]) == 0xbb)
        {
            line = line.substr(3);
        }

        size_t equals = line.find('=');
        if (line.empty() || line[0] == '!' || line[0] == '#' || equals == std::string::npos)
        {
            continue;
        }

        std::string key = Upper(Trimmed(line.substr(0, equals)));
        std::string value = Trimmed(line.substr(equals + 1));
        if (key == "NAME" && !value.empty())
        {
            mod->name = value;
        }
        else if (key == "DESCRIPTION")
        {
            mod->description = value;
        }
        else if (key == "AUTHOR")
        {
            mod->author = value;
        }
        else if (key == "VERSION")
        {
            mod->version = value;
        }
        else if (key == "GAME" && !value.empty() && Upper(value) != "CRASHTS")
        {
            mod->refusal = "a Mod Crate for another game (" + value + ")";
        }
    }
}

// The disc's archive's files (CRASH6\CRASH.BH's names, upper case, '/' between their parts)
std::set<std::string> DiscArchiveNames()
{
    std::set<std::string> names;
    Native::Disc::Entry table;
    if (!Native::Disc::Find("CRASH6\\CRASH.BH", &table))
    {
        return names;
    }

    std::vector<u8> bytes(table.size);
    Native::Disc::Read(table.offset, bytes.data(), table.size);
    for (size_t at = 4; at + 4 <= bytes.size();)
    {
        u32 length;
        std::memcpy(&length, &bytes[at], 4);
        if (length == 0 || at + 4 + length + 8 > bytes.size())
        {
            break;
        }

        std::string name(reinterpret_cast<const char*>(&bytes[at + 4]), length);
        name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
        std::replace(name.begin(), name.end(), '\\', '/');
        names.insert(Upper(name));
        at += 4 + length + 8;
    }

    return names;
}

// A mod's files under a folder, as paths of upper case parts joined by '/' (the disc's and the archive's names), and as they're
// named in the folder (with '\' between their parts, as the archive's table has them) with their files
struct Found
{
    std::string compared;
    std::string named;
    fs::path file;
};

void Walk(const fs::path& folder, const std::string& prefix, const std::string& namedPrefix, std::vector<Found>* files)
{
    std::error_code error;
    std::vector<fs::path> children;
    for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
    {
        children.push_back(it->path());
    }

    std::sort(children.begin(), children.end());
    for (const fs::path& child : children)
    {
        const std::string name = Compared(child.filename());
        const std::string named = Utf8(child.filename());
        if (fs::is_directory(child, error))
        {
            Walk(child, prefix + name + "/", namedPrefix + named + "\\", files);
        }
        else if (fs::is_regular_file(child, error))
        {
            files->push_back({prefix + name, namedPrefix + named, child});
        }
    }
}

// A mod's disc files and archive files: a crate's layers whole; a folder's and the loose files' under the disc's top names and
// the archive's
void ModFiles(const Native::Mods::Mod& mod, std::vector<Found>* disc, std::vector<Found>* entries)
{
    if (mod.kind == Native::Mods::Kind::Crate)
    {
        if (!mod.discFiles.empty())
        {
            Walk(mod.discFiles, "", "", disc);
        }

        if (!mod.archiveFiles.empty())
        {
            Walk(mod.archiveFiles, "", "", entries);
        }

        return;
    }

    std::vector<Found> files;
    Walk(mod.discFiles, "", "", &files);
    for (Found& file : files)
    {
        const std::string top = file.compared.substr(0, file.compared.find('/'));
        if (IsTop(top, true, false))
        {
            disc->push_back(std::move(file));
        }
        else if (IsTop(top, false, true))
        {
            entries->push_back(std::move(file));
        }
    }
}

// An ELF's sections: name, address, file offset, size
struct Section
{
    std::string name;
    u32 address;
    u32 offset;
    u32 size;
    bool code;
};

std::vector<Section> Sections(const std::vector<u8>& elf)
{
    std::vector<Section> sections;
    auto le32 = [&](size_t at) -> u32 {
        u32 value = 0;
        if (at + 4 <= elf.size())
        {
            std::memcpy(&value, &elf[at], 4);
        }

        return value;
    };
    auto le16 = [&](size_t at) -> u32 { return at + 2 <= elf.size() ? static_cast<u32>(elf[at] | elf[at + 1] << 8) : 0u; };
    if (elf.size() < 0x34 || std::memcmp(elf.data(), "\x7f" "ELF", 4) != 0)
    {
        return sections;
    }

    const u32 table = le32(0x20);
    const u32 entry = le16(0x2e);
    const u32 count = le16(0x30);
    const u32 names = le16(0x32);
    if (entry < 40 || count == 0 || names >= count || table + static_cast<u64>(entry) * count > elf.size())
    {
        return sections;
    }

    const u32 strings = le32(table + names * entry + 16);
    for (u32 i = 0; i < count; i++)
    {
        const size_t at = table + static_cast<size_t>(i) * entry;
        Section section;
        const u32 name = strings + le32(at);
        for (size_t c = name; c < elf.size() && elf[c] != 0; c++)
        {
            section.name += static_cast<char>(elf[c]);
        }

        const u32 type = le32(at + 4);
        const u32 flags = le32(at + 8);
        section.address = le32(at + 12);
        section.offset = le32(at + 16);
        section.size = type == 8 ? 0 : le32(at + 20);
        section.code = (flags & 4) != 0;
        sections.push_back(section);
    }

    return sections;
}

// What a mod's SLES_525.68 changes against the disc's ("" when nothing): it never applies
std::string ExecutableChanges(const fs::path& file)
{
    Native::Disc::Entry disc;
    if (!Native::Disc::Find(Executable, &disc))
    {
        return {};
    }

    std::vector<u8> original(disc.size);
    Native::Disc::Read(disc.offset, original.data(), disc.size);
    std::ifstream in(file, std::ios::binary);
    std::vector<u8> changed((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (changed == original)
    {
        return {};
    }

    std::vector<Section> ours = Sections(original);
    std::vector<Section> theirs = Sections(changed);
    bool sameLayout = ours.size() == theirs.size() && !ours.empty();
    for (size_t i = 0; sameLayout && i < ours.size(); i++)
    {
        sameLayout = ours[i].name == theirs[i].name && ours[i].address == theirs[i].address && ours[i].offset == theirs[i].offset &&
                     ours[i].size == theirs[i].size;
    }

    if (!sameLayout)
    {
        return "its SLES_525.68 is laid out differently from the disc's (a rebuilt executable): its changes to the game's code and data "
               "don't apply in this port";
    }

    u32 codePlaces = 0;
    u32 codeWords = 0;
    u32 dataPlaces = 0;
    u32 dataWords = 0;
    for (const Section& section : ours)
    {
        bool inRun = false;
        for (u32 at = 0; at + 4 <= section.size && section.offset + at + 4 <= original.size(); at += 4)
        {
            const bool differs = std::memcmp(&original[section.offset + at], &changed[section.offset + at], 4) != 0;
            if (differs)
            {
                (section.code ? codeWords : dataWords)++;
                if (!inRun)
                {
                    (section.code ? codePlaces : dataPlaces)++;
                }
            }

            inRun = differs;
        }
    }

    char text[320];
    std::snprintf(text, sizeof(text),
                  "its SLES_525.68 changes the game's code in %u place%s (%u words) and its data in %u (%u words): this port runs "
                  "its own code and reads the game's data from your disc's, so they don't apply",
                  codePlaces, codePlaces == 1 ? "" : "s", codeWords, dataPlaces, dataWords);
    return text;
}

// A mod folder: a crate (its ModCrateInfo.txt, maybe one folder down as an unpacked zip leaves it), else a folder of files
Native::Mods::Mod Describe(const fs::path& folder, const std::string& id)
{
    Native::Mods::Mod mod{};
    mod.id = id;
    mod.name = id;
    fs::path at = folder;
    fs::path info;
    if (!Child(at, "MODCRATEINFO.TXT", true, &info))
    {
        // An unpacked zip's one folder
        std::error_code error;
        std::vector<fs::path> inside;
        bool files = false;
        for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
        {
            if (it->is_directory(error))
            {
                inside.push_back(it->path());
            }
            else
            {
                files = true;
            }
        }

        if (!files && inside.size() == 1 && Child(inside[0], "MODCRATEINFO.TXT", true, &info))
        {
            at = inside[0];
        }
    }

    if (!info.empty())
    {
        mod.kind = Native::Mods::Kind::Crate;
        ReadCrateInfo(info, &mod);
        fs::path layer;
        mod.discFiles = Child(at, "LAYER0", false, &layer) ? layer : fs::path();
        mod.archiveFiles = Child(at, "LAYER1", false, &layer) ? layer : fs::path();
        fs::path settings;
        if (Child(at, "MODCRATESETTINGS.TXT", true, &settings))
        {
            mod.warnings.push_back("its ModCrateSettings.txt (Crate Mod Loader's changes to the game's code) doesn't apply in this "
                                   "port");
        }
    }
    else
    {
        mod.kind = id.empty() ? Native::Mods::Kind::Loose : Native::Mods::Kind::Folder;
        mod.discFiles = folder;
        mod.archiveFiles = folder;
    }

    return mod;
}

// What a mod has: its disc files and archive files (of them, those the disc's archive hasn't), its executable's changes
void Count(Native::Mods::Mod* mod, const std::set<std::string>& archive)
{
    std::vector<Found> disc;
    std::vector<Found> entries;
    ModFiles(*mod, &disc, &entries);
    mod->discCount = 0;
    for (const Found& file : disc)
    {
        mod->discCount += file.compared == Executable ? 0 : 1;
    }

    mod->archiveCount = static_cast<u32>(entries.size());
    mod->addedCount = 0;
    for (const Found& entry : entries)
    {
        mod->addedCount += archive.count(entry.compared) == 0 ? 1 : 0;
    }

    fs::path executable;
    if (FindIn(mod->discFiles, {Executable}, &executable))
    {
        std::string changes = ExecutableChanges(executable);
        if (!changes.empty())
        {
            mod->warnings.push_back(changes);
        }
    }
}

std::string KindText(const Native::Mods::Mod& mod)
{
    std::string text = mod.kind == Native::Mods::Kind::Crate ? "a Mod Crate" : mod.kind == Native::Mods::Kind::Folder ? "a folder" : "loose files";
    if (!mod.author.empty())
    {
        text += " by " + mod.author;
    }

    if (!mod.version.empty())
    {
        text += ", " + mod.version;
    }

    return text;
}

// A card folder's name made of the mods' ("memorycard-mods-Padoru Pandemonium"), short and safe for a folder
std::string CardFor(const std::vector<std::string>& ids)
{
    std::string joined;
    for (const std::string& id : ids)
    {
        joined += (joined.empty() ? "" : "+") + (id.empty() ? std::string("loose") : id);
    }

    std::string safe;
    for (char c : joined)
    {
        const bool kept = std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '+' ||
                          static_cast<unsigned char>(c) >= 0x80;
        safe += kept ? c : '_';
    }

    if (safe.size() > 48)
    {
        // FNV-1a of the whole, after its start
        u32 hash = 2166136261u;
        for (char c : joined)
        {
            hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
        }

        char tail[16];
        std::snprintf(tail, sizeof(tail), "-%08x", hash);
        safe = safe.substr(0, 32) + tail;
    }

    while (!safe.empty() && (safe.back() == ' ' || safe.back() == '.'))
    {
        safe.pop_back();
    }

    return "memorycard-mods-" + safe;
}

void SaveOrder()
{
    fs::path file = fs::path(Native::SettingsFolder()) / OrderFile;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "# The mods' order (the first wins where two have the same file) and switches, from the next start: on or off, a tab,\n"
           "# the mod's folder in the mods folder (" << LooseName << " the mods folder's own files). Options > Mods changes them.\n";
    for (const Native::Mods::Mod& mod : g_Mods)
    {
        out << (mod.on ? "on" : "off") << '\t' << (mod.id.empty() ? std::string(LooseName) : mod.id) << '\n';
    }
}
}

namespace
{
// The mods folder's mods, described (their files not counted yet), by name (the loose files last)
std::vector<Native::Mods::Mod> FolderMods()
{
    std::vector<Native::Mods::Mod> found;
    const fs::path folder = Native::Mods::Folder();
    std::error_code error;
    if (fs::is_directory(folder, error))
    {
        bool loose = false;
        for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
        {
            const std::string name = Utf8(it->path().filename());
            const std::string compared = Compared(it->path().filename());
            if (name.empty() || name[0] == '.')
            {
                continue;
            }

            if (IsTop(compared, true, true) || compared == "LAYER0" || compared == "LAYER1" || compared == "MODCRATEINFO.TXT")
            {
                loose = true;
            }
            else if (it->is_directory(error))
            {
                found.push_back(Describe(it->path(), name));
            }
        }

        if (loose)
        {
            Native::Mods::Mod mod = Describe(folder, "");
            mod.kind = mod.kind == Native::Mods::Kind::Crate ? Native::Mods::Kind::Crate : Native::Mods::Kind::Loose;
            mod.name = mod.kind == Native::Mods::Kind::Crate && !mod.name.empty() ? mod.name : "";
            found.push_back(mod);
        }
    }

    std::sort(found.begin(), found.end(), [](const Native::Mods::Mod& a, const Native::Mods::Mod& b) {
        return a.id.empty() != b.id.empty() ? !a.id.empty() : a.id < b.id;
    });
    return found;
}

const std::set<std::string>& ArchiveNames()
{
    if (!g_ArchiveRead)
    {
        g_ArchiveNames = DiscArchiveNames();
        g_ArchiveRead = true;
    }

    return g_ArchiveNames;
}

void LogMod(const Native::Mods::Mod& mod, const char* state)
{
    const std::string name = mod.id.empty() ? "the mods folder's own files" : mod.id;
    Native::Log("mods: %s (%s): %s, %u disc file%s, %u archive file%s (%u new)%s%s", name.c_str(), state, KindText(mod).c_str(), mod.discCount,
        mod.discCount == 1 ? "" : "s", mod.archiveCount, mod.archiveCount == 1 ? "" : "s", mod.addedCount, mod.refusal.empty() ? "" : ": ",
        mod.refusal.c_str());
    for (const std::string& warning : mod.warnings)
    {
        Native::Log("mods: %s: %s", name.c_str(), warning.c_str());
    }
}
}

void Native::Mods::Scan()
{
    std::lock_guard lock(g_Lock);
    if (g_Scanned)
    {
        return;
    }

    // mods.txt's order and switches; the mods not in it first and on, by name (the loose files last)
    std::vector<Mod> found = FolderMods();
    std::vector<std::pair<std::string, bool>> saved;
    std::ifstream in(fs::path(SettingsFolder()) / OrderFile, std::ios::binary);
    std::string line;
    while (std::getline(in, line))
    {
        line = Trimmed(line);
        size_t tab = line.find_first_of("\t ");
        if (line.empty() || line[0] == '#' || tab == std::string::npos)
        {
            continue;
        }

        std::string state = line.substr(0, tab);
        std::string id = Trimmed(line.substr(tab + 1));
        saved.emplace_back(id == LooseName ? std::string() : id, state != "off");
    }

    std::vector<Mod> ordered;
    std::vector<bool> placed(found.size(), false);
    for (size_t i = 0; i < found.size(); i++)
    {
        bool known = std::any_of(saved.begin(), saved.end(), [&](const auto& entry) { return entry.first == found[i].id; });
        if (!known)
        {
            found[i].on = true;
            ordered.push_back(found[i]);
            placed[i] = true;
        }
    }

    for (const auto& [id, on] : saved)
    {
        for (size_t i = 0; i < found.size(); i++)
        {
            if (!placed[i] && found[i].id == id)
            {
                found[i].on = on;
                ordered.push_back(found[i]);
                placed[i] = true;
            }
        }
    }

    // (mods.txt is written when the player changes something: Options > Mods)
    g_Mods = std::move(ordered);

    std::vector<std::string> used;
    for (Mod& mod : g_Mods)
    {
        Count(&mod, ArchiveNames());
        mod.usedNow = mod.on && mod.refusal.empty() && mod.discCount + mod.archiveCount > 0;
        if (mod.usedNow)
        {
            used.push_back(mod.id);
            g_Used.push_back(mod);
        }

        LogMod(mod, !mod.refusal.empty() ? "refused" : !mod.on ? "off" : mod.usedNow ? "on" : "on, empty");
    }

    // A card of these mods' own while any is on (the player's saves never meet a mod's levels)
    if (!used.empty())
    {
        std::sort(used.begin(), used.end());
        g_Card = CardFor(used);
        Log("mods: %zu on: the saves go on %s, the player's own card untouched", used.size(), g_Card.c_str());
    }

    g_Scanned = true;
    if (!used.empty())
    {
        UseMemoryCard(g_Card.c_str());
    }
}

const std::vector<Native::Mods::Mod>& Native::Mods::List()
{
    return g_Mods;
}

void Native::Mods::Refresh()
{
    std::lock_guard lock(g_Lock);
    if (!g_Scanned)
    {
        return;
    }

    // Those still there described anew (an import may have replaced one), keeping their switch, place and use; those gone dropped;
    // the new ones first, on and by name, as the next start would have them
    std::vector<Mod> found = FolderMods();
    std::vector<Mod> added;
    std::vector<Mod> kept;
    for (Mod& mod : found)
    {
        Count(&mod, ArchiveNames());
        auto known = std::find_if(g_Mods.begin(), g_Mods.end(), [&](const Mod& each) { return each.id == mod.id; });
        if (known == g_Mods.end())
        {
            mod.on = true;
            mod.usedNow = false;
            LogMod(mod, "new, on from the next start");
            added.push_back(mod);
        }
    }

    for (const Mod& mod : g_Mods)
    {
        auto now = std::find_if(found.begin(), found.end(), [&](const Mod& each) { return each.id == mod.id; });
        if (now != found.end())
        {
            Mod updated = *now;
            updated.on = mod.on;
            updated.usedNow = mod.usedNow;
            kept.push_back(updated);
        }
        else
        {
            Log("mods: %s gone from the mods folder", mod.id.empty() ? "the mods folder's own files" : mod.id.c_str());
        }
    }

    const bool changed = !added.empty() || kept.size() != g_Mods.size();
    added.insert(added.end(), kept.begin(), kept.end());
    g_Mods = std::move(added);
    if (changed)
    {
        SaveOrder();
    }
}

void Native::Mods::SetOn(size_t index, bool on)
{
    std::lock_guard lock(g_Lock);
    if (index < g_Mods.size() && g_Mods[index].on != on)
    {
        g_Mods[index].on = on;
        SaveOrder();
        Log("mods: %s %s from the next start", g_Mods[index].id.empty() ? "the mods folder's own files" : g_Mods[index].id.c_str(),
            on ? "on" : "off");
    }
}

void Native::Mods::MoveTo(size_t index, size_t place)
{
    std::lock_guard lock(g_Lock);
    if (index >= g_Mods.size() || place >= g_Mods.size() || index == place)
    {
        return;
    }

    Mod mod = g_Mods[index];
    g_Mods.erase(g_Mods.begin() + static_cast<std::ptrdiff_t>(index));
    g_Mods.insert(g_Mods.begin() + static_cast<std::ptrdiff_t>(place), mod);
    SaveOrder();
    Log("mods: %s moved to place %zu from the next start", mod.id.empty() ? "the mods folder's own files" : mod.id.c_str(), place + 1);
}

bool Native::Mods::AnyUsed()
{
    if (!g_Scanned)
    {
        return false;
    }

    return !g_Used.empty();
}

std::string Native::Mods::CardName()
{
    return g_Card;
}

bool Native::Mods::FindDiscFile(const std::vector<std::string>& parts, fs::path* file, std::string* mod)
{
    // (The program, what the PS2 boots and the I/O processor's modules, which the port runs as the PS2 would: always the disc's)
    if (!g_Scanned || parts.empty() || (parts.size() == 1 && (parts[0] == Executable || parts[0] == "SYSTEM.CNF")) ||
        (parts.size() >= 2 && parts[0] == "CRASH6" && parts[1] == "SYS"))
    {
        return false;
    }

    for (const Mod& each : g_Used)
    {
        if ((each.kind == Kind::Crate || IsTop(parts[0], true, false)) && FindIn(each.discFiles, parts, file))
        {
            *mod = each.id;
            return true;
        }
    }

    return false;
}

std::vector<Native::Mods::ArchiveFile> Native::Mods::ArchiveFiles()
{
    std::vector<ArchiveFile> files;
    if (!g_Scanned)
    {
        return files;
    }

    std::set<std::string> taken;
    for (const Mod& mod : g_Used)
    {
        std::vector<Found> disc;
        std::vector<Found> entries;
        ModFiles(mod, &disc, &entries);
        for (Found& entry : entries)
        {
            if (taken.insert(entry.compared).second)
            {
                files.push_back({"/" + entry.compared, entry.named, entry.file, mod.id});
            }
        }
    }

    return files;
}

bool Native::Mods::Scanned()
{
    return g_Scanned;
}

std::string Native::Mods::LogName(const std::string& id)
{
    return id.empty() ? "the mods folder's own files" : id;
}

fs::path Native::Mods::Folder()
{
    return fs::path(SettingsFolder()) / "mods";
}
