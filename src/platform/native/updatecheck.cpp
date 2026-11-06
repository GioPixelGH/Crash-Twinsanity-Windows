// The update check: see updatecheck.h
#include "updatecheck.h"

#include "native.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

namespace
{
constexpr const char* Host = "api.github.com";
constexpr const char* Path = "/repos/GioPixelGH/Crash-Twinsanity-Windows/releases/latest";
// The only pages the check opens: the port's own releases'
constexpr const char* Pages = "https://github.com/GioPixelGH/Crash-Twinsanity-Windows/releases/";
// The most of GitHub's answer read (a release's JSON is a few KB; its notes can be longer)
constexpr size_t MostBytes = 1u << 20;

std::mutex g_Lock;
bool g_Found = false;
std::string g_Version;
std::string g_Page;
std::atomic<bool> g_Started{false};

// A JSON string's value by its key at the answer's top level (the first one found: "tag_name", "html_url" come once); "" when
// there's none. The escapes a tag or a URL may have (\/ \" \\) undone
std::string JsonString(const std::string& json, const char* key)
{
    const std::string quoted = std::string("\"") + key + "\"";
    size_t at = json.find(quoted);
    if (at == std::string::npos)
    {
        return {};
    }

    at = json.find(':', at + quoted.size());
    at = at == std::string::npos ? at : json.find('"', at);
    if (at == std::string::npos)
    {
        return {};
    }

    std::string value;
    for (size_t i = at + 1; i < json.size() && value.size() < 512; i++)
    {
        char c = json[i];
        if (c == '"')
        {
            return value;
        }

        if (c == '\\' && i + 1 < json.size())
        {
            c = json[++i];
        }

        value += c;
    }

    return {};
}

// "v1.3.0" -> "1.3.0"; "" unless it's digits and dots
std::string VersionOfTag(std::string tag)
{
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
    {
        tag.erase(0, 1);
    }

    if (tag.empty() || tag.find_first_not_of("0123456789.") != std::string::npos)
    {
        return {};
    }

    return tag;
}

#if defined(_WIN32)
std::string WinHttpError(DWORD error)
{
    switch (error)
    {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return "the name api.github.com not found: no network?";
    case ERROR_WINHTTP_CANNOT_CONNECT:
        return "no connection to GitHub";
    case ERROR_WINHTTP_TIMEOUT:
        return "GitHub didn't answer in time";
    case ERROR_WINHTTP_SECURE_FAILURE:
        return "the secure connection failed";
    default:
        return "WinHTTP's error " + std::to_string(error);
    }
}

// GitHub's answer (its body) and its HTTP status; false with *why when there's none
bool Ask(std::string* body, DWORD* status, std::string* why)
{
    // A plain User-Agent (GitHub's API refuses requests without one): the program's name and version, nothing else
    const std::string agent = std::string("Crash-Twinsanity-Windows/") + Native::PortVersion();
    std::wstring wideAgent(agent.begin(), agent.end());
    HINTERNET session = WinHttpOpen(wideAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr)
    {
        // (Windows before 8.1 has no automatic proxy: the default one)
        session = WinHttpOpen(wideAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }

    if (session == nullptr)
    {
        *why = WinHttpError(GetLastError());
        return false;
    }

    WinHttpSetTimeouts(session, 10000, 10000, 10000, 10000);
    // No cookies, no credentials kept or sent
    DWORD options = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(session, WINHTTP_OPTION_DISABLE_FEATURE, &options, sizeof(options));
    bool ok = false;
    std::wstring host(Host, Host + std::strlen(Host));
    std::wstring path(Path, Path + std::strlen(Path));
    HINTERNET connection = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection != nullptr ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                              : nullptr;
    if (request != nullptr)
    {
        DWORD noLogon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        WinHttpSetOption(request, WINHTTP_OPTION_AUTOLOGON_POLICY, &noLogon, sizeof(noLogon));
        const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
        if (WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr))
        {
            DWORD size = sizeof(*status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, status,
                                &size, WINHTTP_NO_HEADER_INDEX);
            ok = true;
            for (;;)
            {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0 || body->size() >= MostBytes)
                {
                    break;
                }

                std::vector<char> chunk(available);
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0)
                {
                    break;
                }

                body->append(chunk.data(), read);
            }
        }
    }

    if (!ok)
    {
        *why = WinHttpError(GetLastError());
    }

    if (request != nullptr)
    {
        WinHttpCloseHandle(request);
    }

    if (connection != nullptr)
    {
        WinHttpCloseHandle(connection);
    }

    WinHttpCloseHandle(session);
    return ok;
}
#endif

