#!/usr/bin/env python3
"""The diff harness's version table for every build: GT3's three releases and the later builds of the engine.

Usage:
    python3 -I tools/re/program_map.py <ntsc-u.iso> <pal.iso> <ntsc-j.iso> [<later build.iso> ...] [options]

    --out FILE                  the table to write (default: tools/diffharness/program_map.inc)
    --keep FILE                 a table whose addresses are kept as rows (default: --out when it exists), so a
                                regeneration never drops a row; `--keep ''` for the scanned addresses alone
    --sources DIR_OR_FILE ...   files scanned for program addresses (default: tools/diffharness)
    --report FILE               every address with how each column was mapped (outside the repository)
    --pins FILE                 addresses of the later builds placed by hand (default: program_map.pins beside this
                                script): a line per NTSC-U address, `<ntsc-u> <later 1> <later 2> ...` in hex, `-` for
                                a column left to the matcher, `#` comments. For what the matcher cannot place or
                                places wrong: an instruction that is neither a call nor an access, data another
                                compiler laid out in another order
    --addr ADDR ...             only print these addresses' mappings (no table written)
    --names A,B,...             short names of the later builds for --addr and --report

The columns are NTSC-U, PAL, NTSC-J, then the later builds in the order given (the order they were made: Concept 2001
Tokyo, 2002 Tokyo-Seoul v1.02, v2.00, 2002 Tokyo-Geneva Europe, Asia). PAL and NTSC-J come from version_map.py's code
matcher as before. The later builds come from build_map.py's chain NTSC-U -> PAL -> the later builds in order (only
the step PAL -> the first later build crosses compilers):

  a function start     the chain's function map
  an instruction       a call or a memory access through the aligned calls and accesses; the instruction after a
                       call's delay slot (a return address) from its call; a routine's first instruction after its
                       prologue is not mapped (0)
  a data address       one compiler: version_map's references of aligned instructions. Across compilers: the globals
                       the matched functions refer to, paired in order where both sides refer to as many (votes; the
                       pair most functions agree on), else the nearest paired global below it plus the offset when
                       the nearest pair above has the same shift (marked `check` otherwise)

0 = no counterpart found (the harness leaves out what needs the address). Rows are sorted by the NTSC-U address.

Run with -I. Reads only the discs; writes only --out / --report. See docs/dev/RE_TOOLKIT.md.
"""
import argparse
import bisect
import collections
import os
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_map as bm  # noqa: E402
import version_map as vm  # noqa: E402


class DataMap:
    """Data addresses of a step's first build in its second, across compilers: by the matched functions' references."""

    def __init__(self, st):
        self.st = st
        A, B = st.A, st.B
        votes = collections.defaultdict(collections.Counter)
        for s, t in st.fmap.items():
            ra, rb = self._refs(A, s), self._refs(B, t)
            if not ra or len(ra) != len(rb):
                # the distinct globals in first-use order, when both sides have as many
                ra, rb = list(dict.fromkeys(ra)), list(dict.fromkeys(rb))
                if not ra or len(ra) != len(rb):
                    continue
            for x, y in zip(ra, rb):
                votes[x][y] += 1
        self.pairs = {}
        taken = collections.Counter()
        for x, c in votes.items():
            (y, n), rest = c.most_common(1)[0], sum(c.values())
            if n * 2 > rest or (n >= 2 and n * 3 >= rest * 2):
                self.pairs[x] = (y, n)
                taken[y] += 1
        # a place two globals claim: the one with more votes keeps it
        best = {}
        for x, (y, n) in self.pairs.items():
            if y not in best or n > self.pairs[best[y]][1]:
                best[y] = x
        self.pairs = {x: v for x, v in self.pairs.items() if best[v[0]] == x}
        self.keys = sorted(self.pairs)

    @staticmethod
    def _refs(P, s):
        out = []
        lo = P.text_end
        for a in range(s, P.funcs[s], 4):
            for to, kind in P.refs.get(a, ()):
                if kind != "call" and to >= lo and to not in P.strings:
                    out.append(to)
        return out

    def map(self, a):
        if a in self.pairs:
            y, n = self.pairs[a]
            return y, "data: %d functions' references" % n
        i = bisect.bisect_right(self.keys, a) - 1
        if i < 0:
            return None, "data: no paired global below"
        lo = self.keys[i]
        d = self.pairs[lo][0] - lo
        if a - lo > 0x4000:
            return None, "data: the nearest paired global is %X below" % (a - lo)
        hi = self.keys[i + 1] if i + 1 < len(self.keys) else None
        same = hi is not None and self.pairs[hi][0] - hi == d
        return a + d, "data: %06X+%X%s" % (lo, a - lo, "" if same else " check")


