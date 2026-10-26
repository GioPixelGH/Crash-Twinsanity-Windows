// Replacement textures in PCSX2's format (replace.h). The names are worked out as native/research/HD_TEXTURES.md describes
// PCSX2's GSTextureReplacements and GSTextureCache (written from that description):
//
// - the texture's hash: XXH3-64 (seed 0) of its 256 byte GS blocks as local memory holds them, a row of blocks after another
//   (formats whose every bit is the texel's, at least a block each way, no region), or else of its texels unswizzled: an index
//   byte each for paletted formats, RGBA (TEXA applied) for direct ones, a row of the texture's width after another
// - the palette's hash: XXH3-64 of its 16 or 256 colours in index order as RGBA (TEXA applied to 16 bit ones)
// - the packed word: PSM | TW << 6 | TH << 10 | (TEXA's TA0 << 15 | AEM << 23 | TA1 << 24 for 24 and 16 bit formats); old names'
//   bit 14 is ignored

#include "replace.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

#include "../gs/backend.h"
#include "../gs/drawstate.h"
#include "../gs/gs.h"
#include "native.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <atomic>
#include <memory>
#include <mutex>

namespace NativeGraphics
{
bool ReplacementKey::operator<(const ReplacementKey& other) const
{
    return std::tie(textureHash, clutHash, bits, regionWidth, regionHeight) <
           std::tie(other.textureHash, other.clutHash, other.bits, other.regionWidth, other.regionHeight);
}

namespace
{
constexpr u32 TccBit = 1u << 14;

// The options' switch is on the game's thread, the lookups on the renderer's: the pack is scanned once (the first time it's turned
// on) and its images never freed after (FindReplacement's pointers stay good), the switch an atomic, the maps under a lock
std::atomic<bool> g_Enabled{false};
bool g_Scanned = false;
std::mutex g_Lock;
std::map<ReplacementKey, std::string> g_Files;
std::map<ReplacementKey, std::unique_ptr<ReplacementImage>> g_Images;

std::string Lower(std::string text)
{
    for (char& c : text)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return text;
}

// A file name parsed in PCSX2's order of patterns; false if none fits (a pattern counts only with "." right after it)
bool ParseName(const std::string& name, ReplacementKey* key)
{
    unsigned long long texture = 0;
    unsigned long long clut = 0;
    unsigned long long oldRegion = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int bits = 0;
    int end = -1;
    const char* text = name.c_str();
    auto ends = [&]() { return end > 0 && text[end] == '.'; };
    *key = {};
    end = -1;
    if (std::sscanf(text, "%llx-%llx-r%ux%u-%x%n", &texture, &clut, &width, &height, &bits, &end) == 5 && ends())
    {
        *key = {texture, clut, bits & ~TccBit, static_cast<u16>(width), static_cast<u16>(height)};
        return true;
    }

    end = -1;
    if (std::sscanf(text, "%llx-r%ux%u-%x%n", &texture, &width, &height, &bits, &end) == 4 && ends())
    {
        *key = {texture, 0, bits & ~TccBit, static_cast<u16>(width), static_cast<u16>(height)};
        return true;
    }

    // The old region form: the region's packed corners (x min/max in bits 0-15 and 16-31, y in 32-47 and 48-63)
    auto oldSize = [&]() {
        width = static_cast<u32>(((oldRegion >> 16) & 0xFFFF) - (oldRegion & 0xFFFF));
        height = static_cast<u32>(((oldRegion >> 48) & 0xFFFF) - ((oldRegion >> 32) & 0xFFFF));
    };
    end = -1;
    if (std::sscanf(text, "%llx-%llx-r%llx-%x%n", &texture, &clut, &oldRegion, &bits, &end) == 4 && ends())
    {
        oldSize();
        *key = {texture, clut, bits & ~TccBit, static_cast<u16>(width), static_cast<u16>(height)};
        return true;
    }

    end = -1;
    if (std::sscanf(text, "%llx-r%llx-%x%n", &texture, &oldRegion, &bits, &end) == 3 && ends())
    {
        oldSize();
        *key = {texture, 0, bits & ~TccBit, static_cast<u16>(width), static_cast<u16>(height)};
        return true;
    }

    end = -1;
    if (std::sscanf(text, "%llx-%llx-%x%n", &texture, &clut, &bits, &end) == 3 && ends())
    {
        *key = {texture, clut, bits & ~TccBit, 0, 0};
        return true;
    }

    end = -1;
    if (std::sscanf(text, "%llx-%x%n", &texture, &bits, &end) == 2 && ends())
    {
        *key = {texture, 0, bits & ~TccBit, 0, 0};
        return true;
    }

    return false;
}

void Scan()
{
    g_Files.clear();
    g_Images.clear();
    namespace fs = std::filesystem;
    std::error_code error;
    // The settings folder's textures (as PCSX2 lays them out), or $TWIN_TEXTURES_DIR's
    const char* overridden = std::getenv("TWIN_TEXTURES_DIR");
    fs::path root = overridden != nullptr ? fs::path(overridden) : fs::path(Native::SettingsFolder()) / "textures";
    if (!fs::is_directory(root, error))
    {
        return;
    }

    // The PAL disc's serial and the others whose packs share its hashes
    static const char* Serials[] = {"sles-52568", "slus-20909", "slpm-65801"};
    for (const auto& serial : fs::directory_iterator(root, error))
    {
        std::string name = Lower(serial.path().filename().string());
        if (!serial.is_directory(error) || std::find(std::begin(Serials), std::end(Serials), name) == std::end(Serials))
        {
            continue;
        }

        for (const auto& sub : fs::directory_iterator(serial.path(), error))
        {
            if (!sub.is_directory(error) || Lower(sub.path().filename().string()) != "replacements")
            {
                continue;
            }

            for (auto it = fs::recursive_directory_iterator(sub.path(), fs::directory_options::follow_directory_symlink, error);
                 it != fs::recursive_directory_iterator(); it.increment(error))
            {
                if (error || !it->is_regular_file(error))
                {
                    continue;
                }

                std::string extension = Lower(it->path().extension().string());
                if (extension != ".png" && extension != ".dds")
                {
                    continue;
                }

                ReplacementKey key;
                if (ParseName(it->path().filename().string(), &key))
                {
                    g_Files.emplace(key, it->path().string());
                }
            }
        }
    }

    Native::Log("texture pack: %zu replacement textures in %s", g_Files.size(), root.string().c_str());
}

// A block's size in texels by format
void BlockSize(u32 psm, u32* width, u32* height)
{
    switch (psm)
    {
    case Gs::PSMCT16:
    case Gs::PSMCT16S:
    case Gs::PSMZ16:
    case Gs::PSMZ16S:
        *width = 16;
        *height = 8;
        break;
    case Gs::PSMT8:
        *width = 16;
        *height = 16;
        break;
    case Gs::PSMT4:
        *width = 32;
        *height = 16;
        break;
    default:
        *width = 8;
        *height = 8;
        break;
    }
}

bool Paletted(u32 psm)
{
    return psm == Gs::PSMT8 || psm == Gs::PSMT4 || psm == Gs::PSMT8H || psm == Gs::PSMT4HL || psm == Gs::PSMT4HH;
}

// Formats whose every stored bit is the texel's (FMSK all ones)
bool WholeBits(u32 psm)
{
    return psm == Gs::PSMCT32 || psm == Gs::PSMZ32 || psm == Gs::PSMT8 || psm == Gs::PSMT4;
}

// The block (0-16383) a texel is in
u32 BlockOf(u32 psm, u32 x, u32 y, u32 bp, u32 bw)
{
    switch (psm)
    {
    case Gs::PSMZ32:
        return Gs::PixelAddress32Z(x, y, bp, bw) >> 6;
    case Gs::PSMT8:
        return Gs::PixelAddress8(x, y, bp, bw) >> 8;
    case Gs::PSMT4:
        return Gs::PixelAddress4(x, y, bp, bw) >> 9;
    default:
        return Gs::PixelAddress32(x, y, bp, bw) >> 6;
    }
}

// BC1-3 decoding (4x4 blocks of 8 or 16 bytes)
void DecodeColourBlock(const u8* block, u32* out, bool alphaFromColour)
{
    u16 c0 = static_cast<u16>(block[0] | block[1] << 8);
    u16 c1 = static_cast<u16>(block[2] | block[3] << 8);
    auto expand = [](u16 c) {
        u32 r = (c >> 11) & 31;
        u32 g = (c >> 5) & 63;
        u32 b = c & 31;
        return (r << 3 | r >> 2) | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2) << 16;
    };
    u32 colours[4];
    colours[0] = expand(c0) | 0xFF000000u;
    colours[1] = expand(c1) | 0xFF000000u;
    auto mix = [](u32 a, u32 b, u32 wa, u32 wb, u32 d) {
        u32 out = 0;
        for (u32 shift = 0; shift < 24; shift += 8)
        {
            out |= ((((a >> shift) & 0xFF) * wa + ((b >> shift) & 0xFF) * wb) / d) << shift;
        }

        return out;
    };
    if (c0 > c1 || !alphaFromColour)
    {
        colours[2] = mix(colours[0], colours[1], 2, 1, 3) | 0xFF000000u;
        colours[3] = mix(colours[0], colours[1], 1, 2, 3) | 0xFF000000u;
    }
    else
    {
        colours[2] = mix(colours[0], colours[1], 1, 1, 2) | 0xFF000000u;
        colours[3] = 0;
    }

