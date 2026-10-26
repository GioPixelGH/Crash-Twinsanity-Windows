#pragma once

// What the native platform layer's parts share: its settings, its log and the game's disc image
#include "common.h"

#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace Native
{
// The settings: from the command line (--iso PATH), else $TWINSANITY_ISO, else local.json's "disc_image" in the working folder
struct Settings
{
    const char* discImage;
    // Print each platform call the native build doesn't do yet (stubs), once
    bool logStubs;
    // No window (--headless): the frames are drawn and kept to be read back, nothing shows (tests)
    bool headless;
};
const Settings& GetSettings();
// Takes the native build's own arguments out of argv (what's left goes to the game's Main)
void ParseArguments(int& argc, char** argv);
// Where the app keeps its settings and saves, and the disc image asked for with the system's file dialog (remembered there)
std::string SettingsFolder();
std::string PickDiscImage();

// The portable install (portable.cpp; Windows only): portable.txt beside the program keeps the settings folder's data in the User
// folder beside it rather than in %APPDATA%\Crash Twinsanity. Decided at start-up before anything reads the settings (headless: no
// message boxes or progress dialog), where a first portable start with no User folder copies %APPDATA%'s data there; what it found
// logged once the log is there
bool PortableSupported();
bool PortableMode();
void StartPortable(bool headless);
void LogPortableStart();
// The first start's progress dialog closed: once the game's window is there, or before a message box or the file dialog shows
void EndPortableStart();
// The settings folders: %APPDATA%\Crash Twinsanity, and portable mode's User folder (forward slashes, the system's code page)
std::string AppDataSettingsFolder();
std::string PortableUserFolder();
// Whether the program's folder can hold portable mode's User folder (written to, its path one the settings can use), and why not
bool PortableFolderUsable(std::string* why);
// Options > Gameplay > Portable mode (ui/settings.cpp): the data copied on a thread of its own from the folder in use to the other
// (what that held set aside in a backup folder beside it), then portable.txt made or removed; where it's at (the bytes copied of
// the bytes there are, the backup folder's name, what went wrong); and the game closed once it's done
enum PortableSwitch : u32
{
    PortableSwitchIdle,
    PortableSwitchCopying,
    PortableSwitchDone,
    PortableSwitchFailed,
};
void StartPortableSwitch(bool portable);
PortableSwitch PortableSwitchState(u64* done, u64* total, std::string* backup, std::string* reason);
[[noreturn]] void QuitAfterPortableSwitch();
// disc_image.txt's line: a remembered disc image as it's kept (portable mode: relative to the program's folder when it's inside
// it), and as it's opened (a relative one is the program's folder's); the disc images in the program's folder (.iso, .bin, .img)
std::string DiscImageSetting(const std::string& image);
std::string DiscImageFromSetting(const std::string& line);
std::vector<std::string> DiscImagesBesideProgram();
// The disc image (--iso, $TWINSANITY_ISO, local.json's, the one picked before, or one picked now) opened and checked to be the
// PAL disc, and the retail executable's data read from its SLES_525.68 (build/native/data.cpp's NativeLoadRetailData: the build
// has none of the game's bytes), once, before NativeApplyDataFixups. A remembered or picked image that's gone or wrong is asked
// for again; a given one that is ends the program with a message
void OpenDiscImage();
// --snapshots DIR: the shown picture saved as a PNG every 2 seconds (snapshots.cpp), checked once a frame
void SetSnapshotFolder(const char* folder);
void SnapshotFrame();
// --quick: straight to playing the beach (quickstart.cpp), checked once a frame
void SetQuickStart(bool quick);
void QuickStartFrame();

// The local time of a moment (localtime_r, localtime_s on Windows)
void LocalTime(std::time_t seconds, std::tm* local);
// An environment variable set (value) or removed (nullptr): setenv and unsetenv, _putenv_s on Windows
void SetEnvironment(const char* name, const char* value);

// The hang watch (Windows: lowmemory_win32.cpp): the game's thread's frames counted (at its wait for the vertical blank), and a
// thread of its own that reports in the log a game thread that finishes no frame for 20 s (its state, where the player is, its
// stack) and says when it goes on
#if defined(_WIN32)
void GameFrameDone();
void StartHangWatch();
#else
inline void GameFrameDone()
{
}
inline void StartHangWatch()
{
}
#endif

// Whether the detailed log is on (Options > Gameplay's, or $TWIN_DETAILED_LOG): more of what the game does logged (ui/trail.cpp)
bool DetailedLog();

// The log (stderr): the platform's messages, prefixed (and the seconds since the start), the profile folder in their paths
// written %USERPROFILE% (WithoutProfile). Fatal ends the session's log (its summary) before its line
void Log(const char* format, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void Fatal(const char* format, ...) __attribute__((format(printf, 1, 2)));
// The seconds since the program started (the log's)
double SessionSeconds();

// The session in the log (sessionlog.cpp), for the bug reports: logs go to public issues, so the paths in it name the profile
// folder %USERPROFILE% (and a folder named as the user or the computer %USERNAME%, %COMPUTERNAME%): text with them replaced. Ready
// once StartLogPrivacy has run (the first thing main does)
void StartLogPrivacy();
std::string WithoutProfile(const std::string& text);
// A device's name as the log gives it: a person's name in it ("Headphones (Ann's AirPods)") left out
std::string DeviceNameForLog(const char* name);
// Where the log goes (entry.cpp's SetUpLog): the settings folder's log.txt (the last session's log is log.previous.txt then), or
// what started the program gave it (a console, a file: the settings folder's log.txt is then the last session's that had one)
void SetLogToFile(bool toFile);
// The start's lines after the version's: the date, Windows (and Wine), the command line, the processor and memory; then, once the
// settings folder is said, the options, the resolution, the threads, the TWIN_ variables, the mods, the texture pack and how the
// last session ended
void LogSessionStart(int argc, char** argv);
void LogSessionSettings();
// A frame of the game's (the wait for the vertical blank, the game's thread; the game's thread's work so far): the frames timed
// while the game is played, a line of performance a minute of play ($TWIN_PERFORMANCE_EVERY seconds)
void SessionFrame(u64 gameWorkNanoseconds);
// The session's end, once whichever way it ends: its summary (its time, its play's frame rate) and the line saying how it ended
// (format: "quit: the window closed", "exit: ..."), with its time and where the game was; EndSessionSummary only the summary
// (Fatal's line follows it). Crashes: the summary written as the crash report is (no locks, no memory)
void EndSession(const char* format, ...) __attribute__((format(printf, 1, 2)));
void EndSessionSummary();
void WriteCrashSessionEnd(void (*write)(const char* text));
#if defined(_WIN32)
// The stack where it's called, written to the log as a crash's is (lowmemory_win32.cpp)
void LogBacktraceHere();
#endif

// The movies' decoder (movie/movie.cpp): FFmpeg's version and its libraries' licences, logged once at start-up
void LogMovieLibraries();

// The disc: an ISO 9660 image read in place (the PS2's paths: "CRASH6\CRASH.BD", any case, / or \, with or without ";1")
namespace Disc
{
struct Entry
{
    u64 offset;
    u32 size;
};
// Opens the image (once); false when it can't be read or isn't one
bool Open(const char* path);
// A file's place in the image, false when there's none
bool Find(const char* path, Entry* entry);
// Reads from the image: how many bytes were read
u32 Read(u64 offset, void* destination, u32 size);
// Closes the image (one that turned out not to be the game's), so another can be opened
void Close();
// Mods (disc.cpp): a file of the archive (CRASH6\CRASH.BD's: "Levels\Earth\Hub\beach.rm2") in the settings folder's mods
// folder at its path: its start (from the archive's) and size, past the image's end; whether an image offset is a mod's (read
// whatever the file it was opened as says its size is)
bool FindArchiveMod(const char* path, u32* start, u32* size);
bool IsMod(u64 offset);
}

// The sound processor's memory (the SPU2's 2 MB), where sound banks are streamed to: the audio side plays from it
constexpr u32 SpuMemorySize = 0x200000;
extern u8 g_SpuMemory[SpuMemorySize];
}
