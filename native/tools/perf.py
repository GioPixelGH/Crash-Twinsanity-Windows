r"""The performance measure (THE LIST 67): the same place played the same way, its frame's work read from the log, so a change to
the renderer is measured before and after it on this PC.

Each run plays the beach (the level sweep's start, $TWIN_SWEEP_LEVEL, with no input: $TWIN_SWEEP_IDLE, so the view is the same
every time) in a 1280x720 window at 1x, unlimited frames (refresh 'unlimited', v-sync off), the process on two cores (affinity
0x5: two physical cores where each has two threads), for --seconds of play, with a performance line every 10 s
($TWIN_PERFORMANCE_EVERY) and the renderer's own counts ($TWIN_GL_CALLS: OpenGL's calls, the GPU renderer's batches, copies,
uploads and read backs, a frame). The first line (the loading's end, the shader programs made) is left out; each run's numbers are
its lines' average, the summary the runs' median with their range. The numbers:

    fps            frames a second
    game           the game's thread's own work a frame (ms), waiting: its waits for the others, present: the window's
    vu1            VU1's thread
    render         the render thread, split into gs (the GS emulated on the processor), opengl (inside the graphics API's
                   calls: OpenGL's, or Vulkan's), cardwait (pictures read back) and picture (the picture handed to the window)
    card           the graphics card's own time a frame (timer queries)
    card ...       the card's time a frame by kind of call (ms): the render thread's draws, clears, copies, uploads, and the
                   window's thread's (the picture shown, the UI)
    gl ...         OpenGL calls a frame: all of them, draws, binds, uniforms, state, data uploads, blits, reads, other
    pass ...       the card's time a frame by the GPU renderer's pass (ms): its draws, its copies of a target and of the depth,
                   GS memory's conversions to and from the targets, textures decoded, the picture's copy, the window's
    hw ...         the GPU renderer's batches, textures decoded, GS memory's pages uploaded and downloaded, copies of a target,
                   pictures read back and fallbacks, a frame

    python native/tools/perf.py                                  # 3 runs of 60 s, the summary
    python native/tools/perf.py --out base.json                  # kept to compare with
    python native/tools/perf.py --before base.json               # each number with base's and the change
    python native/tools/perf.py --show base.json --before old.json   # two saved measures compared, nothing run
    --cores 0xFFF (all), --scale 4, --chunk Levels\Earth\Academy\Hub\..., --threads one ($TWIN_RENDER_THREAD=0), --runs, --seconds

Each run's settings folder gets the disc image's disc_identity.txt, as a played copy has it (the image's size, date, CRC-32 and
SHA-1, hashed once by this script and kept in build/native/perf_disc_identity.txt): without it a new settings folder hashes the
image in the background for its first 28-30 s, over the first measured line.
Paths are made absolute (a relative --work would give the runs a relative APPDATA). The window opens on the screen: leave the PC
alone while it runs (a run takes about --seconds + 30 s). Numbers from different PCs, or from runs with other programs busy, don't
compare."""
import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(HERE / "native" / "tools"))
import check_windows  # noqa: E402 (disc_image)

BEACH = "Levels\\Earth\\Hub\\Beach"
EVERY = 10
NUMBER = r"([\d.]+)"
# The performance line's numbers (sessionlog.cpp's FormatWork, each way the threads can be)
FIELDS = {
    "fps": r"performance: " + NUMBER + " fps",
    "game": r"the game " + NUMBER + " ms",
    "waiting": r"\(and " + NUMBER + r" ms waiting\)",
    "present": r"the window's present " + NUMBER + " ms",
    "vu1": r", VU1 " + NUMBER + " ms",
    "render": r"the render thread(?: with VU1)? " + NUMBER + " ms",
    "gs": r"\(the GS(?: and VU1)? " + NUMBER,
    "opengl": r", (?:OpenGL|Vulkan) " + NUMBER,
    "cardwait": r"waiting for the card " + NUMBER,
    "picture": r"the picture handed over " + NUMBER,
    "card": r"the graphics card " + NUMBER + " ms",
    "one": r"the game with VU1 and the GS " + NUMBER + " ms",
}
# The counts' line ($TWIN_GL_CALLS: hw/gl.cpp's and the GPU renderer's, a frame)
COUNTS = re.compile(r"graphics counts: (.*?) \(a frame")
# The GPU renderer's own counts in it (hwgs.cpp's stats); the rest OpenGL's calls, and the card's time by kind (ms)
# The card's time by the GPU renderer's pass ($TWIN_GL_CALLS: gputime.h's labels, ms a frame)
PASSES = re.compile(r"graphics card by pass: (.*?) \(ms a frame\)")
RENDERER = {"batches", "decodes", "page uploads", "page downloads", "copies", "readbacks", "fallbacks"}
ORDER = ["fps", "game", "waiting", "present", "vu1", "render", "gs", "opengl", "cardwait", "picture", "card", "one"]
# Smaller is better for all but the frames a second
BIGGER_BETTER = {"fps"}


