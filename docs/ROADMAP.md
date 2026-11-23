# Roadmap

## Where things stand

GT3 is about 2.4 MB of compiled R5900 (PS2 EE) C++ code, plus VU1
microcode for rendering, IOP modules for sound/pad/disc, and the assets in
`GT3.VOL` (most of the 3.9 GB disc). Reimplementing it means rewriting every
system that the code implements:

| Layer | What it involves | State |
|---|---|---|
| Platform | window, GPU, audio device, input devices, files, Android lifecycle | ✅ done (SDL3) |
| Disc & archive | ISO9660, `CORE.GT3`, `GT3.VOL` with gzip entries | ✅ done |
| Input | virtual DualShock 2, bindings, presets, rumble, wheels, tilt, touch | ✅ done |
| Modding | layered VFS, mod manifests, priorities, data-only mod cars and dealers | ✅ done |
| Textures | `Tex1` sets through a GS memory model | ✅ done |
| Music | `.ads` PS-ADPCM streaming, track index, sequenced music | ✅ done |
| Text & fonts | `MLTX` string tables, `FT00` fonts, `.fnt` / `.kf`, every language on the disc | ✅ done |
| Menus | `.mbl` / `.pmb` layouts, `IMGS` image sets, the `gt2k_menu` widget system and flow | ✅ done ([formats/menu.md](formats/menu.md)) |
| Database | `IDDB` index, car/part/race tables in `database/` | ✅ done ([formats/database.md](formats/database.md)) |
| Car models | `cars/*` bodies, LODs, paint, wheels `GTTW`, tyres, lamps (2073/2073 files) | ✅ done |
| Courses | `courses/<region>/*` scenery, road, walls, AI lines, effects (320/320 files) | ✅ done ([dev/TRACK.md](dev/TRACK.md)) |
| Renderer | SDL_GPU (Metal, Vulkan), GT3 lighting, reflections, shadows, glare, smoke, filters ([dev/RENDERING.md](dev/RENDERING.md)) | ✅ done (D3D12 not yet) |
| Race engine | physics, multi-car world, walls, pit stops ([dev/PHYSICS.md](dev/PHYSICS.md), [dev/RACE.md](dev/RACE.md)) | ✅ done (residuals below) |
| AI | racing lines, overtaking, rubber band, pit stops ([dev/AI.md](dev/AI.md)) | ✅ done |
| Sound | engine banks `ENGN`, tyre/wind/turbo/collision effects, course ambience, SPU2-style mixer and reverb | ✅ done |
| Movies | own MPEG-2 decoder, PSS demux, A/V sync ([dev/MOVIES.md](dev/MOVIES.md)) | ✅ done |
| Game flow | GT Mode economy, licenses, Arcade, 2P split screen, replays, Replay Theater, Analyzer ([dev/GT_MODE.md](dev/GT_MODE.md)) | ✅ done |
| Saves | native save library; PS2 save import and export | ✅ done |
| Online | i.LINK Battle over UDP with a relay server ([dev/ONLINE.md](dev/ONLINE.md)) | ✅ done (new) |

Every system of the original is reimplemented. What is left is fidelity
detail and platform coverage, listed under "Open" below.

## Strategy: an independent reimplementation, verified against the original

1. **Describe, then implement.** Each subsystem's behaviour is written down
   as a description (data layouts, algorithms, constants) in
   [formats/](formats/), [FORMATS.md](FORMATS.md) and the notes in
   [dev/](dev/), and the engine code is written from that description. No
   original code in any form and no game files go into this repository; see
   the project rules in [CONTRIBUTING](../CONTRIBUTING.md).
2. **Data drives everything.** Car specs, tire models, AI lines and course
   layouts already live in the disc's data files. Decoding them correctly is
   most of the "physics feel" battle. Formats are documented in
   [FORMATS.md](FORMATS.md) as they are understood.
3. **Verified against the original's behaviour.** Physics and AI must match
   the original closely to feel like GT3. Each model, up to whole physics
   ticks and the replays of the original's license demos, was checked
   against the original's behaviour with the user's own disc; the unit and
   disc tests keep the results pinned.
4. **Side by side.** Menus, HUD, cameras, materials, lighting, effects and
   2P were compared frame by frame with the original running in an
   emulator.

## Milestones

### M0: Foundation ✅
Platform layer, disc/VOL access, texture and music decoding, input with
remapping and pressure emulation, mod VFS, tooling, CI.

### M1: Static scenes ✅
Menu pages (`IMGS`, `.mbl` / `.pmb`), text and fonts in every language on
the disc, car models in a turntable viewer, rendering on SDL_GPU.

### M2: Drive a car on a track ✅
Time trial on every course with any car ([dev/RACE.md](dev/RACE.md)),
physics ([dev/PHYSICS.md](dev/PHYSICS.md)), track runtime
([dev/TRACK.md](dev/TRACK.md)), engine sound, lap timing.

