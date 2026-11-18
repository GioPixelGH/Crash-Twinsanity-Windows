#pragma once

// The game options of 1.2.0 (THE LIST 6, 10, 11, 12, 13, 14, 38; native/NATIVE.md, "Game options"): changes to the experience
// with the original content only, each off by default, none leaving a save the game couldn't have made itself. The game's code
// asks them where it decides (under TWIN_NATIVE); the settings screen's Gameplay page sets them (options.txt).

#include "common.h"

// Infinite lives (infinite_lives): a death costs no life, the count shown stays; a checkpoint restart never ends in a game over
// for want of lives (commandsgame.cpp's AddLivesCommand, gamecontroller.cpp's RestartFromCheckpoint)
namespace NativeGame
{
bool InfiniteLives();
}

// The stronger Aku Aku mask (strong_mask, #30): with one or two masks an explosion (TNT, Nitro, a bomb) takes a mask as any hit
// does instead of killing, and the invincibility of three masks or the blink after a hit keeps explosions out
// (characterstate.cpp's CharacterAgent::Contact), as in the earlier Crash games
namespace NativeGame
{
bool StrongMask();
}

// The automatic camera (auto_camera): 1 the look stick and the mouse don't turn the follow camera, which swings behind the character
// by itself while it moves; 2 assisted (THE LIST 83, #42, #45): they still turn it, and it swings behind the moving character once
// they've been let go the yaw's own hold time, a second (followrig.cpp's FollowCameraRig::Frame)
namespace NativeGame
{
constexpr u32 CameraAutomatic = 1;
constexpr u32 CameraAssisted = 2;
bool AutoCamera();
bool AssistedCamera();
}

// The slide jump (slide_jump, #11, #35): 0 the original (the long jump only after the slide's first steering moment), 1 from any
// moment of the slide, 2 easier (any moment, the jump pressed a quarter second after the slide, getting up, still counted, steered
// in the air as a jump is); its height and distance stay the original's (charactermoves.cpp). The easier slide jump's late
// press: set for the jump's frame while the character gets up from a slide within SlideJumpLateSeconds (characterframe.cpp), so
// JumpController::Grounded takes the jump as the slide's. The slide's steering moment (CrouchController::Slide, the frame's): a
// slide jump taken then is the option's (the log says so)
namespace NativeGame
{
u32 SlideJump();
constexpr f32 SlideJumpLateSeconds = 0.25f;
void SetSlideJumpLate(bool late);
bool SlideJumpLate();
void SetSlideSteering(bool steering);
bool SlideSteering();
}

// The double jump (double_jump, THE LIST 53, #47): 0 the original (any moment of the rise, and in the fall only for
// LateDoubleSeconds, 0.4, weaker the later; none after a slide jump), 1 later (the fall's window DoubleJumpLaterSeconds, the
// strength by the original's rule: half at the most lost), 2 any moment (the whole fall, at full strength); with 1 and 2 after a
// slide jump too. Once a jump, as the original; not when walking off an edge, nor tied (charactermoves.cpp's JumpController)
namespace NativeGame
{
constexpr u32 DoubleJumpLater = 1;
constexpr u32 DoubleJumpAnyMoment = 2;
constexpr f32 DoubleJumpLaterSeconds = 1.0f;
u32 DoubleJump();
}

// Three masks' touch (mask_touch, THE LIST 54, #18): while three masks make the played character invincible, what it touches is
// hit as by its spin (characterstate.cpp's CharacterAgent::ContactAttack): crates break and enemies are beaten as a spin does it,
// by their own scripts, so a save stays one the game could make. The mask's controller says so each frame it's invincible on the
// player (nodecontrollers.cpp's MaskController::Frame), for MaskTouchSeconds of the global clock
namespace NativeGame
{
constexpr f32 MaskTouchSeconds = 0.1f;
bool MaskTouch();
void SetMaskTouching(const void* character);
bool MaskTouching(const void* character);
// As in the earlier games (THE LIST 93, #56): what the character stands on is hit only once it has stood on it MaskStandSeconds
// by the game clock (now), touches less than MaskTouchSeconds apart counting as one stand, so a bridge of crates can be crossed
constexpr f32 MaskStandSeconds = 1.0f;
bool MaskStoodOn(const void* other, u32 now);
}

// The longer invincibility (long_invincibility, THE LIST 93, #56): three masks make the played character invincible for 20 s, as
// in the earlier games, instead of 8 (agents.cpp's CharacterAgent::StartInvincibility)
namespace NativeGame
{
constexpr f32 LongInvincibilitySeconds = 20.0f;
bool LongInvincibility();
}

// The drop shadow (drop_shadow, #11, #35): a round shadow on the ground under the played character while it's off the ground,
// as Crash 4's (shadows.cpp's CastDropShadow: drawn as the game's shadows are); 0 off, 1 on, 2 with a yellow ring around it on the
// ground (THE LIST 52, #11: the shadow alone hardly shows on dark ground; renderer/shadows.cpp's OutlineDropShadow)
namespace NativeGame
{
constexpr s32 DropShadowOutline = 2;
bool DropShadow();
bool DropShadowOutlined();
}

// Aku Aku's shadow (mask_shadow, THE LIST 84, #51): a round shadow under the mask while it comes down to, follows or leaves the
// played character (shadows.cpp's CastMaskShadow, from MaskController::Frame)
namespace NativeGame
{
bool MaskShadow();
}

// The played character's shadow on the tops of crates too (shadows_on_crates; the PS2 draws none there): the crates' opaque
// materials made shadow receivers (fixes.cpp's MaterialRead, the community mod's change)
namespace NativeGame
{
bool ShadowsOnCrates();
}
