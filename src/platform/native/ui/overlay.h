#pragma once

// The native UI's overlay: what it draws over the window at the window's own resolution (graphics/presenter.h's overlay hook), in
// a 1280 x 720 reference space scaled uniformly to the window and centred. The game's thread records a frame of commands (rounded
// boxes with a gradient, a border, a lip and a soft shadow; texts in the bundled fonts with an outline and a drop; the options'
// backdrop; images) and hands the frame over; the presenter's thread draws the last frame handed over (overlayrender.cpp)
#include "common.h"

#include <string>
#include <vector>

namespace NativeUi
{
constexpr f32 ReferenceWidth = 1280.0f;
constexpr f32 ReferenceHeight = 720.0f;

struct Rgba32
{
    f32 r;
    f32 g;
    f32 b;
    f32 a;
};

// #rrggbb with an alpha
constexpr Rgba32 Hex(u32 rgb, f32 alpha = 1.0f)
{
    return {static_cast<f32>((rgb >> 16) & 0xFF) / 255.0f, static_cast<f32>((rgb >> 8) & 0xFF) / 255.0f,
            static_cast<f32>(rgb & 0xFF) / 255.0f, alpha};
}

// The fonts: the display face (Luckiest Guy: titles, tabs) and the body face at its weights (Fredoka 600 and 700)
enum class OverlayFont : u32
{
    Display,
    Body,
    BodyBold,
    Count,
};

struct BoxStyle
{
    Rgba32 fill = {1, 1, 1, 1};
    // A vertical gradient's bottom colour (the fill's alpha < 0: no gradient)
    Rgba32 fillBottom = {0, 0, 0, -1};
    f32 radius = 0.0f;
    f32 border = 0.0f;
    Rgba32 borderColour = {0, 0, 0, 0};
    // A solid copy below (CSS's "0 lip 0 colour" shadow)
    f32 lip = 0.0f;
    Rgba32 lipColour = {0, 0, 0, 0};
    // A soft shadow (CSS's "0 offset blur colour")
    f32 shadowOffset = 0.0f;
    f32 shadowBlur = 0.0f;
    Rgba32 shadowColour = {0, 0, 0, 0};
};

struct TextStyle
{
    OverlayFont font = OverlayFont::Body;
    f32 size = 20.0f;
    Rgba32 colour = {1, 1, 1, 1};
    f32 letterSpacing = 0.0f;
    // An outline round the letters (CSS's text-stroke painted under the fill: half of it shows)
    f32 outline = 0.0f;
    Rgba32 outlineColour = {0, 0, 0, 0};
    // A solid drop (CSS's "x y 0 colour" text-shadow)
    f32 dropX = 0.0f;
    f32 dropY = 0.0f;
    Rgba32 dropColour = {0, 0, 0, 0};
};

enum class OverlayAlign : u32
{
    Left,
    Centre,
    Right,
};

// The images an overlay can draw: the cursor's art (the Wumpa fruit, from the user's Xbox disc when it's there, else drawn), the
// pads' buttons: the Xbox's A B X Y art (from the disc; the Switch's the same in grey) and the PlayStation's face buttons (drawn:
// the shapes in their colours on a dark button), and the mouse's buttons and wheel (drawn)
enum class OverlayImage : u32
{
    Cursor,
    XboxA,
    XboxB,
    XboxX,
    XboxY,
    SwitchA,
    SwitchB,
    SwitchX,
    SwitchY,
    PlayStationCross,
    PlayStationCircle,
    PlayStationSquare,
    PlayStationTriangle,
    // A mouse with its left, right or middle button lit, and its wheel lit with an arrow up or down (drawn)
    MouseLeft,
    MouseRight,
    MouseMiddle,
    MouseWheelUp,
    MouseWheelDown,
    Count,
};
// Whether an image's art is there to draw (the Xbox's and the Switch's need the disc)
bool OverlayImageAvailable(OverlayImage image);

// A frame being recorded (the game's thread)
class OverlayFrame
{
public:
    void Clear();
    // The options' backdrop: the gradient, the ring target and the spiral (turned by the seconds)
    void Backdrop(f32 seconds);
    void Box(f32 x, f32 y, f32 width, f32 height, const BoxStyle& style);
    // A text on one line: placed by its alignment across, its line's middle at y
    void Text(const std::string& gameText, f32 x, f32 y, const TextStyle& style, OverlayAlign align = OverlayAlign::Left);
    void Image(OverlayImage image, f32 x, f32 y, f32 width, f32 height, f32 alpha = 1.0f);
    // A rectangle the frame's later commands are clipped to (none: the whole window), as a list's scroll needs
    void Clip(f32 x, f32 y, f32 width, f32 height);
    void NoClip();
    // A scale and a shift for the later commands' places and sizes (a part of a screen drawn smaller elsewhere: the settings' live
    // preview, THE LIST 66): a point (x, y) is drawn at (x * scale + shiftX, y * scale + shiftY). None after Clear or NoTransform
    void Transform(f32 scale, f32 shiftX, f32 shiftY);
    void NoTransform();
    bool Empty() const;

    struct Command;
    std::vector<Command>& Commands();
};

// A text's width (reference pixels) and its line's height in a style (the fonts' own metrics)
f32 OverlayTextWidth(const std::string& gameText, const TextStyle& style);
f32 OverlayLineHeight(const TextStyle& style);

// The frame recorded this game frame: handed to the presenter's thread to draw (an empty frame draws nothing)
OverlayFrame& RecordingFrame();
void SubmitFrame();
// The presenter's overlay hook installed (once, at start-up)
void StartOverlay();
// The window's pixels and points as the overlay saw them last, and a window point (SDL's mouse) in reference space
void WindowToReference(f32 x, f32 y, f32* referenceX, f32* referenceY);
// --dump-overlay's and the shots': the last submitted frame drawn offscreen (a hidden window's GL context) at a size, as RGBA
bool RenderFrameOffscreen(u32 width, u32 height, std::vector<u32>* pixels);
}
