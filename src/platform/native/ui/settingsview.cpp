// The settings screen's view, as the approved design has it (native/design/options/SPEC.md, c_split.png and its source
// c_split.html + common.css): drawn on the overlay in its 1280 x 720 reference space at the window's own resolution. The blue
// backdrop with the Twinsanity spiral, "OPTIONS" in Luckiest Guy over "CRASH TWINSANITY", the tabs down the left, the white
// panel of rows (Fredoka; the selected row orange, its value's pill between chevrons), the description under it, the bar of
// keycaps (or the pad's buttons) at the bottom. Its parts are recorded as hit regions for the mouse. The game's menus' bar of
// prompts and the cursor are drawn here too
#include "ui/settingsmodel.h"
#include "ui/overlay.h"

#include "native.h"
#include "graphics/presenter.h"

#include "game/gamecontroller.h"
#include "game/language.h"
#include "game/oleg.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace NativeUi
{
namespace
{
// ------------------------------------------------------------------------------------------------------------- the design's values
constexpr Rgba32 Orange = Hex(0xff9a1f);
constexpr Rgba32 DeepOrange = Hex(0xff8a00);
constexpr Rgba32 Lip = Hex(0xffb43a);
constexpr Rgba32 Brown = Hex(0x3a1200);
constexpr Rgba32 TabBrown = Hex(0x4a1800);
constexpr Rgba32 Ink = Hex(0x173a6b);
constexpr Rgba32 PillBlue = Hex(0x1b6fd0);
constexpr Rgba32 PillFill = Hex(0xeaf2ff);
constexpr Rgba32 Separator = Hex(0xeef3fa);
constexpr Rgba32 White = Hex(0xffffff);
constexpr Rgba32 KeyInk = Hex(0x1b2340);

constexpr f32 PanelX = 360.0f;
constexpr f32 PanelY = 170.0f;
constexpr f32 PanelWidth = 860.0f;
constexpr f32 PanelHeight = 420.0f;
constexpr f32 PanelPadX = 18.0f;
constexpr f32 PanelPadY = 16.0f;
constexpr f32 RowHeight = 48.0f;
// Rows are 48 apart: the design's border-box rows, a 2px divider inside the top of each after the first
constexpr f32 RowPitch = 48.0f;
constexpr f32 RowPad = 22.0f;
constexpr f32 ValueWidth = 280.0f;
constexpr f32 ChevronGap = 14.0f;

std::string Text(u32 id)
{
    return GameText(id);
}

// The game's lower case made the design's: a sentence's first letter capitalised, or every letter (Windows-1252: Latin-1's)
unsigned char Upper(unsigned char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
    {
        return static_cast<unsigned char>(c - 0x20);
    }

    return c;
}

// The game's texts are lower case (its font draws capitals only); the new fonts' have both: names, units and initialisms set
// right ("hz" "Hz", "3d" "3D", "crt" "CRT", "xbox" "Xbox" ...) whole words in any language
std::string ProperWords(const std::string& text)
{
    static const std::pair<const char*, const char*> Words[] = {
        {"hz", "Hz"},           {"3d", "3D"},           {"2d", "2D"},       {"crt", "CRT"},           {"xbox", "Xbox"},
        {"playstation", "PlayStation"}, {"nintendo", "Nintendo"}, {"switch", "Switch"}, {"dolby", "Dolby"}, {"pro logic ii", "Pro Logic II"},
        {"v-sync", "V-sync"},   {"esc", "Esc"},         {"fxaa", "FXAA"},   {"msaa", "MSAA"},         {"smaa", "SMAA"},
        {"crash", "Crash"},     {"cortex", "Cortex"},   {"nina", "Nina"},   {"wumpa", "Wumpa"},       {"tv", "TV"},
        {"hud", "HUD"},         {"ui", "UI"},           {"pc", "PC"},       {"fps", "FPS"},
        // Portable mode's: the folders' names, the stick, the dialog's button
        {"appdata", "AppData"}, {"user", "User"},       {"usb", "USB"},     {"program files", "Program Files"}, {"ok", "OK"},
    };
    auto letter = [](unsigned char c) { return std::isalnum(c) != 0 || c >= 0xC0; };
    std::string out = text;
    for (const auto& [word, proper] : Words)
    {
        size_t length = std::strlen(word);
        for (size_t at = out.find(word); at != std::string::npos; at = out.find(word, at + length))
        {
            bool start = at == 0 || !letter(static_cast<unsigned char>(out[at - 1]));
            bool end = at + length == out.size() || !letter(static_cast<unsigned char>(out[at + length]));
            if (start && end)
            {
                out.replace(at, length, proper);
            }
        }
    }

    // An axis named last ("invert camera y", "maus y umkehren" names it mid-way): X and Y
    for (size_t at = 0; at < out.size(); at++)
    {
        bool axis = (out[at] == 'x' || out[at] == 'y') && at > 0 && out[at - 1] == ' ' &&
                    (at + 1 == out.size() || out[at + 1] == ' ') && at + 1 >= 4;
        // Spanish's "y" (and) is never a label's last word or before a verb here; only labels name axes
        if (axis && (at + 1 == out.size() || out.find("umkehren") != std::string::npos))
        {
            out[at] = static_cast<char>(out[at] - 0x20);
        }
    }

    return out;
}

std::string Sentence(std::string text)
{
    text = ProperWords(text);
    // The first letter, and each sentence's after it (". " "? " "! ")
    bool start = true;
    for (size_t i = 0; i < text.size(); i++)
    {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (start && c != ' ' && c != 0xBF && c != 0xA1)
        {
            text[i] = static_cast<char>(Upper(c));
            start = false;
        }
        else if ((c == '.' || c == '?' || c == '!') && i + 1 < text.size() && text[i + 1] == ' ')
        {
            start = true;
        }
    }

    return text;
}

std::string Capitals(std::string text)
{
    for (char& c : text)
    {
        c = static_cast<char>(Upper(static_cast<unsigned char>(c)));
    }

    return text;
}

// Sizes as "1280x960" the design's "1280 × 960", scales as "2x" its "2×" (× is Latin-1's 0xD7)
std::string Dimensions(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); i++)
    {
        bool between = i > 0 && i + 1 < text.size() && text[i] == 'x' && std::isdigit(static_cast<unsigned char>(text[i - 1])) &&
                       std::isdigit(static_cast<unsigned char>(text[i + 1]));
        // And a scale's "2x" its "2×"
        bool times = !between && i > 0 && text[i] == 'x' && std::isdigit(static_cast<unsigned char>(text[i - 1])) &&
                     (i + 1 == text.size() || text[i + 1] == ' ');
        out += between ? std::string(" \xD7 ") : times ? std::string("\xD7") : std::string(1, text[i]);
    }

    return out;
}

