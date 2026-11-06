// The disc image recognised for the log (THE LIST 5): the program its SYSTEM.CNF boots (and whether its bytes are a retail
// program's), its size, and, once an image, its CRC-32 and SHA-1 over the whole image against redump.org's dumps of the game:
// the retail disc (which one), a modified copy (a retail program, the image not a dump: a translation, a mod built into the
// ISO), or an unknown image. The hashing (a few seconds for 2.4 GB) runs on a thread of its own in Windows' background mode
// (low processor and disc priority) only when the image has a dump's size; its result is kept in the settings folder
// (disc_identity.txt: the image's size, date and hashes), so each image is hashed once. See native/WINDOWS.md, "The log".
#include "native.h"
#include "discimagehash.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
namespace fs = std::filesystem;

// redump.org's dumps of the game (their disc pages, read 2026-11-06): the size, CRC-32 and SHA-1 of each whole image
struct Dump
{
    u32 redump;
    const char* serial;
    const char* region;
    const char* version;
    const char* where;
    u64 bytes;
    u32 crc;
    const char* sha1;
    bool game;
};
constexpr Dump Dumps[] = {
    {4514, "SLES-52568", "PAL", "1.01", "Europe, Australia", 2558263296ull, 0x9055a7bcu, "7cd787a8cb35d1bcde63ad742746d2222813145e", true},
    {42680, "SLUS-20909", "NTSC-U", "1.00", "USA", 2108096512ull, 0x4a1487dfu, "0d0252b6656422b75c37f325cb0002705f13bada", true},
    {13689, "SLUS-20909", "NTSC-U", "2.00", "USA, Action Pack", 2108489728ull, 0x06c5bb4au, "4f1a56f633c1b500bf3e39a0df1fdc8f101018ff",
     true},
    {26782, "SLPM-65801", "NTSC-J", "1.00", "Japan", 1954578432ull, 0xc563818bu, "53fd7c66e14fb3f24896164e5861b5a27858df36", true},
    {35281, "SLED-52574", "PAL", "1.01", "Europe: a demo disc, with Spyro: A Hero's Tail", 1228832768ull, 0x227f06f2u,
     "ba24fe72f623ed16bb23155cb3b0fb1e8b1e5cf5", false},
    {28365, "SLUS-29101", "NTSC-U", "1.00", "USA: a demo disc, with Spyro: A Hero's Tail", 1228832768ull, 0xb2da83c7u,
     "1d2a05d871c867223885f0a3a52f1ab07bd52658", false},
};

// The retail programs known by their bytes (the file SYSTEM.CNF boots, from the dumps above)
struct Program
{
    const char* sha1;
    const char* what;
};
constexpr Program Programs[] = {
    {"d41b8d53f733f930cf03d8ea944bec24429fe57c", "PAL 1.01"},
    {"9637769b520f137a2225eb1a24b35d602ca2c79d", "NTSC-U 2.00"},
};

constexpr const char* KeptFile = "disc_identity.txt";

// What the image's own files and its size say
struct Facts
{
    // SYSTEM.CNF's BOOT2 program ("SLES_525.68"), its serial ("SLES-52568"), VER; the program's SHA-1 and what retail program it is
    std::string program;
    std::string serial;
    std::string version;
    std::string programSha1;
    const char* retailProgram = nullptr;
    u64 bytes = 0;
    s64 date = 0;
    // The dumps of the image's size
    std::vector<const Dump*> sized;
};

std::string Thousands(u64 n)
{
    std::string digits = std::to_string(n);
    for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3)
    {
        digits.insert(static_cast<size_t>(at), ",");
    }

    return digits;
}

fs::path PathOf(const std::string& utf8)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string Trimmed(std::string text)
{
    while (!text.empty() && (text.back() == ' ' || text.back() == '\r' || text.back() == '\n' || text.back() == '\t'))
    {
        text.pop_back();
    }

    size_t start = text.find_first_not_of(" \t");
    return start == std::string::npos ? std::string() : text.substr(start);
}