def set_affinity(process, mask):
    if os.name == "nt":
        kernel = ctypes.windll.kernel32
        kernel.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        if not kernel.SetProcessAffinityMask(ctypes.c_void_p(int(process._handle)), mask):
            sys.exit(f"the affinity {mask:#x} couldn't be set")
    else:
        os.sched_setaffinity(process.pid, {bit for bit in range(64) if mask >> bit & 1})


def disc_identity_line(image):
    """discidentity.cpp's kept line for the image ("<bytes> <date> <crc> <sha1>", the date its last write time in ns), hashed once
    and kept in build/native/perf_disc_identity.txt"""
    stat = os.stat(image)
    key = f"{stat.st_size} {stat.st_mtime_ns} "
    cache = HERE / "build" / "native" / "perf_disc_identity.txt"
    kept = cache.read_text().splitlines() if cache.exists() else []
    for line in kept:
        if line.startswith(key):
            return line
    crc, sha1 = 0, hashlib.sha1()
    with open(image, "rb") as disc:
        while block := disc.read(1 << 24):
            crc = zlib.crc32(block, crc)
            sha1.update(block)
    line = f"{key}{crc:08x} {sha1.hexdigest()}"
    cache.write_text("".join(f"{each}\n" for each in kept + [line]), newline="\n")
    return line


def one_run(args, index):
    folder = Path(args.work) / f"run{index}"
    if folder.exists():
        shutil.rmtree(folder)
    settings = folder / "settings" / "Crash Twinsanity"
    settings.mkdir(parents=True)
    (settings / "disc_image.txt").write_text(check_windows.disc_image() + "\n")
    (settings / "disc_identity.txt").write_text("# Crash Twinsanity's disc images recognised (the log's \"disc:\" line): each image's "
                                                "size in bytes, its date, its CRC-32 and SHA-1\n" +
                                                disc_identity_line(check_windows.disc_image()) + "\n", newline="\n")
    (settings / "options.txt").write_text("pause_on_focus_loss=0\nmouse=0\nmouse_look=0\nrefresh_60hz=4\nvsync=0\n" +
                                          "".join(f"{option}\n" for option in args.option))
    (settings / "resolution.txt").write_text(f"scale={args.scale}\nwindow=1280x720\nfullscreen=0\n")
    env = dict(os.environ, HOME=str(folder / "settings"), APPDATA=str(folder / "settings"), TWIN_SWEEP_LEVEL=args.chunk,
               TWIN_SWEEP_IDLE="1", TWIN_SWEEP_SECONDS=str(args.seconds + EVERY), TWIN_SWEEP_TIMEOUT="180",
               TWIN_PERFORMANCE_EVERY=str(EVERY))
    if not args.no_counts:
        env["TWIN_GL_CALLS"] = "1"
    if args.threads == "one":
        env["TWIN_RENDER_THREAD"] = "0"
    for pair in args.env:
        key, value = pair.split("=", 1)
        env[key] = value
    with open(folder / "log.txt", "w") as log:
        process = subprocess.Popen([str(args.exe), "--iso", check_windows.disc_image()], env=env, stdout=log,
                                   stderr=subprocess.STDOUT, cwd=folder)
        set_affinity(process, args.cores)
        started = time.time()
        while process.poll() is None and time.time() - started < args.seconds + 240:
            time.sleep(1)
        if process.poll() is None:
            process.kill()
        process.wait()
    text = (folder / "log.txt").read_text(errors="replace")
    lines = [line for line in text.splitlines() if "] performance: " in line]
    counts = [match.group(1) for match in map(COUNTS.search, text.splitlines()) if match]
    passes = [match.group(1) for match in map(PASSES.search, text.splitlines()) if match]
    if len(lines) < 2:
        sys.exit(f"run {index}: {len(lines)} performance lines (exit {process.returncode}): see {folder / 'log.txt'}")
    place = re.search(r"the player in (\S+)", lines[-1])
    if place is None or place.group(1).lower() != args.chunk.lower():
        sys.exit(f"run {index}: played in {place.group(1) if place else '?'}, not {args.chunk}: see {folder / 'log.txt'}")
    numbers = {}
    for name, pattern in FIELDS.items():
        values = [float(found.group(1)) for found in (re.search(pattern, line) for line in lines[1:]) if found]
        if values:
            numbers[name] = sum(values) / len(values)
    # The counts' lines follow the performance lines: the first left out the same way
    sums = {}
    for line in counts[1:]:
        for name, value in re.findall(r"([a-z][a-z ]*?) ([\d.]+)(?:,|$)", line):
            name = name.strip()
            key = name if name.startswith("card ") else ("hw " if name in RENDERER else "gl ") + name
            sums.setdefault(key, []).append(float(value))
    for line in passes[1:]:
        for item in line.split(";"):
            name, _, value = item.strip().rpartition(" ")
            sums.setdefault("pass " + name, []).append(float(value))
    for name, values in sums.items():
        numbers[name] = sum(values) / len(values)
    return numbers


