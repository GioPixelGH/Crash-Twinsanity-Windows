# Contributing

Thanks for helping. This is a fan port of a PlayStation 2 game, and reports from people playing it are what improves it most.

## Reporting a bug

- Open an issue with the [bug report form](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/new/choose).
- Attach `log.txt` from `%APPDATA%\Crash Twinsanity\` (paste that path in the Explorer's address bar). After a crash or a
  freeze, start the game again and attach `log.previous.txt` too: that's the run that went wrong. The log names your graphics
  card and its driver.
- Say where in the game it happened and what you were doing. A video helps a lot.
- If you can make it happen again, turn on **Options > Gameplay > Detailed log** first.
- Never attach your disc image or the game's files.

## Suggesting a feature

By default the game stays as it was on the PS2. Each suggestion is one of three kinds:

- **A PC option** changes how the game is shown or controlled, or how fast it runs: resolution, frame rate, mouse, an FPS
  counter. It can be on by default.
- **A game option** changes the experience using only the game's own content: skipping cutscenes, the retail bug fixes, a level
  select. It's off by default, and it must never leave a save inconsistent.
- **A mod** adds or changes content: restored cut content, new levels, texture packs. Players install mods themselves (see the
  [players' guide](../native/PLAYING_WINDOWS.md#mods)); the game never ships one.

## Contributing code

Pull requests are welcome.

- Build it as the [README](../README.md#building-on-windows) says, with your own PAL disc image.
- Before opening the pull request, run `.venv\Scripts\python.exe native\tools\check_windows.py`. It needs `local.json` naming
  your disc image (see `local.example.json`). Paste its summary in the pull request.
- Keep Windows-only code apart (`_WIN32`), so the Mac and Linux builds keep working.
- Explain the change in the document of the part you changed (native/WINDOWS.md for Windows), and anything players notice in
  native/PLAYING_WINDOWS.md.
- Never commit the game's files: its executable (`SLES_525.68`), its data, files taken from the disc, or disc images.
- Fixes to the decompilation itself help upstream too:
  [Smartkin's twinsanity-reversed](https://github.com/Smartkin/twinsanity-reversed).
- What you contribute is under this repository's licence ([LICENSE](../LICENSE): MIT for this repository's own changes).

## Code of conduct

Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).
