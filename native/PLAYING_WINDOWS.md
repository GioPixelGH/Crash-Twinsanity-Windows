# Playing Crash Twinsanity on Windows

This is a native Windows port of Crash Twinsanity, made from a decompilation of the PAL PlayStation 2 game. It contains no
game data, so you need your own copy of the game. The source code, the credits and the bug reports are at
https://github.com/GioPixelGH/Crash-Twinsanity-Windows. On Linux, see LINUX.md there.

## What you need

- 64-bit Windows 10 or 11.
- A graphics card with OpenGL 3.3. Any card from the last ten years has it.
- A disc image of **your own** PAL copy of Crash Twinsanity (Europe/Australia, SLES-52568), as an `.iso`, `.bin` or `.img`.
  Other versions (USA, Japan) don't work.

Nothing else: the folder carries the libraries the game needs.

## First run

1. Unzip `Crash-Twinsanity-Windows.zip` anywhere, and open the `Crash Twinsanity` folder. Keep its files together: the
   program needs the DLLs and the `fonts` folder beside it.
2. Double-click `CrashTwinsanity.exe`. The first time, Windows may warn about an unrecognised app. Choose **More info**,
   then **Run anyway**.
3. The game asks for your disc image. Pick your `.iso`. The game remembers it, and reads the game's data from it at every
   start.

Good to know:

- To pick another disc image, delete `disc_image.txt` from the settings folder (below). You can also start the game with
  `CrashTwinsanity.exe --iso "C:\path\to\game.iso"`.
- If you move or rename the disc image, the game says so and asks for it again.
- If the image isn't the PAL release (another game, or another version of Crash Twinsanity), a message says so and asks
  for another one.

## Where things are

Everything the game keeps is in one folder: **%APPDATA%\Crash Twinsanity\**. Paste that in the Explorer's address bar to
open it. In portable mode (below), it's the **User** folder beside the game instead.

