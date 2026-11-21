"""The disc's files the C++ names, checked on each disc (THE LIST 31; native/WINDOWS.md, "The North American disc").

Every string literal of src/ that is a path in the disc's folders (StartUp\\, Language\\, CRASH6\\, FMV\\, Levels\\: a chunk
without its .rm2/.sm2, a folder with its last backslash) is looked up in the disc's own files and in its archive's
(CRASH6\\CRASH.BD by CRASH.BH's table), on the PAL image and on the North American v2.00 one; so are the files each disc's
languages are read from (the language's name put in the game's paths: Language\\Code\\<name>.txt, CRASH6\\<name>.MB...).
A file that one disc names otherwise is a decision in the C++ (the font: Crash_Euro on PAL, Crash on the US disc), listed in
EXPECTED below; any other path missing on a disc fails, as the game may fail there without saying why.

    python native/tools/disc_paths.py            # the images of local.json ("disc_image", "disc_image_usa")
    python native/tools/disc_paths.py --list     # every path found and where
"""
import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(HERE / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import local_config  # noqa: E402
from iso_extract import archive_files, root, walk  # noqa: E402

BACKSLASH = chr(92)
TOPS = ("STARTUP", "LANGUAGE", "CRASH6", "FMV", "LEVELS")
# The paths only one disc has, each a decision in the C++: path -> the disc that has it
EXPECTED = {
    "STARTUP/FONTS/CRASH_EURO": "PAL",  # gamecontroller.cpp's FontFile
    "STARTUP/FONTS/CRASH": "USA",  # its NorthAmericanFontFile
    "STARTUP/FONTS/CRASH.PSF": "USA",  # xboxart.cpp's, read from the Xbox disc (the same name as the US disc's)
    "STARTUP/FONTS/CRASH_EURO.PSF": "PAL",  # the ui self-test's look at the disc's font (commands.cpp's FontCharacters)
    "LANGUAGE/CODE/AMERICAN.TXT": "USA",  # the ui self-test's sign of the North American disc (commands.cpp's DiscLanguages)
}
# Each disc's languages (the game's names: g_LanguageNames on PAL, gamecontext.cpp's NorthAmericanLanguages) and the files the
# game makes from a language's name
LANGUAGES = {"PAL": ["English", "French", "German", "Spanish", "Italian"], "USA": ["American"]}
LANGUAGE_FILES = ["LANGUAGE/CODE/{}.TXT", "LANGUAGE/AGENTLAB/{}.TXT", "LANGUAGE/CREDITS/{}.TXT", "LANGUAGE/LEGAL/{}.PSM",
                  "LANGUAGE/TITLES/{}/", "CRASH6/{}.MB", "CRASH6/{}.MH"]


def disc_files(path):
    """The disc's files and its archive's, upper case with slashes, and their names without extensions"""
    with open(path, "rb") as image:
        files = {name.upper() for name, _ in walk(image, *root(image), "")}
        files |= {name.upper().replace(BACKSLASH, "/") for name in archive_files(image)}
    stems = {name.rsplit(".", 1)[0] for name in files if "." in name.rsplit("/", 1)[-1]}
    return files, stems


def has(path, files, stems):
    if path.endswith("/"):
        return any(name.startswith(path) for name in files)
    return path in files or path in stems


def literals():
    """path -> [source:line] for every literal naming a disc file"""
    found = {}
    string = re.compile(r'"([^"]*)"')
    for source in sorted((HERE / "src").rglob("*")):
        if source.suffix not in (".cpp", ".c", ".h", ".inc"):
            continue
        for number, line in enumerate(source.read_text(errors="replace").splitlines(), 1):
            if line.lstrip().startswith("//"):
                continue
            for match in string.finditer(line):
                path = match.group(1).replace(BACKSLASH * 2, "/").replace(BACKSLASH, "/").upper().lstrip("/")
                if path.startswith("CDROM0:/"):
                    path = path[len("CDROM0:/"):]
                if " " in path or "%" in path or "..." in path or path.split("/", 1)[0] not in TOPS or "/" not in path:
                    continue
                found.setdefault(path, []).append(f"{source.relative_to(HERE).as_posix()}:{number}")
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--pal", help="the PAL image (local.json's disc_image)")
    parser.add_argument("--usa", help="the North American v2.00 image (local.json's disc_image_usa)")
    parser.add_argument("--list", action="store_true", help="every path found")
    arguments = parser.parse_args()
    images = {"PAL": arguments.pal or local_config.disc_image(),
              "USA": arguments.usa or local_config._setting("TWINSANITY_ISO_USA", "disc_image_usa")}
    discs = {}
    for disc, image in images.items():
        if not image or not Path(image).is_file():
            print(f"{disc}: no image (local.json's {'disc_image' if disc == 'PAL' else 'disc_image_usa'}): not checked")
            continue
        discs[disc] = disc_files(image)

    problems = 0
    paths = literals()
    for path, where in sorted(paths.items()):
        on = [disc for disc, (files, stems) in discs.items() if has(path, files, stems)]
        if arguments.list:
            print(f"  {path:48} {' '.join(on) or 'none':8} {where[0]}{' ...' if len(where) > 1 else ''}")
        for disc in discs:
            if disc in on or EXPECTED.get(path, disc) != disc:
                continue
            problems += 1
            print(f"{disc}: no {path} ({', '.join(where[:3])}{' ...' if len(where) > 3 else ''})")
    for path, disc in EXPECTED.items():
        if path in paths and disc in discs and not has(path, *discs[disc]):
            problems += 1
            print(f"{disc}: no {path}, EXPECTED's ({paths[path][0]})")

    names = 0
    for disc, (files, stems) in discs.items():
        for language in LANGUAGES[disc]:
            for pattern in LANGUAGE_FILES:
                names += 1
                path = pattern.format(language.upper())
                if not has(path, files, stems):
                    problems += 1
                    print(f"{disc}: no {path} (its language {language})")

    print(f"disc paths: {len(paths)} paths in the C++ and {names} language files on {' and '.join(discs) or 'no disc'}: "
          f"{'all there' if problems == 0 else f'{problems} missing'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
