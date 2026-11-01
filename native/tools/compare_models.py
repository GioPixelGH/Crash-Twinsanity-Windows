#!/usr/bin/env python3
"""The characters against the PS2 game (native/WINDOWS.md, "The characters against PCSX2"): the same places and moments in the
port and in the retail game in PCSX2 (the tests' own copy, local.json's), aligned by the picture, side by side in a local HTML
page with each character's crops, a flip, the difference and the numbers. Nothing of it goes into a repository: the pictures
are the game's (the default folder is beside the working tree, ..\\compare).

    .venv\\Scripts\\python.exe native\\tools\\compare_models.py [--out FOLDER] [--exe PATH] [--configs player,ps2,hw8]
        [--moments beach,cortex,...] [--pcsx2-again] [--port-again] [--label TEXT] [--env K=V]...

The configurations:
- player: as the player sees it. The port fullscreen at the screen's size with copies of the player's options, resolution and
  texture pack (%APPDATA%\\Crash Twinsanity's, read only, copied into FOLDER\\user once; the FPS counter off), three threads,
  the window as shown; PCSX2 fullscreen with the [EmuCore/GS] of the player's own PCSX2 (C:\\Program Files\\PCSX2\\inis\\PCSX2.ini,
  read only), its screenshot at the screen's size.
- ps2: the PS2's own picture. The port at 1x against PCSX2's software renderer at 1x (512 x 512).
- hw8: the hardware renderers at the port's scale. The port at 8x against PCSX2's hardware renderer at 8x, the pictures drawn
  (4096 x 4096), the textures filtered as the game asks in both.
A moment is a place's run (a chunk started as $TWIN_START_CHUNK does, a pad script: the camera turned, Crash walked into the
first boss's arena) and a time in it: PCSX2 keeps a few screenshots there, the port's frames around it are drawn at 1x and
the most alike is the moment's (its likeness a number; a pair whose characters differ more than POSE_LIMIT isn't valid), then
drawn again in each configuration. The PCSX2 runs are kept (FOLDER\\refs) and made again only with --pcsx2-again.
The page: FOLDER\\page\\index.html (and LABEL.html with --label, a page of its own for a build's runs).
"""
import argparse
import base64
import html
import io
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gsdump  # noqa: E402

HERE = Path(__file__).resolve().parents[2]
PYTHON = sys.executable
PLAYER_SETTINGS = Path(os.environ.get("APPDATA", "")) / "Crash Twinsanity"
PLAYER_PCSX2_INI = Path(r"C:\Program Files\PCSX2\inis\PCSX2.ini")
# The rendering keys of the player's PCSX2 taken into the player configuration's PCSX2 runs (not its folders, captures, OSD)
PLAYER_GS_KEYS = ["Renderer", "upscale_multiplier", "filter", "MaxAnisotropy", "accurate_blending_unit", "dithering_ps2",
                  "texture_preloading", "mipmap_hw", "mipmap", "TriFilter", "AspectRatio", "FMVAspectRatioSwitch",
                  "linear_present_mode", "pcrtc_antiblur", "pcrtc_offsets", "pcrtc_overscan", "disable_interlace_offset",
                  "deinterlace_mode", "IntegerScaling", "StretchY", "CropLeft", "CropTop", "CropRight", "CropBottom", "fxaa",
                  "CASMode", "CASSharpness", "ShadeBoost", "TVShader", "HWDownloadMode", "paltex", "autoflush_sw", "UserHacks",
                  "UserHacks_HalfPixelOffset", "UserHacks_round_sprite_offset", "UserHacks_BilinearHack",
                  "UserHacks_TextureInsideRt", "UserHacks_NativePaletteDraw", "UserHacks_CPU_FB_Conversion",
                  "UserHacks_GPUTargetCLUTMode", "UserHacks_AutoFlushLevel", "LoadTextureReplacements"]
CONFIGS = {
    "player": "As the player sees it: the player's settings (the port fullscreen, PCSX2 with the player's PCSX2 settings)",
    "ps2": "The PS2's own picture: the port at 1x, PCSX2's software renderer at 1x",
    "hw8": "The hardware renderers at the port's scale: both at 8x, the pictures drawn",
}
# The places: the chunk started in, the port's pad script and PCSX2's keys, the same input. The port runs at 50 Hz here, as the
# PAL game in PCSX2 (the follow camera's smoothing goes a frame at a time: at 60 Hz its turn differs), its play starting at its
# pad's 579th read on the beach and its 460th in bossarea ($TWIN_PAD_SCRIPT logs it): the right stick down 2 s from 0.82 s
# into play (the camera up as far as it goes), the left stick up 6 s from 0.8 s (Crash walked into the arena: its cutscene
# from read 700); and the seconds a PCSX2 run may take
PLACES = {
    "beach": dict(chunk="Levels\\Earth\\Hub\\beach", pad="620-720:ry=255", keys=["p0.82:rdown:2.0"], timeout=200),
    "bossarea": dict(chunk="Levels\\Earth\\Hub\\bossarea", pad="500-800:ly=0", keys=["p0.8:lup:6.0"], timeout=260),
}
# The moments: their place, a title, the port's frames that show it (at 50 Hz), PCSX2's screenshots around it (pN after play
# starts, cN after the cutscene starts, both in the game's own seconds: run_pcsx2.py --game-time, as PCSX2 at a high resolution
# runs slower than the PS2) and each character's box (fractions of the picture: the same camera in both). PCSX2's cutscene
# state comes 1.2 to 2 s after the port's, a run's own (its cN: Cortex against the sky about c2.4-3.0, Crash close up about
# c2.7-4.2, Cortex again after; the port at 50 Hz: Cortex frames 780-900, Crash 920-1100, Cortex 1120-1180, the party
# from 1968): a moment's pair is the most alike of all the place's screenshots and its frames (its frames' ranges: Cortex's
# two shots against the sky)
MOMENTS = {
    "beach": dict(place="beach", title="The beach: Crash and Aku Aku against the sky, the camera looking up",
                  frames=[(700, 1100)], shots=["p3.0", "p3.4", "p3.8", "p4.2", "p4.6", "p5.0", "p5.4", "p5.8"],
                  crops={"Crash": (0.40, 0.25, 0.70, 0.95), "Aku Aku": (0.52, 0.18, 0.70, 0.45)}),
    "cortex": dict(place="bossarea", title="The first boss's cutscene: Cortex against the sky",
                   frames=[(780, 910), (1125, 1190)], shots=["c1.2", "c1.5", "c1.8", "c2.1", "c3.3", "c3.6"],
                   crops={"Cortex": (0.35, 0.15, 0.65, 0.95)}),
    "crash": dict(place="bossarea", title="The first boss's cutscene: Crash close up",
                  frames=[(915, 1100)], shots=["c2.4", "c2.7", "c3.0"],
                  crops={"Crash": (0.25, 0.05, 0.75, 0.95)}),
    "party": dict(place="bossarea", title="The first boss's cutscene: the characters at the party, against the sky",
                  frames=[(1960, 2400)], shots=["c26.4", "c27.0", "c27.6", "c28.2", "c28.8", "c29.4"],
                  crops={"Left": (0.0, 0.1, 0.35, 0.95), "Middle": (0.35, 0.1, 0.65, 0.95), "Right": (0.65, 0.1, 1.0, 0.95)}),
}
# The most two pictures' characters may differ (a channel's mean, 0-255, the crops at 128 pixels) for the pair to be valid
POSE_LIMIT = 14.0
# A difference worth listing: pixels over 48 (a channel), in a region of at least this share of the picture
REGION_SHARE = 0.0004


