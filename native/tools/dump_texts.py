"""Dumps the game's language texts from the disc image: every line of Language\\<file>\\<language>.txt in CRASH6\\CRASH.BD, numbered
as the game numbers them (blank lines don't count, `GameText`), so lines can be looked up by the IDs the code uses.

    python native/tools/dump_texts.py                      # every file of every language
    python native/tools/dump_texts.py --file Code --language English
    python native/tools/dump_texts.py --grep "memory card|ps2|console"     # lines matching a regex (case-insensitive)
    python native/tools/dump_texts.py --out DIR            # the raw files written to DIR

The disc image is local.json's "disc_image" (or $TWINSANITY_ISO, or --iso). The texts are Windows-1252 (the game's font's codes);
they're printed as UTF-8. The PC wording that replaces the console's lines is src/platform/native/ui/pctext.cpp (native/PC_TEXT.md).
"""
import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import local_config  # noqa: E402

FILES = ["Code", "AgentLab", "Credits"]
LANGUAGES = ["English", "French", "German", "Spanish", "Italian", "American"]  # (American: the North American disc's one)
SECTOR = 2048


class Iso:
    def __init__(self, path):
        self.file = open(path, "rb")
        root = self.sectors(16)[156:156 + 34]
        self.files = {}
        self._walk(*struct.unpack_from("<I", root, 2), struct.unpack_from("<I", root, 10)[0], "")

    def sectors(self, lba, count=1):
        self.file.seek(lba * SECTOR)
        return self.file.read(count * SECTOR)

    def _walk(self, lba, size, prefix):
        data = self.sectors(lba, (size + SECTOR - 1) // SECTOR)
        at = 0
        while at < len(data):
            length = data[at]
            if length == 0:
                at = (at // SECTOR + 1) * SECTOR
                continue
            entry = data[at:at + length]
            at += length
            name = entry[33:33 + entry[32]].decode("latin-1")
            if name in ("\0", "\1"):
                continue
            extent, = struct.unpack_from("<I", entry, 2)
            extent_size, = struct.unpack_from("<I", entry, 10)
            if entry[25] & 2:
                self._walk(extent, extent_size, prefix + name + "/")
            else:
                self.files[(prefix + name).split(";")[0].upper()] = (extent, extent_size)

    def read(self, path, offset=0, size=None):
        extent, extent_size = self.files[path.upper()]
        self.file.seek(extent * SECTOR + offset)
        return self.file.read(extent_size if size is None else size)


def archive(iso):
    """The BH's table: path -> (offset, size) in the BD."""
    table = iso.read("CRASH6/CRASH.BH")
    entries = {}
    at = 4
    while at + 4 <= len(table):
        length, = struct.unpack_from("<I", table, at)
        if length == 0 or length > 0x400:
            break
        name = table[at + 4:at + 4 + length].rstrip(b"\0").decode("latin-1")
        offset, size = struct.unpack_from("<II", table, at + 4 + length)
        entries[name.upper()] = (offset, size)
        at += 4 + length + 8
    return entries


def lines_of(text):
    """The game's lines (ReadTextFile): split at each line break, blank lines skipped."""
    return [line for line in re.split(rb"\r\n|\n\r|\r|\n", text) if line]


def load(iso_path):
    iso = Iso(iso_path)
    entries = archive(iso)
    texts = {}
    for file in FILES:
        for language in LANGUAGES:
            key = f"LANGUAGE\\{file}\\{language}.TXT".upper()
            if key in entries:
                offset, size = entries[key]
                texts[(file, language)] = iso.read("CRASH6/CRASH.BD", offset, size)
    return texts


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--iso", default=None)
    parser.add_argument("--file", choices=FILES)
    parser.add_argument("--language", choices=LANGUAGES)
    parser.add_argument("--grep")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    iso_path = args.iso or local_config.disc_image()
    if not iso_path:
        sys.exit("no disc image: --iso, $TWINSANITY_ISO or local.json's disc_image")
    pattern = re.compile(args.grep, re.IGNORECASE) if args.grep else None
    for (file, language), text in load(iso_path).items():
        if (args.file and file != args.file) or (args.language and language != args.language):
            continue
        if args.out:
            args.out.mkdir(parents=True, exist_ok=True)
            (args.out / f"{file}_{language}.txt").write_bytes(text)
        for number, line in enumerate(lines_of(text)):
            decoded = line.decode("cp1252", errors="replace")
            if pattern is None or pattern.search(decoded):
                print(f"{file}\t{language}\t0x{number:02X}\t{decoded}")


if __name__ == "__main__":
    main()