TextStyle Body(f32 size, const Rgba32& colour, bool bold = false)
{
    TextStyle style;
    style.font = bold ? OverlayFont::BodyBold : OverlayFont::Body;
    style.size = size;
    style.colour = colour;
    return style;
}

TextStyle Display(f32 size, const Rgba32& colour)
{
    TextStyle style;
    style.font = OverlayFont::Display;
    style.size = size;
    style.colour = colour;
    style.letterSpacing = 1.0f;
    return style;
}

Rgba32 Faded(Rgba32 colour, f32 alpha)
{
    colour.a *= alpha;
    return colour;
}

f32 Seconds()
{
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<f32>(std::chrono::steady_clock::now() - start).count();
}

// ----------------------------------------------------------------------------------------------------------------- the prompts
// A keycap (common.css's .key): rounded 8, a white to grey gradient, a 2px border and a 4px lip of the dark ink, Fredoka 700 15
std::string PadText(const std::string& label);

// A mouse key's keycap face (the mouse's name in the language: KeyName's): a mouse image (the left, right and middle buttons, the
// wheel up and down), or a short text (the side buttons: "M4", "M5"); none: not the mouse's
OverlayImage MouseKeycap(const std::string& label, std::string* text)
{
    constexpr struct
    {
        s32 key;
        OverlayImage image;
        const char* text;
    } Mouse[] = {
        {KeyMouseLeft, OverlayImage::MouseLeft, ""},       {KeyMouseRight, OverlayImage::MouseRight, ""},
        {KeyMouseMiddle, OverlayImage::MouseMiddle, ""},   {KeyMouse4, OverlayImage::Count, "m4"},
        {KeyMouse5, OverlayImage::Count, "m5"},            {KeyWheelUp, OverlayImage::MouseWheelUp, ""},
        {KeyWheelDown, OverlayImage::MouseWheelDown, ""},
    };
    for (const auto& mouse : Mouse)
    {
        char name[64];
        KeyName(mouse.key, name, sizeof(name));
        if (label == name)
        {
            *text = mouse.text;
            return mouse.image;
        }
    }

    return OverlayImage::Count;
}

f32 Keycap(OverlayFrame& frame, const std::string& label, f32 x, f32 middle)
{
    TextStyle text = Body(15.0f, KeyInk, true);
    std::string mouseText;
    OverlayImage mouse = MouseKeycap(label, &mouseText);
    std::string upper = Capitals(mouse != OverlayImage::Count ? std::string() : mouseText.empty() ? PadText(label) : mouseText);
    f32 width = mouse != OverlayImage::Count ? 44.0f : std::max(44.0f, OverlayTextWidth(upper, text) + 24.0f);
    f32 height = 34.0f;
    BoxStyle box;
    box.fill = Hex(0xfdfdfd);
    box.fillBottom = Hex(0xcfd6e3);
    box.radius = 8.0f;
    box.border = 2.0f;
    box.borderColour = KeyInk;
    box.lip = 4.0f;
    box.lipColour = KeyInk;
    f32 top = middle - height * 0.5f - 2.0f;
    frame.Box(x, top, width, height, box);
    if (mouse != OverlayImage::Count)
    {
        // The mouse 64 x 88 in the keycap
        f32 iconHeight = 26.0f;
        f32 iconWidth = iconHeight * 64.0f / 88.0f;
        frame.Image(mouse, x + (width - iconWidth) * 0.5f, top + (height - iconHeight) * 0.5f, iconWidth, iconHeight);
        return width;
    }

    frame.Text(upper, x + width * 0.5f, top + height * 0.5f, text, OverlayAlign::Centre);
    return width;
}

// A pad button's image for a prompt (the game's glyph codes for a PlayStation pad, the letters for the others), none: a keycap
OverlayImage PadImage(const std::string& label, PromptStyle style)
{
    std::string lower = label;
    for (char& c : lower)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (style == PromptPlayStation)
    {
        return label == "\\"  ? OverlayImage::PlayStationCross
               : label == "]" ? OverlayImage::PlayStationCircle
               : label == "[" ? OverlayImage::PlayStationSquare
               : label == "^" ? OverlayImage::PlayStationTriangle
                              : OverlayImage::Count;
    }

    if (style == PromptXbox || style == PromptSwitch)
    {
        u32 base = static_cast<u32>(style == PromptXbox ? OverlayImage::XboxA : OverlayImage::SwitchA);
        s32 letter = lower == "a" ? 0 : lower == "b" ? 1 : lower == "x" ? 2 : lower == "y" ? 3 : -1;
        if (letter >= 0)
        {
            OverlayImage image = static_cast<OverlayImage>(base + static_cast<u32>(letter));
            return OverlayImageAvailable(image) ? image : OverlayImage::Count;
        }
    }

    return OverlayImage::Count;
}

