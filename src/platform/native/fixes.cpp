// The native build's optional bug fixes: whether they're on, and the ones made on the data the game reads from the disc
#include "fixes.h"

#include "gameoptions.h"
#include "native.h"
#include "ui/nativeui.h"

#include "game/renderer.h"

#include <cstdlib>
#include <cstring>

namespace NativeFixes
{
namespace
{
// $NAME=0 or 1 over an option (the tests run with either without touching the app's settings); -1 none
s32 Override(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || (value[0] != '0' && value[0] != '1'))
    {
        return -1;
    }

    return value[0] - '0';
}

u32 ReadU32(const u8* at)
{
    u32 value;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

// The crate materials' names, as the community mod's tools/materials.py has them (case aside): CRATE_, CRATES_, AKUCRATE_,
// AKUCRATES_ and life_crate (the extra life's)
bool CrateName(const u8* name, u32 length)
{
    auto starts = [&](const char* prefix)
    {
        u32 count = static_cast<u32>(std::strlen(prefix));
        if (length < count)
        {
            return false;
        }

        for (u32 i = 0; i < count; i++)
        {
            char c = static_cast<char>(name[i]);
            if (c >= 'a' && c <= 'z')
            {
                c = static_cast<char>(c - 'a' + 'A');
            }

            if (c != prefix[i])
            {
                return false;
            }
        }

        return true;
    };
    return starts("CRATE_") || starts("CRATES_") || starts("AKUCRATE_") || starts("AKUCRATES_") || starts("LIFE_CRATE");
}
}

bool RetailFixes()
{
    static const s32 forced = Override("TWIN_RETAIL_FIXES");
    return forced >= 0 ? forced != 0 : NativeUi::IsOn(NativeUi::OptionRetailFixes);
}

bool Ultrawide()
{
    return g_WidescreenTv != 0 && NativeUi::IsOn(NativeUi::OptionUltrawide) && !NativeUi::IsOn(NativeUi::OptionSuperUltrawide);
}

bool SuperUltrawide()
{
    return g_WidescreenTv != 0 && NativeUi::IsOn(NativeUi::OptionSuperUltrawide);
}

f32 WideTvAspect()
{
    // TechieSaru's 21:9 fix (Crash-Twinsanity-Improved's "PCSX2 patches", [Widescreen 21:9 Ultrawide]) writes 0x4015 and 0x5554
    // over the halves of 16:9's 0x3FE38E39 where the renderer loads it (0x19BB48, 0x1A074C, 0x1A07DC, 0x1A0DA0)
    constexpr f32 UltrawideAspect = 0x1.2AAAA8p+1f;
    // 32:9 the same way, the game's projection and 2D squeeze (renderer.cpp's ScreenSqueeze) taking the shape
    constexpr f32 SuperUltrawideAspect = 32.0f / 9.0f;
    return SuperUltrawide() ? SuperUltrawideAspect : Ultrawide() ? UltrawideAspect : WideAspect;
}

bool SkipCutscenes()
{
    return NativeUi::IsOn(NativeUi::OptionSkipCutscenes);
}

// The community mod's "shadows on crates" (wiki: engine/graphics, "Shadows"; tools/materials.py): the characters' shadow only
// darkens the pixels of materials whose shader has the settings' byte 24 set (the decomp's noFba), which level scenery has and most
// crate materials don't, so Crash's shadow never showed on a crate. Every opaque shader (no blending) of a crate material gets it.
// The item: its programs' key (8 bytes), its bucket, its name's length and name, its shaders' count and the shaders: each a type,
// the type's own fields (0x10 and 0x11: 4 bytes, 0x17: 12, 0x1A: 20), 30 settings bytes (the blending first, the one changed at
// 24), TEX1's K and L, 16 leftover bytes, the colour, the scrolls' phases and speeds, the texture and the type. As the mod's tool,
// a material that doesn't read to its item's end exactly (an animated shader, a type with other fields) is left alone
void MaterialRead(u8* data, u32 size)
{
    constexpr u32 SettingsBytes = 30;
    constexpr u32 ShadowReceiverSetting = 24;
    constexpr u32 AfterSettings = 4 + 16 + 16 + 16 + 4 + 4;
    // (The bug fixes' change, and the shadows on crates game option's: THE LIST 38, from a level's next load)
    if ((!RetailFixes() && !NativeGame::ShadowsOnCrates()) || size < 16)
    {
        return;
    }

    u32 at = 12;
    u32 nameLength = ReadU32(data + at);
    at += 4;
    if (nameLength > size - at || !CrateName(data + at, nameLength))
    {
        return;
    }

    at += nameLength;
    if (size - at < 4)
    {
        return;
    }

    u32 count = ReadU32(data + at);
    at += 4;
    constexpr u32 MaxShaders = 64;
    u32 settings[MaxShaders];
    if (count > MaxShaders)
    {
        return;
    }

    for (u32 i = 0; i < count; i++)
    {
        if (size - at < 4)
        {
            return;
        }

        u32 type = ReadU32(data + at);
        at += 4;
        at += type == 0x17 ? 12 : type == 0x1A ? 20 : type == 0x10 || type == 0x11 ? 4 : 0;
        if (at > size || size - at < SettingsBytes + AfterSettings)
        {
            return;
        }

        settings[i] = at;
        at += SettingsBytes + AfterSettings;
    }

    if (at != size)
    {
        return;
    }

    for (u32 i = 0; i < count; i++)
    {
        u8* shader = data + settings[i];
        if (shader[0] == 0 && shader[ShadowReceiverSetting] == 0)
        {
            shader[ShadowReceiverSetting] = 1;
            // ($TWIN_FIXES_TRACE: each crate material made a shadow receiver said, its option's check)
            static const bool tracing = std::getenv("TWIN_FIXES_TRACE") != nullptr;
            if (tracing)
            {
                Native::Log("fixes: a crate material's shader made a shadow receiver (%.*s)", static_cast<int>(nameLength),
                            reinterpret_cast<const char*>(data + 16));
            }
        }
    }
}
}
