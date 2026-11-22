#!/usr/bin/env python3
"""The retail data the native build needs, made C++: build/native/data.cpp.

The PS2 build links the game's data (.data, .rodata, .sdata, .bss, .sbss) from the split (asm/data/*.s): words of the retail
executable, its pointers as symbols. The native build's pointers are 64 bit, so each object is laid out again by its C++ type
(build/native/data_types.json, native/tools/data_types.py): clang's record layouts for the PS2 (mips64el n32, the decomp's own
layout checks hold there) and for the host give every field's place on both, numbers and text are copied to their native place,
and every pointer becomes a fixup applied at start-up (NativeApplyDataFixups) before the game runs. The bytes themselves aren't
in the build: data.cpp has where each run of them comes from in the retail executable, and the program reads them from the
player's disc image at start-up (NativeLoadRetailData, before the fixups). The converter checks that the executable's bytes laid
out that way give exactly the objects it made. Another release's executable (the North American v2.00's SLUS_209.09) is laid out
at the PAL one's file offsets through its map (native/discs, read_releases below) and read the same way (NativeLoadRetailDataAs). An object the C++ doesn't
name but other data points at gets its type from the pointer's type; one nothing reaches is left out. Nothing is guessed: an
object whose layout can't be worked out exactly is reported, and the build stops on it unless it's listed in KNOWN_RAW below.

    native/tools/convert_data.py [--report]
"""
import argparse, collections, functools, json, os, re, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import host  # noqa: E402
from pathlib import Path

HERE = Path(__file__).resolve().parents[2]

# Objects whose raw byte members hold addresses the game never reads: those bytes are left zero natively, with the reason
KNOWN_RAW = {
    "D_002EA0E0": "newlib's struct _reent (_impure_data): RetailLibc::Reent's unused04 bytes are stdio's FILE pointers "
                  "(_stdin, _stdout, _stderr) and unused5C is the rest of the block; the game only reads errno, rand's state "
                  "and atexit's list",
}
BUILD = HERE / "build" / "native"

# ---------------------------------------------------------------------------------------------------------------- the split
def read_split():
    """{label: {"section", "data": bytearray, "relocs": {offset: (symbol, addend)}, "align"}} from asm/data"""
    objects, order = {}, []
    for path in sorted((HERE / "asm" / "data").glob("*.s")):
        section = path.stem.split(".")[0]
        current = None
        previous = None
        pad = False
        for raw in path.read_text().splitlines():
            line = raw.strip()
            if "Automatically generated and unreferenced pad" in line:
                pad = True
                continue
            m = re.match(r"(?:dlabel|glabel|jlabel)\s+(\S+)", line)
            if m:
                if pad and previous is not None:
                    previous["pads"] = previous.get("pads", 0) + 1
                    # A label splat made where nothing points: the bytes are the previous object's (its array goes on: the
                    # static constructors' count, then its functions), and stay right after it as on the PS2
                    current = previous
                else:
                    current = {"section": section, "data": bytearray(), "relocs": {}, "address": None,
                               "label": m.group(1)}
                    objects[m.group(1)] = current
                    order.append(m.group(1))
                previous, pad = current, False
                continue
            if line.startswith(("enddlabel", "endlabel")):
                current = None
                continue
            if current is None:
                continue
            if current["address"] is None:
                # The comment in front of the first data: "/* rom vram word */" (data) or "/* vram */" (bss)
                found = re.match(r"/\*\s*([0-9A-Fa-f]+)(?:\s+([0-9A-Fa-f]{8}))?", line)
                if found:
                    current["address"] = int(found.group(2) or found.group(1), 16) - len(current["data"])
            m = re.match(r'\.incbin\s+"([^"]+)"(?:\s*,\s*(\w+)\s*,\s*(\w+))?', line)
            if m:
                # A binary the split keeps as a file (the VU microcode): its bytes, from the start and length given
                blob = (HERE / m.group(1)).read_bytes()
                start = int(m.group(2), 0) if m.group(2) else 0
                length = int(m.group(3), 0) if m.group(3) else len(blob) - start
                current["data"] += blob[start:start + length]
                continue
            m = re.search(r"\.(word|short|half|byte|space|skip|zero|float|asciz|ascii|balign|align)\s*(.*)$", line)
            if not m:
                continue
            kind, arg = m.group(1), m.group(2).split("/*")[0].strip()
            data = current["data"]
            if kind == "word":
                for value in arg.split(","):
                    value = value.strip()
                    if re.match(r"[A-Za-z_.$]", value):
                        sym = re.match(r"([A-Za-z_.$][\w.$]*)\s*(?:([+-])\s*(0x[0-9A-Fa-f]+|\d+))?$", value)
                        addend = int(sym.group(3), 0) * (-1 if sym.group(2) == "-" else 1) if sym.group(2) else 0
                        current["relocs"][len(data)] = (sym.group(1), addend)
                        data += b"\0\0\0\0"
                    else:
                        data += struct.pack("<I", int(value, 0) & 0xFFFFFFFF)
            elif kind in ("short", "half"):
                for value in arg.split(","):
                    data += struct.pack("<H", int(value.strip(), 0) & 0xFFFF)
            elif kind == "byte":
                for value in arg.split(","):
                    data += struct.pack("<B", int(value.strip(), 0) & 0xFF)
            elif kind in ("space", "skip", "zero"):
                data += b"\0" * int(arg.split(",")[0], 0)
            elif kind == "float":
                for value in arg.split(","):
                    data += struct.pack("<f", float(value.strip()))
            elif kind in ("asciz", "ascii"):
                text = bytes(arg.strip()[1:-1], "utf-8").decode("unicode_escape").encode("latin-1")
                data += text + (b"\0" if kind == "asciz" else b"")
            elif kind in ("balign", "align"):
                pass
    return objects, order

# ---------------------------------------------------------------------------------------------------------------- layouts
@functools.lru_cache(None)
def toolchain_includes():
    gxx = host.ee_gxx()
    r = subprocess.run([gxx, "-E", "-v", "-x", "c++", os.devnull], capture_output=True, text=True)
    lines = r.stderr.splitlines()
    s, e = lines.index("#include <...> search starts here:") + 1, lines.index("End of search list.")
    dirs = [os.path.normpath(l.strip()).replace("\\", "/") for l in lines[s:e]]
    return tuple(a for d in dirs if not d.endswith(("15.2.0/include", "include-fixed")) for a in ("-isystem", d))

def probe_source():
    """Every header, and the .cpp files' own types too: the probe includes each .cpp's types by compiling the .cpp files"""
    headers = sorted(p.relative_to(HERE / "include").as_posix() for p in (HERE / "include").rglob("*.h"))
    return "".join(f'#include "{h}"\n' for h in headers)

