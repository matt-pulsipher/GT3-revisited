#!/usr/bin/env python3
"""Match the functions of two or more builds of the engine whatever compiler built them, and say what differs.

Usage:
    python3 -I tools/re/build_map.py <ref> <other> [<later> ...] --out DIR [options]

    <ref> <other> ...       disc images or unpacked programs. With more than two, the builds form a chain
                            (ref -> other -> later -> ...): each step is matched on its own and the maps are composed,
                            so builds of one compiler are followed through their code and only the step between two
                            compilers relies on the compiler-independent matcher.
    --out DIR               where the tables are written (outside the repository)
    --names A,B,...         short names for the builds (default: b0, b1, ...)
    --validate              two builds of one compiler: match them without their code and score the result against
                            version_map.py's matcher (precision / recall); --ablate also scores each rule left out
    --addr ADDR ...         print where these addresses of <ref> are in the last build (a function start, or an
                            instruction through the aligned code)
    --data ADDR:SIZE[:ENTRY] ...
                            say where a table of <ref> (SIZE bytes at ADDR, entries of ENTRY bytes, default 4) is in the
                            last build, through the matched functions that address it, and how many entries differ
    --struct FUNC:ARG ...   print the field offsets of the struct handed to that argument of <ref>'s function FUNC
                            (0 = the first argument), as the last build has them

version_map.py matches two programs by their code with the relocations masked. That needs the same compiler: another
compiler's output shares almost no instruction sequences. This script matches by what a compiler keeps:

  seeds        the same class's virtual table slot (tables as long in both); a string only one function of each build
               refers to; code that is identical after masking (the few functions another compiler leaves alone)
  propagation  the callees at aligned positions of two matched functions' call sequences; unmatched functions that call
               the same matched functions (two or more that few others call, and most of what either calls); virtual
               tables of different lengths aligned by their matched slots; the only unmatched function of each build
               that refers to the same matched global; the unmatched functions between two matched neighbours when
               both builds have as many (link order). A pair is taken when each is the other's best candidate, the
               pairs that look most alike first (sizes, float operations, constants, strings, call counts); a pair
               with nothing in common is refused, except, at the very end, the one open place of two otherwise equal
               call sequences (a routine rewritten or emptied).

A matched pair is compared by what the source says rather than by its code: the counts of float operations (multiply,
add/subtract, divide, square root, compare, min/max; a multiply-add counts as both), VU0 macro operations, the set of
constants (immediates that are not addresses, and float literals loaded from fixed places, so a build that loads a
literal and one that builds it inline compare equal), the calls after mapping and the strings. Two builds of one
compiler are also compared by their masked code. The classes: same / layout only (only field offsets differ) /
calls only / constants / integer or control flow edit / small arithmetic edit / larger change / not found.

For the layout, each matched pair's memory accesses are aligned (calls first, then the accesses between two calls by
kind) and every access through a pointer the function was given votes "offset X here is offset Y there". The pointers
are followed through the calls, so the votes of all functions working on the same struct add up in one table.

Run with -I. Reads only the discs; writes only to --out. See docs/dev/RE_TOOLKIT.md.
"""
import argparse
import bisect
import collections
import difflib
import os
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import version_map as vm  # noqa: E402
from gt3re.mips import LOADS, STORES, Insn  # noqa: E402

# COP1 single-precision functions -> the operation counted.
FP_OPS = {0x00: "add", 0x01: "sub", 0x02: "mul", 0x03: "div", 0x04: "sqrt", 0x05: "abs", 0x07: "neg", 0x16: "rsqrt",
          0x18: "add", 0x19: "sub", 0x1A: "mul", 0x1C: "madd", 0x1D: "msub", 0x1E: "madd", 0x1F: "msub",
          0x28: "max", 0x29: "min", 0x30: "cmp", 0x32: "cmp", 0x34: "cmp", 0x36: "cmp", 0x24: "cvtw"}
ARITH = ("mul", "addsub", "div", "sqrt", "cmp", "minmax", "vu")
CLASSES = ("same", "layout only", "calls only", "constants", "integer/control edit", "small arithmetic edit",
           "larger change", "not found")
ARG_REGS = (4, 5, 6, 7, 8, 9, 10, 11)
CALLER_SAVED = (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25)
ALL_RULES = ("vtable", "string", "exact", "calls", "callees", "vtable-align", "global", "order", "veto",
             "position")


def as_float(v):
    return struct.unpack("<f", struct.pack("<I", v & 0xFFFFFFFF))[0]


def float_like(v):
    """True for the bits of a float of ordinary size (about 1e-9 .. 1e12): not an integer, mask or address."""
    return 0x50 <= (v >> 23) & 0xFF <= 0xA6


def const_text(v):
    """A constant for the tables: a float when its bits are an ordinary one, else hex."""
    return "%.7g" % as_float(v) if float_like(v) else "0x%X" % v


class Feat:
    """What a compiler keeps of a function."""
    __slots__ = ("size", "calls", "jalr", "fp", "vu", "consts", "ints", "strs", "globs", "branches")

    def arith(self):
        fp = self.fp
        return (fp["mul"], fp["add"] + fp["sub"], fp["div"], fp["sqrt"] + fp["rsqrt"], fp["cmp"], fp["max"] + fp["min"],
                self.vu)


