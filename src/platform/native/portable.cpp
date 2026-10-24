// The portable install (issue #11; native/WINDOWS.md, "Portable mode"): portable.txt beside the program keeps everything the game
// writes (the settings, the saves, mods, texture packs, the log) in the User folder beside it, laid out as %APPDATA%\Crash
// Twinsanity is, rather than there. A file decides it, as PCSX2's, DuckStation's and Dolphin's portable modes: the folder has to be
// known before the settings are read. Options > Gameplay > Portable mode switches it (ui/settings.cpp): the data is copied from the
// folder in use to the other one, never moved, what the other held set aside in a backup folder beside it ("User (backup
// 2026-10-24)"), then the game closes, so the old folder gets nothing more. A first start with a portable.txt made by hand and no
// User folder copies %APPDATA%'s data there. A program's folder that can't be written to (C:\Program Files) keeps the settings in
// %APPDATA%. Windows only: elsewhere the settings stay where the system keeps them
#include "native.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

#include <cwctype>
#endif

namespace fs = std::filesystem;

namespace
{
std::string Utf8(const fs::path& path)
{
    std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

fs::path FromUtf8(const std::string& text)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

// The program's folder (empty: unknown)
fs::path g_Program;
}

std::string Native::AppDataSettingsFolder()
{
    // %APPDATA%\Crash Twinsanity (the roaming settings, as SDL_GetPrefPath has them), forward slashes like the rest
    const char* appData = std::getenv("APPDATA");
    std::string folder = std::string(appData != nullptr ? appData : ".") + "/Crash Twinsanity";
    std::replace(folder.begin(), folder.end(), '\\', '/');
    return folder;
}

std::string Native::DiscImageFromSetting(const std::string& line)
{
    // A path relative to the program's folder (one kept in portable mode): not "C:x" or "\x", which are the drive's
    fs::path path = FromUtf8(line);
    if (line.empty() || g_Program.empty() || path.has_root_name() || path.has_root_directory())
    {
        return line;
    }

    return Utf8((g_Program / path).lexically_normal());
}

#if defined(_WIN32)
namespace
{
constexpr const wchar_t* MarkerFile = L"portable.txt";
constexpr const wchar_t* UserFolder = L"User";
// The shell's progress dialog and IOleWindow (shlguid.h's and oleidl.h's, here so as not to link libuuid)
constexpr GUID ProgressDialogClass = {0xf8383852, 0xfcd3, 0x11d1, {0xa6, 0xb9, 0x00, 0x60, 0x97, 0xdf, 0x5b, 0xd4}};
constexpr GUID ProgressDialogInterface = {0xebbc7c04, 0x315e, 0x11d2, {0xb6, 0x2f, 0x00, 0x60, 0x97, 0xdf, 0x5b, 0xd4}};
constexpr GUID OleWindowInterface = {0x00000114, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// The mode (it changes once more, when the switch has copied the data: what's written after that goes to the new folder), the User
// folder as the settings' paths are written (the system's code page, forward slashes) and what the start found, logged once the
// log is there
std::atomic<bool> g_Portable{false};
std::string g_UserFolder;
std::vector<std::string> g_StartNotes;

// A copy's progress (the bytes copied of the bytes there are), the folder what the destination held went to and what went wrong
struct Copy
{
    std::atomic<u64> done{0};
    std::atomic<u64> total{0};
    std::atomic<u32> files{0};
    std::mutex lock;
    std::string backup;
    std::string error;
    std::string reason;
};

bool Fail(Copy& copy, const std::string& error, const std::string& reason)
{
    std::lock_guard guard(copy.lock);
    copy.error = error;
    copy.reason = reason;
    return false;
}

// The program's own folder
fs::path ProgramFolder()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0)
        {
            return {};
        }

        if (length < path.size())
        {
            path.resize(length);
            return fs::path(path).parent_path();
        }

