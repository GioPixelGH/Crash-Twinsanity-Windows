// The native UI's command line: --selftest-ui (the PC wording against the disc, the save flow's native changes on the game's own save
// code, the resolution setting's choices and file) and --dump-texts (every line of every language's text files as the native build
// has them, the PC wording applied)
#include "ui/pctext.h"
#include "ui/bindings.h"
#include "ui/overlayinternal.h"

#include "native.h"

#include "game/clock.h"
#include "game/controllers.h"
#include "game/savedevice.h"
#include "game/savemanager.h"
#include "gcc2.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>
#include <vector>

namespace NativeUi
{
namespace
{
// ------------------------------------------------------------------------------------------------------------- the disc's texts
constexpr const char* TextFolders[TextFiles] = {"Code", "AgentLab"};

u32 Le32(const u8* bytes)
{
    return static_cast<u32>(bytes[0]) | static_cast<u32>(bytes[1]) << 8 | static_cast<u32>(bytes[2]) << 16 |
           static_cast<u32>(bytes[3]) << 24;
}

std::string Upper(std::string text)
{
    for (char& c : text)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    return text;
}

// The archive's files (CRASH6\CRASH.BH's table of CRASH.BD): upper-case path -> (offset, size)
struct Archive
{
    Native::Disc::Entry data{};
    std::map<std::string, std::pair<u32, u32>> files;
    // The paths as the table has them, by their upper case
    std::map<std::string, std::string> names;
};

bool OpenArchive(Archive* archive)
{
    const char* image = Native::GetSettings().discImage;
    Native::Disc::Entry table;
    if (image == nullptr || !Native::Disc::Open(image) || !Native::Disc::Find("CRASH6\\CRASH.BH", &table) ||
        !Native::Disc::Find("CRASH6\\CRASH.BD", &archive->data))
    {
        Native::Log("no disc image to read the texts from (--iso, $TWINSANITY_ISO or local.json's disc_image)");
        return false;
    }

    std::vector<u8> bytes(table.size);
    Native::Disc::Read(table.offset, bytes.data(), table.size);
    for (u32 at = 4; at + 4 <= bytes.size();)
    {
        u32 length = Le32(&bytes[at]);
        if (length == 0 || length > 0x400 || at + 4 + length + 8 > bytes.size())
        {
            break;
        }

        std::string name(reinterpret_cast<const char*>(&bytes[at + 4]), length);
        name.resize(std::strlen(name.c_str()));
        archive->files[Upper(name)] = {Le32(&bytes[at + 4 + length]), Le32(&bytes[at + 8 + length])};
        archive->names[Upper(name)] = name;
        at += 4 + length + 8;
    }

    return !archive->files.empty();
}

// A language's text file's lines as ReadTextFile makes them: split at every line break, blank lines skipped (the text kept in
// storage that stays)
struct TextFile
{
    std::string text;
    std::vector<const char*> lines;
};

bool ReadText(const Archive& archive, u32 file, const char* language, TextFile* out)
{
    auto found = archive.files.find(Upper(std::string("Language\\") + TextFolders[file] + "\\" + language + ".txt"));
    if (found == archive.files.end())
    {
        return false;
    }

    out->text.assign(found->second.second + 2, '\0');
    Native::Disc::Read(archive.data.offset + found->second.first, out->text.data(), found->second.second);
    char* at = out->text.data();
    u32 breaks = 0;
    while (*at != '\0')
    {
        if ((at[0] == '\r' && at[1] == '\n') || (at[0] == '\n' && at[1] == '\r'))
        {
            at[0] = at[1] = '\0';
            breaks++;
            at += 2;
        }
        else if (at[0] == '\n' || at[0] == '\r')
        {
            *at++ = '\0';
            breaks++;
        }
        else
        {
            at++;
        }
    }

    // Unlike the retail split, lines past the text's end (with blank lines) aren't made up: they're left blank
    char* end = out->text.data() + found->second.second;
    at = out->text.data();
    for (u32 line = 0; line < breaks; line++)
    {
        out->lines.push_back(at < end ? at : "");
        while (at < end && *at++ != '\0')
        {
        }

        while (at < end && *at == '\0')
        {
            at++;
        }
    }

    return true;
}

std::string Utf8(const char* game)
{
    std::string out;
    for (const auto* at = reinterpret_cast<const unsigned char*>(game); *at != 0; at++)
    {
        u32 code = *at == 0x92 ? 0x2019 : *at == 0x85 ? 0x2026 : *at;
        if (code < 0x80)
        {
            out += static_cast<char>(code);
        }
        else if (code < 0x800)
        {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    return out;
}

std::string Lower(const char* game)
{
    std::string out = Utf8(game);
    for (char& c : out)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return out;
}

int DumpTexts()
{
    Archive archive;
    if (!OpenArchive(&archive))
    {
        return 1;
    }

    for (const char* language : g_TextLanguages)
    {
        for (u32 file = 0; file < TextFiles; file++)
        {
            TextFile text;
            if (!ReadText(archive, file, language, &text))
            {
                continue;
            }

            u32 count = static_cast<u32>(text.lines.size());
            const char** lines = ApplyTextOverrides(file, language, text.lines.data(), count);
            u32 shown = file == CodeTexts ? static_cast<u32>(NativeTextEnd) : count;
            for (u32 line = 0; line < shown; line++)
            {
                if (line >= count && lines[line][0] == '\0')
                {
                    continue;
                }

                std::printf("%s\t%s\t0x%02X\t%s\n", TextFolders[file], language, line, Utf8(lines[line]).c_str());
            }
        }
    }

    return 0;
}

// --------------------------------------------------------------------------------------------------------------- the self-test
int g_Failures = 0;

void Check(bool ok, const char* format, ...) __attribute__((format(printf, 2, 3)));
void Check(bool ok, const char* format, ...)
{
    if (ok)
    {
        return;
    }

    g_Failures++;
    va_list arguments;
    va_start(arguments, format);
    std::fputs("[native] ui self-test FAILED: ", stderr);
    std::vfprintf(stderr, format, arguments);
    std::fputc('\n', stderr);
    va_end(arguments);
}

// Words that name the console, its cards, slots, ports and pad, in the five languages (lower case, UTF-8)
constexpr const char* ConsoleWords[] = {
    "memory card", "memory~card", "(ps2)",      "ps2",        "playstation", "dualshock", "analog",  "analóg",
    "slot",        "steckplatz",  "ranura",     "fente",      "ingresso",    "console",   "consola", "konsole",
    "format",      "r1-",         "controller port", "controller-anschluss", "port de manette", "puerto de mando",
};

// The lines that keep a console word because it isn't about the console: the security consoles of the game's world
bool AboutTheWorld(u32 file, u32 line)
{
    return file == AgentLabTexts && line == 0x45;
}

void TestTexts()
{
    Archive archive;
    if (!OpenArchive(&archive))
    {
        Check(false, "the disc's texts can't be read");
        return;
    }

    u32 replaced = 0;
    // The characters the languages' files use (the game's one font has them), and the menus' value separator's colon
    std::set<unsigned char> characters = {':'};
    for (const char* language : g_TextLanguages)
    {
        for (u32 file = 0; file < TextFiles; file++)
        {
            TextFile text;
            if (ReadText(archive, file, language, &text))
            {
                for (char c : text.text)
                {
                    characters.insert(static_cast<unsigned char>(c));
                }
            }
        }
    }

    for (const char* language : g_TextLanguages)
    {
        TextFile files[TextFiles];
        for (u32 file = 0; file < TextFiles; file++)
        {
            Check(ReadText(archive, file, language, &files[file]), "no %s text file %u", language, file);
        }

        std::string converted;
        for (u32 i = 0; i < g_TextOverrideCount; i++)
        {
            const TextOverride& entry = g_TextOverrides[i];
            if (std::string_view(entry.language) != language)
            {
                continue;
            }

            const TextFile& file = files[entry.file];
            Check(ToGameText(entry.original, &converted), "%s %#x: the original has characters with no code", language, entry.line);
            Check(entry.line < file.lines.size() && converted == file.lines[entry.line], "%s file %u line %#x isn't the disc's",
                  language, entry.file, entry.line);
            Check(ToGameText(entry.replacement, &converted), "%s %#x: the PC line has characters with no code", language,
                  entry.line);
            for (char c : converted)
            {
                Check(characters.count(static_cast<unsigned char>(c)) != 0, "%s %#x: '%s' has a character (%#x) the font may not have",
                      language, entry.line, entry.replacement, static_cast<unsigned char>(c));
            }

            // The marks and glyphs kept
            std::string original(entry.original);
            std::string replacement(entry.replacement);
            for (const char* mark : {"(xxx)", "(x)"})
            {
                size_t before = 0;
                size_t after = 0;
                for (size_t at = original.find(mark); at != std::string::npos; at = original.find(mark, at + 1))
                {
                    before++;
                }

                for (size_t at = replacement.find(mark); at != std::string::npos; at = replacement.find(mark, at + 1))
                {
                    after++;
                }

                Check(before == after, "%s %#x: %s not kept", language, entry.line, mark);
            }

            for (char glyph : std::string_view("\\^{}<>[]"))
            {
                size_t before = std::count(original.begin(), original.end(), glyph);
                size_t after = std::count(replacement.begin(), replacement.end(), glyph);
                // German's hoverboard hint named R1 in words: it gets the R1 glyph the other languages have
                bool added = entry.file == AgentLabTexts && entry.line == 0x2E && std::string_view(language) == "German" &&
                             glyph == '}';
                Check(after == before + (added ? 1 : 0), "%s %#x: glyph %c not kept", language, entry.line, glyph);
            }

            replaced++;
        }

        // Every line of the language as the game has it: none still about the console
        for (u32 file = 0; file < TextFiles; file++)
        {
            u32 count = static_cast<u32>(files[file].lines.size());
            const char** lines = ApplyTextOverrides(file, language, files[file].lines.data(), count);
            for (u32 line = 0; line < count; line++)
            {
                std::string lower = Lower(lines[line]);
                for (const char* word : ConsoleWords)
                {
                    Check(lower.find(word) == std::string::npos || AboutTheWorld(file, line),
                          "%s file %u line %#x still says \"%s\": %s", language, file, line, word, lower.c_str());
                }
            }

            if (file == CodeTexts)
            {
                for (u32 i = 0; i < g_NativeTextCount; i++)
                {
                    const NativeTextLine& text = g_NativeTexts[i];
                    Check(ToGameText(text.languages[TextLanguageIndex(language)], &converted) && converted == lines[text.text],
                          "%s's native text %#x isn't there", language, text.text);
                    for (char c : converted)
                    {
                        Check(characters.count(static_cast<unsigned char>(c)) != 0,
                              "%s native text %#x has a character (%#x) the font may not have", language, text.text,
                              static_cast<unsigned char>(c));
                    }
                }

                Check(lines[count][0] == '\0' && lines[NativeTextFirst - 1][0] == '\0', "%s: the lines past the file's aren't blank",
                      language);
            }
        }
    }

    Check(replaced == g_TextOverrideCount, "%u of %u PC lines checked", replaced, g_TextOverrideCount);

    // A line that isn't the disc's is left alone
    const char* other[0x30] = {};
    for (const char*& line : other)
    {
        line = "something else";
    }

    const char** kept = ApplyTextOverrides(CodeTexts, "English", other, 0x30);
    Check(std::strcmp(kept[0x27], "something else") == 0, "a line that isn't the disc's was replaced");
    Native::Log("ui self-test: %u PC lines checked against the disc, every language's lines free of console wording",
                replaced);
    // The lines kept point into this test's files, which go now (the prompts' relabelling would read them)
    ForgetTextLines();
}

// ------------------------------------------------------------------------------------------------------------- the save flow
// The game's own save code (savecode.cpp) on a device whose storage is a computer's: always there and ready, and what it holds and
// has room for set by the test. Its vtable's asks, screens and finish are recorded
struct Recorded
{
    u32 asked = 0;
    s32 choices = -1;
    s32 slots = -1;
    s32 finished = -1;
};
Recorded g_Recorded;

struct TestStorage
{
    u32 hasCard = 1;
    u32 hasSave = 0;
    u32 free = 8 << 20;
    u32 needed = 0x3D800;
};
TestStorage g_Storage;

u32 Ask(void*, u32 operation, u32)
{
    g_Recorded.asked = operation;
    return 1;
}

u32 ShowChoices(void* code, u32 screen)
{
    g_Recorded.choices = static_cast<s32>(screen);
    static_cast<SaveCode*>(code)->bits.screen = screen;
    return screen;
}

u32 ShowSlots(void* code, u32 screen)
{
    g_Recorded.slots = static_cast<s32>(screen);
    static_cast<SaveCode*>(code)->bits.screen = screen;
    return screen;
}

void Finish(void* code, u32 flagged, u32 saveDue)
{
    g_Recorded.finished = static_cast<s32>(flagged);
    static_cast<SaveCode*>(code)->Finish(flagged, saveDue);
}

u32 Nothing(void*)
{
    return 0;
}

u32 HasCard(void*)
{
    return g_Storage.hasCard;
}

u32 Formatted(void*)
{
    return 1;
}

u32 HasSave(void*)
{
    return g_Storage.hasSave;
}

u32 SavedBytes(void*)
{
    return g_Storage.hasSave != 0 ? g_Storage.needed : 0;
}

u32 NeededBytes(void*)
{
    return g_Storage.needed;
}

u32 FreeBytes(void*)
{
    return g_Storage.free;
}

GccVTableEntry Entry(void* function)
{
    return {0, 0, function};
}

void TestSaveFlow()
{
    GccVTableEntry codeTable[10] = {};
    codeTable[SaveCode::AskSlot] = Entry(reinterpret_cast<void*>(&Ask));
    codeTable[SaveCode::ShowChoicesSlot] = Entry(reinterpret_cast<void*>(&ShowChoices));
    codeTable[SaveCode::ShowSlotsSlot] = Entry(reinterpret_cast<void*>(&ShowSlots));
    codeTable[SaveCode::FinishSlot] = Entry(reinterpret_cast<void*>(&Finish));
    GccVTableEntry deviceTable[14] = {};
    for (GccVTableEntry& entry : deviceTable)
    {
        entry = Entry(reinterpret_cast<void*>(&Nothing));
    }

    deviceTable[SaveDevice::HasCardSlot] = Entry(reinterpret_cast<void*>(&HasCard));
    deviceTable[SaveDevice::CardFormattedSlot] = Entry(reinterpret_cast<void*>(&Formatted));
    deviceTable[SaveDevice::HasSaveSlot] = Entry(reinterpret_cast<void*>(&HasSave));
    deviceTable[SaveDevice::SavedBytesSlot] = Entry(reinterpret_cast<void*>(&SavedBytes));
    deviceTable[SaveDevice::NeededBytesSlot] = Entry(reinterpret_cast<void*>(&NeededBytes));
    deviceTable[SaveDevice::FreeBytesSlot] = Entry(reinterpret_cast<void*>(&FreeBytes));

    // Four slots' summaries, none holding a game
    FolderSummary summaries[4] = {};
    FolderSummary* summaryList[4] = {&summaries[0], &summaries[1], &summaries[2], &summaries[3]};
    FolderFile folder = {};
    folder.summaries = summaryList;
    SaveDevice device = {};
    device.vtable = deviceTable;
    device.folder = &folder;
    device.flags.fileCount = 4;
    SaveCode code = {};
    code.vtable = codeTable;
    code.device = &device;

    auto start = [&](u32 operation, u32 screen)
    {
        g_Recorded = {};
        code.bits.value = 0;
        code.results.value = 0;
        code.bits.operation = operation;
        code.bits.screen = screen;
        device.flags.asked = SaveDevice::OperationNone;
    };
    auto answer = [&](u32 given)
    {
        code.bits.answer = given;
        code.TakeAnswer();
    };

    // A new game's save with no save yet: made at once (no "create a save file?" question), then the slots
    g_Storage = {};
    start(SaveOperationNewGameSave, SaveScreenMessage);
    code.OperationDone(SaveDevice::OperationMeasure);
    Check(g_Recorded.asked == SaveDevice::OperationCreate && g_Recorded.choices == -1,
          "no save: asked %u, screen %d (the save should be made at once)", g_Recorded.asked, g_Recorded.choices);
    code.OperationDone(SaveDevice::OperationCreate);
    Check(g_Recorded.slots == SaveScreenSaveSlots, "after the save was made: slots screen %d", g_Recorded.slots);

    // A pause save with a save there: the slots read, then shown
    g_Storage.hasSave = 1;
    start(SaveOperationPauseSave, SaveScreenMessage);
    code.OperationDone(SaveDevice::OperationMeasure);
    Check(g_Recorded.asked == SaveDevice::OperationReadFolder, "a save there: asked %u", g_Recorded.asked);

    // Not enough space: the no room screen (not "insert a card with space"), its retry measures again, back ends the save
    g_Storage = {};
    g_Storage.free = 0;
    start(SaveOperationPauseSave, SaveScreenMessage);
    code.OperationDone(SaveDevice::OperationMeasure);
    Check(g_Recorded.choices == SaveScreenNoRoom, "no space: screen %d", g_Recorded.choices);
    answer(SaveCode::AnswerFirst);
    Check(g_Recorded.asked == SaveDevice::OperationMeasure, "no space, retry: asked %u", g_Recorded.asked);
    start(SaveOperationPauseSave, SaveScreenNoRoom);
    answer(SaveCode::AnswerBack);
    Check(g_Recorded.finished == 0 && code.bits.operation == SaveOperationNone && g_Recorded.choices == -1,
          "no space, back: finished %d, screen %d", g_Recorded.finished, g_Recorded.choices);

    // Back on the slots, on a failed save, on "no save data": the save ends at once, no "cancel save?"
    g_Storage = {};
    for (u32 screen : {SaveScreenSaveSlots, SaveScreenSaveFailed, SaveScreenInsertSave})
    {
        start(SaveOperationNewGameSave, screen);
        answer(SaveCode::AnswerBack);
        Check(g_Recorded.finished == 0 && g_Recorded.choices != SaveScreenCancelSave,
              "back on screen %u: finished %d, screen %d", screen, g_Recorded.finished, g_Recorded.choices);
    }

    // Retry after a failed save measures again (the PS2's: the other slots' saves are kept)
    start(SaveOperationPauseSave, SaveScreenSaveFailed);
    answer(SaveCode::AnswerFirst);
    Check(g_Recorded.asked == SaveDevice::OperationMeasure, "failed save, retry: asked %u", g_Recorded.asked);

    // A load from a save whose slots hold no game: "no save data", not a page of no slots; with a game: the slots
    start(SaveOperationLoad, SaveScreenMessage);
    code.OperationDone(SaveDevice::OperationReadFolder);
    Check(g_Recorded.choices == SaveScreenInsertSave && g_Recorded.slots == -1, "load, no game saved: screen %d, slots %d",
          g_Recorded.choices, g_Recorded.slots);
    summaries[2].date.bits = SaveDate::Valid;
    start(SaveOperationLoad, SaveScreenMessage);
    code.OperationDone(SaveDevice::OperationReadFolder);
    Check(g_Recorded.slots == SaveScreenLoadSlots, "load, a game saved: slots %d", g_Recorded.slots);
    summaries[2].date.bits = 0;

    // The save failing to be made: the no room screen (the storage is full or can't be written)
    start(SaveOperationNewGameSave, SaveScreenMessage);
    device.flags.running = SaveDevice::OperationCreate;
    device.flags.asked = SaveDevice::AskTaken;
    device.flags.step = 3; // StepChecking: fails once its wait is over when there's no storage
    device.wait = 0;
    g_Storage.hasCard = 0;
    TimeClock clock = {};
    code.Frame(&clock);
    g_Storage.hasCard = 1;
    Check(g_Recorded.choices == SaveScreenNoRoom, "the save couldn't be made: screen %d", g_Recorded.choices);

    Native::Log("ui self-test: the save flow (made at once, no room, retry, back, load with no game, failed create) checked");
}

// ------------------------------------------------------------------------------------------------------------- the resolution
void TestResolution()
{
    Check(ScaleChoice({3, 640, 480, false}) == 2, "scale 3's choice");
    Check(DisplayChoice({1, 1280, 960, false}) == TextWindow1280 - TextWindow960, "1280x960's choice");
    Check(DisplayChoice({1, 1000, 740, false}) == 0, "a window resized by hand: the nearest");
    Check(DisplayChoice({1, 1280, 960, true}) == DisplayChoices - 1, "fullscreen's choice");
    // The defaults: 2x in a 1280 x 720 window, one of the menu's choices
    Check(DefaultResolution().scale == 2 && !DefaultResolution().fullscreen &&
              DisplayChoice(DefaultResolution()) == TextWindow1280Wide - TextWindow960,
          "the defaults: 2x, a 1280x720 window");

    // The page's choices applied only when the player changes them
    NoteChoices(1, 2);
    Check(!ChoicesChanged(1, 2) && ChoicesChanged(2, 2) && !ChoicesChanged(2, 2), "the page's choices changed or not");

    // Kept in the settings folder (a test one) and read back
    g_ResolutionToGraphics = false;
    std::string folder = (std::filesystem::temp_directory_path() / "twinsanity-ui-selftest").string();
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    std::string home = std::getenv("HOME") != nullptr ? std::getenv("HOME") : "";
    std::string xdg = std::getenv("XDG_CONFIG_HOME") != nullptr ? std::getenv("XDG_CONFIG_HOME") : "";
    std::string appData = std::getenv("APPDATA") != nullptr ? std::getenv("APPDATA") : "";
    Native::SetEnvironment("HOME", folder.c_str());
    Native::SetEnvironment("XDG_CONFIG_HOME", folder.c_str());
    // Windows' settings folder
    Native::SetEnvironment("APPDATA", folder.c_str());
    Resolution chosen = FromChoices(1, DisplayChoices - 1);
    Check(chosen.scale == 2 && chosen.fullscreen, "2x fullscreen from the choices");
    SetResolution(chosen);
    std::ifstream file(Native::SettingsFolder() + "/resolution.txt");
    std::string saved((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Check(saved.find("scale=2") != std::string::npos && saved.find("fullscreen=1") != std::string::npos,
          "resolution.txt: %s", saved.c_str());
    Resolution windowed = FromChoices(3, TextWindow1920Wide - TextWindow960);
    SetResolution(windowed);
    Check(CurrentResolution().scale == 4 && CurrentResolution().windowWidth == 1920 && CurrentResolution().windowHeight == 1080 &&
              !CurrentResolution().fullscreen,
          "4x window 1920x1080 in use");
    ApplySavedResolution();
    Check(CurrentResolution().scale == 4 && CurrentResolution().windowWidth == 1920, "4x window 1920x1080 read back");
    Native::SetEnvironment("HOME", home.c_str());
    Native::SetEnvironment("XDG_CONFIG_HOME", xdg.empty() ? nullptr : xdg.c_str());
    Native::SetEnvironment("APPDATA", appData.empty() ? nullptr : appData.c_str());

    // Windows can't remove a file that's open
    file.close();
    std::filesystem::remove_all(folder, ignored);
    Native::Log("ui self-test: the resolution's choices, applying and keeping checked");
}
}

// The keyboard and mouse scheme: the defaults, WASD's diagonals as a stick's, the mouse's keys' names, mouse look's push from the
// mouse's speed and its fall back to the middle, and "hold any button to skip"
void TestControls()
{
    SetBindingsSaving(false);
    SetOptionSaving(false);
    ResetBindings();
    Check(GetBinding(BindJump).keys[0] == SDL_SCANCODE_SPACE && GetBinding(BindSpin).keys[0] == KeyMouseLeft &&
              GetBinding(BindCrouch).keys[0] == SDL_SCANCODE_LSHIFT && GetBinding(BindMoveForward).keys[0] == SDL_SCANCODE_W &&
              GetBinding(BindCameraLeft).keys[0] == SDL_SCANCODE_Q && GetBinding(BindCameraLeft).keys[1] == SDL_SCANCODE_LEFT &&
              GetBinding(BindShoulderLeft).keys[0] == SDL_SCANCODE_Z && GetBinding(BindWalk).keys[0] == SDL_SCANCODE_LCTRL &&
              GetBinding(BindPause).keys[0] == SDL_SCANCODE_ESCAPE,
          "the keyboard and mouse defaults");

    static bool keys[SDL_SCANCODE_COUNT] = {};
    keys[SDL_SCANCODE_W] = true;
    keys[SDL_SCANCODE_D] = true;
    BoundPad pad;
    ReadBindings(keys, nullptr, &pad);
    Check(pad.leftX == 255 && pad.leftY == 0, "W and D: a diagonal all the way, as a PS2 stick's corner (%u, %u)", pad.leftX, pad.leftY);
    keys[SDL_SCANCODE_D] = false;
    ReadBindings(keys, nullptr, &pad);
    Check(pad.leftX == 128 && pad.leftY == 0, "W: all the way up (%u, %u)", pad.leftX, pad.leftY);
    keys[SDL_SCANCODE_W] = false;

    // A gamepad's stick: a circle's diagonal (an Xbox Series pad's, 0.71 of each axis) reaches the square's corner as the
    // DualShock 2's, half way a half push each, the axes' own bytes as they were
    u8 stickX = 218;
    u8 stickY = 38;
    Squared(&stickX, &stickY);
    Check(stickX >= 254 && stickY <= 1, "a stick's full diagonal to the square's corner (%u, %u)", stickX, stickY);
    stickX = 173;
    stickY = 83;
    Squared(&stickX, &stickY);
    Check(stickX == 192 && stickY == 64, "a stick's half diagonal (%u, %u)", stickX, stickY);
    bool axesKept = true;
    for (u32 value = 0; value < 256; value++)
    {
        u8 alongX = static_cast<u8>(value);
        u8 centreY = 128;
        u8 centreX = 128;
        u8 alongY = static_cast<u8>(value);
        Squared(&alongX, &centreY);
        Squared(&centreX, &alongY);
        axesKept = axesKept && alongX == value && centreY == 128 && centreX == 128 && alongY == value;
    }
    Check(axesKept, "a stick pushed along an axis kept as it was");

    // The mouse's keys' names (the native texts' English: the game's texts aren't loaded here) all there and all different
    std::set<std::string> names;
    for (u32 text : {TextMouseLeft, TextMouseRight, TextMouseMiddle, TextMouse4, TextMouse5, TextWheelUp, TextWheelDown})
    {
        for (u32 i = 0; i < g_NativeTextCount; i++)
        {
            if (g_NativeTexts[i].text == text)
            {
                names.insert(g_NativeTexts[i].languages[0]);
            }
        }
    }
    Check(names.size() == 7, "the mouse's keys' names: %zu of 7 different", names.size());

    // Mouse look: a flick saturates, a slow move pushes a little, no motion falls back to the middle within 50 ms
    SetOption(OptionMouseSensitivity, 5);
    SetOption(OptionInvertMouseY, 0);
    MouseLookForceForTest(true);
    f32 x = 0.0f;
    f32 y = 0.0f;
    MouseLookStick(&x, &y);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    MouseLookTestMotion(60.0f, -60.0f);
    MouseLookStick(&x, &y);
    Check(x == -1.0f && y == 1.0f, "mouse look: a flick saturates, the stick the other way to the mouse (%.2f, %.2f)", x, y);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    MouseLookTestMotion(2.0f, 0.0f);
    MouseLookStick(&x, &y);
    Check(x < -0.2f && x > -0.6f, "mouse look: a slow move pushes a little (%.2f)", x);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    MouseLookStick(&x, &y);
    Check(x == 0.0f && y == 0.0f, "mouse look: back in the middle without motion (%.2f, %.2f)", x, y);
    SetOption(OptionInvertMouseY, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    MouseLookTestMotion(0.0f, 60.0f);
    MouseLookStick(&x, &y);
    Check(y == 1.0f, "mouse look: Y inverted (%.2f)", y);
    SetOption(OptionInvertMouseY, 0);
    // Past a sensitivity of 5 a flick pushes on past the stick's full deflection (the game's fastest turn): 2 at 6, 6 at 10
    for (s32 sensitivity : {6, 10})
    {
        SetOption(OptionMouseSensitivity, sensitivity);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        MouseLookTestMotion(600.0f, 0.0f);
        MouseLookStick(&x, &y);
        Check(x == static_cast<f32>(4 - sensitivity), "mouse look: a flick at a sensitivity of %d pushes %.2f", sensitivity, -x);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        MouseLookStick(&x, &y);
    }

    SetOption(OptionMouseSensitivity, 5);
    MouseLookForceForTest(false);
    // The camera speed on the game's turn: 5 the game's own, 10 twice
    f32 yaw = 1.0f;
    f32 pitch = -1.0f;
    ScaleCameraTurn(&yaw, &pitch, 0, 0);
    Check(yaw == 1.0f && pitch == -1.0f, "camera speed 5: the game's turn (%.2f, %.2f)", yaw, pitch);
    SetOption(OptionCameraSpeed, 10);
    ScaleCameraTurn(&yaw, &pitch, 0, 0);
    Check(yaw == 2.0f && pitch == -2.0f, "camera speed 10: twice the game's turn (%.2f, %.2f)", yaw, pitch);
    SetOption(OptionCameraSpeed, 5);

    // Hold any button to skip: what's held as the scene begins is ignored, a fresh press held the hold's time skips, the skip is
    // reported briefly and the button still held is ignored after, a new scene starts after a gap. The times follow the hold's
    // (SkipHoldSeconds, features.cpp)
    SetOption(OptionSkipCutscenes, 1);
    const f32 needed = SkipHoldSecondsForTest();
    // A scene's polls, a frame's apart (1/60 s), from a time to a time with the inputs held: whether the last one skipped, and
    // whether any did
    f32 clock = 0.0f;
    bool any = false;
    auto hold = [&](std::vector<s32> held, f32 until)
    {
        bool skipped = false;
        any = false;
        for (; clock <= until + 0.0001f; clock += 1.0f / 60.0f)
        {
            SkipHoldForTest(true, held, clock);
            skipped = SkipHoldComplete();
            any = any || skipped;
        }

        return skipped;
    };
    constexpr s32 Space = SDL_SCANCODE_SPACE;
    constexpr s32 MouseLeft = 0x10001;
    constexpr s32 PadSouth = 0x20000;
    hold({Space}, needed + 0.5f);
    Check(!any, "skip: held as the scene began, ignored");
    hold({}, needed + 0.6f);
    // The press starts at the next poll: held about half the time
    f32 pressed = clock;
    hold({MouseLeft}, pressed + needed * 0.5f);
    Check(!any && SkipHoldProgress() > 0.25f && SkipHoldProgress() < 0.75f, "skip: a fresh press, the bar half full (%.2f)",
          SkipHoldProgress());
    hold({MouseLeft}, pressed + needed + 0.05f);
    Check(any, "skip: held %.2f s, skipped", needed);
    hold({MouseLeft}, pressed + needed + 0.8f);
    Check(!hold({MouseLeft}, pressed + needed + 0.9f) && SkipHoldProgress() < 0.0f, "skip: still held after, ignored");
    hold({}, clock + 0.1f);
    pressed = clock;
    hold({Space}, pressed + needed * 0.5f);
    Check(!any, "skip: let go of too soon (%.2f s)", needed * 0.5f);
    // A gap: the next scene
    clock += 1.0f;
    hold({}, clock + 0.1f);
    hold({PadSouth}, clock + needed + 0.05f);
    Check(any, "skip: the next scene, a pad button");
    SetOption(OptionSkipCutscenes, 0);
    clock += 1.0f;
    hold({}, clock + 0.1f);
    hold({PadSouth}, clock + needed + 0.5f);
    Check(!any && SkipHoldProgress() < 0.0f, "skip: off, never");
    SetOption(OptionSkipCutscenes, DefaultOption(OptionSkipCutscenes));
    SkipHoldForTest(false, {}, 0.0f);
    Native::Log("ui self-test: the keyboard and mouse scheme, a gamepad stick's square, mouse look and the hold-any-button skip "
                "checked");
}

// The FPS counter: nothing shown with the option off or before its first half second, the frames a second of pictures shown at
// 50, 59.94, 144 and 60 a second, and drawn in the window's own top left corner whatever the window's shape
void TestFpsCounter()
{
    SetOptionSaving(false);
    double at = 100.0;
    auto show = [&at](double rate, double seconds) {
        u32 pictures = static_cast<u32>(std::lround(rate * seconds));
        for (u32 i = 0; i < pictures; i++)
        {
            CountShownFrame(at);
            at += 1.0 / rate;
        }
    };
    SetOption(OptionShowFps, 0);
    show(60.0, 1.0);
    Check(FpsCounterValue() < 0, "the FPS counter off: %d shown", FpsCounterValue());
    SetOption(OptionShowFps, 1);
    show(60.0, 0.4);
    Check(FpsCounterValue() < 0, "the FPS counter's first half second: %d shown", FpsCounterValue());
    const struct
    {
        double rate;
        s32 shown;
    } Rates[] = {{50.0, 50}, {60000.0 / 1001.0, 60}, {144.0, 144}, {60.0, 60}};
    for (const auto& rate : Rates)
    {
        show(rate.rate, 1.5);
        Check(FpsCounterValue() == rate.shown, "the FPS counter at %.2f a second: %d shown", rate.rate, FpsCounterValue());
    }

    // Its box's corner the margin (6 reference pixels, scaled) from the window's, in a 16:9 window and a 4:3 one
    for (const auto& window : {std::pair<int, int>{1920, 1080}, std::pair<int, int>{960, 720}})
    {
        std::vector<OverlayFrame::Command> commands;
        AddFpsCounter(&commands, window.first, window.second);
        f32 scale;
        f32 offsetX;
        f32 offsetY;
        ReferenceMapping(static_cast<f32>(window.first), static_cast<f32>(window.second), &scale, &offsetX, &offsetY);
        auto box = std::find_if(commands.begin(), commands.end(), [](const auto& command) { return command.kind == CommandKind::Box; });
        auto text = std::find_if(commands.begin(), commands.end(), [](const auto& command) { return command.kind == CommandKind::Text; });
        bool placed = box != commands.end() && std::abs(box->x * scale + offsetX - 6.0f * scale) < 0.5f &&
                      std::abs(box->y * scale + offsetY - 6.0f * scale) < 0.5f;
        Check(placed && text != commands.end() && text->text == U"60 FPS", "the FPS counter in a %dx%d window's corner",
              window.first, window.second);
    }

    SetOption(OptionShowFps, 0);
    std::vector<OverlayFrame::Command> none;
    AddFpsCounter(&none, 1280, 720);
    Check(none.empty() && FpsCounterValue() < 0, "the FPS counter off again: nothing drawn");
    Native::Log("ui self-test: the FPS counter checked");
}

// --dump-button-art DIR: the Xbox's glyphs read from the Xbox disc (and the Switch's made from them) as PNGs, for looking at
int DumpButtonArt(const char* folder)
{
    if (!HaveButtonArt())
    {
        Native::Log("button art: no Xbox disc (xbox_disc.txt, local.json's xbox_disc_image or $TWINSANITY_XBOX_ISO)");
        return 1;
    }

    std::error_code created;
    std::filesystem::create_directories(folder, created);
    constexpr const char* Names[ButtonArtLabels] = {"a", "b", "x", "y", "left_shoulder", "right_shoulder", "left_trigger", "right_trigger"};
    for (PromptStyle style : {PromptXbox, PromptSwitch})
    {
        for (u32 label = 0; label < ButtonArtLabels; label++)
        {
            const ButtonArt* art = GetButtonArt(style, static_cast<ButtonArtLabel>(label));
            if (art == nullptr)
            {
                continue;
            }

            std::string path = std::string(folder) + "/" + (style == PromptXbox ? "xbox_" : "switch_") + Names[label] + ".png";
            WritePngFile(path, art->pixels.data(), static_cast<u32>(art->width), static_cast<u32>(art->height));
            Native::Log("button art: %s (%dx%d)", path.c_str(), art->width, art->height);
        }
    }

    return 0;
}

// ------------------------------------------------------------------------------------------------------------------- mods
// --list-files: the archive's files (CRASH6\CRASH.BD's: the levels, the texts...), their paths as the mods folder takes them and
// their sizes. --extract=PATH: one of them, or one of the disc's own (FMV\H01_A.PSS), copied into the settings folder's mods
// folder at its path, to be changed there (native/WINDOWS.md, "Mods"); a file already there is left as it is
int ListFiles()
{
    Archive archive;
    if (!OpenArchive(&archive))
    {
        return 1;
    }

    for (const auto& [upper, name] : archive.names)
    {
        std::string path = name;
        std::replace(path.begin(), path.end(), '\\', '/');
        std::printf("%10u  %s\n", archive.files[upper].second, path.c_str());
    }

    std::printf("%zu files in the archive; the disc's own: FMV/*.PSS (the movies), CRASH6/*.MB and *.MH (the music and the "
                "voices' banks)\n", archive.names.size());
    std::fflush(stdout);
    return 0;
}

int ExtractFile(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/')
    {
        path.erase(path.begin());
    }

    Archive archive;
    if (!OpenArchive(&archive))
    {
        return 1;
    }

    std::string key = path;
    std::replace(key.begin(), key.end(), '/', '\\');
    key = Upper(key);
    u64 offset = 0;
    u32 size = 0;
    std::string name = path;
    auto found = archive.files.find(key);
    if (found != archive.files.end())
    {
        offset = archive.data.offset + found->second.first;
        size = found->second.second;
        name = archive.names[key];
        std::replace(name.begin(), name.end(), '\\', '/');
    }
    else
    {
        Native::Disc::Entry entry;
        if (!Native::Disc::Find(path.c_str(), &entry))
        {
            std::fprintf(stderr, "no %s in the archive or on the disc (--list-files lists them)\n", path.c_str());
            return 1;
        }

        offset = entry.offset;
        size = entry.size;
    }

    const std::filesystem::path out = (std::filesystem::path(Native::SettingsFolder()) / "mods" / std::filesystem::path(name)).make_preferred();
    std::error_code error;
    if (std::filesystem::exists(out, error))
    {
        std::fprintf(stderr, "%s is there already (left as it is)\n", out.string().c_str());
        return 1;
    }

    std::filesystem::create_directories(out.parent_path(), error);
    std::vector<u8> bytes(size);
    if (Native::Disc::Read(offset, bytes.data(), size) != size)
    {
        std::fprintf(stderr, "%s couldn't be read\n", path.c_str());
        return 1;
    }

    std::ofstream file(out, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file)
    {
        std::fprintf(stderr, "%s couldn't be written\n", out.string().c_str());
        return 1;
    }

    std::printf("%s (%u bytes) -> %s\n", name.c_str(), size, out.string().c_str());
    std::fflush(stdout);
    return 0;
}

bool RunCommand(const char* argument, int* status)
{
    std::string_view command(argument);
    if (command == "--list-files")
    {
        *status = ListFiles();
        return true;
    }

    if (command.starts_with("--extract="))
    {
        *status = ExtractFile(std::string(command.substr(std::strlen("--extract="))));
        return true;
    }

    if (command == "--dump-button-art")
    {
        const char* folder = std::getenv("TWIN_UI_ART_OUT");
        *status = DumpButtonArt(folder != nullptr ? folder : "build/native/button-art");
        return true;
    }

    if (command == "--dump-texts")
    {
        *status = DumpTexts();
        return true;
    }

    if (command == "--selftest-ui")
    {
        TestTexts();
        TestSaveFlow();
        TestResolution();
        TestControls();
        TestFpsCounter();
        Native::Log("ui self-test: %s", g_Failures == 0 ? "passed" : "FAILED");
        *status = g_Failures == 0 ? 0 : 1;
        return true;
    }

    return false;
}
}
