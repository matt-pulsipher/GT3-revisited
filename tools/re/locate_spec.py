#!/usr/bin/env python3
"""Write gt3r_diffharness --locate-gen's spec for every build: where each build addresses ProgramData's tables and has
the routines whose rules differ (src/sim/exact/program_data.cpp's kTableSites / kRuleSites rows are generated from it).

Usage:
    python3 -I tools/re/locate_spec.py --harness build/gt3r_diffharness --out SPEC.tsv \\
        <ntsc-u.iso> <pal.iso> <ntsc-j.iso> [<later build.iso> ...]
    build/gt3r_diffharness --locate-gen SPEC.tsv > rows.txt

The spec stays outside the repository (it holds addresses of the user's discs).

GT3's releases: the routines ProgramData recognises now (`gt3r_diffharness <iso> --locate-sites`), so their rows come
out as they are. A rule ProgramData does not know on a release yet (a new Rule) is taken from the Rule's NTSC-U address
in src/sim/program.h's comment, through version_map.py's matcher; a release without a match for it (NTSC-J's collision
body set-up) gets an `absent` line: it is recognised there by the other releases' routine's absence.

The later builds (another compiler: none of GT3's code hashes are found in them), through build_map.py's chain:
  a rule    the counterpart of the NTSC-U routine
  a table   where the table is (build_map's locate_data: the place the matched routines address, its bytes compared
            with GT3's), then a routine that forms an address in it: the counterpart of GT3's routine first, else any.
            The compiler may address a table's field instead of its start (`off`: the address formed less the start)
            and through a load's own offset.
`--as RULE=RELEASE ...` says which GT3 release's rules a later build's routine has (0 NTSC-U, 1 PAL, 2 NTSC-J; `own`:
rules of its own); the default is in AS below (what the harness proved; see docs/specs/physics.md).

Run with -I. Reads only the discs and src/sim's headers.
"""
import argparse
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import build_map as bm  # noqa: E402
import program_map as pm  # noqa: E402
import version_map as vm  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(HERE))

