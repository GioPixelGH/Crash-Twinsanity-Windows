#!/usr/bin/env python3
"""PCSX2's GS dumps (.gs, uncompressed) read: the GS state they start from and every primitive their GIF packets draw, with the
registers it's drawn with. Its layout is PCSX2's GS dump format (PCSX2 2.x: a 0xFFFFFFFF word, the header's size, the
header (the state's version and size, the serial's and the screenshot's places), the serial, the screenshot, the GS state as
PCSX2's GSState::Freeze writes it (version 9), the privileged registers (0x2000 bytes), then packets: 0 a transfer (a GIF
path, 1-3, its length in bytes, its data), 1 a vertical blank (the field), 2 a FIFO read (its length), 3 the privileged
registers again). The port writes the same ($TWIN_GS_DUMP, graphics/capture.cpp), so the two programs' draws of a frame can
be set side by side (compare_models.py's geometry, `gsdump.py compare`).

    .venv\\Scripts\\python.exe native\\tools\\gsdump.py FILE.gs                  the frame's draws summed up
    .venv\\Scripts\\python.exe native\\tools\\gsdump.py compare A.gs B.gs [--box x0,y0,x1,y1]
"""
import struct
import sys
from pathlib import Path

STATE_ENV = ["PRIM", "PRMODECONT", "TEXCLUT", "SCANMSK", "TEXA", "FOGCOL", "DIMX", "DTHE", "COLCLAMP", "PABE", "BITBLTBUF",
             "TRXDIR", "TRXPOS", "TRXREG", "TRXREG2"]
STATE_CONTEXT = ["XYOFFSET", "TEX0", "TEX1", "CLAMP", "MIPTBP1", "MIPTBP2", "SCISSOR", "ALPHA", "TEST", "FBA", "FRAME", "ZBUF"]
# A+D addresses: (name, context or None)
ADDRESSES = {0x00: ("PRIM", None), 0x01: ("RGBAQ", None), 0x02: ("ST", None), 0x03: ("UV", None), 0x04: ("XYZF2", None),
             0x05: ("XYZ2", None), 0x06: ("TEX0", 0), 0x07: ("TEX0", 1), 0x08: ("CLAMP", 0), 0x09: ("CLAMP", 1),
             0x0A: ("FOG", None), 0x0C: ("XYZF3", None), 0x0D: ("XYZ3", None), 0x14: ("TEX1", 0), 0x15: ("TEX1", 1),
             0x16: ("TEX2", 0), 0x17: ("TEX2", 1), 0x18: ("XYOFFSET", 0), 0x19: ("XYOFFSET", 1), 0x1A: ("PRMODECONT", None),
             0x1B: ("PRMODE", None), 0x1C: ("TEXCLUT", None), 0x22: ("SCANMSK", None), 0x34: ("MIPTBP1", 0),
             0x35: ("MIPTBP1", 1), 0x36: ("MIPTBP2", 0), 0x37: ("MIPTBP2", 1), 0x3B: ("TEXA", None), 0x3D: ("FOGCOL", None),
             0x3F: ("TEXFLUSH", None), 0x40: ("SCISSOR", 0), 0x41: ("SCISSOR", 1), 0x42: ("ALPHA", 0), 0x43: ("ALPHA", 1),
             0x44: ("DIMX", None), 0x45: ("DTHE", None), 0x46: ("COLCLAMP", None), 0x47: ("TEST", 0), 0x48: ("TEST", 1),
             0x49: ("PABE", None), 0x4A: ("FBA", 0), 0x4B: ("FBA", 1), 0x4C: ("FRAME", 0), 0x4D: ("FRAME", 1),
             0x4E: ("ZBUF", 0), 0x4F: ("ZBUF", 1), 0x50: ("BITBLTBUF", None), 0x51: ("TRXPOS", None), 0x52: ("TRXREG", None),
             0x53: ("TRXDIR", None), 0x54: ("HWREG", None), 0x60: ("SIGNAL", None), 0x61: ("FINISH", None),
             0x62: ("LABEL", None)}
# The primitives' vertex counts: the first primitive's and each one after (strips and fans add one a vertex)
PRIM_NAMES = ["point", "line", "line strip", "triangle", "triangle strip", "triangle fan", "sprite", "?"]


