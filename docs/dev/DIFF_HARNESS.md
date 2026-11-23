# Differential test harness (`tools/diffharness/`)

A developer-only tool that runs individual functions of the original game
program (`CORE.GT3` from **your own disc**) in a small R5900 interpreter and
compares their results with our reimplementation over many generated inputs.
It turns "does it behave like GT3?" into numbers: mismatch counts, maximum
absolute/relative error and the concrete inputs that differ.

- Built as `gt3r_diffharness` (CMake option `GT3R_BUILD_DIFFHARNESS`, on by
  default on desktop). The top-level CMakeLists.txt includes any
  `tools/<name>/devtool.cmake`; the harness's is `tools/diffharness/devtool.cmake`,
  so a tree without `tools/diffharness` configures without it.
  Pass `-DGT3R_BUILD_DIFFHARNESS=OFF` for the MinGW cross build. It links
  `gt3r_engine` but is **never linked into the game**.
- The harness *executes* the user's disc at run time. Nothing it reads from the disc is committed: no code,
  expected values or captured outputs (the exact core's program tables are values in `src/sim/exact/program_tables.cpp`,
  like its constants; the harness and `tests/test_program_locate.cpp` check them against each disc).
  Tests contain only addresses, argument layouts and generators; expected
  values come from our own code. Don't paste harness output that contains
  original data (tables, dumps) into the repo.

## Running

Which tests to run for which change (and how long they take): [TESTING.md](TESTING.md).

```bash
cmake --build build-diff --target gt3r_diffharness
./build-diff/gt3r_diffharness "gt3-us.iso"            # all differential tests
./build-diff/gt3r_diffharness "gt3-us.iso" libm_      # tests whose name contains "libm_"
./build-diff/gt3r_diffharness "gt3-us.iso" --iters 10  # 10x the samples
./build-diff/gt3r_diffharness --selftest                          # interpreter unit tests, no disc
./build-diff/gt3r_diffharness --list
# Exploration: run one function with arguments and print v0/v1/f0
./build-diff/gt3r_diffharness "gt3-us.iso" --call 26A5F0 f:1.5707963
./build-diff/gt3r_diffharness "gt3-us.iso" --call 231190 s:tk_r34_gtr_v_99_c --trace
# Coverage: run every leaf function of an RE functions.tsv with dummy args, tally traps
./build-diff/gt3r_diffharness "gt3-us.iso" --probe /path/to/re/out/functions.tsv
```

The probe is a quick way to find instructions that are still missing. With
the current subset, 4,344 leaf functions give: 3,736 return; the other runs
end on dummy-argument effects (bad addresses, syscalls, `break`, calls,
budget). The only unimplemented opcodes left are 6 `vcallms` (VU0 micro
programs, out of scope).

### Other program versions (PAL, NTSC-J)

The harness runs on every disc whose program it knows: NTSC-U (US 1.00 and 1.10 ship the same CORE.GT3 and GT3.VOL),
PAL and NTSC-J. `main.cpp` identifies the program with `ProgramData`'s locator (`sim/exact/program_data.h`,
`ProgramData::program()`) and prints `program version:`. Tests name the original's functions, call sites and globals by their **NTSC-U** addresses;
`pa(addr)` (`program_map.h`) gives the disc's own address through the version table `program_map.inc` (`Pa` does it
where a constexpr table holds the address). Only tests registered with `DIFF_TEST_ALL` run on PAL and NTSC-J (the exact
tests: the libm / libgcc and prep leaves, the car parameters, ground, AI course, collision, race start, race collide and
race tick tests, the pad frame recorder); the others print `SKIP` there. What else differs per program, in the harness: the course files
(`course_path`: courses/US, courses/UK, NTSC-J courses/; where the region's folder lacks the course, the folders the game's `course_file_path` tries: 2002 Tokyo-Geneva Asia's), the car database (`db_path`: paramdb_us / _eu / paramdb.db),
NTSC-J's conductor virtual slots (no virtual 4; `conductor_slot`), its race step's registers (`race_step_reg`), its race
event ids (`ntsc_u_event`) and its single battle conductor (`race_setup::has_conductor`); ports whose function or call
site the program does not have are left out and listed.
The exact tests check each disc's own release behaviour (`release_behaviour(program())`: PAL's rules at 50 steps a
second, NTSC-J's and NTSC-U's at 60); the game itself always runs 60 steps a second (docs/specs/physics.md §12).

When a test gains a program address, regenerate the table from the discs with `tools/re/program_map.py` (any address in
the harness's sources in the program's range gets a row, and every row of the table that is there is kept, so a
regeneration never drops one; rows whose comment says `check` were mapped from a nearby reference and want a look). The
table has a column per `sim::Program`: NTSC-U, PAL, NTSC-J, then Concept 2001 Tokyo, 2002 Tokyo-Seoul v1.02 and v2.00,
2002 Tokyo-Geneva Europe and Asia (0: no counterpart found; what needs the address is left out or fails with the address
named). Give the discs in that order:

```bash
python3 -I tools/re/program_map.py "gt3-us.iso" "<PAL>.iso" "<NTSC-J>.iso" <the five Concept isos>   # writes program_map.inc
python3 -I tools/re/program_map.py <the isos> --addr 1F39D8 1F3B6C                                               # look addresses up
python3 -I tools/re/program_map.py <the isos> --report r.txt --out /tmp/x.inc    # how each column was mapped (outside the repository)
python3 -I tools/re/version_map.py <three GT3 isos> --sources tools/diffharness src/sim/exact --report r.txt --out /tmp/x.inc
```

The GT3 columns are `version_map.py`'s (the same code matcher as before: a regeneration reproduces them); the Concept
columns come from `build_map.py`'s chain (docs/dev/RE_TOOLKIT.md): function starts by the function map, calls and memory
accesses inside a routine by the aligned call and access sequences, data by the matched routines' references. About 115 of
the 943 addresses have no Concept counterpart yet (instructions that are neither a call nor an access, routines the
compiler inlined or the matcher did not find).

### The later builds (Gran Turismo Concept)

`gt3r_diffharness "<Concept iso>"` loads the program (three segments; the dual-layer Asian disc too), identifies the build
(`program version: Concept 2002 Tokyo-Geneva (Europe)`; a build no one has mapped adds `(races on GT3's core)`) and runs the `DIFF_TEST_ALL` tests against
the build's own rules (`release_behaviour(program())`: the `Rule`s after GT3's 24 take the build's branch, docs/specs/physics.md
§14). `GT3R_PROGRAM=<0..7>` takes the disc for another `sim::Program` (experiments). State of each test per build and what
blocks the rest: docs/specs/physics.md §14.4 and §14.5. In short: the C library, soft-double, phase 2 leaf, collision body, ground
and parameter tests run and show the builds' real differences, and `race_start_exact` and `race_tick_exact` run on the
builds' own layouts:

- **Layouts** (`program_map.h`): `car_off()` / `f_off()` / `car_stride()` (the car record's extra word), `cond_off()` /
  `k_off()` / `Co` (the conductor), `entry_off()` / `entry_size()` (a race entry), `conductor_slot()` (Tokyo-Seoul v2.00's
  and Tokyo-Geneva's two extra virtuals), `holder_off()` (from v2.00 on the database holder's record 8 higher, after the
  table index `race_setup::set_database` builds). `car_exact::snapshot()` / `write_image()` give and take records in NTSC-U's layout, with the
  feedback words the later builds keep in the conductor at their old places. The database's tables are asked for by
  `CarDatabase::place_of`. A test that reads or writes a record or a conductor by offset goes through these.
- **Places inside routines**: another compiler's code has no counterpart for an instruction that is neither a call nor
  an access, and keeps other registers. `race_tick_exact` runs with `LenientPlaces` (such a place reads as 0, its probe
  is not installed, the places are listed) and names an input it cannot read from the original's memory instead of
  stopping. `tools/re/program_map.pins` places by hand what the matcher cannot (data another compiler laid out in
  another order; the race step's places between its loops, which the whole-step checks hang on: read from each build's
  step by the calls around them); a table byte's address comes from `ProgramData`'s locator. A place GT3's code has no
  counterpart for at all (a call a later build makes twice) is a `LaterPlace` (`program_map.h`): the nth call to a
  routine (or to any routine) inside another (an NTSC-U routine or a `later_fn(rule)` key), found in the disc's program;
  one marked `every` is found in GT3's programs too (the race step's call of the pass-by states, whose return the pass-by
  check hangs on).
- **The car in a register**: a port that finds its car in a callee-saved register of GT3's code takes, on a later build,
  the car of the step's per-car routine that runs (`g_phase_car`: a0 at the entry of the last one called); argument
  registers are the same in every build. The step's flag register at its gate is read from the gate's own instruction.
- **Variants and direct calls** (every 30 ticks of the CPU races) poke GT3's layout and are not run on a later build
  (`GT3R_TICK_VARY=2` runs them there, for the work of porting them); a port only they reach is listed as never
  called, not failed.