def local_json():
    return json.loads((HERE / "local.json").read_text())


# ---- the pictures' measures


def load(path, size=None):
    im = Image.open(path).convert("RGB")
    if size is not None and im.size != size:
        im = im.resize(size, Image.BOX if im.width > size[0] else Image.LANCZOS)
    return np.asarray(im, dtype=np.int32)


def small(path, width=192):
    im = Image.open(path).convert("RGB")
    return np.asarray(im.resize((width, width * 3 // 4), Image.BOX), dtype=np.float32)


def box_of(a, box):
    h, w = a.shape[:2]
    x0, y0, x1, y1 = box
    return a[int(y0 * h):int(y1 * h), int(x0 * w):int(x1 * w)]


def ps2_steps(a, n):
    """The PS2-sized steps' excess per 1000 PS2 pixels in a picture of n x n pixels a PS2 pixel (see gridsteps)"""
    excess, ratio, phase = gridsteps(a, n)
    return excess, ratio


def gridsteps(a, n, threshold=18):
    """Edges exactly a PS2 pixel long on the PS2 pixels' boundaries: for each of the n x n phases of the grid, the blocks'
    sides whose n pixel pairs across all differ (a channel sum over threshold). Drawn at the scale every phase has about as
    many (a straight edge lies anywhere); a layer made at the PS2's resolution adds them at one phase. The excess over the
    phases' median per 1000 PS2 pixels, its ratio to the median and the phase"""
    if n < 2:
        return 0.0, 1.0, (0, 0)
    h, w, _ = a.shape
    dx = np.abs(a[:, 1:] - a[:, :-1]).sum(axis=2) > threshold
    dy = np.abs(a[1:] - a[:-1]).sum(axis=2) > threshold
    res = np.zeros((n, n), np.int64)
    for ox in range(n):
        xs = np.arange(n + ox, w, n) - 1
        for oy in range(n):
            rows = (h - oy) // n
            if rows > 0 and len(xs):
                res[oy, ox] += dx[oy:oy + rows * n, xs].reshape(rows, n, len(xs)).all(axis=1).sum()
    for oy in range(n):
        ys = np.arange(n + oy, h, n) - 1
        for ox in range(n):
            cols = (w - ox) // n
            if cols > 0 and len(ys):
                res[oy, ox] += dy[ys, ox:ox + cols * n].reshape(len(ys), cols, n).all(axis=2).sum()
    phase = np.unravel_index(np.argmax(res), res.shape)
    median = float(np.median(res))
    blocks = max(1, (h // n) * (w // n))
    return (res[phase] - median) * 1000.0 / blocks, res[phase] / max(median, 1.0), (int(phase[0]), int(phase[1]))


def edges(a, threshold=40):
    g = np.zeros(a.shape[:2], bool)
    g[:, 1:] |= np.abs(a[:, 1:] - a[:, :-1]).sum(axis=2) > threshold
    g[1:, :] |= np.abs(a[1:] - a[:-1]).sum(axis=2) > threshold
    return g


def dilate(m, r):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out |= np.roll(np.roll(m, dy, axis=0), dx, axis=1)
    return out


def outline_match(a, b, radius):
    """The share of each picture's outline pixels within radius of the other's (both ways, the lower)"""
    ea, eb = edges(a), edges(b)
    if ea.sum() == 0 or eb.sum() == 0:
        return 0.0
    near_b, near_a = dilate(eb, radius), dilate(ea, radius)
    return min((ea & near_b).sum() / ea.sum(), (eb & near_a).sum() / eb.sum())


def sharpness(a):
    """The texture's detail: the mean of the luminance's Laplacian's absolute value"""
    y = a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114
    lap = np.abs(4 * y[1:-1, 1:-1] - y[:-2, 1:-1] - y[2:, 1:-1] - y[1:-1, :-2] - y[1:-1, 2:])
    return float(lap.mean())


def best_shift(port, pcsx2, reach):
    """The whole-pixel shift (dx, dy) of PCSX2's picture that best fits the port's, within reach: at a higher scale the port's
    picture lies half a PS2 pixel up and left of PCSX2's (its samples start at the GS's sample point, PCSX2's at the PS2
    pixel's centres), which alone would make every outline a difference"""
    a = port[..., 1].astype(np.float32)
    b = pcsx2[..., 1].astype(np.float32)
    h, w = a.shape
    y0, y1, x0, x1 = h // 6, 5 * h // 6, w // 6, 5 * w // 6
    best = None
    for dy in range(-reach, reach + 1):
        for dx in range(-reach, reach + 1):
            d = float(np.abs(a[y0:y1:2, x0:x1:2] - b[y0 + dy:y1 + dy:2, x0 + dx:x1 + dx:2]).mean())
            if best is None or d < best[0]:
                best = (d, dx, dy)
    return best[1], best[2]


def shifted(port, pcsx2, dx, dy):
    """The two pictures where they overlap, PCSX2's moved by (dx, dy)"""
    h, w = port.shape[:2]
    ax0, ay0 = max(0, -dx), max(0, -dy)
    ax1, ay1 = w - max(0, dx), h - max(0, dy)
    return port[ay0:ay1, ax0:ax1], pcsx2[ay0 + dy:ay1 + dy, ax0 + dx:ax1 + dx]


def compare(port, pcsx2, n, crops):
    """The numbers of a pair of pictures of the same size, n pixels a PS2 pixel (PCSX2's moved to fit the port's first)"""
    reach = max(2, int(round(max(n, 4) / 2)) + 1)
    dx, dy = best_shift(port, pcsx2, reach)
    port, pcsx2 = shifted(port, pcsx2, dx, dy)
    d = np.abs(port - pcsx2)
    out = {"mean": float(d.mean()), "over16": float((d.max(axis=2) > 16).mean() * 100),
           "steps_port": gridsteps(port, n)[0] if n == int(n) else None,
           "steps_pcsx2": gridsteps(pcsx2, n)[0] if n == int(n) else None, "crops": {}}
    radius = max(1, int(round(n / 2)))
    for name, box in crops.items():
        p, q = box_of(port, box), box_of(pcsx2, box)
        dd = np.abs(p - q)
        out["crops"][name] = {
            "mean": float(dd.mean()), "over16": float((dd.max(axis=2) > 16).mean() * 100),
            "outline": outline_match(p, q, radius) * 100,
            "steps_port": gridsteps(p, int(n))[0] if n == int(n) and n > 1 else None,
            "steps_pcsx2": gridsteps(q, int(n))[0] if n == int(n) and n > 1 else None,
            "sharp_port": sharpness(p), "sharp_pcsx2": sharpness(q)}
    out["shift"] = (dx, dy)
    return out


def regions(port, pcsx2, limit=8):
    """The differences' regions: pixels over 48 apart, grouped in cells of 1/40 of the picture; the largest groups' boxes"""
    d = np.abs(port - pcsx2).max(axis=2) > 48
    h, w = d.shape
    ch, cw = max(1, h // 40), max(1, w // 40)
    cells = d[:h // ch * ch, :w // cw * cw].reshape(h // ch, ch, w // cw, cw).mean(axis=(1, 3)) > 0.08
    seen = np.zeros_like(cells)
    found = []
    for y in range(cells.shape[0]):
        for x in range(cells.shape[1]):
            if cells[y, x] and not seen[y, x]:
                stack, members = [(y, x)], []
                seen[y, x] = True
                while stack:
                    cy, cx = stack.pop()
                    members.append((cy, cx))
                    for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                        if 0 <= ny < cells.shape[0] and 0 <= nx < cells.shape[1] and cells[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = True
                            stack.append((ny, nx))
                ys, xs = [m[0] for m in members], [m[1] for m in members]
                share = d[min(ys) * ch:(max(ys) + 1) * ch, min(xs) * cw:(max(xs) + 1) * cw].sum() / (h * w)
                if share >= REGION_SHARE:
                    found.append((share, ((min(xs)) / cells.shape[1], min(ys) / cells.shape[0], (max(xs) + 1) / cells.shape[1],
                                          (max(ys) + 1) / cells.shape[0])))
    found.sort(reverse=True)
    return found[:limit]


# ---- the runs


def prepare_player(out):
    """Copies of the player's options, resolution and texture pack, and their PCSX2's GS settings (once)"""
    user = out / "user"
    user.mkdir(parents=True, exist_ok=True)
    for name in ("options.txt", "resolution.txt"):
        if not (user / name).exists() and (PLAYER_SETTINGS / name).exists():
            shutil.copy2(PLAYER_SETTINGS / name, user / name)
    if not (user / "textures").exists() and (PLAYER_SETTINGS / "textures").exists():
        shutil.copytree(PLAYER_SETTINGS / "textures", user / "textures")
    if not (user / "pcsx2_gs.json").exists() and PLAYER_PCSX2_INI.exists():
        text = PLAYER_PCSX2_INI.read_text(encoding="utf-8", errors="replace")
        section = re.search(r"^\[EmuCore/GS\]\n(.*?)(?=^\[|\Z)", text, re.M | re.S)
        values = {}
        for line in (section.group(1) if section else "").splitlines():
            if "=" in line:
                key, value = (part.strip() for part in line.split("=", 1))
                if key in PLAYER_GS_KEYS:
                    values[key] = value
        (user / "pcsx2_gs.json").write_text(json.dumps(values, indent=1))
    return user


def settings_folder(run, disc, options, resolution, pack_from=None):
    if run.exists():
        junction = run / "settings" / "Crash Twinsanity" / "textures"
        if os.path.lexists(junction):
            subprocess.run(["cmd", "/c", "rmdir", str(junction)], capture_output=True)
        shutil.rmtree(run)
    folder = run / "settings" / "Crash Twinsanity"
    folder.mkdir(parents=True)
    (folder / "disc_image.txt").write_text(disc + "\n")
    (folder / "options.txt").write_text("".join(f"{k}={v}\n" for k, v in options.items()))
    if resolution:
        (folder / "resolution.txt").write_text("".join(f"{k}={v}\n" for k, v in resolution.items()))
    if pack_from is not None and pack_from.exists():
        # The pack copy linked in (never copied per run; the junction taken away after)
        subprocess.run(["cmd", "/c", "mklink", "/J", str(folder / "textures"), str(pack_from)], capture_output=True)


def port_run(exe, run, disc, place, frames, scale=None, window=False, user=None, extra_env=None, timeout=600, options=None,
             dumps=False):
    """The port's frames (and the window's, with a window) at those frames, the place's pad script, a fixed frame time"""
    # (the reference configurations: 4:3, as the PS2 game starts in PCSX2, the textures filtered as the game asks)
    overrides = options or {}
    options = {"pause_on_focus_loss": "0", "texture_pack": "0", "texture_filter": "0", "show_fps": "0", "widescreen": "0"}
    options.update(overrides)
    resolution = None
    pack = None
    if user is not None:
        options = {}
        for line in (user / "options.txt").read_text().splitlines():
            if "=" in line and not line.startswith("#"):
                key, value = line.split("=", 1)
                options[key] = value
        options["pause_on_focus_loss"] = "0"
        options["show_fps"] = "0"
        resolution = {}
        for line in (user / "resolution.txt").read_text().splitlines():
            if "=" in line and not line.startswith("#"):
                key, value = line.split("=", 1)
                resolution[key] = value
        pack = user / "textures"
    # 50 Hz, as the PAL game in PCSX2 (PLACES): how the pictures are drawn doesn't depend on it
    options["refresh_60hz"] = "0"
    settings_folder(run, disc, options, resolution, pack)
    wanted = sorted(set(frames))
    # (the pictures' list: only those written; the trailing comma makes one picture a list too)
    listed = ",".join(str(f) for f in wanted) + ","
    env = {k: v for k, v in os.environ.items() if not k.startswith("TWIN_")}
    env.update(HOME=str(run / "settings"), APPDATA=str(run / "settings"), TWIN_FIXED_TIME="1", TWINSANITY_AUDIO="frames",
               TWIN_DUMP_FRAMES=f"{run}:{listed}", TWIN_PAD_SCRIPT=PLACES[place]["pad"],
               TWIN_START_CHUNK=PLACES[place]["chunk"])
    if scale is not None:
        env["TWIN_SCALE"] = str(scale)
    if window:
        env["TWIN_WINDOW_SHOTS"] = f"{run}:{listed}"
    if dumps:
        # The frames' GS commands too, as PCSX2's GS dumps (frame_NNNNNN.gs): PCSX2 draws the same ones
        env["TWIN_GS_DUMP"] = f"{run}:{listed}"
    env.update(extra_env or {})
    args = [str(exe), "--quick"] + ([] if window else ["--headless"])
    last = wanted[-1]
    targets = [run / f"frame_{last:06d}.png"] + ([run / f"window_{last:06d}.png"] if window else [])
    with open(run / "log.txt", "w") as log:
        process = subprocess.Popen(args, env=env, stdout=log, stderr=subprocess.STDOUT, cwd=run)
        started = time.time()
        while process.poll() is None and time.time() - started < timeout:
            time.sleep(0.3)
            for f in list(run.glob("frame_*.png")) + list(run.glob("window_*.png")):
                if int(f.stem.split("_")[1]) not in wanted:
                    try:
                        f.unlink()
                    except OSError:
                        pass
            if all(t.exists() for t in targets):
                for t in targets:
                    size = -1
                    while t.stat().st_size != size:
                        size = t.stat().st_size
                        time.sleep(1.0)
                break
        process.kill()
        process.wait()
    for f in list(run.glob("frame_*.png")) + list(run.glob("window_*.png")):
        if int(f.stem.split("_")[1]) not in wanted:
            f.unlink()
    junction = run / "settings" / "Crash Twinsanity" / "textures"
    if os.path.lexists(junction):
        subprocess.run(["cmd", "/c", "rmdir", str(junction)], capture_output=True)


def pcsx2_capture(folder, disc, place, shots, wide):
    """The retail game's GS commands at a place's moments: PCSX2 (run_pcsx2.py retail, the tests' copy) at 1x, where it keeps
    the PS2's speed, writes a GS dump at each (gsdump_T.gs; the game's widescreen TV flag on for the player's configuration,
    renderer.h's g_WidescreenTv). Each configuration's picture is that dump drawn again (pcsx2_pictures): a picture and the
    commands of the same frame, whatever the settings cost PCSX2 (at 8x the screenshots alone took it a second and more each,
    and its moments slid along the cutscene)"""
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    p = PLACES[place]
    cmd = [PYTHON, str(HERE / "tools" / "run_pcsx2.py"), "retail", disc, "--manual", "--start-chunk", p["chunk"],
           "--timeout", str(p["timeout"]), "--snapshots", str(folder), "--game-time", "--gs", "upscale_multiplier=1"]
    for key in p["keys"]:
        cmd += ["--key", key]
    cmd += ["--write", f"p0.1:D_0030AAC4:{1 if wide else 0}b"]
    for shot in shots:
        cmd += ["--gsdump", shot]
    out = subprocess.run(cmd, capture_output=True, text=True, cwd=HERE)
    (folder / "run.txt").write_text(" ".join(cmd) + "\n\n" + out.stdout + out.stderr)


def pcsx2_pictures(dumps, folder, config, user):
    """Each retail GS dump drawn by PCSX2 in a configuration, kept as screenshot_T.png"""
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    for dump in dumps:
        shot = pcsx2_replay(dump, folder / "replay", config, user)
        if shot is not None:
            shutil.move(str(shot), str(folder / dump.name.replace("gsdump_", "screenshot_").replace(".gs", ".png")))
    if (folder / "replay").exists():
        shutil.rmtree(folder / "replay")


def pcsx2_settings(config, user):
    """run_pcsx2.py's arguments for a configuration's PCSX2 picture"""
    if config == "player":
        values = json.loads((user / "pcsx2_gs.json").read_text()) if (user / "pcsx2_gs.json").exists() else {}
        return ["--fullscreen", "--screenshot-size", "0"] + [a for k, v in values.items() for a in ("--gs", f"{k}={v}")]
    if config == "ps2":
        return ["--gs", "Renderer=13", "--gs", "upscale_multiplier=1"]
    return ["--gs", "upscale_multiplier=8"]


def pcsx2_replay(dump, folder, config, user):
    """A frame's GS commands (the port's $TWIN_GS_DUMP or PCSX2's own of the retail game) drawn by PCSX2 in a configuration:
    its screenshot's path"""
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    cmd = [PYTHON, str(HERE / "tools" / "run_pcsx2.py"), "replay", str(dump), "--snapshots", str(folder), "--screenshot", "8"]
    cmd += pcsx2_settings(config, user)
    out = subprocess.run(cmd, capture_output=True, text=True, cwd=HERE)
    (folder / "run.txt").write_text(" ".join(cmd) + "\n\n" + out.stdout + out.stderr)
    shots = shot_files(folder)
    return shots[0] if shots else None


def shot_files(folder):
    return sorted(folder.glob("screenshot_*.png"))


# ---- alignment


def align(pcsx2_shot, port_frames, crops):
    """The port frame most like PCSX2's screenshot: (frame, whole picture's mean difference, the characters' crops')"""
    target = small(pcsx2_shot)
    best = None
    for number, path in port_frames:
        a = small(path)
        score = float(np.abs(a - target).mean())
        if best is None or score < best[1]:
            best = (number, score, path)
    if best is None:
        return None
    a = small(best[2])
    pose = max(float(np.abs(box_of(a, box) - box_of(target, box)).mean()) for box in crops.values())
    return best[0], best[1], pose


# ---- the page


def data_url(im):
    buffer = io.BytesIO()
    im.save(buffer, "PNG")
    return "data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode()


def heat(port, pcsx2):
    d = np.abs(port - pcsx2).max(axis=2).astype(np.float32)
    v = np.clip(d * 4, 0, 255)
    rgb = np.stack([v, np.clip(v * 0.6 - 60, 0, 255), np.clip(255 - v * 2, 0, 255) * (v > 0)], axis=2)
    rgb[d == 0] = 0
    return Image.fromarray(rgb.astype(np.uint8))


def write_assets(folder, key, port, pcsx2, crops, found, zoom):
    folder.mkdir(parents=True, exist_ok=True)
    ip, iq = Image.fromarray(port.astype(np.uint8)), Image.fromarray(pcsx2.astype(np.uint8))
    files = {"port": f"{key}_port.png", "pcsx2": f"{key}_pcsx2.png", "heat": f"{key}_heat.png", "crops": {}, "regions": []}
    ip.save(folder / files["port"])
    iq.save(folder / files["pcsx2"])
    heat(port, pcsx2).save(folder / files["heat"])
    w, h = ip.size
    for name, box in list(crops.items()):
        x0, y0, x1, y1 = int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h)
        for side, im in (("port", ip), ("pcsx2", iq)):
            c = im.crop((x0, y0, x1, y1))
            c = c.resize((c.width * zoom, c.height * zoom), Image.NEAREST)
            name_file = f"{key}_crop_{re.sub(r'[^A-Za-z0-9]', '', name)}_{side}.png"
            c.save(folder / name_file)
            files["crops"].setdefault(name, {})[side] = name_file
    for index, (share, box) in enumerate(found):
        x0, y0, x1, y1 = int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h)
        pad_x, pad_y = (x1 - x0) // 4 + 8, (y1 - y0) // 4 + 8
        rect = (max(0, x0 - pad_x), max(0, y0 - pad_y), min(w, x1 + pad_x), min(h, y1 + pad_y))
        entry = {"share": share, "box": box}
        for side, im in (("port", ip), ("pcsx2", iq)):
            c = im.crop(rect)
            scale = max(1, min(4, 360 // max(1, c.height)))
            c = c.resize((c.width * scale, c.height * scale), Image.NEAREST)
            name_file = f"{key}_region{index}_{side}.png"
            c.save(folder / name_file)
            entry[side] = name_file
        files["regions"].append(entry)
    return files


PAGE_HEAD = """<!doctype html><html><head><meta charset="utf-8"><title>{title}</title><style>
body{{background:#1b1b1f;color:#ddd;font:14px/1.45 Segoe UI,sans-serif;margin:24px}}
h1{{font-size:22px}} h2{{font-size:18px;margin-top:40px;border-top:1px solid #444;padding-top:16px}} h3{{font-size:15px}}
.pair{{display:flex;gap:8px;flex-wrap:wrap}} .pair figure{{margin:0}} figcaption{{color:#aaa;font-size:12px}}
img{{image-rendering:pixelated;max-width:100%}} .big img{{width:960px;max-width:96vw}}
.flip{{position:relative;display:inline-block;cursor:pointer}} .flip img.b{{position:absolute;left:0;top:0;opacity:0}}
.flip:hover img.b,.flip.on img.b{{opacity:1}} .flip .tag{{position:absolute;left:6px;top:6px;background:#000a;padding:2px 6px}}
table{{border-collapse:collapse;margin:8px 0}} td,th{{border:1px solid #444;padding:3px 8px;text-align:right}}
th{{background:#2a2a30}} pre{{background:#111;padding:8px;white-space:pre-wrap}} td:first-child,th:first-child{{text-align:left}} .bad{{color:#ff7b7b}} .note{{color:#bbb}}
</style><script>
function flip(e){{e.classList.toggle('on');var t=e.querySelector('.tag');t.textContent=e.classList.contains('on')?'PCSX2':'port';}}
</script></head><body>
"""


def fmt(v, digits=1):
    return "-" if v is None else f"{v:.{digits}f}"


def pair_section(body, f, n, other):
    """A pair's pictures (a flip, side by side, the difference), numbers, characters' crops and regions"""
    body.append("<div class='pair big'><figure><div class='flip' onclick='flip(this)'>"
                f"<img class='a' src='{f['port']}'><img class='b' src='{f['pcsx2']}'><span class='tag'>port</span></div>"
                f"<figcaption>the port (hover or click: {html.escape(other)})</figcaption></figure></div>")
    body.append("<div class='pair big'>"
                f"<figure><img src='{f['port']}'><figcaption>the port</figcaption></figure>"
                f"<figure><img src='{f['pcsx2']}'><figcaption>{html.escape(other)}</figcaption></figure>"
                f"<figure><img src='{f['heat']}'><figcaption>the difference</figcaption></figure></div>")
    body.append("<table><tr><th></th><th>mean difference (0-255)</th><th>% of pixels over 16</th><th>outlines matched %</th>"
                f"<th>PS2-sized steps: port</th><th>{html.escape(other)}</th><th>sharpness: port</th>"
                f"<th>{html.escape(other)}</th></tr>")
    body.append(f"<tr><td>whole picture</td><td>{fmt(n['mean'], 2)}</td><td>{fmt(n['over16'])}</td><td>-</td>"
                f"<td>{fmt(n['steps_port'])}</td><td>{fmt(n['steps_pcsx2'])}</td><td>-</td><td>-</td></tr>")
    for name, c in n["crops"].items():
        body.append(f"<tr><td>{html.escape(name)}</td><td>{fmt(c['mean'], 2)}</td><td>{fmt(c['over16'])}</td>"
                    f"<td>{fmt(c['outline'])}</td><td>{fmt(c['steps_port'])}</td><td>{fmt(c['steps_pcsx2'])}</td>"
                    f"<td>{fmt(c['sharp_port'], 2)}</td><td>{fmt(c['sharp_pcsx2'], 2)}</td></tr>")
    body.append("</table>")
    for name, sides in f["crops"].items():
        body.append(f"<h3>{html.escape(name)}</h3><div class='pair'>"
                    f"<figure><div class='flip' onclick='flip(this)'><img class='a' src='{sides['port']}'>"
                    f"<img class='b' src='{sides['pcsx2']}'><span class='tag'>port</span></div>"
                    "<figcaption>flip</figcaption></figure>"
                    f"<figure><img src='{sides['port']}'><figcaption>the port</figcaption></figure>"
                    f"<figure><img src='{sides['pcsx2']}'><figcaption>{html.escape(other)}</figcaption></figure></div>")
    if f["regions"]:
        body.append("<h3>Where they differ (pixels over 48 apart, largest first)</h3>")
        for index, region in enumerate(f["regions"]):
            b = region["box"]
            body.append(f"<p>{index + 1}. at {b[0] * 100:.0f}%-{b[2] * 100:.0f}% across, {b[1] * 100:.0f}%-{b[3] * 100:.0f}% "
                        f"down: {region['share'] * 100:.2f}% of the picture</p><div class='pair'>"
                        f"<figure><div class='flip' onclick='flip(this)'><img class='a' src='{region['port']}'>"
                        f"<img class='b' src='{region['pcsx2']}'><span class='tag'>port</span></div>"
                        "<figcaption>flip</figcaption></figure>"
                        f"<figure><img src='{region['port']}'><figcaption>the port</figcaption></figure>"
                        f"<figure><img src='{region['pcsx2']}'><figcaption>{html.escape(other)}</figcaption></figure></div>")


def page(out, label, results, notes):
    title = "Characters: the port against PCSX2" + (f" ({label})" if label else "")
    body = [PAGE_HEAD.format(title=html.escape(title)), f"<h1>{html.escape(title)}</h1>"]
    body.append("<p class='note'>Each moment: the port's picture and PCSX2's at the same size (hover or click the picture to "
                "flip from the port to PCSX2 in the same place), the difference (black the same, red to yellow apart), each "
                "character's crop enlarged with sharp pixels, the numbers and every region the two differ in. "
                "Steps of the PS2's size: edges exactly one PS2 pixel long on the PS2 pixels' boundaries, per 1000 PS2 "
                "pixels above the other grid positions' (0 a picture drawn at its own resolution). Outlines: the share of "
                "each picture's outline pixels within half a PS2 pixel of the other's. Sharpness: the textures' detail "
                "(the luminance's Laplacian's mean).</p>")
    for note in notes:
        body.append(f"<p class='note'>{html.escape(note)}</p>")
    for r in results:
        body.append(f"<h2>{html.escape(r['title'])} &mdash; {html.escape(CONFIGS[r['config']])}</h2>")
        if r.get("error"):
            body.append(f"<p class='bad'>{html.escape(r['error'])}</p>")
            continue
        valid = r["pose"] <= POSE_LIMIT
        sx, sy = r["numbers"]["shift"]
        body.append(f"<p>Port frame {r['frame']}, PCSX2's screenshot {html.escape(r['shot'])}: pictures {fmt(r['likeness'])} "
                    f"apart at 192 pixels, the characters {fmt(r['pose'])} (valid up to {POSE_LIMIT:.0f})"
                    + ("" if valid else " <span class='bad'>: the poses differ, this pair isn't valid</span>")
                    + f". PCSX2's picture moved {sx}, {sy} pixels to fit (at a higher scale the port's lies half a PS2 pixel "
                      "up and left of PCSX2's: its samples start at the GS's sample point, PCSX2's at the pixels' centres)."
                    + "</p>")
        pair_section(body, r["files"], r["numbers"], "PCSX2")
        if r.get("same"):
            same = r["same"]["numbers"]
            body.append("<h3>The same GS commands drawn by PCSX2</h3><p class='note'>The port's GS commands of this frame "
                        "(its $TWIN_GS_DUMP, a PCSX2 GS dump) played by PCSX2 with the same settings: the game's work, VU1's "
                        "and the commands are the port's in both pictures, so what differs is the two renderers' drawing "
                        f"(PCSX2's picture moved {same['shift'][0]}, {same['shift'][1]} pixels to fit).</p>")
            pair_section(body, r["same"]["files"], same, "PCSX2, the port's commands")
        if r.get("geometry"):
            body.append("<h3>The port's GS commands against the retail game's</h3><p class='note'>This frame's commands in "
                        "the port and the retail game's in PCSX2 at the screenshot's moment (its GS dump), grouped by how "
                        "they're drawn; the poses and the camera a fraction of a frame apart change the counts a little.</p>"
                        f"<pre>{html.escape(chr(10).join(r['geometry']))}</pre>")
    body.append("</body></html>")
    name = (re.sub(r"[^A-Za-z0-9_-]", "_", label) if label else "index") + ".html"
    (out / "page").mkdir(parents=True, exist_ok=True)
    (out / "page" / name).write_text("\n".join(body), encoding="utf-8")
    return out / "page" / name


# ---- the whole


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(HERE.parent / "compare"))
    parser.add_argument("--exe", default=str(HERE / "build/native/cmake/CrashTwinsanity.exe"))
    parser.add_argument("--configs", default="player,ps2,hw8")
    parser.add_argument("--moments", default=",".join(MOMENTS))
    parser.add_argument("--pcsx2-again", nargs="?", const="all", default="",
                        help="make PCSX2's screenshots again (all, or a list of PLACE_CONFIG: bossarea_hw8,beach_player)")
    parser.add_argument("--port-again", action="store_true", help="make the port's coarse frames again (alignment)")
    parser.add_argument("--label", default="", help="the page's name and the port runs' folder (a build's)")
    parser.add_argument("--env", action="append", default=[], metavar="K=V", help="a variable for the port's runs")
    parser.add_argument("--summary", help="write the numbers as JSON there too")
    parser.add_argument("--no-same", dest="same", action="store_false",
                        help="no draw-level comparison (the port's GS commands drawn by PCSX2) nor geometry's")
    args = parser.parse_args()

    out = Path(args.out).resolve()
    exe = Path(args.exe).resolve()
    disc = local_json()["disc_image"]
    user = prepare_player(out)
    configs = [c for c in args.configs.split(",") if c]
    moments = [m for m in args.moments.split(",") if m]
    places = sorted({MOMENTS[m]["place"] for m in moments})
    extra_env = dict(kv.split("=", 1) for kv in args.env)
    label = args.label or "current"
    runs = out / "runs" / label
    refs = out / "refs"
    notes = [f"Port: {exe} ({time.strftime('%Y-%m-%d %H:%M', time.localtime(exe.stat().st_mtime))})"
             + (f", variables {' '.join(args.env)}" if args.env else "")]

    # PCSX2's GS dumps of each place's moments (4:3, and widescreen for the player's), then each configuration's pictures of
    # them (kept)
    wide = "widescreen=0" not in (user / "options.txt").read_text() if (user / "options.txt").exists() else False
    again = args.pcsx2_again.split(",")
    for place in places:
        shots = sorted({s for m in moments if MOMENTS[m]["place"] == place for s in MOMENTS[m]["shots"]},
                       key=lambda t: (t[0], float(t[1:])))
        shapes = {config: "wide" if config == "player" and wide else "narrow" for config in configs}
        captured = set()
        for shape in sorted(set(shapes.values())):
            folder = refs / f"{place}_{shape}"
            if "all" in again or f"{place}_{shape}" in again or not list(folder.glob("gsdump_*.gs")):
                print(f"PCSX2: {place}, the retail game's GS dumps ({shape})", flush=True)
                pcsx2_capture(folder, disc, place, shots, shape == "wide")
                print(f"  {len(list(folder.glob('gsdump_*.gs')))} dumps", flush=True)
                captured.add(shape)
        for config in configs:
            folder = refs / f"{place}_{config}"
            if shapes[config] in captured or f"{place}_{config}" in again or not shot_files(folder):
                print(f"PCSX2: {place}, {config}: the dumps drawn", flush=True)
                pcsx2_pictures(sorted((refs / f"{place}_{shapes[config]}").glob("gsdump_*.gs")), folder, config, user)
                print(f"  {len(shot_files(folder))} screenshots", flush=True)

    # The port's frames around each moment at 1x, to find PCSX2's moment among (kept): 4:3 for the references, the player's
    # widescreen for the player's configuration
    coarse = {}
    for place in places:
        frames = sorted({f for m in moments if MOMENTS[m]["place"] == place
                         for lo, hi in MOMENTS[m]["frames"] for f in range(lo, hi + 1, 2)})
        for shape in (["narrow", "wide"] if "player" in configs and wide else ["narrow"]):
            folder = out / "runs" / "align" / f"{place}_{shape}"
            if args.port_again or not list(folder.glob("frame_*.png")):
                print(f"port, 1x frames: {place} ({shape})", flush=True)
                port_run(exe, folder, disc, place, frames, scale=1, extra_env=extra_env,
                         options={"widescreen": "1"} if shape == "wide" else None)
            coarse[(place, shape)] = sorted((int(p.stem.split("_")[1]), p) for p in folder.glob("frame_*.png"))

    # Each moment's pair, then the port's frame in each configuration
    chosen = {}
    for m in moments:
        spec = MOMENTS[m]
        place = spec["place"]
        for config in configs:
            shape = "wide" if config == "player" and wide else "narrow"
            candidates = [(n, p) for n, p in coarse[(place, shape)] if any(lo <= n <= hi for lo, hi in spec["frames"])]
            # (any of the place's screenshots: where PCSX2's cutscene falls in them changes by up to a second a run; the
            # moment's frames decide which pair is its)
            shots = shot_files(refs / f"{place}_{config}")
            best = None
            for shot in shots:
                found = align(shot, candidates, spec["crops"])
                if found and (best is None or found[2] < best[2]):
                    best = (*found, shot)
            chosen[(m, config)] = best
    wanted = {}
    for (m, config), best in chosen.items():
        if best is not None:
            wanted.setdefault((MOMENTS[m]["place"], config), set()).add(best[0])
    for (place, config), frames in sorted(wanted.items()):
        folder = runs / f"{place}_{config}"
        print(f"port: {place}, {config}, frames {sorted(frames)}", flush=True)
        if config == "player":
            port_run(exe, folder, disc, place, sorted(frames), window=True, user=user, extra_env=extra_env, dumps=True)
        else:
            port_run(exe, folder, disc, place, sorted(frames), scale=1 if config == "ps2" else 8, extra_env=extra_env,
                     dumps=config == "hw8")

    results = []
    summary = {}
    for m in moments:
        for config in configs:
            spec = MOMENTS[m]
            best = chosen.get((m, config))
            entry = {"title": spec["title"], "config": config}
            if best is None:
                entry["error"] = "no PCSX2 screenshot or no port frame to pair it with"
                results.append(entry)
                continue
            frame, likeness, pose, shot = best
            folder = runs / f"{spec['place']}_{config}"
            port_file = folder / (f"window_{frame:06d}.png" if config == "player" else f"frame_{frame:06d}.png")
            if not port_file.exists():
                entry["error"] = f"the port's picture is missing: {port_file}"
                results.append(entry)
                continue
            q = Image.open(shot).convert("RGB")
            p = Image.open(port_file).convert("RGB")
            # The same size: PCSX2's picture made the port's (the player's: the screen's both)
            size = p.size
            port = np.asarray(p, dtype=np.int32)
            pcsx2 = load(shot, size)
            n = size[0] / 512 if config != "player" else size[0] / 512
            numbers = compare(port, pcsx2, round(n) if config != "player" else 0, spec["crops"])
            if config == "player":
                numbers["steps_port"] = numbers["steps_pcsx2"] = None
                for c in numbers["crops"].values():
                    c["steps_port"] = c["steps_pcsx2"] = None
                # The internal picture's steps beside the window's (the port's drawn picture at its scale)
                internal = folder / f"frame_{frame:06d}.png"
                if internal.exists():
                    a = load(internal)
                    scale = round(a.shape[1] / 512)
                    numbers["steps_port"] = gridsteps(a, scale)[0]
                    for name, box in spec["crops"].items():
                        numbers["crops"][name]["steps_port"] = gridsteps(box_of(a, box), scale)[0]
            # (the pictures shown as compared: PCSX2's moved by the shift that fits them)
            port, pcsx2 = shifted(port, pcsx2, *numbers["shift"])
            found = regions(port, pcsx2)
            zoom = 3 if size[0] <= 1024 else 2 if config == "player" else 1
            files = write_assets(out / "page" / (args.label or "index"), f"{m}_{config}", port, pcsx2, spec["crops"], found, zoom)
            files = {k: (f"{args.label or 'index'}/{v}" if isinstance(v, str) else v) for k, v in files.items()}
            for name in files["crops"]:
                files["crops"][name] = {s: f"{args.label or 'index'}/{v}" for s, v in files["crops"][name].items()}
            for region in files["regions"]:
                region["port"] = f"{args.label or 'index'}/{region['port']}"
                region["pcsx2"] = f"{args.label or 'index'}/{region['pcsx2']}"
            # The draw's level: the port's GS commands of this frame drawn by PCSX2 the same way (any difference left is the
            # renderers'), and the geometry's: those commands against the retail game's in PCSX2 at the moment
            dump = folder / f"frame_{frame:06d}.gs"
            if args.same and dump.exists():
                replayed = pcsx2_replay(dump, runs / "same" / f"{m}_{config}", config, user)
                if replayed is not None:
                    port_full = np.asarray(Image.open(port_file).convert("RGB"), dtype=np.int32)
                    again = load(replayed, size)
                    same = compare(port_full, again, round(n) if config != "player" else 0, spec["crops"])
                    sp, sq = shifted(port_full, again, *same["shift"])
                    same_files = write_assets(out / "page" / (args.label or "index"), f"{m}_{config}_same", sp, sq,
                                              spec["crops"], regions(sp, sq), zoom)
                    prefix = f"{args.label or 'index'}/"
                    same_files = {k: (prefix + v if isinstance(v, str) else v) for k, v in same_files.items()}
                    same_files["crops"] = {k: {s: prefix + f for s, f in v.items()} for k, v in same_files["crops"].items()}
                    for region in same_files["regions"]:
                        region["port"], region["pcsx2"] = prefix + region["port"], prefix + region["pcsx2"]
                    entry["same"] = {"numbers": same, "files": same_files}
                    print(f"  the same commands in PCSX2: shift {same['shift']}, mean {same['mean']:.2f}, steps port "
                          f"{fmt(same['steps_port'])} PCSX2 {fmt(same['steps_pcsx2'])}", flush=True)
                retail = refs / f"{spec['place']}_narrow" / shot.name.replace("screenshot_", "gsdump_").replace(".png", ".gs")
                if config == "hw8" and retail.exists():
                    entry["geometry"] = gsdump.compare(str(dump), str(retail), quiet=True)
            entry.update(frame=frame, likeness=likeness, pose=pose, shot=shot.name, numbers=numbers, files=files)
            results.append(entry)
            summary[f"{m}/{config}"] = {"frame": frame, "pose": pose, "valid": pose <= POSE_LIMIT, **{
                k: v for k, v in numbers.items() if k != "crops"}, "crops": numbers["crops"],
                "regions": [r[0] for r in found]}
            if "same" in entry:
                s = entry["same"]["numbers"]
                summary[f"{m}/{config}"]["same"] = {k: s[k] for k in ("mean", "over16", "steps_port", "steps_pcsx2", "shift")}
            if "geometry" in entry:
                summary[f"{m}/{config}"]["geometry"] = entry["geometry"]
            print(f"{m} {config}: frame {frame} ({shot.name}), pose {pose:.1f}, shift {numbers['shift']}, mean {numbers['mean']:.2f}, "
                  f"steps port {fmt(numbers['steps_port'])} PCSX2 {fmt(numbers['steps_pcsx2'])}", flush=True)
    path = page(out, args.label, results, notes)
    if args.summary:
        Path(args.summary).write_text(json.dumps(summary, indent=1))
    (out / "page" / f"{args.label or 'index'}.json").write_text(json.dumps(summary, indent=1))
    print(f"page: {path}")


if __name__ == "__main__":
    main()
