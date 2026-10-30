// The Xbox's button glyphs, read from the user's own Xbox disc at run time (nothing of it is in the repository or the app): the disc
// image an optional setting names (Native::SettingsFolder()/xbox_disc.txt, local.json's "xbox_disc_image" or $TWINSANITY_XBOX_ISO),
// read as XDVDFS; its font, Startup\Fonts\Crash.psf, whose glyphs have the PS2's codes ('\' A, ']' B, '[' X, '^' Y, '{' the left
// trigger, '}' the right trigger, 0xA6 the white button (L), 0xAC the black one (R)) on pages of raw 32 bit BGRA kept bottom row
// first. The Switch's glyphs are made from them: the same shapes in neutral grey, by the Switch pad's own letters
#include "ui/bindings.h"
#include "ui/overlayinternal.h"

#include "native.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace NativeUi
{
namespace
{
constexpr u32 Sector = 2048;
constexpr u32 HeaderOffset = 0x10000;
constexpr const char Magic[] = "MICROSOFT*XBOX*MEDIA";
constexpr const char* FontPath = "Startup/Fonts/Crash.psf";

u32 Le32(const u8* at)
{
    return static_cast<u32>(at[0]) | static_cast<u32>(at[1]) << 8 | static_cast<u32>(at[2]) << 16 | static_cast<u32>(at[3]) << 24;
}

u16 Le16(const u8* at)
{
    return static_cast<u16>(at[0] | at[1] << 8);
}

f32 LeF32(const u8* at)
{
    u32 bits = Le32(at);
    f32 value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string Lower(std::string text)
{
    for (char& c : text)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return text;
}

// The disc image the settings name (none: no Xbox art)
std::string XboxDiscPath()
{
    if (const char* environment = std::getenv("TWINSANITY_XBOX_ISO"); environment != nullptr && environment[0] != '\0')
    {
        return environment;
    }

    std::ifstream setting(Native::SettingsFolder() + "/xbox_disc.txt");
    std::string line;
    if (setting && std::getline(setting, line) && !line.empty())
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
        {
            line.pop_back();
        }

        return line;
    }

    // local.json's "xbox_disc_image" (a plain string value)
    std::ifstream json("local.json");
    std::stringstream text;
    text << json.rdbuf();
    std::string content = text.str();
    size_t key = content.find("\"xbox_disc_image\"");
    if (key == std::string::npos)
    {
        return {};
    }

    size_t open = content.find('"', content.find(':', key) + 1);
    size_t close = open == std::string::npos ? std::string::npos : content.find('"', open + 1);
    return close == std::string::npos ? std::string() : content.substr(open + 1, close - open - 1);
}

// An XDVDFS image: its directories' binary trees of entries (u16 left and right in dwords, u32 sector, u32 size, u8 attributes, u8
// the name's length, the name)
class Xiso
{
public:
    bool Open(const std::string& path)
    {
        file.open(path, std::ios::binary);
        std::vector<u8> header = Read(HeaderOffset, Sector);
        if (header.size() < 28 || std::memcmp(header.data(), Magic, sizeof(Magic) - 1) != 0)
        {
            return false;
        }

        rootSector = Le32(&header[20]);
        rootSize = Le32(&header[24]);
        return true;
    }

    std::vector<u8> Read(u64 offset, u32 size)
    {
        std::vector<u8> bytes(size);
        file.clear();
        file.seekg(static_cast<std::streamoff>(offset));
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        bytes.resize(static_cast<size_t>(file.gcount()));
        return bytes;
    }

    // A file's contents by its path ("/" between its parts, any case)
    bool ReadFile(const std::string& path, std::vector<u8>* out)
    {
        u32 sector = rootSector;
        u32 size = rootSize;
        std::stringstream parts(path);
        std::string part;
        bool directory = true;
        while (std::getline(parts, part, '/'))
        {
            if (!directory || !Find(sector, size, Lower(part), &sector, &size, &directory))
            {
                return false;
            }
        }

        if (directory)
        {
            return false;
        }

        *out = Read(static_cast<u64>(sector) * Sector, size);
        return out->size() == size;
    }

private:
    bool Find(u32 sector, u32 size, const std::string& name, u32* foundSector, u32* foundSize, bool* directory)
    {
        std::vector<u8> entries = Read(static_cast<u64>(sector) * Sector, size);
        std::vector<u32> pending = {0};
        while (!pending.empty())
        {
            u32 at = pending.back() * 4;
            pending.pop_back();
            if (at + 14 > entries.size() || Le16(&entries[at]) == 0xFFFF)
            {
                continue;
            }

            u16 left = Le16(&entries[at]);
            u16 right = Le16(&entries[at + 2]);
            u8 length = entries[at + 13];
            if (at + 14 + length > entries.size())
            {
                continue;
            }

            std::string entry(reinterpret_cast<const char*>(&entries[at + 14]), length);
            if (Lower(entry) == name)
            {
                *foundSector = Le32(&entries[at + 4]);
                *foundSize = Le32(&entries[at + 8]);
                *directory = (entries[at + 12] & 0x10) != 0;
                return true;
            }

            if (left != 0)
            {
                pending.push_back(left);
            }

            if (right != 0)
            {
                pending.push_back(right);
            }
        }

        return false;
    }

    std::ifstream file;
    u32 rootSector = 0;
    u32 rootSize = 0;
};

struct Page
{
    u32 width = 0;
    u32 height = 0;
    // BGRA, the bottom row first
    std::vector<u8> pixels;
};

struct Glyph
{
    f32 height;
    f32 width;
    f32 u0;
    f32 v0;
    f32 u1;
    f32 v1;
    s32 page;
};

// The Xbox's font (its PSF): pages of a texture (a 0x88 byte header: the source's length, 0xBBCC0000, log2 of the width and height,
// ... and at 0x84 the type: 0 raw BGRA, 2 DXT5) and a material each, then the glyphs (28 bytes each) from the first character
bool ReadFont(const std::vector<u8>& data, std::vector<Page>* pages, std::vector<Glyph>* glyphs, s32* first)
{
    auto need = [&](size_t at, size_t size) { return at + size <= data.size(); };
    size_t at = 0;
    if (!need(at, 4))
    {
        return false;
    }

    s32 count = static_cast<s32>(Le32(&data[at]));
    at += 4;
    for (s32 page = 0; page < count && page < 8; page++)
    {
        if (!need(at, 8 + 0x88))
        {
            return false;
        }

        at += 8;
        const u8* header = &data[at];
        u32 sourceLength = Le32(header);
        u32 width = 1u << Le16(header + 8);
        u32 height = 1u << Le16(header + 10);
        u32 type = Le32(header + 0x84);
        size_t length = type == 0 ? std::max<size_t>(static_cast<size_t>(width) * height * 4, sourceLength - 0x84)
                                  : static_cast<size_t>(std::max(1u, width / 4)) * std::max(1u, height / 4) * 16;
        if (Le32(header + 4) != 0xBBCC0000 || !need(at + 0x88, length))
        {
            return false;
        }

        Page read;
        read.width = width;
        read.height = height;
        if (type == 0)
        {
            read.pixels.assign(data.begin() + static_cast<long>(at + 0x88),
                               data.begin() + static_cast<long>(at + 0x88 + static_cast<size_t>(width) * height * 4));
        }

        pages->push_back(std::move(read));
        at += 0x88 + length;
        // The material: shader flags (8), DMA index (4), the name's length and the name, the shaders
        if (!need(at, 16))
        {
            return false;
        }

        u32 nameLength = Le32(&data[at + 12]);
        at += 16 + nameLength;
        if (!need(at, 4))
        {
            return false;
        }

        u32 shaders = Le32(&data[at]);
        at += 4;
        for (u32 shader = 0; shader < shaders; shader++)
        {
            if (!need(at, 4))
            {
                return false;
            }

            u32 shaderType = Le32(&data[at]);
            at += 4;
            at += shaderType == 23 ? 12 : shaderType == 26 ? 20 : shaderType == 28 ? 8 : (shaderType == 16 || shaderType == 17 || shaderType == 24) ? 4 : 0;
            if (!need(at, 34) || data[at + 29] != 0)
            {
                // A shader with an animation isn't a font's
                return false;
            }

            at += 34 + 48 + 8;
        }
    }

    if (!need(at, 8))
    {
        return false;
    }

    s32 glyphCount = static_cast<s32>(Le32(&data[at]));
    *first = static_cast<s32>(Le32(&data[at + 4]));
    at += 8;
    if (glyphCount <= 0 || !need(at, static_cast<size_t>(glyphCount) * 28))
    {
        return false;
    }

    for (s32 index = 0; index < glyphCount; index++, at += 28)
    {
        const u8* entry = &data[at];
        glyphs->push_back({LeF32(entry), LeF32(entry + 4), LeF32(entry + 8), LeF32(entry + 12), LeF32(entry + 16), LeF32(entry + 20),
                           static_cast<s32>(Le32(entry + 24))});
    }

    return true;
}

// A glyph's image, upright, as RGBA (red in the low byte)
bool GlyphImage(const std::vector<Page>& pages, const std::vector<Glyph>& glyphs, s32 first, u8 code, ButtonArt* art)
{
    s32 index = static_cast<s32>(code) - first;
    if (index < 0 || index >= static_cast<s32>(glyphs.size()))
    {
        return false;
    }

    const Glyph& glyph = glyphs[index];
    if (glyph.page < 1 || glyph.page > static_cast<s32>(pages.size()))
    {
        return false;
    }

    const Page& page = pages[glyph.page - 1];
    if (page.pixels.empty())
    {
        return false;
    }

    s32 left = static_cast<s32>(glyph.u0 * static_cast<f32>(page.width));
    s32 bottom = static_cast<s32>(glyph.v1 * static_cast<f32>(page.height));
    s32 width = static_cast<s32>(glyph.width + 0.5f);
    s32 height = static_cast<s32>(glyph.height + 0.5f);
    if (left < 0 || bottom < 0 || width <= 0 || height <= 0 || left + width > static_cast<s32>(page.width) ||
        bottom + height > static_cast<s32>(page.height))
    {
        return false;
    }

    art->width = width;
    art->height = height;
    art->pixels.assign(static_cast<size_t>(width) * height, 0);
    for (s32 y = 0; y < height; y++)
    {
        // The stored rows go up from the bottom
        s32 row = bottom + (height - 1 - y);
        for (s32 x = 0; x < width; x++)
        {
            const u8* bgra = &page.pixels[(static_cast<size_t>(row) * page.width + static_cast<size_t>(left + x)) * 4];
            art->pixels[static_cast<size_t>(y) * width + x] =
                static_cast<u32>(bgra[2]) | static_cast<u32>(bgra[1]) << 8 | static_cast<u32>(bgra[0]) << 16 | static_cast<u32>(bgra[3]) << 24;
        }
    }

    return true;
}

// The Xbox's glyphs by their labels, read once
bool g_Read = false;
bool g_Have = false;
ButtonArt g_Xbox[ButtonArtLabels];
ButtonArt g_Switch[ButtonArtLabels];

// The Xbox font's codes of the labels' glyphs (ButtonArtLabel's order: A B X Y, the shoulders, the triggers). The Xbox's
// shoulders were its white and black buttons, which have no glyph (its 0xA6 and 0xAC, "L" and "R", are the triggers again): a
// shoulder (0 here) has no art and stays a label in the font
constexpr u8 XboxCodes[ButtonArtLabels] = {'\\', ']', '[', '^', 0, 0, '{', '}'};

// The Switch's: the shape in neutral grey (the Switch's buttons aren't coloured), every glyph the same brightness (the Xbox's red and
// blue would be darker greys than its green and yellow)
ButtonArt Grey(const ButtonArt& art)
{
    constexpr u32 Brightness = 150;
    ButtonArt grey = art;
    u64 sum = 0;
    u64 count = 0;
    for (u32 pixel : art.pixels)
    {
        if ((pixel >> 24) > 0x80)
        {
            sum += ((pixel & 0xFF) * 77 + (pixel >> 8 & 0xFF) * 150 + (pixel >> 16 & 0xFF) * 29) >> 8;
            count++;
        }
    }

    f32 scale = count != 0 && sum != 0 ? static_cast<f32>(Brightness) / (static_cast<f32>(sum) / static_cast<f32>(count)) : 1.0f;
    for (u32& pixel : grey.pixels)
    {
        u32 luminance = ((pixel & 0xFF) * 77 + (pixel >> 8 & 0xFF) * 150 + (pixel >> 16 & 0xFF) * 29) >> 8;
        u32 value = std::min<u32>(255, static_cast<u32>(static_cast<f32>(luminance) * scale));
        pixel = (pixel & 0xFF000000u) | value | value << 8 | value << 16;
    }

    return grey;
}

void ReadArt()
{
    if (g_Read)
    {
        return;
    }

    g_Read = true;
    std::string path = XboxDiscPath();
    if (path.empty())
    {
        return;
    }

    Xiso disc;
    std::vector<u8> font;
    std::vector<Page> pages;
    std::vector<Glyph> glyphs;
    s32 first = 0;
    if (!disc.Open(path) || !disc.ReadFile(FontPath, &font) || !ReadFont(font, &pages, &glyphs, &first))
    {
        Native::Log("xbox art: %s isn't an Xbox disc image with Crash Twinsanity's font: the prompts are letters",
                    Native::LogPath(path).c_str());
        return;
    }

    for (u32 label = 0; label < ButtonArtLabels; label++)
    {
        if (XboxCodes[label] == 0)
        {
            continue;
        }

        if (!GlyphImage(pages, glyphs, first, XboxCodes[label], &g_Xbox[label]))
        {
            Native::Log("xbox art: no glyph %#x in the Xbox font: the prompts are letters", XboxCodes[label]);
            return;
        }

        g_Switch[label] = Grey(g_Xbox[label]);
    }

    g_Have = true;
    Native::Log("xbox art: the Xbox's button glyphs read from %s", Native::LogPath(path).c_str());
}
}

// The cursor's art: the Xbox's HUD Wumpa fruit, Startup/Icons.psm's one 32 x 64 texture (its textures' headers as the font's pages:
// 0xBBCC in the signature's high half, raw BGRA kept bottom row first), upright as RGBA
bool CursorArt(std::vector<u32>* pixels, s32* width, s32* height)
{
    std::string path = XboxDiscPath();
    Xiso disc;
    std::vector<u8> icons;
    if (path.empty() || !disc.Open(path) || !disc.ReadFile("Startup/Icons.psm", &icons))
    {
        return false;
    }

    for (size_t at = 0; at + 0x88 <= icons.size(); at++)
    {
        const u8* header = &icons[at];
        if ((Le32(header + 4) >> 16) != 0xBBCC || Le16(header + 8) != 5 || Le16(header + 10) != 6 || Le32(header + 0x84) != 0 ||
            Le32(header) != 32 * 64 * 4 + 0x84 || at + 0x88 + 32 * 64 * 4 > icons.size())
        {
            continue;
        }

        *width = 32;
        *height = 64;
        pixels->assign(32 * 64, 0);
        for (s32 y = 0; y < 64; y++)
        {
            for (s32 x = 0; x < 32; x++)
            {
                const u8* bgra = &icons[at + 0x88 + (static_cast<size_t>(63 - y) * 32 + static_cast<size_t>(x)) * 4];
                (*pixels)[static_cast<size_t>(y) * 32 + x] = static_cast<u32>(bgra[2]) | static_cast<u32>(bgra[1]) << 8 |
                                                              static_cast<u32>(bgra[0]) << 16 | static_cast<u32>(bgra[3]) << 24;
            }
        }

        return true;
    }

    return false;
}

bool ImageArt(OverlayImage image, std::vector<u32>* pixels, s32* width, s32* height)
{
    if (image == OverlayImage::Cursor)
    {
        return CursorArt(pixels, width, height);
    }

    if (image >= OverlayImage::MouseLeft && image < OverlayImage::Count)
    {
        return MouseArt(image, pixels, width, height);
    }

    u32 index = static_cast<u32>(image);
    u32 first = static_cast<u32>(OverlayImage::XboxA);
    if (index < first || index >= static_cast<u32>(OverlayImage::PlayStationCross))
    {
        return false;
    }

    bool xbox = index < static_cast<u32>(OverlayImage::SwitchA);
    u32 label = (index - first) % 4;
    const ButtonArt* art = GetButtonArt(xbox ? PromptXbox : PromptSwitch, static_cast<ButtonArtLabel>(ArtA + label));
    if (art == nullptr)
    {
        return false;
    }

    // Cropped to the button (the font's glyph cell has room under it): its opaque pixels' bounds
    s32 left = art->width;
    s32 top = art->height;
    s32 right = -1;
    s32 bottom = -1;
    for (s32 y = 0; y < art->height; y++)
    {
        for (s32 x = 0; x < art->width; x++)
        {
            if ((art->pixels[static_cast<size_t>(y) * art->width + x] >> 24) != 0)
            {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }

    if (right < left)
    {
        return false;
    }

    *width = right - left + 1;
    *height = bottom - top + 1;
    pixels->clear();
    for (s32 y = top; y <= bottom; y++)
    {
        const u32* row = art->pixels.data() + static_cast<size_t>(y) * art->width;
        pixels->insert(pixels->end(), row + left, row + right + 1);
    }

    return true;
}

bool OverlayImageAvailable(OverlayImage image)
{
    u32 index = static_cast<u32>(image);
    if (index >= static_cast<u32>(OverlayImage::PlayStationCross) || image == OverlayImage::Cursor)
    {
        // Drawn without art
        return true;
    }

    return HaveButtonArt();
}

bool HaveButtonArt()
{
    ReadArt();
    return g_Have;
}

const ButtonArt* GetButtonArt(PromptStyle style, ButtonArtLabel label)
{
    ReadArt();
    if (!g_Have || label >= ButtonArtLabels || (style != PromptXbox && style != PromptSwitch))
    {
        return nullptr;
    }

    const ButtonArt* art = style == PromptXbox ? &g_Xbox[label] : &g_Switch[label];
    return art->pixels.empty() ? nullptr : art;
}

void ForgetButtonArtForTest()
{
    g_Read = false;
    g_Have = false;
}
}
