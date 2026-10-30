// The mouse in the game's menus, for every menu at once: the menus' drawer (DrawMenuPage) notes each item's line as it queues its
// text (its rectangle from the font's own layout, as the text is drawn), the overlay notes the game's hints (select, back and the
// pages' arrows), and a menu page's step (MenuPage::Step) takes the mouse as the game's own input: hovering a line selects its
// item as up and down do (with their sound), a left click is select (a choice's click steps it on, as right does) and on a hint
// what it says (an arrow: the pause's pages turned as L1 or R1 turns them), a right click is back, the wheel
// steps the hovered choice left and right or moves the selection on a longer page. The system's cursor shows while the mouse
// moves, hidden after 3 seconds still and while playing. Window points are mapped into the game's picture as the display shows it
// (the TV's shape, letterboxed in the window)
#include "ui/bindings.h"

#include "native.h"
#include "graphics/presenter.h"

#include "game/controllers.h"
#include "game/font.h"
#include "game/gamecontroller.h"
#include "game/language.h"
#include "game/menus.h"
#include "game/renderer.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace NativeUi
{
namespace
{
struct Line
{
    const MenuPage* page;
    const MenuItem* item;
    // The line's rectangle in fractions of the picture
    f32 left;
    f32 top;
    f32 right;
    f32 bottom;
};

// What a hint of the game's (OLEG's) does when it's clicked
enum class FooterKind
{
    Select,
    Back,
    PagesLeft,
    PagesRight,
};

struct Footer
{
    FooterKind kind;
    u32 text;
    f32 left;
    f32 top;
    f32 right;
    f32 bottom;
};

constexpr f32 IdleSeconds = 3.0f;
// The hints' lines: select and next (select), back and cancel (back), the pages' arrows (L1 and L2, R1 and R2)
constexpr struct
{
    u32 text;
    FooterKind kind;
} FooterTexts[] = {
    {0x1C, FooterKind::Select},   {0xB2, FooterKind::Select},     {0x1D, FooterKind::Back},
    {0xBB, FooterKind::Back},     {0x1E, FooterKind::PagesLeft},  {0x1F, FooterKind::PagesRight},
};
// The pad's buttons a click on a pages' arrow presses (the pause turns its pages on L1 and R1 themselves: GameController's
// PausePage), and for how many more of the pad's reads
constexpr u32 PadPressReads = 3;
std::atomic<u32> g_PadPress{0};
std::atomic<u32> g_PadPressReads{0};

std::vector<Line> g_Lines;
std::vector<Line> g_Drawing;
std::vector<Footer> g_Footers;
std::vector<Footer> g_FooterDrawing;
f32 g_MouseX = -1.0f;
f32 g_MouseY = -1.0f;
bool g_Moved = false;
bool g_LeftClick = false;
// When the left click came: a click after a frame that drew none of the menu waits for one that does, so long
std::chrono::steady_clock::time_point g_LeftClickAt;
constexpr f32 ClickWaitSeconds = 0.5f;
bool g_RightClick = false;
// Enter and Escape pressed since the last menu step: a computer's select and back in every menu (as well as their bindings)
bool g_EnterKey = false;
bool g_EscapeKey = false;
s32 g_Wheel = 0;
std::chrono::steady_clock::time_point g_LastMotion;
bool g_CursorShown = true;
bool g_Scripted = false;

// A window point in fractions of the picture
void ToPicture(f32 x, f32 y, f32* outX, f32* outY)
{
    f32 left = 0.0f;
    f32 top = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
    NativeGraphicsPictureRect(&left, &top, &width, &height);
    if (width <= 0.0f || height <= 0.0f)
    {
        int windowWidth = 0;
        int windowHeight = 0;
        SDL_Window* window = SDL_GetMouseFocus();
        if (window == nullptr || !SDL_GetWindowSize(window, &windowWidth, &windowHeight) || windowWidth <= 0 || windowHeight <= 0)
        {
            *outX = -1.0f;
            *outY = -1.0f;
            return;
        }

        f32 aspect = g_WidescreenTv != 0 ? 16.0f / 9.0f : 4.0f / 3.0f;
        width = static_cast<f32>(windowWidth);
        height = width / aspect;
        if (height > static_cast<f32>(windowHeight))
        {
            height = static_cast<f32>(windowHeight);
            width = height * aspect;
        }

        left = (static_cast<f32>(windowWidth) - width) * 0.5f;
        top = (static_cast<f32>(windowHeight) - height) * 0.5f;
    }

    *outX = (x - left) / width;
    *outY = (y - top) / height;
}

// A text's rectangle as the font lays it out (the native text drawing's: its size the font's scale times the text's, its place by
// its alignment), in fractions of the picture
bool TextRectangle(const Renderer* renderer, const char* text, f32 x, f32 y, f32* left, f32* top, f32* right, f32* bottom)
{
    const Font* font = renderer->font;
    if (font == nullptr || text == nullptr || text[0] == '\0' || g_RendererWidth <= 0 || g_RendererHeight <= 0)
    {
        return false;
    }

    Vector2 scale;
    FontScale(font, &scale);
    Vector2 size = {renderer->textScale.x * scale.x, renderer->textScale.y * scale.y};
    static TextLayout layout;
    std::memset(layout.pages, 0, sizeof(layout.pages));
    RenderText(font, text, &size, &layout);
    f32 width = 0.0f;
    for (s32 line = 0; line < layout.lineCount; line++)
    {
        width = layout.lines[line].x > width ? layout.lines[line].x : width;
    }

    // The layout's units: sixteenths of the renderer's pixels
    f32 widthFraction = width / (static_cast<f32>(g_RendererWidth) * 16.0f);
    f32 heightFraction = layout.height / (static_cast<f32>(g_RendererHeight) * 16.0f);
    TextAlignment alignment = renderer->textAlignment;
    f32 topY = alignment.top != 0 ? y : alignment.bottom != 0 ? y - heightFraction : y - heightFraction * 0.5f;
    f32 leftX = alignment.left != 0 ? x : alignment.right != 0 ? x - widthFraction : x - widthFraction * 0.5f;
    *left = leftX;
    *top = topY;
    *right = leftX + widthFraction;
    *bottom = topY + heightFraction;
    return true;
}

bool Inside(f32 left, f32 top, f32 right, f32 bottom)
{
    return g_MouseX >= left && g_MouseX <= right && g_MouseY >= top && g_MouseY <= bottom;
}

// A choice or a value with more than one value (a single choice, as the controls page's bindings, is picked like a plain item)
bool IsValueItem(const MenuItem* item)
{
    if (item->vtable != g_ChoiceItemVTable && item->vtable != g_ValueItemVTable)
    {
        return false;
    }

    const auto* value = static_cast<const ValueItem*>(item);
    return value->maximum > value->minimum;
}

bool WatchMouse(void*, SDL_Event* event)
{
    if (g_Scripted)
    {
        return true;
    }

    switch (event->type)
    {
    case SDL_EVENT_MOUSE_MOTION:
        ToPicture(event->motion.x, event->motion.y, &g_MouseX, &g_MouseY);
        g_Moved = true;
        g_LastMotion = std::chrono::steady_clock::now();
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        ToPicture(event->button.x, event->button.y, &g_MouseX, &g_MouseY);
        g_LastMotion = std::chrono::steady_clock::now();
        if (Capturing())
        {
            // A click while the controls page waits for a key: nothing bound
            CancelCapture();
            break;
        }

        if (event->button.button == SDL_BUTTON_LEFT)
        {
            g_LeftClick = true;
            g_LeftClickAt = std::chrono::steady_clock::now();
        }
        else if (event->button.button == SDL_BUTTON_RIGHT)
        {
            g_RightClick = true;
        }

        break;
    case SDL_EVENT_KEY_DOWN:
        if (event->key.repeat || Capturing())
        {
            break;
        }

        if ((event->key.scancode == SDL_SCANCODE_RETURN || event->key.scancode == SDL_SCANCODE_KP_ENTER) &&
            (event->key.mod & SDL_KMOD_ALT) == 0)
        {
            g_EnterKey = true;
        }
        else if (event->key.scancode == SDL_SCANCODE_ESCAPE)
        {
            g_EscapeKey = true;
        }

        break;
    case SDL_EVENT_MOUSE_WHEEL:
        g_Wheel += event->wheel.y > 0 ? 1 : event->wheel.y < 0 ? -1 : 0;
        g_LastMotion = std::chrono::steady_clock::now();
        break;
    default:
        break;
    }

    return true;
}
}

void StartMouse()
{
    static bool watching = false;
    if (!watching)
    {
        watching = true;
        g_LastMotion = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        SDL_AddEventWatch(WatchMouse, nullptr);
    }
}

// Between frames: the lines drawn last frame are the ones the mouse is over, and the cursor shown or hidden
void MouseFrame()
{
    g_Lines.swap(g_Drawing);
    g_Drawing.clear();
    g_Footers.swap(g_FooterDrawing);
    g_FooterDrawing.clear();
    GameController* controller = G_GameController;
    bool playing = controller != nullptr && controller->State() == GameController::StatePlaying;
    f32 idle = std::chrono::duration<f32>(std::chrono::steady_clock::now() - g_LastMotion).count();
    bool show = IsOn(OptionMouse) && !playing && idle < IdleSeconds && !Native::GetSettings().headless;
    if (show != g_CursorShown)
    {
        g_CursorShown = show;
        // The system's own pointer (the user didn't want a game-art cursor)
        if (show)
        {
            SDL_ShowCursor();
        }
        else
        {
            SDL_HideCursor();
        }
    }

    // The settings screen takes the keys and the mouse itself (settings.cpp)
    if (playing || SettingsOpen())
    {
        g_EnterKey = false;
        g_EscapeKey = false;
        g_Moved = false;
        g_LeftClick = false;
        g_RightClick = false;
        g_Wheel = 0;
    }

    if (!IsOn(OptionMouse) || playing)
    {
        g_Moved = false;
        g_LeftClick = false;
        g_RightClick = false;
        g_Wheel = 0;
    }
}

void WindowToPicture(f32 x, f32 y, f32* outX, f32* outY)
{
    ToPicture(x, y, outX, outY);
}

bool TextExtent(const Renderer* renderer, const char* text, f32* width, f32* height)
{
    f32 left;
    f32 top;
    f32 right;
    f32 bottom;
    if (!TextRectangle(renderer, text, 0.0f, 0.0f, &left, &top, &right, &bottom))
    {
        *width = 0.0f;
        *height = 0.0f;
        return false;
    }

    *width = right - left;
    *height = bottom - top;
    return true;
}

bool CursorShown()
{
    return g_CursorShown;
}

bool MouseOverAnyMenuLine()
{
    for (const Line& line : g_Lines)
    {
        if (Inside(line.left, line.top, line.right, line.bottom))
        {
            return true;
        }
    }

    for (const Footer& footer : g_Footers)
    {
        if (Inside(footer.left, footer.top, footer.right, footer.bottom))
        {
            return true;
        }
    }

    return false;
}

u32 MouseMenuButtons()
{
    u32 reads = g_PadPressReads.load(std::memory_order_relaxed);
    if (reads == 0)
    {
        return 0;
    }

    g_PadPressReads.store(reads - 1, std::memory_order_relaxed);
    return g_PadPress.load(std::memory_order_relaxed);
}

namespace
{
bool g_SelectedLine = false;
f32 g_SelectedRect[4];
}

bool TakeSelectedMenuLine(f32* left, f32* top, f32* right, f32* bottom)
{
    if (!g_SelectedLine)
    {
        return false;
    }

    g_SelectedLine = false;
    *left = g_SelectedRect[0];
    *top = g_SelectedRect[1];
    *right = g_SelectedRect[2];
    *bottom = g_SelectedRect[3];
    return true;
}

void RecordMenuRect(const MenuPage* page, const MenuItem* item, f32 left, f32 top, f32 right, f32 bottom)
{
    g_Drawing.push_back(Line{page, item, left, top, right, bottom});
}

void RecordMenuLine(const MenuPage* page, const MenuItem* item, const Renderer* renderer, const char* text, f32 x, f32 y)
{
    Line line = {page, item, 0.0f, 0.0f, 0.0f, 0.0f};
    if (TextRectangle(renderer, text, x, y, &line.left, &line.top, &line.right, &line.bottom))
    {
        g_Drawing.push_back(line);
        // The selected line (as the drawer draws it: the selected item's text) for its oval (settings.cpp's DrawAfterOleg)
        s32 selected = page->selections != nullptr ? page->selections[0] : -1;
        if (selected >= 0 && selected < static_cast<s32>(page->items.count) && page->items.data[selected] == item)
        {
            g_SelectedLine = true;
            g_SelectedRect[0] = line.left;
            g_SelectedRect[1] = line.top;
            g_SelectedRect[2] = line.right;
            g_SelectedRect[3] = line.bottom;
        }
    }
}

void RecordQueuedText(const Renderer* renderer, const char* text, f32 x, f32 y)
{
    if (text == nullptr || g_Texts[CodeTexts] == nullptr)
    {
        return;
    }

    for (const auto& hint : FooterTexts)
    {
        if (std::strcmp(text, GameText(hint.text)) == 0)
        {
            Footer footer = {hint.kind, hint.text, 0.0f, 0.0f, 0.0f, 0.0f};
            if (TextRectangle(renderer, text, x, y, &footer.left, &footer.top, &footer.right, &footer.bottom))
            {
                g_FooterDrawing.push_back(footer);
            }

            return;
        }
    }
}

namespace
{
std::atomic<s64> g_LastMenuStep{0};
}

bool MenuActiveRecently()
{
    s64 now = std::chrono::steady_clock::now().time_since_epoch().count();
    s64 window = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(250)).count();
    return now - g_LastMenuStep.load(std::memory_order_relaxed) < window;
}

