# Reverse-engineering toolkit (`tools/re/`)

Python scripts that analyse the game program (`CORE.GT3`) from **your own disc
image** and write reports for format and behaviour research. They support the
workflow and rules in [../REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md):
read the reports, write down *facts*, and implement from those. Never copy
listings or decompiled code into the repository.

Everything the tools produce comes from the disc you give them. Outputs go
only to the directory you name on the command line, and that directory must be
outside the repository.

## Requirements

- Python 3.10 or newer, run in isolated mode (`python3 -I`).
- Optional: [`rabbitizer`](https://pypi.org/project/rabbitizer/) for proper
  R5900 text disassembly (MMI, COP2/VU0 macro mode). Without it the tools use
  `capstone` if it is installed, which shows R5900-only instructions as
  `.word`. If neither is installed, the listing contains raw words only. The
  analysis itself has no dependencies.

```bash
python3 -m venv ~/gt3re-venv && ~/gt3re-venv/bin/pip install rabbitizer
```

## Running

```bash
~/gt3re-venv/bin/python -I tools/re/analyze.py "game.iso" /path/to/out
# or from an unpacked program (gt3tool core needs -DGT3R_PROGRAM_TOOLS=ON):
gt3tool core game.iso core.bin
python3 -I tools/re/analyze.py core.bin /path/to/out
```

The input can be a disc image (CORE.GT3 is located via ISO9660 and inflated,
and the GT3.VOL table of contents and file headers are read), a raw program
image as written by `gt3tool core`, or a still-packed `CORE.GT3`. A full run
takes a few seconds.

Other games on the engine load the same way: a program of several segments is
laid out as one image from its lowest load address; a dual-layer image with a
file system per layer is read as one set of files (CORE.GT3 may be on the
second); without a GT3.VOL, every `*.VOL` in the roots that has the archive's
magic is listed (the one with the largest index first, a path two have is the
first's).

Options:

| Option | Effect |
|---|---|
| `--labels FILE` | apply manual names (TSV: `addr<TAB>name[<TAB>lib]`, header and `#` lines ignored); repeatable |
| `--no-disasm` | skip the full listing |
| `--no-vol` | do not read GT3.VOL file headers (no magic survey) |
| `--backend rabbitizer\|capstone\|none` | choose the text disassembler |
| `--text-end ADDR` | override the detected end of `.text` |

Keep your label files with your notes, outside the repo, and pass them on
every run so names accumulate.

## Outputs

| File | Contents |
|---|---|
| `summary.txt` | segment layout (`.text`, data, BSS, `$gp`), counts |
| `functions.tsv` | every function: start, end, size, name, library label, caller/callee counts, tags (`eh` = bounds from `.eh_frame`, `virtual`, `ctor/dtor:Class`, `labelled`) |
| `callgraph.tsv` | direct call edges (`jal`, tail `j`) |
| `disasm.s` | annotated listing: function headers with callers, call targets by name, string literals and symbol names next to the instructions that form their addresses |
| `strings.tsv` | every string in the data section with category and referencing functions |
| `string_xrefs.txt` | index of asset-related strings, grouped as VOL paths, printf path patterns matched against the real VOL contents (for example `sound/%s/%05d` matching 1,208 files), directories, path-like strings, extensions and magics, each with the functions that use it. References through pointer tables in data are followed up to two levels and shown as `(via table)` |
| `magics.txt` | every printable 4-byte file magic found at the start of VOL files (with file counts), and where the program builds the same value as an immediate (`lui`/`ori`) or references it as a string. Also lists other four-character immediates (IOP RPC ids, tags) |
| `rtti_classes.tsv` | C++ types: type_info object, type-info function, kind, demangled name, bases (with offsets for multiple inheritance), vtables, constructor/destructor candidates |
| `rtti_hierarchy.txt` | the class tree |
| `vtables.txt` | each vtable slot with the function it points to. Inherited slots keep the name of the class that introduced them |
| `sdk_functions.tsv` | library/runtime functions and the evidence for each label |
| `symbols.txt` | `name address f\|l` lines for Ghidra's `ImportSymbolsScript.py` |
| `db.json` | compact database used by `query.py` |

## Querying

```bash
python3 -I tools/re/query.py OUT func <name|hexaddr>    # listing of the containing function
python3 -I tools/re/query.py OUT xref <hexaddr|name>    # code/data references and callers
python3 -I tools/re/query.py OUT str '<regex>'          # strings and who uses them
python3 -I tools/re/query.py OUT class '<regex>'        # bases and vtable slots
python3 -I tools/re/query.py OUT tree <func> [depth]    # callees
python3 -I tools/re/query.py OUT callers <func> [depth] # callers
```

The usual way to find a format's loader is: `str` for the path or magic, then
`func` on the referencing function, then `tree` to reach the parser. When the
path is built with `sprintf`, follow the pattern string instead.

## Other program versions (`version_map.py`)

The NTSC-U program (US 1.00 and 1.10 ship the same CORE.GT3) is the reference; the PAL and NTSC-J programs hold the same
code at other addresses, with some routines changed. `version_map.py` analyses all three discs and maps NTSC-U addresses
to the others (it writes the diff harness's version table, `tools/diffharness/program_map.inc`):

```bash
python3 -I tools/re/version_map.py NTSCU.iso PAL.iso NTSCJ.iso                     # the harness's table
python3 -I tools/re/version_map.py NTSCU.iso PAL.iso NTSCJ.iso --addr 1ECFC8 1F3B6C # look addresses up
python3 -I tools/re/version_map.py ... --sources src/sim/exact --report r.txt --out /tmp/x.inc --funcs /tmp/map
```

- **Functions** match by their code with the relocations masked (address `lui` halves, the low halves and `$gp` offsets
  that form addresses, jump targets): unique masked code in both programs first; then the callees at aligned calls of
  matched functions, the slots of the same class's vtables (aligned by the slots already matched: NTSC-J's conductors
  have one virtual fewer; `CLASS_ALIASES` names a class another program calls otherwise, NTSC-J's single battle conductor
  for NTSC-U's split-screen one) and functions between two matched neighbours. A match is *same* (identical masked code,
  branch offsets included) or *differs*.
- **Addresses inside a function** map through the aligned instructions: one to one for the same code; for code that
  differs, the calls first (by callee, a callee's repeated calls in order), then each stretch between two calls position
  by position or, when the lengths differ, through the code with the registers masked.
- **Data** maps through the references of aligned instructions (else from the nearest referenced address below, marked
  `check` in the table).
- `--report` lists the scanned functions that differ or have no match, and the constants they load whose values differ
  (pointers left out): the starting point for a version branch in a port.

### ProgramData's locator rows (`gt3r_diffharness --locate-gen`)

`ProgramData` (`src/sim/exact/program_data.cpp`) serves the tables written out in `src/sim/exact/program_tables.cpp` and
checks the disc's against them: it finds each table through the routine that addresses it and recognises the routines
whose rules differ by a hash of their masked code (`src/sim/exact/program_locate.h`: jump targets, address
halves and `$gp` offsets cleared, a load from a fixed address replaced by the value it loads). Its rows (`kTableSites`,
`kRuleSites`) hold only those hashes, the window lengths, the address instructions' positions inside the window and the
releases that have each. They are generated from the three discs:

```bash
gt3r_diffharness --locate-gen spec.tsv > rows.txt   # the rows on stdout, the checks' problems on stderr
```

The spec (kept outside the repository) is tab-separated: `iso <p> <path>` for p = 0 NTSC-U, 1 PAL, 2 NTSC-J; per table
and program `table <kName> <p> <fn start> <fn end> <lui> <addiu> <table>` (one `$gp` addiu: lui = addiu); per rule and
program `rule <Rule> <p> <fn start> <fn end>`, or `absent <Rule> <p>` for a release without the routine. Function bounds
come from each disc's `functions.tsv` and `version_map.py`'s function map. The generator checks that each table decodes
to its address, that each window is unique in its program and that no row is found in a release it does not name. A
table's window is its whole routine up to 256 instructions, else 32 instructions either side of the address; a rule's
is the whole routine without its trailing padding. `tests/test_program_locate.cpp` checks the rows on a disc
(`GT3R_DISC`) and reports other builds of the engine (`GT3R_DISC_CONCEPT`, discs separated by `;`).

`gt3r_diffharness --locate-survey spec.tsv` measures the masking on a corpus: `iso <n> <path>` for each build (0 the
reference), `funcs <the reference's functions.tsv>` (or `funcs auto`: functions from `jr $ra` to `jr $ra`) and the
reference's `table` / `rule` lines. For ProgramData's masking and three looser ones (no loaded values; no load / store
offsets; no registers) it prints how many of the reference's functions each build has and where each window is found.

## Other builds of the engine (`build_map.py`)

`version_map.py` needs two programs of one compiler. The Gran Turismo Concept discs are the same source tree built with
another compiler: almost no function keeps its instructions (the masked-code match finds about 8%). `build_map.py`
matches two or more builds by what a compiler keeps, says what differs in each matched pair, and infers how struct
layouts moved:

```bash
python3 -I tools/re/build_map.py REF.iso OTHER.iso --out /path/to/out                  # two builds
python3 -I tools/re/build_map.py NTSCU.iso PAL.iso C2001.iso C2002a.iso ... --names us,pal,tokyo,... --out /path/to/out
python3 -I tools/re/build_map.py PAL.iso NTSCU.iso --validate --ablate                 # score the matcher
python3 -I tools/re/build_map.py NTSCU.iso PAL.iso C2001.iso --addr 1ECFC8             # where an address went
python3 -I tools/re/build_map.py NTSCU.iso PAL.iso C2001.iso --struct 1E8598:0         # a struct's field offsets
python3 -I tools/re/build_map.py NTSCU.iso PAL.iso C2001.iso --data 2D28B0:288:4       # a table: where, how many entries differ
```

With more than two builds they form a **chain** (ref -> other -> later ...): each step is matched on its own and the
maps are composed. A step between two builds of one compiler starts from `version_map.py`'s matcher (its pairs with
identical masked code are taken as they are; the pairs it reaches through calls, tables and neighbours in code that
differs are only candidates) and the rules below find the rest; a step between two compilers uses the rules alone. Put
the builds in the order they were made, so only one step crosses compilers (GT3 NTSC-U -> GT3 PAL -> Concept 2001 ->
the 2002 builds in release order).

**Matching.** Seeds: the same class's virtual table slot (tables as long in both builds), a string only one function of
each build refers to, code that is identical after masking. Then, in rounds: the callees at aligned positions of two
matched functions' call sequences; unmatched functions that call the same matched functions; virtual tables of
different lengths aligned by their matched slots; the only unmatched function of each build that refers to the same
matched global; the unmatched functions between two matched neighbours when both builds have as many (link order). A
pair is taken when each is the other's best candidate, and the pairs that look most alike go first (a similarity of
sizes, float operations, constants, strings and call counts, in falling tiers), so an ambiguous one is decided when
most of its alternatives are gone; a pair with nothing in common is refused. At the very end the one open place of
two otherwise equal call sequences is taken whatever it looks like (a routine rewritten or emptied).

**Validation** (`--validate`: the rules without any code seed, scored against `version_map.py`'s matcher on two builds
of one compiler; `--ablate` scores each rule left out). On the discs this was developed with:

| pair | matched by code (truth) | right | wrong | precision | recall of the truth's code |
|---|---|---|---|---|---|
| GT3 PAL -> NTSC-U | 10,008 functions | 8,892 | 0 | 100% | 96.1% |
| GT3 PAL -> NTSC-J | 9,763 | 8,560 | 24 | 99.7% | 95.5% |
| Concept 2001 -> 2002 Geneva | 9,601 | 8,870 | 35 | 99.6% | 96.1% |

Most of the "wrong" pairs are look-alike accessors of 8 to 80 bytes, and some are the truth's own (its pairs in code
that differs). Across compilers there is no truth; matching Concept 2001 from GT3 PAL and from GT3 NTSC-J separately
gives the same counterpart for 99.4% of the functions both find. GT3 PAL -> Concept 2001 matches 85% of GT3's code.
Rules that were tried and lowered the precision are not in the script: the only unmatched callee of both functions,
any unmatched callees between the same two matched calls, look-alikes between the same matched neighbours, and a float
constant only one function of each build has.