### M3: Racing ✅
AI opponents ([dev/AI.md](dev/AI.md)) in a
multi-car physics world, Arcade single race / time trial / free run from the
interactive menus, the game's HUD and mini-map, results, flags and messages,
2-player split screen, replays with the game's TV cameras (2P too), ghost
car, race music and reverb, per-car engine, turbo, wind, tyre and contact
sounds, course ambience, the game's opponent paint draw.

### M4: GT Mode ✅
([dev/GT_MODE.md](dev/GT_MODE.md))
Dealers with the colour picker, garage, tune shop, wheels, wash and oil, car
settings with load/save and the gear chart, licenses (medals, name entry,
license cards), races and championships with entry rules, prizes, prize cars
and trophies, pit stops and tyre wear in endurance races (AI pits too),
machine tests, records, endings, arcade progression and unlocks, guest-garage
trade, attract mode. Opponent, trophy and prize draws use the original's
random generator.

### M5: Fidelity and polish ✅
Physics follows the original's step and phase order, in the EE's own float
arithmetic (its multiplier and divider bit for bit): the disc's license and
arcade demo runs all end on the times they were recorded with (on a European
disc's data, all but one). The Replay Theater (Load Replay, the disc's
Demonstrations, the Analyzer data logger), REPLAY OPTIONS and the replay view
modes ([dev/RACE.md](dev/RACE.md) "Replay Theater"). PSS movies, the
original 640×448 render option, widescreen, packaging for every platform.
GT Mode saves move to and from the PS2 original (verified in PCSX2).

## Open

Genuinely open items; each links to the detailed notes.

- **Physics** ([dev/PHYSICS.md](dev/PHYSICS.md) "Known remaining
  deviations"): wall corners against car edges and deep wall push-out;
  undecoded data (the AWD strength byte, the power cap of power-restricted
  events, arcade-car settings, turbo intake smoothing for sound).
- **AI** ([dev/AI.md](dev/AI.md)): the AI's own aid parameters, driver kinds
  3–8, the recommended-gear output.
- **Rendering** ([dev/RACE.md](dev/RACE.md) "Known gaps"): course env passes of
  vertex colours above 0x80 are clamped; the reflection map's light state; the bumper
  and chase cameras' fields of view are ours; no D3D12 (DXIL) path, so Windows needs
  Vulkan for 3D.
- **GT Mode** ([dev/GT_MODE.md](dev/GT_MODE.md) "What is left against the
  original"): a few presentation details measured from captures rather than
  specified exactly; small differences in when the random generator is
  stepped (they only add entropy); a few unverified save fields. The
  memory-card pages are replaced by native saves on purpose.
- **Online** ([dev/ONLINE.md](dev/ONLINE.md) "Known issues"): remote cars'
  turbo / whine sounds, online replays, 2P split screen combined with online
  players.
- **Movies** ([dev/MOVIES.md](dev/MOVIES.md)): seeking.
- **Platforms** ([dev/PLATFORMS.md](dev/PLATFORMS.md)): performance on real
  Android devices, a real Linux desktop session, installing a mod on iOS (the
  rest has been played on an iPhone), signed/notarized macOS builds;
  `.bin/.cue` and CHD images.
- **European discs** ([COMPATIBILITY.md](COMPATIBILITY.md) "Physics on
  European and Japanese discs"): races run at 60 Hz, not the European
  release's 50 Hz, and its demonstrations are the US recordings.
- **Modding** ([MODDING.md](MODDING.md) "Planned"): PNG texture replacement
  packs, record-level data patches, new courses and events, scripting.

## Beyond the original

Added by GT3 Revisited; all optional, none changes the original game unless
chosen:

- **Online battle**: the arcade's i.LINK Battle over the internet and LANs,
  up to 16 players (32 cars with CPU cars), session codes with hole punching and a relay server
  (`gt3r.gameversions.com`), mod car downloads, cheat warnings.
- **Quick race** (any event with any car; custom races on any course against
  up to five opponents of any car, color and skill, saved by name) and **Time
  trial** (any car on any course) without unlocking anything; a **Car
  database** browser and a **Movie theater**.
- **Native saves**: any number of named saves, autosave, PS2 save import and
  export in game and in `gt3tool`.
- **Display**: any resolution and aspect ratio, widescreen, high refresh
  rates (fixed simulation tick, interpolated rendering), MSAA, render scale,
  or the original 640×448 look.
- **Controls**: remapping for keyboards, pads and wheels; analog triggers as
  pressure buttons; DualSense adaptive triggers, Xbox impulse triggers, wheel
  force feedback, rumble, player LEDs; tilt steering; on-screen touch
  controls.
- **Mods**: file overrides and additions with priorities; data-only new cars
  (electric too) at the dealers, in the arcade and online.
- **Race options**: extra camera views, unit choice.
- **Android** build with the system file picker and touch controls.