void MouseMenuStep(MenuPage* page, MenuInput* input, MenuSounds* sounds, u32 player)
{
    g_LastMenuStep.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    if (input == nullptr)
    {
        return;
    }

    // The save test picks its items itself
    if (SaveTestMenuStep(page, input, player))
    {
        return;
    }

    auto press = [input](u32 action)
    {
        input->pressed.value = static_cast<u8>(input->pressed.value | (1u << action));
        input->held.value = static_cast<u8>(input->held.value | (1u << action));
    };

    // Enter selects and Escape goes back, in every menu. Enter is also start (the title's "press start"): in a menu it's select
    // only, so a page that start leaves (the pause menu) isn't left by it. Escape leaves such a page, as start does
    if (g_EnterKey)
    {
        g_EnterKey = false;
        if (!g_EscapeKey)
        {
            input->pressed.value = static_cast<u8>(input->pressed.value & ~(1u << MenuInput::ActionLeave));
        }

        press(MenuInput::ActionSelect);
    }

    if (g_EscapeKey)
    {
        g_EscapeKey = false;
        if (page->flags.takesLeave == 0)
        {
            press(MenuInput::ActionBack);
        }
    }

    if (!IsOn(OptionMouse))
    {
        return;
    }

    // The page's line under the mouse
    s32 hovered = -1;
    for (const Line& line : g_Lines)
    {
        if (line.page == page && Inside(line.left, line.top, line.right, line.bottom))
        {
            for (u32 i = 0; i < page->items.count; i++)
            {
                if (page->items.data[i] == line.item && line.item->CanSelect(player))
                {
                    hovered = static_cast<s32>(i);
                }
            }
        }
    }

    // Hovering selects, as up and down do
    if (g_Moved && hovered >= 0 && hovered != page->selections[player])
    {
        page->selections[player] = static_cast<u8>(hovered);
        if (sounds != nullptr)
        {
            sounds->Play(MenuSounds::SoundDown);
        }
    }

    g_Moved = false;

    // A click after a frame that drew none of the menu (no line, no hint: a frame the game didn't draw) waits for one that does,
    // half a second at most: taken now it would land on nothing and be lost (the menu hints check's one-off "stuck")
    bool nothingDrawn = g_Lines.empty() && g_Footers.empty();
    bool waits = g_LeftClick && hovered < 0 && nothingDrawn &&
                 std::chrono::duration<f32>(std::chrono::steady_clock::now() - g_LeftClickAt).count() < ClickWaitSeconds;
    if (g_LeftClick && !waits)
    {
        // $TWIN_MOUSE_TRACE: each click a menu takes, with what the last frame drew under it
        static const bool trace = std::getenv("TWIN_MOUSE_TRACE") != nullptr;
        if (trace)
        {
            u32 under = 0;
            for (const Footer& footer : g_Footers)
            {
                under += Inside(footer.left, footer.top, footer.right, footer.bottom) ? 1 : 0;
            }

            Native::Log("mouse: a click at (%.3f, %.3f): line %d, %zu lines and %zu hints drawn last frame, %u hints under it",
                        g_MouseX, g_MouseY, hovered, g_Lines.size(), g_Footers.size(), under);
        }

        g_LeftClick = false;
        if (hovered >= 0)
        {
            page->selections[player] = static_cast<u8>(hovered);
            press(IsValueItem(page->items.data[hovered]) ? MenuInput::ActionRight : MenuInput::ActionSelect);
        }
        else
        {
            for (const Footer& footer : g_Footers)
            {
                if (!Inside(footer.left, footer.top, footer.right, footer.bottom))
                {
                    continue;
                }

                if (footer.kind == FooterKind::Select || footer.kind == FooterKind::Back)
                {
                    press(footer.kind == FooterKind::Select ? MenuInput::ActionSelect : MenuInput::ActionBack);
                }
                else
                {
                    g_PadPress.store(footer.kind == FooterKind::PagesLeft ? PadBitL1 : PadBitR1, std::memory_order_relaxed);
                    g_PadPressReads.store(PadPressReads, std::memory_order_relaxed);
                }
            }
        }
    }

    if (g_RightClick)
    {
        g_RightClick = false;
        press(MenuInput::ActionBack);
    }

    if (g_Wheel != 0)
    {
        bool up = g_Wheel > 0;
        g_Wheel = 0;
        if (hovered >= 0 && IsValueItem(page->items.data[hovered]))
        {
            press(up ? MenuInput::ActionRight : MenuInput::ActionLeft);
        }
        else
        {
            // Moves the selection (the list's window follows it)
            press(up ? MenuInput::ActionUp : MenuInput::ActionDown);
        }
    }
}

