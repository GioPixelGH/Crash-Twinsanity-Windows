#pragma once

// The draw distance option (OptionRenderDistance, the Display page; HAB3X's, 17abe53): how far the game draws. Two things bound
// it: each model's level of detail (LodMeshAt: its simpler meshes farther away, and past its farthest distance none: the grass
// and small things that pop in as the player gets close) and the camera lens's far plane (the projection, view.cpp, and the
// culling's frustum, culling.cpp). The level of detail is the choice's always (LodDistance: chosen as if the camera were that
// many times nearer; unlimited's cut-off none). The far plane is the choice's only while no
// screen effect reads the depth (ScreenEffectsReadDepth): it moves every pixel's depth by a constant, and the game's colour
// filter colours the pixels past 25 units by their depth's bits 8-15 (renderer/effects.cpp), so its haze would change: up to
// 2/128 of it in the middle distance, 12/128 in the last fifth before the game's far plane
#include "common.h"

#include <atomic>

namespace Native
{
// options.txt's render_distance: codes that keep their meaning whatever the menu's order (settings.cpp's row goes from the
// shortest to the farthest). The original is 0, the default and what an options.txt from before the option reads as
enum RenderDistanceChoice : u32
{
    DistanceOriginal = 0,
    DistanceShort = 1,
    DistanceMedium = 2,
    DistanceLong = 3,
    DistanceUnlimited = 4,
    DistanceChoices,
};

inline std::atomic<u32> g_RenderDistance{DistanceOriginal};
// The far plane's scale of the projection last made (view.cpp), which the culling's frustum takes too
inline std::atomic<f32> g_FarPlaneScale{1.0f};

inline void SetRenderDistance(u32 choice)
{
    g_RenderDistance.store(choice < DistanceChoices ? choice : DistanceOriginal, std::memory_order_relaxed);
}

// The choice's scale of the game's distances (1: the original; unlimited's far plane 4 times the game's)
inline f32 RenderDistanceScale()
{
    static constexpr f32 Scales[DistanceChoices] = {1.0f, 0.6f, 2.0f, 3.0f, 4.0f};
    return Scales[g_RenderDistance.load(std::memory_order_relaxed)];
}

// The distance (squared, as the culling gives it) a level of detail is chosen by: the camera's as if it were the choice's scale
// times nearer, so its nearest and farthest distances and its meshes' switches all move out (in for short) together; the
// original's unchanged
inline u32 LodDistance(u32 distance)
{
    const f32 scale = RenderDistanceScale();
    if (scale == 1.0f)
    {
        return distance;
    }

    const double nearer = static_cast<double>(distance) / (static_cast<double>(scale) * scale);
    return nearer >= 4294967295.0 ? 0xFFFFFFFFu : static_cast<u32>(nearer);
}

// No level of detail's cut-off (unlimited): its farthest mesh drawn however far
inline bool LodCutOffGone()
{
    return g_RenderDistance.load(std::memory_order_relaxed) == DistanceUnlimited;
}

// Whether a screen effect reads the depth now (renderer/effects.cpp: the colour filter on with a palette that changes a colour)
bool ScreenEffectsReadDepth();

// The far plane's scale a projection made now takes: the choice's, unless a screen effect reads the depth (then the game's;
// $TWIN_RENDER_DISTANCE=far: the choice's anyway, to see what it changes)
f32 FarPlaneScaleNow();

// A projection made with the far plane's scale (view.cpp): kept for the culling, and a line in the log when it changes
void SetFarPlaneScale(f32 scale);

// The scale of the projection in use (the culling's frustum takes it, so the two agree)
inline f32 FarPlaneScale()
{
    return g_FarPlaneScale.load(std::memory_order_relaxed);
}
}