class Build(vm.Prog):
    """A program with the features of its functions."""

    def __init__(self, path, name=None):
        super().__init__(path)
        A, R = self.analysis, self.rtti
        self.name = name or os.path.basename(path)
        self.path = path
        self.mem_end = A.mem_end
        self.strings = {a: s for a, s in A.strings.items() if len(s) >= 4}
        self.fnames = {s: f.label() for s, f in A.functions.items()}
        self.libs = {s: f.lib for s, f in A.functions.items() if f.lib}
        self.vt = {}
        count = collections.Counter()
        for va in sorted(R.vtables):
            v = R.vtables[va]
            self.vt[(v.cls.name, count[v.cls.name])] = [e[2] for e in v.entries]
            count[v.cls.name] += 1
        slots = collections.Counter(x for t in self.vt.values() for x in t[1:] if x)
        self.pure = slots.most_common(1)[0][0] if slots else 0  # the pure virtual handler: the most common slot
        self.feat = {s: self._features(s, e) for s, e in self.funcs.items()}
        self.callers = collections.defaultdict(set)
        for s, f in self.feat.items():
            for t in f.calls:
                self.callers[t].add(s)
        self.glob_users = collections.defaultdict(set)
        for s, f in self.feat.items():
            for g in f.globs:
                self.glob_users[g].add(s)

    def in_mem(self, v):
        return self.base <= v < self.mem_end

    def _features(self, s, e):
        f = Feat()
        f.size, f.calls, f.jalr, f.fp, f.vu = e - s, [], 0, collections.Counter(), 0
        f.consts, f.ints, f.strs, f.globs, f.branches = set(), set(), set(), set(), 0
        val = {}    # register -> (the value its lui / ori / addiu built, True while it is a lui alone)
        small = {}  # register -> a small integer loaded into it
        fint = {}   # float register -> an integer waiting for its conversion to a float
        refs, word, in_mem, insns = self.refs, self.word, self.in_mem, self.analysis.insns
        data_lo, data_hi = self.text_end, self.bss[0]

        def settle(r):  # a built value that is used as it is: a constant unless it is an address
            v, hi_only = val.pop(r)
            if in_mem(v):
                return
            if float_like(v):
                f.consts.add(v)
            elif v >= 0x10000 and not hi_only:
                f.ints.add(v)

        for a in range(s, e, 4):
            i = insns[a]
            w, op, rs, rt, k = i.word, i.op, i.rs, i.rt, i.kind
            for to, kind in refs.get(a, ()):
                if to in self.strings:
                    f.strs.add(self.strings[to])
                elif to >= data_lo:
                    f.globs.add(to)
                    if op == 0x31 and kind == "load" and to < data_hi - 3:  # lwc1 from a fixed place: a literal
                        v = word(to & ~3)
                        if float_like(v):
                            f.consts.add(v)
                        elif v < 0x10000 or v >= 0xFFFF0000:
                            fint[rt] = v - (v >> 31 << 32)
            if k in (Insn.JAL, Insn.J):
                t = i.target
                if t in self.funcs and t != s and not s < t < e:
                    f.calls.append(t)
            elif k == Insn.LUI:
                if rt in val:
                    settle(rt)
                small.pop(rt, None)
                val[rt] = ((w & 0xFFFF) << 16, True)
                continue
            elif k in (Insn.ADDI, Insn.ORI):
                if rs in val:
                    v = val[rs][0] | (w & 0xFFFF) if k == Insn.ORI else (val[rs][0] + i.simm) & 0xFFFFFFFF
                    if rt != rs and rt in val:
                        settle(rt)
                    val.pop(rs, None)
                    val[rt] = (v, False)
                    small.pop(rt, None)
                    continue
                if rs == 0:
                    if rt in val:
                        settle(rt)
                    small[rt] = (w & 0xFFFF) if k == Insn.ORI else i.simm
                    continue
            elif k in (Insn.LOAD, Insn.STORE):
                val.pop(rs, None)  # an address
                if k == Insn.STORE and rt in val and op not in (0x39, 0x3E):
                    settle(rt)     # a constant stored as it is
            elif k == Insn.MOVE:
                src = val.pop(rs, None)
                if i.rd in val:
                    settle(i.rd)
                if src:
                    val[i.rd] = src
                small.pop(i.rd, None)
                continue
            elif op == 0x11:
                fs, fn = i.rd, w & 0x3F
                if rs == 16:
                    n = FP_OPS.get(fn)
                    if n in ("madd", "msub"):
                        f.fp["mul"] += 1
                        f.fp["add" if n == "madd" else "sub"] += 1
                    elif n:
                        f.fp[n] += 1
                elif rs == 8:
                    f.branches += 1
                elif rs == 4:  # mtc1: a float built inline, or an integer on its way to one
                    if rt in val:
                        settle(rt)
                    elif rt in small:
                        fint[fs] = small[rt]
                    continue
                elif rs == 0x14 and fn == 0x20 and fs in fint:  # cvt.s.w
                    v = fint.pop(fs)
                    if v:
                        f.consts.add(struct.unpack("<I", struct.pack("<f", float(v)))[0])
            elif op == 0x12 and (w >> 25) & 1:
                f.vu += 1
            elif k == Insn.BRANCH:
                f.branches += 1
            elif k == Insn.JALR:
                f.jalr += 1
            elif op == 0 and i.funct in (0x20, 0x21, 0x22, 0x23, 0x2C, 0x2D, 0x2E, 0x2F):
                val.pop(rs, None)  # added to something: an offset or an address, not a value of the source
                val.pop(rt, None)
            # whatever else reads a built value uses it as a constant; a register written loses what it held
            if op == 0 and k not in (Insn.JR, Insn.JALR):
                for r in (rs, rt):
                    if r in val:
                        settle(r)
            elif op in (0x08, 0x0A, 0x0B, 0x0C, 0x0E, 0x18, 0x1C) or k == Insn.BRANCH:
                for r in (rs, rt):
                    if r in val:
                        settle(r)
            wr = i.writes_reg()
            if wr is not None:
                if wr in val:
                    settle(wr)
                small.pop(wr, None)
            if k in (Insn.JAL, Insn.JALR):
                for r in CALLER_SAVED:
                    if r in val:
                        settle(r)
                    small.pop(r, None)
        for r in list(val):
            settle(r)
        return f

    def func_of(self, a):
        i = bisect.bisect_right(self.starts, a) - 1
        if i >= 0 and a < self.funcs[self.starts[i]]:
            return self.starts[i]
        return None


def vm_sext(w):
    return ((w & 0xFFFF) ^ 0x8000) - 0x8000


# ---------------------------------------------------------------------------------------------------------- matching
def similarity(fa, fb):
    """0 .. 1: how alike two functions' features are (sizes, float operations, constants, strings, call counts)."""
    r = min(fa.size, fb.size) / max(fa.size, fb.size, 4)
    xa, xb = fa.arith(), fb.arith()
    ta, tb = sum(xa), sum(xb)
    if ta or tb:
        fp = 1.0 - sum(abs(x - y) for x, y in zip(xa, xb)) / max(ta + tb, 1)
    else:
        fp = 1.0
    ca, cb = fa.consts, fb.consts
    cs = len(ca & cb) / len(ca | cb) if ca or cb else 1.0
    na, nb = len(fa.calls) + fa.jalr, len(fb.calls) + fb.jalr
    nc = 1.0 - abs(na - nb) / max(na + nb, 1)
    st = len(fa.strs & fb.strs) / len(fa.strs | fb.strs) if fa.strs or fb.strs else 1.0
    return 0.3 * r + 0.25 * fp + 0.2 * cs + 0.15 * nc + 0.1 * st


def implausible(fa, fb):
    """True for two functions that share nothing a compiler keeps (the veto of a pair found by position alone)."""
    big, small = max(fa.size, fb.size), min(fa.size, fb.size)
    if big >= 64 and small * 4 < big:
        return True
    ta, tb = sum(fa.arith()), sum(fb.arith())
    if (ta >= 6 or tb >= 6) and (min(ta, tb) * 3 < max(ta, tb)):
        return True
    if len(fa.consts) >= 3 and len(fb.consts) >= 3 and not fa.consts & fb.consts:
        return True
    return False


