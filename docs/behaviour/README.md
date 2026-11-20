# Behavior

What the engine does, system by system: state, step order, rules, equations,
constants and parameter tables. The implementation notes in [docs/dev/](../dev/)
say how the code is organized and how each system was tested; these pages
describe the behavior itself.

| Page | Covers | Code |
|---|---|---|
| [physics](physics.md) | the driving simulation every race runs: constants, step and state, the releases' rules, engine and driveline, tires, suspension, steering, brakes and aids, aerodynamics, collisions (§0–§5); car parameters from the database and the player's settings, numbered reference rules, tire wear, smoke and brake heat, pit stops, flap pendulums, race starts, engine condition, dirt courses, airborne cars, leaving the road (§6–§11); the Gran Turismo Concept builds (§14) | `src/sim/exact/`, `src/sim/`, `src/race/exact_*`, `src/base/ee_*` |
| [ai](ai.md) | CPU drivers: the driver and its outputs, the end of a run, the AI driving the player's car, AI lines, target point, speed planning, steering, recovery, traffic, driver parameters and event difficulty, rubber band, countdown and start, the race field and the race step, no respawn, pits, car-to-car interaction, the recommended gear | `src/sim/exact/` (the AI's phases, the race), `src/formats/course/ai_lines.*`, `driver_sets.*`, `src/race/exact_field.cpp`, `src/app/screen_race_setup.cpp`, `screen_race_drive.cpp`, `screen_race_license.cpp` |
| [gt_mode](gt_mode.md) | GT Mode's rules: money, dealers, garage, parts and settings, car condition, licenses, events and championships, endurance, prizes, records, endings, saves, randomness, the attract demo's field, trade and setting files; the arcade progression | `src/game/` |
| [menus](menus.md) | the menu system and the game flow: pages, focus, input, popups, drawing, transitions, arcade, code-drawn screens, options, race quick menus, save page and name entry, Replay Theater and Analyzer, menu sounds | `src/menu/`, `src/app/`, `src/game/gt_mode_menu/`, `src/race/results.*`, `src/race/post_run.*` |
| [presentation](presentation.md) | how a race is shown: license tests, start and finish, leaving a race, HUD, pause, prize pages, replays, ghosts and demonstrations, cameras, lighting, lamps, wet road, effects, which events trigger race sounds | `src/race/`, `src/app/screen_race*` |
| [concept](concept.md) | the Gran Turismo Concept discs: the releases, boot, title and attract, the menu session and its pages, Game Status, prize keys and presents, the credit transfer to GT3, what follows a run, the pace car, the pod race, display rows, quick menus, car settings, the game save, the releases' differences | `src/game/concept_*`, `src/menu/flow/concept_flow.cpp`, `src/menu/flow/concept_hooks.h` |
| [sound](sound.md) | voices and mixing, reverb, the sequencer, sound effects, the engine sound model, other race sounds, ambience | `src/audio/`, `src/race/race_audio.*`, `src/race/car_sfx.*`, `src/race/race_sfx.*`, `src/menu/flow/menu_sound.cpp` |
| [music](music.md) | which music plays when, how a request becomes sound, volume, song markers, the jukebox | `src/audio/`, `src/race/race_audio.*`, `src/race/race_playlist.*`, `src/race/music_markers.*`, `src/menu/flow/` |

Some systems have no page here; their behavior is described with their
implementation notes: online battle ([docs/dev/ONLINE.md](../dev/ONLINE.md)),
VR headsets ([docs/dev/VR.md](../dev/VR.md)), movies
([docs/dev/MOVIES.md](../dev/MOVIES.md)), controllers and input mapping
([docs/INPUT.md](../INPUT.md)) and mods ([docs/MODDING.md](../MODDING.md)).

## Conventions

- Code comments cite these pages as `<page> §n`: `physics §4.2` is
  [physics.md](physics.md) §4.2, and a bare `§n` later in the same comment is
  on the same page. Subsections are numbered `§n.m`, a few with a letter
  (`physics §8.3b`, `gt_mode §18b`). Presentation's sections are lettered
  (§A–§M), with numbered subsections (`presentation §G11`).
- One step (tick) is 1/60 s: every race runs 60 steps a second on every disc,
  with the rules of the disc's release (online races, demonstrations and the
  attract demo: the US release's; physics §1.1). Units are SI unless a value
  says otherwise.
- Where the code marks a model as a placeholder or an approximation, it is a
  conventional model chosen by this engine, not GT3's behavior; the pages say so.
- Where the code leaves a question open, the page says so.

When a pull request changes behavior, it updates the page that describes it.
