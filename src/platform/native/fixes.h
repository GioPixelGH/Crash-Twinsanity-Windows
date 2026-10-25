#pragma once

// The native build's optional bug fixes (native/NATIVE.md, "Bug fixes (optional)"): the community mod's gameplay fixes and the
// retail bugs of docs/RETAIL_BUGS.md whose intent is evident, each in the game's code under TWIN_NATIVE and asked for here. With
// the "bug fixes" option off (OptionRetailFixes, the Gameplay tab) the game plays as retail
#include "common.h"

struct ScriptGraph;
struct StateBody;
struct BehaviourRunner;
struct BehaviourLevel;
struct TimeClock;

namespace NativeFixes
{
// Whether the bug fixes are on ($TWIN_RETAIL_FIXES=0 or 1 overrides the option, for tests)
bool RetailFixes();
// Whether the cutscene skip option is on (the level recipes that restore the scenes' cut skips go with it)
bool SkipCutscenes();

// The level recipes (levelrecipes.cpp): the community mod's edits of the levels' behaviour scripts, made on the scripts as they're
// read from the disc. A graph read (its ID and the item's bytes), and a graph let go of
void ScriptRead(ScriptGraph* graph, u32 id, const u8* data, u32 size);
void ScriptDestroyed(ScriptGraph* graph);
// --selftest-recipes (in the game, once it's started): every script of every level on the disc read back unchanged, and every
// recipe made on its level's scripts. 0 when it passed
int SelfTestRecipes();
// $TWIN_FIXES_TEST=skips: every skippable scene of every level and what its skip leaves undone (levelrecipes.cpp). 0 when it ran
int SurveySkips();

// The cutscene skip's end of a scene (sceneskip.cpp): whether a body that passed is a skip's rule (condition 572 with a player)
// with the skip option on; and, as it jumps a director's level out of the state that runs the scene, what the rest of the scene
// would have done: the player's inputs given back, its "over" messages (207) sent, the actors it gives back noted
bool IsSkipRule(const StateBody* body);
void SceneSkipped(BehaviourRunner* runner, BehaviourLevel* level, TimeClock* clock);
// Once a frame: the skips' actors given back their own object as they finish the scene's behaviour (sent 207 when they wait for
// it), or stopped past a time
void GiveBackFrame();

// Once a frame (the pads' read): the checks of $TWIN_FIXES_TEST (fixestest.cpp)
void Frame();

// The 21:9 option (OptionUltrawide) and the game's widescreen setting both on: the widescreen picture is 21:9 (fixes.cpp)
bool Ultrawide();
// The widescreen TV's shape: 21:9 (the patch's 0x40155554) with Ultrawide, else 16:9 (WideAspect)
f32 WideTvAspect();

// A material item about to be read (its bytes, changed in place): opaque crates' shaders made shadow receivers
void MaterialRead(u8* data, u32 size);
}