# The later builds' routines ProgramData recognises, per Rule: the GT3 release whose rules the routine has (0 NTSC-U, 1 PAL,
# 2 NTSC-J), `own` (rules of its own: the port's branch for that build) or `absent` (the build has no such routine); one
# value for every later build, or a list per build (in the order given on the command line). Only what the diff harness
# proved on each disc is listed: a rule not listed gets no row for the later builds and stays "not a known release's
# code" there.
AS = {
    # GT3's rules whose later-build routine follows PAL's variant (the later builds descend from GT3's PAL release):
    "Oversteer": 1,   # race_tick_exact's steer_indicators with PAL's rule: 0 differing
    # The race flow's (race_tick_exact's race flow ports on each disc; the routines read beside PAL's):
    "RubberBand": 1,                 # PAL's cap at 1.3
    "BattleCap": "absent:1",         # the i.LINK battle's virtual 4 only calls the base one, as PAL's
    "LicenceStop": [0, 0, 0, "own", "own"],  # NTSC-U's 1 km/h; 2002 Tokyo-Geneva's own constants (Europe: 1.5 km/h)
    "LapRecord": 1,                  # PAL's: a lower time
    "SpeedEvent": 1,                 # PAL's: nothing for a ghost
    "RecordsLocked": 1,              # PAL's: no ghost test of its own
    "LapTime": 1,
    "SectorTime": 1,
    "SectorDiff": 0,
    "LapDiff": 0,
    # NTSC-J's own changes are not in the later builds (they follow PAL's line, whose routines are NTSC-U's here):
    "AngleWrap": [0, 0, 0, "own", "own"],  # (2002 Tokyo-Geneva: its own constants, pi a unit lower)
    "RunUp": 0,
    "BattleStart": 0,
    "CatchupAnswer": 0,
    "WearAnswer": 0,
    "DrawMode": 0,
    "LapStartRestore": 0,
    "CollisionCommit": 0,
    "WallFriction": 0,
    "PushSegment": 0,
    "LapUsable": 0,
    "PadRumble": 1,   # ... pad_rumble
    "SteerForce": 1,  # ... steer_force
    "Atan2": "own",
    "FloatToDouble": "own",
    "EngineCondition": "absent",  # (a three-instruction stub that returns 1: nothing to tell it by but GT3's routine's lack)
    "SlipAngle": "own",
    "SpecBuild": "own",
    "BodyBuild": "own",
    "DriverSpread": "own",
    "MovingStart": "own",
    "GroundMiss": "own",
    # (routines only the later builds have: their comment in program.h says `later 0x<Concept 2001 Tokyo address>`)
    "GearSpec": "own",
    "Layout": "own",
    "Driveline": "own",
    "AwdGain": "own",
    "DriveClass": "own",
    "AxleStep": "own",
    "PadClutch": "own",
    "PadControls": "own",
    "DriverInput": "own",
    "AiOutputs": "own",
    "AiDecide": "own",
    "AiTraffic": "own",
    "AiBraking": "own",
    "Ascc": "own",
    "TyreEffects": "own",
    "GapClosing": "own",
    "ContactIntensity": "own",
    "SlowDown": "own",
    "ParamsBuild": "own",
    "Aids": "own",
    "RecordStore": "own",  # control_record_exact
    "StuckNudge": "own",  # model_collide_exact_step
    "SoftDouble": "own",
    "HybridStep": "own",
    "PodMood": "own",
    "PodStop": "own",
    "ActiveWing": "own",
    # (the single race's flying start: 2002 Tokyo-Geneva's conductor has GT3's virtuals 22 and 25 again)
    "FlyingGrid": ["own", "own", "own", "absent:0", "absent:0"],
    # The pod race (race_tick_exact's pod races on each disc: the zone at the race's start, the race flow's virtual 14 and
    # lap handlers, the wheel suspension's spring rate, the collision report):
    "StopZone": "own",
    "PodRule": "own",
    "PodLine": "own",
    "PodSpring": "own",
    "HitReport": "own",
    "PassBy": [0, 0, 0, 1, 0],  # race_tick_exact's pass-by states on every disc (2002 Tokyo-Geneva Europe's: PAL's 50 Hz counts)
    "LicenceJudge": "own",  # race_tick_exact's licence races (the later builds' judge and answers)
    "Leaves": "own",  # leaves_exact and race_tick_exact's leaf checks (the leaf maker on every later build)
    # sparks_emit_exact on each disc: 2001 Tokyo's and Tokyo-Seoul v1.02's emission is GT3's; from v2.00 on the body's plane
    "SparkEmit": [0, 0, "own", "own", "own"],
}


def parse_enum(path, name):
    """[(enumerator, comment)] of `enum class <name>` in a header."""
    src = open(path).read()
    body = src[src.index("enum class %s" % name):]
    body = body[body.index("{") + 1:body.index("};")]
    out = []
    for line in body.split("\n"):
        m = re.match(r"\s*(\w+)(?:\s*=\s*\w+)?,?\s*(?://\s*(.*))?$", line)
        if m and m.group(1) not in ("Count", "End"):
            out.append((m.group(1), m.group(2) or ""))
    return out


def sites(harness, iso):
    """ProgramData's recognised sites on a disc: ({table index: (at, words, hi, lo, start, tier)}, {rule: (at, ok)})."""
    out = subprocess.run([harness, iso, "--locate-sites"], capture_output=True, text=True).stdout
    tables, rules = {}, {}
    for line in out.split("\n"):
        c = line.split("\t")
        if c[0] == "table":
            tables[int(c[1])] = (int(c[2], 16), int(c[3]), int(c[4]), int(c[5]), int(c[6], 16), int(c[7]))
        elif c[0] == "rule":
            rules[int(c[1])] = (int(c[2], 16), int(c[3]))
    return tables, rules


def hi_of(P, a):
    """The lui of the address instruction `a` completes (a itself for a $gp offset), or None."""
    w = P.word(a)
    rs = (w >> 21) & 31
    if rs == 28:
        return a
    s = P.func_of(a)
    b = a - 4
    while s is not None and b >= s and a - b <= 0x80:
        v = P.word(b)
        if v >> 26 == 0x0F and (v >> 16) & 31 == rs:
            return b
        b -= 4
    return None


