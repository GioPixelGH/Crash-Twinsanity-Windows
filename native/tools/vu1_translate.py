#!/usr/bin/env python3
"""VU1's programs translated to C++ (native/GRAPHICS.md, "VU1 translation").

    python3 native/tools/vu1_translate.py OUTPUT.cpp            (from the repository's folder)
    python3 native/tools/vu1_translate.py --disassemble         (the programs, as the translator reads them)
    python3 native/tools/vu1_translate.py --vu0 OUTPUT.cpp      (VU0's microprogram sets, which the game's VCALLMS and decals run)

The game's VU1 microcode is a fixed set of programs in the executable's .vutext (assets/vutext.textbin.bin, the split's copy),
their sizes in .vudata (asm/data/vudata.data.s). The renderer uploads them (MPG) to wherever there's room in micro memory, so
the translation doesn't depend on where a program is: its branches are relative, the addresses it computes (BAL's and JALR's
links) are made from the base it runs at.

The C++ does what src/platform/native/graphics/ee/vu.cpp's interpreter does, instruction by instruction, with the timing
worked out here instead of at run time: the interpreter's pipeline model (FMAC stalls and their flags 4 cycles late, Q after
DIV's latency, ILW's results for the branches, the branch read rule) only depends on what's in flight, never on data, so for a
block of straight code entered with a known state in flight (a "signature": what's in flight relative to the block's first
cycle) every stall, every flag's arrival and every old value a branch reads is known here. The code is made for each block
once per signature it's reached with (a variant); a block's exit state is its successors' signature. The dispatcher
(vu1translate.cpp) enters a variant whose signature matches the interpreter's state, and the interpreter runs anything that
has no variant.

The VU's state lives in the C++'s variables: registers, and for the flags the values (a MAC flag is kept as the raw results
it came from, see vu1jit.h). Each instruction's upper and lower both read the registers as they were before it (the
interpreter's same-cycle rule makes the lower read the upper's target's old value, and the upper runs first).
"""

import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from vudecode import decode, imm11, FLAG_CLIP  # noqa: E402

