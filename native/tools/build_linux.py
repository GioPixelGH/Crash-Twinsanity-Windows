#!/usr/bin/env python3
"""The native build on Linux x86-64 (native/LINUX.md): finds clang (21 or later), builds SDL3 and glslang (the Vulkan backend's)
from their source releases into build/native/deps and an LGPL FFmpeg (native/tools/build_ffmpeg.sh) when they aren't there, then
configures and builds build/native/cmake/twinsanity.

    .venv/bin/python native/tools/build_linux.py [--configure] [--clean] [ninja arguments...]

The split (tools/split.py) has to have been made first: the build converts its data. native/tools/package_linux.py makes the
AppImage from the result.
"""
import argparse
import glob
import hashlib
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]
DEPS = HERE / "build" / "native" / "deps"
BUILD = HERE / "build" / "native" / "cmake"
FFMPEG = HERE / "build" / "native" / "ffmpeg" / "linux-x86_64"
# The clang the build was tried with (Debian 13's trixie-backports); the layout tools need clang itself, not GCC
MIN_CLANG_MAJOR = 21
# 3.4 or later: SDL_LoadPNG (Debian 13 has 3.2)
SDL_VERSION = "3.4.18"
SDL_URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/SDL3-{SDL_VERSION}.tar.gz"
SDL_SHA256 = "9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3"
SDL_PREFIX = DEPS / f"SDL3-{SDL_VERSION}-linux"
# glslang (BSD 3-Clause and others, its LICENSE.txt): the Vulkan backend makes the renderer's GLSL SPIR-V with it (THE LIST 71, 73),
# the same release as build_windows.py's, static libraries
GLSLANG_VERSION = "16.6.0"
GLSLANG_URL = f"https://github.com/KhronosGroup/glslang/archive/refs/tags/{GLSLANG_VERSION}.tar.gz"
GLSLANG_SHA256 = "9c09b901149c729df745057dafa815278aaa101b84d2b6e14f16a42de52f97f2"
GLSLANG_PREFIX = DEPS / f"glslang-{GLSLANG_VERSION}-linux"
# The C++ runtime linked in, so the program runs where the system's libstdc++ is older than the builder's, with none of its
# symbols exported for the system's own C++ libraries (Mesa's LLVM) to bind to
STATIC_RUNTIME_FLAGS = "-static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL"


