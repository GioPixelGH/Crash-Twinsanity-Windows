#!/usr/bin/env python3
"""The Linux release (native/LINUX.md): the program built by native/tools/build_linux.py as an AppImage,
build/native/dist/Crash-Twinsanity-Linux-x86_64.AppImage, one file the player marks executable and runs.

    .venv/bin/python native/tools/package_linux.py [--build build/native/cmake]

The AppImage holds the program, the libraries it loads from build/native (SDL3, FFmpeg's libavcodec and libavutil), the UI's
fonts and the licences. The rest comes from the player's system: the C library, OpenGL (their graphics driver), and what SDL
loads at run time (X11 or Wayland, PipeWire or PulseAudio). Nothing of the game's: the player picks their own disc image.
appimagetool and its runtime are downloaded into build/native/deps at the versions below; patchelf has to be installed.

TRADEOFF: the AppImage needs the building system's glibc or a newer one (it prints which); build on the oldest system it
should run on (a container of an older distribution) to reach more of them.
"""
import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]
NAME = "crash-twinsanity"
APPIMAGETOOL_VERSION = "1.9.1"
APPIMAGETOOL_URL = (f"https://github.com/AppImage/appimagetool/releases/download/{APPIMAGETOOL_VERSION}/"
                    "appimagetool-x86_64.AppImage")
APPIMAGETOOL_SHA256 = "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
RUNTIME_VERSION = "20251108"
RUNTIME_URL = f"https://github.com/AppImage/type2-runtime/releases/download/{RUNTIME_VERSION}/runtime-x86_64"
RUNTIME_SHA256 = "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"
# Libraries every desktop Linux has, left to the system: the C library's and OpenGL's (the graphics driver's own)
SYSTEM_LIBRARIES = {"linux-vdso.so.1", "ld-linux-x86-64.so.2", "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0",
                    "librt.so.1", "libGL.so.1", "libGLX.so.0", "libOpenGL.so.0", "libGLdispatch.so.0", "libEGL.so.1"}
ICO_HEADER = struct.Struct("<HHH")
ICO_ENTRY = struct.Struct("<BBBBHHII")
# An ICO entry's width or height of 0 means 256
ICO_FULL_SIZE = 256
PNG_SIGNATURE = b"\x89PNG"
BYTES_PER_MB = 1024 * 1024
# rwxr-xr-x: the downloaded tools run
EXECUTABLE_MODE = 0o755
# X-AppImage-Version: the port's version (native/CMakeLists.txt's), which release_windows.py checks
DESKTOP_ENTRY = """[Desktop Entry]
Type=Application
Name=Crash Twinsanity
Comment=Crash Twinsanity (PS2, PAL) running natively, from your own disc image
Exec={name}
Icon={name}
Categories=Game;ActionGame;
Terminal=false
X-AppImage-Version={version}
"""


def version():
    """native/CMakeLists.txt's project version"""
    found = re.search(r"project\(\S+ VERSION (\S+)", (HERE / "native" / "CMakeLists.txt").read_text())
    if found is None:
        sys.exit("native/CMakeLists.txt has no project version")
    return found.group(1)


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
    target.chmod(EXECUTABLE_MODE)


def libraries(binary):
    """{name: resolved path} of every library a binary loads (ldd: the dependencies' too), None for one with no file"""
    found = {}
    for line in subprocess.run(["ldd", str(binary)], capture_output=True, text=True, check=True).stdout.splitlines():
        match = re.match(r"\s*(\S+)(?: => (.+?))? \(0x[0-9a-f]+\)$", line)
        if match is not None:
            # "name => path", or the dynamic loader by its path alone ("/lib64/ld-linux-x86-64.so.2"), or the vDSO (no file)
            if match.group(2) is not None:
                path = Path(match.group(2)).resolve()
            elif match.group(1).startswith("/"):
                path = Path(match.group(1)).resolve()
            else:
                path = None
            found[Path(match.group(1)).name] = path
        elif "not found" in line:
            found[line.split()[0]] = None
        else:
            print(f"ldd: {line.strip()} (not understood, left out)", file=sys.stderr)
    return found


