#!/usr/bin/env python3
"""Translate NTSC-U program addresses to the PAL and NTSC-J programs: the diff harness's version table.

Usage:
    python3 -I tools/re/version_map.py <ntsc-u.iso> <pal.iso> <ntsc-j.iso> [options]

    --sources DIR_OR_FILE ...   files scanned for program addresses (default: tools/diffharness)
    --out FILE                  the table to write (default: tools/diffharness/program_map.inc)
    --report FILE               also write every address with how it was mapped, and the scanned functions
                                whose code differs between the versions (for porting the differences)
    --addr ADDR ...             only print these addresses' mappings (no table written)

The functions of two programs are matched by their code with the relocations masked (lui immediates of addresses, the low
halves and $gp offsets that form an address, jump targets): a function whose masked code is unique in both programs is a
match; then callees at aligned call instructions, virtual table slots of the same class (aligned by the slots already
matched) and functions between two matched neighbours. Each match is "same" (identical masked code, branch offsets
included) or "differs". An address inside a function maps through the aligned instructions (difflib over the masked code
without branch offsets); a data address through the references of aligned instructions (or the nearest referenced address
below it plus the offset, marked in the row's comment: check those).

Run with -I. Reads only the discs; writes only --out / --report. See docs/dev/RE_TOOLKIT.md.
"""
import argparse
import bisect
import collections
import difflib
import os
import re
import struct
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gt3re import analysis, image, rtti  # noqa: E402

BRANCH_OPS = (0, 1, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17)
# Classes another program names otherwise, for the virtual table pass: NTSC-J has one battle conductor whose virtuals are
# the split-screen battle's (NTSC-U's DynamicsConductorBattle2P; its i.LINK path hands over to the base conductor's).
CLASS_ALIASES = {"DynamicsConductorBattle2P": "DynamicsConductorBattle"}


class Prog:
    """One program: its bytes, functions, references and virtual tables."""

    def __init__(self, path):
        p, _ = image.load(path)
        A = analysis.Analysis(p, log=lambda *a: None).run()
        R = rtti.Rtti(A, log=lambda *a: None).run()
        self.analysis, self.rtti = A, R  # for build_map.py (strings, names, call graph)
        self.data, self.base = p.data, p.base
        self.funcs = {s: f.end for s, f in A.functions.items()}
        self.refs = {}
        for r in A.refs:
            self.refs.setdefault(r.frm, []).append((r.to, r.kind))
        self.gp, self.text_end, self.bss = A.gp, A.text_end, (A.bss_start, A.bss_end)
        self.vtables = {}
        for v in R.vtables.values():
            self.vtables.setdefault(v.cls.name, []).append([e[2] for e in v.entries])
        self.starts = sorted(self.funcs)

    def word(self, a):
        return struct.unpack_from("<I", self.data, a - self.base)[0]

    def norm(self, a, loose, regless=False):
        w = self.word(a)
        op, rs = w >> 26, (w >> 21) & 31
        if op in (2, 3):
            return w & 0xFC000000
        if op == 0x0F:
            return w & 0xFFFF0000 if 0x10 <= (w & 0xFFFF) < 0x50 else w  # an address in the program (else a constant)
        if op not in BRANCH_OPS and a in self.refs and any(k != "call" for _, k in self.refs[a]):
            return w & 0xFFFF0000
        if rs == 28 and op not in (0, 0x10, 0x11, 0x12, 0x13, 0x1C) and op not in BRANCH_OPS:
            return w & 0xFFFF0000
        if regless:  # registers (and $sp offsets) masked: functions whose registers were allocated otherwise
            w = w & 0xFC0007FF if op == 0 else w & 0xFFE0003F if op == 0x11 else w & 0xFC00FFFF
            if op and rs == 29:
                w &= 0xFFFF0000
        if loose and (op in BRANCH_OPS[2:] or op == 1 or (op == 0x11 and rs == 8)):
            return w & 0xFFFF0000
        return w

    def seqs(self, loose, regless=False):
        return {s: tuple(self.norm(a, loose, regless) for a in range(s, e, 4)) for s, e in self.funcs.items() if e > s}


