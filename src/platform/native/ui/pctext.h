#pragma once

// The PC wording's table (pctext.cpp), for the override and the self-test
#include "ui/nativeui.h"

#include "game/language.h"

#include <string>

namespace NativeUi
{
struct TextOverride
{
    u32 file;
    const char* language;
    u32 line;
    const char* original;
    const char* replacement;
};

constexpr u32 TextLanguages = 5;
// The native texts' columns: the game's five languages', then Brazilian Portuguese's (THE LIST 56, #25: shown where the disc's
// text bank is Portuguese, as #25's disc has it in its English slot; a line without it, nullptr or empty, shows the English)
constexpr u32 PortugueseColumn = TextLanguages;
constexpr u32 NativeTextColumns = TextLanguages + 1;

struct NativeTextLine
{
    u32 text;
    const char* languages[NativeTextColumns];
};

extern const TextOverride g_TextOverrides[];
extern const u32 g_TextOverrideCount;
extern const NativeTextLine g_NativeTexts[];
extern const u32 g_NativeTextCount;
// The game's languages as its Language folders name them, in the native texts' order
extern const char* const g_TextLanguages[TextLanguages];

// A language's index in g_TextLanguages (-1: none)
s32 TextLanguageIndex(const char* language);
// A native text's line for a language's text bank: Portuguese's when the bank is Portuguese and the line has it, else the
// English's there; the language's own (English for an unknown one) elsewhere
const char* NativeTextFor(const NativeTextLine& line, s32 index, bool portuguese);
// UTF-8 to the game's Windows-1252: false when a character has no code there
bool ToGameText(const char* utf8, std::string* out);
// Whether an entry replaces a line of a language's bank: its own language's, and for the North American disc's American (THE
// LIST 31) the English entries too, but where American has its own
bool TextOverrideApplies(const TextOverride& entry, const char* language);
}