def code_hash(words):
    """FNV-1a (64 bits) of instructions' bytes, as vu1jit.h's CodeHash: how the dispatcher tells a program in micro memory"""
    h = 0xCBF29CE484222325
    for word in words:
        for i in range(8):
            h = ((h ^ ((word >> (8 * i)) & 0xFF)) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def entry_key(word):
    """An entry's instruction's key (vu1jit.h's EntryKey): its hash's top 32 bits"""
    return code_hash([word]) >> 32


VUTEXT_ADDRESS = 0x2D9D90
VUDATA_ADDRESS = 0x2EC348
CODE_MASK = 0x7FF
VU0 = False
# The emitted code's masks and names (VU1's; --vu0 changes them): micro memory's instructions, data memory's bytes, the
# functions' and tables' names
CODE = '0x7FF'
DATA = '0x3FF0'
NAME = 'Program'
TABLE = ''
# At most this many variants of a block (more: the interpreter runs it)
MAX_VARIANTS = 32


# VU0's three microprogram sets (the DMA chains to VIF0 in .vutext, each a tag and MPGs from address 0): the standard set
# (the maths' helpers, which the game calls with VCALLMS), the culling set and the decal set. Each set is translated as one
# program: its micro memory from 0
VU0_SET_CHAINS = (0x2E6110, 0x2E4EB0, 0x2E5BD0)


def read_vu0_programs(root):
    blob = (root / 'assets/vutext.textbin.bin').read_bytes()
    programs = []
    for chain in VU0_SET_CHAINS:
        at = chain - VUTEXT_ADDRESS
        tag = struct.unpack_from('<Q', blob, at)[0]
        qwc = tag & 0xFFFF
        words = struct.unpack_from('<%dI' % (2 + qwc * 4), blob, at + 8)
        image = {}
        i = 0
        while i < len(words):
            code = words[i]
            i += 1
            cmd, num, imm = (code >> 24) & 0x7F, (code >> 16) & 0xFF, code & 0xFFFF
            if cmd == 0x4A:
                count = num or 256
                for n in range(count):
                    image[imm + n] = words[i + 2 * n] | words[i + 2 * n + 1] << 32
                i += count * 2
            else:
                assert cmd == 0, hex(code)
        size = max(image) + 1
        assert sorted(image) == list(range(size))
        programs.append((None, [image[n] for n in range(size)]))
    return programs


def read_programs(root):
    if VU0:
        return read_vu0_programs(root)
    blob = (root / 'assets/vutext.textbin.bin').read_bytes()
    words = struct.unpack('<%dQ' % (len(blob) // 8), blob[:len(blob) // 8 * 8])
    # .vudata: each program's size (in instructions) at a word of its own, in the order they're in .vutext
    shorts = {}
    for line in (root / 'asm/data/vudata.data.s').read_text().splitlines():
        m = re.search(r'/\* [0-9A-F]+ ([0-9A-F]{8}) \*/ \.short 0x([0-9A-F]+)', line)
        if m:
            shorts[int(m.group(1), 16)] = int(m.group(2), 16)
    sizes = []
    address = VUDATA_ADDRESS
    while address in shorts:
        sizes.append(shorts[address])
        address += 4
    programs = []
    start = 0
    for size in sizes:
        if size == 0 or start + size > len(words):
            break
        programs.append((start, list(words[start:start + size])))
        start += size
    # The renderer's own list of program addresses must be among these
    listed = set(int(x, 16) for x in re.findall(r'VuCode\((0x[0-9A-Fa-f]+)\)', (
        root / 'src/platform/native/graphics/renderer/vuprograms.cpp').read_text()))
    starts = set(VUTEXT_ADDRESS + s * 8 for s, _ in programs)
    missing = listed - starts
    if missing:
        raise SystemExit('vu1_translate: programs the renderer lists but .vudata does not: %s' % sorted(map(hex, missing)))
    return programs


# What the translation handles (anything else: the block stops before it and the interpreter runs it)
UNSUPPORTED_LOWER = {'unknown', 'esadd', 'ersadd', 'eleng', 'erleng', 'eatanxy', 'eatanxz', 'esum', 'ercpr', 'esqrt',
                     'ersqrt', 'esin', 'eatan', 'eexp', 'waitp', 'mfp', 'fsset', 'fcset', 'fseq', 'fsor'}
ARITH_UPPERS = {'arith', 'opmula', 'opmsub'}


def supported(d):
    if d.upper == 'unknown':
        return False
    if d.ibit:
        return True
    if d.lower in UNSUPPORTED_LOWER:
        return False
    if d.lower == 'fsand':
        imm12 = ((d.lower_word >> 21) & 1) << 11 | (d.lower_word & 0x7FF)
        # The sticky bits (6-11) of the visible status flag aren't kept as the interpreter sees them mid-run
        if imm12 & ~0x3F:
            return False
    return True


class Entry:
    __slots__ = ('start', 'ur', 'um', 'lr', 'lm', 'flags', 'mac', 'clip')

    def __init__(self, start, ur, um, lr, lm, flags, mac, clip):
        self.start, self.ur, self.um, self.lr, self.lm, self.flags, self.mac, self.clip = start, ur, um, lr, lm, flags, mac, clip


def sig_key(sig):
    return sig


EMPTY_SIGNATURE = ((), None, (), (0, 0), False)  # fmac entries, fdiv ready, ialu, backup, anyEntry

# Every signature gets a number (the tables in the C++ refer to them by it)
SIGNATURES = {}


def signature_id(sig):
    if sig not in SIGNATURES:
        SIGNATURES[sig] = len(SIGNATURES)
    return SIGNATURES[sig]


def lanes(mask):
    return [c for c in range(4) if mask & (1 << c)]


class Translator:
    def __init__(self, index, start, words):
        self.index = index
        self.start = start
        self.words = words
        self.size = len(words)
        self.ins = [decode(o, w) for o, w in enumerate(words)]
        self.variants = []          # (offset, signature)
        self.variant_ids = {}       # (offset, signature) -> id
        self.code = []              # C++ lines of the variants
        self.exit_signatures = {}   # signature -> name
        self.indirect_exits = set() # signatures the program leaves with at JR, JALR and other exits to the dispatcher
        self.tmp = 0
        self.max_used = 0

    # ---- variants
    def variant(self, offset, sig):
        key = (offset, sig)
        if key in self.variant_ids:
            return self.variant_ids[key]
        if sum(1 for o, _ in self.variants if o == offset) >= MAX_VARIANTS:
            return None
        if not supported(self.ins[offset]) or self.block_end(offset) is None:
            return None
        vid = len(self.variants)
        self.variants.append(key)
        self.variant_ids[key] = vid
        self.pending.append(vid)
        return vid

    def block_end(self, offset):
        """The block from offset: (last instruction, kind) with kind 'branch' (the last is its delay slot), 'ebit', 'end' (the
        program's end), 'stop' (the instruction after the last isn't translated); None when nothing can be translated."""
        o = offset
        while o < self.size:
            d = self.ins[o]
            if not supported(d):
                return (o - 1, 'stop') if o > offset else None
            if d.ebit or d.is_branch:
                if d.ebit and d.is_branch:
                    return (o - 1, 'stop') if o > offset else None
                if o + 1 >= self.size:
                    return (o - 1, 'stop') if o > offset else None
                slot = self.ins[o + 1]
                if not supported(slot) or slot.ebit or slot.is_branch:
                    return (o - 1, 'stop') if o > offset else None
                return (o + 1, 'branch' if d.is_branch else 'ebit')
            o += 1
        return (self.size - 1, 'end')

    def new(self, prefix):
        self.tmp += 1
        return '%s_%d' % (prefix, self.tmp)

    def exit_sig_name(self, sig):
        return str(signature_id(sig))

    # ---- translation of one variant
    def translate(self, vid):
        offset, sig = self.variants[vid]
        out = self.code
        out.append('V%d: // offset %#x' % (vid, offset))
        out.append('{')
        sim = Sim(self, sig)
        last, kind = self.block_end(offset)
        count = last - offset + 1
        self.max_used = max(self.max_used, last)
        out.append('    count += %d;' % count)
        branch = None
        for o in range(offset, last + 1):
            d = self.ins[o]
            out.append('    // %04x %s' % (o, d.text()))
            result = sim.step(o, d, out)
            if result is not None:
                branch = result
        exit_sig = sim.signature()
        if kind == 'branch':
            b = branch
            if b['kind'] == 'static':
                self.go(out, b['target'], exit_sig, sim, '    ')
            elif b['kind'] == 'cond':
                out.append('    if (%s)' % b['cond'])
                out.append('    {')
                self.go(out, b['target'], exit_sig, sim, '        ')
                out.append('    }')
                self.go(out, last + 1, exit_sig, sim, '    ')
            else:
                # JR, JALR: a target in a register
                out.append('    next = %s & %s;' % (b['target'], CODE))
                self.leave(out, exit_sig, sim, '    ', indirect=True)
        elif kind == 'ebit':
            out.append('    next = (base + %d) & %s;' % (last + 1, CODE))
            out.append('    ended = 1;')
            self.leave(out, exit_sig, sim, '    ', indirect=True)
        else:
            # The program's end, or an instruction the translation leaves to the interpreter
            self.go(out, last + 1, exit_sig, sim, '    ')
        out.append('}')

    def go(self, out, target, sig, sim, indent):
        if 0 <= target < self.size:
            vid = self.variant(target, sig)
            if vid is not None:
                sim.assign_slots(out, indent)
                out.append(indent + 'goto V%d;' % vid)
                return
            out.append(indent + 'next = (base + %d) & %s;' % (target, CODE))
            self.leave(out, sig, sim, indent, indirect=False)
        else:
            out.append(indent + 'next = (base + %d) & %s;' % (target, CODE))
            self.leave(out, sig, sim, indent, indirect=True)

    def leave(self, out, sig, sim, indent, indirect):
        if indirect:
            self.indirect_exits.add(sig)
        sim.assign_slots(out, indent)
        out.append(indent + 'exitSignature = %s;' % self.exit_sig_name(sig))
        out.append(indent + 'goto Exit;')


def vf(r):
    return 'vf_%d' % r


def vi(r):
    return '0' if r == 0 else 'vi_%d' % r


class Sim:
    """The interpreter's state while a variant's code is made: cycles relative to the block's first, what's in flight, and
    the names of the C++ values that hold the flags, Q and the branch rule's old value."""

    def __init__(self, translator, sig):
        self.t = translator
        entries, fdiv, ialu, backup, any_entry = sig
        self.cycle = 0
        self.entries = []
        for k, (start, ur, um, lr, lm, flags) in enumerate(entries):
            self.entries.append(Entry(start, ur, um, lr, lm, flags, 'e%d_mac' % (k + 1), 'e%d_clip' % (k + 1)))
        self.vis_mac = 'vis_mac'
        self.vis_clip = 'vis_clip'
        self.vis_div = 'vis_div'
        self.q = 'Q'
        self.pmac = 'pmac'
        self.pclip = 'pclip'
        self.fdiv = None if fdiv is None else (fdiv, 'fdiv_val', 'fdiv_flags')
        self.ialu = [(ready, reg) for ready, reg in ialu]
        self.backup_cycles, self.backup_reg = backup
        self.bk_val = 'bk_val'
        self.any_entry = any_entry
        # Lanes known to hold VU-clean floats (what Clamp would leave alone), per register (32: ACC)
        self.clean = {0: 0xF}

    # ---- the state at the block's exit, relative to the cycle after its last instruction
    def signature(self):
        now = self.cycle
        self.flush(now)
        entries = tuple((e.start - now, e.ur, e.um, e.lr, e.lm, e.flags) for e in self.entries)
        assert all(-3 <= e[0] <= -1 for e in entries), entries
        fdiv = None if self.fdiv is None else self.fdiv[0] - now
        ialu = tuple(sorted((ready - now, reg) for ready, reg in self.ialu if ready > now))
        backup = (self.backup_cycles, self.backup_reg if self.backup_cycles > 0 else 0)
        return (entries, fdiv, ialu, backup, self.any_entry)

    def assign_slots(self, out, indent):
        """The values the next variant (or the exit) finds in the slot variables."""
        pairs = []
        for k, e in enumerate(self.entries):
            pairs.append(('e%d_mac' % (k + 1), e.mac, 'V4u'))
            pairs.append(('e%d_clip' % (k + 1), e.clip, 'u32'))
        pairs += [('vis_mac', self.vis_mac, 'V4u'), ('vis_clip', self.vis_clip, 'u32'), ('vis_div', self.vis_div, 'u32'),
                  ('Q', self.q, 'u32'), ('pmac', self.pmac, 'V4u'), ('pclip', self.pclip, 'u32'),
                  ('bk_val', self.bk_val, 'u16')]
        if self.fdiv is not None:
            pairs += [('fdiv_val', self.fdiv[1], 'u32'), ('fdiv_flags', self.fdiv[2], 'u32')]
        pairs = [p for p in pairs if p[0] != p[1]]
        if not pairs:
            return
        temps = []
        for slot, value, ctype in pairs:
            t = self.t.new('s')
            out.append(indent + 'const %s %s = %s;' % (ctype, t, value))
            temps.append((slot, t))
        for slot, t in temps:
            out.append(indent + '%s = %s;' % (slot, t))

    # ---- the interpreter's timing
    def fmac_stall(self, reg, mask):
        if reg == 0 or mask == 0:
            return
        for e in self.entries:
            if self.cycle - e.start >= 4:
                continue
            if (e.ur == reg and (e.um & mask)) or (e.lr == reg and (e.lm & mask)):
                self.cycle = max(self.cycle, e.start + 4)

    def flush(self, cycle):
        while self.entries and cycle - self.entries[0].start >= 4:
            e = self.entries.pop(0)
            if e.flags & FLAG_CLIP:
                self.vis_clip = e.clip
            self.vis_mac = e.mac
        if self.fdiv is not None and cycle >= self.fdiv[0]:
            self.q = self.fdiv[1]
            self.vis_div = self.fdiv[2]
            self.fdiv = None

    # ---- operands
    def clean_lanes(self, reg):
        return self.clean.get(reg, 0)

    def set_clean(self, reg, mask, clean_mask):
        """Lanes in mask written, clean where clean_mask says."""
        old = self.clean.get(reg, 0)
        self.clean[reg] = (old & ~mask) | (clean_mask & mask)

    def operand(self, reg, needed):
        """A register's value clamped (VuFloat) in the lanes needed."""
        name = 'acc' if reg == 32 else vf(reg)
        if self.clean_lanes(reg) & needed == needed:
            return name
        return 'Clamp(%s)' % name

    def step(self, o, d, out):
        t = self.t
        before = self.cycle
        self.fmac_stall(d.upper_read0, d.upper_read0_mask)
        self.fmac_stall(d.upper_read1, d.upper_read1_mask)
        if not d.ibit:
            self.fmac_stall(d.lower_read0, d.lower_read0_mask)
            self.fmac_stall(d.lower_read1, d.lower_read1_mask)
            if d.pipe == 'fdiv' and self.fdiv is not None:
                self.cycle = max(self.cycle, self.fdiv[0])
            elif d.pipe == 'branch':
                for ready, reg in self.ialu:
                    if ready > self.cycle and (d.lower_read_vi >> reg) & 1:
                        self.cycle = ready
        self.flush(self.cycle)
        elapsed = self.cycle - before + 1
        self.backup_cycles = self.backup_cycles - elapsed if self.backup_cycles > elapsed else 0

        discard_lower = (not d.ibit and d.upper_write_vf != 0 and d.lower_write_vf == d.upper_write_vf)

        # The upper: computed from the registers as they are, written after the lower has read them
        commit = []
        mac_changed = False
        clip_changed = None
        upper_fmac = d.upper_fmac
        if d.upper == 'arith':
            mac_changed = True
            self.upper_arith(d, out, commit)
        elif d.upper in ('max', 'mini'):
            if d.fd != 0:
                a = vf(d.fs)
                if d.source == 'vector':
                    b = vf(d.ft)
                    bclean = self.clean_lanes(d.ft)
                elif d.source == 'broadcast':
                    b = 'SplatLane<%d>(%s)' % (d.bc, vf(d.ft))
                    bclean = 0xF if self.clean_lanes(d.ft) & (1 << d.bc) else 0
                else:
                    b = 'Splat(I)'
                    bclean = 0
                if d.source == 'vector' and d.fs == d.ft:
                    # MAX or MINI of a register and itself (the upper pipeline's move): the register
                    commit.append((d.fd, d.dest, a, self.clean_lanes(d.fs)))
                else:
                    r = t.new('u')
                    out.append('    const V4u %s = %s(%s, %s);' % (r, 'FpMax' if d.upper == 'max' else 'FpMin', a, b))
                    cl = self.clean_lanes(d.fs) & bclean
                    commit.append((d.fd, d.dest, r, cl))
        elif d.upper == 'itof':
            if d.ft != 0:
                r = t.new('u')
                out.append('    const V4u %s = Itof<%d>(%s);' % (r, d.shift, vf(d.fs)))
                commit.append((d.ft, d.dest, r, 0xF))
        elif d.upper == 'ftoi':
            if d.ft != 0:
                r = t.new('u')
                out.append('    const V4u %s = Ftoi<%d>(%s);' % (r, d.shift, vf(d.fs)))
                commit.append((d.ft, d.dest, r, 0))
        elif d.upper == 'abs':
            if d.ft != 0:
                r = t.new('u')
                out.append('    const V4u %s = %s & 0x7FFFFFFFu;' % (r, vf(d.fs)))
                commit.append((d.ft, d.dest, r, self.clean_lanes(d.fs)))
        elif d.upper == 'clip':
            c = t.new('c')
            out.append('    const u32 %s = Clip(%s, %s, %s);' % (c, self.pclip, vf(d.fs), vf(d.ft)))
            self.pclip = c
            clip_changed = c
        elif d.upper in ('opmula', 'opmsub'):
            mac_changed = True
            self.upper_outer(d, out, commit)

        # The upper's results are written before the lower runs (an XGKICK's packet and registers are seen after them), unless
        # the lower reads the upper's target: then it sees the register as it was (the same-cycle rule) and the result is
        # written after it
        def write_upper():
            for reg, mask, value, cl in commit:
                if reg == 32:
                    out.append('    acc = Blend<%d>(acc, %s);' % (mask, value))
                else:
                    out.append('    %s = Blend<%d>(%s, %s);' % (vf(reg), mask, vf(reg), value))
                self.set_clean(reg, mask, cl)

        late = (not d.ibit and d.upper_write_vf != 0 and
                (d.lower_read0 == d.upper_write_vf or d.lower_read1 == d.upper_write_vf))
        if not late:
            write_upper()

        lower_fmac = False
        branch = None
        if d.ibit:
            out.append('    I = Opaque(%#010xu);' % d.lower_word)
        elif not discard_lower:
            lower_fmac = d.pipe == 'fmac'
            branch = self.lower(o, d, out)

        if late:
            write_upper()

        # The FMAC pipeline's entry
        if upper_fmac or lower_fmac:
            if not self.any_entry and not mac_changed:
                # The run's first entry carries the starting status: its bits go into the sticky ones
                out.append('    sticky_bits |= StatusLow(MacOf(%s));' % self.pmac)
            self.any_entry = True
            flags = FLAG_CLIP if d.upper == 'clip' else 0
            entry = Entry(self.cycle,
                          d.upper_write_vf if upper_fmac else 0, d.upper_write_mask if upper_fmac else 0,
                          d.lower_write_vf if lower_fmac else 0, d.lower_write_mask if lower_fmac else 0,
                          flags, self.pmac, self.pclip)
            # An entry that writes no register and brings no new flag changes nothing when it lands: it's left out (the
            # dispatcher leaves such entries out of the interpreter's state's signature too)
            previous = self.entries[-1].mac if self.entries else self.vis_mac
            if entry.ur != 0 or entry.lr != 0 or entry.flags != 0 or entry.mac != previous:
                self.entries.append(entry)
            assert len(self.entries) <= 4
        self.cycle += 1
        return branch

    def upper_arith(self, d, out, commit):
        t = self.t
        dest = d.dest
        a = self.operand(d.fs, dest)
        if d.source == 'vector':
            b = self.operand(d.ft, dest)
        elif d.source == 'broadcast':
            if self.clean_lanes(d.ft) & (1 << d.bc):
                b = 'SplatLane<%d>(%s)' % (d.bc, vf(d.ft))
            else:
                b = 'SplatLane<%d>(Clamp(%s))' % (d.bc, vf(d.ft))
        elif d.source == 'i':
            b = 'Clamp(Splat(I))'
        else:
            b = 'Clamp(Splat(%s))' % self.q
        raw = t.new('r')
        if d.op == 'add':
            out.append('    const V4u %s = AsU(AsF(%s) + AsF(%s));' % (raw, a, b))
        elif d.op == 'sub':
            out.append('    const V4u %s = AsU(AsF(%s) - AsF(%s));' % (raw, a, b))
        elif d.op == 'mul':
            out.append('    const V4u %s = AsU(AsF(%s) * AsF(%s));' % (raw, a, b))
        else:
            p = t.new('p')
            out.append('    const V4f %s = AsF(%s) * AsF(%s);' % (p, a, b))
            accv = self.operand(32, dest)
            out.append('    const V4u %s = AsU(AsF(%s) %s %s);' % (raw, accv, '+' if d.op == 'madd' else '-', p))
        m = t.new('m')
        out.append('    const V4u %s = Blend<%d>(Splat(0x3F800000u), %s);' % (m, dest, raw))
        out.append('    AccumulateResults(sticky, %s);' % m)
        self.pmac = m
        if d.to_acc or d.fd != 0:
            res = t.new('u')
            out.append('    const V4u %s = Result(%s);' % (res, raw))
            commit.append((32 if d.to_acc else d.fd, dest, res, 0xF))

    def upper_outer(self, d, out, commit):
        t = self.t
        a = self.operand(d.fs, 7)
        b = self.operand(d.ft, 7)
        p = t.new('p')
        out.append('    const V4f %s = AsF(VU1_SHUFFLE(%s, 1, 2, 0, 3)) * AsF(VU1_SHUFFLE(%s, 2, 0, 1, 3));' % (p, a, b))
        raw = t.new('r')
        if d.upper == 'opmula':
            out.append('    const V4u %s = AsU(%s);' % (raw, p))
        else:
            out.append('    const V4u %s = AsU(AsF(%s) - %s);' % (raw, self.operand(32, 7), p))
        m = t.new('m')
        # The w lane's MAC bits stay as they were
        out.append('    const V4u %s = Blend<7>(%s, %s);' % (m, self.pmac, raw))
        out.append('    Accumulate(sticky, %s);' % m)
        self.pmac = m
        res = t.new('u')
        out.append('    const V4u %s = Clamp(%s);' % (res, raw))
        if d.upper == 'opmula':
            commit.append((32, 7, res, 0x7))
        elif d.fd != 0:
            commit.append((d.fd, 7, res, 0x7))

    # ---- the lower instructions
    def write_vi(self, reg, value, out, alu=False):
        """vu.cpp's writeVi: an integer register written; the integer ALU's keep the old value for the branch rule."""
        if reg == 0:
            return
        if alu:
            if not (self.backup_cycles > 0 and self.backup_reg == reg):
                self.backup_reg = reg
                b = self.t.new('b')
                out.append('    const u16 %s = %s;' % (b, vi(reg)))
                self.bk_val = b
            self.backup_cycles = 2
        out.append('    %s = static_cast<u16>(%s);' % (vi(reg), value))

    def vi_read(self, reg):
        """viRead: the branch rule's old value while it lasts."""
        if self.backup_cycles > 0 and self.backup_reg == reg:
            return 'static_cast<s16>(%s)' % self.bk_val
        return 'static_cast<s16>(%s)' % vi(reg)

    def address(self, reg, imm):
        """A data address in bytes: a quadword index (the register's signed value plus the offset) times 16, wrapped."""
        if reg == 0:
            return '%#x' % ((imm * 16) & int(DATA, 16))
        return '((static_cast<u32>(static_cast<s32>(static_cast<s16>(%s)) + %d) * 16) & %s)' % (vi(reg), imm, DATA)

    def lower(self, o, d, out):
        t = self.t
        lw = d.lower_word
        is_, it, id_ = d.lfs & 0xF, d.lft & 0xF, d.lfd & 0xF
        imm = imm11(lw)
        mask = d.ldest
        op = d.lower

        def load_into(reg, at):
            if reg == 0:
                return
            v = t.new('l')
            out.append('    const V4u %s = Load(mem + %s);' % (v, at))
            out.append('    %s = Blend<%d>(%s, %s);' % (vf(reg), mask, vf(reg), v))
            self.set_clean(reg, mask, 0)

        def store_from(reg, at):
            if mask == 0xF:
                out.append('    Store(mem + %s, %s);' % (at, vf(reg)))
            elif mask != 0:
                a = t.new('a')
                out.append('    const u32 %s = %s;' % (a, at))
                out.append('    Store(mem + %s, Blend<%d>(Load(mem + %s), %s));' % (a, mask, a, vf(reg)))

        if op in ('nop', 'waitq'):
            return None
        if op == 'lq':
            load_into(d.lft, self.address(is_, imm))
        elif op == 'sq':
            store_from(d.lfs, self.address(it, imm))
        elif op == 'lqi':
            load_into(d.lft, '((%s * 16) & %s)' % (vi(is_), DATA))
            if is_ != 0:
                self.write_vi(is_, '%s + 1' % vi(is_), out)
        elif op == 'sqi':
            store_from(d.lfs, '((%s * 16) & %s)' % (vi(it), DATA))
            if it != 0:
                self.write_vi(it, '%s + 1' % vi(it), out)
        elif op == 'lqd':
            if is_ != 0:
                self.write_vi(is_, '%s - 1' % vi(is_), out)
            load_into(d.lft, '((%s * 16) & %s)' % (vi(is_), DATA))
        elif op == 'sqd':
            if it != 0:
                self.write_vi(it, '%s - 1' % vi(it), out)
            store_from(d.lfs, '((%s * 16) & %s)' % (vi(it), DATA))
        elif op in ('ilw', 'ilwr'):
            at = self.address(is_, imm) if op == 'ilw' else '((%s * 16) & %s)' % (vi(is_), DATA)
            ls = lanes(mask)
            if it != 0:
                if ls:
                    a = t.new('a')
                    out.append('    const u32 %s = %s;' % (a, at))
                    value = 'ReadU32(mem + %s + %d)' % (a, ls[-1] * 4)
                else:
                    value = vi(it)
                self.write_vi(it, value, out)
                self.ialu.append((self.cycle + 4, it))
        elif op in ('isw', 'iswr'):
            at = self.address(is_, imm) if op == 'isw' else '((%s * 16) & %s)' % (vi(is_), DATA)
            if mask:
                a = t.new('a')
                out.append('    const u32 %s = %s;' % (a, at))
                for c in lanes(mask):
                    out.append('    WriteU32(mem + %s + %d, %s);' % (a, c * 4, vi(it)))
        elif op == 'iaddiu':
            imm15 = ((lw >> 10) & 0x7800) | (lw & 0x7FF)
            self.write_vi(it, '%s + %d' % (vi(is_), imm15), out, alu=True)
        elif op == 'isubiu':
            imm15 = ((lw >> 10) & 0x7800) | (lw & 0x7FF)
            self.write_vi(it, '%s - %d' % (vi(is_), imm15), out, alu=True)
        elif op == 'iaddi':
            imm5 = (lw >> 6) & 0x1F
            imm5 = imm5 - 32 if imm5 & 0x10 else imm5
            self.write_vi(it, '%s + (%d)' % (vi(is_), imm5), out, alu=True)
        elif op == 'iadd':
            self.write_vi(id_, '%s + %s' % (vi(is_), vi(it)), out, alu=True)
        elif op == 'isub':
            self.write_vi(id_, '%s - %s' % (vi(is_), vi(it)), out, alu=True)
        elif op == 'iand':
            self.write_vi(id_, '%s & %s' % (vi(is_), vi(it)), out, alu=True)
        elif op == 'ior':
            self.write_vi(id_, '%s | %s' % (vi(is_), vi(it)), out, alu=True)
        elif op == 'fceq':
            self.write_vi(1, '(%s & 0xFFFFFF) == %#x ? 1 : 0' % (self.vis_clip, lw & 0xFFFFFF), out)
        elif op == 'fcand':
            self.write_vi(1, '(%s & %#x) != 0 ? 1 : 0' % (self.vis_clip, lw & 0xFFFFFF), out)
        elif op == 'fcor':
            self.write_vi(1, '((%s & 0xFFFFFF) | %#x) == 0xFFFFFF ? 1 : 0' % (self.vis_clip, lw & 0xFFFFFF), out)
        elif op == 'fcget':
            self.write_vi(it, '%s & 0xFFF' % self.vis_clip, out)
        elif op == 'fsand':
            imm12 = ((lw >> 21) & 1) << 11 | (lw & 0x7FF)
            self.write_vi(it, '(StatusLow(MacOf(%s)) | %s) & %#x' % (self.vis_mac, self.vis_div, imm12), out)
        elif op == 'fmeq':
            self.write_vi(it, '(MacOf(%s) & 0xFFFF) == %s ? 1 : 0' % (self.vis_mac, vi(is_)), out)
        elif op == 'fmand':
            self.write_vi(it, '%s & (MacOf(%s) & 0xFFFF)' % (vi(is_), self.vis_mac), out)
        elif op == 'fmor':
            self.write_vi(it, '%s | (MacOf(%s) & 0xFFFF)' % (vi(is_), self.vis_mac), out)
        elif op == 'b':
            return {'kind': 'static', 'target': o + 1 + imm}
        elif op == 'bal':
            if it != 0:
                self.write_vi(it, '((base + %d) & %s) + 2' % (o, CODE), out)
            return {'kind': 'static', 'target': o + 1 + imm}
        elif op == 'jr':
            target = t.new('j')
            out.append('    const u32 %s = %s;' % (target, vi(is_)))
            return {'kind': 'register', 'target': target}
        elif op == 'jalr':
            target = t.new('j')
            out.append('    const u32 %s = %s;' % (target, vi(is_)))
            if it != 0:
                self.write_vi(it, '((base + %d) & %s) + 2' % (o, CODE), out)
            return {'kind': 'register', 'target': target}
        elif op in ('ibeq', 'ibne', 'ibltz', 'ibgtz', 'iblez', 'ibgez'):
            cond = t.new('k')
            if op == 'ibeq':
                expr = '%s == %s' % (self.vi_read(it), self.vi_read(is_))
            elif op == 'ibne':
                expr = '%s != %s' % (self.vi_read(it), self.vi_read(is_))
            else:
                expr = '%s %s 0' % (self.vi_read(is_), {'ibltz': '<', 'ibgtz': '>', 'iblez': '<=', 'ibgez': '>='}[op])
            out.append('    const bool %s = %s;' % (cond, expr))
            return {'kind': 'cond', 'cond': cond, 'target': o + 1 + imm}
        elif op == 'move':
            if d.lft != 0:
                out.append('    %s = Blend<%d>(%s, %s);' % (vf(d.lft), mask, vf(d.lft), vf(d.lfs)))
                self.set_clean(d.lft, mask, self.clean_lanes(d.lfs))
        elif op == 'mr32':
            if d.lft != 0:
                r = t.new('u')
                out.append('    const V4u %s = VU1_SHUFFLE(%s, 1, 2, 3, 0);' % (r, vf(d.lfs)))
                src = self.clean_lanes(d.lfs)
                rot = ((src >> 1) | (src << 3)) & 0xF
                out.append('    %s = Blend<%d>(%s, %s);' % (vf(d.lft), mask, vf(d.lft), r))
                self.set_clean(d.lft, mask, rot)
        elif op == 'mtir':
            self.write_vi(it, '%s[%d]' % (vf(d.lfs), d.fsf), out)
        elif op == 'mfir':
            if d.lft != 0:
                out.append('    %s = Blend<%d>(%s, Splat(static_cast<u32>(static_cast<s32>(static_cast<s16>(%s)))));' % (
                    vf(d.lft), mask, vf(d.lft), vi(is_)))
                self.set_clean(d.lft, mask, 0)
        elif op == 'rinit':
            out.append('    R = 0x3F800000u | (%s[%d] & 0x007FFFFFu);' % (vf(d.lfs), d.fsf))
        elif op == 'rxor':
            out.append('    R = 0x3F800000u | ((R ^ %s[%d]) & 0x007FFFFFu);' % (vf(d.lfs), d.fsf))
        elif op == 'rget':
            if d.lft != 0:
                out.append('    %s = Blend<%d>(%s, Splat(R));' % (vf(d.lft), mask, vf(d.lft)))
                self.set_clean(d.lft, mask, 0xF)
        elif op == 'rnext':
            if d.lft != 0:
                out.append('    R = NextR(R);')
                out.append('    %s = Blend<%d>(%s, Splat(R));' % (vf(d.lft), mask, vf(d.lft)))
                self.set_clean(d.lft, mask, 0xF)
        elif op == 'xtop':
            self.write_vi(it, 'vu.top', out)
        elif op == 'xitop':
            self.write_vi(it, 'vu.itop', out)
        elif op == 'xgkick':
            self.kick(o, is_, out)
        elif op in ('div', 'sqrt', 'rsqrt'):
            v = t.new('q')
            f = t.new('f')
            out.append('    u32 %s;' % f)
            if op == 'div':
                out.append('    const u32 %s = Divide(%s[%d], %s[%d], %s);' % (v, vf(d.lfs), d.fsf, vf(d.lft), d.ftf, f))
            elif op == 'sqrt':
                out.append('    const u32 %s = SquareRoot(%s[%d], %s);' % (v, vf(d.lft), d.ftf, f))
            else:
                out.append('    const u32 %s = ReciprocalSquareRoot(%s[%d], %s[%d], %s);' % (
                    v, vf(d.lfs), d.fsf, vf(d.lft), d.ftf, f))
            self.fdiv = (self.cycle + d.latency, v, f)
        else:
            raise AssertionError('lower %s' % op)
        return None

    def kick(self, o, is_, out):
        """XGKICK: the registers written back (the GIF and the checks see them), the packet sent."""
        out.append('    SYNC_REGISTERS(%s, %s, %s);' % (self.vis_mac, self.vis_clip, self.q))
        out.append('    vu.kickPc = (base + %d) & %s;' % (o, CODE))
        out.append('    vu.Kick(%s & 0x3FF);' % vi(is_))


def translate_all(root):
    programs = read_programs(root)
    translators = []
    for index, (start, words) in enumerate(programs):
        translators.append(Translator(index, start, words))

    # Where programs are entered: their starts and after their E bits (a new run: nothing in flight), and with whatever a
    # program leaves with at its indirect exits (or with nothing in flight), their starts, the places a call (BAL, JALR)
    # returns to and the code after a jump (JR, B: a subroutine's start)
    run_entries = []
    indirect_entries = []
    for tr in translators:
        run_entries.append((tr, 0))
        indirect_entries.append((tr, 0))
        for o, d in enumerate(tr.ins):
            if d.ebit and o + 2 < tr.size:
                run_entries.append((tr, o + 2))
                indirect_entries.append((tr, o + 2))
            # (VU0's VCALLMS targets can be one past that: after a padding instruction)
            if VU0 and d.ebit and o + 3 < tr.size:
                run_entries.append((tr, o + 3))
                indirect_entries.append((tr, o + 3))
            if not d.ibit and d.lower in ('bal', 'jalr', 'jr', 'b') and o + 2 < tr.size:
                indirect_entries.append((tr, o + 2))

    for tr in translators:
        tr.pending = []
    for tr, o in run_entries:
        tr.variant(o, EMPTY_SIGNATURE)
    settled = EMPTY_SIGNATURE[:4] + (True,)
    for tr, o in indirect_entries:
        tr.variant(o, EMPTY_SIGNATURE)
        tr.variant(o, settled)
    seen_indirect = set()
    while True:
        for tr in translators:
            while tr.pending:
                tr.translate(tr.pending.pop(0))
        exits = set()
        for tr in translators:
            exits |= tr.indirect_exits
        new = exits - seen_indirect
        if not new:
            break
        seen_indirect |= new
        for sig in new:
            for tr, o in indirect_entries:
                tr.variant(o, sig)
    return translators


def c_signature(sig):
    entries, fdiv, ialu, backup, any_entry = sig
    fm = ', '.join('{%d, %d, %d, %d, %d, %d}' % e for e in entries) or '{}'
    ia = ', '.join('{%d, %d}' % e for e in ialu) or '{}'
    return '{%d, %d, %d, %d, %d, %d, %d, 0, {%s}, {%s}}' % (
        len(entries), len(ialu), 0 if fdiv is None else 1, 0 if fdiv is None else fdiv, backup[0], backup[1],
        1 if any_entry else 0, fm, ia)


HEADER = '''// Made by native/tools/vu1_translate.py from the game's VU1 microcode (assets/vutext.textbin.bin): don't edit.
// See native/GRAPHICS.md, "VU1 translation".
#include "graphics/ee/vu1jit.h"

#pragma clang fp contract(off)

namespace Ee::Vu1Jit
{
namespace
{
inline u32 ReadU32(const u8* at)
{
    u32 v;
    std::memcpy(&v, at, 4);
    return v;
}

inline void WriteU32(u8* at, u16 value)
{
    u32 v = value;
    std::memcpy(at, &v, 4);
}

inline u32 Clip(u32 previous, V4u fs, V4u ft)
{
    s32 w = static_cast<s32>(ft[3]);
    w = (w & 0x7F800000) ? (w & 0x7FFFFFFF) : 0x007FFFFF;
    u32 flags = (previous << 6) & 0xFFFFFF;
    for (int c = 0; c < 3; c++)
    {
        if (static_cast<s32>(fs[c]) > w)
        {
            flags |= 1u << (c * 2);
        }

        if (static_cast<s32>(fs[c] ^ 0x80000000u) > w)
        {
            flags |= 2u << (c * 2);
        }
    }

    return flags;
}

inline u32 NextR(u32 r)
{
    u32 x = (r >> 4) & 1;
    u32 y = (r >> 22) & 1;
    r <<= 1;
    r ^= x ^ y;
    return (r & 0x7FFFFF) | 0x3F800000;
}
}

#define STORE_REGISTERS() \\
    do \\
    { \\
        StoreVf(vu.vf[1], vf_1); \\
        StoreVf(vu.vf[2], vf_2); \\
        StoreVf(vu.vf[3], vf_3); \\
        StoreVf(vu.vf[4], vf_4); \\
        StoreVf(vu.vf[5], vf_5); \\
        StoreVf(vu.vf[6], vf_6); \\
        StoreVf(vu.vf[7], vf_7); \\
        StoreVf(vu.vf[8], vf_8); \\
        StoreVf(vu.vf[9], vf_9); \\
        StoreVf(vu.vf[10], vf_10); \\
        StoreVf(vu.vf[11], vf_11); \\
        StoreVf(vu.vf[12], vf_12); \\
        StoreVf(vu.vf[13], vf_13); \\
        StoreVf(vu.vf[14], vf_14); \\
        StoreVf(vu.vf[15], vf_15); \\
        StoreVf(vu.vf[16], vf_16); \\
        StoreVf(vu.vf[17], vf_17); \\
        StoreVf(vu.vf[18], vf_18); \\
        StoreVf(vu.vf[19], vf_19); \\
        StoreVf(vu.vf[20], vf_20); \\
        StoreVf(vu.vf[21], vf_21); \\
        StoreVf(vu.vf[22], vf_22); \\
        StoreVf(vu.vf[23], vf_23); \\
        StoreVf(vu.vf[24], vf_24); \\
        StoreVf(vu.vf[25], vf_25); \\
        StoreVf(vu.vf[26], vf_26); \\
        StoreVf(vu.vf[27], vf_27); \\
        StoreVf(vu.vf[28], vf_28); \\
        StoreVf(vu.vf[29], vf_29); \\
        StoreVf(vu.vf[30], vf_30); \\
        StoreVf(vu.vf[31], vf_31); \\
        StoreVf(vu.acc, acc); \\
        vu.vi[1] = vi_1; \\
        vu.vi[2] = vi_2; \\
        vu.vi[3] = vi_3; \\
        vu.vi[4] = vi_4; \\
        vu.vi[5] = vi_5; \\
        vu.vi[6] = vi_6; \\
        vu.vi[7] = vi_7; \\
        vu.vi[8] = vi_8; \\
        vu.vi[9] = vi_9; \\
        vu.vi[10] = vi_10; \\
        vu.vi[11] = vi_11; \\
        vu.vi[12] = vi_12; \\
        vu.vi[13] = vi_13; \\
        vu.vi[14] = vi_14; \\
        vu.vi[15] = vi_15; \\
        vu.i = I; \\
        vu.r = R; \\
    } while (false)

#define SYNC_REGISTERS(SYNC_MAC, SYNC_CLIP, SYNC_Q) \\
    do \\
    { \\
        STORE_REGISTERS(); \\
        vu.q = (SYNC_Q); \\
        vu.macFlag = MacOf(SYNC_MAC); \\
        vu.clipFlag = (SYNC_CLIP); \\
        vu.executed += count; \\
        count = 0; \\
    } while (false)

'''

TABLES_HEADER = '''// Made by native/tools/vu1_translate.py from the game's VU1 microcode (assets/vutext.textbin.bin): don't edit.
// See native/GRAPHICS.md, "VU1 translation". The programs' functions are in the files with _0 to _7 after this one's name.
#include "graphics/ee/vu1jit.h"

namespace Ee::Vu1Jit
{
'''

FUNCTION_START = '''int %(name)s%(index)d(Vu& vu, VuMicroState& st, Slots& slots, u32 base, u32 variant)
{
    u8* const mem = vu.data();
    V4u vf_0 = LoadVf(vu.vf[0]);
%(vfloads)s
    V4u acc = LoadVf(vu.acc);
%(viloads)s
    u32 I = vu.i;
    u32 Q = vu.q;
    u32 R = vu.r;
    V4u pmac = slots.pendingMac;
    V4u vis_mac = slots.visibleMac;
    V4u e1_mac = slots.entryMac[0];
    V4u e2_mac = slots.entryMac[1];
    V4u e3_mac = slots.entryMac[2];
    u32 pclip = slots.pendingClip;
    u32 vis_clip = slots.visibleClip;
    u32 e1_clip = slots.entryClip[0];
    u32 e2_clip = slots.entryClip[1];
    u32 e3_clip = slots.entryClip[2];
    u32 vis_div = slots.visibleDivFlags;
    u32 fdiv_val = slots.fdivValue;
    u32 fdiv_flags = slots.fdivFlags;
    u16 bk_val = static_cast<u16>(slots.backupValue);
    u32 sticky_bits = slots.stickyBits;
    Sticky sticky = slots.sticky;
    u64 count = 0;
    u32 next = 0;
    int ended = 0;
    u32 exitSignature = 0;
    switch (variant)
    {
%(cases)s
    default:
        return -1;
    }

'''

FUNCTION_END = '''Exit:
    STORE_REGISTERS();
    vu.q = Q;
    vu.executed += count;
    slots.pendingMac = pmac;
    slots.visibleMac = vis_mac;
    slots.entryMac[0] = e1_mac;
    slots.entryMac[1] = e2_mac;
    slots.entryMac[2] = e3_mac;
    slots.pendingClip = pclip;
    slots.visibleClip = vis_clip;
    slots.entryClip[0] = e1_clip;
    slots.entryClip[1] = e2_clip;
    slots.entryClip[2] = e3_clip;
    slots.visibleDivFlags = vis_div;
    slots.fdivValue = fdiv_val;
    slots.fdivFlags = fdiv_flags;
    slots.backupValue = bk_val;
    slots.stickyBits = sticky_bits;
    slots.sticky = sticky;
    slots.exitSignature = exitSignature;
    st.pc = next;
    return ended;
}

'''


# The functions go into this many files, compiled side by side
SHARDS = 8


def write_if_changed(path, text):
    path = Path(path)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def generate(root, output):
    """OUTPUT (the tables) and OUTPUT's name with _0 to _7 (the programs' functions, shared out by size)."""
    translators = translate_all(root)
    for tr in translators:
        for o, sig in tr.variants:
            signature_id(sig)
    output = Path(output)

    # The functions, the biggest first into the file with the least so far
    shards = [[] for _ in range(SHARDS)]
    sizes = [0] * SHARDS
    for tr in sorted(translators, key=lambda tr: -len(tr.code)):
        n = sizes.index(min(sizes))
        shards[n].append(tr)
        sizes[n] += len(tr.code)
    for n, members in enumerate(shards):
        out = [HEADER.replace("game's VU1 microcode", "game's VU0 microcode") if VU0 else HEADER]
        for tr in sorted(members, key=lambda tr: tr.index):
            vfloads = '\n'.join('    V4u vf_%d = LoadVf(vu.vf[%d]);' % (r, r) for r in range(1, 32))
            viloads = '\n'.join('    u16 vi_%d = vu.vi[%d];' % (r, r) for r in range(1, 16))
            cases = '\n'.join('    case %d:\n        goto V%d;' % (v, v) for v in range(len(tr.variants)))
            out.append(FUNCTION_START % {'name': NAME, 'index': tr.index, 'vfloads': vfloads, 'viloads': viloads, 'cases': cases})
            out.extend(tr.code)
            out.append(FUNCTION_END)
        out.append('}')
        write_if_changed(output.with_name('%s_%d%s' % (output.stem, n, output.suffix)), '\n'.join(out) + '\n')

    out = [TABLES_HEADER.replace("game's VU1 microcode", "game's VU0 microcode").replace('_0 to _7', '_0 and _1') if VU0 else TABLES_HEADER]
    out.append('const Signature g_Signatures%s[] = {' % TABLE)
    for sig, sid in sorted(SIGNATURES.items(), key=lambda kv: kv[1]):
        out.append('    %s,' % c_signature(sig))
    out.append('};')
    out.append('const u32 g_SignatureCount%s = %d;' % (TABLE, len(SIGNATURES)))
    out.append('')
    for tr in translators:
        out.append('int %s%d(Vu& vu, VuMicroState& st, Slots& slots, u32 base, u32 variant);' % (NAME, tr.index))
    out.append('')
    out.append('namespace\n{')
    for tr in translators:
        # The microcode itself isn't kept: a program is told in micro memory by its words' hash, its entries by their word's
        out.append('const Variant kVariants%d[] = {' % tr.index)
        for vid, (o, sig) in sorted(enumerate(tr.variants), key=lambda v: (v[1][0], v[0])):
            out.append('    {%d, %d, %d, %#010x},' % (o, vid, signature_id(sig), entry_key(tr.words[o])))
        if not tr.variants:
            out.append('    {0, 0, 0, 0},')
        out.append('};')
    out.append('}')
    out.append('')
    out.append('const Program g_Programs%s[] = {' % TABLE)
    for tr in translators:
        out.append('    {%#018xull, %d, %s%d, kVariants%d, %d},' % (code_hash(tr.words[:tr.max_used + 1]), tr.max_used + 1, NAME,
                                                                         tr.index, tr.index, len(tr.variants)))
    out.append('};')
    out.append('const u32 g_ProgramCount%s = %d;' % (TABLE, len(translators)))
    # Each program's address in the retail executable's .vutext (VU0's sets: 0), for the logs (THE LIST 74's split by kind)
    out.append('const u32 g_ProgramAddresses%s[] = {' % TABLE)
    for tr in translators:
        out.append('    %#x,' % (VUTEXT_ADDRESS + tr.start * 8 if tr.start is not None else 0))
    out.append('};')
    out.append('}')
    write_if_changed(output, '\n'.join(out) + '\n')
    total = sum(len(tr.variants) for tr in translators)
    instructions = sum(tr.size for tr in translators)
    print('vu1_translate: %d programs, %d instructions, %d variants' % (len(translators), instructions, total))


def main():
    root = Path(__file__).resolve().parents[2]
    if len(sys.argv) > 1 and sys.argv[1] == '--disassemble':
        for index, (start, words) in enumerate(read_programs(root)):
            print('program %d at %#x (%d instructions)' % (index, VUTEXT_ADDRESS + start * 8, len(words)))
            for o, w in enumerate(words):
                print('  %04x  %s' % (o, decode(o, w).text()))
        return
    args = sys.argv[1:]
    if args and args[0] == '--vu0':
        # VU0's micro mode: 4 KB of code (512 instructions) and of data
        global VU0, CODE, DATA, NAME, TABLE, SHARDS
        VU0, CODE, DATA, NAME, TABLE, SHARDS = True, '0x1FF', '0x0FF0', 'Vu0Program', 'Vu0', 2
        args = args[1:]
    if len(args) != 1:
        raise SystemExit(__doc__)
    generate(root, args[0])


if __name__ == '__main__':
    main()