| What | Where |
| --- | --- |
| Saves (the game's memory card) | `memorycard\` |
| The options | `options.txt`, `resolution.txt` |
| Your controls | `bindings.txt` |
| The disc image's path | `disc_image.txt` |
| Texture packs | `textures\SLES-52568\replacements\` (PCSX2's layout: a PCSX2 pack's `replacements` folder works as it is) |
| The log | `log.txt`, and the run before's `log.previous.txt` (when the game wasn't started from a terminal) |
| Mods, their order and their switches (below) | `mods\`, `mods.txt` |
| The graphics' shaders, made at each start so play doesn't stop for them (made again if deleted) | `shaders.txt` |

Saving works like a PC game, with no memory card screens. Save from the pause menu or at the game's save points. Load from
the main menu.

### Portable mode

Portable mode keeps everything in the game's own folder, to carry it on a USB stick or keep it all in one place.

1. Turn on **Options > Gameplay > Portable mode**.
2. The game copies your settings, saves, mods and texture packs into a **`User`** folder beside `CrashTwinsanity.exe`, makes
   a `portable.txt` file there, and closes.
3. Start it again. From then on it uses the `User` folder.

Nothing is moved or deleted. The copy in `%APPDATA%` stays as it was. A `User` folder that was already there is kept as
`User (backup DATE)`. Turning the option off copies the `User` folder back to `%APPDATA%` the same way, keeps the old one as
`Crash Twinsanity (backup DATE)`, and removes `portable.txt`.

- The game's folder must be one you can write to, so not `C:\Program Files`. Otherwise the game says so and stays on
  `%APPDATA%`.
- Your disc image can go in the game's folder too. If the game has no disc image yet, or the one it had is gone, it looks for
  an `.iso`, `.bin` or `.img` in its folder and uses it without asking. It remembers that one by its name, so the folder
  still works after you move it, or on a USB stick that gets another drive letter.
- You can also make an empty `portable.txt` beside `CrashTwinsanity.exe` yourself. If there's no `User` folder yet, the next
  start copies `%APPDATA%`'s data into it first, showing its progress.
- **To update in portable mode**, unzip the new version over the old folder. Or unzip it into a new folder, then move your
  `User` folder, `portable.txt` and your disc image (if it's there) into it. The zip never contains `User` or `portable.txt`.

## Controls

A controller works out of the box: Xbox, PlayStation, Switch and most others. It plays as on the PS2. Its round stick
reaches as far as the PS2 stick's square corners, so Crash runs on diagonals too.

On keyboard and mouse:

| Action | Keys |
| --- | --- |
| Move | W A S D |
| Look and turn the camera | The mouse (or the arrow keys, Q and E) |
| Jump | Space |
| Spin | Left mouse button (or J) |
| Crouch, skid, slide | Left Shift (or K) |
| Status | Tab (or I) |
| Shoulders | Z / C |
| Pause | Esc (or Enter) |
| Skip a cutscene | Hold any button for a second (with Options > Gameplay > Skip cutscenes on) |
| Menus | Arrow keys (or the mouse), Enter to select, Esc to go back |
| Fullscreen | Alt+Enter |

You can rebind everything in **Options > Controls**. The button prompts follow what you used last. Controls also has:

- **Mouse look**, its sensitivity, and inverted Y. Up to a sensitivity of 5 (the default), the mouse turns the camera at most
  as fast as a stick does, as on the PS2. Above 5, a quick move turns it faster, up to six times at 10.
- **Menu hints**: the menus' own hints as the game draws them (the default, with your controller's buttons, and clickable),
  or a bar of prompts at the bottom.

**Camera speed** (Options > Accessibility) makes the camera turn faster or slower, for the mouse, a controller and the keys
alike.

## Options

Open Options from the main menu or the pause menu. The defaults are the game as on the PS2: a 1280×720 window at twice the
PS2's resolution, 60 Hz, cutscenes that can't be skipped, and the original bugs kept. Faster loading and pausing in the
background are on. **Reset to defaults**, at the bottom of each tab, puts it all back.

**Display**

| Option | What it does |
| --- | --- |
| Window size, fullscreen | How the game's window is shown. |
| Resolution | 1x to 8x the PS2's. It's applied when you stop on a choice, not at each step. |
| Widescreen | 4:3, 16:9, 21:9 or 32:9. At 32:9 the menus and the HUD stay in the middle 16:9. |
| Refresh rate | 50 Hz (the European PS2's), 60 Hz (the default), 120 Hz, 144 Hz, or unlimited (turn v-sync off for it). The game runs at the same speed at any rate, and Crash jumps as far and as high as at 60 Hz. Above 60, it goes only as fast as your computer can draw it. |
| Texture filtering, Anisotropic filtering | Smooth or sharp textures. Anisotropic filtering is off by default, as on the PS2. Up to 16x, it sharpens textures seen at an angle. |
| Anti-aliasing | FXAA or SMAA smooth the edges on the finished picture (SMAA is the sharper). SSAA 2x and 4x draw the picture 2 or 4 times larger: the smoothest, and the slowest. |
| Dithering | As on the PS2 (the default): its 16-bit picture with a fine pattern. Or off, for full colour. |
| Draw distance | Short, original (the default, the PS2's), medium, long or unlimited. Farther settings draw the grass and small things before you get close, and the detailed models of trees and statues from farther away. They also draw more: unlimited draws about twice as much on the beach, which is slower on an older PC. Short draws less. |
| Texture packs, scanlines, v-sync | As their names say. |
| Show FPS | A frames per second counter in the top left corner. Off by default. |

**Gameplay**

| Option | What it does |
| --- | --- |
| Text language | English, French, German, Italian or Spanish. Windows' language by default. It applies from the next start. |
| Voice language | The voices and the movies' sound. The same as the texts by default, or another one, from the next start. A translated disc image that keeps its texts in another language's place shows that language's name: Brazilian Portuguese texts in the English slot read "português". |
| Skip cutscenes | Hold any button for a second to skip a cutscene. Off by default, as the PS2 game couldn't. |
| Faster loading | On by default. |
| Bug fixes | Optional fixes of the original game's bugs. Off plays exactly like the PS2. |
| Check for updates | Off by default. When it's on, each start asks GitHub in the background for the latest version. A newer one is shown in the main menu, and Gameplay gets a row that opens its page in your browser. It never downloads or installs anything: you update by downloading the new zip yourself. |
| Credits | Who made the port, and what it's built on. |
| Quit to desktop | Asks first, then closes the game as the window's close button or Alt+F4 does. Progress since your last save is lost. |

The volumes are in **Audio**.

**Game options** (in Gameplay, all off unless you turn them on). Your saves stay ones the original game could make.

- **Infinite lives**: dying costs no life.
- **Stronger Aku Aku**: with one or two masks, TNT and Nitro take a mask instead of a life, as in the earlier Crash games.
- **Slide jump**: the long jump from a slide at any moment of it. Or easier: also just after the slide, and steered in the
  air.
- **Drop shadow**: a round shadow under you while you're in the air, so you see where you'll land.
- **Shadows on crates**: your shadow falls on crates too, from the next level loaded.
- **Speedrun timer**: real time from New Game at the top of the screen, with each level's time as you finish it.
- **Level select**: start any level from the main menu (Options > Gameplay > Level). What you save in such a game is kept
  apart from your own saves, which come back once you're at the title again.

## Mods

Each mod is a folder of its own in **%APPDATA%\Crash Twinsanity\mods\**. It can be:

- a **Mod Crate** from Crate Mod Loader (a folder with `ModCrateInfo.txt`, `layer0` and `layer1`), unzipped as it comes, for
  example `mods\Padoru Pandemonium\`;
- or a folder of changed files laid out as the game's: `mods\My mod\Levels\Earth\Hub\beach.rm2` for a level,
  `mods\My mod\Language\AgentLab\Italian.txt` for texts, `mods\My mod\FMV\H01_A.PSS` for a movie.

**Importing.** You can also put a mod's **zip**, or a **changed disc image**, straight in the mods folder. A changed disc
image is an `.iso` made with the community's tools, or one patched first with an xdelta, PPF or BPS file. Then choose
**Options > Mods > Import mods**:

- a zip is unpacked into a mod folder;
- a disc image is kept only as the files that differ from your disc, so a 4 GB image becomes the few hundred megabytes the
  mod changes.

After that, you can delete the zip or the image. From the command line: `.\CrashTwinsanity.exe --import-mods`.

**Choosing mods.** **Options > Mods** lists the mods. Each one is off, or on in its place: on · 1 is the first. Where two
mods change the same file, the first one wins. A new mod comes first, and on. Mods you just imported are listed at once. The
choices apply from the next start, and are kept in **mods.txt** in the settings folder: a line for each mod, `on` or `off`,
a tab, and the mod's folder (`*` for files put straight in the mods folder).

Files put straight in the mods folder at their path (`mods\Levels\Earth\Hub\beach.rm2`) still work, as before 1.2.0.

**Your saves are safe.** While any mod is on, your saves go on a memory card of their own (`memorycard-mods-...` in the
settings folder). Your own saves are never touched, and they're back when the mods are off.

A mod's changed game program (SLES_525.68) can't work here, because this port has its own. The log says what the mod
changed in it.

**Making a mod.** To get a file to change, open PowerShell or the Command Prompt in the game's folder:

    .\CrashTwinsanity.exe --list-files
    .\CrashTwinsanity.exe --extract=Language/AgentLab/Italian.txt

The first lists the game's files. The second copies one from your disc image into the mods folder, and leaves a file that's
already there alone. The log says which mods the game used. To play the original again, turn a mod off in Options > Mods, or
remove its folder. [MODDING.md](MODDING.md) (MODDING.txt beside the game) is the modding guide: a first mod step by step,
the game's files, the community's tools and the limits.

## If something goes wrong

The game writes what it's doing to `log.txt` in the settings folder. Started from a terminal (`CrashTwinsanity.exe` in
PowerShell or the Command Prompt), it writes there instead.

- A crash writes a report in the log, with the program's addresses.
- So does a freeze: if the game is stuck for 20 seconds, the log says where.
- Each start keeps the last run's log as `log.previous.txt`. After a freeze or a crash, you can start the game again and
  still send it.

Please include the log with any bug report, and say where in the game it happened. The log also names your graphics card
and its driver's version.

What the log has:

- always: the time of each line, and the game's states as they change (loading, playing, a cutscene, paused, a death, game
  over), with where Crash is, the area he's in, the saves written and the movies played;
- at the start: what a bug report would ask you. That's the date, your Windows version (and Wine's, on Linux or a Steam
  Deck), your processor, memory, screen and sound device, the game's options and resolution, the mods and texture pack in
  use, and how the last session ended (closed, crashed or stuck);
- while you play: how long each level took to load, a line a minute with the frame rate and how long the processor and the
  graphics card took for a frame, and a line for each option you change;
- when the game closes: a summary of the session's frame rate, and how it closed.

To report a bug you can repeat, turn on **Options > Gameplay > Detailed log** first. It also writes where Crash is every 5
seconds, every sound played and every file read from the disc image.

**The log is made to be shared.** It writes your user folder as `%USERPROFILE%`, and the game's settings folder as
`%APPDATA%\Crash Twinsanity`, so your user name isn't in its paths. A device named after someone ("Ann's AirPods") loses the
name. The game doesn't write your computer's name, serial numbers or addresses. Folders outside your user folder keep their
names, so have a look before you share it if one has yours.

## Licences

The game's code here is a decompilation for preservation and study. Crash Twinsanity and its content belong to their owners.

The folder carries:

- SDL 3 (zlib licence);
- FFmpeg 9.0.2's libavcodec and libavutil (LGPL 2.1 or later). It's a decode-only build with just the MPEG-2 video decoder,
  from https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz. `avcodec-63.dll` and `avutil-61.dll` are separate files that you can
  replace with your own build of FFmpeg 9.x;
- the fonts Luckiest Guy (Apache 2.0) and Fredoka (SIL Open Font Licence);
- stb_truetype (public domain or MIT);
- SMAA by Jorge Jimenez and others (MIT);
- code from TwinsanityEvolved by menaman123 (MIT: `twinsanityevolved-README.md` says which).

Their licences are in the `licenses` folder, with the port's own: `Crash-Twinsanity-Windows-LICENSE.txt`, MIT for the
Windows port's changes. The decompilation and the Mac port it's built on keep their authors' rights. The icon is the port's
own drawing.
