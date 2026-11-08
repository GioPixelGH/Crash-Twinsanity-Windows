# Security and safety

## Is the download safe?

- **It's built from this repository's code.** The release zip holds `CrashTwinsanity.exe`, SDL 3 (`SDL3.dll`), FFmpeg's
  MPEG-2 decoder (`avcodec-63.dll`, `avutil-61.dll`), two fonts, the licences and a README. There's no installer, and it
  doesn't need administrator rights. The Linux AppImage (since 1.2.0) holds the same program built for Linux, SDL 3 and
  FFmpeg's two libraries, the fonts and the licences; its settings, saves and logs are in `~/.config/crash-twinsanity/`.
- **What it does on your PC:** it reads your disc image, and it writes its settings, saves, logs and imported mods only in
  `%APPDATA%\Crash Twinsanity\` (in portable mode, the `User` folder beside the program; on Linux,
  `~/.config/crash-twinsanity/`). The `--extract` command also writes to the folder you name.
- **Mods are data, never programs:** the game reads a mod's levels, textures, sounds, music and texts in place of the disc's.
  A mod's own executable, `SYSTEM.CNF` and the disc's I/O processor modules are never used, so no code of a mod's is run (a
  crafted file that makes the program run code anyway is a security problem: please report it as below). Importing a mod's
  zip or changed disc image (Options > Mods) only unpacks or compares its files into the mods folder; a file name that would
  lead out of it is skipped.
- **The network:** with its default settings the program never connects to the internet. One option does, and only when you
  turn it on: **Check for updates** (Options > Gameplay, off by default). Turned on, each start makes one HTTPS request, in
  the background, to GitHub's API for this repository's latest release
  (`https://api.github.com/repos/GioPixelGH/Crash-Twinsanity-Windows/releases/latest`), through Windows' own WinHTTP. It sends
  no personal data (no account, no cookies: only the program's name and version as its User-Agent, which GitHub requires) and
  reads only the latest version's number and its page. When that version is newer, the main menu says so and Gameplay gets a
  row that opens the release's page in your browser, only if you choose it. It never downloads, replaces or runs anything;
  if there's no network, it only writes a line in the log. The code is `src/platform/native/updatecheck.cpp`. The Linux
  AppImage has no such option and never connects.
- **Check your download:** the release page shows each file's SHA-256 next to it, and the release notes repeat them. In
  PowerShell, `Get-FileHash Crash-Twinsanity-Windows.zip` must print the same value; on Linux,
  `sha256sum Crash-Twinsanity-Linux-x86_64.AppImage`.
- **Antivirus scans:** you can upload the zip to [VirusTotal](https://www.virustotal.com) yourself. Here is
  [VirusTotal's report on version 1.0.12's download](https://www.virustotal.com/gui/url/6ed9050e25993157d30ef77a69c4b7e7aba61368c9669caca1caf95553cd1e90/detection).
- **"Windows protected your PC":** the program isn't code-signed, because a certificate costs money every year and this is a
  free fan project. Windows SmartScreen warns about new unsigned programs until enough people have downloaded them: choose
  **More info**, then **Run anyway**. If your antivirus flags the program, please open an issue naming the antivirus and what
  it detected. It's most likely a false positive, and we'll look into it.
- **Build it yourself:** the README's "Building on Windows" makes the same program from the source and your own disc image.
  GitHub's servers can't build the releases: every build needs the game's executable from the builder's own disc, which can't
  be uploaded.

## Reporting a vulnerability

If you find a security problem, please report it privately: use **Report a vulnerability** on this repository's
[Security tab](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/security). For example, a crafted disc image, mod or
texture pack that makes the program run code. Please don't open a public issue for it.

Only the latest release gets fixes, in a new release.