// A pad's label as text for the new fonts: the PlayStation's shoulder glyphs by name ("L1" ...), the letters in capitals
std::string PadText(const std::string& label)
{
    if (label == "{")
    {
        return "L1";
    }
    if (label == "}")
    {
        return "R1";
    }
    if (label == "\xA6")
    {
        return "L2";
    }
    if (label == "\xAC")
    {
        return "R2";
    }
    if (label.size() <= 2 && label != "-")
    {
        return Capitals(label);
    }

    return label;
}

constexpr f32 ButtonSize = 38.0f;

f32 PromptWidth(const std::string& prompt)
{
    if (prompt.empty())
    {
        return 0.0f;
    }

    if (PadImage(prompt, CurrentPromptStyle()) != OverlayImage::Count)
    {
        return ButtonSize;
    }

    std::string mouseText;
    if (MouseKeycap(prompt, &mouseText) != OverlayImage::Count)
    {
        return 44.0f;
    }

    return std::max(44.0f, OverlayTextWidth(Capitals(mouseText.empty() ? PadText(prompt) : mouseText), Body(15.0f, KeyInk, true)) + 24.0f);
}

f32 Prompt(OverlayFrame& frame, const std::string& prompt, f32 x, f32 middle)
{
    if (prompt.empty())
    {
        return 0.0f;
    }

    OverlayImage image = PadImage(prompt, CurrentPromptStyle());
    if (image != OverlayImage::Count)
    {
        frame.Image(image, x, middle - ButtonSize * 0.5f, ButtonSize, ButtonSize);
        return ButtonSize;
    }

    return Keycap(frame, prompt, x, middle);
}

// A line of text whose prompt glyph codes are drawn as the input's prompts (keycaps or the pad's buttons), small
void DescriptionLine(OverlayFrame& frame, const std::string& text, f32 x, f32 middle, const TextStyle& style)
{
    std::string run;
    auto flush = [&] {
        if (!run.empty())
        {
            frame.Text(run, x, middle, style);
            x += OverlayTextWidth(run, style);
            run.clear();
        }
    };
    for (char c : text)
    {
        if (std::strchr(PromptGlyphs, c) == nullptr || c == '\0')
        {
            run += c;
            continue;
        }

        flush();
        x += 4.0f;
        x += Prompt(frame, GlyphPrompt(c), x, middle) + 4.0f;
    }
    flush();
}

// A text broken into lines no wider than width, at its spaces (a lone "?", ":", "!" or ";", French's spaced punctuation, stays with
// the word before it)
std::vector<std::string> Wrapped(const std::string& text, f32 width, const TextStyle& style)
{
    std::vector<std::string> lines;
    std::string line;
    size_t at = 0;
    while (at < text.size())
    {
        size_t space = text.find(' ', at);
        std::string word = text.substr(at, space == std::string::npos ? std::string::npos : space - at);
        at = space == std::string::npos ? text.size() : space + 1;
        bool punctuation = word.size() == 1 && std::strchr("?:!;", word[0]) != nullptr;
        std::string longer = line.empty() ? word : line + " " + word;
        if (!line.empty() && !punctuation && OverlayTextWidth(longer, style) > width)
        {
            lines.push_back(line);
            line = word;
        }
        else
        {
            line = longer;
        }
    }

    if (!line.empty())
    {
        lines.push_back(line);
    }

    return lines;
}

// A button of a dialog: orange (its yes) or the pill's blue (its no), its word, its region
void DialogButton(OverlayFrame& frame, f32 x, f32 y, u32 text, bool primary, HitKind kind, std::vector<HitRegion>& regions)
{
    constexpr f32 Width = 140.0f;
    constexpr f32 Height = 42.0f;
    BoxStyle button;
    button.fill = primary ? Orange : PillFill;
    button.radius = 20.0f;
    frame.Box(x, y, Width, Height, button);
    frame.Text(Sentence(Text(text)), x + Width * 0.5f, y + Height * 0.5f, Body(21.0f, primary ? White : PillBlue), OverlayAlign::Centre);
    regions.push_back({kind, x, y, Width, Height, -1, -1, -1});
}