class SemMatcher:
    """Functions of A -> functions of B by vtable slots, strings, call sequences, globals and link order.

    fmap: A start -> B start; how: A start -> the rule that found it; score: A start -> the votes it had."""

    def __init__(self, A, B, seeds=None, rules=ALL_RULES, log=None, tiers=(0.97, 0.9, 0.8, 0.65, 0.5, 0.0),
                 callee_rarity=1.0, hints=None):
        self.A, self.B = A, B
        self.rules = set(rules)
        self.tiers = tiers
        self.callee_rarity = callee_rarity
        self.hints = hints or {}
        self.fmap, self.how, self.score = {}, {}, {}
        self.taken = set()
        self.log = log or (lambda *a: None)
        for a, b in (seeds or {}).items():
            self._add(a, b, "code", 99)
        self._seed()
        self._propagate()

    def _add(self, a, b, how, score):
        if a in self.fmap or b in self.taken or a not in self.A.funcs or b not in self.B.funcs:
            return False
        self.fmap[a] = b
        self.how[a] = how
        self.score[a] = score
        self.taken.add(b)
        return True

    def _seed(self):
        A, B = self.A, self.B
        if "exact" in self.rules:
            ka, kb = A.seqs(False), B.seqs(False)
            ha, hb = collections.defaultdict(list), collections.defaultdict(list)
            for s, q in ka.items():
                ha[q].append(s)
            for s, q in kb.items():
                hb[q].append(s)
            for q, la in ha.items():
                if len(la) == 1 and len(hb.get(q, ())) == 1 and len(q) >= 6:
                    self._add(la[0], hb[q][0], "exact", 50)
        if "vtable" in self.rules:
            for key, ta in A.vt.items():
                tb = B.vt.get(key)
                if tb and len(ta) == len(tb):
                    for x, y in zip(ta, tb):
                        if x and y and x != A.pure and y != B.pure:
                            self._add(x, y, "vtable", 20)
        if "string" in self.rules:
            sa, sb = collections.defaultdict(set), collections.defaultdict(set)
            for s, f in A.feat.items():
                for t in f.strs:
                    sa[t].add(s)
            for s, f in B.feat.items():
                for t in f.strs:
                    sb[t].add(s)
            votes = collections.Counter()
            for t, fa in sa.items():
                fb = sb.get(t)
                if fb and len(fa) == 1 and len(fb) == 1:
                    votes[(next(iter(fa)), next(iter(fb)))] += 1
            for (a, b), n in votes.most_common():
                if "veto" not in self.rules or not implausible(A.feat[a], B.feat[b]):
                    self._add(a, b, "string", 10 * n)

    def _call_votes(self, votes):
        A, B, fmap, taken = self.A, self.B, self.fmap, self.taken
        self.positional = {}  # (a's callee, b's callee) alone unmatched at the same place of otherwise equal sequences
        for a, b in fmap.items():
            ca, cb = A.feat[a].calls, B.feat[b].calls
            if not ca or not cb:
                continue
            ta = [fmap.get(x, "?") for x in ca]
            if "?" not in ta:
                continue
            tb = [y if y in taken else "?" for y in cb]
            if "calls" in self.rules:
                if len(ta) == len(tb):
                    ok = sum(1 for x, y in zip(ta, tb) if x == y)
                    strict = all(x == y or (x == "?" and y == "?") for x, y in zip(ta, tb))
                    if all(x == y or x == "?" or y == "?" for x, y in zip(ta, tb)) or ok * 2 >= len(ta):
                        open_ = [(ca[i], cb[i]) for i, (x, y) in enumerate(zip(ta, tb)) if x == "?" and y == "?"]
                        for pair in open_:
                            votes[pair]["calls"] += 2
                        if strict and len(set(open_)) == 1:
                            self.positional[open_[0]] = a
                        continue
                sm = difflib.SequenceMatcher(a=ta, b=tb, autojunk=False)
                for blk in sm.get_matching_blocks():
                    for k in range(blk.size):
                        if ta[blk.a + k] == "?":
                            votes[(ca[blk.a + k], cb[blk.b + k])]["calls"] += 1

    def _callee_votes(self, votes):
        """Unmatched functions that call the same matched functions: at least two that few others call, and most of
        what either calls."""
        A, B, fmap, taken = self.A, self.B, self.fmap, self.taken
        inv = collections.defaultdict(set)
        for y, f in B.feat.items():
            if y not in taken:
                for c in set(f.calls):
                    if c in taken:
                        inv[c].add(y)
        for x, f in A.feat.items():
            if x in fmap:
                continue
            mine = {fmap[c] for c in set(f.calls) if c in fmap}
            if len(mine) < 2:
                continue
            shared = collections.defaultdict(list)
            for t in mine:
                for y in inv.get(t, ()):
                    shared[y].append(t)
            for y, cs in shared.items():
                theirs = {c for c in set(B.feat[y].calls) if c in taken}
                rare = sum(1.0 / len(B.callers[c]) for c in cs)
                if len(cs) >= 2 and len(cs) * 2 >= len(mine | theirs) and rare >= self.callee_rarity:
                    votes[(x, y)]["callees"] += min(len(cs), 4)

    def _vtable_votes(self, votes):
        A, B, fmap, taken = self.A, self.B, self.fmap, self.taken
        for key, va in A.vt.items():
            vb = B.vt.get(key)
            if not vb or len(va) == len(vb):
                continue
            ta = ["PV" if x in (0, A.pure) else fmap.get(x, "?") for x in va]
            tb = ["PV" if y in (0, B.pure) else (y if y in taken else "?") for y in vb]
            if sum(1 for t in ta if t not in ("?", "PV")) < 2:
                continue
            sm = difflib.SequenceMatcher(a=ta, b=tb, autojunk=False)
            for blk in sm.get_matching_blocks():
                for k in range(blk.size):
                    if ta[blk.a + k] == "?":
                        votes[(va[blk.a + k], vb[blk.b + k])]["vtable-align"] += 2

    def _global_map(self):
        """Globals of A -> globals of B: referred to by the same (matched) functions, and by no other pair's."""
        A, B, fmap, taken = self.A, self.B, self.fmap, self.taken
        sig_a, sig_b = collections.defaultdict(list), collections.defaultdict(list)
        for g, users in A.glob_users.items():
            m = frozenset(fmap[u] for u in users if u in fmap)
            if len(m) >= 2:
                sig_a[m].append(g)
        for g, users in B.glob_users.items():
            m = frozenset(u for u in users if u in taken)
            if len(m) >= 2:
                sig_b[m].append(g)
        return {ga[0]: sig_b[m][0] for m, ga in sig_a.items() if len(ga) == 1 and len(sig_b.get(m, ())) == 1}

    def _global_votes(self, votes):
        A, B, fmap, taken = self.A, self.B, self.fmap, self.taken
        for g, h in self._global_map().items():
            ua = [u for u in A.glob_users[g] if u not in fmap]
            ub = [u for u in B.glob_users[h] if u not in taken]
            if len(ua) == 1 and len(ub) == 1:
                votes[(ua[0], ub[0])]["global"] += 1

    def _order_votes(self, votes):
        A, B, fmap, taken = self.A, self.B, self.fmap, self.taken
        sa, sb = A.starts, B.starts
        ib = {s: i for i, s in enumerate(sb)}
        i = 0
        while i < len(sa):
            if sa[i] in fmap:
                i += 1
                continue
            j = i
            while j < len(sa) and sa[j] not in fmap:
                j += 1
            if i > 0 and j < len(sa) and j - i <= 12:
                pb, nb = ib.get(fmap[sa[i - 1]]), ib.get(fmap[sa[j]])
                if pb is not None and nb is not None and nb - pb == j - i + 1:
                    pairs = [(sa[i + k], sb[pb + 1 + k]) for k in range(j - i)]
                    ok = True
                    for x, y in pairs:
                        r = (B.funcs[y] - y) / max(4, A.funcs[x] - x)
                        if y in taken or not 0.5 <= r <= 2.0:
                            ok = False
                    if ok:
                        for x, y in pairs:
                            votes[(x, y)]["order"] += 1
            i = j

    def _propagate(self):
        """Rounds of votes; a pair is taken when each is the other's best candidate. The pairs that look alike go
        first (the tiers of similarity), so an ambiguous one is decided when most of its alternatives are gone."""
        A, B = self.A, self.B
        tiers = self.tiers if "veto" in self.rules else (0.0,)
        tier = 0
        for rnd in range(200):
            votes = collections.defaultdict(collections.Counter)
            self._call_votes(votes)
            for x, y in self.hints.items():
                if x not in self.fmap and y not in self.taken:
                    votes[(x, y)]["code"] += 2
            if "vtable-align" in self.rules:
                self._vtable_votes(votes)
            if "callees" in self.rules:
                self._callee_votes(votes)
            if "global" in self.rules:
                self._global_votes(votes)
            if "order" in self.rules:
                self._order_votes(votes)
            best_a, best_b = {}, {}
            for (a, b), c in votes.items():
                if a in self.fmap or b in self.taken:
                    continue
                fa, fb = A.feat[a], B.feat[b]
                sim = 0.0
                if "veto" in self.rules:
                    sim = similarity(fa, fb)
                    if sim < tiers[tier] or implausible(fa, fb):
                        continue
                n = sum(c.values()) + sim
                if n > best_a.get(a, (0, 0))[0]:
                    best_a[a] = (n, b, c)
                if n > best_b.get(b, (0, 0))[0]:
                    best_b[b] = (n, a)
            added = 0
            for a, (n, b, c) in best_a.items():
                if best_b.get(b, (0, 0))[1] == a:
                    added += self._add(a, b, c.most_common(1)[0][0], n)
            self.log("round %d (tier %.2f): +%d (%d matched)" % (rnd, tiers[tier], added, len(self.fmap)))
            if not added:
                tier += 1
                if tier >= len(tiers):
                    break
        if "position" in self.rules:
            # what is left at the one open place of two otherwise equal call sequences is the counterpart whatever it
            # looks like (a routine rewritten or emptied)
            for (x, y), _caller in sorted(self.positional.items()):
                if x not in self.fmap and y not in self.taken:
                    ok_a = sum(1 for (p, q) in self.positional if p == x and p not in self.fmap)
                    ok_b = sum(1 for (p, q) in self.positional if q == y and q not in self.taken)
                    if ok_a == 1 and ok_b == 1:
                        self._add(x, y, "position", 1)


