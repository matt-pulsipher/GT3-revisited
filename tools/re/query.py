#!/usr/bin/env python3
"""Query an analysis output directory produced by analyze.py.

Usage:
    python3 -I tools/re/query.py <outdir> func <name|addr>      annotated disassembly of a function
    python3 -I tools/re/query.py <outdir> xref <addr|name>      who references an address / function
    python3 -I tools/re/query.py <outdir> str <regex>           strings matching regex, with referencing functions
    python3 -I tools/re/query.py <outdir> class <regex>         RTTI classes: bases, vtables, virtual methods
    python3 -I tools/re/query.py <outdir> tree <func> [depth]   call tree below a function
    python3 -I tools/re/query.py <outdir> callers <func> [depth] caller tree above a function
"""
import bisect
import json
import os
import re
import sys


class Db:
    def __init__(self, d):
        self.dir = d
        with open(os.path.join(d, "db.json")) as f:
            j = json.load(f)
        self.j = j
        self.funcs = {int(k, 16): v for k, v in j["functions"].items()}
        self.starts = sorted(self.funcs)
        self.by_name = {v[1]: k for k, v in self.funcs.items()}
        self.strings = {int(k, 16): v for k, v in j["strings"].items()}
        self.refs_to = {}
        for frm, to, kind in j["refs"]:
            self.refs_to.setdefault(to, []).append((frm, kind))
        self.classes = {int(k, 16): v for k, v in j.get("classes", {}).items()}
        self.vtables = {int(k, 16): v for k, v in j.get("vtables", {}).items()}

    def resolve(self, s):
        if s in self.by_name:
            return self.by_name[s]
        try:
            return int(s, 16) if not s.startswith("0x") else int(s, 0)
        except ValueError:
            hits = [k for n, k in self.by_name.items() if s in n]
            if len(hits) == 1:
                return hits[0]
            sys.exit("unknown or ambiguous name %r (%d matches)" % (s, len(hits)))

    def func_at(self, a):
        k = bisect.bisect_right(self.starts, a) - 1
        if k < 0:
            return None
        s = self.starts[k]
        return s if a < self.funcs[s][0] else None

    def name(self, s):
        return self.funcs[s][1] if s in self.funcs else "%08X" % s

    def where(self, a):
        f = self.func_at(a)
        return "%s+0x%X" % (self.name(f), a - f) if f is not None else "data@%08X" % a


def cmd_func(db, arg):
    s = db.func_at(db.resolve(arg))
    if s is None:
        sys.exit("not inside a function")
    label = db.name(s) + ":"
    path = os.path.join(db.dir, "disasm.s")
    out = False
    with open(path) as f:
        prev = []
        for line in f:
            if not out:
                prev = (prev + [line])[-4:]
                if line.rstrip("\n") == label:
                    out = True
                    sys.stdout.write("".join(prev))
                continue
            if line.startswith("# ----") or (line.endswith(":\n") and not line.startswith(" ")):
                break
            sys.stdout.write(line)


def cmd_xref(db, arg):
    a = db.resolve(arg)
    for frm, kind in sorted(db.refs_to.get(a, [])):
        print("%-5s %08X  %s" % (kind, frm, db.where(frm)))
    if a in db.funcs:
        for c in db.funcs[a][4]:
            print("call  %08X  %s" % (c, db.name(c)))


def cmd_str(db, rx):
    r = re.compile(rx)
    for a in sorted(db.strings):
        s = db.strings[a]
        if r.search(s):
            users = sorted({db.where(frm) if kind != "data" else "data@%08X" % frm for frm, kind in db.refs_to.get(a, [])})
            print("%08X %r\n    %s" % (a, s, ", ".join(users) or "-"))


def cmd_class(db, rx):
    r = re.compile(rx)
    for ti, (name, mangled, kind, bases, vts, tf) in sorted(db.classes.items(), key=lambda kv: kv[1][0]):
        if not r.search(name):
            continue
        bn = [db.classes[b][0] if b in db.classes else "%08X" % b for b in bases]
        print("%s  (%s) ti=%08X tf=%08X bases=[%s]" % (name, kind, ti, tf or 0, ", ".join(bn)))
        for vt in vts:
            print("  vtable %08X" % vt)
            for n, pfn in enumerate(db.vtables.get(vt, [None, []])[1]):
                print("    [%2d] %08X %s" % (n, pfn, db.name(pfn) if pfn in db.funcs else ""))


def cmd_tree(db, arg, depth=3, up=False):
    root = db.func_at(db.resolve(arg))
    seen = set()

    def rec(s, d):
        print("%s%s %08X" % ("  " * d, db.name(s), s))
        if d >= depth or s in seen:
            return
        seen.add(s)
        for c in db.funcs[s][4 if up else 3]:
            rec(c, d + 1)
    rec(root, 0)


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    db = Db(sys.argv[1])
    cmd, arg = sys.argv[2], sys.argv[3]
    extra = int(sys.argv[4]) if len(sys.argv) > 4 else 3
    if cmd == "func":
        cmd_func(db, arg)
    elif cmd == "xref":
        cmd_xref(db, arg)
    elif cmd == "str":
        cmd_str(db, arg)
    elif cmd == "class":
        cmd_class(db, arg)
    elif cmd == "tree":
        cmd_tree(db, arg, extra)
    elif cmd == "callers":
        cmd_tree(db, arg, extra, up=True)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