void Check()
{
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    std::string body;
    std::string why;
    unsigned long status = 0;
    const char* test = std::getenv("TWIN_UPDATE_TEST");
    bool answered = false;
    if (test != nullptr && test[0] != '\0')
    {
        // The checks' answer, from a file: no network
        std::ifstream file(test, std::ios::binary);
        body.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        answered = file.good() || !body.empty();
        status = answered ? 200 : 0;
        why = answered ? "" : "$TWIN_UPDATE_TEST's file can't be read";
    }
    else
    {
#if defined(_WIN32)
        DWORD code = 0;
        answered = Ask(&body, &code, &why);
        status = code;
#endif
    }

    if (!answered)
    {
        Native::Log("update check: no answer from GitHub (%s)", why.c_str());
        return;
    }

    if (status != 200)
    {
        Native::Log("update check: GitHub answered %lu: no release read", status);
        return;
    }

    const std::string version = VersionOfTag(JsonString(body, "tag_name"));
    const std::string page = JsonString(body, "html_url");
    if (version.empty())
    {
        Native::Log("update check: GitHub's answer has no release's version");
        return;
    }

    if (Native::CompareVersions(version, Native::PortVersion()) <= 0)
    {
        Native::Log("update check: the latest release is %s, this is %s: nothing newer", version.c_str(), Native::PortVersion());
        return;
    }

    // Only a page of the port's own releases is ever offered
    const bool ours = page.rfind(Pages, 0) == 0 && page.find_first_of(" \"<>\\") == std::string::npos;
    {
        std::lock_guard lock(g_Lock);
        g_Version = version;
        g_Page = ours ? page : std::string(Pages) + "latest";
        g_Found = true;
    }

    Native::Log("update check: %s is out (this is %s): the menus say so, its page is %s", version.c_str(), Native::PortVersion(),
                ours ? page.c_str() : "the releases' latest");
}
}

bool Native::UpdateCheckSupported()
{
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}

void Native::StartUpdateCheck()
{
    if (!UpdateCheckSupported() || g_Started.exchange(true))
    {
        return;
    }

    Log("update check: asking GitHub for the latest release (the option's on)");
    std::thread(Check).detach();
}

bool Native::NewerRelease(std::string* version, std::string* page)
{
    std::lock_guard lock(g_Lock);
    if (!g_Found)
    {
        return false;
    }

    if (version != nullptr)
    {
        *version = g_Version;
    }

    if (page != nullptr)
    {
        *page = g_Page;
    }

    return true;
}

void Native::OpenNewerReleasePage()
{
    std::string page;
    if (!NewerRelease(nullptr, &page) || page.rfind(Pages, 0) != 0)
    {
        return;
    }

    Log("update check: the release's page opened in the browser");
    SDL_OpenURL(page.c_str());
}

int Native::CompareVersions(const std::string& a, const std::string& b)
{
    std::istringstream left(a);
    std::istringstream right(b);
    for (;;)
    {
        std::string x;
        std::string y;
        const bool more = static_cast<bool>(std::getline(left, x, '.')) | static_cast<bool>(std::getline(right, y, '.'));
        if (!more)
        {
            return 0;
        }

        const long p = x.empty() ? 0 : std::strtol(x.c_str(), nullptr, 10);
        const long q = y.empty() ? 0 : std::strtol(y.c_str(), nullptr, 10);
        if (p != q)
        {
            return p < q ? -1 : 1;
        }
    }
}
