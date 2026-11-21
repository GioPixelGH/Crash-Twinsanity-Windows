# Crash Twinsanity on Linux

Each release since 1.2.0 has a Linux download beside the Windows one.
It's an AppImage, a single file you run.
It plays on 64-bit PCs (x86-64), with the same options as the Windows version.
It was tried on Debian 13, with an AMD graphics card on Mesa.

[Play it](#play-it) · [The Steam Deck](#the-steam-deck) · [Building it yourself](#building-it-yourself) · [For developers](#for-developers)

## Play it

1. Download `Crash-Twinsanity-Linux-x86_64.AppImage` from the [latest release](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/releases/latest).
2. Make it executable.
   In a terminal, run `chmod +x Crash-Twinsanity-Linux-x86_64.AppImage`.
   In a file manager, open the file's **Properties > Permissions** and tick **Is executable** (or **Allow executing file as program**).
3. Run it.
4. Pick the disc image of your own copy, as an `.iso`, `.bin` or `.img`.
   It can be the PAL disc (SLES-52568) or the North American disc, version 2.00 (SLUS-20909).
   The game remembers it.
   You can also give it on the command line, with `--iso PATH`.

### What you need

- A 64-bit PC (x86-64).
- OpenGL 3.3 or Vulkan, from Mesa or your graphics card's own driver.
  OpenGL is the default.
  Vulkan is new in 1.3.0, and you choose it in **Options > Display > Renderer > Graphics API**.
- glibc 2.38 or later.
  Ubuntu 24.04, Linux Mint 22, Debian 13, Fedora 39, Arch and the versions after them have it.
  SteamOS should have it too, but it hasn't been tried yet.
  Ubuntu 22.04, Linux Mint 21 and Debian 12 are too old for the AppImage.
  On those, you can [build the game yourself](#building-it-yourself).
- FUSE, which most desktops have for AppImages.

Everything else is inside the AppImage.
It holds SDL 3, FFmpeg's two libraries, the fonts and the licences.
It holds nothing of the game's.

### If it doesn't start

- Without FUSE, the AppImage says so when you start it from a terminal.
  Then run `./Crash-Twinsanity-Linux-x86_64.AppImage --appimage-extract-and-run` instead.
  It runs the game all the same.
- A message about a `GLIBC_` version not found means your distribution is older than the AppImage.
  You can [build the game yourself](#building-it-yourself) there.
- For anything else, open an [issue](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues) and attach your `log.txt`.

### Where your saves and settings are

Settings, saves, texture packs, mods and `log.txt` are in `~/.config/crash-twinsanity`.
If you set `XDG_CONFIG_HOME`, they're in `$XDG_CONFIG_HOME/crash-twinsanity` instead.

The [players' guide](PLAYING_WINDOWS.md) explains the options.
They're the same on Linux, with two exceptions.

- The update check is only on Windows, where Windows' own WinHTTP makes its one request.
  The Linux version never connects to the internet, and its FFmpeg is built without its network parts.
- Portable mode is only on Windows.

## The Steam Deck

Nobody has tried it on a Deck yet, so **testers are wanted** ([#34](https://github.com/GioPixelGH/Crash-Twinsanity-Windows/issues/34)).
An issue with your `log.txt`, saying what worked and what didn't, helps the most.

### Add it to Steam

1. Switch to Desktop Mode.
   Press the Steam button, then choose **Power > Switch to Desktop**.
2. Download the AppImage, for example into `/home/deck/Games`.
3. In Dolphin, the file manager, open the AppImage's **Properties > Permissions** and tick **Is executable**.
4. Put your disc image in the same folder.
5. Start the AppImage once from Dolphin, and pick your disc image.
   The game remembers it, so Game Mode never needs the file dialog.
6. In Steam on the desktop, choose **Games > Add a Non-Steam Game to My Library**, then **Browse**.
7. Choose the AppImage.
   If it isn't listed, set the file type to **All Files**.
8. Choose **Add Selected Programs**.
   You can change its name and artwork in its **Properties**.
9. Choose **Return to Gaming Mode** on the desktop.
   The game is in the Library, under **Non-Steam**.

Instead of step 5, you can put `--iso "/home/deck/Games/Crash Twinsanity.iso"` in the game's launch options in Steam.

### The picture

The first time, the game starts fullscreen on the Deck's own 1280 x 800 screen, at 2x the PS2's resolution.
The picture is 16:10, the shape of the Deck's screen.
16:9 in **Options > Display > Widescreen** puts thin bars above and below.
**Options > Display** changes all of these, and its **Reset** puts them back.

The game's speed on the Deck isn't measured yet.
If it's slow, try the resolution at 1x in **Options > Display**.
It's the lightest.

### The controls

Steam gives a non-Steam game the Deck's controls as an Xbox-style controller.
The game takes it as it comes, with the Xbox's button prompts.
The Deck's A, B, X and Y are in the same places.

If the sticks or buttons act as a mouse or keys instead, choose the **Gamepad** layout in the game's controller settings.
They're behind the controller icon on the game's page in the Library.

These are the game's buttons, as on the PS2.
**Options > Controls** can change any of them.

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

The back buttons (L4, L5, R4, R5) and the trackpads do nothing, unless Steam's layout gives them a button.
With **Options > Game > Skip cutscenes** on, holding any button for a second skips a cutscene.

<details>
<summary><b>For testers</b></summary>

The log's `system:` line names the Deck's model.
`Jupiter` is the LCD one, `Galileo` the OLED one.

`TWIN_STEAM_DECK=1` makes another computer start as a Deck.
`TWIN_STEAM_DECK=0` makes a Deck start as a PC.

</details>

## Building it yourself

Build the game yourself on a distribution too old for the AppImage, or to change the code.
You need about 3 GB of disk space, your PAL disc image (SLES-52568) and an internet connection for the first build.
A program built on a system runs on that system and newer ones.

1. **Install the tools.**
   On Debian 13, clang 21 is in trixie-backports:

   ```sh
   sudo apt install -t trixie-backports clang-21
   sudo apt install cmake ninja-build pkg-config make curl nasm patchelf python3-venv libmpc3 \
       libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
       libwayland-dev libdecor-0-dev libegl-dev libgl-dev libgbm-dev libdrm-dev \
       libpipewire-0.3-dev libpulse-dev libasound2-dev libdbus-1-dev libudev-dev libibus-1.0-dev
   ```

   Other distributions need clang 21 or later, and the development packages of the same libraries.
   On Ubuntu 22.04, Linux Mint 21 and Debian 12, clang 21 comes from [apt.llvm.org](https://apt.llvm.org/), in its packages for jammy and bookworm.
   The build hasn't been tried on those three yet.

2. **Get the PS2 toolchain.**
   Download `ps2dev-ubuntu-latest.tar.gz` from [ps2dev's latest release](https://github.com/ps2dev/ps2dev/releases/tag/latest), and extract it anywhere.
   The build looks for it in `$PS2DEV`, then in local.json's `ps2dev` (see local.example.json), then in `/usr/local/ps2dev`.

3. **Clone this repository and make the Python environment**:

   ```sh
   git clone https://github.com/GioPixelGH/Crash-Twinsanity-Windows.git
   cd Crash-Twinsanity-Windows
   python3 -m venv .venv
   .venv/bin/python -m pip install -r requirements.txt
   ```

4. **Take the game's executable out of your disc image, check it, and split it**:

   ```sh
   .venv/bin/python native/tools/iso_extract.py "/path/to/Crash Twinsanity (Europe).iso" SLES_525.68
   sha1sum SLES_525.68    # d41b8d53f733f930cf03d8ea944bec24429fe57c
   .venv/bin/python tools/split.py
   ```

5. **Build** (the first time it also builds SDL 3, glslang and FFmpeg):

   ```sh
   .venv/bin/python native/tools/build_linux.py
   ```

6. **Play**:

   ```sh
   build/native/cmake/twinsanity
   ```

   The first time, it asks for your disc image and remembers it.
   `--iso PATH`, `$TWINSANITY_ISO` and local.json's `disc_image` give one too.
   Settings, saves and texture packs are in the same folder as the AppImage's, `~/.config/crash-twinsanity`.

<details>
<summary><b>What the build does</b> (for the curious)</summary>

- The program is built with clang, SDL 3 built from its source release, and the same decode-only LGPL FFmpeg as on the other systems (native/tools/build_ffmpeg.sh).
- The platform code is HAB3X's x86-64 one (native/NATIVE.md).
  Beyond the macOS code, Linux needs only the low memory emulation's register access (`lowmemory.cpp`).
- The second `apt install` command's libraries are SDL's backends, which it loads at run time.
  SDL's configure stops when one is missing, and names it.
- libmpc3 is for the PS2 toolchain's compiler, which loads it from the system.
  That compiler needs only glibc 2.17, with libmpc3, libmpfr6 and libgmp10.
- The native build uses the PS2 toolchain's headers to lay out the game's data.
- `build_linux.py` finds clang.
  It takes `clang++` if it's 21 or later, else the newest `clang++-N`.
- It builds SDL 3 and glslang into `build/native/deps`, and FFmpeg into `build/native/ffmpeg/linux-x86_64`, when they aren't there.
  glslang 16.6.0 is built static, for the Vulkan backend's shaders.
  A build configured without it is configured again.
- It configures `build/native/cmake` the first time, then builds.
  `--configure` configures it again, and `--clean` builds from nothing.
- CMake needs clang as the C++ compiler, not GCC.
  The layout tools (`data_types.py`, `convert_data.py`) run it for the game's record layouts, the PS2 target's included.
- The C++ runtime is linked in (`-static-libstdc++`), so the program doesn't depend on the system's libstdc++.
- Distributions other than Debian keep the system's libraries elsewhere, and the build finds them.
  The stand-ins' script (native/tools/make_stubs.py, native/NATIVE.md) asks the compiler where `libc.so.6`, `libm.so.6` and `libstdc++.so.6` are (`-print-file-name`).
  Otherwise it looks in Debian's `/lib/x86_64-linux-gnu`, Fedora's and openSUSE's `/usr/lib64` and Arch's `/usr/lib`, and prints which it read.
  Without the C library, it stops and says where it looked.
  On Arch it used to stand in for `strlen`, and the program crashed at start (issue #27).
- It was tried on Debian 13 with clang 21.1.8 from trixie-backports.

</details>

## For developers

<details>
<summary><b>The AppImage</b></summary>

    .venv/bin/python native/tools/package_linux.py

This makes `build/native/dist/Crash-Twinsanity-Linux-x86_64.AppImage` from the build above.
A player marks it executable (`chmod +x`) and runs it, on most distributions, with nothing installed.

- It holds the program, the libraries of SDL 3 and FFmpeg, the UI's fonts and the licences (glslang's among them).
  It holds nothing of the game's.
- The C library and OpenGL or Vulkan are the system's, from the player's driver.
  SDL loads `libvulkan.so.1` when the setting asks for Vulkan.
- What SDL loads at run time is the system's too: X11 or Wayland, and PipeWire, PulseAudio or ALSA.
- The script checks that nothing else is loaded from outside the AppImage.
- It downloads appimagetool and its runtime at fixed versions, checked by their SHA-256.

The AppImage needs a glibc as new as the system it was built on, or newer.
The script prints which.
A release meant for older distributions is built on the oldest one it should run on, in a container of it with clang 21 from apt.llvm.org.

FFmpeg's libraries are LGPL 2.1, and the licence's relinking clause lets you replace them.
`--appimage-extract` unpacks the AppImage into `squashfs-root/`.
Its `usr/lib` holds `libavcodec` and `libavutil`, and `squashfs-root/AppRun` runs it.

</details>

<details>
<summary><b>In Docker</b></summary>

    python native/tools/build_linux_docker.py [--configure] [--clean]

This makes the AppImage on any computer with Docker running, Windows included.
The releases are made this way, on Windows.

1. It builds the image native/tools/linux.Dockerfile describes.
   That's Ubuntu 24.04, clang 21 from apt.llvm.org, the packages of SDL's backends, and ps2dev's Linux toolchain in `/usr/local/ps2dev`.
2. It copies the source tree into a Docker volume of its own.
   It leaves out the build's folders and the PS2 build's ninja files, the virtual environment, git's folder, texture packs and local.json.
   It also leaves out FFmpeg's sources as the Windows build unpacked them, since the Linux build downloads its own.
   A file gone from the tree goes from the copy too.
3. It runs the steps above in it: the virtual environment, `build_linux.py` and `package_linux.py`.

The AppImage comes out in `build/native/linux/`, and the container's output in `build/native/linux/build.txt`.

The volume keeps the build between runs, so a second run rebuilds only what changed.
`docker volume ls` lists the volumes (`crash-twinsanity-linux-<the tree's folder>-...`).
Removing one starts the build again from nothing.

The split has to be made first, as for any build.
The copy takes `SLES_525.68` and `asm/` along, and they stay in the volume, on your computer.

`--check --iso IMAGE` then runs native/tools/check_linux.py in the same image.
It checks what the AppImage holds and loads, its licences, the glibc it needs and its version.
Then it starts the game as a player would, headless on Xvfb with Mesa's software OpenGL, and plays the beach for 20 seconds from your disc image (mounted read-only).
Then it makes the same start with Vulkan (`$TWIN_GPU_API=vulkan`), on Mesa's Vulkan on the processor (lavapipe, in the image since 1.3.0).
`release_windows.py check` does both (native/WINDOWS.md, "Releasing").

The image is Ubuntu 24.04 because ps2dev's own Linux toolchain is built there.
Its EE compiler, which the layout tools ask for its headers' folders, runs there.
It's also an LTS that many distributions follow.
The AppImage needs the glibc of the newest symbols it uses, which package_linux.py and the checks print.

</details>
