"""Core analysis: segments, function boundaries, address references, strings.

Everything is heuristic but conservative. The main ideas:
  * The program starts with .text; it ends where `jr $ra` stops appearing.
  * $gp and the BSS range are read from the start-up code at the entry point.
  * References are recovered by tracking constants through registers in a
    linear sweep (lui/addiu/ori pairs, $gp-relative accesses, register moves).
  * Function starts are jal targets, the entry point, code addresses taken in
    code or data that sit on a function boundary, and stack-frame prologues
    that follow a `jr $ra` epilogue.
"""
import bisect
import re
import struct

from .mips import Insn, JR_RA

TEMP_REGS = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25]

STR_RE = re.compile(rb"(?:[\x20-\x7e\t\n\r]|[\x81-\x9f\xe0-\xfc][\x40-\xfc]){4,}\x00")


class Function:
    __slots__ = ("start", "end", "name", "lib", "calls", "callers", "refs", "tags", "kind")

    def __init__(self, start):
        self.start = start
        self.end = start
        self.name = None
        self.lib = None      # SDK/runtime library label, None for game code
        self.calls = set()
        self.callers = set()
        self.refs = set()    # data/string addresses referenced
        self.tags = set()
        self.kind = None

    def label(self):
        return self.name or ("fn_%08X" % self.start)


class Ref:
    __slots__ = ("frm", "to", "kind", "size")

    def __init__(self, frm, to, kind, size=0):
        self.frm = frm
        self.to = to
        self.kind = kind  # 'addr' (address formed), 'load', 'store', 'data' (pointer in data)
        self.size = size


