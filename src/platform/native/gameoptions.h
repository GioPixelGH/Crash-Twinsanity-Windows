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

// The drop shadow (drop_shadow, #11, #35): a round shadow on the ground under the played character while it's off the ground,
// as Crash 4's (shadows.cpp's CastDropShadow: drawn as the game's shadows are)
namespace NativeGame
{
bool DropShadow();
}

// The played character's shadow on the tops of crates too (shadows_on_crates; the PS2 draws none there): the crates' opaque
// materials made shadow receivers (fixes.cpp's MaterialRead, the community mod's change)
namespace NativeGame
{
bool ShadowsOnCrates();
}
