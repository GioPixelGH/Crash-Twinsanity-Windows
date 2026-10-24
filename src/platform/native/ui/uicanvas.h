#pragma once

// Where the parts of the native UI's screens are (hit regions, in the overlay's reference pixels): the view (settingsview.cpp) records
// them as it draws, the logic (settings.cpp) takes the mouse against them
#include "common.h"


namespace NativeUi
{
// What a part of the screen is, for the mouse
enum class HitKind : u32
{
    Row,
    Cell,
    Tab,
    ArrowLeft,
    ArrowRight,
    Slider,
    FooterSelect,
    FooterBack,
    FooterTabs,
    FooterClear,
    KeepYes,
    KeepNo,
    // Portable mode's dialog: yes (or its one button, OK) and no
    DialogYes,
    DialogNo,
};

struct HitRegion
{
    HitKind kind;
    f32 x;
    f32 y;
    f32 width;
    f32 height;
    s32 row;
    s32 column;
    s32 tab;

    bool Contains(f32 px, f32 py) const
    {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }
};

}