// Portable mode's dialog (settings.cpp) over the screen: its title and message, then yes and no (asked), OK (refused, failed, done:
// the game closes, by itself after a few seconds) or the copy's bar and its megabytes. Done says where the old folder went, failed
// what went wrong (in English, as the log has it)
void DrawPortableDialog(OverlayFrame& frame, const SettingsView& view, const BoxStyle& panel, std::vector<HitRegion>& regions)
{
    constexpr f32 Width = 660.0f;
    constexpr f32 Left = (ReferenceWidth - Width) * 0.5f;
    constexpr f32 Pad = 40.0f;
    constexpr f32 LineHeight = 28.0f;
    constexpr f32 NoteHeight = 22.0f;
    constexpr f32 Bottom = 42.0f;
    TextStyle title = Body(24.0f, Ink, true);
    TextStyle body = Body(20.0f, Ink);
    TextStyle note = Body(16.0f, Hex(0x6b86a8));
    u32 message = view.dialog == DialogAsk       ? (view.dialogPortable ? TextPortableAskOn : TextPortableAskOff)
                  : view.dialog == DialogRefused ? TextPortableRefused
                  : view.dialog == DialogCopying ? TextPortableCopying
                  : view.dialog == DialogDone    ? TextPortableDone
                                                 : TextPortableFailed;
    std::string text = Sentence(Text(message));
    if (view.dialog == DialogDone)
    {
        char seconds[16];
        std::snprintf(seconds, sizeof(seconds), " (%d)", view.closeSecondsLeft < 0 ? 0 : view.closeSecondsLeft);
        text += seconds;
    }

    f32 textWidth = Width - Pad * 2.0f;
    std::vector<std::string> lines = Wrapped(text, textWidth, body);
    std::vector<std::string> notes;
    if (view.dialog == DialogDone && !view.backup.empty())
    {
        lines.push_back(Sentence(Text(TextPortableBackup)));
        lines.push_back("\"" + view.backup + "\"");
    }
    else if (view.dialog == DialogFailed && !view.failure.empty())
    {
        notes = Wrapped(view.failure, textWidth, note);
    }

    f32 height = 32.0f + 36.0f + 10.0f + static_cast<f32>(lines.size()) * LineHeight + static_cast<f32>(notes.size()) * NoteHeight +
                 24.0f + Bottom + 30.0f;
    f32 top = (ReferenceHeight - height) * 0.5f;
    BoxStyle dim;
    dim.fill = Hex(0x000000, 0.35f);
    frame.Box(0.0f, 0.0f, ReferenceWidth, ReferenceHeight, dim);
    frame.Box(Left, top, Width, height, panel);
    f32 y = top + 32.0f + 18.0f;
    frame.Text(Sentence(Text(TextPortable)), ReferenceWidth * 0.5f, y, title, OverlayAlign::Centre);
    y += 18.0f + 10.0f + LineHeight * 0.5f;
    for (const std::string& line : lines)
    {
        frame.Text(line, ReferenceWidth * 0.5f, y, body, OverlayAlign::Centre);
        y += LineHeight;
    }

    y += NoteHeight * 0.5f - LineHeight * 0.5f;
    for (const std::string& line : notes)
    {
        frame.Text(line, ReferenceWidth * 0.5f, y, note, OverlayAlign::Centre);
        y += NoteHeight;
    }

    f32 bottomTop = top + height - 30.0f - Bottom;
    switch (view.dialog)
    {
    case DialogAsk:
        DialogButton(frame, ReferenceWidth * 0.5f - 170.0f, bottomTop, GameTextYes, true, HitKind::DialogYes, regions);
        DialogButton(frame, ReferenceWidth * 0.5f + 30.0f, bottomTop, GameTextNo, false, HitKind::DialogNo, regions);
        break;
    case DialogCopying:
    {
        // The bar, and the megabytes copied of the megabytes there are under it
        BoxStyle track;
        track.fill = PillFill;
        track.radius = 8.0f;
        frame.Box(Left + Pad, bottomTop, textWidth, 16.0f, track);
        f32 share = view.copyTotal != 0 ? std::min(1.0f, static_cast<f32>(view.copied) / static_cast<f32>(view.copyTotal)) : 0.0f;
        if (share > 0.0f)
        {
            BoxStyle fill;
            fill.fill = Orange;
            fill.radius = 8.0f;
            frame.Box(Left + Pad, bottomTop, std::max(16.0f, textWidth * share), 16.0f, fill);
        }

        char megabytes[48];
        std::snprintf(megabytes, sizeof(megabytes), "%.1f / %.1f MB", static_cast<double>(view.copied) / (1024.0 * 1024.0),
                      static_cast<double>(view.copyTotal) / (1024.0 * 1024.0));
        frame.Text(megabytes, ReferenceWidth * 0.5f, bottomTop + 16.0f + 16.0f, note, OverlayAlign::Centre);
        break;
    }
    default:
        DialogButton(frame, ReferenceWidth * 0.5f - 70.0f, bottomTop, TextOk, true, HitKind::DialogYes, regions);
        break;
    }
}

struct FooterItem
{
    std::string prompt;
    std::string prompt2;
    std::string word;
    HitKind kind;
};

// The bar (common.css's .foot and .prompt): black 45%, 58px, the prompts right-aligned 60px from the edge, 34px apart, each its
// keys (8px apart) and its word (Fredoka 600 19 white, a 2px black drop)
void Footer(OverlayFrame& frame, const std::vector<FooterItem>& items, std::vector<HitRegion>& regions)
{
    f32 top = ReferenceHeight - 58.0f;
    f32 middle = top + 29.0f;
    BoxStyle bar;
    bar.fill = Hex(0x000000, 0.45f);
    frame.Box(-1000.0f, top, ReferenceWidth + 2000.0f, 58.0f + 1000.0f, bar);
    TextStyle word = Body(19.0f, White);
    word.dropY = 2.0f;
    word.dropColour = Hex(0x000000);
    f32 width = 0.0f;
    for (size_t i = 0; i < items.size(); i++)
    {
        const FooterItem& item = items[i];
        width += (i == 0 ? 0.0f : 34.0f) + PromptWidth(item.prompt) + 8.0f;
        if (!item.prompt2.empty())
        {
            width += PromptWidth(item.prompt2) + 8.0f;
        }

        width += OverlayTextWidth(item.word, word);
    }

    f32 x = ReferenceWidth - 60.0f - width;
    for (size_t i = 0; i < items.size(); i++)
    {
        const FooterItem& item = items[i];
        x += i == 0 ? 0.0f : 34.0f;
        f32 start = x;
        x += Prompt(frame, item.prompt, x, middle) + 8.0f;
        if (!item.prompt2.empty())
        {
            x += Prompt(frame, item.prompt2, x, middle) + 8.0f;
        }

        frame.Text(item.word, x, middle, word);
        x += OverlayTextWidth(item.word, word);
        regions.push_back({item.kind, start, top, x - start, 58.0f, -1, -1, -1});
    }
}

// --------------------------------------------------------------------------------------------------------------- the animations
struct Animation
{
    f32 barY = -1.0f;
    f32 barFrom = -1.0f;
    f32 barTarget = -1.0f;
    f32 barStart = 0.0f;
    u32 page = ~0u;
    u32 pageChanges = 0;
    f32 pageStart = -10.0f;
    f32 scroll = 0.0f;
    f32 lastTime = 0.0f;
};
Animation g_Animation;

