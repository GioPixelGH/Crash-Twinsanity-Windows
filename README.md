<div align="center">

# Crash Twinsanity for Windows

Crash Twinsanity (PlayStation 2, PAL) running natively on 64-bit Windows and Linux, without an emulator.

[![Latest release](https://img.shields.io/github/v/release/GioPixelGH/Crash-Twinsanity-Windows?style=flat-square)](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/GioPixelGH/Crash-Twinsanity-Windows/total?style=flat-square)](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/releases)
[![Discussions](https://img.shields.io/github/discussions/GioPixelGH/Crash-Twinsanity-Windows?style=flat-square)](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/discussions)
[![Licence: MIT for the port's changes](https://img.shields.io/badge/licence-MIT%20%28the%20port%27s%20changes%29-blue?style=flat-square)](#licence)
![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20Linux%20x64-blue?style=flat-square)


This is a Windows port of [HAB3X's Crash Twinsanity for Mac](https://github.com/HAB3X/Crash-Twinsanity-Mac). The Mac port runs
[Smartkin's complete C++ decompilation of the game](https://github.com/Smartkin/twinsanity-reversed) on a native platform layer
(SDL3, an OpenGL renderer for the PS2's graphics, sound, movies, saves and a new options screen). This repository adds what
Windows needs, makes it run at a steady 60 fps, and fixes the crashes found by playing it.

[Download](#download) · [Is it safe?](#is-it-safe) · [Status](#status) · [Known issues](#known-issues) ·
[If something goes wrong](#if-something-goes-wrong) · [What this port adds](#what-this-port-adds) · [Roadmap](#roadmap) ·
[Building](#building-on-windows) · [Credits](#credits)

</div>

![Title](docs/screenshots/title.jpg)

> [!IMPORTANT]
> **No game data is included.** You need your own copy of the PAL game (Europe/Australia, SLES-52568) as a disc image.
> A translated or modded copy of the PAL disc works too; the North American disc doesn't yet (it's on the roadmap).
> Crash Twinsanity and Crash Bandicoot are trademarks of Activision. PlayStation is a trademark of Sony Interactive
> Entertainment. This is a fan project, not affiliated with or endorsed by either. Please don't ask for the game's files or
> disc images in the issues: such requests are closed.

## Download

The [latest release](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/releases/latest) has
`Crash-Twinsanity-Windows.zip` for 64-bit Windows 10 and 11: unzip it anywhere, run `CrashTwinsanity.exe` in the
`Crash Twinsanity` folder and pick your disc image.

### Linux and the Steam Deck

Since 1.2.0 the release also has `Crash-Twinsanity-Linux-x86_64.AppImage`: mark it executable
and run it (64-bit Linux with glibc 2.38 or later: Ubuntu 24.04, Linux Mint 22, Debian 13, Fedora 39, Arch and later ones;
on older ones, such as Ubuntu 22.04, Debian 12 and Linux Mint 21, LINUX.md explains how to build it yourself). How to add it
to a Steam Deck as a non-Steam game is in [native/LINUX.md](native/LINUX.md); it hasn't been tried on a Deck yet, so
**testers are wanted**.

### Requirements

- 64-bit Windows 10 or 11 (or 64-bit Linux, for the AppImage).
- A graphics card with OpenGL 3.3 (any from the last ten years).
- A disc image of **your own** PAL copy of the game (Europe/Australia, SLES-52568), as an `.iso` (or `.bin`/`.img`). A
  translated or modded copy of it works too; the log says which it is (the retail disc, a modified copy or an unknown image,
  by redump.org's dumps).
- A keyboard and mouse, or a controller: Xbox, PlayStation, Switch and most others work out of the box.

![Menu](docs/screenshots/menu.jpg)

## Is it safe?

- The zip (and the Linux AppImage) holds only the program built from this repository's code, the SDL 3 and FFmpeg
  libraries, the fonts and the licences. There's no installer.
- The program connects to the internet for one thing only, and only if you turn it on: **Check for updates** (Options >
  Gameplay, off by default). Then, at each start, it asks GitHub once which version of this port is the latest (one request
  to `api.github.com` for this repository's latest release: no personal data, no account), and the menu tells you when
  there's a newer one. It never downloads, installs or runs anything: you update by downloading the zip yourself. With the
  option off, the program makes no network connection at all. The Linux AppImage has no update check: it never connects.
- The release page shows the files' SHA-256, so you can check your download with `Get-FileHash` (or `sha256sum` on Linux).
  Here is [VirusTotal's report on 1.0.12's download](https://www.virustotal.com/gui/url/6ed9050e25993157d30ef77a69c4b7e7aba61368c9669caca1caf95553cd1e90/detection).
- The program isn't code-signed, so Windows may say "Windows protected your PC" the first time: choose **More info**, then
  **Run anyway**.
- You can also build it yourself (below).

More in [SECURITY.md](.github/SECURITY.md), including how to report a security problem privately.

![autosave](docs/screenshots/autosave.jpg)

## Status

- **Tested on Windows 11** with an NVIDIA GeForce RTX 4070 SUPER, keyboard and mouse, and an Xbox One controller.
- **Played by hand from the start to the credits**, in Italian with an Xbox One controller, on the releases up to 1.0.7.
  Every crash found on the way is fixed (the last ones: Slip Slide Icecapades' mines in 1.0.5, after beating Dingodile in
  1.0.7).
- **All 131 of the game's areas were tried** with scripted input for 30 seconds each, with no unexpected crash
  ([native/sweep/results_windows.md](native/sweep/results_windows.md)). 108 played (7 of them until a game over); the other
  23 can't be started on their own, because they are parts of hubs or tunnels only entered from a neighbouring area.
- **Linux:** an AppImage in each release since 1.2.0, built in Docker on Ubuntu 24.04 and started by the release's checks
  (the beach played from it); it builds and plays on x86-64 (tried on Debian 13). The Steam Deck starts it fullscreen on its
  1280 x 800 screen, but it hasn't been tried on a Deck yet: testers wanted. See [native/LINUX.md](native/LINUX.md).
- **macOS:** the platform code is HAB3X's and the Windows changes are kept apart (`_WIN32`), but this fork isn't built or
  tested on macOS.

![beach](docs/screenshots/beach_play.jpg)

## Known issues

- **The North American disc isn't accepted yet** ([#14]): the game says the disc image isn't the PAL release and asks for
  another. It's on the [roadmap](#roadmap).
- **Older PCs can run well below 60 fps** ([#23], [#39]): the PS2's graphics are emulated on the processor, which costs a lot
  on older ones (a two-core laptop processor from 2011 ran it at 18 fps). A faster renderer, as an option, is on the roadmap.
- **A black screen at start on a few AMD PCs** ([#16]). It doesn't happen on the PC the port is tested on: a log from an
  affected PC helps.
- **Aku Aku disappearing in two cutscenes** ([#18]), by Cavern Catastrophe's excavator and at Dingodile's house: not solved
  yet.
- **The Steam Deck hasn't been tried** ([#34]): testers wanted.

## If something goes wrong

| Problem | What to do |
| --- | --- |
| Windows says "Windows protected your PC" | The program isn't code-signed: choose **More info**, then **Run anyway**. |
| The game asks for the disc image again | It was moved or renamed: pick it again. To use another one, delete `disc_image.txt` from the settings folder (below). |
| The game refuses the disc image | It takes only the PAL disc (SLES-52568), or a translated or modded copy of it. The North American disc isn't accepted yet ([#14]). |
| A black screen at start | Update the graphics card's driver. If it stays black, attach `log.txt` on [#16]. |
| Below 60 fps | In Options > Display set Resolution to 1x, Draw distance to short and Anti-aliasing off. Then attach `log.txt` on [#23]: its performance lines say where each frame's time goes. |
| A pause when a new area appears | The first start after a graphics driver update takes a little longer, while the game prepares its shaders. If the game still pauses while playing, the log's `slow frame` lines say what it waited for: attach `log.txt`. |
| Sounds or a level wrong with mods on | Turn the mods off in Options > Mods: it applies from the next start. Saves made with mods are on a memory card of their own, so yours come back as soon as all mods are off. |

**Before you report a bug:** use the [latest release](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/releases/latest),
search the [issues](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues?q=is%3Aissue) first, and attach `log.txt`.
After a crash or a freeze, start the game again and attach `log.previous.txt` too: that's the run that went wrong. For a bug
you can repeat, turn on **Options > Gameplay > Detailed log** first. Say where in the game it happened, and add a screenshot or
a video if you can. Never attach your disc image or the game's files. Questions and help are welcome in the
[Discussions](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/discussions).

**Where your saves and settings are:** saves, options, controls, mods, texture packs and `log.txt` are all in
`%APPDATA%\Crash Twinsanity\` (paste that in the Explorer's address bar), `~/.config/crash-twinsanity` on Linux, or, with
**Options > Gameplay > Portable mode** on, in the `User` folder beside the program. To uninstall, delete the game's folder and
that one. The [players' guide](native/PLAYING_WINDOWS.md#where-things-are) lists every file.

## What this port adds

On top of HAB3X's port (GPU renderer, widescreen and 21:9, HD texture packs, remappable keyboard,
mouse and controller, saves, cutscene skipping, community fixes):

- **Windows support:**
  - Windows' calling convention, which passes float arguments differently from the PS2's.
  - The retail code's reads of address 0, emulated on Windows.
  - Frame pacing and file access.
  - A crash report with a backtrace and the area the player was in.
- **Speed:** a steady 60 fps everywhere tested. The game, the PS2's vector unit (VU1) and the graphics now run on three
  threads, and the renderer's hot paths are faster. The graphics' shader programs are made when the game starts, so
  reaching a new area doesn't pause for them.
- **Frame rates:** 50, 60, 120 and 144 Hz, or unlimited, at the same game speed.
- **Anti-aliasing:** FXAA, SMAA ([iryoku's](https://github.com/iryoku/smaa): smoother edges than FXAA), and SSAA 2x and 4x
  (Options > Display > Anti-aliasing), which draw the picture 2 or 4 times larger, up to 8x the PS2's, and shrink it to the
  window: the smoothest edges, more work for the graphics card.
- **Anisotropic filtering** (off by default, 2x to 16x): textures seen at an angle stay sharper.
- **32:9** for super-ultrawide screens, besides 4:3, 16:9 and 21:9, with the menus and the HUD in the middle 16:9.
- **Dithering option:** as the PS2 (its 16-bit picture with a fine pattern, the default) or off, for the picture's full
  colour.
- **Resolution:** 1x to 8x the PS2's (6x and 8x added); a buffer the graphics card can't hold at the chosen scale is drawn
  by the software renderer instead of failing.
- **Draw distance option** (from HAB3X's Mac port): short, original (the default), medium, long or unlimited; the farther
  settings draw the grass and small things before you get close, at a cost on older PCs.
- **Text language and voice language:** two options (English, French, German, Italian or Spanish each), so English texts
  with French voices, say; a translated disc's texts are recognised by their language ([#25]).
- **Game options** (Options > Gameplay, all off by default, so the game stays the original): infinite lives, a stronger Aku
  Aku that takes TNT and Nitro blasts ([#30]), the slide's long jump at any moment or easier ([#11], [#35]), level select
  (from HAB3X's Mac port, on a memory card of its own), a speedrun timer, a drop shadow under you in the air, and your shadow
  on crates.
- **Cutscene skip for many more scenes:** besides the developers' own skips, those they never wrote are skipped by ending the
  scene's waits in order (TwinsanityEvolved's skip, with its author's permission), each one checked to end as when watched
  ([#28]).
- **Quit to desktop, a Credits page, and an update check** (Options > Gameplay; the update check is Windows only, off by
  default, and only tells you a newer version is out).
- **Mods:** each mod a folder in `%APPDATA%\Crash Twinsanity\mods` (a Crate Mod Loader Mod Crate as it comes, or files laid
  out as the disc's); a mod's zip or changed disc image imported from Options > Mods as only the files it changes; the Mods
  page orders them and turns each on or off; their saves on a memory card of their own. Files put straight in the mods
  folder replace the disc's as before. [native/MODDING.md](native/MODDING.md) is the modding guide.
- **Portable mode** (Options > Gameplay, off by default): settings, saves and mods in a `User` folder beside the game, and the
  disc image found in the game's folder, to carry it on a USB stick.
- **Fixes:**
  - The crash entering the first boss's area (a level title picture read wrongly).
  - The crash during the Mecha-Bandicoot fight (a float argument on Windows).
  - Crashes in the caverns (a reference to an object already freed, an animation of zero length).
  - VU1 programs that ran away (two copies of the same data declared with different sizes).
  - The characters drawn as on the PS2 at high resolutions: the game's distance haze and the first boss's sky at the chosen
    resolution (no more stepped outlines), the colours VU1 writes after sending its vertices (no more patches on the
    bodies), triangles thinner than a PS2 pixel (no more thin black lines), textures' detail for the chosen resolution.
  - Ant Agony's metal doors, walls and floors, read from an old buffer of the first boss's area ([#32]).
  - Under the Bug fixes option (off by default), three bugs of the PS2 game itself: the masks' invincibility particles lost
    in the next area and two masks' stars lost after a cutscene ([#18]), and Bandicoot Pursuit's Evil Crash standing still.
- **Controllers:** a modern controller's round stick is mapped to the PS2 stick's square, so Crash runs on diagonals too.
- **The game's data from your disc:** the program carries none of the game's data. It reads the data from your disc image's
  executable when it starts, checks that it's the PAL release, and says so clearly when the disc image is wrong. The log
  says what the image is: the retail disc, a modified copy or an unknown image, by redump.org's dumps.

[native/WINDOWS.md](native/WINDOWS.md) explains each change, with the reasons and how they were tested.
[native/PLAYING_WINDOWS.md](native/PLAYING_WINDOWS.md) is the player's guide: options, controls, saves, mods.
[native/MODDING.md](native/MODDING.md) is the modding guide: what the port reads, the game's files, the community's tools,
the limits and how to share a mod.

![cortexdimension](docs/screenshots/cortexdimension.jpg)

## Roadmap

The game stays the original by default: new game options are off until you turn them on.

Done:

- [x] The characters drawn as on the PS2 at high resolutions ([#15], [#19], [#24]): no more stepped outlines, patches on
  the bodies or thin black lines.
- [x] 6x and 8x the PS2's resolution in the options ([#19]), and a safe fallback when the graphics card refuses one of the
  renderer's buffers.
- [x] A Dithering option ([#24]): the PS2 game shows its picture in 16-bit colour with a fine pattern to hide the steps, and
  the port reproduces it; turned off, the picture would keep its full colour.
- [x] Draw distance as a PC option (from HAB3X's Mac port): farther than the PS2's draws the grass and small things before
  you get close, at a cost on older PCs.
- [x] Anti-aliasing by supersampling (2x, 4x).
- [x] A Credits page in the PC options, with everyone behind the Windows version.
- [x] Quit to desktop in the options ([#33]).
- [x] The texts' language and the voices' language as two options, with translated disc images recognised ([#25]).
- [x] SMAA anti-aliasing, anisotropic filtering, and a 32:9 shape with the menus in the middle 16:9.
- [x] The resolution applied when you stop on it, not at each step through the choices ([#39]).
- [x] An update check, off unless you turn it on: it only tells you a newer version is out.
- [x] Infinite lives, a stronger Aku Aku ([#30]) and a speedrun timer, as game options.
- [x] The slide's long jump at any moment, or easier, as a game option ([#11], [#35]).
- [x] A drop shadow under you in the air, and your shadow on crates, as game options ([#11], [#35]).
- [x] Level select, as a game option (its saves kept apart from yours).
- [x] Mods as the community ships them ([#36]): sound effects right with a mod's levels, Mod Crates used as they come (new
  levels too), a mod's zip or changed disc image imported as only what it changes, a Mods page with an order and a switch
  for each, and their saves kept apart from yours.
- [x] A modding guide ([native/MODDING.md](native/MODDING.md)): the game's files, the community's tools for them, and how to
  share a mod.
- [x] The two cutscenes that ended a little differently when skipped than when played ([#9]): Classroom Chaos' start now ends as
  when played, and so does the bear's.
- [x] A skip for many cutscenes that had none ([#28]), like one of Cavern Catastrophe's, the Tiki Mon's, the party before the
  first fight with Cortex and Henchmania's fight: their waits ended in order, as TwinsanityEvolved does it, each checked to
  end as when played.
- [x] The masks' invincibility particles kept when you move into the next area, a fix under the bug fixes option ([#18]).
- [x] With two masks, Aku Aku's stars back after a cutscene, a fix under the bug fixes option ([#18]).
- [ ] Aku Aku disappearing in two cutscenes, by Cavern Catastrophe's excavator and at Dingodile's house ([#18]): not solved
  yet, to compare with the original game.
- [x] Bandicoot Pursuit's Evil Crash following his route instead of standing still, a fix under the bug fixes option (found by
  TwinsanityEvolved).
- [x] Ant Agony's metal doors, walls and floors no longer streaked or blocky ([#32]).
- [x] Fewer pauses when you reach a new area ([#23]): the graphics' shaders are made when the game starts instead of the
  first time something needs them.
- [x] A log that says what your disc image is, where the renderer's time goes, and what a slow frame was doing.
- [x] A Linux download ([#34]): an AppImage beside the Windows zip, and the Steam Deck's defaults (fullscreen on its screen,
  the Xbox's button prompts) with the steps to add it as a non-Steam game. Steam Deck testers wanted.

Next, roughly from the quickest job to the longest (plans, not promises):

- [ ] A 16:10 shape for the Steam Deck's screen (the picture is 16:9 there now, with thin bars above and below).
- [ ] The game closing after the end credits instead of going back to the Iceberg Lab, as reported in [#20].
- [ ] Eight save slots.
- [ ] A skip for the cutscenes that still play whole, like the first fight with Cortex's start and the final boss's ([#28]).
- [ ] Frame rates other than 60 checked through the game: the seal chase at 240 fps ([#17]), the cannon's bombs below 60 fps
  ([#12]).
- [ ] The black screen at start on some AMD PCs ([#16]).
- [ ] HD texture packs in BC7, and texture upscaling.
- [ ] The characters' shadows drawn at the chosen resolution: they stay at the PS2's, softer than the rest of the picture at
  high resolutions.
- [ ] The North American disc, NTSC-U v2.00 ([#14]): aimed at the next major version.
- [ ] A faster renderer as an option for weaker PCs ([#23], [#37], [#39]): the game's models and scenes drawn straight on
  the graphics card instead of emulating the PS2's graphics chip a shape at a time; aimed at the next major version.
- [ ] Achievements in the game, kept on your PC (RetroAchievements doesn't award them for ports).
- [ ] Subtitles for the cutscenes ([#11]).

Maybe later: fast-forwarding cutscenes as well as skipping them ([#9]), two-player co-op, some of the NTSC-U release's
differences as options on the PAL disc ([#14]; playing the North American disc itself is on the roadmap above), progress
kept when you go back to an area: crates broken, cutscenes seen, challenges done ([#38]), the port's own texts in Brazilian
Portuguese if someone translates them ([#25]), and character packs from the other Crash games (a mod: it needs those games'
discs).

[#9]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/9
[#11]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/11
[#12]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/12
[#14]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/14
[#15]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/15
[#16]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/16
[#17]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/17
[#18]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/18
[#19]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/19
[#20]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/20
[#23]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/23
[#24]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/24
[#25]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/25
[#28]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/28
[#30]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/30
[#32]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/32
[#33]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/33
[#34]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/34
[#35]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/35
[#36]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/36
[#37]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/37
[#38]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/38
[#39]: https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/39

## Building on Windows

You need about 3 GB of disk space, your PAL disc image and an internet connection for the first build.

1. **Install the tools** (PowerShell):

   ```powershell
   winget install MartinStorsjo.LLVM-MinGW.UCRT Kitware.CMake Ninja-build.Ninja Python.Python.3.13 Git.Git
   ```

2. **Get the PS2 toolchain**: download `ps2dev-windows-latest.tar.gz` from
   [ps2dev's latest release](https://github.com/ps2dev/ps2dev/releases/tag/latest) and extract it into `C:\`, which makes `C:\ps2dev`.
   The native build uses its headers to lay out the game's data.

   ```powershell
   tar -xzf "$HOME\Downloads\ps2dev-windows-latest.tar.gz" -C C:\
   ```

3. **Clone this repository and make the Python environment** (in a new PowerShell, so the tools above are found):

   ```powershell
   git clone https://github.com/GioPixelGH/Crash-Twinsanity-Windows.git
   cd Crash-Twinsanity-Windows
   py -3 -m venv .venv
   .venv\Scripts\python.exe -m pip install -r requirements.txt
   .venv\Scripts\python.exe tools\toolchain_dlls.py
   ```

4. **Take the game's executable out of your disc image, check it, and split it**:

   ```powershell
   tar -xf "C:\path\to\Crash Twinsanity (Europe).iso" SLES_525.68
   Get-FileHash -Algorithm SHA1 SLES_525.68    # D41B8D53F733F930CF03D8EA944BEC24429FE57C
   .venv\Scripts\python.exe tools\split.py
   ```

5. **Build** (the first time it also downloads SDL3 and builds a small FFmpeg; Git for Windows' Git Bash is used for that):

   ```powershell
   .venv\Scripts\python.exe native\tools\build_windows.py
   ```

6. **Play**:

   ```powershell
   build\native\cmake\CrashTwinsanity.exe
   ```

   The first time, it asks for your disc image and remembers it. Settings, saves and texture packs are in
   `%APPDATA%\Crash Twinsanity\`.

More on the Windows build: [native/WINDOWS.md](native/WINDOWS.md). The decompilation itself (the PS2 build that matches the
retail executable, and its tools): [README_DECOMP.md](README_DECOMP.md) and [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).
HAB3X's native layer: [native/NATIVE.md](native/NATIVE.md), [native/GRAPHICS.md](native/GRAPHICS.md),
[native/AUDIO.md](native/AUDIO.md).

![dungeon](docs/screenshots/dungeon.jpg)

## Texture packs

PCSX2's texture packs for this game work as they are. Put the pack's `SLES-52568` folder into
`%APPDATA%\Crash Twinsanity\textures\` and turn texture packs on in Options. The HD texture pack by CRASHARKI that HAB3X's
repository shipped isn't included here: get it from its author.

## Credits

**Upstream projects (most of the work):**

- [Smartkin](https://github.com/Smartkin/twinsanity-reversed) and everyone behind the Crash Twinsanity decompilation: every
  function of the game rewritten in C++, with the tools that build and test it.
- [HAB3X](https://github.com/HAB3X/Crash-Twinsanity-Mac): the native port for Mac. This includes the platform layer, the
  emulation of the PS2's graphics (GS, VIF, VU) and its OpenGL renderer, audio and movies, saves, the options screen and the
  community fixes.
- [Crash Twinsanity Improved](https://github.com/AlexMollard/Crash-Twinsanity-Improved) by AlexMollard: the Mac port's
  optional fixes come from this mod. Its 21:9 mode is TechieSaru's fix, from the mod's PCSX2 patches.
- CRASHARKI, for the HD texture pack the Mac port was tested with.
- [SMAA](https://github.com/iryoku/smaa) by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego
  Gutierrez (MIT): the SMAA anti-aliasing choice.
- [TwinsanityEvolved](https://github.com/menaman123/TwinsanityEvolved) by menaman123, another PC port: code under its MIT
  licence (the disc image's CRC-32 and SHA-1, and how a text bank's language is told, from its engine NativeEE), its notices kept;
  and, with menaman123's permission ([TwinsanityEvolved#19](https://github.com/menaman123/TwinsanityEvolved/issues/19)), its
  cutscene skip and its census of the game's cutscenes, under its MIT licence;
  [`native/third_party/twinsanityevolved`](native/third_party/twinsanityevolved/README.md) lists what.

**Contributors and testers of the Windows port:**

- [Gabriel-TwoBits](https://github.com/Gabriel-TwoBits): the Linux build, with its fixes and low memory emulation, named
  backtraces, and the build script with its AppImage (#1, #2, #3).
- The players whose reports and tests made it better: [NitorikoSkye](https://github.com/NitorikoSkye) (the ROG Ally and AMD
  graphics, the physics past 60 Hz, many suggestions), [SSStylishh](https://github.com/SSStylishh) (played it to the end),
  [APerson7778](https://github.com/APerson7778) (mouse look) and [VenezuelanHell](https://github.com/VenezuelanHell) (older
  AMD drivers).
- [Onaga00](https://github.com/Onaga00), with thanks.

Bug reports, suggestions and pull requests are welcome: see [CONTRIBUTING.md](.github/CONTRIBUTING.md).

**References:**

- [PCSX2](https://github.com/PCSX2/pcsx2): how the PS2's VU, VIF and GS behave, the model of its float maths, and the design
  of its hardware renderers.
- Microsoft's documentation of the x64 calling convention, and the System V and MIPS EABI conventions.

**Tools:**

- [splat](https://github.com/ethteck/splat), [spimdisasm](https://github.com/Decompollaborate/spimdisasm) and
  [rabbitizer](https://github.com/Decompollaborate/rabbitizer): split the executable.
- [ps2dev](https://github.com/ps2dev/ps2dev): the PS2 toolchain, GCC 15.2 with PS2SDK.
- Ghidra: used upstream for the decompilation's symbols.
- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) by Martin Storsjö (clang 23, UCRT), [CMake](https://cmake.org/) and
  [Ninja](https://ninja-build.org/): build the Windows program.
- [Python](https://www.python.org/) 3.13 and [Pillow](https://python-pillow.org/): build tools and test scripts.
- [Git for Windows](https://gitforwindows.org/), MSYS2's `make` and [winget](https://github.com/microsoft/winget-cli): build
  FFmpeg and install the tools.

**Libraries and fonts bundled with the program:**

| What | Licence |
| --- | --- |
| [SDL 3.4.18](https://github.com/libsdl-org/SDL) | zlib |
| [FFmpeg 9.0.2](https://ffmpeg.org/) (libavcodec and libavutil, built with only the MPEG-2 video decoder) | LGPL 2.1 or later ([native/third_party/ffmpeg](native/third_party/ffmpeg)) |
| [stb_truetype](https://github.com/nothings/stb) (one fix of ours, marked in the file) | public domain or MIT |
| [Luckiest Guy](https://fonts.google.com/specimen/Luckiest+Guy) | Apache 2.0 |
| [Fredoka](https://fonts.google.com/specimen/Fredoka) | SIL Open Font Licence 1.1 |

[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) says where each licence's text is, SMAA's and TwinsanityEvolved's too.

**The icon** is our own drawing (`native/tools/draw_icon.py` draws it), under this repository's licence.

**AI assistance:** this port was written with the help of [Claude Code](https://claude.com/claude-code) (Anthropic). As the
developer, I reviewed and supervised the work. Each commit made with it says so.

## Licence

- **Upstream code:** Smartkin's decompilation and HAB3X's Mac port are published without a licence: all their rights stay
  with their authors. They are included here, unchanged in the first commit, with the credit above.
- **This repository's own changes** (the commits after the first) are under the MIT licence: see [LICENSE](LICENSE).
- **Third-party libraries and fonts** keep their own licences (table above).
- **The game** (its code, data, art and sound) belongs to Activision. None of the game's files are in this repository:
  the executable, data, art and sound come from your own disc image.

## Gallery
| | |
| --- | --- |
| ![N. Sanity Beach](docs/screenshots/beach.jpg) | ![High Seas Hi-Jinks](docs/screenshots/ice.jpg) |
| ![A cutscene with the villains](docs/screenshots/villains.jpg) | ![Twinsanity Island](docs/screenshots/twinsanity.jpg) |
| ![The pause menu, with an Xbox controller's buttons](docs/screenshots/pause_menu.jpg) | ![The PC options](docs/screenshots/options.jpg) |

*Taken from the Windows program at 4× the PS2's resolution, without texture packs. The menus show the
buttons of the controller in use, and the PC options have their own screen.*