def exact_usable(A, B, M):
    """True when version_map's matcher follows most of A's code into B (two builds of one compiler)."""
    code = sum(e - s for s, e in A.funcs.items())
    return sum(A.funcs[s] - s for s in M.fmap) * 2 > code


class Step:
    """One pair of builds: the function map, and the code matcher when both are one compiler's."""

    def __init__(self, A, B, log=None, rules=ALL_RULES):
        self.A, self.B = A, B
        M = vm.Matcher(A, B)
        self.exact = M if exact_usable(A, B, M) else None
        # version_map's pairs with identical masked code are taken as they are; the pairs it reached through calls,
        # tables and neighbours in code that differs are candidates like any other (they must look alike)
        seeds, hints = {}, {}
        if self.exact:
            for s, (t, k) in M.fmap.items():
                (seeds if k == "exact" or M.same(s, t) else hints)[s] = t
        self.sem = SemMatcher(A, B, seeds=seeds, hints=hints, rules=rules, log=log)
        self.fmap = self.sem.fmap
        self.how = dict(self.sem.how)
        if self.exact:
            for s, (t, k) in M.fmap.items():
                if self.fmap.get(s) == t and self.how[s] == "code":
                    self.how[s] = "code:" + k

    def confidence(self, a):
        """0 .. 1 for a matched function: by the rule that found it, how alike the two are and the votes it had."""
        how = self.how[a]
        if how.startswith("code:") or how == "exact":
            return 1.0
        sim = similarity(self.A.feat[a], self.B.feat[self.fmap[a]])
        base = {"code": 0.9, "vtable": 0.8, "string": 0.8, "calls": 0.7, "callees": 0.7, "vtable-align": 0.6,
                "global": 0.6, "order": 0.5, "position": 0.3}.get(how, 0.5)
        return round(min(1.0, base * (0.6 + 0.4 * sim) + min(self.sem.score[a], 6) * 0.03), 2)


# ---------------------------------------------------------------------------------------------------------- comparing
def call_delta(A, B, fmap, taken, fa, fb):
    """The calls that differ after mapping (a callee without a counterpart pairs with any unmatched one)."""
    ca = collections.Counter(fmap.get(x, "?") for x in fa.calls)
    cb = collections.Counter(y if y in taken else "?" for y in fb.calls)
    d = (ca - cb) + (cb - ca)
    return sum(d.values())


def masked_kind(A, a, B, b):
    """Two builds of one compiler: "same", "offsets" (only load / store / pointer offsets differ), "immediates" (other
    immediates too) or None (the code differs otherwise)."""
    na, nb = (A.funcs[a] - a) // 4, (B.funcs[b] - b) // 4
    if na != nb:
        return None
    kind = "same"
    for k in range(na):
        x, y = A.norm(a + 4 * k, False), B.norm(b + 4 * k, False)
        if x == y:
            continue
        if (x ^ y) & 0xFFFF0000:
            return None
        op = x >> 26
        if op in LOADS or op in STORES or op in (0x09, 0x19):
            kind = "offsets" if kind == "same" else kind
        else:
            kind = "immediates"
    return kind


