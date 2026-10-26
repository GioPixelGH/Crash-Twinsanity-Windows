# Playing Crash Twinsanity on Windows

This is a native Windows port of Crash Twinsanity, made from a decompilation of the PAL PlayStation 2 game. It contains no game
data: you need your own copy of the game. Its source code, credits and bug reports:
https://github.com/GioPixelGH/Crash-Twinsanity-Windows

## What you need

- 64 bit Windows 10 or 11, and a graphics card with OpenGL 3.3 (any from the last ten years).
- A disc image of **your own** PAL copy of Crash Twinsanity (Europe/Australia, SLES-52568), as an `.iso` (or `.bin`/`.img`).
  Other versions (USA, Japan) aren't supported.

Nothing else: the folder carries the libraries the game needs.

## First run

1. Unzip `Crash-Twinsanity-Windows.zip` anywhere and open the `Crash Twinsanity` folder. Keep the files together: the program
   needs the DLLs and the `fonts` folder beside it.
2. Double-click `CrashTwinsanity.exe`. Windows may warn about an unrecognised app the first time: choose **More info**, then
   **Run anyway**.
3. On the first launch the game asks for your disc image with a file dialog. Pick your `.iso`. It remembers it; to pick another,
   delete `disc_image.txt` from the settings folder (below), or start it with `CrashTwinsanity.exe --iso "C:\path\to\game.iso"`.
   The game's data is read from it every time the game starts. If you move or rename the disc image, the game says it isn't
   where it was and asks for it again. If it isn't the PAL release (another game, another version of Crash Twinsanity, or a
   changed copy), a message says so and asks for another.

## Where things are