**Outputs** (in `--out`; `<ref>` is the first build, one pair of files per later build):

| File | Contents |
|---|---|
| `map_<ref>_<other>.tsv` | every function of the reference: its start, size and name, the counterpart's, how it was matched (the weakest step of the chain) and a confidence 0..1, the class of difference, the float operation deltas (other minus reference: mul, addsub, div, sqrt, cmp, minmax, vu), the constants only each side has, the number of calls that differ, notes |
| `offsets_<ref>_<other>.txt` | per struct, "offset X in the reference is offset Y in the other" with vote counts |

The **classes**: `same`; `layout only` (only field offsets differ); `calls only`; `constants`; `integer/control edit`;
`small arithmetic edit` (float operation counts differ by up to 3); `larger change`; `not found`. A pair of one
compiler is compared by its masked code first (identical: same; only load / store / pointer offsets differ: layout
only). Otherwise the comparison is by features: the counts of float operations (a multiply-add counts as both), VU0
macro operations, the set of float constants, the calls after mapping and the strings. The constants are immediates
that are not addresses and float literals loaded from fixed places, and an integer converted to float counts as that
float, so a build that loads a literal and one that builds it inline compare equal. A `constants` pair whose constants
differ only by one unit in the last place has the note `literal rounding only` (another compiler's rounding of the
same decimal literals).

