#!/usr/bin/env python3
"""Checks the repository's text files for bytes an edit made by a script can leave in them by mistake: a control character
(any byte below 0x20 but tab, LF and CR: a "\\b" in a script's string becomes a backspace, a "\\0" a NUL) and Windows line ends
(CR) in a file that has LF ones (only .bat files keep CRLF). The pre-commit hook (.githooks/pre-commit) runs it on the files
staged.

    check_text_files.py [FILE...]

With no files, every file git tracks; with files, those. Prints one line a file refused (where its first such byte is) and
exits 1 when there's one.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEXT = {".c", ".cc", ".cpp", ".h", ".hpp", ".inc", ".py", ".md", ".txt", ".cmake", ".in", ".yml", ".yaml", ".json", ".sh", ".rc",
        ".s", ".ld", ".toml", ".cfg", ".ini", ".gitignore", ".gitattributes", ".css", ".html", ".js", ".glsl", ".vert", ".frag",
        ".bat"}
ALLOWED = {9, 10, 13}


def problem(path, data):
    """What's wrong with a text file's bytes, or None"""
    for at, byte in enumerate(data):
        if byte < 0x20 and byte not in ALLOWED:
            line = data.count(b"\n", 0, at) + 1
            return f"a control byte 0x{byte:02x} on line {line}"
    if b"\r" in data and path.suffix.lower() != ".bat":
        line = data.count(b"\n", 0, data.index(b"\r")) + 1
        return f"a CR (Windows line end) on line {line}, in a file of LF ones"
    return None


def main():
    names = sys.argv[1:] or subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True).stdout.splitlines()
    found = 0
    for name in names:
        path = Path(name)
        full = ROOT / path
        if (path.suffix.lower() not in TEXT and path.name not in (".gitignore", ".gitattributes", "pre-commit")) or not full.is_file():
            continue
        why = problem(path, full.read_bytes())
        if why:
            print(f"{name}: {why}")
            found += 1
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    main()
