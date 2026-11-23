#!/usr/bin/env python3
"""Finds the original program's code in the repository's files.

    python3 -I tools/check_program_code.py <tree> <disc.iso | core.bin>...   exit 3: findings (printed), 0: none
    python3 -I tools/check_program_code.py --self-test

The repository may decode instructions and keep hashes of masked code (src/sim/exact/program_locate.h), never the
program's own instruction words. A file is flagged when MIN_WORDS or more of its 32-bit hex literals (0x1234ABCD) are
words of a program's code that lie within WINDOW words of each other, at least one of them rare there (at most RARE
occurrences): one nop, return or register move is generic MIPS, three of a routine's words in a row are its code.
Every program given is checked (all regions; their code differs in places). Build directories (build*), hidden
directories and untracked output are the tree's own business: run it on a checkout (docs/dev/TESTING.md).
"""
import bisect
import os
import re
import struct
import sys

sys.dont_write_bytecode = True  # keep the repo free of __pycache__
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "re"))

WINDOW = 16
MIN_WORDS = 3
RARE = 64
RETURN = 0x03E00008  # jr ra
LITERAL = re.compile(r"(?<![0-9A-Za-z_])0[xX]([0-9A-Fa-f]{8})(?![0-9A-Fa-f])")
TEXT_EXT = re.compile(r"\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inl|m|mm|py|sh|ps1|cmake|txt|md|json|ini|java|kt|kts|gradle|glsl|hlsl|"
                      r"metal|js|ts|html|css|yml|yaml|xml|toml|cfg)$", re.I)


def code_words(program, base):
    """The program's code as u32 words: up to the last return before a long run without one (as CodeIndex finds it)."""
    n = len(program) // 4
    words = struct.unpack_from("<%dI" % n, program, 0)
    last = 0
    for k, w in enumerate(words):
        if w != RETURN:
            continue
        if last and 4 * (k - last) > 0x4000:
            break
        last = k
    return words[: last + 2] if last else words


def literals(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return {int(m.group(1), 16) for m in LITERAL.finditer(f.read())}
    except OSError:
        return set()


def text_files(out):
    for root, dirs, names in os.walk(out):
        dirs[:] = [d for d in dirs if not d.startswith((".", "build"))]
        for name in names:
            if TEXT_EXT.search(name) or name in ("CMakeLists.txt",):
                yield os.path.join(root, name)


def findings(file_literals, programs):
    """file_literals: {name: set of values}; programs: [(label, base, words)]. Returns [(name, label, addr, values)]."""
    wanted = set().union(*file_literals.values()) if file_literals else set()
    out = []
    for label, base, words in programs:
        pos = {}
        for i, w in enumerate(words):
            if w in wanted:
                pos.setdefault(w, []).append(i)
        for name, lits in sorted(file_literals.items()):
            present = [v for v in lits if v in pos]
            if len(present) < MIN_WORDS:
                continue
            hit = None
            for v in present:
                if len(pos[v]) > RARE:
                    continue
                for p in pos[v]:
                    near = [u for u in present if _within(pos[u], p - WINDOW, p + WINDOW)]
                    if len(near) >= MIN_WORDS:
                        hit = (p, sorted(near))
                        break
                if hit:
                    break
            if hit:
                out.append((name, label, base + 4 * hit[0], hit[1]))
    return out


def _within(sorted_positions, lo, hi):
    i = bisect.bisect_left(sorted_positions, lo)
    return i < len(sorted_positions) and sorted_positions[i] <= hi


def load_programs(paths):
    from gt3re import image  # noqa: E402 (tools/re)
    programs = []
    for p in paths:
        try:
            prog, _ = image.load(p)
        except (OSError, ValueError) as e:  # not a GT3 disc (no CORE.GT3)
            print("check_program_code: skipped %s (%s)" % (os.path.basename(p), e), file=sys.stderr)
            continue
        programs.append((os.path.basename(p), prog.base, code_words(prog.data, prog.base)))
    return programs


def self_test():
    import random
    rnd = random.Random(1)
    words = [rnd.getrandbits(32) for _ in range(4000)]
    for k in range(0, 4000, 97):
        words[k] = RETURN
    common = 0x0F1E2D3C  # invented, like every word here (the program is synthetic)
    for k in range(5, 4000, 40):
        words[k] = common  # more than RARE times
    routine = [0x5A17C0DE, 0x6B28D1EF, 0x7C39E20A, common, 0x8D4AF311, 0x9E5B0422]
    words[2000:2006] = routine
    programs = [("synthetic", 0x100000, tuple(words))]
    cases = {
        "copied": {0x5A17C0DE, 0x6B28D1EF, 0x9E5B0422},          # three of the routine's words
        "masked": {0x5A17C0DE, 0x6B28D1EF, 0x7C39E200},          # one masked: two left
        "generic": {RETURN, common, 0x00000000},                  # common words only
        "apart": {words[100], words[1500], words[3500]},         # rare words far from each other
        "with-common": {0x6B28D1EF, common, 0x8D4AF311},         # a rare one and its neighbours
        "hash": {0xCE487E5D},                                    # unrelated values
    }
    got = {f[0] for f in findings(cases, programs)}
    want = {"copied", "with-common"}
    if got != want:
        print("self-test FAILED: flagged %s, expected %s" % (sorted(got), sorted(want)))
        return 1
    print("self-test passed")
    return 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    if len(argv) < 3:
        print(__doc__.strip())
        return 2
    out, discs = argv[1], argv[2:]
    if self_test() != 0:
        return 1
    programs = load_programs(discs)
    if not programs:
        print("check_program_code: no program among %s" % ", ".join(discs), file=sys.stderr)
        return 1
    lits = {os.path.relpath(p, out): literals(p) for p in text_files(out)}
    found = findings({k: v for k, v in lits.items() if v}, programs)
    for name, label, addr, values in found:
        print("%s: %d instruction words of %s's code near %08X: %s" %
              (name, len(values), label, addr, " ".join("0x%08X" % v for v in values)))
    return 3 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
