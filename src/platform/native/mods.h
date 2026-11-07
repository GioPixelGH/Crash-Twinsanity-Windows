#pragma once

// The mods (mods.cpp; native/MODDING.md): the settings folder's mods folder, each mod in it a folder of its own, used in an order
// with a switch each (mods.txt), from the start of the game to its end.
//
// What a mod folder can be:
// - a Mod Crate (Crate Mod Loader's: its ModCrateInfo.txt, layer0/ the disc's files, layer1/ the archive's, CRASH6\CRASH.BD's,
//   by their paths: Levels/Earth/Hub/beach.rm2), as the community ships them; a crate of another game is refused, its
//   ModCrateSettings.txt (Crate Mod Loader's settings of the game's code) doesn't apply;
// - a folder of files laid out as the disc's (CRASH6/MUSIC.MB, FMV/H01_A.PSS) and the archive's (Levels/..., Language/...).
// The mods folder's own files and folders named as the disc's and the archive's (mods\Levels\..., as before 1.2.0) are one more,
// "the loose files".
//
// A file of the disc's or the archive's comes from the first mod on that has it, else from the disc. The archive's files a mod
// adds (new levels) are found as its own are. A mod's SLES_525.68 is never used (the program is the port's, the game's data is
// read from the player's disc's): its changes are said in the log. While any mod is on, the saves go on a memory card of their
// own (memorycard-mods-...), never the player's.
#include "common.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Native::Mods
{
enum class Kind
{
    Loose,
    Crate,
    Folder,
};

struct Mod
{
    // Its folder's name in the mods folder ("" for the loose files)
    std::string id;
    Kind kind;
    // Where its disc files and its archive files are (a crate's layer0 and layer1; else both the folder)
    std::filesystem::path discFiles;
    std::filesystem::path archiveFiles;
    // A crate's ModCrateInfo.txt (else the folder's name)
    std::string name;
    std::string author;
    std::string version;
    std::string description;
    // Saved for the next start (mods.txt), and as this session uses it
    bool on;
    bool usedNow;
    // Why it can't be used ("" when it can), and what's worth knowing (its executable's changes...)
    std::string refusal;
    std::vector<std::string> warnings;
    // Its files: the disc's, the archive's (of them added: not in the disc's archive)
    u32 discCount;
    u32 archiveCount;
    u32 addedCount;
};

// The mods folder's mods, looked at once the disc is open (disc.cpp asks first): mods.txt's order and switches, a mod not in it
// yet first and on; the log says each
void Scan();
// In their order (the first wins where two have the same file)
const std::vector<Mod>& List();
// Saved to mods.txt for the next start
void SetOn(size_t index, bool on);
void MoveTo(size_t index, size_t place);
// Whether this session uses any mod
bool AnyUsed();
// The memory card's folder this session's saves go to: "memorycard", or one of the mods on's own
std::string CardName();
// Whether the mods folder has been looked at (Scan)
bool Scanned();
// A file the mods on have for a disc path's parts (upper case, as disc.cpp's PathParts makes them: "FMV", "H01_A.PSS"): its file
// and the mod's name, from the first that has it (never the executable or SYSTEM.CNF)
bool FindDiscFile(const std::vector<std::string>& parts, std::filesystem::path* file, std::string* mod);
// The archive's files the mods on have, each from the first that has it: its key ("/LEVELS/EARTH/HUB/BEACH.RM2"), its name as the
// archive's table would have it ("Levels\Earth\Hubeach.rm2"), its file and the mod's name
struct ArchiveFile
{
    std::string key;
    std::string name;
    std::filesystem::path file;
    std::string mod;
};
std::vector<ArchiveFile> ArchiveFiles();
// A mod's name in the log ("the mods folder's own files" for the loose files)
std::string LogName(const std::string& id);
// The mods folder (exists or not)
std::filesystem::path Folder();

}
