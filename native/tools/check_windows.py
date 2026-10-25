#!/usr/bin/env python3
"""The checks a change needs before it's committed (native/WINDOWS.md, "Checking a change"), each summed up in a line: the
self-tests, the places that once crashed played again, the pause menu's hints, the camera turned by the mouse, the physics past
60 Hz against 60 Hz's, portable mode's cases, the pictures against another build's and, with --sweep, every level chunk against
the last sweep's results.
The runs' logs and pictures go to build/native/check/.

    .venv\\Scripts\\python.exe native\\tools\\check_windows.py [--build] [--exe PATH] [--reference PATH] [--sweep]

--build builds first (build_windows.py). The program checked is build/native/cmake/CrashTwinsanity.exe unless --exe says;
the pictures are compared with --reference's (by default the last packaged build's, build/native/dist/). Every run has a
settings folder of its own (none of the player's), and the disc image is local.json's. Exits 1 when a check fails. --physics
runs only the physics' check; --portable only portable mode's; --scenarios only the scenarios; --fps-shots FOLDER only the FPS
counter's, with a window (the counter counts the pictures shown).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]
TOOLS = HERE / "native" / "tools"
SELF_TESTS = ["abi", "lowmemory", "saves", "ui"]
# Places that crashed once, played again: a name, the chunk the sweep starts in, the seconds played and the hops
# (native/WINDOWS.md, "Debugging"), and for some what else the run is: options.txt's lines (the cutscene skip on), the run's
# variables, and what its log must and mustn't have (regular expressions)
SKIPPING = {"options": "skip_cutscenes=1\n"}
SCENARIOS = [
    ("Slip Slide's mine cradles (1.0.5)", "Levels\\Ice\\SlipSlide\\l05start", 60,
     "wait34,idle,wait4,bring:mine_cradle,wait6,bring:mine_cradle,wait6"),
    ("Dingodile beaten, the boiler room's lock and the Academy (1.0.7)", "Levels\\school\\Boiler\\boiler_6", 65,
     "wait20,state:dingodile_bossfight_director_activated:3,wait12,boil2lck,wait5,sch_hub,wait8"),
    # Issue #9: the angry skunk's scene skipped, then the skunk attacked at once, while it still plays the scene's behaviour (left
    # taking its object from the scene's director it was drawn as Crash, or crashed on an "OGI" read past the director's models)
    ("The skunk's scene skipped, the skunk attacked at once (#9)", "Levels\\Earth\\Hub\\highpath", 30,
     "idle,goto:-9:-4:-50,input,on:skunk:0,wait3,on:skunk:0,wait3,on:skunk:0,wait3",
     dict(SKIPPING, env={"TWIN_SKIP_TEST": "0.2"},
          expect=[r"skip: .*COM_ANGRY_SKUNK_CUSCENE_MAIN skipped: .*given back as they finish: .*act_GLOBAL_SKUNK"])),
    # ... and given back its own object once it's done (the training scene before it too: Coco's creature, Crash), as the scene
    # played out leaves them
    ("The skunk's scene skipped, the skunk given back (#9)", "Levels\\Earth\\Hub\\highpath", 30, "idle,goto:-9:-4:-50,wait15",
     dict(SKIPPING, env={"TWIN_SKIP_TEST": "0.2", "TWIN_SCENE_STATE": "10"},
          expect=[r"skip: .* after the skip, actors given their own object back: .*act_GLOBAL_SKUNK",
                  r"scene state: the player \|Highpath\|act_CRASH, inputs locked 00000000"],
          forbid=[r"scene state: .* takes its object from", r"inputs locked ffffffff"])),
    # Issue #9: Cortex chased by the bear (Doc Amok), the scene skipped: the controls stayed locked
    ("The bear's scene skipped, the controls given back (#9)", "Levels\\Earth\\DocAmok\\docamok3", 12, "",
     dict(SKIPPING, env={"TWIN_SKIP_TEST": "0.2", "TWIN_SCENE_STATE": "1"},
          expect=[r"skip: .*COM_DOCAMOK_BEAR_CUTSCENE skipped: .*the inputs given back",
                  r"scene state: the player .*inputs locked 00000000"],
          forbid=[r"inputs locked ffffffff"])),
    # Issue #9: Rooftop Rampage's scene in roofcor2 skipped as a player reaches it, as Nina (flown through the bus chase from its
    # start, into roofcor2 by its link): the scene makes Cortex the player, whose idle after it was the director's slot, Nina's
    # scene behaviour, which left him driven by its animations, unable to move (started in roofcor2 he's the player already)
    ("Rooftop Rampage's scene skipped arriving as Nina, Cortex moves (#9)", "Levels\\school\\RoofTop\\buschase", 80,
     "idle,wait2,fly:1.2:-3:160:25,fly:11:-3:167:25,fly:26.6:-4:168.5:15,wait2,fly:37.6:-0.5:47:10,roofcor2,wait3,"
     "fly:-9:-1:1:10,fly:5.5:-1:0:5,wait3,input,wait4,moved:2",
     dict(SKIPPING, env={"TWIN_SKIP_TEST": "0.2", "TWIN_SCENE_STATE": "3", "TWIN_TRIGGER_PLAN": "list"},
          expect=[r"skip: .*COM_CUTSCENE_H03D skipped: .*the character switched as it ends; .*\|BusChase\|act_NINA1",
                  r"\|RoofCor2\|act_CORTEX asked for its slot 12, given its own object back: its own COM_CORTEX_IDLE",
                  r"scene state: the player \|RoofCor2\|act_CORTEX, inputs locked 00000000, .*COM_CORTEX_IDLE",
                  r"hop: the player \S+act_CORTEX moved [\d.]+ units since the last step, at least"])),
]
# The pause menu's hints checked for each controller's prompts and the menu hints option (hintstest.cpp): the prompts' style
# ($TWIN_UI_PROMPTS) and the option (options.txt's menu_hints: 0 the game's own hints, 1 the bar). The pause on the window's focus
# lost is off: a run with a window (the shots) isn't the window in front
HINTS = [("playstation", 0), ("xbox", 0), ("switch", 0), ("keyboard", 0), ("xbox", 1)]
# The FPS counter checked with a window (fpstest.cpp): a name, the show FPS option (options.txt's show_fps), the refresh option
# (refresh_60hz: 0 50 Hz, 1 60 Hz, 3 144 Hz) and v-sync (off for 144 Hz: a 60 Hz screen's refresh would hold it to 60)
FPS_CASES = [("60 Hz", 1, 1, 1), ("50 Hz", 1, 0, 1), ("144 Hz", 1, 3, 0), ("off", 0, 1, 1)]
# What a scenario's log mustn't have besides a crash: the disk manager's pool out of room (game/disk.cpp), and DMA reading
# memory that isn't there (what a texture's data overwritten looks like before the crash)
MEMORY_PROBLEMS = ["disk: no room", "reads from no memory"]
# The physics past 60 Hz against 60 Hz's (physicstest.cpp; native/WINDOWS.md, "Frame rates past 60"), each rate's name, its frames
# ($TWIN_FIXED_TIME: "144,144,60" a 144 Hz screen's with a slow frame now and then) and the refresh option (options.txt's
# refresh_60hz: 1 60 Hz, 4 unlimited), 60 Hz's first. Played at each: the beach's jumps (issue #7: the slide jump twice as
# long), and Crash stepping off Cavern Catastrophe's first lift as it rises with Cortex tied to him (issue #8: thrown up)
PHYSICS_RATES = [("60 Hz", "60", 1), ("144 Hz uneven", "144,144,60", 4), ("240 Hz uneven", "240,240,240,40", 4)]
# How far from 60 Hz's a measure may be, a share of it and at least so much: a jump's distance along the ground, its height and its
# time in the air (seconds: 60 Hz's frames measure it in 60ths), and the lift's hop (its height)
PHYSICS_TOLERANCE = {"along": (0.06, 0.2), "high": (0.02, 0.05), "air": (0.0, 0.05), "hop": (0.0, 0.15)}
LIFT_CHUNK = "Levels\\Earth\\Cavern\\nitrocav"
# Portable mode (native/WINDOWS.md, "Portable mode"): the program copied to a folder of its own with a settings folder of its own as
# %APPDATA% (never the player's), started as Explorer starts it (no console, no output: its log goes to log.txt in the settings
# folder in use). The chunk a run plays a few seconds of, and the data the settings folder starts with (a save, a mod of 20 MB)
PORTABLE_CHUNK = "Levels\\Earth\\Hub\\beach"
PORTABLE_DATA = {"options.txt": (b"# a test's\nshow_fps=1\npause_on_focus_loss=0\n", 1),
                 "memorycard/BESLES-52568CRASH/TEST.DAT": (bytes(range(256)), 32),
                 "mods/test/mod.bin": (bytes(range(255, -1, -1)), 80 * 1024)}
# What starts it as Explorer does, run by pythonw (no console: python.exe made detached gets a console of its own, which the
# program would attach to): the program started with no output handles and no console of its parent's (entry.cpp's SetUpLog)
PORTABLE_LAUNCHER = ("import subprocess, sys\n"
                     "none = subprocess.STARTUPINFO(dwFlags=subprocess.STARTF_USESTDHANDLES, hStdInput=0, hStdOutput=0, hStdError=0)\n"
                     "try:\n"
                     "    sys.exit(subprocess.run(sys.argv[2:], startupinfo=none, timeout=float(sys.argv[1])).returncode)\n"
                     "except subprocess.TimeoutExpired:\n"
                     "    sys.exit(99)\n")


def disc_image(root=HERE):
    """The disc image local.json names"""
    local = root / "local.json"
    if not local.exists():
        sys.exit(f"{local} is missing: it names the disc image (local.example.json)")
    image = json.loads(local.read_text()).get("disc_image", "")
    if not Path(image).exists():
        sys.exit(f"local.json's disc image isn't there: {image}")
    return image


def run_game(exe, folder, arguments, environment=None, timeout=900, options=None):
    """The program run with a settings folder of its own (the disc image remembered there, and options.txt's lines if given), its
    log in folder/log.txt: (exit status, log)"""
    folder = Path(folder)
    if folder.exists():
        shutil.rmtree(folder)
    settings = folder / "settings" / "Crash Twinsanity"
    settings.mkdir(parents=True)
    (settings / "disc_image.txt").write_text(disc_image() + "\n")
    if options:
        (settings / "options.txt").write_text(options)
    env = dict(os.environ, HOME=str(folder / "settings"), APPDATA=str(folder / "settings"), TWINSANITY_AUDIO="off")
    env.update(environment or {})
    with open(folder / "log.txt", "w") as log:
        try:
            status = subprocess.run([str(exe)] + arguments, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout,
                                    cwd=folder).returncode
        except subprocess.TimeoutExpired:
            status = "timeout"
    return status, (folder / "log.txt").read_text(errors="replace")


def self_tests(exe, work):
    """Each self-test's result: [(name, passed, its last line)]"""
    def one(name):
        arguments = [f"--selftest-{name}"] + (["--iso", disc_image()] if name == "ui" else [])
        status, log = run_game(exe, work / f"selftest-{name}", arguments, timeout=600)
        last = log.strip().splitlines()[-1] if log.strip() else ""
        return name, status == 0 and "pass" in last, last
    with ThreadPoolExecutor(2) as pool:
        return list(pool.map(one, SELF_TESTS))


def scenarios(exe, work):
    """Each scenario's result: [(name, passed, what happened)]"""
    def one(scenario):
        name, chunk, seconds, hops, *more = scenario
        more = more[0] if more else {}
        folder = work / ("scenario-" + "".join(c if c.isalnum() else "_" for c in name))
        environment = {"TWIN_SWEEP_LEVEL": chunk, "TWIN_SWEEP_SECONDS": str(seconds), "TWIN_SWEEP_TIMEOUT": "180",
                       "TWIN_HOP": hops, "TWIN_DETAILED_LOG": "1", **more.get("env", {})}
        status, log = run_game(exe, folder, ["--headless", "--quiet-stubs"], environment, timeout=seconds * 6 + 300,
                               options=more.get("options"))
        fatal = [line for line in log.splitlines() if line.startswith("[native] fatal")]
        played = any("sweep:" in line and (": ran (" in line or ": game over (" in line) for line in log.splitlines())
        problems = [line for line in log.splitlines() if any(problem in line for problem in MEMORY_PROBLEMS)]
        missing = [pattern for pattern in more.get("expect", []) if not re.search(pattern, log)]
        found = [line for pattern in more.get("forbid", []) for line in log.splitlines() if re.search(pattern, line)]
        if fatal:
            return name, False, fatal[0].replace("[native] ", "")
        if problems:
            return name, False, f"{len(problems)} memory problems, the first: {problems[0].replace('[native] ', '')}"
        if status != 0 or not played:
            return name, False, f"exit {status}, played: {played}"
        if missing:
            return name, False, f"nothing in the log like {missing[0]!r}"
        if found:
            return name, False, f"the log has {found[0].replace('[native] ', '')!r}"
        return name, True, "no crash, no memory problem" + (", the skips' ends as expected" if more else "")
    with ThreadPoolExecutor(3) as pool:
        return list(pool.map(one, SCENARIOS))


def menu_hints(exe, work, shots=None):
    """The pause menu's hints (hintstest.cpp) for each of HINTS: shown as the option has them, their lines as the prompts label
    them, and the game's own clicked with the mouse (the pages' arrows, back). With shots (a folder) the window is saved there,
    run with a window rather than headless: [(name, passed, what happened)]"""
    def one(case):
        style, bar = case
        name = f"{style}, {'bar' if bar else 'original'}"
        folder = work / f"hints-{style}-{'bar' if bar else 'original'}"
        pictures = Path(shots) / f"{style}-{'bar' if bar else 'original'}" if shots else None
        if pictures:
            pictures.mkdir(parents=True, exist_ok=True)
        status, log = run_game(exe, folder, ["--quick"] + ([] if pictures else ["--headless"]),
                               {"TWIN_HINTS_TEST": str(pictures.resolve()) if pictures else "1", "TWIN_UI_PROMPTS": style},
                               timeout=300, options=f"menu_hints={bar}\npause_on_focus_loss=0\n")
        lines = [line.split("hints test: ", 1)[1] for line in log.splitlines() if "hints test: " in line]
        hints = next((line for line in lines if line.startswith(("original, the hints", "bar, the hints"))), "")
        hints = hints.split(", the hints ", 1)[1].rsplit(" (", 1)[0] if hints else ""
        last = lines[-1] if lines else f"exit {status}, no result"
        return name, status == 0 and last.startswith("pass"), f"{last}; {hints}"
    with ThreadPoolExecutor(1 if shots else 2) as pool:
        return list(pool.map(one, HINTS))


def fps_counter(exe, work, shots):
    """The FPS counter (fpstest.cpp) for each of FPS_CASES, with a window: the frames a second it shows against the game's own (and
    at 50 and 60 Hz the game holding its rate), or none with the option off; the window's pictures saved in shots (a folder for each
    case, fps_play.png and fps_pause.png): [(name, passed, what happened)]"""
    results = []
    for name, shown, refresh, vsync in FPS_CASES:
        slug = name.replace(" ", "-").lower()
        pictures = Path(shots) / slug
        pictures.mkdir(parents=True, exist_ok=True)
        status, log = run_game(exe, work / f"fps-{slug}", ["--quick"], {"TWIN_FPS_TEST": str(pictures.resolve())}, timeout=300,
                               options=f"show_fps={shown}\nrefresh_60hz={refresh}\nvsync={vsync}\npause_on_focus_loss=0\n")
        lines = [line.split("fps test: ", 1)[1] for line in log.splitlines() if "fps test: " in line]
        last = lines[-1] if lines else f"exit {status}, no result"
        counted = "; ".join(line for line in lines[:-1] if line.startswith(("playing", "paused")))
        results.append((name, status == 0 and last == "pass", f"{last} ({counted})"))
    return results


def mouse_look(exe, work):
    """The camera turned by the mouse (looktest.cpp): the defaults at the stick's full deflection (the game's fastest turn), a
    sensitivity of 10 six times that, a camera speed of 10 twice: (passed, what happened)"""
    status, log = run_game(exe, work / "look", ["--quick", "--headless"], {"TWIN_LOOK_TEST": "1"}, timeout=300,
                           options="pause_on_focus_loss=0\n")
    lines = [line.split("look test: ", 1)[1] for line in log.splitlines() if "look test: " in line]
    turns = [line.split(": ", 1)[1].replace(" degrees a second", "") for line in lines if line.startswith("sensitivity")]
    last = lines[-1] if lines else f"exit {status}, no result"
    return status == 0 and last == "pass", f"{last} ({', '.join(turns)} degrees a second)"


def physics(exe, work):
    """The jumps and the lift's hop at each of PHYSICS_RATES (physicstest.cpp) against 60 Hz's: [(name, passed, what happened)]"""
    def jumps(rate):
        name, frames, refresh = rate
        status, log = run_game(exe, work / f"physics-jumps-{frames.replace(',', '-')}", ["--quick", "--headless", "--quiet-stubs"],
                               {"TWIN_PHYSICS_TEST": "jumps", "TWIN_FIXED_TIME": frames, "TWIN_PHYSICS_WAY": "-90"}, timeout=900,
                               options=f"refresh_60hz={refresh}\npause_on_focus_loss=0\n")
        lines = [line.split("physics test: ", 1)[1] for line in log.splitlines() if "physics test: " in line]
        moves = {}
        for line in lines:
            measure = re.match(r"(.+): ([\d.]+) s in the air, ([\d.]+) along the ground, ([\d.]+) high", line)
            if measure:
                moves[measure[1]] = {"air": float(measure[2]), "along": float(measure[3]), "high": float(measure[4])}
        last = lines[-1] if lines else f"exit {status}, no result"
        return moves if status == 0 and last == "done" else last

    def lift(rate):
        name, frames, refresh = rate
        status, log = run_game(exe, work / f"physics-lift-{frames.replace(',', '-')}", ["--headless", "--quiet-stubs"],
                               {"TWIN_SWEEP_LEVEL": LIFT_CHUNK, "TWIN_SWEEP_SECONDS": "600", "TWIN_SWEEP_TIMEOUT": "180",
                                "TWIN_SWEEP_IDLE": "1", "TWIN_PHYSICS_TEST": "step:0.3", "TWIN_PHYSICS_WAY": "90",
                                "TWIN_PHYSICS_ON": "cavern_lift17", "TWIN_PHYSICS_TIED": "1", "TWIN_PHYSICS_SECONDS": "8",
                                "TWIN_FIXED_TIME": frames}, timeout=600, options=f"refresh_60hz={refresh}\npause_on_focus_loss=0\n")
        lines = [line.split("physics test: ", 1)[1] for line in log.splitlines() if "physics test: " in line]
        hops = [float(hop[1]) for line in lines for hop in [re.match(r"off the ground [\d.]+ s, ([\d.]+) high", line)] if hop]
        if status != 0 or "watched" not in lines or "the player tied to the second" not in lines or not hops:
            return f"exit {status}: {lines[-1] if lines else 'no result'}"
        return max(hops)

    def allowed(kind, value):
        share, least = PHYSICS_TOLERANCE[kind]
        return max(share * value, least)

    with ThreadPoolExecutor(3) as pool:
        jumping = [pool.submit(jumps, rate) for rate in PHYSICS_RATES]
        lifting = [pool.submit(lift, rate) for rate in PHYSICS_RATES]
        jumped = [future.result() for future in jumping]
        lifted = [future.result() for future in lifting]
    results = []
    reference, reference_hop = jumped[0], lifted[0]
    if isinstance(reference, str) or isinstance(reference_hop, str):
        return [("at 60 Hz", False, reference if isinstance(reference, str) else reference_hop)]
    for (name, _, _), moves, hop in list(zip(PHYSICS_RATES, jumped, lifted))[1:]:
        if isinstance(moves, str) or isinstance(hop, str):
            results.append((name, False, moves if isinstance(moves, str) else hop))
            continue
        # Each measure's difference from 60 Hz's over its tolerance (past 1: too far), the largest kept
        worst, worst_what = 0.0, ""
        for move, measures in reference.items():
            ours = moves.get(move)
            if ours is None:
                worst, worst_what = 2.0, f"{move} not measured"
                break
            for kind, value in measures.items():
                apart = abs(ours[kind] - value)
                share = apart / allowed(kind, value)
                if share > worst:
                    unit = " s in the air" if kind == "air" else " along" if kind == "along" else " high"
                    worst, worst_what = share, f"{move} {ours[kind]:.2f}{unit} (60 Hz {value:.2f})"
        hop_share = abs(hop - reference_hop) / allowed("hop", reference_hop)
        passed = worst <= 1.0 and hop_share <= 1.0
        results.append((name, passed, f"the jumps' most apart: {worst_what}; "
                                      f"the lift's hop {hop:.2f} high (60 Hz {reference_hop:.2f})"))
    return results


def snapshot(folder, logs=True):
    """A folder's files (relative paths) and their SHA-1s; without the logs if logs is False"""
    folder = Path(folder)
    if not folder.is_dir():
        return {}
    return {path.relative_to(folder).as_posix(): hashlib.sha1(path.read_bytes()).hexdigest()
            for path in folder.rglob("*") if path.is_file() and (logs or path.name not in ("log.txt", "log.previous.txt"))}


def portable(exe, work):
    """Portable mode's cases, played in turn on a copy of the program (portable.cpp; portabletest.cpp drives the options' switch
    as a player would): [(name, passed, what happened)]"""
    root = work / "portable"
    user_name = f"{os.environ.get('USERDOMAIN', '')}\\{os.environ.get('USERNAME', '')}"
    locked = root / "locked"
    if locked.exists():
        subprocess.run(["icacls", str(locked), "/remove:d", user_name, "/t", "/q"], capture_output=True)
    if root.exists():
        shutil.rmtree(root)
    appdata = root / "appdata"
    settings = appdata / "Crash Twinsanity"
    shots = root / "shots"
    play = {"TWIN_SWEEP_LEVEL": PORTABLE_CHUNK, "TWIN_SWEEP_SECONDS": "3", "TWIN_SWEEP_TIMEOUT": "120"}

    def program(folder):
        """The program with what it needs beside it (the DLLs, the fonts) copied to folder"""
        folder.mkdir(parents=True)
        shutil.copy2(exe, folder / exe.name)
        for dll in exe.parent.glob("*.dll"):
            shutil.copy2(dll, folder / dll.name)
        (folder / "fonts").mkdir()
        for font in (HERE / "native" / "third_party" / "fonts").glob("*.ttf"):
            shutil.copy2(font, folder / "fonts" / font.name)
        return folder

    def run(game, environment, timeout=300):
        """The copy in game run headless with appdata as %APPDATA%: its exit status (99: timed out)"""
        env = dict(os.environ, APPDATA=str(appdata), HOME=str(appdata), TWINSANITY_AUDIO="off", **environment)
        env.pop("TWINSANITY_ISO", None)
        return subprocess.run([str(Path(sys.executable).with_name("pythonw.exe")), "-c", PORTABLE_LAUNCHER, str(timeout),
                               str(game / exe.name), "--headless", "--quiet-stubs"], env=env, cwd=game,
                              timeout=timeout + 60).returncode

    def log_of(folder):
        path = folder / "log.txt"
        return path.read_text(errors="replace") if path.exists() else ""

    def result(name, problems, fine):
        return name, not problems, "; ".join(problems) if problems else fine

    results = []
    game = program(root / "Crash Twinsanity")
    user = game / "User"
    for name, (pattern, times) in PORTABLE_DATA.items():
        (settings / name).parent.mkdir(parents=True, exist_ok=True)
        (settings / name).write_bytes(pattern * times)
    (settings / "disc_image.txt").write_text(disc_image() + "\n")

    # 1. A portable.txt made by hand and no User folder: the first start copies %APPDATA%'s data there and writes nothing to it
    (game / "portable.txt").write_text("")
    before = snapshot(settings)
    status = run(game, play)
    log, copied, problems = log_of(user), snapshot(user), []
    if status != 0:
        problems.append(f"exit {status}")
    if any(copied.get(name) != digest for name, digest in before.items()):
        problems.append("User isn't %APPDATA%'s data")
    if snapshot(settings) != before:
        problems.append("%APPDATA% written to")
    if "portable mode's first start" not in log or "settings and saves in the User folder" not in log or "sweep: " not in log:
        problems.append("User\\log.txt doesn't say it")
    results.append(result("first start", problems, f"{len(before)} files copied to User, the log there, %APPDATA% untouched"))

    # 2. Turned on in the options (portable.txt gone, an older User folder there): %APPDATA%'s data copied, the old User folder
    # set aside, portable.txt made, the game closed
    (game / "portable.txt").unlink()
    (user / "old.txt").write_text("the User folder before\n")
    old_user = snapshot(user)
    status = run(game, {"TWIN_PORTABLE_TEST": str(shots / "on")})
    log, problems = log_of(settings), []
    backups = sorted(game.glob("User (backup *)"))
    if status != 0 or "portable test: done" not in log or "portable mode on: the game closes" not in log:
        problems.append(f"exit {status}, {'the switch done' if 'portable test: done' in log else 'no switch'}")
    if not (game / "portable.txt").exists():
        problems.append("no portable.txt")
    if not backups or snapshot(backups[-1]) != old_user:
        problems.append("the old User folder not set aside whole")
    if snapshot(user, logs=False) != snapshot(settings, logs=False):
        problems.append("User isn't %APPDATA%'s data")
    results.append(result("turned on", problems, f"copied, the old User folder kept as \"{backups[-1].name if backups else ''}\""))

    # 3. Played portable, then turned off: nothing written to %APPDATA% meanwhile (what it held is set aside whole), User's data
    # copied there, portable.txt removed
    appdata_before = snapshot(settings)
    status = run(game, {"TWIN_PORTABLE_TEST": str(shots / "off")})
    log, problems = log_of(user), []
    backups = sorted(appdata.glob("Crash Twinsanity (backup *)"))
    if status != 0 or "portable test: done" not in log or "portable mode off: the game closes" not in log:
        problems.append(f"exit {status}, {'the switch done' if 'portable test: done' in log else 'no switch'}")
    if (game / "portable.txt").exists():
        problems.append("portable.txt still there")
    if not backups or snapshot(backups[-1]) != appdata_before:
        problems.append("%APPDATA% written to while portable, or not set aside whole")
    if snapshot(settings, logs=False) != snapshot(user, logs=False):
        problems.append("%APPDATA% isn't User's data")
    results.append(result("turned off", problems, f"copied back, %APPDATA%'s old folder kept as \"{backups[-1].name if backups else ''}\""))

    # 4. Portable with no disc image remembered: the one in the program's folder found (not the decoy before it), remembered relative
    (game / "portable.txt").write_text("")
    (user / "disc_image.txt").unlink(missing_ok=True)
    (game / "A decoy.iso").write_bytes(b"not a disc image\n" * 1024)
    try:
        os.link(disc_image(), game / "Crash Twinsanity.ISO")
    except OSError as error:
        results.append(("disc image found", False, f"the disc image can't be linked into the work folder: {error}"))
        return results
    status = run(game, play)
    log, problems = log_of(user), []
    remembered = (user / "disc_image.txt").read_text().strip() if (user / "disc_image.txt").exists() else ""
    if status != 0 or "sweep: " not in log:
        problems.append(f"exit {status}")
    if "the disc image found in the program's folder" not in log or "A decoy.iso" not in log:
        problems.append("not found in the folder")
    if remembered != "Crash Twinsanity.ISO":
        problems.append(f"remembered as \"{remembered}\"")
    results.append(result("disc image found", problems, "the decoy passed over, remembered as \"Crash Twinsanity.ISO\""))

    # 5. The folder moved (renamed): the image remembered relative is still there
    moved = game.with_name("Crash Twinsanity moved")
    game.rename(moved)
    status = run(moved, play)
    log, problems = log_of(moved / "User"), []
    opened = next((line for line in log.splitlines() if "] disc image " in line), "")
    if status != 0 or "sweep: " not in log:
        problems.append(f"exit {status}")
    if "Crash Twinsanity moved" not in opened:
        problems.append(f"opened: {opened or 'nothing'}")
    results.append(result("folder moved", problems, "the image opened from the moved folder"))

    # 6. A folder that can't be written to with portable.txt: the settings stay in %APPDATA%, the options' switch refuses. As
    # C:\Program Files is to a user: no files or folders can be made in it (the files' own rights as they were, so it runs)
    program(locked)
    (locked / "portable.txt").write_text("")
    subprocess.run(["icacls", str(locked), "/deny", f"{user_name}:(WD,AD)", "/q"], capture_output=True, check=True)
    try:
        status = run(locked, {"TWIN_PORTABLE_TEST": str(shots / "locked")})
    finally:
        subprocess.run(["icacls", str(locked), "/remove:d", user_name, "/t", "/q"], capture_output=True)
    log, problems = log_of(settings), []
    if status != 0 or "portable test: pass (refused" not in log:
        problems.append(f"exit {status}, {'refused' if 'portable test: refused' in log else 'not refused'}")
    if "its folder can't be used for portable mode" not in log:
        problems.append("the start didn't say portable.txt can't be used")
    if (locked / "User").exists():
        problems.append("a User folder made")
    results.append(result("folder not writable", problems, "the settings kept in %APPDATA%, the switch refused"))
    return results


# The disc images a player may keep beside the program (portable mode looks for them there)
DISC_SUFFIXES = (".iso", ".bin", ".img")


def playable_copy(program, folder):
    """The program to run for a check: itself, or, when its folder is a player's portable one (a portable.txt or a User folder
    beside it: build/native/dist/ is the user's), a copy of its own files in folder (never the player's settings, saves or disc
    images), so the run can't read or write them"""
    program = Path(program)
    home = program.parent
    if not (home / "portable.txt").exists() and not (home / "User").exists():
        return program
    if folder.exists():
        shutil.rmtree(folder)
    for item in home.iterdir():
        if item.name in ("User", "portable.txt") or item.name.startswith("User (") or item.suffix.lower() in DISC_SUFFIXES:
            continue
        if item.is_dir():
            shutil.copytree(item, folder / item.name)
        else:
            folder.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, folder / item.name)
    return folder / program.name