def compare(A, a, B, b, fmap, taken, same_compiler=False, layout=None):
    """dict(cls, d (the float operation deltas, B - A, in ARITH's order), only_a / only_b (constants), calls, notes).
    With `layout` (a Layout of A and B), a pair that is otherwise the same is "layout only" when its field offsets
    differ."""
    fa, fb = A.feat[a], B.feat[b]
    xa, xb = fa.arith(), fb.arith()
    d = tuple(y - x for x, y in zip(xa, xb))
    only_a, only_b = sorted(fa.consts - fb.consts), sorted(fb.consts - fa.consts)
    # the same literal rounded otherwise (one compiler rounds a decimal literal to nearest, another truncates it)
    ulp = [(x, y) for x in only_a for y in only_b if abs(x - y) == 1]
    rounding = bool(ulp) and len(ulp) == len(only_a) == len(only_b)
    calls = call_delta(A, B, fmap, taken, fa, fb)
    notes = []
    if fa.strs != fb.strs:
        notes.append("strings")
    if fa.ints != fb.ints:
        notes.append("integers -%s +%s" % (",".join("0x%X" % v for v in sorted(fa.ints - fb.ints)) or "0",
                                         ",".join("0x%X" % v for v in sorted(fb.ints - fa.ints)) or "0"))
    mk = masked_kind(A, a, B, b) if same_compiler else None
    nd = sum(abs(x) for x in d)
    ctl = abs(fa.branches - fb.branches) > max(2, (fa.branches + fb.branches) // 8) or fa.jalr != fb.jalr
    big, small = max(fa.size, fb.size), min(fa.size, fb.size)
    if mk == "same":
        cls = "same"
    elif mk == "offsets":
        cls = "layout only"
    elif nd > 3:
        cls = "larger change"
    elif nd:
        cls = "small arithmetic edit"
    elif only_a or only_b:
        cls = "constants"
        if rounding:
            notes.append("literal rounding only")
    elif calls or "strings" in notes:
        cls = "calls only"
    elif same_compiler:
        cls = "integer/control edit"
    elif ctl or (big >= 96 and small * 2 < big):
        cls = "integer/control edit"
        notes.append("by branch count / size: check")
    elif layout is not None and layout.offsets_differ(a, b):
        cls = "layout only"
    else:
        cls = "same"
    if not same_compiler and big >= 64 and small * 3 < big:
        notes.append("size %d/%d" % (fa.size, fb.size))
    return dict(cls=cls, d=d, only_a=only_a, only_b=only_b, calls=calls, notes=notes)


# ---------------------------------------------------------------------------------------------------------- layout
BIG_STRUCT = 0x100  # a pointer whose accesses reach this far is a struct of its own kind (not a vector or a list node)


class Access:
    """The memory accesses of a build's functions through the pointers they are given.

    A pointer is ("arg", function, k) (argument k) or ("ptr", pointer, offset) (the pointer loaded from that field).
    events[function] lists its calls and accesses in address order:
        (addr, "call", callee or None, {k: (pointer, delta)})   the pointers it passes
        (addr, "Lf" / "S4" / ..., pointer or None, offset)      load / store and width, through pointer + offset
    struct(pointer) names the struct a pointer points at (a representative pointer)."""

    def __init__(self, P):
        self.P = P
        self.events = {s: self._walk(s, e) for s, e in P.funcs.items()}
        self._solve()

    def _walk(self, s, e):
        P = self.P
        word = P.word
        reg = {r: (("arg", s, k), 0) for k, r in enumerate(ARG_REGS)}
        stack = {}
        out = []
        a = s
        while a < e:
            w = word(a)
            op = w >> 26
            call = op == 3 or (op == 0 and (w & 0x3F) == 9)
            if call:
                if a + 4 < e:  # the delay slot runs first
                    self._insn(a + 4, word(a + 4), reg, stack, out)
                t = (w & 0x3FFFFFF) << 2 if op == 3 else None
                out.append((a, "call", t, {k: reg[r] for k, r in enumerate(ARG_REGS) if r in reg}))
                for r in CALLER_SAVED:
                    reg.pop(r, None)
                a += 8
                continue
            self._insn(a, w, reg, stack, out)
            a += 4
        return out

    def _insn(self, a, w, reg, stack, out):
        op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
        imm = vm_sext(w)
        if op in LOADS or op in STORES:
            load = op in LOADS
            if rs == 29:  # a pointer kept on the stack
                if not load:
                    if op in (0x2B, 0x3F) and rt in reg:
                        stack[imm] = reg[rt]
                    else:
                        stack.pop(imm, None)
                elif op in (0x23, 0x27, 0x37) and imm in stack:
                    reg[rt] = stack[imm]
                    return
            elif rs not in (0, 28) and a not in self.P.refs:
                # by width; a float is a 4 (compilers copy floats through either kind of register)
                cls = "v" if op in (0x36, 0x3E) else str((LOADS.get(op) or STORES[op])[1])
                base = reg.get(rs)
                out.append((a, ("L" if load else "S") + cls, base[0] if base else None, (base[1] if base else 0) + imm))
                if load and op in (0x23, 0x27, 0x37) and base and rt:
                    reg[rt] = (("ptr", base[0], base[1] + imm), 0)
                    return
            if load and op not in (0x31, 0x36):
                reg.pop(rt, None)
        elif op in (0x09, 0x19):
            if rs in reg and rs != 29:
                reg[rt] = (reg[rs][0], reg[rs][1] + imm)
            else:
                reg.pop(rt, None)
        elif op == 0:
            fn, rd = w & 0x3F, (w >> 11) & 31
            if fn in (0x2D, 0x25, 0x21) and (rt == 0 or rs == 0):
                src = rs if rt == 0 else rt
                if src in reg:
                    reg[rd] = reg[src]
                else:
                    reg.pop(rd, None)
            elif fn in (0x2D, 0x21) and (rs in reg) != (rt in reg):  # pointer + index: an element of an array there
                reg[rd] = reg[rs] if rs in reg else reg[rt]
            elif fn not in (8, 0x0C, 0x0D, 0x0F, 0x18, 0x19, 0x1A, 0x1B, 0x11, 0x13):
                reg.pop(rd, None)
        elif op in (0x0F, 0x0D, 0x08, 0x0A, 0x0B, 0x0C, 0x0E, 0x18):
            reg.pop(rt, None)
        elif op in (0x10, 0x11, 0x12) and rs in (0, 1, 2):
            reg.pop(rt, None)
        elif op == 0x1C:
            reg.pop((w >> 11) & 31, None)

    # -- the structs
    def _find(self, n):
        par = self._par
        root = n
        while root in par:
            root = par[root]
        while n in par:
            par[n], n = root, par[n]
        return root

    def _canon(self, p, depth=0):
        if p[0] == "ptr":
            p = ("ptr", self._canon(p[1], depth + 1) if depth < 4 else p[1], p[2])
        return self._find(p)

    def _solve(self):
        """One struct for the `this` of the functions only one class's virtual table has, when they use it as a large
        struct. (Pointers handed on to callees are not merged: a callee is often handed a part of several structs;
        Layout counts a callee's accesses for each caller instead.)"""
        P = self.P
        extent = collections.Counter()
        for s, evs in self.events.items():
            for ev in evs:
                if ev[1] != "call" and ev[2] is not None and ev[2][0] == "arg" and ev[3] > extent[ev[2]]:
                    extent[ev[2]] = ev[3]
        self.extent = extent
        tables = collections.Counter(x for slots in P.vt.values() for x in set(slots[1:]))
        self._par = {}
        for key, slots in P.vt.items():
            if key[1]:
                continue  # a second table of the class: another base's `this`
            fs = [("arg", x, 0) for x in slots[1:]
                  if tables[x] == 1 and x in P.funcs and extent[("arg", x, 0)] >= BIG_STRUCT]
            for x in fs[1:]:
                ra, rb = self._find(fs[0]), self._find(x)
                if ra != rb:
                    self._par[rb] = ra
        self._final = {}

    def struct(self, p):
        """The struct a pointer of an event points at (a representative pointer)."""
        if p not in self._final:
            self._final[p] = self._canon(p)
        return self._final[p]


def align_events(ea, eb, call_key_a, call_key_b):
    """Pairs (i, j) of two event lists: the calls aligned by callee, then the accesses between two aligned calls by
    their kind (load / store and width)."""
    ca = [(i, call_key_a(ev[2])) for i, ev in enumerate(ea) if ev[1] == "call"]
    cb = [(j, call_key_b(ev[2])) for j, ev in enumerate(eb) if ev[1] == "call"]
    anchors = [(-1, -1)]
    sm = difflib.SequenceMatcher(a=[k for _, k in ca], b=[k for _, k in cb], autojunk=False)
    for blk in sm.get_matching_blocks():
        for k in range(blk.size):
            anchors.append((ca[blk.a + k][0], cb[blk.b + k][0]))
    anchors.append((len(ea), len(eb)))
    pairs = []
    for (a0, b0), (a1, b1) in zip(anchors, anchors[1:]):
        if 0 <= a0 < len(ea):
            pairs.append((a0, b0))
        sa = [(i, ea[i][1]) for i in range(a0 + 1, a1) if ea[i][1] != "call"]
        sb = [(j, eb[j][1]) for j in range(b0 + 1, b1) if eb[j][1] != "call"]
        if not sa or not sb:
            continue
        sm = difflib.SequenceMatcher(a=[t for _, t in sa], b=[t for _, t in sb], autojunk=False)
        for blk in sm.get_matching_blocks():
            for k in range(blk.size):
                pairs.append((sa[blk.a + k][0], sb[blk.b + k][0]))
    return pairs


def _subst(p, callee, args):
    """Pointer p of a callee as its caller's (pointer, delta), given the pointers the call passes; None if unknown."""
    if p[0] == "arg":
        return args.get(p[2]) if p[1] == callee else None
    q = _subst(p[1], callee, args)
    return (("ptr", q[0], q[1] + p[2]), 0) if q else None


def _shape(p):
    """A pointer without the function and offsets that name it: argument k, or a pointer loaded through one."""
    return ("arg", p[2]) if p[0] == "arg" else ("ptr", _shape(p[1]))


class Layout:
    """Field offsets of A's structs -> B's, voted by the aligned accesses of the matched functions.

    A vote is one aligned access of a matched pair of functions (its own, or a matched callee's through a pointer
    both calls pass on) that uses offset X of a struct in A where B uses offset Y."""

    def __init__(self, A, B, fmap, accA=None, accB=None):
        self.A, self.B, self.fmap = A, B, fmap
        self.taken = set(fmap.values())
        self.accA = accA or Access(A)
        self.accB = accB or Access(B)
        self._memo = {}
        self._busy = set()

    def aligned(self, a, b):
        """[(event of a, event of b)] of a matched pair: its aligned calls and accesses (Access.events' tuples)."""
        ea, eb = self.accA.events[a], self.accB.events[b]
        fmap, taken = self.fmap, self.taken
        pairs = align_events(ea, eb, lambda t: fmap.get(t, "?") if t is not None else "jalr",
                             lambda t: (t if t in taken else "?") if t is not None else "jalr")
        return [(ea[i], eb[j]) for i, j in pairs]

    def pair_votes(self, a, b, depth=0):
        """{(pointer of a, X, pointer of b, Y): n} for the matched pair: the offsets at its n aligned accesses, its
        matched callees' through the pointers both calls pass included."""
        key = (a, b)
        if key in self._memo:
            return self._memo[key]
        if key in self._busy:
            return {}
        self._busy.add(key)
        out = collections.Counter()
        for x, y in self.aligned(a, b):
            if x[1] != "call":
                if x[2] is not None and y[2] is not None:
                    out[(x[2], x[3], y[2], y[3])] += 1
            elif depth < 6 and x[2] is not None and self.fmap.get(x[2]) == y[2] and x[3] and y[3]:
                for (pa, ox, pb, oy), n in self.pair_votes(x[2], y[2], depth + 1).items():
                    qa, qb = _subst(pa, x[2], x[3]), _subst(pb, y[2], y[3])
                    if qa and qb:
                        out[(qa[0], qa[1] + ox, qb[0], qb[1] + oy)] += n
        self._busy.discard(key)
        if not self._busy:
            self._memo[key] = out
        return out

    def vote(self, only=None):
        """Collect the votes of every matched pair (or of the functions in `only`)."""
        self.votes = collections.defaultdict(collections.Counter)    # (struct A, X) -> {(struct B, Y): n}
        self.rank = collections.defaultdict(collections.Counter)     # the same from pairing the sorted offsets
        self.members = collections.defaultdict(collections.Counter)  # struct A -> {("arg", function, k): votes}
        self.covered = set()   # structs of A a caller hands on unchanged: their votes are in the caller's too
        sa, sb = self.accA.struct, self.accB.struct
        for a, b in self.fmap.items():
            if only is not None and a not in only:
                continue
            for ev in self.accA.events[a]:
                if ev[1] == "call" and ev[2] in self.fmap:
                    for k, (p, d) in ev[3].items():
                        if d == 0 and sa(p) != sa(("arg", ev[2], k)):
                            self.covered.add(sa(("arg", ev[2], k)))
            local = collections.defaultdict(lambda: (set(), set()))
            for (pa, x, pb, y), n in self.pair_votes(a, b).items():
                ka = sa(pa)
                self.votes[(ka, x)][(sb(pb), y)] += n
                self.members[ka][pa if pa[0] == "arg" else ("ptr",)] += n
                local[(ka, sb(pb))][0].add(x)
                local[(ka, sb(pb))][1].add(y)
            # the same accesses paired in offset order (a struct's fields keep their order when it grows)
            for (ka, kb), (xs, ys) in local.items():
                if len(xs) == len(ys):
                    for x, y in zip(sorted(xs), sorted(ys)):
                        self.rank[(ka, x)][(kb, y)] += 1
        return self

    def offsets_differ(self, a, b):
        """True when a matched pair uses other field offsets: through the same pointer (an argument, or a pointer
        loaded through one) and for the same kind of access, both use as many offsets but not the same ones."""
        def sets(P, acc, f):
            d = collections.defaultdict(set)
            for ev in acc.events[f]:
                if ev[1] != "call" and ev[2] is not None:
                    d[(_shape(ev[2]), ev[1])].add(ev[3])
            return d
        da, db = sets(self.A, self.accA, a), sets(self.B, self.accB, b)
        return any(k in db and len(db[k]) == len(xs) and db[k] != xs for k, xs in da.items())

    def same_struct(self, seed):
        """The pointers of A that are the seed's struct: handed on unchanged from or to it. A callee that does not use
        the pointer as a large struct (a helper many structs are handed to) is included but not followed to its other
        callers."""
        if not hasattr(self, "_down"):
            self._down, self._up = collections.defaultdict(set), collections.defaultdict(set)
            for a, evs in self.accA.events.items():
                for ev in evs:
                    if ev[1] == "call" and ev[2] in self.A.funcs:
                        for k, (p, d) in ev[3].items():
                            if d == 0:
                                self._down[p].add(("arg", ev[2], k))
                                self._up[("arg", ev[2], k)].add(p)
        strong = {seed}
        seen = {seed}
        todo = [seed]
        while todo:
            n = todo.pop()
            for c in self._down[n]:
                if c not in seen:
                    seen.add(c)
                    if self.accA.extent[c] >= 4 * BIG_STRUCT and c[1] not in self.A.libs:
                        strong.add(c)
                        todo.append(c)
            if n in strong:
                for c in self._up[n]:
                    if c not in seen:
                        seen.add(c)
                        strong.add(c)
                        todo.append(c)
        return seen

    @staticmethod
    def _pick(aligned, rank):
        """The offset in B for one offset of A: (Y, aligned votes, rank votes, the other candidates). The pairing in
        offset order decides when two or more functions give it: a struct's fields keep
        their order when it grows, while two compilers may load the operands of one expression in either order."""
        y = aligned.most_common(1)[0][0]
        if rank and rank.most_common(1)[0][1] >= 2:
            y = rank.most_common(1)[0][0]
        return y, aligned.get(y, 0), rank.get(y, 0), [(yy, nn) for yy, nn in aligned.most_common(4) if yy != y][:2]

    def struct_table(self, seed, min_votes=2):
        """([(X, Y, aligned votes, rank votes, other candidates)], pointers, outermost pointers) for the struct handed
        to `seed` (("arg", function, k)): the votes of every pointer that is the same struct (same_struct), a callee's
        counted once (in its outermost caller's)."""
        nodes = self.same_struct(seed)
        tops = [n for n in nodes if not (self._up[n] & nodes)]
        want = {self.accA.struct(n) for n in tops}
        rows = collections.defaultdict(collections.Counter)
        rank = collections.defaultdict(collections.Counter)
        for src, dst in ((self.votes, rows), (self.rank, rank)):
            for (ka, x), c in src.items():
                if ka in want:
                    for (_kb, y), n in c.items():
                        dst[x][y] += n
        out = []
        for x in sorted(rows):
            y, n, r, other = self._pick(rows[x], rank[x])
            if n + r >= min_votes:
                out.append((x, y, n, r, other))
        return out, len(nodes), len(tops)

    def tables(self, min_votes=2, covered=False):
        """[(struct A, [(X, Y, aligned votes, rank votes, other candidates)], struct B, total)] by the number of
        votes: each struct of A against the struct of B most of its votes go to. Without `covered`, only the structs no
        caller hands on unchanged (a caller's table holds its callees' votes)."""
        per = collections.defaultdict(dict)
        for (ka, x), c in self.votes.items():
            if covered or ka not in self.covered:
                per[ka][x] = c
        out = []
        for ka, rows in per.items():
            kb_votes = collections.Counter()
            for c in rows.values():
                for (kb, _y), n in c.items():
                    kb_votes[kb] += n
            kb = kb_votes.most_common(1)[0][0]
            lines = []
            for x in sorted(rows):
                c = collections.Counter({y: n for (k, y), n in rows[x].items() if k == kb})
                if not c:
                    continue
                r = collections.Counter({y: n for (k, y), n in self.rank.get((ka, x), {}).items() if k == kb})
                y, n, rn, other = self._pick(c, r)
                if n + rn >= min_votes:
                    lines.append((x, y, n, rn, other))
            if lines:
                out.append((ka, lines, kb, sum(ln[2] + ln[3] for ln in lines)))
        out.sort(key=lambda t: -t[3])
        return out

    def label(self, ka):
        """A struct of A as text: the function and argument most of its votes came through."""
        best = [m for m, _n in self.members[ka].most_common(4) if m[0] == "arg"]
        return struct_label(self.A, best[0] if best else ka)


def struct_label(P, s):
    if s[0] == "arg":
        return "arg%d of %06X %s" % (s[2], s[1], P.fnames.get(s[1], ""))
    return "the pointer at +0x%X of (%s)" % (s[2], struct_label(P, s[1]))


# ---------------------------------------------------------------------------------------------------------- chains
class Chain:
    """Builds matched step by step; maps of the first build into each later one."""

    def __init__(self, builds, log=None):
        self.builds = builds
        self.steps = [Step(a, b, log=log) for a, b in zip(builds, builds[1:])]

    def follow(self, a, upto=None):
        """[(start in build k or None)] for k = 0 .. upto of a function of the first build."""
        out = [a]
        for st in self.steps[:upto]:
            a = st.fmap.get(a) if a is not None else None
            out.append(a)
        return out

    def composed(self, k):
        """({start in build 0: start in build k}, taken) through the steps."""
        m = {}
        for a in self.builds[0].funcs:
            b = self.follow(a, k)[-1]
            if b is not None:
                m[a] = b
        return m

    def how(self, a, k):
        """How a was followed to build k (the weakest step) and the confidence (the product)."""
        conf, hows = 1.0, []
        for st in self.steps[:k]:
            if a not in st.fmap:
                return "", 0.0
            hows.append(st.how[a])
            conf *= st.confidence(a)
            a = st.fmap[a]
        weak = [h for h in hows if not h.startswith("code:")]
        return (weak[0] if weak else hows[0]), round(conf, 2)

    def same_compiler(self, k):
        return all(st.exact is not None for st in self.steps[:k])

    def map_addr(self, a, k):
        """(address in build k or None, how): a function start, or an instruction through the aligned code / the
        aligned calls and accesses where the compilers differ."""
        how = []
        for st in self.steps[:k]:
            A = st.A
            s = A.func_of(a)
            if s is None or s not in st.fmap:
                return None, "no function / unmatched in " + A.name
            t = st.fmap[s]
            if a == s:
                a = t
                continue
            if st.exact is not None and s in st.exact.fmap:
                b, h = st.exact.map_addr(a)
                if b is None:
                    return None, h
                a = b
                how.append(h)
                continue
            if not hasattr(st, "layout"):
                st.layout = Layout(A, st.B, st.fmap)
            d = {x[0]: y[0] for x, y in st.layout.aligned(s, t)}
            if a not in d:
                return None, "%06X+%X is not an aligned call or access" % (s, a - s)
            a = d[a]
            how.append("aligned access")
        return a, "; ".join(how) or "function"


def locate_data(A, B, fmap, addr, size, entry=4):
    """Where the `size` bytes at `addr` of A (a table) are in B: through the matched functions that address them.

    The candidates are the places those functions' counterparts address; the one whose bytes are most like A's wins.
    Else B's data is searched for the bytes, for floats within one unit in the last place of A's (entries of 4 bytes:
    another compiler's rounding of the same literals), then for the table's first bytes. Returns dict(users:
    [(function of A, its counterpart or None)], addr: the place in B or None, differ: the entries (of `entry` bytes)
    that differ there, entries, how)."""
    want = A.data[addr - A.base:addr - A.base + size]
    users = {}
    for g in A.glob_users:
        if addr <= g < addr + size:
            for f in A.glob_users[g]:
                users.setdefault(f, set()).add(g - addr)
    n = (size + entry - 1) // entry
    lo = B.text_end - B.base

    def differ(b):
        got = B.data[b - B.base:b - B.base + size]
        if len(got) < size:
            return n
        return sum(1 for k in range(0, size, entry) if got[k:k + entry] != want[k:k + entry])

    def find(what):
        hits = []
        at = B.data.find(what, lo)
        while at >= 0 and len(hits) < 3:
            hits.append(B.base + at)
            at = B.data.find(what, at + 1)
        return hits

    best = None
    for f, offs in users.items():
        t = fmap.get(f)
        if t is None:
            continue
        for g in B.feat[t].globs:
            for off in offs:
                b = g - off
                if B.text_end <= b and b + size <= B.base + len(B.data):
                    d = differ(b)
                    if best is None or d < best[0]:
                        best = (d, b)
    out = dict(users=sorted((f, fmap.get(f)) for f in users), entries=n)
    if best is not None and best[0] * 2 <= n:
        out.update(addr=best[1], differ=best[0], how="addressed by the matched routine")
        return out
    hits = find(want)
    if len(hits) == 1:
        out.update(addr=hits[0], differ=0, how="found by its bytes")
        return out
    if entry == 4 and size >= 8:
        wa = struct.unpack("<%dI" % (size // 4), want[:size // 4 * 4])
        for o in range((lo + 3) & ~3, len(B.data) - size, 4):
            if abs(struct.unpack_from("<I", B.data, o)[0] - wa[0]) <= 1:
                wb = struct.unpack_from("<%dI" % len(wa), B.data, o)
                if all(abs(x - y) <= 1 for x, y in zip(wa, wb)):
                    out.update(addr=B.base + o, differ=sum(1 for x, y in zip(wa, wb) if x != y),
                               how="found by its values, the entries that differ by one unit in the last place")
                    return out
    head = find(want[:max(8, min(32, size // 2))])
    if len(head) == 1:
        out.update(addr=head[0], differ=differ(head[0]), how="found by its first bytes: values or size differ")
    elif best is not None:
        out.update(addr=best[1], differ=best[0], how="the closest place the matched routine addresses: check")
    else:
        out.update(addr=None, differ=n, how="not located" + (" (%d places hold the bytes)" % len(hits) if hits else ""))
    return out


def write_map(path, A, B, fmap, how, same_compiler, layout=None):
    taken = set(fmap.values())
    counts = collections.Counter()
    with open(path, "w") as o:
        o.write("ref\tref_size\tref_name\tother\tother_size\tother_name\thow\tconfidence\tclass\t" +
                "\t".join("d_" + n for n in ARITH) + "\tconsts_only_ref\tconsts_only_other\tcalls_differ\tnotes\n")
        for a in A.starts:
            sa = A.funcs[a] - a
            b = fmap.get(a)
            if b is None:
                counts["not found"] += 1
                o.write("%08X\t%d\t%s\t-\t0\t\t\t0\tnot found\t%s\t\t\t\t\n" % (
                    a, sa, A.fnames.get(a, ""), "\t".join("0" for _ in ARITH)))
                continue
            c = compare(A, a, B, b, fmap, taken, same_compiler, layout)
            h, conf = how(a)
            counts[c["cls"]] += 1
            o.write("%08X\t%d\t%s\t%08X\t%d\t%s\t%s\t%.2f\t%s\t%s\t%s\t%s\t%d\t%s\n" % (
                a, sa, A.fnames.get(a, ""), b, B.funcs[b] - b, B.fnames.get(b, ""), h, conf, c["cls"],
                "\t".join("%+d" % x if x else "0" for x in c["d"]),
                " ".join(const_text(v) for v in c["only_a"]), " ".join(const_text(v) for v in c["only_b"]),
                c["calls"], "; ".join(c["notes"])))
    return counts


def write_offsets(path, A, B, lay, limit=60):
    with open(path, "w") as o:
        o.write("# Field offsets of %s -> %s by struct (the structs with the most votes first). votes: aligned accesses of\n"
                "# matched functions, their matched callees' included; rank: the functions whose accesses give the same\n"
                "# pair when taken in offset order (it decides when two or more agree); other: the next candidates by\n"
                "# aligned accesses. A struct is named by the function it is handed to (the outermost one: a callee's\n"
                "# accesses count in its caller's table).\n" % (A.name, B.name))
        for ka, lines, kb, total in lay.tables()[:limit]:
            shifts = collections.Counter()
            for x, y, n, r, _other in lines:
                shifts[y - x] += n + r
            o.write("\nstruct %s -> %s  (%d votes; shifts %s)\n" % (
                lay.label(ka), struct_label(B, kb), total, ", ".join("%+d: %d" % (d, n) for d, n in shifts.most_common(6))))
            for x, y, n, r, other in lines:
                o.write("  %s -> %s  %+5d  votes %3d  rank %3d%s\n" % (
                    hex_off(x), hex_off(y), y - x, n, r,
                    "  other " + ", ".join("%s:%d" % (hex_off(yy), nn) for yy, nn in other) if other else ""))


def hex_off(x):
    return "+0x%04X" % x if x >= 0 else "-0x%04X" % -x


def validate(A, B, ablate, log=print):
    """Match A -> B without their code and score against version_map's matcher."""
    truth = {s: t for s, (t, _k) in vm.Matcher(A, B).fmap.items()}
    code = sum(e - s for s, e in A.funcs.items())

    def score(rules):
        m = SemMatcher(A, B, rules=rules).fmap
        judged = [a for a in m if a in truth]
        right = [a for a in judged if m[a] == truth[a]]
        size = lambda fs: sum(A.funcs[a] - a for a in fs)  # noqa: E731
        return (len(m), len(right), len(judged) - len(right), len(m) - len(judged),
                100.0 * len(right) / max(1, len(judged)), 100.0 * len(right) / len(truth),
                100.0 * size(right) / max(1, size(truth)), 100.0 * size(m) / code)

    log("%s -> %s: version_map's matcher has %d of %d functions (%.1f%% of the code)" % (
        A.name, B.name, len(truth), len(A.funcs), 100.0 * sum(A.funcs[s] - s for s in truth) / code))
    fmt = ("%-28s matched %5d  right %5d  wrong %4d  unjudged %4d  precision %6.2f%%  recall %5.1f%% (%5.1f%% of code)"
           "  code matched %5.1f%%")
    base = [r for r in ALL_RULES if r != "exact"]
    log(fmt % (("all rules, no code seeds",) + score(base)))
    if ablate:
        for r in base:
            log(fmt % (("without " + r,) + score([x for x in base if x != r])))
        log(fmt % (("with the exact seeds",) + score(ALL_RULES)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("builds", nargs="+", help="disc images or unpacked programs: the reference, then the others in order")
    ap.add_argument("--out", help="output directory (outside the repository)")
    ap.add_argument("--names")
    ap.add_argument("--validate", action="store_true")
    ap.add_argument("--ablate", action="store_true")
    ap.add_argument("--addr", nargs="+")
    ap.add_argument("--data", nargs="+", metavar="ADDR:SIZE[:ENTRY]",
                    help="say where a table of <ref> is in the last build and how many of its entries differ there")
    ap.add_argument("--struct", nargs="+", metavar="FUNC:ARG",
                    help="print the field offset table of the struct handed to argument ARG (0 ..) of <ref>'s function "
                         "FUNC, as the last build has it")
    args = ap.parse_args(argv)
    if len(args.builds) < 2:
        ap.error("two builds or more")
    names = args.names.split(",") if args.names else ["b%d" % i for i in range(len(args.builds))]
    builds = [Build(p, n) for p, n in zip(args.builds, names)]
    if args.validate:
        validate(builds[0], builds[1], args.ablate)
        return
    ch = Chain(builds)
    last = len(builds) - 1
    if args.addr:
        for s in args.addr:
            b, how = ch.map_addr(int(s, 16), last)
            print("%s  %s (%s)" % (s, "%06X" % b if b is not None else "-", how))
        return
    if args.data:
        fmap = ch.composed(last)
        for spec in args.data:
            parts = spec.split(":")
            addr, size = int(parts[0], 16), int(parts[1], 0)
            r = locate_data(builds[0], builds[last], fmap, addr, size, int(parts[2], 0) if len(parts) > 2 else 4)
            print("%06X (%d bytes): %s; %s; addressed by %s" % (
                addr, size, "-" if r["addr"] is None else "%06X" % r["addr"],
                r["how"] + ("" if r["addr"] is None else ", %d of %d entries differ" % (r["differ"], r["entries"])),
                ", ".join("%06X -> %s" % (f, "%06X" % t if t else "unmatched") for f, t in r["users"]) or "no function"))
        return
    if args.struct:
        lay = Layout(builds[0], builds[last], ch.composed(last)).vote()
        for spec in args.struct:
            fn, _, k = spec.partition(":")
            rows, n, tops = lay.struct_table(("arg", int(fn, 16), int(k or 0)))
            print("struct handed to argument %s of %s: %d pointers to it found, %d outermost" % (k or 0, fn, n, tops))
            for x, y, v, r, other in rows:
                print("  %s -> %s  %+5d  votes %3d  rank %3d%s" % (
                    hex_off(x), hex_off(y), y - x, v, r,
                    "  other " + ", ".join("%s:%d" % (hex_off(yy), nn) for yy, nn in other) if other else ""))
        return
    if not args.out:
        ap.error("--out DIR")
    here = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    if os.path.commonpath([here, os.path.abspath(args.out)]) == here:
        ap.error("--out must be outside the repository")
    os.makedirs(args.out, exist_ok=True)
    A = builds[0]
    for k in range(1, len(builds)):
        B = builds[k]
        fmap = ch.composed(k)
        same = ch.same_compiler(k)
        lay = Layout(A, B, fmap).vote()
        counts = write_map(os.path.join(args.out, "map_%s_%s.tsv" % (A.name, B.name)), A, B, fmap,
                           lambda a, k=k: ch.how(a, k), same, lay)
        code = sum(e - s for s, e in A.funcs.items())
        print("%s -> %s (%s): %d of %d functions matched, %.1f%% of the code; %s" % (
            A.name, B.name, "one compiler" if same else "compilers differ", len(fmap), len(A.funcs),
            100.0 * sum(A.funcs[a] - a for a in fmap) / code,
            ", ".join("%s %d" % (c, counts[c]) for c in CLASSES if counts[c])))
        write_offsets(os.path.join(args.out, "offsets_%s_%s.txt" % (A.name, B.name)), A, B, lay)
    print("wrote the maps and offset tables to %s" % args.out)


if __name__ == "__main__":
    main()
