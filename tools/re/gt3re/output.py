"""Writers for all analysis products (text/TSV/JSON, all in the output directory)."""
import json
import os
import re

from .mips import Insn


def esc(s):
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


class Writer:
    def __init__(self, outdir, A, R=None, X=None, sdk=None, log=print):
        self.d = outdir
        self.A = A
        self.R = R
        self.X = X          # dict with vol index, magic sets, ...
        self.sdk = sdk
        self.log = log
        os.makedirs(outdir, exist_ok=True)

    def path(self, name):
        return os.path.join(self.d, name)

    def open(self, name):
        """Open an output file; it is written to a temp name and renamed into place on close, so
        readers never see a half-written file while the analysis is re-run."""
        return _AtomicFile(self.path(name))

    def flabel(self, addr):
        f = self.A.func_at(addr)
        return f.label() if f else "0x%08X" % addr

    def sym(self, addr):
        """Best symbolic name for an address."""
        A = self.A
        f = A.functions.get(addr)
        if f:
            return f.label()
        if self.R:
            c = self.R.classes.get(addr)
            if c:
                return "__ti" + c.mangled
            v = self.R.vtables.get(addr)
            if v:
                return "__vt_" + v.cls.mangled
        if addr in A.strings:
            return "str_%08X" % addr
        return None

    # ------------------------------------------------------------------ functions
    def functions(self):
        A = self.A
        with self.open("functions.tsv") as o:
            o.write("start\tend\tsize\tname\tlib\tncallers\tncalls\ttags\n")
            for s in sorted(A.functions):
                f = A.functions[s]
                o.write("%08X\t%08X\t%d\t%s\t%s\t%d\t%d\t%s\n" % (
                    f.start, f.end, f.end - f.start, f.label(), f.lib or "", len(f.callers), len(f.calls),
                    ",".join(sorted(f.tags))))
        with self.open("callgraph.tsv") as o:
            o.write("caller\tcallee\tcaller_name\tcallee_name\n")
            for s in sorted(A.functions):
                f = A.functions[s]
                for c in sorted(f.calls):
                    o.write("%08X\t%08X\t%s\t%s\n" % (f.start, c, f.label(), A.functions[c].label()))

    def symbols(self):
        """Ghidra ImportSymbolsScript.py format: `name address [f|l]`."""
        A = self.A
        with self.open("symbols.txt") as o:
            for s in sorted(A.functions):
                f = A.functions[s]
                o.write("%s 0x%08X f\n" % (f.label().replace(" ", "_"), s))
            if self.R:
                for c in self.R.classes.values():
                    o.write("__ti%s 0x%08X l\n" % (c.mangled, c.ti))
                for v in self.R.vtables.values():
                    o.write("__vt_%s_%08X 0x%08X l\n" % (v.cls.mangled, v.addr, v.addr))

    # ------------------------------------------------------------------ strings
    def strings(self):
        A = self.A
        vol = self.X.get("vol") if self.X else None
        rows = []
        for a in sorted(A.strings):
            s = A.strings[a]
            users = A.funcs_referencing(a)
            cat = vol.classify(s) if vol else None
            rows.append((a, s, users, cat))
        with self.open("strings.tsv") as o:
            o.write("addr\tcategory\tfunctions\tstring\n")
            for a, s, users, cat in rows:
                fl = ",".join(sorted({self._user_label(f, path) for f, path in users}))
                o.write("%08X\t%s\t%s\t%s\n" % (a, cat[0] if cat else "", fl, esc(s)))
        # Human-readable index of interesting strings
        groups = {}
        for a, s, users, cat in rows:
            key = cat[0] if cat else None
            if key is None:
                if s in (self.X or {}).get("magics", {}):
                    key = "magic"
                elif "/" in s and " " not in s and len(s) > 3:
                    key = "path-like"
                else:
                    continue
            groups.setdefault(key, []).append((a, s, users, cat))
        order = ["vol-path", "vol-pattern", "vol-dir", "vol-like", "path-like", "extension", "magic"]
        with self.open("string_xrefs.txt") as o:
            o.write("# String cross-reference index (asset paths, extensions, magics)\n")
            o.write("# Format: \"string\" @addr [detail]\n#     <- function (via data table @x if indirect)\n\n")
            for g in order:
                items = groups.get(g, [])
                o.write("## %s (%d)\n\n" % (g, len(items)))
                for a, s, users, cat in sorted(items, key=lambda r: r[1].lower()):
                    det = " [%s]" % cat[1] if cat and cat[1] != s else ""
                    o.write("\"%s\" @%08X%s\n" % (esc(s), a, det))
                    if not users:
                        o.write("    <- (no code reference found)\n")
                    for f, path in users[:40]:
                        o.write("    <- %s\n" % self._user_label(f, path))
                o.write("\n")
        self.string_rows = rows

    def _user_label(self, f, path):
        if path:
            return "%s(via %s)" % (f.label(), ">".join("%08X" % p for p in path))
        return f.label()

    # ------------------------------------------------------------------ magics
    def magics(self):
        A = self.A
        magics = self.X.get("magics", {}) if self.X else {}
        imm = {}
        for a, v in A.constants:
            for b, order in ((v.to_bytes(4, "little"), "le"), (v.to_bytes(4, "big"), "be")):
                if all(0x20 < c < 0x7F for c in b):
                    m = b.decode("ascii")
                    imm.setdefault(m, []).append((a, order))
        with self.open("magics.txt") as o:
            o.write("# Magic numbers: VOL file magics vs. references in the program\n")
            o.write("# 'imm' = 32-bit immediate built with lui+ori/addiu. 'le' = value equals the magic read as a\n")
            o.write("# little-endian u32 (i.e. compared against *(u32*)file). 'str' = string literal reference.\n\n")
            for m in sorted(magics, key=lambda k: (-len(magics[k]), k)):
                files = magics[m]
                o.write("%s  (%d VOL files%s)\n" % (m, len(files), ", e.g. " + files[0] if files else ""))
                for a, order in imm.get(m, []):
                    o.write("    imm %s @%08X in %s\n" % (order, a, self.flabel(a)))
                for sa, s in A.strings.items():
                    if s == m or (s.startswith(m) and len(s) <= 8):
                        for f, path in A.funcs_referencing(sa):
                            o.write("    str \"%s\" @%08X <- %s\n" % (esc(s), sa, self._user_label(f, path)))
            o.write("\n# Other printable 4-char immediates (candidate magics/tags)\n")
            for m in sorted(imm):
                if m in magics:
                    continue
                for a, order in imm[m]:
                    o.write("%s  imm %s @%08X in %s\n" % (m, order, a, self.flabel(a)))

    # ------------------------------------------------------------------ rtti
    def rtti(self):
        R = self.R
        A = self.A
        if not R:
            return
        with self.open("rtti_classes.tsv") as o:
            o.write("ti\ttf\tkind\tname\tmangled\tbases\tvtables\tctor_dtor\n")
            for c in sorted(R.classes.values(), key=lambda c: c.name):
                bases = ";".join("%s@%s%s" % (n, "" if b[1] is None else b[1], " virtual" if b[2] else "")
                                 for n, b in zip(R.base_names(c), c.bases))
                o.write("%08X\t%08X\t%s\t%s\t%s\t%s\t%s\t%s\n" % (
                    c.ti, c.tf or 0, c.kind, c.name, c.mangled, bases,
                    ",".join("%08X" % v.addr for v in c.vtables),
                    ",".join("%08X" % s for s in sorted(c.ctor_dtor))))
        ch = R.children()
        roots = [c for c in R.classes.values() if not c.bases or all(b[0] not in R.classes for b in c.bases)]
        with self.open("rtti_hierarchy.txt") as o:
            o.write("# Class hierarchy recovered from GCC 2.9x RTTI (%d types)\n" % len(R.classes))
            o.write("# name  [ti=type_info object, vt=vtable(s), nv=virtual slots]  (+N other bases)\n\n")
            seen = set()

            def emit(c, depth):
                extra = ""
                if len(c.bases) > 1:
                    extra = "  (+ %s)" % ", ".join(R.base_names(c)[1:])
                o.write("%s%s  [ti=%08X%s%s]%s\n" % (
                    "    " * depth, c.name, c.ti,
                    "".join(" vt=%08X" % v.addr for v in c.vtables[:3]),
                    " nv=%d" % (len(c.vtables[0].entries) - 1) if c.vtables else "", extra))
                if c.ti in seen:
                    return
                seen.add(c.ti)
                for k in sorted(ch.get(c.ti, []), key=lambda k: k.name):
                    # list each class under its primary (first) base only
                    if k.bases and k.bases[0][0] == c.ti:
                        emit(k, depth + 1)

            for c in sorted(roots, key=lambda c: c.name):
                emit(c, 0)
        with self.open("vtables.txt") as o:
            o.write("# Vtables: 8-byte entries {s16 delta, s16 index, pfn}; slot 0 = __tf (type-info function)\n\n")
            for vt in sorted(R.vtables):
                v = R.vtables[vt]
                users = sorted({self.flabel(r.frm) for r in A.code_refs_to(vt)})
                o.write("%08X %s (%d slots) used by: %s\n" % (vt, v.cls.name, len(v.entries) - 1,
                                                              ", ".join(users) or "-"))
                for n, (d, idx, pfn) in enumerate(v.entries):
                    f = A.functions.get(pfn)
                    o.write("    [%2d] delta=%d %08X %s\n" % (n, d, pfn, f.label() if f else ""))
                o.write("\n")

    # ------------------------------------------------------------------ summary / db
    def summary(self, extra_lines=()):
        A = self.A
        libs = {}
        for f in A.functions.values():
            libs[f.lib or "game"] = libs.get(f.lib or "game", 0) + 1
        with self.open("summary.txt") as o:
            o.write("source        %s\n" % A.p.source)
            o.write("load/entry    0x%08X / 0x%08X\n" % (A.p.base, A.p.entry))
            o.write(".text         0x%08X-0x%08X\n" % (A.text_start, A.text_end))
            o.write("data          0x%08X-0x%08X\n" % (A.text_end, A.p.end))
            o.write("bss           0x%08X-0x%08X\n" % (A.bss_start, A.bss_end))
            o.write("gp            0x%08X\n" % (A.gp or 0))
            o.write("functions     %d (%d with .eh_frame FDEs)\n" % (len(A.functions), len(A.fdes)))
            o.write("by library    %s\n" % ", ".join("%s=%d" % kv for kv in sorted(libs.items())))
            o.write("references    %d\n" % len(A.refs))
            o.write("strings       %d\n" % len(A.strings))
            if self.R:
                o.write("rtti types    %d, vtables %d\n" % (len(self.R.classes), len(self.R.vtables)))
            for line in extra_lines:
                o.write(line + "\n")

    def db(self):
        """Compact JSON database used by query.py."""
        A = self.A
        d = {
            "base": A.p.base, "entry": A.p.entry, "text_end": A.text_end, "end": A.p.end,
            "gp": A.gp, "bss": [A.bss_start, A.bss_end],
            "functions": {"%08X" % s: [f.end, f.label(), f.lib, sorted(f.calls), sorted(f.callers), sorted(f.tags)]
                          for s, f in A.functions.items()},
            "strings": {"%08X" % a: s for a, s in A.strings.items()},
            "refs": [[r.frm, r.to, r.kind] for r in A.refs],
        }
        if self.R:
            d["classes"] = {"%08X" % c.ti: [c.name, c.mangled, c.kind, [b[0] for b in c.bases],
                                            [v.addr for v in c.vtables], c.tf] for c in self.R.classes.values()}
            d["vtables"] = {"%08X" % v.addr: [v.cls.ti, [e[2] for e in v.entries]] for v in self.R.vtables.values()}
        with self.open("db.json") as o:
            json.dump(d, o, separators=(",", ":"))

    # ------------------------------------------------------------------ disassembly
    def disasm(self, td):
        A = self.A
        strings = A.strings
        lines_at = {}
        for r in A.refs:
            if r.kind == "data":
                continue
            lines_at.setdefault(r.frm, []).append(r.to)
        fn_starts = A.functions
        with self.open("disasm.s") as o:
            o.write("# R5900 disassembly of %s (text backend: %s)\n" % (A.p.source, td.backend))
            o.write("# .text 0x%08X-0x%08X, gp=0x%08X\n\n" % (A.text_start, A.text_end, A.gp or 0))
            for a in range(A.text_start, A.text_end, 4):
                f = fn_starts.get(a)
                if f:
                    o.write("\n# ---------------------------------------------------------------\n")
                    o.write("# %s  [%08X-%08X]%s%s\n" % (f.label(), f.start, f.end,
                                                       " lib=" + f.lib if f.lib else "",
                                                       " tags=" + ",".join(sorted(f.tags)) if f.tags else ""))
                    if f.callers:
                        cl = sorted(f.callers)
                        o.write("# callers: %s%s\n" % (", ".join(A.functions[c].label() for c in cl[:12]),
                                                     " ..." if len(cl) > 12 else ""))
                    o.write("%s:\n" % f.label())
                i = A.insns[a]
                txt = td.text(a, i.word)
                notes = []
                if i.kind in (Insn.JAL, Insn.J) and i.target in fn_starts:
                    lab = fn_starts[i.target].label()
                    t2 = re.sub(r"func_[0-9A-F]+", lab, txt)
                    if t2 == txt:
                        notes.append("-> " + lab)
                    txt = t2
                for to in lines_at.get(a, ()):
                    s = strings.get(to)
                    if s is not None:
                        notes.append("\"%s\"" % esc(s[:80]))
                    else:
                        sy = self.sym(to)
                        notes.append("&%s" % sy if sy else "&%08X" % to)
                o.write("  %08X: %08X  %-40s%s\n" % (a, i.word, txt, ("  # " + "; ".join(notes)) if notes else ""))


class _AtomicFile:
    def __init__(self, path):
        self.path = path
        self.tmp = path + ".tmp"
        self.f = open(self.tmp, "w")

    def __enter__(self):
        return self.f

    def __exit__(self, *exc):
        self.f.close()
        if exc[0] is None:
            os.replace(self.tmp, self.path)
        else:
            os.unlink(self.tmp)
        return False
