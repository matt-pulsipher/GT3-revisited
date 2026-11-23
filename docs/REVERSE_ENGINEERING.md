# Reverse-engineering workflow

## Ground rules

GT3 Revisited is an independent reimplementation written from documented
behaviour (file formats and behavioural specifications), verified against the
original's observable behaviour. It contains no original code, data or
assets; game content is read from the player's own disc at runtime.

1. **Never commit original code**, in any form: no disassembly, decompiler
   output, or code transcribed from either.
2. Record *what* the game does (formats, algorithms, constants, behaviour)
   in `docs/` or an issue, then implement from that description.
3. Facts are fine to record: file layouts, constants, table values, and
   observed behaviour.
4. Never commit game files or extracted assets, including test fixtures.
   Tests that need real data must load them from the user's disc at test time
   and skip when no disc is configured.

The contributor form of these rules is in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Specifications

Behavioural specifications written from this process (facts only) live in
`docs/specs/`: [physics.md](specs/physics.md) (vehicle simulation),
[ai.md](specs/ai.md) (AI drivers), [menus.md](specs/menus.md) (menu system),
[gt_mode.md](specs/gt_mode.md) (GT Mode rules and save data),
[presentation.md](specs/presentation.md) (rendering, cameras, HUD, effects),
[music.md](specs/music.md), [race_sound.md](specs/race_sound.md) and
[sound_sequencer.md](specs/sound_sequencer.md) (sound), and
[concept.md](specs/concept.md) (GT Concept discs). They cite the original
program's addresses (NTSC-U unless marked). The player-facing summaries
are the pages in [behaviour/](behaviour/README.md).
Implementations follow them and note any deviations.

## Analysis toolkit

`tools/re/` analyses the program straight from your disc image: function
bounds, RTTI class hierarchy and vtables, string and magic cross-references,
SDK library labels, an annotated disassembly and a Ghidra symbol file. See
[dev/RE_TOOLKIT.md](dev/RE_TOOLKIT.md). Output goes to a directory you choose
and must never be committed.

## Getting the program into Ghidra

```bash
gt3tool info game.iso                 # shows load address and entry point
```
```bash
gt3tool core game.iso core.bin       # only built with -DGT3R_PROGRAM_TOOLS=ON
```

In Ghidra, import `core.bin` as **Raw Binary**:

- Language: MIPS R5900 (install the `ghidra-emotionengine-reloaded`
  extension), or `MIPS:LE:32:default` as a fallback
- Base address and entry: the load address and entry point that
  `gt3tool info` prints (the `CORE.GT3` header, [FORMATS.md](FORMATS.md))

Useful first steps:

- **RTTI names:** search strings for `Q29gt2k_menu…`, `Race…`, `Garage…`.
  GCC 2.9x type-info objects point at these strings, and vtables point at the
  type-info, which recovers class hierarchies and virtual methods.
- **Library code:** the Sony SDK (`libgraph 2.2.2.1`, `libpad 2.2.0.0`,
  `libmc`, `libcdvd`, `libkernl 2.2.4.0`; version strings are in the binary)
  can be identified by its patterns and labelled, leaving only game code.
- **File loads:** VOL paths such as `race/brake.bin`, `font/system.fnt` and
  `stream/opening.pss` appear as strings. Cross-referencing them finds each
  format's loader, which is the fastest way into the parsers.

## Asset research loop

1. `gt3tool ls` / `extract` the files of interest.
2. Compare files of the same kind (for example all `wheel/*`) to separate
   headers, counts, offsets and data.
3. Confirm with the loader in Ghidra.
4. Implement a decoder in `src/formats/`, expose it in the asset browser, and
   document the layout in [FORMATS.md](FORMATS.md).

## Behavioural verification

`tools/diffharness` runs single original functions, up to whole physics
ticks, in a minimal R5900 interpreter from the user's own `CORE.GT3` and
compares their outputs with the reimplementation
([dev/DIFF_HARNESS.md](dev/DIFF_HARNESS.md)). It is a developer tool, not
part of the shipped game, and needs the user's disc to run. Tests contain
only addresses, argument layouts and input generators, never original code
or data. Which tests (unit suites, harness) to run for which change:
[dev/TESTING.md](dev/TESTING.md).