Everything the game keeps is in **%APPDATA%\Crash Twinsanity\** (paste that in the Explorer's address bar), or in portable mode
in the **User** folder beside the game (below):

| What | Where |
| --- | --- |
| Saves (the game's memory card) | `memorycard\` |
| The options | `options.txt`, `resolution.txt` |
| Your controls | `bindings.txt` |
| The disc image's path | `disc_image.txt` |
| Texture packs | `textures\SLES-52568\replacements\` (PCSX2's layout: a PCSX2 pack's `replacements` folder works as it is) |
| The log | `log.txt` (when the game wasn't started from a terminal), the run before's `log.previous.txt` |
| Mods (files read instead of the game's: below) | `mods\` |

Saving works like a PC game: no memory card screens. Save from the pause menu or at the game's save points; load from the
main menu.

### Portable mode

To keep everything in the game's own folder instead (to carry it on a USB stick, or keep it all in one place), turn on
**Options > Gameplay > Portable mode**. The game copies your settings, saves, mods and texture packs from
`%APPDATA%\Crash Twinsanity\` into a **`User`** folder beside `CrashTwinsanity.exe` (same files as in the table above), makes a
`portable.txt` file there, and closes: start it again and it uses the `User` folder from then on. Nothing is moved or deleted:
the copy in `%APPDATA%` stays as it was, and if a `User` folder was already there it's kept as `User (backup DATE)`.
Turning the option off copies the `User` folder back to `%APPDATA%` the same way (the old `%APPDATA%` folder is kept as
`Crash Twinsanity (backup DATE)`) and removes `portable.txt`.

- The game's folder must be one you can write to: not `C:\Program Files`. If it isn't, the game says so and stays on
  `%APPDATA%`.
- Your disc image can go in the game's folder too: in portable mode, if the game has no disc image yet (or the one it had
  isn't there any more), it looks for an `.iso`, `.bin` or `.img` in its folder and uses it without asking. One inside the
  folder is remembered by its name, so the folder still works after moving it or on a USB stick that gets another drive letter.
- You can also make an empty `portable.txt` beside `CrashTwinsanity.exe` yourself: if there's no `User` folder yet, the next
  start copies `%APPDATA%`'s data into it first, showing its progress.
- **Updating the game in portable mode**: unzip the new version over the old folder, or into a new folder and then move your
  `User` folder, `portable.txt` (and your disc image, if it's there) into it. Everything else is the game's and can be
  replaced; `User` and `portable.txt` are never in the zip.

## Controls

A controller (Xbox, PlayStation, Switch and most others) works out of the box, as on the PS2 (its round stick's diagonals reach as far as the PS2 stick's square corners, so Crash runs on a diagonal too). Keyboard and mouse:

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

Everything can be rebound in **Options > Controls**; the button prompts follow what you last used. Mouse look, its sensitivity
and inverted Y are there too, and **Menu hints**: the menus' own hints as the game draws them (the default, their buttons the
controller's, and clickable), or a bar of prompts at the bottom. Up to a sensitivity of 5 (the default) the mouse turns the
camera at most as fast as a stick does, as the PS2 did; above 5 a quick move turns it faster (up to six times at 10). **Camera
speed** (Options > Accessibility) makes the camera turn faster or slower for the mouse, a controller and the keys alike.

## Options

Options (from the main or pause menu) has PC settings: the language (English, French, German, Italian or Spanish texts, voices
and movies; Windows' language by default, the new one from the game's next start), window size or fullscreen, the internal
resolution, widescreen (4:3, 16:9 or 21:9), the refresh rate, texture filtering, scanlines, texture packs, anti-aliasing
(FXAA), v-sync, a frames per second counter (**Show FPS**, off by default: in the top left corner), volumes, skipping cutscenes,
faster loading, and optional fixes of the original game's bugs (off plays exactly like the PS2).

The defaults are the game as the PS2 had it, in a 1280×720 window at twice the PS2's resolution and 60 Hz: cutscenes can't be
skipped (the PS2 game couldn't; turn **Skip cutscenes** on to hold any button through them), the original bugs are kept, and
faster loading and pausing in the background are on. **Reset to defaults** at the bottom of each tab puts it back to these.

The refresh rate is 50 Hz (the European PS2's), 60 Hz (the default), or 120 Hz, 144 Hz or unlimited for smoother play on a fast
computer and a high refresh rate monitor (turn v-sync off for unlimited). The game runs at the same speed at any rate, and Crash
jumps as far and as high as at 60 Hz; above 60 it goes only as fast as the computer can draw it.

## Mods

A changed copy of one of the game's files in **%APPDATA%\Crash Twinsanity\mods\** at its path is read instead of it:
`mods\Language\AgentLab\Italian.txt` for texts, `mods\Levels\Earth\Hub\beach.rm2` for a level, `mods\FMV\H01_A.PSS` for a
movie. To get a file to change, from PowerShell or the Command Prompt in the game's folder:

    .\CrashTwinsanity.exe --list-files
    .\CrashTwinsanity.exe --extract=Language/AgentLab/Italian.txt

The first lists the game's files; the second copies one from your disc image into the mods folder (a file already there is
left alone). The log says which mods the game used. Remove the folder (or the file) to play the original again.

## If something goes wrong

The game writes what it's doing to `log.txt` in the settings folder, or to the terminal when it's started from one
(`CrashTwinsanity.exe` in PowerShell or the Command Prompt). A crash writes a report there with the program's addresses, and so
does a freeze (the game stuck for 20 seconds: where it's stuck). Each start keeps the last run's log as `log.previous.txt`, so
after a freeze or a crash you can start the game again and still send it. Please include it with any bug report, with where in
the game it happened: it also names your graphics card and its driver's version.

The log always has the time of each line, the game's states as they change (loading, playing, a cutscene, paused, a death,
game over) with where Crash is, the area he's in, the saves written and the movies played. At the start it also says what a bug
report would ask you: the date, your Windows version (and Wine's, on Linux or a Steam Deck), your processor, memory, screen and
sound device, the game's options and resolution, the mods and texture pack in use, and how the last session ended (closed,
crashed or stuck). While you play it adds how long each level took to load and a line a minute with the frame rate, and when the
game closes, a summary of the session's frame rate and how it closed. To report a bug you can reproduce, turn on **Options >
Gameplay > Detailed log** first: it also writes where Crash is every 5 seconds, every sound played and every file read from the
disc image.

The log is made to be shared: its paths write your user folder as `%USERPROFILE%` (so your user name isn't in them), a device
named after someone ("Ann's AirPods") loses the name, and the game doesn't write your computer's name, serial numbers or
addresses. Folders outside your user folder keep their names: have a look before you share it if one has yours.

## Licences

The game's code here is a decompilation for preservation and study; Crash Twinsanity and its content belong to their owners.
The folder carries SDL 3 (zlib licence), FFmpeg 9.0.2's libavcodec and libavutil (LGPL 2.1 or later: a decode-only build with
just the MPEG-2 video decoder, its source https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz; `avcodec-63.dll` and `avutil-61.dll`
are separate files you can replace with your own build of FFmpeg 9.x), the fonts Luckiest Guy (Apache 2.0) and Fredoka (SIL Open
Font Licence) and stb_truetype (public domain / MIT); their licences are in the `licenses` folder, with the port's own
(`Crash-Twinsanity-Windows-LICENSE.txt`: MIT for the Windows port's changes; the decompilation and the Mac port it's built on
keep their authors' rights). The icon is the port's own drawing.