def read(path):
    """(header dict, state dict, privileged registers bytes, packets [(kind, path, data)])"""
    data = Path(path).read_bytes()
    if data[:4] != b"\xff\xff\xff\xff":
        raise ValueError(f"{path}: not a PCSX2 2.x GS dump (no 0xFFFFFFFF first; an older or compressed one?)")
    header_size = struct.unpack_from("<I", data, 4)[0]
    names = ["state_version", "state_size", "serial_offset", "serial_size", "crc", "screenshot_width", "screenshot_height",
             "screenshot_offset", "screenshot_size"]
    header = dict(zip(names, struct.unpack_from("<9I", data, 8)))
    header["serial"] = data[8 + header["serial_offset"]:8 + header["serial_offset"] + header["serial_size"]].decode(errors="replace")
    at = 8 + header_size
    state_bytes = data[at:at + header["state_size"]]
    at += header["state_size"]
    regs = data[at:at + 0x2000]
    at += 0x2000
    packets = []
    while at < len(data):
        kind = data[at]
        at += 1
        if kind == 0:
            path_id = data[at]
            length = struct.unpack_from("<I", data, at + 1)[0]
            packets.append((0, path_id, data[at + 5:at + 5 + length]))
            at += 5 + length
        elif kind == 1:
            packets.append((1, data[at], b""))
            at += 1
        elif kind == 2:
            packets.append((2, 0, data[at:at + 4]))
            at += 4
        elif kind == 3:
            packets.append((3, 0, data[at:at + 0x2000]))
            at += 0x2000
        else:
            raise ValueError(f"{path}: an unknown packet {kind} at {at - 1}")
    return header, parse_state(state_bytes), regs, packets


def parse_state(b):
    version = struct.unpack_from("<I", b, 0)[0]
    at = 4
    state = {"version": version}
    for name in STATE_ENV:
        state[name] = struct.unpack_from("<Q", b, at)[0]
        at += 8
    state["ctx"] = []
    for _ in range(2):
        ctx = {}
        for name in STATE_CONTEXT:
            ctx[name] = struct.unpack_from("<Q", b, at)[0]
            at += 8
        state["ctx"].append(ctx)
    # (PCSX2's vertex: RGBAQ and ST 64 bits, UV and FOG 32, XYZ 64, then an obsolete register)
    for name, size in [("RGBAQ", 8), ("ST", 8), ("UV", 4), ("FOG", 4), ("XYZ", 8), ("obsolete", 8)]:
        state[name] = struct.unpack_from("<Q" if size == 8 else "<I", b, at)[0]
        at += size
    state["FOG"] = state["FOG"] << 56
    return state


class Gs:
    """The registers and vertex queue a primitive needs, from GIF packets"""

    def __init__(self, state):
        self.reg = {name: state.get(name, 0) for name in STATE_ENV}
        self.ctx = [dict(c) for c in state["ctx"]]
        self.reg["PRMODE"] = 0
        self.rgbaq = state.get("RGBAQ", 0)
        self.st = state.get("ST", 0)
        self.uv = state.get("UV", 0)
        self.fog = state.get("FOG", 0)
        self.q = 1.0
        self.queue = []
        self.draws = []
        self.transfers = 0
        self.recording = True

    def attrs(self):
        prim = self.reg["PRIM"]
        mode = prim if (self.reg["PRMODECONT"] & 1) else (self.reg["PRMODE"] & ~7) | (prim & 7)
        return prim & 7, mode

    def write(self, address, value):
        name, context = ADDRESSES.get(address, (None, None))
        if name is None:
            return
        if context is not None:
            self.ctx[context][name] = value
            return
        if name == "PRIM":
            self.reg["PRIM"] = value & 0x7FF
            self.queue = []
        elif name == "RGBAQ":
            self.rgbaq = value
        elif name == "ST":
            self.st = value
        elif name == "UV":
            self.uv = value
        elif name == "FOG":
            self.fog = value
        elif name in ("XYZ2", "XYZF2", "XYZ3", "XYZF3"):
            if name in ("XYZF2", "XYZF3"):
                x, y, z, f = value & 0xFFFF, (value >> 16) & 0xFFFF, (value >> 32) & 0xFFFFFF, (value >> 56) & 0xFF
            else:
                x, y, z, f = value & 0xFFFF, (value >> 16) & 0xFFFF, value >> 32, (self.fog >> 56) & 0xFF
            self.vertex(x, y, z, f, kick=name in ("XYZ2", "XYZF2"))
        else:
            self.reg[name] = value

    def vertex(self, x, y, z, f, kick):
        s, t = struct.unpack("<ff", struct.pack("<Q", self.st))
        q = struct.unpack("<f", struct.pack("<I", self.rgbaq >> 32))[0]
        r, g, b, a = (self.rgbaq >> 0) & 0xFF, (self.rgbaq >> 8) & 0xFF, (self.rgbaq >> 16) & 0xFF, (self.rgbaq >> 24) & 0xFF
        u, v = self.uv & 0x3FFF, (self.uv >> 16) & 0x3FFF
        self.queue.append((x / 16.0, y / 16.0, z, s, t, q, u / 16.0, v / 16.0, r, g, b, a, f))
        if not kick:
            return
        kind, mode = self.attrs()
        need = {0: 1, 1: 2, 2: 2, 3: 3, 4: 3, 5: 3, 6: 2}.get(kind, 99)
        if len(self.queue) < need:
            return
        verts = self.queue[-need:]
        if kind in (0, 1, 3, 6):
            self.queue = []
        elif kind in (2, 4):
            self.queue = self.queue[-(need - 1):]
        elif kind == 5:
            self.queue = [self.queue[0], self.queue[-1]]
        self.primitive(kind, mode, verts)

    def primitive(self, kind, mode, verts):
        if not self.recording:
            return
        c = self.ctx[(mode >> 9) & 1]
        key = (kind, mode, c["TEX0"], c["TEX1"], c["CLAMP"], c["ALPHA"], c["TEST"], c["FRAME"], c["ZBUF"], c["XYOFFSET"],
               self.reg["TEXA"], self.reg["PABE"], self.reg["DTHE"])
        if self.draws and self.draws[-1]["key"] == key:
            self.draws[-1]["prims"].append(verts)
        else:
            self.draws.append({"key": key, "prims": [verts], "index": len(self.draws)})


