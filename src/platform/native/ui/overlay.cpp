// The overlay's frames and fonts: a frame recorded on the game's thread and handed to the presenter's (overlayrender.cpp draws it),
// the bundled fonts (native/third_party/fonts) read with stb_truetype for their (native/third_party/fonts/README.md: Luckiest Guy under the Apache License 2.0, Fredoka under the SIL Open Font License),
// and the window's points mapped into the 1280 x 720 reference space
#include "ui/overlayinternal.h"

#include "native.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../../../native/third_party/stb/stb_truetype.h"

namespace NativeUi
{
// ------------------------------------------------------------------------------------------------------------------- the fonts
namespace
{
struct FontFile
{
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool loaded = false;
    f32 ascent = 0.0f;
    f32 descent = 0.0f;
    f32 lineGap = 0.0f;
};

FontFile g_Fonts[static_cast<u32>(OverlayFont::Count)];
std::once_flag g_FontsLoaded;

bool ReadFile(const std::string& path, std::vector<unsigned char>* out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    out->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !out->empty();
}

void LoadFonts()
{
    // Where the fonts may be: $TWIN_UI_FONTS, the repository's folder (run from its root), the app's resources
    std::vector<std::string> folders;
    if (const char* folder = std::getenv("TWIN_UI_FONTS"); folder != nullptr)
    {
        folders.push_back(std::string(folder) + "/");
    }

    folders.push_back("native/third_party/fonts/");
    if (const char* base = SDL_GetBasePath(); base != nullptr)
    {
        folders.push_back(std::string(base) + "../Resources/fonts/");
        folders.push_back(std::string(base) + "fonts/");
    }

    // Each face's file (Fredoka's weights its static instances: native/tools/instance_fonts.py)
    const std::vector<std::string> names[static_cast<u32>(OverlayFont::Count)] = {
        {"LuckiestGuy-Regular.ttf"},
        {"Fredoka-SemiBold.ttf"},
        {"Fredoka-Bold.ttf"},
    };
    for (u32 face = 0; face < static_cast<u32>(OverlayFont::Count); face++)
    {
        FontFile& font = g_Fonts[face];
        std::string used;
        for (const std::string& folder : folders)
        {
            for (const std::string& name : names[face])
            {
                if (used.empty() && ReadFile(folder + name, &font.data))
                {
                    used = folder + name;
                }
            }
        }

        if (used.empty() || !stbtt_InitFont(&font.info, font.data.data(), stbtt_GetFontOffsetForIndex(font.data.data(), 0)))
        {
            Native::Log("overlay: no font for face %u (native/third_party/fonts)", face);
            continue;
        }

        int ascent;
        int descent;
        int lineGap;
        stbtt_GetFontVMetrics(&font.info, &ascent, &descent, &lineGap);
        font.ascent = static_cast<f32>(ascent);
        font.descent = static_cast<f32>(descent);
        font.lineGap = static_cast<f32>(lineGap);
        font.loaded = true;
        Native::Log("overlay: face %u from %s", face, Native::LogPath(used).c_str());
    }
}
}

const stbtt_fontinfo* OverlayFontInfo(OverlayFont face)
{
    std::call_once(g_FontsLoaded, LoadFonts);
    const FontFile& font = g_Fonts[static_cast<u32>(face)];
    return font.loaded ? &font.info : nullptr;
}

f32 OverlayFontScale(OverlayFont face, f32 size)
{
    const stbtt_fontinfo* info = OverlayFontInfo(face);
    return info != nullptr ? stbtt_ScaleForMappingEmToPixels(info, size) : 0.0f;
}

void OverlayFontMetrics(OverlayFont face, f32 size, f32* ascent, f32* descent, f32* lineGap)
{
    const FontFile& font = g_Fonts[static_cast<u32>(face)];
    f32 scale = OverlayFontScale(face, size);
    *ascent = font.ascent * scale;
    *descent = font.descent * scale;
    *lineGap = font.lineGap * scale;
}

std::u32string GameTextCodepoints(const std::string& gameText)
{
    // The game's texts are Windows-1252 (its own font's codes): Latin-1's, and 0x80 to 0x9F's (the quotes, the dashes, the ellipsis,
    // the angle quotes ...; the five codes Windows-1252 leaves out stay themselves)
    static constexpr char32_t Windows1252[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
    };
    std::u32string out;
    for (unsigned char c : gameText)
    {
        out.push_back(c >= 0x80 && c < 0xA0 ? Windows1252[c - 0x80] : static_cast<char32_t>(c));
    }

    return out;
}

f32 OverlayTextWidth(const std::string& gameText, const TextStyle& style)
{
    const stbtt_fontinfo* info = OverlayFontInfo(style.font);
    if (info == nullptr)
    {
        return 0.0f;
    }

    f32 scale = OverlayFontScale(style.font, style.size);
    std::u32string text = GameTextCodepoints(gameText);
    f32 width = 0.0f;
    for (size_t i = 0; i < text.size(); i++)
    {
        int advance;
        int bearing;
        stbtt_GetCodepointHMetrics(info, static_cast<int>(text[i]), &advance, &bearing);
        width += static_cast<f32>(advance) * scale + style.letterSpacing;
        if (i + 1 < text.size())
        {
            width += static_cast<f32>(stbtt_GetCodepointKernAdvance(info, static_cast<int>(text[i]), static_cast<int>(text[i + 1]))) *
                     scale;
        }
    }

    return width - (text.empty() ? 0.0f : style.letterSpacing);
}

f32 OverlayLineHeight(const TextStyle& style)
{
    f32 ascent;
    f32 descent;
    f32 lineGap;
    OverlayFontMetrics(style.font, style.size, &ascent, &descent, &lineGap);
    return ascent - descent + lineGap;
}

// ------------------------------------------------------------------------------------------------------------------- the frames
namespace
{
std::vector<OverlayFrame::Command> g_Recording;
std::vector<OverlayFrame::Command> g_Submitted;
std::mutex g_SubmitLock;
OverlayFrame g_Frame;

// The window as the overlay's hook saw it last
std::atomic<int> g_WindowPixelWidth{0};
std::atomic<int> g_WindowPixelHeight{0};
std::atomic<float> g_PixelScale{1.0f};
}

std::vector<OverlayFrame::Command>& OverlayFrame::Commands()
{
    return g_Recording;
}

void OverlayFrame::Clear()
{
    g_Recording.clear();
}

bool OverlayFrame::Empty() const
{
    return g_Recording.empty();
}

void OverlayFrame::Backdrop(f32 seconds)
{
    Command command{};
    command.kind = CommandKind::Backdrop;
    command.seconds = seconds;
    g_Recording.push_back(command);
}

void OverlayFrame::Box(f32 x, f32 y, f32 width, f32 height, const BoxStyle& style)
{
    Command command{};
    command.kind = CommandKind::Box;
    command.x = x;
    command.y = y;
    command.width = width;
    command.height = height;
    command.box = style;
    g_Recording.push_back(command);
}

void OverlayFrame::Text(const std::string& gameText, f32 x, f32 y, const TextStyle& style, OverlayAlign align)
{
    if (gameText.empty())
    {
        return;
    }

    Command command{};
    command.kind = CommandKind::Text;
    f32 width = OverlayTextWidth(gameText, style);
    command.x = align == OverlayAlign::Left ? x : align == OverlayAlign::Right ? x - width : x - width * 0.5f;
    command.y = y;
    command.width = width;
    command.text = GameTextCodepoints(gameText);
    command.textStyle = style;
    g_Recording.push_back(command);
}

void OverlayFrame::Image(OverlayImage image, f32 x, f32 y, f32 width, f32 height, f32 alpha)
{
    Command command{};
    command.kind = CommandKind::Image;
    command.image = image;
    command.x = x;
    command.y = y;
    command.width = width;
    command.height = height;
    command.alpha = alpha;
    g_Recording.push_back(command);
}

void OverlayFrame::Clip(f32 x, f32 y, f32 width, f32 height)
{
    Command command{};
    command.kind = CommandKind::Clip;
    command.x = x;
    command.y = y;
    command.width = width;
    command.height = height;
    g_Recording.push_back(command);
}

void OverlayFrame::NoClip()
{
    Command command{};
    command.kind = CommandKind::NoClip;
    g_Recording.push_back(command);
}

OverlayFrame& RecordingFrame()
{
    return g_Frame;
}

void SubmitFrame()
{
    std::lock_guard<std::mutex> lock(g_SubmitLock);
    g_Submitted = g_Recording;
}

std::vector<OverlayFrame::Command> SubmittedFrame()
{
    std::lock_guard<std::mutex> lock(g_SubmitLock);
    return g_Submitted;
}

void NoteWindow(int pixelWidth, int pixelHeight, float pixelScale)
{
    g_WindowPixelWidth = pixelWidth;
    g_WindowPixelHeight = pixelHeight;
    g_PixelScale = pixelScale;
}

void ReferenceMapping(f32 pixelWidth, f32 pixelHeight, f32* scale, f32* offsetX, f32* offsetY)
{
    *scale = std::min(pixelWidth / ReferenceWidth, pixelHeight / ReferenceHeight);
    *offsetX = (pixelWidth - ReferenceWidth * *scale) * 0.5f;
    *offsetY = (pixelHeight - ReferenceHeight * *scale) * 0.5f;
}

void WindowToReference(f32 x, f32 y, f32* referenceX, f32* referenceY)
{
    int width = g_WindowPixelWidth;
    int height = g_WindowPixelHeight;
    if (width <= 0 || height <= 0)
    {
        *referenceX = -1.0f;
        *referenceY = -1.0f;
        return;
    }

    f32 scale;
    f32 offsetX;
    f32 offsetY;
    ReferenceMapping(static_cast<f32>(width), static_cast<f32>(height), &scale, &offsetX, &offsetY);
    f32 pixelScale = g_PixelScale;
    *referenceX = (x * pixelScale - offsetX) / scale;
    *referenceY = (y * pixelScale - offsetY) / scale;
}
}