def table_site(P, start, size, prefer, gp_loads=False):
    """(function, hi, lo, off) of an instruction of P that forms an address in [start, start + size): off 0 first, the
    functions of `prefer` first, an add (not a load's own offset) first. gp_loads: also a load or store through the
    global pointer (a table in the small data whose bytes the code reads one by one)."""
    best = None
    for a, rs in P.refs.items():
        if a >= P.text_end:
            continue
        for to, kind in rs:
            if kind == "call" or not start <= to < start + size:
                continue
            w = P.word(a)
            op = w >> 26
            hi = hi_of(P, a)
            f = P.func_of(a)
            if hi is None or f is None or P.func_of(hi) != f:
                continue
            if hi != a:
                got = (((P.word(hi) & 0xFFFF) << 16) + vm_sext(w)) & 0xFFFFFFFF
            else:
                got = (P.gp + vm_sext(w)) & 0xFFFFFFFF
            if got != to or (op != 9 and (op < 0x20 or (hi == a and not gp_loads))):
                continue
            key = (to != start, f not in prefer, op != 9, to - start, a)
            if best is None or key < best[0]:
                best = (key, (f, hi, a, to - start))
    return best[1] if best else None


def scan_site(P, start, size, prefer):
    """table_site() by the instructions alone, for an address the analysis does not follow (the upper half indexed
    before the offset is used: lui r; addu r, r, x; load off(r)): a lui of the table's upper half and, within 12
    instructions of the same function, an add or a load / store through that register that lands in the table."""
    best = None
    his = {((start + k + 0x8000) >> 16) & 0xFFFF for k in (0, size - 1)}
    for f, e in P.funcs.items():
        for h in range(f, e, 4):
            w = P.word(h)
            if w >> 26 != 0x0F or (w & 0xFFFF) not in his:
                continue
            r = (w >> 16) & 31
            last = min(e, h + 52)
            for a in range(h + 4, min(e, h + 52), 4):
                if a >= last:
                    break
                v = P.word(a)
                op, rs, rt = v >> 26, (v >> 21) & 31, (v >> 16) & 31
                if (op == 9 or 0x20 <= op) and op not in (0x2F, 0x33) and rs == r:
                    to = (((w & 0xFFFF) << 16) + vm_sext(v)) & 0xFFFFFFFF
                    if start <= to < start + size:
                        key = (to != start, f not in prefer, op != 9, to - start, a)
                        if best is None or key < best[0]:
                            best = (key, (f, h, a, to - start))
                    break
                if op == 0 and (v & 0x3F) in (0x21, 0x2D) and ((v >> 11) & 31) == r and r in (rs, rt):
                    continue  # indexed: addu r, r, x
                if op in (2, 3) or (op == 0 and (v & 0x3F) in (8, 9)) or op in (1, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17):
                    last = a + 8  # (its delay slot still runs)
                    continue
                if (op == 0 and ((v >> 11) & 31) == r) or (op in (8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF) and rt == r) or (
                        0x20 <= op < 0x28 and rt == r):
                    break
    return best[1] if best else None


