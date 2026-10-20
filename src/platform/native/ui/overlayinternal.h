#pragma once

// The overlay's parts' shared insides (overlay.cpp records and measures, overlayrender.cpp draws)
#include "ui/overlay.h"

#include <algorithm>
#include <string>
#include <vector>

struct stbtt_fontinfo;

namespace NativeUi
{
enum class CommandKind : u32
{
    Backdrop,
    Box,
    Text,
    Image,
    Clip,
    NoClip,
};

struct OverlayFrame::Command
{
    CommandKind kind;
    // Reference pixels (the text's left and its line's middle)
    f32 x;
    f32 y;
    f32 width;
    f32 height;
    BoxStyle box;
    std::u32string text;
    TextStyle textStyle;
    OverlayImage image;
    f32 alpha;
    f32 seconds;
};

const stbtt_fontinfo* OverlayFontInfo(OverlayFont face);
f32 OverlayFontScale(OverlayFont face, f32 size);
void OverlayFontMetrics(OverlayFont face, f32 size, f32* ascent, f32* descent, f32* lineGap);
std::u32string GameTextCodepoints(const std::string& gameText);
std::vector<OverlayFrame::Command> SubmittedFrame();
void NoteWindow(int pixelWidth, int pixelHeight, float pixelScale);
void ReferenceMapping(f32 pixelWidth, f32 pixelHeight, f32* scale, f32* offsetX, f32* offsetY);
// The cursor's art: the Xbox disc's Wumpa fruit (RGBA, red the low byte), none without the disc
bool CursorArt(std::vector<u32>* pixels, s32* width, s32* height);
// An image's art (the cursor's, the Xbox's and the Switch's buttons from the disc); none: the overlay draws it
bool ImageArt(OverlayImage image, std::vector<u32>* pixels, s32* width, s32* height);
// A mouse image's art (mouseart.cpp): drawn at a fixed size, RGBA
bool MouseArt(OverlayImage image, std::vector<u32>* pixels, s32* width, s32* height);
// The show FPS option's counter added to the commands drawn over a window of these pixels (fpscounter.cpp; none when it's off)
void AddFpsCounter(std::vector<OverlayFrame::Command>* commands, int pixelWidth, int pixelHeight);
}
