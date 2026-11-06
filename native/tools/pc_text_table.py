"""Writes native/PC_TEXT.md's table of the native menu texts ("## The native menu texts") from the code: their numbers from
NativeUi::NativeText's order (src/platform/native/ui/nativeui.h, 0x100 on) and their five languages from pctext.cpp's
g_NativeTexts. Run it after adding or changing a text:

    python native/tools/pc_text_table.py           # rewrites the table
    python native/tools/pc_text_table.py --check   # exits 1 if the table isn't the code's
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = "nativeui.h"
UI = ROOT / "src" / "platform" / "native" / "ui"
DOC = ROOT / "native" / "PC_TEXT.md"


def numbers():
    text = (UI / HEADER).read_text(encoding="utf-8")
    body = text[text.index("enum NativeText : u32"):]
    body = body[body.index("{") + 1:body.index("};")]
    names = {}
    number = None
    for line in body.splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        if not line:
            continue
        if "=" in line:
            name, value = (part.strip() for part in line.split("=", 1))
            number = 0x100 if value in ("0x100", "NativeTextFirst") else names.get(value, None)
            if number is None:
                number = int(value, 0)
        else:
            name = line
            number += 1
        names[name] = number
    return names


def texts():
    source = (UI / "pctext.cpp").read_text(encoding="utf-8")
    table = source[source.index("g_NativeTexts[]"):]
    entry = re.compile(r'\{\s*(\w+)\s*,\s*\{((?:\s*"(?:[^"\\]|\\.)*"\s*,?){5})\}\s*\}')
    found = {}
    for match in entry.finditer(table):
        found[match.group(1)] = re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(2))
    return found


def table():
    order = numbers()
    lines = []
    for name, languages in texts().items():
        if name not in order:
            sys.exit(f"{name} isn't in NativeText")
        cells = [language.replace("|", "\\|") for language in languages]
        lines.append((order[name], f"| 0x{order[name]:X} | " + " | ".join(cells) + " |"))
    return "\n".join(line for _, line in sorted(lines)) + "\n"


def main():
    doc = DOC.read_text(encoding="utf-8")
    start = doc.index("## The native menu texts")
    rows = doc.index("|---|", start)
    rows = doc.index("\n", rows) + 1
    end = doc.index("\n## ", rows) + 1
    new = doc[:rows] + table() + "\n" + doc[end:]
    if "--check" in sys.argv:
        same = new == doc
        print("PC_TEXT.md's native texts:", "the code's" if same else "NOT the code's (run pc_text_table.py)")
        sys.exit(0 if same else 1)
    with open(DOC, "w", encoding="utf-8", newline="\n") as file:
        file.write(new)
    print(f"PC_TEXT.md: {table().count(chr(10))} native texts")


main()
