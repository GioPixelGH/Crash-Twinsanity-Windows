#!/usr/bin/env python3
"""The checks a change needs before it's committed (native/WINDOWS.md, "Checking a change"), each summed up in a line: the
self-tests, the places that once crashed played again, the pause menu's hints, the camera turned by the mouse, the physics past
60 Hz against 60 Hz's, the log a player's start writes, portable mode's cases, the pictures against another build's and, with
--sweep, every level chunk against the last sweep's results.
The runs' logs and pictures go to build/native/check/.

    .venv\\Scripts\\python.exe native\\tools\\check_windows.py [--build] [--exe PATH] [--reference PATH] [--sweep]

--build builds first (build_windows.py). The program checked is build/native/cmake/CrashTwinsanity.exe unless --exe says;
the pictures are compared with --reference's (by default the last packaged build's, build/native/dist/). Every run has a
settings folder of its own (none of the player's), and the disc image is local.json's. Exits 1 when a check fails. --physics
runs only the physics' check; --portable only portable mode's; --scenarios only the scenarios; --log only the log's; --fps-shots
FOLDER only the FPS counter's, with a window (the counter counts the pictures shown).
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
import zlib
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
    # scene behaviour, which left him driven by its animations, unable to move (started in roofcor2 he's the player already); and
    # its drones, the director's linked objects, never got the scene's messages that send them at Cortex
    ("Rooftop Rampage's scene skipped arriving as Nina, Cortex moves (#9)", "Levels\\school\\RoofTop\\buschase", 80,
     "idle,wait2,fly:1.2:-3:160:25,fly:11:-3:167:25,fly:26.6:-4:168.5:15,wait2,fly:37.6:-0.5:47:10,roofcor2,wait3,"
     "fly:-9:-1:1:10,fly:5.5:-1:0:5,input,wait3,moved:2",
     dict(SKIPPING, env={"TWIN_SKIP_TEST": "0.2", "TWIN_SCENE_STATE": "3", "TWIN_TRIGGER_PLAN": "list"},
          expect=[r"skip: .*COM_CUTSCENE_H03D skipped: .*messages [\d, ]*287 to its linked objects, .*the character switched as "
                  r"it ends; .*\|BusChase\|act_NINA1",
                  r"\|RoofCor2\|act_CORTEX asked for its slot 12, given its own object back: its own COM_CORTEX_IDLE",
                  r"scene state: the player \|RoofCor2\|act_CORTEX, inputs locked 00000000",
                  r"hop: the player \S+act_CORTEX moved [\d.]+ units since the last step, at least"])),
    # Issue #9: the Academy hub's first scene skipped (its triggers stepped into): its staircase, the director's linked object,
    # never got the scene's message that extends it, so the Boiler Room couldn't be reached
    ("The Academy's first scene skipped, its staircase extended (#9)", "Levels\\school\\Sch_Hub\\sch_hub", 45,
     "idle,wait40,scripts:staircase",
     dict(SKIPPING, env={"TWIN_SKIP_TEST": "1", "TWIN_TRIGGER_PLAN": "0,1", "TWIN_TRIGGER_EVERY": "14"},
          expect=[r"skip: .*COM_SCHOOL_HUB_CUTSCENE_H03A skipped: .*messages 4 to its linked objects",
                  r"act_EXTENDING_SPIRAL_STAIRCASE's runner 0"])),
    # Issue #19: the options' 6x and 8x drawn by the hardware renderer whole (its targets fit this PC's graphics card); and a
    # target the card can't hold ($TWIN_HW_MAX_TARGET makes the 4x frame one) drawn by the software GS instead (HAB3X's 148af1e)
    ("The beach at 6x (#19)", "Levels\\Earth\\Hub\\beach", 12, "idle",
     dict(env={"TWIN_SCALE": "6"}, expect=[r"hardware renderer: drawing at 6x"], forbid=[r"can't be drawn into"])),
    ("The beach at 8x (#19)", "Levels\\Earth\\Hub\\beach", 12, "idle",
     dict(env={"TWIN_SCALE": "8"}, expect=[r"hardware renderer: drawing at 8x"], forbid=[r"can't be drawn into"])),
    # Supersampling (the anti-aliasing option's SSAA 4x): the internal scale 4 times the headless 4x, up to 8
    ("The beach with supersampling (SSAA 4x)", "Levels\\Earth\\Hub\\beach", 12, "idle",
     dict(options="anti_aliasing=3\n", expect=[r"hardware renderer: drawing at 8x"], forbid=[r"can't be drawn into"])),
    ("A buffer the graphics card can't hold drawn by the software GS", "Levels\\Earth\\Hub\\beach", 12, "idle",
     dict(env={"TWIN_SCALE": "4", "TWIN_HW_MAX_TARGET": "1024"},
          expect=[r"hardware renderer: drawing at 4x", r"the target \w+/8 class \d+ \(2048x\d+ at the scale\) can't be drawn into",
                  r"left to the software GS: a target the graphics card can't hold at this scale"])),
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
# The log a player's start writes (sessionlog.cpp; native/WINDOWS.md, "The log"), started as Explorer starts it (its own log.txt
# in the settings folder): the last session's log a 1.0.18 one that crashed, a path in the profile folder in its last line; a mod,
# a few options. What the start's log must have (regular expressions, in its order), played on the beach for LOG_SECONDS with a
# performance line every 10 s of play (the graphics card's time in it: the render thread's context times it, a hidden window's);
# then a start that ends on an error (a disc image that isn't there), its last lines; then the settings screen driven from the
# main menu through changes ($TWIN_SETTINGS_LOG_TEST: settingsshots.cpp), each logged once as it was left, with a settings folder
# of its own (LOG_SETTINGS_OPTIONS), and no other change line (LOG_SETTINGS_CHANGES of them)
LOG_PREVIOUS = ("[native] [   0.000] Crash Twinsanity 1.0.18\n"
                "[native] [ 812.500] disc image {profile}\\Desktop\\Crash Twinsanity.ISO\n"
                "[native] fatal exception 0xc0000005 at 0xffffffffffffffff (pc CrashTwinsanity.exe+0x1eb40d), in {profile}\\x\n"
                "  #0  CrashTwinsanity.exe+0x1eb40d FUN_001eb400+0xd\n")
LOG_OPTIONS = "crt_filter=1\nanti_aliasing=1\nshow_fps=1\npause_on_focus_loss=0\n"
LOG_SECONDS = 25
# A duration as the log writes it: "45 s", "16 min 05 s", "2 h 03 min"
DURATION = r"\d+ (?:h \d+ min|min \d+ s|s)"
LOG_START = [
    r"^\[native\] \[ *[\d.]+\] Crash Twinsanity \d+\.\d+\.\d+\n\[native\] \[ *[\d.]+\] started \d{4}-\d\d-\d\d \d\d:\d\d:\d\d",
    r"\] system: Windows \d+ \(\d+\.\d+\.\d+\)",
    r"\] command line: --headless --quiet-stubs --iso ",
    r"\] processor: .+, \d+ threads, [^;]+; memory [\d.]+ GB",
    r"\] options \(options\.txt's\): skip_cutscenes=0 .*crt_filter=1 .*anti_aliasing=1 ",
    r"\] options, the rest: .*show_fps=1",
    r"\] resolution \(the defaults: no resolution\.txt yet\): 2x the PS2's picture, a 1280x720 window",
    r"\] threads: three, .*\(the default\)",
    r"\] variables: .*TWIN_PERFORMANCE_EVERY=10",
    r"\] mods: 1 file in the mods folder: test/mod\.bin",
    r"\] texture pack: on, none installed",
    r"\] the last session \(log\.previous\.txt\): 1\.0\.18 for 13 min 33 s, ended in a crash: \"fatal exception 0xc0000005 .*"
    r"in %USERPROFILE%\\x\"",
    r"\] display: none",
    r"\] audio: none",
    r"\] game: Levels\\Earth\\Hub\\beach loaded in [\d.]+ s",
    r"\] performance: [\d.]+ fps over \d+ s of play, the slowest frame \d+ ms; a frame's work: the game [\d.]+ ms .*"
    r"VU1 [\d.]+ ms, the render thread [\d.]+ ms, the graphics card [\d.]+ ms; the player in levels\\earth\\hub\\beach",
    rf"\] session: {DURATION}, {DURATION} of it played at [\d.]+ fps on average, \d+% of it below 57 fps .*"
    r"the slowest frame \d+ ms; a frame's work: .*, the graphics card [\d.]+ ms\n",
    rf"\] exit: the level sweep's run ended \(status 0\) after {DURATION}; the game's state ",
]
LOG_ERROR = [
    rf"\] the last session \(log\.previous\.txt\): \d+\.\d+\.\d+ for {DURATION}, ended: "
    r"\"exit: the level sweep's run ended \(status 0\)",
    rf"\] session: {DURATION}, none of it played\n\[native\] fatal: The disc image isn't where it was: .*missing image\.iso.*\n*$",
]
# The settings screen's changes: the refresh rate stepped from 144 Hz to 120, 60, 50 and back to 60 is one change, the resolution
# stepped 2x, 3x, 2x, 3x one, a key bound one; all three once the screen is closed, before the test's end
LOG_SETTINGS_OPTIONS = "refresh_60hz=3\npause_on_focus_loss=0\n"
LOG_SETTINGS = [
    r"\] options \(options\.txt's\): .*refresh_60hz=3 ",
    r"\] options: refresh_60hz 3 -> 1 \(144 Hz -> 60 Hz\)\n",
    r"\] options: resolution 2x -> 3x the PS2's picture\n",
    r"\] options: \w+'s (?:second )?key .+ -> F8\n",
    r"\] settings log test: done\n",
    rf"\] exit: the program ended after {DURATION}",
]
LOG_SETTINGS_CHANGES = 3
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
        # (a click the game didn't take, clicked again by the test: passes, said)
        again = sum(line.startswith("clicked again") for line in lines)
        return name, status == 0 and last.startswith("pass"), f"{last}; {hints}" + (f"; clicked again {again}" if again else "")
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


def private_names(log):
    """What a log names that it mustn't (they're attached to public issues): the profile folder's name anywhere, the user's as a
    path's part, the computer's as a word"""
    found = []
    profile = Path.home().name
    if profile and profile.lower() in log.lower():
        found.append(f"the profile folder's name ({profile})")
    user = os.environ.get("USERNAME", "")
    if user and re.search(r"[\\/]" + re.escape(user) + r"(?=[\\/\s\"']|$)", log, re.I | re.M):
        found.append(f"the user's name in a path ({user})")
    computer = os.environ.get("COMPUTERNAME", "")
    if computer and re.search(r"\b" + re.escape(computer) + r"\b", log, re.I):
        found.append(f"the computer's name ({computer})")
    return found


def session_log(exe, work):
    """The log a player's start writes (sessionlog.cpp), started as Explorer starts it: a start played on the beach (the last
    session's log one that crashed), then a start that ends on an error (a disc image that isn't there), then the settings
    screen's changes (a settings folder of its own), each log's lines against LOG_START's, LOG_ERROR's and LOG_SETTINGS', nothing
    private in them and no path written with / in a folder named by its variable: [(name, passed, what happened)]"""
    root = work / "log"
    if root.exists():
        shutil.rmtree(root)
    appdata = root / "appdata"
    settings = appdata / "Crash Twinsanity"
    (settings / "mods" / "test").mkdir(parents=True)
    (settings / "mods" / "test" / "mod.bin").write_bytes(b"a mod's file\n")
    (settings / "disc_image.txt").write_text(disc_image() + "\n")
    (settings / "options.txt").write_text(LOG_OPTIONS)
    (settings / "log.txt").write_text(LOG_PREVIOUS.format(profile=Path.home()))
    changing = root / "appdata-settings"
    (changing / "Crash Twinsanity").mkdir(parents=True)
    (changing / "Crash Twinsanity" / "disc_image.txt").write_text(disc_image() + "\n")
    (changing / "Crash Twinsanity" / "options.txt").write_text(LOG_SETTINGS_OPTIONS)

    def run(name, arguments, environment, timeout, folder=appdata):
        """The program started headless as Explorer starts it: its exit status (99: timed out) and log.txt, kept as root/name"""
        env = dict(os.environ, APPDATA=str(folder), HOME=str(folder), TWINSANITY_AUDIO="off", **environment)
        env.pop("TWINSANITY_ISO", None)
        status = subprocess.run([str(Path(sys.executable).with_name("pythonw.exe")), "-c", PORTABLE_LAUNCHER, str(timeout),
                                 str(exe), "--headless", "--quiet-stubs", *arguments], env=env, cwd=root,
                                timeout=timeout + 60).returncode
        log_file = folder / "Crash Twinsanity" / "log.txt"
        log = log_file.read_text(errors="replace") if log_file.exists() else ""
        (root / name).write_text(log)
        return status, log

    def result(name, status, expected_status, log, patterns, changes=None):
        # Each pattern found after the one before it
        at, missing = 0, None
        for pattern in patterns:
            found = re.compile(pattern, re.M).search(log, at)
            if not found:
                missing = pattern
                break
            at = found.end()
        private = private_names(log)
        # (The command line as it was given: local.json's disc image has /)
        slashed = [line for line in log.splitlines() if re.search(r"%(?:APPDATA|LOCALAPPDATA|USERPROFILE)%", line) and "/" in line
                   and "] command line: " not in line]
        changed = len(re.findall(r"^\[native\] \[ *[\d.]+\] options: ", log, re.M))
        if status != expected_status:
            return name, False, f"exit {status}"
        if missing:
            return name, False, f"nothing in the log like {missing!r} (after the lines before it)"
        if private:
            return name, False, f"the log names {private[0]}"
        if slashed:
            return name, False, f"a path written with / in it: {slashed[0]!r}"
        if changes is not None and changed != changes:
            return name, False, f"{changed} lines of settings changed, not {changes}"
        return name, True, f"its {len(patterns)} kinds of lines there, nothing private"

    status, log = run("start.txt", ["--iso", disc_image()],
                      {"TWIN_SWEEP_LEVEL": PORTABLE_CHUNK, "TWIN_SWEEP_SECONDS": str(LOG_SECONDS), "TWIN_SWEEP_TIMEOUT": "120",
                       "TWIN_PERFORMANCE_EVERY": "10"}, 300)
    results = [result("a start played", status, 0, log, LOG_START, changes=0)]
    status, log = run("error.txt", ["--iso", str(root / "missing image.iso")], {}, 60)
    results.append(result("a start ending on an error", status, 1, log, LOG_ERROR))
    status, log = run("settings.txt", ["--iso", disc_image()], {"TWIN_SETTINGS_LOG_TEST": "1"}, 120, folder=changing)
    results.append(result("settings changed", status, 0, log, LOG_SETTINGS, changes=LOG_SETTINGS_CHANGES))
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


def frame_rgba(path):
    """A dumped frame (png.cpp: RGBA, stored, no filter): (width, height, its rows' bytes, each after its filter byte)"""
    data = Path(path).read_bytes()
    width, height = int.from_bytes(data[16:20], "big"), int.from_bytes(data[20:24], "big")
    at, compressed = 8, b""
    while at < len(data):
        size = int.from_bytes(data[at:at + 4], "big")
        if data[at + 4:at + 8] == b"IDAT":
            compressed += data[at + 8:at + 8 + size]
        at += 12 + size
    return width, height, zlib.decompress(compressed)


# The 1.1.0 drawing of what 2026-11-01's fixes changed: the game's colour filter (hwgs.cpp's DepthIndexColour) and the scenes
# drawn into buffers narrower than the screen (bossarea's sky) at the scale; VU1's packets read when the GIF reads them
# (ee/vu.cpp's Kick: the skinned meshes' last two vertices' colours); mipmaps' level of detail at the drawing's scale
AS_BEFORE = {"TWIN_HW_DEPTH_INDEX": "0", "TWIN_HW_SMALL_SCENES": "0", "TWIN_VU1_KICK_AT_ONCE": "1", "TWIN_HW_LOD_AT_SCALE": "0"}
# The GS's edge fixes alone (the pictures' outline rule is theirs)
GS_FIXES_ONLY = {"TWIN_VU1_KICK_AT_ONCE": "1", "TWIN_HW_LOD_AT_SCALE": "0"}


def scene_buffers(exe, work, frame=832, scale=4):
    """Bossarea's sky (the first boss's cutscene, Cortex against the sky: compare_models.py's moment), drawn by the game at
    half the PS2's resolution into a 256 wide buffer and laid over the frame with the nearest texels, at the scale with the
    buffer at the PS2's pixels (TWIN_HW_SMALL_SCENES=0: blocks of two PS2 pixels on the clouds, the island, the horizon) and at
    the scale (as PCSX2 draws it): the steps of the PS2's pixel's size gone: (passed, what happened)"""
    def run(name, extra):
        folder = work / f"scene-buffers-{name}"
        if folder.exists():
            shutil.rmtree(folder)
        settings = folder / "settings" / "Crash Twinsanity"
        settings.mkdir(parents=True)
        (settings / "disc_image.txt").write_text(disc_image() + "\n")
        # 50 Hz as the bench's (the PAL game's: the cutscene's moment at the same frame)
        (settings / "options.txt").write_text("widescreen=0\nrefresh_60hz=0\n")
        env = dict(os.environ, HOME=str(folder / "settings"), APPDATA=str(folder / "settings"), TWIN_FIXED_TIME="1",
                   TWINSANITY_AUDIO="frames", TWIN_DUMP_FRAMES=f"{folder}:{frame},", TWIN_SCALE=str(scale),
                   TWIN_PAD_SCRIPT="500-800:ly=0", TWIN_START_CHUNK="Levels\\Earth\\Hub\\bossarea", **extra)
        with open(folder / "log.txt", "w") as log:
            process = subprocess.Popen([str(exe), "--quick", "--headless"], env=env, stdout=log, stderr=subprocess.STDOUT,
                                       cwd=folder)
            shot = folder / f"frame_{frame:06d}.png"
            started = time.time()
            while process.poll() is None and time.time() - started < 900:
                if shot.exists():
                    time.sleep(2)
                    break
                time.sleep(1)
            process.kill()
            process.wait()
        return folder
    with ThreadPoolExecutor(2) as pool:
        old, new = pool.map(run, ["old", "new"], [{"TWIN_HW_SMALL_SCENES": "0"}, {}])
    a, b = old / f"frame_{frame:06d}.png", new / f"frame_{frame:06d}.png"
    if not a.exists() or not b.exists():
        return False, f"no frame {frame}"
    drawn = "drawn as a scene" in (new / "log.txt").read_text(errors="replace")
    before, after = ps2_steps(a, scale), ps2_steps(b, scale)
    what = (f"bossarea's sky at {scale}x, steps of the PS2's pixel {before:.1f} -> {after:.1f} per 1000 PS2 pixels"
            + ("" if drawn else "; the log doesn't say a narrow buffer is drawn as a scene"))
    return drawn and after < 3 and after < before / 3, what


def skinned_light(exe, work, frame=1770):
    """#15: Crash at the Iceberg Lab's start (levels\\ice\\hub\\labext) looks round in his idle, his chin lit pale green by the
    level's lights as the retail game lights it (its executable in PCSX2 2.9, the same pose: the patch's mean colour 124, 190,
    148): the frame at a fixed time, 1x, 4:3, the patch's mean colour within 10 of the retail's: (passed, what happened)"""
    folder = work / "skinned-light"
    if folder.exists():
        shutil.rmtree(folder)
    frames = folder / "frames"
    frames.mkdir(parents=True)
    settings = folder / "settings" / "Crash Twinsanity"
    settings.mkdir(parents=True)
    (settings / "disc_image.txt").write_text(disc_image() + "\n")
    (settings / "options.txt").write_text("widescreen=0\n")
    env = dict(os.environ, HOME=str(folder / "settings"), APPDATA=str(folder / "settings"), TWIN_FIXED_TIME="1",
               TWINSANITY_AUDIO="frames", TWIN_DUMP_FRAMES=f"{frames}:30", TWIN_SCALE="1",
               TWIN_START_CHUNK="Levels\\Ice\\Hub\\labext")
    with open(folder / "log.txt", "w") as log:
        process = subprocess.Popen([str(exe), "--quick", "--headless"], env=env, stdout=log, stderr=subprocess.STDOUT, cwd=folder)
        beyond = frames / f"frame_{frame + 30:06d}.png"
        started = time.time()
        while not beyond.exists() and process.poll() is None and time.time() - started < 600:
            time.sleep(1)
        process.kill()
        process.wait()
    shot = frames / f"frame_{frame:06d}.png"
    if not shot.exists():
        return False, f"no frame {frame}"
    width, height, raw = frame_rgba(shot)
    row = 1 + width * 4
    # Crash's head and chest (the PS2's 512 x 512 picture), the pixels far greener than red and bright
    patch = []
    for y in range(299, 459):
        for x in range(192, 288):
            r, g, b = raw[y * row + 1 + x * 4:y * row + 4 + x * 4]
            if g > r + 30 and g > 150:
                patch.append((r, g, b))
    if len(patch) < 50:
        return False, f"no pale green patch on Crash's chin ({len(patch)} pixels)"
    mean = [sum(p[c] for p in patch) / len(patch) for c in range(3)]
    retail = (124, 190, 148)
    off = max(abs(m - e) for m, e in zip(mean, retail))
    return off <= 10, (f"Crash's chin in the Iceberg Lab's light {round(mean[0])}, {round(mean[1])}, {round(mean[2])} on "
                       f"{len(patch)} pixels, the retail game's 124, 190, 148")


def window_downsample(exe, work, frame=600):
    """A picture larger than the window made its size as the mean of the texels each window pixel covers (display.cpp's
    DownsampleSource, whatever the anti-aliasing option): the beach at 8x in a 1280x720 window, anti-aliasing off, the window
    ($TWIN_WINDOW_SHOTS) against the same frame's internal picture box-filtered here around Crash; a single bilinear read (1.1.0
    and before, anti-aliasing off or FXAA) left 5.2% of the edges' pixels off by more than 32 there: (passed, what happened)"""
    folder = work / "window-downsample"
    if folder.exists():
        shutil.rmtree(folder)
    settings = folder / "settings" / "Crash Twinsanity"
    settings.mkdir(parents=True)
    (settings / "disc_image.txt").write_text(disc_image() + "\n")
    # (not paused when the window isn't in front: a check run while the PC is in use)
    (settings / "options.txt").write_text("anti_aliasing=0\nwidescreen=1\npause_on_focus_loss=0\n")
    (settings / "resolution.txt").write_text("scale=8\nwindow=1280x720\nfullscreen=0\n")
    env = dict(os.environ, HOME=str(folder / "settings"), APPDATA=str(folder / "settings"), TWIN_FIXED_TIME="1",
               TWINSANITY_AUDIO="frames", TWIN_DUMP_FRAMES=f"{folder}:{frame}", TWIN_WINDOW_SHOTS=f"{folder}:{frame}")
    shot, picture = folder / f"window_{frame:06d}.png", folder / f"frame_{frame:06d}.png"
    with open(folder / "log.txt", "w") as log:
        process = subprocess.Popen([str(exe), "--quick"], env=env, stdout=log, stderr=subprocess.STDOUT, cwd=folder)
        started = time.time()
        while not (shot.exists() and picture.exists()) and process.poll() is None and time.time() - started < 600:
            time.sleep(1)
        time.sleep(3)
        process.kill()
        process.wait()
    if not shot.exists() or not picture.exists():
        return False, f"no {'window' if not shot.exists() else 'picture'} of frame {frame}"
    ww, wh, wraw = frame_rgba(shot)
    pw, ph, praw = frame_rgba(picture)
    if (ww, wh) != (1280, 720) or (pw, ph) != (4096, 4096):
        return False, f"a {ww}x{wh} window, a {pw}x{ph} picture"

    def spans(count, texels):
        # Each window pixel's texels and their weights along one axis (the share of each texel it covers)
        step = texels / count
        out = []
        for i in range(count):
            a, b = i * step, (i + 1) * step
            out.append([(t, min(b, t + 1) - max(a, t)) for t in range(int(a), min(int(b - 1e-9) + 1, texels))])
        return out

    # The window's middle (Crash), 16:9 filling it: the picture's 4096 texels over 1280 and 720 pixels
    x0, x1, y0, y1 = 550, 740, 400, 650
    columns, rows = spans(ww, pw), spans(wh, ph)
    prow = 1 + pw * 4
    wrow = 1 + ww * 4
    expected, got = {}, {}
    for y in range(y0 - 1, y1):
        for x in range(x0 - 1, x1):
            total = [0.0, 0.0, 0.0]
            for ty, wy in rows[y]:
                base = ty * prow + 1
                for tx, wx in columns[x]:
                    w = wx * wy
                    at = base + tx * 4
                    total[0] += praw[at] * w
                    total[1] += praw[at + 1] * w
                    total[2] += praw[at + 2] * w
            area = sum(w for _, w in rows[y]) * sum(w for _, w in columns[x])
            expected[x, y] = [c / area for c in total]
            at = y * wrow + 1 + x * 4
            got[x, y] = wraw[at:at + 3]
    edges, off, error = 0, 0, 0.0
    for y in range(y0, y1):
        for x in range(x0, x1):
            e = expected[x, y]
            grey = sum(e) / 3
            if abs(grey - sum(expected[x, y - 1]) / 3) + abs(grey - sum(expected[x - 1, y]) / 3) <= 24:
                continue
            edges += 1
            d = sum(abs(a - b) for a, b in zip(got[x, y], e)) / 3
            error += d
            off += d > 32
    if edges < 200:
        return False, f"only {edges} edge pixels around Crash"
    what = (f"8x in a 1280x720 window: {edges} edge pixels around Crash {error / edges:.1f} from the texels' mean, "
            f"{off / edges:.2%} off by more than 32")
    return off / edges < 0.004 and error / edges < 3.5, what


def frame_colours(path, step=4):
    """A dumped frame's (png.cpp: RGBA, stored, no filter) colour bytes, every step-th row: bytes"""
    width, height, raw = frame_rgba(path)
    row = 1 + width * 4
    colours = bytearray()
    for y in range(0, height, step):
        pixels = raw[y * row + 1:(y + 1) * row]
        colours += pixels[0::4] + pixels[1::4] + pixels[2::4]
    return bytes(colours)


def dithering(exe, work, frame=300):
    """The Dithering option off (hwgs.cpp's full colour dithered draws): the beach's frame at a fixed time against the pictures
    check's, made as the PS2 (the default): that one 5 bits a channel (no low three bits), this one the frame's full colour (most
    channels' low bits set) and a little brighter (the truncation's floor gone): (passed, what happened)"""
    ps2 = work / "pictures-checked-filter" / "frames" / f"frame_{frame:06d}.png"
    folder = work / "dithering"
    if folder.exists():
        shutil.rmtree(folder)
    frames = folder / "frames"
    frames.mkdir(parents=True)
    settings = folder / "settings" / "Crash Twinsanity"
    settings.mkdir(parents=True)
    (settings / "disc_image.txt").write_text(disc_image() + "\n")
    (settings / "options.txt").write_text("dithering=1\n")
    env = dict(os.environ, HOME=str(folder / "settings"), APPDATA=str(folder / "settings"), TWIN_FIXED_TIME="1",
               TWINSANITY_AUDIO="frames", TWIN_DUMP_FRAMES=f"{frames}:{frame}")
    with open(folder / "log.txt", "w") as log:
        process = subprocess.Popen([str(exe), "--quick", "--headless"], env=env, stdout=log, stderr=subprocess.STDOUT, cwd=folder)
        beyond = frames / f"frame_{frame * 2:06d}.png"
        started = time.time()
        while not beyond.exists() and process.poll() is None and time.time() - started < 600:
            time.sleep(1)
        process.kill()
        process.wait()
    full = frames / f"frame_{frame:06d}.png"
    if not ps2.exists() or not full.exists():
        return False, f"no frame {frame} ({'as the PS2' if not ps2.exists() else 'off'})"
    # (the channels lit, 16 and up, off: half the frame is the dark sky's or black)
    a, b = frame_colours(ps2), frame_colours(full)
    lit = [(x, y) for x, y in zip(a, b) if y >= 16]
    if not lit:
        return False, f"frame {frame} is dark"
    low_a = sum(1 for x in a if x & 7) / len(a)
    low_b = sum(1 for _, y in lit if y & 7) / len(lit)
    brighter = sum(y - x for x, y in lit) / len(lit)
    what = (f"off keeps the full colour ({low_b:.0%} of the lit channels past 5 bits, {brighter:+.1f} brighter on average), as "
            f"the PS2 5 bits a channel ({low_a:.0%} past them)")
    return low_a == 0 and low_b > 0.5 and 1.0 < brighter < 8.0, what


def pictures(exe, reference, work, last=2500, every=100):
    """The beach from the start (--quick) at a fixed time with the sound made frame by frame, every 100th picture compared
    byte for byte: ours with the colour filter and the small scene buffers as the reference's program draws them (AS_BEFORE: at
    the PS2's pixels) against the reference's, then ours as a player has it (both at the scale) against the reference's, whose
    differences
    must lie within a PS2 pixel of an outline (the filter's near and far edges) but one in a thousand at most (an edge between
    a near and a far surface of like colours is no outline to the colours: a leaf's dark green over the olive behind it):
    (pictures the same with the old filter, [different or missing ones], [(picture, pixels changed, of them off the outlines,
    their mean change, the first off ones' places)] with the new)"""
    def one(program, name, extra):
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
                   TWINSANITY_AUDIO="frames", TWIN_DUMP_FRAMES=f"{frames}:{every}", **extra)
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
    with ThreadPoolExecutor(4) as pool:
        ours, theirs, scaled, whole = pool.map(one, [exe, reference, exe, exe],
                                               ["checked", "reference", "checked-filter", "checked-all"],
                                               [AS_BEFORE, {}, GS_FIXES_ONLY, {}])
    same, different, changed, packets = 0, [], [], []
    for frame in range(every, last + 1, every):
        name = f"frame_{frame:06d}.png"
        a, b, c = ours / name, theirs / name, scaled / name
        if not a.exists() or not b.exists():
            different.append(name + " (missing)")
        elif a.read_bytes() != b.read_bytes():
            different.append(name)
        else:
            same += 1
        if b.exists() and c.exists() and b.read_bytes() != c.read_bytes():
            width = int.from_bytes(b.read_bytes()[16:20], "big")
            changed.append((name, *changed_off_outlines(b, c, max(1, round(width / 512)))))
        d = whole / name
        if c.exists() and d.exists() and c.read_bytes() != d.read_bytes():
            packets.append((name, pixels_apart(c, d)))
    return same, different, changed, packets