        path.resize(path.size() * 2);
    }
}

// The settings' paths are strings in the system's code page (getenv's %APPDATA%, fopen's, std::filesystem's from a string): a
// folder as one, forward slashes; empty when its name has a character the code page hasn't
std::string CodePagePath(const fs::path& path)
{
    const std::wstring& wide = path.native();
    // A UTF-8 code page (Windows' beta option) has every character, and takes no "lost" flag
    BOOL lost = FALSE;
    BOOL* lostFlag = GetACP() == CP_UTF8 ? nullptr : &lost;
    DWORD flags = GetACP() == CP_UTF8 ? 0 : WC_NO_BEST_FIT_CHARS;
    int length = WideCharToMultiByte(CP_ACP, flags, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, lostFlag);
    std::string text(static_cast<size_t>(std::max(length, 0)), '\0');
    if (length <= 0 || WideCharToMultiByte(CP_ACP, flags, wide.c_str(), static_cast<int>(wide.size()), text.data(), length, nullptr,
                                           lostFlag) != length ||
        lost)
    {
        return {};
    }

    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

std::wstring Lowered(std::wstring text)
{
    for (wchar_t& c : text)
    {
        c = static_cast<wchar_t>(std::towlower(c));
    }

    return text;
}

// A path inside a folder (Windows' names: any case, either slash): its part past the folder
bool RelativeInside(const fs::path& path, const fs::path& folder, fs::path* relative)
{
    fs::path inner = fs::path(path).make_preferred().lexically_normal();
    std::wstring outer = fs::path(folder).make_preferred().lexically_normal().native();
    if (!outer.empty() && outer.back() != L'\\')
    {
        outer += L'\\';
    }

    const std::wstring& name = inner.native();
    if (outer.size() < 2 || name.size() <= outer.size() || Lowered(name.substr(0, outer.size())) != Lowered(outer))
    {
        return false;
    }

    *relative = fs::path(name.substr(outer.size()));
    return true;
}

bool SameOrInside(const fs::path& a, const fs::path& b)
{
    fs::path relative;
    return Lowered(fs::path(a).make_preferred().lexically_normal().native()) ==
               Lowered(fs::path(b).make_preferred().lexically_normal().native()) ||
           RelativeInside(a, b, &relative);
}

// Whether the program's folder can hold the User folder: a file made there (and removed), its path one the settings can use
bool FolderUsable(const fs::path& folder, std::string* why)
{
    if (CodePagePath(folder).empty())
    {
        *why = "its path has characters Windows' code page for programs like this one hasn't";
        return false;
    }

    fs::path probe = folder / L".twinsanity-write-test";
    HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        *why = "it can't be written to (" + (error == ERROR_ACCESS_DENIED ? std::string("access denied") : "error " + std::to_string(error)) +
               ")";
        return false;
    }

    CloseHandle(file);
    return true;
}

// The folder what a folder held is set aside in: "User (backup 2026-10-24)", "... 2)" when that's there already
fs::path BackupFolder(const fs::path& folder)
{
    std::tm local{};
    Native::LocalTime(std::time(nullptr), &local);
    char date[16];
    std::strftime(date, sizeof(date), "%Y-%m-%d", &local);
    for (int i = 1; i < 1000; i++)
    {
        fs::path aside = folder;
        aside += std::string(" (backup ") + date + (i > 1 ? " " + std::to_string(i) : std::string()) + ")";
        std::error_code error;
        if (!fs::exists(aside, error) && !error)
        {
            return aside;
        }
    }

    return {};
}

