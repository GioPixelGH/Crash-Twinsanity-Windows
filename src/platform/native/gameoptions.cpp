// The game options of 1.2.0: see gameoptions.h
#include "gameoptions.h"

#include "ui/nativeui.h"

// Infinite lives
bool NativeGame::InfiniteLives()
{
    return NativeUi::IsOn(NativeUi::OptionInfiniteLives);
}

// The stronger Aku Aku mask
bool NativeGame::StrongMask()
{
    return NativeUi::IsOn(NativeUi::OptionStrongMask);
}

// The slide jump: the option, its late press and its steering moment for the frame
u32 NativeGame::SlideJump()
{
    return static_cast<u32>(NativeUi::GetOption(NativeUi::OptionSlideJump));
}

namespace
{
// The slide jump's late press and steering moment, this frame's
bool g_SlideJumpLate = false;
bool g_SlideSteering = false;
}

void NativeGame::SetSlideJumpLate(bool late)
{
    g_SlideJumpLate = late;
}

bool NativeGame::SlideJumpLate()
{
    return g_SlideJumpLate;
}

void NativeGame::SetSlideSteering(bool steering)
{
    g_SlideSteering = steering;
}

bool NativeGame::SlideSteering()
{
    return g_SlideSteering;
}

// The drop shadow
bool NativeGame::DropShadow()
{
    return NativeUi::IsOn(NativeUi::OptionDropShadow);
}

// The shadows on crates
bool NativeGame::ShadowsOnCrates()
{
    return NativeUi::IsOn(NativeUi::OptionShadowsOnCrates);
}