def dump_layouts(target):
    """{record name: {"size", "align", "fields": [(offset, bit, depth, type, name)]}} for "native" or "ps2" """
    BUILD.mkdir(parents=True, exist_ok=True)
    cache = BUILD / f"layouts_{target}.json"
    probe = BUILD / "probe.cpp"
    reused = [HERE / l.strip() for l in (HERE / "native" / "reused_ps2.txt").read_text().splitlines()
              if l.strip() and not l.startswith("#")]
    sources = [probe] + sorted(p for p in (HERE / "src").rglob("*.cpp")
                               if (p.relative_to(HERE / "src").parts[0] != "platform"
                                   or p.relative_to(HERE / "src").parts[1] == "native") and p.name != "abi.cpp") + reused
    probe_text = probe_source()
    if not probe.exists() or probe.read_text() != probe_text:
        probe.write_text(probe_text)
    if target == "native":
        base = [host.CLANG, "-std=gnu++2c", "-fsyntax-only", "-DTWIN_NATIVE", "-Inative/include", "-Iinclude",
                "-Isrc/platform/native"] + host.host_includes() + host.native_flags()
    else:
        base = ([host.CLANG, "--target=mips64el-unknown-elf", "-mabi=n32", "-std=gnu++2c", "-fsyntax-only", "-nostdinc++"]
                + list(toolchain_includes()) + ["-D_EE", "-Iinclude", "-I" + host.ps2sdk() + "/ee/include",
                                          "-I" + host.ps2sdk() + "/common/include"])
    base += ["-fno-exceptions", "-fno-rtti", "-Wno-everything", "-Xclang", "-fdump-record-layouts-complete"]
    layouts = {}
    from concurrent.futures import ThreadPoolExecutor
    def run(source):
        args = base
        if target == "ps2" and "platform/native" in str(source):
            # The native platform's own types laid out as the PS2 had them: built for the PS2's target (its C++ library's
            # headers) with the native build's headers in place of PS2SDK's (uiptr is the target's pointer size, 32 bits)
            args = ([host.CLANG, "--target=mips64el-unknown-elf", "-mabi=n32", "-std=gnu++2c", "-fsyntax-only", "-nostdinc++"]
                    + list(toolchain_includes()) + ["-DTWIN_NATIVE", "-Inative/include", "-Iinclude", "-Isrc/platform/native"]
                    + host.host_includes() + ["-fno-exceptions", "-fno-rtti", "-Wno-everything",
                                              "-Xclang", "-fdump-record-layouts-complete"])
        return subprocess.run(args + [str(source)], capture_output=True, text=True, cwd=HERE).stdout

    # Each file's layouts kept between runs (native/tools/host.py's FileCache): only the files that changed are laid out again
    files = host.FileCache(f"layouts_{target}_cache", " ".join(base) + " " + " ".join(toolchain_includes() if target == "ps2"
                                                                                       else []))

    def run_cached(source):
        found = files.get(source)
        if found is None:
            found = {}
            parse_layouts(run(source), found)
            files.put(source, found)
        return found

    with ThreadPoolExecutor(10) as pool:
        for found in pool.map(run_cached, sources):
            # The first file to lay a record out gives it, as the dumps were read in this order
            for name, layout in found.items():
                if name not in layouts:
                    layouts[name] = layout
    files.save()
    cache.write_text(json.dumps(layouts))
    return layouts

FIELD = re.compile(r"^\s*(\d+)(?::(\d+)-(\d+))?\s\|(\s+)(.*)$")

def parse_layouts(text, layouts):
    for block in text.split("*** Dumping AST Record Layout")[1:]:
        lines = [l for l in block.splitlines() if l.strip()]
        head = FIELD.match(lines[0])
        if not head:
            continue
        name = head.group(5).strip()
        fields = []
        size = align = None
        for line in lines[1:]:
            m = re.search(r"\[sizeof=(\d+),.*align=(\d+)", line)
            if m:
                size, align = int(m.group(1)), int(m.group(2))
                continue
            m = FIELD.match(line)
            if not m:
                continue
            depth = (len(m.group(4)) - 1) // 2
            body = m.group(5).rstrip()
            bits = (int(m.group(2)), int(m.group(3))) if m.group(2) else None
            fields.append((int(m.group(1)), bits, depth, body))
        if size is not None and name not in layouts:
            layouts[name] = {"size": size, "align": align, "fields": fields}

# ---------------------------------------------------------------------------------------------------------------- types
SCALARS = {  # name: (ps2 size, native size, kind)
    **{n: (1, 1, "int") for n in ("u8", "s8", "char", "signed char", "unsigned char", "bool", "uint8_t", "int8_t", "vu8")},
    **{n: (2, 2, "int") for n in ("u16", "s16", "short", "unsigned short", "uint16_t", "int16_t", "char16_t", "vu16")},
    **{n: (4, 4, "int") for n in ("u32", "s32", "int", "unsigned int", "uint32_t", "int32_t", "vu32", "vs32", "char32_t",
                                  "wchar_t")},
    **{n: (4, 4, "float") for n in ("f32", "float")},
    **{n: (8, 8, "int") for n in ("u64", "s64", "long long", "unsigned long long", "uint64_t", "int64_t")},
    **{n: (8, 8, "float") for n in ("double",)},
    **{n: (16, 16, "int") for n in ("u128", "s128", "__int128", "unsigned __int128")},
    # Pointer-sized integers: 32 bit on the PS2, 64 natively (an address in them is a fixup)
    **{n: (4, 8, "uptr") for n in ("uiptr", "uintptr_t", "std::uintptr_t", "size_t", "std::size_t", "unsigned long")},
    **{n: (4, 8, "sptr") for n in ("siptr", "intptr_t", "std::intptr_t", "ptrdiff_t", "long")},
}
if host.WINDOWS:
    # Windows' long is 32 bits (LLP64): pointer-sized integers are long long there, and long is n32's own size
    SCALARS.update({n: (4, 4, "int") for n in ("long", "unsigned long", "long int", "unsigned long int")})

def enum_bases():
    """Enums with an underlying type of their own (enum X : u8): their size"""
    sizes = {}
    for path in list((HERE / "include").rglob("*.h")) + list((HERE / "src").rglob("*.cpp")):
        for m in re.finditer(r"\benum\s+(?:class\s+|struct\s+)?(\w+)\s*:\s*([\w:]+)", path.read_text(errors="ignore")):
            base = SCALARS.get(m.group(2))
            if base:
                sizes[m.group(1)] = base[0]
    return sizes

def aliases():
    found = {}
    for path in list((HERE / "include").rglob("*.h")) + list((HERE / "src").rglob("*.cpp")):
        text = path.read_text(errors="ignore")
        for m in re.finditer(r"\busing\s+(\w+)\s*=\s*([^;{]+);", text):
            found[m.group(1)] = m.group(2).strip()
        for m in re.finditer(r"\btypedef\s+([^;{]*?)\(\s*\*\s*(\w+)\s*\)\s*\(([^;]*)\)\s*;", text):
            found[m.group(2)] = f"{m.group(1).strip()} (*)({m.group(3)})"
        for m in re.finditer(r"\btypedef\s+([\w\s:*<>]+?)\s+(\w+)\s*;", text):
            found.setdefault(m.group(2), m.group(1).strip())
    return found

