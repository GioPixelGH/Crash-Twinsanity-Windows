// The game options of 1.2.0: see gameoptions.h
#include "gameoptions.h"

#include "game/clock.h"
#include "ui/nativeui.h"

// Infinite lives
bool NativeGame::InfiniteLives()
{
    return NativeUi::IsOn(NativeUi::OptionInfiniteLives);
}

// The automatic camera, and the assisted one
bool NativeGame::AutoCamera()
{
    return NativeUi::GetOption(NativeUi::OptionAutoCamera) == static_cast<s32>(CameraAutomatic);
}

bool NativeGame::AssistedCamera()
{
    return NativeUi::GetOption(NativeUi::OptionAutoCamera) == static_cast<s32>(CameraAssisted);
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

// The double jump
u32 NativeGame::DoubleJump()
{
    return static_cast<u32>(NativeUi::GetOption(NativeUi::OptionDoubleJump));
}

// Three masks' touch: the option, and the character the mask last said was invincible, until when
bool NativeGame::MaskTouch()
{
    return NativeUi::IsOn(NativeUi::OptionMaskTouch);
}

namespace
{
const void* g_MaskTouchCharacter = nullptr;
u32 g_MaskTouchUntil = 0;
}

void NativeGame::SetMaskTouching(const void* character)
{
    g_MaskTouchCharacter = character;
    g_MaskTouchUntil = g_GlobalClock.time + static_cast<u32>(MaskTouchSeconds * g_ClockUnitsPerSecond);
}

bool NativeGame::MaskTouching(const void* character)
{
    return character != nullptr && character == g_MaskTouchCharacter && static_cast<s32>(g_MaskTouchUntil - g_GlobalClock.time) >= 0;
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

// Its outline (THE LIST 52, #11)
bool NativeGame::DropShadowOutlined()
{
    return NativeUi::GetOption(NativeUi::OptionDropShadow) == DropShadowOutline;
}

// Aku Aku's shadow
bool NativeGame::MaskShadow()
{
    return NativeUi::IsOn(NativeUi::OptionMaskShadow);
}

// The shadows on crates
bool NativeGame::ShadowsOnCrates()
{
    return NativeUi::IsOn(NativeUi::OptionShadowsOnCrates);
}
