"""The level sweep (native/LEVEL_SWEEP.md): every level chunk on the disc started headless (ui/sweep.cpp's $TWIN_SWEEP_LEVEL) and
played with scripted input for a while, a process each; each one's exit, the states it went through, any fatal signal or stray
free, the time it took to load and its snapshots.

    python native/tools/level_sweep.py OUT [--seconds 30] [--jobs 3] [--only BEACH,HUBA] [--app PATH]

Writes OUT/<chunk>/ (log.txt, frame_*.png) and OUT/results.json, and prints a table. The sound is off (TWINSANITY_AUDIO=off), each
run has its own HOME (no settings or saves of the user's), and every process it starts it stops (a time limit each).
"""
import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "native" / "tools"))
sys.path.insert(0, str(ROOT / "tools"))
import dump_texts  # noqa: E402
import local_config  # noqa: E402

WINDOWS = os.name == "nt"
APP = (ROOT / "build/native/cmake/CrashTwinsanity.exe" if WINDOWS else
       ROOT / "build/native/cmake/Crash Twinsanity.app/Contents/MacOS/Crash Twinsanity")


def symbolize(text, app):
    """Windows' crash reports give return addresses as module+offset (lowmemory_win32.cpp): the executable's ones get their
    function names and lines from llvm-symbolizer, added under each"""
    frames = re.findall(r"^  #\d+ +[0-9a-f]+ " + re.escape(Path(app).name) + r"\+0x([0-9a-f]+)(?: .*)?$", text, re.M)
    if not frames:
        return text
    import shutil
    tool = shutil.which("llvm-symbolizer")
    if tool is None:
        return text
    base = 0x10000000
    addresses = [hex(base + int(f, 16)) for f in frames]
    output = subprocess.run([tool, "--obj=" + str(app), "-f", "-i"] + addresses, capture_output=True, text=True).stdout
    # llvm-symbolizer answers each address with its frames' lines and a blank line
    blocks = [" ".join(b.split()) for b in re.split(r"\n\s*\n", output.strip())]
    names = dict(zip(frames, blocks))
    return re.sub(r"^(  #\d+ +[0-9a-f]+ " + re.escape(Path(app).name) + r"\+0x([0-9a-f]+))$",
                  lambda m: m.group(1) + "  " + names.get(m.group(2), ""), text, flags=re.M)


def level_chunks():
    """The disc's level chunks (Levels\\...\\NAME, the archive's own case), the always-on ones (loaded with their hubs) left out."""
    iso = dump_texts.Iso(local_config.disc_image())
    table = iso.read("CRASH6/CRASH.BH")
    names = []
    at = 4
    import struct
    while at + 4 <= len(table):
        length, = struct.unpack_from("<I", table, at)
        if length == 0 or length > 0x400:
            break
        name = table[at + 4:at + 4 + length].rstrip(b"\0").decode("latin-1")
        at += 4 + length + 8
        if name.upper().endswith(".RM2") and name.upper().startswith("LEVELS\\") and "ALWAYSON" not in name.upper():
            names.append(name[:-4])
    return sorted(names, key=str.upper)


def run(chunk, out, seconds, timeout):
    folder = out / chunk.replace("\\", "_")
    folder.mkdir(parents=True, exist_ok=True)
    for old in folder.glob("frame_*.png"):
        old.unlink()
    home = folder / "home"
    home.mkdir(exist_ok=True)
    # (Windows keeps the settings in APPDATA)
    environment = dict(os.environ, TWINSANITY_AUDIO="off", TWIN_SWEEP_LEVEL=chunk, TWIN_SWEEP_SECONDS=str(seconds),
                       TWIN_SWEEP_TIMEOUT=str(timeout), HOME=str(home), APPDATA=str(home))
    started = time.time()
    with open(folder / "log.txt", "w") as log:
        try:
            process = subprocess.run([str(APP), "--headless", "--quiet-stubs", "--snapshots", str(folder)], env=environment,
                                     stdout=log, stderr=subprocess.STDOUT, timeout=timeout + seconds * 6 + 60, cwd=ROOT)
            status = process.returncode
        except subprocess.TimeoutExpired:
            status = "timeout"
    if WINDOWS:
        text = (folder / "log.txt").read_text(errors="replace")
        (folder / "log.txt").write_text(symbolize(text, APP))
    return parse(chunk, folder, status, round(time.time() - started, 1))


