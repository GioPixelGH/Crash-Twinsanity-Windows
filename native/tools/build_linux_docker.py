#!/usr/bin/env python3
"""The Linux program and its AppImage made in Docker (native/LINUX.md, "In Docker"), from any computer with Docker: Windows
too, where the Windows release is made. Docker has to be running.

    python native/tools/build_linux_docker.py [--configure] [--clean] [--out FOLDER] [--volume NAME]
                                              [--check --iso DISC_IMAGE [--version X.Y.Z]]

It builds the image native/tools/linux.Dockerfile describes (Ubuntu 24.04, clang 21, SDL's backends, the PS2 toolchain), copies
this source tree into a Docker volume of its own (sent as a tar on the container's input, then synced there, so only what changed
is written; the build's folders, the virtual environment, git's folder, texture packs and local.json left out; a file gone from
the tree goes from the copy too), and runs native/tools/build_linux.py and package_linux.py there. The volume keeps the build between runs (SDL3, FFmpeg and the objects made again only when changed).
The AppImage comes out as FOLDER/Crash-Twinsanity-Linux-x86_64.AppImage (build/native/linux by default), the container's
output in FOLDER/build.txt; a summary line a step.

--check then runs native/tools/check_linux.py on the AppImage in the same image (the disc image mounted read-only): its lines
printed, the program copied out as FOLDER/crash-twinsanity (release_windows.py looks for the game's bytes in it), the checks'
files in FOLDER/check.

The split (tools/split.py) has to be in the tree, as for the Windows build: the copy takes asm/ and SLES_525.68 with it. They
stay in the volume, on this computer.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import tarfile
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]
IMAGE = "crash-twinsanity-linux-build"
DOCKERFILE = HERE / "native" / "tools" / "linux.Dockerfile"
APPIMAGE = "Crash-Twinsanity-Linux-x86_64.AppImage"
# Not copied: what each system makes for itself (the builds, the PS2 build's ninja files, FFmpeg's sources as the Windows
# build unpacked them: their scripts lose their x bit in the tar, and configure was refused in 1.2.0's first release check;
# the Linux build downloads and checks its own), git's own folder, what's this computer's (local.json's paths, texture packs,
# editors' folders); at the tree's top, at these paths, and anywhere. The split (SLES_525.68, asm/) is copied: the build needs it
EXCLUDED_TOP = {"build", ".venv", ".git", "textures", "ghidra", "local.json", "build.ninja", ".ninja_log", ".ninja_deps",
                ".vscode", ".idea"}
EXCLUDED_PATHS = {Path("native/third_party/ffmpeg-src")}
EXCLUDED_ANYWHERE = {"__pycache__", ".cache"}
# rw-r--r--, and rwxr-xr-x for the scripts (Windows has no such bits to give)
FILE_MODE = 0o644
SCRIPT_MODE = 0o755
# In the container: the tar unpacked beside the copy, then synced into it (what's the same keeps its time, so ninja rebuilds only
# what changed; what's gone goes; the copy's own build, virtual environment and FFmpeg download stay)
RECEIVE = ("set -e; rm -rf /work/incoming; mkdir -p /work/incoming /work/tree; tar -xf - -C /work/incoming; "
           "rsync -a --delete --exclude=/build/ --exclude=/.venv/ --exclude=/native/third_party/ffmpeg-src/ "
           "/work/incoming/ /work/tree/; rm -rf /work/incoming")


def run(command, log, step):
    """A step's command, its output appended to the log; exits with the step's name when it fails"""
    with open(log, "a", encoding="utf-8", errors="replace") as out:
        out.write(f"\n===== {step}: {' '.join(command)}\n")
        out.flush()
        result = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        sys.exit(f"{step:15} FAILED: see {log}")


def tree_files():
    """The tree's files to copy, as paths relative to it"""
    for folder, folders, files in os.walk(HERE):
        relative = Path(folder).relative_to(HERE)
        folders[:] = sorted(name for name in folders if name not in EXCLUDED_ANYWHERE
                            and not (relative == Path(".") and name in EXCLUDED_TOP) and relative / name not in EXCLUDED_PATHS)
        for name in sorted(files):
            if (relative == Path(".") and name in EXCLUDED_TOP) or name.endswith(".pyc"):
                continue
            yield relative / name