def decode(packets, state, frame=None):
    """The draws of a dump's packets: each a run of primitives with the same registers (frame: only those between that
    vertical blank and the next, the registers from the packets before them; -1 the last whole one)"""
    gs = Gs(state)
    vsyncs = [i for i, p in enumerate(packets) if p[0] == 1]
    if frame is not None and frame < 0:
        frame = max(0, len(vsyncs) - 1 + frame)
    seen = 0
    for kind, path, data in packets:
        if kind == 1:
            seen += 1
        if kind != 0:
            continue
        if frame is not None:
            gs.recording = seen == frame
        at = 0
        while at + 16 <= len(data):
            lo, hi = struct.unpack_from("<QQ", data, at)
            at += 16
            nloop = lo & 0x7FFF
            pre = (lo >> 46) & 1
            prim = (lo >> 47) & 0x7FF
            flg = (lo >> 58) & 3
            nreg = (lo >> 60) & 0xF or 16
            regs = [(hi >> (4 * i)) & 0xF for i in range(nreg)]
            if pre and flg == 0:
                gs.write(0x00, prim)
            if flg == 0:
                for _ in range(nloop):
                    for r in regs:
                        if at + 16 > len(data):
                            break
                        w = struct.unpack_from("<4I", data, at)
                        q64 = struct.unpack_from("<QQ", data, at)
                        at += 16
                        packed(gs, r, w, q64)
            elif flg == 1:
                count = nloop * nreg
                for i in range(count):
                    if at + 8 > len(data):
                        break
                    value = struct.unpack_from("<Q", data, at)[0]
                    at += 8
                    r = regs[i % nreg]
                    if r != 0xE:
                        gs.write(r, value)
                if count & 1:
                    at += 8
            else:
                gs.transfers += 1
                at += nloop * 16
    return gs


def packed(gs, r, w, q64):
    if r == 0x0:
        gs.write(0x00, w[0] & 0x7FF)
    elif r == 0x1:
        q = struct.unpack("<I", struct.pack("<f", gs.q))[0]
        gs.rgbaq = (w[0] & 0xFF) | (w[1] & 0xFF) << 8 | (w[2] & 0xFF) << 16 | (w[3] & 0xFF) << 24 | q << 32
    elif r == 0x2:
        gs.st = w[0] | w[1] << 32
        gs.q = struct.unpack("<f", struct.pack("<I", w[2]))[0]
    elif r == 0x3:
        gs.uv = (w[0] & 0x3FFF) | (w[1] & 0x3FFF) << 16
    elif r in (0x4, 0xC):
        adc = (w[3] >> 15) & 1
        z = (w[2] >> 4) & 0xFFFFFF
        f = (w[3] >> 4) & 0xFF
        gs.vertex(w[0] & 0xFFFF, w[1] & 0xFFFF, z, f, kick=(r == 0x4 and not adc))
    elif r in (0x5, 0xD):
        adc = (w[3] >> 15) & 1
        gs.vertex(w[0] & 0xFFFF, w[1] & 0xFFFF, w[2], (gs.fog >> 56) & 0xFF, kick=(r == 0x5 and not adc))
    elif r in (0x6, 0x7, 0x8, 0x9):
        gs.write(r, q64[0])
    elif r == 0xA:
        gs.fog = ((w[3] >> 4) & 0xFF) << 56
    elif r == 0xE:
        gs.write(w[2] & 0xFF, q64[0])


