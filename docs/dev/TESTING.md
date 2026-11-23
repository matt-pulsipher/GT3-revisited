# Which tests to run

What to run for a change, and about how long it takes on the development Mac. The rules for contributors are in
[CONTRIBUTING.md](../../CONTRIBUTING.md#tests); the harness is [DIFF_HARNESS.md](DIFF_HARNESS.md).

## Tiers

A merge to the private master needs only the change's own tier. The whole suite runs on no disc and US 1.10 only;
the other discs run the changed area's tests, picked by a name filter, not whole suites:

| Change | Run | Time |
|---|---|---|
| Every change | the whole unit suite with no disc and with US 1.10; the program-code check (`tools/check_program_code.py`) | about 1 min per suite; the check seconds |
| Beyond that, the changed area | its tests (a `gt3r_tests` name filter) on PAL and NTSC-J, and on a Concept disc where the area applies (opening discs, the program locator, the menu flow: `GT3R_DISC_CONCEPT=... gt3r_tests concept_disc`) | seconds to a minute |
| Physics or race code (`sim/`, the race flow, collisions, the AI) | also the diff harness on US 1.10, and `race_tick_exact` on PAL and NTSC-J | about 20 min for the US harness (`race_tick_exact` about 11) |

The filter is a substring of the test names (`ok` / `FAIL` lines; `grep -h 'TEST(' tests/*.cpp` lists them). A change
to the GT Mode dealer menu, for example, runs on the other discs as (`$PAL`, `$JP`: see Commands):

```bash
GT3R_DISC="$PAL" ./build/gt3r_tests gtmode_menu_disc_dealer
GT3R_DISC="$JP" ./build/gt3r_tests gtmode_menu_disc_dealer
```

A test that only reads NTSC-U data (`test::test_disc()`) skips on PAL and NTSC-J ("skip ... needs GT3R_DISC"); the
ones that work on every region use `test::any_region_disc()`. If the area's tests all skip there, the change needs
a test that runs on every disc.

Playtest builds are built right away from the branch, with the two unit suites of "every change" (about 2 min) beside
the build. Nothing else holds them up.

The full matrix runs before a release, or in the background; it never blocks a merge or a playtest
build. It is the unit tests with no disc, US 1.10, PAL and NTSC-J (US 1.00: `disc_identity` only), with
`GT3R_DISC_CONCEPT` set (`program_locate_other_builds`, and every `concept_disc` test: its data, languages and the
front end's flow), the whole harness on US 1.10 with `GT3R_CARHIT_SWEEP=1`,
`race_tick_exact` on PAL and NTSC-J, `race_start_exact` with `GT3R_START_EVENTS=all`, and the platform hashes on
x86-64 as well as arm64 (the no-disc unit suite on both; the `*_platform_hash` tests pin the values).

US 1.00 (`SCUS-97102`): only `GT3R_DISC=<US 1.00> gt3r_tests disc_identity` (it opens as the NTSC-U release), in the
full matrix. Its CORE.GT3 and GT3.VOL are byte-identical to US 1.10's, so every other test repeats the 1.10 run.

One-off, in no tier: when disc support changes or another NTSC-U printing turns up, compare the two US discs byte for
byte, `GT3R_DISC=<US 1.00> GT3R_DISC_IDENTITY=<US 1.10> gt3r_tests disc_identity` (CORE.GT3 and every GT3.VOL file as
stored; about 3 s). Nothing else sets `GT3R_DISC_IDENTITY`.

## Commands

```bash
D=<folder with the disc images>
US="$D/gt3-us-1.10.iso"; US100="$D/gt3-us-1.00.iso"
PAL="$D/gt3-pal.iso"; JP="$D/gt3-ntsc-j.iso"

# Whole unit suites, all at once (every change: the first two; the full matrix: all four); each ends with
# "N failure(s), M skipped"
./build/gt3r_tests > none.log &
GT3R_DISC="$US" ./build/gt3r_tests > us.log &
GT3R_DISC="$PAL" ./build/gt3r_tests > pal.log &
GT3R_DISC="$JP" ./build/gt3r_tests > jp.log &
wait; tail -n 1 none.log us.log pal.log jp.log

# US 1.00: it opens as the NTSC-U release (the byte comparison with 1.10 is the one-off above)
GT3R_DISC="$US100" ./build/gt3r_tests disc_identity

# The program's instruction words stay out of the repository: any file with three or more of a routine's words
# close together fails (keep hashes of masked code instead, src/sim/exact/program_locate.h); exit 3 = findings
python3 -I tools/check_program_code.py . "$US" "$PAL" "$JP"

# Diff harness (any build directory with GT3R_BUILD_DIFFHARNESS, the default on desktop)
./build/gt3r_diffharness "$US"
./build/gt3r_diffharness "$PAL" race_tick_exact
./build/gt3r_diffharness "$JP" race_tick_exact
GT3R_CARHIT_SWEEP=1 ./build/gt3r_diffharness "$US"                       # full matrix
GT3R_START_EVENTS=all ./build/gt3r_diffharness "$US" race_start_exact    # full matrix

# Full matrix: the Concept discs, separated by ';'
GT3R_DISC_CONCEPT="$D/concept-2001-tokyo.iso;..." ./build/gt3r_tests program_locate_other_builds
# The Concept discs' own data (database, game config, course files): run it when the car database or disc/region changes
GT3R_DISC_CONCEPT="..." ./build/gt3r_tests concept_disc_data
# Concept's front end: each release's languages and language page (concept_disc_languages), and its menus walked from
# the title to a race request and back (concept_disc_flow); run them when menu/flow or game/concept_* changes. The
# rules alone (concept_state_*, concept_release_languages) need no disc.
GT3R_DISC_CONCEPT="..." ./build/gt3r_tests concept_disc
```

x86-64 for the platform hashes: an x86-64 build on the Mac, run under Rosetta (`cmake -S . -B build-x86
-DCMAKE_OSX_ARCHITECTURES=x86_64 -DGT3R_FETCH_SDL=ON -DFETCHCONTENT_SOURCE_DIR_SDL3=<an SDL source tree, e.g. a
build's _deps/sdl3-src>`, then `./build-x86/gt3r_tests`); or an `ubuntu:24.04` container with `--platform linux/amd64`
and the CI's packages ([PLATFORMS.md](PLATFORMS.md)), or the Windows build PC.

## Running suites in parallel

Every `gt3r_tests` process writes only in its own folder, `$TMPDIR/gt3r-tests-<pid>-<random>/`
(`tests/test_framework.h`): the tests through `test::temp_path` / `test::temp_dir`, the code under test through
`TMPDIR` / `TMP` / `TEMP` and `GT3R_DATA_DIR` (its user data folder: saves, replays, mods), all pointed there as the
run starts. The folder is removed when the run ends; `GT3R_KEEP_TEST_TMP=1` keeps it and prints its path, and so does
a crash. Nothing goes to the working directory or the player's user data folder, so runs with different discs (and
ctest) can share one build directory and go at once. The harness only reads. Machine load slows every run: the times
above are with other runs going.
