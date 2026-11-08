#!/bin/sh
# Builds the FFmpeg the native build links: libavcodec and libavutil only, LGPL 2.1 (no GPL or version 3 parts, nothing
# nonfree), decode-only, with nothing but what the movies need (src/platform/native/movie/: the MPEG-2 video decoder and the
# MPEG video parser; the PSS files are demultiplexed by pss.cpp and their sound goes the game's own way). Shared libraries, so
# whoever has the app can swap them for their own build (the LGPL's relinking clause). See native/third_party/ffmpeg/README.md.
#
#   native/tools/build_ffmpeg.sh [ARCH]
#
# ARCH is the host's (uname -m) unless given: arm64 or x86_64 on macOS (the other one is cross-compiled), the host's on Linux.
# It installs into build/native/ffmpeg/<os>-<arch> (macos-arm64, macos-x86_64, linux-x86_64...), where native/CMakeLists.txt
# looks first. The source release (native/third_party/ffmpeg-src/, not in git) is downloaded from ffmpeg.org when it isn't
# there, checked against the SHA-256 below, and against its signature by FFmpeg's release key when gpg is installed.
set -eu

VERSION=9.0.2
SHA256=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
# "FFmpeg release signing key <ffmpeg-devel@ffmpeg.org>" (https://ffmpeg.org/ffmpeg-devel.asc)
KEY_FINGERPRINT=FCF986EA15E6E293A5644F10B4322F04D67658D8
URL=https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SOURCES=$ROOT/native/third_party/ffmpeg-src
TARBALL=$SOURCES/ffmpeg-$VERSION.tar.xz

case $(uname -s) in
    Darwin) OS=macos ;;
    Linux) OS=linux ;;
    # Windows: Git Bash (or MSYS2) with LLVM-MinGW's clang on the PATH and an MSYS make (native/tools/build_windows.py fetches
    # one); configure's own temporary files want a POSIX folder
    MINGW*|MSYS*) OS=windows; export TMPDIR=/tmp ;;
    *) echo "build_ffmpeg.sh: $(uname -s) isn't supported" >&2; exit 1 ;;
esac
HOST_ARCH=$(uname -m)
[ "$HOST_ARCH" = aarch64 ] && HOST_ARCH=arm64
ARCH=${1:-$HOST_ARCH}
PREFIX=$ROOT/build/native/ffmpeg/$OS-$ARCH
BUILD=$ROOT/build/native/ffmpeg/build-$OS-$ARCH

# The source release, checked
mkdir -p "$SOURCES"
if [ ! -f "$TARBALL" ]; then
    curl -fL -o "$TARBALL.part" "$URL"
    mv "$TARBALL.part" "$TARBALL"
fi
if command -v shasum > /dev/null; then
    ACTUAL=$(shasum -a 256 "$TARBALL" | cut -d' ' -f1)
else
    ACTUAL=$(sha256sum "$TARBALL" | cut -d' ' -f1)
fi
if [ "$ACTUAL" != "$SHA256" ]; then
    echo "build_ffmpeg.sh: $TARBALL's SHA-256 is $ACTUAL, not $SHA256" >&2
    exit 1
fi
if command -v gpg > /dev/null; then
    [ -f "$TARBALL.asc" ] || curl -fL -o "$TARBALL.asc" "$URL.asc"
    [ -f "$SOURCES/ffmpeg-devel.asc" ] || curl -fL -o "$SOURCES/ffmpeg-devel.asc" https://ffmpeg.org/ffmpeg-devel.asc
    # A keyring of its own, so the user's isn't touched (short path: gpg's agent socket has a length limit)
    KEYRING=$(mktemp -d /tmp/ffmpeg-gpg.XXXXXX)
    trap 'rm -rf "$KEYRING"' EXIT
    gpg --homedir "$KEYRING" --quiet --import "$SOURCES/ffmpeg-devel.asc" 2> /dev/null
    if ! gpg --homedir "$KEYRING" --status-fd 1 --verify "$TARBALL.asc" "$TARBALL" 2> /dev/null |
        grep -q "VALIDSIG $KEY_FINGERPRINT"; then
        echo "build_ffmpeg.sh: $TARBALL's signature isn't FFmpeg's release key's ($KEY_FINGERPRINT)" >&2
        exit 1
    fi
    echo "build_ffmpeg.sh: signature good (FFmpeg release signing key $KEY_FINGERPRINT)"
else
    echo "build_ffmpeg.sh: no gpg, the SHA-256 checked only"
fi
# Unpacked afresh from the tarball just checked, each build: a folder left there may be another system's (unpacked on Windows
# and copied into Linux's Docker volume without the scripts' x bit: configure refused, 1.2.0's release check), or cut short
rm -rf "$SOURCES/ffmpeg-$VERSION"
tar -xf "$TARBALL" -C "$SOURCES"

# The configuration (README.md quotes it): everything off, then the MPEG-2 decoder and its parser
set -- \
    --prefix="$PREFIX" \
    --disable-everything \
    --disable-gpl --disable-nonfree --disable-version3 \
    --disable-programs --disable-doc --disable-network --disable-autodetect \
    --disable-avdevice --disable-avformat --disable-avfilter --disable-swresample --disable-swscale \
    --enable-decoder=mpeg2video --enable-parser=mpegvideo \
    --enable-shared --disable-static
if [ "$OS" = windows ]; then
    # MinGW's DLLs (avcodec-63.dll and avutil-61.dll with their import libraries), Windows' own threads
    set -- "$@" --target-os=mingw32 --arch=x86_64 --enable-w32threads --cc=x86_64-w64-mingw32-clang --ar=llvm-ar \
        --nm=llvm-nm --ranlib=llvm-ranlib
else
    set -- "$@" --enable-pic --enable-pthreads
    CC=${CC:-clang}
    command -v "$CC" > /dev/null || CC=cc
    set -- "$@" --cc="$CC"
fi
if [ "$OS" = macos ]; then
    # The same macOS the app runs on, whichever Mac builds it
    TARGET=${MACOSX_DEPLOYMENT_TARGET:-11.0}
    set -- "$@" --extra-cflags="-arch $ARCH -mmacosx-version-min=$TARGET" \
        --extra-ldflags="-arch $ARCH -mmacosx-version-min=$TARGET"
    if [ "$ARCH" != "$HOST_ARCH" ]; then
        set -- "$@" --enable-cross-compile --arch="$ARCH" --target-os=darwin
    fi
fi
# x86's hand-written assembly needs nasm; without it the C (slower, and not checked to give the same pictures: native/
# movie-tests compares them)
if [ "$ARCH" = x86_64 ] && ! command -v nasm > /dev/null; then
    echo "build_ffmpeg.sh: no nasm, x86 assembly off"
    set -- "$@" --disable-x86asm
fi

rm -rf "$BUILD" "$PREFIX"
mkdir -p "$BUILD"
cd "$BUILD"
"$SOURCES/ffmpeg-$VERSION/configure" "$@"
JOBS=$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 4)
make -j"$JOBS"
make install
echo "build_ffmpeg.sh: FFmpeg $VERSION in $PREFIX"