class Types:
    def __init__(self, ps2, native):
        self.ps2, self.native, self.enums, self.aliases = ps2, native, enum_bases(), aliases()

    @staticmethod
    def clean(t):
        t = re.sub(r"\b(const|volatile|struct|class|union|enum|typename)\b", "", t)
        return re.sub(r"\s+", " ", t).strip()

    def record(self, t):
        """The layouts of a record type by the name the dumps give it, on both targets"""
        # A record's own dump calls an anonymous member's type "unnamed", its parent's line "anonymous"
        t = t.replace("(anonymous at", "(unnamed at")
        for key in (t, "struct " + t, "class " + t, "union " + t):
            if key in self.ps2 and key in self.native:
                return self.ps2[key], self.native[key]
        # Namespaced names print without their namespace in field types (or the other way)
        tail = t.split("::")[-1]
        hits = [k for k in self.ps2 if k.split(" ", 1)[-1].split("::")[-1] == tail and k in self.native]
        if len(hits) == 1:
            return self.ps2[hits[0]], self.native[hits[0]]
        return None

    def parse(self, t):
        """("array", elem, count) | ("pointer",) | ("scalar", ps2size, nativesize, kind) | ("record", ps2, native) | None"""
        t = t.strip()
        m = re.match(r"^(.*)\(\s*\*\s*(?:const\s*)?\[(\d*)\]\s*\)\s*(\(.*\))$", t)
        if m:
            return ("array", f"{m.group(1).strip()} (*){m.group(3)}", int(m.group(2)) if m.group(2) else None)
        m = re.match(r"^(.*?)\s*\[(\d*)\]$", t)
        if m:
            return ("array", m.group(1), int(m.group(2)) if m.group(2) else None)
        if "(*)" in t or "(&)" in t or re.search(r"\*\s*(const|volatile)?\s*$", t) or t.endswith("&"):
            return ("pointer",)
        c = self.clean(t)
        if c in SCALARS:
            return ("scalar",) + SCALARS[c]
        if c in self.aliases and self.aliases[c] != c and not self.record(c):
            return self.parse(self.aliases[c])
        if c in self.enums:
            return ("scalar", self.enums[c], self.enums[c], "int")
        r = self.record(c)
        if r:
            return ("record",) + r
        # A plain enum (int sized) the dumps don't list
        if re.search(r"\benum\b", t):
            return ("scalar", 4, 4, "int")
        return None

    def native_offset(self, t, ps2_offset):
        """Where a PS2 offset inside an object of type t is natively (a member's start), None when it's inside a member"""
        if ps2_offset == 0:
            return 0
        p = self.parse(t)
        if p is None or p[0] in ("pointer", "scalar"):
            return None
        if p[0] == "array":
            es, en, _, _ = self.sizes(p[1])
            index, rest = divmod(ps2_offset, es)
            inner = self.native_offset(p[1], rest)
            return None if inner is None else index * en + inner
        ps2, native = p[1], p[2]
        if not self.has_pointers(t):
            return ps2_offset
        pf, nf = ps2["fields"], native["fields"]
        best = None
        for (po, pbits, pd, pbody), (no, nbits, nd, nbody) in zip(pf, nf):
            if pd == 1 and not pbits and po <= ps2_offset:
                best = (po, no, split_field(pbody)[0])
        if best is None:
            return None
        inner = self.native_offset(best[2], ps2_offset - best[0])
        return None if inner is None else best[1] + inner

    def register_nested(self, ftype, pf, nf, pstart, nstart):
        """A member's record type whose layout only shows nested in its parent's dump (a template's instance): taken from
        the lines below the member, on both targets"""
        def sub(fields, start):
            offset, depth = start
            i = next(k for k, f in enumerate(fields) if f[0] == offset and f[2] == depth and split_field(f[3])[0] == ftype)
            lines = []
            for f in fields[i + 1:]:
                if f[2] <= depth:
                    break
                lines.append((f[0] - offset, f[1], f[2] - depth, f[3]))
            end = max((f[0] + 8 for f in lines), default=0)
            return {"size": end, "align": 4, "fields": lines, "union": False}
        try:
            ps2, native = sub(pf, pstart), sub(nf, nstart)
        except StopIteration:
            return
        if ps2["fields"]:
            name = self.clean(ftype)
            self.ps2[name], self.native[name] = ps2, native

    def sizes(self, t):
        p = self.parse(t)
        if p is None:
            raise KeyError(t)
        if p[0] == "pointer":
            return 4, 8, 4, 8
        if p[0] == "scalar":
            return p[1], p[2], min(p[1], 8) if p[1] < 16 else 16, min(p[2], 16)
        if p[0] == "record":
            return p[1]["size"], p[2]["size"], p[1]["align"], p[2]["align"]
        es, en, ea, na = self.sizes(p[1])
        return es * (p[2] or 0), en * (p[2] or 0), ea, na

    def has_pointers(self, t, seen=()):
        p = self.parse(t)
        if p is None:
            raise KeyError(t)
        if p[0] == "pointer":
            return True
        if p[0] == "scalar":
            return p[3] in ("uptr", "sptr")
        if p[0] == "array":
            return self.has_pointers(p[1], seen)
        return p[1]["size"] != p[2]["size"] or any(
            self.has_pointers(split_field(f[3])[0], seen) for f in direct_fields(p[1]) if split_field(f[3])[0] not in seen)

def split_field(body):
    """("type", "name") of a dump's member line; an anonymous struct/union member is all type"""
    body = body.strip()
    if body.endswith(")") and "(anonymous" in body or "(unnamed" in body and body.endswith(")"):
        return body, None
    if " " not in body:
        return body, None
    return body.rsplit(" ", 1)

def direct_fields(layout):
    """The record's own members (depth 1), as (offset, bits, depth, "type name"); bases are flattened in by the dump"""
    return [f for f in layout["fields"] if f[2] == 1]

