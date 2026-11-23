#!/usr/bin/env python3
"""GT3 Revisited RE toolkit: analyse the game program and write reports.

Usage:
    python3 -I tools/re/analyze.py <disc.iso | core.bin> <outdir> [options]

Run with -I (isolated mode). Outputs go only to <outdir>. See docs/dev/RE_TOOLKIT.md.
"""
import argparse
import os
import sys
import time

sys.dont_write_bytecode = True  # keep the repo free of __pycache__
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gt3re import analysis, image, mips, output, rtti, sdk, xrefs  # noqa: E402


def apply_labels(A, path):
    """Apply a label file: lines `addr<TAB>name[<TAB>lib]`; '#' comments and a header line are allowed."""
    n = 0
    with open(path) as f:
        for line in f:
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.rstrip("\n").split("\t")
            try:
                addr = int(cols[0], 16)
            except ValueError:
                continue  # header
            if len(cols) < 2 or not cols[1]:
                continue
            f_ = A.functions.get(addr) or A.split_at(addr)
            if f_ is None:
                continue
            f_.name = cols[1].replace(" ", "_")
            if len(cols) > 2 and cols[2] and cols[2] != "game":
                f_.lib = cols[2]
            f_.tags.add("labelled")
            n += 1
    return n


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="disc image (.iso) or unpacked program (core.bin from `gt3tool core`)")
    ap.add_argument("outdir", help="output directory (created if missing)")
    ap.add_argument("--text-end", type=lambda s: int(s, 0), help="override end of .text")
    ap.add_argument("--no-disasm", action="store_true", help="skip the full disassembly listing")
    ap.add_argument("--no-vol", action="store_true", help="do not read GT3.VOL file headers")
    ap.add_argument("--labels", action="append", default=[],
                    help="TSV of manual labels (addr<TAB>name[<TAB>lib...]); may be repeated")
    ap.add_argument("--backend", choices=["rabbitizer", "capstone", "none"], help="text disassembler")
    args = ap.parse_args(argv)

    t0 = time.time()

    def log(msg):
        print("[%6.1fs] %s" % (time.time() - t0, msg), flush=True)

    prog, vol = image.load(args.input)
    log("loaded %s: %d bytes at 0x%08X, %d VOL entries" % (prog.source, len(prog.data), prog.base, len(vol)))
    A = analysis.Analysis(prog, text_end=args.text_end, log=log).run()
    log("%d functions, %d refs, %d strings" % (len(A.functions), len(A.refs), len(A.strings)))
    R = rtti.Rtti(A, log=log).run()
    S = sdk.Sdk(A, log=log).run()

    for lp in args.labels:
        n = apply_labels(A, lp)
        log("applied %d labels from %s" % (n, lp))
    if args.labels:
        A.reattribute()

    X = {"vol": xrefs.VolIndex(vol)}
    heads = {}
    if vol and not args.no_vol:
        heads = xrefs.vol_magics(args.input, vol)
        log("read headers of %d VOL files" % len(heads))
    X["magics"] = xrefs.magic_set(heads)

    W = output.Writer(args.outdir, A, R, X, S, log=log)
    W.strings()
    W.magics()
    W.rtti()
    log("wrote strings/magics/rtti")
    S.write(W)
    W.functions()
    W.symbols()
    W.summary(S.summary_lines())
    W.db()
    log("wrote functions/symbols/db")
    if not args.no_disasm:
        td = mips.TextDisassembler(args.backend)
        W.disasm(td)
        log("wrote disasm.s (%s)" % td.backend)
    log("done -> %s" % args.outdir)


if __name__ == "__main__":
    main()