bool CopyOneFile(const fs::path& from, const fs::path& to, Copy& copy)
{
    std::ifstream in(from, std::ios::binary);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!in || !out)
    {
        return Fail(copy, "can't open " + Utf8(!in ? from : to), !in ? "a file couldn't be read." : "a file couldn't be written.");
    }

    std::vector<char> buffer(1 << 20);
    for (;;)
    {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize got = in.gcount();
        if (got <= 0)
        {
            break;
        }

        out.write(buffer.data(), got);
        if (!out)
        {
            return Fail(copy, "can't write " + Utf8(to) + " (is the disk full?)", "a file couldn't be written (is the disk full?).");
        }

        copy.done += static_cast<u64>(got);
    }

    out.close();
    if (in.bad() || out.fail())
    {
        return Fail(copy, (in.bad() ? "can't read " + Utf8(from) : "can't write " + Utf8(to)),
                    in.bad() ? "a file couldn't be read." : "a file couldn't be written (is the disk full?).");
    }

    // Its time kept (the saves' own dates)
    std::error_code error;
    fs::file_time_type time = fs::last_write_time(from, error);
    if (!error)
    {
        fs::last_write_time(to, time, error);
    }

    copy.files++;
    return true;
}

// A folder's files and folders copied into another, made whole beside it ("<to>.copying") then put in its place: what that held
// is set aside in a backup folder beside it (an empty one is removed). A folder that isn't there copies as an empty one. On a failure
// the destination is as it was
bool CopyFolder(const fs::path& from, const fs::path& to, Copy& copy)
{
    if (SameOrInside(to, from) || SameOrInside(from, to))
    {
        return Fail(copy, Utf8(to) + " and " + Utf8(from) + " are one inside the other", "the game's folder is inside the settings' folder.");
    }

    std::vector<fs::path> folders;
    std::vector<fs::path> files;
    u64 total = 0;
    std::error_code error;
    if (fs::is_directory(from, error))
    {
        fs::recursive_directory_iterator entries(from, error);
        for (; !error && entries != fs::recursive_directory_iterator(); entries.increment(error))
        {
            const fs::directory_entry& entry = *entries;
            std::error_code kind;
            fs::path relative = entry.path().lexically_relative(from);
            if (entry.is_directory(kind))
            {
                folders.push_back(relative);
            }
            else if (entry.is_regular_file(kind))
            {
                files.push_back(relative);
                uintmax_t size = entry.file_size(kind);
                total += kind ? 0 : static_cast<u64>(size);
            }
        }

        if (error)
        {
            return Fail(copy, "can't list " + Utf8(from) + ": " + error.message(), "the folder couldn't be read.");
        }
    }

    copy.total = total;
    fs::path staging = to;
    staging += ".copying";
    // A copy left by a run that was closed half way
    fs::remove_all(staging, error);
    fs::create_directories(staging, error);
    if (error)
    {
        return Fail(copy, "can't make " + Utf8(staging) + ": " + error.message(), "a folder couldn't be made.");
    }

    bool copied = true;
    for (const fs::path& folder : folders)
    {
        fs::create_directories(staging / folder, error);
        if (error)
        {
            copied = Fail(copy, "can't make " + Utf8(staging / folder) + ": " + error.message(), "a folder couldn't be made.");
            break;
        }
    }

    for (size_t i = 0; copied && i < files.size(); i++)
    {
        copied = CopyOneFile(from / files[i], staging / files[i], copy);
    }

    std::error_code ignored;
    if (!copied)
    {
        fs::remove_all(staging, ignored);
        return false;
    }

    fs::path aside;
    if (fs::exists(to, error))
    {
        if (fs::is_empty(to, error))
        {
            fs::remove(to, error);
        }
        else
        {
            aside = BackupFolder(to);
            if (aside.empty())
            {
                error = std::make_error_code(std::errc::file_exists);
            }
            else
            {
                fs::rename(to, aside, error);
            }
        }
    }

    if (error)
    {
        fs::remove_all(staging, ignored);
        return Fail(copy, "can't set " + Utf8(to) + " aside: " + error.message() + " (is a file of it open?)",
                    "the old folder couldn't be set aside (is a file of it open?).");
    }

    fs::rename(staging, to, error);
    if (error)
    {
        if (!aside.empty())
        {
            fs::rename(aside, to, ignored);
        }

        fs::remove_all(staging, ignored);
        return Fail(copy, "can't make " + Utf8(to) + ": " + error.message(), "the new folder couldn't be put in place.");
    }

    std::lock_guard guard(copy.lock);
    copy.backup = aside.empty() ? std::string() : Utf8(aside.filename());
    return true;
}