**Layout.** For every function the accesses through the pointers it is handed (its arguments, and pointers loaded
through them) are listed in order. For a matched pair the lists are aligned (the calls first, by callee; then the
accesses between two aligned calls by kind), and each aligned access votes "offset X here is offset Y there"; a
matched callee's votes count for its callers at the offset they hand it. The same accesses paired in offset order
give a second vote (`rank`), which decides when two or more functions agree: a struct's fields keep their order when
it grows, while two compilers may load the operands of one expression in either order. `--struct FUNC:ARG` prints the
table of the struct handed to one argument, joined over every function handed the same pointer.

**Limits.**

- The feature comparison cannot see integer logic that keeps its calls and constants (it reads as `same`, or as
  `integer/control edit` when the branch count or the size moved, with a note to check). Integer immediates below
  0x10000 are not compared between compilers. An integer in the program's address range is taken for an address.
- Float operation counts move by one or two between compilers without a source change (a shared term evaluated twice,
  the float-to-unsigned sequence): read both listings before porting a `small arithmetic edit`.
- A callee a compiler inlined has no counterpart; a function it split or merged matches one part at most. Functions
  matched by link order or position alone have a low confidence: check them.
- The layout votes are evidence, not a proof: a row is as good as its votes. Arrays of structs (the wheels) and pairs
  of copies (current / previous) attract misaligned votes that the `rank` column usually outvotes.
