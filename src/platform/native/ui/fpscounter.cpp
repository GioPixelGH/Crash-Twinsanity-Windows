// The show FPS option's counter (OptionShowFps, off by default): the pictures the window shows a second, as a PC game's counter has
// them. The overlay's hook counts each picture as it's drawn (the presenter's thread: the game's frames, the movies' and the loading
// screens' alike), the rate worked out every half second; it's drawn small in the window's own top left corner, whatever the
// picture's shape and place: above OLEG's HUD (the health bar, the wumpa fruit and the lives start 5% of the picture in) and away
// from the menus' hints (the bottom corners)
#include "ui/nativeui.h"
#include "ui/overlayinternal.h"

#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace NativeUi
{
namespace
{
std::atomic<bool> g_Shown{false};
std::atomic<s32> g_Value{-1};
// The presenter's thread's: the half second being counted (its start's time, -1 none) and the pictures since
double g_From = -1.0;
u32 g_Frames = 0;
constexpr double CountSeconds = 0.5;
// Reference pixels: the margin from the window's corner, the text's size, the box's padding across and its height
constexpr f32 Margin = 6.0f;
constexpr f32 TextSize = 16.0f;
constexpr f32 Padding = 7.0f;
constexpr f32 BoxHeight = 20.0f;
}

void SetFpsCounterShown(bool shown)
{
    g_Shown = shown;
}

void CountShownFrame(double seconds)
{
    if (!g_Shown)
    {
        g_From = -1.0;
        g_Value = -1;
        return;
    }

    if (g_From < 0.0)
    {
        g_From = seconds;
        g_Frames = 0;
        return;
    }

    g_Frames++;
    double elapsed = seconds - g_From;
    if (elapsed >= CountSeconds)
    {
        g_Value = static_cast<s32>(std::lround(static_cast<double>(g_Frames) / elapsed));
        g_From = seconds;
        g_Frames = 0;
    }
}

s32 FpsCounterValue()
{
    return g_Shown ? g_Value.load() : -1;
}

void AddFpsCounter(std::vector<OverlayFrame::Command>* commands, int pixelWidth, int pixelHeight)
{
    s32 value = FpsCounterValue();
    if (value < 0 || pixelWidth <= 0 || pixelHeight <= 0)
    {
        return;
    }

    // The window's top left corner in reference pixels (the reference space is centred in the window)
    f32 scale;
    f32 offsetX;
    f32 offsetY;
    ReferenceMapping(static_cast<f32>(pixelWidth), static_cast<f32>(pixelHeight), &scale, &offsetX, &offsetY);
    f32 left = -offsetX / scale + Margin;
    f32 top = -offsetY / scale + Margin;
    TextStyle style;
    style.font = OverlayFont::BodyBold;
    style.size = TextSize;
    style.colour = Hex(0xffffff);
    std::string text = std::to_string(value) + " FPS";
    f32 width = OverlayTextWidth(text, style);

    // Drawn whatever clip the frame's commands left
    OverlayFrame::Command noClip{};
    noClip.kind = CommandKind::NoClip;
    commands->push_back(noClip);
    OverlayFrame::Command box{};
    box.kind = CommandKind::Box;
    box.x = left;
    box.y = top;
    box.width = width + Padding * 2.0f;
    box.height = BoxHeight;
    box.box.fill = Hex(0x000000, 0.5f);
    box.box.radius = 6.0f;
    commands->push_back(box);
    OverlayFrame::Command line{};
    line.kind = CommandKind::Text;
    line.x = left + Padding;
    line.y = top + BoxHeight * 0.5f;
    line.width = width;
    line.text = GameTextCodepoints(text);
    line.textStyle = style;
    commands->push_back(line);
}
}