// The shell's progress dialog while a copy runs at start-up (no window yet), as Explorer's copies show. Its window is on a thread of
// its own, but it only closes while the thread that opened it takes its window messages: they're taken while it shows, and after
// it's stopped until it's gone. ole32's and user32's functions are looked up (the program imports only what it has to)
class ProgressDialog
{
public:
    explicit ProgressDialog(const fs::path& to)
    {
        m_Ole = LoadLibraryW(L"ole32.dll");
        m_User = LoadLibraryW(L"user32.dll");
        auto initialise = Function<HRESULT(WINAPI*)(LPVOID, DWORD)>(m_Ole, "CoInitializeEx");
        auto create = Function<HRESULT(WINAPI*)(const GUID&, LPUNKNOWN, DWORD, const GUID&, LPVOID*)>(m_Ole, "CoCreateInstance");
        m_Uninitialise = Function<void(WINAPI*)()>(m_Ole, "CoUninitialize");
        m_Peek = Function<BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT)>(m_User, "PeekMessageW");
        m_Translate = Function<BOOL(WINAPI*)(const MSG*)>(m_User, "TranslateMessage");
        m_Dispatch = Function<LRESULT(WINAPI*)(const MSG*)>(m_User, "DispatchMessageW");
        m_Visible = Function<BOOL(WINAPI*)(HWND)>(m_User, "IsWindowVisible");
        if (initialise == nullptr || create == nullptr || m_Uninitialise == nullptr || m_Peek == nullptr || m_Translate == nullptr ||
            m_Dispatch == nullptr || m_Visible == nullptr)
        {
            return;
        }

        m_Initialised = SUCCEEDED(initialise(nullptr, COINIT_APARTMENTTHREADED));
        void* dialog = nullptr;
        if (create(ProgressDialogClass, nullptr, CLSCTX_INPROC_SERVER, ProgressDialogInterface, &dialog) != S_OK || dialog == nullptr)
        {
            return;
        }

        m_Dialog = static_cast<IProgressDialog*>(dialog);
        // The first line in large type (a short one fits), the second the folder made (shortened to fit); the third is the time left
        m_Dialog->SetTitle(L"Crash Twinsanity: portable mode");
        m_Dialog->SetLine(1, L"Copying your settings and saves", FALSE, nullptr);
        m_Dialog->SetLine(2, to.c_str(), TRUE, nullptr);
        m_Dialog->StartProgressDialog(nullptr, nullptr, PROGDLG_NORMAL | PROGDLG_NOCANCEL | PROGDLG_AUTOTIME | PROGDLG_NOMINIMIZE, nullptr);
    }

    ~ProgressDialog()
    {
        if (m_Dialog != nullptr)
        {
            // Its window (IOleWindow's) waited for till it's hidden, at most 3 s
            HWND window = nullptr;
            IOleWindow* ole = nullptr;
            if (m_Dialog->QueryInterface(OleWindowInterface, reinterpret_cast<void**>(&ole)) == S_OK && ole != nullptr)
            {
                ole->GetWindow(&window);
                ole->Release();
            }

            m_Dialog->StopProgressDialog();
            for (int waited = 0; waited < 300 && (window == nullptr ? waited < 20 : m_Visible(window) != FALSE); waited++)
            {
                Pump();
                Sleep(10);
            }

            m_Dialog->Release();
        }

        if (m_Initialised)
        {
            m_Uninitialise();
        }

        for (HMODULE module : {m_Ole, m_User})
        {
            if (module != nullptr)
            {
                FreeLibrary(module);
            }
        }
    }

    void Show(u64 done, u64 total)
    {
        if (m_Dialog != nullptr)
        {
            m_Dialog->SetProgress64(std::min(done, total), total != 0 ? total : 1);
            Pump();
        }
    }

    // The copy done: shown till the game's window is there
    void Starting()
    {
        if (m_Dialog != nullptr)
        {
            m_Dialog->SetLine(1, L"Done: starting the game", FALSE, nullptr);
            m_Dialog->SetProgress64(1, 1);
            Pump();
        }
    }

