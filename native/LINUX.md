# The native build on Linux

The game built for Linux x86-64 with clang, SDL3 built from its source release and the same decode-only LGPL FFmpeg as the
other systems (native/tools/build_ffmpeg.sh). Tried on Debian 13 with clang 21.1.8 from trixie-backports and an AMD graphics
card on Mesa. The platform code is HAB3X's x86-64 one (native/NATIVE.md): what Linux needs apart from macOS is only the
low memory emulation's register access (`lowmemory.cpp`).

Since 1.2.0 each release has an AppImage beside the Windows zip, made in Docker on Ubuntu 24.04 ("In Docker" below) and
checked by native/tools/release_windows.py. Playing it is in "Playing from the AppImage" and "The Steam Deck".

## Playing from the AppImage

1. Download `Crash-Twinsanity-Linux-x86_64.AppImage` from the
   [latest release](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/releases/latest).
2. Make it executable: `chmod +x Crash-Twinsanity-Linux-x86_64.AppImage`, or the file's Properties > Permissions >
   "Is executable" (or "Allow executing file as program").
3. Run it. The first time it asks for your disc image (a PAL `.iso`, `.bin` or `.img` of your own copy, SLES-52568) and
   remembers it; `--iso PATH` gives one on the command line.

What it needs: an x86-64 PC with OpenGL 3.3 (Mesa or the graphics card's own driver); a distribution with glibc 2.38 or
later: Ubuntu 24.04, Linux Mint 22, Debian 13, Fedora 39, Arch and the ones after them, not Ubuntu 22.04, Linux Mint 21 or
Debian 12 (their glibc is older: there, build the program yourself, below; SteamOS should have it, untried so far); and
FUSE, which most desktops have for AppImages. Without FUSE (it
says so when started from a terminal), `./Crash-Twinsanity-Linux-x86_64.AppImage --appimage-extract-and-run` runs it all the
same. Everything else is inside: SDL3, FFmpeg's two libraries, the fonts and the licences; nothing of the game's.

Settings, saves, texture packs, mods and `log.txt` are in `~/.config/crash-twinsanity` (`$XDG_CONFIG_HOME/crash-twinsanity`).
The options are the Windows version's (native/PLAYING_WINDOWS.md), except the update check, which only the Windows version
has (Windows' own WinHTTP makes its one request; the Linux build never connects, and its FFmpeg is built without its network
parts), and portable mode.

## The Steam Deck

Not tried on a Deck yet: **testers wanted**. An issue with your `log.txt` (above), saying what worked and what didn't, helps
the most.

On a Deck the game starts fullscreen, on the Deck's own 1280 x 800 screen, at 2x the PS2's resolution (the first start's
defaults: Options > Display changes them, and the Reset puts these back). The picture is 16:9 (or 4:3), with thin bars above
and below on the 16:10 screen. The log's `system:` line names the Deck's model (`Jupiter` the LCD one, `Galileo` the OLED
one); `$TWIN_STEAM_DECK=1` makes another computer start as a Deck, `0` a Deck as a PC.

1. **Desktop Mode** (the Steam button > Power > Switch to Desktop): download the AppImage, for example into
   `/home/deck/Games`, and in Dolphin (the file manager) its Properties > Permissions > "Is executable".
2. **Your disc image** in the same folder. Start the AppImage once from Dolphin and pick it: the game remembers it, so Game
   Mode never needs the file dialog. (Or, at step 4, the launch options `--iso "/home/deck/Games/Crash Twinsanity.iso"`.)
3. **Add it to Steam**: in Steam (on the desktop), Games > Add a Non-Steam Game to My Library > Browse, choose the AppImage
   (set the file type to "All Files" if it isn't listed), then Add Selected Programs. Its name and artwork can be changed in
   its Properties.
4. **Game Mode** (the desktop's "Return to Gaming Mode"): the game is in the Library, under Non-Steam.

The controls: Steam gives a non-Steam game the Deck's controls as an Xbox-style controller, which the game takes as it comes,
with the Xbox's button prompts (the Deck's A, B, X and Y are in the same places). If the sticks or buttons act as a mouse or
keys instead, choose the "Gamepad" layout in the game's controller settings (the controller icon on its page in the
Library). The game's own buttons, as on the PS2 (all can be changed in Options > Controls):

| Deck | Game | PS2 |
| --- | --- | --- |
| Left stick | Move | Left stick |
| Right stick | Camera | Right stick |
| A | Jump | Cross |
| X | Spin | Square |
| B | Crouch, skid, slide | Circle |
| Y | Status | Triangle |
| L1 / L2, R1 / R2 | Shoulders | L1 / L2, R1 / R2 |
| Menu (☰) | Pause | Start |
| View (⧉) | Select | Select |
| D-pad | D-pad (menus) | D-pad |

The back buttons (L4, L5, R4, R5) and the trackpads do nothing unless Steam's layout gives them a button. Holding any
button for a second skips a cutscene when Options > Gameplay > Skip cutscenes is on. Speed on the Deck isn't measured yet:
if it's slow, Options > Display's resolution at 1x is the lightest.

## Building and running

You need about 3 GB of disk space, your PAL disc image (SLES-52568) and an internet connection for the first build.

1. **Install the tools.** On Debian 13 (clang 21 is in trixie-backports; other distributions: clang 21 or later and the same
   libraries' development packages):

   ```sh
   sudo apt install -t trixie-backports clang-21
   sudo apt install cmake ninja-build pkg-config make curl nasm patchelf python3-venv libmpc3 \
       libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
       libwayland-dev libdecor-0-dev libegl-dev libgl-dev libgbm-dev libdrm-dev \
       libpipewire-0.3-dev libpulse-dev libasound2-dev libdbus-1-dev libudev-dev libibus-1.0-dev
   ```

   The second command's libraries are SDL's backends, which it loads at run time. SDL's configure stops when one is missing
   and names it. libmpc3 is for the PS2 toolchain's compiler (step 2), which loads it from the system.

   On distributions too old for the AppImage (Ubuntu 22.04, Linux Mint 21, Debian 12): clang 21 from
   [apt.llvm.org](https://apt.llvm.org/) (its packages for jammy and bookworm), the same libraries otherwise. The PS2
   toolchain's compiler needs only glibc 2.17 (with libmpc3, libmpfr6 and libgmp10). A program built there runs on that
   system and newer ones. Not tried on those distributions yet.

2. **Get the PS2 toolchain**: `ps2dev-ubuntu-latest.tar.gz` from
   [ps2dev's latest release](https://github.com/ps2dev/ps2dev/releases/tag/latest), extracted anywhere. The native build uses
   its headers to lay out the game's data. It's looked for in `$PS2DEV`, local.json's `ps2dev` (local.example.json), else
   `/usr/local/ps2dev`.

3. **The Python environment, the game's executable and the split**:

   ```sh
   python3 -m venv .venv
   .venv/bin/python -m pip install -r requirements.txt
   .venv/bin/python native/tools/iso_extract.py "/path/to/Crash Twinsanity (Europe).iso" SLES_525.68
   sha1sum SLES_525.68    # d41b8d53f733f930cf03d8ea944bec24429fe57c
   .venv/bin/python tools/split.py
   ```

4. **Build** (the first time it also builds SDL3 and FFmpeg):

   ```sh
   .venv/bin/python native/tools/build_linux.py
   ```

   It finds clang (`clang++` if it's 21 or later, else the newest `clang++-N`), builds SDL3 into `build/native/deps` and
   FFmpeg into `build/native/ffmpeg/linux-x86_64` when they aren't there, configures `build/native/cmake` the first time
   (`--configure` again, `--clean` from nothing) and builds. CMake needs clang as the C++ compiler, not GCC: the layout tools
   (`data_types.py`, `convert_data.py`) run it for its record layouts, the PS2's target's included. The C++ runtime is linked
   in (`-static-libstdc++`), so the program doesn't depend on the system's libstdc++.

   Distributions other than Debian keep the system's libraries elsewhere, and the build finds them: the stand-ins' script
   (native/tools/make_stubs.py, native/NATIVE.md) asks the compiler where `libc.so.6`, `libm.so.6` and `libstdc++.so.6` are
   (`-print-file-name`), else looks in Debian's `/lib/x86_64-linux-gnu`, Fedora's and openSUSE's `/usr/lib64` and Arch's
   `/usr/lib`, and prints which it read. Without the C library it stops and says where it looked (issue #27: on Arch it used to
   stand in for `strlen` and the program crashed at start).

5. **Play**:

   ```sh
   build/native/cmake/twinsanity
   ```

   The first time, it asks for your disc image and remembers it (`--iso PATH`, `$TWINSANITY_ISO` and local.json's
   `disc_image` give one too). Settings, saves and texture packs are in `$XDG_CONFIG_HOME/crash-twinsanity`, by default
   `~/.config/crash-twinsanity`.

## The AppImage

    .venv/bin/python native/tools/package_linux.py

makes `build/native/dist/Crash-Twinsanity-Linux-x86_64.AppImage` from the build above: one file a player marks executable
(`chmod +x`) and runs, on most distributions, with nothing installed. It holds the program, SDL3 and FFmpeg's libraries,
the UI's fonts and the licences, and nothing of the game's. The C library, OpenGL (the player's driver) and what SDL loads at
run time (X11 or Wayland, PipeWire, PulseAudio or ALSA) are the system's. The script checks that nothing else is loaded from
outside the AppImage, and downloads appimagetool and its runtime at fixed versions, checked by their SHA-256.

The AppImage needs a glibc as new as the system it was built on, or newer; the script prints which. A release meant for older
distributions is built on the oldest one it should run on (a container of it, with clang 21 from apt.llvm.org).

FFmpeg's libraries are LGPL 2.1 and can be replaced (the licence's relinking clause): `--appimage-extract` unpacks it into
`squashfs-root/`, whose `usr/lib` holds `libavcodec` and `libavutil`, and `squashfs-root/AppRun` runs it.

## In Docker

    python native/tools/build_linux_docker.py [--configure] [--clean]

makes the AppImage on any computer with Docker running, Windows included (where the releases are made). It builds the image
native/tools/linux.Dockerfile describes (Ubuntu 24.04, clang 21 from apt.llvm.org, SDL's backends' packages, ps2dev's Linux
toolchain in `/usr/local/ps2dev`), copies the source tree into a Docker volume of its own (the build's folders and the PS2
build's ninja files, FFmpeg's sources as the Windows build unpacked them (the Linux build downloads its own), the virtual
environment, git's folder, texture packs and local.json left out; a file gone from the tree goes from the copy too), and runs
the steps above in it: the virtual environment, `build_linux.py`, `package_linux.py`. The AppImage comes out in
`build/native/linux/`, the container's output in `build/native/linux/build.txt`. The volume keeps the build between runs, so a
second one rebuilds only what changed; `docker volume ls` lists them (`crash-twinsanity-linux-<the tree's folder>-...`), and
removing one starts it again from nothing. The split has to be made first, as for any build: the copy takes `SLES_525.68` and
`asm/` along, and they stay in the volume, on your computer.

`--check --iso IMAGE` then runs native/tools/check_linux.py in the same image: what the AppImage holds and loads, its licences,
the glibc it needs, its version, and a start as a player's, headless on Xvfb with Mesa's software OpenGL, the beach played for 20 seconds
from your disc image (mounted read-only). release_windows.py check does both (native/WINDOWS.md, "Releasing").

Ubuntu 24.04 because it's where ps2dev's own Linux toolchain is built (its EE compiler, which the layout tools ask for its
headers' folders, runs there) and an LTS many distributions follow; the AppImage needs the glibc of the newest symbols it
uses, which package_linux.py and the checks print.
