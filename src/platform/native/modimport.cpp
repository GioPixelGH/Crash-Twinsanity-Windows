// Importing mods (mods.h): a zip or a changed disc image put in the mods folder made a mod folder of its own beside it. A zip (a Mod
// Crate's as the community ships them, or of files laid out as the disc's) is unpacked (TwinsanityEvolved's zip reader, modzip.h);
// a disc image is compared with the player's disc file by file, the archive (CRASH6\CRASH.BD) entry by entry through both tables,
// and only what differs is kept, laid out as a Mod Crate (layer0 the disc's files, layer1 the archive's, its ModCrateInfo.txt):
// a 4 GB image becomes the few hundred megabytes a mod changes. Each import is written in a folder of its own first
// (.importing-NAME) and takes the mod's place only once whole; imported.txt in the mod's folder says from what (its name, size
// and date), so a source not changed since isn't imported again
#include "mods.h"

#include "modzip.h"
#include "native.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
constexpr const char* ImportedFile = "imported.txt";
constexpr u32 Sector = 2048;
constexpr size_t Chunk = 1u << 20;

std::string Utf8(const fs::path& path)
{
    std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

fs::path FromUtf8(const std::string& text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

std::string Lower(std::string text)
{
    for (char& c : text)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return text;
}

std::string Upper(std::string text)
{
    for (char& c : text)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    return text;
}

u32 Le32(const u8* bytes)
{
    return static_cast<u32>(bytes[0]) | static_cast<u32>(bytes[1]) << 8 | static_cast<u32>(bytes[2]) << 16 |
           static_cast<u32>(bytes[3]) << 24;
}

// What a source is by its name: a zip, a disc image, or neither
enum class Source
{
    None,
    Zip,
    Image,
};

Source SourceOf(const fs::path& file)
{
    const std::string extension = Lower(Utf8(file.extension()));
    return extension == ".zip" ? Source::Zip : extension == ".iso" ? Source::Image : Source::None;
}

// A source's mark: its name, size and date, as imported.txt keeps it
std::string Mark(const fs::path& source)
{
    std::error_code error;
    const u64 size = fs::file_size(source, error);
    const auto time = fs::last_write_time(source, error);
    const long long ticks = static_cast<long long>(time.time_since_epoch().count());
    std::ostringstream text;
    text << "source=" << Utf8(source.filename()) << "\nsize=" << size << "\nmodified=" << ticks << "\n";
    return text.str();
}

std::string ReadText(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool WriteText(const fs::path& file, const std::string& text)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    return static_cast<bool>(out);
}

// A file written from a sink of chunks
class Writer
{
public:
    explicit Writer(const fs::path& file)
    {
        std::error_code error;
        fs::create_directories(file.parent_path(), error);
#if defined(_WIN32)
        stream_ = _wfopen(file.wstring().c_str(), L"wb");
#else
        stream_ = std::fopen(file.string().c_str(), "wb");
#endif
    }

    ~Writer() { Close(); }

    bool Write(const u8* data, size_t size) { return stream_ != nullptr && std::fwrite(data, 1, size, stream_) == size; }

    bool Close()
    {
        bool ok = stream_ != nullptr;
        if (stream_ != nullptr)
        {
            ok = std::fclose(stream_) == 0;
            stream_ = nullptr;
        }

        return ok;
    }

private:
    std::FILE* stream_ = nullptr;
};

// ---------------------------------------------------------------------------------------------------------- a disc image's files
// A changed disc image (ISO 9660, 2048-byte sectors) read as files: their paths (as the image names them, '/' between parts),
// sectors and sizes
struct ImageFile
{
    std::string path;
    u32 sector;
    u32 size;
};

class Image
{
public:
    bool Open(const fs::path& file, std::string* error)
    {
        if (!file_.OpenRead(Utf8(file)))
        {
            *error = "it can't be opened";
            return false;
        }

        u8 descriptor[Sector];
        if (!file_.ReadAt(16ull * Sector, descriptor, Sector) || descriptor[0] != 1 || std::memcmp(descriptor + 1, "CD001", 5) != 0)
        {
            *error = "it isn't a disc image of 2048-byte sectors (an .iso)";
            return false;
        }

        Walk(Le32(descriptor + 156 + 2), Le32(descriptor + 156 + 10), "", 0);
        return true;
    }

    bool Read(u64 offset, void* destination, size_t size) { return file_.ReadAt(offset, destination, size); }
    const std::vector<ImageFile>& Files() const { return files_; }

private:
    void Walk(u32 sector, u32 size, const std::string& prefix, int depth)
    {
        std::vector<u8> data(size);
        if (depth > 16 || size > (16u << 20) || !file_.ReadAt(static_cast<u64>(sector) * Sector, data.data(), size))
        {
            return;
        }

        for (size_t at = 0; at < data.size();)
        {
            const u8 length = data[at];
            if (length == 0)
            {
                at = (at / Sector + 1) * Sector;
                continue;
            }

            if (at + 33 > data.size() || at + length > data.size())
            {
                break;
            }

            const u8* record = &data[at];
            const u8 nameLength = record[32];
            std::string name(reinterpret_cast<const char*>(record + 33), std::min<size_t>(nameLength, length - 33));
            at += length;
            if (name.empty() || name[0] == '\0' || name[0] == '\1')
            {
                continue;
            }

            name = name.substr(0, name.find(';'));
            if (!name.empty() && name.back() == '.')
            {
                name.pop_back();
            }

            const u32 extent = Le32(record + 2);
            const u32 bytes = Le32(record + 10);
            if ((record[25] & 2) != 0)
            {
                Walk(extent, bytes, prefix + name + "/", depth + 1);
            }
            else
            {
                files_.push_back({prefix + name, extent, bytes});
            }
        }
    }

    te::modimport::io::File file_;
    std::vector<ImageFile> files_;
};

// An archive's table (CRASH.BH): its files' names (as the table has them), starts and sizes
struct TableFile
{
    std::string name;
    u32 start;
    u32 size;
};

std::vector<TableFile> TableFiles(const std::vector<u8>& bytes)
{
    std::vector<TableFile> files;
    for (size_t at = 4; at + 4 <= bytes.size();)
    {
        const u32 length = Le32(&bytes[at]);
        if (length == 0 || length > 0x400 || at + 4 + length + 8 > bytes.size())
        {
            break;
        }

        std::string name(reinterpret_cast<const char*>(&bytes[at + 4]), length);
        name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
        files.push_back({name, Le32(&bytes[at + 4 + length]), Le32(&bytes[at + 8 + length])});
        at += 4 + length + 8;
    }

    return files;
}

std::string KeyOf(std::string name)
{
    std::replace(name.begin(), name.end(), '\\', '/');
    return Upper(name);
}

// Whether two runs of bytes are the same: the image's at one place, the player's disc's at another
bool Same(Image& image, u64 imageOffset, u64 discOffset, u64 size, std::vector<u8>& a, std::vector<u8>& b)
{
    for (u64 done = 0; done < size;)
    {
        const size_t count = static_cast<size_t>(std::min<u64>(Chunk, size - done));
        if (!image.Read(imageOffset + done, a.data(), count) || Native::Disc::Read(discOffset + done, b.data(), static_cast<u32>(count)) != count ||
            std::memcmp(a.data(), b.data(), count) != 0)
        {
            return false;
        }

        done += count;
    }

    return true;
}

// A run of the image's bytes copied into a file
bool CopyOut(Image& image, u64 offset, u64 size, const fs::path& file, std::vector<u8>& buffer)
{
    Writer out(file);
    for (u64 done = 0; done < size;)
    {
        const size_t count = static_cast<size_t>(std::min<u64>(Chunk, size - done));
        if (!image.Read(offset + done, buffer.data(), count) || !out.Write(buffer.data(), count))
        {
            return false;
        }

        done += count;
    }

    return out.Close();
}

// A path inside the mod's folder that stays inside it ("a/b.txt"; never "..", "/x", "C:x")
bool SafePath(const std::string& name, fs::path* out)
{
    std::string clean;
    if (!te::modimport::ZipReader::SafeName(name, &clean))
    {
        return false;
    }

    *out = FromUtf8(clean);
    return true;
}

// The image's files that differ from the player's disc into the folder, as a Mod Crate's layers
bool ImportImage(const fs::path& source, const fs::path& folder, const Native::Mods::ImportProgress& progress, std::string* error)
{
    Image image;
    if (!image.Open(source, error))
    {
        return false;
    }

    u64 total = 0;
    for (const ImageFile& file : image.Files())
    {
        total += file.size;
    }

    const ImageFile* table = nullptr;
    const ImageFile* data = nullptr;
    for (const ImageFile& file : image.Files())
    {
        table = Upper(file.path) == "CRASH6/CRASH.BH" ? &file : table;
        data = Upper(file.path) == "CRASH6/CRASH.BD" ? &file : data;
    }

    if (table == nullptr || data == nullptr)
    {
        *error = "it isn't a disc image of Crash Twinsanity (no CRASH6\\CRASH.BH and CRASH.BD)";
        return false;
    }

    std::vector<u8> a(Chunk);
    std::vector<u8> b(Chunk);
    u64 done = 0;
    u32 discFiles = 0;
    u32 archiveFiles = 0;
    auto step = [&](const char* stage) {
        if (progress)
        {
            progress(stage, total != 0 ? static_cast<double>(done) / static_cast<double>(total) : 0.0);
        }
    };

    // The disc's files, each whole when it differs (the archive's two apart)
    for (const ImageFile& file : image.Files())
    {
        if (&file == table || &file == data)
        {
            continue;
        }

        step("comparing the disc's files");
        Native::Disc::Entry original{};
        const bool known = Native::Disc::FindOriginal(file.path.c_str(), &original);
        if (!known || original.size != file.size || !Same(image, static_cast<u64>(file.sector) * Sector, original.offset, file.size, a, b))
        {
            fs::path inside;
            if (!SafePath(file.path, &inside) || !CopyOut(image, static_cast<u64>(file.sector) * Sector, file.size, folder / "layer0" / inside, a))
            {
                *error = file.path + " can't be written into the mods folder (is the disc full?)";
                return false;
            }

            discFiles++;
        }

        done += file.size;
    }

    // The archive's files by name through both tables: the ones that differ (or the disc's archive hasn't)
    std::vector<u8> theirs(table->size);
    Native::Disc::Entry ourTable{};
    Native::Disc::Entry ourData{};
    if (!image.Read(static_cast<u64>(table->sector) * Sector, theirs.data(), theirs.size()) ||
        !Native::Disc::FindOriginal("CRASH6\\CRASH.BH", &ourTable) || !Native::Disc::FindOriginal("CRASH6\\CRASH.BD", &ourData))
    {
        *error = "its archive's table can't be read";
        return false;
    }

    std::vector<u8> ours(ourTable.size);
    Native::Disc::Read(ourTable.offset, ours.data(), ourTable.size);
    std::map<std::string, TableFile> disc;
    for (const TableFile& file : TableFiles(ours))
    {
        disc[KeyOf(file.name)] = file;
    }

    for (const TableFile& file : TableFiles(theirs))
    {
        step("comparing the archive's files");
        if (static_cast<u64>(file.start) + file.size > data->size)
        {
            continue;
        }

        auto found = disc.find(KeyOf(file.name));
        const u64 at = static_cast<u64>(data->sector) * Sector + file.start;
        if (found == disc.end() || found->second.size != file.size ||
            !Same(image, at, ourData.offset + found->second.start, file.size, a, b))
        {
            std::string name = file.name;
            std::replace(name.begin(), name.end(), '\\', '/');
            fs::path inside;
            if (!SafePath(name, &inside) || !CopyOut(image, at, file.size, folder / "layer1" / inside, a))
            {
                *error = name + " can't be written into the mods folder (is the disc full?)";
                return false;
            }

            archiveFiles++;
        }

        done += file.size;
    }

    done = total;
    step("done");
    if (discFiles + archiveFiles == 0)
    {
        *error = "it's the same as your disc: nothing to import";
        return false;
    }

    WriteText(folder / "ModCrateInfo.txt", "Name=" + Utf8(source.stem()) + "\nDescription=The files of " + Utf8(source.filename()) +
                                                   " that differ from your disc\nGame=CrashTS\n");
    Native::Log("mods: %s: %u of the disc's files and %u of the archive's differ from your disc", Utf8(source.filename()).c_str(),
                discFiles, archiveFiles);
    return true;
}

// A zip unpacked into the folder (its one top folder, if every file is in it, left out)
bool ImportZip(const fs::path& source, const fs::path& folder, const Native::Mods::ImportProgress& progress, std::string* error)
{
    te::modimport::ZipReader zip;
    if (!zip.Open(Utf8(source), error))
    {
        return false;
    }

    std::string top;
    bool oneTop = true;
    u64 total = 0;
    for (const auto& entry : zip.Entries())
    {
        if (entry.directory)
        {
            continue;
        }

        total += entry.size;
        const size_t slash = entry.name.find('/');
        const std::string first = slash == std::string::npos ? std::string() : entry.name.substr(0, slash + 1);
        if (first.empty() || (!top.empty() && first != top))
        {
            oneTop = false;
        }

        top = top.empty() ? first : top;
    }

    u64 done = 0;
    u32 files = 0;
    for (const auto& entry : zip.Entries())
    {
        if (entry.directory)
        {
            continue;
        }

        const std::string name = oneTop ? entry.name.substr(top.size()) : entry.name;
        fs::path inside;
        if (!SafePath(name, &inside))
        {
            Native::Log("mods: %s: %s left out (its name leads out of the folder)", Utf8(source.filename()).c_str(), entry.name.c_str());
            continue;
        }

        Writer out(folder / inside);
        auto sink = [&](const uint8_t* bytes, size_t size) {
            done += size;
            if (progress)
            {
                progress("unpacking", total != 0 ? static_cast<double>(done) / static_cast<double>(total) : 0.0);
            }

            return out.Write(bytes, size);
        };
        if (!zip.Extract(entry, sink, error) || !out.Close())
        {
            if (error->empty())
            {
                *error = name + " can't be written into the mods folder (is the disc full?)";
            }

            return false;
        }

        files++;
    }

    if (files == 0)
    {
        *error = "it has no files";
        return false;
    }

    Native::Log("mods: %s: %u files unpacked", Utf8(source.filename()).c_str(), files);
    return true;
}
}

std::vector<fs::path> Native::Mods::Importables()
{
    std::vector<fs::path> found;
    std::error_code error;
    const fs::path folder = Folder();
    for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
    {
        if (!it->is_regular_file(error) || SourceOf(it->path()) == Source::None)
        {
            continue;
        }

        const fs::path mod = folder / it->path().stem();
        if (ReadText(mod / ImportedFile) != Mark(it->path()))
        {
            found.push_back(it->path());
        }
    }

    std::sort(found.begin(), found.end());
    return found;
}

bool Native::Mods::Import(const fs::path& source, const ImportProgress& progress, std::string* error)
{
    const fs::path folder = Folder();
    const fs::path mod = folder / source.stem();
    const fs::path working = folder / (".importing-" + Utf8(source.stem()));
    std::error_code ignored;
    if (fs::exists(mod, ignored) && !fs::exists(mod / ImportedFile, ignored))
    {
        *error = "the mods folder has a folder named " + Utf8(source.stem()) + " already, not made by an import: rename one of them";
        return false;
    }

    fs::remove_all(working, ignored);
    Log("mods: importing %s", Utf8(source.filename()).c_str());
    const auto started = std::chrono::steady_clock::now();
    const bool done = SourceOf(source) == Source::Zip ? ImportZip(source, working, progress, error)
                                                      : ImportImage(source, working, progress, error);
    if (!done || !WriteText(working / ImportedFile, Mark(source)))
    {
        fs::remove_all(working, ignored);
        Log("mods: %s not imported: %s", Utf8(source.filename()).c_str(), error->c_str());
        return false;
    }

    fs::remove_all(mod, ignored);
    std::error_code moved;
    fs::rename(working, mod, moved);
    if (moved)
    {
        *error = "the imported folder can't take its place (" + moved.message() + ")";
        fs::remove_all(working, ignored);
        Log("mods: %s not imported: %s", Utf8(source.filename()).c_str(), error->c_str());
        return false;
    }

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    Log("mods: %s imported into the mods folder's %s in %.1f s: used from the next start (the source may be deleted)",
        Utf8(source.filename()).c_str(), Utf8(source.stem()).c_str(), seconds);
    return true;
}

s32 Native::Mods::ImportAll()
{
    s32 failed = 0;
    const std::vector<fs::path> sources = Importables();
    if (sources.empty())
    {
        std::printf("mods: nothing to import in %s\n", LogPath(Utf8(Folder())).c_str());
    }

    for (const fs::path& source : sources)
    {
        int shown = -1;
        auto progress = [&](const std::string& stage, double fraction) {
            const int tenth = static_cast<int>(fraction * 10.0);
            if (tenth != shown)
            {
                shown = tenth;
                std::printf("%s: %s %d%%\n", Utf8(source.filename()).c_str(), stage.c_str(), tenth * 10);
                std::fflush(stdout);
            }
        };
        std::string error;
        if (Import(source, progress, &error))
        {
            std::printf("%s: imported\n", Utf8(source.filename()).c_str());
        }
        else
        {
            std::printf("%s: not imported: %s\n", Utf8(source.filename()).c_str(), error.c_str());
            failed++;
        }
    }

    std::fflush(stdout);
    return failed;
}