class Analysis:
    def __init__(self, prog, text_end=None, log=print):
        self.p = prog
        self.log = log
        n = len(prog.data) // 4
        self.words = struct.unpack("<%dI" % n, prog.data[:n * 4])
        self.text_start = prog.base
        self.text_end = text_end or self._find_text_end()
        self.gp, self.bss_start, self.bss_end = self._startup_info()
        self.mem_end = max(self.bss_end, prog.end)
        self.insns = {}
        self.refs = []           # Ref objects
        self.refs_to = {}        # to -> [Ref]
        self.constants = []      # (insn addr, 32-bit value) for non-address immediates built from lui
        self.functions = {}      # start -> Function
        self._starts = []
        self.strings = {}        # addr -> str
        self.data_ptrs = {}      # data addr -> pointer value

    # ------------------------------------------------------------------ helpers
    def word(self, addr):
        return self.words[(addr - self.p.base) >> 2]

    def in_text(self, a):
        return self.text_start <= a < self.text_end

    def in_mem(self, a):
        return self.p.base <= a < self.mem_end

    def insn(self, addr):
        i = self.insns.get(addr)
        if i is None:
            i = Insn(addr, self.word(addr))
        return i

    def func_at(self, addr):
        """Function containing addr, or None."""
        k = bisect.bisect_right(self._starts, addr) - 1
        if k < 0:
            return None
        f = self.functions[self._starts[k]]
        return f if addr < f.end else None

    # ------------------------------------------------------------------ segments
    def _find_text_end(self):
        base = self.p.base
        blk = 0x4000
        last_jr = None
        for b in range(0, len(self.words) * 4, blk):
            seg = self.words[b // 4:(b + blk) // 4]
            idx = [k for k, w in enumerate(seg) if w == JR_RA]
            if not idx:
                break
            last_jr = base + b + idx[-1] * 4
        end = last_jr + 8
        while end < self.p.end and self.word(end) == 0 and (end & 0xF):
            end += 4
        return end

    def _startup_info(self):
        """Read $gp and the BSS clear range from the start-up code."""
        gp = bss_s = bss_e = None
        regs = {}
        a = self.p.entry
        for _ in range(48):
            if not self.p.contains(a):
                break
            i = Insn(a, self.word(a))
            if i.kind == Insn.LUI:
                regs[i.rt] = i.imm << 16
            elif i.kind == Insn.ADDI and i.rs in regs:
                regs[i.rt] = (regs[i.rs] + i.simm) & 0xFFFFFFFF
            elif i.kind == Insn.ORI and i.rs in regs:
                regs[i.rt] = regs[i.rs] | i.imm
            elif i.op == 0x1F and bss_s is None and i.rs in regs:  # sq zero, 0(v0): BSS clear loop
                bss_s = regs[i.rs]
                bss_e = regs.get(3)
            elif i.kind == Insn.MOVE and i.rd == 28 and i.rs in regs:
                gp = regs[i.rs]
            a += 4
        if bss_s is None:
            bss_s = bss_e = (self.p.end + 15) & ~15
        return gp, bss_s, bss_e

    # ------------------------------------------------------------------ passes
    def run(self):
        self.log("text 0x%08X-0x%08X, data -0x%08X, bss 0x%08X-0x%08X, gp 0x%08X" % (
            self.text_start, self.text_end, self.p.end, self.bss_start, self.bss_end, self.gp or 0))
        self._decode()
        self._sweep_refs()
        self._scan_data_pointers()
        self._scan_eh_frame()
        self._find_functions()
        self._attribute()
        self._scan_strings()
        return self

    def _decode(self):
        base = self.p.base
        for k in range((self.text_end - base) >> 2):
            a = base + k * 4
            self.insns[a] = Insn(a, self.words[k])

    def _add_ref(self, frm, to, kind, size=0):
        r = Ref(frm, to, kind, size)
        self.refs.append(r)
        self.refs_to.setdefault(to, []).append(r)

    def _sweep_refs(self):
        """Linear constant propagation over .text."""
        val = {}
        if self.gp is not None:
            val[28] = self.gp
        pending_clear = False
        reset_after = False
        a = self.text_start
        in_mem = self.in_mem
        while a < self.text_end:
            i = self.insns[a]
            k = i.kind
            if k == Insn.LUI:
                val[i.rt] = (i.imm << 16)
            elif k == Insn.ADDI or k == Insn.ORI:
                if i.rs == 0:
                    val[i.rt] = i.imm if k == Insn.ORI else (i.simm & 0xFFFFFFFF)
                elif i.rs in val:
                    v = (val[i.rs] + i.simm) & 0xFFFFFFFF if k == Insn.ADDI else (val[i.rs] | i.imm)
                    if i.rt != 0:
                        val[i.rt] = v
                    if in_mem(v):
                        self._add_ref(a, v, "addr")
                    elif i.rs != 28:
                        self.constants.append((a, v))
                else:
                    val.pop(i.rt, None)
            elif k == Insn.LOAD or k == Insn.STORE:
                if i.rs in val:
                    v = (val[i.rs] + i.simm) & 0xFFFFFFFF
                    if in_mem(v):
                        self._add_ref(a, v, "load" if k == Insn.LOAD else "store", i.mem)
                if k == Insn.LOAD:
                    w = i.writes_reg()
                    if w is not None:
                        val.pop(w, None)
            elif k == Insn.MOVE:
                if i.rs in val:
                    val[i.rd] = val[i.rs]
                else:
                    val.pop(i.rd, None)
            else:
                w = i.writes_reg()
                if w is not None and w != 0:
                    val.pop(w, None)
            if pending_clear:
                for r in TEMP_REGS:
                    val.pop(r, None)
                pending_clear = False
            if reset_after:
                val = {28: self.gp} if self.gp is not None else {}
                reset_after = False
            if k in (Insn.JAL, Insn.JALR):
                pending_clear = True
            elif i.word == JR_RA:
                reset_after = True
            if val.get(28) != self.gp and self.gp is not None:
                val[28] = self.gp
            a += 4

    def _scan_data_pointers(self):
        base = self.p.base
        start = (self.text_end + 3) & ~3
        lo, hi = base, self.mem_end
        for k in range((start - base) >> 2, len(self.words)):
            w = self.words[k]
            if lo <= w < hi:
                a = base + k * 4
                self.data_ptrs[a] = w
                self._add_ref(a, w, "data")

    def _scan_eh_frame(self):
        """Find DWARF .eh_frame FDEs (GCC emits them for C++ code): exact function ranges."""
        base = self.p.base
        w = self.words
        k0 = (((self.text_end + 3) & ~3) - base) >> 2
        cies = set()
        for k in range(k0, len(w) - 3):
            ln = w[k]
            if 8 <= ln < 0x100 and w[k + 1] == 0 and (w[k + 2] & 0xFF) == 1:
                cies.add(base + k * 4)
        fdes = {}
        for k in range(k0, len(w) - 4):
            ln, cp, beg, rng = w[k], w[k + 1], w[k + 2], w[k + 3]
            if not (12 <= ln < 0x1000) or cp == 0 or cp & 3:
                continue
            a = base + k * 4
            if (a + 4 - cp) not in cies:
                continue
            if self.in_text(beg) and 0 < rng < 0x40000 and not (rng & 3) and beg + rng <= self.text_end:
                fdes[beg] = beg + rng
        self.fdes = fdes
        self.cies = cies

    def _is_boundary(self, a):
        """True if `a` follows an epilogue (jr ra + delay slot + nop padding) or the text start."""
        if a == self.text_start:
            return True
        b = a - 4
        while b >= self.text_start and self.word(b) == 0 and (a - b) <= 64:
            b -= 4
        # b is the delay slot of the jr ra (or a j / data word); check b-4
        return b - 4 >= self.text_start and self.word(b - 4) in (JR_RA,) or \
            (b - 4 >= self.text_start and (self.word(b - 4) >> 26) == 0x02)  # tail jump `j`

    def _is_prologue(self, a):
        i = self.insns.get(a)
        if i is None:
            return False
        return i.op in (0x09, 0x19) and i.rs == 29 and i.rt == 29 and i.simm < 0

    def _find_functions(self):
        starts = {self.p.entry}
        jal_targets = set()
        j_targets = set()
        for a, i in self.insns.items():
            if i.kind == Insn.JAL and self.in_text(i.target):
                jal_targets.add(i.target)
            elif i.kind == Insn.J and self.in_text(i.target):
                j_targets.add(i.target)
        starts |= jal_targets
        starts |= set(self.fdes)
        for e in self.fdes.values():  # code right after an FDE range starts a new function
            b = e
            while b < self.text_end and self.word(b) == 0:
                b += 4
            if b < self.text_end:
                starts.add(b)
        taken = set()
        for r in self.refs:
            if r.kind in ("addr", "data") and self.in_text(r.to) and not (r.to & 3):
                taken.add(r.to)
        for t in taken | j_targets:
            if t in starts:
                continue
            if self._is_boundary(t) or (self._is_prologue(t) and self._is_boundary(t)):
                starts.add(t)
        # prologues after epilogues
        for a, i in self.insns.items():
            if a not in starts and i.op in (0x09, 0x19) and i.rs == 29 and i.rt == 29 and i.simm < 0:
                if self._is_boundary(a):
                    starts.add(a)
        # leading code at text start
        starts.add(self.text_start)
        ss = sorted(s for s in starts if self.in_text(s))
        # Skip leading nops of a "function" that is only padding
        self.functions = {}
        for n, s in enumerate(ss):
            e = ss[n + 1] if n + 1 < len(ss) else self.text_end
            f = Function(s)
            if s in self.fdes and self.fdes[s] <= e:
                e = self.fdes[s]
                f.tags.add("eh")
            # trim trailing padding
            while e > s + 4 and self.word(e - 4) == 0 and self.word(e - 8) != JR_RA and \
                    not ((self.word(e - 8) >> 26) in (0x02, 0x03)):
                e -= 4
            f.end = e
            self.functions[s] = f
        self._starts = sorted(self.functions)
        self.jal_targets = jal_targets
        self.taken_code = taken

    def _attribute(self):
        for a, i in self.insns.items():
            if i.kind == Insn.JAL:
                f = self.func_at(a)
                g = self.functions.get(i.target)
                if f and g:
                    f.calls.add(g.start)
                    g.callers.add(f.start)
            elif i.kind == Insn.J and i.target in self.functions:
                f = self.func_at(a)
                g = self.functions[i.target]
                if f and g and f is not g:
                    f.calls.add(g.start)
                    g.callers.add(f.start)
        for r in self.refs:
            if r.kind != "data":
                f = self.func_at(r.frm)
                if f:
                    f.refs.add(r.to)

    def _scan_strings(self):
        data = self.p.data
        base = self.p.base
        off0 = self.text_end - base
        for m in STR_RE.finditer(data, off0):
            s = m.start()
            raw = m.group()[:-1]
            self.strings[base + s] = _decode_str(raw)
        # A reference into the middle of a scanned string means the scan started too early
        # (e.g. the tail byte of a float such as 1.0f = 00 00 80 3F looks like '?').
        starts = sorted(self.strings)
        for to in list(self.refs_to):
            if to in self.strings or not (self.text_end <= to < self.p.end):
                continue
            k = bisect.bisect_right(starts, to) - 1
            if k < 0:
                continue
            s0 = starts[k]
            if s0 not in self.strings:
                continue
            if to < data.find(b"\0", s0 - base) + base and s0 not in self.refs_to:
                o = to - base
                e = data.find(b"\0", o)
                del self.strings[s0]
                self.strings[to] = _decode_str(data[o:e])
        # Short strings that are referenced explicitly
        for to in list(self.refs_to):
            if to in self.strings or not (self.text_end <= to < self.p.end):
                continue
            o = to - base
            if o > 0 and data[o - 1] != 0:
                continue
            e = data.find(b"\0", o, o + 256)
            if e <= o:
                continue
            raw = data[o:e]
            if all(0x20 <= c < 0x7F or c in (9, 10, 13) for c in raw):
                self.strings[to] = _decode_str(raw)

    def split_at(self, addr):
        """Make addr a function start, splitting the function that contains it. Returns the new Function."""
        if addr in self.functions:
            return self.functions[addr]
        f = self.func_at(addr)
        if f is None or not self.in_text(addr) or addr & 3:
            return None
        g = Function(addr)
        g.end = f.end
        f.end = addr
        f.tags.discard("eh")
        self.functions[addr] = g
        self._starts = sorted(self.functions)
        return g

    def reattribute(self):
        """Recompute calls/callers/refs after function boundaries changed."""
        for f in self.functions.values():
            f.calls.clear()
            f.callers.clear()
            f.refs.clear()
        self._attribute()

    # ------------------------------------------------------------------ per-function tracking
    def walk(self, f, visit):
        """Linear walk of function f with constant tracking; visit(insn, vals) is called *before*
        each instruction executes, except that a call's delay slot is applied first, so for
        JAL/JALR visit sees the argument registers as the callee will."""
        val = {28: self.gp} if self.gp is not None else {}
        a = f.start
        while a < f.end:
            i = self.insns[a]
            if i.kind in (Insn.JAL, Insn.JALR) and a + 4 < f.end:
                d = self.insns[a + 4]
                self._apply(d, val)
                visit(i, val)
                for r in TEMP_REGS:
                    val.pop(r, None)
                a += 8
                continue
            visit(i, val)
            self._apply(i, val)
            a += 4

    @staticmethod
    def _apply(i, val):
        k = i.kind
        if k == Insn.LUI:
            val[i.rt] = i.imm << 16
        elif k == Insn.ADDI or k == Insn.ORI:
            if i.rs == 0:
                val[i.rt] = i.imm if k == Insn.ORI else (i.simm & 0xFFFFFFFF)
            elif i.rs in val:
                val[i.rt] = (val[i.rs] + i.simm) & 0xFFFFFFFF if k == Insn.ADDI else (val[i.rs] | i.imm)
            else:
                val.pop(i.rt, None)
        elif k == Insn.MOVE:
            if i.rs in val:
                val[i.rd] = val[i.rs]
            else:
                val.pop(i.rd, None)
        else:
            w = i.writes_reg()
            if w:
                val.pop(w, None)

    def call_sites(self, f):
        """[(call addr, target or None, {reg: value})] for every call in f (args a0-a3, t0-t3)."""
        out = []

        def visit(i, val):
            if i.kind == Insn.JAL:
                out.append((i.addr, i.target, {r: val[r] for r in (4, 5, 6, 7, 8, 9, 10, 11) if r in val}))
            elif i.kind == Insn.JALR:
                out.append((i.addr, val.get(i.rs), {r: val[r] for r in (4, 5, 6, 7, 8, 9, 10, 11) if r in val}))
        self.walk(f, visit)
        return out

    # ------------------------------------------------------------------ queries
    def string_at(self, addr):
        return self.strings.get(addr)

    def code_refs_to(self, addr):
        return [r for r in self.refs_to.get(addr, []) if r.kind != "data"]

    def data_refs_to(self, addr):
        return [r for r in self.refs_to.get(addr, []) if r.kind == "data"]

    def funcs_referencing(self, addr, via_data=True, depth=2):
        """Functions referencing addr directly, or through pointer tables in data (up to `depth` levels).

        Returns list of (function, path) where path is a list of data addresses traversed.
        """
        out = []
        seen = set()

        def visit(target, path, d):
            for r in self.refs_to.get(target, []):
                if r.kind != "data":
                    f = self.func_at(r.frm)
                    if f and (f.start, tuple(path)) not in seen:
                        seen.add((f.start, tuple(path)))
                        out.append((f, list(path)))
                elif via_data and d > 0:
                    holder = self.enclosing_table(r.frm)
                    if holder is not None and holder not in path:
                        visit(holder, path + [holder], d - 1)

        visit(addr, [], depth)
        return out

    def enclosing_table(self, data_addr, max_back=0x2000):
        """Start of the code-referenced data object that contains data_addr (heuristic)."""
        a = data_addr
        lo = max(self.text_end, data_addr - max_back)
        while a >= lo:
            rs = self.refs_to.get(a)
            if rs and any(r.kind != "data" for r in rs):
                return a
            a -= 4
        return None


def _decode_str(raw):
    if all(c < 0x80 for c in raw):
        return raw.decode("ascii")
    try:
        return raw.decode("cp932")
    except UnicodeDecodeError:
        return raw.decode("latin-1")
