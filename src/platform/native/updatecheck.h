#pragma once

// The update check (THE LIST 9b; native/WINDOWS.md, "The update check"): a PC option, off by default. Turned on, each start of
// the game asks GitHub once, on a thread of its own, for the port's latest release (one HTTPS GET of
// https://api.github.com/repos/GioPixelGH/Crash-Twinsanity-Windows/releases/latest through Windows' WinHTTP: no cookie, no
// personal data, a plain User-Agent), compares its version with this one's, and when it's newer the menus say so and the
// options offer its page, opened in the browser only when the player asks. It never downloads, replaces or runs anything. A
// failure (no network, GitHub not answering) is a log line. $TWIN_UPDATE_TEST=FILE reads FILE as GitHub's answer (the checks:
// no network). Windows only: the option isn't shown elsewhere.

#include "common.h"

#include <string>

namespace Native
{
// This program's version (native/CMakeLists.txt's: entry.cpp)
const char* PortVersion();

// Whether this platform has the check
bool UpdateCheckSupported();
// The check started (the option on, at the start): a thread of its own; nothing when it isn't supported
void StartUpdateCheck();
// A newer release found: its version ("1.3.0") and page (its GitHub release's: https://github.com/GioPixelGH/...); false until then
bool NewerRelease(std::string* version, std::string* page);
// The release's page opened in the browser (only a page of the port's own releases)
void OpenNewerReleasePage();
// Versions compared ("1.2.0" < "1.10.0"): -1, 0, 1
int CompareVersions(const std::string& a, const std::string& b);
}
