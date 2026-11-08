#!/usr/bin/env python3
"""A release of the Windows build and the Linux AppImage (native/WINDOWS.md, "Releasing") in two steps, each summed up in a few
lines. Run it in a clone of the public repository with no changes of its own, so nothing local gets into the build.

    .venv\\Scripts\\python.exe native\\tools\\release_windows.py check [--sweep] [--no-linux]
    .venv\\Scripts\\python.exe native\\tools\\release_windows.py publish NOTES.md [--preview]

check: pulls (fast-forward only), configures and builds afresh (build_windows.py --configure), packages (package_windows.py),
then checks the zip, unpacked: the program's version against native/CMakeLists.txt's, its company and copyright against
native/windows.rc.in's, that it has nothing of a player's (portable mode's portable.txt or User folder, a disc image), the DLLs
it imports, its files against the last release's, that the program has no 32-byte block of the game executable's data or VU microcode the last release's didn't,
and check_windows.py's checks with the pictures compared with the last release's program. Then the AppImage, made in Docker
(build_linux_docker.py --configure --check: Docker has to be running) and checked there by check_linux.py (its libraries, the
glibc it needs, its version, the beach played from it headless), and its program searched for the game's bytes as the zip's
is (none the Windows program of the same commit or the last release's hasn't). --no-linux leaves the AppImage out of the
release (said so in the result). It prints the commit, the version and the files' SHA-256, and keeps them for publish
(build/native/release/release.json).

publish: the release vVERSION on GitHub (a preview with --preview, as every release before 1.0.8 was), its text NOTES.md with
{commit}, {sha256} (the zip's) and {sha256_linux} (the AppImage's) filled in; the zip and the AppImage uploaded, then
downloaded back and their SHA-256 compared. Only after check, at the same commit, with the same files. GitHub's token is git's
(git credential fill): never printed or kept.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]
TOOLS = HERE / "native" / "tools"
sys.path.insert(0, str(TOOLS))
import build_windows  # noqa: E402
import check_windows  # noqa: E402
import package_windows  # noqa: E402

WORK = HERE / "build" / "native" / "release"
ZIP_NAME = "Crash-Twinsanity-Windows.zip"
ZIP = HERE / "build" / "native" / "dist" / ZIP_NAME
PROGRAM = "Crash Twinsanity/CrashTwinsanity.exe"
# The AppImage (build_linux_docker.py's) and its program as the checks copy it out
LINUX = HERE / "build" / "native" / "linux"
APPIMAGE_NAME = "Crash-Twinsanity-Linux-x86_64.AppImage"
APPIMAGE = LINUX / APPIMAGE_NAME
LINUX_PROGRAM = LINUX / "crash-twinsanity"
# The DLLs the program may import: Windows' own (the UCRT's API sets among them; WinHTTP for the update check, since 1.2.0),
# SDL and FFmpeg's, which the zip has
ALLOWED_IMPORTS = re.compile(r"^(KERNEL32|WINMM|WINHTTP|SDL3|avcodec-\d+|avutil-\d+|api-ms-win-crt-[a-z]+-l1-1-0)\.dll$", re.I)
# The retail executable's sections whose bytes the program mustn't carry (native/WINDOWS.md, "The game's data from the disc")
GAME_SECTIONS = (".data", ".rodata", ".sdata", ".vutext", ".vudata")


def git(*arguments):
    return subprocess.run(["git", *arguments], cwd=HERE, capture_output=True, text=True, check=True).stdout.strip()


def version():
    """native/CMakeLists.txt's version"""
    found = re.search(r"project\(TwinsanityNative VERSION ([\d.]+)", (HERE / "native" / "CMakeLists.txt").read_text())
    return found.group(1)


def repository():
    """The GitHub repository behind origin: owner/name (origin may be a local clone of it, whose origin is followed)"""
    url = git("remote", "get-url", "origin")
    for _ in range(5):
        found = re.search(r"github\.com[:/](.+?)(?:\.git)?/?$", url)
        if found:
            return found.group(1)
        url = subprocess.run(["git", "-C", url, "remote", "get-url", "origin"], capture_output=True, text=True,
                             check=True).stdout.strip()
    sys.exit("origin doesn't lead to a GitHub repository")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def api(url, token=None, data=None, content_type="application/json", method=None):
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "release_windows.py"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if data is not None:
        headers["Content-Type"] = content_type
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(request) as response:
        return json.loads(response.read() or b"null")


def download(url, path):
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "release_windows.py"})) as response:
        Path(path).write_bytes(response.read())