def needed(patchelf, binary):
    """The libraries a binary names itself (DT_NEEDED)"""
    return subprocess.run([patchelf, "--print-needed", str(binary)], capture_output=True, text=True, check=True).stdout.split()


def own_libraries(patchelf, program, own):
    """{name: path} of the libraries the program loads from the own folder, directly or through each other. Exits on one that is
    neither there nor one of SYSTEM_LIBRARIES (the system's libraries' own dependencies are theirs)"""
    resolved = libraries(program)
    found = {}
    queue = [program]
    while queue:
        binary = queue.pop()
        for name in needed(patchelf, binary):
            path = resolved.get(name)
            if path is None:
                sys.exit(f"{binary.name} loads {name}, which isn't found")
            elif path.is_relative_to(own):
                if name not in found:
                    found[name] = path
                    queue.append(path)
            elif name in SYSTEM_LIBRARIES:
                continue
            elif name.startswith("libstdc++"):
                sys.exit(f"{binary.name} loads the system's {name}: build with native/tools/build_linux.py, which links it in")
            else:
                sys.exit(f"{binary.name} loads {name} ({path}), which isn't built here nor one every Linux has")
    return found


def largest_png(ico):
    """The biggest picture of an ICO whose pictures are PNGs (native/icon/AppIcon.ico's are)"""
    data = ico.read_bytes()
    _, _, count = ICO_HEADER.unpack_from(data)
    best = None
    for index in range(count):
        width, _, _, _, _, _, size, offset = ICO_ENTRY.unpack_from(data, ICO_HEADER.size + ICO_ENTRY.size * index)
        width = width or ICO_FULL_SIZE
        picture = data[offset:offset + size]
        if picture.startswith(PNG_SIGNATURE) and (best is None or width > best[0]):
            best = (width, picture)
    if best is None:
        sys.exit(f"{ico} has no PNG picture")
    return best[1]