    u32 indexes = static_cast<u32>(block[4] | block[5] << 8 | block[6] << 16 | block[7] << 24);
    for (u32 i = 0; i < 16; i++)
    {
        out[i] = colours[(indexes >> (i * 2)) & 3];
    }
}

bool LoadDds(const std::string& path, ReplacementImage* image)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        return false;
    }

    std::vector<u8> data;
    u8 buffer[65536];
    size_t got;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        data.insert(data.end(), buffer, buffer + got);
    }

    std::fclose(file);
    if (data.size() < 128 || std::memcmp(data.data(), "DDS ", 4) != 0)
    {
        return false;
    }

    auto read32 = [&](size_t at) { return static_cast<u32>(data[at] | data[at + 1] << 8 | data[at + 2] << 16 | data[at + 3] << 24); };
    u32 height = read32(12);
    u32 width = read32(16);
    u32 flags = read32(80);
    u32 fourCc = read32(84);
    u32 bitCount = read32(88);
    u32 masks[4] = {read32(92), read32(96), read32(100), read32(104)};
    size_t at = 128;
    u32 format = 0;
    auto four = [](const char* t) { return static_cast<u32>(t[0] | t[1] << 8 | t[2] << 16 | t[3] << 24); };
    if ((flags & 4) != 0)
    {
        if (fourCc == four("DXT1"))
        {
            format = 1;
        }
        else if (fourCc == four("DXT2") || fourCc == four("DXT3"))
        {
            format = 2;
        }
        else if (fourCc == four("DXT4") || fourCc == four("DXT5"))
        {
            format = 3;
        }
        else if (fourCc == four("DX10") && data.size() >= 148)
        {
            u32 dxgi = read32(128);
            at = 148;
            format = (dxgi == 71 || dxgi == 72) ? 1 : (dxgi == 74 || dxgi == 75) ? 2 : (dxgi == 77 || dxgi == 78) ? 3 : 0;
            if (format == 0)
            {
                Native::Log("texture pack: %s: a DDS format not read yet (DXGI %u)", path.c_str(), dxgi);
                return false;
            }
        }
        else
        {
            return false;
        }
    }

    image->width = width;
    image->height = height;
    image->pixels.assign(static_cast<size_t>(width) * height, 0);
    if (format != 0)
    {
        u32 blockBytes = format == 1 ? 8 : 16;
        u32 blocksWide = (width + 3) / 4;
        u32 blocksHigh = (height + 3) / 4;
        if (data.size() < at + static_cast<size_t>(blocksWide) * blocksHigh * blockBytes)
        {
            return false;
        }

        for (u32 by = 0; by < blocksHigh; by++)
        {
            for (u32 bx = 0; bx < blocksWide; bx++)
            {
                const u8* block = &data[at + (static_cast<size_t>(by) * blocksWide + bx) * blockBytes];
                u32 texels[16];
                DecodeColourBlock(format == 1 ? block : block + 8, texels, format == 1);
                if (format == 2)
                {
                    for (u32 i = 0; i < 16; i++)
                    {
                        u32 a = (block[i / 2] >> ((i & 1) * 4)) & 15;
                        texels[i] = (texels[i] & 0xFFFFFF) | (a * 17) << 24;
                    }
                }
                else if (format == 3)
                {
                    u32 a0 = block[0];
                    u32 a1 = block[1];
                    u32 alphas[8] = {a0, a1};
                    for (u32 i = 2; i < 8; i++)
                    {
                        alphas[i] = a0 > a1 ? ((8 - i) * a0 + (i - 1) * a1) / 7 : i < 6 ? ((6 - i) * a0 + (i - 1) * a1) / 5 : (i == 6 ? 0 : 255);
                    }

                    u64 bitsAlpha = 0;
                    for (u32 i = 0; i < 6; i++)
                    {
                        bitsAlpha |= static_cast<u64>(block[2 + i]) << (i * 8);
                    }

                    for (u32 i = 0; i < 16; i++)
                    {
                        texels[i] = (texels[i] & 0xFFFFFF) | alphas[(bitsAlpha >> (i * 3)) & 7] << 24;
                    }
                }

                for (u32 i = 0; i < 16; i++)
                {
                    u32 x = bx * 4 + (i & 3);
                    u32 y = by * 4 + (i >> 2);
                    if (x < width && y < height)
                    {
                        image->pixels[static_cast<size_t>(y) * width + x] = texels[i];
                    }
                }
            }
        }

        return true;
    }

    // Uncompressed: 32 bit with masks, or 24 bit
    u32 bytes = bitCount / 8;
    if ((bytes != 4 && bytes != 3) || data.size() < at + static_cast<size_t>(width) * height * bytes)
    {
        return false;
    }

    auto channel = [](u32 value, u32 mask) -> u32 {
        if (mask == 0)
        {
            return 0xFF;
        }

        u32 shift = static_cast<u32>(__builtin_ctz(mask));
        u32 v = (value & mask) >> shift;
        u32 max = mask >> shift;
        return max == 255 ? v : v * 255 / max;
    };
    bool hasAlpha = (flags & 1) != 0 && masks[3] != 0;
    for (size_t i = 0; i < static_cast<size_t>(width) * height; i++)
    {
        const u8* p = &data[at + i * bytes];
        u32 value = bytes == 4 ? static_cast<u32>(p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24)
                               : static_cast<u32>(p[0] | p[1] << 8 | p[2] << 16);
        u32 r = channel(value, masks[0]);
        u32 g = channel(value, masks[1]);
        u32 b = channel(value, masks[2]);
        u32 a = hasAlpha ? channel(value, masks[3]) : 0xFF;
        image->pixels[i] = r | g << 8 | b << 16 | a << 24;
    }

    return true;
}