private:
    template <typename Pointer>
    static Pointer Function(HMODULE module, const char* name)
    {
        return module != nullptr ? reinterpret_cast<Pointer>(reinterpret_cast<void*>(GetProcAddress(module, name))) : nullptr;
    }

    void Pump()
    {
        MSG message;
        while (m_Peek(&message, nullptr, 0, 0, PM_REMOVE))
        {
            m_Translate(&message);
            m_Dispatch(&message);
        }
    }

    HMODULE m_Ole = nullptr;
    HMODULE m_User = nullptr;
    void(WINAPI* m_Uninitialise)() = nullptr;
    BOOL(WINAPI* m_Peek)(LPMSG, HWND, UINT, UINT, UINT) = nullptr;
    BOOL(WINAPI* m_Translate)(const MSG*) = nullptr;
    LRESULT(WINAPI* m_Dispatch)(const MSG*) = nullptr;
    BOOL(WINAPI* m_Visible)(HWND) = nullptr;
    bool m_Initialised = false;
    IProgressDialog* m_Dialog = nullptr;
};

std::string Megabytes(u64 bytes)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

// A message box (unless headless: the tests'), as the program has no window yet; the log has it on a line
void Tell(bool headless, const std::string& message)
{
    std::string line = message;
    for (size_t at = line.find("\n\n"); at != std::string::npos; at = line.find("\n\n", at))
    {
        line.replace(at, 2, " ");
    }

    g_StartNotes.push_back(line);
    if (!headless)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Crash Twinsanity", message.c_str(), nullptr);
    }
}

// The first start's progress dialog, kept till the game's window is there (EndPortableStart): the window opens while the game is
// the app in front, with no moment of nothing on the screen
ProgressDialog* g_StartDialog = nullptr;

// The switch's copy (a thread of its own) and where it's at
Copy g_Switch;
std::atomic<u32> g_SwitchState{Native::PortableSwitchIdle};
bool g_SwitchTo = false;
}

bool Native::PortableSupported()
{
    return true;
}

bool Native::PortableMode()
{
    return g_Portable;
}

std::string Native::PortableUserFolder()
{
    return g_UserFolder;
}