def describe(draw, origin):
    kind, mode, tex0, tex1, clamp, alpha, test, frame, zbuf, xyoffset = draw["key"][:10]
    ox, oy = (xyoffset & 0xFFFF) / 16.0, ((xyoffset >> 32) & 0xFFFF) / 16.0
    xs = [v[0] - ox for p in draw["prims"] for v in p]
    ys = [v[1] - oy for p in draw["prims"] for v in p]
    tme = (mode >> 4) & 1
    return {"prim": PRIM_NAMES[kind], "tme": tme, "abe": (mode >> 6) & 1, "fst": (mode >> 8) & 1, "aa1": (mode >> 7) & 1,
            "tbp": tex0 & 0x3FFF if tme else None, "psm": (tex0 >> 20) & 0x3F if tme else None,
            "cbp": (tex0 >> 37) & 0x3FFF if tme else None, "fbp": frame & 0x1FF, "zbp": zbuf & 0x1FF,
            "box": (min(xs), min(ys), max(xs), max(ys)), "count": len(draw["prims"]),
            "tex1": tex1, "alpha": alpha, "test": test, "zmsk": (zbuf >> 32) & 1}


def summary(path, quiet=False):
    header, state, regs, packets = read(path)
    gs = decode(packets, state, 0)
    vsyncs = sum(1 for p in packets if p[0] == 1)
    gs.line = (f"{Path(path).name}: {vsyncs} vertical blank(s), the first frame's {len(gs.draws)} draws, "
               f"{sum(len(d['prims']) for d in gs.draws)} primitives, {gs.transfers} image transfers")
    if not quiet:
        print(gs.line)
    return header, state, gs


def in_box(desc, box):
    x0, y0, x1, y1 = desc["box"]
    return x1 >= box[0] and x0 <= box[2] and y1 >= box[1] and y0 <= box[3]


def compare(a, b, box=None, quiet=False):
    """Two dumps' first frames' draws side by side (A the port's, B the retail game's), grouped by how they're drawn (the
    primitive, texturing, the texture's format, the frame buffer, blending, FST, AA1, TEX1's filtering, ALPHA, TEST, the
    depth's mask; the texture's address left out: the texture cache's slots needn't be the same): the groups each has, and
    for those in both, the primitives each draws. Lines, printed unless quiet"""
    _, _, ga = summary(a, True)
    _, _, gb = summary(b, True)
    lines = [ga.line, gb.line]

    def grouped(gs):
        out = {}
        for d in gs.draws:
            desc = describe(d, None)
            if box is not None and not in_box(desc, box):
                continue
            key = (desc["prim"], desc["tme"], desc["psm"], desc["fbp"], desc["abe"], desc["fst"], desc["aa1"],
                   desc["tex1"] & 0xFFFFFFFF, desc["alpha"], desc["test"], desc["zmsk"])
            out.setdefault(key, []).append(desc)
        return out

    def name(k):
        return (f"{k[0]}{' textured psm 0x%x' % k[2] if k[1] else ''} into page {k[3]}{', blended' if k[4] else ''}"
                f"{', AA1' if k[6] else ''}, TEX1 {k[7]:x} ALPHA {k[8]:x} TEST {k[9]:x}{', depth kept' if k[10] else ''}")

    ta, tb = grouped(ga), grouped(gb)
    both = [k for k in ta if k in tb]
    only_a = [k for k in ta if k not in tb]
    only_b = [k for k in tb if k not in ta]
    aa1 = sum(d["count"] for k in ta for d in ta[k] if k[6]), sum(d["count"] for k in tb for d in tb[k] if k[6])
    lines.append(f"ways of drawing: {len(both)} in both, {len(only_a)} only the port's, {len(only_b)} only the game's; "
                 f"antialiased (AA1) primitives: {aa1[0]} and {aa1[1]}")
    for k in both:
        ca = sum(d["count"] for d in ta[k])
        cb = sum(d["count"] for d in tb[k])
        if ca != cb:
            lines.append(f"  {name(k)}: {ca} primitives against the game's {cb}")
    for k in only_a[:12]:
        lines.append(f"  only the port's: {name(k)} ({sum(d['count'] for d in ta[k])} primitives)")
    for k in only_b[:12]:
        lines.append(f"  only the game's: {name(k)} ({sum(d['count'] for d in tb[k])} primitives)")
    if not quiet:
        print("\n".join(lines))
    return lines


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "compare":
        box = None
        if "--box" in sys.argv:
            box = tuple(float(v) for v in sys.argv[sys.argv.index("--box") + 1].split(","))
        compare(sys.argv[2], sys.argv[3], box)
    else:
        for f in sys.argv[1:]:
            summary(f)