def pictures(exe, reference, work, last=2500, every=100):
    """The beach from the start (--quick) at a fixed time with the sound made frame by frame, every 100th picture of both
    programs compared byte for byte: (pictures the same, [different or missing ones])"""
    def one(program, name):
        folder = work / f"pictures-{name}"
        if folder.exists():
            shutil.rmtree(folder)
        program = playable_copy(program, work / f"program-{name}")
        frames = folder / "frames"
        frames.mkdir(parents=True)
        settings = folder / "settings" / "Crash Twinsanity"
        settings.mkdir(parents=True)
        (settings / "disc_image.txt").write_text(disc_image() + "\n")
        env = dict(os.environ, HOME=str(folder / "settings"), APPDATA=str(folder / "settings"), TWIN_FIXED_TIME="1",
                   TWINSANITY_AUDIO="frames", TWIN_DUMP_FRAMES=f"{frames}:{every}")
        with open(folder / "log.txt", "w") as log:
            process = subprocess.Popen([str(program), "--quick", "--headless"], env=env, stdout=log, stderr=subprocess.STDOUT,
                                       cwd=folder)
            # Past the last picture compared, so that one is whole
            beyond = frames / f"frame_{last + every:06d}.png"
            started = time.time()
            while not beyond.exists() and process.poll() is None and time.time() - started < 900:
                time.sleep(1)
            process.kill()
            process.wait()
        return frames
    with ThreadPoolExecutor(2) as pool:
        ours, theirs = pool.map(one, [exe, reference], ["checked", "reference"])
    same, different = 0, []
    for frame in range(every, last + 1, every):
        name = f"frame_{frame:06d}.png"
        a, b = ours / name, theirs / name
        if not a.exists() or not b.exists():
            different.append(name + " (missing)")
        elif a.read_bytes() != b.read_bytes():
            different.append(name)
        else:
            same += 1
    return same, different


