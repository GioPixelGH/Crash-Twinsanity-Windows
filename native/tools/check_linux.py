#!/usr/bin/env python3
"""The AppImage's checks (native/LINUX.md), run on Linux: in the Docker image native/tools/linux.Dockerfile describes, where
release_windows.py check runs them (build_linux_docker.py --check). A line a check; the exit status 1 when one fails.

    python3 native/tools/check_linux.py APPIMAGE --iso DISC_IMAGE --work FOLDER [--version X.Y.Z] [--program-out FILE]

- version: the AppImage's desktop entry's X-AppImage-Version and the program's own (the log's first line) against --version;
- libraries: the AppImage holds SDL3, libavcodec and libavutil, and the program loads nothing else but what every Linux has
  (package_linux.py's SYSTEM_LIBRARIES): checked on the files extracted (--appimage-extract);
- glibc: the newest version of the C library its files need, which says on which systems it runs;
- licences: the licences it carries (LICENCES), the MIT code's in the program among them;
- start: the AppImage run as a player runs it (extracting itself, FUSE isn't there in a container) with a settings folder of its
  own in FOLDER, headless on Xvfb with Mesa's software OpenGL, the disc image given: the beach played for 20 seconds, no fatal
  error, the log saying it runs from the AppImage.
--program-out copies the program out (release_windows.py looks for the game's bytes in it).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import package_linux  # noqa: E402

BUNDLED = re.compile(r"^(libSDL3\.so\.0|libavcodec\.so\.\d+|libavutil\.so\.\d+)$")
# The licences the AppImage must carry: the port's, the bundled libraries' and fonts', and the MIT code's built into the program
# (SMAA's, TwinsanityEvolved's: menaman123's permission asks for his licence beside the program), as the zip's licenses folder
LICENCES = ("Crash-Twinsanity-Windows-LICENSE.txt", "SDL3-LICENSE.txt", "ffmpeg", "stb-LICENSE.txt",
            "fonts-LICENSE-LuckiestGuy.txt", "fonts-OFL-Fredoka.txt", "SMAA-LICENSE.txt",
            "twinsanityevolved-LICENSE-TwinsanityEvolved.txt", "twinsanityevolved-LICENSE-NativeEE.txt")
# The beach (its chunk as the sweep names it) played for this long, drawn at 1x (Mesa on the processor); the run's own limit
# (the sweep's) and the process's
LEVEL = "Levels\\Earth\\Hub\\Beach"
SECONDS = 20
TIMEOUT = 300
DISPLAY = ":77"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("appimage")
    parser.add_argument("--iso", required=True, help="the PAL disc image")
    parser.add_argument("--work", required=True, help="a folder for the extracted files, the settings and the logs")
    parser.add_argument("--version", help="the version it must be (native/CMakeLists.txt's)")
    parser.add_argument("--program-out", help="where to copy the program")
    arguments = parser.parse_args()
    appimage = Path(arguments.appimage).resolve()
    work = Path(arguments.work).resolve()
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    failed = False

    # The files inside, as the AppImage unpacks them
    subprocess.run([str(appimage), "--appimage-extract"], cwd=work, capture_output=True, check=True)
    root = work / "squashfs-root"
    program = root / "usr" / "bin" / package_linux.NAME
    desktop = (root / f"{package_linux.NAME}.desktop").read_text()
    declared = re.search(r"^X-AppImage-Version=(.+)$", desktop, re.M)
    declared = declared.group(1).strip() if declared else "none"

    libraries = sorted(path.name for path in (root / "usr" / "lib").iterdir())
    stray = [name for name in libraries if not BUNDLED.match(name)]
    patchelf = shutil.which("patchelf")
    loaded = set()
    for binary in [program] + [root / "usr" / "lib" / name for name in libraries]:
        loaded.update(package_linux.needed(patchelf, binary))
    outside = sorted(name for name in loaded - set(libraries) if name not in package_linux.SYSTEM_LIBRARIES)
    print(f"{'libraries':15} {', '.join(libraries)}{'' if not stray and not outside else ': FAILED, also ' + ', '.join(stray + outside)}")
    failed |= bool(stray or outside)
    glibc = package_linux.needed_glibc([program] + [root / "usr" / "lib" / name for name in libraries])
    print(f"{'glibc':15} {glibc[0]}.{glibc[1]} or later")
    folder = root / "usr" / "share" / "licenses" / package_linux.NAME
    missing = [name for name in LICENCES if not (folder / name).exists()]
    print(f"{'licences':15} {len(LICENCES) - len(missing)} of {len(LICENCES)}"
          f"{': FAILED, missing ' + ', '.join(missing) if missing else ''}")
    failed |= bool(missing)

    # A player's start: the settings folder a new one, the disc image given, the beach played headless
    home = work / "home"
    (home / ".config").mkdir(parents=True)
    xvfb = subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1280x800x24", "-nolisten", "tcp"], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    time.sleep(2)
    env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(home / ".config"), DISPLAY=DISPLAY, TWINSANITY_AUDIO="off",
               APPIMAGE_EXTRACT_AND_RUN="1", TWIN_SWEEP_LEVEL=LEVEL, TWIN_SWEEP_SECONDS=str(SECONDS), TWIN_SCALE="1",
               TWIN_SWEEP_TIMEOUT="180", LIBGL_ALWAYS_SOFTWARE="1")
    env.pop("TWIN_STEAM_DECK", None)
    log = work / "start.txt"
    try:
        with open(log, "w") as out:
            status = subprocess.run([str(appimage), "--headless", "--quiet-stubs", "--iso", arguments.iso], env=env, stdout=out,
                                    stderr=subprocess.STDOUT, cwd=work, timeout=TIMEOUT).returncode
    except subprocess.TimeoutExpired:
        status = "timeout"
    finally:
        xvfb.terminate()
    text = log.read_text(errors="replace")
    own = re.search(r"Crash Twinsanity (\d+\.\d+\.\d+)", text)
    own = own.group(1) if own else "none"
    wanted = arguments.version or own
    print(f"{'version':15} {own} (the desktop entry's {declared})"
          f"{'' if own == wanted and declared == wanted else ': FAILED, it should be ' + wanted}")
    failed |= own != wanted or declared != wanted
    played = re.search(re.escape(f"sweep: {LEVEL}: ran (") + r"([\d.]+) s played, (\d+) frames", text)
    fatal = [line for line in text.splitlines() if line.startswith("[native] fatal")]
    system = re.search(r"system: (.*)", text)
    appimaged = system is not None and "from the AppImage" in system.group(1)
    ok = status == 0 and played is not None and not fatal and appimaged
    if ok:
        print(f"{'start':15} the beach played {played.group(1)} s ({played.group(2)} frames) from the AppImage "
              f"({system.group(1)})")
    else:
        print(f"{'start':15} FAILED: exit {status}, {fatal[0] if fatal else 'played' if played else 'not played'}"
              f"{'' if appimaged else ', no from the AppImage in the system line'}: see {log}")
    failed |= not ok

    if arguments.program_out:
        shutil.copy2(program, arguments.program_out)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
