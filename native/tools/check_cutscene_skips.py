"""The cutscene skip's check (THE LIST 28, src/platform/native/cutsceneskip.h): each scripted cutscene played twice from the same
start, watched and skipped, and what each leaves compared: the player (its character, locked inputs, place, behaviours), the
actors still taking another's object or running a scene's graph, and every agent of the cutscene (where it is, whether it's
drawn, what its runners run). Places within PLACE_TOLERANCE, the rest the same. A cutscene that checks out can be marked
skippable in native/tools/cutscene_skip_table.py.

Each run is the level sweep's (ui/sweep.cpp: $TWIN_SWEEP_LEVEL, $TWIN_FIXED_TIME, no scripted input), the skip option on;
the skipped run holds a button in every cutscene ($TWIN_SKIP_TEST), the skip as a player has it (the table's cutscenes), so a
level's entry cutscenes are compared too; with --waits a third run skips every cutscene by the waits ($TWIN_SKIP_ALL_SCENES,
$TWIN_SKIP_WAITS_FIRST: a cutscene not checked yet, or one a rule skips, tried). What each leaves is $TWIN_SCENE_STATE's, 0.5 s into the play after the cutscene and again a second later (what changed between the two goes on by itself).

    python native/tools/check_cutscene_skips.py [NAME...] [--jobs 4] [--out ..\\compare\\skips] [--exe PATH]

NAME picks cutscenes by the census' names (03_bear) or a part of them; none: every one SCENES has. Prints a line a cutscene;
the runs' logs are in OUT/NAME/watched and OUT/NAME/skipped."""
import argparse
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(HERE / "tools"))
import local_config  # noqa: E402

PLACE_TOLERANCE = 0.05