f32 EaseOut(f32 t)
{
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

// --------------------------------------------------------------------------------------------------------------------- a row
struct RowColours
{
    Rgba32 label;
    Rgba32 pillFill;
    Rgba32 pillText;
    Rgba32 chevron;
};

// A value between chevrons (common.css's .v: centred in its 280px on the row's right)
void ChoiceValue(OverlayFrame& frame, const std::string& value, f32 centre, f32 middle, const RowColours& colours, s32 index,
                 f32 top, std::vector<HitRegion>& regions)
{
    TextStyle text = Body(21.0f, colours.pillText);
    TextStyle chevron = Body(21.0f, colours.chevron, true);
    f32 textWidth = OverlayTextWidth(value, text);
    f32 pillWidth = textWidth + 36.0f;
    f32 pillHeight = OverlayLineHeight(text) + 8.0f;
    std::string left = "\x8B";
    std::string right = "\x9B";
    f32 chevronWidth = OverlayTextWidth(left, chevron);
    f32 total = chevronWidth * 2.0f + ChevronGap * 2.0f + pillWidth;
    f32 x = centre - total * 0.5f;
    frame.Text(left, x, middle, chevron);
    regions.push_back({HitKind::ArrowLeft, x - 12.0f, top, chevronWidth + 24.0f, RowHeight, index, -1, -1});
    x += chevronWidth + ChevronGap;
    BoxStyle pill;
    pill.fill = colours.pillFill;
    pill.radius = 20.0f;
    frame.Box(x, middle - pillHeight * 0.5f, pillWidth, pillHeight, pill);
    frame.Text(value, x + pillWidth * 0.5f, middle, text, OverlayAlign::Centre);
    x += pillWidth + ChevronGap;
    frame.Text(right, x, middle, chevron);
    regions.push_back({HitKind::ArrowRight, x - 12.0f, top, chevronWidth + 24.0f, RowHeight, index, -1, -1});
}

// A slider: a chunky track, its orange fill, the Wumpa-orange knob, the value next to it
void SliderValue(OverlayFrame& frame, const Row& row, f32 left, f32 middle, const RowColours& colours, s32 index, f32 top,
                 std::vector<HitRegion>& regions)
{
    s32 value = row.get();
    f32 fraction = static_cast<f32>(value - row.minimum) / static_cast<f32>(row.maximum - row.minimum);
    f32 width = 220.0f;
    BoxStyle track;
    track.fill = Hex(0xdbe7f7);
    track.radius = 8.0f;
    frame.Box(left, middle - 6.0f, width, 12.0f, track);
    BoxStyle fill;
    fill.fill = Hex(0xffc04d);
    fill.fillBottom = DeepOrange;
    fill.radius = 8.0f;
    if (fraction > 0.0f)
    {
        frame.Box(left, middle - 6.0f, std::max(12.0f, width * fraction), 12.0f, fill);
    }

    BoxStyle knob;
    knob.fill = Hex(0xffb347);
    knob.fillBottom = Hex(0xe8650a);
    knob.radius = 13.0f;
    knob.border = 2.0f;
    knob.borderColour = Brown;
    knob.lip = 2.0f;
    knob.lipColour = Hex(0x000000, 0.25f);
    frame.Box(left + width * fraction - 13.0f, middle - 13.0f, 26.0f, 26.0f, knob);
    char number[8];
    std::snprintf(number, sizeof(number), "%d", value);
    frame.Text(number, left + width + 34.0f, middle, Body(21.0f, colours.label), OverlayAlign::Centre);
    regions.push_back({HitKind::Slider, left, top, width, RowHeight, index, -1, -1});
}

// The controls table's columns' middles and their pills' width
constexpr f32 ColumnMiddle[BindingColumns] = {800.0f, 945.0f, 1095.0f};
constexpr f32 CellWidth = 132.0f;
}