void Native::StartPortable(bool headless)
{
    g_Program = ProgramFolder();
    std::error_code error;
    if (g_Program.empty() || !fs::exists(g_Program / MarkerFile, error))
    {
        return;
    }

    std::string why;
    if (!FolderUsable(g_Program, &why))
    {
        Tell(headless, "portable.txt is beside the program, but its folder can't be used for portable mode: " + why +
                           ".\n\nThe settings and saves stay in %APPDATA%\\Crash Twinsanity. To play portable, move the game's folder "
                           "somewhere you can write to (not Program Files).\n\n" + Utf8(g_Program));
        return;
    }

    fs::path user = g_Program / UserFolder;
    if (!fs::exists(user, error))
    {
        // A portable.txt made by hand: the first start copies what %APPDATA% has, showing how far it's got
        fs::path from = fs::path(AppDataSettingsFolder()).make_preferred();
        Copy copy;
        bool copied = false;
        std::atomic<bool> finished{false};
        auto started = std::chrono::steady_clock::now();
        std::thread worker([&] {
            copied = CopyFolder(from, user, copy);
            finished = true;
        });
        g_StartDialog = headless ? nullptr : new ProgressDialog(user);
        while (!finished)
        {
            if (g_StartDialog != nullptr)
            {
                g_StartDialog->Show(copy.done, copy.total);
            }

            Sleep(30);
        }

        worker.join();
        char seconds[16];
        std::snprintf(seconds, sizeof(seconds), "%.1f", std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
        if (!copied)
        {
            EndPortableStart();
            Tell(headless, "Portable mode: the settings and saves couldn't be copied from " + Utf8(from) + " to " + Utf8(user) + ": " +
                               copy.error + ".\n\nThey're used from %APPDATA%\\Crash Twinsanity this time; nothing was changed.");
            return;
        }

        if (g_StartDialog != nullptr)
        {
            g_StartDialog->Starting();
        }

        g_StartNotes.push_back("portable mode's first start: " + std::to_string(copy.files.load()) + " files (" +
                               Megabytes(copy.done) + ") copied from %APPDATA%\\Crash Twinsanity to the User folder in " + seconds + " s");
    }

    g_UserFolder = CodePagePath(user);
    g_Portable = true;
}

void Native::EndPortableStart()
{
    delete g_StartDialog;
    g_StartDialog = nullptr;
}

void Native::LogPortableStart()
{
    for (const std::string& note : g_StartNotes)
    {
        Log("%s", note.c_str());
    }

    // (Named, not spelt out: the logs go to bug reports, and %APPDATA%'s path has the user's name)
    Log("settings and saves in %s", g_Portable ? "the User folder beside the program (portable mode: portable.txt is there)"
                                               : "%APPDATA%\\Crash Twinsanity");
}

bool Native::PortableFolderUsable(std::string* why)
{
    return !g_Program.empty() && FolderUsable(g_Program, why);
}

void Native::StartPortableSwitch(bool portable)
{
    if (g_SwitchState == PortableSwitchCopying || g_SwitchState == PortableSwitchDone || g_Program.empty())
    {
        return;
    }

    g_Switch.done = 0;
    g_Switch.total = 0;
    g_Switch.files = 0;
    {
        std::lock_guard guard(g_Switch.lock);
        g_Switch.backup.clear();
        g_Switch.error.clear();
        g_Switch.reason.clear();
    }

    g_SwitchTo = portable;
    g_SwitchState = PortableSwitchCopying;
    std::thread([portable] {
        fs::path user = g_Program / UserFolder;
        fs::path appData = fs::path(AppDataSettingsFolder()).make_preferred();
        fs::path from = portable ? appData : user;
        fs::path to = portable ? user : appData;
        Log("portable mode %s: copying %s to %s", portable ? "on" : "off", Utf8(from).c_str(), Utf8(to).c_str());
        bool done = CopyFolder(from, to, g_Switch);
        std::error_code error;
        fs::path marker = g_Program / MarkerFile;
        if (done && portable)
        {
            // What the file is for, for whoever finds it
            std::ofstream file(marker, std::ios::trunc);
            file << "Crash Twinsanity's portable mode: the settings, saves, mods, texture packs and log are in the User folder beside\n"
                    "the program. To keep them in %APPDATA%\\Crash Twinsanity again, turn Options > Gameplay > Portable mode off: that\n"
                    "copies them back. Deleting this file instead goes back to what %APPDATA% had, without copying anything.\n";
            file.close();
            done = !file.fail() || Fail(g_Switch, "can't write " + Utf8(marker), "portable.txt couldn't be written.");
        }
        else if (done)
        {
            fs::remove(marker, error);
            done = !error || Fail(g_Switch, "can't remove " + Utf8(marker) + ": " + error.message(), "portable.txt couldn't be removed.");
        }

        if (!done)
        {
            std::lock_guard guard(g_Switch.lock);
            Log("portable mode %s: failed, nothing changed: %s", portable ? "on" : "off", g_Switch.error.c_str());
            g_SwitchState = PortableSwitchFailed;
            return;
        }

        // From now on what's written goes to the new folder
        if (portable)
        {
            g_UserFolder = CodePagePath(user);
        }

        g_Portable = portable;
        std::lock_guard guard(g_Switch.lock);
        Log("portable mode %s: %u files (%s) copied to %s%s; portable.txt %s", portable ? "on" : "off", g_Switch.files.load(),
            Megabytes(g_Switch.done).c_str(), Utf8(to).c_str(),
            g_Switch.backup.empty() ? "" : ("; what it held is in \"" + g_Switch.backup + "\" beside it").c_str(),
            portable ? "made" : "removed");
        g_SwitchState = PortableSwitchDone;
    }).detach();
}

Native::PortableSwitch Native::PortableSwitchState(u64* done, u64* total, std::string* backup, std::string* reason)
{
    if (done != nullptr)
    {
        *done = g_Switch.done;
    }

    if (total != nullptr)
    {
        *total = g_Switch.total;
    }

    u32 state = g_SwitchState;
    std::lock_guard guard(g_Switch.lock);
    if (backup != nullptr)
    {
        *backup = g_Switch.backup;
    }

    if (reason != nullptr)
    {
        *reason = g_Switch.reason;
    }

    return static_cast<PortableSwitch>(state);
}

void Native::QuitAfterPortableSwitch()
{
    // As the window's close button does (pads.cpp): the saves and settings are written as they're made
    Log("portable mode %s: the game closes, to start again with the settings in %s", g_SwitchTo ? "on" : "off",
        SettingsFolder().c_str());
    std::_Exit(0);
}

std::string Native::DiscImageSetting(const std::string& image)
{
    // Portable mode: an image inside the program's folder relative to it (the folder can move, a USB stick's drive letter change)
    fs::path relative;
    if (!g_Portable || g_Program.empty() || !RelativeInside(FromUtf8(image), g_Program, &relative))
    {
        return image;
    }

    return Utf8(relative.generic_wstring());
}

std::vector<std::string> Native::DiscImagesBesideProgram()
{
    std::vector<fs::path> found;
    std::error_code error;
    if (!g_Program.empty())
    {
        fs::directory_iterator entries(g_Program, error);
        for (; !error && entries != fs::directory_iterator(); entries.increment(error))
        {
            std::wstring extension = Lowered(entries->path().extension().native());
            std::error_code kind;
            if ((extension == L".iso" || extension == L".bin" || extension == L".img") && entries->is_regular_file(kind))
            {
                found.push_back(entries->path());
            }
        }
    }

    std::sort(found.begin(), found.end());
    std::vector<std::string> images;
    for (const fs::path& path : found)
    {
        images.push_back(Utf8(path));
    }

    return images;
}
#else
// Elsewhere the settings stay where the system keeps them
bool Native::PortableSupported()
{
    return false;
}

bool Native::PortableMode()
{
    return false;
}

std::string Native::PortableUserFolder()
{
    return {};
}

void Native::StartPortable(bool)
{
    const char* base = SDL_GetBasePath();
    g_Program = base != nullptr ? fs::path(base) : fs::path();
}

void Native::EndPortableStart()
{
}

void Native::LogPortableStart()
{
}

bool Native::PortableFolderUsable(std::string* why)
{
    *why = "portable mode is Windows' only";
    return false;
}

void Native::StartPortableSwitch(bool)
{
}

Native::PortableSwitch Native::PortableSwitchState(u64*, u64*, std::string*, std::string*)
{
    return PortableSwitchIdle;
}

void Native::QuitAfterPortableSwitch()
{
    std::_Exit(0);
}

std::string Native::DiscImageSetting(const std::string& image)
{
    return image;
}

std::vector<std::string> Native::DiscImagesBesideProgram()
{
    return {};
}
#endif