- **The race information**: a later build's event queue has four more counts per id, so its count and what follows it
  (the session's times) are 4 higher (`queue_count_off()`, `info_off()`); its ids from 11 on are GT3's plus one
  (`ntsc_u_event()`; its own 11, a car that missed its stops, is `sim::exact::kStopsMissedEvent`); a data logger entry is 4 bytes longer (`snap_size()`), and holds
  no feedback words (the conductor's).
- **Calls a later build makes by a jump**: its compiler ends a routine by jumping to its last call, so that call returns
  where the routine's own call does and its GT3 return place has no counterpart. The plan tape takes the traffic step's
  last line request at the step's own return (`kPlanSites`), the race flow check takes the pit machine's effects at
  the per-car routine's return in the step's loop, and the end of a run's last call (its AI driver calls) returns
  through a place of the harness (`kJumpReturn`: the call's return address is turned there, the effect taken, then on
  to the real one). Phase 12 ends by jumping to the tire effect emitter: the emitter's return is phase 12's own. A routine's locals sit elsewhere too: phase 3's pad frame is 0x30
  below the caller's stack pointer, GT3's 0x40.
- **Rules**: `disc_behaviour()` is the disc's release behaviour, on a later build with the routines recognised as a GT3
  release's variant (`RuleSite::as`). `GT3R_RULES_AS=<rule>:<program>,...` sets a routine's variant by hand, to try
  whether a build's routine follows a release's rule before a row says so.
- **Events**: a database without GT3's events (the 2002 builds') races its own first events: `race_tick_exact`'s CPU races
  (six courses), `model_car_params_exact`'s race calls (four), the pad drives on the hit set-up's event (with its car where
  a drive's car is not in the database) and the pit-lane run on the first event whose course has a pit lane;
  `race_start_exact` leaves out an event whose course file the disc lacks (Tokyo-Geneva's `demo_gtc_ca`).
- **Return places the matcher misplaces**: a pinned return address (`program_map.pins`) where a later build's insertion
  shifts the matcher's guess, such as the AI decision's call of the aim point on Tokyo-Geneva (its longer start
  look-ahead sits just before it). A plan call the tape does not hold at the place its kind is asked for shows as the
  port's first differing field (the aim point then reads 0).
- **Routines GT3 does not have** (the race step's four per-car routines of the special cars, docs/specs/physics.md §14.5):
  they have no NTSC-U address, so a port names them by their `sim::Rule`: `later_fn(rule)` is the routine as
  `ProgramData` located it, `later_site(rule)` its call in the disc's race step (the `jal` found in the step),
  `later_tap(rule)` where the routine's second call returns (`program_map.h`; `pa()` and `Pa` take these keys). On GT3's
  discs the ports are left out silently.
- **The later builds' own words of the car record** (`Car::later`: the word at their record's +0x1328 and the eight bytes
  at +0x149C) are read and written beside the snapshot (`car_exact::read_car` / `write_car`) and compared after the field
  table (`first_difference`: `later.ratio`, `later.special[i]`), at the race start too (`start_records` gives them
  beside the record images). The steering force's step count, a byte of the
  conductor's feedback block there, is at its old place F +0x518 in the snapshot.
- **The special cars in a race**: `GT3R_TICK_EVENTS=<event>[tags]%<car label>%<car label>...` adds cars after the event's
  own entries (the `%` cars come last in the label), `GT3R_CARHIT_CAR=<car>` runs the pad scenarios with one. The mood
  routine's rival level is tapped inside the original and compared; `kPokes` sets pass-by states and lap counts on
  2001 Tokyo's `demo_tmv_g`. The whole-step check gives ours the original's pass-by states and gap matrices; the pass-by
  routine itself (0x1F59B0, `pass_by_step`) is checked on every disc at its call at the step's end (the step's proximity
  matrices in, every pair's timer and state compared; reported as a note, a difference fails the test without adding to
  the sample count). A stepless car and the special car in one race, whole steps included (2001 Tokyo):
  `GT3R_TICK_EVENTS="demo_tmv_g%fit_w_ff_01%tmvr_fit_w_ff_01"`. A run that covers every kind (2001 Tokyo; the 2002 builds race their own events, for example
  Tokyo-Geneva's `A_cien_dmv_02_0003%gtcrr_pod_tmv_01%gtcrr_crossfire_dmv_02`):
  `GT3R_TICK_ONLY=later GT3R_CARHIT_CAR=pod_tmv_01 GT3R_TICK_EVENTS="demo_tmv_g%tmvr_r34_gtr_v2_00%tmvr_pod_tmv_01%tmvr_r32_gtr_v2_00%tmvr_pod_tmv_01%tmvr_r35_gtr_tmv_01,demo_tmv_g#finish%tmvr_pod_tmv_01%tmvr_pod_tmv_01"`.

- **The pod race** (docs/specs/physics.md §14.5; test kind 5, `APOD0001` and the split-screen `APO20001` on every
  Concept disc): the race start compares the stop zone our `stop_zone_setup` gives with the conductor's (+0xE408); the
  race flow checks read and compare it with the flow's state (`read_zone`, `zone_differ`: its stop counts and points), the
  per-step rule runs in virtual 14 (`race_mode_clock`), the line's disqualification in the per-car race flow (its stop
  driver 0x1EF8A0 taken from the original's run, `kPodStopDriverCall`); the AI ports get the zone as it was at their
  call (`Call::zone`; the braking's writes on the plan tape, `PlanCall::zone_post`; the decision's lap length for its
  stop point through the conductor's 0x1F2EA8 is a plan site, `kDecideStopLapReturn`); the wheel suspension compares the
  spring rate the later build's caller gives (special car 1's sitting, `pod_spring`); the collision report's check starts
  at the report's entry on a later build (special car 1's moods, `hit_mood`). `#pod` drives car 0 on the pad along the
  race line and stops it in the zone on the odd laps (`Follower::pod`), `#follow` never stops. The runs on each disc
  (Tokyo's pod is `tmvr_pod_tmv_01`, the 2002 builds' `gtcrr_pod_tmv_01`; `GT3R_TICK_AI_SECONDS=150`):
  `GT3R_TICK_EVENTS="APOD0001%P%P%P%P%P,APOD0001#follow+pad%P%P%P%P%P,APOD0001#pod+pad%P%P%P%P%P,APO20001@battle2p#pod%P%P"`.
- **A layout 7 car in a race** (the hybrid system, docs/specs/physics.md §14.5): 2001 Tokyo
  `GT3R_TICK_EVENTS="demo_tmv_g%super_sports_tmv_01"`, Tokyo-Seoul `demo_tmv_b,AEHF0001`, Tokyo-Geneva
  `A_super_sports_tmv_01_0001%acura_dnx_02%super_sports_tmv_01`; `GT3R_CARHIT_CAR=super_sports_tmv_01` runs the pad
  scenarios with it. `race_tick_exact` also compares the set-up's 25 floats with the program's globals (`check_hybrid`,
  found from the set-up routine's first store through the global pointer).
- **The sparks inside the races** (every program; docs/specs/physics.md §14.3 `SparkEmit`, §14.4): `#sparks` gives every
  car of a CPU race its model's spark points and checks the sparks' frame between the steps (each car's emission, the
  update, the register and the whole pool; one note, no samples), `#scrape` drives car 0 flat out on the pad as well.
  Most models have no spark points (those that have: the Formula cars a disc has, the touring and DTM cars and a few
  sports prototypes and GT cars, such as the 787B, GT-One, R390 and Corvette C5-R; 2002 Tokyo-Geneva Europe has no
  Formula car models), so the runs pick races of those cars. GT3's three discs and 2001 Tokyo / Tokyo-Seoul:
  `GT3R_TICK_EVENTS="+Gf1_h_1003#sparks,Gf1_h_1005#scrape,Ggtworld_h_1010#sparks,Gdeu_touring_n_0501#sparks,Gdeu_touring_n_0505#scrape"`;
  2002 Tokyo-Geneva: `GT3R_TICK_EVENTS="+A_tt_dtm_02_0003#sparks,A_tt_dtm_02_0003#scrape~trialmt,A_tt_dtm_02_0002#sparks~grandvalley"`
  (the leading `+`: after the default races, so GT3's discs call every port and the run's count is the default's plus
  these races').
- **Other tests on the later builds**: `race_start_exact`'s default events include the license events raced with their
  cars under the single race conductor (the later builds' flying start, `FlyingGrid`); `gt_grid_exact` on a disc without
  GT Mode races (2002 Tokyo-Geneva) runs the showroom of every database car alone; `model_collide_exact_step` writes each
  car's control source (a quarter are computer cars: the later builds' stuck nudge leaves them alone); `model_ai_course`
  does not call the passing-line lookups where a later build has none of their own (inlined in the lane lookups it checks);
  `control_record_exact`'s manager takes the disc's store layout (`ControlManager::later`, `Rule::RecordStore`);
  `libm_tan_exact` compares tanf (NTSC-U `0x26A6C0`; the later builds' has the same arithmetic).

Debugging aids: `GT3R_TRAP_BACKTRACE=1` prints the host's call stack and the interpreted registers and return addresses
at every trap (a trap from the harness's own read reports the last interpreted pc); `Ee::watch` prints every store into
one word.

The report lists, per scanned function, its match in each program and whether its code differs, and the constants the
functions load whose values differ (where a port needs a version branch).

Each test prints `PASS`/`FAIL`, samples, mismatches, `max_abs`/`max_rel`
error, up to 8 mismatching inputs and the input with the worst error. A trap
(unimplemented opcode, bad address, disallowed call, instruction budget)
fails the test and prints the PC. Exit status is non-zero on any failure.
`--seed` changes the generator seed (default fixed, so runs are repeatable).

## Design

| File | Role |
|---|---|

**Program loading.** `Disc::read_core_gt3` + `CoreImage::decode` give the
image (load 0x100000, entry 0x100008). `main.cpp` derives the rest from the
image itself: `.text` ends after the last `jr $ra` followed by more than 16 KB
without one (0x28E760); the start-up code at the entry gives BSS
(0x353200–0x4B2C0C, cleared by an `sq` loop between `$v0`/`$v1`) and `$gp`
(0x3579F0). RAM is 32 MB; BSS is zeroed; a pristine copy is kept and
`Ee::reset()` restores it (the runner resets before every test; tests reset
between samples when the function writes globals).

**Memory map.** 0x00000000–0x01FFFFFF RAM (also through the kseg0/kseg1 and
0x2/0x3xxxxxxx mirrors), 0x70000000 16 KB scratchpad, 0x11004000 4 KB VU0
data memory. Anything else traps. Stores into `.text` trap (catches runaway
pointers; self tests turn this off). Unaligned accesses trap.
Test scratch: a bump heap at 0x01000000–0x01F00000 (`alloc`, `alloc_cstr`,
`alloc_bytes`, `free_all`), stack top 0x01FFFF00.

**Calls.** `Ee::call(addr, args, max_instructions)` follows the PS2 GCC
EABI: integer/pointer arguments in `$a0–$a3, $t0–$t3`, float arguments
independently in `$f12–$f19` (`Arg::integer`, `Arg::ptr`, `Arg::flt`,
`Arg::fbits`); `$gp` is set, `$sp` = stack top, `$ra` = a sentinel address
that ends the run. The result has `v0`, `v1` (full 128 bits) and `f0`
(PS2 float bits; `f0_host()`). Stack-passed arguments are not supported.
Structures are passed by allocating them in the heap and passing the pointer;
read outputs back with `r32/rf/read_bytes`.

**Call control.** `jal`/`jalr` targets are checked: by default anything in
`.text` is allowed; once a test calls `allow_calls({...})` only those
addresses (and HLE stubs) are. `hle(addr, fn)` replaces a function with host
code (runs on entry, returns to `$ra`); `hle_libc_defaults()` installs
memcpy/memmove/memset/strlen. `mfc0` reads Count (9) as the instruction
counter, PRId (15) as the R5900's, and Random (1) as `Ee::cop0_random`
(default 0). Syscalls, `break`, trap instructions, COP0
beyond `mfc0`/`mtc0`/`ei`/`di`, and VU0 micro programs (`vcallms`, CTC2 to
CMSAR1) trap.

### Instruction subset

From an opcode census of the whole program (not committed: mnemonic, count, functions
using it).

- **Integer:** all MIPS III user-mode instructions the program uses: 32/64-bit
  ALU, shifts (incl. `dsll32` & co.), `slt*`, `movz/movn`, `lui`,
  loads/stores incl. `lwl/lwr/ldl/ldr/swl/swr/sdl/sdr`, `lwu`, `lq/sq`,
  branches incl. likely forms and delay slots, `j/jal/jr/jalr`, `bltzal` &
  co., `sync`, `cache`, `pref`. 32-bit results are sign-extended; the upper
  64 bits of a GPR are only touched by MMI/`lq`. `add`/`addi`/`sub` do not
  model the overflow exception.
- **R5900 specifics:** three-operand `mult/multu` (rd = LO), `div/divu` with
  the R5900 results for division by zero, `mfsa/mtsa/mtsab/mtsah` (SA is kept
  in bytes), MMI (`madd*`, `mult1/div1/mflo1/...`, `plzcw`, MMI0–3 parallel
  ops, `qfsrv`, `pmfhl`, ...; see `ee_mmi.cpp`).
- **COP1 (PS2 semantics, `ps2float.h`):** no denormals (inputs with exponent 0
  read as ±0, results below the smallest normal flush to ±0), no Inf/NaN
  (exponent 255 is an ordinary exponent; results clamp to ±0x7FFFFFFF),
  add/sub round toward zero; the adder drops the smaller operand's bits
  below one guard bit (an operand 25+ binades smaller is ignored), as in
  PCSX2's accurate mode. `mul` is the EE's multiplier: radix-4 Booth rows of
  the first operand's mantissa by the second's (a -0 digit is a zero row; a
  negative row is the one's complement, its +1 in the next row), 4:2
  compressors in three levels, then the two vectors added above column 16
  only (the carry out of the low 16 columns is lost: the chopped product or
  one below). `div` and `sqrt` are its divider: radix-2 SRT on a carry-save
  remainder of 25 fraction bits (sqrt: 26, its root multiples entering the
  adder at 25), the digit from a 3-bit adder of the sum's and carry's top
  bits plus the OR of their fourth bits (Toshiba's selection, US 5,798,955),
  the selection seeing the remainder after a 0 digit only shifted while the
  register takes it through the adder; 25 digits, no correction (the chopped
  result or one above). `rsqrt` is `div(a, sqrt(b))`; a multiply-add whose
  product overflows gives that product (`msub`: negated). Division by zero
  gives ±max; `sqrt` uses |x|; `cvt.w.s` truncates and saturates;
  `madd/msub/madda/msuba/adda/suba/mula` use the ACC register; `max/min`,
  `c.f/eq/lt/le`, `bc1*` (incl. likely). Flags in FCR31 are set
  approximately. The multiplier, divider and multiply-add rules were
  measured as black boxes (outputs only) against PCSX2 PR 12001's soft-float
  FPU built locally (never in this repository: GPL), with the disc's demo
  runs as the hardware check: 0 differences on billions of operands (sqrt on
  every bit pattern), and all 58 demos end on their recorded times (7 with
  exact chopped products and divisions to nearest). VU0 uses the same
  functions; its own units were not measured (no demo runs a VU0 multiply or
  divide).
  `GT3R_FLOAT_ORACLE=<shared library>` runs the interpreter's FPU arithmetic
  on an outside float model instead (`oracle_add` / `_sub` / `_mul` /
  `_madd` / `_msub` / `_div` / `_sqrt` / `_rsqrt`, bit patterns in and out;
  `GT3R_FLOAT_ORACLE_OPS=add,mul,div,sqrt,rsqrt` picks the operations, "sqrt"
  also on with "rsqrt"; not on Windows); `GT3R_OPLOG=<file>` logs every FPU
  and VU0 multiply, divide and square root's operands (`GT3R_OPLOG_UNIQUE`:
  each pair once; record kinds in `ps2float.h`). `gt3r_diffharness --float-check
  <library> [millions]` compares `mul`, `madd`, `msub`, `div`, `sqrt` and
  `rsqrt` with such a library's on random bit patterns (default 100 million),
  every exponent pair, zero and denormal operands and every square root bit
  pattern (about a minute on 12 cores; exit 1 on a difference).
- **COP2 / VU0 macro mode:** `qmfc2/qmtc2/cfc2/ctc2`, `lqc2/sqc2`, the
  FMAC ops with dest masks and broadcast/Q/I forms (`vadd/vsub/vmul/vmadd/
  vmsub/vmax/vmini` + accumulator forms), `vopmula/vopmsub`, `vdiv/vsqrt/
  vrsqrt/vwaitq`, `vftoi*/vitof*`, `vabs/vmove/vmr32/vclipw`, integer and
  VU-memory ops (`viadd`, `vlqi/vsqi`, ...); see `ee_vu0.cpp`. Uses the same
  PS2 float rules.

## Adding a test

1. Find the function and its ABI (address, arguments, what it reads and
   writes) in the RE outputs; the physics spec lists candidates in
   `docs/specs/physics.md` §7.
2. Add a `DIFF_TEST(name, "description")` in a `diff_<area>.cpp` file:

```cpp
DIFF_TEST(sound_note_to_pitch, "note-to-pitch 0x2687B0 vs sound::note_to_pitch") {
    for (int i = 0; i < ctx.iterations; ++i) {
        int note = int(ctx.rng.range(0, 0x7FFF)), center = int(ctx.rng.range(0, 0x7FFF));
        u32 orig = u32(ctx.ee.call(0x2687B0, {Arg::integer(note), Arg::integer(center)}).v0.lo);
        ctx.expect_eq(orig, sound::note_to_pitch(note, center),
                      [&] { return "note=" + std::to_string(note) + " center=" + std::to_string(center); });
    }
}
```

3. For functions taking structures, build the input structure in the heap
   (`alloc`, `w32`, `wf`, or copy a file from the disc with `alloc_bytes` and
   relocate its offsets the way the loader does: see `diff_text.cpp`), set any
   globals the function reads, call, then read the outputs back. Restrict
   callees with `allow_calls` and replace big ones with `hle` stubs.
   `probe(addr, fn)` observes a function entry without replacing it (for example to record the arguments of every call);
   `clear_hle()` removes the stubs earlier tests left installed (reinstall `hle_libc_defaults()` afterwards).
4. Use `expect_eq` for integer/bit-exact results and `expect_near(orig, ours,
   abs_tol, rel_tol, describe)` for floats. Put the tolerance you expect from
   the PS2 FPU vs host float differences (usually a few ulps) and let a FAIL
   show the inputs.
5. When a trap reports an unimplemented opcode, implement it in the matching
   `ee_*.cpp` and add a `SELF_TEST` for it from the ISA definition.

## Current tests and findings

| Test | Original | Ours | Result |
|---|---|---|---|
| `db_id_hash`, `db_id_hash_hle` | `string_hash` 0x231190 (+ SIMD `strlen` 0x269A68) | `db::id_hash` | exact; found that the original masks the byte-sum seed to 16 bits (fixed in `gt_db.cpp`) |
| `db_setting_lerp` | 0x233F60 | formula in `docs/formats/database.md` | exact |
| `sound_note_to_pitch` | 0x2687B0 | `sound::note_to_pitch` | exact (9,504 samples incl. all notes × centres) |
| `text_measure_kerning` | measure step 0x24D230 (kerning profiles via `paddsb`/`pminh`/`qfsrv`), all fonts of `font/system.fnt` and `font/menu.fnt` | `text::layout_text` width | within 1.3e-6 relative (chop vs nearest rounding); strings avoid profile-less glyphs (null-pointer read in the original) |
| `libm_*` | sinf 0x26A5F0, cosf 0x26A1A0, atan2f, expf, powf | `std::` | within 1.3e-6 abs / 2e-6 rel; found sinf/cosf labels swapped |
| `vu0_transform_point`, `vu0_clip_code` | 0x2580EC, 0x24BF10 (VU0 matrix × vector, `vclipw`) | host math | within 4 ulps of the largest term; clip codes exact |
| `model_collide_exact_build` | body build 0x1FAE38 (with 0x1FA740's chamfer) and pair tables 0x1FB0D8, random outlines | `collide::exact::build` / `pair_tables` | exact (every field) |
| `model_collide_exact_step` | the all-bodies step 0x1FA8F8 and the walls-only step 0x1FACB8 on random scenes: a synthetic course in the program's layout (two closed wall loops with corner and sensor flags, a one-cell wall grid, two linked segments), one to three bodies near the walls and each other, crawling with steering, ghosts, spun past 8 turns or 20π rad/s, four steps in a row; the sound / vibration calls 0x1DFEB8 / 0x1DFD98 patched out | `collide::exact::step_all` / `step_walls` with `sim::CourseCollision` on the same course | exact: every field of every body record after every step; on a difference the step reruns function by function on both sides (`collide_exact_probe.h`). Prints how often the original took each path (corners, sensors, holds and their walks, push-out cases, stuck nudge, clamps) |
| `model_ground_course` | the vehicle tick's course ground query 0x18FB58 (0x2280C8) and the ground contact 0x1E8B68 on thirteen disc courses (relocated by the original's loader): points inside triangles of every material and flags class, on their edges and vertices, just outside, near the walls, on grid breakpoints, anywhere over the grid and off it; record 0's hint the triangle's segment, a neighbour, another or none; stale record contents; the body and hubs around the points, both answers of the conductor's virtual 39; the road bump tables 0x1DAE70 builds | `sim::exact::CourseGround` from our parse of the same files, `ground_contact`, `surface_bumps` | exact (79,540 samples): every byte of the four ground records, every field of the car record (and no word outside the field table) |
| `model_collide_course` | the wall grid walk 0x228848 (the sweep callback 0x1FD368 replaced by a recorder), the segment links 0x228B68 (rows a = −1, 0, ..., every b) and the road segment query 0x2280C8 on ten disc courses (relocated by the original's loader) | `sim::CourseCollision` built from our parse of the same files | exact: candidate lists in order, link answers, segments |
| `libm_trig_exact` | sinf 0x26A5F0, cosf 0x26A1A0, sincosf 0x26A4D0 | `ee::sin` / `ee::cos` / `ee::sincos` | exact (82,300 arguments: random, around multiples of π/2, zeros, denormals, the largest) |
| `libm_atan_exact` | atanf 0x26A088, atan2f 0x269FC8 (which returns 0 whenever x + y == 0) | `ee::atan` / `ee::atan2` | exact (40,114 arguments: the range edges tan(π/8), tan(3π/8), 1; y = −x and \|y\| near \|x\|; zeros, denormals, every exponent) |
| `prep_leaves_exact` | phase 2's leaves: lookups 0x1D8FB8 / 0x1D8E50 (random tables, some unsorted or with repeated knots, queries on and next to knots), condition factor 0x1DAD28 with its block 0x1DAB50 rebuilt from random static bytes, wear grip 0x1E9E80 (random curves, wheels in every segment, at and below the knots, the pit flag, wear off; curves with a negative knee reach the segments' t = 1 levels), slip angle 0x1EA348 (fade edges, x = −y) | `sim::exact::lookup` / `lookup_bisect` / `condition_factor` / `condition_params` / `wear_grip` / `slip_angle` | exact (48,000 samples) |
| `model_ai_course` | the AI course object's line queries on the original's own course setup 0x1F9B98 / 0x1F92F8 of the disc's courses (21 by default with pit lines, side maps stored in either order, licence files without road segments, dirt; `GT3R_AICOURSE_ALL=1`: all 80 of courses/US) and on 40 synthetic objects (unordered distances, every kind, arcs either way, absent lines): the line position 0x1FA018 (outputs untouched without a span), the stop line's 0x1FA368, the span search 0x1F9F08, the side map 0x1FA530, the fallback 0x1F9BF8, the passing lines 0x1F9C80 / 0x1F9CB0, the lane lookups 0x1F9CE0 / 0x1F9D38, the lap length 0x18FA10 and the target point 0x1F7658 at short look-aheads, at race distances over the lap and beyond it, at the points' own distances and their float neighbours, the lap's ends, with in- and out-of-range search hints | `sim::exact::AiCourse` (`src/sim/exact/ai_course.h`) as `ai_course_setup` builds it from our parse of the file (compared with the original's object point by point first; the synthetic ones read from memory, `ai_exact_probe.h`), `ai_target` | exact (1,569,722 samples by default, 5,510,485 with all 80 courses; samples whose search would read past a line are skipped and counted) |
| `libgcc_double_exact` | the program's software doubles (GCC fp-bit): `__adddf3` 0x2793C0, `__subdf3` 0x279418, `__muldf3` 0x279480, `__extendsfdf2` 0x27AFA0, `__truncdfsf2` 0x279C50 with the parts they call (unpack_d 0x2790E0, pack_d 0x278FB0, _fpadd_parts 0x279180, __muldi3 0x278F48, unpack_f 0x27A510 with make_dp 0x279C20, make_fp 0x27AF70 with pack_f 0x27A400) (also the AI braking distance's factor (double) b × 0.01 + 1 for every s8 b, step by step) | `ee::D` operators, `ee::widen` / `ee::narrow` (`src/base/ee_double.h`) | exact (134,654 samples at the default iterations: every pair of edge doubles and every edge single, the wear curve's own 1 − a − b, random bits, every class (zero, denormal, Inf, NaN), the extreme normals, widened floats, sparse fractions (rounding ties), cancellation, alignment over 0..70 orders and exponent gaps past 64, under- and overflow, halfway roundings of the narrowing) |
| `model_car_params_exact` | the parameter block P (car+0x10 .. +0xEBF): the spec builder 0x1DA308 (0x2325B8 and its per-table helpers) and the converter 0x1DBFD8 (with its sub-builders and the top-speed walk), from race entries built by CarEquipments 0x2312B0 / 0x231BC0 / 0x231DB0: all cars, every variant an event races (composed here as its base car with the variant's part overrides and stock settings, for the builder's coverage; the race setups below and `gt_grid_exact` build event cars as the race information's 0x238550 does, from the record's parts and settings, and the unit test `exact_demo_licence_cars` checks them against the demos' car records), the arcade cars, every car with random upgrade parts and with random parts and settings; the race's arguments (cleared record, control source 2, rule block, the entry's +0x2104 block) and a spread of others (control source 0, rule bytes, condition words, mode 2, the tyre override, the physics globals before and after 0x1DAE70, random previous record content); 600 runs on specs with random values in every field the converter reads; then the race's own calls in four CPU race initialisations (`race_setup.h`). `GT3R_PARAMS_ONLY`, `GT3R_PARAMS_RANDOM`, `GT3R_PARAMS_PERTURB`, `GT3R_PARAMS_EVENTS`, `GT3R_PARAMS_INIT_MAP=1` (lists the record bytes each function of the race's car initialisation changes) | `sim::exact::stock_equipment` / `build_spec` / `convert_params` / `race_params` (`src/sim/exact/params_build.h`; the program's tables and static bytes from `ProgramData`) | exact: the equipment from our database parse (the words the builder reads), every spec byte, every byte of P and the converter's spec edits, 1511 + 600 runs and 19 race calls; also `physics_globals()` against 0x1DAE70 |
| `race_start_exact` | the race start: whole race initialisations (`race_setup.h`, the conductor's virtual 23 0x1F3628 on the user's disc) of every event with entries (451: cars, variants and arcade cars), by default each also with car 0 a player's entry (its input device set; pedal style, wheel type, aids off, condition words from the entry: `+pad`), a player's rolling start (start type 3: `+pad/rolling`), configuration 2 (the CPU drivers varied by position: `/cfg2`) and a player under the AI (`+pad/cfg3`); per course once with the AI lines' segment words cleared (the loader traces them: `/noseg`), with every class's event overrides set (`/override`), under the time trial (`@tt`, a player, a player in configuration 2; car 1 its ghost), free run (`@freerun`, `+pad@freerun`; car 1 its ghost), free practice (`@practice`, on the courses whose stop line has car 0's pit box; on the others the game's menu rule is checked instead: such a course has no stop line, its course record's +8 read by the program's 0x2389A0 matches ours and FREE RUN's item switch 0x12C430 is off exactly when it is 2 (dirt), as the GT event menu 0x132BA0 sets it, and every course of GT mode's races (menu/gt_mode.mbl's series) without the box is a dirt course: 8 of 34, listed), both battles (`@battle2p:<handicap>`, `@battlemp`), the licence (`+pad@licence`) and the machine test conductors (`+pad@machine`); Sunday Cup race 1's six cars on courses with fewer grid slots (`Gsunday_e_0001~L_seattle1`, `+pad~L_seattle1`, `~smtnorth`, `~r_smtnorth`, `~L_circle60_dry1`: the slots past the grid table read what follows it, with the road block's address for the loader's pointer words; a wheel starting off the road takes the original's ground record's stack words, `car_exact::take_missed_ground`); and the moving start 0x1DD8A8 called directly on a start record with each database car's parameter block at 80, 120, 1 and 400 km/h. `GT3R_START_EVENTS=all` (the 451 events alone, as CPU races) or `<event[+pad][/rolling][/cfg2\|/cfg3][/noseg][/override][~course][@mode[:handicap]],...>`, `GT3R_START_LIST=1` (every differing record word), `GT3R_START_MOVING=0` | `sim::exact::start_cars` (`src/sim/exact/start.h`, through `start_setup.h`: our database's equipment, the course file's road (`CourseQuery`), AI lines (`ai_course_setup`) and driver sets, the event's configuration, the program's tables (`ProgramData`) and the race's setup inputs read from its entries); `ai_course_setup` against the AI course object conductor + 0xBBA8; `moving_start` | exact: every byte of every car's record (0x16DC bytes) after the initialisation, the pointer words pointing at the corresponding objects (race info, conductor, course, the entry's human object, the driver object's links), every point of every line of the AI course object, with the paths' coverage counted from the records (control sources, flying / rolling starts, pit boxes, ranks off the index, traced lines, overrides, locked differentials, spooled turbos, ...). One full run: 3255 set-ups, 11,096 records (16,233,448 words), 976 moving starts, 0 differing, about 107 s (`GT3R_START_EVENTS=all`: 1868 records, about 15 s) |
| `race_tick_exact` | GT3's race step 0x1F39D8 in the harness's EE interpreter, from the user's disc, with every ported function checked where the step (or a ported function) calls it. **Pad races** (`race_hits.h`): the two-car contact races (`GT3R_CARHIT_SWEEP=1` adds the 50 wall side swipes); `tick_scenarios()` (hard braking, braking and turning, lift-off, power and handbrake turns, wheelspin, every ASCC channel on both axles, AI driving by control source 1 and by driver kind 1) and `pad_scenarios()` (per-tick pad frames: steering sweeps past full lock, buttons and analog axes mixed, controller kinds 0-4, steering-wheel types -1..5, pedal styles 1-7, braking to a stop and reversing, shift requests, the hold counter with driver kinds 8 and 1, the race hold); pad drives on the same placement (full-lock circles, a locked stop, reversing with steering, off the road); both cars rolling back on full throttle; scripted manoeuvres (braking to a stop, reversing, full lock in reverse, handbrake, the driver-kind-8 and spawn-counter holds; `GT3R_TICK_MANOEUVRES=0` skips them); a contact race with the longitudinal force's config bytes 0x2D2936/7 at 2; a pad run down Grand Valley's pit lane (`GT3R_TICK_PIT=0`: none); the scripted drives `kPadDrives` (one car alone on the Test Course per layout / differential / pedal style: a standing start with full-throttle shifts, braking to a stop, reverse, steering, the handbrake; synthetic ones with layout 7, 'r' / 't' differentials, a power cap, turbo stage shapes 1 / 2, controller gearbox modes 0 / 2; `GT3R_TICK_DRIVES=<substring>`, `-` for none). **CPU races** (`race_setup.h`, the original's own race set-up) after the start countdown with the race hold (`GT3R_TICK_HOLD` frames, default 90), `GT3R_TICK_AI_SECONDS` each (default 40), listed in `GT3R_TICK_EVENTS` as `<event>[+pad][/worn][~<course file>][@<race mode>][#<setting>]` (`+<event>,...`: the default races and these after them); by default Sunday Cup 3 at Trial Mountain, the Evolution meeting at Laguna Seca, a GT World race at Grand Valley, the Tahiti Maze rally, Sunday Cup 2 at Midfield, the Spider and the 4WD meetings at Deep Forest, the wet Route 5 demo race, the Swiss Alps rally, a GT World race at Trial Mountain on part-worn tyres (`/worn`, `GT3R_TICK_WORN_SECONDS`, default 120: the worn flags, pit requests and stops), with car 0 on the pad (`+pad`) the Smoky Mountain rally and the wet demo race, a Turbo Sports race at Midfield from seeded start states (engine condition counters and tyre wear in every segment of their curves, the HUD message timer, the spawn hold), a Sunday Cup race on L_seattle1's course file (`~`: AI lines without road segments, the line builder's kind-2 arcs); under the other race modes' conductors `@tt` (the time trial at Trial Mountain), `@licence` (a licence test at Grand Valley), `@freerun` (free run at Deep Forest, with a stand-in race object for its second input device and its replay recorders: the lap recording, the best lap's and the ghost's playback stores, whose calls 0x2106D8 / 0x210700 / 0x17AAD8 / 0x1F52B0 / 0x210668 are recorded and the pad frame's record 0x2106B0 stubbed), `@machine` (the 0-400 m machine test), `@battle2p` (a two-car split-screen battle), `@battlemp` (an i.LINK battle with the tyre wear and catch-up settings and its laps in race info +8), `@practice` (free practice), the one-player modes with car 0 alone on the pad and the event's own laps byte; and settings no event uses or a 40 s race does not reach: `#band1` / `#band2` (the rubber band's pass modes 1 and 2), `#flow` (the race-flow byte with a time scale of 0.37), `#finish` (one-lap races of `GT3R_TICK_FINISH_SECONDS`, default 150), `#reverse` (a pad car backing over the start line), `#rolling` (the rolling start's hand-back), `#ai` (car 0 driven by the AI, control source 2), `#runup` (car 0 on the pad with driver kind 1 until its first crossing hands it back), `#follow` (car 0's pad frames follow the AI's line 0 (a follower with stuck recovery; `GT3R_TICK_FOLLOW_SECONDS`, default 240), so the solo modes complete laps, sectors and splits), `#pod` (the same, stopping in a later build's pod race's stop zone on the odd laps), `#leaves` (the same over a later build's leaf ground: the center line's points moved onto the road triangles with flag 0x20 beside them, on the side that has the most; Autumn Ring's verges), `#sparks` (every car with its model's spark points as the app gives them, `race::exact_spark_points` on the day models, the ones the original draws on these day courses (the eve and night models keep the same points but NTSC-U's and NTSC-J's night ml0001 / pd0001: formats/car_model.md), and the sparks' frame between the steps checked: below), `#scrape` (the same with car 0 on the pad following the race line as `#follow` at `GT3R_TICK_SCRAPE_PACE` times its pace, default 2.5, so it bottoms out over crests and kerbs at speed), `#logger` (the data logger's recording pass: race info + 0x70 and + 0x74 set, each car with its input object and table at its + 0x12C), `#logger0` (its playback: + 0x70 only), `#ghost` (free run: a second car, the ghost, F +0x3EA, driven by the AI, put back on the best lap's start by the lap handlers, with the race step's extra run for it in its entry mode 1); by default also the time trial, licence, machine test and free run with `#follow` and `#logger`. State pokes (`kPokes`): spawn / stop holds with headings some turns out, a tipped and spinning body, a 2 m drop, steer-coupled camber, a zero swing arm, the AI's stop driver kinds 5-7, a car turned round and hit at the front, one turned 57 degrees and hit at the rear, the arc tick cap, the end of a run 0x1EACB8 in each of its modes and a human's; other force feedback settings per race and condition words near their limits. The VU0 random register is seeded as each run is set up. **Checks.** A port (`kPorts`): at the function's entry, only when $ra is its call site's return address, the car record is snapshotted (`Port::car`: where the record is when a0 is not the car); at the return address ours runs on the snapshot with the call's inputs (`Call`: arguments, values tapped inside the call, conductor bytes, our course queries); `only` limits a port to the calls on its path (the others counted as skipped); ports with variants also run every 30 ticks of the CPU races on perturbed copies of each car's record against the original called directly (`GT3R_TICK_VARY=0`: off); phase 11, called once for all cars (`kRacePorts`), is snapshotted on every car with the conductor bytes it writes. The AI's calls into the line evaluation and speed planning (`kPlanSites`: 0x1F7088, 0x1F7658, 0x1F7590, 0x1F8980, 0x1F9CE0, 0x1F9D38, 0x18FA10 where the AI's own functions call them) are recorded on a tape every race step; an AI port of the step's own part runs on the tape (`TapePlan`, an `AiPlan`) call for call, so each part is checked on its own, and the plan's ports at their own call sites on our AI course object. The AI's code in the original (`kAiCode`) is marked as it runs and the stretches no race reached are listed. The race step as a whole (`StepCheck`): `tick_until_collision` and `tick_after_collision` for every car, compared at the collision step's call and at the end of phase 13 (with the proximity matrices and the random register), phase 3 on `CoursePlan` over our course setup's AI course object (`GT3R_TICK_AI_TAPE=1`: on the tape), the tyre effect emitter's random draws from the original's run (a later build: each race's course object gets the original's particle manager, built by its own constructor, as RacePS2Base::vf9 gives it; our leaf calls run on our requests in the step check, the register after each car's emitter and the manager at the end of phase 13 compared, and the CPU races update both managers between steps; reported in one note, counted in no sample, so the counts stay as the ports make them); in the `#sparks` / `#scrape` races the sparks' frame RacePS2Base::vf30 and RacePS2Base::vf29 run after the step (`StepCheck::spark_frame`, `sparks_probe.h`): each car's emission 0x1AADD0 in entry order on a model holder made per car (its model's GTCI, the car record at the holder's +0x6AC / a later build's +0x6C8) with the program's VU0 micro code for its vcallms, ours (`Sparks::emit`) on the original's record as the step left it with our road query and the car's draw mode from the race's words, the register compared after each car, then both updates and the whole pool (a later build's: the leaves' manager, the pool they share; GT3's made in the interpreter, the tuning block by its static constructor's routine); the register the frame leaves is the next step's in both runs; reported in one note, counted in no sample; then the step's loop after phase 13 (0x1F3DF0 .. 0x1F3E4C, `tick_flow`: virtual 13, the race flow 0x1EF060 of each car of the race order with the pit lane and the end of a run) from our records, compared at its end on every car, the race's end countdown, its event queue (the race display's posts too: type, priority and the word's bits the event sets), the race flow's records and the pit lane's display events; the free run ghost's extra step (the step called again inside the step, 0x1F1400's call) is checked as a step of its own and its changes applied to the outer step's check. The step's own parts (`FlowCheck`, `tick_race.cpp`): the race-flow gate at the step's start and the rubber band after the race flow, every car. The race flow alone (`RaceFlowCheck`, `race_flow.h`): virtual 13 (`race_mode_start`), each car's 0x1EF060 (on our road's `CourseQuery`), virtual 1 (`race_mode_display`) and virtual 14 (`race_mode_clock`), compared on the cars, the race clock, the end countdown, the race info's result words (+0x40 / +0x44 / +0x48 / +0x4C), the session's and every entry's time records (lap lists, best and current splits, best lap, top speed), the single races' sector order, the data logger's table entries (every byte of the snapshot but the collision body's local geometry), the free run's recorder calls and its lap recording's header, and the events posted (the race display's words by the bits each event sets), with the calls inside that other checks cover (0x1EDB40, 0x1EACB8's AI driver calls, the hand-backs 0x1E3BF0, the split-screen battle's AI take-over 0x1F6A20) applied from what they changed in the original's run; virtual 41 (`hit_course_out`) at the collision step's hits (0x1DFEB8's call); then the gap matrices (0x1ED458) and the ranking (virtual 3) at their calls. Called directly every 30 ticks on changed states: the course-out rule (virtual 40, 0x1DA020) on copies of each car's record (the flag, control source, course-out ticks, progress in and around the pit stretch, every pair of wheel counts); the conductors' virtuals 1, 13-19 and 41 (car i's finish, ghost, control source, driver kind, course-out, index, best and last crossing, top speed, stop-zone ground and speed, run-up; the other cars' control sources; the race info's replay, judging, locked, data logger and result words, the session's and the entries' time records, the sector order, a stand-in licence record's judging times; the free run's recorders (usable, best, ghost); the race-flow byte; the calls' arguments); the gap matrices and the ranking (laps, lap base, progress, finish flags, crossing times, best laps, a shuffled order; free practice's 0x1F24C0 too); also the angle helpers 0x1D8C00 / 0x1D8C80 at 6000 random and edge angle pairs. The inputs outside the record (`tick_inputs.h`) are built as the game builds them (`race_inputs.h`: the program's tables from the disc's CORE.GT3, `ProgramData`, and the race's event, course and conductor; the course-out rule's pit stretch and lap length and the spawn hold's pit box headings from our AI course, `ai_course_setup` on the race's course file), fed to every port and to the step check, and checked against the original's memory on every call (a variant on its changed memory) and at every race's start, with the race configuration block, the wear curve, the rubber band's parameters, the race-flow globals, the proximity matrices, the AI course object and the conductor's answers to virtuals 10, 11, 32, 35, 37, 38, 39 and 40 (called directly); the start countdown is read from memory. The CPU races (every conductor) also start from our own start records (`start_setup.h`, `start_cars`): each compared with the original's after the initialisation, then the step check carries our phases on our own results for the start hold and `GT3R_TICK_FROM_START` steps more (default 60; 0 off; what the step does outside the ported phases, the collision step and the pokes copied from the original's records); a course file with fewer grid slots than the race's cars (the `~` race) starts its cars past the grid table as the program does (the wheels starting off the road take the original's ground records' stack words, `car_exact::take_missed_ground`). **Ported**, each at every call site: phase 1 (0x1EADB8); 2 (0x1EA428 with 0x1DDFF0, 0x1ECFC8, 0x1DB118, 0x1E0BC0, 0x1E0C28, 0x1EA1E8 with 0x1E9E80, 0x1EA3D8; 0x1EA348 through 0x1EA3D8 and `prep_leaves_exact`); 3 (0x1E4D38 for every control source: the pad path's 0x1E4290, 0x1E4CB0, 0x1E4478, 0x1E4368, 0x1E4320, 0x1E4180, 0x1EAC60 and its driver object update 0x1F6648, the AI paths' apply 0x1E4CF0 / 0x1F79F0 / 0x1EAC90, the AI's step 0x1E35D8 with the decision 0x1F7AE0, the traffic step 0x1F6D70, the start hold 0x1F8A00, the stop driver 0x1F7810, the parked car 0x1F7900, the steering slew 0x1F77B8 and the gear hint 0x1F6588; the lock curves' layout in P checked on every call), with the AI's lines and speed plan on our AI course object (ai §2-§4: 0x1F79A0, 0x1F7088 with 0x18FA10, 0x1F6958, 0x1F6678, 0x1F6A20, 0x1F8958, 0x1F8980 with 0x1F9BF8, 0x1F9CE0 / 0x1F9D38, 0x1F7948, 0x1F7658 with 0x1FA018 / 0x1FA368, 0x1F7590 with 0x1F7520; variants of the braking point, the re-plan, the tick start and the speed controller, `Vary::args` for their other arguments); 4 (0x1E8978); 5 (0x1E7328); 6 (0x1E3B18 and its 13 parts); 7 (0x1E8598 whole, and 0x1E6EC8, 0x1E84D8, 0x1E73C0, 0x1E7450, 0x1E7C08, 0x1E7848, 0x1E5768, 0x1E57F8, 0x1E5B98, 0x1E6488, 0x1E7500, 0x1E77E0, 0x1E7D90, 0x1E8008 with 0x1E6D48, 0x1E6CA8, 0x1E17F0, 0x1E0ED8, 0x1E1768, 0x1E5998, 0x1E5B30, 0x1E6D00, 0x1E5690, 0x1E4E78, 0x1E4FF8, 0x1E5458, 0x1E5540, 0x1E5600); 8 (0x1E6928); 9 (0x1DFD90); 11 (0x1EC098); 12 (0x1EB218 whole, and 0x1E9D48, 0x1E9E18, 0x1E8B68 with the course query 0x18FB58 (`CourseGround`), 0x1E8A48, 0x1E8AC0, 0x1E8AE0, the course-out rule 0x1DA020 and its counts, 0x1E91A8, 0x1E9348 with 0x1E8E58, 0x1E96F0 with 0x1E9528 and 0x1D8A80, 0x1E9998 with 0x1E9CE8 and 0x1E9010, 0x1DF2B0, 0x1E0068, 0x1DEBB0, 0x1DF148, 0x1DFC60, 0x1E06D8, 0x1E0768, 0x1E9DE8 with 0x1E9D48, 0x1EA4D8, 0x1EA9B8, 0x1EAFC0, 0x1DF150 and each tyre effect request at the emitter call 0x18FDE0); 13 (0x1E8190, 0x1E8340, 0x1E6A38); after phase 13 the race flow 0x1EF060 with 0x1EE978 and the race progress (virtual 28: 0x1EE900 with 0x18FA20, the machine test's nose 0x1EE878), the pit lane's state machine 0x1EDB40 with 0x1EAB80, the AI's pit switches 0x1E3BF0, 0x1E3D08, 0x1E3D60, 0x1E3DA8, the end of a run 0x1EACB8 (in the pit lane and at the lap handlers' finish) with 0x1E3C78, 0x1E3E38, 0x1E3E80 and 0x1F6518 (variants: every pit kind and section, a service due or not, the wrong way, every end-of-run mode) and the race modes' virtuals 13, 14, 17-19 and 41; the race-flow gate and the rubber band (virtual 4 0x1ECEB8 with 0x1ED028, 0x1ECDA8, 0x1ECE40, the i.LINK battle's 0x1F0478); the gap matrices 0x1ED458 and the ranking (0x1ED1F0, free practice's 0x1F24C0). `GT3R_TICK_ONLY=<substring of a port's name>` runs only those ports. To port a function: its fields to `car_record.h` and `fields()` (`car_fields.h`), the port to `src/sim/exact`, a line to `kPorts` in `diff_tick_exact.cpp` | `sim::exact` (`src/sim/exact/`: `tick.h`, `pit_lane.h`, `race_flow.h`, `race_inputs.h`, `start.h`) on the car record mirror `sim::exact::Car` | exact: every field of the field table (`src/sim/exact/car_fields.h`) after every call, and what a call leaves outside the record (return values, the caller's arrays, the proximity matrices, the VU0 random register, the tyre effect requests, the course-out hook's counts, the race's words and events) through `Call::out`; also lists the record words the original's call changed that no field covers, and counts the calls that changed the record. One full run (default options, NTSC-U): 527,945 calls of each per-car phase, 246,247 race steps, 527,945 per-car race flows (every race mode's), 41,208 direct calls of the race flow's virtuals, 35 races started from our own start records (15,750 steps carried on our results), 0 differing, about 765 s on a loaded machine (`GT3R_CARHIT_SWEEP=1`: about 755 s); the race modes' CPU races (time trial, licence, machine test, free run with the data logger, the follower and the ghost) also 0 differing on the PAL and NTSC-J discs |
| `race_exact_lockstep` | whole races of the original's race step 0x1F39D8 side by side with ours, every step: race_tick_exact's CPU races (`GT3R_LOCK_EVENTS`, its labels and tags; default its list: every conductor, pad cars, wear and the pit lane, the race-flow gate, both rubber band modes, the rolling start, finishes, its state pokes and force feedback settings), race_hits' two-car pad races (contact, the wall lane, `tick_scenarios`, `pad_scenarios`, the placed drives, rolling back, the manoeuvres, the force config bytes at 2, Grand Valley's pit lane; `GT3R_CARHIT_SWEEP=1` adds the 50 wall side swipes; `GT3R_LOCK_PAD=0` none) and the scripted pad drives `kPadDrives`; the original runs the course's tyre effect emitter (RacePS2Base's particle system at RaceCourse + 4 on the course's Env0 block, reset by 0x1A4630); also the race information's start countdown 0x17A5E8 called directly (every start type, a restart, the timer; its display stubbed) | `sim::exact::ExactRace` (`src/sim/exact/race.h`) built from our own data (`RaceCourse` from our parse of the course file, each car's `StartEntry` from our database with the race's setup inputs as the original's entries hold them, `start_cars`), fed the same pad frames (each car's phase 3 frame in the original) and start countdown (race info + 0x5C); the scenario's own changes to the original between steps (pokes, placements, end-of-run calls, body flags) made to ours as the same field changes; `race_info_frame` | exact: after every step every car record (the field table; record words changed outside it listed: none), every collision body record, the VU0 random register, the race clock, end countdown, result words and order, the proximity and gap matrices, the race's event queue posts (the race display's too), the race flow's records (the session's and entries' times, the sector order, the data logger's tables, the free run's recorder events and lap recording, its ghost's extra steps with the frames the original read in them), the pit events, every collision response's report (0x1DFEB8: car, kind, part, strength) and every tyre effect particle (all 0x40 bytes but the pool links); a copy of each race taken at its first step and replayed alone at its end matches when nothing was changed after the start (`GT3R_LOCK_REPLAYS=<n>`: the fastest of n replays is timed). One full run (`GT3R_CARHIT_SWEEP=1`): 153 races (35 CPU races under every conductor, among them the free run's ghost races and the data logger's, 118 pad races and drives), 251,925 steps, 30,377 collision reports (20,377 wall, 10,000 car), 159,642 particles, 318 race events, 0 differing, about 250 s; the `~L_seattle1` race (more cars than grid slots) runs too, its cars past the grid off the road from the start (their ground records' stack words from the original's). Ours alone in a Release build on the development Mac (under load): about 77 us per six-car step (13 us per car) |
| `race_exact_demos` | every demo run on the disc (`demo/<event>`: 48 licence tests, 10 arcade time attacks; `GT3R_DEMOS=<codes>`) as the original's race: the event under its conductor (licence, time trial), the replay header's car record written over entry 0 as 0x18D3C0 loads it (equipment, condition words, EQ +0x1E8, grid slot, pedal style, spec flag, wheel type, ASM, TCS), the start type as RaceBase::vf9 sets it, run by the race information's frame before every step; entry 0's input device plays the recording with the original's own playback (+0x130 = 1: the pad read 0x210740 -> 0x1F4660 on the save's recorder block); a second decoder of the same stream checks `DemoReplay`'s frames | `sim::exact::ExactRace` from the same setup (`lockstep_setup`, entry 0's equipment `equipment_image` of the record) and the original's phase 3 frames, in `Lockstep` (race_exact_lockstep's comparison) | exact: every step as race_exact_lockstep; the decoded frames equal the original's decoder's; the run's time (the licence's judged result, the time trial's lap) against the save's. Full run: 58 races (licence 48, time trial 10), 259,521 steps, 2,365 collision reports, 107,564 particles, 0 differing, 0 frames decoded differently, 7 runs on their recorded time (the others leave that run on the original too: RACE.md "Demo runs"); about 110 s. Options: `GT3R_DEMO_EXTRA=<steps after the recording>` (600), `GT3R_DEMO_VERBOSE=1` (each run's end and contacts), `GT3R_DEMO_TRACE=1` (car 0 every 30 steps), `GT3R_DEMO_DUMP=1` (the header's words), experiments on the original alone: `GT3R_DEMO_ORIG=1` with `GT3R_DEMO_R=<hex>` (the random register at the start: no effect on any run), `GT3R_DEMO_SHIFT=<n>` (the recording n frames later / earlier: worse on every run), `GT3R_DEMO_REPLAY=1` (race info +0x6C: no result) |
| `control_record_exact` | AutomobileControlRecord: the manager 0x1F4ED0 bound to the record memory's pool (0x1F4FB0, the pools at [0x3529B0]), the recorder's rewind 0x1F44C8, recording 0x1F4998, close 0x1F4C00, playback 0x1F4660 and seek 0x1F47D8; six frame sequences (the pad races' scripted frames, a stick driver, a digital pad, every byte random every frame, the largest changes with long holds, nothing at all), each recorded both over a whole pool and over a store (whose recordings close themselves when full), played back past their end and sought to frames along them | `sim::exact::ControlRecorder` (`src/sim/exact/control_record.h`) on the same memory | exact: every byte of the record memory and every recorder word after every frame and at the close, every played frame, the recorder after each seek; the difference classes' table 0x2D8EE0 against ours. All three programs: 82,800 frames recorded (2 recordings closed themselves full), 82,836 played, 144 seeks, 0 differing, about 1 s |
| `race_ghost_exact` | the solo modes' ghosts on the original's own recorders: the race object's input devices (RaceInput 0x2104E8) and record memory built in the original's memory as RaceSolitaire's constructor 0x186E10 builds them (RaceFreeRun's devices in mode 2), each run started and ended as the race object starts and ends it, the pad read 0x210740 recording the player's car (a line follower's pad frames) and playing the ghost car's; the free run's recorder calls (0x2106B0, 0x2106D8, 0x210700, 0x17AAD8, 0x1F52B0, 0x210668; the Options' Ghost on). Per mode (`GT3R_GHOST_EVENTS`, default a time trial, a licence test, the machine test and a free run; `GT3R_GHOST_SECONDS` a run's length, default 240; `GT3R_GHOST_PACE` the line follower's speed scale, default 1; a licence run's judged result words, race info + 0x40 / + 0x44, are printed against ours): run 1 records a run, run 2 races its ghost (the whole run, 0x17AAD8's kind 0; the free run: the visit's next run with run 1's ghost carried) | `ExactRace` with `SoloRecorders` (`RaceSetup::recorders`, the ghost car's device its own), in `Lockstep` | exact: every step of both runs as race_exact_lockstep, the free run ghost's extra steps among them; after run 1 the record memory and every device's recorder byte for byte. 8 runs (two per mode), 8 record memories the same, 103,650 steps with a ghost car (85,971 frames played, 5 ghost restarts, 2 extra steps), 0 differing, about 87 s. NTSC-U only (PAL's and NTSC-J's race objects are laid out differently) |
| `analyzer_replay_exact` | GT3's Analyzer over two runs of a time trial, licence test and machine test: runs A and B recorded by the original's recorders (as race_ghost_exact's run 1, B's driver slower), then the logger's race (RaceSolitaire::vf58's devices +0xCB50 / +0xCC98 both playing: car 0 run A's recording, car 1, a ghost car, run B's; race info + 0x70 / + 0x74) until A's recording ends; then the REPLAY view's jumps 0x1873B8 to entries along the lap (`GT3R_ANALYZER_EVENTS`, `GT3R_ANALYZER_STEPS` steps after each, default 600) | `SoloRecorders::analysis` and `ExactRace::logger_jump`, in `Lockstep` | exact: every step of the pass (the logger's tables among the race flow), after each jump both cars, their collision bodies and their devices' recorders, then every step after it; and the original's race after a jump against its own pass at the same device frame (position and velocity, while the recording plays). 3 passes (43,473 steps), 15 jumps (9,000 steps after them), 0 differing; after every jump the original's cars are the pass's bit for bit (no drift); about 85 s. NTSC-U only |
| `sparks_burst_update_exact` | the sparks' burst 0x19F450 and update 0x19F698 / 0x19E7C8 on a ParticleManager made in the interpreter (the pool, its lists by 0x19EFF8, the tuning block 0x2C03B0 by 0x19F898; a later build's manager by the program's own constructor as `leaves_exact`'s, its particle record 4 bytes longer): random bursts (any direction and speed up to 90 m/s, planes tilted up to 25 degrees, 1..25 particles, into a full pool) and frames of updates (drag, gravity, the kick window, bounces) at the build's frame time. Every program | `sim::exact::Sparks` (`SparkRules`) | exact after every call: the VU0 random register, the lists, every particle's mirrored words (a later build's: every active particle's) (2,290 bursts, 11,558 particles, 4,800 updates by default; 2002 Tokyo-Geneva Europe at 1/50: 13,124 particles) |
| `sparks_emit_exact` | the sparks' emission 0x1AADD0 (with the road query 0x18FC18 on the course relocated by `race_setup::load_course`, the VU0 matrix library in the interpreter) and the update, every frame of an F090/S driven round Trial Mountain, Grand Valley and Rome by our `ExactRace` (its car record written to the original's memory, the register as our race step left it); vcallms 0x1D0 in 0x257A44 run by a small executor of the program's own VU0 micro code (0x349600). Every program; a disc without the F090/S's model (PAL, 2002 Tokyo-Geneva Europe) drives the Calibra Touring Car (`GT3R_SPARK_CAR`: another car). From 2002 Tokyo-Seoul v2.00 on the emission's body plane (Rule::SparkEmit) | `ExactRace::step`'s sparks | exact every frame (21,600 frames; NTSC-U 73 with bursts, the later builds 19 .. 115) |
| `leaves_exact` | a later build's particle manager built by the program's own constructor (GT3's 0x19EEF8's counterpart; its allocation, its file load of race/particle.img and its texture set-up stood in for): its eight leaf shapes; random wheel requests to the leaf maker (Rule::Leaves: slips up to +-20 m/s, any heading, ground planes tilted up to 25 degrees, car velocities up to 70 m/s; every other sequence a storm that fills the pool) and frames of the manager's update (0x19F698's counterpart, each leaf's vf1) at the build's frame time. GT3's programs: noted, no leaves | `sim::exact::Sparks` with its leaves (`leaves.h`) | exact after every call: the VU0 random register, the lists, every leaf's record (+0x08 .. +0x4C), the shapes (40,363 maker calls, about 15,000 leaves made, half as many calls into a full pool, 7,200 updates, on each later build) |
| `leaves_emit_exact` | the tire effect emitter (0x1DF150's counterpart) on our `ExactRace`'s car driven over a course with leaf ground (the first event on one: Autumn Ring), every frame: the car record as our step left it written to the original's memory (its course object with the manager and no smoke), the emitter run from our race's register, then the update; the driver seeks the leaf patches and spins and locks the wheels on them (`GT3R_LEAF_EVENT`, `GT3R_LEAF_CAR`: another event or car) | `tyre_effect_requests` (the ground's flag byte), `Sparks::leaf`, the update | exact every frame: 6,000 frames, 13,000 .. 18,600 maker calls and 1,800 .. 3,800 leaves made per later build |
| `car_gear_generator` | computed-gearbox ratio generator 0x1E28A8 on random spec inputs | `sim::generate_gear_ratios` (PS2 arithmetic, `ee::F`) | exact (12,984 words) |
| `car_surface_index` | 0x1E8A48, materials 0-15 x all flags | `sim::surface_index` | exact |
| `race_road_roar` | 0x1DF9D8 road roar of one wheel (volume, pitch) with the axle's tyre width, random rim speed, surface, airborne counter | `race::road_noise` | pass |
| `rng_step`, `rng_unit`, `rng_shared_int`, `rng_gt_skip_draw`, `rng_gt_stir` | the generator step 0x26C118, unit 0x26C178, the shared state's integer draw 0x26C0C0, the GT-mode state's skip 0x211AC0 / draw 0x211B00 / stir 0x211A68 (COP0 Random from `Ee::cop0_random`) on edge and random states | `GameRandom` (`base/game_random.h`) | exact (0 of 65,000) |
| `rng_trophy_pick`, `rng_prize_pick`, `rng_prize_paint` | trophy pick 0x1390D0 (table built by its static initializer 0x139CA0; game status and completion stubbed); the car_get code slices 0x13D138..0x13D22C (weighted prize-car pick) and 0x13EA28..0x13EA84 (paint roll), run from their first instruction with the registers and stack words they read, ended by a stub | `game::pick_trophy_index`, `pick_weighted`, `prize_paint_index` | exact (0 of 25,000) with PS2 float adds / multiplies (`base/ps2_float.h`) |
| `rng_pool_draw` | RaceInformation virtual 13 0x179D70 (opponent pool draw) with the pool count / entry / region test / assignment and the paint pass stubbed; pools 1..32, random unavailable entries, more draws than entries | `race::draw_pool` | exact (0 of 5,000) |
| `gt_display_power` | menu max power: set-car 0x231DB0, CarEquipments part words written, showroom build 0x1D93C0 with condition words {odometer, E, O}, peak search 0x1E14E8 (show+0x1294: PS, rpm) and the text value 0x1D9680 (computed branch, units word 0 / 1) for every car, stock and random tune-shop parts (no turbo + NA tune together) with random condition | `game::spec_power_peak(sim::build_car_spec, GtMode::engine_factor)` (the factor on each looked-up torque, as the original), `display_power` | pass (7,254 builds at --iters 3, worst 7.6e-7 relative; 2 peak-rpm ties on flat plateaus within 2e-6 counted as ties). Found: the port polish factors apply twice (the builder calls its helper twice) and the part-factor order; the break-in factor holds 97 % below E = −300 km (GtMode had extrapolated); the menus truncate (and HP = PS × 0.98619) |
| `gt_grid_exact` | GT Mode's starting grid (every program): (1) the showroom build 0x1D93C0 of every database car and every pool car of a GT Mode race (entries built by race_setup::build_entry, a fresh engine): the spec's mass +0x2A, the peak 0x1E14E8 at +0x1294 (PS, kgf m, both rpm) and 0x1D9680's whole power with the units word 0 (PS) and 1 (HP); (2) the race grid 0x12E670 for every race of menu/gt_mode.mbl and the championship grid 0x18E190 (six-car championship rounds; its own places against the entries' +0x20E0), entry 0 a random pool car and the opponents pool cars in a random order, in PS and HP, with 0x1D93C0 replaced by the words part 1 recorded for each car (operator new[] 0x27C2D8 / delete 0x27C0C0 on the harness heap) | `sim::exact::showroom_build` (new: 0x1E14E8 as `engine_peak` over the converter's P), `shown_power`; `race::exact_grid_figure`, `gt_grid_places` | pass on US 1.10, PAL and NTSC-J (US: 742 showroom builds bit for bit; 1,492 grids, 337 races and 184 championship rounds, 1,354 of them with equal figures). NTSC-J's 0x1D9680 (0x1D70C8) has no unit conversion, so its "HP" is PS. Found: ours had ranked exact kg-per-PS floats (mass over the curve's peak torque × rpm); the original divides whole numbers in the language's unit and keeps entry order for equal figures |
| `race_course_tires_exact` | the course's tyres RaceBase::vf40 0x17CC80 (every program): every car of tables 30, 31 and 35 (race_setup::build_entry) in one race information per 64 cars (+0x18 the entries, +0x28 a database holder whose +0x20 is the course id), on the first dirt (category 2), wet (1) and road (0) course of the database | `sim::exact::course_tires` on each car's tyres before the call | pass on US 1.10, PAL and NTSC-J (US: 1,041 cars, 550 refitted on dirt, 1,007 on wet, none on road). Found (issue 112): our races never fitted them (only the arcade car page did), so the rally and wet licence cars, GT Mode's garage cars and the arcade dirt opponents raced on their dry tyres |
| `gt_field_exact` | GT Mode's field (every program): RaceGTmodeInformation's constructor 0x18F3E0, the event 0x179C48 and its virtual 12 0x18F540 (the pool draw: virtual 14, 13 0x179D70, each drawn car built by 0x238550 from its record's parts and settings; the paints 0x176FD0 stubbed) on a race of six empty entries, the database holder 0x2EBD88 and the language index 0x352FD8 set, for every GT Mode race of more than one car (297) and three draw states (0x351DE0) | `race::draw_opponents` and `race::exact_equipment` (`sim::exact::variant_equipment`): the spec `build_spec` gives each | pass on US 1.10, PAL and NTSC-J (4,455 drawn cars each; 2,181 on US carry an engine output factor above 1000). Found (issue 112): ours built event cars with stock settings, without the record's settings and output factor |
| `gt_wheel_inventory` | the wheel inventory on the game state (the pointer 0x352A94): a new game 0x218CF0 (its record, licence and arcade set-ups skipped, the game config's sums from our data), a ride car in garage slot 0, then the purchase 0x2196A8 for 200 wheels and the shop's fit 0x126160 on a held wheel and on the one the full inventory refused | `GtMode::buy_wheel` / `fit_shop_wheel` | exact (610 samples: credits, inventory entries with the placeholder, the ride car's wheel id): 199 bought, the 200th neither added nor charged, and the fit puts the refused wheel on the car |
