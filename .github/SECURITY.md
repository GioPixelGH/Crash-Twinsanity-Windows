# Security and safety

## Is the download safe?

- **It's built from this repository's code.** The release zip holds `CrashTwinsanity.exe`, SDL 3 (`SDL3.dll`), FFmpeg's
  MPEG-2 decoder (`avcodec-63.dll`, `avutil-61.dll`), two fonts, the licences and a README. There's no installer, and it
  doesn't need administrator rights.
- **What it does on your PC:** it reads your disc image, and it writes its settings, saves and logs only in
  `%APPDATA%\Crash Twinsanity\`. The `--extract` command also writes to the folder you name. The program has no network code:
  it never connects to the internet.
- **Check your download:** the release page shows the zip's SHA-256 next to the file, and the release notes repeat it. In
  PowerShell, `Get-FileHash Crash-Twinsanity-Windows.zip` must print the same value.
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