def pixels_apart(old, new):
    """Two pictures of the same size (png.cpp's): the pixels with a channel more than 8 apart"""
    width, height, a = frame_rgba(old)
    _, _, b = frame_rgba(new)
    row = 1 + width * 4
    count = 0
    for y in range(height):
        start = y * row + 1
        if a[start:start + width * 4] == b[start:start + width * 4]:
            continue
        for at in range(start, start + width * 4, 4):
            if max(abs(a[at] - b[at]), abs(a[at + 1] - b[at + 1]), abs(a[at + 2] - b[at + 2])) > 8:
                count += 1
    return count


def vu1_packets(exe, work, frame=832):
    """VU1's packets as the GIF reads them (ee/vu.cpp's Kick and FlushKick: at the next XGKICK or when the program stops): the
    first boss's cutscene in bossarea at a fixed time (Cortex against the sky, compare_models.py's moment), the frame's GS
    commands written twice (TWIN_GS_DUMP; gsdump.strips), the packets read at the XGKICK as 1.1.0 and before read them
    (TWIN_VU1_KICK_AT_ONCE=1) and as the GIF reads them. The skinned meshes' program writes its packet's last two vertices'
    colours 2 and 5 cycles after its XGKICK: those, and nothing else, apart: (passed, what happened)"""
    import gsdump

    def run(name, extra):
        folder = work / f"vu1-packets-{name}"
        if folder.exists():
            shutil.rmtree(folder)
        settings = folder / "settings" / "Crash Twinsanity"
        settings.mkdir(parents=True)
        (settings / "disc_image.txt").write_text(disc_image() + "\n")
        # 50 Hz as the bench's (the cutscene's moment at the same frame)
        (settings / "options.txt").write_text("widescreen=0\nrefresh_60hz=0\n")
        env = dict(os.environ, HOME=str(folder / "settings"), APPDATA=str(folder / "settings"), TWIN_FIXED_TIME="1",
                   TWINSANITY_AUDIO="frames", TWIN_GS_DUMP=f"{folder}:{frame},", TWIN_SCALE="1",
                   TWIN_PAD_SCRIPT="500-800:ly=0", TWIN_START_CHUNK="Levels\\Earth\\Hub\\bossarea", **extra)
        dump = folder / f"frame_{frame:06d}.gs"
        with open(folder / "log.txt", "w") as log:
            process = subprocess.Popen([str(exe), "--quick", "--headless"], env=env, stdout=log, stderr=subprocess.STDOUT,
                                       cwd=folder)
            started = time.time()
            while process.poll() is None and time.time() - started < 900:
                if dump.exists() and "GS dump" in (folder / "log.txt").read_text(errors="replace"):
                    time.sleep(2)
                    break
                time.sleep(1)
            process.kill()
            process.wait()
        return dump
    with ThreadPoolExecutor(2) as pool:
        old, new = pool.map(run, ["old", "new"], [{"TWIN_VU1_KICK_AT_ONCE": "1"}, {}])
    if not old.exists() or not new.exists():
        return False, f"no GS dump of frame {frame}"
    a, b = gsdump.strips(old), gsdump.strips(new)
    if len(a) != len(b):
        return False, f"{len(a)} strips read at the XGKICK, {len(b)} as the GIF reads them"
    strips_apart = vertices_apart = elsewhere = 0
    for sa, sb in zip(a, b):
        if len(sa["verts"]) != len(sb["verts"]):
            elsewhere += 1
            continue
        apart = [k for k, (va, vb) in enumerate(zip(sa["verts"], sb["verts"])) if va != vb]
        if apart:
            strips_apart += 1
            vertices_apart += len(apart)
        for k in apart:
            va, vb = sa["verts"][k], sb["verts"][k]
            # (only the colour, RGBAQ, and only the strip's last two)
            if va[:3] != vb[:3] or va[4:] != vb[4:] or k < len(sa["verts"]) - 2:
                elsewhere += 1
    what = (f"{strips_apart} of {len(a)} strips with their last two vertices' colours as the GIF reads them ({vertices_apart} "
            f"vertices), {elsewhere} other differences")
    return strips_apart > 0 and elsewhere == 0, what