// ---------------------------------------------------------------------------------------------------------------- the screen
void DrawSettingsView(OverlayFrame& frame, std::vector<HitRegion>& regions)
{
    SettingsView view = CurrentSettingsView();
    const std::vector<Row>& rows = SettingsRows(view.page);
    f32 now = Seconds();
    frame.Backdrop(now);

    // The title and its line
    TextStyle title = Display(66.0f, Orange);
    title.outline = 2.0f;
    title.outlineColour = Brown;
    title.dropY = 4.0f;
    title.dropColour = Brown;
    frame.Text("OPTIONS", 64.0f, 40.0f + OverlayLineHeight(title) * 0.5f, title);
    TextStyle sub = Body(18.0f, Hex(0xbcd9ff));
    sub.letterSpacing = 3.0f;
    frame.Text("CRASH TWINSANITY", 68.0f, 118.0f + OverlayLineHeight(sub) * 0.5f, sub);

    // The tabs
    TextStyle tabText = Display(26.0f, Hex(0xdbe9ff));
    f32 tabHeight = 52.0f;
    for (u32 tab = 0; tab < TabCount; tab++)
    {
        f32 y = 170.0f + static_cast<f32>(tab) * 64.0f;
        bool selected = tab == view.page;
        BoxStyle box;
        box.radius = 16.0f;
        box.border = 2.0f;
        if (selected)
        {
            box.fill = White;
            box.borderColour = White;
            box.lip = 6.0f;
            box.lipColour = Lip;
        }
        else
        {
            box.fill = Hex(0xffffff, 0.08f);
            box.borderColour = Hex(0xffffff, 0.15f);
        }

        frame.Box(60.0f, y, 250.0f, tabHeight, box);
        TextStyle text = tabText;
        if (selected)
        {
            text.colour = DeepOrange;
            text.outline = 1.2f;
            text.outlineColour = TabBrown;
        }

        frame.Text(Capitals(Text(PageTitleText(tab))), 86.0f, y + tabHeight * 0.5f + 2.0f, text);
        regions.push_back({HitKind::Tab, 60.0f, y, 250.0f, tabHeight, -1, -1, static_cast<s32>(tab)});
    }

    // The panel: white, rounded, its orange lip and its soft shadow
    BoxStyle panel;
    panel.fill = White;
    panel.radius = 22.0f;
    panel.lip = 10.0f;
    panel.lipColour = Lip;
    panel.shadowOffset = 26.0f;
    panel.shadowBlur = 50.0f;
    panel.shadowColour = Hex(0x000000, 0.4f);
    frame.Box(PanelX, PanelY, PanelWidth, PanelHeight, panel);

    // The page's cross-fade, the list's scroll (smooth) and the selection bar's slide
    Animation& a = g_Animation;
    f32 delta = std::min(0.1f, std::max(0.0f, now - a.lastTime));
    a.lastTime = now;
    if (view.pageChanges != a.pageChanges || view.page != a.page)
    {
        a.pageChanges = view.pageChanges;
        a.page = view.page;
        a.pageStart = now;
        a.barY = -1.0f;
        a.scroll = 0.0f;
    }

    f32 fade = EaseOut((now - a.pageStart) / 0.12f);
    std::vector<s32> visible = VisibleRowIndexes(view.page);
    s32 selectedLine = 0;
    // The controls table's header line (-1 none): it sticks to the top of the panel when the table scrolls under it
    s32 headerLine = -1;
    for (s32 i = 0; i < static_cast<s32>(visible.size()); i++)
    {
        if (visible[static_cast<size_t>(i)] == view.row)
        {
            selectedLine = i;
        }

        if (rows[static_cast<size_t>(visible[static_cast<size_t>(i)])].kind == RowHeader)
        {
            headerLine = i;
        }
    }

    f32 rowsTop = PanelY + PanelPadY;
    f32 rowsHeight = PanelHeight - PanelPadY * 2.0f;
    f32 contentHeight = static_cast<f32>(visible.size()) * RowPitch;
    f32 maxScroll = std::max(0.0f, contentHeight - rowsHeight);
    f32 selectedTop = static_cast<f32>(selectedLine) * RowPitch;
    f32 wantScroll = a.scroll;
    // A row under the stuck header scrolls into view below it
    f32 headerAllowance = headerLine >= 0 && selectedLine > headerLine ? RowPitch : 0.0f;
    if (selectedTop - headerAllowance < a.scroll)
    {
        wantScroll = selectedTop - headerAllowance;
    }
    else if (selectedTop + RowHeight > a.scroll + rowsHeight)
    {
        wantScroll = selectedTop + RowHeight - rowsHeight;
    }

    wantScroll = std::min(maxScroll, std::max(0.0f, wantScroll));
    a.scroll += (wantScroll - a.scroll) * std::min(1.0f, delta * 18.0f);
    if (std::fabs(wantScroll - a.scroll) < 0.5f)
    {
        a.scroll = wantScroll;
    }

    SettingsScroll() = static_cast<s32>(a.scroll / RowPitch);
    frame.Clip(PanelX, rowsTop, PanelWidth, rowsHeight);
    f32 rowLeft = PanelX + PanelPadX;
    f32 rowWidth = PanelWidth - PanelPadX * 2.0f;
    f32 valueCentre = rowLeft + rowWidth - RowPad - ValueWidth * 0.5f;

    // The selection bar sliding to its row (in the rows' own space: the scroll moves it with the rows)
    f32 targetY = selectedTop;
    if (a.barY < 0.0f || a.barTarget != targetY)
    {
        a.barFrom = a.barY < 0.0f ? targetY : a.barY;
        a.barTarget = targetY;
        a.barStart = now;
    }

    a.barY = a.barFrom + (a.barTarget - a.barFrom) * EaseOut((now - a.barStart) / 0.08f);
    f32 barTop = rowsTop + a.barY - a.scroll;
    const Row& selectedRow = rows[static_cast<size_t>(view.row)];
    if (selectedRow.kind != RowHeader)
    {
        BoxStyle bar;
        bar.fill = Faded(selectedRow.kind == RowBinding ? Hex(0xfff0dc) : Orange, fade);
        bar.radius = 12.0f;
        frame.Box(rowLeft, barTop, rowWidth, RowHeight, bar);
    }

    for (s32 line = 0; line < static_cast<s32>(visible.size()); line++)
    {
        f32 top = rowsTop + static_cast<f32>(line) * RowPitch - a.scroll;
        if (top + RowHeight < rowsTop || top > rowsTop + rowsHeight)
        {
            continue;
        }

        s32 index = visible[static_cast<size_t>(line)];
        const Row& row = rows[static_cast<size_t>(index)];
        // The text in the middle of the row below its divider (the selected row's divider is clear)
        f32 middle = top + (line > 0 ? 1.0f : 0.0f) + RowHeight * 0.5f;
        bool selected = index == view.row && row.kind != RowBinding;
        if (line > 0 && index != view.row)
        {
            BoxStyle separator;
            separator.fill = Faded(Separator, fade);
            frame.Box(rowLeft, top, rowWidth, 2.0f, separator);
        }

        if ((index == view.flashRows[0] || index == view.flashRows[1]) && view.flashOn)
        {
            BoxStyle flash;
            flash.fill = Faded(Lip, fade * 0.8f);
            flash.radius = 12.0f;
            frame.Box(rowLeft, top, rowWidth, RowHeight, flash);
        }

        RowColours colours;
        colours.label = Faded(selected ? White : Ink, fade);
        colours.pillFill = Faded(selected ? Hex(0xffffff, 0.25f) : PillFill, fade);
        colours.pillText = Faded(selected ? White : PillBlue, fade);
        colours.chevron = Faded(selected ? White : Orange, fade);
        if (row.kind == RowHeader)
        {
            // Drawn after the rows (it sticks)
            continue;
        }

        regions.push_back({HitKind::Row, rowLeft, std::max(top, rowsTop), rowWidth, RowHeight, index, -1, -1});
        frame.Text(Sentence(Text(row.label)), rowLeft + RowPad, middle, Body(21.0f, colours.label));
        switch (row.kind)
        {
        case RowChoice:
        {
            std::string value = Sentence(Dimensions(row.valueText(row.get())));
            ChoiceValue(frame, value, valueCentre, middle, colours, index, top, regions);
            break;
        }
        case RowSlider:
            SliderValue(frame, row, valueCentre - ValueWidth * 0.5f, middle, colours, index, top, regions);
            break;
        case RowBinding:
        {
            for (u32 column = 0; column < BindingColumns; column++)
            {
                bool cell = index == view.row && column == view.column;
                std::string text = cell && view.capturing ? std::string("\x85") : BindingCellText(row.function, column);
                // Every cell at full contrast (the controller's sticks aren't rebound, but they're the pad's controls all the same)
                TextStyle style = Body(17.0f, Faded(cell ? White : PillBlue, fade));
                f32 cellHeight = 32.0f;
                BoxStyle pill;
                pill.fill = Faded(cell ? Orange : PillFill, fade);
                pill.radius = 16.0f;
                f32 left = ColumnMiddle[column] - CellWidth * 0.5f;
                frame.Box(left, middle - cellHeight * 0.5f, CellWidth, cellHeight, pill);
                regions.push_back({HitKind::Cell, left, middle - cellHeight * 0.5f, CellWidth, cellHeight, index, static_cast<s32>(column), -1});
                // The controller's button as its art (the pad's style: the disc's Xbox and Switch buttons, the PlayStation's
                // shapes), the PlayStation's shoulders by name; a mouse key as the mouse
                OverlayImage image = OverlayImage::Count;
                std::string mouseText;
                if (!(cell && view.capturing))
                {
                    image = column == ColumnPad ? PadImage(text, PadPromptStyle()) : MouseKeycap(text, &mouseText);
                }

                if (image != OverlayImage::Count && column == ColumnPad)
                {
                    constexpr f32 ImageSize = 28.0f;
                    frame.Image(image, ColumnMiddle[column] - ImageSize * 0.5f, middle - ImageSize * 0.5f, ImageSize, ImageSize);
                    continue;
                }

                text = column == ColumnPad ? Sentence(PadText(text)) : Sentence(text);
                // A mouse key: the mouse beside its name
                f32 iconHeight = 24.0f;
                f32 iconWidth = image != OverlayImage::Count ? iconHeight * 64.0f / 88.0f : 0.0f;
                f32 gap = image != OverlayImage::Count ? 5.0f : 0.0f;
                f32 room = CellWidth - 16.0f - iconWidth - gap;
                f32 textWidth = OverlayTextWidth(text, style);
                if (textWidth > room)
                {
                    style.size *= room / textWidth;
                    textWidth = room;
                }

                if (image != OverlayImage::Count)
                {
                    f32 x = ColumnMiddle[column] - (iconWidth + gap + textWidth) * 0.5f;
                    frame.Image(image, x, middle - iconHeight * 0.5f, iconWidth, iconHeight);
                    frame.Text(text, x + iconWidth + gap, middle, style);
                    continue;
                }

                frame.Text(text, ColumnMiddle[column], middle, style, OverlayAlign::Centre);
            }

            break;
        }
        default:
            break;
        }
    }

    // The controls table's header: Action, Key, Alt key, Controller over the columns, stuck to the panel's top once the table
    // scrolls under it (on the panel's white, a divider under it)
    if (headerLine >= 0)
    {
        f32 headerTop = std::max(rowsTop, rowsTop + static_cast<f32>(headerLine) * RowPitch - a.scroll);
        if (headerTop < rowsTop + rowsHeight)
        {
            BoxStyle backing;
            backing.fill = Faded(White, fade);
            frame.Box(rowLeft, headerTop, rowWidth, RowHeight, backing);
            BoxStyle divider;
            divider.fill = Faded(Separator, fade);
            frame.Box(rowLeft, headerTop + RowHeight - 2.0f, rowWidth, 2.0f, divider);
            f32 middle = headerTop + RowHeight * 0.5f;
            TextStyle header = Body(15.0f, Faded(Hex(0x6b86a8), fade), true);
            frame.Text(Sentence(Text(TextColumnAction)), rowLeft + RowPad, middle, header);
            constexpr u32 Headers[BindingColumns] = {TextColumnKey, TextColumnKey2, TextColumnPad};
            for (u32 column = 0; column < BindingColumns; column++)
            {
                frame.Text(Sentence(Text(Headers[column])), ColumnMiddle[column], middle, header, OverlayAlign::Centre);
            }
        }
    }

    frame.NoClip();

    // The scrollbar: thin and orange, when the rows overflow
    if (maxScroll > 0.0f)
    {
        f32 trackHeight = rowsHeight - 8.0f;
        f32 thumbHeight = std::max(30.0f, trackHeight * rowsHeight / contentHeight);
        f32 thumbTop = rowsTop + 4.0f + (trackHeight - thumbHeight) * (a.scroll / maxScroll);
        BoxStyle thumb;
        thumb.fill = Orange;
        thumb.radius = 3.0f;
        frame.Box(PanelX + PanelWidth - 9.0f, thumbTop, 5.0f, thumbHeight, thumb);
    }

    // The description of the highlighted row (or what's waited for)
    std::string description = view.capturing ? Text(TextPressKey) : Text(selectedRow.description);
    TextStyle describe = Body(19.0f, Hex(0xd6e8ff));
    DescriptionLine(frame, Sentence(description), 360.0f, 612.0f + OverlayLineHeight(describe) * 0.5f, describe);

    // The countdown: keep these display settings?
    if (view.keeping)
    {
        frame.Box(390.0f, 270.0f, 500.0f, 170.0f, panel);
        char seconds[16];
        std::snprintf(seconds, sizeof(seconds), " (%d)", view.keepSecondsLeft < 0 ? 0 : view.keepSecondsLeft);
        frame.Text(Sentence(Text(TextKeepDisplay)) + seconds, 640.0f, 320.0f, Body(23.0f, Ink), OverlayAlign::Centre);
        BoxStyle yes;
        yes.fill = Orange;
        yes.radius = 20.0f;
        frame.Box(470.0f, 370.0f, 140.0f, 42.0f, yes);
        frame.Text(Sentence(Text(GameTextYes)), 540.0f, 391.0f, Body(21.0f, White), OverlayAlign::Centre);
        BoxStyle no;
        no.fill = PillFill;
        no.radius = 20.0f;
        frame.Box(670.0f, 370.0f, 140.0f, 42.0f, no);
        frame.Text(Sentence(Text(GameTextNo)), 740.0f, 391.0f, Body(21.0f, PillBlue), OverlayAlign::Centre);
        regions.push_back({HitKind::KeepYes, 470.0f, 370.0f, 140.0f, 42.0f, -1, -1, -1});
        regions.push_back({HitKind::KeepNo, 670.0f, 370.0f, 140.0f, 42.0f, -1, -1, -1});
    }

    if (view.dialog != DialogNone)
    {
        DrawPortableDialog(frame, view, panel, regions);
    }

    // The bar of prompts for the device in use
    std::vector<FooterItem> items;
    if (view.page == TabControls && selectedRow.kind == RowBinding)
    {
        std::string clear = CurrentPromptStyle() == PromptKeyboard ? "del" : PromptOfButton(SDL_GAMEPAD_BUTTON_WEST);
        items.push_back({clear, "", Sentence(Text(TextClear)), HitKind::FooterClear});
    }

    items.push_back({TabPrompt(false), TabPrompt(true), Sentence(Text(TextTabs)), HitKind::FooterTabs});
    items.push_back({MenuSelectPrompt(), "", Sentence(Text(TextSelect)), HitKind::FooterSelect});
    items.push_back({MenuBackPrompt(), "", Sentence(Text(GameTextBack)), HitKind::FooterBack});
    Footer(frame, items, regions);
}