def unpack(archive, folder):
    if folder.exists():
        shutil.rmtree(folder)
    with zipfile.ZipFile(archive) as z:
        z.extractall(folder)
        return sorted(name for name in z.namelist() if not name.endswith("/"))


def resource_string(exe, key):
    """A value of the program's version resource (FileVersion, CompanyName, LegalCopyright...): "" when it has none"""
    data = Path(exe).read_bytes()
    at = data.find((key + "\0").encode("utf-16-le"))
    if at < 0:
        return ""
    text = data[at:at + 1024].decode("utf-16-le", "replace").split("\0")
    return next((part for part in text[1:] if part), "")


def program_version(exe):
    """The version resource's FileVersion"""
    return resource_string(exe, "FileVersion")


def declared_details():
    """The author's details native/windows.rc.in gives the program (its CompanyName and LegalCopyright), the resource
    compiler's octal escapes (\\251 for the copyright sign) decoded"""
    text = (HERE / "native" / "windows.rc.in").read_text(encoding="utf-8")
    values = {}
    for key, value in re.findall(r'VALUE "(\w+)", "((?:[^"\\]|\\.)*)"', text):
        values[key] = re.sub(r"\\([0-7]{1,3})", lambda found: chr(int(found.group(1), 8)), value)
    return {key: values.get(key, "") for key in ("CompanyName", "LegalCopyright")}


def imports(exe):
    """The DLLs the program imports"""
    tool = build_windows.find_llvm_mingw() / "llvm-objdump.exe"
    listing = subprocess.run([str(tool), "-p", str(exe)], capture_output=True, text=True).stdout
    return sorted(set(re.findall(r"DLL Name: (\S+)", listing)))


def elf_sections(data, names):
    """An ELF32 (little endian) file's sections of those names: their bytes"""
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x2E)
    def header(index):
        return struct.unpack_from("<IIIIIIIIII", data, shoff + index * shentsize)
    strings = header(shstrndx)
    found = []
    for index in range(shnum):
        name_at, kind, _, _, offset, size = header(index)[:6]
        name = data[strings[4] + name_at:data.index(b"\0", strings[4] + name_at)].decode()
        if name in names and kind != 8:  # not NOBITS
            found.append(data[offset:offset + size])
    return found


def game_blocks_in(exe, retail):
    """The 32-byte blocks of the game executable's data and VU microcode (all but the repetitive ones) found in the program:
    (blocks checked, {(section, offset) found}). Version 1.0.5's has 23 of 3832, none of the microcode: runs of small
    integers, the standard library's tables (powers of ten, digits) and file names the decompilation's C++ spells out"""
    program = Path(exe).read_bytes()
    checked, found = 0, set()
    for index, section in enumerate(elf_sections(Path(retail).read_bytes(), GAME_SECTIONS)):
        for at in range(0, len(section) - 31, 32):
            block = section[at:at + 32]
            if len(set(block)) < 8:
                continue
            checked += 1
            if program.find(block) >= 0:
                found.add((index, at))
    return checked, found