// The shots' script: the mouse moved to a place of the picture, clicked
void ScriptMouse(f32 x, f32 y, bool click)
{
    g_Scripted = true;
    g_MouseX = x;
    g_MouseY = y;
    g_Moved = true;
    g_LeftClick = click;
    g_LeftClickAt = std::chrono::steady_clock::now();
}

// The shots' script: the mouse moved onto the middle of the n-th line drawn last frame (of the page drawn last), clicked or not
bool ScriptMouseToLine(u32 index, bool click)
{
    if (index >= g_Lines.size())
    {
        return false;
    }

    const Line& line = g_Lines[index];
    ScriptMouse((line.left + line.right) * 0.5f, (line.top + line.bottom) * 0.5f, click);
    return true;
}

bool ScriptMouseToHint(u32 text, bool click)
{
    for (const Footer& footer : g_Footers)
    {
        if (footer.text == text)
        {
            ScriptMouse((footer.left + footer.right) * 0.5f, (footer.top + footer.bottom) * 0.5f, click);
            return true;
        }
    }

    return false;
}

bool MouseOverLineForTest(const MenuPage* page)
{
    for (const Line& line : g_Lines)
    {
        if (line.page == page && Inside(line.left, line.top, line.right, line.bottom))
        {
            return true;
        }
    }

    return false;
}
}
