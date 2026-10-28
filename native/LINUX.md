# The native build on Linux

The game built for Linux x86-64 with clang, SDL3 built from its source release and the same decode-only LGPL FFmpeg as the
other systems (native/tools/build_ffmpeg.sh). Tried on Debian 13 with clang 21.1.8 from trixie-backports and an AMD graphics
card on Mesa. The platform code is HAB3X's x86-64 one (native/NATIVE.md): what Linux needs apart from macOS is only the
low memory emulation's register access (`lowmemory.cpp`).

## Building and running

You need about 3 GB of disk space, your PAL disc image (SLES-52568) and an internet connection for the first build.

1. **Install the tools.** On Debian 13 (clang 21 is in trixie-backports; other distributions: clang 21 or later and the same
   libraries' development packages):

   ```sh
   sudo apt install -t trixie-backports clang-21
   sudo apt install cmake ninja-build pkg-config make curl nasm patchelf python3-venv \
       libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
       libwayland-dev libdecor-0-dev libegl-dev libgl-dev libgbm-dev libdrm-dev \
       libpipewire-0.3-dev libpulse-dev libasound2-dev libdbus-1-dev libudev-dev libibus-1.0-dev
   ```

   The second command's libraries are SDL's backends, which it loads at run time. SDL's configure stops when one is missing
   and names it.

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