# ---------------------------------------------------------------------------------------------------------------- converting
class Converter:
    def __init__(self, types, objects):
        self.types, self.objects = types, objects
        self.problems = []
        self.last_type = None
        # What each symbol data points at is, by the pointer's type ("const char" for text): {symbol: {pointee types}}
        self.pointees = collections.defaultdict(set)
        # Where each object's bytes come from (place's copies): ("copy", native offset, PS2 offset, bytes) and ("widen", native
        # offset, PS2 offset, signed) for a 32 bit address-sized integer made 64 bits
        self.ops = []

    def convert(self, label, t):
        """(native bytes, align, fixups [(offset, symbol, addend)]) of the object as type t"""
        obj = self.objects[label]
        data, relocs = obj["data"], obj["relocs"]
        p = self.types.parse(t)
        if self.types.parse(t) is None and not relocs and not any(data):
            # Zero storage (.bss) of a type without a layout dump (a template only a .cpp instantiates, a platform type):
            # room for it whatever its pointers make of it, at most twice the PS2's size
            size = len(data) * 2 + 16
            return bytearray(size), 16, []
        if p and p[0] == "array" and p[2] is None:
            es, en, ea, na = self.types.sizes(p[1])
            count = len(data) // es if es else 0
            element = self.types.clean(p[1])
            if element == "char":
                count = data.index(0) + 1 if 0 in data else len(data)
            elif self.types.has_pointers(p[1]):
                count = self.whole_elements(p[1], es, data, relocs, count)
            t = f"{p[1]} [{count}]"
        self.last_type = t
        ps2_size, native_size, _, native_align = self.types.sizes(t)
        out = bytearray(native_size)
        fixups = []
        self.used_relocs = set()
        self.ops = []
        self.place(label, t, data, relocs, 0, out, 0, fixups)
        for off in relocs:
            if off not in self.used_relocs and off < ps2_size:
                self.problems.append(f"{label}: the pointer at +{off:#x} ({relocs[off][0]}) isn't a pointer field of {t}")
        return out, max(native_align, 16 if obj["section"] in ("vutext", "vudata") else 4), fixups

    def copied(self, dst, src, n, data):
        """A run of the retail bytes placed (as many as the object has)"""
        n = min(n, len(data) - src)
        if n > 0:
            self.ops.append(("copy", dst, src, n))

    def whole_elements(self, element, size, data, relocs, count):
        """How many elements of an unsized array are its own: up to the first holding a number where a pointer goes (another,
        unlabelled object follows, which the C++ can't name)"""
        for i in range(count):
            probe = Converter(self.types, self.objects)
            probe.used_relocs = set()
            probe.place("probe", element, data, relocs, i * size, bytearray(self.types.sizes(element)[1]), 0, [])
            if any("without a symbol" in problem for problem in probe.problems):
                return i
        return count

    def place(self, label, t, data, relocs, src, out, dst, fixups):
        p = self.types.parse(t)
        if p is None:
            self.problems.append(f"{label}: no layout for type {t!r}")
            return
        kind = p[0]
        if kind == "pointer":
            if src in relocs:
                self.used_relocs.add(src)
                fixups.append((dst, *relocs[src]))
                pointee = re.sub(r"\*\s*(const|volatile)?\s*$", "", t).strip()
                if "(" not in t:
                    self.pointees[relocs[src][0]].add(pointee)
            else:
                value = struct.unpack_from("<I", data, src)[0] if src + 4 <= len(data) else 0
                if value != 0:
                    self.problems.append(f"{label}+{src:#x}: a pointer of {value:#x} without a symbol")
            return
        if kind == "scalar":
            ps2, native, k = p[1], p[2], p[3]
            if k in ("uptr", "sptr"):
                if src in relocs:
                    self.used_relocs.add(src)
                    fixups.append((dst, *relocs[src]))
                    return
                value = struct.unpack_from("<I" if k == "uptr" else "<i", data, src)[0] if src + 4 <= len(data) else 0
                out[dst:dst + 8] = struct.pack("<Q" if k == "uptr" else "<q", value)
                if src + 4 <= len(data):
                    self.ops.append(("widen", dst, src, k == "sptr"))
                return
            if src in relocs:
                # An address kept in an integer the C++ doesn't widen: it must be widened (uiptr)
                self.used_relocs.add(src)
                self.problems.append(f"{label}+{src:#x}: the address of {relocs[src][0]} in a {t}")
            out[dst:dst + native] = data[src:src + ps2].ljust(native, b"\0")
            self.copied(dst, src, ps2, data)
            return
        if kind == "array":
            es, en, _, _ = self.types.sizes(p[1])
            if not self.types.has_pointers(p[1]):
                n = es * p[2]
                for off in range(src, src + n, 4):
                    if off in relocs:
                        self.used_relocs.add(off)
                        if label not in KNOWN_RAW:
                            self.problems.append(f"{label}+{off:#x}: the address of {relocs[off][0]} in {t}")
                out[dst:dst + n] = data[src:src + n].ljust(n, b"\0")
                self.copied(dst, src, n, data)
                return
            for i in range(p[2]):
                self.place(label, p[1], data, relocs, src + i * es, out, dst + i * en, fixups)
            return
        ps2, native = p[1], p[2]
        if not self.types.has_pointers(t):
            n = ps2["size"]
            for off in range(src, src + n, 4):
                if off in relocs:
                    self.used_relocs.add(off)
                    self.problems.append(f"{label}+{off:#x}: the address of {relocs[off][0]} in {t}")
            out[dst:dst + n] = data[src:src + n].ljust(n, b"\0")
            self.copied(dst, src, n, data)
            return
        # Field by field: the two dumps list the same members in the same order
        pf, nf = ps2["fields"], native["fields"]
        if len(pf) != len(nf):
            self.problems.append(f"{label}: {t} has {len(pf)} fields on the PS2 and {len(nf)} natively")
            return
        covered = set()
        for (po, pbits, pd, pbody), (no, nbits, nd, nbody) in zip(pf, nf):
            if pd != 1:
                continue
            ftype = split_field(pbody)[0]
            if self.types.parse(ftype) is None:
                self.types.register_nested(ftype, pf, nf, (po, pd), (no, nd))
            if pbits:
                # Bitfields: their storage unit copied whole (pointer-free by construction)
                unit = po
                if unit not in covered:
                    covered.add(unit)
                    out[dst + no:dst + no + 4] = data[src + unit:src + unit + 4]
                    self.copied(dst + no, src + unit, 4, data)
                continue
            # Unions: the first member placed decides (the dump lists them all at one offset)
            if po in covered and native.get("union"):
                continue
            covered.add(po)
            self.place(label, ftype, data, relocs, src + po, out, dst + no, fixups)

def pointee_type(conv, symbol, objects, problems):
    """The type of an object only reached through pointers: what they point to (as an array when there's room for more than
    one), or its bytes when it holds no pointers and nothing says more (void*, a function's)"""
    kinds = {re.sub(r"\b(const|volatile)\b", "", k).strip() for k in conv.pointees.get(symbol, set())} - {"void", ""}
    size = len(objects[symbol]["data"])
    if len(kinds) == 1:
        kind = kinds.pop()
        try:
            es = conv.types.sizes(kind)[0]
        except KeyError:
            problems.append(f"{symbol}: pointed to as {kind!r}, which has no layout")
            return None
        return kind if es >= size or es == 0 else f"{kind} [{size // es}]"
    if objects[symbol]["relocs"]:
        problems.append(f"{symbol}: holds pointers, and what points at it says nothing of its type ({sorted(kinds) or 'void'})")
        return None
    return f"u8 [{size}]"