class Later:
    """The chain NTSC-U -> PAL -> the later builds, and an address's place in each."""

    def __init__(self, builds):
        self.ch = bm.Chain(builds)
        self.dm = {}
        self.lay = {}

    def _step(self, k, a):
        """One step: (address in the step's second build or None, how)."""
        st = self.ch.steps[k]
        A = st.A
        if a >= A.text_end:
            if st.exact is not None:
                return st.exact.map_addr(a)
            if k not in self.dm:
                self.dm[k] = DataMap(st)
            return self.dm[k].map(a)
        s = A.func_of(a)
        if s is None or s not in st.fmap:
            return None, "no function / unmatched in " + A.name
        t = st.fmap[s]
        if a == s:
            return t, "function"
        if st.exact is not None and s in st.exact.fmap and st.exact.fmap[s][0] == t:
            return st.exact.map_addr(a)
        if k not in self.lay:
            self.lay[k] = bm.Layout(A, st.B, st.fmap)
        d = {x[0]: y[0] for x, y in self.lay[k].aligned(s, t)}
        if a in d:
            return d[a], "aligned"
        if a - 8 in d and (A.word(a - 8) >> 26 == 3 or (A.word(a - 8) >> 26 == 0 and A.word(a - 8) & 0x3F == 9)):
            return d[a - 8] + 8, "aligned call's return"
        if a - 4 in d and (A.word(a - 4) >> 26 == 3):  # a call's delay slot
            return d[a - 4] + 4, "aligned call's delay slot"
        return None, "%06X+%X is not an aligned call or access in %s" % (s, a - s, A.name)

    def map(self, a, k):
        """(address in build k, how) of NTSC-U address a."""
        hows = []
        for i in range(k):
            a, h = self._step(i, a)
            if a is None:
                return None, h
            if h not in ("function", "same", "exact"):
                hows.append(h)
        return a, "; ".join(dict.fromkeys(hows))


def read_pins(path):
    """{ntsc-u address: [the later builds' addresses or None]} of a pins file (none: {})."""
    pins = {}
    if not path or not os.path.exists(path):
        return pins
    for line in open(path):
        c = line.split("#")[0].split()
        if len(c) >= 2:
            pins[int(c[0], 16)] = [None if x == "-" else int(x, 16) for x in c[1:]]
    return pins


def table_rows(path):
    if not path or not os.path.exists(path):
        return []
    return [int(x, 16) for x in re.findall(r"^\{0x([0-9A-Fa-f]{8})", open(path).read(), re.M)]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ntsc_u")
    ap.add_argument("pal")
    ap.add_argument("ntsc_j")
    ap.add_argument("later", nargs="*")
    ap.add_argument("--sources", nargs="+", default=["tools/diffharness"])
    ap.add_argument("--out", default="tools/diffharness/program_map.inc")
    ap.add_argument("--keep")
    ap.add_argument("--report")
    ap.add_argument("--addr", nargs="+")
    ap.add_argument("--names", default="")
    ap.add_argument("--pins", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "program_map.pins"))
    args = ap.parse_args(argv)
    names = args.names.split(",") if args.names else []
    names += ["later%d" % i for i in range(len(names), len(args.later))]
    U = bm.Build(args.ntsc_u, "us")
    P = bm.Build(args.pal, "pal")
    J = vm.Prog(args.ntsc_j)
    later = Later([U, P] + [bm.Build(p, n) for p, n in zip(args.later, names)])
    pal = later.ch.steps[0].exact or vm.Matcher(U, P)
    ntsc_j = vm.Matcher(U, J)

    pins = read_pins(args.pins)

    def columns(a):
        out = [pal.map_addr(a), ntsc_j.map_addr(a)]
        out += [later.map(a, 2 + i) for i in range(len(args.later))]
        for i, b in enumerate(pins.get(a, [])[:len(args.later)]):
            if b is not None:
                out[2 + i] = (b, "pinned")
        return out

    if args.addr:
        for s in args.addr:
            a = int(s, 16)
            print("%06X  " % a + "  ".join("%s %s%s" % (n, "%06X" % b if b is not None else "-", " (%s)" % h if h else "")
                                          for n, (b, h) in zip(["PAL", "NTSC-J"] + names, columns(a))))
        return
    keep = args.out if args.keep is None else args.keep
    addrs = sorted(set(vm.scan(args.sources, U.base, U.bss[1])) | set(table_rows(keep)) | set(pins))
    rows, report = [], []
    for a in addrs:
        cols = columns(a)
        notes = sorted({h for b, h in cols[:2] if b is None or "check" in h or "ambiguous" in h})
        if any(b is not None and "check" in h for b, h in cols[2:]):
            notes.append("later builds: check")
        rows.append("{0x%08X, %s},%s" % (a, ", ".join("0x%08X" % (b or 0) for b, _ in cols),
                                         " // " + "; ".join(notes) if notes else ""))
        report.append("%06X" % a + "".join("\t%s %s (%s)" % (n, "%06X" % b if b is not None else "-", h)
                                           for n, (b, h) in zip(["PAL", "NTSC-J"] + names, cols)))
    with open(args.out, "w") as o:
        o.write("// Generated by tools/re/program_map.py from the user's discs (do not edit): {NTSC-U, PAL, NTSC-J, then the later\n"
                "// builds: Concept 2001 Tokyo, 2002 Tokyo-Seoul v1.02, v2.00, 2002 Tokyo-Geneva Europe, Asia} for every program\n"
                "// address in the diff harness's sources; 0 = no counterpart.\n")
        o.write("\n".join(rows) + "\n")
    print("%d addresses -> %s" % (len(rows), args.out))
    if args.report:
        with open(args.report, "w") as o:
            o.write("\n".join(report) + "\n")


if __name__ == "__main__":
    main()
