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

