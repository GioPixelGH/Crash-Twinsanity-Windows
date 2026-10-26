#!/usr/bin/env python3
"""Checks that none of the game's files is in the repository: its executable, the disc's and the archive's files, disc images,
what's made from the retail executable (splat's asm and assets, the Ghidra project), texture packs, PCSX2's states, memory cards
and BIOS. A GitHub workflow (.github/workflows/game-data.yml) runs it on every push and pull request.

    check_game_data.py [FILE...]

With no files, every file git tracks; with files (git diff --cached --name-only, say), those. A file is refused by its name, its
folder, its first bytes (an ISO 9660 image, a PS2 executable or IOP module, an MPEG movie) or its size (over 4 MiB: nothing of
ours is that big). Prints one line a file refused and exits 1 when there's one.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# The disc's executable (SLES_525.68 and the other releases' alike) and its boot file
NAMES = re.compile(r"^(S[CL][EUPK][SMD]_\d{3}\.\d{2}|SYSTEM\.CNF)$", re.I)
EXTENSIONS = {
    # The disc's files: movies, music and voices, the archive and its table, IOP modules and their image
    ".pss", ".mh", ".mb", ".bd", ".bh", ".irx", ".img",
    # The archive's: levels, models, fonts, particles
    ".rm2", ".sm2", ".psm", ".psf", ".ptc",
    # Disc images
    ".iso", ".bin", ".cue", ".chd", ".cso", ".zso", ".mdf", ".mds", ".nrg", ".ecm",
    # PCSX2's save states and memory cards, the PS2's BIOS
    ".p2s", ".ps2", ".mcd", ".mcr", ".rom0", ".rom1", ".rom2", ".erom", ".nvm", ".mec",
}
# Made from the retail executable (.gitignore has them): splat's output, the Ghidra project; texture packs are the game's art
FOLDERS = ("asm/", "assets/", "ghidra/", "textures/")
LARGEST = 4 << 20


def refused(path, full):
    """Why a file (its path in the repository, and on disk) is the game's, or None."""
    name, lower = path.name, path.as_posix().lower()
    if NAMES.match(name):
        return "the disc's executable or boot file"
    if path.suffix.lower() in EXTENSIONS:
        return f"a {path.suffix} file, one of the game's types"
    if lower.startswith(FOLDERS) or "sles-52568" in lower.split("/"):
        return "a folder made from the game"
    if not full.is_file():
        return None
    if full.stat().st_size > LARGEST:
        return f"{full.stat().st_size >> 20} MiB, more than anything of ours"
    with open(full, "rb") as file:
        head = file.read(0x9320)
    if head[0x8001:0x8006] == b"CD001" or head[0x9311:0x9316] == b"CD001":
        return "an ISO 9660 disc image"
    if head[:4] == b"\x7fELF" and head[18:20] == b"\x08\x00":
        return "a MIPS executable (the PS2's or an IOP module)"
    if head[:4] == b"\x00\x00\x01\xba":
        return "an MPEG movie"
    return None


def main():
    if sys.argv[1:]:
        files = [(Path(name), Path(name)) for name in sys.argv[1:]]
    else:
        listed = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True).stdout
        files = [(Path(name), ROOT / name) for name in listed.decode("utf-8").split("\0") if name]
    found = [(path, why) for path, full in files if (why := refused(path, full))]
    for path, why in found:
        print(f"{path.as_posix()}: {why}")
    summary = "none is the game's" if not found else f"{len(found)} refused: never commit the game's files"
    print(f"{len(files)} files, {summary}")
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    main()