// --------------------------------------------------------------------------------------------------------- the game's menus' bar
void DrawGameMenuChrome(OverlayFrame& frame, std::vector<HitRegion>& regions)
{
    GameController* controller = G_GameController;
    if (controller == nullptr)
    {
        return;
    }

    // The game's hints (OLEG's labels): the game draws them itself (their glyphs the controller's: SetPromptLabels), or the bar
    // draws them as its prompts while they show (their own drawing left out)
    OLEG& oleg = controller->oleg;
    Label* hints[] = {&oleg.selectHint, &oleg.backHint, &oleg.cancelHint, &oleg.pagesLeftHint, &oleg.pagesRightHint, &oleg.nextHint};
    bool bar = IsOn(OptionMenuHints);
    bool any = false;
    for (Label* hint : hints)
    {
        hint->flags.invisible = bar ? 1 : 0;
        any = any || hint->State() >= Widget::StateAppearing;
    }

    if (!bar || !any)
    {
        return;
    }

    auto shown = [](const Label& hint) { return hint.State() >= Widget::StateAppearing; };
    std::vector<FooterItem> items;
    if (shown(oleg.pagesLeftHint) || shown(oleg.pagesRightHint))
    {
        items.push_back({TabPrompt(false), TabPrompt(true), Sentence(Text(TextTabs)), HitKind::FooterTabs});
    }

    if (shown(oleg.selectHint))
    {
        items.push_back({MenuSelectPrompt(), "", Sentence(Text(TextSelect)), HitKind::FooterSelect});
    }

    if (shown(oleg.nextHint))
    {
        items.push_back({MenuSelectPrompt(), "", Sentence(Text(TextNext)), HitKind::FooterSelect});
    }

    if (shown(oleg.backHint))
    {
        items.push_back({MenuBackPrompt(), "", Sentence(Text(GameTextBack)), HitKind::FooterBack});
    }

    if (shown(oleg.cancelHint))
    {
        items.push_back({MenuBackPrompt(), "", Sentence(Text(GameTextCancel)), HitKind::FooterBack});
    }

    Footer(frame, items, regions);
}