# How each cutscene is reached: its census name, the chunk the sweep starts in, the hops ($TWIN_HOP: "tell:DIRECTOR" sends the
# director the message its trigger sends, 87), the seconds played, and the run's own variables
SCENES = [
    ("01_training", "Levels\\Earth\\Hub\\highpath", "", 60),
    ("03_bear", "Levels\\Earth\\DocAmok\\docamok3", "", 40),
    ("04_cavern", "Levels\\Earth\\Cavern\\cavbridg", "", 25),
    ("06_tiki_mon", "Levels\\Earth\\Hub\\hubd", "wait3,tell:act_tiki_mon", 70),
    ("08_henchmania_bossfight", "Levels\\Ice\\HighSeas\\gpa12", "", 60),
    ("09_battleship_switchthrower", "Levels\\Ice\\HighSeas\\gpa04", "wait3,tell:act_battleship_ngin_switchthrower:4", 50),
    ("10_dingodile_intro", "Levels\\school\\Boiler\\boiler_6", "wait3,tell:act_dingodile_bossfight_director", 70),
    ("12_ukafight", "Levels\\Ice\\IceClimb\\ukafight", "wait3,tell:act_ukafight_cutscene_director", 70),
    ("13_icelab", "Levels\\Ice\\Hub\\labext", "wait3,tell:act_icelab_cutscene_director", 70),
    ("14_sentry", "Levels\\school\\Crash\\crgpa02", "", 40),
    ("15_slipslide", "Levels\\Ice\\SlipSlide\\l05start", "wait3,tell:act_slipslide_cutscene_director", 70),
    ("16_school_hub", "Levels\\school\\Sch_Hub\\sch_hub", "wait3,tell:act_school_hub_cutscene_director", 70),
    ("17_dorm_room", "Levels\\school\\Cortex\\cogpa08", "", 50),
    ("18_ukauka_defeated", "Levels\\Ice\\IceClimb\\ukafight", "wait3,tell:act_ukauka_defeated_cutscene_director", 80),
    ("20_henchmania", "Levels\\Ice\\HighSeas\\gpa11", "wait3,tell:act_henchmania_cutscene_director", 70),
    ("22_totem_tied", "Levels\\Earth\\Totem\\l03stock", "wait3,tell:act_totem_tied_cutscene_director", 60),
    ("23_totem_falling", "Levels\\Earth\\Totem\\l03stock", "wait3,tell:act_totem_falling_cutscene_director", 60),
    ("24_rockslide", "Levels\\AltEarth\\RockSlid\\l10start", "wait3,tell:act_altearth_rockslide_cutscene_director", 60),
    ("25_walrus", "Levels\\Ice\\HighSeas\\gpa10", "", 50),
    ("28_classroom_chaos", "Levels\\school\\Crash\\crgpa08", "wait3,tell:act_classroom_chaos_cutscene_director", 60),
    ("29_angry_skunk", "Levels\\Earth\\Hub\\highpath", "wait3,tell:act_angry_skunk_cutscene_director", 60),
    ("33_icelab_interior", "Levels\\Ice\\Hub\\labint", "wait3,tell:act_icelabint_cutscene_director", 70),
    ("36_treasure_room", "Levels\\AltEarth\\Core\\treasure", "wait3,tell:act_treasureroom_cutscene_director", 70),
    ("40_rooftop", "Levels\\school\\RoofTop\\roof01", "wait3,tell:act_rooftop_cutscene_director", 60),
    ("41_evilcrash_cuddle", "Levels\\AltEarth\\Lab\\psycho", "", 60),
    ("42_core_intro", "Levels\\AltEarth\\Hub\\coreent", "wait3,tell:act_altearth_coreintro_cutscene_director", 60),
    ("43_penguin", "Levels\\Ice\\IceClimb\\bergext", "wait3,tell:act_ice_penguin_cutscene_director", 60),
    ("46_party_arena", "Levels\\Earth\\Hub\\bossarea", "wait3,tell:act_party_arena_director", 70),
    ("48_school_hub_to_ice_hub", "Levels\\Ice\\Hub\\labext", "wait3,tell:act_cortex_airship:83", 60),
    ("50_beach_training", "Levels\\Earth\\Hub\\beach", "wait3,tell:act_beach_training_cutscene_director1", 70),
    # Reached in play by TwinsanityEvolved's tests (their pad logs); here their directors told as their triggers tell them
    ("05_earth_hub", "Levels\\Earth\\Hub\\hubd", "wait3,tell:act_earth_hub_cutscene_director", 70),
    ("07_totem", "Levels\\Earth\\DocAmok\\docamok1", "wait3,tell:act_totem_cutscene_director", 70),
    ("11_psychotron", "Levels\\Ice\\Hub\\psycho", "wait3,tell:act_psychotron_cutscene_director", 80),
    ("26_lab_psychetron_room", "Levels\\Ice\\Hub\\psycho", "wait3,tell:act_lab_psychetron_room_cutscene_director", 80),
    ("27_slipslide_shed", "Levels\\Ice\\SlipSlide\\l05chasa", "wait3,tell:act_slipslide_shed_cutscene_director", 70),
    ("30_bell_tower", "Levels\\school\\Madame\\amberly", "wait3,tell:act_bell_tower_cutscene_director", 70),
    ("34_dingodile_hut", "Levels\\Ice\\HighSeas\\gpa01", "wait3,tell:act_slipslide_dingodile_hut_cutscene", 70),
    ("35_cortex_pipe", "Levels\\school\\Boiler\\boiler_1", "wait3,tell:act_br_cortex_pipe_cutscene_director", 70),
    ("37_psychetron_room_h02j", "Levels\\Ice\\Hub\\psycho", "wait3,tell:act_psychetron_room_cutscene_h02j_director", 80),
    ("39_beach_aku", "Levels\\Earth\\Hub\\beach", "wait3,tell:act_beach_aku_cutscene_director", 70),
    ("44_buschase", "Levels\\school\\RoofTop\\buschase", "wait3,tell:act_buschase_cutscene_director", 70),
    ("45_evilcrash_house", "Levels\\AltEarth\\Hub\\alta", "wait3,tell:act_evilcrash_house_cutscene_director", 80),
    ("49_crows_nest", "Levels\\Ice\\HighSeas\\gpa09", "wait3,tell:act_crows_nest_director", 70),
]