# ---------------------------------------------------------------------------------------------------------------- output
def cxx_escape(label):
    return re.sub(r"\W", "_", label)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", action="store_true", help="print every object's type and the problems, write nothing")
    args = parser.parse_args()

    objects, order = read_split()
    types_info = json.loads((BUILD / "data_types.json").read_text())
    ps2, native = dump_layouts("ps2"), dump_layouts("native")
    for layouts in (ps2, native):
        for name, layout in layouts.items():
            layout["union"] = name.startswith("union ")
    types = Types(ps2, native)
    conv = Converter(types, objects)

    # What the native build needs: every retail variable the C++ declares and doesn't define, then whatever their pointers reach
    wanted = collections.OrderedDict()
    for label, info in types_info.items():
        if info["kind"] == "var" and not info.get("defined") and label in objects:
            # The type as written when it's a scalar the table knows (uiptr): its canonical type is the host's (Windows'
            # uintptr_t is unsigned long long, 8 bytes on the PS2 too by that name), not the PS2's
            written = Types.clean(info["type"])
            wanted[label] = written if written in SCALARS else info["canonical"]
    missing_from_split = sorted(l for l, i in types_info.items()
                                if i["kind"] == "var" and not i.get("defined") and l not in objects)

    results, problems, warnings = {}, [], []
    recipes = {}
    aliases = {}  # label -> (container label, PS2 offset)
    by_section = collections.defaultdict(list)
    for label in order:
        if objects[label]["address"] is not None:
            by_section[objects[label]["section"]].append(label)
    position = {label: (objects[label]["section"], i) for section, labels in by_section.items() for i, label in enumerate(labels)}
    for label in sorted(wanted, key=lambda l: objects[l]["address"] if objects[l]["address"] is not None else -1):
        obj = objects[label]
        if label in aliases or obj["address"] is None:
            continue
        try:
            extent = types.sizes(wanted[label])[0] if types.parse(wanted[label]) and types.parse(wanted[label])[0] != "array" \
                or (types.parse(wanted[label]) and types.parse(wanted[label])[2] is not None) else 0
        except KeyError:
            continue
        if extent <= len(obj["data"]):
            continue
        section, index = position[label]
        labels = by_section[section]
        for following in labels[index + 1:]:
            other = objects[following]
            offset = other["address"] - obj["address"]
            if offset >= extent:
                break
            # The bytes between, then the following object's own, its pointers moved along
            obj["data"] += bytes(offset - len(obj["data"])) if offset > len(obj["data"]) else b""
            for at, target in other["relocs"].items():
                obj["relocs"][offset + at] = target
            obj["data"][offset:offset + len(other["data"])] = other["data"]
            aliases[following] = (label, offset)
    queue = [label for label in wanted if label not in aliases]
    inferred = {}
    while queue:
        label = queue.pop(0)
        if label in results:
            continue
        t = wanted.get(label) or inferred.get(label)
        try:
            out, align, fixups = conv.convert(label, t)
        except KeyError as error:
            problems.append(f"{label}: no layout for {error}")
            continue
        results[label] = (t, out, align, fixups)
        recipes[label] = conv.ops
        obj = objects[label]
        ps2_covered = conv.types.sizes(conv.last_type)[0] if conv.last_type else len(obj["data"])
        if obj.get("pads") and ps2_covered < len(obj["data"]) and (obj["relocs"] or any(obj["data"][ps2_covered:])):
            warnings.append(f"{label}: {len(obj['data']) - ps2_covered} bytes past its type {conv.last_type} (a splat pad) "
                            f"aren't brought over")
        for _, symbol, _ in fixups:
            if symbol in objects and symbol not in results and symbol not in wanted and symbol not in inferred \
                    and symbol not in aliases:
                # Data only other data points at: its type is the pointer's
                inferred[symbol] = pointee_type(conv, symbol, objects, problems)
                if inferred[symbol] is not None:
                    queue.append(symbol)
    alias_offsets = {}
    # Only what's named needs a symbol: by the C++, or by a pointer in the data converted
    named = set(types_info) | {symbol for (_, _, _, fx) in results.values() for _, symbol, _ in fx}
    for label, (container, offset) in aliases.items():
        if container not in results or label not in named:
            continue
        native = types.native_offset(results[container][0], offset)
        if native is None:
            problems.append(f"{label}: inside {container} at +{offset:#x}, which isn't a member's start natively")
        else:
            alias_offsets[label] = (container, native)
    problems += conv.problems

    if args.report:
        for label, (t, out, align, fixups) in results.items():
            print(f"{label:40} {t!s:40} {len(objects[label]['data']):6} -> {len(out):6} fixups {len(fixups)}")
        print(f"\n{len(results)} objects, {sum(len(r[3]) for r in results.values())} fixups, "
              f"{len(missing_from_split)} declared but not in the split, {len(problems)} problems")
        for p in problems:
            print("PROBLEM", p)
        return

    for w in warnings:
        print("WARNING", w, file=sys.stderr)
    marks = mark_narrow_returns(results, types_info, types, problems)
    write_cpp(results, objects, alias_offsets, recipes, marks, problems)
    if problems:
        for p in problems:
            print("PROBLEM", p, file=sys.stderr)
        sys.exit(f"{len(problems)} problems (see above)")

# gcc2.h's GccNarrowReturn: what a function returning a narrow integer is marked with in its vtable entries' index
NARROW_RETURNS = {
    **{n: 0x1000 for n in ("bool", "u8", "unsigned char", "uint8_t", "char8_t")},
    **{n: 0x2000 for n in ("s8", "signed char", "int8_t", "char")},
    **{n: 0x3000 for n in ("u16", "unsigned short", "uint16_t", "char16_t")},
    **{n: 0x4000 for n in ("s16", "short", "int16_t")},
}
# A GccVTableEntry natively: delta (s16), index (s16), then the function at 8; 16 bytes
VTABLE_ENTRY_BYTES, VTABLE_INDEX_OFFSET, VTABLE_FUNCTION_OFFSET = 16, 2, 8

def narrow_return(info, types):
    """The GccNarrowReturn mark of a function's return type (0: as wide as a register's 32 bits, or not an integer)"""
    m = re.match(r"^(.*?)\s*\(", info["type"])
    if not m:
        return 0
    returned = Types.clean(m.group(1))
    for _ in range(8):
        if returned in NARROW_RETURNS:
            return NARROW_RETURNS[returned]
        if returned in types.enums:
            # An enum with a narrow base, taken as unsigned
            return {1: 0x1000, 2: 0x3000}.get(types.enums[returned], 0)
        alias = types.aliases.get(returned)
        if alias is None or alias == returned:
            return 0
        returned = Types.clean(alias)
    return 0

def mark_narrow_returns(results, types_info, types, problems):
    """Marks the retail vtables' entries whose C++ function returns a narrow integer (gcc2.h's CallNarrowed): x86-64 leaves
    the register's bits above it as they were, where the PS2 and Apple's arm64 extend it, and the calls through the vtables
    read 32 bits (CallVirtual<u32> of a bool function)"""
    marks = []
    for label, (t, out, align, fixups) in results.items():
        for offset, symbol, addend in fixups:
            info = types_info.get(symbol)
            if info is None or info["kind"] != "function" or addend != 0:
                continue
            mark = narrow_return(info, types)
            if mark == 0:
                continue
            if "GccVTableEntry" not in str(t) or offset % VTABLE_ENTRY_BYTES != VTABLE_FUNCTION_OFFSET:
                problems.append(f"{label} +{offset:#x}: {symbol} returns a narrow integer from outside a vtable entry")
                continue
            at = offset - VTABLE_FUNCTION_OFFSET + VTABLE_INDEX_OFFSET
            index = struct.unpack_from("<H", out, at)[0]
            struct.pack_into("<H", out, at, index | mark)
            marks.append((label, at, mark))
    print(f"{len(marks)} vtable entries of functions returning narrow integers marked", file=sys.stderr)
    return marks

# ------------------------------------------------------------------------------------------------- the bytes from the disc
EXECUTABLE = HERE / "SLES_525.68"
FNV_BASIS, FNV_PRIME = 0xCBF29CE484222325, 0x100000001B3

def fnv1a(data, h=FNV_BASIS):
    """FNV-1a, 64 bits (data.cpp's Fnv1a the same): the executable's and the objects' check"""
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h

def read_executable():
    """The retail executable's bytes and its loaded segments: [(address, file offset, bytes in the file, bytes in memory)]"""
    elf = EXECUTABLE.read_bytes()
    phoff = struct.unpack_from("<I", elf, 0x1C)[0]
    phentsize, phnum = struct.unpack_from("<HH", elf, 0x2A)
    segments = []
    for i in range(phnum):
        kind, offset, address, _, file_size, memory_size = struct.unpack_from("<IIIIII", elf, phoff + i * phentsize)
        # PT_LOAD: past the file's bytes, zero storage (.bss)
        if kind == 1:
            segments.append((address, offset, file_size, memory_size))
    return elf, segments

def file_offset(segments, address, size):
    """Where size bytes at address are in the executable's file: None in zero storage (.bss)"""
    for start, offset, file_size, memory_size in segments:
        if start <= address and address + size <= start + file_size:
            return offset + address - start
        if start + file_size <= address and address + size <= start + memory_size:
            return None
    raise LookupError(f"{address:#x} (+{size:#x}) isn't inside the executable's loaded segments")