- Indirect calls are not followed; a pointer kept across a branch can be mistaken (the tracking is linear).

### The harness's version table and the locator's rows for every build

`program_map.py` writes the diff harness's version table `tools/diffharness/program_map.inc` with a column per build
(GT3's three releases by `version_map.py`'s matcher, the later builds by the chain above; see docs/dev/DIFF_HARNESS.md).
It keeps every row of the table it replaces and adds the addresses found in the harness's sources, so running it again
gives the same file.

`tools/re/program_map.pins` (`--pins`) places by hand the later builds' addresses the matcher cannot place or places
wrong: a line per NTSC-U address with each later build's address (`-`: the matcher's), and a comment saying how it was
read. Pinned so far: the surface tables and the controller settings' globals, whose address instructions another
compiler orders the other way round.

`locate_spec.py` writes `gt3r_diffharness --locate-gen`'s spec for all eight programs (outside the repository):

```bash
python3 -I tools/re/locate_spec.py --harness build/gt3r_diffharness --out /path/to/spec.tsv NTSCU.iso PAL.iso NTSCJ.iso <the five Concept isos>
build/gt3r_diffharness --locate-gen /path/to/spec.tsv > rows.txt    # the rows for program_data.cpp's kTableSites / kRuleSites
```

For GT3's releases it takes what `ProgramData` recognises now (`gt3r_diffharness <iso> --locate-sites` prints each
table's routine window and each rule's routine), so their rows come out unchanged; a new `Rule` is found from the NTSC-U
address in its comment in `src/sim/program.h`. For the later builds it finds each table through `build_map.py`'s
`locate_data` and then a routine that forms an address in it (the analysis's references, else the instruction pattern
upper half / indexed / offset, also in a jump's delay slot), and each rule's routine through the chain. Which rules get a
row for the later builds is the script's `AS` table: only what the diff harness has proven on the discs (`own`: a branch
of the port for that build; 0..2: a GT3 release's rules; `absent`: the build has no such routine). `--locate-gen` checks
that every table decodes to its address, that no window is found in a program its row does not name, and prints how
many rows tell each pair of programs apart (none: an error, the two would be taken for one).

A routine or table only the later builds have (the special cars' routines of the race step, their mood tables and
labels, the AWD system 3 settings) is named by its address in the first later build: the comment of its `Rule` or
`Table` enumerator starts `later 0x<address>`. The script maps it to the builds after that one with a chain of the later
builds alone, writes `absent` lines for GT3's releases (a rule) or none (a table: GT3's discs have no such table, and
`ProgramData::has()` is false there). A table whose content differs between the later builds names the part every
build has, `later 0x<start>@0x<offset>+0x<length>`: that part is located and the table's start taken from it. For these
tables an instruction that loads a byte through the global pointer counts as forming the address (a small-data table the
code reads byte by byte).