parser = argparse.ArgumentParser()
parser.add_argument("names", nargs="*")
parser.add_argument("--jobs", type=int, default=4)
parser.add_argument("--out", default=str(HERE.parent / "compare" / "skips"))
parser.add_argument("--exe", default=str(HERE / "build" / "native" / "cmake" / "CrashTwinsanity.exe"))
parser.add_argument("--seconds", type=int, default=0, help="play this long instead of each scene's")
parser.add_argument("--waits", action="store_true",
                    help="a third run, the waits' skip where a skip rule is polled too ($TWIN_SKIP_WAITS_FIRST): both compared")
args = parser.parse_args()
# Absolute: each run starts in its own folder, and a relative APPDATA there would miss the settings (the disc image dialog)
args.out = str(Path(args.out).resolve())
args.exe = str(Path(args.exe).resolve())


def run(scene, mode):
    name, chunk, hops, seconds = scene[:4]
    seconds = args.seconds or seconds
    folder = Path(args.out) / name / mode
    if folder.exists():
        shutil.rmtree(folder)
    settings = folder / "settings" / "Crash Twinsanity"
    settings.mkdir(parents=True)
    (settings / "disc_image.txt").write_text(local_config.disc_image() + "\n", newline="\n")
    (settings / "options.txt").write_text("pause_on_focus_loss=0\nskip_cutscenes=1\n", newline="\n")
    env = dict(os.environ, APPDATA=str(folder / "settings"), HOME=str(folder / "settings"), TWIN_SWEEP_LEVEL=chunk,
               TWIN_SWEEP_SECONDS=str(seconds), TWIN_SWEEP_TIMEOUT="180", TWIN_SWEEP_IDLE="1", TWIN_FIXED_TIME="1",
               TWIN_DETAILED_LOG="1", TWIN_SCENE_STATE="0.5", TWIN_HOP=hops,
               TWIN_SWEEP_GAME_TIME="1", TWINSANITY_AUDIO="frames", TWIN_TEST_PICK=local_config.disc_image())
    if mode == "skipped":
        env.update(TWIN_SKIP_TEST="0.5")
    if mode == "waits":
        env.update(TWIN_SKIP_TEST="0.5", TWIN_SKIP_ALL_SCENES="1", TWIN_SKIP_WAITS_FIRST="1")
    log = folder / "log.txt"
    with open(log, "w") as out:
        try:
            status = subprocess.run([args.exe, "--headless", "--quiet-stubs"], env=env, stdout=out, stderr=subprocess.STDOUT,
                                    cwd=folder, timeout=seconds * 4 + 300).returncode
        except subprocess.TimeoutExpired:
            status = "timeout"
    return status, log.read_text(errors="replace")


NUMBER = re.compile(r"-?\d+\.\d+")


def blocks(log):
    """The scene states a run logged: each a list of its lines (the time stamp off)"""
    found, current = [], None
    for line in log.splitlines():
        m = re.search(r"scene state: (.*)", line)
        if not m:
            continue
        text = m.group(1)
        if text.startswith("the player "):
            current = [text]
            found.append(current)
        elif text == "end":
            current = None
        elif current is not None:
            current.append(text)
    return found


IDLE_STATE = re.compile(r"(\[\d+\.\d+ \S*IDLE\S*) -?\d+")
RUNNER = re.compile(r"\[(\d+\.\d+) (\S+)")


def same(a, b):
    """Two scene state lines alike: the same once their places' numbers are within the tolerance (an idle graph's state, which
    goes round by itself, left out)"""
    a, b = IDLE_STATE.sub(r"\1", a), IDLE_STATE.sub(r"\1", b)
    if NUMBER.sub("#", a) != NUMBER.sub("#", b):
        return False
    return all(abs(float(x) - float(y)) <= PLACE_TOLERANCE for x, y in zip(NUMBER.findall(a), NUMBER.findall(b)))