def parse(chunk, folder, status, wall):
    """A run's result from its log and snapshots."""
    text = (folder / "log.txt").read_text(errors="replace")
    fatal = re.findall(r"\[native\] fatal .*", text)
    frees = len(re.findall(r"a free of 0x[0-9a-f]+, outside", text))
    loaded = re.search(r"sweep: state 9 at ([\d.]+) s", text)
    playing = re.search(r"sweep: state (?:12|13|14) at ([\d.]+) s", text)
    ran = re.search(r"sweep: .*: (ran|game over|no character in the chunk|never played[^(]*\(state \d+\)) \(([\d.]+) s played, (\d+) frames\)", text)
    states = re.findall(r"sweep: state (\d+) at", text)
    # Play started in a chunk without the played character: the start's follow camera restarted through a null rig
    # (FollowCameraRig::Restart from GameController::StartingPlay), before any play. The game only enters these from a neighbour
    no_player = bool(fatal) and not playing and re.search(r"FUN_0015e4a8.*\n.*FUN_001745f8", text) is not None
    result = {
        "chunk": chunk,
        "status": status,
        "outcome": ran.group(1) if ran else ("no character in the chunk" if no_player else "fatal" if fatal else "no end"),
        "played": float(ran.group(2)) if ran else None,
        "frames": int(ran.group(3)) if ran else None,
        "load_seconds": round(float(playing.group(1)) - float(loaded.group(1)), 1) if loaded and playing else None,
        "states": states,
        "fatal": fatal[:1],
        "stray_frees": frees,
        "snapshots": len(list(folder.glob("frame_*.png"))),
        "wall": wall,
    }
    return result


def report(results, path):
    """The sweep's table (native/LEVEL_SWEEP.md's): a row a chunk, by its world and area."""
    rows = ["| World | Area | Chunk | Status | Load (s) | Frames in 30 s | Notes |", "| --- | --- | --- | --- | --- | --- | --- |"]
    for chunk in sorted(results, key=str.upper):
        result = results[chunk]
        parts = chunk.split("\\")
        world, area, name = (parts[1], parts[2], parts[3]) if len(parts) == 4 else ("", "", chunk)
        if result["outcome"] == "ran":
            status = "OK"
        elif result["outcome"] == "game over":
            status = "OK (game over)"
        elif result["outcome"] == "no character in the chunk":
            status = "Entered from a neighbour"
        elif result["fatal"]:
            status = "Crash"
        else:
            status = result["outcome"]
        notes = result.get("notes", "")
        if result["stray_frees"]:
            notes = (notes + "; " if notes else "") + f"{result['stray_frees']} stray frees"
        if result["fatal"] and not notes and result["outcome"] != "no character in the chunk":
            notes = result["fatal"][0].replace("[native] ", "")
        rows.append(f"| {world} | {area} | {name} | {status} | {result['load_seconds'] if result['load_seconds'] is not None else '-'} "
                    f"| {result['frames'] if result['frames'] is not None and result['outcome'] == 'ran' else '-'} | {notes} |")
    Path(path).write_text("\n".join(rows) + "\n", newline="\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--jobs", type=int, default=3)
    parser.add_argument("--only", default="")
    parser.add_argument("--resume", action="store_true", help="skip the chunks OUT/results.json has already")
    parser.add_argument("--reparse", action="store_true", help="the results made again from OUT's logs (no runs)")
    parser.add_argument("--report", help="write the markdown table of OUT/results.json to this file and stop")
    parser.add_argument("--app", help="the native build's executable")
    arguments = parser.parse_args()
    if arguments.app:
        global APP
        APP = Path(arguments.app)
    out = Path(arguments.out)
    if arguments.reparse:
        results = json.loads((out / "results.json").read_text())
        for chunk, old in results.items():
            results[chunk] = parse(chunk, out / chunk.replace("\\", "_"), old["status"], old["wall"])
        (out / "results.json").write_text(json.dumps(results, indent=1), newline="\n")
    if arguments.report:
        report(json.loads((out / "results.json").read_text()), arguments.report)
        return
    out.mkdir(parents=True, exist_ok=True)
    chunks = level_chunks()
    if arguments.only:
        wanted = {name.upper() for name in arguments.only.split(",")}
        chunks = [c for c in chunks if c.upper().split("\\")[-1] in wanted or c.upper() in wanted]
    results_path = out / "results.json"
    results = json.loads(results_path.read_text()) if results_path.exists() else {}
    if arguments.resume:
        chunks = [c for c in chunks if c not in results]
    with concurrent.futures.ThreadPoolExecutor(arguments.jobs) as pool:
        futures = {pool.submit(run, c, out, arguments.seconds, arguments.timeout): c for c in chunks}
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results[result["chunk"]] = result
            results_path.write_text(json.dumps(results, indent=1), newline="\n")
            print(f"{result['chunk']:42} {str(result['status']):8} {result['outcome']:28} load {result['load_seconds']} "
                  f"frees {result['stray_frees']} {result['fatal'][0][:90] if result['fatal'] else ''}", flush=True)


if __name__ == "__main__":
    main()