def summary(runs):
    names = [name for name in ORDER if all(name in run for run in runs)]
    names += sorted({name for run in runs for name in run if name.startswith(("card ", "gl ", "hw ", "pass "))} - set(names))
    return {name: {"median": statistics.median(run[name] for run in runs if name in run),
                   "low": min(run[name] for run in runs if name in run), "high": max(run[name] for run in runs if name in run)}
            for name in names}


def show(result, before=None):
    print(f"{result['chunk']} at {result['scale']}x, cores {result['cores']}, {result['threads']} thread(s), {result['runs']} runs "
          f"of {result['seconds']} s, {result.get('exe', '?')}")
    for name, value in result["summary"].items():
        digits = 3 if name.startswith("pass ") else 2 if name.startswith("card") else 1
        line = f"  {name:22} {value['median']:9.{digits}f}   ({value['low']:.{digits}f}-{value['high']:.{digits}f})"
        if before and name in before["summary"]:
            old = before["summary"][name]["median"]
            change = (value["median"] - old) / old * 100 if old else 0.0
            better = change > 0 if name in BIGGER_BETTER else change < 0
            line += f"   before {old:9.{digits}f}  {change:+6.1f}%" + (" better" if better and abs(change) >= 3 else
                                                                   " worse" if not better and abs(change) >= 3 else "")
        print(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default=str(HERE / "build/native/cmake/CrashTwinsanity.exe"))
    parser.add_argument("--work", default=str(HERE / "build/native/perf"))
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--seconds", type=int, default=60, help="the seconds of play measured in each run (after the first 10)")
    parser.add_argument("--cores", type=lambda text: int(text, 0), default=0x5, help="the affinity mask (0x5: two cores)")
    parser.add_argument("--scale", type=int, default=1)
    parser.add_argument("--chunk", default=BEACH, help="the chunk's full name")
    parser.add_argument("--threads", choices=["three", "one"], default="three")
    parser.add_argument("--no-counts", action="store_true", help="without $TWIN_GL_CALLS (an older program has no counts)")
    parser.add_argument("--option", action="append", default=[], help="an options.txt line more (repeatable)")
    parser.add_argument("--env", action="append", default=[], help="NAME=VALUE for the runs (repeatable)")
    parser.add_argument("--out", help="the result saved as JSON")
    parser.add_argument("--before", help="a saved result to compare with")
    parser.add_argument("--show", help="a saved result shown (nothing run)")
    args = parser.parse_args()
    before = json.loads(Path(args.before).read_text()) if args.before else None
    if args.show:
        show(json.loads(Path(args.show).read_text()), before)
        return
    args.work = str(Path(args.work).resolve())
    # (A player's portable folder, as build/native/dist's, is copied first: its User folder never used)
    args.exe = str(check_windows.playable_copy(Path(args.exe).resolve(), Path(args.work) / "program"))
    runs = []
    for index in range(args.runs):
        runs.append(one_run(args, index))
        print(f"run {index + 1}/{args.runs}: {runs[-1].get('fps', 0):.1f} fps, the render thread {runs[-1].get('render', 0):.1f} ms",
              flush=True)
    result = {"chunk": args.chunk, "scale": args.scale, "cores": f"{args.cores:#x}", "threads": args.threads, "runs": args.runs,
              "seconds": args.seconds, "exe": args.exe, "options": args.option, "env": args.env, "each": runs,
              "summary": summary(runs), "when": time.strftime("%Y-%m-%d %H:%M")}
    show(result, before)
    if args.out:
        Path(args.out).write_text(json.dumps(result, indent=1) + "\n", newline="\n")


if __name__ == "__main__":
    main()