def outline(line):
    """What a line says without its numbers and states: the instance, the player's inputs, the graphs its runners' first levels
    run (a level under them comes and goes as the agent goes on by itself)"""
    head = re.sub(r" at \(.*|: *\[.*", "", NUMBER.sub("#", line))
    return head, [graph for level, graph in RUNNER.findall(line) if level.endswith(".0")]


def key(line):
    """What a scene state line is about: the player, an agent, an actor taking another's object or running a scene"""
    m = re.match(r"(agent \S+|the player|\S+ takes its object|\S+ runs a scene)", line)
    return m.group(1) if m else line


def compare(watched, skipped):
    """The differences between two runs' scene states, as text lines. Each cutscene's state is taken twice, a second apart: what
    changed between the two in either run goes on by itself (a patrol, a chase, an idle loop) and is compared by the graphs it
    runs alone; the rest strictly. An agent only one run counts among the cutscene's isn't a difference (which instances a run
    notes as agents follows what it did: a skip rule's path tells others)"""
    differences = []
    for index in range(0, min(len(watched), len(skipped)) - 1, 2):
        w0, w1, s0, s1 = ({key(line): line for line in block} for block in watched[index:index + 2] + skipped[index:index + 2])
        for name in sorted(set(w1) | set(s1)):
            a, b = w1.get(name), s1.get(name)
            if (a is None or b is None) and name.startswith("agent "):
                continue
            live = w0.get(name) != a or s0.get(name) != b
            if a is None or b is None or (outline(a) != outline(b) if live else not same(a, b)):
                differences.append(f"#{index // 2 + 1}{' (live)' if live else ''} watched: {a}\n      skipped: {b}")
    return differences


def how(log):
    """How the skipped run's cutscenes went: skipped by the waits, by a rule, or refused"""
    parts = []
    for line in log.splitlines():
        m = re.search(r"cutscene skip: (over after \d+ frames|given back.*|.* plays as watched.*)", line)
        if m:
            parts.append(m.group(1)[:80])
        m = re.search(r"\] skip: (.{0,60})", line)
        if m:
            parts.append("rule: " + m.group(1))
    return "; ".join(parts) or "nothing skipped"


def verdict(watched, other, logs):
    differences = compare(watched, other)
    fatal = [line for log in logs for line in log.splitlines() if "fatal" in line]
    result = "FATAL" if fatal else "same" if not differences and watched else "no scene" if not watched else "DIFFERENT"
    return result, differences, fatal


def check(scene):
    modes = ["watched", "skipped"] + (["waits"] if args.waits else [])
    with ThreadPoolExecutor(len(modes)) as pool:
        logs = [log for _, log in pool.map(lambda mode: run(scene, mode), modes)]
    watched = blocks(logs[0])
    results = []
    for mode, log in zip(modes[1:], logs[1:]):
        result, differences, fatal = verdict(watched, blocks(log), [logs[0], log])
        results.append((mode, result, how(log), differences, fatal))
    return scene[0], len(watched), results


wanted = [scene for scene in SCENES if not args.names or any(n in scene[0] for n in args.names)]
Path(args.out).mkdir(parents=True, exist_ok=True)
report = Path(args.out) / "report.txt"
with ThreadPoolExecutor(max(1, args.jobs // (3 if args.waits else 2))) as pool, open(report, "w") as out:
    for name, count, results in pool.map(check, wanted):
        for mode, result, skip, differences, fatal in results:
            line = f"{name:30} {mode:8} {result:9} {count} scene states; {skip}"
            print(line, flush=True)
            out.write(line + "\n")
            for text in fatal[:2] + differences[:12]:
                out.write("    " + text + "\n")
print(f"details in {report}")