def changed_off_outlines(old, new, reach):
    """Two pictures of the same size (png.cpp's): the pixels apart (a channel over 8), how many of them lie more than reach
    pixels (a PS2 pixel) from an outline of either picture (neighbours over 48 apart, the channels summed), the changed
    pixels' mean change (the channels summed) and the first three off ones' places (x, y)"""
    width, height, a = frame_rgba(old)
    _, _, b = frame_rgba(new)
    row = 1 + width * 4

    def outline(pic, x, y):
        at = y * row + 1 + x * 4
        here = pic[at:at + 3]
        for nx, ny in ((x + 1, y), (x, y + 1)):
            if nx < width and ny < height:
                there = pic[ny * row + 1 + nx * 4:ny * row + 4 + nx * 4]
                if abs(here[0] - there[0]) + abs(here[1] - there[1]) + abs(here[2] - there[2]) > 48:
                    return True
        return False

    count, off, total, places = 0, 0, 0, []
    for y in range(height):
        start = y * row + 1
        if a[start:start + width * 4] == b[start:start + width * 4]:
            continue
        for x in range(width):
            at = start + x * 4
            change = abs(a[at] - b[at]) + abs(a[at + 1] - b[at + 1]) + abs(a[at + 2] - b[at + 2])
            if max(abs(a[at] - b[at]), abs(a[at + 1] - b[at + 1]), abs(a[at + 2] - b[at + 2])) <= 8:
                continue
            count += 1
            total += change
            near = False
            for ny in range(max(0, y - reach - 1), min(height, y + reach + 2)):
                for nx in range(max(0, x - reach - 1), min(width, x + reach + 2)):
                    if outline(a, nx, ny) or outline(b, nx, ny):
                        near = True
                        break
                if near:
                    break
            if not near:
                off += 1
                if len(places) < 3:
                    places.append((x, y))
    return count, off, (total / count if count else 0.0), places