// ------------------------------------------------------------------------------------------------------------------- the cursor
// The Wumpa fruit (the game's own HUD art, from the Xbox disc; drawn without it), its top the hot spot, swelling a little over what
// can be clicked
void DrawSkipProgress(OverlayFrame& frame, f32 progress)
{
    // Under the prompt (the game's bottom text at the picture's 0.5, 0.92) in the letterbox's bottom bar: the picture's place in
    // the window, in reference pixels (no window: the reference's middle 4:3)
    f32 left = 0.0f;
    f32 top = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
    NativeGraphicsPictureRect(&left, &top, &width, &height);
    f32 x;
    f32 y;
    if (width > 0.0f && height > 0.0f)
    {
        WindowToReference(left + width * 0.5f, top + height * 0.965f, &x, &y);
    }
    else
    {
        x = ReferenceWidth * 0.5f;
        y = ReferenceHeight * 0.965f;
    }

    constexpr f32 BarWidth = 180.0f;
    constexpr f32 BarHeight = 8.0f;
    BoxStyle track;
    track.fill = Hex(0xffffff, 0.25f);
    track.radius = BarHeight * 0.5f;
    track.shadowOffset = 2.0f;
    track.shadowBlur = 4.0f;
    track.shadowColour = Hex(0x000000, 0.5f);
    frame.Box(x - BarWidth * 0.5f, y - BarHeight * 0.5f, BarWidth, BarHeight, track);
    if (progress > 0.0f)
    {
        BoxStyle fill;
        fill.fill = White;
        fill.radius = BarHeight * 0.5f;
        frame.Box(x - BarWidth * 0.5f, y - BarHeight * 0.5f, std::max(BarHeight, BarWidth * progress), BarHeight, fill);
    }
}

void DrawCursor(OverlayFrame& frame, f32 x, f32 y, bool overClickable)
{
    f32 pulse = overClickable ? 1.12f + 0.04f * std::sin(Seconds() * 6.0f) : 1.0f;
    f32 height = 44.0f * pulse;
    f32 width = height * 0.5f;
    frame.Image(OverlayImage::Cursor, x - width * 0.35f, y - height * 0.08f, width, height);
}
}
