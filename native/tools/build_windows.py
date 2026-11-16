#!/usr/bin/env python3
"""The native build on Windows (native/WINDOWS.md): finds LLVM-MinGW's clang, CMake and Ninja (winget installs them), fetches
SDL3's MinGW development release and an LGPL shared FFmpeg into build/native/deps when they aren't there, then configures and
builds build/native/cmake/CrashTwinsanity.exe.

    .venv\\Scripts\\python.exe native\\tools\\build_windows.py [--configure] [--clean] [ninja arguments...]

The split (tools/split.py) has to have been made first: the build converts its data.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]
DEPS = HERE / "build" / "native" / "deps"
BUILD = HERE / "build" / "native" / "cmake"
SDL_VERSION = "3.4.18"
SDL_URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/SDL3-devel-{SDL_VERSION}-mingw.tar.gz"
FFMPEG_NAME = "ffmpeg-n9.0-latest-win64-lgpl-shared-9.0"
FFMPEG_URL = f"https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/{FFMPEG_NAME}.zip"
MSYS2_REPO = "https://repo.msys2.org/msys/x86_64/"
# glslang (BSD 3-Clause and others, its LICENSE.txt): the Vulkan backend makes the renderer's GLSL SPIR-V with it while the game
# runs (THE LIST 71). A static library built here once, the release's source checked by its SHA-256
GLSLANG_VERSION = "16.6.0"
GLSLANG_URL = f"https://github.com/KhronosGroup/glslang/archive/refs/tags/{GLSLANG_VERSION}.tar.gz"
GLSLANG_SHA256 = "9c09b901149c729df745057dafa815278aaa101b84d2b6e14f16a42de52f97f2"


def find_llvm_mingw():
    """LLVM-MinGW's bin folder: on the PATH, $LLVM_MINGW, or where winget puts it"""
    if os.environ.get("LLVM_MINGW"):
        return Path(os.environ["LLVM_MINGW"]) / "bin"
    found = shutil.which("x86_64-w64-mingw32-clang++")
    if found:
        return Path(found).parent
    packages = Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "WinGet" / "Packages"
    for candidate in sorted(glob.glob(str(packages / "MartinStorsjo.LLVM-MinGW.UCRT_*" / "llvm-mingw-*" / "bin")), reverse=True):
        return Path(candidate)
    return None


def configured(name):
    """A variable of the build's CMake cache ("" before it was configured)"""
    cache = BUILD / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(errors="replace").splitlines():
            if line.startswith(name + ":"):
                return line.split("=", 1)[1]
    return ""


def configured_compiler():
    """The C++ compiler the build was configured with ("" before it was)"""
    return configured("CMAKE_CXX_COMPILER")


def find_tool(name, winget_id):
    found = shutil.which(name)
    if found:
        return found
    for candidate in (Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "CMake" / "bin" / f"{name}.exe",
                      Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "WinGet" / "Links" / f"{name}.exe"):
        if candidate.exists():
            return str(candidate)
    if name == "ninja":
        # requirements.txt's ninja, in the virtual environment
        venv = Path(sys.executable).parent / "ninja.exe"
        if venv.exists():
            return str(venv)
    sys.exit(f"{name} isn't installed: winget install --id {winget_id}")


def download(url, target):
    if target.exists():
        return
    print(f"downloading {url}")
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_suffix(target.suffix + ".part")
    with urllib.request.urlopen(url) as response, open(partial, "wb") as out:
        shutil.copyfileobj(response, out)
    partial.rename(target)