def check(arguments):
    # (The build compiles every source file there is: a new one would get in. A new script in native/tools wouldn't)
    changes = [line for line in git("status", "--porcelain").splitlines()
               if not (line.startswith("?? ") and line[3:].startswith("native/tools/"))]
    if changes:
        sys.exit("The clone has changes of its own (a release is built from the repository as it is): " + changes[0])
    before = git("rev-parse", "HEAD")
    git("pull", "--ff-only")
    if git("rev-parse", "HEAD") != before:
        # Run again as the pulled commit has the scripts: this one and the modules it imported (check_windows.py's scenarios)
        # are the clone's from before the pull
        sys.exit(subprocess.run([sys.executable] + sys.argv).returncode)
    commit = git("rev-parse", "HEAD")
    print(f"commit          {commit[:7]} {git('log', '-1', '--format=%s')}")
    # None of the game's files in the repository released from (the pushes' workflow and the pre-commit hook run it too)
    gameFiles = subprocess.run([sys.executable, str(TOOLS / "check_game_data.py")], cwd=HERE, capture_output=True, text=True)
    if gameFiles.returncode != 0:
        sys.exit("game files      FAILED: " + gameFiles.stdout.strip().replace("\n", "; "))
    print(f"game files      {gameFiles.stdout.strip()}")
    WORK.mkdir(parents=True, exist_ok=True)
    for step, command in (("build", [sys.executable, str(TOOLS / "build_windows.py"), "--configure"]),
                          ("package", [sys.executable, str(TOOLS / "package_windows.py")])):
        with open(WORK / f"{step}.txt", "w") as log:
            if subprocess.run(command, cwd=HERE, stdout=log, stderr=subprocess.STDOUT).returncode != 0:
                sys.exit(f"{step:15} FAILED: see {WORK / (step + '.txt')}")
        print(f"{step:15} ok")

    files = unpack(ZIP, WORK / "zip")
    exe = WORK / "zip" / PROGRAM
    failed = False
    wanted, have = version(), program_version(exe)
    print(f"version         {have}{'' if have == wanted else ' (CMakeLists.txt says ' + wanted + ': FAILED)'}")
    failed |= have != wanted
    # The author's details (Properties > Details): only our program carries them; the DLLs keep their own authors'
    wrong = [key for key, value in declared_details().items() if not value or resource_string(exe, key) != value]
    print(f"details         {'company and copyright as native/windows.rc.in has them' if not wrong else 'FAILED: ' + ', '.join(wrong)}")
    failed |= bool(wrong)
    # Nothing of a player's (the folder packaged can be one played from: package_windows.py leaves their files out of the zip)
    theirs = [name for name in files if package_windows.players(Path(*Path(name).parts[:2]))]
    print(f"player's files  {'none (no portable.txt, User folder or disc image)' if not theirs else 'FAILED: ' + ', '.join(theirs[:5])}")
    failed |= bool(theirs)
    stray = [name for name in imports(exe) if not ALLOWED_IMPORTS.match(name)]
    print(f"imports         {'Windows, SDL and FFmpeg only' if not stray else 'FAILED, also: ' + ', '.join(stray)}")
    failed |= bool(stray)

    previous = None
    for release in api(f"https://api.github.com/repos/{repository()}/releases"):
        asset = next((a for a in release["assets"] if a["name"] == ZIP_NAME), None)
        if not release["draft"] and release["tag_name"] != f"v{wanted}" and asset:
            previous = release["tag_name"]
            download(asset["browser_download_url"], WORK / "previous.zip")
            break
    if previous:
        before = unpack(WORK / "previous.zip", WORK / "previous")
        added, removed = sorted(set(files) - set(before)), sorted(set(before) - set(files))
        print(f"files           {len(files)}{'' if not added and not removed else ', added: ' + ', '.join(added) + '; removed: ' + ', '.join(removed)}"
              f" (against {previous})")
    retail = WORK / "SLES_525.68"
    subprocess.run([sys.executable, str(TOOLS / "iso_extract.py"), check_windows.disc_image(), "SLES_525.68", str(retail)],
                   capture_output=True, check=True)
    with ProcessPoolExecutor(2) as pool:
        ours = pool.submit(game_blocks_in, exe, retail)
        theirs = pool.submit(game_blocks_in, WORK / "previous" / PROGRAM, retail) if previous else None
        failed |= check_windows.report(exe, WORK / "previous" / PROGRAM if previous else None, WORK / "checks",
                                       arguments.sweep, f"{previous}")
        checked, found = ours.result()
        new = found - theirs.result()[1] if theirs else found
    print(f"game's bytes    {len(found)} of the executable's {checked} data and microcode blocks in the program, "
          f"{len(new)} not in the last release's{': FAILED' if new else ''}")
    failed |= bool(new)

    linux = None
    if arguments.no_linux:
        print("linux           left out (--no-linux): the release has no AppImage")
    else:
        failed |= not linux_check(wanted, retail, found | (theirs.result()[1] if theirs else set()))
        linux = sha256(APPIMAGE) if APPIMAGE.exists() else None

    digest = sha256(ZIP)
    (WORK / "release.json").write_text(json.dumps({"version": wanted, "commit": commit, "sha256": digest,
                                                   "sha256_linux": linux}, indent=1))
    print(f"zip             {ZIP} ({ZIP.stat().st_size / 1e6:.1f} MB)")
    print(f"sha-256         {digest}")
    if linux:
        print(f"appimage        {APPIMAGE} ({APPIMAGE.stat().st_size / 1e6:.1f} MB)")
        print(f"sha-256         {linux}")
    print("result          " + ("FAILED: don't publish" if failed else f"ready: release_windows.py publish NOTES.md"))
    sys.exit(1 if failed else 0)


