"""Makes src/platform/native/cutsceneskiptable.h, the table the cutscene skip asks which scripted cutscenes the waits' skip may
end (THE LIST 28, src/platform/native/cutsceneskip.h): every scripted cutscene of the disc as TwinsanityEvolved's census found
them (native/third_party/twinsanityevolved/cutscene_skip_pal.json, its cutscene skip judge's fixture, used with menaman123's
permission under its MIT licence: menaman123/TwinsanityEvolved#19), each by its site (the graph that runs CutsceneStart) and the
graphs only it runs, and OUR status for it below: only a cutscene checked here (watched and skipped, what each leaves compared)
is skippable; the rest play as watched (the skip button does nothing in them). A cutscene a skip rule (condition 572 or the
triangle) skips stays the rule's: the waits' skip only starts where no rule is polled, or where WAITS_FIRST says the waits' skip
leaves the game as watching does and the rule doesn't (THE LIST 22).

    python native/tools/cutscene_skip_table.py          (writes the header)
    python native/tools/cutscene_skip_table.py --check  (exit 1 when the header isn't what it would write)
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]
JSON = HERE / "native" / "third_party" / "twinsanityevolved" / "cutscene_skip_pal.json"
HEADER = HERE / "src" / "platform" / "native" / "cutsceneskiptable.h"

# The cutscenes checked here (native/tools/check_cutscene_skips.py --waits, 2026-11-08: watched and skipped from the same start,
# what each leaves alike), by their names in the census: skippable by the waits' skip
VERIFIED = {
    "03_bear", "04_cavern", "06_tiki_mon", "08_henchmania_bossfight", "10_dingodile_intro", "12_ukafight", "13_icelab",
    "14_sentry", "15_slipslide", "16_school_hub", "17_dorm_room", "18_ukauka_defeated", "22_totem_tied", "23_totem_falling",
    "24_rockslide", "25_walrus", "28_classroom_chaos", "29_angry_skunk", "33_icelab_interior", "40_rooftop",
    "41_evilcrash_cuddle", "42_core_intro", "43_penguin", "46_party_arena", "48_school_hub_to_ice_hub", "50_beach_training",
    # Their directors told their triggers' message (TwinsanityEvolved's tests reach them in play). Dingodile's hut: only Cortex of
    # l05brawl differs, falling through its floor in both runs (the test starts in gpa01, without the ball's arrival)
    "05_earth_hub", "07_totem", "11_psychotron", "30_bell_tower", "34_dingodile_hut", "39_beach_aku", "44_buschase",
    "45_evilcrash_house", "49_crows_nest",
}
# The cutscenes whose skip is the waits' even where a skip rule is polled: the rule's skip leaves them unlike watching does (an
# actor still taking the director's object, Crash stopped elsewhere in Classroom Chaos: THE LIST 22), or a polled rule never
# skips them (the triangle polled in a cutscene for something else)
WAITS_FIRST = {
    "10_dingodile_intro", "12_ukafight", "13_icelab", "14_sentry", "16_school_hub", "17_dorm_room", "22_totem_tied",
    "24_rockslide", "25_walrus", "28_classroom_chaos", "29_angry_skunk", "42_core_intro", "43_penguin", "50_beach_training",
    "05_earth_hub", "07_totem", "11_psychotron", "30_bell_tower", "39_beach_aku",
    # The rule left Dingodile and the snowball elsewhere (the hut), the blimp shown and flying (Rooftop Rampage's start)
    "34_dingodile_hut", "40_rooftop",
}


def main():
    data = json.loads(JSON.read_text(encoding="utf-8"))
    rows = []
    for cutscene in data["cutscenes"]:
        name = cutscene["name"]
        if cutscene["status"] == "refused":
            status = "CutsceneSite::Refused"
        elif name in VERIFIED:
            status = "CutsceneSite::Skippable"
        else:
            status = "CutsceneSite::Unchecked"
        first = "true" if name in WAITS_FIRST else "false"
        graphs = ", ".join(str(graph) for graph in cutscene["graphs"])
        label = cutscene["label"].replace("\\", "\\\\").replace('"', '\\"')
        chunk = cutscene.get("chunk", "").replace("\\", "\\\\")
        rows.append(f'    {{"{name}", "{label}", "{chunk}", {cutscene["site"]}, {status}, {first}, {len(cutscene["graphs"])}, '
                    f'{{{graphs}}}}},')
    unknown = (set(VERIFIED) | set(WAITS_FIRST)) - {cutscene["name"] for cutscene in data["cutscenes"]}
    if unknown:
        sys.exit(f"not in the census: {sorted(unknown)}")
    most = max(len(cutscene["graphs"]) for cutscene in data["cutscenes"])
    text = f"""#pragma once
// Made by native/tools/cutscene_skip_table.py (don't edit): the scripted cutscenes of the disc as TwinsanityEvolved's census found
// them (its cutscene skip judge's fixture, native/third_party/twinsanityevolved/cutscene_skip_pal.json: Copyright (c) 2026 the
// TwinsanityEvolved contributors, MIT, used with menaman123's permission, menaman123/TwinsanityEvolved#19), each by its site and
// the graphs only it runs, with this port's status for the waits' skip (cutsceneskip.h)
#include "common.h"

struct CutsceneSite
{{
    enum Status : u8
    {{
        // Not checked here yet: plays as watched
        Unchecked = 0,
        // Checked: watched and skipped leave the game alike
        Skippable = 1,
        // A level's transition or the title's flyby (TwinsanityEvolved's refusals): never skipped
        Refused = 2,
    }};

    static constexpr u32 MostGraphs = {most};

    const char* name;
    const char* label;
    const char* chunk;
    u32 site;
    Status status;
    // Skipped by the waits even where a skip rule is polled (THE LIST 22)
    bool waitsFirst;
    u32 graphCount;
    u32 graphs[MostGraphs];
}};

inline constexpr CutsceneSite g_CutsceneSites[] = {{
{chr(10).join(rows)}
}};
"""
    if "--check" in sys.argv:
        sys.exit(0 if HEADER.exists() and HEADER.read_text(encoding="utf-8") == text else 1)
    HEADER.write_text(text, encoding="utf-8", newline="\n")
    print(f"{HEADER.relative_to(HERE)}: {len(rows)} cutscenes, {len(VERIFIED)} skippable, {len(WAITS_FIRST)} the waits' first")


if __name__ == "__main__":
    main()