def make_recipe(results, objects, recipes, marks, problems):
    """The runs of the executable's bytes that make the objects: copies [(label, native offset, file offset, bytes)], widened
    integers [(label, native offset, file offset, signed)]; checked by laying the executable's bytes out that way"""
    elf, segments = read_executable()
    copies, widens = [], []
    for label, (t, out, align, fixups) in results.items():
        obj = objects[label]
        base, data = obj["address"], obj["data"]
        if base is None:
            # The split's binaries (the VU microcode's .incbin) have no address comment: splat's name has it
            named = re.fullmatch(r"D_([0-9A-Fa-f]{8})", label)
            if named is None:
                problems.append(f"{label}: no address in the split")
                continue
            base = int(named.group(1), 16)
        # The addresses' words are 0 in the split's bytes (they're fixups) and the PS2's addresses in the executable's
        address_bytes = {at + i for at in obj["relocs"] for i in range(4)}
        runs = []
        for op in recipes.get(label, []):
            if op[0] == "widen":
                _, dst, src, signed = op
                if any(data[src:src + 4]):
                    widens.append((label, dst, file_offset(segments, base + src, 4), signed))
                continue
            _, dst, src, n = op
            i = 0
            while i < n:
                if src + i in address_bytes:
                    i += 1
                    continue
                j = i
                while j < n and src + j not in address_bytes:
                    j += 1
                # Zeros needn't come from anywhere (the objects start zero), so neither does storage the file has none of
                if any(data[src + i:src + j]):
                    runs.append((dst + i, file_offset(segments, base + src + i, j - i), j - i))
                i = j
        runs.sort()
        for dst, offset, n in runs:
            if copies and copies[-1][0] == label and copies[-1][1] + copies[-1][3] == dst \
                    and copies[-1][2] + copies[-1][3] == offset:
                copies[-1] = (label, copies[-1][1], copies[-1][2], copies[-1][3] + n)
            else:
                copies.append((label, dst, offset, n))

    built = {label: bytearray(len(out)) for label, (t, out, align, fixups) in results.items()}
    for label, dst, offset, n in copies:
        built[label][dst:dst + n] = elf[offset:offset + n]
    for label, dst, offset, signed in widens:
        value = struct.unpack_from("<i" if signed else "<I", elf, offset)[0]
        built[label][dst:dst + 8] = struct.pack("<q" if signed else "<Q", value)
    for label, at, mark in marks:
        index = struct.unpack_from("<H", built[label], at)[0]
        struct.pack_into("<H", built[label], at, index | mark)
    for label, (t, out, align, fixups) in results.items():
        if built[label] != out:
            at = next(i for i in range(len(out)) if built[label][i] != out[i])
            problems.append(f"{label}: the executable's bytes laid out give {built[label][at]:#04x} at +{at:#x}, the split's "
                            f"{out[at]:#04x}")
    data_hash = FNV_BASIS
    for label, (t, out, align, fixups) in results.items():
        data_hash = fnv1a(out, data_hash)
    return copies, widens, (len(elf), fnv1a(elf), data_hash), segments

# ------------------------------------------------------------------------------------------ the other releases (THE LIST 31)
# Another release's program runs as the PAL one does, on its own data: its executable's bytes laid out at the PAL executable's
# file offsets through its map's segments (native/discs/region_map_<serial>.json, native/tools/region_map.py: addresses, sizes
# and hashes only), the laid out image checked by its hash, then the same runs copied from it. The map's differences in the
# bytes the program reads are decided here one by one, by the native object they land in: the release's own bytes taken (None,
# and why), or the PAL's value kept (a number written here, checked against the PAL executable when the data is converted: the
# build still has none of the game's bytes). A map whose read differences reach an object not decided here stops the build.
DISCS = HERE / "native" / "discs"
DECISIONS = {
    "SLUS-20909": {
        "D_002E70F8": (None, "g_Movies: the movies' bits as its disc's files are (TTIdent 512x448, the story's movies one sound "
                             "channel, Complete 640x480); the map gives PAL's Vivendi its Vivendi, past its FMV\\Spyro"),
        "D_002E80E0": (None, "g_ScreenMessages: the start's no-card screen's message 11, whose text (0x31) its disc words as "
                             "PAL's 12 (0xB7)"),
        "D_002F4840": (None, "g_ProductCode: BASLUS-20909, its saves' name (savedevice.cpp names their folder from it too)"),
        "D_003098A8": (None, "the languages' names: not read on it, its languages are its program's own (gamecontext.cpp: "
                             "one, American)"),
        "D_003098B0": (None, "as D_003098A8"),
        "D_003098B8": (None, "as D_003098A8"),
        "D_003098C0": (None, "as D_003098A8"),
        "D_003098C8": (None, "as D_003098A8"),
        "D_00309AE8": (0x200, "g_ScreenHeight: PAL's 512 lines, as the PAL program runs at 60 Hz (the 60 Hz option never "
                              "changes it; NTSC's 448 only if its data needs them, proved by pictures)"),
        "D_00309AEC": (1, "g_Pal: PAL's, so the renderer, the clock and the particles start as the PAL program's at 60 Hz "
                          "(the refresh rate option sets the pacing, 60 Hz by default)"),
    },
}

def read_releases(copies, widens, pal_segments, results, problems):
    """The other releases' maps: [{"program", "serial", "version", "size", "hash", "image": (from, to, hash), "segments":
    [(PAL file offset, its file offset, bytes)], "keeps": [(label, native offset, bytes, value)]}]"""
    releases = []
    for path in sorted(DISCS.glob("region_map_*.json")):
        doc = json.loads(path.read_text())
        if doc.get("kind") != "region-map-native":
            continue
        serial = doc["serial"]
        decided = DECISIONS.get(serial)
        if decided is None:
            problems.append(f"{path.name}: {serial}'s differences aren't decided in DECISIONS")
            continue
        loads = [(int(v, 16), int(o, 16), int(n, 16)) for v, o, n in doc["elf"]["loads"]]

        def theirs(address, size):
            for v, o, n in loads:
                if v <= address and address + size <= v + n:
                    return o + address - v
            raise LookupError(f"{path.name}: {address:#x} (+{size:#x}) isn't in {doc['executable']}'s loaded segments")

        start, end = int(doc["image"]["pal_start"], 16), int(doc["image"]["pal_bss_start"], 16)
        # (the image ends at .bss's start, a little past the file's bytes: zero there, as in the map's hash)
        image_from = file_offset(pal_segments, start, 1)
        image_to = image_from + end - start
        segments = []
        for pal, other, n in doc["segments"]:
            pal, other = int(pal, 16), int(other, 16)
            lo, hi = max(pal, start), min(pal + n, end)
            if lo < hi:
                segments.append((file_offset(pal_segments, lo, hi - lo), theirs(other + lo - pal, hi - lo), hi - lo))
        # Every byte the program reads comes from the image (zero where no segment reaches)
        reads = [(label, offset, n) for label, _, offset, n in copies] + [(label, offset, 4) for label, _, offset, _ in widens]
        for label, offset, n in reads:
            if offset < image_from or offset + n > image_to:
                problems.append(f"{path.name}: {label}'s run at file offset {offset:#x} is outside the image the map lays out")
        # The read differences' objects, each decided
        read_by = {at: label for label, offset, n in reads for at in range(offset, offset + n)}
        reached = set()
        for difference in doc["differences"]:
            if not difference["read"]:
                continue
            a = file_offset(pal_segments, int(difference["pal"], 16), difference["bytes"])
            reached |= {read_by[at] for at in range(a, a + difference["bytes"]) if at in read_by}
        for label in sorted(reached - set(decided)):
            problems.append(f"{path.name}: {serial}'s bytes differ in {label}, which the program reads: decide it in DECISIONS")
        keeps = []
        for label, (value, why) in decided.items():
            if label not in reached:
                problems.append(f"{path.name}: DECISIONS has {label} for {serial}, whose map has no read difference there")
            if value is None:
                continue
            out = results[label][1]
            if len(out) > 4 or bytes(out) != value.to_bytes(len(out), "little"):
                problems.append(f"{label}: the PAL value kept for {serial} ({value:#x}) isn't the PAL executable's")
            keeps.append((label, 0, len(out), value))
        releases.append({"program": doc["executable"], "serial": serial, "version": doc["version"],
                         "size": int(doc["elf"]["bytes"]), "hash": int(doc["elf"]["fnv1a64"], 16),
                         "image": (image_from, image_to, int(doc["image"]["fnv1a64"], 16)), "segments": segments,
                         "keeps": keeps})
    return releases

