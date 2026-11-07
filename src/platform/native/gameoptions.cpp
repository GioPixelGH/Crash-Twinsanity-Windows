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