class Matcher:
    def __init__(self, A, B):
        self.A, self.B = A, B
        self.la, self.lb = A.seqs(True), B.seqs(True)    # alignment
        self.ka, self.kb = A.seqs(False), B.seqs(False)  # sameness
        self.ra, self.rb = A.seqs(True, True), B.seqs(True, True)  # alignment of differing code
        self.fmap = {}
        self._al = {}
        self._run()

    def same(self, s, t):
        return self.ka.get(s) == self.kb.get(t)

    def align(self, s, t, replaced=False):
        key = (s, t, replaced)
        if key not in self._al:
            qa, qb = self.la.get(s, ()), self.lb.get(t, ())
            m = {}
            if qa == qb:
                m = {4 * i: 4 * i for i in range(len(qa))}
            else:
                qa, qb = self.ra.get(s, ()), self.rb.get(t, ())
                for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(a=qa, b=qb, autojunk=False).get_opcodes():
                    if tag == "equal" or (replaced and tag == "replace" and i2 - i1 == j2 - j1):
                        for k in range(i2 - i1):
                            m[4 * (i1 + k)] = 4 * (j1 + k)
            self._al[key] = m
        return self._al[key]

    def _run(self):
        A, B, fmap = self.A, self.B, self.fmap
        ha, hb = collections.defaultdict(list), collections.defaultdict(list)
        for s, q in self.ka.items():
            ha[q].append(s)
        for s, q in self.kb.items():
            hb[q].append(s)
        for s, q in self.ka.items():
            if len(ha[q]) == 1 and len(hb.get(q, [])) == 1:
                fmap[s] = (hb[q][0], "exact")
        idx_b = {s: i for i, s in enumerate(B.starts)}
        for _ in range(40):
            changed = False
            taken = {v[0] for v in fmap.values()}
            for s, (t, _k) in list(fmap.items()):
                for oa, ob in self.align(s, t).items():
                    wa, wb = A.word(s + oa), B.word(t + ob)
                    if (wa >> 26) in (2, 3) and (wb >> 26) == (wa >> 26):
                        ca, cb = (wa & 0x3FFFFFF) << 2, (wb & 0x3FFFFFF) << 2
                        if ca in A.funcs and cb in B.funcs and ca not in fmap and cb not in taken:
                            fmap[ca] = (cb, "call")
                            taken.add(cb)
                            changed = True
            for cls, la in A.vtables.items():
                lb = B.vtables.get(cls) or B.vtables.get(CLASS_ALIASES.get(cls, ""))
                if not lb or len(la) != 1 or len(lb) != 1:
                    continue
                ta, tb = la[0], lb[0]
                pos_b = {x: j for j, x in enumerate(tb)}
                known = {i: pos_b[fmap[x][0]] - i for i, x in enumerate(ta) if x in fmap and fmap[x][0] in pos_b}
                if len(known) < 2:
                    continue
                ks = sorted(known)
                for i, x in enumerate(ta):
                    if i in known or x not in A.funcs or x in fmap:
                        continue
                    left = [known[k] for k in ks if k < i]
                    right = [known[k] for k in ks if k > i]
                    dl, dr = (left[-1] if left else None), (right[0] if right else None)
                    if dl is not None and dr is not None and dl != dr:
                        continue
                    j = i + (dl if dl is not None else dr)
                    if 0 <= j < len(tb) and tb[j] in B.funcs and tb[j] not in taken:
                        fmap[x] = (tb[j], "vtable")
                        taken.add(tb[j])
                        changed = True
            for i, s in enumerate(A.starts):
                if s in fmap or i == 0 or i + 1 >= len(A.starts):
                    continue
                p, n = A.starts[i - 1], A.starts[i + 1]
                if p in fmap and n in fmap:
                    pb, nb = fmap[p][0], fmap[n][0]
                    if pb in idx_b and nb in idx_b and idx_b[nb] - idx_b[pb] == 2:
                        cand = B.starts[idx_b[pb] + 1]
                        if cand not in taken:
                            fmap[s] = (cand, "neighbour")
                            taken.add(cand)
                            changed = True
            if not changed:
                break
        self.dmap = collections.defaultdict(collections.Counter)
        for s, (t, _k) in fmap.items():
            for oa, ob in self.align(s, t).items():
                ra, rb = A.refs.get(s + oa), B.refs.get(t + ob)
                if ra and rb and len(ra) == len(rb):
                    for (x, k1), (y, k2) in zip(ra, rb):
                        if k1 != "call" and k2 != "call":
                            self.dmap[x][y] += 1
        self.dkeys = sorted(self.dmap)

    def calls(self, P, s, e, mapped):
        """The calls of a function in order: (address, callee key) with the callee as B's address (jalr: "jalr")."""
        out = []
        for a in range(s, e, 4):
            w = P.word(a)
            if w >> 26 == 3:
                t = (w & 0x3FFFFFF) << 2
                out.append((a, self.fmap.get(t, (("A", t),))[0] if mapped else t))
            elif w >> 26 == 0 and w & 0x3F == 9:
                out.append((a, "jalr"))
        return out

    def anchored(self, s, t):
        """Instruction offset map a->b of functions whose code differs: their calls aligned by callee (the anchors; a
        callee's repeated calls in order), then each stretch between two anchors position by position when both are as
        long, else through the masked code (registers masked) of the stretch."""
        key = ("anchored", s, t)
        if key in self._al:
            return self._al[key]
        A, B = self.A, self.B
        ca = [(a - s, k) for a, k in self.calls(A, s, A.funcs[s], True)]
        cb = [(b - t, k) for b, k in self.calls(B, t, B.funcs[t], False)]
        anchors = [(0, 0)]
        sm = difflib.SequenceMatcher(a=[k for _, k in ca], b=[k for _, k in cb], autojunk=False)
        for blk in sm.get_matching_blocks():
            for k in range(blk.size):
                anchors.append((ca[blk.a + k][0], cb[blk.b + k][0]))
        anchors.append((A.funcs[s] - s, B.funcs[t] - t))
        qa, qb = self.ra.get(s, ()), self.rb.get(t, ())
        m = {}
        for (a0, b0), (a1, b1) in zip(anchors, anchors[1:]):
            if a1 < a0 or b1 < b0:
                continue
            if a1 - a0 == b1 - b0:
                for o in range(0, a1 - a0, 4):
                    m[a0 + o] = b0 + o
                continue
            sa, sb = qa[a0 // 4:a1 // 4], qb[b0 // 4:b1 // 4]
            for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(a=sa, b=sb, autojunk=False).get_opcodes():
                if tag == "equal" or (tag == "replace" and i2 - i1 == j2 - j1):
                    for k in range(i2 - i1):
                        m[a0 + 4 * (i1 + k)] = b0 + 4 * (j1 + k)
        self._al[key] = m
        return m

    def const_diffs(self, s):
        """Words a matched function loads from the program's data whose values differ in the other program."""
        A, B = self.A, self.B
        t = self.fmap.get(s)
        out = []
        if not t:
            return out
        al = self.align(s, t[0], True) if self.same(s, t[0]) else self.anchored(s, t[0])
        for oa, ob in sorted(al.items()):
            ra, rb = A.refs.get(s + oa), B.refs.get(t[0] + ob)
            if not ra or not rb or len(ra) != len(rb):
                continue
            for (x, k1), (y, k2) in zip(ra, rb):
                if k1 == "call" or not (A.text_end <= x < A.bss[0]) or not (B.text_end <= y < B.bss[0]):
                    continue
                vx, vy = A.word(x & ~3), B.word(y & ~3)
                if vx != vy and not (A.base <= vx < A.bss[1] and B.base <= vy < B.bss[1]):
                    fx = struct.unpack("<f", struct.pack("<I", vx))[0]
                    fy = struct.unpack("<f", struct.pack("<I", vy))[0]
                    out.append("+0x%X: %06X = %08X (%g) vs %06X = %08X (%g)" % (oa, x, vx, fx, y, vy, fy))
        return out

    def map_addr(self, a):
        """(address or None, how)."""
        A = self.A
        if a < A.base or a >= A.bss[1]:
            return None, "outside the program"
        if a < A.text_end:
            i = bisect.bisect_right(A.starts, a) - 1
            if i < 0:
                return None, "before the first function"
            s = A.starts[i]
            if a >= A.funcs[s]:
                if a == A.funcs[s] and s in self.fmap:
                    return self.B.funcs[self.fmap[s][0]], "end of %06X" % s
                return None, "between functions"
            t = self.fmap.get(s)
            if not t:
                return None, "function %06X unmatched" % s
            b = t[0]
            same = self.same(s, b)
            if a == s:
                return b, "function" + ("" if same else ", code differs")
            al = self.align(s, b, True) if same else self.anchored(s, b)
            if a - s in al:
                return b + al[a - s], "in %06X" % s + ("" if same else " (aligned)")
            return None, "%06X+%X has no counterpart in %06X" % (s, a - s, b)
        if a in self.dmap:
            ys = self.dmap[a].most_common()
            return ys[0][0], "data" + ("" if len(ys) == 1 else ", ambiguous: " + " ".join("%06X" % y for y, _ in ys))
        j = bisect.bisect_right(self.dkeys, a) - 1
        if j >= 0 and a - self.dkeys[j] < 0x400:
            base = self.dkeys[j]
            return self.dmap[base].most_common()[0][0] + (a - base), "data %06X+0x%X: check" % (base, a - base)
        return None, "data not referenced by matched code"


HEX = re.compile(r"\b0[xX]([0-9A-Fa-f]{5,8})(?:[uUlL]*)\b")


def scan(paths, lo, hi):
    out = set()
    for p in paths:
        files = [p] if os.path.isfile(p) else [os.path.join(p, f) for f in sorted(os.listdir(p))
                                                 if f.endswith((".cpp", ".h"))]
        for f in files:
            with open(f, errors="replace") as fh:
                for m in HEX.finditer(fh.read()):
                    v = int(m.group(1), 16)
                    if lo <= v < hi:
                        out.add(v)
    return sorted(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ntsc_u")
    ap.add_argument("pal")
    ap.add_argument("ntsc_j")
    ap.add_argument("--sources", nargs="+", default=["tools/diffharness"])
    ap.add_argument("--out", default="tools/diffharness/program_map.inc")
    ap.add_argument("--report")
    ap.add_argument("--addr", nargs="+")
    ap.add_argument("--funcs", help="also write every function's match to FUNCS_pal.tsv / FUNCS_ntscj.tsv")
    args = ap.parse_args(argv)
    U = Prog(args.ntsc_u)
    others = [("PAL", Matcher(U, Prog(args.pal))), ("NTSC-J", Matcher(U, Prog(args.ntsc_j)))]
    if args.funcs:
        for n, m in others:
            with open("%s_%s.tsv" % (args.funcs, n.lower().replace("-", "")), "w") as o:
                for s in U.starts:
                    t = m.fmap.get(s)
                    o.write("%08X\t%s\t%s\t%s\t%d\t%d\n" % (s, "%08X" % t[0] if t else "-", t[1] if t else "unmatched",
                                                          ("same" if m.same(s, t[0]) else "DIFFERS") if t else "-", U.funcs[s] - s,
                                                          (m.B.funcs[t[0]] - t[0]) if t else 0))
    if args.addr:
        for s in args.addr:
            a = int(s, 16)
            print("%06X  " % a + "  ".join("%s %s (%s)" % (n, "%06X" % b if b is not None else "-", how)
                                          for n, m in others for b, how in [m.map_addr(a)]))
        return
    addrs = scan(args.sources, U.base, U.bss[1])
    rows = []
    for a in addrs:
        maps = [m.map_addr(a) for _, m in others]
        notes = sorted({how for b, how in maps if b is None or "check" in how or "ambiguous" in how})
        rows.append("{0x%08X, 0x%08X, 0x%08X},%s" % (a, maps[0][0] or 0, maps[1][0] or 0,
                                                    " // " + "; ".join(notes) if notes else ""))
    with open(args.out, "w") as o:
        o.write("// Generated by tools/re/version_map.py from the user's discs (do not edit): {NTSC-U, PAL, NTSC-J} for every\n"
                "// program address in the diff harness's sources; 0 = no counterpart.\n")
        o.write("\n".join(rows) + "\n")
    print("%d addresses -> %s" % (len(rows), args.out))
    if args.report:
        with open(args.report, "w") as o:
            for a in addrs:
                o.write("%06X" % a + "".join("\t%s %s (%s)" % (n, "%06X" % b if b is not None else "-", how)
                                             for n, m in others for b, how in [m.map_addr(a)]) + "\n")
            o.write("\n# scanned functions whose code differs or that have no match\n")
            seen = set()
            for a in addrs:
                if not (U.base <= a < U.text_end):
                    continue
                i = bisect.bisect_right(U.starts, a) - 1
                s = U.starts[i]
                if s in seen or a >= U.funcs[s]:
                    continue
                seen.add(s)
                st = []
                for n, m in others:
                    t = m.fmap.get(s)
                    st.append("%s %s" % (n, "unmatched" if not t else "%06X %s" % (t[0], "same" if m.same(s, t[0]) else "DIFFERS")))
                if any("same" not in x for x in st):
                    o.write("%06X\t%s\n" % (s, "\t".join(st)))
            o.write("\n# constants the scanned functions load whose values differ (aligned instructions; pointers left out)\n")
            for s in sorted(seen):
                for n, m in others:
                    for line in m.const_diffs(s):
                        o.write("%06X\t%s %s\n" % (s, n, line))


if __name__ == "__main__":
    main()