def write_releases(lines, releases):
    """data.cpp's table of the other releases and their loader, NativeLoadRetailDataAs"""
    lines.append("// The other releases (convert_data.py's DECISIONS, native/discs/*.json): their executable's bytes laid out at the PAL\n"
                 "// executable's file offsets through their map's segments, then read as the PAL executable's are; the values kept PAL's\n"
                 "struct Segment\n{\n    uint32_t pal;\n    uint32_t other;\n    uint32_t size;\n};\n\n"
                 "struct Keep\n{\n    unsigned char* at;\n    uint32_t size;\n    uint32_t value;\n};\n\n"
                 "struct Release\n{\n    const char* program;\n    const char* serial;\n    const char* version;\n    uint64_t size;\n"
                 "    uint64_t hash;\n    uint32_t imageFrom;\n    uint32_t imageTo;\n    uint64_t imageHash;\n"
                 "    const Segment* segments;\n    const Keep* keeps;\n};\n")
    for release in releases:
        name = re.sub(r"\W", "_", release["serial"])
        lines.append(f"const Segment g_Segments_{name}[] = {{")
        for pal, other, n in release["segments"]:
            lines.append(f"    {{{pal:#x}, {other:#x}, {n}}},")
        lines.append(f"    {{0, 0, 0}},\n}};\n\nconst Keep g_Keeps_{name}[] = {{")
        for label, at, n, value in release["keeps"]:
            lines.append(f"    {{x_{cxx_escape(label)} + {at}, {n}, {value:#x}}},")
        lines.append("    {nullptr, 0, 0},\n};\n")
    lines.append("const Release g_Releases[] = {")
    for release in releases:
        name = re.sub(r"\W", "_", release["serial"])
        image_from, image_to, image_hash = release["image"]
        lines.append(f'    {{"{release["program"]}", "{release["serial"]}", "{release["version"]}", {release["size"]}, '
                     f'{release["hash"]:#x}ull, {image_from:#x}, {image_to:#x}, {image_hash:#x}ull, g_Segments_{name}, g_Keeps_{name}}},')
    lines.append("    {nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr, nullptr},\n};\n}\n")
    lines.append("// The other releases' programs this build plays (SLUS_209.09), with their serial and version: nullptr past the last\n"
                 'extern "C" const char* NativeOtherRelease(uint32_t index, const char** serial, const char** version)\n{\n'
                 "    for (uint32_t i = 0; g_Releases[i].program != nullptr; i++)\n    {\n        if (i == index)\n        {\n"
                 "            *serial = g_Releases[i].serial;\n            *version = g_Releases[i].version;\n"
                 "            return g_Releases[i].program;\n        }\n    }\n\n    return nullptr;\n}\n")
    lines.append("// Another release's executable (its program read from the disc image) laid out into the objects, before the fixups: 0\n"
                 "// when done, 1 when it isn't the executable the map is for (another version of the release, or changed), 2 when its\n"
                 "// image laid out doesn't come out as the map says\n"
                 'extern "C" int NativeLoadRetailDataAs(const char* program, const unsigned char* executable, uint64_t size)\n{\n'
                 "    const Release* release = g_Releases;\n"
                 "    while (release->program != nullptr && strcmp(release->program, program) != 0)\n    {\n        release++;\n    }\n\n"
                 "    if (release->program == nullptr || size != release->size || Fnv1a(executable, size, FnvBasis) != release->hash)\n"
                 "    {\n        return 1;\n    }\n\n"
                 "    unsigned char* laid = static_cast<unsigned char*>(calloc(ExecutableSize, 1));\n"
                 "    if (laid == nullptr)\n    {\n        return 2;\n    }\n\n"
                 "    for (const Segment* segment = release->segments; segment->size != 0; segment++)\n    {\n"
                 "        if (segment->other + uint64_t(segment->size) > size || segment->pal + uint64_t(segment->size) > ExecutableSize)\n"
                 "        {\n            free(laid);\n            return 2;\n        }\n\n"
                 "        memcpy(laid + segment->pal, executable + segment->other, segment->size);\n    }\n\n"
                 "    if (Fnv1a(laid + release->imageFrom, release->imageTo - release->imageFrom, FnvBasis) != release->imageHash)\n"
                 "    {\n        free(laid);\n        return 2;\n    }\n\n"
                 "    LayOut(laid);\n    free(laid);\n"
                 "    for (const Keep* keep = release->keeps; keep->at != nullptr; keep++)\n    {\n"
                 "        memcpy(keep->at, &keep->value, keep->size);\n    }\n\n    return 0;\n}\n")

