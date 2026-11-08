# The Linux build's system (native/LINUX.md, "In Docker"): what native/tools/build_linux.py and package_linux.py need, so the
# Linux program and its AppImage can be made on any computer with Docker, Windows included. native/tools/build_linux_docker.py
# builds this image and runs the build in it; the source tree is copied into a volume of its own, never built in place.
#
# Ubuntu 24.04: an LTS the AppImage reaches from (its glibc 2.39; the AppImage runs where the glibc is as new as the symbols it
# uses, package_linux.py prints which), and the system ps2dev's own Linux toolchain is built on, so its EE compiler runs here.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG CLANG_MAJOR=21

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl gnupg \
    && rm -rf /var/lib/apt/lists/*

# clang 21 from apt.llvm.org (Ubuntu 24.04 has 18; the layout tools need 21 or later, native/LINUX.md)
RUN install -d /etc/apt/keyrings \
    && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key -o /etc/apt/keyrings/apt.llvm.org.asc \
    && echo "deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] http://apt.llvm.org/noble/ llvm-toolchain-noble-${CLANG_MAJOR} main" \
       > /etc/apt/sources.list.d/llvm.list \
    && apt-get update && apt-get install -y --no-install-recommends clang-${CLANG_MAJOR} \
    && rm -rf /var/lib/apt/lists/*

# The build's tools and SDL's backends' development packages (the list in native/LINUX.md), rsync for the tree's copy
RUN apt-get update && apt-get install -y --no-install-recommends \
       cmake ninja-build pkg-config make nasm patchelf python3-venv python3-pip rsync file binutils xz-utils \
       libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
       libwayland-dev libdecor-0-dev libegl-dev libgl-dev libgbm-dev libdrm-dev \
       libpipewire-0.3-dev libpulse-dev libasound2-dev libdbus-1-dev libudev-dev libibus-1.0-dev \
    && rm -rf /var/lib/apt/lists/*

# The PS2 toolchain in its default place (tools/local_config.py): its EE compiler tells the layout tools its headers' folders.
# ps2dev's "latest" is rebuilt in place, so it isn't pinned: its SHA-256 is written down beside it
RUN curl -fsSL https://github.com/ps2dev/ps2dev/releases/download/latest/ps2dev-ubuntu-latest.tar.gz -o /tmp/ps2dev.tar.gz \
    && sha256sum /tmp/ps2dev.tar.gz | tee /usr/local/ps2dev-ubuntu-latest.sha256 \
    && mkdir -p /tmp/ps2dev && tar -xzf /tmp/ps2dev.tar.gz -C /tmp/ps2dev \
    && mv "$(dirname "$(find /tmp/ps2dev -maxdepth 3 -type d -name ee | head -n 1)")" /usr/local/ps2dev \
    && rm -rf /tmp/ps2dev /tmp/ps2dev.tar.gz \
    && /usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-g++ --version | head -n 1

ENV PS2DEV=/usr/local/ps2dev

# libmpc3 for ps2dev's EE compiler (its cc1plus loads it, with MPFR and GMP, from the system); for the AppImage's checks
# (native/tools/check_linux.py), a screen with no monitor (Xvfb) and Mesa's OpenGL on the processor
RUN apt-get update && apt-get install -y --no-install-recommends libmpc3 xvfb xauth libgl1-mesa-dri libglx-mesa0 libegl-mesa0 \
    && rm -rf /var/lib/apt/lists/*