bool LoadPng(const std::string& path, ReplacementImage* image)
{
    SDL_Surface* loaded = SDL_LoadPNG(path.c_str());
    if (loaded == nullptr)
    {
        return false;
    }

    SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (rgba == nullptr)
    {
        return false;
    }

    image->width = static_cast<u32>(rgba->w);
    image->height = static_cast<u32>(rgba->h);
    image->pixels.resize(static_cast<size_t>(image->width) * image->height);
    for (u32 y = 0; y < image->height; y++)
    {
        std::memcpy(&image->pixels[static_cast<size_t>(y) * image->width], static_cast<const u8*>(rgba->pixels) + y * rgba->pitch,
                    image->width * 4);
    }

    SDL_DestroySurface(rgba);
    return true;
}
}

void ReplacementsSetEnabled(bool on)
{
    if (on)
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        if (!g_Scanned)
        {
            g_Scanned = true;
            Scan();
        }
    }

    g_Enabled = on;
}

bool ReplacementsEnabled()
{
    return g_Enabled;
}

size_t ReplacementCount()
{
    std::lock_guard<std::mutex> lock(g_Lock);
    return g_Files.size();
}

bool ReplacementKeyFor(const Gs::Gs& gs, const Gs::TextureLevel& level, const Gs::RegTex0& tex0, const Gs::RegClamp& clamp,
                       ReplacementKey* key)
{
    const u32 psm = tex0.psm;
    const u32 tw = 1u << std::min<u32>(tex0.tw, 10);
    const u32 th = 1u << std::min<u32>(tex0.th, 10);
    // The region CLAMP sets (PCSX2's SourceRegion); textures with one aren't drawn by the hardware renderer yet
    (void)clamp;
    u32 blockWidth = 0;
    u32 blockHeight = 0;
    BlockSize(psm, &blockWidth, &blockHeight);
    XXH3_state_t* hash = XXH3_createState();
    XXH3_64bits_reset(hash);
    if (tw >= blockWidth && th >= blockHeight && WholeBits(psm))
    {
        // The blocks as local memory holds them, a row of blocks after another
        const u8* memory = gs.memory();
        for (u32 y = 0; y < th; y += blockHeight)
        {
            for (u32 x = 0; x < tw; x += blockWidth)
            {
                u32 block = BlockOf(psm, x, y, tex0.tbp0, tex0.tbw) & (Gs::BlockCount - 1);
                XXH3_64bits_update(hash, memory + static_cast<size_t>(block) * 256, 256);
            }
        }
    }
    else
    {
        // The texels unswizzled: an index byte each, or RGBA
        bool paletted = Paletted(psm);
        std::vector<u8> row(paletted ? tw : tw * 4);
        for (u32 y = 0; y < th; y++)
        {
            for (u32 x = 0; x < tw; x++)
            {
                if (paletted)
                {
                    row[x] = static_cast<u8>(gs.ReadPixel(psm, x, y, tex0.tbp0, tex0.tbw));
                }
                else
                {
                    u32 texel = Gs::BackendTexel(gs, level, x, y, tex0);
                    std::memcpy(&row[x * 4], &texel, 4);
                }
            }

            XXH3_64bits_update(hash, row.data(), row.size());
        }
    }

    key->textureHash = XXH3_64bits_digest(hash);
    XXH3_freeState(hash);
    key->clutHash = 0;
    if (Paletted(psm))
    {
        u32 entries = (psm == Gs::PSMT8 || psm == Gs::PSMT8H) ? 256 : 16;
        u32 clut[256];
        for (u32 i = 0; i < entries; i++)
        {
            clut[i] = Gs::BackendClutEntry(gs, i, tex0);
        }

        key->clutHash = XXH3_64bits(clut, entries * 4);
    }

    key->bits = psm | std::min<u32>(tex0.tw, 15) << 6 | std::min<u32>(tex0.th, 15) << 10;
    bool shortDirect = psm == Gs::PSMCT24 || psm == Gs::PSMCT16 || psm == Gs::PSMCT16S || psm == Gs::PSMZ24 || psm == Gs::PSMZ16 ||
                       psm == Gs::PSMZ16S;
    if (shortDirect)
    {
        Gs::RegTexa texa = gs.texa();
        key->bits |= static_cast<u32>(texa.ta0) << 15 | static_cast<u32>(texa.aem) << 23 | static_cast<u32>(texa.ta1) << 24;
    }

    key->regionWidth = 0;
    key->regionHeight = 0;
    return true;
}