def write_cpp(results, objects, aliases, recipes, marks, problems):
    copies, widens, (executable_size, executable_hash, data_hash), pal_segments = make_recipe(results, objects, recipes, marks,
                                                                                               problems)
    releases = read_releases(copies, widens, pal_segments, results, problems)
    lines = ["// Generated by native/tools/convert_data.py from the split's data: don't edit. None of the game's bytes are here: the",
             "// objects start zero and NativeLoadRetailData lays the executable's bytes (from the player's disc) out into them",
             "#include <stdint.h>", "#include <stdlib.h>", "#include <string.h>", "",
             "// The retail names as symbols (common.h's RETAIL): Mach-O's start with an underscore",
             "#ifdef __APPLE__", '#define SYMBOL(name) "_" name', "#else", "#define SYMBOL(name) name", "#endif", ""]
    symbols = sorted({s for (_, _, _, fx) in results.values() for _, s, _ in fx})
    lines.append('extern "C"\n{')
    for s in symbols:
        if s not in results and s not in aliases:
            lines.append(f'extern char x_{cxx_escape(s)} asm(SYMBOL("{s}"));')
    for label, (t, out, align, fixups) in results.items():
        # x86-64's ABI gives a global array of 16 bytes or more 16 byte alignment, and clang counts on it wherever the C++
        # declares one (aligned SSE loads and stores, movaps): an alignas of less would lower it here, so objects that big are
        # 16 byte aligned (only more than they need elsewhere)
        if len(out) >= 16:
            align = max(align, 16)
        lines.append(f'// {t}\nalignas({align}) unsigned char x_{cxx_escape(label)}[{max(len(out), 1)}] asm(SYMBOL("{label}")) = {{}};')
    lines.append("}\n")
    # Labels inside another object (the PS2's neighbouring globals a C++ struct covers): symbols at their native offsets in it
    for label, (container, offset) in sorted(aliases.items()):
        lines.append(f'asm(".globl " SYMBOL("{label}") "\\n.set " SYMBOL("{label}") ", " SYMBOL("{container}") " + {offset}\\n");')
    for label in aliases:
        lines.append(f'extern "C" char x_{cxx_escape(label)} asm(SYMBOL("{label}"));')
    lines.append("\nnamespace\n{\nstruct Fixup\n{\n    unsigned char* at;\n    const void* to;\n};\n")
    lines.append("const Fixup g_Fixups[] = {")
    for label, (t, out, align, fixups) in results.items():
        for off, symbol, addend in fixups:
            target = f"x_{cxx_escape(symbol)}"
            lines.append(f"    {{x_{cxx_escape(label)} + {off}, reinterpret_cast<const char*>(&{target}) + ({addend})}},")
    lines.append("};\n")
    # The executable's bytes: runs copied, 32 bit integers widened, the vtables' narrow returns marked (mark_narrow_returns)
    lines.append("struct Copy\n{\n    unsigned char* at;\n    uint32_t from;\n    uint32_t size;\n};\n")
    lines.append("struct Widen\n{\n    unsigned char* at;\n    uint32_t from;\n    uint32_t isSigned;\n};\n")
    lines.append("struct Mark\n{\n    unsigned char* at;\n    uint32_t bits;\n};\n")
    lines.append("struct Object\n{\n    const unsigned char* at;\n    uint32_t size;\n};\n")
    lines.append("// Each list ends with a null entry\nconst Copy g_Copies[] = {")
    for label, dst, offset, n in copies:
        lines.append(f"    {{x_{cxx_escape(label)} + {dst}, {offset:#x}, {n}}},")
    lines.append("    {nullptr, 0, 0},\n};\n\nconst Widen g_Widens[] = {")
    for label, dst, offset, signed in widens:
        lines.append(f"    {{x_{cxx_escape(label)} + {dst}, {offset:#x}, {int(signed)}}},")
    lines.append("    {nullptr, 0, 0},\n};\n\nconst Mark g_Marks[] = {")
    for label, at, mark in marks:
        lines.append(f"    {{x_{cxx_escape(label)} + {at}, {mark:#x}}},")
    lines.append("    {nullptr, 0},\n};\n\nconst Object g_Objects[] = {")
    for label, (t, out, align, fixups) in results.items():
        lines.append(f"    {{x_{cxx_escape(label)}, {len(out)}}},")
    lines.append("    {nullptr, 0},\n};\n")
    lines.append(f"constexpr uint64_t ExecutableSize = {executable_size};\nconstexpr uint64_t ExecutableHash = {executable_hash:#x}ull;")
    lines.append(f"constexpr uint64_t DataHash = {data_hash:#x}ull;\nconstexpr uint64_t FnvBasis = {FNV_BASIS:#x}ull;\n")
    lines.append("uint64_t Fnv1a(const unsigned char* bytes, uint64_t size, uint64_t hash)\n{\n"
                 "    for (uint64_t i = 0; i < size; i++)\n    {\n"
                 f"        hash = (hash ^ bytes[i]) * {FNV_PRIME:#x}ull;\n    }}\n\n    return hash;\n}}\n")
    lines.append("// The executable's bytes (the PAL one's, or another release's laid out as it) copied into the objects, widened, marked\n"
                 "void LayOut(const unsigned char* executable)\n{\n"
                 "    for (const Copy* copy = g_Copies; copy->at != nullptr; copy++)\n    {\n"
                 "        memcpy(copy->at, executable + copy->from, copy->size);\n    }\n\n"
                 "    for (const Widen* widen = g_Widens; widen->at != nullptr; widen++)\n    {\n"
                 "        uint32_t value;\n        memcpy(&value, executable + widen->from, sizeof(value));\n"
                 "        uint64_t wide = widen->isSigned != 0 ? static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(value))) : value;\n"
                 "        memcpy(widen->at, &wide, sizeof(wide));\n    }\n\n"
                 "    for (const Mark* mark = g_Marks; mark->at != nullptr; mark++)\n    {\n"
                 "        uint16_t index;\n        memcpy(&index, mark->at, sizeof(index));\n"
                 "        index = static_cast<uint16_t>(index | mark->bits);\n        memcpy(mark->at, &index, sizeof(index));\n    }\n}\n")
    write_releases(lines, releases)
    lines.append("// The retail executable's bytes (SLES_525.68, read from the disc image) laid out into the objects, before the fixups:\n"
                 "// 0 when done, 1 when it isn't the executable this build is for (another version, or changed), 2 when the objects\n"
                 "// don't come out as the build made them\n"
                 'extern "C" int NativeLoadRetailData(const unsigned char* executable, uint64_t size)\n{\n'
                 "    if (size != ExecutableSize || Fnv1a(executable, size, FnvBasis) != ExecutableHash)\n    {\n        return 1;\n    }\n\n"
                 "    LayOut(executable);\n"
                 "    uint64_t hash = FnvBasis;\n    for (const Object* object = g_Objects; object->at != nullptr; object++)\n    {\n"
                 "        hash = Fnv1a(object->at, object->size, hash);\n    }\n\n"
                 "    return hash == DataHash ? 0 : 2;\n}\n")
    lines.append("// Whether a disc image's program (PROGRAM's bytes, read from it) is one this build plays, its data left as it is (the\n"
                 "// game's options choosing another image for the next start, THE LIST 31d): 0 yes, 1 another version or a changed copy,\n"
                 "// 2 a program this build has nothing for\n"
                 'extern "C" int NativeCheckRetailData(const char* program, const unsigned char* executable, uint64_t size)\n{\n'
                 '    if (strcmp(program, "SLES_525.68") == 0)\n    {\n'
                 "        return size == ExecutableSize && Fnv1a(executable, size, FnvBasis) == ExecutableHash ? 0 : 1;\n    }\n\n"
                 "    for (const Release* release = g_Releases; release->program != nullptr; release++)\n    {\n"
                 "        if (strcmp(release->program, program) == 0)\n        {\n"
                 "            return size == release->size && Fnv1a(executable, size, FnvBasis) == release->hash ? 0 : 1;\n"
                 "        }\n    }\n\n    return 2;\n}\n")
    lines.append('extern "C" void NativeApplyDataFixups()\n{\n    for (const Fixup& fixup : g_Fixups)\n    {\n'
                 '        memcpy(fixup.at, &fixup.to, sizeof(fixup.to));\n    }\n}')
    (BUILD / "data.cpp").write_text("\n".join(lines) + "\n")
    print(f"build/native/data.cpp: {len(results)} objects, {len(aliases)} inside others, "
          f"{sum(len(r[3]) for r in results.values())} fixups, {len(copies)} runs of the executable's bytes "
          f"({sum(c[3] for c in copies)} bytes), {len(widens)} widened; other releases: "
          f"{', '.join(r['program'] + ' (' + r['serial'] + ' v' + r['version'] + ')' for r in releases) or 'none'}")


if __name__ == "__main__":
    main()