def off_outlines_allowed(count):
    """The changed pixels that may lie off the outlines: one in a thousand (depth edges between like colours)"""
    return count // 1000


def ps2_steps(path, n):
    """A picture's (png.cpp's) steps of the PS2's pixel's size, n pixels a PS2 pixel: for each of the n x n positions of the
    PS2 pixels' grid, the blocks' sides whose n pixel pairs across all differ (the channels summed over 18); the most's excess
    over the positions' median per 1000 PS2 pixels (compare_models.py's gridsteps: a picture drawn at its scale about 0, a
    layer at the PS2's pixels adds its edges at one position)"""
    width, height, raw = frame_rgba(path)
    row = 1 + width * 4
    pixels = [raw[y * row + 1:(y + 1) * row] for y in range(height)]

    def differ(p, i, j):
        return abs(p[i] - p[j]) + abs(p[i + 1] - p[j + 1]) + abs(p[i + 2] - p[j + 2]) > 18

    dx = [[differ(line, 4 * x, 4 * x + 4) for x in range(width - 1)] for line in pixels]
    dy = [[differ_rows(pixels[y], pixels[y + 1], x) for x in range(width)] for y in range(height - 1)]
    counts = []
    for oy in range(n):
        for ox in range(n):
            total = 0
            for x in range(n + ox - 1, width - 1, n):
                for y0 in range(oy, height - n + 1, n):
                    total += all(dx[y0 + i][x] for i in range(n))
            for y in range(n + oy - 1, height - 1, n):
                for x0 in range(ox, width - n + 1, n):
                    total += all(dy[y][x0 + i] for i in range(n))
            counts.append(total)
    counts.sort()
    median = counts[len(counts) // 2] if len(counts) % 2 else (counts[len(counts) // 2 - 1] + counts[len(counts) // 2]) / 2
    return (counts[-1] - median) * 1000.0 / max(1, (width // n) * (height // n))


def differ_rows(a, b, x):
    i = 4 * x
    return abs(a[i] - b[i]) + abs(a[i + 1] - b[i + 1]) + abs(a[i + 2] - b[i + 2]) > 18


def colour_filter(exe, work, frame=700, scale=4):
    """The game's colour filter at the scale (hwgs.cpp's DepthIndexColour): the beach with the camera looking up at Crash and
    Aku Aku against the sky (a pad script) at a fixed time, drawn at the pictures' scale with the filter at the PS2's pixels
    (TWIN_HW_DEPTH_INDEX=0) and at the scale: the steps of the PS2's pixel's size gone (at 4x: at 2x a PS2 pixel's two
    pixels are no longer than the scene's own edges, the measure can't tell), the changed pixels within a PS2 pixel of an
    outline (one in a thousand may not be: changed_off_outlines); and, with the software GS at 1x, every pixel the filter
    draws on taking the depth's bits 8-15 as its index (TWIN_FILTER_PROBE: what the GPU's way relies on): (passed, what
    happened)"""
    def run(name, extra, scale_now):
        folder = work / f"colour-filter-{name}"
        if folder.exists():
            shutil.rmtree(folder)
        folder.mkdir(parents=True)
        settings = folder / "settings" / "Crash Twinsanity"
        settings.mkdir(parents=True)
        (settings / "disc_image.txt").write_text(disc_image() + "\n")
        env = dict(os.environ, HOME=str(folder / "settings"), APPDATA=str(folder / "settings"), TWIN_FIXED_TIME="1",
                   TWINSANITY_AUDIO="frames", TWIN_DUMP_FRAMES=f"{folder}:{frame},", TWIN_SCALE=str(scale_now),
                   TWIN_PAD_SCRIPT="620-700:ry=255", **extra)
        with open(folder / "log.txt", "w") as log:
            process = subprocess.Popen([str(exe), "--quick", "--headless"], env=env, stdout=log, stderr=subprocess.STDOUT,
                                       cwd=folder)
            shot = folder / f"frame_{frame:06d}.png"
            started = time.time()
            while process.poll() is None and time.time() - started < 900:
                if shot.exists():
                    time.sleep(2)
                    break
                time.sleep(1)
            process.kill()
            process.wait()
        return folder
    with ThreadPoolExecutor(3) as pool:
        old, new, probe = pool.map(run, ["old", "new", "probe"], [{"TWIN_HW_DEPTH_INDEX": "0"}, {},
                                                                  {"TWIN_GS": "software", "TWIN_FILTER_PROBE": "1"}],
                                   [scale, scale, 1])
    a, b = old / f"frame_{frame:06d}.png", new / f"frame_{frame:06d}.png"
    if not a.exists() or not b.exists():
        return False, f"no frame {frame}"
    drawn = "colour filter drawn at" in (new / "log.txt").read_text(errors="replace")
    before, after = ps2_steps(a, scale), ps2_steps(b, scale)
    count, off, mean, places = changed_off_outlines(a, b, scale)
    probes = re.findall(r"filter probe: (\d+) of (\d+) filtered pixels", (probe / "log.txt").read_text(errors="replace"))
    exact = bool(probes) and all(m == t and int(t) > 0 for m, t in probes)
    what = (f"{scale}x, steps of the PS2's pixel {before:.1f} -> {after:.1f} per 1000 PS2 pixels, {count} pixels changed "
            f"({off} off the outlines{' at ' + ' '.join(f'{x},{y}' for x, y in places) if places else ''}, {mean:.0f} apart on "
            f"average); the software GS's indexes the depth's bits 8-15 at "
            f"{', '.join(f'{m} of {t}' for m, t in probes) or 'no filter draw'}"
            + ("" if drawn else "; the log doesn't say the filter is drawn at the scale"))
    return (drawn and exact and off <= off_outlines_allowed(count) and count > 0 and after < 3 and after < before / 3,
            what)


def vs_pcsx2(exe, label="check"):
    """The characters against the PS2 game (compare_models.py, its moments: the beach against the sky, the first boss's
    cutscene) in each configuration: a moment passes when its pair is valid (the poses alike), the port's edges have no more
    steps of the PS2's pixel's size than PCSX2's at the same scale (3 per 1000 PS2 pixels over them at most), and PCSX2
    drawing the port's own GS commands gives the port's picture (a mean within 1.5 of 255 once the half pixel is fitted):
    [(name, passed, what)]"""
    summary = HERE.parent / "compare" / "page" / f"{label}.json"
    run = subprocess.run([sys.executable, str(TOOLS / "compare_models.py"), "--exe", str(exe), "--label", label,
                          "--port-again"], capture_output=True, text=True)
    if not summary.exists():
        return [("bench", False, (run.stdout + run.stderr).strip().splitlines()[-1:] or "no summary")]
    numbers = json.loads(summary.read_text())
    results = []
    for name, n in sorted(numbers.items()):
        problems = []
        if not n["valid"]:
            problems.append(f"the poses differ ({n['pose']:.1f})")
        if n.get("steps_port") is not None and n.get("steps_pcsx2") is not None and n["steps_port"] > n["steps_pcsx2"] + 3:
            problems.append(f"PS2-sized steps {n['steps_port']:.1f} against PCSX2's {n['steps_pcsx2']:.1f}")
        same = n.get("same")
        if same is not None and same["mean"] > 1.5:
            problems.append(f"PCSX2 drawing the port's commands {same['mean']:.2f} apart")
        what = (f"mean {n['mean']:.2f}, steps {n['steps_port'] if n['steps_port'] is None else round(n['steps_port'], 1)}"
                f" (PCSX2 {n['steps_pcsx2'] if n['steps_pcsx2'] is None else round(n['steps_pcsx2'], 1)})"
                + (f", the same commands {same['mean']:.2f} apart" if same is not None else ""))
        results.append((name, not problems, "; ".join(problems) if problems else what))
    return results


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
    parser.add_argument("--log", action="store_true", help="only the log's check (what a player's start writes in it)")
    parser.add_argument("--hints-shots", metavar="FOLDER",
                        help="only the menu hints' check, with a window: the pause menu's pictures saved in FOLDER")
    parser.add_argument("--vs-pcsx2", action="store_true",
                        help="only the characters against the PS2 game in PCSX2 (compare_models.py: its page and numbers in "
                             "..\\compare, PCSX2's pictures kept there)")
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
    if arguments.log:
        results = session_log(exe, work)
        for name, ok, what in results:
            print(f"log             {'ok' if ok else 'FAILED'} {name}: {what}")
        sys.exit(0 if all(ok for _, ok, _ in results) else 1)
    if arguments.vs_pcsx2:
        results = vs_pcsx2(exe)
        for name, ok, what in results:
            print(f"vs PCSX2        {'ok' if ok else 'FAILED'} {name}: {what}")
        sys.exit(0 if results and all(ok for _, ok, _ in results) else 1)
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
    again = [name for name, ok, what in hints if ok and "clicked again" in what]
    print(f"menu hints      {sum(ok for _, ok, _ in hints)}/{len(hints)} as the option and the controller have them"
          + (f" (a click taken only the second time: {', '.join(again)})" if again else ""))
    for name, ok, what in hints:
        if not ok:
            failed = True
            print(f"  FAILED {name}: {what}")

    looked, what = mouse_look(exe, work)
    print(f"mouse look      {'ok' if looked else 'FAILED'}: {what}")
    failed |= not looked

    ok, what = window_downsample(exe, work)
    print(f"window          {'ok' if ok else 'FAILED'}: {what}")
    failed |= not ok

    for name, ok, what in physics(exe, work):
        print(f"physics         {'ok' if ok else 'FAILED'} {name} as 60 Hz: {what}")
        failed |= not ok

    logs = session_log(exe, work)
    print(f"log             {sum(ok for _, ok, _ in logs)}/{len(logs)} as a report needs it, nothing private "
          f"({', '.join(name for name, _, _ in logs)})")
    for name, ok, what in logs:
        if not ok:
            failed = True
            print(f"  FAILED {name}: {what}")

    cases = portable(exe, work)
    print(f"portable        {sum(ok for _, ok, _ in cases)}/{len(cases)} cases as they should be "
          f"({', '.join(name for name, _, _ in cases)})")
    for name, ok, what in cases:
        if not ok:
            failed = True
            print(f"  FAILED {name}: {what}")

    if reference and reference.exists():
        same, different, changed, packets = pictures(exe, reference, work)
        print(f"pictures        {same}/{same + len(different)} the same as {reference_name or reference}'s with the colour "
              f"filter and the scene buffers at the PS2's pixels, VU1's packets read at the XGKICK and mipmaps' levels the 1x "
              f"ones"
              f"{'' if not different else ': not ' + ', '.join(different[:5])}")
        failed |= bool(different)
        off = [c for c in changed if c[2] > off_outlines_allowed(c[1])]
        print(f"pictures        with them at the scale (as players have it): {len(changed)} differ, the "
              f"changed pixels within a PS2 pixel of an outline (but one in a thousand): {'yes' if not off else 'NO'}"
              + "".join(f"; {c[0][6:12].lstrip('0')}: {c[1]} pixels, {c[2]} off"
                        + (f" (at {' '.join(f'{x},{y}' for x, y in c[4])})" if c[4] else "") + f", {c[3]:.0f} apart"
                        for c in changed[:25]))
        failed |= bool(off)
        print(f"pictures        with VU1's packets as the GIF reads them and mipmaps' levels at the scale too: {len(packets)} differ"
              + "".join(f"; {name[6:12].lstrip('0')}: {count} pixels" for name, count in packets[:25]))
        ok, what = vu1_packets(exe, work)
        print(f"vu1 packets     {'ok' if ok else 'FAILED'}: {what}")
        failed |= not ok
        ok, what = colour_filter(exe, work)
        print(f"colour filter   {'ok' if ok else 'FAILED'}: {what}")
        failed |= not ok
        ok, what = scene_buffers(exe, work)
        print(f"scene buffers   {'ok' if ok else 'FAILED'}: {what}")
        failed |= not ok
        ok, what = dithering(exe, work)
        print(f"dithering       {'ok' if ok else 'FAILED'}: {what}")
        failed |= not ok
        ok, what = skinned_light(exe, work)
        print(f"skinned light   {'ok' if ok else 'FAILED'}: {what}")
        failed |= not ok
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