std::string ReplacementName(const ReplacementKey& key)
{
    char name[96];
    if (key.clutHash != 0)
    {
        std::snprintf(name, sizeof(name), "%llx-%llx-%08x", static_cast<unsigned long long>(key.textureHash),
                      static_cast<unsigned long long>(key.clutHash), key.bits);
    }
    else
    {
        std::snprintf(name, sizeof(name), "%llx-%08x", static_cast<unsigned long long>(key.textureHash), key.bits);
    }

    return name;
}

const ReplacementImage* FindReplacement(const ReplacementKey& key)
{
    if (!g_Enabled)
    {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_Lock);
    if (g_Files.empty())
    {
        return nullptr;
    }

    auto loaded = g_Images.find(key);
    if (loaded != g_Images.end())
    {
        return loaded->second.get();
    }

    auto file = g_Files.find(key);
    if (file == g_Files.end())
    {
        return nullptr;
    }

    auto image = std::make_unique<ReplacementImage>();
    std::string lower = Lower(file->second);
    bool ok = lower.size() > 4 && lower.substr(lower.size() - 4) == ".dds" ? LoadDds(file->second, image.get())
                                                                           : LoadPng(file->second, image.get());
    if (!ok)
    {
        Native::Log("texture pack: the replacement %s can't be read", file->second.c_str());
        image.reset();
    }

    const ReplacementImage* result = image.get();
    g_Images[key] = std::move(image);
    return result;
}
}
