"""Writes native/PC_TEXT.md's table of the native menu texts ("## The native menu texts") from the code: their numbers from
NativeUi::NativeText's order (src/platform/native/ui/nativeui.h, 0x100 on) and their five languages from pctext.cpp's
g_NativeTexts, then Brazilian Portuguese's (a sixth string, THE LIST 56: empty where there's none yet, the English shown). Run it
after adding or changing a text:

    python native/tools/pc_text_table.py           # rewrites the table
    python native/tools/pc_text_table.py --check   # exits 1 if the table isn't the code's
    python native/tools/pc_text_table.py --import FILE

--import takes a translator's PC_TEXT.md (its native texts' table with a "Português" column, #25) into pctext.cpp's sixth
strings, each row matched to a text by its English (the numbers move as texts are added), then rewrites the table; it says how
many it took, and which rows' English no text has.
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
    entry = re.compile(r'\{\s*(\w+)\s*,\s*\{((?:\s*"(?:[^"\\]|\\.)*"\s*,?){5,6})\}\s*\}')
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
        cells = [language.replace("|", "\\|") for language in languages + [""] * (6 - len(languages))]
        lines.append((order[name], f"| 0x{order[name]:X} | " + " | ".join(cells) + " |"))
    return "\n".join(line for _, line in sorted(lines)) + "\n"


def import_portuguese(path):
    """A translator's table's Portuguese into pctext.cpp's sixth strings, matched by the English: (taken, unknown rows' English)"""
    rows = {}
    columns = None
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line.startswith("|"):
            columns = None
            continue
        cells = [cell.strip().replace("\\|", "|") for cell in re.split(r"(?<!\\)\|", line.strip())[1:-1]]
        names = [cell.lower() for cell in cells]
        if columns is None:
            portuguese = [i for i, name in enumerate(names) if name.startswith("portugu") or name == "pt-br"]
            if "english" in names and portuguese:
                columns = (names.index("english"), portuguese[0])
            continue
        if set("".join(cells)) <= set("-: ") or max(columns) >= len(cells) or not cells[columns[1]]:
            continue
        rows[cells[columns[0]]] = cells[columns[1]]

    source_path = UI / "pctext.cpp"
    source = source_path.read_text(encoding="utf-8")
    start = source.index("g_NativeTexts[]")
    entry = re.compile(r'(\{\s*\w+\s*,\s*\{)((?:\s*"(?:[^"\\]|\\.)*"\s*,?){5,6})(\}\s*\})')
    taken = set()

    def replace(match):
        strings = re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(2))
        portuguese = rows.get(strings[0])
        if portuguese is None:
            return match.group(0)
        taken.add(strings[0])
        # (A bare quote made the C string's)
        portuguese = re.sub(r'(?<!\\)"', r'\\"', portuguese)
        return match.group(1) + ", ".join(f'"{text}"' for text in strings[:5] + [portuguese]) + match.group(3)

    source = source[:start] + entry.sub(replace, source[start:])
    with open(source_path, "w", encoding="utf-8", newline="\n") as file:
        file.write(source)
    return len(taken), sorted(set(rows) - taken)


def main():
    if "--import" in sys.argv:
        taken, unknown = import_portuguese(sys.argv[sys.argv.index("--import") + 1])
        print(f"pctext.cpp: {taken} texts' Portuguese taken" + (f"; no text has these rows' English: {unknown}" if unknown else ""))
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
