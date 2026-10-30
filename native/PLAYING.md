# Playing Crash Twinsanity on a Mac

This is a native Mac port of Crash Twinsanity, made from a decompilation of the PAL PlayStation 2 game. It contains no game data:
you need your own copy of the game.

## What you need

- A Mac with Apple Silicon or Intel, running a recent macOS.
- A disc image of **your own** PAL copy of Crash Twinsanity (Europe/Australia, SLES-52568), as an `.iso` (or `.bin`/`.img`).
  Other versions (USA, Japan) aren't supported.

Nothing else: the app carries its own libraries (no Homebrew needed).

## First run

1. Unzip `Crash-Twinsanity-macOS.zip` and move **Crash Twinsanity.app** wherever you like (Applications is fine).
2. The app is signed ad hoc, not by an Apple developer account. The first time you open it, macOS may say it can't check it:
   right-click (or Control-click) the app, choose **Open**, then **Open** again. You only have to do this once.
3. On the first launch the app asks for your disc image with a file dialog. Pick your `.iso`. It remembers it; to pick another,
   delete `disc_image.txt` from the settings folder (below).

## Where things are

Everything the game keeps is in **~/Library/Application Support/Crash Twinsanity/** (in Finder: Go > Go to Folder…, then paste
that path):

| What | Where |
| --- | --- |
| Saves (the game's memory card) | `memorycard/` |
| The options | `options.txt`, `resolution.txt` |
| Your controls | `bindings.txt` |
| The disc image's path | `disc_image.txt` |
| Texture packs | `textures/SLES-52568/replacements/` (PCSX2's layout: a PCSX2 pack's `replacements` folder works as it is) |

Saving works like a PC game: no memory card screens. Save from the pause menu or at the game's save points; load from the
main menu.

## Controls

A controller (Xbox, PlayStation, Switch and most others) works out of the box, as on the PS2. Keyboard and mouse:

| Action | Keys |
| --- | --- |
| Move | W A S D |
| Look and turn the camera | The mouse (or the arrow keys); Q / E turn it left and right |
| Jump | Space |
| Spin | Left mouse button (or J) |
| Crouch, skid, slide | Left Shift (or K) |
| Status | Tab (or I) |
| Shoulders (strafe, hover, the pause pages) | Z / C |
| Walk / run | Left Ctrl toggles walking |
| Pause | Esc (or Enter) |
| Skip a cutscene | Hold any button |
| Menus | Arrow keys (or Q / E, or the mouse), Enter to select, Esc to go back |
| Fullscreen | Cmd+F (or Alt+Enter) |

Everything can be rebound in **Options > Controls**; the button prompts follow what you last used. Mouse look, its sensitivity
and inverted Y are there too.

## Options

Options (from the main or pause menu) has PC settings: window size or fullscreen, the internal resolution (1× to 8× the PS2's),
widescreen (4:3, 16:9 or 21:9), 50 or 60 Hz, texture filtering, scanlines, texture packs, anti-aliasing, v-sync, volumes,
skipping cutscenes, faster loading, and optional fixes of the original game's bugs (off plays exactly like the PS2).

## If something goes wrong

The app writes what it's doing to its standard output: run it from Terminal to see it,
`"/Applications/Crash Twinsanity.app/Contents/MacOS/Crash Twinsanity"`. A crash prints a report there; please include it with any
bug report.

## Building the release

From the repository, with the native build configured (native/NATIVE.md):

    cmake --build build/native/cmake --target release

This builds the game and makes `build/native/dist/Crash Twinsanity.app` (standalone: its libraries inside, no Homebrew needed)
and `build/native/dist/Crash-Twinsanity-macOS.zip`. Our own LGPL FFmpeg is used when `native/tools/build_ffmpeg.sh` has been run.

## Licences

The game's code here is a decompilation for preservation and study; Crash Twinsanity and its content belong to their owners.
The app bundles SDL 3 (zlib licence), FFmpeg 9.0.2's libavcodec and libavutil (LGPL 2.1 or later: a decode-only build with just
the MPEG-2 video decoder; its source is https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz), the fonts Luckiest Guy (Apache 2.0) and
Fredoka (SIL Open Font Licence) and stb_truetype (public domain / MIT); their licences are in `Contents/Resources`
(`licenses/ffmpeg` has FFmpeg's, with how it was built).

FFmpeg's libraries are separate files you can replace with your own build: `libavcodec.63.dylib` and `libavutil.61.dylib` in
`Crash Twinsanity.app/Contents/Frameworks/` (FFmpeg 9.x, with the `mpeg2video` decoder). Then sign the app again with
`codesign --force --deep --sign - "Crash Twinsanity.app"`. `Contents/Resources/licenses/ffmpeg/README.md` has the details.