def send_tree(volume, log):
    """The tree into the volume's /work/tree: a tar of it on the container's input (much quicker than reading Windows' files
    through a mount), unpacked and synced there"""
    def owned(info):
        info.uid = info.gid = 0
        info.uname = info.gname = "root"
        info.mode = SCRIPT_MODE if info.name.endswith((".sh", ".py")) else FILE_MODE
        return info

    with open(log, "a", encoding="utf-8", errors="replace") as out:
        out.write("\n===== tree: a tar of the tree into the volume\n")
        out.flush()
        process = subprocess.Popen(["docker", "run", "--rm", "-i", "-v", f"{volume}:/work", IMAGE, "bash", "-c", RECEIVE],
                                   stdin=subprocess.PIPE, stdout=out, stderr=subprocess.STDOUT)
        count = 0
        with tarfile.open(fileobj=process.stdin, mode="w|", format=tarfile.PAX_FORMAT) as tar:
            for relative in tree_files():
                tar.add(HERE / relative, arcname=relative.as_posix(), recursive=False, filter=owned)
                count += 1
        process.stdin.close()
        if process.wait() != 0:
            sys.exit(f"{'tree':15} FAILED: see {log}")
    return count


def default_volume():
    """A volume for each tree (the working tree and the release's clone don't share their builds)"""
    digest = hashlib.sha1(str(HERE).lower().encode()).hexdigest()[:8]
    return f"crash-twinsanity-linux-{re.sub(r'[^a-z0-9]+', '-', HERE.name.lower())}-{digest}"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--configure", action="store_true", help="configure CMake again (build_linux.py --configure)")
    parser.add_argument("--clean", action="store_true", help="the native build from nothing (build_linux.py --clean)")
    parser.add_argument("--out", default=str(HERE / "build" / "native" / "linux"), help="where the AppImage and the log go")
    parser.add_argument("--volume", default=default_volume(), help="the Docker volume the tree is copied into and built in")
    parser.add_argument("--check", action="store_true", help="check the AppImage after (check_linux.py)")
    parser.add_argument("--iso", help="the PAL disc image the check plays from")
    parser.add_argument("--version", help="the version the check wants (native/CMakeLists.txt's)")
    arguments = parser.parse_args()
    if arguments.check and not arguments.iso:
        parser.error("--check needs --iso")
    out = Path(arguments.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    log = out / "build.txt"
    log.write_text("")
    if not (HERE / "asm" / "data").exists():
        sys.exit("No split: run tools/split.py first (README.md)")
    if subprocess.run(["docker", "info"], capture_output=True).returncode != 0:
        sys.exit("Docker isn't running (docker info fails): start Docker Desktop, or the docker service")

    run(["docker", "build", "-t", IMAGE, "-f", str(DOCKERFILE), str(DOCKERFILE.parent)], log, "image")
    print(f"{'image':15} {IMAGE}", flush=True)
    run(["docker", "volume", "create", arguments.volume], log, "volume")

    build = ["--configure"] if arguments.configure else []
    build += ["--clean"] if arguments.clean else []
    print(f"{'tree':15} {send_tree(arguments.volume, log)} files in the volume {arguments.volume}", flush=True)
    script = "\n".join([
        "set -e",
        "cd /work/tree",
        "[ -x .venv/bin/python ] || python3 -m venv .venv",
        ".venv/bin/python -m pip install --disable-pip-version-check -q -r requirements.txt",
        "export PATH=\"$PWD/.venv/bin:$PATH\"",
        f".venv/bin/python -B native/tools/build_linux.py {' '.join(build)}",
        ".venv/bin/python -B native/tools/package_linux.py",
        f"cp build/native/dist/{APPIMAGE} /out/{APPIMAGE}",
        "cat /usr/local/ps2dev-ubuntu-latest.sha256",
    ])
    run(["docker", "run", "--rm", "-v", f"{arguments.volume}:/work", "-v", f"{out}:/out", IMAGE, "bash", "-c", script], log,
        "build")
    text = log.read_text(encoding="utf-8", errors="replace")
    summary = re.findall(r"^\S+\.AppImage \(.*\)$", text, re.M)
    print(f"{'build':15} ok ({summary[-1] if summary else 'no summary line'})")
    print(f"{'appimage':15} {out / APPIMAGE}")
    if not arguments.check:
        return

    # The checks: the tree's own check_linux.py (the copy's), the AppImage and the disc image read-only, the results in out
    check = ["python3", "-B", "/work/tree/native/tools/check_linux.py", f"/out/{APPIMAGE}", "--iso", "/disc/image.iso",
             "--work", "/out/check", "--program-out", "/out/crash-twinsanity"]
    check += ["--version", arguments.version] if arguments.version else []
    result = subprocess.run(["docker", "run", "--rm", "-v", f"{arguments.volume}:/work", "-v", f"{out}:/out",
                             "-v", f"{Path(arguments.iso).resolve()}:/disc/image.iso:ro", IMAGE] + check,
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    with open(log, "a", encoding="utf-8") as text:
        text.write(f"\n===== check\n{result.stdout}{result.stderr}")
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.exit(f"{'check':15} FAILED: see {log}")


if __name__ == "__main__":
    main()