def vm_sext(w):
    v = w & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("isos", nargs="+")
    ap.add_argument("--harness", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--as", dest="as_", nargs="*", default=[])
    args = ap.parse_args(argv)
    if os.path.commonpath([ROOT, os.path.abspath(args.out)]) == ROOT:
        ap.error("--out must be outside the repository")
    rules = parse_enum(os.path.join(ROOT, "src/sim/program.h"), "Rule")
    tables = parse_enum(os.path.join(ROOT, "src/sim/exact/program_data.h"), "Table")
    # (a table placed beside one of GT3's, `beside <Table> +0x<offset>`: no routine of its own, no row)
    tables = [(n, c) if not c.startswith("beside ") else (n, None) for n, c in tables]
    as_ = dict(AS)
    for s in args.as_:
        k, _, v = s.partition("=")
        as_[k] = v
    U, P = bm.Build(args.isos[0], "us"), bm.Build(args.isos[1], "pal")
    J = bm.Build(args.isos[2], "jp")
    later = [bm.Build(p, "later%d" % i) for i, p in enumerate(args.isos[3:])]
    gt3 = [U, P, J]
    match = [None, vm.Matcher(U, P), vm.Matcher(U, J)]
    ch = bm.Chain([U, P] + later) if later else None
    lines = ["iso\t%d\t%s" % (i, p) for i, p in enumerate(args.isos)]
    us_tables, us_rules = sites(args.harness, args.isos[0])
    rule_addr = {}
    # A routine or table only the later builds have: `later 0x<address in the first later build>` in its comment; mapped
    # to the builds after it by a chain of the later builds alone.
    # (a table whose content differs between the later builds names the part every build has: `@0x<offset>+0x<length>`)
    # (`later<n> 0x...`: a table only the later builds from the n-th on have (0 the first), at its address in that build;
    # `later<n>-<m> 0x...`: only the n-th to the m-th)
    later_addr, later_part, later_from, later_to = {}, {}, {}, {}
    for name, comment in rules + [t for t in tables if t[1] is not None]:
        m = re.match(r"later(\d*)(?:-(\d+))? 0x([0-9A-Fa-f]+)(?:@0x([0-9A-Fa-f]+)\+0x([0-9A-Fa-f]+))?", comment)
        if m:
            later_addr[name] = int(m.group(3), 16)
            later_from[name] = int(m.group(1) or 0)
            if m.group(2):
                later_to[name] = int(m.group(2))
            if m.group(4):
                later_part[name] = (int(m.group(4), 16), int(m.group(5), 16))
    lch = bm.Chain(later) if later_addr and len(later) > 1 else None
    fch = {n: bm.Chain(later[n:]) for n in set(later_from.values()) if n and len(later) > n + 1}
    # (routines the chain finds no counterpart for, placed by hand: program_map.pins, a column per later build)
    pins = pm.read_pins(os.path.join(HERE, "program_map.pins"))
    for i, (name, comment) in enumerate(rules):
        m = re.match(r"0x([0-9A-Fa-f]+)", comment)
        rule_addr[name] = int(m.group(1), 16) if m else us_rules.get(i, (0, 0))[0]
    # GT3's releases: what ProgramData recognises, else the NTSC-U routine's match.
    for p, B in enumerate(gt3):
        tb, rl = (us_tables, us_rules) if p == 0 else sites(args.harness, args.isos[p])
        for i, (name, comment) in enumerate(tables):
            if name in later_addr or comment is None:
                continue
            at, words, hi, lo, start, tier = tb[i]
            if not at or tier != 1:
                continue
            f = B.func_of(at)
            lines.append("table\t%s\t%d\t%X\t%X\t%X\t%X\t%X" % (name, p, f, B.funcs[f], at + 4 * hi, at + 4 * lo, start))
        for i, (name, _) in enumerate(rules):
            if name in later_addr:
                lines.append("absent\t%s\t%d" % (name, p))
                continue
            at, ok = rl.get(i, (0, 0))
            later_rule = i > [n for n, _ in rules].index("LapUsable")  # (a later build's change: GT3 has the routine)
            if (later_rule or (not at and not ok)) and rule_addr[name]:  # by its NTSC-U address
                a = rule_addr[name]
                at = a if p == 0 else (match[p].fmap.get(a) or (0,))[0]
            if at:
                lines.append("rule\t%s\t%d\t%X\t%X" % (name, p, at, B.funcs[at]))
            elif ok or later_rule:  # (a later build's rule whose GT3 routine this release does not have as such: its own)
                lines.append("absent\t%s\t%d" % (name, p))
    # The later builds.
    for k, B in enumerate(later):
        p = 3 + k
        fmap = ch.composed(2 + k)
        lmap = lch.composed(k) if lch and k else None  # (the first later build -> this one)
        for i, (name, comment) in enumerate(tables):
            if comment is None:
                continue
            size = table_size(i)
            if name in later_addr:
                a0, n0 = later_addr[name], later_from[name]
                part_off, part_len = later_part.get(name, (0, size))
                if k < n0 or k > later_to.get(name, k):
                    lines.append("# table %s p%d: not in this build" % (name, p))
                    continue
                if k == n0:
                    addr, how, users = a0, "given", set()
                else:
                    r = bm.locate_data(later[n0], B, fch[n0].composed(k - n0) if n0 else lmap, a0 + part_off, part_len, 1)
                    if r["addr"] is None:
                        lines.append("# table %s p%d: not located (%s)" % (name, p, r["how"]))
                        continue
                    addr, how, users = r["addr"] - part_off, r["how"], {t for f, t in r["users"] if t}
                # (the address instruction is looked for in the part every build has)
                s = table_site(B, addr + part_off, part_len, users, True) or scan_site(B, addr + part_off, part_len, users)
                if s is None:
                    lines.append("# table %s p%d: at %X (%s), no routine forms its address" % (name, p, addr, how))
                    continue
                f, hi_a, lo_a, off = s
                off += part_off
                lines.append("table\t%s\t%d\t%X\t%X\t%X\t%X\t%X\t%d\t# %s" % (name, p, f, B.funcs[f], hi_a, lo_a, addr + off, off, how))
                continue
            at, words, hi, lo, start, tier = us_tables[i]
            r = bm.locate_data(U, B, fmap, start, size, 4)
            if r["addr"] is None:
                lines.append("# table %s p%d: not located (%s)" % (name, p, r["how"]))
                continue
            users = {t for f, t in r["users"] if t}
            uf = U.func_of(at)
            if fmap.get(uf):
                users = {fmap[uf]} | users
            s = table_site(B, r["addr"], size, users) or scan_site(B, r["addr"], size, users)
            if s is None:
                lines.append("# table %s p%d: at %X (%s), no routine forms its address" % (name, p, r["addr"], r["how"]))
                continue
            f, hi_a, lo_a, off = s
            lines.append("table\t%s\t%d\t%X\t%X\t%X\t%X\t%X\t%d\t# %s, %d of %d entries differ%s" % (
                name, p, f, B.funcs[f], hi_a, lo_a, r["addr"] + off, off, r["how"], r["differ"], r["entries"],
                "" if f in users else "; not GT3's routine's counterpart"))
        for i, (name, _) in enumerate(rules):
            a = rule_addr[name]
            t = fmap.get(a)
            if t is None and k < len(pins.get(a, [])):
                t = pins[a][k]
            if name in later_addr:
                t = later_addr[name] if k == 0 else lmap.get(later_addr[name])
            v = as_.get(name)
            if isinstance(v, (list, tuple)):
                v = v[k]
            if v is None:
                lines.append("# rule %s p%d: not proven yet (counterpart %s)" % (name, p, "%X" % t if t else "-"))
                continue
            if isinstance(v, str) and v.startswith("absent:"):  # (no such routine, as that GT3 release has none)
                lines.append("absent\t%s\t%d\t%s" % (name, p, v[7:]))
                continue
            if t is None or v == "absent":
                lines.append(("absent\t%s\t%d" if v == "absent" else "# rule %s p%d: no counterpart") % (name, p))
                continue
            lines.append("rule\t%s\t%d\t%X\t%X\t%s" % (name, p, t, B.funcs[t], v))
    with open(args.out, "w") as o:
        o.write("\n".join(lines) + "\n")
    print("%d lines -> %s (%d not located)" % (len(lines), args.out, sum(1 for x in lines if x.startswith("#"))))


def table_size(i):
    src = open(os.path.join(ROOT, "src/sim/exact/program_data.h")).read()
    key = "constexpr u32 sizes[kAllTables] = {"
    body = src[src.index(key) + len(key):]
    body = body[:body.index("}")]
    return [eval(x.strip()) for x in body.split(",")][i]  # (the header's own size expressions: 3 * 32, 0x120, ...)


if __name__ == "__main__":
    main()