## How the analysis works

- **Segments.** `.text` runs from the load address until `jr $ra` stops
  appearing. `$gp` and the BSS range come from the start-up code at the entry
  point.
- **References.** A linear sweep tracks constants through registers
  (`lui`+`addiu`/`ori`, `$gp`-relative accesses, register moves), with
  caller-saved registers cleared after calls. Every address formed this way,
  and every pointer-sized word in data that points into the image, becomes a
  reference.
- **Functions.** GCC emitted DWARF `.eh_frame` records for the C++ code, which
  give exact bounds for about 90% of functions. Other function starts come
  from `jal` targets, code addresses taken in code or data that sit on a
  function boundary, and stack-frame prologues after an epilogue.
- **RTTI (GCC 2.9x old ABI).** Type-info functions are found as the callers of
  the three libgcc helpers (`__rtti_user`, `__rtti_si`, `__rtti_class`), which
  are recognised by the shape of their call sites: argument 1 is a mangled type
  name, and the argument count separates the three. Each call gives the
  type_info object, the name and the bases. A static base list
  (`{type_info*, offset:29, virtual:1, access:2}`) gives multiple-inheritance
  offsets. Vtables are arrays of 8-byte `{s16 delta, s16 index, pfn}` entries
  whose slot 0 points at the type-info function. Functions that store a vtable
  address are constructor/destructor candidates. A later compiler of the same
  family (other games on the engine) ends every vtable with a null entry and
  leaves about half of the functions without `.eh_frame` records. That layout
  is detected from the tables themselves; the vtables are then read up to
  their null entry, and the functions are split at the slots' targets and at
  taken code addresses that follow a return with an empty delay slot (about
  130 starts per program, type-info functions behind an empty function among
  them). Programs without that layout are analysed exactly as before.
- **Libraries.** EE syscall stubs are named exactly. Functions are assigned to
  libraries by their message strings (`libpad: …`, `sceCd…`, …), and the label
  spreads to unlabelled neighbours between two functions of the same library
  (link order). Code without `.eh_frame` after the last C++ function is the
  C/assembly runtime region.
- **Strings.** NUL-terminated ASCII or Shift-JIS runs in the data section, plus
  any referenced address that starts a string. A reference into the middle of
  a scanned run splits it.

## Limits

- Constant tracking is linear and ignores control flow, so an address formed
  across a branch can be missed or, rarely, misattributed.
- Indirect calls (`jalr`, virtual calls) are not in the call graph. Use
  `vtables.txt` for virtual dispatch.
- Switch jump tables appear as data references into the middle of functions.
  They do not split functions.
- Names such as `Class::vf3` give only the vtable slot. Real method names
  are not recoverable.
