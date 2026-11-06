// The text language and the speech language, two settings (THE LIST 7; native/NATIVE.md, "Languages"). The language option
// (`language`) is the texts' (the menus, the title pictures, the credits) and, unless the speech language option
// (`speech_language`) chooses another, the voices' too: the voice bank (CRASH6\<language>.MB), the levels' voices and the movies'
// sound channel (include/game/language.h's SpeechLanguage). Both from the game's next start, as the game loads them once.
//
// A disc's text banks are recognised by what they say, not by their names (TwinsanityEvolved's way: its language_probes.h,
// languageprobes.h here): #25's Brazilian Portuguese disc keeps its Portuguese in the English slot (Language\Code\English.txt),
// so the text language's choice for that slot says "português", and its voices stay the English slot's unless another is chosen.
#include "ui/nativeui.h"
#include "ui/languageprobes.h"

#include "native.h"
#include "game/language.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace NativeUi
{
namespace
{
// The options' languages (the language option's order: 1 English ... 5 Spanish) and the game's names for them (its Language
// folders' and the speech banks')
constexpr const char* const OptionNames[] = {nullptr, "English", "French", "German", "Italian", "Spanish"};

u32 Le32(const u8* bytes)
{
    return static_cast<u32>(bytes[0]) | static_cast<u32>(bytes[1]) << 8 | static_cast<u32>(bytes[2]) << 16 |
           static_cast<u32>(bytes[3]) << 24;
}

// The first bytes of an archive's file (CRASH6\CRASH.BD's, by CRASH.BH's table), a mod's in its place; "" when there's none
std::string ArchiveStart(const std::string& path, u32 most)
{
    Native::Disc::Entry table;
    Native::Disc::Entry archive;
    if (!Native::Disc::Find("CRASH6\\CRASH.BH", &table) || !Native::Disc::Find("CRASH6\\CRASH.BD", &archive) ||
        table.size > (1u << 20))
    {
        return {};
    }

    u32 start = 0;
    u32 size = 0;
    bool found = Native::Disc::FindArchiveMod(path.c_str(), &start, &size);
    if (!found)
    {
        std::vector<u8> bytes(table.size);
        Native::Disc::Read(table.offset, bytes.data(), table.size);
        for (u32 at = 4; at + 4 <= bytes.size() && !found;)
        {
            const u32 length = Le32(&bytes[at]);
            if (length == 0 || length > 0x400 || at + 4 + length + 8 > bytes.size())
            {
                break;
            }

            std::string name(reinterpret_cast<const char*>(&bytes[at + 4]), length);
            name.resize(std::strlen(name.c_str()));
            if (name.size() == path.size() && std::equal(name.begin(), name.end(), path.begin(), [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                }))
            {
                start = Le32(&bytes[at + 4 + length]);
                size = Le32(&bytes[at + 8 + length]);
                found = true;
            }

            at += 4 + length + 8;
        }
    }

    if (!found)
    {
        return {};
    }

    std::string text(std::min(size, most), '\0');
    const u32 read = Native::Disc::Read(archive.offset + start, text.data(), static_cast<u32>(text.size()));
    text.resize(read);
    return text;
}

struct Recognised
{
    bool done = false;
    // Each option language's text bank's language by its content ("" as its name says, or unknown), lower case
    std::string content[6];
};
Recognised g_Recognised;
std::mutex g_Lock;

const Recognised& Recognise()
{
    std::lock_guard lock(g_Lock);
    if (g_Recognised.done)
    {
        return g_Recognised;
    }

    g_Recognised.done = true;
    for (u32 option = 1; option < 6; option++)
    {
        std::string text = ArchiveStart(std::string("Language\\Code\\") + OptionNames[option] + ".txt",
                                        static_cast<u32>(ps2r::app::language_probes::kProbeBytes));
        std::string language = text.empty() ? std::string() : ps2r::app::language_probes::Recognize(text, nullptr);
        std::string named = OptionNames[option];
        for (char& c : named)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        g_Recognised.content[option] = language == named ? std::string() : language;
    }

    return g_Recognised;
}
}

// The language a text choice's bank is written in when that isn't its name (#25's "portuguese" in the English slot), else nullptr
const char* TextBankLanguage(s32 option)
{
    if (option < 1 || option > 5)
    {
        return nullptr;
    }

    const std::string& content = Recognise().content[option];
    return content.empty() ? nullptr : content.c_str();
}

void LogLanguages()
{
    std::string unlike;
    for (s32 option = 1; option <= 5; option++)
    {
        if (const char* content = TextBankLanguage(option))
        {
            unlike += std::string(unlike.empty() ? "" : "; ") + "the " + OptionNames[option] + " texts are in " + content;
        }
    }

    const s32 speech = GetOption(OptionSpeechLanguage);
    Native::Log("languages: the texts' banks as their names say%s%s; the voices %s", unlike.empty() ? "" : " but: ",
                unlike.c_str(), speech <= 0 ? "as the texts" : OptionNames[speech]);
}
}

// The game's language index (g_LanguageNames') the voices use: the speech language option's when it chose one the game has, read
// once (the voice bank is loaded once, the levels' voices with each level: a change mid-game would mix them), else the texts'
u32 NativeSpeechLanguage(u32 language)
{
    static const s32 chosen = NativeUi::GetOption(NativeUi::OptionSpeechLanguage);
    if (chosen <= 0 || chosen > 5 || g_LanguageNames == nullptr)
    {
        return language;
    }

    for (u32 index = 0; index < g_LanguageCount; index++)
    {
        if (g_LanguageNames[index] != nullptr && std::strcmp(g_LanguageNames[index], NativeUi::OptionNames[chosen]) == 0)
        {
            return index;
        }
    }

    return language;
}