def linux_check(wanted, retail, windows_blocks):
    """The AppImage made and checked in Docker (build_linux_docker.py), its lines printed as "linux ..."; its program's blocks
    of the game's against the Windows program's (this commit's and the last release's). True when all passed"""
    for stale in (APPIMAGE, LINUX_PROGRAM):
        if stale.exists():
            stale.unlink()
    command = [sys.executable, str(TOOLS / "build_linux_docker.py"), "--configure", "--check", "--iso", check_windows.disc_image(),
               "--version", wanted, "--out", str(LINUX)]
    result = subprocess.run(command, cwd=HERE, capture_output=True, text=True)
    (WORK / "linux.txt").write_text(result.stdout + result.stderr)
    for line in result.stdout.splitlines():
        print(f"linux {line}")
    if result.returncode != 0 or not APPIMAGE.exists() or not LINUX_PROGRAM.exists():
        last = (result.stderr.strip().splitlines() or ["no AppImage"])[-1]
        print(f"linux           FAILED: {last} (build_linux_docker.py; --no-linux releases without it)")
        return False
    checked, found = game_blocks_in(LINUX_PROGRAM, retail)
    new = found - windows_blocks
    print(f"linux game's    {len(found)} of the executable's {checked} blocks in the Linux program, {len(new)} not in the "
          f"Windows program's{': FAILED' if new else ''}")
    return not new


def token():
    found = subprocess.run(["git", "credential", "fill"], cwd=HERE, input="protocol=https\nhost=github.com\n\n",
                           capture_output=True, text=True).stdout
    value = re.search(r"^password=(.+)$", found, re.M)
    if not value:
        sys.exit("No GitHub token from git credential fill")
    return value.group(1)


def publish(arguments):
    checked = json.loads((WORK / "release.json").read_text()) if (WORK / "release.json").exists() else None
    if not checked or checked["commit"] != git("rev-parse", "HEAD") or checked["sha256"] != sha256(ZIP):
        sys.exit("Run check first: the commit or the zip isn't the one it checked")
    linux = checked.get("sha256_linux")
    if linux and (not APPIMAGE.exists() or sha256(APPIMAGE) != linux):
        sys.exit("Run check first: the AppImage isn't the one it checked")
    tag, slug, secret = f"v{checked['version']}", repository(), token()
    try:
        api(f"https://api.github.com/repos/{slug}/commits/{checked['commit']}", secret)
    except urllib.error.HTTPError:
        sys.exit(f"{checked['commit'][:7]} isn't on GitHub yet: push it first")
    if any(release["tag_name"] == tag for release in api(f"https://api.github.com/repos/{slug}/releases", secret)):
        sys.exit(f"{tag} is released already")
    notes = Path(arguments.notes).read_text(encoding="utf-8")
    if "{sha256_linux}" in notes and not linux:
        sys.exit("The notes name the AppImage's SHA-256, and check left the AppImage out (--no-linux)")
    notes = notes.replace("{commit}", checked["commit"][:7]).replace("{sha256}", checked["sha256"])
    notes = notes.replace("{sha256_linux}", linux or "")
    name = f"Crash Twinsanity for Windows {checked['version']}" + (" (preview)" if arguments.preview else "")
    # The release opens its own discussion (Discussions > Announcements), where players can talk about it
    body = {"tag_name": tag, "target_commitish": checked["commit"], "name": name, "body": notes,
            "prerelease": arguments.preview, "draft": False, "discussion_category_name": "Announcements"}
    release = api(f"https://api.github.com/repos/{slug}/releases", secret, json.dumps(body).encode(), method="POST")
    print(f"release         {release['html_url']}")
    files = [(ZIP_NAME, ZIP, "application/zip", checked["sha256"])]
    files += [(APPIMAGE_NAME, APPIMAGE, "application/octet-stream", linux)] if linux else []
    all_same = True
    for name, path, kind, digest in files:
        upload = release["upload_url"].split("{")[0] + f"?name={name}"
        asset = api(upload, secret, path.read_bytes(), kind, method="POST")
        download(asset["browser_download_url"], WORK / f"downloaded-{name}")
        same = sha256(WORK / f"downloaded-{name}") == digest
        all_same &= same
        print(f"download        {name}: {'the same SHA-256 as the file checked' if same else 'FAILED: its SHA-256 differs'}")
    sys.exit(0 if all_same else 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    steps = parser.add_subparsers(dest="step", required=True)
    checking = steps.add_parser("check")
    checking.add_argument("--sweep", action="store_true", help="every level chunk too (some 20 minutes)")
    checking.add_argument("--no-linux", action="store_true", help="no AppImage (when Docker isn't there): the zip alone")
    publishing = steps.add_parser("publish")
    publishing.add_argument("notes", help="the release's text: {commit} and {sha256} are filled in")
    publishing.add_argument("--preview", action="store_true", help="a preview (GitHub's pre-release), not the latest release")
    arguments = parser.parse_args()
    {"check": check, "publish": publish}[arguments.step](arguments)


if __name__ == "__main__":
    main()