def clang_major(command):
    """The major version of a clang on the PATH, None when it doesn't run"""
    try:
        version = subprocess.run([command, "-dumpversion"], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    match = re.match(r"(\d+)\.", version)
    if match is None:
        return None
    return int(match.group(1))


def find_clang():
    """(clang, clang++): clang++ if it's MIN_CLANG_MAJOR or later, else the newest clang++-N on the PATH that is"""
    versioned = set()
    for folder in os.environ.get("PATH", "").split(os.pathsep):
        for path in glob.glob(os.path.join(folder, "clang++-[0-9]*")):
            versioned.add(Path(path).name)
    candidates = ["clang++"] + sorted(versioned, key=lambda name: int(name.rsplit("-", 1)[1]), reverse=True)
    for cxx in candidates:
        major = clang_major(cxx)
        if major is not None and major >= MIN_CLANG_MAJOR:
            return cxx.replace("clang++", "clang"), cxx
    return None


def find_tool(name, package):
    found = shutil.which(name)
    if found:
        return found
    if name == "ninja":
        # requirements.txt's ninja, in the virtual environment
        venv = Path(sys.executable).parent / "ninja"
        if venv.exists():
            return str(venv)
    sys.exit(f"{name} isn't installed: sudo apt install {package} (or your distribution's)")


def download(url, target, sha256):
    if not target.exists():
        print(f"downloading {url}", flush=True)
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_suffix(target.suffix + ".part")
        with urllib.request.urlopen(url) as response, open(partial, "wb") as out:
            shutil.copyfileobj(response, out)
        partial.rename(target)
    actual = hashlib.sha256(target.read_bytes()).hexdigest()
    if actual != sha256:
        sys.exit(f"{target}'s SHA-256 is {actual}, not {sha256}")


def own_sdl(cmake, ninja, clang):
    """SDL3 built from its source release into SDL_PREFIX when it isn't there. Its video and audio backends' development
    packages have to be installed (native/LINUX.md lists them; its configure names a missing one); it loads their libraries at
    run time"""
    if (SDL_PREFIX / "lib" / "cmake" / "SDL3" / "SDL3Config.cmake").exists():
        return SDL_PREFIX
    archive = DEPS / f"SDL3-{SDL_VERSION}.tar.gz"
    download(SDL_URL, archive, SDL_SHA256)
    source = DEPS / f"SDL3-{SDL_VERSION}"
    if not source.exists():
        with tarfile.open(archive) as tar:
            tar.extractall(DEPS, filter="data")
    build = DEPS / f"SDL3-{SDL_VERSION}-build"
    subprocess.run([cmake, "-S", str(source), "-B", str(build), "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}",
                    f"-DCMAKE_C_COMPILER={clang}", "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={SDL_PREFIX}",
                    "-DCMAKE_INSTALL_LIBDIR=lib", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF"], check=True)
    subprocess.run([cmake, "--build", str(build), "--target", "install"], check=True)
    return SDL_PREFIX


def own_glslang(cmake, ninja, clang, clangxx):
    """glslang built from its source release into GLSLANG_PREFIX when it isn't there (static, no tools, no HLSL)"""
    if (GLSLANG_PREFIX / "lib" / "cmake" / "glslang" / "glslang-config.cmake").exists():
        return GLSLANG_PREFIX
    archive = DEPS / f"glslang-{GLSLANG_VERSION}.tar.gz"
    download(GLSLANG_URL, archive, GLSLANG_SHA256)
    source = DEPS / f"glslang-{GLSLANG_VERSION}"
    if not source.exists():
        with tarfile.open(archive) as tar:
            tar.extractall(DEPS, filter="data")
    build = DEPS / f"glslang-{GLSLANG_VERSION}-build-linux"
    print(f"building glslang {GLSLANG_VERSION}", flush=True)
    subprocess.run([cmake, "-S", str(source), "-B", str(build), "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}",
                    f"-DCMAKE_C_COMPILER={clang}", f"-DCMAKE_CXX_COMPILER={clangxx}", "-DCMAKE_BUILD_TYPE=Release",
                    f"-DCMAKE_INSTALL_PREFIX={GLSLANG_PREFIX}", "-DCMAKE_INSTALL_LIBDIR=lib", "-DBUILD_SHARED_LIBS=OFF",
                    "-DENABLE_OPT=OFF", "-DENABLE_HLSL=OFF", "-DENABLE_GLSLANG_BINARIES=OFF", "-DGLSLANG_TESTS=OFF",
                    "-DBUILD_EXTERNAL=OFF", "-DENABLE_SPVREMAPPER=OFF", "-DGLSLANG_ENABLE_INSTALL=ON",
                    f"-DPython3_EXECUTABLE={sys.executable}"], check=True)
    subprocess.run([cmake, "--build", str(build), "--target", "install"], check=True)
    return GLSLANG_PREFIX


def configured(name):
    """A variable of the build's CMake cache ("" before it was configured)"""
    cache = BUILD / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(errors="replace").splitlines():
            if line.startswith(name + ":"):
                return line.split("=", 1)[1]
    return ""


def own_ffmpeg(clang):
    """Our decode-only LGPL 2.1 FFmpeg (native/tools/build_ffmpeg.sh), built when it isn't there; CMake finds it in FFMPEG"""
    if (FFMPEG / "lib" / "pkgconfig" / "libavcodec.pc").exists():
        return
    for tool, package in (("make", "make"), ("curl", "curl")):
        find_tool(tool, package)
    if shutil.which("nasm") is None:
        print("no nasm: FFmpeg's x86 assembly will be off (sudo apt install nasm)", flush=True)
    env = dict(os.environ)
    env["CC"] = clang
    print("building FFmpeg (native/tools/build_ffmpeg.sh)", flush=True)
    subprocess.run(["sh", str(HERE / "native" / "tools" / "build_ffmpeg.sh")], cwd=HERE, env=env, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--configure", action="store_true", help="configure again")
    parser.add_argument("--clean", action="store_true", help="delete the native build first")
    parser.add_argument("--sdl", help="an SDL3 3.4 or later install prefix (lib/cmake/SDL3), else built")
    parser.add_argument("ninja", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    if platform.machine() != "x86_64":
        sys.exit(f"{platform.machine()}: the Linux build is x86-64 only (the low memory's emulation, native/NATIVE.md)")
    if not (HERE / "asm" / "data").exists():
        sys.exit("No split: run tools/split.py first (README.md)")

    found = find_clang()
    if found is None:
        sys.exit(f"No clang {MIN_CLANG_MAJOR} or later on the PATH: install it (Debian 13: sudo apt install -t trixie-backports "
                 f"clang-{MIN_CLANG_MAJOR}), native/LINUX.md")
    clang, clangxx = found
    cmake = find_tool("cmake", "cmake")
    ninja = find_tool("ninja", "ninja-build")
    if args.sdl:
        sdl = Path(args.sdl)
    else:
        sdl = own_sdl(cmake, ninja, clang)
    glslang = own_glslang(cmake, ninja, clang, clangxx)
    own_ffmpeg(clang)

    if args.clean and BUILD.exists():
        shutil.rmtree(BUILD)
    # (A build configured before glslang was there, 1.2.0's, configured again to have the Vulkan backend)
    without_glslang = str(glslang) not in configured("CMAKE_PREFIX_PATH")
    if args.configure or without_glslang or not (BUILD / "build.ninja").exists():
        subprocess.run([cmake, "-S", str(HERE / "native"), "-B", str(BUILD), "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}",
                        f"-DCMAKE_CXX_COMPILER={clangxx}", f"-DCMAKE_PREFIX_PATH={sdl};{glslang}",
                        f"-DCMAKE_EXE_LINKER_FLAGS={STATIC_RUNTIME_FLAGS}", f"-DPython3_EXECUTABLE={sys.executable}"],
                       check=True)
    result = subprocess.run([cmake, "--build", str(BUILD), "--"] + args.ninja)
    if result.returncode == 0:
        print(f"built {BUILD / 'twinsanity'}")
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