// The disc open: SYSTEM.CNF and its program read through Native::Disc
Facts ReadFacts(const std::string& image)
{
    Facts facts;
    Native::Disc::Entry config;
    if (Native::Disc::Find("SYSTEM.CNF", &config) && config.size < 4096)
    {
        std::string text(config.size, '\0');
        Native::Disc::Read(config.offset, text.data(), config.size);
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line))
        {
            size_t equals = line.find('=');
            if (equals == std::string::npos)
            {
                continue;
            }

            std::string key = Trimmed(line.substr(0, equals));
            std::string value = Trimmed(line.substr(equals + 1));
            if (key == "BOOT2")
            {
                // "cdrom0:\SLES_525.68;1"
                size_t from = value.find_last_of("\\:/");
                value = from == std::string::npos ? value : value.substr(from + 1);
                facts.program = value.substr(0, value.find(';'));
            }
            else if (key == "VER")
            {
                facts.version = value;
            }
        }
    }

    // "SLES_525.68" -> "SLES-52568"
    if (facts.program.size() == 11 && facts.program[4] == '_' && facts.program[8] == '.')
    {
        facts.serial = facts.program.substr(0, 4) + "-" + facts.program.substr(5, 3) + facts.program.substr(9, 2);
    }

    Native::Disc::Entry program;
    if (!facts.program.empty() && Native::Disc::Find(facts.program.c_str(), &program) && program.size < (64u << 20))
    {
        std::vector<u8> bytes(program.size);
        if (Native::Disc::Read(program.offset, bytes.data(), program.size) == program.size)
        {
            facts.programSha1 = ps2r::app::Sha1::HexOf(bytes.data(), bytes.size());
            for (const Program& known : Programs)
            {
                if (facts.programSha1 == known.sha1)
                {
                    facts.retailProgram = known.what;
                }
            }
        }
    }

    std::error_code error;
    facts.bytes = fs::file_size(PathOf(image), error);
    facts.date = error ? 0 : static_cast<s64>(fs::last_write_time(PathOf(image), error).time_since_epoch().count());
    for (const Dump& dump : Dumps)
    {
        if (dump.bytes == facts.bytes)
        {
            facts.sized.push_back(&dump);
        }
    }

    return facts;
}

std::string ProgramWords(const Facts& facts)
{
    if (facts.program.empty())
    {
        return "no program named in its SYSTEM.CNF";
    }

    std::string words = facts.program + (facts.version.empty() ? "" : " (VER " + facts.version + ")");
    return words + (facts.retailProgram != nullptr ? std::string(", the retail ") + facts.retailProgram + " program"
                                                   : ", not a retail program known here");
}

// What the image is, once its hashes are there (crc and sha1 empty: not hashed, its size is no dump's)
std::string Verdict(const Facts& facts, bool hashed, u32 crc, const std::string& sha1)
{
    char line[512];
    if (hashed)
    {
        for (const Dump* dump : facts.sized)
        {
            if (dump->crc == crc && sha1 == dump->sha1)
            {
                std::snprintf(line, sizeof(line), "%s %s v%s %s (%s): its size, CRC-32 %08x and SHA-1 are redump.org's disc %u",
                              dump->game ? "the retail disc," : "a retail disc that isn't the game,", dump->serial, dump->version,
                              dump->region, dump->where, dump->crc, dump->redump);
                return line;
            }
        }
    }

    // The nearest dump of its serial (for the sizes)
    const Dump* nearest = nullptr;
    for (const Dump& dump : Dumps)
    {
        if (!facts.serial.empty() && facts.serial == dump.serial && dump.game)
        {
            nearest = &dump;
        }
    }

    std::string size = Thousands(facts.bytes) + " bytes";
    if (nearest != nullptr)
    {
        size += nearest->bytes == facts.bytes ? std::string(", the retail disc's size, other bytes")
                                              : "; redump.org's disc " + std::to_string(nearest->redump) + " has " +
                                                    Thousands(nearest->bytes);
    }

    if (hashed)
    {
        char hashes[96];
        std::snprintf(hashes, sizeof(hashes), "; CRC-32 %08x, SHA-1 %s", crc, sha1.c_str());
        size += hashes;
    }

    if (facts.retailProgram != nullptr)
    {
        std::snprintf(line, sizeof(line), "a modified copy of the %s disc: %s, but the image isn't one of redump.org's dumps (%s)",
                      facts.retailProgram, ProgramWords(facts).c_str(), size.c_str());
    }
    else
    {
        std::snprintf(line, sizeof(line), "an unknown image: %s; not one of redump.org's dumps (%s)", ProgramWords(facts).c_str(),
                      size.c_str());
    }

    return line;
}

std::string KeptPath()
{
    return Native::SettingsFolder() + "/" + KeptFile;
}

// The hashes kept for an image of this size and date ("<bytes> <date> <crc> <sha1>")
bool Kept(const Facts& facts, u32* crc, std::string* sha1)
{
    std::ifstream file(PathOf(KeptPath()));
    std::string line;
    while (std::getline(file, line))
    {
        unsigned long long bytes = 0;
        long long date = 0;
        unsigned int value = 0;
        char hex[48] = {};
        if (line.empty() || line[0] == '#' ||
            std::sscanf(line.c_str(), "%llu %lld %x %40s", &bytes, &date, &value, hex) != 4)
        {
            continue;
        }

        if (bytes == facts.bytes && date == facts.date)
        {
            *crc = value;
            *sha1 = hex;
            return true;
        }
    }

    return false;
}