def git_bash():
    """Git for Windows' bash (not System32's, which is WSL's)"""
    git = shutil.which("git")
    candidates = [Path(git).resolve().parent.parent / "bin" / "bash.exe"] if git else []
    candidates.append(Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git" / "bin" / "bash.exe")
    return next((c for c in candidates if c.exists()), None)


def msys_make():
    """GNU make for MSYS (FFmpeg's makefiles want its paths), from MSYS2's package repository"""
    folder = DEPS / "msys-make"
    if (folder / "make.exe").exists():
        return folder
    import io
    import re
    import zstandard
    index = urllib.request.urlopen(MSYS2_REPO).read().decode("utf-8", "replace")
    names = sorted(set(re.findall(r'"(make-[0-9][^"]*-x86_64\.pkg\.tar\.zst)"', index)))
    if not names:
        raise RuntimeError("no make package in MSYS2's repository")
    archive = DEPS / names[-1]
    download(MSYS2_REPO + names[-1], archive)
    raw = zstandard.ZstdDecompressor().decompressobj().decompress(archive.read_bytes())
    with tarfile.open(fileobj=io.BytesIO(raw)) as tar:
        member = tar.getmember("usr/bin/make.exe")
        folder.mkdir(parents=True, exist_ok=True)
        (folder / "make.exe").write_bytes(tar.extractfile(member).read())
    return folder


def own_ffmpeg(llvm):
    """Our decode-only LGPL 2.1 FFmpeg (native/tools/build_ffmpeg.sh), built when it isn't there: None if it can't be"""
    prefix = HERE / "build" / "native" / "ffmpeg" / "windows-x86_64"
    if (prefix / "include" / "libavcodec" / "avcodec.h").exists():
        return prefix
    bash = git_bash()
    if bash is None:
        return None
    try:
        make = msys_make()
    except Exception as error:  # noqa: BLE001 (any failure: the prebuilt one instead)
        print(f"no MSYS make ({error}): FFmpeg's prebuilt release instead")
        return None
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([str(llvm), str(make), env.get("PATH", "")])
    print("building FFmpeg (native/tools/build_ffmpeg.sh)")
    result = subprocess.run([str(bash), "native/tools/build_ffmpeg.sh"], cwd=HERE, env=env)
    return prefix if result.returncode == 0 else None


def own_glslang(llvm, cmake, ninja):
    """glslang built as static libraries (build/native/glslang/windows-x86_64, CMake's package there): None if it can't be"""
    import hashlib
    prefix = HERE / "build" / "native" / "glslang" / "windows-x86_64"
    if (prefix / "lib" / "cmake" / "glslang" / "glslang-config.cmake").exists():
        return prefix
    archive = DEPS / f"glslang-{GLSLANG_VERSION}.tar.gz"
    try:
        download(GLSLANG_URL, archive)
    except Exception as error:  # noqa: BLE001 (no network: built without Vulkan)
        print(f"no glslang ({error}): built without Vulkan")
        return None
    if hashlib.sha256(archive.read_bytes()).hexdigest() != GLSLANG_SHA256:
        print(f"{archive} isn't glslang {GLSLANG_VERSION}'s release: built without Vulkan")
        return None
    source = DEPS / f"glslang-{GLSLANG_VERSION}"
    if not source.exists():
        with tarfile.open(archive) as tar:
            tar.extractall(DEPS, filter="data")
    build = DEPS / f"glslang-{GLSLANG_VERSION}-build"
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([str(llvm), str(Path(ninja).parent), env.get("PATH", "")])
    print(f"building glslang {GLSLANG_VERSION}")
    configure = [cmake, "-S", str(source), "-B", str(build), "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={Path(ninja).as_posix()}",
                 f"-DCMAKE_C_COMPILER={(llvm / 'clang.exe').as_posix()}", f"-DCMAKE_CXX_COMPILER={(llvm / 'clang++.exe').as_posix()}",
                 "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}", "-DBUILD_SHARED_LIBS=OFF",
                 "-DENABLE_OPT=OFF", "-DENABLE_HLSL=OFF", "-DENABLE_GLSLANG_BINARIES=OFF", "-DGLSLANG_TESTS=OFF",
                 "-DBUILD_EXTERNAL=OFF", "-DENABLE_SPVREMAPPER=OFF", "-DGLSLANG_ENABLE_INSTALL=ON",
                 f"-DPython3_EXECUTABLE={Path(sys.executable).as_posix()}"]
    if subprocess.run(configure, env=env).returncode != 0 or             subprocess.run([cmake, "--build", str(build), "--target", "install"], env=env).returncode != 0:
        print("glslang didn't build: built without Vulkan")
        return None
    return prefix


def fetch_dependencies(llvm):
    sdl = DEPS / f"SDL3-{SDL_VERSION}" / "x86_64-w64-mingw32"
    if not sdl.exists():
        archive = DEPS / f"SDL3-devel-{SDL_VERSION}-mingw.tar.gz"
        download(SDL_URL, archive)
        with tarfile.open(archive) as tar:
            tar.extractall(DEPS, filter="data")
    ffmpeg = own_ffmpeg(llvm)
    if ffmpeg is not None:
        return sdl, ffmpeg
    # A prebuilt LGPL shared FFmpeg (every decoder: bigger)
    ffmpeg = DEPS / FFMPEG_NAME
    if not ffmpeg.exists():
        archive = DEPS / f"{FFMPEG_NAME}.zip"
        download(FFMPEG_URL, archive)
        with zipfile.ZipFile(archive) as z:
            z.extractall(DEPS)
    return sdl, ffmpeg


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--configure", action="store_true", help="configure again")
    parser.add_argument("--clean", action="store_true", help="delete the native build first")
    parser.add_argument("--sdl", help="SDL3's MinGW folder (x86_64-w64-mingw32), else fetched")
    parser.add_argument("--ffmpeg", help="an LGPL shared FFmpeg build's folder, else fetched")
    parser.add_argument("ninja", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    if not (HERE / "asm" / "data").exists():
        sys.exit("No split: run tools/split.py first (README.md)")

    llvm = find_llvm_mingw()
    if llvm is None:
        sys.exit("LLVM-MinGW isn't installed: winget install --id MartinStorsjo.LLVM-MinGW.UCRT (or set LLVM_MINGW)")
    cmake = find_tool("cmake", "Kitware.CMake")
    ninja = find_tool("ninja", "Ninja-build.Ninja")
    sdl, ffmpeg = fetch_dependencies(llvm)
    glslang = own_glslang(llvm, cmake, ninja)
    sdl = Path(args.sdl) if args.sdl else sdl
    ffmpeg = Path(args.ffmpeg) if args.ffmpeg else ffmpeg

    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([str(llvm), str(Path(ninja).parent), env.get("PATH", "")])
    if args.clean and BUILD.exists():
        shutil.rmtree(BUILD)
    compiler = (llvm / "clang++.exe").as_posix()
    # A build configured with another LLVM-MinGW is configured from scratch: its cache keeps that one's tools (the resource
    # compiler, the archiver), and winget's upgrades delete the version before
    fresh = configured_compiler() not in ("", compiler)
    # (A build configured before glslang was there, 1.2.0's, configured again to have the Vulkan backend)
    without_glslang = glslang is not None and glslang.as_posix() not in configured("CMAKE_PREFIX_PATH")
    if args.configure or fresh or without_glslang or not (BUILD / "build.ninja").exists():
        subprocess.run([cmake] + (["--fresh"] if fresh else []) + ["-S", str(HERE / "native"), "-B", str(BUILD), "-G", "Ninja",
                        f"-DCMAKE_MAKE_PROGRAM={Path(ninja).as_posix()}",
                        f"-DCMAKE_CXX_COMPILER={compiler}",
                        f"-DCMAKE_PREFIX_PATH={sdl.as_posix()}" + (f";{glslang.as_posix()}" if glslang else ""),
                        f"-DTWIN_FFMPEG_DIR={ffmpeg.as_posix()}",
                        f"-DPython3_EXECUTABLE={Path(sys.executable).as_posix()}"], check=True, env=env)
    result = subprocess.run([cmake, "--build", str(BUILD), "--"] + args.ninja, env=env)
    if result.returncode == 0:
        print(f"built {BUILD / 'CrashTwinsanity.exe'}")
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