def outcome_class(result):
    """A sweep result's kind: played (the run went on, or to a game over: scripted input decides which), entered from a
    neighbour (no character in the chunk: the expected crash), or a problem"""
    if result["outcome"] in ("ran", "game over") and not result["fatal"] and not result["stray_frees"]:
        return "played"
    if result["outcome"] == "no character in the chunk":
        return "entered from a neighbour"
    return "problem"


def sweep(exe, work):
    """Every level chunk (level_sweep.py) against native/sweep/results_windows.json: (counts, [differences])"""
    out = work / "sweep"
    subprocess.run([sys.executable, str(TOOLS / "level_sweep.py"), str(out), "--app", str(exe), "--jobs", "4"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    results = json.loads((out / "results.json").read_text())
    baseline = json.loads((HERE / "native" / "sweep" / "results_windows.json").read_text())
    counts, differences = {}, []
    for chunk, result in sorted(results.items()):
        kind = outcome_class(result)
        counts[kind] = counts.get(kind, 0) + 1
        before = outcome_class(baseline[chunk]) if chunk in baseline else "not swept"
        if kind == "problem" or kind != before:
            detail = result["fatal"][0].replace("[native] ", "") if result["fatal"] else result["outcome"]
            differences.append(f"{chunk}: {kind} (was {before}): {detail}")
    return counts, differences


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", action="store_true", help="build first (build_windows.py)")
    parser.add_argument("--exe", default=str(HERE / "build/native/cmake/CrashTwinsanity.exe"))
    parser.add_argument("--reference", default=str(HERE / "build/native/dist/Crash Twinsanity/CrashTwinsanity.exe"),
                        help="the program whose pictures ours must match (none: no comparison)")
    parser.add_argument("--sweep", action="store_true", help="every level chunk too (some 20 minutes)")
    parser.add_argument("--work", default=str(HERE / "build/native/check"))
    parser.add_argument("--physics", action="store_true", help="only the physics' check (past 60 Hz against 60 Hz's)")
    parser.add_argument("--portable", action="store_true", help="only portable mode's check")
    parser.add_argument("--scenarios", action="store_true", help="only the scenarios (the places that once crashed)")
    parser.add_argument("--hints-shots", metavar="FOLDER",
                        help="only the menu hints' check, with a window: the pause menu's pictures saved in FOLDER")
    parser.add_argument("--fps-shots", metavar="FOLDER",
                        help="only the FPS counter's check, with a window: the window's pictures saved in FOLDER")
    arguments = parser.parse_args()

    if arguments.build:
        built = subprocess.run([sys.executable, str(TOOLS / "build_windows.py")], stdout=subprocess.DEVNULL,
                               stderr=subprocess.STDOUT)
        print(f"build           {'ok' if built.returncode == 0 else 'FAILED (run build_windows.py to see why)'}")
        if built.returncode != 0:
            sys.exit(1)
    exe = Path(arguments.exe)
    if not exe.exists():
        sys.exit(f"{exe} isn't built")
    work = Path(arguments.work)
    work.mkdir(parents=True, exist_ok=True)
    if arguments.physics:
        results = physics(exe, work)
        for name, ok, what in results:
            print(f"physics         {'ok' if ok else 'FAILED'} {name} as 60 Hz: {what}")
        sys.exit(0 if all(ok for _, ok, _ in results) else 1)
    if arguments.scenarios:
        results = scenarios(exe, work)
        for name, ok, what in results:
            print(f"scenario        {'ok' if ok else 'FAILED'} {name}: {what}")
        sys.exit(0 if all(ok for _, ok, _ in results) else 1)
    if arguments.portable:
        results = portable(exe, work)
        for name, ok, what in results:
            print(f"portable        {'ok' if ok else 'FAILED'} {name}: {what}")
        sys.exit(0 if all(ok for _, ok, _ in results) else 1)
    if arguments.hints_shots:
        results = menu_hints(exe, work, arguments.hints_shots)
        for name, ok, what in results:
            print(f"hints           {'ok' if ok else 'FAILED'} {name}: {what}")
        sys.exit(0 if all(ok for _, ok, _ in results) else 1)
    if arguments.fps_shots:
        results = fps_counter(exe, work, arguments.fps_shots)
        for name, ok, what in results:
            print(f"fps counter     {'ok' if ok else 'FAILED'} {name}: {what}")
        sys.exit(0 if all(ok for _, ok, _ in results) else 1)
    print(f"checking        {exe}")
    failed = report(exe, Path(arguments.reference) if arguments.reference else None, work, arguments.sweep)
    print(f"details in      {work}")
    sys.exit(1 if failed else 0)


def report(exe, reference, work, with_sweep, reference_name=None):
    """The checks run and printed a line each; whether one failed"""
    failed = False
    tests = self_tests(exe, work)
    passed = [name for name, ok, _ in tests if ok]
    print(f"self-tests      {len(passed)}/{len(tests)} pass")
    for name, ok, last in tests:
        if not ok:
            failed = True
            print(f"  FAILED {name}: {last}")

    played = scenarios(exe, work)
    print(f"scenarios       {sum(ok for _, ok, _ in played)}/{len(played)} without a crash or a memory problem (and as expected)")
    for name, ok, what in played:
        if not ok:
            failed = True
            print(f"  FAILED {name}: {what}")

    hints = menu_hints(exe, work)
    print(f"menu hints      {sum(ok for _, ok, _ in hints)}/{len(hints)} as the option and the controller have them")
    for name, ok, what in hints:
        if not ok:
            failed = True
            print(f"  FAILED {name}: {what}")

    looked, what = mouse_look(exe, work)
    print(f"mouse look      {'ok' if looked else 'FAILED'}: {what}")
    failed |= not looked

    for name, ok, what in physics(exe, work):
        print(f"physics         {'ok' if ok else 'FAILED'} {name} as 60 Hz: {what}")
        failed |= not ok

    cases = portable(exe, work)
    print(f"portable        {sum(ok for _, ok, _ in cases)}/{len(cases)} cases as they should be "
          f"({', '.join(name for name, _, _ in cases)})")
    for name, ok, what in cases:
        if not ok:
            failed = True
            print(f"  FAILED {name}: {what}")

    if reference and reference.exists():
        same, different = pictures(exe, reference, work)
        print(f"pictures        {same}/{same + len(different)} the same as {reference_name or reference}'s"
              f"{'' if not different else ': not ' + ', '.join(different[:5])}")
        failed |= bool(different)
    else:
        print("pictures        not compared (no reference program)")

    if with_sweep:
        counts, differences = sweep(exe, work)
        summary = ", ".join(f"{count} {kind}" for kind, count in sorted(counts.items()))
        print(f"sweep           {summary}; {'as the last sweep' if not differences else 'differences:'}")
        for difference in differences[:20]:
            print(f"  {difference}")
        failed |= any("problem" in difference.split(": ")[1] for difference in differences)
    return failed


if __name__ == "__main__":
    main()
