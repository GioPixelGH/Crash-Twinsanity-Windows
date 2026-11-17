#pragma once

// Replacement textures in PCSX2's format (native/research/HD_TEXTURES.md): <settings>/textures/<serial>/replacements/**,
// PNG or DDS files named by the texture's hash in local memory (XXH3-64 of its GS blocks or of its texels), its palette's hash,
// its CLAMP region and a packed word of TEX0's format and size and TEXA. Any pack made for PCSX2 works unchanged. Nothing is
// bundled: the user's own files are read

#include "common.h"

#include <string>
#include <vector>

namespace Gs
{
class Gs;
struct TextureLevel;
union RegTex0;
union RegClamp;
}

namespace NativeGraphics
{
struct ReplacementKey
{
    u64 textureHash;
    u64 clutHash;
    u32 bits;
    u16 regionWidth;
    u16 regionHeight;
    bool operator<(const ReplacementKey& other) const;
};

// The folders searched (made when the replacements are switched on: serials SLES-52568, SLUS-20909, SLPM-65801)
void ReplacementsSetEnabled(bool on);
bool ReplacementsEnabled();
size_t ReplacementCount();
// The replacement images read so far (kept in memory once read): how many, and their bytes (the log's memory line, #55)
size_t ReplacementImagesHeld(size_t* bytes);

// A texture's key as PCSX2 names it (level 0 only, as with PCSX2's mipmapping off); false for a format it doesn't replace
bool ReplacementKeyFor(const Gs::Gs& gs, const Gs::TextureLevel& level, const Gs::RegTex0& tex0, const Gs::RegClamp& clamp,
                       ReplacementKey* key);
// A key's file name (as PCSX2 dumps it, without the folder)
std::string ReplacementName(const ReplacementKey& key);

// The replacement image of a key (read the first time; null when there's none or it can't be read): RGBA with the PS2's
// alpha (0x80 = 1.0), its size
struct ReplacementImage
{
    u32 width = 0;
    u32 height = 0;
    std::vector<u32> pixels;
};
const ReplacementImage* FindReplacement(const ReplacementKey& key);
}