void Keep(const Facts& facts, u32 crc, const std::string& sha1)
{
    // The other images' lines kept, this one's added
    std::vector<std::string> lines;
    {
        std::ifstream file(PathOf(KeptPath()));
        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty() && line[0] != '#')
            {
                lines.push_back(line);
            }
        }
    }

    std::ofstream file(PathOf(KeptPath()), std::ios::trunc | std::ios::binary);
    file << "# Crash Twinsanity's disc images recognised (the log's \"disc:\" line): each image's size in bytes, its date, its "
            "CRC-32 and SHA-1\n";
    for (const std::string& line : lines)
    {
        file << line << "\n";
    }

    char line[160];
    std::snprintf(line, sizeof(line), "%llu %lld %08x %s\n", static_cast<unsigned long long>(facts.bytes),
                  static_cast<long long>(facts.date), crc, sha1.c_str());
    file << line;
}

// The whole image's CRC-32 and SHA-1 (false when it can't be read to its end)
bool HashImage(const std::string& image, u32* crc, std::string* sha1, double* seconds)
{
    const auto began = std::chrono::steady_clock::now();
#if defined(_WIN32)
    std::FILE* file = _wfopen(PathOf(image).c_str(), L"rb");
#else
    std::FILE* file = std::fopen(image.c_str(), "rb");
#endif
    if (file == nullptr)
    {
        return false;
    }

    ps2r::app::Crc32 crc32;
    ps2r::app::Sha1 sha;
    std::vector<u8> buffer(4u << 20);
    for (;;)
    {
        size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
        if (read == 0)
        {
            break;
        }

        crc32.Update(buffer.data(), read);
        sha.Update(buffer.data(), read);
    }

    const bool whole = std::ferror(file) == 0;
    std::fclose(file);
    u8 digest[ps2r::app::Sha1::kDigestBytes];
    sha.Finish(digest);
    *crc = crc32.Value();
    *sha1 = ps2r::app::Sha1::Hex(digest);
    *seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    return whole;
}

void HashAndLog(const std::string& image, const Facts& facts)
{
#if defined(_WIN32)
    // Below the game's threads, its disc reads first
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
    u32 crc = 0;
    std::string sha1;
    double seconds = 0.0;
    if (!HashImage(image, &crc, &sha1, &seconds))
    {
        Native::Log("disc: the image couldn't be read to its end to check it: %s", Verdict(facts, false, 0, {}).c_str());
        return;
    }

    Keep(facts, crc, sha1);
    Native::Log("disc: %s (checked in %.1f s)", Verdict(facts, true, crc, sha1).c_str(), seconds);
}
}

void Native::IdentifyDisc(const std::string& image, DiscHashing hashing)
{
    Facts facts = ReadFacts(image);
    u32 crc = 0;
    std::string sha1;
    if (Kept(facts, &crc, &sha1))
    {
        Log("disc: %s (checked before)", Verdict(facts, true, crc, sha1).c_str());
        return;
    }

    // No dump has its size: what it is needs no hashing
    if (facts.sized.empty())
    {
        Log("disc: %s", Verdict(facts, false, 0, {}).c_str());
        return;
    }

    if (hashing == DiscHashing::None)
    {
        Log("disc: %s, %s bytes (redump.org's disc %u's size; not hashed without a window)", ProgramWords(facts).c_str(),
            Thousands(facts.bytes).c_str(), facts.sized.front()->redump);
        return;
    }

    Log("disc: %s, %s bytes (redump.org's disc %u's size): its CRC-32 and SHA-1 being checked, once", ProgramWords(facts).c_str(),
        Thousands(facts.bytes).c_str(), facts.sized.front()->redump);
    if (hashing == DiscHashing::Wait)
    {
        HashAndLog(image, facts);
        return;
    }

    std::thread([image, facts] { HashAndLog(image, facts); }).detach();
}

void Native::LogRefusedDisc(const std::string& image)
{
    Facts facts = ReadFacts(image);
    std::string sized;
    for (const Dump* dump : facts.sized)
    {
        sized += (sized.empty() ? "; the size of redump.org's disc " : " or ") + std::to_string(dump->redump) + " (" + dump->serial +
                 " v" + dump->version + ", " + dump->region + ", " + dump->where + ")";
    }

    Log("disc: %s, %s bytes%s", ProgramWords(facts).c_str(), Thousands(facts.bytes).c_str(), sized.c_str());
}
