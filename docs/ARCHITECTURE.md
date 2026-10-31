# Architecture

```
            ┌──────────────────────────── app/ ─────────────────────────────┐
            │ SDL3 callback main · screen stack · setup/hub/browser/jukebox │
            │ game menus · race · GT Mode · online · settings · tools       │
            └───────┬───────────────┬───────────────┬───────────────┬───────┘
                    │               │               │               │
              input/            audio/          formats/          vfs/
     virtual DS2, bindings   SDL audio streams  Tex1+GsMemory,   mods over
     presets, devices,       ADS music          ADS, PS-ADPCM,   GT3.VOL
     rumble, tilt, capture                      MADS, PNG/WAV       │
                                                                  disc/
                                                    ISO9660, disc id, CORE.GT3,
                                                    GT3.VOL archive
                                                                    │
                                                                 base/
                                       logging, SDL_IOStream files, INI, zlib
```

`gt3r_engine` is a static library containing everything except the entry
point. The app (`gt3r`, or `libmain.so` on Android), `gt3tool` and the unit
tests all link it.

## Principles

- **No game data in the repo, none written to disk.** Assets are read from
  the user's image on demand (music streams straight from the disc). The
  only exception is `gt3tool`, which extracts files when the user asks.
- **Everything through the VFS.** Engine code asks for `"bgm/race_a05.ads"`,
  never "the disc", so mods can override anything.
- **The game sees a DualShock 2.** Engine logic reads `PadState`
  (buttons, pressures, sticks) exactly as the original reads the pad. Remapping,
  device quirks and modern features live entirely in `input/`.
- **Decode through hardware models where the data depends on them.** PS2
  textures only make sense through GS memory (`GsMemory`); the same approach
  will apply to VU-generated geometry where needed.
- **SDL3 is the only platform layer.** Windowing, gamepads/sensors/haptics,
  audio, file dialogs (including Android's Storage Access Framework, via
  `SDL_IOFromFile` on `content://` URIs) and GPU access. 3D rendering uses
  `SDL_GPU` (Metal, Vulkan), sharing one device with the 2D UI renderer, or on
  PCs without Vulkan our OpenGL ES 3.0 implementation of it (`render/gpu_gles.*`); see
  [dev/RENDERING.md](dev/RENDERING.md). Requires SDL 3.4+.

## Threading

The main thread runs input, game logic and rendering. SDL's audio thread
pulls decoded music through `AudioEngine::feed` and renders the voice mixer
block by block from a copy of its voices, so the main thread's voice calls
never wait for a block (their changes apply after it); movie playback decodes video
on its own worker thread. `File::read_at` is
mutex-protected, so the disc can be read from both threads. On phones and
tablets a background thread creates the 3D pipelines at start-up
(`GpuContext::warm_pipelines`, [dev/RENDERING.md](dev/RENDERING.md)); the
pipeline cache is mutex-protected. Online sessions receive on their own
thread, and the update check runs on a detached one.

## Modules

| Module | Responsibility |
|---|---|
| `base/` | logging, files, INI, zlib, SHA-256, the original's random generator, PS2 float helpers |
| `disc/` | ISO9660, disc identification, `CORE.GT3`, `GT3.VOL` |
| `vfs/` | mods layered over `GT3.VOL` |
| `formats/` | decoders: textures, music, sound banks, models, courses, menus, text, databases, movies, PS2 saves |
| `input/` | virtual DualShock 2, bindings, force feedback, touch controls |
| `audio/` | SDL audio, SPU2-style voice mixer and reverb, sequencer, engine and ambience sounds |
| `video/` | own MPEG-2 decoder and movie player (worker thread; [dev/MOVIES.md](dev/MOVIES.md)) |
| `render/`, `scene/` | SDL_GPU device, shaders, model upload, showroom ([dev/RENDERING.md](dev/RENDERING.md)) |
| `xr/` | VR headsets: the OpenXR session, head tracking and Touch controllers (Quest builds, `GT3R_XR`; [dev/VR.md](dev/VR.md)) |
| `text/` | the game's fonts |
| `sim/` | the race step (`sim/exact/`: vehicle physics, AI drivers, the race), collisions, the menus' car specs ([dev/PHYSICS.md](dev/PHYSICS.md), [dev/AI.md](dev/AI.md)) |
| `track/` | course runtime: road surface, walls, lap timing ([dev/TRACK.md](dev/TRACK.md)) |
| `race/` | the race screen's engine side: field, HUD, cameras, replays, effects, sounds ([dev/RACE.md](dev/RACE.md)) |
| `menu/` | the game's menu system (pages, items, transitions) and game flow |
| `game/` | GT Mode rules and state, saves, replay theater, mod cars ([dev/GT_MODE.md](dev/GT_MODE.md)) |
| `net/` | online sessions and the relay server ([dev/ONLINE.md](dev/ONLINE.md)) |
| `platform/` | platform specifics (tilt, display insets, render scale, recent discs) |
| `app/` | the screens and the main menu |