def needed_glibc(binaries):
    """The newest GLIBC_x.y symbol version the binaries need (objdump -T), as (x, y)"""
    newest = (0, 0)
    for binary in binaries:
        symbols = subprocess.run(["objdump", "-T", str(binary)], capture_output=True, text=True, check=True).stdout
        for major, minor in re.findall(r"GLIBC_(\d+)\.(\d+)", symbols):
            newest = max(newest, (int(major), int(minor)))
    return newest


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", default=str(HERE / "build" / "native" / "cmake"))
    parser.add_argument("--deps", default=str(HERE / "build" / "native" / "deps"),
                        help="where build_linux.py put SDL3 (for its licence) and where the AppImage tools go")
    arguments = parser.parse_args()
    build = Path(arguments.build)
    deps = Path(arguments.deps)
    exe = build / "twinsanity"
    if not exe.exists():
        sys.exit(f"{exe} isn't built: native/tools/build_linux.py")
    patchelf = shutil.which("patchelf")
    if patchelf is None:
        sys.exit("patchelf isn't installed: sudo apt install patchelf (or your distribution's)")

    # What goes in: the libraries built in build/native; the system's only when every Linux has them
    bundled = own_libraries(patchelf, exe, HERE / "build" / "native")

    dist = HERE / "build" / "native" / "dist"
    appdir = dist / "AppDir"
    if appdir.exists():
        shutil.rmtree(appdir)
    bin_folder = appdir / "usr" / "bin"
    lib_folder = appdir / "usr" / "lib"
    bin_folder.mkdir(parents=True)
    lib_folder.mkdir(parents=True)

    program = bin_folder / NAME
    shutil.copy2(exe, program)
    subprocess.run([patchelf, "--set-rpath", "$ORIGIN/../lib", str(program)], check=True)
    for name, path in sorted(bundled.items()):
        target = lib_folder / name
        shutil.copy2(path, target)
        subprocess.run([patchelf, "--set-rpath", "$ORIGIN", str(target)], check=True)
    (appdir / "AppRun").symlink_to(f"usr/bin/{NAME}")

    # The UI's fonts beside the program, where overlay.cpp looks (SDL_GetBasePath's fonts/)
    fonts = bin_folder / "fonts"
    fonts.mkdir()
    for name in ("LuckiestGuy-Regular.ttf", "Fredoka-SemiBold.ttf", "Fredoka-Bold.ttf"):
        shutil.copy2(HERE / "native" / "third_party" / "fonts" / name, fonts / name)

    (appdir / f"{NAME}.desktop").write_text(DESKTOP_ENTRY.format(name=NAME, version=version()))
    (appdir / f"{NAME}.png").write_bytes(largest_png(HERE / "native" / "icon" / "AppIcon.ico"))

    licenses = appdir / "usr" / "share" / "licenses" / NAME
    licenses.mkdir(parents=True)
    for name in ("LICENSE-LuckiestGuy.txt", "OFL-Fredoka.txt", "README.md"):
        shutil.copy2(HERE / "native" / "third_party" / "fonts" / name, licenses / f"fonts-{name}")
    shutil.copy2(HERE / "native" / "third_party" / "stb" / "LICENSE.txt", licenses / "stb-LICENSE.txt")
    # SMAA (iryoku's, MIT) and TwinsanityEvolved's MIT code (its README says which files), as package_windows.py
    shutil.copy2(HERE / "native" / "third_party" / "smaa" / "LICENSE.txt", licenses / "SMAA-LICENSE.txt")
    for name in ("LICENSE-TwinsanityEvolved.txt", "LICENSE-NativeEE.txt", "README.md"):
        shutil.copy2(HERE / "native" / "third_party" / "twinsanityevolved" / name, licenses / f"twinsanityevolved-{name}")
    # The port's own licence (what it covers is in it)
    shutil.copy2(HERE / "LICENSE", licenses / "Crash-Twinsanity-Windows-LICENSE.txt")
    found = sorted(deps.glob("SDL3-*/LICENSE.txt"))
    if found:
        shutil.copy2(found[-1], licenses / "SDL3-LICENSE.txt")
    else:
        print(f"warning: no SDL3-*/LICENSE.txt in {deps}", file=sys.stderr)
    # FFmpeg's licence and how to replace its libraries (the LGPL's relinking clause: extract the AppImage, swap usr/lib's)
    ffmpeg = licenses / "ffmpeg"
    ffmpeg.mkdir()
    for path in (HERE / "native" / "third_party" / "ffmpeg").iterdir():
        shutil.copy2(path, ffmpeg / path.name)

    # Check: the program loads those from inside the AppDir now, and nothing else but SYSTEM_LIBRARIES
    inside = own_libraries(patchelf, program, appdir.resolve())
    if sorted(inside) != sorted(bundled):
        sys.exit(f"in the AppDir the program loads {sorted(inside)} of its own, not {sorted(bundled)}")

    appimagetool = deps / f"appimagetool-{APPIMAGETOOL_VERSION}-x86_64.AppImage"
    runtime = deps / f"appimage-runtime-{RUNTIME_VERSION}-x86_64"
    download(APPIMAGETOOL_URL, appimagetool, APPIMAGETOOL_SHA256)
    download(RUNTIME_URL, runtime, RUNTIME_SHA256)
    output = dist / "Crash-Twinsanity-Linux-x86_64.AppImage"
    if output.exists():
        output.unlink()
    env = dict(os.environ)
    # Run from its own extraction: no FUSE needed to build
    env["APPIMAGE_EXTRACT_AND_RUN"] = "1"
    env["ARCH"] = "x86_64"
    subprocess.run([str(appimagetool), "--no-appstream", "--runtime-file", str(runtime), str(appdir), str(output)], check=True,
                   env=env)

    glibc = needed_glibc([program, *(lib_folder / name for name in bundled)])
    size = output.stat().st_size / BYTES_PER_MB
    print(f"{output} ({size:.0f} MB; bundled: {', '.join(sorted(bundled))}; needs glibc {glibc[0]}.{glibc[1]} or later)")


if __name__ == "__main__":
    main()
