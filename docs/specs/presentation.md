# GT3 race-finish presentation and transitions: behavioural specification

Owner: `physics-spec` workstream (decoded on request for `race`). Behavioural
facts from a study of `CORE.GT3`: timings, colours, alpha ramps and stage
order. No code is transcribed. Tags: **[C]** confirmed from code (address
given), **[I]** inferred. Frames are 60 Hz ticks: the race loop runs the
logic and the display once per frame with dt = 1/60 [C, `RaceBase::vf2`
0x17C570].

## Changelog

- 2026-11-25 (public issue 442): §G4c / §G4e / §G5 the aim's move and the shots [C]: a new shot ends the move
  (`fn_001FF3D8`) and forgets nothing else; a target set while a move runs forces the shot's re-set (the answer of
  `fn_001FF490` / `fn_001FF4F8`); +0x34 and the cleared +0x92 belong to another car only, so a battle's begin or end
  never ends a shot; modes 0 / 3's Up / Down is not forced; the manual flag +0x36 outlives a move a shot cut short;
  `fn_00202760`'s thresholds. Ours had let the move run into the new shot and kept the last aim point of the car left
  (mode 1's cuts, mode 2's switches), so the shots opened with a whip across or a swing of up to 180 degrees a second.
- 2026-11-21 (public issue 366): §G4d the replay pad works in demonstrations (play mode 2) too: Cross
  turns the driver's view on and off in a licence / time-trial DEMONSTRATION as in a replay; the "driver
  mode" handler `fn_0020D208` checks the same bit (0x400000, 0x20D414). Checked in PCSX2 (B-1 DEMO: TV shot,
  Cross the bumper view with no mirror, Cross the TV shots again). Ours had allowed it in replays and the
  Preview only; the demonstration's driver's view now draws no mirror either.
- 2026-11-17 (public issue 249): §G4c the driver's view on the followed car is the car's own camera: `fn_002096C0`
  hands the live driving camera (OnBoardCamera, +0x600) the view's target, and `OnBoardCamera::vf9` keeps the target's
  car (+0x15C) and car index (+0x160); `fn_00201150` reads the mount from that car's model (car +0x11F8 -> +4 -> GTCI
  +0x68 count, +0x6C records) [C]. So an opponent's driver's view sits at that car's in-car eye (mount 0) or chase point
  (mount 1), and the followed car's body is not drawn (seen in PCSX2, Sunday Cup's Preview: each car's own eye height).
  Ours had used the player's car's points on every car (`CameraRig::set_car_mounts`, bound with the camera's car), and
  until 0.6.4 had left the followed opponent drawn and hidden the player's car instead (the camera inside the
  opponent's cabin behind its mirror).
- 2026-11-17 (public issue 324): §K3 / §I the replay HUD's car is the display's D+0x2469 (the
  camera's target) for every part, not only the pop-ups: gauges, lamps, steering, pedals,
  tyres, odometers, rank, lap count, lap clock, Best / Total from that car's entry, the map's
  own-car dot (implemented; ours had shown the player's values with an opponent followed).
- 2026-11-04: §A8c the attract demo's end: event 7, its time limit, comes before its laps can
  end it (gt_mode §15); ours had ended it after 30 s, and 5 s after a finish.
- 2026-11-03: §M2 the main view's Z resets after the sky and after the backdrop [C, GS dump]: the
  course always covers the backdrop. Ours had depth-tested the backdrop against the course, so
  Tahiti Maze's backdrop sea showed through the lagoon shores (public issue 162). Fixed
  (`DrawItem::depth_reset`).
- 2026-11-03: §K3 the steering wheel hangs from the tick's end (its top edge at -43, so it turns
  about its hub; it had been taken as centred there) and the meter presets' rows (`fn_00195230`):
  the outside view's odometers are right-aligned at their x, not centred [C]. Fixed in ours; our
  steering and accel / brake monitors now read the car's steer byte and pedals (F+0x523, +0x428,
  +0x42C) instead of the pad, so runs the computer drives show them too.
- 2026-11-04 (public issue 199): the computer's races (the attract demo, GT Mode's Preview, gt_mode §11), checked in
  PCSX2 on both: Start ends them at once, no pause window (`RaceBasic::vf29`: event 1 in play mode 2), through the race
  exit (§C2); Circle, Triangle, Square and Select do nothing, and Cross, Up / Down and mode 1's cuts are the replay pad's
  (§G3; `fn_00208BE8` runs them, the camera's view has the race's input). In the view the car is the computer's, so as in
  a replay: no rear-view mirror, no vibration (physics §1.2). Ours: the Preview as above; the attract demo keeps its own
  way out on purpose (any button, at once).
- 2026-11-03 (public issue 161): §I a licence lap test's line crossings short of its end are
  splits [C]: `DynamicsConductorLicense::vf18` (0x1F0ED8) posts CheckPoint (lap, laps x count - 1)
  and GeneralTime with the time since the car's lap start F +0x530, which only the licence's vf17
  sets (the flying start; GO for a standing test) and its vf18 never moves, so every licence split
  is the time since the run's start ("T1 0'23.546" on IB-2's circle). Ours had shown no pop-up at
  the line. §K3 the licence target lines are time lines drawn by `RaceTimeDisplay::vf5` with an
  empty label: the value at the row's y like the other lines, the mark at its own size
  (`fn_0019A148`, no scale), right-aligned at x - trunc(max(value width, 90) + 10) + w, centred
  on y + h (w 8, h -7). Ours had drawn the values about 20 units low and the marks at 75 %.
- 2026-11-02: the demonstrations' ends [C]. The licence and time-trial drivers' DEMONSTRATION
  (0x132930 / 0x13117C) loads `demo/%s` over the race's data (`0x12F5A8`) and plays it with
  `RacePS2Base::vf42(race, 1, 1)` (0x13295C / 0x1311A8): play mode 1, a replay, ended by
  `RaceSolitaire::vf29` (event 1 at 0x187CE4 when the recorder's done flag +0xCC+0x28 is set) and
  closed by the race-exit transition; then the driver's quick menu again, opened on START,
  whose licence box shows the demonstration. The recorder
  (`fn_001F4660`) counts a frame per step and sets its done flag on the step its count reaches
  the block's frame count T: the save's T - 1 frames are handed over, and the step after them,
  the first with the cleared frame, ends the replay. The licence menu's box restarts on that
  same step (`fn_00134C30` reads the posted event through `fn_0017CC38`). Ours: the quick menu's
  DEMO played on until a button ended it (Start, Cross or Circle), the box restarted 180 steps
  late, the theater's demonstration ended 180 steps late; all now as above
  (`DemoReplay::run_out`, `RaceScreen::demo_replay`). The race behind the quick menus (§A8c)
  corrected from PCSX2 captures and traced: the computer drives the car in play mode 2 (control
  source 1; a GT race's field, every car control source 2, races after its countdown), on and
  on (records off clear the race's laps and time limit, `fn_001F35B8`), from the driver's camera
  (records off leave the camera's view without the race's input; start.cam over a standing
  hold), the licence box with the same camera; the view-mode pick is the attract demo's only.
  Ours had replayed the last run, then let the car coast to a stop on a cleared frame (public
  issue 150), before any run parked it, stood a field still on its grid, and used the TV
  cameras. Later the same day: the free run, machine test, licence box, championship round and
  QUALIFY return seen in PCSX2 (§A8c), all as traced; ours matches (machine test in
  `exact_game_menu_race`).
- 2026-11-02: §M7 the GS's 8-bit arithmetic (texel, MODULATE and fog truncate; F = 255 still
  fogs), checked on the Super Speedway GS dump's road passes. Implemented in `model.frag`; the
  race frame's few levels of extra brightness are gone.
- 2026-11-02: §M6 the VU1 clipper at the guard band [C]: vf23 (0.5, 0.5, 1, 1), the planes' order,
  the polygon walk, the strip it sends and the rounded colours; checked corner for corner against a
  GS dump. Implemented (`render::guard_clip`, `DrawItem::guard_band`); the near road's sheen now
  falls off as the original's.
- 2026-11-01: the replay depth of field (§G8) [C]: `fn_001ACF38`, gated by the play mode and Env0
  flag 0x100 (the frame survey had taken it for the heat haze); the lens from Env0 +0x930..+0x938
  and the camera's +0xD0 focus, a 5-level pyramid, depth-tested layers. Ours: implemented.
- 2026-11-01: the sun disc, the flare system's disc mesh (every lamp halo: not a 72-spike burst,
  which is the glare's style 1), the rear-view mirror's passes (its smoke, no pylons, billboards
  by Env0 +0xF30 / +0xF34 / +0xF38) and the round meters' 8 needle passes [C], section Q.
  Implemented: `race::SunDisc`, `Flare::disc_mesh`, `ItemsView`, `Hud::needle_passes`.
- 2026-11-01: the passes after the 3D picture (§R) [C]: their order (the haze lenses, the smoke,
  the glare and lamp glows, the grade, the vignette, the dissolve, the trail), the heat haze (the
  GTRC record +0x1C camera flag, Env0 0x20 and +0xB0..+0xC8, its set-up, drift and lens; projected
  with the view's rotation and the projection: always ahead), the replay vignette, no grade in the
  2P halves, the field copy. Ours: implemented except the field copy's filter.
- 2026-11-01: §S, the race wheels: brake parts from race/brake.bin, the rotors' glow from the
  brake heat, the tyre and rim spin blurs, the visual camber, each wheel's own detail level
  [C]. Implemented (`race/wheel_fx.h`, `formats/model/brake_model.h`, `CarVisual`).
- 2026-11-01: §T the sparks (`ParticleManager`): bursts from the cars' underbody spark points below the road at 100 km/h
  or more on materials 0 / 1 / 7 / 9, not from hits; the pool, the burst's and the update's draws of the VU0 random
  register, the bounce, the streak draw and its colour keys. Implemented (sim::exact::Sparks, race::SparkDraw), the
  simulation bit for bit against the original.
- 2026-11-20: §G4g the course camera's operator [C] (public issue 370): records looking at the car aim
  through a lagging, leading, randomly geared filter per angle, the pitch high by 0.4 k cos, a bob while panning, and
  the zoom on a spring; mode 1's yaw seed (§G2b step 5) is that filter's first-frame swing, not a no-op.
  Implemented (`race::CourseCameraAim`).
- 2026-10-30: the race behind a quick menu and the licence menu's preview box are silent [C].
  Every race driver sets R+0x6C (records off) just before its quick-menu runner and clears it
  after (time trial 0x130EA8, free run 0x13046C, machine test 0x131AD0, licence 0x132464, GT race
  0x132E24, championship 0x133680); the menu's race is (re)started by `RaceBase::vf9`, which sets
  the SE mute (race +0x9788 and 0x3521E8, `fn_0017BBB8`) from R+0x70 or R+0x6C, and
  `RacePS2Base::vf30` skips the sound system's frame, every car's sound routine (0x177A50: the
  engine units and the 32 effect slots, `EngineSound` 0x1A6C38) and the course ambience (0x1A79C0)
  while it is set; `RaceBase::vf30` then leaves the race-SE factor (0x3521E0) alone. The licence
  driver's DEMONSTRATION runs with R+0x6C clear (`RacePS2Base::vf42(1, 1)` at 0x13295C): heard.
  Ours: the preview box was playing its tyre and collision sounds (fixed, `RaceScreen::se_on`).

- 2026-10-29: the time formatters and the "none" placeholders [C, all three programs: NTSC-U
  1.10, PAL and NTSC-J the same bytes and code]. Every placeholder is drawn with the system
  fonts' (sw_12, sw_16, sw_20, sq_9) long dash, code 0x7F (sw_16: 12 px against the hyphen's 7),
  never '-'; the strings are game text. Ours wrote ASCII hyphens everywhere (implemented:
  `race/time_text.h`, tests `race_time_text_*`).
  - The race display's `fn_0019A7F0` (callers: the HUD's `RaceTimeDisplay::vf5`,
    `RaceLapTimesDisplay::vf5`, the pop-ups `RaceDisplay::vf21` / `0x193260` / `fn_001945B0`,
    the licence result's `fn_0018AD58`): ms < 0 or == 0x157529FF -> the eight bytes built in
    registers (`0x19A808`: addiu 0x7F7F, pcpyh, lui/ori 0x7F2E277F, pextlh) =
    `\x7F'\x7F\x7F.\x7F\x7F\x7F`, one dash for the minutes; else clamped at 59,999,999,
    "m'ss.mmm" with 1-3 minute digits (ours `Hud::format_time`).
  - The menus' `fn_0023B930` (unsigned): == 0x157529FF exactly -> the 10 bytes at 0x343A28
    (PAL 0x345398, NTSC-J 0x33DB68) `\x7F\x7F'\x7F\x7F.\x7F\x7F\x7F`, two dashes; hours
    > 0 -> "%d:%02d'%02d.%03d" (0x343A38), else "%d'%02d.%03d" (0x343A50). Callers: the quick
    menus' `qm_list_time<n>` (0x12D240: the result list +0x38 + 0x18 n +0xC, which
    `fn_0012F070` fills with a car's +0x14B4 when it has finished (+0x1464), else
    0x157529FF: the pre-race list and a car still racing), `qm_rank_time<n>` (0x12CE68),
    `qm_best_time` (0x12CCA8, info +0x24; no layout uses it), `qm_mt_1000_time_rank<n>`
    (0x12D158; no layout uses it), the licence targets (0x12D540 selectors 1-3, from the
    event's bytes, never "no time"); the records page (`fn_00171CA8` from the
    `RecordsMenuArg*::vf4`); `ResultImplementRecords::vf10` (the time-trial overlay's "Lap");
    `ResultArcade::vf10` -> `fn_001B21A0` -> `fn_001B1750` (" " / "+" and the time or the gap
    to the leader in ms: finished cars only, never "no time"); the Replay Theater's
    `list_track_time_info` (`fn_0016B7B0`) and the Analyzer (`LoggerReplayRoot::vf10`,
    `LoggerGraphRoot::vf10`); the licence trophy times (`fn_0011DF08`, bytes); the memory-card
    file page's `b_pt_time` (`fn_0014BE20` / `fn_0014BBF8`; not ours); the arcade time
    trial's `first_time`..`third_time` (`fn_00103FC8`: the ranking's time, or the ASCII
    "--:--:--" at 0x3096C0 when it is 0). Ours `race::menu_time_text` (also the overlay's
    hours, which ours had already).
  - No speed `\x7F\x7F\x7F.\x7F`: the HUD's speed lines (`fn_0019A968`, from
    `RaceSpeedDisplay::vf5`: <= 0 or == FLT_MAX 0x7F7FFFFF; else "%.1f" of min(v, 999.9)) and
    the records' top speed `fn_0023BA08` (<= 0, + the unit; NTSC-J also holds it with "km/h"
    at 0x33DBA0). No date `\x7F\x7F/\x7F\x7F/\x7F\x7F`: the records page's
    `RecordsButton::vf3` for stamp 0 (0x3256F8); `fn_0023BAF8` (the memory-card date) has its
    own: year < 0 the same, and with the long form (arg 5, the memory-card file pages'
    `fn_0014B6D8`) a year below 2000 `\x7F\x7F/\x7F\x7F/\x7F\x7F\x7F\x7F` (0x343A90) or in
    Japanese `\x7F\x7F\x7F\x7F/\x7F\x7F/\x7F\x7F` (0x343A80). Not reproduced: the
    demonstration list's Date (ours `--/--/----`, ASCII; the original's comes through the
    memory-card file page, not checked).
- 2026-10-28: §B3 the title's text: the course table's name copied (`fn_00238A00` → strcpy
  0x191DF4) and capitalised byte by byte, a signed compare of 'a'..'z' only (0x191E10..0x191E38),
  so the game text's escape bytes (≥ 0x80: "Côte d'Azur" is `C {81 F4} te d {A1 C7} Azur`, a
  Latin-1 ô and a JIS quote) pass unchanged: drawn as game text, "CôTE D’AZUR". Ours drew the
  bytes as UTF-8 ("C??TE D??AZUR", public issue 45); now game text (`Hud::draw_title`).
- 2026-10-25: §E the licence test's result R+0x40 is 2 / 3 / 4 for gold / silver / bronze (1 only
  for a time of 0), 0 a time slower than every target, -1 out of the course, as §A1 has it; the
  2026-10-21 entry's "the medal 1..3" was wrong [C, the race flow's port `sim::exact` checked
  against the original in `race_tick_exact`]. Ours is not affected: `race::RecordFinish::result`
  is its own 1..3 / 0 and only zero / non-zero decides (`locks_retry`), the licence screen judges
  the medal itself (`race::license_medal`) and a course-out run never reaches the check.
- 2026-10-22 (car morphs): §P2b the car callback (op 0x06 ids 0-5) and its per-frame values;
  implemented (`gtm1::MorphDraws`, `race::CarVisual`).
- 2026-10-22 (moving nodes): §P2a the GTM1 +0x3C animators (types 1-7, op 0x34 / 0x35); implemented.

- 2026-10-22 (CLUT animations): §P added: Tex1 +0x24 palette animations (the course's camera
  flashes, blinking lights, start-signal lights), their update / apply / reset / release code,
  the start sequence's group-1 step and the frame step of 1/60 s; why Route 11's grandstand
  glow cards (node 1216, chunk 971) are not seen in the original. Implemented.

- 2026-10-22 (reflection map): §O how the car reflection map is built (the map camera, three
  panels, the embedded warp mesh); our cube faces were turned half a turn and our dome turned
  in the wrong order with the wrong signs.
- 2026-10-22 (draw order): §N the race frame's scenery pass (one DMA chain in node order, before
  the wet road and the cars) and each car's shadows before its body; Laguna's floating ground
  overlay (node 575) that cut a band across the car when drawn after it.

- 2026-10-22 (course lighting): §M added: the VU1 colour programs per chunk
  kind, the course's scene lights and the sheen's local viewer, per-vertex fog, wet and dirt
  courses, the mirror and the reflection map. Implemented (§M5). Then: kind 4's program from the shininess alone
  (Ms = 0 materials add nothing); opaque packets with the alpha test off are drawn whole (§M5).

- 2026-10-22 (demonstration start): §G5 new: the course camera's shot picker of view modes 0, 2
  and 3 (`fn_0020AD18`) in full: the set-up leaves it on the script's last record and the
  view's reset re-arms it, so a replay's first frame cuts to the record at D at any progress;
  the lap variant is 0 before a car's first crossing of the start line (D = s: the script's
  first records cover the start); the records the game finds at D; the shot states and the
  gate (modes 0 / 3 cut when a shot runs out, mode 2 on x >= 1.3 s or a request); other lap
  variants (held 3 s) and turning on board (the in-car view) when no record fits; the outgoing
  shot's dissolve at searched cuts only. Every replay (automatic, REPLAY, theater files and
  demonstrations, attract demo) opens this way; start.cam is the live views' shot only, with
  1 s dissolves at its cuts (§D 7, implemented: ours had shown it in single-player races only,
  without dissolves). The GTRC's second list is course stills for a dead
  slideshow, not start shots. Mode 2's on-board cut lasts until its shot ends (not 180
  frames); TOP / AUTO wait 3 s after every set; the attract car change waits for the gate.
  Implemented (ours had k = completed laps + 1, the first matching record at u <= 0.6 kept
  until the car left it, the incoming record's dissolve, a 3 s partner cut and no gate). The
  arcade time-trial demonstrations'
  openings listed in §G5.
- 2026-10-22 (view mode 1): §G rewritten for the avant-garde mode from the camera manager, the
  race music object and the race renderer: §G2a the cut requests and shot length from the
  songs' markers (music §7) and the countdown, corrected (the request and length functions run
  every frame for every view mode; requests 1..4; `race+0x5C` is the standing-start hold; the
  1.3 s threshold gates modes 0 / 2 / 3, not mode 1); §G2b the five procedural cameras
  (Automatic, Automount, Matrix, Zoomup, a GTCI mount) and every draw of a cut; §G6 the shot
  look and the grade as an 8-bit post-process pass with the course's Env0 defaults (+0x98C /
  +0x990 levels) and its gating; §G7 `+0x268` is the dissolve (p 0.4, 0.1 for request 3) and
  `+0x25C` the trail (p 0.1, 0.6 after a MatrixCamera cut), both measured on the footage, and
  the dissolve's other triggers (every replay / demo start: the "dimmed start"); §G4c Up / Down
  +0.1 / -0.1, Right on / Left off. Implemented (ours: the 121-frame timer, x = 2 s, stand-in
  cameras, a per-fragment grade without the Env0 levels, no dissolve or trail).

- 2026-10-22 (replay overlays): §K3 the start title's clock and forms in full, and its run
  kinds (every live run and replay, never the attract demo or the backdrops); §G when the
  replay mark is drawn (no ITEM on; the attract demo always) and its 64 s opacity timer, both
  checked against PCSX2 footage of the original. Training (mode 3) = Normal, and the only race
  line is course slot 27 drawn whenever the course file has it (course-line branch).

- 2026-10-21 (course line): new §L. The race line on the road is course slot 27 (13 licence course
  files): drawn whole after the scenery and billboards by `fn_001A93B0` whenever present, with
  node 0's state (no culling, no depth write, FBMSK 0xFF000000) carried into node 1. Per test it
  is on exactly when the event's course file has it (B-3 .. B-8, A-1 .. A-3, A-6 .. A-8). Replay
  mode 3 draws nothing extra (no reader of the mode outside the camera code and ReplayOption).
  Ours drew the slot culled (invisible); fixed.
- 2026-10-21 (flying start signal, quick-menu items): §B0 new: a flying start's start signal
  is the start-line crossing that ends the computer's run-up (time trial, flying licence test,
  free run; the conductors' vf17 post event 4): `count_s`, "@start" for 1 s, the race music,
  and the free run's two notes [C]. Implemented (ours played nothing and started the music at
  the run's start). §A8: a licence run left with EXIT disables REPLAY (+0x20 of the menu
  block cleared at 0x132648; the earlier "REPLAY plays the demonstration [I]" was wrong), and
  DEMONSTRATION disables it too (menus.md §11 "Item availability"). Implemented.
- 2026-10-21 (replay cameras): §G rewritten from the camera manager: the replay pad (no mode
  buttons: the view mode changes only in REPLAY OPTIONS, and the mode indicator is never
  shown), MANUAL's Up / Down, Cross's driver's view, view mode 1's pad cuts, the battle table
  and interest scores behind TOP / AUTO and the two-car framing (§G4 new), the attract demo's
  car switch, BGM stored on with the avant-garde mode, modes 0 and 3 identical (no other
  reader of the view mode found). Implemented: ours cycled the mode with L1 / R1 / L2 / R2,
  chose the car with Left / Right, followed the leader at once (TOP) or the closest gap at
  every shot (AUTO), had no driver's view, no pad cuts and no pairs.

- 2026-10-21 (replay pop-ups): §I / §K3 corrected: `RaceDisplay::vf4` draws the event display
  (and the gap's car picture D+0xA43C) inside the item-4 block (`beqz` at 0x193760 skips to
  0x1939CC): in replays and demonstrations only with REPLAY OPTIONS ITEM time (default off);
  live play always (its mask has bit 4). The layer byte D+0x246C is 0x1F from the constructor
  (0x190C74) and its setters vf11..vf15 are called only by the data logger's menus and the 2P
  split display's forwarders: never cleared in races or replays. The second event display
  D+0x838 is never posted to or ticked (only its constructor, destructor, the layout builder
  and vf4 touch it; every add goes to D+0x3BC): it draws nothing. The followed car D+0x2469 is
  the current camera view's target (+0x4A8), set every frame by `RaceSinglePlayer::vf29` /
  `RaceLinkBattle::vf29` / `RaceSolitaire::vf29` (display vf9), and the layout view D+0x10 =
  (current camera's vf14 != 0) (display vf10): an on-board camera's mount index (+0x100, 0 =
  the in-car view), a designer camera on a car mount (+0xA78 == 3) its mount, every other
  camera -1. In replays every event is posted as live (lap time, splits, race gaps, the
  machine tests' times and top speed, pit-in), but not the time trials' / free runs' gaps to
  the best lap (0x37AC), and the licence pass's LapTime is shown (`RaceLicenseDisplay::vf21`)
  [C]. Implemented: ours showed no pop-ups in replays (`race::lap_event_rules`,
  `event_display_of`, `Hud::event_row`).
- 2026-10-21 (post-run replay): §A8c new: the drivers' sequence after every run (implemented:
  `race::post_run`; ours replayed only finished GT single races and 2P battles both players
  finished, with a dim and a Cross / Circle skip in 2P that has no source; the arcade cup and
  the championship prize now come before the replay). The quick menu's backdrop: play mode 2
  (`ShortmenuRender::vf2` 0x12C9F8 steps the race through `RaceBase::vf2` while +0x5C, default
  1; `vf3` 0x12CA50 draws it with `vf45`); in mode 2 `RaceSolitaire::vf29` 0x187A50 posts no
  end event, and the playback past the recording (`fn_001F4660`) sets the done flag and
  returns the cleared frame (`fn_001F4330`) [C]. The result menus' layouts have an opaque
  checker `bg` (7E7E7EFF). §E pause EXIT as a finish in the free run (`fn_001897D8`) and the
  top-speed test (`fn_0018CFB0`, race +0x21800 = the driver's test index 2, MMAX) [C]. §A8 the
  licence driver loads `demo/%s` (`0x12F5A8`) over a run that did not end (0x132624) [C].
  §I replays: the lap events are posted without a play-mode test (0x1EC620) and the replay
  layout (11) has the event display, drawn under the layer bit 0x10 and not the item mask
  (`RaceDisplay::vf4` 0x19396C) [C]; who clears that bit (`vf15`) is not traced, so ours still
  shows no pop-ups in replays. §B3 corrected: a free run does post the finish event (its pause
  EXIT after a lap).
- 2026-10-21 (later): §B3 the time trial is one lap: `DynamicsConductorTimeTrial::vf18` ends
  the car at every completed lap (stop driver mode 3, event 5) and its lap total (vf30) is the
  constant 1; the AT events' laps byte (2) is never read (implemented; ours had run the event's
  2 laps). §B3 / §E / §I R+0x6C is not set by a FAIL: only the drivers write it, around the
  runs behind their quick menus (0x130EA8 / 0x130F34); the FAIL (`fn_001F1BB8`) posts event
  0xA and hands the car to the stop driver (mode 2), whose hand-over marks the car ended
  (F+0x4E0), so its crossings count no more laps. §B3 the record overlay's timing (5.0 s, then
  10.0 s or confirm, Circle / Cross; implemented: Circle had not counted). §A8 the ghost
  actions were swapped: 10 = load (0x13), 11 = save (0x12). §A8b no menu-side record:
  implemented (ours had also entered the posted result's last lap after leaving).
- 2026-10-21: §B3 / §E the RETRY lock is the session record check: a time at or under the
  session's best lap before it (none yet counts), never the stored record; R+0x40 is the
  run's result (-2 time trials and machine tests, so always; licences the medal 1..3, 0 none),
  R+0x6C the drivers' quick-menu flag (not a FAIL: corrected in the later entry), R+0x70 the
  data logger, R+0x4C the record flag the
  drivers act on after the run (implemented: `race::sets_record`; ours had compared with the
  stored record's best entry). Licence and machine-test times join the session best.
- 2026-10-21: §I / §K3 / §B3 the session's best lap (organisation +0x8AC / +0x8BC) starts at
  "no time": no stored record seeds it (the setters have no callers; 0x2B4780.. is the
  function descriptor table, not a set-up command table), and it lasts the visit, retries
  included (the organisation is built once before every driver's run loop) (implemented;
  ours had seeded the gap reference and Record Lap / P.P. with the stored record and reset
  them every run).
- 2026-10-21: §K a time-trial FAIL is not a finish (only "ended"): its Time runs on with the
  clock (implemented; ours had shown the best lap, then the laps plus the best lap).
- 2026-10-21: §K the race display's layout per mode: how each race class picks its mode
  (endurance for 10 laps or more or a time limit, replay / demonstration layouts), the layout
  builder's row format and part types, the time lines' values (Total, the lap-time list,
  Record Lap / P.P. / Fastest = the session's fastest lap, the Lap line gone after the finish),
  the item mask, the title's 160 % font and per-mode width, the replay's small cluster, car
  name, steering and accel / brake monitors (implemented: `race::hud_layout`; ours had the
  race layout with "Remaining" for time-limited races, Best / Time for free runs and free
  practice, the map at (16, 80 / 32) by car count, WRONG WAY in licence tests, the full
  cluster and Best / Last / Lap in replays, the 2P Fastest as the player's own best lap).
- 2026-10-21: §B3 the machine tests' HUD: the race display's layout rows (`racemode.db` /
  `racedetail.db`; M400 / M1000 mode 4, MMAX mode 9): no lap count in any test, the map at
  (16, 16); MMAX has no event display and no message display (no pop-ups, no WRONG WAY) and
  two speed lines instead of the times, "Record" (RaceOrganization +0x8A4: zeroed by its
  constructor and never written, so dashes) and "Best" (the display's own maximum of the
  shown speed); §I corrected: MMAX's pop-ups are queued but never drawn (implemented; ours
  had the lap count, the times and the pop-ups in MMAX).
- 2026-10-21 (latest): §A8b the free-run driver (course time trials) and the replay every
  run-driver plays after a run (not reproduced); §B3 the free run's result overlay is never
  started (no finish event).
- 2026-10-21 (later): §B3 corrected: the machine-test quick menu's Top 3 speed is not
  `fn_0023BA08` but its own handler 0x12CD60, "%4.1f %s" (rounded, width 4, a space, the
  unit), and it always shows three rows (the tables hold 12 entries from a new game);
  `fn_0014BE20` is the memory-card file page's `b_pt_time` field (a best-lap replay's time,
  or `fn_0023BA08` for a top-speed run). §A8b the machine-test and time-trial drivers record
  every run with its name entry before the quick menu, as the licence driver (implemented;
  ours recorded one run per session when the race screen closed).
- 2026-10-21: §B3 a top speed in the records is shown in the Options unit everywhere
  (`fn_0023BA08` from the result overlay and GT Mode's records page
  `RecordsMenuArgMaxSpeed::vf4`; the quick menu's Top 3: corrected above); ours had the Top 3
  and the records page in km/h always (implemented: `race::record_speed_text`).
- 2026-10-21: §B3 the machine-test conductor (test type = the event's laps byte, timing
  points 400 / 1000 / 6845 m of track distance, no line handling, the goal handler, the HUD
  time, the top speed frozen at the finish), §I its pop-ups (splits, the time or "%.1f%s" at
  the goal, no gaps) (implemented; ours had run MMAX for the event's "laps", measured M400 /
  M1000 as distance driven, and seeded MMAX's time-trial gaps with its speed records).
- 2026-10-20: §I the in-race pop-ups (lap time, "T%d" splits, race gaps with the other car's
  icon, "+N Laps", time-trial gaps to the best lap; the event display's queue and timing;
  the GTRW checkpoints), §J the race loading screen (the menu's Fadeout and the "still
  loading" cursor, no course picture) (implemented).
- 2026-10-20: §B3 the record overlay (ranking in the stored 12-place table, labels, machine
  tests), §E the RETRY lock, §G the view modes 0-3 (mode 2's on-board cut, mode 1's cuts and
  procedural cameras), the attract demo's view pick and the director's generator, records
  with a duration on time, §H the speedometer, §C1 the race exit wash colour (implemented).
- 2026-10-20: §F the prize counter step by step (single precision, the tripled step stored
  back as the velocity, the velocity cap drops the acceleration to 0) and its beep (played on
  the step's frame, never on the finishing step, never holding the count); exact frames.
- 2026-10-11 (draft 1): licence finish (medal and fail), race and time-trial
  finish, race ↔ menu transitions.

## Summary

- **Behind the medal:** the live race keeps running. The car is under the
  stop driver, the player's race camera and the HUD stay active, and a black
  dim of at most 1/3 opacity is drawn on top. There is no replay camera, no
  frozen frame and no hidden HUD.
- **Medal timing:**
  - 6 frames wait, then a 30-frame dim ramp. The jingle starts at the end of
    the dim.
  - Then 180 frames (gold) or 30 (silver/bronze) wait. After that the model
    fades in over 60 frames, the word over about 31, and the time text over
    60.
  - Then a 300-frame hold, plus 300 more frames that the confirm button can
    skip.
  - Total about 14.2 s (gold) or 11.7 s (silver/bronze).
  - The medal model flies in for 2 s, then turns in a loop every 3.33 s.
- **Fail or slower than bronze:** "Time Over" or "Disqualified" text with a
  bluish wash and split-merge text, after a 1 s delay. There is no medal and
  no "NO PRIZE".
- **After the presenter:** the quick menu, whose preview box shows the race
  replaying live.
- **Race finish:** "@finish" message for 4 s and a finish jingle. The
  results panel starts at 5 s, with rank bars every 0.12 s. Confirm or a
  15 s timeout after all cars finish ends it.
- **Transitions:** a loader overlay is drawn over the last frame (race exit
  = delayed 50 % wash). The next screen arrives as a cut, with no fade-in.
  BGM is not linked to the screen fades.

## Details

Event ids on the race queue (`fn_001796C0` post(queue, id, arg, prio); `fn_00179640` poll/consume;
queue at race obj + 0x84):

| id | meaning | posted by | consumed by |
|---|---|---|---|
| 1 | leave the race loop (go back to the menu code) | display stage machines, `ResultArcade::vf9`, `RaceBasic::vf29` (replay mode 2), `RaceSolitaire::vf29` | `RaceBase::vf28` (0x17C630) -> vf34 -> `RacePS2Base::vf34` sets `race+0x8000+0x17C0 = 1`; next `RacePS2Base::vf28` runs shutdown `fn_0017C970` |
| 2/3 | start-sequence tick / "GO" message (3 is posted by `vf33`) | `fn_0017A5E8`, `RaceBase::vf33` | displays (`fn_00192880` drains) |
| 4 | start signal (GO) | `fn_0017A5E8` after the hold, `DynamicsConductorLicense::vf17` etc. | `RaceBase::vf28` -> `vf33` (posts 3 and BGM 0x14/0x14) |
| 5 | a car finished (arg = car, prio 2) | `DynamicsConductor::vf18` 0x1EFB70, `DynamicsConductorTimeTrial::vf18` 0x1F1D40, licence pass in replay | displays |
| 7 | the race's time limit ran out (organisation +0x68; only the attract demo sets one, gt_mode §15) | `fn_0017A5E8` 0x17A764 | `RaceArcadeDemo::vf28` -> `vf35(5)`: exit reason 5, the next demo |
| 9 | ExamFail (arg = car) | `fn_001EF520` (licence fail), `fn_001EF5C0` (licence time slower than all targets) | `RaceLicenseDisplay::vf3` first; if still queued `RaceBase::vf2` polls it after `vf29` and ends the race |
| 0xA | TimeTrialFail | `fn_001F1BB8` (called from `DynamicsConductorTimeTrial::vf13`, player car only: car index byte `F+0x3E8 == 0`) | `RaceTimeTrialDisplay::vf3` |
| 0x13 | "new best"/result decided | `fn_001EFDE8` | `RacePauseWithRetry::vf3` |
| 0x14 | BGM request, arg = music id (-1 stop, -2 pause, -3 resume, 0x13 finish jingle, 0x1D/0x1E/0x1F medal, 0x20 fail jingle) | many | `RaceBase::vf28` -> BGM `vf5` (`RaceLicenseBGM::vf5` turns arg 0x14 into stop) |
| 0x17 | error message ("VisionList is invalid or empty") | `RacePS2Base::vf47` and camera code | - |

---------------------------------------------------------------------------------------------

## A. Licence test finish

### A1. Judging registers [C]
`R` = the race object (`RaceDisplay+0x18`).
- `R+0x40` result code, `R+0x44` result time in ms. `DynamicsConductorLicense::vf23` (0x1F0B18)
  resets them to -2 and `0x157529FF` (= 359,999,999 ms, "no time") at the start.
- Pass handler `fn_001EF5C0` (called from `DynamicsConductorLicense::vf18/vf19`): calls the stop
  driver starter `fn_001EACB8(car, mode)` with mode 1 if the exam state byte (`+0xE340+2`) is 1,
  else 3. Then walks the 5-entry threshold list of `fn_0017AD78(i)`: i = 0 -> 0, 1 -> gold
  (+0x1C4), 2 -> silver (+0x1C8), 3 -> bronze (+0x1CC), 4 -> 0 (times in ms = min*60000 + s*1000 +
  ms). It takes the first i whose threshold is not smaller than the time; code = i + 1.
  So: **gold = 2, silver = 3, bronze = 4**; 1 and 5 can only occur for time 0. No threshold
  matched: code stays 0 and event 9 (ExamFail) is posted with the time stored (`R+0x44` = time).
  Pass: `R+0x40 = code`, `R+0x44 = time`; then `fn_001EFDE8` and the time-event objects.
  (When the byte at `race+0x8000+0x37AC` is set, i.e. a replay/demo run, the code is not stored
  and event 5 is posted instead, so no presentation runs.)
- Fail handler `fn_001EF520`: `fn_001EACB8(car, 2)` (stop driver mode 2: `fn_001E3E80` writes 7 to
  car byte +0x14B8), event 9 with arg = car index, `R+0x40 = -1`, `R+0x44 = 0x157529FF`.

### A2. What is behind the medal [C for code path, I for the camera]
- The race keeps running. The car is handed to the stop driver in the same call that judges
  (`fn_001EACB8`; modes: 1 -> `fn_001E3E38` driver kind 6; 2 -> `fn_001E3E80`; 3 -> `fn_001F6518`
  (halves one value, sets +0x6C = 0.5) + `fn_001E3C78`); the HUD and the 3-D scene are drawn as
  usual, then the presenter on top: `RaceLicenseDisplay::vf4` (0x18C5C0) calls `RaceDisplay::vf4`
  (the whole HUD) first and only then the message object. HUD stays visible under the dim.
- No camera mode switch exists in the finish path: none of the camera functions
  (0x1F8000-0x215000) read the car end flag (+0x4E0) or finished byte (+0x3EA); the replay camera
  is not used; there is no frozen frame. The view is the player's current race camera following the
  decelerating car. [I: absence of any camera reader; the code that builds the cameras was not
  walked exhaustively]
- BGM: `RaceLicenseBGM::vf5` (0x189B70) replaces the race-music request (arg 0x14) with stop; only
  the start jingle and the finish/medal/fail jingles play. No BGM fade (see C).

### A3. Display stage machine `RaceLicenseDisplay::vf3` (0x18C458) [C]
`D` = the display. Stage byte `S = D[0xA690]`, sub-stage `D[0xA691]`, timer (int) `D[0xA68C]`,
"success layer" flag `D[0xA698]`, message object `M = D+0xA47C`, input object pointer
`D[0xA694]` (= race+0xC8C0, button mask via its vtable +0x30; bit 0x40 is the bit the result screens
use for confirm). Setter: `fn_0018BF80(D, idx, value)` writes `D[0xA690+idx]`.

Jump table 0x32C9D0:
| S | action |
|---|---|
| 0 | poll+consume event 9: if present S = 3. Else if `R+0x40 > 0` (note: -2/-1/0 do nothing) set `D[0x2470] = 0`, S = 1 |
| 1 | `D[0xA698] = 1`, S = 2, sub = 0, call success presenter `fn_0018BF98` |
| 2 | call `fn_0018BF98` every frame |
| 3 | `D[0xA698] = 0`, S = 4, sub = 0, post BGM event (0x14, 0x20) = fail jingle, call fail presenter `fn_0018C1E0` |
| 4 | call `fn_0018C1E0` every frame |
| 5, 6 | S = 6 then `fn_0018C400`: S = 0, sub = 0, **post event 1** (the next frame the race loop is ended) |

Order inside one frame: `RaceBase::vf28` pumps events (1, 4, 0x14), then `vf29` ticks everything
(including this machine), then event 9 is polled. Event 1 posted at S = 6 is therefore seen one
frame later; its handler sets the exit flag, shutdown runs on the following frame.

### A4. Success presenter `fn_0018BF98` + message object `M` (`LicenseExaminationMessages`) [C]
`M` fields: +0 active, +1 done, +4 vtable, +8 int counter, +0xC state (0..5), +0x10 t1, +0x14 t2,
+0x18 t3 (floats), +0x1C type (R+0x40), +0x20 string/id, +0x28 text-fader {+0 id, +4 alpha, +8 mode,
+9 enable}, +0x34/+0xD0/+0x16C three `LicensePrize` (gold, silver, bronze; 0x9C bytes each, files
`race/gold.prm`, `silver.prm`, `bronze.prm`, 0x32000-byte load buffers), +0x208 current prize
pointer, +0x20C jingle flag (= `D[0x246F]`, the "this display is visible" byte). Per frame the
presenter calls `M.vf1` (state machine, 0x18B510) then `M.vf3` (0x18B758: advances the prize
animation if started, `counter--`, text-fader update).

Presenter sub-stages (`D[0xA691]`):
- sub 0: timer = 6, sub = 1 and fall through (so the first wait ticks 6 frames; `M.vf9` is
  called on the 7th frame after the display saw the result).
- sub 1: when timer <= 0: timer = 1800 (safety), and if `R+0x40 > 0` call `M.vf9(type = R+0x40,
  time = R+0x44, jingle flag = D[0x246F], string 0, showTime 1)`; sub = 2.
- sub 2: run `M.vf1/vf3`. When `M.done` (+1) is set or timer <= 0: timer = 300, sub = 3.
- sub 3: run `M.vf1/vf3` again. When timer <= 0, **or** the confirm bit (0x40) is in the input
  object's mask: `M.vf8` (active = 0, prize stop), S = 5. (The only skippable part is this last hold.)
- the timer is decremented once per call at the end.

`M.vf9` (0x18B2C0) start: type -> prize pointer through table 0x32C6E0 (types 1,2 -> +0x34 gold; 3 ->
+0xD0 silver; 4,5 -> +0x16C bronze), `fn_00229F28` resets the prize animation to its start frame,
prize hidden (+0x8C = 0), state = 0, t1 = t2 = t3 = 0, active = 1, done = 0.

`M.vf1` state machine (jump table 0x32C700), counts are frames at 60 Hz:
| state | what | length |
|---|---|---|
| 0 | clears, falls into 1 | 0 |
| 1 | t1 += 1/30 per frame (f32 0x34FD2C); at t1 >= 1.0: t1 = 1.0, post the jingle (only if the jingle flag is set): gold event (0x14, 0x1D), silver 0x1E, bronze 0x1F, OR a bit into the global at `fn_00218660()+0xCC` (0x20000 / 0x40000 / 0x80000 respectively); set counter = **180** (gold) or **30** (silver, bronze); state 2 | 30 frames (31 if the float sum lands under 1.0) |
| 2 | wait while counter > 0 (`vf3` decrements once per frame). At counter <= 0: `fn_00229F68` (prize visible + animating), t3 = 0, text-fader mode = fade in, state 3 | 180 / 30 frames |
| 3 | t2 += 1/30 per frame (0x34FD30); at t2 > 1.0: t2 = 1.0, counter = **300**, state 4. t3 += 1/60 per frame from the first frame of this state (clamped 1.0) | ~31 frames |
| 4 | wait counter 300 (5 s); then done (+1) = 1, state 5 | 300 |
| 5 | idle (only t3 clamp) | until the presenter ends it |

Text fader (`fn_0018ACE0`): mode 1 adds 1/60 per frame to alpha (+4), mode 2 subtracts 1/60 and
stops at 0 (mode 0). It is never clamped at 1.0 in the update; the draw clamps (see below).

Timeline from the frame the display sees the result (f = 0):
- f 0..6: nothing new (the world is still decelerating under the stop driver).
- f 7: `M.vf9`. f 7..37: dim ramps in.
- f ~37: jingle posted. Gold: model and words start at f ~37+180 = ~217; silver/bronze at ~67.
- model fade-in (t3) 60 frames from there; word alpha ramp (t2) ~31 frames; time-text fade 60 frames.
- hold 300 frames, then the presenter's own 300 frames (skippable with confirm), S = 5, event 1.
- total: gold about 7+30+180+31+300+300 = ~850 frames (14.2 s), silver/bronze about 7+30+30+31+300+300
  = ~700 frames (11.7 s), before the exit.

### A5. What `M.vf4` draws (success, 0x18B798) [C]
Order: (1) dim, (2) prize model, (3) medal word, (4) time text fader.
1. **Dim**: full-screen quad (0,0,w,h) in black, RGBA word 0x80000000 (`fn_0025A038`), blend mode
   id 0x64 with constant alpha `pgl_set_alpha(0x64, t1/3)` (0x25282.. stores clamp(a*128, 0, 127)):
   constant alpha ramps 0 -> 1/3 over the 30 frames of state 1 and stays. (Display flag
   `[0x2F424C] != 0` (the alternate aspect/mode flag) changes the rectangle to a centred one of
   height 2*(h/2*4/3+1).) Afterwards state is reset (`pglEnable 4`, `pgl_set_alpha(0x44, 0)`,
   `pglDisable 8`).
2. **Prize model** (`fn_0018AF10` -> `MiniPrize::vf5` = `fn_0018A3D0`): only when prize visible
   (+0x8C == 1). Render state: depth/lighting enables (disable 4,6,5,3,0xF; enable 0xC), blend
   (1,1,1,1), alpha mode 0x44 = 1.0. It is drawn **twice** with sub-pixel jitter (0.25, 0.75) and
   (0.75, 0.25) (table 0x2B96D8) and composited with accumulation weights 1/1 then 1/2
   (`fn_00253828(5, w)`), the final composite weight is **t3** (so the model fades in over the
   60 frames after it starts). The viewport projection: `fn_0022A120(0,0)`: vertical FOV from
   f32 0x3509EC, scale by `[0x2F424C] != 0` x0.75 horizontal. No special backdrop.
   Animation (`fn_002298C0`, per frame when +0x8D == 1): time += step (= header fps/60 = 1.0),
   reset to start 0 at `M.vf9`; at `time >= last+1` it continues from the loop frame (up to 20 wraps
   per call). `race/gold|silver|bronze.prm` (checked with `gt3tool prm`): frames 0..**320**, loop
   frame **120**, 60 fps, 322 model keys, static view (flag 1, view T (0,0,-75.23)), 2 lights;
   model translation z goes -43.69 -> +50.83 over frames 0..120 (fly-in, 2 s), then 120..320 loops
   (one turn of the model, 200 frames = 3.33 s) forever. (The 240/60/60 values mentioned elsewhere
   belong to other `.prm`; for these three it is 320/120/60.)
3. **Medal word** `fn_0018A798`: string = "GOLD"/"SILVER"/"BRONZE" (message table `[0x2C43E8 + 4*0x35..0x37]`),
   font `sw_20`, scale 1.25 x `fn_0018A938` (1.0, or 0.75 if `[0x2F424C] != 0`), centred at
   x = 320 (0x140), y = **336** (0x150). Shadow: the string is drawn twice, first black
   (0,0,0, same alpha) at (x, y), then in the medal colour at (x + 2, y - 2) (offset argument 2).
   Colours (R,G,B):
   gold (255,215,0) = word 0x00D7FF, silver (224,238,224) = 0xE0EEE0, bronze (208,150,122) = 0x7A96D0.
   Alpha = u8(t2 * 128) (so 0 -> 0x80 over ~31 frames; constant afterwards).
4. **Time**: `fn_0018AD58` with the text fader (+0x28): `fn_0019A7F0` formats the ms value
   (m'ss.mmm, `'` and `.` inserted), font `sw_20`, scale 1.25 x `fn_0018A938`, colour = same as the
   word, alpha = clamp(fader alpha x 128, 0, 128) (fades in over 60 frames from the start of state 3),
   centred x = 320, **y = 112 (0x70)**. These agree with `docs/dev/RACE.md`.

No text "animation" beyond these fades. (`fn_0018A960`, a per-character alpha wave, has no
caller and is unused.)

### A6. Failure presenter `fn_0018C1E0` and `M.vf5` (0x18BA48) [C]
Used for event 9 (S = 3/4). sub-stages: sub 0: timer = 60 (**1 s delay**), sub 1: at timer <= 0: timer = 1800,
then `M.vf9(type = -1, ...)` with
- `R+0x40 == 0` (time slower than bronze): text id from `[0x2C4404]` = "タイムオーバー" (Time Over, EUC-JP),
  time `R+0x44` shown (fader enabled).
- `R+0x40 != 0` (-1 = course-out/wall/pit-lane failure from `fn_001EF520`): `[0x2C4400]` = "失格です"
  (Disqualified), no time.
- sub 2: run `M.vf1/vf3`; when done or timer <= 0: `M.vf8`, timer = **48** (0x30), sub = 3;
  sub 3: wait 48 frames, S = 5.
The `M` state machine runs as above with type -1: no jingle in state 1 (the BGM 0x20 "fail" jingle was
posted at S = 3), counter stays -1 so state 2 passes at once, state 3 about 31 frames, state 4 300 frames.
Fail drawing (vf5): bluish wash colour (160,160,255) with alpha `u8(t1*256) >> 1` in blend mode 0x42
over the full screen, then the string with the same font at a vertical split: two copies at
y = 224 - s and y = 224 + s, s = int((1 - t2) * 30), converging to y = 224 (centre of the 448 screen)
over the ~31 frames of state 3; text colour (160,192,64) = word 0x40C0A0, alpha `u8(t2 * 256)`; time line
(if enabled) at y = 140 (0x8C). Arithmetic quirk [C]: `fixunssfsi(256.0) & 0xFF = 0`, and the ramp
code stores exactly t1 = 1.0 / t2 = 1.0 when it completes, so by the arithmetic both the wash alpha
(`>> 1`, 0..127 before) and the text alpha (0..255 before; the fail text alpha exceeds the GS 0x80
"1.0" from t2 = 0.5 on) drop to 0 at the end of the ramp. The meaning of blend mode ids 0x42 / 0x44 / 0x64
(0x42 is the one the transitions use for their alpha-lerp wash) is not decoded, so the visual result of the
alpha values above 0x80 is [I]. Reimplementation hint: wash alpha rises to ~0.5 and text alpha to 1.0,
and keep them there (a wrap to 0 looks like an original bug).
Note this differs from `docs/dev/RACE.md` "NO PRIZE": the original shows "Time Over" (slower than
bronze) or "Disqualified" (fail), with no medal model and no medal word.

### A7. Leaving: licence quick menu (retry / replay / exit) [C unless marked]
1. S = 5 -> S = 6 -> event 1 -> `RacePS2Base::vf34` flag -> `fn_0017C970` (pause audio:
   `vf38` posts BGM -2 and stops engine sounds; BGM `vf8` stop = `fn_0022BA18`; `fn_0017AA48`;
   wake the menu thread `fn_0017CB80`).
2. The race loop `RacePS2Base::vf42` (0x17D3D8) returns after `fn_00102228(1, 1)` (start loader
   transition type 1, see C) + 2 vsyncs + `vf36` cleanup.
3. The licence driver (`fn_00132030`, the same function runs the test) calls `fn_0012E480`: builds
   `ShortmenuLicenseControl` (`qm_gt_license`-style pages) and runs `fn_00134B50`: loop {
   `fn_00134C30` (**ticks the same race object every menu frame**: `vf2` step, and when the
   run/replay finished (`fn_0017CC38 == 1`) restarts it with `vf36` + `vf9(mode = D[0xE54])`;
   sets `race+0x8000+0x178C` accordingly), menu update, `fn_00211AC0(1)` } until the control sets
   its result; then vsync x2, `fn_00101978(0)`, vsync x2, `fn_0022BA18` (stop all sound).
4. `ShortmenuLicenseRender::vf3` (0x1346D0) draws the menu and the **live race scene** into a
   preview rectangle x 32, y 58, w 280, h 210 (0x20, 0x3A, 0x118, 0xD2) through `RacePS2Base::vf48`
   with scale 1.0 (full-screen variant `ShortmenuRender::vf3` uses `vf45`, the whole scene).
   So the "video preview box" is the race object playing back (replay mode `race+0x1784` != 0
   when the caller passed t1 = 1: it is 1 for this call) in a loop; it is not black. The same
   control also runs before the test (pre-race, test name, targets, description) with the same
   preview. Result of the control (action index 1..17 via jump table 0x315F70): start/retry,
   replay, records, exit, setting, ... (not decoded further here).
5. BGM in the menu: result music (`music.md` §3).

### A8. The licence driver around a run (`fn_00132030`) [C]
Loop: quick menu (`fn_0012E480`, result = action 1..17 via the jump table 0x315F70: 1 exit,
2 replay (`vf42(race, 1, 1)`), 3 / 6 start (`vf42(race, 0, 1)`), 8 option, 10 load ghost
(`fn_0012EF40(0x13)`), 11 save ghost (`fn_0012EF40(0x12)`), 14 data logger, 15
demonstration, 17 records (RecordsControl), others nothing). After a run (`vf42` returns):
- a new best lap (result flag +8): with the ghost "update" option on the ghost is replaced at
  once (`fn_0017AAD8`), else the next quick menu opens on its `ghost_dlg` page (the details:
  §A8a);
- the medal (`fn_0012EBB0`: result 1-2 gold, 3 silver, 4-5 bronze, else none) is stored; the
  best medal of the visit is kept; flags: bit 0 = the licence became held with this run,
  bit 1 = it became all gold;
- **a run with a medal whose time makes the test's ranking (`fn_00215940`, 6 places) runs the
  name entry right there** (NameEntryControl), before the quick menu comes back;
- a run that did not end (car +0x1464 clear: left with pause EXIT; skipped when the run's
  result block has its new-best flag R+0x48) gets the demonstration loaded over its data
  (0x132624 -> `0x12F5A8`, "demo/%s"), so the preview box shows the demonstration again, and
  the menu block's REPLAY switch +0x20 is cleared (0x132648): REPLAY is disabled until the
  next run (menus.md §11 "Item availability") [C]. DEMONSTRATION (0x132930) does the same
  (+0x10, +0x14, +0x20 cleared after it) and plays it once as a replay (`vf42(race, 1, 1)`,
  0x13295C; its end: §A8c). Ours: `start_preview` (`demo_loaded_`),
  `race::QuickMenuItems::after_run` / `after_demonstration`.
- on Exit (result 1): the licence card presentation (`license_get`) if bit 0 was set, then the
  licence prize car (`fn_0013F508`) when the licence is all gold and the visit's best medal is
  gold.
Retry from the pause menu (RacePauseWithRetry, §E) restarts inside `vf42` (exit reason 4)
without the quick menu.

### A8a. The ghost and Best in the solo runs [C] (2026-10-25)
The time-trial (`fn_00130A50`), machine-test (`fn_00131690`) and licence (`fn_00132030`) drivers
share the ghost code and its conductors' vf27 (`fn_001EFE80`); the bullets below give the
licence test's details, then the time trial's, machine test's and free run's differences.
- **Best** (`fn_00193040`, D+0x178 = entry +0x11F0, §K3): the licence pass `fn_001EF5C0`
  posts the judged time through `0x1EC620` as a lap (any finish, medal or not, after the
  record check `fn_001EFDE8`); a lap allowed (`fn_001EC4E8`: not the ghost car +0x136E, not a
  replay, R+0x7C and R+0x6C clear) with failure bits +0x14A2 clear updates entry +0x11F0 as
  a minimum (`fn_0017B6D0`). Intermediate line crossings of a lap test post checkpoint events
  only (`DynamicsConductorLicense::vf18` -> `0x1EC520`), never a lap. +0x11F0 is set to "no
  time" only by the entry's constructor (`RaceEntryCar` 0x177210; entries are built with the
  organisation once per visit); `RaceBase::vf9` resets each entry's RaceLapTime (+0x210) per
  run, not +0x11F0. So Best = the visit's best judged time, dashes until one, kept across
  retries.
- **Ghost decision** at the race's end (`DynamicsConductorLicense::vf27` -> `fn_001EFE80`):
  nothing while R+0x6C, in a replay (+0x37AC) or with the logger (R+0x70); the run's
  recorder (race vtable +0xD8 with 0x100) is invalid when its flag +0xCC+0x28 is set or its
  frame count +0xCC+0x30 is >= 36,000 (0x8CA0, 10 minutes): then R+0x4C (the session record)
  is cleared and no ghost. Else, with the player's time (car +0x16C4, set by `fn_001EC4C0` in
  `0x1EC620`; "no time" after a FAIL or EXIT -> nothing): R+0x48 = 1 when there is no ghost
  car (one entry), when the ghost car's +0x136D is non-zero, or when the player's time is
  strictly under the ghost car's own time of this run (its +0x16C4, "no time" if it has not
  finished).
- **After the run** (the driver, 0x1325B0..): R+0x4C -> `fn_0017AD28` (`RaceSolitaire::vf24`
  0x187430: the run's recording copied into the best-run store +0xCB50; entry +0x11F4 = the
  time) and SAVE GHOST / DATA LOGGER on. R+0x48 -> Q+0x14 = 1; options +0x28 (the Options
  menu's Ghost: its widget reads the menu copy +0xE58 + 0x28, `fn_001401B0` / `fn_00142380`;
  on by default) on: Q+0x10 = 1; off: Q+0x10 = 0 and the next quick menu's start page is
  `ghost_dlg` ("Update Ghost?": `qm_yes` `fn_0012CC70` Q+0x10 = 1, `qm_no` `fn_0012CC80` Q+0x10
  = 0). When the quick menu returns (any action), Q+0x14 && Q+0x10 -> `fn_0017AAD8(R, 0, 0)`:
  the ghost entry is created if there is none (count 1), the player's entry copied into it
  (`fn_00177488`: the car, its information +0x1E8 = 1), the best-run store copied into the
  ghost's playback store (`fn_001F4E08(1, 2)` -> `(0, 0)`, 0x8000 bytes). So the ghost is the
  session's best run, not necessarily the run just driven. LOAD GHOST (action 10) loads a
  memory-card ghost file and installs it through the race's vf20 (`fn_0017AAD8(R, 2, 0)`, no
  copy), with no dialog; SAVE GHOST (11) saves a ghost file.
- **The ghost car** is an entry re-simulated from its recorded controls from the run's
  start, beside the player (collisions and laps skip it: +0x136E). Drawn by
  `RacePS2Base::vf51` in mode `fn_001E0668`: a non-ghost car 3; a ghost car 1 (not drawn)
  while race +0x217A4 is 0, else 0 (not drawn) when its +0x136D is 1 or its control state
  +0x14B8 (F+0x534) is 1, else 2 (`fn_00255B70(2)`, translucent). +0x534 is 1 while the
  computer drives the car (`fn_001E3C78`: the stop driver's hand-over `fn_001EACB8`, a run-up)
  and 0 from the flying start's hand-over (`fn_001E3BF0`, `DynamicsConductorLicense::vf17`).
  Only the camera's own car is skipped (`fn_00209370`), so the in-car view draws the ghost too.
- **The ghost switch** race +0x217A4: 1 when the race is built (`RaceSolitaire` 0x186F9C, once
  per visit); `RaceSolitaire::vf29` (0x187AD4) flips it on every new press of raw pad bit
  0x100 (Select: the raw word is pad bytes 3 << 8 | 4, cf. `Control::vf10`'s pressures).
  The licence driver zeroes it around its quick menu (0x132468) and puts it back
  (0x132488); `ShortmenuLicenseRender` zeroes it for the preview box (0x134718).
- **Time trials** (driver 0x130EF8.. / 0x131020..): the same flow and rules; the time is the
  car's best lap +0x16C4 (`fn_001EC4C0` from the lap's `0x1EC620`), "no time" after a FAIL
  (the run's one lap is never completed) or an EXIT before the lap. Best = entry +0x11F0, the
  visit's best lap.
- **Machine tests** (driver 0x131B20.. / 0x131CA0..): the same flow and rules.
  `DynamicsConductorMachineTest::vf19` (the goal) checks the record (`fn_001EFDE8`) and posts
  the time to the goal as a lap (`0x1EC620`) for the 0-400 m and 0-1000 m tests only (the
  test index `fn_001F0678` 1 / 2); the top-speed test posts its speed (`fn_001EC8C0`), so its
  car's +0x16C4 stays "no time": it never sets the record or R+0x48, and never gets a ghost
  of its own. Best (0-400 m / 0-1000 m) = entry +0x11F0, the visit's best time to the goal.
- **Free runs** (`DynamicsConductorFreeRun::vf18`, the player's line crossing, not a replay):
  the lap's recorder (race vtable +0xD8 with 0x100) is checked (`fn_00210690` and its frame
  count under 36,000); the lap time goes into its header (+0x10) and, when strictly under the
  best-lap store's (rec +0x124; "no time" at first, kept for the visit), the lap is copied into
  it (`fn_00210700`) and R+0x4C = 1; then, when the ghost recorder is empty (+0x140 = 1, its
  state from the race's construction 0x186F80) or the lap is strictly under the ghost's lap
  (its header +0x10), R+0x48 = 1 and `fn_0017AAD8(R, 1, 1)`: live (play mode 0) with the
  Ghost option on, the lap store becomes the ghost at once (`fn_001F4E38(1, 0)`) and the ghost
  car starts. At every crossing (vf17 the first, vf18 each lap) with two cars and no logger
  the ghost car is put back on its lap's start and its playback restarted
  (`fn_001F1400`: `fn_001EE6D8` with the recording's header, `fn_001F52B0`, the sub-tick
  fraction aligned), so the ghost runs in step with every lap. After the run the free-run
  driver (0x1306AC..): R+0x48 with the option off -> Q+0x14 = 1, Q+0x10 = 0, start page
  `ghost_dlg`; on: nothing (already replaced); YES -> `fn_0017AAD8(R, 1, 0)` when the menu
  returns. R+0x4C -> `fn_0017AD28`, SAVE GHOST / DATA LOGGER. The free practice before a GT
  race is a `RaceSinglePlayer` (not `RaceSolitaire`): no ghost and no Select switch.
- **LOAD GHOST** (action 10, `fn_0012EF40(0x13)`) in every driver installs a memory-card ghost
  file through the race's vf20 at once; none is loaded when a visit starts.
- Ours: `race::ghost_recording_kept`, `race::replaces_ghost`, `race::takes_best_lap`,
  `RaceScreen::whole_run_ghost` / `lap_ghost` / `ghost_run_time` / `solo_run_ghost` /
  `take_best_run_ghost`, the whole-run ghost as a car of the exact race (the best run's race
  record, `ExactFieldOptions::ghost`), the free run's own recorders (`SoloRecorders`), Best from the
  session best `best_splits_` (the player's laps alone in these runs); LOAD / SAVE GHOST use
  replay files (the newest ten of the course and car: a free run's fastest lap, else the
  fastest run of the same kind and start; the best run's replay). Not reproduced: the ghost's
  translucency mode 2 (ours alpha 0.4), +0x136D's meaning (taken as 0), the recorders'
  overflow checks (`fn_00210690`, +0xCC+0x28), the ghost's model loading a moment after a
  live replacement.

### A8b. The machine-test and time-trial drivers around a run [C]
The machine-test driver (`fn_00131690`, called with no day charge: the day is the
`go_machine_test` action's, `fn_00126308`: GameStatus +0x8, the day, += 1, once per visit) and the
arcade time-trial driver (`fn_00130A50`) loop like the licence driver: before every quick
menu they read entries 0..2 of the ranking into the menu's Top 3 (machine test: time
`fn_00212498` / speed `fn_00212528` and name `fn_00212598`; time trial `fn_00213BF8`,
`fn_00213C50`). After every run (`vf42` returns) with the car finished (+0x1464):
- machine test, `fn_00131410(test, time = car +0x16C4, ..., top speed +0x16AC)` (the
  driver's own numbering, = the table index: M400 0, M1000 1, MMAX 2): the time tests' time
  in ms if <= 0x3FFFFF, the top speed's round(speed x 1000) if in 1..0x3FFFFF; inserted (`fn_00212468` / `fn_002124F8`: the first
  place 0..11 empty or not better, so a tie goes ahead; the speed stored as int(v x 1e5));
  a rank >= 0 runs the name entry at once (`0x128540`: page `name_entry`, starting text
  GameStatus +0x78, 15 characters, then strcpy to +0x78), stores the name `fn_00212558`, the
  car code `fn_002125C8` (CarEquipments +0x10) and a 32-byte block `fn_00212638` (built by
  `fn_0012F3F0` from the run: 30 characters, `fn_0021B5E8`; I: the records page's entry
  code);
- time trial (`0x1310FC`): the car's best lap +0x16C4 (not "no time", <= 0x3FFFFF) into the
  challenge's table (`fn_00213BD0`), a rank >= 0 the same name entry, `fn_00213C20` name,
  `fn_00213C78` block.
- free run (the arcade time attack and GT Mode's free run, `fn_0012FF48`, `0x130764`): the
  car's best lap into the course's time-trial table, however the run ended (gt_mode.md §17
  "Course time trials").
After the record (or none) every one of these drivers plays the run's replay
(`vf42(race, 1, 1)`, as the quick menu's Replay item) before the quick menu comes back.
Nothing is recorded anywhere else: no menu reads a run's result into a ranking after the
driver returns. Ours (`RaceScreen::record_run`): the same at the race's exit transition, the
name entry a presentation over the race before the quick menu, the Top 3 read again when it
closes; the menus never record from the posted `GameResult` (its `records_done` only makes
the arcade menus save). A time-trial EXIT before the lap records nothing. The automatic
replay after each run: §A8c.

### A8c. After every run: prize, name entry, replay, quick menu [C unless marked]
Every driver runs the live run with `RacePS2Base::vf42(race, 0, 1)` (0x17D3D8; the GT race
and championship drivers through vtable +0x150) and the replay with `(race, 1, 1)`. So does the
Replay Theater (`fn_00100838`, from the top-level loop `fn_00100140`): Load Replay (file kind
0x11) and DEMONSTRATION (kind 0xC, the `demo_loader` page) build the race from the file's
`RaceReplayInformation` (`fn_0018D380`) and run `vf42(race, 1, 1)` once: play mode 1, the
REPLAY item's, with the same camera set-up (§G5). (`fn_00136FC0` is the Analyzer's.) vf42 sets
the race up (`vf9`), makes it the loading object (the replay cuts in), loops until the race
ends, starts the race-exit transition (`fn_00102228(1, 1)`, §C1) over the last frame, waits 2
vsyncs and cleans up; it repeats only on a retry (exit reason 4), which the replay pause does
not have, so a replay plays once. In replay mode each class's vf9 installs `ReplayPause`
(`fn_001A3248`) and no result object (`fn_00181538(race, 0)`): no RESULTS panel, record
overlay or medal. A replay ends when its recording runs out (`RaceSinglePlayer::vf29`
0x181FDC, `RaceSolitaire::vf29` 0x187A50 at 0x187CE4, `RaceSplitBattle::vf29`: event 1).
Each car's recording is closed at the race's shutdown (`fn_0017C970` -> `fn_0017AA48`), so it
holds the whole run to the frame its loop ended (the auto-drive under the result screens
included), or to a pause EXIT that counted as the finish (§E). Only Start (`RaceBasic::vf29`
0x18125C, pad bit 0x10) opens the replay pause; no replay-mode handler reads Cross / Circle.
After `vf42(race, 0, 1)` returns, per driver:

| Driver | After the live run | REPLAY item |
|---|---|---|
| Time trial `fn_00130A50` | ghost flags; the best lap into the challenge table, name entry if it ranks (0x1310FC); **replay** 0x1311DC (also with no rank, 0x1311D4) | action 2 0x1311D4 |
| Free run `0x12FF48` | ghost flags; the best lap into the course table, name entry if it ranks (0x130764); **replay** 0x13080C | action 2 0x130804 |
| Machine test `fn_00131690` | a run that ended (+0x1464): records and name entry `fn_00131410`; **replay** 0x131D84 | action 2 0x131D78 |
| Licence `fn_00132030` | a run that did not end: `demo/%s` loaded over its data (0x132640); else medal, name entry (§A8); no replay | action 2 0x132844 |
| Arcade race `0x12F840` | finished: place, a win's trophy `fn_00137DC0` (0x12FCF0), result list, record `fn_00216780`; else list reset; **replay** 0x12FD78 | action 2 0x12FDA8 |
| GT race `fn_00132BA0` | finished: prize 0x133024, result list; else list reset 0x13306C; **replay** 0x1330B0 | action 2 0x133204 |
| Championship `fn_00133388` | finished: prize 0x133898, points `fn_0018E838`, result list; else `fn_0018EAB8`; **replay** 0x133908 | action 2 0x133A24 |
| 2P `fn_00133F48` | result list; **replay** 0x134414, always | action 2 0x134278 |

The GT race's free practice (0x1330E0: the race object destroyed, a `RaceFreePractice` run,
the race rebuilt, `fn_00182260`) has no replay.

**The quick menu's backdrop.** Before every quick menu the time-trial, free-run, machine-test,
GT race and championship drivers set the records-off flag (org +0x6C = 1, e.g. 0x130EA8 /
0x132E24) and restart the race in play mode 2 (`vf9(race, 2)`, `RaceTimeTrial::vf9` at
0x130EAC), and pass it to the runner `fn_0012E2C8` (a2), which hands it to `ShortmenuRender`
(+0x58, `fn_0012CAB0`). `ShortmenuRender::vf2` (0x12C9F8) steps it every menu frame
(`RaceBase::vf2`) while +0x5C is set (1 from the constructor 0x12C978; `fn_0012CAC0` /
`fn_0012CAC8`, referenced only from data, clear / set it), `vf3` (0x12CA50) draws it over the
whole frame (`vf45`). Mode 2 has no end: `RaceSolitaire::vf29` posts event 1 for mode 1 only
(0x187C40, the `slti 2` branch skips mode 2). **The car is the computer's** [C, 2026-11-02]:
play mode 2 is the race-flow byte (conductor + 0x8000 + 0x37AC, `RaceBase::vf10` to the
conductor's virtual 23), under which the solo conductors' virtual 24 (0x1F3218) give the
player's entry control source 1 (F+0x3E9: the AI drives, the pad only decoded), so the
recording is never driven, and the races' virtual 24 (0x1F3188) every car control source 2;
the time trial's first crossing hands only a pad car back (`DynamicsConductorTimeTrial::vf17`
0x1F1CC0, control 0). With records off its lap handler returns before it ends the run
(0x1F1D40, R+0x6C), and the conductor's set-up from `RaceBase::vf9` (`fn_001F35B8`) clears the
race configuration's laps (+1) and time limit (+0x38) when R+0x7C or R+0x6C is set, so the
base lap handler (`DynamicsConductor::vf18` 0x1EFB70: the leader finished, the lap total
virtual 30 = config +1, or the time limit) never ends a race either; with records on the attract
demo would end 3 s after car 0's last lap (0x1F2AE8) and return to the title, but its time limit
comes first (gt_mode §15). The start sequence (`fn_0017A5E8`) has no
play-mode test: a standing start's hold counts down and gives the signal as live. So the
computer drives the car from the start, lap after lap, for as long as the menu stays, before any
run, after a run and after a DEMONSTRATION alike, and a GT race's field races on after its
countdown. **Its
camera** [C]: records off make `RaceBase::vf9` set the SE mute (0x17BC58: `fn_0017BBB8(race,
1)`, race +0x1788), and the race set-up stores the camera view's input (CM + 0x2780, cleared by
the replay set-up `fn_00208320` at 0x208618) only without it (0x17BF38), so `fn_00208BE8` reads
the play mode as 0 and runs the live handler `fn_0020B788`: start.cam over a standing hold,
then the driver's camera, the view the player last drove with (§G5). The view-mode pick of §G13
needs records on (0x2085D4) and is the attract demo's. The licence box (§A7) is restarted inside
the same runner, under the same flag, so it is seen the same way. Seen in PCSX2 (US 1.10, Time
Trial No. 1 at Midfield, 2026-11-02): the car lapping on and on from the in-car view before any
run (4 min), after a run left with pause EXIT (the run's own replay first, then the menu's car
from its start) and after DEMONSTRATION; and the Sunday Cup's race 1 RACE MENU (3 laps of Super
Speedway): start.cam's shots of the grid over the 10 s countdown, then the field racing from the
player's in-car view for 7 minutes (no end, no restart). Also seen there (2026-11-02; the
player's view was the bumper view in each): the arcade Free Run menu at Super Speedway (the car
lapping on for 3.5 minutes; after a run left with pause EXIT its replay, then the menu's car from
its start); the 0-400 m machine test menu (start.cam on the line, then on past 400 m and round
the Test Course for 3 minutes); licence B-2's box (start.cam on the line, the drive to the goal,
then from the start again); and the Vitz Race championship's round 1 menu (start.cam over the
countdown, then the field racing for 5 minutes, past its 2 laps). QUALIFY opens no menu of its
own (the practice starts at once); left with pause EXIT it shows no replay, and the round's menu
race starts again from its grid. Which view the menus show is the player's (§G5), so it differs
from menu to menu. The arcade
race, 2P, GT race result and championship result menus get no race (a2 = 0; the GT driver
clears it at 0x1330DC, the championship at 0x133954); their layouts' opaque `bg` (the GT-logo
checker, colour 7E7E7EFF) fills the frame.
The menu's race is silent, the licence menu's preview box too: the drivers turn records off
(R+0x6C) around their quick-menu runner, and `RaceBase::vf9` makes that the race's SE mute, under
which no car or course sound is sent (changelog 2026-10-30).
Ours: `race::post_run` / `menu_backdrop` / `replay_over`, `RaceScreen::after_run`,
`start_auto_replay`, `return_to_menu`, `start_menu_backdrop` (`screen_race_post_run.cpp`). Not
reproduced: the arcade record per run (ours
records the result posted on leaving); the backdrop restarting after the records page and the
panels (ours keeps running). Ours: `start_menu_backdrop` builds the race again live with the
race-flow byte 2 and records off (`ExactFieldOptions::config` / `records_off`; `ExactRace` clears
the laps and time limit), keeps the run's recording, replay and result for REPLAY and SAVE
REPLAY (`end_backdrop_race` puts the run's race back), and gives it the driver's camera
(`RaceScreen::live_camera`); test `exact_game_menu_race`.
**DEMONSTRATION** in the time-trial driver (0x13117C) loads `demo/%s` into the race (0x131198),
plays it once as a replay (`vf42(race, 1, 1)`, 0x1311A8: to the step its recording runs out, see
the changelog 2026-11-02), and clears REPLAY (0x1311C0); the next quick menu's backdrop is the
computer's car as above (the demonstration's inputs drive nothing in mode 2). The driver's loop
then runs its quick menu again, which opens on its first item, START (seen in PCSX2). Ours:
`return_to_menu` opens the menu again.

---------------------------------------------------------------------------------------------

## B. Race finish and time trial

### B0. The start signal of rolling and flying starts [C, 2026-10-21]
- The start sequence `fn_0017A5E8` (from `RaceBase::vf30`, jump table 0x327640 on start type
  + 1) holds standing starts (type 0: the event's seconds; 1: 4 s, BGM 0x1B; 2: 10 s, BGM 0x12;
  each with event 2) and posts **event 4** (the start signal) when the hold counter (race
  +0x5C, frames) reaches 0. Type 3 (rolling and flying) sets no hold and posts nothing; type
  -1 posts BGM 0x14 at once.
- Event 4 -> `RaceBase::vf28` -> `vf33` (0x17C918): event 3 and BGM request 0x14 (the race
  music, music.md §2; `RaceLicenseBGM` turns it into a stop). Event 3 is read by
  `fn_00192880` every frame from the race's vf29 (`RaceSolitaire::vf29` 0x187AF0,
  `RaceSinglePlayer::vf29`, `RaceLinkBattle::vf29`, the split battle's `fn_00183CA0`): it
  plays SE 0x15 = `count_s` (`fn_001A7750`, effect table 0x2E9380: 0x14 `count_d`, 0x15
  `count_s`; the race-SE switch 0x3521E8 = race +0x1788 gates it) and sets 0x351FB4, on which
  `RaceDisplay::vf3` (0x192B44) calls display vf19: `RaceDisplay::vf19` (0x1947E8) shows
  "@start" (D+0xA2EC, colour 0x80D0D0D0, 1.0 s) unless the display's play mode D+0xC is set
  (replays). The countdown's beeps are the same routine: while event 2's flag is up, `count_d`
  at every whole second of the last 240 frames of the hold.
- Who posts event 4 apart from the hold: the conductors at the start-line crossing (vf17:
  lap counter 0 -> 1, `fn_001EE978`) and the rolling start's hand-over:
  - `DynamicsConductorTimeTrial::vf17` (0x1F1CC0): a car with control byte F+0x3E9 = 0 (the
    player's pad): `fn_001E3BF0` (the hand-over: car +0xF48 = 0, the driver object's mode 7
    via `fn_001F8980`, driver kind F+0x534 cleared) and event 4 (arg the car); then the lap start F+0x530 = the crossing. Every time
    trial (all are flying).
  - `DynamicsConductorLicense::vf17` (0x1F0E40): when the rule block's initial speed byte
    (K+0xE340, the event's +0x198) is non-zero (a flying test): lap start, hand-over, and for
    car 0 event 4. Standing tests: nothing (their GO is the hold's).
  - `DynamicsConductorFreeRun::vf17` (0x1F1540): car 0 with driver kind F+0x534 != 0 (the
    computer's run-up): hand-over, +0x16CE = 0, event 4. ATAF0001 (the arcade time attack and
    GT Mode's free run) is type 3 at 80 km/h, 0 m before the line: the signal comes on the
    first crossing.
  - `DynamicsConductorSinglePlayer::vf13` (0x1F2860): type 3 races: car 0's counter +0x16CE
    (300 frames) runs out -> hand-over and event 4 (the rolling start).
  - `DynamicsConductorFreePractice::vf17` (0x1F2248) posts an Information event (kind 3)
    instead; machine tests (type 0, vf17 empty) start from their hold.
- So a flying start's crossing of the line is its GO: `count_s`, "@start" for 1 s, the race
  music from there (silence during the run-up: type 3 posts no jingle), and nothing at
  later crossings. `RaceFreeRunDisplay::vf19` (0x1899D0) adds two notes in the event display
  (green 0x802BCC49, the display's font, sw_16 from its constructor 0x197F08): the table
  0x2C43E8 +0x40 (`RACEDISPLAY_FreeRunReady`, US "Timer starts at start/finish line") for
  3.0 s with delay 2.0 (in parallel, 2 s after the group start), then +0x44
  (`RACEDISPLAY_FreeRunHelp`, "Press START button to end session") for 3.0 s queued.
- Replays: "@start" is not shown (D+0xC). The SE mute 0x3521E8 (race +0x1788,
  `fn_0017BBB8`) is set by `RaceBase::vf9` (0x17BC34..0x17BC58) only while the data logger runs
  (R+0x70) or records are off (R+0x6C: the quick menus' backdrop, the demonstration), so a
  replay of the REPLAY item (records on) would play `count_d` / `count_s` if its conductor
  re-posts the events [I: the replay's conductor path not traced]. Ours has no start sounds in
  replays (unchanged).
- Ours: `RaceScreen::flying_start_signal` (live runs; no start sounds in replays, as the
  countdown's), `hud_data` (START), `music_cues` (`holding` through the run-up and the
  rolling start's 300 ticks), `EventDisplay::after`.

### B1. Finish detection and stop driver [C]
- `DynamicsConductor::vf18` (0x1EFB70, any lap finish): builds the time-event objects, and if the car
  is the leader/player or the configured time condition holds: posts **event 5** (arg = car, prio 2),
  calls `fn_001EACB8(car, 3)` (stop driver mode 3), `fn_001F2AE8(…, 3)` for car 0, and
  `fn_001DD5B8`. TimeTrial: `DynamicsConductorTimeTrial::vf18` (0x1F1D40) does the same (mode 3,
  event 5 with prio 2, `fn_001EFDE8` record test, time events).
- `RaceDisplay::vf17` (0x193DA8; the display's finish handler) for a car that is the player: sets
  `D[0x2471]` (once), posts BGM (0x14, **0x13**) = "jg12" finish jingle and event 8, and if
  `D[0x2478]` calls `vf20` -> `fn_00197E90(msg, "@finish", 4.0, 0)`: the finish image/message for
  **4.0 s**. 2P display modes (7, 8): text "PLAYER %d WIN!" (`[0x2C4430]`, font `sw_20*150,150`,
  colour 0x80D0D0D0 base) only for the winner (index 1st) and the same BGM 0x13.
  `RaceMessageDisplay` (0x197AA0..): elapsed `+0x18` += 1/60 until duration `+0x1C`; alpha is a
  smooth ramp: in = elapsed / `+0x20`, out = (duration - elapsed)/`+0x24`, squared, clamped 0..1;
  it blinks off every other 12 frames when `+0x2C` > 0.

### Race order and finish [C]
- **Per tick** the conductor handles the cars in the current race order, the leader first; each
  car's progress update runs the line-crossing handler. The lap counter is 0 behind the line, 1
  past it, +1 per forward crossing; on a crossing with an old value != 0, `vf18` gets the old
  value (= laps now completed) and **finishes the car** if any of: the event's laps != 0 and equal
  to the laps completed; the car in **rank slot 0** (as of the last sort) has finished (checked
  live, so a car crossing on the tick the leader finishes finishes too); a time limit (minutes)
  != 0 and this crossing's time >= limit x 60 s. Nothing else finishes a car: the time limit ends
  the race for nobody by itself (each car finishes at its first crossing after it), there is no
  retirement / DNF rule, and a lapped car (the player included) finishes at its next crossing
  after the leader. 2P battle: the same laps and leader rules, no time-limit check.
- After the finish the car is skipped by the crossing handler: its lap counter, lap start and best
  lap are frozen.
- **Ranking** (insertion sort after the clock tick each frame; ties keep the previous order; the
  position byte = rank + 1, which the HUD shows and the player's place is read from at the end of
  the race loop): (1) lap counter minus reverse crossings, more ahead; (2) equal and neither
  finished: course progress (distance along the reference line), more ahead; (3) equal and one
  finished: the **unfinished** car ahead (a finished car's distance stopped at the line); (4) both
  finished: the earlier finish time ahead.
- Times: the race clock is reset to 0 at init and adds 50 per tick (1/3000 s) once the start hold
  counter is 0 (on the GO tick the physics and the crossings still see 0, then 0 -> 50; a rolling
  start has no hold, so its lap 1 includes the run-up); a crossing sees the clock before the
  tick's advance plus trunc(f x 50) (f a float, truncated), f = the tick fraction at the line on
  course progress (prev / cur of this tick's update; at the control line (len - prev) / ((len -
  prev) + cur)), ms = floor(units / 3). Lap time = crossing time - the lap start; best = the
  minimum of every lap.
- **Lap 1** in races (single player and 2P conductors): the lap start is set to 0 at setup and the
  start-line crossing does not touch it, so lap 1 is timed from GO for every car (a car placed
  past the line has no start crossing and is timed from GO too). Time trial, free run, free
  practice and flying licence tests: the start crossing sets the lap start to the crossing time
  (lap 1 from the line); standing licence tests keep GO. All modes time crossings alike.
- HUD current lap (conductor vf29): races floor(clock / 3) - lap start, so it runs from GO before
  the first crossing and runs on after the finish (the lap start stops at the finish crossing);
  time trial, licences, free run, free practice: 0 while the lap counter is 0. Total time =
  the completed laps' sum + the current lap.
- Reimplementation: `race::ranks_ahead`, `race::finishes_at_line` (`race/field.h`),
  `track::LapTimer` (the clock, `set_lap1_from_go`, `freeze`).

### B2. Results panel (ResultArcade and derivatives) [C]
`ResultArcade::vf2` (0x174D50) -> `fn_001B1DF0(bars, n, 0, 5.0)`; per-frame `t += 1/60`
(`fn_001B1F70`, f32 0x34FFF0 = 1/60 per call of `ResultArcade::vf4`):
- title bar starts at **t = 5.0 s after the finish** (the clock starts when the finish is
  registered), rank bars at **6.0 + 0.12 i** (+1.0 then +0.12 each, f32 0x34FFEC = 0.12), the
  "OFFICIAL TIMER" bar after the last rank bar; panel end time = last bar + 1.0 (`+0x910`).
  Matches `RACE.md`.
- exit: `ResultArcade::vf9` (0x174EF8): once the panel is fully shown (`fn_001B23C0`: end time passed
  is false -> shown) and either the confirm bit (0x40 of the pad mask) is down or a countdown
  `+0x944` (starts at 15.0 and runs down by 1/60 per frame once `+0x940` is set = every car has
  finished) goes below 0: **post event 1**.
- **Championship rounds** (`ResultChampionship`, vtable 0x326198): `vf2` (0x1753C0) runs
  `ResultArcade::vf2`, then page state `+0x9A4` = 0 and the countdown 10.0. `vf4` (0x1753F8), once
  the window is fully shown (`fn_001B23C0`), switches on the state (jump table 0x326180):
  0 / 2 (RACE RESULTS / POINTS): the countdown runs down by 1/60 while `+0x940` (every car
  finished); confirm or below 0 -> state + 1 and `fn_001B20D8(bars, 0.5)`: every bar gets the fade
  descriptor 0x331C80 (type 0x19, rate -2.0: out over 0.5 s; the opening ones 0x331D28 / 0x331DC8
  are +2.0 and 0x331E88 +2.5) and the window's end time = now + 0.5. 1: state 2, countdown 10.0,
  `fn_001B1DF0(bars, n, 1, 0.0)` (page 1 = title `b_title_point`, `fn_001B12F0` binds the three
  titles), clock restarted (`fn_001B1F60`), rows in race order (`fn_001756F0`) with the points of
  their place (`fn_00238A80`, `fn_001B22B0`), the player's row marked (`fn_00174C50`). 3: state 4,
  `+0x940` = 1, countdown 15.0, page 2 (`b_title_total_point`), `fn_00175790` builds the totals: per
  row (race order) the entrant's points before the round (entry +0x20FC) + its place's, inserted
  after every node with at least as many (highest first, a tie in race order). 4: confirm or the
  countdown (`fn_001756C0`) below 0 -> event 1 (end the race). Points text `fn_001B1830`:
  `"%c%d"` with '+' (page 1) or ' ' (page 2), then "pt" (under 2) or "pts" (0x2C5D10); PAL the same
  (0x1B2738, its "pt"/"pts"); NTSC-J `"%c%d pt"`. Ours: `race::ResultPanelFlow`,
  `race::total_points_order`, `race::result_points_text`, `RaceScreen::result_panel_rows`.
- Between finish and results the world is live: the car is driven by the stop driver (mode 3), the
  camera is the player camera, the HUD is shown (the finish message is on top for 4 s).
- Draw order (`RaceBasic::vf46` 0x181400, the race's 2-D pass; `RaceSolitaire::vf46` calls it
  outside the logger): the race display (`race+0x8000+0x4888` = the `RaceDisplay` at race
  +0xC8C0, set by the `RaceBasicWithRaceDisplay` ctor) vf4 (0x1934B8: every HUD part, lap
  count D+0x60 and time lines included), then the result object (+0x4884) vf5 (the panel,
  `fn_001B2028`), then the pause menu (+0x4880) vf4 while +0x1790 is set. So the panel is over
  the whole HUD. The reporter's capture in public issue 261 (16:9 patch) agrees: "Best" /
  "Last" dimmed under the panel's edge. (Corrected: the time block had been put over it.)
- `RaceBasic::vf29` (0x1811B8): when the pad mask bit 0x10 is pressed (and `race+0x1788 == 0`):
  in play mode 2 -> event 1; otherwise `fn_001814D8(race, 1)` sets `race+0x8000+0x1790 = 1` (if the
  pause-menu object `+0x4880` exists) and `vf38` pauses audio. From then `RaceBase::vf2` calls
  `RaceBasic::vf31` (0x1812D8) instead of `vf29`: the **post-race menu stage** (menu object at
  `+0x4880` returns an action 2..5: 2/3/4 -> `RaceBase::vf35(n)` stores the exit reason in
  `+0x1794` and ends the race (event-1 path), 5 -> back: `fn_001814D8(0)`, `vf37` resume audio).
  `RacePS2Base::vf42` repeats the whole race (`vf9` again) while the reason is 4 (restart).
- Play mode `race+0x8000+0x1784` (set by `RaceBase::vf9(mode)`): 0 live race, 1 and 2 are replay
  modes (BGM volume: live x0.5, replay x0.7 via f32 0x34FCE4). A replay is the same race object
  re-simulated from the recorded input; the camera is whatever the replay menu selects.

### B3. Time trial [C]
- One lap: `DynamicsConductorTimeTrial::vf18` (0x1F1D40) runs at every completed lap and,
  unless R+0x6C, ends the car there: stop driver mode 3 (`fn_001EACB8`, which also marks the
  car ended, F+0x4E0), `fn_001F2AE8(…, 3)` for car 0, event 5 (prio 2); it never reads the
  event's laps byte (+0x199, 2 for every AT event). The lap total `vf30` (0x1F1B78) is the
  constant 1, and the HUD's lap number (`fn_001EAB08`) is the lap counter (at least 1) minus 1
  once the car has ended, so "Lap 1/1" for the whole run. An ended car counts no more laps:
  the crossing handler skips it (0x1EEDF8). The seeded records are one-lap times (AT01
  1'25.553 on the first challenge's course, gt_mode.md §16). Ours: `race::kTimeTrialLaps`
  (the request's event laps ignored), `race::solo_run_finished` (tests
  `race_time_trial_finish`, `race_time_trial_one_lap`), `lap_freeze` at the finish.
- Finish: as B1. The session record check `fn_001EFDE8(conductor, car, time)` (R = the
  organisation, *conductor; callers: the licence pass `fn_001EF5C0`,
  `DynamicsConductorTimeTrial::vf18` (every lap: the time trial's car ends at its first line
  crossing, so its one lap), `DynamicsConductorMachineTest::vf19` (the goal of the time tests:
  type `fn_001F0678` 1 or 2; type 3, the top speed, never)) returns 0 for a car other than the
  player's (a1 = the car's index byte F+0x3E8, 0 = the player), when `R+0x6C` is set (only the
  drivers write it: they bracket the runs behind their quick menus, the demonstration among
  them, with R+0x6C = 1 / 0, time trial 0x130EA8 / 0x130F34; a FAIL does not set it, and a
  failed run never gets here because its car has ended), for a replay / demonstration (the
  conductor's byte +0x8000+0x37AC),
  while `R+0x70` is set (the data logger, `fn_00136CF0`, sets it through `fn_0017A1C0` while it
  runs), for the "no time" value 0x157529FF, or when the time (ms, unsigned) is above
  `R+0x8BC`, the session's best lap before this one (§I: "no time" at first, never a stored
  record). All three callers check before 0x1EC620 adds the time to the session (TT vf18:
  `fn_001EFDE8` at 0x1F1DEC, then the gap 0x1EC818, then 0x1EC620; MT vf19 and the licence pass
  alike), so a time equal to the best so far counts. Otherwise it sets `R+0x4C = 1` and, when
  `R+0x40 != 0`, posts **event 0x13** (§E).
- `R+0x40..+0x4F` {result, time, +0x48, record}: `fn_0017B030` sets {-2, "no time", 0, 0} at
  every run start (`RaceBase::vf9` -> `fn_0017A578`) and at the time trial / free run / free
  practice conductors' restart (`DynamicsConductor::vf22`); `DynamicsConductorLicense::vf23`
  {-2, no time}; the licence fail `fn_001EF520` {-1, no time} (no check); the licence pass
  `fn_001EF5C0` {k + 1 for the first target (gold, silver, bronze) the time is not slower
  than, or 0 (slower than all: event 9), the time} before the check. Nothing else writes
  +0x40, so the time trial and the machine time tests check with -2: a session record there
  always posts 0x13; a licence test only with a medal.
- `R+0x4C` after the run (the time trial, machine test and licence drivers copy
  R+0x40..0x4F after `RacePS2Base::vf42`): when set, `fn_0017AD28(R)`: the race's vf24
  (`RaceSolitaire::vf24` 0x187430: the run's recording copied into the race's +0xCB50 store
  [I: the session's best run]) and the player's entry +0x11F4 = the car's time +0x16C4
  (`fn_00177400`), and two flags of the quick menu's argument block (+0x18, +0x1C) set to 1
  [their use not traced]. Not reproduced.
- Fail (event 0xA, out of course / voided) - `RaceTimeTrialDisplay::vf3` (0x188C10) stages in
  `D[0xA4A0]`, frame counter `D[0xA49C]`: S0 polls event 0xA (-> S = 1); S1: sub = 0, post BGM
  0x20 "fail", call `vf23`; S2: `vf23` each frame; S3, S4: S = 0 and **event 1** (`vf24`).
  `vf23` (0x188978) sub-stages (timer `D[0xA498]`): sub 0: timer = 60 -> sub 1: at timer <= 0, timer = 1800
  and start the message `fn_001884D8(M, id = [0x2C4408] = "失格です" Disqualified)`; sub 2: update `M`
  until done or timer; then M inactive, timer = **240** (4 s), sub 3: wait 240 frames or confirm
  bit 0x40 (input object `D[0x24A4]`), S = 3. `TimeTrialExaminationMessages::vf5` draws the same
  bluish wash + split text as A6 (wash colour (160,160,255), text colour 0x40C0A0).
  The failure itself (`fn_001F1BB8`, from vf13): nothing while R+0x6C is set or the car has
  ended (F+0x4E0); else event 0xA (player car only, prio 0) and the stop driver, mode 2
  (`fn_001EACB8`: the car ends, so no lap is completed after it). It writes nothing else:
  R+0x6C stays 0, the car's best lap +0x16C4 stays "no time" (the run is one lap, so a FAIL
  always comes before a completed lap) and the driver ranks nothing.
- **Record overlay** (`ResultImplementRecords`, vtable 0x326460; `ResultTimeTrial` 0x326970,
  `ResultMachineTest` 0x326680). Timing (vf2 0x1759C8, vf4 0x175A80): +0x10 = 5.0 and
  +0x14 = 10.0 when it starts (the finish event); +0x10 runs down by 1/60 a frame; once out,
  confirm (pad bit 0x40, Circle / Cross) or +0x14 < 0 posts event 1 (the race ends), +0x14
  running down by 1/60 a frame. Ours: `TimeTrialOverlay::ends_race`. The rank (+0x18) comes
  from vf9 when the result starts (vf7):
  - time trial: `fn_00213BA8` -> `fn_00213310(table, time)` on the challenge's 12-entry table
    (stride 0x2A4 from `fn_00218660() + 0x246F8`), the time = the car's +0x220 (the value the
    "Lap" line shows; the best lap [I]);
  - machine test (`+0x20` = top-speed test): times `fn_00212438` -> `fn_00211CD0(table, time)`,
    speed `fn_002124C8` -> `fn_00211DD8(table, speed)` (table at `fn_00218660() + 0x2E560`;
    speeds stored as u32, x 1e-5 (f32 0x350914) when read; the speed = car +0x16AC).
  - The rule (all three): the first place 0..11 that is empty (time 0x157529FF / speed 0) or
    whose entry is **not better** than the run (time >= run, speed <= run), so a tie ranks
    ahead of the stored entry; past the 12th: -1.
  - vf5 draws after `+0x10` (5.0 s, minus 1/60 per frame, f32 0x34FCA4) has run out and only
    for ranks 0..11: the rank string `fn_0019AA08(rank + 1)` ("1st".."12th") in font `rank`
    (glyph spacing 4) at (320, 150), amber 0x801880D0 with a black shadow; then the label in
    `sw_20` at (320, 200): vf12 = "NEW RECORD" for rank 0, else "RECORD TIME" (machine
    top-speed test: "RECORD SPEED"); its colour toggles every frame (`+0x1C ^= 1`) between
    (0.8, 0.4, 0) floats = (204, 102, 0) and 0x80D0D0D0.
  - vf10: `ResultImplementRecords` "Lap" (sw_16#1) at (200, 270) and the value (vf11 = car
    +0x220, m'ss.mmm) in `sw_20#1*130,130` at (440, 270); `ResultMachineTest` top-speed test:
    "Max Speed" at (160, 270) and the speed at (480, 270), formatted by `fn_0023BA08`:
    "%d.%1d%s" (whole units, tenths, both truncated; the value through `fn_00245A18`, x 3.6
    km/h or x 3.6 x 0.62139 mph, unit "km/h" / "mph" by the options `fn_002458A0`; 0 or
    less a fixed `\x7F\x7F\x7F.\x7F` + unit). The time tests use the base vf10 ("Lap" + time).
    The same formatter shows a stored top speed on GT Mode's records page
    (`gt2k_menu::RecordsMenuArgMaxSpeed::vf4`). Ours: `race::record_speed_text` for both.
    The machine-test quick menu's Top 3 has its own handler (`qm_rank_speed<n>`, 0x12CD60):
    the list entry's speed (info +0xD4 + 16 n) through `fn_00245A18`, "%4.1f %s" with the
    unit `fn_002458A0` (rounded: "218.8 mph"; an empty entry " 0.0 km/h"). Its neighbours:
    `qm_rank_time<n>` (0x12CE68) `fn_0023B930` of +0xC8 + 16 n (no time:
    `\x7F\x7F'\x7F\x7F.\x7F\x7F\x7F`), `qm_rank_name<n>` (0x12CF50) the name pointer +0xCC
    + 16 n ("Polyphony Digital" for a null pointer, never the case for a table entry);
    `qm_list_car_name<n>` (0x12D040) the car name (`fn_0023A050`, US: car +0xE4) of the
    car-list entry +0x38 + 0x18 n, "---" for car 0. Each draws in 0x800080FF when its entry's
    flag is set (Top 3 +8, car list +0x10): the time-trial, free-run and machine-test drivers
    fill the Top 3 from a cleared entry and never set it; the car-list flag marks the
    player's row of a race result list (`fn_0012F070`). The free time trial (`fn_0012FF48`,
    `qm_arc_tt_free`) fills the car list with the cars of its course ranking's entries 0..2.
    `fn_0014BE20` (once thought to be the Top 3's) is the memory-card file page's
    `b_pt_time` (a best-lap replay's time, or the speed for a top-speed run).
  - Ours: `race::record_rank`, `TimeTrialOverlay` (time trials, all three machine tests), the
    table = the GT-mode save's ranking ("at:<code>", "mt:<test>"); a run without one (debug
    runs, Quick race practice) ranks as in an empty table. A free run has no finish at the
    line; its own result (`ResultFreeRun`, the course table's rank) comes with the finish
    event its pause EXIT posts once a lap is complete (§E "EXIT as the finish"; implemented:
    ours had shown none).
- **Machine-test conductor** (`DynamicsConductorMachineTest`, vtable 0x33D050) [C, 2026-10-21]:
  - Test type `fn_001F0678` = the base `DynamicsConductor::vf30` (0x1EF8B8, race
    +0x8000+0x6341 = the event's laps byte +0x199) called non-virtually, values >= 4 read as
    0. Disc: M400 1, M1000 2, MMAX 3 (all on the test course layouts, 10342.3 m a lap, grid
    slot 0 0.06 m before the line). The class's own vf30 returns 0 (no laps).
  - Timing points (vf33 0x1F06C0 = count with the goal: type 1 -> 1, 2 -> 2, 3 and 0 -> 3,
    negative or other -> 1; vf34 0x1F0780 = checkpoint distance: idx < count-1 ->
    `[0x2D75F0 + 4 idx]`, else entry 3 = -1 (never crossed); vf31 0x1F0730 = the goal,
    entry count-1). Table 0x2D75F0 = {400.0, 1000.0, 6845.0, -1.0}. So M400: goal 400 m;
    M1000: checkpoint 400, goal 1000; MMAX: checkpoints 400 and 1000, goal 6845 (on lap 1).
  - The crossing handler `fn_001EE978` (per car, per tick; track distance prev f20 / cur
    F+0x3E4, course length): only in a tick without a line crossing (no wrap), with no
    pending reverse crossings (F+0x467), the lap counter (+0x434) != 0 and the car not
    ended (+0x4E0): the next checkpoint (+0x492, reset at every forward line crossing) when
    prev < d <= cur: time units = clock + trunc(f x 50), ms = units / 3, split = ms - lap
    start (+0x530); vf5 (MachineTest 0x1F0A80 returns 1) skips the gap recorder vf15; vf16;
    +0x492++. Then, when vf31 != 0 (`fn_001F2CB0`) and prev < goal <= cur: the same timing
    and **vf19** (lap counter, units, ms, ms - lap start). The machine test's vf28
    (0x1F06A0 -> `fn_001EE878`) is a different hook (positions for the race object), not
    the crossing test.
  - Line crossings: vf17 is the base's (empty, 0x1EFB68), so the lap start stays 0 and times
    run from GO; vf18 (0x1F0880) is empty: no LapTime, no TimeDiff, no lap-count finish.
  - vf16 (0x1F07E0): unless race +0x6C, posts CheckPoint + GeneralTime (`0x1EC520`).
  - vf19 (0x1F0888, the goal): car +0x16AC := max(+0x16AC, +0x16A8); returns 0 if race
    +0x6C; `fn_001EACB8(car, 3)` (driven on, slowed); the record check `fn_001EFDE8` for
    types 1-2 only (not for car flag +0x136E or a replay); types 1-2: `0x1EC620` (LapTime +
    GeneralTime of ms - lap start, also the car's best +0x16C4); types 3 and 0:
    `fn_001EC8C0` (Speed event of +0x16AC in display units, `fn_00245A18`: x 3.6 km/h, x 3.6
    x 0.62139 mph; stores it at the car result +0x2100 when allowed); car 0 posts event 5.
  - Max speed +0x16AC: `fn_001EA9B8` updates it every tick from |F+0x488| (+0x16A8) only
    while the car has not ended (+0x4E0), in every mode; so it stops at the finish.
  - HUD time (vf29 0x1F0640): ended -> +0x16C4 (the goal time of a time test; "no time"
    0x157529FF, shown as dashes, for the top-speed test); else `fn_001EF8C8` = floor(clock
    / 3) - lap start (from GO, also before the line). Only the time tests' HUD shows it
    (and the replay layout's "Lap" line); the live MMAX HUD has no time line.
- **Machine-test HUD** [C, 2026-10-21]: `RaceMachineTest::vf9` (0x18CD30) picks the race
  display mode: `RACE_MODE_MAX_SPEED_CHALLENGE` (9) for "MMAX", else `RACE_MODE_0_400` (4)
  (mode 5 `RACE_MODE_0_1000` has no rows and is unused). The layout builder `fn_00194888`
  first clears every display object's active flag (+0x12, `fn_00194BF8`: the event display
  D+0x3BC and the message display D+0xCB4 included), then activates the mode's rows
  (`racedetail.db`, decoded type / sub / position); `RaceDisplay::vf4` skips inactive ones.
  - Mode 4 (M400, M1000): meters; map (16, 16) 100x100; time lines "Best" (D+0x134, the car's
    best: set to the goal time at the goal by `0x1EC620`) at (615, 84) and "Time" (D+0x20C,
    vf29) at (615, 112); message display (WRONG WAY / AUTO DRIVE) (320, 305); event display
    (320, 130); title / "@finish" (320, 160) width 480. No lap count, no tyre gauge.
  - Mode 9 (MMAX): meters; map (16, 16); tyre gauge (20, 435) when wear is on; speed lines
    "Record" (D+0x32C) at (615, 84) and "Best" (D+0x374) at (615, 112); title / "@finish"
    (320, 160) width 400. No lap count, no time lines, no event display, no message display.
  - The speed lines (`RaceSpeedDisplay::vf5` 0x198EF0, labels from the line setup
    `fn_00194DF0`: D+0x32C "Record", D+0x374 "Best"): right-aligned at x, the unit
    (`fn_002458A0`) in sw_12, the value left of it in sw_16 (`fn_0019A968`: the value through
    `fn_00245A18`, clamped to [0, 999.9], "%.1f"; 0 or less, or FLT_MAX, "\x7F\x7F\x7F.\x7F"),
    the label in sw_12 at x - max(value + unit width, 90) - 10.
  - Record (D+0x32C+0x44) = RaceOrganization +0x8A4 (`RaceDisplay::vf3` 0x192DBC). The
    RaceOrganization constructor (0x17A088) zeroes it (`fn_0017B708`); nothing writes it
    afterwards: no other store or address at that offset (its out-of-line address getter
    0x17B250 has no callers), the machine test's setup hook
    (`RaceMachineTestInformation::vf12`) is empty, the machine-test driver `fn_00131690`
    puts the stored top speeds (`fn_00212528`) only into the quick menu's Top 3, the event
    queue embedded at +0x84 ends at +0x8A0, and no copy of the 0x8C8-byte object exists. So
    it always shows dashes. (+0x8A8 likewise: zeroed by the constructor, its address getter
    0x17B248 has no callers, no other access but `RaceDisplay::vf1` reading it as D+0xA464's
    start value below. The "Record Lap" line reads +0x8BC, §K3.)
  - Best (D+0x374+0x44) = `fn_0017B718(D+0xA464, car +0x16A8)` every frame (D+0xA464 starts
    at organisation +0x8A8 = 0, `RaceDisplay::vf1`; then the maximum, at most 999.9): the
    highest speed shown, past the goal too.
  - Ours: `race::MachineTest` (timing points and goal on the lap timer's unwrapped track
    distance, `LapTimer::crossing_ms`), `RaceScreen` (finish, records, HUD, pop-ups).

---------------------------------------------------------------------------------------------

## C. Race <-> menu transitions and fades

### C1. Loader transition objects [C, with a conflict to report]
`fn_00102228(type, flag)` (callers `RacePS2Base::vf42` with type 1, and `fn_001CB4B8` with the
menu RenderBase field `+0x40`, default 0, setter `fn_001CB908`) starts one of four global objects
(the loading thread draws it over the last picture every frame, no clearing). Decoded mapping:

| type | object (address) | effect |
|---|---|---|
| 0 | `FadeoutTransitionObject` 0x49BB40 | wash colour 0x0C0C0C0C (rgb 12, alpha 12/128), blend 0x42, every frame |
| 1 | `DelayFadeoutTransitionObject` 0x49BB28 | counter 10 frames (vf5), then a wash of float colour `[0x34FA08]` = 0.1 grey, alpha 0.5 every frame |
| 2 | `LineoutTransitionObject` 0x49BB10 | columns: even columns from the left, odd from the right, progress +16 px per frame |
| 3 | `ComboutTransitionObject` 0x49B700 | combed row fill |

All four: after a 180-frame counter (set by `TransitionObject::vf5` = 0xB4) a 16x16 "still loading"
square at (20,20) blinks every 32 frames.

**Conflict:** `docs/specs/menus.md` §6 numbers the types the other way round (0 Combout, 1 Lineout,
2 DelayFadeout, 3 Fadeout) and states "leaving a menu control = Combout; race -> menu = Lineout".
By the code above the default (type 0, menu leave) is the Fadeout and the race exit (type 1) is the
DelayFadeout. I decoded `fn_00102228` branch by branch (type 0: `beqz` -> s0 = 0x49BB40; type 1: `beq`
-> 0x49BB28; type 2 -> 0x49BB10; type 3 -> 0x49B700) and confirmed the object addresses against the
constructor `fn_00102328` (vtables 0x3089E8 / 0x3089B0 / 0x308978 / 0x308940). Please re-check the
menu side if exact transition look matters.

### C2. What happens at the ends [C]
- Race exit: `RacePS2Base::vf42`: race loop ends -> `fn_00102228(1, 1)` -> the transition keeps
  drawing over the last race frame while the menu thread builds the quick menu / next screen
  (a grey 0.1 wash, not black; ours: `ScreenTransition::delay_fadeout_cover` over the held frame);
  a replay's loop is the same vf42, so its end (or its pause's EXIT) runs the same transition
  over the replay's last frame before the quick menu cuts in, and the live run's transition is
  up while the prize page, name entry or replay set-up run (§A8c; ours: `begin_exit_transition`);
  `fn_00101978(non-zero)` (called when the race is ready) / `fn_00101978(0)` (menu done) toggle the
  semaphore the loading screen waits on. There is no fade-in on the way back into a race or a menu:
  the transition just stops drawing (menus.md: "entering a menu is a cut, the elements fade in").
- Menu -> race: type from the menu control (default 0). The race appears as a cut at "ready".
- Inside the race there is no black fade at all for the finish/licence flow; the only screen
  effects are the dim (A5), the wash (A6) and the loader transition at the end.
- BGM is not linked to any screen fade: the BGM layer has no fades (`docs/specs/music.md` §1); at
  race exit audio is simply paused (BGM -2 / engine sounds stop), the BGM is stopped by `vf8`, and
  `fn_0022BA18` stops everything at the end of the quick-menu loop. The jingles are separate
  stream starts: 0x13 finish, 0x1D/0x1E/0x1F medal (restart the stream), 0x20 fail.

---------------------------------------------------------------------------------------------

## E. Pause windows (RaceMenuSelectWindow) [C]

Built from `menu/<LANG>/pause1..3.pmb` (page `pause_race` / `pause_replay` / `pause_retry`,
no `.mbl` items): buttons are the hidden layer's `m_<name>` (orange 0xFF6600) with
`m_<name>_sel` (0xFFD1B3, z + 1) over the selected one (RaceMenuImageBlinkButton).
- Which window: `RacePause` (PauseMenu, pause1: CONTINUE id 3, EXIT id 1) for RaceArcade
  (single races, GT races, championships), free practice / free run, 2P split and i.LINK
  battles, the arcade demo; `RacePauseWithRetry` (PauseMenuWithRetry, pause3: CONTINUE 3,
  EXIT 1, RETRY 5 "TRY AGAIN") for RaceTimeTrial, RaceLicense (LicensePause) and
  RaceMachineTest; `ReplayPause` (ReplayPauseMenu, pause2: CONTINUE 3, EXIT 4, REPLAY
  OPTIONS 1) in replays.
- EXIT (id 1) switches to the question row: `exit_message` "Exit race?", OK (id 4) and
  CANCEL (id 1), **the cursor on CANCEL**; CANCEL or Triangle / Square there go back to the
  button row with the cursor on CONTINUE (`fn_001A37A0`). RacePauseWithRetry alike
  (`fn_001A38A8`: the two-button row, cursor +0x3C = 1; vf3 0x1A3B80). OK ends the race
  (event-1 path), also in a time trial (then the driver's post-run, §A8b). Ours:
  `PauseMenu` (test `race_pause_exit_question`).
- **EXIT as the finish** [C]: two classes' vf31 handle EXIT themselves.
  - `RaceFreeRun` (`fn_001897D8`): unless its flag (race +0x3A874) is set or the race is a
    replay (+0x8000+0x1784), when the HUD lap number (`fn_001EAB08`: the lap counter, at
    least 1) is 2 or more, i.e. a lap is complete: the flag, event 5 (the finish: the course
    table's record overlay, `ResultFreeRun`, 5 s then 10 s or confirm), the car's end
    (`fn_001EACB8(car, 4)`: kind 7 on the pit surface, else 1, ai.md §1), the recording closed
    (`fn_001F4C00`, `fn_002106D8`); the race goes on. Otherwise (or the second time) `vf35(2)`:
    the race ends.
  - `RaceMachineTest` (`fn_0018CFB0`): when race +0x21800 (the driver's test index, the
    constructor's argument: M400 0, M1000 1, any other code 2) is 2, its flag (+0x48A40) is
    clear, the race is live, the car's top speed +0x16AC (m/s) is over 1.0 and the car has not
    ended (+0x1464): the flag, event 5, the end (`fn_001EACB8(car, 3)`, as at the goal), the
    recording closed, and the result's delay +0x10 = 0.1 (`fn_00175F68`, f32 0x34FCB4): the
    "Max Speed" overlay 0.1 s after the press, then 10 s or confirm. Otherwise `vf35(2)`.
  Ours: `race::pause_exit_finishes`, `RaceScreen::pause_exit` / `exit_as_finish` (test
  `race_pause_exit_finish`).
- Input (`fn_001B0BA8`): confirm = pad bit 0x40 (Circle / Cross), cancel = 0x80 (Triangle /
  Square); Left / Up = previous, Right / Down = next with auto-repeat 30 / 3 frames; the
  cursor clamps (no wrap); moving right onto a disabled button is refused. SE (by se.inf
  index, `fn_001C4F80`): 3 `cursor` on a move, 7 `ok` on confirm, 1 `cancel` on cancel.
- Results (`RaceBasic::vf31`): 3 continue (unpause, resume audio); 5 retry (`vf35(4)`, the
  race restarts); 4 exit (`vf35(2)`, the race ends: results / quick menu); 2 = an idle
  timeout (0 = off in retail) exits too (`vf35(3)`); Triangle / Square in the button row =
  continue. Start does not close the window (no reader) [C].
- The selected button's light copy: alpha 1.0, minus 1/30 per frame, back to 1.0 below 0 (a
  30-frame sawtooth, reset on every move). A disabled button's normal image is drawn at
  alpha 0.5. RacePauseWithRetry disables RETRY when the run's record event (0x13) arrives.
- **RETRY lock** [C]: the window with RETRY is built only for RaceTimeTrial, RaceLicense and
  RaceMachineTest (`fn_001A3A08`, the RacePauseWithRetry constructor). Every run start
  re-enables RETRY (`RaceBasic::vf9` -> `RacePauseWithRetry::vf2` -> `fn_001A39D8(1)`: the
  enable word at window +0x25C). `RacePauseWithRetry::vf3` (0x1A3B80) polls and consumes
  event 0x13 (`fn_00179640(queue, 0x13, 0, 1)` at 0x1A3BD4) and on it calls
  `fn_001A39D8(0)`: RETRY drawn at alpha 0.5 and refused (the cursor cannot step onto it,
  confirm does nothing) until the next run. Event 0x13 is the session record check of §B3:
  the player's car, not a replay or demonstration, not a run behind a quick menu (R+0x6C),
  not during the data logger (a failed run never reaches it: its car has ended and completes
  no lap), a time at or under the session's best lap before it (the visit's, §I: none yet
  counts; the stored records take no part), and `R+0x40 != 0`: always in time trials and the
  0-400 m / 0-1000 m tests (-2), with a medal in licence tests (2 / 3 / 4 gold / silver / bronze,
  §A1; 0 = slower than every target); the top-speed test is never checked. So the visit's first finished time-trial
  lap, and every later one that ties or beats the session's best, locks RETRY.
  Ours: `race::sets_record` / `race::locks_retry` in `RaceScreen::rank_finish` against
  `best_splits_` (the session best; licence and machine-test times added after the check)
  set `retry_locked_`, `open_pause` applies it (`PauseMenu::set_retry_enabled`), `restart`
  clears it.
- Every window draws its pmb `defaultLayer`: `bg1` dims the whole picture (about 50 %) and
  `bg` darkens the band behind the buttons (reference captures of the licence B-2 and the
  replay pause; an earlier arcade capture without the dim was taken as the window opened).

## F. Race prizes after a race (`fn_00137DC0`) [C unless marked]

Called by the race drivers once the player finished (race flag +0x1464), with the place p
(the player's index in the finishing order), the event's trophy class t (+0x1D3) and the
prize money m = event +0x1D4[p] x 100 Cr (`0x238950`; x100 again in Japanese):
- arcade single race (t 1..4 = easy / normal / hard / professional): only for a win (p = 0):
  ArcadeTrophyControl, page `arcade_mini`: BGM 0x17 "jg05", the cup `race/a_<easy|normal|hard|
  special>_gold.prm`, "Congratulations", OK accepted after 180 frames; then the options' trophy
  count +1 and gold flag 0x1000 << (t - 1).
- GT-mode races (t 5..8 = beginner / amateur / professional+endurance / rally; needs m > 0):
  p < 3: GTmodeTrophyControl, page `gtmode_mini`, cup `race/g_<easy|normal|hard|special>_
  <gold|silver|bronze>.prm`, the place (`rank_disp`), the prize money (`get_money_disp`) and
  the credits counting up by it (`count`, PrizeCounter digit roller); OK once the counter is
  done. p >= 3: ConsolationControl, page `consolation_prize`, `race/consolation.prm`, same
  counter (BGM 0x17 restarted). One GT driver (`fn_00132BA0`) shows the cup only for a win
  when the result list has fewer than 6 cars.
- The GT cup page (reference captures): "1st" at the top, the cup, "Prize Cr. <n>" on the cup
  counting **down** while the balance "Cr. <n>" under it counts **up** by the same amount; when
  the count is done the round OK button appears (no OK before) and the prize line goes.
  The three PrizeCounter items (H): PrizeCounterControlItem `count` = "Cr. %s" (the balance,
  comma-grouped by `0x23B868`), PrizeCounterShadowItem `count_shadow` the same in black,
  PrizeCounterRemainItem `get_money_disp` = "%s Cr. %s" (the localized "Prize", target −
  balance). The two templates are program strings (`0x3512E8`, `0x313A20`; PAL `0x352C98` /
  `0x315280`, NTSC-J `0x34B4D8` / `0x30E390`), the same in every language. Only the first
  "%s" is localized: the realtime text slot `PRIZE_RT_MSG` / `BONUS_PREFIX` (`0x351D18`,
  registered by `0x174460`), i.e. `text/realtime.dat` key `PRIZE_RT_MSG_BONUS_PREFIX` in the
  language ("Prize" US / BR, "Prix", "Preis", "Premio", "Precio" on the PAL disc), else the
  program's built-in default, the EUC-JP 賞金 at `0x351D10` (PAL `0x3536A8`, NTSC-J
  `0x34BF18`). The NTSC-J disc's realtime.dat (67 keys) has no such key, so Japanese shows
  the default. `0x23B868` groups digits with ',' in every language and release (PAL
  `0x23C8E8`, NTSC-J `0x238688`), over the printed "%ld" text sign included. The Remain item fades in over 60 frames once its layer is shown, stays 61 frames
  after the count is done ("Prize Cr. 0"), then fades out over 60 frames (`0x129DE0`). The
  control item's start delay is per page: 120 frames on the cup and consolation pages, 360 on
  the championship trophy page (the three constructors `0x1294E0` / `0x1295E8` / `0x1296F0`),
  counted from the page's first frame; the first step comes two frames after the delay.
  The NTSC-J program has the same delays and step constants (it counts its ×100 sums with
  them); the PAL program has 50 Hz ones (delays 100 / 100 / 300, 75 creep steps, velocity
  0.2857143, acceleration 7.591740, its cap 569.3805) [H].
  Counter [C]: the item counts frames from its creation (the page's frame 0); once the count
  passes the start delay it counts, and the first step is on the next frame (frame 122 with the
  120-frame delay). From then on it steps on **every** frame until done; nothing (a beep
  included) ever holds it. All its arithmetic is single precision: velocity, acceleration,
  speed, the carried fraction and the beep sum are floats; the balance and the target are
  64-bit integers. One step, in this order:
  - the step count + 1 (every state);
  - state 1 (the creep): velocity = 0.2380952 (5/21) on every frame, which also overwrites a
    tripled velocity stored back the frame before; then, if the step count is above 90, state 2.
    That frame still steps as the creep (acceleration 0), so there are 91 creep steps and the
    first accelerating step is the 92nd;
  - state 2: the increment is 5.2720418; an acceleration already at or above 474.48376 is set
    to 474.48376 and the increment is 0; a velocity already at or above 142345.125 is set to
    142345.125, the acceleration to 0 and the increment is 0. Both tests look at the old values
    before anything is added, so the acceleration passes its cap by one increment for one frame
    and snaps back on the next;
  - acceleration += increment; step = (velocity + acceleration) x speed; the step is stored
    back as the velocity; carried fraction + step: its whole part (truncated toward zero, a
    32-bit integer) goes to the balance, the rest is carried;
  - balance at or past the target: the balance is set to the target and the count is done
    (state 3), with no beep on that frame;
  - else the beep: the step is added to the beep sum and a beep step counter + 1; when the sum
    is 1 or more and the counter 3 or more, SE `cursor` (se.inf index 3) plays at once, on this
    frame, and both reset to 0; a sum of 1 or more before the third step is held at 1 (and the
    counter is held at 3 while the sum is below 1).
  Confirm is read on counting frames only, before the step (presses during the wait do
  nothing): the first press sets the speed to 3 for the rest of the count, used by that
  frame's step. As the tripled step is stored back as the velocity, the velocity triples on
  every accelerating frame until it reaches its cap; from then on every step is 3 x 142345.125
  (in the creep the speed only gives 0.714 a frame, as the velocity is reset). The second press
  sets the balance to the target, so that frame's step finishes the count (no beep); later
  presses do nothing.
  Results without presses: 500 Cr: the first credit on the 5th step, done on the 99th step,
  220 frames after the page's first frame, with 20 beeps (every 5th step of the creep, every
  3rd once accelerating: steps 5, 10, ..., 90, 93, 96); 1,000 Cr: frame 222, 21 beeps; 5,000 Cr:
  the 108th step. Single precision delays some creep credits by one step (5 Cr arrives on the
  22nd step; exactly 5/21 a step would give it on the 21st). The console's floats round toward
  zero: no frame differs from round-to-nearest up to at least 100,000 Cr; a 2,000,000 Cr count
  shows a few middle digits on other frames.
- The cup: MiniPrize draws the .prm with its own camera, frustum 60 degrees horizontal, vertical
  half-extent 0.7 x the horizontal (640 x 448), near 0.1, far 300 (`fn_0022A120`; x 0.75 in the
  16:9 mode). The cup in the capture is still smaller than our full-frame draw gives; the source
  of the difference was not found (M: x 0.86, fitted).
- RACE RESULTS overlay (1P capture): a car picture straight on each rank bar (no box); the car
  name is the car record's name plus its second string (+0xE6, e.g. "ALTO WORKS" + "SUZUKI
  SPORTS LIMITED(J)"), while the result quick menu shows the name only ("ALTO WORKS"). The whole
  HUD, the lap / time block (Lap, Best, Last) included, is under the panel (§B2 draw order).
- **Order in the GT single-race driver** (`fn_00132BA0`, menu loop over the pre-race quick menu
  `fn_0012E2C8`, actions 1..9 via the jump table 0x315FC0, 6 = start): the race (race vf at
  +0x150 with (0, 1)) -> the RACE RESULTS overlay over the live race -> on a finish the prize
  page (`fn_00137DC0`) -> **the race's replay starts by itself** (the same vf with (1, 1), replay
  mode 1) -> back in the loop: the result quick menu (`qm_gt_result`: REPLAY / SAVE / OPTIONS /
  EXIT, the GT-logo checker background, every finished car with its own race time, the player's
  row orange; the list is filled once when the race loop ends, before the replay, in race order:
  a finished car's total time from the start, a car still on track the "no time" value printed
  as `--'--.---` (the font's long dash 0x7F: changelog 2026-10-29), and with the player not
  finished every row reset to `---` / `--'--.---`)
  -> EXIT -> the race list page, where the race's button now carries the best-result
  mark (`mark_<place>`). (User captures of the Sunday Cup sequence.)
- `not_get_car` (NotCarGetControl, page `not_car_get`): "Garage is full / You failed in
  acquiring a bonus car" when a prize car cannot be stored.
- `car_get` (CarGetControl, page `car_get`): the prize car's carousel and roulette, the controls
  held until it has stopped on the car won (gt_mode §10.1).

## G. Replay camera director: view modes [C]

The replay camera manager keeps a view mode (+0x96E4, 0..3; per view +8; handlers 0x20B8E8
modes 0 and 3, 0x20C580 mode 1, 0x20BE58 mode 2, 0x20D208 the "driver" mode 5 of race +0x70).
No pad input changes it during a replay: `fn_00208BE8` cycles it on input bits 0x800000 /
0x1000000, which the race input never produces (§G4d); REPLAY OPTIONS sets it
(`fn_0020EBA0`). At replay set-up
(0x208550..) the mode is the value stored in a settings block (+0xD0, default 0), except for
replay mode 2 with records on (the attract demo; R+0x6C clear, 0x2085D4), which picks it at
random (`fn_0020E930`). Every replay goes
through this set-up, whatever started it: the automatic replay after a run, the REPLAY item,
the Replay Theater's saved replays and its DEMONSTRATIONs (all `vf42(race, 1, 1)`, §A8c) and
the attract demo; so a demonstration opens with the same shots as any replay (§G5). **Only mode 1's
handler rolls the per-shot colour filter / levels** (re notes: replay colour filter). So the
post-race replay (mode 0 by default) shows the course's GTRC trackside cameras with no random
grades; the colourful shots belong to the attract demo.

**The handlers** (called from `fn_00208BE8` per view; before the per-view loop it runs
`fn_0020B200`, the shot length, and after it `fn_0020B380`, the cut request, every frame for
every view mode, §G2a) [C unless marked]:
- **Modes 0 and 3** (`fn_0020B8E8`): the GTRC course shots of the course camera's picker
  (`fn_0020AD18`, §G5). Identical code for both.
- **Mode 2** (`fn_0020BE58`): the same shot picking; at every new shot, when the current
  camera is the course camera (its byte +5 == 1), one draw of the director's generator
  (+0x961C): r < 0.07 (f32 0x350890) -> `fn_001FF648` (the followed car when the target state
  +0x16 == 1) and `fn_00209738`: a random on-board mount (types 0x10 / 9) of that car for the
  shot (debug text "TEST : Automatic ONBOARD"). The draw comes at every new shot while the
  current camera is the course camera; the cut needs a pair: `fn_001FF648` returns the
  target state's second car (+0xA, only when +0x16 == 1, §G4c), the mount is on that car (the
  battle partner, not the followed car), `fn_00201708` turns the course camera on board
  (§G5). The turn lasts until the shot ends by §G5's rules: its record runs out and the gate
  opens, or a new target ends it (a battle change or a TOP / AUTO switch re-aims, §G4c); its
  +0xF0 = 180 (`DesignerCamera::vf18` = +0xF0 < 1) only gates TOP / AUTO (§G4c). TOP / AUTO: §G4.
- **Mode 1** (`fn_0020C580`): no course-shot picker. Every frame: the view's reset flag
  (+0x9604 + 2 v, s16, read and cleared; set for every view at replay set-up `fn_00208320`
  0x208428 and for the edited view by every REPLAY OPTIONS apply `fn_0020EBA0` 0x20EC68), the
  pad bits (Cross, Circle, R1, L1, §G4d), Up / Down / Left / Right (§G4c), the trail's timer
  (§G7), a battle change of the followed car (§G4c), then **a cut when the request
  (+0x9620, forced to 0 while the standing-start hold `race+0x5C` > 0) is > 0, or the reset
  flag, or a pad bit**. The cut (§G2b) takes the shot length x (+0x9624).
- **The director's generator** (+0x961C, `fn_0026C118` step, `fn_0026C178` unit): seeded at
  replay set-up (`fn_00208320`, 0x2083E0) from a hardware counter (`fn_002502C8`), so its
  sequences differ from run to run. Every roll below and the attract pick use it, and
  `fn_0020B380` steps it once every frame. `fn_0026C178` itself steps (it calls `fn_0026C140`,
  which calls `fn_0026C118`), so each "step + unit" draw advances the state twice [C]; ours
  draws the same way (`ReplayDirector::draw`).
- **Records with a duration** (`DesignerCamera::vf13` 0x201830): for a record aiming at its
  own target curves (kind & 0xC == 0) whose duration (+0x0C) is 1.0 or more, t = frames since
  the shot began (+0xE0) / (duration x 60), clamped 0..1; eye, target and zoom all use that t.
  Records aiming at the car (kind & 0xC != 0), and those under 1 s, use the distance t of
  `fn_00201418`.
- **Zoom** (+0x88) = 1 / tan(half the horizontal angle) over the 640-pixel frame (each class's
  projection multiplies the screen distance by it); over the 448-line frame the vertical
  half-angle has tan = 0.7 / zoom, and the projection's Scale(1, 1 / 1.098, 1) (§G8) widens it
  to 0.7 x 1.098 / zoom. The frame is shown at 4:3, so the picture's horizontal half-angle has
  tan = 1 / zoom: 0.976 of a square-pixel 4:3 frustum with that vertical angle. Ours:
  `ReplayDirector::zoom_fovy` (with `kYScale`) for the course and procedural cameras and the
  player views, `CameraRig::kPixelAspect` (0.976) in the projection and every overlay placed with
  it (`CameraRig::frustum`). Checked 2026-11-02 on arcade grid captures (six courses, in-car and
  chase): ours over the original's scale 1.000 across and 0.99-1.00 up and down; before, 1.071 /
  1.098 (our views were narrower: a lighter sky band, a larger car in the chase view).
- **Ours** (`race::ReplayDirector`): modes 0 / 2 / 3 as above with §G5's picker (mode 2's cut =
  our bumper view on the partner's car, `set_pair`; TOP / AUTO `race::ReplayFocus`, §G4); mode 1
  `avant_garde_due` / `avant_garde_cut` with `race::CutClock` (§G2a), `race::ProceduralCamera`
  (§G2b), `race::ShotLook` and the post-process pass (§G6, §G7); `shot_t` for the timed
  records; the generator seeded from the clock at every replay / demonstration start
  (`restart_director`).

**§G2a The cut requests and the shot length** [C unless marked] (`fn_0020B200` / `fn_0020B380`,
every frame after the race music object's marker step, music §7; `CM` = the camera manager):
- **Shot length x** (+0x9624, f32 s; 4.0 at set-up 0x208470): i1..i4 = the music's intervals
  (time to the next marker) of tracks 1..4; view 0's mode 1: x = i4 with the music-timed cuts
  on, else i3; other modes: x = i3, min with i2 once phrases are in use (+0x962C, 1 at
  set-up), min with i1 once beats are (+0x962A). x > 80 (no marked song) -> x = countdown / 60.
  No minimum in mode 1: cuts 3 frames apart happen.
- **Request** (+0x9620, s32; 1 at set-up 0x208468): 0; the countdown +0x9638 (s32; 60 at every
  `fn_00207CC0`: set-up and REPLAY OPTIONS apply) is set to 120 while `race+0x5C` > 0; when no
  relevant interval is under 80 (tracks 4 with music-timed cuts on, else 2 and 3): -1 a frame,
  below 0 -> request 1 and 120. Pad and reset cuts do not restart it. Then the music's events
  4..9 (tracks 4..9) set the beat / phrase / section flags (+0x9628..+0x962E; 6 and 7 draw once,
  comparing with 10.5: always); mode 1: a track-4 event -> request 4 (music-timed cuts on), a
  track-3 event -> request 3 (off), return; other modes: track 3 -> 3, track 1 -> 1 when +0x9628
  (never on the disc), track 2 -> 2 when phrases are in use (+0x962C, cleared with p 0.5 under
  +0x9629). The request lasts one frame: the next frame's handler cuts with it.
- So with a race song the mode-1 cuts fall on the song's track-3 (= track-4) markers: in the
  PCSX2 footage (`race_a09` "sky scraper", song = video + 51.5 s) all 44 markers between 26 s
  and 100 s have a cut 0.02..0.09 s after the heard marker (2.9 frames: the code's 2-frame lead,
  the request's frame and the emulator's audio latency [I]); no cut falls between markers.
  Without markers (jingles, licence runs, an empty playlist, after a stop): the set-up cut
  (request 1, x 1.0), then 61 frames on, then every 121 (x 2.0). A song (re)start fires every
  marked track once: a cut when the race song starts, then one at its first marker.
- **Request values**: 1 the countdown: the cut's car alone (no partner); 3 track 3: with the
  partner, the dissolve's odds 0.1; 4 track 4: with the partner, odds 0.4; 2 (phrases) modes
  0 / 2 / 3 only.
- **Modes 0 / 2 / 3** use the requests only in the shot gate `fn_0020ABF0` (f32 0x350888 /
  0x35088C = 1.3), called by their course-shot pickers (flag 1), mode 2's TOP / AUTO switch
  (flag 0) and the attract demo's 300-frame car change: with the current camera's shot state
  (`DesignerCamera::vf16`, 0 / 1 / 2) 1 and flag 1, a change waits until x >= 1.3 or a request
  is pending; the states in §G5 [C; untested against footage]. Ours: §G5
  (`ReplayDirector::set_cut_gate`, with x after the shot length and the request the handlers
  see, before the next one).
- Ours: `race::CutClock` (setup / reset / update_shot_length / update_request; the flags of
  tracks 5..9 kept though the disc has none), stepped by `RaceScreen::update_camera_manager`
  every 60 Hz frame, live and in replays, after `RaceMusic::step_markers`. With BGM off the
  race's requests still play, at level 0 (music §1.1), so the cuts keep the markers. Not
  reproduced: the extra events a play request's -1 stream position may give in its first
  frame or two [?]. Replays re-run their start sequence (music §2.5): the race song, and with
  it the markers, starts at the replayed start signal; the start jingle has none (the countdown
  runs over the hold, held at 120).

**§G2b The cut and the procedural cameras** [C unless marked] (`fn_0020C580`; every "draw" is
the generator's step + unit pair):
1. x = +0x9624; request 3 sets the dissolve bias f22 = -0.3 (0x3508A8).
2. The car: draw, i = int(r n), +1 if the current car, 0 if >= n, then the first valid car
   (`fn_00209A40`). The current car is never picked; the next one has 2 / n.
3. The target over 1 frame: alone when it has no valid partner or the request is 1, else
   with its battle partner (`fn_001FF490` / `fn_001FF4F8(..., 1)`).
4. The course-shot search, up to n cars; a failure moves to the next car, aimed with its
   partner whatever the request; none: back on the first car. `fn_002074A0(car)`: lap
   variants k from the car's laps + 1 (k > nvar -> 1), nvar tries; D = k L + s; each record
   containing D (`fn_0018FD20`, up to 32) that is not the current one, with u = (D - start) /
   (end - start) in [0, 0.8] (`fn_00207278(rec, D, 0, 0.8)`), whose time left at the car's
   speed (`fn_00207178`: track metres per frame, the lap wrap handled; forward: max(v, 0.05),
   (end - D) / v; backward: min(v, -0.05), (start - D) / v; at least 0.01) is at least x x 60
   frames, and whose camera position at t = 0 is at least 15 m (x and z only: dx^2 + dz^2 >=
   225) from the current record's at t = 0. A stopped car always qualifies; a fast one rarely
   does for long x.
5. A record found: the course camera (vf25: zoom 1.428, +0xF0 180, the roll swing at rest),
   +0xE8 2, +0xEC 600; draw: roll +0xE4 = 20 r - 10 (degrees, Rz of the view); draw < 0.17
   (0x3508AC): a wide shot (+0xA58, the first frame's zoom 1.0 = 90 degrees across, then the
   zoom spring of §G4g); draw < 0.30 (0x3508B0): one more unit draw, a yaw of 60 r - 90
   (+0xA5A on, +0xA60 pitch 0, +0xA64 yaw) added to the first frame's aim target (§G4g): the
   camera starts swinging at 30 .. 90 degrees a second [C]; draw < 0.10 (0x3508B4): draw, a roll swing from 200 r - 100 degrees
   (`fn_00206BE8`; per frame v += (-30 x - 10 v) / 60, |v| <= 900, x += v / 60: back to 0 in
   about 1 s), added for records aiming at the car (kind & 0xC).
6. Draw < 0.25, or no record: a procedural camera. Draw r', kind = `fn_0020C510(x, r', hold)`:
   the first i with r' < table[i]: x >= 3 {0.35, 0.6, 0.7, 0.85, 1.0}, x < 3 {0, 0.3, 0.5, 0.7,
   1.0}, the hold {0, 0, 0, 0, 1}. Kind 2 sets the trail bias f23 = 0.5. Kind 4: m = the car's
   GTCI mount count; m - 3 > 0: draw, mount = clamp(int(r (m - 3)), 0, m - 4) + 3, else 0.
7. The look (§G6, §G7): levels, trail, dissolve, filter.
8. Aim now; the pop-up record on a car change.
The camera follows its own shot until the next cut: no class ends or re-plans itself in mode 1.
Each procedural class keeps its own generator, seeded from the hardware counter in its set-up
(vf24), and follows the cut's car only (never the partner). Angles in degrees; view = Rz(roll)
Rx(pitch) Ry(yaw) T(-eye); yaw(v) = 180 - atan2(v.x, v.z), pitch(v) = -atan2(v.y, |v.xz|)
(positive looks down); the car's yaw frame Ry(-yaw) T(-P), P the interpolated body origin, its
step V = P - P(last frame). Body extents: front F, rear R, half width W (ours: half the length
and width).
- **0 AutomaticCamera** (vtable 0x33E730; vf24 0x2032A8): zoom 1/tan 35; draw > 0.5: variant A
  (`fn_00203528`): s = r > 0.5 (look on ahead); FOV 30 r + 55; k = 0.15 r + 0.05 (s) or
  0.35 r + 0.5; t = x k; eye.xz = P + 60 V t + (2 r - 1, 2 r - 1); the road under it
  (`fn_0018FBF8` from P.y + 60 V.y t + 100) + 0.1, then + r; pitch 20 r - 15; yaw = yaw(P - eye)
  (s: yaw(eye - P)) + 20 r - 10; roll 30 r - 15. Variant B (`fn_00203958`): l = (+-W (0.7 + 1.5 r),
  0, +-(F + R) r) in the car's yaw-and-pitch frame, the road + 0.1 + 0.6 r; FOV 30 r + 55; T = P +
  60 V (1 + 2 r); yaw = yaw(T - eye) +- FOV / 2 (the side of the turn from P to T); pitch 20 r -
  15, roll 30 r - 15. No road (or a vertical face): eye P + (0, 1, 0), yaw(V), pitch 20 r - 10,
  roll 0. Built once: a still camera.
- **1 AutomountCamera** (0x33E850; vf24 0x204208, vf13 0x2043E0): d = 8 r + 1, theta = 2 pi r,
  o = (d sin, 4 r + 3, d cos) in the car's yaw frame; FOV 30 r + 55; roll 40 r - 20. Per frame
  four springs (+0x124, +0x130 k 18; +0x13C, +0x148 k 5): with p 0.1 a target 256 r - 128,
  v += (k (target - x) - 10 v) / 60, x += v / 60, w = x / 128; eye = o + (2 w13C - 1, 0,
  2 w148 - 1); A = FOV / 7; view Ry(A w130 - A/2) Rx(A w124 - A/2) Rz(roll) Rx(pitch(-o))
  Ry(yaw(-o)) T(-eye) (aimed without the wobble).
- **2 MatrixCamera** (0x33EBB8; vf24 0x204F50, vf13 0x2052C0): extra radius 5 r, end azimuth
  60 r - 30, sweep 100 + 220 r min(x, 3), direction +-1, the centre's x 3 r - 1.5 -> r - 0.5 and
  z 3 r - 1.5 -> r - 0.5, height 2 r + (r + 0.1) -> r + 0.1, FOV 30 r + 40, roll 40 r - 20, over
  T = max(x, 0.01) s (f = time left / T, 1 -> 0, then held). Radius = max(F, R, W) + 0.2 + extra;
  theta = the quadrant-eased (end + sweep f dir) (g(a) = 90 h(a / 90), h(t) = ((0.1285715 t -
  0.557143) t + 1.4285715) t: fast through the front and back, slow at the sides); pitch =
  atan(h / radius) - (FOV / 3) 0.4 sin of it; view Rz(roll) Rx(pitch) T(0, -h, -radius)
  Ry(theta) T(-centre); the eye (cx - r sin, h, cz + r cos): theta 0 behind, the shot ends
  within 30 degrees of behind; raised (pitch kept) to the road + 0.2.
- **3 ZoomupCamera** (0x33ECD0; vf24 0x205A00, vf13 0x205CA8): roll 40 r - 20; rear with p 0.5
  (else front); distance (R or F) + 2.5 + 4 r past it; height 1.2 r + 0.1; lateral 3 r - 1.5;
  with p 0.3 (0x3507E4) lateral 0 and roll 0; aim height 0.5 r + 0.2; FOV 15 r + 25. Per frame
  the speed factor q / 123.457 with q = clamp(|60 V|^2 - 1.929, 0, 123.457) (5 .. 40 km/h); two
  shake springs (stiffness 57.5 x the factor, a new target with p 0.4) and two vibration dampers
  (input (r - 0.5) 750 with p 0.5; v += ((u - u') - 12 v) / 60); view Rx(n1) Rx(d2 f) Ry(0.8 n2)
  Ry(0.25 d1 f) Rz(roll) Rx(pitch) Ry(yaw) T(-eye), aimed at the aim point; raised above the
  road + 0.2. No zoom animation.
- **4 a car mount**: the course camera made on-board (`fn_00201708`), mount 3..m-1 of the car's
  GTCI table (+0x68 count, +0x6C records of 0x40: +0 pos, +0xC pitch, +0x10 yaw, +0x14 roll
  (degrees), +0x18 zoom, +0x1C flags; the eye at -pos in the model frame): mirrors looking
  back, bumpers, roof, the driver's head left / right, side sills, outside follow points (the
  Altezza's 20). View Rz(roll) Rx(pitch) Ry(yaw) T(pos) (world -> model); the in-car shaker
  terms (mounts without flag 4) are not reproduced. Flags bit 1 (`fn_00201150` -> +0x109) is
  the view kind `OnBoardCamera::vf10` returns: 1 outside (mirrors, roof, sills, outside points),
  0 on board (in-car, the bumpers, the driver's head 10 / 11); with 0 the car passes
  (`RacePS2Base::vf51..53` via `fn_00209370`) skip the camera's target car. Ours:
  `CarMount::outside`, `CameraRig::replay_on_board` (the car the camera rides not drawn: the followed car, or the
  partner on mode 2's on-board cut, `CameraRig::replay_on_partner`).
- Footage (`2-video-mode.mp4`): Zoomup head-on at 26 s, Matrix at 30 s, Automatic A at 35.6 s
  and 59.4 s (and with s at 66.8 s), Automount at 37.2 s and 61.1 s, the stuck player car's
  course record at 36.1 s and 63.1 s, all as decoded.
- Ours: `ReplayDirector::avant_garde_cut` (the draws in this order; returns the car, the
  partner rule and the dissolve), `race::ProceduralCamera` (planned at the cut with the shot's
  clock seed, stepped at 60 Hz by `CameraRig`, the view built on the car's pose each frame;
  the road from `Track::probe`), `race::car_camera_mounts`.

**§G5 The course camera's shots (modes 0, 2, 3) and a replay's first shot** [C unless marked]
(`fn_0020AD18`, called by the handlers of modes 0 / 3 `fn_0020B8E8`, 2 `fn_0020BE58` and 5;
view v at CM + 0x300 + 0x24C0 v: its event record +0, cameras OnBoard +0x300 (the driver's
camera), Designer +0x480 (the course camera), Passline +0x1940, Starting +0x1A80, Automatic
+0x1BC0, Automount +0x1D00, Logger +0x1E80, Matrix +0x1FC0, Zoomup +0x2140, Heli +0x22C0; the
current camera CM + 0x5D4 + 0x24C0 v):
- **Which handler.** `fn_00208BE8` dispatches per view on its input's mode (RaceInput +0x130 =
  the play mode, set by `fn_00210658` at every race set-up): 0 (live) `fn_0020B788`; 1..3
  (replays, demonstrations, the attract demo) race +0x70's driver mode or the view mode's
  handler. A view with no input (CM + 0x2780 null: a race set up under records off, the quick
  menus' races and the licence box, §A8c) reads as 0, live. `fn_0020B788`: while the
  standing-start hold C (`race+0x5C`) - 60 >= 0 the StartingCamera (+0x1A80 via
  `fn_00209660`; `cameras/start.cam`, t = 1 - (C - 60) / 600, `StartingCamera::vf13`), with a
  1.0 s dissolve (CM+0x268 1, +0x26C 1.0) when C - 60 is 120, 60 or 0 (start.cam's cuts at t 0.8
  and 0.9, and the cut away); else the driver's camera (`fn_002096C0`). So start.cam is a live
  shot of every view with a standing hold (races, standing licence tests, machine tests with a
  long enough hold; the 2P battle has no hold), and of the menu races of those, never in a
  replay or demonstration. Ours: `race::start_shot_on` / `start_shot_t` /
  `start_shot_dissolve`, `RaceScreen::live_camera` / `frames_to_signal` (both 2P cameras too).
- **The lap variant** (`fn_001FEE18`, in the aim `fn_001FF068`, every frame): from the followed
  car's lap counter (car +0x13B8: 0 before its first crossing of the start line, then the lap
  it is on, §B0) k = laps > 0 ? (laps - 1) mod nvar + 1 : 0, and D = k L + s (target state
  +0x38; s +0x3C, k +0x70). Before the line D = s: the script's first records, from
  first.start (Laguna Seca L - 276 m, Tokyo R246 L - 90 m, Trial Mountain L - 107 m, Midfield
  L - 1217 m), cover the start and carry on across the line (k 0 -> 1 keeps D continuous).
  Mode 1's per-car table (`fn_00207050`, +0xC: laps > 0 ? (laps - 1) mod nvar : 0) starts its
  variants one above (§G2b). [Ours had k = completed laps mod nvar + 1: a car before the line
  took variant 1's records at the lap's end.]
- **Records at D** (`fn_0018FD20` -> `fn_001A4258`): the last record starting at or before D
  (binary search `fn_001A41F0`) when D is before its end; past the end of the list the scan goes
  on from the first record with D - P (P = nvar L) while they start at or before it; at most
  32. Of two overlapping records only the later-starting one is found. `fn_002072C0(race,
  target, flag)`: the first of them with u = (D - start) / (end - start) in [0, 1] (flag) or
  [0, 0.6] (0x350848) (`fn_00207278`; u from D, not D - P). `fn_00207370`: variants v = k + 1,
  k + 2, ... (past nvar: 1), nvar tries, the first record at v L + s with u in [0, 0.6]
  (0x35084C); it returns v.
- **The course camera's shot** (`DesignerCamera::vf25`: +0x180 record, +0xA70 its variant (-1
  the car's), +0xA74 the car's k at the set, +0xE0 frames 0, +0xE8 0, +0xEC and +0xF0 180,
  +0xA88..+0xA8A cleared; an on-board record (kind & 3 == 3) on its mount +0x18 via
  `fn_002004D0`). Its D (`fn_00201760`): variant -1: D, + P while the car's k is below the k at
  the set (wrapped past nvar); else s + variant L, + L once the car's k differs (crossed the
  line). Every frame's step (`fn_001FE850` -> `vf13`, after the handler) counts +0xE0 and sets
  "run out" (+0xA88): a timed record (§G "Records with a duration") once frames / (duration x
  60) passes 1; every other record, an on-board record and a turn on board once u of its D
  leaves 0..1 (`fn_00201418`). +0xA89: a car-aimed record's car moving backwards.
- **Turning on board** (`fn_00201708`): the course camera becomes an on-board camera on the
  followed car's mount 0 (`OnBoardCamera::vf9`: the in-car view), +0xA78 3, +0xA8A 1, +0xF0
  180, keeping its record (its u still decides run out). It clears the camera's type byte +4,
  which nothing sets again before the next race (the constructor's 1).
- **Set-up** (`fn_00208320` 0x208850): the course camera is set to the script's LAST record
  (index 0), and the view's reset flag is set (§G).
- **The picker** (`fn_0020AD18(CM, view, race, input, flags)`: bit 2 always; bit 1 forced = the
  view's reset or a new car; bit 0 search first = the attract demo's car change in modes 0 / 3,
  mode 2's manual change, or a TOP / AUTO switch that ended the shot or whose car is far off
  the view (`fn_00202760`); the course camera becomes the current camera):
  1. No record (never after a set-up): `fn_002072C0(.., 1)`, else `fn_00207370`, else the
     driver's camera.
  2. Search first: step 5.
  3. Ended (`vf17`): the target was set this frame (target state +0x34) and the shot is on board,
     has kind 0x400, or D (the car's, not the shot's) is outside its record. Forced or ended:
     re-set the same record and variant (`vf25`) and step it; run out: the record
     `fn_002072C0(.., 1)` at D whatever its progress (no dissolve), or with none turn on board;
     return.
  4. The gate `fn_0020ABF0(.., 1)` on the shot state (below); closed: return.
  5. The search: R = `fn_002072C0(.., manual)` (manual = target state +0x36, set by a manual Up /
     Down change for the aim's move, 15 frames in modes 0 / 3, 120 in mode 2: u up to 1). R and
     (manual, search first or R not the current record): the dissolve, set R. Else another
     variant's record (`fn_00207370`) not the current one: the dissolve, set it with its
     variant, +0xE8 2, +0xEC 180 (held). Else the dissolve unless the type byte +4 is clear
     (turned on board before; the search-first path always dissolves), turn on board, +0xE8
     2, +0xEC 180.
  The dissolve (`fn_0020AB80`, before the set): `DesignerCamera::vf22` of the OUTGOING shot,
  1.0 s on board (+0xA78 3: an on-board record or a turn), else its record's +0x64, at most 4 s,
  none for 0.
- **Shot state** (`DesignerCamera::vf16`, by view mode): an on-board shot on a mount of 5 or
  more seen through another car (`fn_002009F0`) 2 [not reproduced]; +0xE8 2 holds 0 while
  +0xEC counts down (180 frames; a 0x400 record not on board is not held); else
  `fn_00202500`: modes 0, 3 (and 5): run out 2, else 0. Modes 1, 2: a record without 0x400, or
  on board: run out 1; a 0x400 record: run out 2, else 1 in its last tenth in the car's
  direction (t > 0.9; backwards t < 0.1; 0x350740 / 0x35073C), else 0.
- **Gate** (`fn_0020ABF0(CM, view, race, flag)`; x the shot length +0x9624, a request +0x9620 >
  0, §G2a): the driver mode (race +0x70): run out. Flag 1: state 2 open, 1 open when x >= 1.3
  (0x350888) or a request, 0 closed. Flag 0 (mode 2's TOP / AUTO switch): state 2, x >= 1.3
  (0x35088C) or a request.
- **So** a replay's or demonstration's first frame re-sets the last record, finds D outside it
  and cuts to the record at D at any progress: before the start line the script's first
  record once the car is at or past first.start; further back the camera turns on board (the
  in-car view) for that frame, and the next frame's gate takes another variant's record at the
  lap's end (u <= 0.6; held 3 s, dissolving 1 s from the in-car frame) or, with none, stays on
  board until a retry after the 3 s hold finds the start record. Modes 0 / 3 cut when a shot
  runs out (a frame late: the state is the last step's), mode 2 waits for x >= 1.3 s or a
  request. A timed record that runs out with the car still inside it (and no other variant's
  record) turns on board. The REPLAY item, the automatic replay, theater files and
  demonstrations, and the attract demo all open this way.
- **The arcade time-trial demonstrations** (start before the line; first.start): AT01 Midfield
  -199 m (-1217): record 5. AT02 Trial Mountain -129 (-107): in-car one frame, then variant
  1's record 21. AT03 Tokyo R246 -199 (-90): in-car one frame, record 25. AT04 Deep Forest
  -199 (-133): in-car (no variant's record at u <= 0.6) until the start record. AT05 Laguna
  Seca -149 (-276): record 0. AT06 Apricot Hill -199 (-126): in-car one frame, record 17. AT07
  Grand Valley -199 (-397): record 0. AT08 Smokey Mountain -169 (-258): record 0. AT09 Tahiti
  Maze -309 (-43), AT10 (slalom80r) -399 (-152): in-car until the start record. PCSX2 captures
  of the original at about 2 s and 5 s into AT05, AT03 and AT02 (brightness session,
  2026-10-22) show the same shots as ours with these rules (AT05's opening had differed); the
  in-car openings (AT04, AT09, AT10, and the single in-car frame) were not captured [C].
- **The GTRC's second list** (+0x2C count, +0x30 records): course stills, shown only by a
  slideshow (`fn_0020A9C0`, reached only from a function table, sets CM +0x9610;
  `fn_0020AA90` then cycles them every 240 frames with 2 s dissolves): never in play. [It had
  been taken for start shots.]
- Ours: `GtrcScript::lap_variant` / `records_at` / `record_at` / `other_variant`,
  `race::lap_counter` (`RaceScreen::lap_count`, `RaceField::opponent_lap_count`),
  `ReplayDirector::setup` / `reset` / `car_changed` / `target_set` / `target_switched` /
  `set_cut_gate` / `shot_state` / `gate_open` / `shot_gate` / `camera_fresh`; on-board records
  and turns on the car's mount through `race::ProceduralCamera` (kind 4; no in-car shake).
  Not reproduced: `fn_002009F0`, mode 2's cut on a random on-board mount of the
  partner (ours: a bumper view), the driver mode 5.

**§G6 The shot look and the grade** [C unless marked]:
- **Rolls at a cut** (after the camera): draw < 0.25 -> levels (+0x23C): black = min(0.175
  draw, 0.175), white = 0.2 + 0.8 draw, swapped when the first draw < 0.0125; trail (§G7);
  dissolve (§G7); the colour filter (+0x248): on -> kept with draw < 0.8 (0x3508D0); off ->
  on with draw < 0.1 (0x3508D4), then saturation (+0x258) q < 0.7: 1, < 0.85: draw, else 0,
  and the preset int(5 draw) mod 5 of 0x2DED30 (red (1, 0, 0), blue (0, 0, 1), cyan (0, 1, 1),
  (0.3, 0.3, 0.3), (1, 1, 1)) into +0x24C..+0x254. The filter persists across cuts; levels and
  trail are rolled afresh. `fn_00207CC0` (set-up, REPLAY OPTIONS apply) clears them all. At
  stationarity: filter 1/3 of shots, levels 1/4, either 1/2; dissolve 40 % of cuts.
- **The pass** (`RacePS2Base::vf45` 0x17ECC0, after the 3-D scene, before the 2-D layer: the
  replay mark is not tinted): `fn_002582F8(buf, g_r, g_g, g_b, black, white, sat)`. In live play
  (`race+0x9784` 0) when the course's Env0 flag 0x80 is set; in replays and demos only when
  the shot's levels or filter is on. Parameters (`fn_00208B68`): black / white = the shot's when
  levels are on, else `CM+0x20 / +0x24` = Env0 **+0x98C / +0x990** (copied at set-up by
  `fn_0020ED28`); g and saturation = the shot's when the filter is on, else Env0 +0x980..+0x988
  and +0x994. Super Speedway: g (0.87, 0.90, 0.95), saturation 1, black 0, white 0.8: its
  levels-only shots brighten strongly with a cool cast, its filter shots stretch x1.25.
- **Arithmetic** (an 8-bit CLUT pass per 64 x 32 tile; `fn_00258DB0`, CLUT copy, saturation
  colours 0x350A9C..A4): table L_c[i] = trunc(clamp(255 (t^2 + 2 g_c t (1 - t)), 0, 255)),
  t = clamp((i / 255 - black) / (white - black), 0, 1) (inv 0 when equal; entry 0 uses the
  unclamped t), g_c clamped 0..1; s' = min(128 sat, 255), m = max(128 - s', 0), M[c][k] =
  trunc(clamp(s' + m w_k)) for c = k, trunc(m w_k) otherwise (w = 0.30, 0.59, 0.11);
  out_c = min(255, sum_k (L_c[in_k] M[c][k]) >> 7) [C for the arithmetic, I for the lane-to-byte
  mapping]. In floats out_c = s L_c(in_c) + (1 - s) sum_k w_k L_c(in_k): each output channel's
  curve on every input channel (the red preset at saturation 0 turns pure red grey 0.3).
- Ours: `race::ShotLook` / `ReplayDirector::roll_look`, `RaceScreen::apply_post_pass`, the
  View3D post-process pass (`DrawOptions::post_grade`, model.frag `gt3_grade`, CPU twin
  `render::post_grade_pixel`). The sun glare and the lamp glows are drawn into the picture before
  the pass, as the original's FlareClass (§R1).

**§G7 The dissolve and the trail** [C unless marked]:
- **Dissolve** (`CM+0x268`, time `CM+0x26C`; both reset to 0 / 0.5 at the start of every camera
  frame 0x208C70): set by a mode-1 cut with draw - f22 < 0.4 (0x3508CC; p 0.1 for request 3),
  by the modes 0 / 3 Up / Down car change (0x20BB08: 1.0 s; also the attract demo's simulated
  Up every 300 frames), by the course-shot picker of modes 0 / 2 / 3 at its searched cuts
  (§G5; `fn_0020AB80`, called before the new record is set: `DesignerCamera::vf22` of the
  outgoing shot = 1.0 on board (+0xA78 == 3: an on-board record or a turn), else its record's
  +0x64, at most 4; Akasaka 0.5 on 46 of 80 records, licence courses 0.3, most race courses 0;
  none at the forced cuts, e.g. a replay's first shot), by `fn_0020AA90` (2.0 s, a dead
  slideshow) and `fn_0020B788` (1.0 s at the live start shot's cuts, §G5). Cancelled when the new camera's vf23 (`DesignerCamera` +0xA8B) is set [?, not
  reproduced]. The renderer (vf45 0x17EDE8): on the first frame after any race / replay / demo
  set-up (`race+0x9CCC` 0) d = 0.5, else with the flag d = its time; it freezes the last
  picture shown (`fn_00180AE0`, the alpha bytes of three 640 x 224 buffers) and x =
  max(1 - (1/60) / d, 0); every race step x -= (1/60) / d; while x > 0 the frozen picture is
  drawn over the frame with FIX = trunc(128 x^3) (`fn_00180BF8`). Footage (cut at 38.73 s):
  .92 .82 .75 .66 .60 .52 .35 .19 .09 .04 against x^3 .90 .81 .73 .65 .58 .51 .34 .18 .08 .04.
  The replay start's dissolve is the "dimmed start" (the exit dialog dissolving into the
  replay), not a grey wash.
- **Trail** (`CM+0x25C`): a cut sets it with draw - f23 < 0.1 (0x3508C8; 0.6 after a MatrixCamera
  pick), timer +0x260 = (150 draw + 50) / 100 (0.5..2.0), fade flag +0x264 with draw < 0.5;
  every mode-1 frame with both set the timer -= 0.02 (0x3508A4), below 0 -> fade off. The
  renderer (0x17EE28) draws the other display buffer (the previous picture, already trailed:
  recursive) over the frame with FIX = trunc(128 min(clamp(timer, 0, 1), 0.8))
  (`fn_00209498`). Footage (61.22 s): 0.8^(k+1).
- **Order**: the grade, the dissolve, the trail, then the 2-D layer.
- Ours: `race::DissolveClock` (`RaceScreen::start_dissolve`: mode-1 cuts, `restart_director`,
  the modes 0 / 3 car change, `ReplayDirector::take_dissolve` for records), `ShotLook::step` /
  `trail_weight`; View3D keeps every picture (a GPU copy) and draws the frozen and the last one
  over the graded frame (`DrawOptions::snapshot / dissolve / trail / keep_last`).
  Approximate: the start's frozen picture is our last 3-D picture (the original's frame holds
  the 2-D layer too, e.g. the exit dialog); a new race screen has none, so its first replay or
  demo starts without the dissolve.

**§G8 The replay depth of field** [C unless marked] (`fn_001ACF38`; its lens object at race
+0x97CC, built by `fn_001AC870` in the `RacePS2Base` constructor; the pyramid `fn_001AD440`, the
layers `fn_001AD650`):
- **When** (`RacePS2Base::vf45` 0x17EB0C..0x17EB28): the play mode race +0x9784 is not 0 (replays
  and demonstrations 1 / 3, the attract demo and the quick menu's race 2; §B2, §K1) and the course's
  Env0 +0x10 has bit 8 (0x100); else the plain copy `fn_00244680` (§R5). Only
  vf45 draws it: the 2P halves and the licence menu's preview box (vf48) never. 0x100 is set on 76
  of the 80 US course files; clear on r_seattle, smtnorth, r_smtnorth and L_smtnorth1 (the last
  three hold garbage at +0x930 / +0x934).
- **The lens, every frame** (0x17EB30..0x17EBB4): `fn_001AC948(obj, Env0 +0x934, 1.333
  (0x34FCF0))`: obj +0x0 the focal length in mm, +0x4 the film height 0.001 (0x34FF90) x mm /
  sqrt(1 + 1.333^2) (`rsqrt.s`: a film 1.333 times as wide as high whose diagonal is the focal
  length; 0.0210 m at 35 mm); +0x8 = Env0 +0x930 (the f-number N); +0x10 = max(`CameraManager::vf11`
  0x209EE8 = the current camera's +0xD0, Env0 +0x938) (the focus s); `fn_001AC970` from the
  projection stack's matrix M (stack 1, row vectors): +0xC = 0.5 x film x M11 (the focal length f
  in metres at the shot's zoom), +0x14 = M32 / (M22 + 1) (far), +0x18 = M32 / (M22 - 1) (near).
  The projection (vf45 0x17E840..0x17E8B8): the camera's frustum `fn_002555A8` on the camera
  manager's block (+0x4..+0x1C, set at `fn_00208320`: l, r -320 / 320, t, b 224 / -224, near 0.1
  x 0.7002 (0x350744) x zoom, far Env0 +0x20 (`CameraManager::vf10`), 320 x zoom), then Scale(1,
  1 / 1.098 (0x34FCE8), 1): M11 = zoom x (640 / 448) / 1.098 = zoom / 0.7686, near = 0.07 zoom.
  The constructor's +0x8 2, +0xC 0.035, +0x10 10, +0x14 0.1, +0x18 1000 are overwritten before any
  use, as is the 9.0 that race set-up writes to +0x8 (0x17D6A0). Constants of `fn_001AC870`, never
  changed: +0x1C 8.0 (the largest far blur), +0x20 4.0 (the largest near blur), +0x24 2 (far steps
  per doubling), +0x28 1 (near steps per doubling).
- **The blur unit**: c = film / the 3D work target's height (its descriptor +0xC: 448; the run-time
  descriptors at 0x2F4CF4 hold (0, 49, 640, 448), read in PCSX2). A blur of r lines lies at d = f^2 s / (f^2 + N r c (s - f)) before the focus and
  f^2 s / (f^2 - N r c (s - f)) behind it (EE floats, the product taken as (s - f) r c N; logf /
  expf `fn_0026A388` / `fn_0026A270`, ln 2 = 0x34FF9C / 0x34FFA0).
- **The pyramid** (`fn_001AD440(obj, 3D target)`) in a 640 x 224 PSMCT24 buffer (descriptor obj
  +0x2C, from 0x2F4D68 with PSM 1; no alpha), table 0x330BD0 {x, y, w, h}: level 0 (0, 0, 320, 224)
  from the 3D target, 1 (320, 0, 320, 112) from 0, 2 (320, 112, 160, 112) from 1, 3 (480, 112, 160,
  56) from 2, 4 (480, 168, 160, 56) from 3. One sprite each over the level's rectangle, texture
  coordinates the source rectangle + 0.5 (and `fn_00256360` adds 0.5: +8 in 12.4), bilinear (TEX1
  MMAG / MMIN 1), REGION_CLAMP to the source rectangle, no blending, depth or alpha test. The GS
  samples a pixel at its corner and filters at the coordinate - 0.5, so a halved axis takes the
  mean of 2 texels, a kept axis the mean of texel k and k + 1 (that level moves half a source texel
  right / down): level 0 = 2 x 2 means of the 640 x 448 picture, 1 = (1, 1) / 2 across x 2 down, 2
  = 2 across x (1, 1) / 2 down, 3 = (1, 1) / 2 across x 2 down, 4 = (1, 1) / 2 both ways. Level 4
  is never drawn below.
- **The layers** (`fn_001ACF38`): FRAME the display buffer, ZBUF a PSMZ24 descriptor of the same
  address (`fn_002444D0(sp+0x80, display, 0x31)`) [? the run-time buffer table 0x2F4CF4 is zero in
  the file; the captures show a depth-ordered blur, i.e. the scene's Z]; depth test on with ZTST
  table 0x2FC1F8 [3] = 2 GEQUAL (GS Z grows towards the eye: a layer passes where the scene is at or
  beyond it), no Z writes, fog off, ALPHA 0x44 ((Cs - Cd) As + Cd), FIX 128. Each layer's Z is
  scratchpad +0x16C from `fn_00256B80(d)`: (0, 0, -d, 1) through the projection (VU0 qword 8 =
  stack 1), NDC z clamped to [-1, 1], Z = z x 0xB80 + 0xB90; d = 0 gives the nearest Z (passes
  everywhere).
  1. Near, k = 1 x trunc(log(4 + 1) / ln 2) = 2 down to 0: r = exp(k ln 2 / 1) = 4, 2, 1; d at the
     near formula; d < +0x18 (the near plane): skipped; else drawn at the last drawn d (0 for the
     first) as level k / 1 at alpha (1 - k mod 1) / 1 = 1, and d kept.
  2. The sharp picture: the plain copy `fn_00244680` (blending off, its own filter) at the last
     drawn d (0x1AD138).
  3. Far, k = 0, 1, ...: r = exp(k ln 2 / 2); stop when 8 < r (+0x1C); d at the far formula; stop
     when d > +0x14 (the far plane) or d < 0 (past the hyperfocal distance f^2 / (N c)); drawn at d
     as level k / 2 at alpha ((k + 1) mod 2) / 2, or 1 when that is 0: levels 0, 0, 1, 1, 2, 2, 3 at
     1/2, 1, 1/2, 1, 1/2, 1, 1/2 for 1, 1.41, 2, 2.83, 4, 5.66 and 8 lines.
  Then Z writes and the depth test on (`fn_00252978(1)`, `fn_002513E0(5)`), blending off.
  `fn_001AD650(obj, level, alpha)`: level >= 5 draws nothing; TEX the pyramid, REGION_CLAMP to the
  level's rectangle; alpha 1: blending off, else on; RGBAQ (1, 1, 1, alpha) x 128 (MODULATE: the
  texel, As = alpha); ten 64-pixel-wide sprites over x 0..640, y 0..448 (y x 8 in field mode,
  scratchpad +0x23), texture coordinates (x - 0.5 + i w / 10, y - 0.5) to (x - 0.5 + (i + 1) w / 10,
  y + h - 0.5), + 0.5 in `fn_00256188`: one stretched copy of the level whose picture lands half a
  frame pixel right and down of the pixel it covers.
- **So**: from the eye to d(4) the 4-line level 2, to d(2) level 1, to d(1) level 0, then sharp to
  D(1); behind it level 0 at half, level 0, level 1 at half (a mix with level 0), level 1, ...
  Pixels without depth (the sky and backdrop: the Z reset at step 8 of the frame) take every far
  layer drawn. The 1-line level is already the 2 x 2 mean: no part beyond d(1)..D(1) is as sharp as
  the plain picture.
- **The focus, camera +0xD0**: `DesignerCamera::vf13` (0x202260; the course records; Passline and
  Starting cameras the same, 0x202D18) |aim - eye|, the aim being the record's target curves (kind &
  0xC == 0) or the target state's point +0x40..+0x48 + (0, 0.4 (0x350734), 0); `OnBoardCamera` 100
  (vf9 0x1FFA20, vf24): the player's views, car mounts, a course camera turned on board; Automatic
  (0x203330) and Logger (0x204894) 100; Automount (vf24 100, vf13 0x204640) the eye's distance from
  the car's origin, wobble included, every frame; Matrix (vf24 100, vf13 0x2057EC) sqrt(radius^2 +
  h^2) with h after its rise above the road; Zoomup (vf24 0x205BE4): its 8th draw at set-up, r <
  0.5 the distance past the bumper (2.5 + 4 r), else the full distance from the car's origin (no
  other change). `fn_001FEC58` (a setter) has no caller.
- **Env0** (US): +0x930 N 10 on most courses; 8 laguna, slalom80r; 6 seattle, tahitimaze; 5
  tahitidirt; 12 rome; 4 the circle tests; 3 route5; 17.8 montecarlo. +0x934 35 mm (35.04
  midfield). +0x938 4 m on most; 5 Tahiti; 6 Monte Carlo; 8 Seattle; 10 Midfield; -18.67 Laguna (the
  camera's focus always). +0x20 far 610..3010 m.
- **Checked in PCSX2** (B-5 demonstration, L_grandvalley2a, 25 save states; the object found at EE
  0x1FCDB1C): focal 35, film 0.021, N 10 (9.0 back in the menu), far 1000; f 0.0237 (a tunnel
  shot) .. 0.3107 m (a telephoto); near = 5.124 f in every state, i.e. 0.07 zoom with f = 0.0105
  zoom / 0.7686: the frustum and the 1 / 1.098 scale as above. Ours at the matching shots: f 0.3114
  / focus 93.8 against 0.3107 / 90.3 (the telephoto at the curve), 0.2307 / 27.9 against 0.2333 /
  26.5 (the rear three-quarter shot with the banners); the pictures' blur matches.
- `fn_001ACE00` (with `fn_001AD2F0`, `fn_001ACA00`) is another version (the depth read through a
  Z24 texture at 0x2F4D68, a 320 x 224 pyramid buffer): no caller in this build.
- Ours: `RaceScreen::apply_depth_of_field` (the gate; the 1P full-frame view and the full-frame
  menu race), `CameraRig::focus` / `ProceduralCamera::focus`, `render::dof_layers`,
  `View3D::encode_dof` (the layers depth-tested against the scene's depth; up to 448 lines the
  pyramid at GT3's sizes, wider for wider pictures; taller pictures each level at their own
  resolution with a Gaussian of the level's spread, `render::dof_level_blur`); RACE.md "Replay depth
  of field".

**Attract demo pick** (`fn_0020E930(&mode, &target, r)`, called from `fn_00208320` for replay
mode 2 with one draw of the director's generator) [C]: mode = 0 if r < 0.33 (f32 0x3508F0),
else 2 if r < 0.66 (0x3508F4), else 1; the counter `fn_00218660() + 0x307C8 + 0x4A44` (0 at
start-up) + 1 >= 4 forces mode 1. Target: mode 0 MANUAL; mode 2 int(100 r) mod 3 (MANUAL /
TOP / AUTO); mode 1 AUTO and the counter reset to 0. Then the counter + 1 and the mode stored
at +0x4A40. So mode 1 comes at least every third demo (the fourth after start-up). The colour
filter belongs to mode 1 only (`fn_0020B200` / `fn_0020B380` and the handler run for mode 1
only). Ours: `race::pick_attract_view`, `RaceScreen::attract_view_` (not stored in the
options).

Mode names (fn_00199600, the indicator's art): 0 traditional, 1 avant-garde, 2 broadcast,
3 course line (`replay_mode_crsline`; it draws no line of its own, §L), other values "driver". The indicator
(RaceReplayModeDisplay, race display +0xDC4) draws `replay_mode_l2`, the mode's icon and
`replay_mode_r2` (display.imgs) side by side 1 px apart with alpha min(1, 4 t^2), t falling by
1/60 per frame. It is never shown: its timer (+0x24) is 0 from the constructor (0x199420) and
only `vf4` (0x199470) writes it (down); its mode setter `fn_00199670` has no caller [C]; in the
Replay Theater's demonstrations neither L1 / R1 nor L2 / R2 showed it (captures). Ours had
cycled the mode on both pairs with the indicator for 2 s (removed).

**REPLAY OPTIONS** (ReplayOption, fn_001AF218, opened from the replay pause's REPLAY OPTIONS;
`menu/<LANG>/replay_panel1.pmb` page `replay_option`, `replay_panel2.pmb` `replay_option_2p`):
rows MODE (4), TARGET (MANUAL, TOP, AUTO), ITEM (row 1 easy meter, car name, time; row 2 map,
steering, tyres, accel / brake), SOUND (BGM, SE), 2P also SCREEN (single, split). Up / Down
pick the row (an item-row change keeps the column), Left / Right move the row's cursor
(repeat 30 / 3 frames), Circle / Cross apply: MODE sets the view mode (+0xD0) and re-applies the
target, TARGET sets the mode's target, ITEM toggles bit (row 1: 1 << col, row 2: 1 << (col + 3))
in +0x4C, SOUND toggles BGM (+0x54) / SE (+0x58), SCREEN sets +0x50; a move plays SE 3, an
apply SE 7; Square / Triangle close. Settings live in the global settings block (+0x2FF38 of
fn_00218660(): +0x4C items, +0x50 split, +0x54 BGM, +0x58 SE, +0xD0 mode, +0xD4 + 4 mode the
mode's target). Per mode (table 0x2DF228: allowed-target mask, flag, default target; the
flag makes `fn_0020EBA0` store BGM on (+0x54 = 1), so BGM stays on after leaving the mode
[C]):
traditional {MANUAL} / MANUAL, avant-garde {AUTO} / AUTO with BGM forced on, broadcast
{MANUAL, TOP, AUTO} / AUTO, course line {MANUAL} / MANUAL (driver: mask 0x10). Captured
defaults: traditional, MANUAL, no items, BGM and SE on. Cell colours: the page's quads
color_on ffc800 (value in force), color_off 6b5600, color_disable 2d2300 (a target the mode
does not allow); the focused cell lighter (captured 251,197,0 -> 255,255,89 and 104,84,0 ->
197,177,93). Closing returns to the pause row with REPLAY OPTIONS focused.
The replay mark (`gt_logo_mini`) sits at the bottom right of the 640 x 448 frame (right edge 620,
bottom 421). The game has no wider frame: its 16:9 option draws the same frame anamorphically
(the 3-D projection x 0.75), so the mark ends near the screen's right edge; our wide layout
keeps right-anchored HUD parts at the screen edge for the same reason. The dimmed look of the
user's capture right after the replay starts is the start's 0.5 s dissolve from the last
picture (§G7) [C]. Ours had a dark tint over the 2P replay (removed).
**When the mark shows** (`RaceDisplay::vf4` 0x19360C..0x193694, `fn_00196A38`) [C]: in replays
(kind 1) only while no ITEM is on (the item mask is 0); in the attract demo (kind 2) always; never
live. Its opacity (D+0x58) follows a timer at D+0x50 (`fn_00196928` at the display's set-up,
`fn_00196938` per race step: hidden 180 steps, +1/120 a step to 1.0, shown 1800 steps, -1/120 a
step to 0, hidden 1800 steps, fade in again ...: 3 s off, 2 s in, 30 s on, 2 s out, 30 s off), so
it stands still while the replay is paused (REPLAY OPTIONS included: the race step does not run).
Camera, driver's view, view mode and buttons play no part; every disappearance in the PCSX2
footage fits the timer. Drawn at (620.0, 420.0), right / bottom aligned. Ours:
`race::ReplayMarkTimer`, `Hud::draw_replay_mark(alpha)`.

**§G4 The followed car, the replay pad and pairs** [C unless marked]

- **Order** (`fn_00208BE8`, every frame, per view): the battle table (`fn_0020DAC0`, skipped
  while race +0x5C > 0 [I: before the start]), the interest scores (`fn_0020E1F0`), the aim
  (`fn_001FF068(state, 1)`), then the view mode's handler. Per view: battle table +0x320
  (7 entries of 12 bytes: +0 in a battle, +2 changed this frame, +4 partner, +5 state, +6
  timer, +8 / +9 the last partner / state, +0xA frames since; +0x60 the hold per car, int[7],
  40 at set-up `fn_0020D840`; +0x7C relations 7 x 7; +0x140 / +0x14E order / rank, +0x15C /
  +0x16A the previous frame's), interest +0x574, target state +0x4A0.
- **G4a Battles** (`fn_0020DAC0`; `fn_0020D5E8` squared distance, `fn_0020D6C8` relation,
  `fn_0020D900` search, setters `fn_0020D770` / `D7B8` / `D7D0` / `D7E8` / `D808`). Relation
  (i, j) = 1 when the squared distance is under 100, replaced by the pair byte (conductor
  +0xBA34, +0x84 + 8 i + j: the overtaking / following machine `fn_001F59B0`) when that is
  1..3; for a partner rel(i, p), else rel(p, i). Search(i, min): the best-placed car (lowest
  rank) related by min or more within 20 m (squared 400); failing that, scanning the cars, one
  related by 1 or more, placed lower than the last found and related at least as much, within
  20 m. Each frame every car's hold counts down and a held car is skipped; "frozen" = the
  current camera's vf19 is 0 (`DesignerCamera::vf19` = +0xA80: 0 for a shot's first 120
  frames (+0xA82), 1 after them or while the second car is nearer the camera; on-board
  cameras 1). No partner: the timer runs to 240, then (not frozen) search(i, 1) -> state 1.
  State 1: frozen -> none (flagged); search(i, 2) found another car -> state 2 with it
  (flagged); relation 1: timer + 1, at 121 -> state 3 (flagged); relation 2 / 3 -> state 2
  (flagged); else none (flagged). State 2: over 30 m (squared 900) -> none (flagged); timer
  < 240 -> + 1; else relation not 2 / 3 -> state 3 (not flagged). State 3: over 30 m -> none
  (flagged); frozen -> nothing; another car found -> state 2 with it (flagged); timer + 1, at
  241 -> none (flagged); else relation 2 / 3 -> state 2 (not flagged). The partner
  (`fn_0020D828`) counts in states 2 and 3 only.
- **G4b Interest** (+0x574: 7 x {score, bonus, a counter nothing sets}; +0x54 its own
  generator, +0x58 frames, +0x5C pick, +0x5D the last pick, +0x5E pending). Target set
  (`fn_0020E020`, at set-up and from `fn_0020EBA0`): the generator from the hardware counter,
  pick = last = the followed car, frames 0; MANUAL 1200 for the followed car, TOP 1200 for
  the car in first place, AUTO the rank's base (table 0x2DF108 per rank 0..5: base 400 / 300
  / 200 / 100 / 100 / 100; rate 1/60, 0.01, 1/150, 0.0056667, 0.0046667, 1/300; factor 1.5,
  0.95, 0.85, 0.75, 0.7, 0.65). Each frame: frames + 1; MANUAL / TOP: the same scores (TOP by
  the table's ranks), bonus 0; AUTO: the followed car and its partner - 1/30 (0x3508E0),
  every other car a draw (`fn_0026C118` + `fn_0026C178`) under its rate: + 10; a place gained
  since the last frame + 150, one lost - 100; clamped 0..1200; bonus = (state 1: 100, 2: 400,
  3: 200) x factor[rank]. Then, unless pending: f = (frames - 240) / 1560 (>= 0), f > 1 ->
  look; every 30 frames a draw (`fn_0026C178`) under f -> look. Look: the valid car
  (`fn_001E0668` == 3) with the highest score + bonus above the pick's becomes the pick,
  pending. Applied (`fn_0020E790`): last = pick, pending 0, frames 0, the pick + 200, every
  other car x 0.98 / 0.84 / 0.95 by the pick's state 1 / 2 / 3 (else x 1), the left car back
  to its rank's base, clamped. So TOP follows the leader, never sooner than 240 frames after
  the last switch (or target set), certainly within 1800.
- **G4c Handlers.** Mode 2 (`fn_0020BE58`): MANUAL: Up / Down the next / previous car
  (`fn_002099A0` / `fn_00209900`: the car list's links +0xC / +0x10, wrapping at the list's
  +0x18 / +0x14, skipping invalid cars) [I: entry order, as the index walkers `fn_00209A40` /
  `fn_00209B00`]. TOP / AUTO: while the course camera's shot is 3 s old or more (its vf18:
  +0xF0, 180 at every set or turn on board, counted down per frame), the pick is pending, the
  followed car's hold is out and `fn_0020ABF0(..., 0)` (§G5) -> the pick (`fn_0020E790`). A
  new car (validated by `fn_00209A40`): its partner (valid) or alone, target set with 120
  (`fn_001FF490` / `fn_001FF4F8`), hold 120; a manual change sets the state's +0x36 and forces
  a new shot (the picker's bits 1 and 0: search first); an AUTO / TOP switch keeps the shot
  unless it ends it (`vf17` for the new target, §G5) or the new car is far off the view
  (`fn_00202760`, on the car's unblended aim +0x64..+0x6C, the record's eye at the last t, the
  shown angles +0x5A0 / +0x9F0 and zoom +0x88: the yaw more than 180 / zoom degrees off, or the
  pitch more than 135 / zoom (90 / zoom is about the view's angle); never in the shot's first
  frame, on board, or with an on-board record; debug "ilegal target reset."), which search
  first. Otherwise, when the followed car's battle state changed this
  frame: target set again with its partner (or alone), 120, hold 120. Cross: the driver's
  view word (+0x96E0 per view; 0 at set-up) toggles; on, `fn_002096C0` puts the live driving
  camera (+0x600, the view the player last drove with, settings +0x11C) on the followed car,
  at that car's own GTCI mount (in-car mount 0, chase mount 1: `OnBoardCamera::vf9` keeps the
  target's car, `fn_00201150` reads its model's table), the followed car not drawn in the in-car view;
  off, the course shots (a new one). Modes 0 / 3 (`fn_0020B8E8`): Up / Down as above, target
  set with 15, hold 15, never a partner (0x20BB44), +0x36 set, the 1 s dissolve; Cross as mode 2.
  Forced (§G5) only in the driver's view, in the start hold or while a move ran (0x20BAEC,
  0x20BB04, 0x20BBAC): else a shot the new car is inside of goes on and the aim moves there. The attract demo (race +0x3C -> +0x9784 ==
  2, race +0x7C == 0): a counter (+0x960C) + 1 a frame; over 299, and `fn_0020ABF0(..., 1)`:
  0, the next car (a simulated Up) and a new shot. Mode 1 (`fn_0020C580`): Cross, Circle, R1,
  L1 cut as the timer does; the cut's car with its partner, except a cut of request 1 (the
  timer without music) alone, at once (1 frame); a battle change of the followed car: target
  set with its partner, 120, no hold. Mode 1's Up / Down: + / - 0.1 (0..1) on +0x9630 (stored
  at settings +0x114), the engine sound level (0x3521E4) in the music's sections marked by its
  event 9 (`+0x162D` / `+0x162E`; event 8 restores 1.0); Left / Right: + / - 1.1 clamped 0..1
  on +0x9634 (settings +0x118), the music-timed cuts on (>= 0.5) or off (`fn_0020B1D8`; the
  attract demo sets 0). Up (0x100000) +0.1 (0x350898), Down (0x200000) -0.1; Right (0x40000)
  +1.1 (0x3508A0), on; Left (0x80000) -1.1, off; both clamped 0..1 and stored in the options
  every time (a new game 0.1 / 0); a change of the switch posts race event 0x15 (the mode
  indicator), drained unread. No song on the disc has tracks 8 / 9, so the level never
  applies (the race-sound level 0x3521E4 stays 1.0) and the switch changes only the request
  value (3 or 4: the dissolve's odds). Ours: `CutClock::step_level / step_music_cuts`,
  `[options] replay_level / replay_music_cuts`, `game::ps2_replay_pad` (+0x114 / +0x118). 2P with SCREEN split: every view
  mode 0, MANUAL (0x208D5C).
- **G4d The pad** (`RaceInput::vf10` 0x210AA0, while the input's +0x130 is 1..3; pad word
  Up 0x1000, Down 0x4000, Left 0x8000, Right 0x2000, Triangle 0x10, Circle 0x20, Cross 0x40,
  Square 0x80, L2 0x1, R2 0x2, L1 0x4, R1 0x8, as `MenuControl::vf10` and the pressure order
  of `Control::vf10` confirm): Up 0x100000, Down 0x200000, Left 0x80000, Right 0x40000, Cross
  0x400000 | 0x2000000, Circle 0x4000000, L1 0x10000000, R1 0x8000000; the handlers take new
  presses (`Control::vf6`). L2, R2, Square, Triangle, Select and the sticks do nothing in a
  replay; Start opens its pause. The same pad drives demonstrations (play mode 2): Cross gives the
  driver's view in a licence / time-trial DEMONSTRATION too (checked in PCSX2, B-1).
- **G4e Aim and pairs** (target state +0x4A0: +8 car, +0xA second car, +0x16 pair). A target
  set (`fn_001FF400`) keeps the last car (or pair) at +0xA4 / +0xA8 and starts a move of
  min(n, 60) frames (+0xD0 / +0xD2); `fn_001FF068` aims at the car (`fn_001FEE18`) or the
  midpoint of the pair with the first car's track distance (`fn_001FEF10`), blended with the
  last target's (live) aim by e = 3 t^2 - 2 t^3, t = frames left / total (curve
  `fn_00206330(0, 0, 3)` from the constructor `fn_001FED38`). Another car (and +0x92 1) also sets +0x34 (one frame,
  §G5's "target set") and clears +0x92; every target set clears the manual flag +0x36.
  `fn_001FF490` / `fn_001FF4F8` answer whether a move was still running: then modes 0, 2 and 3
  force the shot's re-set (§G5, 0x20BBAC / 0x20C110 / 0x20C268). A new shot ends the move: when
  the picker answers 1 (or is forced) modes 0, 2 and 3 call `fn_001FF3D8` (+0xD0 = 0 and the aim
  again, 0x20BD6C / 0x20C39C; the re-set itself 0x20AEFC, before it), mode 1 at every cut
  (0x20D148), REPLAY OPTIONS' apply too, so the shot's first frame aims at the new target.
  +0x36 is cleared only as +0xD0 counts down to 0, so a move a shot ended leaves it set until
  the next target set. Mode 1's search sets the target of every car it tries, so +0x92 is
  cleared whenever another car was tried. `DesignerCamera::vf13`
  (0x201D6C..0x201FD0) widens a pair's zoom: need = 1.1 (0x33E408) x the cars' yaw apart
  (`fn_00207248`, `fn_00206260`) + the angle of 5 m at the second car's distance (when it is
  the nearer one: that distance - 60 x the difference, at least 1); a view narrower than need
  (its angle 90 / (zoom x 0.5)) takes zoom x 0.5 = 90 / need, at most 70 degrees (0x350738 =
  90 / 70), then the record's zoom limits [C for the steps; I for the units: ours compares
  horizontal view angles]. The limits (0x201FA4..0x201FD0) always apply: the zoom is capped at
  +0x2C (max), then raised to +0x28 (min), so min = max fixes it and min > max gives min (the
  game then only prints "zoom min-max value" to the debug log). On the US disc 147 of the 4493
  race-course records have min = max (all zoom by distance; Grand Valley's start record 0:
  zoom(0) = 0, min = max = 22.904, a telephoto of the grid; Deep Forest's 9.51 at ~105 m, the
  reverse's 7.60, both zoom(0) near 1) and 28 min > max [C]. [Ours had applied them only when
  max > min: Grand Valley's start shot fell to our 0.2 floor, a fish-eye from 125 m, and Deep
  Forest's zoomed ~x105 into the road (issue 306).] The mode-2 on-board cut is mounted on the second car (§G).
- **G4g The camera operator** (`DesignerCamera::vf13` 0x201830; 2026-11-20, public issue 370) [C].
  Per frame, A = the aim point (target state +0x40..+0x48 + (0, 0.4, 0), or the record's
  target curves), E the eye; yaw = (pi - atan2(dx, dz)) x 57.29578 (`fn_00207248`), pitch =
  -atan2(dy, |dxz|) x 57.29578 (`fn_00207208`, both with atan2 `fn_00245C10`), d = A - E.
  s6 = (kind & 0xC) == 0 (target curves). The view is Rz(roll) Rx(pitch') Ry(yaw') T(-E)
  (0x20230C..0x202330) with the filtered angles below.
  - **First frame** (+0xDC == 0, cleared by vf25 at every record set): k = 30 / zoom (the
    record's, after its limits); targets yaw_t, pitch_t = yaw, pitch, except: not s6 and not
    kind 0x400: when the target state's +0x92 is 1, the angles to its +0x94..+0x9C (the aim
    point last frame without the lift: `fn_001FF380` copies +0x40.. there every frame after
    the cameras, `fn_00208BE8` 0x2090B0; `fn_001FF400` clears +0x92 when the target becomes
    another car); not s6 and +0xA5A: + (+0xA60, +0xA64). Then `fn_00206CC0`(+0xA28) (the bob,
    below), yaw' = `fn_00206710`(+0x188, yaw, yaw_t, k, mode 0, 1.0), pitch' =
    `fn_00206710`(+0x5D8, pitch, pitch_t, k, mode !s6, 1.0), the zoom `fn_00206B30`(+0xA3C,
    +0xA58 ? +0xA5C : zoom).
  - **Later frames**: zoom' = `fn_00206B40`(+0xA3C, zoom, p) with p = `fn_001FF5F8` (1 with a
    pair (+0x16), else 1 - +0xD0 / +0xD2 while the aim's move runs, else 1); k = 30 / zoom'.
    Not s6: yaw' = `fn_00206808`(+0x188, yaw, k, 1.0, t35), pitch' = `fn_00206808`(+0x5D8,
    pitch, k, 1.0, t35), t35 = the target state's +0x0 is 3 or 5 (only the constructor
    writes it, 0, in replays: t35 never holds there [I]). Then a = the yaw filter's speed
    (+0x188+0x424) clamped to +-30, `fn_00206E68`(+0xA28, a / 30), and when not t35 and not
    s6, pitch' += ((n(+0xA28) / 3) min(k, 5)) a / 30. s6: yaw' = yaw, pitch' = pitch.
  - **Filter** (0x450 bytes): +0x0 count (64-bit, at most 128), +0x8.. the last 128 target
    angles, +0x208.. their changes (`fn_00206520`: the new one wrap180(new - previous) once two
    are known), +0x408 the curve `fn_00206330`(1, 0, 2) = ((0 t - 0.5) t + 1) t 2, +0x418 the
    angle shown, +0x41C k, +0x420 mode, +0x424 speed, +0x428 its noise. Reset (`fn_00206710`):
    c = cur; mode 1: c -= scale ((k 0.4) cos(c pi / 180)); +0x418 = c; the history cleared
    with the target (`fn_002064B8`), then c pushed; speed = wrap180(c - target); noise reset;
    returns c. Step (`fn_00206808`): noise step (amp 1.0); mode 1: theta -= ((scale 0.4) k)
    cos(theta pi / 180); push theta; L = lead (`fn_002065A8`: d = 5 newest change, theta + d,
    then + k P(d / 3k) for d > 0, - k P(-d / 3k) for d < 0, P the curve on t clamped 0..1);
    e = wrap180(theta - shown) + (L - theta); g = (n + 1) 0.5 + 0.2 (t35: 1); dv = 2 ((g 300)
    e + v (-30)) / 60, clamped to +-25 unless t35; v += dv within +-250; shown =
    wrap360(shown + v / 60) (`fn_002062B0`: whole turns truncated; below 0 as 360 - (|x| mod
    360)). wrap180 is `fn_00206260` (one turn either way past +-180). cos is `fn_0026A1A0`.
  - **Noise** (`fn_00206CC0` reset: x, target, v, counters 0, generator = the hardware
    counter `fn_002502C8`; `fn_00206E68` step(amp)): `fn_00206D08`: a draw (step + unit); the
    first two calls after a pick only count; then: amp = min(|amp|, 1), p = amp x -0.1575
    (0x350820) + 0.99333 (0x350824); p < r: a step + unit draw (unused) and a unit draw r3,
    target = ((r3 0.8 + 0.2) 256) - 128, frames 0. Then frames = max(frames, 1); v += (60
    frames (target - x) + v (-10)) / 60; x += v / 60; the value `fn_00206E50` = x / 128.
  - **Zoom spring** (`fn_00206B40`): d = (want - x) 30, x p when x < want; v += (d + v (-10)) /
    60, then v <= 10 and >= -30; x += v / 60.
  - **Checked live** (2026-11-20, PCSX2, US 1.10, licence B-5 DEMO, 14 save states over 4 shots,
    the course camera at EE 0x1FC4B10): every shot looked at the car (kind 0x24), both filters
    ran (pitch mode 1, yaw mode 0), the history's newest yaw equals the raw yaw (+0xA8C), k =
    30 / zoom, the zoom equals the spring's value (wanted 6.725, shown 6.765), the noise values
    stay within -76.8 .. 128; the yaw shown was up to 4.4 degrees off the car (k 3.95: about
    half the half-width of the picture). Unclamped, a step gives v = 10 g e (the -30 v term
    cancels the old speed): v matched 10 g e from the state's own history, shown angle, k and
    noise in 28 of 28 samples (yaw and pitch) to 0.0003.
  - Ours: `race::CourseCameraAim` (EE arithmetic, host atan2 for the raw angles), stepped at
    60 Hz in `ReplayDirector::evaluate` and blended between frames; `ReplayDirector::update`
    keeps the last aim point (forgotten at a new followed car), `set_aim_progress` from
    `RaceScreen::aim_replay_camera`. Tests `course_camera_aim_*`, `replay_director_course_aim`.
- **G4f Training** (mode 3): the mode-0 handler; the camera manager treats 0 and 3 alike
  (both skip the battle re-set). Nothing outside the camera manager reads its per-view mode,
  and the stored mode (settings +0xD0) only ReplayOption, `fn_0020EB38` and `fn_0020EBA0`;
  the per-view event record's mode (view +0x300, +0x10 with flag +3, `fn_0020EE60`) is never
  read (only its +0x302, by `RacePS2Base::vf29`). No race-line drawing tied to the mode was
  found [I: a capture of a Training replay would settle it].
- **Ours** (`race::ReplayFocus`, `RaceScreen::step_replay_focus` / `replay_pad` /
  `aim_replay_camera`, `ReplayDirector::set_pair` / `pair_angle`): as above; the pair bytes
  are not reproduced (relation 1 only), "frozen" = a course shot's first 2 s, cars always
  valid; the battles wait for the race start [I].

**Pop-ups in replays** (§I, [C]): the re-run posts the lap times, split times, race gaps and the
rest as live (not the time trials' gaps to the best lap); the event display at (320, 130) shows
those of the car the cameras follow at that moment (MANUAL's chosen car, or whichever car TOP,
AUTO or an avant-garde cut puts the camera on), in every camera view, and only with ITEM time
on (the captured default is no items). The replay layout's second event display (outside
views, (615, 400)) is never posted to. In demonstrations the event display is the in-car
view's only. The camera angle thus changes which car's pop-ups are shown, not whether they
are drawn (outside a demonstration).

## H. Meters: the speedometer scale [C]

`fn_001D9978` (the meter set-up): N = int(v_top x 1.1 / 22.2222) + 1 (f32 0x350130 / 0x350134;
v_top = car +0xDF8, m/s), below 2 -> 2, 5 or more -> 4; the scale = N x 80 in km/h or N x 50 in
mph (the units word `[0x2F59D8]`, 0 = km/h). The same function returns the tachometer scale
(rev limit +0x1C4 + 1500) / 1000 x 1000. `fn_0019DF08` picks the face: the first entry of
{limit, face} with scale <= limit: km/h 0x32F9E8 {180, 240, 320, FLT_MAX -> the 320 face}, mph
0x32F998 {120, 160, 200, FLT_MAX -> the 200 face}; the needle's full scale (+0x1C) = the scale
(so N = 2 draws 160 km/h on the 180 face, 100 mph on the 120 face); the band step (+0x64) 10.0
(km/h) or 5.0 (mph). Ours: `Hud::speed_scale` (the mph scale was 40 + 40 N with a 10 mph step
before).

## I. In-race pop-ups: lap time, splits, gaps (the event display) [C unless marked]

The HUD's event display (`RaceEventDisplay`, race display `D+0x3BC`; per 2P half its own) shows
short messages in the middle of the screen. The conductor posts display events on the race
queue (`fn_001901B8`: post(race+0x84, id, packed arg, prio 2); per-frame queue, read by
`RaceDisplay::vf3` 0x192A58 for the display's followed car `D+0x8000+0x2469`):

| id | event (pack: vf2) | posted by |
|---|---|---|
| 0xB | LapTime {car 4 bits, lap 10 bits} | `0x1EC620` from every conductor's line-crossing handler (vf18), the licence pass |
| 0xC | CheckPoint {car, lap, checkpoint} | `0x1EC520` from vf16 (every checkpoint crossing) |
| 0xE | GeneralTime {car, time ms, max 0xFFFFFFF} | with 0xB (the lap time) and 0xC (the split since the lap start) |
| 0xF | TimeDiff {car, signed ms, 28 bits} | race gaps `fn_001EF268`; time trial / free run vs the best lap `0x1EC758` / `0x1EC818` |
| 0x10 | LapDiff {car, signed laps} | `fn_001EF920` |
| 0x11 | Speed {car, value x 10} | machine top-speed test at the goal (`fn_001EC8C0` from vf19, §B3) |
| 0x12 | Information {car, kind} | pit-in and similar (`fn_001941C0`) |

**Checkpoints** (`DynamicsConductor::vf33/vf34`): the course's GTRW markers (+0x22 count,
+0x24 table of {u16 kind, u16 b, f32 distance}) with kind 2 and b 0, in table order;
count = markers + 1 (the line), at most 4, so at most 3 checkpoints; checkpoint index
`count - 1` is the line. Every race course has 2-3, licence courses 0-4 (checked on the 80 US
courses). Crossing (`fn_001EE978`, the crossing handler): only the car's next checkpoint
(+0x492, reset to 0 at the line), only once the lap counter (+0x434) is non-zero and the
car has not ended (+0x4E0); prev < d <= cur on track distance; time = clock before the
tick + trunc(f x 50) (f = (d - prev) / (cur - prev)), ms = units / 3 (as the line, §B9);
split = ms - lap start. Then `vf15` (gap recorder, lap index = laps completed, checkpoint
index), then `vf16`. At the line: `vf15` with index = checkpoints passed, then `vf18`
(not for the start crossing, lap counter 0 -> `vf17`).

**What the display shows** (all through the event display's message queue, below):
- **Lap time** (`0x193118` -> `RaceDisplay::vf21` 0x1944A0): every LapTime of the followed car
  (every lap of races, 2P, time trials, free runs, the finishing lap included): the lap time
  "m'ss.mmm" (`fn_0019A7F0`) in `sw_20`, green 0x802BCC49 = (73, 204, 43), 3.0 s, queued
  (delay 0). Licence tests: `RaceLicenseDisplay::vf21` 0x18C6E8 shows it only when the
  display's play mode `D+0xC` (race display init arg 4) is non-zero, i.e. in replays only.
- **Split** (`0x193260`): every CheckPoint of the followed car: `"T%d %s"` (0x351FF0) with
  checkpoint + 1 and the split since the lap start, `sw_16`, green, 3.0 s, queued. Races, 2P,
  time trials, free runs and licence tests alike (the licence conductor's vf16 0x1F0D90 posts
  lap 1 and checkpoint (lap - 1) x count + index; its line handler vf18 0x1F0ED8, unless the
  crossing ends a type 3 test, posts lap = the lap counter and checkpoint lap x count - 1, so a
  lap test's laps show as the splits "T count", "T 2 count", ...; the licence keeps the lap
  start F +0x530 where its vf17 or GO put it, so its splits are the time since the run's start).
- **Gap** (`0x194010` -> `fn_001945B0`): every TimeDiff of the followed car: sign '+' for
  diff >= 0 or the glyph 0x7F (the font's dash) for < 0, then "m'ss.mmm" of |diff|; `sw_16`;
  colour (240, 0, 0) for >= 0, (128, 128, 255) (0x80FF8080) for < 0; 3.0 s, delay -3 (in the
  current parallel group, below). With the other car's icon: diff >= 0 -> the car in race
  position 1, < 0 -> position 2 (`fn_001930D0`: the car list index whose position byte +0x1465
  is 1 / 2; in qualifying `D+0x2474` the leader of the last frame `D+0x246D`); with an icon the
  text is offset (+37, +25) and the icon (car icon display `D+0xA43C`, 64 x 32, centred) at
  (-62, +20) for >= 0, mirrored (-37, +25) / (+62, +20) for < 0; no icon (no such car):
  text at (0, +25). The icon: 3.0 s with the message's start delay, blink counter 72 (-1 per
  frame from the post): hidden while (counter / 12) is odd (frames 1-12, 25-36, 49-60 after
  the post), drawn white (0x80FFFFFF). One icon object: a new gap moves it.
- **Lap down** (`0x193F58` -> `fn_00194500`): LapDiff: '+' (or 0x7F if < 0) and
  `RACEDISPLAY_LapDiffInfo` ("%d Laps", US) of |laps|, red, `sw_16`, 3.0 s, delay -3, at (0, +25). The text is the
  sprintf format: PAL's texts have no "%d" ("Laps", "Tours", "Runden", "Vueltas", "Giri"), so PAL shows "+Laps" without
  the count (PAL 0x195408); NTSC-J's program has no `LapDiffInfo` slot and prints a fixed "%d Laps" (0x192EB8).
- Information (pit-in "%s Pitting in...", ...) and the machine test's speed ("%.1f%s"):
  green `sw_16`, 3.0 s, queued; "Replay save complete": the same. The speed (`fn_001946E0`):
  the posted value (already in km/h or mph) printed "%.1f" (rounded) with the unit
  `fn_002458A0` ("km/h" / "mph", no space). Never seen live: the MMAX layout has no event
  display (§B3 machine-test HUD).
- **Machine tests** (§B3): splits at their checkpoints (M1000: T1 at 400 m; MMAX: T1, T2 at
  400 / 1000 m), at the goal the time as a lap time (M400 / M1000) or the top speed; nothing
  at the line and no gaps (vf5 skips the gap recorder, vf18 is empty). Only M400 / M1000
  show them: MMAX's are queued but its HUD draws no event display. Ours posts none for MMAX.
- (The GeneralTime of a lap / split is looked up as the queue's first 0xE event with the car
  filter; when another car's came first that frame, nothing is shown. Not reproduced.)

**Race gaps** (`fn_001EF920` per crossing = vf15 of SinglePlayer / Battle2P / BattleMP; and
`fn_001EF268` = their vf1, run in the conductor tick right after all cars' crossings
(0x1F3E38 -> vf1), so in the same frame) [C]. Per checkpoint index k (0..3; reset by
`fn_001EE7E8`: lead lap -1, counts 0, times "no time"): a lead lap L[k], up to 8 entries
{time, car} in arrival order, a count n[k] and a processed count p[k]:
- crossing (car, lap, k, t): lap > L[k]: a new group: L[k] = lap, entry 0 = (t, car), n = 1,
  p = 0; lap == L[k]: inserted by time among the unprocessed entries (ties after; the program
  shifts only the times, not the car bytes - an original bug for two cars in one tick out of
  time order, not reproduced), n + 1; lap < L[k]: LapDiff (car, L[k] - lap) at once.
- vf1, every tick: for each k, entries i = p..n-1: i = 0 nothing; i >= 1: TimeDiff (car_i,
  t_i - t_0) (the gap to the first car through, always positive) - and for i = 1 also
  TimeDiff (car_0, t_0 - t_1) (the first car's lead over the second, negative). (`fn_001EC960`..
  `fn_001EC978`, the hooks for the first / last car and the gap to the car directly ahead,
  are empty.) Then p = n.
- So a car behind sees "+gap to the car that was first through"; the leader sees "-lead" when
  the second car arrives; a car a lap (or more) down sees "+N Laps".

**Time trial / free run** (`DynamicsConductorTimeTrial` / `FreeRun` vf16 / vf18) [C]: no race
gaps; at a checkpoint TimeDiff = split - the best lap's split (`race+0x8AC[k]`), at the line
lap time - the best lap (`race+0x8BC`), posted before this lap updates the best; none when
the reference is "no time", in replays / demos (`race+0x8000+0x37AC`) or for a car with
+0x14A2 set. The best lap {splits[4], lap} (`race+0x8AC`, 0x14 bytes) is replaced by the car's
lap ({splits so far, lap}, car +0x11DC) when faster (`fn_0017B810`), for every allowed lap
(`fn_001EC4E8`: car +0x136E clear, not a replay, race +0x7C and +0x6C clear). Its initial
value is "no time" (0x157529FF, splits and lap: the organisation's constructor 0x17A088 ->
`fn_0017B778` -> `fn_0017B7A0`), and nothing else writes it: `fn_0017B810` (callers
`fn_0017AF10` / `fn_001786E8`, from 0x1EC620) is the only update; the setters `fn_0017B258` /
`fn_0017B260` / `fn_0017B2B0` and the offer `fn_0017B290` have no callers (0x2B4780.. is the
program's per-function descriptor table, not a command table). So no stored record seeds it:
a session's first lap has no gap. The organisation is built once per visit, before the run
loop (`fn_001798A0` in every driver: time trial 0x130A50, free run 0x12FF48, machine test
0x131690, licence 0x132030, arcade race 0x12F840, GT race 0x132BA0, championship 0x133388,
2P 0x133F48; their loops branch back after it), and no per-run step touches +0x8AC, so the
best lap of the visit is kept across retries (TRY AGAIN, START from the quick menu, pause
RETRY) and starts again from "no time" on the next visit. After a time-trial fail the car's
crossings post nothing: the stop driver's hand-over marks it ended (F+0x4E0, `fn_001EACB8`) and
the crossing handler skips an ended car (0x1EEDF8); a FAIL does not touch `race+0x6C` (§B3).
TT vf18 posts TimeDiff, then LapTime; the display shows the lap time first, then the gap.

**The event display** (`RaceEventDisplay`, 0x197F08) [C]: 8 message slots
(`RaceMessageDisplay`, 0x88 bytes: elapsed +0x18, duration +0x1C, fade in +0x20 / out +0x24 = 0,
blink +0x2C = 0) and two timers, queue end Q (+0x28) and group start G (+0x24), both -1/60 per
frame, floored at 0 (vf4 0x1980F8; slots: elapsed += 1/60 while < duration). Add
(`fn_001981E8(E, text, duration, delay)`): the first free slot (elapsed >= duration; none
free: dropped) takes the current colour (+0x20), font (+0x2C), position (+0x8/+0xA) plus
offset (+0x18/+0x1C, both reset to 0 after every add), max width (+0xC), alignment
(+0x10/+0x11); delay 0: start = Q, Q = Q + duration (a queue: one after the other); delay
!= 0: start = max(delay, 0) + G, Q = max(Q, start + duration), G = start (in parallel with the
group). The slot's elapsed = -start; it draws while 0 <= elapsed <= duration, at full alpha
(no fades), text pen at (x aligned, y) with y the baseline; the x scale min(1, max width /
width) when the max width is set. Layout (racedetail type 7, `fn_00195168`): (320, 130)
centred in every one-player race mode (max width 0, 320 or 400 by mode); 2P horizontal
(racemode 7, 640 x 224 halves) (320, 75) max width 320; 2P vertical (racemode 8, 426.667 x
448) (213, 165) max width 384.

**When it is drawn** (`RaceDisplay::vf4` 0x1934B8) [C]: inside the item-4 ("time") block of the
item mask (§K3: `andi s4, 4` / `beqz` at 0x19375C / 0x193760 jumps past the times, the lap
count, the P.P. icon, the event display and the gap's car picture D+0xA43C to the meters at
0x1939CC), then under the layer bit 0x10 (0x19396C). Live play's mask is every bit but 0x10 and
0x40, so the display always draws there; in replays and demonstrations the mask is the REPLAY
OPTIONS ITEM bits (`[GT +0x2FF38 +0x4C]`, default 0 per the captures): the pop-ups show only
with ITEM time on. The layer byte D+0x8000+0x246C is 0x1F from the constructor (0x190C74);
its setters (vf11..vf15: bits 1, 2, 4, 8, 0x10) are called only from the data logger's menus
(`LoggerRoot` / `LoggerSector` / `LoggerReplayControl` vf18, `LoggerTop` vf15) and
`RaceSplitDisplay::vf15` (the logger's split display forwarding to its halves), so no race,
replay or demonstration clears a bit [C, every `lh a0, 0x78(vt)` call site checked].

**Which car** [C]: the handlers take only the display's followed car's events (D+0x2469; the
filter `fn_001902B0` with a2 = that car: lap time 0x193118, split 0x193260, lap down 0x193F58,
gap 0x194010, speed `fn_00194110`); D+0x2469 is the current camera view's target car
(CameraManager view block +0x4A8, the view `+0x8000+0x16D8`, 0x24C0 a view), stored every
frame before vf3 by `RaceSinglePlayer::vf29` 0x181EC4 (and `RaceLinkBattle::vf29`,
`RaceSolitaire::vf29`) through the display's vf9. So in a replay the pop-ups belong to the car
the cameras follow at that moment (MANUAL: the chosen car; TOP / AUTO and the avant-garde cuts:
whichever car the camera is on, §G). The lap-time list and Last instead take the events of
D+0x246B (the display's own car, `fn_00197A10` at 0x1931D4), as do the Information kinds other
than pit-in (kind 1 "%s Pitting in..." has no car test).

**Which camera view** [C]: the layout's view D+0x10 = (current camera's vf14 != 0) ? 1 : 0
(display vf10 0x192128, from the same vf29 with the view block's camera +0x5D4).
`OnBoardCamera::vf14` returns its mount index (+0x100, set by `fn_002004D0`), so only mount 0
(the in-car view) is view 0; `DesignerCamera::vf14` its mount when it rides a car (+0xA78 ==
3), else -1; every other camera (Automatic, Automount, Heli, Matrix, Zoomup, Passline,
Starting, Logger) -1. Replay on-board shots on mounts 3 and up (§G mode 1 kind 4, mode 2's
automatic on-board of the mount types 0x10 / 9) are outside views.

**Replays (play kind 1 / 3, layout 11)**: the event display (320, 130), centred, max width 400,
in every view; the second event display (type 25, D+0x838; (615, 400) right-aligned, pushed,
bottom-aligned, width 320, outside views only) is never posted to: every add (`fn_001981E8`) is
on D+0x3BC, only D+0x3BC is ticked (vf3 0x192CA4), and D+0x838 appears only in the constructor,
destructor, the layout builder (`fn_00194888` / `fn_00194BF8`) and vf4's draw (under layer bit
8 with the car name, not item-masked) [C]: it draws nothing. What a replay posts (the race is
re-run, its conductor posting as live) [C]: lap times and GeneralTime (0x1EC620: no play test),
splits (0x1EC520), race gaps and laps down (`fn_001EF920` / `fn_001EF268`: no test), pit-in
(`fn_001EDB40` -> `fn_001ED9F0`: no play test found), the machine tests' splits, goal time and top speed
(MT vf19: the 0x37AC test skips only the record check `fn_001EFDE8`); the time trials' and free
runs' gaps to the best lap are not posted (0x1EC758 / 0x1EC818 test the conductor's
+0x8000+0x37AC), and no lap counts for the session (`fn_001EC4E8`: not a replay). The licence
pass (`fn_001EF5C0`) posts LapTime {car, 1} with the run's time in replays too (0x1EF744, after
skipping the stored result for 0x37AC), and `RaceLicenseDisplay::vf21` shows it there (D+0xC !=
0): the licence replay's pass time in sw_20, green; the fail handler posts none. The top-speed
test's splits and top speed ("%.1f%s", `fn_001946E0`), queued but never drawn live (its layout
has no event display), show in its replays. An ended car's crossings post nothing (0x1EEDF8),
as live.

**Demonstrations (play kind 2, layout 12)**: the event display (320, 130) in the in-car view
(view 0) only, item-masked as in replays; the second event display at (610, 425) right-aligned,
width 570, every view, never posted to [C]. So a demonstration shows pop-ups only with ITEM
time on and an in-car (mount 0) camera. The quick menu's backdrop race (play mode 2, §A8c)
uses this layout, and the attract demo (replay mode 2, §G) [I: its play kind not traced].

Ours: `race::PassingOrder`, `race::BestLapSplits`, `race::EventDisplay` (`race/lap_events.h`),
`track::LapTimer` checkpoint crossings (`checkpoints()`, `crossings()`), the field's
`last_crossings()`; `RaceScreen::lap_events` feeds the halves' event displays, `Hud` draws
them (`Hud::event_row`: the time item, the layout's row for the view); `race::MachineTest` the
machine tests' (§B3). Replays: `race::lap_event_rules` (what a run posts per play kind),
`race::event_display_of` (the followed car, `RaceScreen::replay_focus_`), the machine test's
marks re-run in playback (`race::machine_speed_message` at the top-speed goal), a solo run's
end in playback (its laps done at the line, or the recorded end-of-run driver kind once
timing began), the licence's pass time at the live run's end time (`music_license_end_s_`;
not for a replay without its live run, e.g. from the theater); `draw_replay_items` draws them
with the car picture. Not reproduced: the demonstration layout (ours draws no pop-ups in
demonstrations or the attract demo), 2P replays (ours draws no HUD on either half; the
original keeps the 2P layout with SCREEN split, item-masked), the qualifying icon rule. The
replay HUD's other values follow the display's car too (`RaceDisplay::vf3` loads D+0x2469,
-1 -> car 0, and reads every gauge, lamp, the steering +0x14A7, pedals +0x13AC / +0x13B0,
speed, gear, odometers and the entry's laps from that car; vf4 passes it to the map, whose
loop draws it last in the own-car colour): `RaceScreen::followed_car_hud` (issue 324).

## J. Loading a race: the loading screen [C]

There is no course picture, logo or "Now Loading" text while a race loads. The loading
thread (`fn_001012E0`) draws the current "loading object" (`fn_00101978` sets the global
0x350B14) every frame over the last picture, without clearing, until the race object itself
becomes the loading object (`RacePS2Base::vf42` 0x17D3D8: `vf9` start, `vf44` car icons, then
`fn_00101978(race)`: the race appears as a cut):
- **From the menus**: the menu control ends with its transition (`fn_001CB4B8`, type
  RenderBase +0x40 = 0 for every control: the Fadeout, §C1, menus.md §6); the race drivers
  then build the race and load the course (`fn_001798A0`, RaceCourse etc.) with the Fadeout
  still the loading object: the last menu frame keeps darkening (colour 12 at alpha 12/128
  per frame, towards (12, 12, 12)). The "still loading" cursor (`fn_00102170`, every transition
  type): a counter set to 180 at the start (`TransitionObject::vf5`), -1 per drawn frame; at
  0 a blink counter + 1 per frame, phase = (blink >> 5) & 1: phase 0 = grey 0.9 (229, 229,
  229; f32 0x34FA00 x 255), phase 1 = black; a 16 x 16 square (20, 20)-(36, 36) of the
  640 x 448 frame. So it first lights 180 frames (3 s) after the menu was left, stays lit 31
  frames, then alternates 32 black / 32 lit while the load lasts. No minimum time: the race
  appears when loaded.
- **During `vf44`** the car icon maker is the loading object (it renders the 64 x 32 icons
  of every car, then clears the loading object and wakes the race thread): nothing new is
  drawn on the picture.
- **Retry inside a run** (pause RETRY, exit reason 4: `vf42` loops): the DelayFadeout (type 1)
  over the last race frame (nothing for 10 frames, then the 0.1 grey wash) while the run
  restarts, then the cut. **Start / retry from a quick menu**: the quick-menu loop ends with
  `fn_00101978(0)` (no loading object): the last quick-menu frame stays as it is until the
  race appears. **Back to the menus**: the DelayFadeout (§C1).
- Ours: the race screen launched from the menus draws the menu's frozen frame and its
  running Fadeout (handed over through `menu::flow::race_loading_picture()`) until its run is
  loaded, catching up the frames the load blocked, with the cursor (`ScreenTransition`); the
  load runs over two frames (set-up, then the course and cars), so a picture is shown before
  each blocking part. Our loads take a fraction of a second, so the cursor (3 s) is normally
  never reached, as on a fast original load. Before, the race screen's own course / car picker
  was drawn for the frame before the load (it stayed on screen while loading).

## K. The race display's layout per mode [C unless marked]

### K1. Which mode a run uses
- The race classes store the display mode at race +0x8000+0x488C (`fn_00181598(label)`:
  `fn_00239650` looks the label up in the racemode table 0x2F00C0): `RaceBasic`'s constructor
  0x180F98 `RACE_MODE_SINGLE` (kept by `RaceArcade` 0x182260 (arcade race driver 0x12F840, GT
  race driver 0x132BA0, the attract demo `RaceArcadeDemo`), `RaceLinkBattle` 0x184E00 (i.LINK
  0x100DB8), `RaceChampionship` (0x133388)); `RaceFreePractice` 0x182F40 `FREE_PRACTICE` (the
  GT race / championship drivers' pre-race "Free Run", 0x132BA0 / 0x133388); `RaceSplitBattle`
  0x183480 `2P_BATTLE` (0x133F48); `RaceTimeTrial` 0x188088 `TIME_TRIAL` (0x130A50);
  `RaceFreeRun` 0x189138 `TIME_ATTACK` (arcade and GT free run 0x12FF48); `RaceLicense`
  0x189CA0 `LICENSE`; `RaceMachineTest::vf9` 0x18CD30 `MAX_SPEED_CHALLENGE` for MMAX, else
  `0_400` (§B3). The event record's own race-mode label (`RACE_MODE_ENDULANCE` on the
  `E…` events) is not used for this. The setter `fn_00181798` has no callers.
- `RaceBasic::vf9` 0x181078: display `vf1(organisation, mode, kind)` with kind = the race's
  play kind (race +0x8000+0x1784, passed by `RacePS2Base::vf43`: 0 live, 1 / 3 replay, 2
  demonstration [I: the names]), then display `vf5(kind == 0)` = D+0x8000+0x246F, read by
  `vf6`: the position
  and the message display (WRONG WAY, AUTO DRIVE) are drawn only live.
- `RaceDisplay::vf1` 0x1918D8: D+4 = the mode; 2P (7 or 8) becomes 8 with the 16:9 option
  `[0x2F424C]`, else 7; any other mode but `FREE_PRACTICE` becomes `ENDULANCE` (D+0xA475 = 1)
  when the event's laps byte (+0x199) is 10 or more or its time limit byte (+0x1E4) is not 0
  (on the disc: the GT All Stars (15 laps), Polyphony Cup (20), turbo cups (10 / 15), the
  `E…` endurance races and the attract `demo_*` events (20); no licence, time trial, free run
  or machine test event). D+8 = the layout used; D+0xA474 = (mode == `FREE_PRACTICE`).
- `fn_00192010` (from vf1, and from vf3 when the 2P replay screen option changes): kind 1 / 3
  (replay): D+0xA477 = 1 for modes 4 / 9; 2P keeps its mode when the REPLAY OPTIONS SCREEN is
  split (`[GT +0x2FF38 +0x50]`), else `REPLAY` (11); every other mode `REPLAY`. Kind 2: 2P
  keeps its mode, every other `DEMO` (12). Kind 0: unchanged.
- Modes 3 (`FREE_TIME_TRIAL`), 5 (`0_1000`) and 10 have no rows and are never picked; mode 8's
  label is not a known string; 15 (`LOGGER`) is the data logger's own display
  (`RaceSolitaire::vf9` 0x187598, a `RaceSplitDisplay` at race +0xCDE0).

### K2. The layout builder
- `fn_00194888(D, view)` (vf1 with view 0, vf3 every frame with D+0x10): when the view
  changed, clear every object's active flag (`fn_00194BF8`), then for every row of the mode
  (`racedetail.db`: +8 type u16, +0xA sub u16, +0xC v-align u8, +0x12 h-align u8, +0x18 view
  s16 (127 = any; 0 the in-car view, 1 the outside views), +0x1A x, +0x1C y, +0x1E w, +0x20 h)
  whose view matches: the type's case (jump table 0x32E0D0); `fn_00195168` sets the object's
  position (+8 / +0xA), size (+0xC / +0xE), alignments (+0x10 h, +0x11 v) and active (+0x12).
- Types: 1 rank D+0x84 (scale w x 0.01, 0x34FD7C); 2 lap count D+0x60; 3 sub < 0x40 the
  meter preset `fn_00194FF8` (0 in-car: tachometer D+0xE7C, digital speed unit offset (-44,
  20), gear shift-light offset (-35, 0), back panel shape 0x41; 1 outside: D+0x3F3C, (5, 0),
  (-4, -12), shape 0x40; positions from the tables 0x2BC080 / 0x2BC120), sub >= 0x40 the back
  panel D+0xA1D0 alone with that shape; 4 boost D+0xA0BC; 5 tyre gauge D+0xA2BC only when
  D+0xA472 (wear on); 6 message display D+0xCB4; 7 event display D+0x3BC; 8 licence target
  line D+0x254 + 0x48 sub (their icons the `mark_1st..3rd` images); 9 mini-map D+0x20 (scale
  w x 0.01, 0x34FD80); 10 D+0xA3FC (unused); 11 a time line (below); 12 steering D+0xA29C; 13
  accel / brake D+0xA274; 14-17 tachometers / speedometer D+0xE7C, D+0x3F3C, D+0x6FFC, D+0x7040;
  18 digital speed D+0xA17C (unit offset = (w, h)); 19 gear D+0xA1A0 (sub & 1: offsets (-4,
  -12), else (-35, 0)); 20 / 21 odometers D+0xA10C / D+0xA134; 22 lamps D+0xA15C; 23 the
  car-name message display D+0xD3C; 24 the replay view-mode display D+0xDC4; 25 the second
  event display D+0x838; 26 sub 0 the title message display D+0xA2EC, sub != 0 D+0xA374.
- Type 11 (`fn_00194DF0`, jump table 0x32E140 by sub - 1; labels from 0x2C444C..0x2C4474):
  1 the lap-time list D+0xA4; 2 D+0xEC "Total"; 3 / 6 D+0x134 "Best"; 4 D+0x1C4 "Last"; 5
  D+0x20C "Lap"; 7 D+0x17C "Record"; 8 D+0x17C "Record Lap"; 9 D+0x17C with the car icon
  D+0xA414 (its label stays vf1's 0x2C4448 "P.P."); 10 D+0x20C "Time"; 11 D+0x32C "Record"
  (speed); 12 D+0x374 "Best" (speed); 13 D+0x17C "Fastest"; 14 D+0xEC "Time".

### K3. What the parts show (`RaceDisplay::vf3` 0x192A58, `vf4` 0x1934B8)
- **Item mask** (vf4): `LOGGER` all; live (D+0xC == 0) all but 0x10 and 0x40; replays and
  demonstrations the REPLAY OPTIONS ITEM bits (`[GT +0x2FF38 +0x4C]`): 1 easy meter (digital
  speed, back panel, the current tachometer, speedometer, boost, lamps, odometers, gear), 2 car
  name (D+0xD3C), 4 times (lap count, time lines, licence targets, the P.P. icon, and the event
  display with the gap's car picture: §I), 8 map, 0x10 steering, 0x20 tyres, 0x40 accel /
  brake. The second event display (under layer bit 8, never posted to) and the title displays
  are not masked. (Corrected: the event display had been listed as unmasked.)
- **View** (D+0x10, `fn_00194888`'s argument): 0 when the current camera is an on-board camera
  on mount 0 (the in-car view), else 1 (§I "Which camera view").
- **Values** (vf3, `fn_00193040`, the event handlers): Best = car +0x11F0 (the best allowed
  lap; the entry's, kept for the visit, §A8a); Lap / Time (D+0x20C) = the conductor's HUD lap time vf29 (`fn_001EB2F8`, §B9); Total
  and "Time" (D+0xEC) = car +0x220 (the sum of the allowed laps, `fn_0017B508` from 0x1EC620)
  + vf29, or +0x220 alone once the followed car has finished (D+0xA471, set by vf17 on its
  finish event 5). A time-trial FAIL is no finish: `DynamicsConductorTimeTrial::vf13`
  (0x1F1E50) tests the car's failure bits (+0x14A2: 2 hit, 1 course-out) and calls
  `fn_001F1BB8`, which posts event 0xA (when +0xF84+0x3E8 is 0) and calls `fn_001EACB8(car,
  2)`: that sets "ended" (+0xF84+0x4E0) and the stop driver, never the finished flag +0x1464
  (no store to it anywhere but the reset) nor event 5. The race clock (race +0x8000+0x6380,
  `DynamicsConductor::vf14` +50 a tick unless race +0x5C, the start count) runs on, so the
  time trial's vf29 (0x1F1B80: +0x1464 ? the best +0x16C4 : `fn_001EF8F8` = 0 before the
  first crossing, else clock / 3 - lap start) keeps counting and Time = the laps + the
  running lap after a FAIL; the Lap line is switched off at the finish where the list or Last is on;
  Last = the followed car's latest LapTime event (0x193118); the list takes the same events
  (`fn_00197A10`: a ring of 8, a count); Record / Record Lap / Fastest / P.P. (D+0x17C) =
  race organisation +0x8BC every frame: the session's fastest allowed lap of any car
  (0x1EC620 -> `fn_0017AF10` -> `fn_0017B810` for every car's lap in every conductor;
  "no time" at first, no stored record; kept across the visit's retries, §I), so these lines
  show dashes until the first lap is done.
- **Time line** (`RaceTimeDisplay::vf5` 0x197468): the value in sw_16 right-aligned at x
  (+ 106.667 for align 5, - for 4, 0x34FD94); the label in sw_12, its right edge max(value
  width, 90) + 10 left of the value's; with an icon (+0x1C: the P.P. car picture, the licence
  marks) the icon right-aligned at that edge + the row's w, centred on y + the row's h, and the
  label left of the icon. The edge is truncated (x - trunc(max(value width, 90) + 10)); the
  icon is drawn at its own size (`fn_0019A148`). The licence target lines (type 8: gold, silver,
  bronze at (615, 140 / 168 / 196), w 8, h -7) are such lines with an empty label and the
  `mark_1st..3rd` icons. The P.P. line's icon: the car in position 1 at set-up
  (`fn_001943E8`), then the car of each new fastest lap (the free-practice conductor's vf18
  posts Information kind 2, `fn_00194420`), which also sets the label to "".
- **Lap-time list** (`RaceLapTimesDisplay::vf5` 0x197760): N lines (+0x20 = the conductor's
  laps vf30 when 1..3, else 3, set in vf1), the last N of the ring oldest first, dashes for
  lines not yet run; lines (sw_16 line height) x 0.8 (0x34FD98) apart; the lap numbers "%d"
  (sw_12) from count - N + 1 (at least 1), right-aligned max(90, widest value) + 10 left of
  the values.
- **Lap count** (`RaceLapDisplay::vf5` 0x196B00): always pushed right in 16:9 (0x34FD90),
  whatever the row's alignment; "%d/%d" (total > 0) or "%d" in sw_20 (lap clamped to 9999),
  "Lap" in sw_16 10 left of it. Lap = `fn_001EAB08` (the lap counter, at least 1, minus 1 once
  the car has ended); total = the conductor's vf30: the event's laps (single player, licence),
  1 (time trial 0x1F1B78), 0 (free run, free practice, machine test).
- **Rank** (`RaceRankDisplay::vf5` 0x196D08): rank#1 at the row's scale, glyph spacing 3,
  the shadow 0x60000000 at (+4, +4) first, at the raw row position.
- **Map** (`fn_0019E420`): the matrix scaled by the row's scale, the image placed by
  `fn_0019A348` with the row's alignment [I: the scale applies to the position too, which
  would put the 2P maps at 70 % of their rows].
- **Message displays** (`RaceMessageDisplay::vf5` 0x197BF0): the font spec's scale, the text
  squeezed in x to the row's width (+0xC) when wider, x aligned, y the pen; "@name" texts are
  images placed with both alignments (`fn_0019A148`).
- **Title** (D+0xA2EC, vf1): the course name in capitals (game text, a–z capitalised byte by
  byte, escapes untouched: changelog 2026-10-28), "sw_20*160,160", 0x80D0D0D0, at the
  title row (its width the squeeze limit); FINISH there too (vf20). D+0xA374: QUALIFY (live
  free practice) or DEMONSTRATION / LIVE / REPLAY (other kinds), erased for kinds 0 and 1 right
  after (0x191DB4): no "REPLAY" banner is ever shown. Timing (vf1, by the conductor's vf9; the
  clock +0x18 starts at -1.0 / -0.2 and gains 1/60 per race step while under the duration +0x1C;
  visible while 0 <= t <= duration; alpha = clamp(f^2), f = t / fade-in, then (duration - t) /
  fade-out): 1 (the base conductor with initial speed 0: standing races, standing licence and
  machine tests, 2P) -> shown from 1 s for 4 s (fades 0.3 / 1.0), the event display blocked for
  5 s; 0 / 2 / 3 (moving starts; the free run, time trial and free practice conductors) -> from
  0.2 s for 2 s (fades 0.1 / 0.3), blocked 3 s. Set by every race set-up of kinds 0 and 1
  (`RaceBasic::vf9`): live runs, the automatic replay, the REPLAY item, Replay Theater files and
  demonstrations, the licence DEMONSTRATION; never kind 2 (attract demo, quick-menu backdrops:
  `vf1` skips it for kind >= 2). Camera-independent and not masked by ITEM; a course name
  starting with '-' gives none. The countdown (`fn_001925E8`: "@count%d" at 3 / 2 / 1 s left of the
  hold, 1.0 s each, only when D+0xC == 0), "@start" (vf19) and "@finish" (vf20) are written into
  this same object, so in a live run the name is gone once one shows: a race's 10 s hold lets it
  play out ("3" at 7 s), a licence test's 4 s hold writes "3" at 1 s and the name never shows
  (PCSX2 capture of B-3's live start: no title); replays and demonstrations keep it (no count).
  Not a licence rule: `RaceLicenseDisplay::vf1` runs the base set-up, then only clears its own
  fields (`fn_0018BF50`). Footage (PCSX2, Super Speedway): replay 1 shows it 1.0-5.0 s
  after its first frame, as the live start does (the start shot runs on ~2.5 s after it) [C].
  Ours: `race::TitleClock` (`RaceScreen::start_hud_clocks`, `long_title`; stopped live once the
  countdown or FINISH shows), the course table's name (`title_name_`), drawn by `Hud::draw_title`
  live and in replays whatever ITEM says (2P halves: none [?]).
- **Digital speed** (0x1970E0): "%3d" right-aligned at the pushed x, the unit (sw_12) at (+w,
  +h). **Gear** (0x196E98): centred in its box, pen at the cell's top. **Accel / brake**
  (0x198940): `brake_gage` at the pushed x, `accel_gage` at the pushed x + w (16 x 50), each a
  grey 0x80202020 bar filled from the bottom (0x800000F0 / 0x802BCC49) by the value (car
  +0x13B0 / +0x13AC), the art on top, bottoms at y. **Steering** (0x198B90): a grey tick from
  (0, -24) to (0, -43) about the pivot (pushed x, y; 0x32E878 x 24), then `steering` (47 x 66:
  the wheel, its red mark in rows 0-19 over the tick, its hub at row 43) placed by `fn_0019A148`
  with alignments 2 / 0 at (0, -43), i.e. centred across with its top edge there, turned by -90
  degrees x the value (car +0x14A7 / 100) about the pivot: the wheel turns about its hub.
  **Odometers** (`RaceOdometer::vf5` 0x197258): "%06d" / "%04d" in the odo font, colour
  0x80C0C062, the width measured once and cached, x aligned by the row (`fn_0019A730`), y the
  pen. The meter presets' rows (`fn_00195230` fills 0x2BC080 / 0x2BC120): in-car odometer (205,
  428) and trip (205, 370) centred (2); outside odometer (605, 424) and trip (525, 424)
  right-aligned and pushed (5), lamps (620, 415) 125 x 10 (5), back panel (620, 430) 136 x 30.
  **Replay view
  mode** (0x199490): the three images side by side, 1 apart, alpha min(1, 4 t^2).
- "Time Limit" / "%dsec." (`fn_00192678`, sw_12 at (8, 400) / (8, 416); "GAME OVER" sw_20 at
  (234, 224) at 0): live only, when organisation +0x64 (race information vf7, the attract
  demo's limit) is set; not the events' time limit [I]. Not reproduced.

### K4. The layouts (rows in table order; empty rows left out)

| Mode | Rows |
|---|---|
| 0 single | rank (40, 64) 100; lap count (615, 48); messages (320, 305); events (320, 130); map (16, 80) 100; Best (615, 84); meters 0 (view 0), 1 (view 1); title (320, 160) w 500; tyres (20, 435); Last (615, 112); Lap (615, 140) |
| 1 endurance | Total (615, 84); Best (615, 112); list (615, 140); Lap (615, 216); meters; map (16, 96); rank (40, 60); lap count (615, 48); messages (320, 305); events (320, 130) w 320; title (320, 160) w 400; tyres (20, 435) |
| 2 licence | Best (615, 84); Time (615, 112); meters; map (16, 32); targets (615, 140 / 168 / 196) icon (8, -7); title (320, 160) w 500; events (320, 130). No lap count, position or message display |
| 4 0-400 m | §B3 |
| 6 free run | meters; map (16, 16); lap count; Record Lap (84); Best (112); list (140); Lap (216); messages; events w 400; title w 500; tyres |
| 7 2P (4:3) | rank (24, 48) 80; lap count (615, 36); messages (320, 200); events (320, 75) w 320; map (16, 80) 70; digital speed (440, 210) unit (3, 0); bar tachometer (605, 208) 48 x 25; gear (520, 195); back panel (610, 211) 128 x 31; Fastest (615, 64); Last (615, 88); Lap (615, 112); title (320, 120); steering (150, 188); tyres (20, 212); accel / brake (68, 212) |
| 8 2P (16:9) | rank (16, 60) 80; lap count (310, 60); messages (213, 384); events (213, 165) w 384; map (10, 105) 70; digital speed (220, 435); bar tachometer (413, 435) 65 x 25; gear (313, 422); Fastest (413, 96); Last (413, 120); Lap (413, 144); title (213, 210) w 360; steering (40, 307); tyres (20, 392); accel / brake (68, 392) |
| 9 top speed | §B3 |
| 11 replay | map (16, 16); lap count (615, 48); view mode (20, 32); car name view 0 (16, 290) w 300 left, view 1 (615, 433) w 320 right; Lap (615, 84); meters 0 (view 0); steering (40, 350); tyres (20, 435); accel / brake (68, 435); title (320, 160) w 500; events 2 view 1 (615, 400) w 320; events (320, 130) w 400; title 1 (320, 220) w 400; view 1: digital speed (170, 433) unit (5, 0), gear (250, 420), back panel (210, 436) 70 x 30 |
| 12 demonstration | view mode (20, 32); events 2 (610, 425) w 570; events view 0 (320, 130) |
| 13 free practice | meters; rank (40, 60); lap count; P.P. (84) icon (-99, -4); Best (112); list (140); Lap (216); map (16, 96); messages; events; title (320, 150) w 480; tyres; title 1 (320, 220) w 400 |
| 14 time trial | meters; map (16, 32); lap count; Best (84); Time (Total, 112); messages; events; title w 500; tyres |

Right-hand rows are right-aligned and pushed (align 5) except 2P's (align 1) and the
endurance / replay lap counts (0, which the lap count ignores); the map, tyres and replay
small cluster are pushed left (4).

### K5. Ours
`race::hud_layout` (the table; test `race_hud_layout_disc` compares every row with the
disc), `race::hud_mode` (K1; test `race_hud_mode_select`), `Hud::draw` (rows per mode and
view, the item mask `HudData::items`, live-only parts; `Hud::event_row`: the event display
under the time item, test `race_replay_event_display_rows`), `RaceScreen::hud_data` (the run's
mode and values: `lap_lists_`, `session_best_ms_`). Not reproduced or unconfirmed: the event
display's 5 s / 3 s hold at a start (K3 Title); the second event display (never posted
to: nothing to draw, §I); the demonstration layout (the attract demo and theater demonstrations keep ours:
the mark only); the free practice's event display gaps (to the session's best lap of any car in
GT3: event 15; ours to the player's own best lap; its notes and P.P. line: gt_mode.md §8.7); the 2P map position (the scale question above) and the 2P meters (measured on
captures); the licence marks' 75 % size; the replay view-mode indicator (ours at (28, 397),
matched by playing: the row (20, 32) plus the images' own offsets [I]).

## L. The licence course line (course slot 27) [C unless marked]

The only race line the program draws is a model in the course file: slot 27 (course file +0x6C),
on 13 licence course files (format: `docs/formats/course.md` "Course line").

- **Load.** The course relocation `fn_001A8D18` (from `fn_0018F930`, `RaceBase::vf9`) turns
  +0x6C into a pointer and relocates it as a GTM1 (`fn_00224600`), like slots 1, 21-24.
- **Draw.** `fn_001A93B0` (course, visibility list, flags; from `fn_001A92E8`, called by
  `RacePS2Base::vf48`, `vf49` and `vf54` for the views that draw the course) draws slot 1 (+0x4)
  node by node from the visibility list (or whole when there is none), then the BLBd billboards
  (+0x78) when flags bit 0 is set, then **slot 27 whole** (`fn_00225028`: begin `fn_00224F18`,
  every node `fn_002250D8`, end `fn_00224FE8`) whenever +0x6C is non-zero. There is no other
  condition: no race, mode, replay or camera flag is tested, and no code writes +0x6C after the
  load (all stores of 0x6C offsets checked). So the line shows in the live run, the
  demonstration, the licence menu's preview [I: same course draw] and every replay of those
  courses, in every replay view mode, and never on a course without the slot.
- **State.** `fn_00224F18` sets the default state once per model (depth test per the global
  flag, ATE on GREATER 0x20, DATE off, culling on, ALPHA 0x44, depth write on, FBMSK 0, AFAIL
  KEEP); `fn_002250D8` does not reset it between nodes. Slot 27's node 0 is `2E 25 24
  000000FF 00` (op 0x24 is decoded by `fn_00225508` as `fn_00252950(~word)`, which stores
  `~arg`: FBMSK = 0xFF000000), node 1 `2B 00 04 00 00`. The line is therefore drawn without
  culling (its triangles face down in the strip order), without depth writes, colour only;
  PRIM 0x2C: opaque, Gouraud, fogged, untextured.
- **On / off per licence test** = the event's course label (table 32 +0x08): B-3 `L_deepforest1a`,
  B-4 `L_deepforest1b`, B-5 `L_grandvalley2a`, B-6 `L_grandvalley2b`, B-7 `L_deepforest2a`,
  B-8 `L_trialmt1`, A-1 `L_grandvalley1`, A-2 `L_laguna1`, A-3 `L_midfield1`, A-6 `L_midfield3`,
  A-7 `L_seattle2`, A-8 `L_midfield2` have it; B-1, B-2 (`L_testcourse1`), A-4, A-5
  (`L_circle*_dry1`) and every IB, IA, S and R test do not. No event field, demo file or timer
  switches it during a test (none is read by the draw).
- **Replay view mode 3** ("course line", `replay_mode_crsline`; the US manual's "Training ...
  playback with visible race lines"): it draws nothing extra. The per-view mode (camera manager
  +0x96E4) is read only by the camera code (`fn_00208BE8` dispatches mode 1 to `fn_0020C580`,
  mode 2 to `fn_0020BE58`, modes 0 and 3 alike to `fn_0020B8E8`, which tests `mode == 0 || mode
  == 3` the same way twice) and by ReplayOption; the getter `fn_0020A090` has no caller; the
  stored mode (settings +0xD0) is read by ReplayOption, `fn_0020EB38` and `fn_0020EBA0` (apply
  mode and target: the camera resets for modes 0, 1 and 3, the BGM flag of the mode table
  0x2DF228, whose row for mode 3 equals mode 0's {1, 0, 0}); the per-view change record's mode
  (view +0x300 +0x10, flag +3, set by `fn_0020EE60`) is never read. So in the US 1.10 program
  Training looks exactly like Normal; its lines are only the slot 27 lines, which Normal shows
  too. Nothing draws the AI lines (slot 31) or a driven path: the AI line evaluator
  (`fn_001FA018`) has no caller outside the driver and conductor code.
- **Ours**: `course::course_line_model` (gtm1 `Options::carry_state` applies node 0's state),
  drawn by `race::CourseScene::add_items` right after the scenery in every view of the course
  (in-order, after the scenery's blended passes, frame alpha kept, never distance-culled);
  the merged course model (`build_course_model`, tools and the course browser) includes it by
  default. Before, the slot was merged into the scenery with each node's own default state, so
  the downward-facing strips were culled and the line never showed. The billboards are drawn in
  the sorted blended phase, so ours draws them after the line, the game before it [I: no
  visible difference expected, sprites stand beside the road]. Tests `course_line_all_us`,
  `course_line_synthetic`, `gtm1_state_carried_between_nodes`, `race_license_course_line_disc`.

## M. Course lighting: the VU colour programs [C unless marked]

`fn_002512D0` (from `fn_00250820`) uploads the VU1 microcode by MPG.
Each packet sends ITOP = its program; VU 0x44 runs XTOP, XITOP, JR vi1. The renderer is an
OpenGL-like state machine at scratchpad 0x70003000 (`fn_002513E0` / `fn_00251670` Enable /
Disable, caps 0x65 + i = light i; `fn_002557E8` Light; `fn_00255898` LightModel;
`fn_0024CCC0` Material; `fn_00254E20` MatrixMode; `fn_002555A8` glFrustum).

### M1. Chunk dispatch (`fn_0024E028`, table 0x344E40, chunk byte 8 bits 0-4)
- 0: no CPU setup, the V4-8 colours as RGBA (0x80 = 1.0 under MODULATE).
- 1 / 0x41 / 0x101 (`fn_0024EC68`): colour = clamp(s (Me + Ma (Ga + sum La of every enabled
  light)) + sum_slots s Md Ld max(N.L, 0) + sum_slots s Ms Ls max(N.H, 0)^p, 0, 255), then trunc;
  alpha the same on w, clamped to t. H = normalize(L + the camera's back axis) in object space
  (`fn_0024EF80`: H = normalize(L - M^-1 (0, 0, -1))). Slots (`fn_0024EE80`): in light order, the
  enabled directional lights with a non-zero diffuse or specular, at most 4; point lights add
  ambient only. Program 0x185 (no specular) when Ms.rgb = 0 or no enabled light has a specular,
  else 0x57 / 0x82 / 0xAF (0-2 slots) or 0xDE / 0x115 / 0x14C (3-4 slots) for p = 4 / 8 / 16
  (shininess < 6 / < 12 / >= 12). Program 0x82 starts its sum with an add instead of a reset
  (an original bug, at most +-1.5 levels). Every lit course material has Ms = 0: courses run 0x185.
- 2 (`fn_0024F3C0` + `fn_0024F348`, program 0x257): sphere map of the packet's texture,
  R = D - 2 (D.N) N (N from the UV slot), R_eye = modelview rotation x R / |D|,
  m = |R_eye + (0, 0, 1)|, s = 0.5 + 0.5 Rx / m, t = 0.5 - 0.5 Ry / m; colour =
  trunc(c_v K (C0 + C1 min(D^.N, 0))), not clamped. On courses K = 1, C0 = 1, C1 = 0.
- 3 (`fn_0024F670` + `fn_0024F600`, program 0x297): a panoramic lookup [M1a]; colour =
  trunc(c_v (C0 + C1 min(D^.N, 0))), no K, no clamp, alpha too.
- 4 (`fn_0024ED50`, programs 0x1AD / 0x1E1 / 0x21A for p 4 / 8 / 16, from the shininess alone at
  +0x9A0: < 6.0, < 12.0, else; no Ms test, so the Ms = 0 materials (shininess ~6.6e-37) of the
  flag-0x104 untextured passes of laguna, akasaka, apricot, midfield and rome run 0x1AD and add
  0): uploads VU 0xC9..0xCB (the
  light directions transposed, one light per lane), 0xCC the viewer in object space, 0xCD /
  0xCE spec slot 0 / 1 x (s, s, s, t). Per vertex: D = P - E, R = D - 2 (D.N) N (N not
  renormalised), c = max(0, L.R / |D|)^p by repeated squaring, RGB = FTOI0(s Ls Ms c), A = 127.
  Only lane x (slot 0) is used. No clamp: PACKED RGBAQ keeps the low 8 bits.
- 5 (program 0x2DB): colour = clamp(A50 + c_v A40): car shadows; not in courses.
- Byte 8 bits 5-7 (m) move the program start by -2m: 0x20 morph (position, UV, colour), 0x40
  morph (position, UV, normal), 0x60 integer positions and 4.12 UVs. Morph weight 0x70003B70
  (VU 0xF2.w), result (1 - w) key0 + w key1; op 0x35 sets it. Byte 9 bit 0: s = 255, bit 1:
  t = 255 (else 128); byte 9 is read only when byte 8 is non-zero.

### M1a. Kind 3 [C]
- Pass setup (`fn_001A8130` -> `fn_001A8178`): `fn_0024F928((0,1,0), (1,0,0))` builds the basis
  B = I (0x4A6930..50) and stores W = the transposed view rotation (0x4A6960..80), the camera
  position camP (0x4A6990) and fwd = (0, 0, -1) W, then `fn_0024F7B8(fwd)`; `fn_0024F7A8` stores
  Env0 +0xEC0 / +0xEC4 (4 / 0.25 on 76 files, 6 / 0.3 on the four akasaka files).
- `fn_0024F7B8(d)`: S = dx + dy + dz (a bug: all three multiply-adds use basis row 0; meant B d),
  g = `fn_00255AE0`(0, S) = 0 if S > 0 else 4 (S = 0: 0/0), 0x3530C8 = -(g + 1) / 8,
  phi = 45 (g + 1) degrees (`fn_00257A44`), Y = rows (cos, 0, -sin), (0, 1, 0), (sin, 0, cos),
  F = W Y at 0x4A6900. Node op 0x31 (vec3 p) runs `fn_0024F7B8(p - camP)`; it persists until the
  next op 0x31 or pass setup.
- Per chunk (`fn_0024F670`; `fn_0024F600` uploads VU 0xC9..0xCC): rows = MV3 F (= Y on course
  geometry), 0xCC = the camera in object space; w lanes -0.125, 0.875 - (g + 1) / 8, -EC0 / 6,
  EC4 + 0.5.
- Program 0x297 per vertex: D = P - E, p = 1 / |D| (ERLENG), R = D - 2 (D.N) N,
  r = Rx row0 + Ry row1 + Rz row2; the case from the signs of (r.x - r.z, r.x + r.z): (>=, >=)
  u = r.z / r.x, (<, >=) 2 - r.x / r.z, (<, <) 4 + r.z / r.x, (>=, <) 6 - r.x / r.z (VU DIV);
  y = r.y p, v = y^3 + 2 y; s = 0.875 - (g + 1) / 8 - u / 8, t = 0.5 + EC4 - (EC0 / 6) v; S, T
  as floats (S Q, T Q, Q with Q = 1 / w in the transform), no int step.
- Texture: the packet's record (mat & 0x1FF) - 1 (`fn_0024CF40`); all 1,059 flag-3 packets
  MODULATE, TCC 1, S REPEAT; T REPEAT on 679, REGION_CLAMP rows 0..127 on 380 (akasaka 512x128).
- C1: 0 except op 0x33 before a draw (0.8, 0.75, or alpha-only 0.25..0.75 on akasaka, the
  L_circle tests and route5), reset by op 0x32 0.
- Flag 2 on courses: 1,140 packets use their own texture; 164 (chunks 0x102, apricot, midfield)
  the runtime slot 0x1FF, which is null while the course draws (the reflection-map pass zeroes
  it every frame), so the previous packet's TEX0 stays [I]. E for flags 2 and 3 is the camera
  in object space (0x70003AA0 / 0x4A68F0), not the light model viewer (0x70003A10, flag 4 only).

### M2. Lights and viewer
- `fn_001A8268` (from `fn_001A8130`) enables light i when Env0 +0x10 bit 11 + i is set; record
  +0x00 direction, +0x10 ambient, +0x20 diffuse, +0x30 specular (+0x40 unused); no shade
  factors; global ambient (0, 0, 0, 1) (0x2C2670). It runs after the view matrix and before
  the sky, backdrop and slot 1 in vf45, vf48 and vf54. The car lights (`fn_001AC218` /
  `fn_001A83C8`) overwrite the slots when the cars are drawn. Lamp records light cars only.
- Point-light courses: `fn_001AB7D0(lights, race, car, alpha)` (vf51 / vf52, per car, when GTRW
  +0x21 != 0) picks the lamps around the hint `lh` car+0x14E8 (signed: wheel 0's ground query
  result +0x2C, written by `fn_002280C8` only on a hit as the u16 A of the hit triangle's first
  vertex, so a miss keeps the last hit's value): start = hint - 4, + n once when < 0; scan
  min(n, 9) (9 when n >= 10), index + 1 and - n once when it reaches n. Lights (+0x230, at most
  4) and the disc list (+0x234, at most 6 point lamps) both come from the one weight-sorted list.
  On the disc A = 0xFFFF (lh -1) only on the start-line strip of all ten point-light files ->
  lamps n - 5 .. n - 1, 0 .. 3. Ours: `race::lamp_index`, `race::first_candidate`,
  `RaceScreen::car_lamp_index` (2026-10-24; was read unsigned: cars black for ~4.5 m at the
  Route 11 / Route 5 line).
- LightModel(3) sets the viewer to (0, 2, 0, 1) (0x2C2680) in eye space; `fn_0024EF80` maps it
  and the light directions (normalised) to object space with the rigid inverse of the
  modelview. Eye space is y up (frustum top 224, bottom -224 in `fn_00207E20`; viewport y scale
  -h/2): the viewer is 2 m above the camera. No caller enables the local-viewer pname 1: only
  kind 4 has a local viewer.
- Slot 22 is drawn with modelview Scale(0.01) x view and fog distance x 0.01; the rigid inverse
  does not undo the scale, so its kind-4 viewer sits at 0.0001 camera + 0.02 up [I].
- The car reflection map (`fn_001D8698` -> `fn_001D83A8` -> `fn_001D8288`) draws slot 23
  (translation removed) and slot 5 without a light setup of its own [I: the slots left by the
  frame before].
- The rear-view mirror (vf54) draws Scale(1, 1, -1) x view, re-runs `fn_001A8130` after it
  (lights and viewer in that view; the rigid inverse of an orthonormal matrix with a reflection
  is still its inverse, so the flag-4 viewer is the camera + 2 up as in the main view), uses its
  own fog (`fn_001A81C8`(Env0 + 0xA90, k = 1): RGB +0xA90, A +0xAA0, b +0xAA4; A = 0 gives a
  constant visibility b), the sky slot 24, then slot 1 with the sheen (BLBd on Env0 +0xF38 bit
  0, slot 27), then the cars and the tyre smoke (§Q3). Projection: far 120 m (60 m with the
  pass's third argument), the camera's frustum, then Scale(1, 1 / 1.098,
  1), Scale(0.75, 1, 1) when 0x2F424C is set, Scale(1.4285 r m, m, 1) (r the mirror viewport
  ratio, Race+0xC6F4..+0xC700; m = Race+0xC704) [? runtime values].
- Frame order (vf48): the reflection map (`fn_001D8698`: slots 23 and 5, no light setup); the
  main view with `fn_001A8130`; sky 21, sun, backdrop 22, slot 1 (BLBd, slot 27); vf50 (wet);
  vf51 cars (per car: ambient Env0 +0xEF0 and its lights); vf54 mirror. So the next frame's
  reflection map sees the last car's lights in that pass's eye space [I].
- The Z resets of the main view [C, PCSX2 GS dump, Tahiti Maze in-car, 2026-11-03]: after the
  sky, full-screen sprites write Z = 0 (the far value) and the alpha alone (FBMSK 0x00FFFFFF);
  after the backdrop's last pass (its sea's 0x58 pass included), sprites write Z = 0 alone (TEST
  ATE on, ATST NEVER, AFAIL ZB_ONLY), and slot 1 starts after them. So the backdrop keeps its own
  depth order but never hides the course: the scenery covers it wherever its planes lie. Tahiti
  Maze's backdrop sea (y -1.95, under the whole course) would otherwise show through the lagoon
  shores, whose ground lies below it.

### M3. Fog
Per vertex in every transform kernel (`fn_00253EF0` sets the constants): F = trunc(b' + 0.1 +
(255 - b') min(W, A') / W), b' = clamp(255 b, 0, 255), A' = max(A k, 0) (k = 1, the backdrop
0.01), W = clip w = -z_eye; FTOI4 into the XYZF2 FOG field (2048 added on strip-start vertices
for ADC). The node programs set the fog colour to 0 (op 0x21, saved / restored by 0x22 / 0x23)
around their passes that add to the frame, so those fade to nothing with distance instead of
adding the fog colour a second time: the sheen, the kind 2 / 3 environment passes (US disc:
about 9,600 course draws), and the cars' reflection, lamp-glow (ALPHA 0x48) and glint (0x58)
passes. Ours: `gtm1::Options::node_fog` on course and car models.

### M4. Wet and dirt courses
- vf48: slot 1 (base, then the sheen over the base pass's Ad) -> vf50 (when slot 13 exists: the
  slot-13 mask, slot 9 into a buffer with the Env0 +0xC40 fog, blur, composite with Env0 +0xC20)
  -> cars. The wet routes have no kind-4 chunks.
- No course-type branch in the lighting or draw path: dirt courses differ only by data.

### M5. Ours
`race::SunParams::course_lights` (M2: the course items' `ItemLights`), `gtm1` chunk kind 4 ->
`scene::Material::specular_local` + `kAdditive | kDestAlphaAdd` (the sheen; cars: weighted by
the body's written alpha), `model.vert` (kind 1 clamped 0..255, kind 4 with the viewer and the
byte wrap, the fog per vertex, lit colour and fog linear in screen space), `DrawItem::modelview_scale`
(slot 22), kinds 2 / 3 per vertex (`Primitive::env_kind`, `env_point`: the op-0x31 point of the
chunk's own node; the GS texture kept for slot 0x1FF), the mirror's fog block and slot 24.
Checked on a lossless PCSX2 capture (Super Speedway, grid, bumper view facing the sun): shape
right; with GT3's field of view (§G zoom) the road just ahead is still ~12 % brighter (the sheen
adds ~33 levels flat where the original's falls from ~31 to ~23 towards the bottom of the frame):
see M6 (now clipped). Not yet: the reflection map's light state, program 0x82's bug, the mirror's
projection, op 0x31 persisting across nodes (and culled nodes).

### M6. Clipping at the guard band [C]
The VU1 vertex loops CLIP every transformed vertex against |w|. A strip triangle with a vertex
outside is culled when its three vertices share a flag; otherwise it is CLIPped again on
(x, y, z, w) x vf23 and drawn as it is when none of the three has a flag (partly off screen),
else clipped (the clipper at microcode 0x2DA8: from the pre-lit loop at 0x2238 / 0x2388 / 0x24D8,
the other programs through 0x2D70). vf23 is the packet header's VU 200 (`fn_0024E7F0`: the
renderer state's +0xBB0), (0.5, 0.5, 1, 1), stored once by `fn_002503E0` (the only store found).
So the guard band is |x|, |y| <= 2 w, twice the screen, and z is clipped at the projection's own
near and far planes (the race frustum's near 0.1 x 0.7002 x zoom and far Env0 +0x20, §G).
The clipper keeps a linked list of corners: the clip position, the input ST with the corner's
flags, the colour bytes (ITOF0 of the program's output colour), and the scaled plane distances
(x + w, y + w, z + w) and (x - w, y - w, z - w).
- The list starts as the triangle's vertices in strip order (vertices i, i + 1, i + 2; odd
  triangles are not swapped).
- The planes run in the order -z (near, flag 0x20), +z (far, 0x10), -y (bottom, 0x08), +y (top,
  0x04), -x (left, 0x02), +x (right, 0x01), each only when a corner has its flag.
- Each plane walks the list from its first corner: a corner without the flag is kept, and an edge
  whose two corners differ in it gets a new corner (P_d d_c - P_c d_d) / (d_c - d_d), d the
  plane's distance (the position, ST and colour alike), with the flags of CLIP on its scaled
  position. The kept and new corners in walk order are the new list; fewer than 3: nothing drawn.
- Out: one strip, p0, p(n-1), p1, p(n-2), ... (the first corner, then alternately from the end
  and from the start); each corner's colour + 0.5 then FTOI0 (rounded), ST x Q from its own w,
  XYZF from its own clip position (so its fog F is computed from its own W, not interpolated).
Node op 0x0a (`fn_002250D8` 0x2252D4: u8 n, n vec3 corners, s16 skip; every scenery node of the
US courses starts with one of 8 corners, and so does a car body, e.g. am0007's) is the node's
box test: each corner through vf24..27 (`fn_002486B8`: modelview x projection; vf20 = the renderer state's
+0xBB0, the guard band's (0.5, 0.5, 1, 1)), CLIPped against its w and again x vf20 (the flags
turned round when w < 0). When every corner shares a screen flag (beyond one side, the near or
the far plane) the node is skipped (pc + s16). In the cars' passes (vf51, vf52, vf53 set
0x352AD8 to 1 at entry through `fn_00225C38`, 0 at exit) a node with any corner past the far
plane (guard flag 0x10) is skipped too: a car leaves whole, never cut at the plane. Otherwise
the clip test (capability 0x11) goes on (`fn_002513E0`) when any corner is outside the guard band
or the near / far planes, off (`fn_00251670`) when all are inside, which changes nothing drawn.
Checked against a PCSX2 GS dump of the Super Speedway grid frame (in-car view, 2026-11-02): with
the clipped-away vertices recovered from the clip points (all behind the eye), the five road
triangles under the camera (two strips) come out corner for corner and in the dump's order, and
so do the sheen's colours (14 and 35 at the vertices 23 m ahead, 1 and 3 on the bottom edge). On
that frame about 75 GS triangles are clipped pieces.
Ours: `render::guard_clip` (render/guard_band.h; the dump's five triangles are a unit test) for
`DrawItem::guard_band` items (the course scenery, its moving nodes, the backdrop with its 0.01
modelview scale, the course line, the wet road's slot-13 mask) in views with
`DrawOptions::guard_near` (`CameraRig::gt3_near`: the main views). View3D clips those triangles
on the CPU every frame (40-140 pieces on the arcade grids, ~0.1 ms on an M-series Mac) and draws
the pieces after each primitive's own draw, which leaves those triangles out (model.vert); the
vertex colour of these items is linear in screen space too. Strip order = the indices ascending
(test `course_triangles_in_strip_order_disc`). Not done: the mirror (its projection is runtime
values, M5), VR, the reflection map and the wet road's buffer (slot 9: constant colour and fog,
nothing to see).
Result (arcade grids, the earlier captures): Super Speedway in-car road 1.11 -> 1.04 of the
original, now falling towards the bottom of the frame like it; the rows ahead of the clipped area
are unchanged at 1.03-1.04, a further small offset (candidate: the GS truncates texel x colour >> 7
and each blend where we round). Route 5 dry: under 0.5 % (its mask's alpha varies little); its wet
road stays ~1.1x (not clipping). Every other course and view within 0.01 of before.
The far plane, ours: `DrawOptions::far_clip` = Env0 +0x20 (`CameraRig::gt3_far`; the race's
views, the race behind the quick menu and VR; not the mirror, whose projection keeps GT3's
120 m, or the reflection cube) is the race view's only draw distance (it was 2500 m, with a
6000 m projection). A mesh wholly beyond it is culled; `guard_clip` cuts the +z plane (flag
0x10, after the near plane) for the guard band's items, whose primitives no plane can cut are
skipped on the CPU (`GpuModel::MeshInfo::primitive_spheres`); the other items are cut at it in
model.frag, and the cars (`DrawItem::far_whole`) are dropped whole once a mesh's sphere reaches
past it. The backdrop's plane lies 100 times as far (its modelview scale) and the projection's
far plane with it (`CameraRig::set_gt3_far`): no US backdrop comes near (7.3 km at most, Monte
Carlo, against 61 km; test `race_far_plane_disc`). The tyre smoke's (near, far) test uses the
same far plane. `GT3R_NO_FAR_PLANE=1` draws as before.
Checked: PCSX2 GS dumps of three arcade grids (Super Speedway, Route 5 dry, reverse Trial
Mountain; in-car view, three frames each) have no main-view vertex past 0.9 x the far plane (the
deepest at w 661, 609 and 1,075 m against 1,000, 1,000 and 1,200; the 720 at w ~3,450 on Trial
Mountain are textured passes of the cars, at ten times the cars' own depth [I: their Q is not
1 / w]): there the visibility lists stop short of it [I]. So does the dump of B-3's
demonstration at the Goodyear bridge (L_deepforest1a, far 700 m, 2026-11-02): the deepest vertex
at w 444 m; the trees over the bridge end on its right, where the backdrop shows. Ours draws
those trees out to the far plane (every slot 1 node: the main view applied no visibility list
then; it does since 2026-11-08, §N), in a shot framed tighter than the original's. Ours with and without the plane, every arcade
grid in both views, 7 points of a lap on 19 courses and the B-1 .. B-7 demonstrations (a picture
a second): at most 0.25 % of the pixels change, under 0.05 % but once (distant trees and stands
seen through gaps, Laguna's farthest buildings; B-3's trees over the bridge), the CPU clipping
+0.01 to +0.04 ms on most courses and +0.11 ms at Monte Carlo (610 m) on an M-series Mac.

### M7. The GS's 8-bit arithmetic
The GS computes every pixel in 8-bit integers and truncates, as PCSX2's renderers model it (the
hardware one in its pixel shader, the software one in its scanline code; they differ only in the
fog's rounding and, at the default accuracy, the hardware one's blending):
- The filtered texel is an 8-bit value: bilinear filtering truncates (PCSX2 HW: trunc(t 255 +
  0.05); SW: 4-bit weights, each lerp shifted down).
- TFX MODULATE: (texel x colour) >> 7 per channel, clamped to 255 before the fog, with the colour
  interpolated at more than 8 bits (SW: 7 fraction bits; HW: float).
- Fog (FGE): (F C + (256 - F) FOGCOL) >> 8 (HW: trunc(mix(FOGCOL, C, F / 256)); SW rounds this
  one). F = 255 is not "none": a colour past FOGCOL comes out a level lower, and a pass fogged to
  black (the sheen, §M3) loses a level everywhere.
- Alpha blending: ((A - B) C >> 7) + D, an arithmetic shift (floor), COLCLAMP clamping; PSMCT32
  frame buffers keep the 8 bits (no dither).
Super Speedway grid, in-car view (the PCSX2 GS dump, the road 1.25-23 m ahead): the base pass
(PRIM 0x3C, texture 256 x 128 T4 bilinear, colour (57, 56, 56), F 255, FOGCOL 0x9B5B5B) writes
alpha 0x80; the sheen (PRIM 0x7C, ALPHA 0x58 FIX 0x80, the same texel, colour ~(33, 30, 26) there,
FOGCOL 0) adds Cs Ad >> 7 at Ad = 0x80, exact. Over the road texture's texels this arithmetic
gives a graded road 3.3 % darker than float maths with rounded writes (texel (104, 105, 106):
71, 68, 66 against 73, 71, 68), the gap measured on the captures (1.03-1.04).
Ours: `model.frag` `gs_texel`, `gs_color`, `gs_fog` in the base, sheen, reflection and sphere-map
passes (`render::gs_pixel` on the CPU, test `render_gs_pixel_truncates`); the race views fog
whenever the course has Env0, b = 1 too (§M3: every transform kernel writes F). The blend unit
still rounds Cs x As / Cs x Ad (no frame-buffer reads in SDL_GPU): exact at 0x80, half a level
high on average otherwise (PCSX2's HW renderer at its default blending accuracy rounds these
too).
Result (the arcade grid captures, 8 courses x 2 views): the whole frame 1.015 of the original's
on average before, 0.998 after (-2.0 .. +1.6 %; Akasaka chase 0.965, 0.977 before); Super
Speedway in-car road 1.041 -> 1.012 in every row, Grand Valley 1.024 -> 1.003, Route 5 dry 1.103 ->
1.029; CT32 skies (colour 0x80, no fog: the texel truncation only) +0.9 % -> +0.3 %. Route 5's lit
windows (colours past 0x80): the clamp before the fog brings the 99th percentile from 215 to 185
(the original's 162; median 30.1 on all three). The fog's rounding (HW vs SW) is the one open
choice: HW (ours) reproduces the captures, SW would leave the road ~1.5 % brighter.

GS state on courses: an opaque packet whose program turned ATE off (op 0x1B) is drawn whole
(`gtm1::Options::gs_state`), not cut out by the texel-alpha guess: the GS writes every texel and
the texel alpha only weights later ALPHA 0x58 passes (Route 11's panels, Laguna's banners).
Checked 2026-10-22 against lossless PCSX2 captures: Laguna (the pavilion eaves' Ms = 0 pass:
was white from pow(0, 0), now nothing; the ADVAN / BOSCH wall banners now present), Route 11
(the grid facade with its kind-2 pass: median 52 vs the original's 48, was 99; the orange band
over the sky and the see-through panels were the cut-out panels under their kind-2 pass),
wet Route 5 and Tahiti dirt (same look; the grids are not like for like).

## N. Race frame: draw order of the scenery and the cars [C]

- Slot 1 (`RacePS2Base::vf48` -> `fn_001A92E8`): the camera position (three virtual getters)
  picks the visibility cell (`fn_00226950`); `fn_001A93B0` then runs `fn_00224F18` (render state
  entry: ALPHA 0x44, TEST GREATER 0x20, ...), every node of the cell's u16 list through the node
  interpreter `fn_002250D8`, `fn_00224FE8`, then +0x78 (`fn_00226D40` / `fn_00226EE8` per
  node / `fn_00226EB0`, or `fn_00227100`; the BLBd billboards [I]) and +0x6C (`fn_00225028`, a
  whole model; slot 27 [I]). Draw ops 0x04 / 0x05 reach `fn_0024D048` -> `fn_0024E2B8`, which appends each packet's
  setup (`fn_0024E028`) and data reference (`fn_0024E1B8`) to the one DMA chain at the renderer
  state +0x118: packets reach the GS in node-program order, opaque and blended interleaved; no
  list defers the blended ones.
- vf48 then calls vf50 (the wet road, vtable +0x190) and vf51 (the cars, +0x198), so every
  scenery pass, depth-writing or not, is in the frame before any car pixel.
- **The views' lists** (`fn_001A92E8(course, list, camera, flags)`: no list = `fn_00225028`, the
  whole model): vf49 (the 1P view, from vf45) course +0x8 = slot 2; vf48 slot 4 (+0x10) when its
  a2 is set (`RaceSplitBattle::vf3`, the 2P halves), else slot 2 (the licence menu's preview);
  vf54 (the mirror) slot 7 (+0x1C) when its a2 is set and the slot exists, else slot 3 (+0xC):
  vf45 passes a2 = 0, vf48 a2 = 1. Slot 7 names slot 1's nodes (route11: up to 1343; slot 5
  has 366), so it is not slot 5's list. The same lookup `fn_00226950` serves the reflection
  map (`fn_001D83A8`: slot 6 at the map camera +0x58, drawn by `fn_001D8288` over slot 5 node
  by node) and the wet road (vf50: `fn_001A94C8` slot 9 from slot 10, `fn_001A9578` slot 13
  from slot 14, at the camera's position). `fn_002250D8` returns at once for a node index at or
  past the model's count (+0x10) or with a zero program offset: slot 6 of seattle,
  tahitidirt, tahitimaze (13 US files) names slot 1's nodes, past slot 5's 64 / 146 / 126, and
  those draw nothing. The billboards: `fn_00226EE8(blbd, node)` draws BLBd list `node` (node <
  +0x10), for each node of the same list after the scenery.
- **The lists' order** is not the file's (none of the US slot 2 lists is sorted): blocks of 100
  nodes in ascending block order, each block in the same order of its own (Grand Valley from
  its hairpin: 47, 46, 45, 6, 5, 44, 74, ... then 147, 146, 145, ...). The packets reach the GS
  in that order.
- **What it changes on screen**: the backdrop's depth is reset before slot 1 (§M2), so a node
  the list leaves out would cover the backdrop wherever it lies. Grand Valley from the hairpin
  before the lake (public issue 254; camera about (111.2, 7.7, -17.8)): 127 of 1300 nodes
  listed; the hill about 450 m ahead with the tunnel and the bridge (nodes 265, 289 and others)
  is left out and the backdrop's hills show (the reporter's shot of the original). Ours drew every node
  until 2026-11-08; now `DrawItem::nodes` (docs/dev/RACE.md), with a 0.6 s dithered fade when a
  node joins or leaves a view's list (`race::SceneryFade`; ours only: at our resolution and 16:9
  the instant switch popped buildings in on Route 11). GT3 looks the list up at the camera's
  eye: the camera's +0x90 vector, which `VariableCameraBase` sets as the rigid inverse of its
  matrix (`fn_00258078`) applied to the origin (0x1FE954..0x1FE970), as ours does.
- Laguna (US `laguna`, slot 1): node 575 chunk 539 (PRIM 0x7C, ALPHA 0x44, ATE off, ZWRITE off,
  FBMSK 0xFF000000, mat 6 = set 11 texture 5, vertex alpha 0x70..0x80) spans x -333..-236,
  z -321..-235 in 60 triangles and lies 0.1 to 0.2 m above the road (node 23 chunk 53) at
  x -323..-326, z -292.6. Drawn after the car (our old blended pass) it cut a translucent band
  with a straight top edge across the lower body in the Arcade time-trial 5 demonstration.
- vf51, per car: for each lamp shadow (count from the lamp pass) `fn_001AC328` +
  `fn_00177898` -> `fn_001AA630` -> `fn_0021F148` (node 1, sheared); when `fn_001A8618`,
  `fn_001778C0` -> `fn_001AA680` -> `fn_0021F2E0` (node 5, the headlamp pool); `fn_001AC3D0` +
  `fn_001778E8` -> `fn_001AA6D0` -> `fn_0021F258` (node 6); `fn_001AC218` (the car lights) +
  `fn_00177910` -> `fn_001AA720` (the body); then `fn_00177960` / `fn_00177938`, `fn_00177988`.
  The shadows (no depth writes) are under the body and wheels drawn after them.
- Ours: `DrawItem::in_order` (an item drawn whole at its place, before later items' opaque
  meshes) on the course scenery, the wet-road frame items (inserted right after the scenery)
  and each car shadow (node 1, 5, 6). Checked on the demonstration (frames 700 / 800, the band
  gone; the tyres' lower edge no longer cut by the shadow) and A/B on Trial Mountain, Deep
  Forest, Laguna and wet Route 5: the only other changes are tyre bottoms and, on Route 5, the
  additive spray now over the shadow (the original adds its spray buffer with the frame's depth,
  after the cars).

## O. The car reflection map: how it is built [C]

- `DynamicEnvmapCamera` (ctor 0x1D8750): +0x50 = 15.0 (tilt), +0x54 = 0.8 (0x3500D0, height).
  `fn_001D8968` applies the main camera, then `fn_001D8850` -> `fn_001D8798`: view =
  RotX(-15) RotY(180) x the main view's rotation (translation cleared by `fn_00257B84`), then
  Translate(-pos) Translate(0, -0.8, 0): the map's camera sits 0.8 m above the focus car and looks
  back at the viewer, tilted 15 degrees up in the main camera's eye space (y up).
- The focus car [C]: `fn_001D8968` takes pos from the race's view car, race +0x8000+0x16D8 ->
  +0x24C0 k + 0x4B8, +0x18 (the camera view's target, as the HUD's display car): in a replay
  the car the cameras follow, not car 0. `fn_001D8698` runs from vf45 (1P) and vf48 (each 2P
  half), so each half has its own map around its own car. Ours: `RaceScreen::env_map_car`
  (the followed opponent, else the player; player 2's half renders a second cube in `view2_`).
- `fn_001D83A8`, three passes k = 0, 1, 2: Frustum(`fn_002555A8`: l -0.5, r 0.5, b -0.7537780,
  t 0.9045340 (0x3500BC / C0), near 0.005, far 200, x/y distance 0.1599 (0x3500C8): the x / y
  scale is 2 d / (r - l), 2 d / (t - b)), modelview RotX(79.97501) (0x3500CC) RotZ(-120 k) x the
  map camera; `fn_001D8288` draws slot 23 (RotX(Env0 +0x48) then RotY(+0x28), post-multiplied,
  i.e. the dome turned by +0x28 first, then +0x48) and the slot 5 cell list. atan(0.9045 / 0.1599)
  = 79.975: each panel's top-centre ray is the map camera's -Z (the pole); a panel spans 144.5
  degrees across and reaches 158 degrees from the pole; three panels 120 degrees apart cover the
  sphere but a cap opposite the pole.
- Each panel is then warped into the 256 x 256 map: ortho (-0.9 .. 0.9, 0x3500B4 / B8),
  RotZ(240 - 120 k), the program's embedded GTM1 at 0x339540 (relocated by `fn_00224600` from
  `RacePS2Base::vf9`; 1 node, 1 chunk, flags 0x105, 9 TRI_STRIP packets of 41 vertices, texture
  slot 0x1FF = the panel). Its vertices sit at sin(theta / 2) from the centre, theta = the angle of
  their panel ray from the pole (e.g. uv (0.25, 0.5): 70.5 degrees, radius 0.5774): one 120-degree
  sector of a true sphere map. With the 0.9 ortho, lookup radius r holds the direction
  2 asin(0.9 r) from the pole: our `env_gt3_lookup` (compression and 15-degree tilt) is exact.
- Ours: a 128 px cube from the same point; faces in the sampler's orientation (fixed 2026-10-22:
  the OpenGL face table turned every face half a turn under SDL_GPU, so downward surfaces
  reflected the sky: the pale band on the Viper's lower flanks at the start of Laguna's
  demonstration, darker upper bodies and rear glass). Checked against the PCSX2 capture of that
  demonstration (matched TV-camera shots at 3.7 s and 10 s).
- The dome's rotation helpers (`fn_00257F60` RotX, `fn_00257FA0` RotY; vf1 = (sin, cos) from
  `fn_00257A44`, degrees) are right-handed and combine the matrix's columns (vf28..vf30), i.e.
  post-multiply as glRotate: world = RotX(+0x48) RotY(+0x28) v (the same helpers give the map
  camera's 15-degree tilt up). Ours had RotY(-0x28) RotX(-0x48) (a sign found with the turned
  cube); fixed 2026-10-22. Check: the brightest texels of the unrotated slot 23 dome then lie at
  the sun's azimuth (Env0 +0x9E0) within 2 (Rome), 4 (Midfield), 5 (Seattle, Super Speedway),
  16 (Trial Mountain) degrees, against 30..170 with the old transform; Laguna 70 (its brightest
  patch is not at the sun; +0x48 4.7, the only non-zero pitch of these six).

## P. Course texture-set (CLUT) animations [C]

### P1. Why Route 11's grandstand glow cards are invisible
Node 1216 (slot 2 Vls0 list from the grid: it is drawn) draws chunk 971 with the stream's entry
state (`fn_00224F18`: ATE on, ATST 6 GREATER, AREF 0x20, ZTST GEQUAL, depth write, FBMSK 0, cull
on; state carries over between nodes, none of the preceding nodes changes it), PRIM 0x3C, set 13
records 12-15 (T4 16x16 at TBP 148, CBP 149, CSA 0/2/4/6, four identical stored palettes). The
GS state, VU culling (stub 0x32A -> routine 0x3C4, front faces = CCW in y-up NDC, as ours),
occluders and texture LOD were all checked: the quads are drawn. Their palettes are animated:
set 13's Tex1 +0x24 has four entries (group 0, mode 0, 16 words, 12 frames) targeting the four
CSA rows of the CLUT buffer (Tex1 +0x9BB0, +0x40, +0x80, +0xC0) with palettes at Tex1 +0x3A0 (the
glow), +0x3E0 (magenta) and +0x420 (alpha 0 throughout). Frames: 0.001 s 0x420, 0.001 / 0.02 /
0.001 s 0x3A0, 0.001 / 0.07 / 0.001 s 0x420, 0.001 / 0.01 / 0.01 s 0x3E0, 0.01 s 0x420, then
1.3 / 1.5 / 0.9 / 1.1 s 0x420: camera flashes, transparent (alpha test fails everywhere) but for
about one frame per flash. The reset at load already makes them transparent. Ours drew the
stored palette (the glow) permanently.

### P2. Data and code
- Tex1 +0x24 -> `{u32 count, u32 entries}`, entries 0x20 bytes: +0 u16 group, +2 u16 mode, +4
  u16 words, +6 u16 frames, +8 target, +0xC frame table `{f32 seconds, u32 colours}`; +0x10 u16
  cur, +0x12 u16 next, +0x14 f32 time, +0x18 f32 weight, +0x1C s16 hold, +0x1E s8 direction,
  +0x1F u8 dirty (run time). The relocation (`fn_002487B8`) turns the offsets into pointers.
- Reset `fn_00249518` -> `fn_00248FF8` per entry (time 0, direction 1, weight 1.0, cur 0, next =
  frames != 0 (1), hold = frames[0].seconds < 0) then `fn_00249048` (apply). Called by
  `fn_002248D0` (per set of a GTM1: +0x2C sets and every +0x34 LOD table's) and `fn_00226C80`
  (BLBd) from `fn_001A9720` (course: slots 1, 22, 5, 9 via +0x04 / +0x58 / +0x14 / +0x24, BLBd
  +0x78) <- `fn_001A9180` <- `RacePS2Base::vf9`.
- Update `fn_00249180(tex1, group, dt)` (group < 0: all), entries with hold > 0 skipped:
  time += dt; while |d(cur)| <= time: time -= |d(cur)|, cur = next, next by mode (0: +1, wraps;
  1: direction flips at 0 / last, next = cur + direction; 2: at the last frame stop without the
  hold test, else +1; other: unchanged); a frame with d < 0 increments hold; hold > 0 -> time 0,
  stop. time < 0 (rewinding): per step, a hold frame with old time > 0 increments hold (> 0 ->
  time 0, stop); next = cur, cur = previous (0: wraps; 1: direction -1 at 0, +1 at the last, cur -
  direction; 2: at frame 0 time 0, stop; other: unchanged), time += |d(cur)| until >= 0. Then
  time stored, weight = 1 - max(time, 0) / |d(cur)|, dirty. Driver `fn_002249F8` <- `fn_001A9768`
  (the same slots) <- `RacePS2Base::vf29(dt)` with group -1; `RaceBase::vf2` passes dt = 1/60
  (0x34FCD8) when race +0x1790 is 0.
- Apply `fn_00249048` (dirty only): w = cvt.w(weight x 4096); per byte of `words` words
  `(cur x w + next x (4096 - w)) >> 12` (pmulth / pmaddh / psrlw / ppach / ppacb) into the target,
  dirty cleared. Driver `fn_00249110` per set <- `fn_00224B48` / `fn_00226CE0` <- `fn_001A97E8` <-
  `RacePS2Base::vf4` (every frame). The sets' buffers are uploaded when bound (`fn_00244228`).
- Release `fn_002494A0(tex1, group)`: hold -= 1. The start sequence `fn_0017A5E8` (§B0) calls
  `fn_001A96B8(course, +0x58 seconds)` for every start type once (types 1 / 2: 4 / 10, type 0
  the event's, 3 and -1: 0): update group 1 by 100 s, release, update by -seconds, release
  (`fn_001A9830` -> `fn_00224C60` / `fn_00226D10`).
- Survey (US): 64 of 80 files, 1,249 entries (group 0 mode 0: 519 of 16 words, 56 of 256; group
  0 mode 2: 14; group 1 mode 2: 660, all with hold frames). Every target lies in a CT32 CLUT buffer
  (8x2 .. 16x16) that only CLUTs read; none in cars, wheels or race/.
- GTM1 +0x3C is a different list (`fn_00226518` relocate, `fn_00226640` reset, `fn_00226748`
  update, jump tables 0x3412F0 / 0x341310 / 0x341330 by type 1..7): type 7 (`fn_00226410`)
  writes a wrapping f32 phase into node program operands (Route 11: node 366's op 0x34).

### P2a. The +0x3C animators (moving nodes)
- Relocation `fn_00226518` (from `fn_00224600`: targets + base via `fn_00225D90`), reset
  `fn_00226640` (from `fn_002248D0`, every run with the CLUT reset), update `fn_00226748` (from
  `fn_002249F8` only when its group argument is < 0, i.e. the frame update, after the Tex1
  updates), dispatch tables 0x3412F0 / 0x341310 / 0x341330 by type - 1. Update functions return
  at once when dt = 0.
- Types 1-4 (`fn_00225DC0` / `fn_00225DE8` / `fn_00225E58`): +4 speed, +8 start, +0xC angle,
  targets +0x10; angle += speed dt, > 360: -360 once, < 0: +360 once; 4 bytes into each target.
- Type 5 (`fn_00225F18` / `fn_00225F40` / `fn_00225FE0`): +4 speed, +8 u16 first, +0xA u16 end,
  +0xC start, +0x10 phase, targets +0x14; phase += speed dt; frame = __fixdfsi(__adddf3(phase,
  0.5)); frame >= end: phase and frame -= end - first; 2 bytes (u16) into each target. Reset:
  phase = start, frame likewise.
- Type 6 (`fn_002260C0` / `fn_002260E8` / `fn_002261D8`): not used on the disc, not read.
- Type 7 (`fn_00226378` / `fn_002263A0` / `fn_00226410`): +4 speed, +8 start, +0xC phase, targets
  +0x10; phase += speed dt wrapped into [0, 1) (loops); 4 bytes.
- Op 0x34 (`fn_002250D8`, `fn_00225C00`): the global phase 0x35325C = clamp(f, 0, 1). Op 0x35
  (0x225230): f = phase x count, k = cvt.w.s(f), k >= count -> count - 1, `fn_00258150(f - k)`
  stores the VU morph weight (scratch +0xB70), draws chunk base + k (`fn_0024D048`).
- Targets (US): type 2-4 op 0x14 / 0x15 / 0x16 operands, type 5 op 0x05 operands, type 7 op 0x34
  operands; 428 nodes in 69 files, slots 1 and 22 only.

### P2b. Car callback morphs
- `CarModel::Callback` (vtable 0x340C28; constructed by `0x21EC08` at offset 0 of the car model):
  +4 brake flag, +0x10..+0x1C the four wheels' values, +0x20 the steering value (constructor: 0,
  0, 0, 0, 0.5). The node interpreter's op 0x06 (`0x225508`) calls the installed callback's
  vf2(id) (global 0x352AD4, set by `0x225BF8` before a model's programs run) and branches on the
  result: vf2 (`0x21EC90`) returns +4 for id 0; ids 1-4 call `0x225C00` (the op 0x34 phase setter,
  clamp 0..1) with +0x10 + 4 (id - 1), id 5 with +0x20, and return 0; other ids return 0.
- Every frame `0x1AB0F8` (from `RacePS2Base::vf30`) sets them from the car: brake flag = car+0x13B0 >
  0 (`0x21EDF8`), wheel i = `0x1EB9A0`(car, i, 0) = 0.5 + 0.5 lerp (the flap pendulum, physics §8.8),
  +0x30 = `0x1EB6D8` (unread here), steering = `0x1EB9F0`(car, 0) = lerp of car+0x15A4 (physics §8.8:
  clamp(0.5 - 0.5 F+0x41C / P+0x58, 0, 1)) and its previous value; lerp weight t = car+0xEC4.
  Menu cars (`gt2k_menu::Car::vf3`) only set +0x30: the constructor values stay.
- The car model sets (`fn_001A9768`, `fn_001A9720`'s lists) are courses only: car +0x3C animators
  are never reset or advanced.
- Users (US): la0008 id 4, la0011 3/4, ty0032 1-4 (mud flaps); ma0095 1/2, ma0099 3/4 (aerials);
  ml0003, pd0010 id 5 (driver's arms and wheel).

### P3. Ours
`Tex1::clut_animations`, `ClutPlayer` (reset, update, release, start_signal, colours: the
code above, a step bound added against 0 s frames), `ClutTextures` (records fed by animations
found by reading their CLUTs from a GS memory holding markers at the animated words; texel
indices + palette; min alpha over every frame), `gtm1::decode_texture_set` (load state),
`build_model` (alpha-test decision over every frame; `Options::clut_sets`), `race::ClutAnimations`
(scenery, backdrop, slot 5, slot 9, billboards; restart in `reset_run_state` with the run's
countdown, 1/60 s per race step, upload per drawn frame). Moving nodes: `Gtm1::animators`,
`RunParams::operands` and op 0x34 / 0x35 in `Gtm1::run`, `DecodedPacket::key1`,
`gtm1::AnimatedNodes` (parts per chunk and draw state, re-run per frame), `CourseScene`'s moving
models (`GpuModel::update_geometry`, textures shared with the static models). Not done: the
texture-LOD sets (ours draws LOD 0 only). (A morph weight an op 0x35 leaves for later plain draws
of morph chunks: no program on the US disc draws its 1,919 morph chunks other than through op
0x35, so ours ignores it.)

## Q. Sun disc, lamp halos, the mirror's passes, meter needles [C unless marked]

### Q1. The flare system's disc mesh
- `FlareClass` ctor (0x19B098) -> `fn_00246710` builds one mesh at 0x4A56A0 (packet 0x4A5240;
  `fn_002469E0` returns it): GIF tag NLOOP 28, PRIM 0x4C (TRI_STRIP, Gouraud, untextured,
  ABE), REGS ST / RGBAQ / XYZF2; strip table (S-8 at 0x40) 7, then 6 x 7: seven strips of four
  vertices; positions (V3-32 at 0x00): strip k = rim 2k, centre (0, 0, 0), rim 2k + 1, rim 2k + 2
  with rim j = (cos t, sin t, 0), t = f32(4k + 2i) x pi (0x350A5C) / 14 for j = 2k + i (j = 14:
  t = 0), cos / sin in double (`fn_00274F70` cos, `fn_00275078` sin); colours (V4-8 at 0x80)
  0xFFFFFFFF on the centre vertices, 0 on the rims; descriptor +0xA = 1 sub-mesh, +0xC = 28,
  +0xE = 14, +0x16 = 0 (no texture). A 14-gon of radius 1 fading from the centre.
- `fn_00247DF0(flare, mesh, r, g, b, gain)`: colour (r, g, b) x gain x 0.5 into the vertex
  program (VU 0xC9), with 0x344AB0 (four words 0xFF) as the visibility words and selector 0:
  full visibility. `fn_00247DA0` (the lamp glows) passes the occlusion results instead. Both
  through `fn_00247690`, i.e. the flare.bin colour rule: vertex RGB x colour (clamped to 255),
  vertex alpha x visibility.
- Users: the sun disc (Q2), every course lamp halo (`fn_00247FE8`: after the glow, Scale(halo
  size) at the pulled point, depth test on (cap 5), `fn_00247DF0(rgb, halo gain)`, when the lamp
  is further than 1 m), the round car lamps' halos (also `fn_00247FE8`), the bar car lamps' halos
  (`fn_002481F0`: depth test on, a 2 x 2 matrix with columns H (vx, vy) and H b / |v| (-vy, vx)
  in eye space at the pulled point, `fn_00247DF0(rgb, gain)`; v from `fn_0019B6B0` (record +0x18):
  e = the eye-space x and y of the lamp's x axis (+0x00, through the model-view matrix), v =
  e ((a - b) + b / |e|), or (a, 0) when e is 0: a semi-axis H ((a - b)|e| + b) along e and H b
  across it; a bar is |a - b| >= 0.01 (0x34FDC8, 0x34FDCC)), and the glare's style 2
  (`fn_0019BA38` picks `fn_002466F0` (style 0), `fn_00246D28` (style 1) or `fn_002469E0` (style 2)
  for `fn_00248458`). The "72-spike burst" is the glare's style 1 mesh, not the halo. All ALPHA
  0x48 FIX 0 (Cs As + Cd, As 0x80 = 1.0), depth writes off.
- The car lamp records (race +0xA2C count, +0xA30 records of 0x24; course lamps +0x1840,
  +0x1844 of 0x18) are reset per view (`fn_0019B358`) and filled per car by `fn_001AB1B8`
  (headlamps +0x40, brake lamps +0x50, tail lamps +0x48 through `fn_0019B6B0`), which returns at
  once for a car whose draw mode `fn_001E0668` is below 2. `RacePS2Base::vf55` calls it (through
  `fn_00177A00`) for each car that `CameraManager::vf13` (0x2092B0) passes: when the current
  camera's vtable +0x50 (`fn_00209220`) returns 0, not the camera's car (+0x4A8); and when the
  current camera is the one at +0x600, not a car `fn_00200E08` returns non-zero for (the car draw's
  `fn_00209370` makes the same two calls). So the in-car view has no lamps of its own car.
  `fn_0019BA38` draws both lists.
- Ours: `race::Flare::disc_mesh`, `Flare::gs_color`, `Flare::add_mesh`; the halos go into the
  glow batch (two additive passes for As / 128) at the pulled point's depth, so the view's depth
  test (GEQUAL, no depth writes) is the GS's (VR eyes: one ray at the halo's centre); the bar
  halo through `fn_002481F0`'s matrix (`Flare::add_mesh` with a 2 x 2 matrix).

### Q2. The sun disc
- vf45 @0x17E8FC and vf48 @0x17F300, right after the sky (`fn_001A91D8`) and before the backdrop
  (`fn_001A9240`): `fn_001A88C0(Env0, race + 0x9830)`. Not in vf54 (the mirror) or the reflection
  map. Gate Env0 +0x10 & 0x400. `fn_001A86B0` makes the direction from +0x9DC / +0x9E0 (as the
  glare); the point is 10 x it (f32 10 at 0x1A88F4), the size +0x9D4 x 10, the gain +0x9D8, RGB
  +0x9C0. `fn_0019B568` -> `fn_00248538`: returns when size x 10 <= 0; depth write off
  (`fn_00252978(0)`), caps 5 (depth test), 0xF (fog: the VU program without fog,
  `fn_0025AA00`), 4 (alpha test) off; ALPHA 0x48 FIX 0; push; translation cleared
  (`fn_00257B84`), Translate(10 d) (`fn_00257EB4`: in the view's rotated frame), the rotation
  reset to identity (`fn_00257B5C`: vf28..vf30 = the unit axes), Scale(10 size); `fn_00247DF0`
  (sun RGB, gain) with the disc mesh; pop; depth write and fog back on (caps 5 and 4 stay off for
  the backdrop's own state). So: a 14-gon parallel to the screen, centre 10 m out along the sun,
  radius 10 size (angular radius atan(size)), colour trunc(min(255, 255 x RGB x gain / 2)) at the
  centre, alpha 0xFF, both to 0 at the rim, added at As / 128: the backdrop and the scenery draw
  over it, the glare (step 24) over everything.
- Values (US): Trial Mountain RGB 2 / 2 / 2, size 0.045, gain 2 (centre 255: a white disc of
  2.6 degrees); Seattle 1.35 / 0.92 / 0.63, 0.025, 2; Smokey Mountain North 0.4 / 0.3 / 0.2, 0.05,
  2; Grand Valley 0.024, 0.28; Akasaka 0.27, 0.26 (a wide faint glow); Laguna 0.12, 0.1 (centre
  4 / 3 / 1: barely visible); Rome 0.001; Super Speedway 0 (none); Route 11 / Midfield / Tahiti
  dirt: sun off or RGB 0.
- Ours: `race::SunDisc` (two background items after the sky dome for As / 128), main view, 2P
  halves, the race behind the quick menu and VR (re-centred per eye with the sky); not in the
  mirror or the reflection cube.

### Q3. The rear-view mirror's passes (vf54, 0x180330)
- `fn_001A8130` (lights), `fn_001A81C8(Env0 + 0xA90)` (its fog), clear colour 0, sky slot 24
  (`fn_001A91D8(course, course + 0x60)`), depth clear, `fn_00225C50(1)`, `fn_00225C60(2)`,
  `fn_001A92E8(course, course + 0x1C (a2 set and present) or + 0xC, camera, Env0 +0xF38)`,
  `fn_00225C50(0)`, frame alpha 0x80 (`fn_001807B0`), the cars (vf51, vtable +0x198, a2 = 2),
  then the tyre smoke `fn_001A4F28(race + 0xBC30, 1)`; then the 1 px outline. No backdrop, sun
  disc, wet road (vf50), pylons (`fn_001A2278` / `fn_001A24B0`), sparks, glare or lamps.
- `fn_001A92E8` -> `fn_001A93B0(course, cell list, flags)`: slot 1's nodes, then the BLBd
  billboards (course + 0x78) only when flags & 1, then the course line (course + 0x6C) whenever
  it exists. The flags are Env0 +0xF30 in the 1P view (vf49), +0xF34 in the 2P halves (vf48 with
  a2 = 1, which `RaceSplitBattle::vf3` always passes; else +0xF30), +0xF38 in the mirror. US:
  0xFFFFFFFF / 0xFFFFFFFE / 0xFFFFFFFE (no billboards in the 2P halves and the mirror) on
  akasaka, midfield, seattle, smtsouth, tahitidirt, tahitimaze and their reverse layouts; rome,
  route11 0xFFFFFFFF / 0xFFFFFFFF / 0xFFFFFFFE; montecarlo 1 / 0 / 0; L_midfield2 / 3,
  L_slalom80r4 0 / 0 / 0 (none at all); the rest all set.
- `fn_00225C60(n)` (0x352AEC) forces the node LOD level n (clamped to the model's count) in
  `fn_002250D8` for models with a LOD table (+0x34): 2 in the mirror and the 2P halves, -1 (by
  distance) in the 1P view. Course slot 1 runs no LOD switches (docs/dev/RACE.md), so no effect
  found [I].
- `fn_001A4F28(smoke, mode)`: mode 0 (vf45 / vf48 at full resolution) draws a puff whose +0x8 is
  above 3 whole (`fn_001A55E0`) and the others through the ground-line split (`fn_001A5760`);
  mode 1 (the mirror, and the reduced buffer of Env0 +0x914 / +0x91C) every puff whole. The
  blend comes from Env0 +0x918: 0x48 -> ALPHA 0x48, else 0x44; every US course has 0x44 (68, or
  68.0f stored as a float on five files): the mirror's puffs are alpha-blended even on Route 5,
  whose main view adds its buffer.
- Ours: `RaceScreen::collect_items(..., ItemsView)`: billboards by the view's flag, no pylons and
  `TyreSmoke::add_items(mirror)` (whole puffs, alpha-blended) in the mirror.

### Q4. Round meter needles (`RaceRoundMeterBase::vf15`, 0x19C738)
- Called from `RaceRoundMeterBase::vf11` (the matrix at the meter's centre and scale) for the
  tachometers (`RaceRoundTachometerBase`, meter at +0x20), the in-car speedometer
  (`RaceOnboardSpeedmeter`, a `RaceRoundMeter`) and the boost meter.
- Colour `fn_0025A038(0xFF0000C0)`: RGB (0xC0, 0, 0) untextured (`fn_0024FD18` takes the raw
  +0xB60 without a texture); cap 6 on; ALPHA 0x64 FIX 0.125 ((Cs - Cd) x 16 / 128 + Cd).
- Angle a = clamp(value +0x18, 0, scale +0x1C + 8) / scale x (+0x3C - +0x38) + +0x38; step =
  (a - +0x24) x 0.125, or 0 while +0x24 = FLT_MAX (0x3520F0); then +0x24 = a. Pass k = 0..7:
  push, RotZ(a + step (k + 1)) (`fn_00257FE0`), Translate(+0x40, 0, 0), Scale(+0x44 - +0x40),
  the 4-vertex strip at 0x32F250 ((1, 0.01), (0, 0.02), (1, -0.01), (0, -0.02)) through
  `fn_002501A8`, then `fn_00252B40(step k)` adds the step at 0x32F568 ((0.5, 0), (-0.5, 0.5),
  (0.5, 0), (-0.5, -0.25), (0.5, 0), (-0.5, 0.5), (0.5, 0), (-0.5, -0.75)) to XYOFFSET (x 16,
  y x 16 or x 8 in field mode: frame pixels either way); pop. After the loop the offsets are back
  at 0 and ALPHA 0x44. So the needle is 8 passes of 1/8 each (a still needle reaches 1 -
  (7/8)^8 = 66 %), spread over the last frame's movement beyond the new angle and over a 2 x 4
  sub-pixel pattern.
- `RaceRoundMeter::vf14` (0x19CB30) adds the model +0x70 8 times at ALPHA 0x68 FIX 0.125 with the
  same steps when +0x4C >= 0; the constructor sets +0x70 = 0, +0x4C = -1, and no writer of either
  was found [?].
- Ours: `Hud::needle_passes`, `Hud::draw_meter` (one last angle per meter: in-car tachometer,
  speedometer, outside tachometer, boost; reset per race).

## R. Race frame: the passes after the 3D picture [C unless marked]

### R1. Order

`RacePS2Base::vf45` (the 1P frame) after the 3D scene (sky, scenery, wet road, cars, mirror,
sparks) runs, into the display buffer:
1. the depth-of-field composite (replays on Env0 flag 0x100, `fn_001ACF38`, §G8) or the plain copy
   of the 640 x 448 work target (`fn_00244680`, §R5);
2. the heat haze lenses (`fn_00246380`, 0x17EC88; §R2);
3. the tyre smoke (race+0xBC30, `fn_001A4CC8`, 0x17ECAC);
4. FlareClass (race+0x9830, `fn_0019BA38`): the sun glare and the lamp glows;
5. the colour grade (`fn_002582F8`, §G6);
6. the vignette (`fn_001AD818`, 0x17EDB0; §R3);
7. the 2-D state, the dissolve (0x17EDE8), the trail (0x17EE28) (§G7), then the 2-D layer.

So the glare and the lamp glows are graded and lie under the vignette; the smoke is not seen
through the haze's lenses. The display buffers are 640 x 224 (one field; the dissolve's buffers
are 0x23000 words apart, 640 x 224 x 4 bytes): everything after step 1 is drawn into a field.

Ours: `View3D::draw` (DrawOptions `haze`, `overlay`, `vignette`; `DrawItem::late` for the smoke).
The scene pass ends with the late items and the overlay, or, on frames with lenses, keeps its
picture and depth, a copy is taken and a second pass draws the lenses, the late items and the
overlay; the post pass draws the graded picture, the vignette, the dissolve and the trail. The
glare and the lamp glows (`SunGlare::add`, `LampGlows::add`, `CarLampGlows::add` into a
`render::ScreenBatch`) are drawn inside the picture at its resolution (the original-resolution
frame: 640 x 448); VR eyes, which have no grade, keep drawing them with the 2-D renderer.

### R2. The heat haze

- **Gate** (0x17EC48..0x17EC6C): Env0 +0x10 bit 0x20 and the current camera's flags
  (CameraManager vf12 -> the camera's vtable +0x58) & 0x2000. `DesignerCamera::vf13`
  (0x202344..0x2023A0) and `PasslineCamera::vf13` (0x202D90..0x202DC8) set the camera's halfword +6
  every frame from the current GTRC record: +0x1C > 0 -> 0x2000, +0x20 > 0 -> 0x4000, +0x24 > 0 ->
  0x8000 (0.0 or 1.0 on the disc; nothing reads 0x4000 / 0x8000). `OnBoardCamera::vf11` returns 0,
  `MatrixCamera` / `ZoomupCamera` 0x4000, the others +6, which only the GTRC cameras set. So: the
  course shots of replays, demonstrations, the attract demo and view mode 1 whose record is
  flagged, on-board records included; never the live views, the procedural cameras, a turn on
  board or 2P (vf48 has no haze). Not gated by zoom, weather or the replay flag. US courses: every
  record of Grand Valley, Laguna, Rome and Tahiti; most of Trial Mountain (65 / 66), Smokey Mt
  North, Super Speedway; some of Deep Forest and Test Course; none on Akasaka, Midfield, Route 11;
  Seattle forward, Monte Carlo, Route 5 and Smokey Mt South flag records but lack Env0 0x20.
- **Set-up** (`RacePS2Base::vf9`, every race, replay and demonstration set-up, 0x17D66C..0x17D69C):
  the generator 0x3530AC = 0x7F7F7F7F (`fn_00245D88`); `fn_002461D8(field, buffer, Env0 +0xB0)`:
  defaults (size 8, box 32, cap 8, ahead 16, drift 0.5 / 0.01 (0x350A58) / 0.5) and every particle
  drawn; then `fn_001A8678` -> `fn_00246290`: Env0 +0xB4 size S0, +0xB8 box b, +0xBC vertical
  cap, +0xC0 ahead, +0xC4 horizontal drift range (both +0x20 and +0x28), +0xC8 vertical drift,
  and every particle drawn again. Particle (`fn_00245E58`, count - 1 down to 0, 32 bytes): x =
  U(-b, b), y = U(-cap / 2, cap / 2), z = U(-b, b), vx = U(-C4, C4), vy = C8, vz = U(-C4, C4).
  Generator `fn_00245D90`: s = s x 0x5D588B65 + 1, r = the float 1.m from the low 23 bits;
  `fn_00245DC8`: (r - 1) (hi - lo) + lo. 400 particles on every US course (buffer 800).
- **Motion**: `RacePS2Base::vf29` -> `fn_00246320` every frame: p += v (no time step), whatever the
  camera; not reset at cuts, only by the next set-up.
- **Draw** (`fn_00246380`): the centre c = (0, 0, -ahead) through the rigid inverse of the view
  (its translation zeroed by vf45, `fn_00257B84`): `ahead` metres along the view axis, in
  camera-relative world axes. m is read from the projection alone: vf24..vf27 loaded from VU0
  data qwords 8..11 (or vf28..vf31 in matrix mode 1; `fn_00254E20` saves the mode-1 (projection)
  matrix there when switching to the modelview, whose own slot is qwords 4..7), m = vf24.x, the
  frustum's x scale (the zoom, x 0.75 in the 16:9 option). But before the lenses `fn_002486B8`
  (0x24653C) rebuilds vf24..vf27 when the matrices are dirty, which they are (`fn_00257B84` and the
  inverse `fn_00258078` set the flag): `fn_0024E670` runs the VU0 micro program at 0x120, the
  modelview (its translation zeroed) times the projection. So M is the view's rotation and the
  projection: the box is in front of the camera whatever its heading. (Corrected 2026-11-02: the
  first reading took M as the projection alone, which put the lenses on screen only for cameras
  looking near world -z.) B = (1 / m) (2 b) (the box's
  edge across), S = S0 sqrt(|m|); each particle wrapped per axis (`fn_002465B8`): r = d - trunc(d
  (1 / L)) L, + L when d < 0, coordinate c + r - L / 2, L = B for x and z, min(cap, B) for y; then
  (`fn_00245F30`) clip = (x, y, z, 1) M, kept when inside all six planes (w < 0 never), screen =
  clip / w x (320, -112) + centre (the field's lines).
- **Lens**: TEX0 = the display buffer itself (TEXFLUSH per lens: each samples the frame with the
  lenses before it), REGION_CLAMP to it, ALPHA 0x64 FIX 64 ((Cs - Cd) / 2 + Cd), RGBAQ 0x80808080,
  depth writes off. The depth test is as the frame left it: off after the plain copy; after a
  depth-of-field frame on (GEQUAL) against the scene's depth, which `fn_00244590` (0x17EB04) copied
  into the field's Z24 buffer at the display buffer's address (`fn_002444D0`, `fn_00244A98`).
  But `fn_00245F30` converts all four components of each vertex with `vftoi4` (12.4 fixed point),
  so its z is 16 times the projected depth: past Z24's 0xFFFFFF for every lens, clamped to the
  maximum (the nearest), and the test never fails. So the lenses lie over everything, the cars
  and the road included, with or without the depth of field. A 6-vertex strip about the projected point P with texels about the same
  pixel: positions (+-0.58 S, +-0.5 S), (+-1.16 S, 0) (0x350A4C 0.58), texels (+-0.58 S,
  +-0.45 S) (0x350A50), (+-0.928 S, 0) (0x350A54 1.6 x 0.58): horizontal in 640-pixel units,
  vertical in field lines (2 of the frame's). The left-hand vertices are kept at x >= the left
  edge, the top-left one's y >= the top; texels >= 0. The frame under the hexagon is magnified 1.25x
  across and 1.11x up and down and mixed 50/50. Trial Mountain (S0 3.5, b 40, cap 3): zoom 1.5 S
  4.3 px; zoom 19 S 15 px (a lens 35 x 30 frame pixels).
- Ours (off unless `[video] heat_haze = on`): `race::HeatHaze` (set-up and drift in the EE's arithmetic, `RaceScreen::restart` and
  every race tick), `GtrcRecord::haze` -> `ReplayDirector::Shot::haze` -> `CameraRig::replay_haze`,
  `race::haze_hexagon` / `add_haze_lenses`, View3D's haze pass (one copy of the picture for all
  lenses: overlapping lenses sample the picture without each other; in front of everything; above
  448 lines the lens's texel from a field-sized copy (640 x 224 at the picture's aspect) plus the
  pixel's detail finer than it, so the shift shows no more than it did at the original's size). m is our projection's x scale (`proj(0, 0)`), which is GT3's for our 4:3
  and 16:9 pictures.

### R3. The vignette

`fn_001AD818(display, Env0)` from vf45 when race+0x9784 (replays and demonstrations) and Env0
+0x10 & 0x40: matrix mode 1 Ortho(-320, 320, -224, 224, -1, 1), mode 0 identity; a 64-vertex
strip of 32 steps, angle i x 2 pi / 31 (0x34FFA4 / 31.0; the last 0), alternating (cos, sin) x
outer radius Env0 +0x950 and x inner radius +0x954, colours +0x958..+0x964 and +0x968..+0x974
(`fn_0025A090`: RGB trunc(255 c + 0.5), A trunc(128 a + 0.5), clamped to 255); Gouraud, no
texture, ALPHA 0x44 ((Cs - Cd) As + Cd). Inside the inner radius nothing is drawn. Most courses
420 / 200, outer black at 0.4 (51 / 128), inner clear: the corners (390 px out) about 35 % darker;
none on Grand Valley, Seattle, Smokey Mt North, Tahiti (22 of 80 US files). Ours:
`race::vignette_ring` / `add_vignette`, drawn by View3D after the graded picture, under the
dissolve and the trail (both pictures carry it), for `!RaceScreen::live_view()`.

### R4. The 2P halves

`RaceSplitBattle::vf3` draws each half with `RacePS2Base::vf48`, which has no sparks, haze,
grade (`fn_002582F8`), vignette, dissolve or trail. Ours: no grade on either half (or their
mirrors); the glare is drawn inside each half.

### R5. The copy into the field

`fn_00244680(display, work, k = 0x2F4244, flag = 0x2F4248)` scales the 640 x 448 work target into
the 640 x 224 display buffer. The program sets the GS to interlaced NTSC field mode at start-up
(0x101348: `fn_002430E0(0x2F4230, 1, 2, 1)` -> sceGsResetGraph(..., interlace 1, NTSC, FFMD 1)),
so `fn_00258218` returns 3 on the fields of one parity and 4 on the others (0x4A6A5B = 1; +0x1BC
the field, copied from 0x4A69D4 at each vsync; +0x1BD cleared by `fn_002529E0(0)` in the copy):
on mode 3 fields two sprites, the second at FIX 0.5, the source offset by +(1 - k) and -(1 - k)
lines (a 2-tap vertical filter), on the others one copy (half a line down when the flag is set).
Neither k nor the flag has a writer (0 in the program image): offsets of +-1 source line. Ours:
not reproduced: it is a property of the interlaced 224-line output; our original-resolution
frame is progressive 640 x 448 (a filter on every other frame would flicker).

## S. The race wheels: brake parts, glow, spin blurs, camber [C unless marked]

The race tyre module (`RaceTireModel`, 3 x `RaceTirePacket` at +0x94, 0x4C each) draws each
wheel; the menus' `MenuTireModel` (`fn_00224260`) has none of what follows (no brake.bin, no
decals, no blur; the menu car files carry their own brake geometry in GTM1 nodes 2 / 3).

### S1. Call chain and the wheel's frame
- `RacePS2Base::vf51` (per car) -> `fn_00177938` -> `fn_001AA7A0(car, mode, lod, f12 = Env0
  +0x4C, f13 = t)`; per wheel i: translate to the hub (`fn_001EB710/750/7A8`, the car record's
  interpolated hub), then `fn_002222B8(model = car+0x3C+0x18C i, lod, tab[mode], f12 = steer x
  57.29578, f13 = camber x 57.29578, f14 = spin x 57.29578, f15 = heat^2 x Env0 +0x4C, f16 =
  the blur's turn)`. The interpolators `fn_001EB7D0` / `fn_001EB810` / `fn_001EB850` / `fn_001E6A08` read the wheel
  record (car+0xFE4+0xA4 i: +0x0C steer, +0x10 camber, +0x38 spin, +0x94 brake heat) and the
  car's prev copies (+0x15E4 / +0x15F4 / +0x1614 / +0x1624): value = cur (1 - t') + prev t',
  t' = t + car+0xEC4 (the race-flow accumulator, 0 unless the flow gate runs); the glow's
  `fn_001E6A08` lerps by t alone and squares.
- vf51's arguments: vf45 (1P) `(f12 0, a2 -1, a3 1)`; vf48 (2P halves) `(0, fp ? -1 : 0, 0)`;
  vf54 (rear-view mirror) `(0, 2, 0)`; the wet road's vf52 never calls the tyre module's
  draw (it uses `fn_00177960` -> `fn_001AAA90`: every wheel at the far level, `fn_00222550`).
  vf51 with a2 > 0 (the mirror) also takes `fn_00177960`. mode (a1 of `fn_001AA7A0`) = a3 ?
  (focus car ? 1 : 2) : 0, the focus car = the race's view car (race+0x8000+0x16D8 ->
  +0x24C0 k + 0x4B8); tab 0x330B48 = {0, 16, -1}.
- `fn_002222B8`: lod < 0 -> `fn_00222258` (below); lod 3 -> the drum `fn_00222550`; else
  Push, RotY(steer + 180 x right) (`fn_00257FA0`; right = model+0 bit 0, the wheel index),
  RotZ(camber) (`fn_00257FE0`), visibility `TireModelBase::vf2` unless lod was given as 0
  (then `fn_0021F9D0`: +0x80 = +0x7C = 1), RotX(right ? spin : -spin), Disable ATE / DATE,
  depth on, then `fn_002227E0(packet, tab, f12 = spin x pi / 180 (unsigned), f13 = f15,
  f14 = f16)`, Pop. Then the band (+0x24, `fn_0021FC58`) when the tyre is not symmetric,
  lod < 3, the level has one (0x340FF0: 1, 0, 0) and +0x80: Push, RotY(180 x right),
  RotZ(camber) (no steer), FBMSK colour only. Ours drew the band steered.
- The camber is the physics' visual camber (`0x1E9218`, wheel +0x10, physics §8 / P+0xE04),
  drawn as RotZ in the wheel's frame after the steer (half turn included).
- `TireModelBase::vf2` (0x21F940): a frustum test (`fn_0021F810`), then +0x80 = (the eye,
  0x340CA0 = (0, 0, 0, 1) through the inverse modelview, has x < -W/2): the outer face in view.
- `fn_00222258` / `fn_002221C8`: s = P[0][0] x model+0x18 / -(the hub's eye z), model+0x18 =
  sqrt(R^2 + W^2 / 4) (`fn_0021F6E0`: +0x8 W, +0xC rim radius b, +0x10 R = b + 0.75 sw);
  level 0 if s > 0.09, 1 if > 0.03, 2 if > 0.015 (0x350990..98), else 3.
- Levels (`fn_002220B8`, tables 0x340FD0..0x341020): segments 18 / 12 / 8, shoulder 1 1 0,
  band 1 0 0, inner faces and brake parts (+0x30) 1 1 0, the level-2 hexagon (+0x34) at 2,
  outer inset -0.004.

### S2. `fn_002227E0`, the wheel's passes
Translate(-W/2, 0, 0), Scale(b) (the rim's unit), Scale(1, 1, -1) when `fn_0021F5E8` (right
and GTTW +0x10 bit 2: a mirrored rim) with the cull face swapped; then:
1. if +0x80: the rim blur `fn_00223320` (Q4); the tread blur `fn_00222F00` (Q3);
2. FBMSK colour only; packet A (+0x1C: tread, shoulder, sidewall; slot 2 = the tyre material
   0x2E5E90: Ma 0.3, Md 0.15, Ms 0.9, Me 0 (w 1), shininess GTTR +0x18; `TirePacket::vf3`);
3. if +0x30 (levels 0, 1) and +0x80: `fn_00221E78` (brake.bin's Tex1 once per frame, flag
   0x352AD0, cleared by `fn_00222668` after vf51's cars); the rotor (+0x40): positions from
   `fn_00222B68`, texture brake+0x14 + kind A (+0x178), Enable ATE, ATST GREATER AREF 0x40
   (`fn_00252890(6, 0x40)` = TEST.ATST / AREF), draw; the caliper (+0x3C, if +0x17C >= 0):
   positions from `fn_00222CE0`, texture brake+0x18 + kind B, AREF 0x7F, AFAIL FB_ONLY
   (`fn_002527A8(1)`), ALPHA 0x44 FIX 0x80, draw, AFAIL KEEP;
4. ALPHA 0x68 (Cs FIX + Cd) with FIX = trunc(clamp(128 f13, 0, 255)) (`fn_002527E8`), ATE off,
   packet B (+0x20: the black faces, slot 0, PRIM 0x2C, unlit; plus the glow quad, slot 12 =
   brake.bin texture brake+0x1C (0) with depth writes off, PRIM 0x5C (ABE, no FGE), colour
   0x80808080); DepthMask on, ATST GREATER 0x20;
5. if +0x80 and not level 2: FBMSK 0, the rim (the wheel GTM1 model+0x88 through `fn_0021F9E0`
   with the packet's callback +0x44: its runtime texture slot is what Q4 left).
Ours: the same order through draw items (the parts `in_order` before the tyre, the glow an
additive item with tint = FIX, unfogged).

### S3. The brake parts' geometry
- `RaceTirePacket::vf2`: after `TirePacket::vf2` (packets A, B, C), when +0x30: +0x40 =
  `fn_00223A70` (slot 0, PRIM 0x3C, chunk kind 1 lit), +0x3C = `fn_00223B70` if +0x17C >= 0
  (PRIM 0x7C); each a 4-vertex strip with the normal (-1, 0, 0) (0x3410B0) and UVs only: the
  positions are written per frame (`fn_0024D0C0(4)`). Corners 0x341084: (y, z) = (1, 1),
  (1, -1), (-1, 1), (-1, -1). s2 = right XOR mirrored.
  - rotor UV: S = 0.5 + 0.5 z (s2: 0.5 - 0.5 z), T = 0.5 - 0.5 y;
  - caliper UV: S = 0.5 - 0.5 y, T = 0.5 - 0.5 z (s2: 0.5 + 0.5 z).
- `fn_00222B68` (rotor): (s, c) = sincosf(-spin) (0x26A4D0), s negated when s2; corner (y, z)
  -> x = (+0x184 + W / 2) / b, (y, z) r / b turned by (y c + z s, -y s + z c). With the RotX
  of Q1 the net turn is 0: the rotor stays put in the steered frame.
- `fn_00222CE0` (caliper): angle -(spin + a) (a = +0x188), record p = brake+0x24 + 32 kind B;
  p[2] negated when s2; corner -> (y0, z0) = (p3 + p1 y, p2 + p0 z), turned the same way;
  x = (+0x184 + W / 2 - 0.005) / b (0x3509A0). Net: turned by -a in the left wheel's frame,
  mirrored for right wheels, so a right caliper is the left one reflected through the car's
  centre plane. am0006 rear a = -40: the caliper 40 degrees up from the back; a front -180
  (Fiat Coupé, B-3): in front of the hub, as the PCSX2 captures show.
- `RaceTirePacket::vf5` (0x223920): after `TirePacket::vf5`, when +0x30 and the rotor
  table's record (brake+0x20 + 16 kind A) has bit 0: slot 12, PRIM 0x5C, `fn_00223C70`: x =
  (+0x184 + W / 2 - 0.001) / b, (y, z) r / b at the same corners, UV (0.5 + 0.5 z, 0.5 - 0.5 y),
  in the spinning frame (static geometry).
- `RaceTirePacket::vf3`: slot 12 = brake.bin record brake+0x1C, DepthMask off; slot 13 =
  material 0x341030 and the next texture record (the level-2 hexagon).
- Records: `fn_001AA250` -> `fn_00222090(model, a1 = rec+4, a2 = rec+0, rec+8, +0xC, +0x10)`:
  +0x178 = rec+4 - 1 (rotor), +0x17C = rec+0 - 2 (caliper), +0x180 r, +0x184 offset, +0x188 =
  angle x 0x35098C (pi / 180). (The 2026-11-01 survey had the two kinds swapped.)
- brake.bin: `fn_00102B58` -> `fn_00222028` (0x352ACC) -> `fn_00221E08` (relocates +0x10,
  +0x20, +0x24, the Tex1 through `fn_00248DA8`). Tex1 records: MXL 0, bilinear, REGION_CLAMP.

### S4. The blur's turn, the tread blur, the rim blur
- The turn (`fn_001AA7A0`, mode != 0): the hub through the modelview-projection (vf24..27,
  `fn_002486B8` / `fn_0024E670`), divided by w, its x / y against the car's last ones
  (car+0x66C + 16 i, updated here only), m = sqrt(dx^2 + dy^2); t_b = Env0 +0xEA0 x exp(Env0
  +0xEA4 x m) (`fn_0026A270` = expf); turn = spin(t_b) - spin(t) = (prev - cur)(t_b - t), t = 0
  from vf45. Env0 +0xEA0 / +0xEA4 = 0.8 / -20 on every US course.
- Tread `fn_00222F00(packet, work, tab, turn)`: when (GTTR bit 1 ? bit 0 : +0x80 and bit 0),
  tab > 0 (mode 1: the focus car), turn != 0, level < 2. Work 128 x 64; depth / ABE / FGE off;
  ALPHA 0x64 FIX 0x40; f = turn / 2 x repeats x 325.94931 (0x3509A4, 1024 / pi: 1/16 texels per
  radian of a 128-texel repeat), negated on right wheels; a sprite copies the texture
  (0..128 x 0..64, ABE off), then while k (from 2) < tab: f /= 2, k *= 2, ABE on, a sprite at
  UV (sh, 0)..(sh + 128, 64), sh = (trunc(f) & 0x7FF) / 16: three 50 % passes. The tyre then
  draws from the buffer. Ours: eight taps at S + every subset sum of the three shifts / 128.
- Rim `fn_00223320(packet, work, work2, tab, turn)`: texture-LOD table index = level + 3 x
  axle (when GTTW +0x10 bit 0, `fn_0021F510`); entry +0x4 (the rim) normally; with tab < 0
  (mode 2) and |turn| > 0.05236 (0x3509A8 / AC: 3 degrees) entry +0x8 (the spinning rim; +0x4
  when null). A paint variant from `fn_0021F5B8` (`fn_00248EC8`). tab <= 0 or turn == 0: the
  set's record 0 into the runtime slot (current record + 0x28), done. Else W = min(1 << TW,
  64), a W x 3W work area, the set's record 1 (the same image) as source, ABE / depth / ATE
  off, colour (1, 1, 1, 1), then `fn_002231D0(angle, W, tw, row)` draws the image turned about
  its centre (dest = c + R(angle)(src - c), R(x, y) = (x cos - y sin, x sin + y cos)),
  stretched 2x vertically from row W + row: copy 0 at angle 0 on every row, copy 1 at turn / 2
  (negated on right wheels) with SCANMSK 3 (`fn_00255B70`: odd rows masked) offset one row,
  then a 2:1 vertical bilinear sprite back into rows 0..W: the mean of the two. The loop (k
  from 2, k < tab, k *= 2) repeats with the angle halved and the tile as source: four turned
  copies, 16 in all, turned by 0, 1/16, .. 15/16 of the turn. `fn_00244B68` / `fn_0024BBA0` make the
  tile (W x W) the runtime slot. The bilinear minify averages the alpha too; the rim's AREF
  0x20 then cuts the coverage at 1/4.
- Ours: 16 taps of the rim image at the mip level log2(width / W), the coverage of our
  cut-out copy tested at 0x20 / 0x80.

### S5. Ours, and what differs
`formats/model/brake_model.*` (brake.bin, the records, the geometry), `race/wheel_fx.*` (level,
turn, shifts, angles), `CarVisual::wheel_look` / `add_body_items`, `model.frag`
`wheel_texture`. The tyre and rim geometry keep the body's level (not the wheel's: no level-2
hexagon or level-3 drum); the mirrors' and the wet road's cars keep full wheels (no parts, no
blur); the 2P halves' explicit level 0 is not reproduced (their wheels use the size test).
Open (the wheel decoder, before this work): GTTW +0x10 bit 2 (8 car files: right rims
mirrored, Scale(1, 1, -1)) and bit 0 (18: the rear axle's own texture-LOD tables, level + 3)
are not reproduced: every rim is drawn from table 0, unmirrored, and the spinning image
follows suit. The parts are unaffected (their car-axes placement is the same either way).

## T. Sparks: the ParticleManager [C unless marked]

The race's only spark system is `ParticleManager` (RacePS2Base + 0xBC90; constructor 0x19EEF8, which loads
`race/particle.img`). Wall and car hits make none of their own: the collision step's reports (0x1DFEB8, the impact effects
0x1FB890) only sound and rumble. Sparks come from a car's underbody spark points dipping below the road, which a hit can
cause by tipping the car. Its other two spawners (0x19F0D8, wheel debris, and 0x19F040) have no caller. Its tuning is a
static block P at 0x2C03B0 filled by its static constructor (0x19F898, from 0x34FE74 .. 0x34FEBC and immediates), not
Env0 or any file.

### T1. Emission (0x1AADD0, every car, every frame)

- `RacePS2Base::vf30` (inside vf29, once per 60 Hz frame while the race runs, not in the pause / post-race stage where vf31
  runs instead) runs the race step (`RaceBase::vf30`), then for every car of the race list in order with a loaded model
  (entry +0x18BC) 0x1779D8 -> 0x1AADD0(RaceCarModel, manager, t = 0). Live runs, replays, demonstrations and 2P battles
  alike.
- Skipped when the draw mode 0x1E0668 is below 2 (a hidden ghost; a visible translucent ghost sparks) or the model's GTCI
  +0x58 (spark point count) is 0 (formats/car_model.md "GTCI": 24 of the 286 models).
- The model is the one the race draws: RaceBase::vf9 loads each entry's car model from the folder of the course's time of
  day (Env0 +0x10 bit 19 `cars/eve/`, else bit 15 `cars/night/`, else the race mode's own choice, `cars/day/` in races;
  Europe 0x179F78, the path 0x2208E0), so its GTCI's spark points are that folder's model's. The eve and night models
  keep the day models' points on every disc but NTSC-U's and NTSC-J's night ml0001 / pd0001, which have none
  (formats/car_model.md). Ours: `RaceScreen::car_spark_points` reads the drawn folder's model (`car_folder_`); the
  harness's `#sparks` races (day courses) read `cars/day/`.
- v = (car +0x13D0, +0x13D8, -(+0x13D4)): the world velocity in the course frame (x, height, z). None when
  (vx^2 + vh^2) + vz^2 < (100 x 100) x 0.0771604925 (1/3.6^2; 0x34FF64): below 100 km/h (3-D speed).
- The car's matrix as its body is drawn (0x1AA3A0): identity, Translate(the drawn body point), RotY(heading x 57.2957802),
  RotX(pitch x 57.29578), RotZ(-(roll x 57.29578)) (0x34FF68), each value between the last two steps by the race-flow
  accumulator t = car +0xEC4: prev x t + cur x (1 - t) (0x1EB400 .. 0x1EB520 on +0x158C / +0x15A8, +0x13F4 / +0x15C8,
  +0x13EC / +0x15C0, +0x13F0 / +0x15C4).
- The road at the body point (0x18FC18: the course's single-point query from the car's segment hint, car +0x1370): off the
  road, no sparks. The triangle's whole material word (+0x08) must be 0, 1, 7 or 9 (tarmac, kerbs, the paved sections);
  its plane (nx, ny, nz, d) is the one every particle of this frame's bursts bounces on.
- Each spark point p in GTCI order, world p = M (x, y, z): the road height there on that plane is
  g = -(((nx px) + (nz pz)) + d) / ny; when py < g (strictly), count = trunc((g - py) x 200), then at least 5, at most 20,
  and a burst of count particles at (px, g + 0.05, pz) (0x34FF6C) with velocity v. (P +0xA8, a switch to burst above the
  road too, is 0.)

### T2. The VU0 matrix library

- 0x257A44 (the rotations' sine and cosine, angle in degrees) reduces both lanes on the EE: x from a - 90, y from a, each
  r = (90 - b) x 0.0174532924 - (pi when b < 0) + trunc(b / 180) x pi, times -1 when trunc(b / 180) + (-1 when b < 0) is odd;
  then VU0 micro program 0x1D0 (the 0x398-byte micro code at 0x349600, uploaded by 0x250820) computes
  ((((x^3 c1) + x^5 c2) + x^7 c3) + x) + x^9 c4 (c1..c4 = 0xBE2AAAA4, 0x3C08873E, 0xB94FB21F, 0x362E9C14, the libm sinf's
  terms, in another order: the accumulator starts with the x^3 term) on both lanes: (sin a, cos a). So sin 0 is a few
  units of 1e-7, not 0 (180 x 0.0174532924 is not quite the program's pi).
- RotX / RotY / RotZ (0x257F60 / 0x257FA0 / 0x257FE0) post-multiply the current matrix (columns vf28..vf31) as glRotate,
  each new column a multiply then a multiply-add; Translate (0x257EB4) sets column 3 to ((c0 x + c1 y) + c2 z) + c3.

### T3. Burst (0x19F450)

- h = sqrt((vx vx) + (vz vz)). Then per particle until count are made: one from the free list's head onto the active list's
  head (0x19F640; newest first; an empty free list ends the burst: no particle is ever taken back), then six draws of the
  VU0 random register R (0x245D5C) in this order:
  1. a = ((R - 0.5) x 0.0174532924) x 10: a turn of +-5 degrees about the vertical;
  2. k = R (0.699998 - 0.299998) + 0.299998; s = sinf(a) k, c = cosf(a) k (0x26A5F0 / 0x26A1A0);
     vx' = (c vx) - (s vz), vz' = (s vx) + (c vz);
  3., 4. vy' = vy + ((((R3 + R4) x 0.5) - 0.5) x h) x 0.199999: up or down by a triangular +-0.1 h;
  5. w = R (0.029998 - 0.009998) + 0.009998 (the half-width, m); mass = ((w w) w) x 500;
  6. life = R (1.999997 - 0.599999) + 0.599999 (s).
- The record (0x19E6F0, 0x54 bytes): +0x08 the burst point (no spread), +0x18 v', +0x24 1 / mass, +0x28 w, +0x2C the plane,
  +0x3C life, +0x40 life (its whole life), +0x06 min(trunc(|v'| x 1024), 0x7FFF), flags +0x04 cleared; the colour word
  +0x44 (0xFF229EFF) is never read.

### T4. Update (0x19F698 -> 0x19E7C8, every frame, dt = 1/60)

- From `RacePS2Base::vf29` after the emission, the cars' animators, the flares, the tyre smoke's update and the course's
  rigid bodies (none of which draws R), over the active list newest first; a new particle moves in its own frame. The view's
  camera cut byte (camera manager view record +0x302) clears every particle's flag bit 0 (0x19EEE8).
- Per particle: life -= dt; below 0 it is freed (back on the free list's head, 0x19F670). Position += v dt (the old v).
  K = ((-w) w) 0.349999 (dt / mass): dv = K v |v| per axis, and -9.81 dt on the height. Age = whole life - life: while
  0.089998 < age < 0.109999 (strictly) one more draw of R per frame, and when R < 0.899999 a lift of
  ((0.024999 speed0) / 1024) (dt / mass) (speed0 = +0x06). v' = v + dv.
- On the new position, dist = ((x nx + y ny) + z nz) + d; below the plane (dist < 0): life x 0.599999, the point back onto
  the plane, vn = v'.n, vt = v' - vn n, q = ((2e) mu) vn / sqrt(|vt|^2) (rsqrt.s), s = 1 - q, and v = -e v' when s < 0,
  else vt s - e v' (e = mu = 0.499998). No other ground, wall or car.
- The register's draws in a frame: the race step's (the tyre smoke emitter's six per particle, the flaps), then the cars'
  bursts (six per particle), then the update's kick draws. Sparks change nothing physical, but every later flap and smoke
  puff depends on the register.

### T5. Draw (0x19F738, from `RacePS2Base::vf45` only)

- After the cars, the rear-view mirror and the flare set-up, before the 3D picture's copy, the motes, the tyre smoke and the
  glare: the one-player view only. The mirror (vf54) and the 2P halves (vf48) draw none.
- `race/particle.img` (16x16 PSMCT24 grey radial glow, TCC 0, MODULATE, bilinear, clamped), depth test on, depth writes off,
  culling off, ALPHA 0x68 with FIX 0x80: Cs + Cd (added; alpha unused). Fog and the alpha test are left as earlier passes
  left them: off [I].
- Per active particle: E = the view matrix x position. With flag bit 0: a quad from last frame's E (+0x48) P0 to this one
  P1 in eye space: L^2 = dx^2 + dy^2; 0: a square of 2w round P0; else e = (dx, dy) w / sqrt(L^2), n = (-ey, ex),
  P0 - e + n, P0 - e - n (z0), P1 + e + n, P1 + e - n (z1), UVs (0,0), (1,0), (0,1), (1,1), a triangle strip. Without it,
  only the bit set (a particle is first drawn in its second frame). Then +0x48 = E. The camera's own motion streaks too.
- Colour (0x19EDA0) by life remaining L: the first of the keys (t 0.8: 1, 0.6, 0.2; 0.4: 1, 0.5, 0; 0.15: 0.8, 0, 0;
  0: 0.2, 0, 0) with t < L; key 0 itself, a later key i blended f = (t(i-1) - L) / (t(i-1) - t(i)) from key i - 1; none: 0.
  Each channel x 127.5 truncated (alpha 1 -> 127), one colour for the four vertices.
- During the pause / post-race stage the particles stand still and the streaks fall to squares once both ends are the
  same [I].

### T6. Ours

- `sim::exact::Sparks` (`sim/exact/sparks.h`): the pool, lists, burst, update and emission in EE arithmetic, the matrix library
  and the micro program's polynomial (`vu0_sin_cos`); `ExactRace::step` runs the bursts and the update after the race step
  (`RaceSetup::sparks`: each entry's model's spark points; `RaceRecord` version 4 keeps them, older records play without
  sparks, as they ran). Posed (online remote) cars make none: their records have no drawn pose. Checked bit for bit
  against the original in the diff harness (`sparks_burst_update_exact`; `sparks_emit_exact`: emission and update on an
  F090/S's frames round Trial Mountain, Grand Valley and Rome, the micro program run from the program's own micro code).
- `race::SparkDraw` (`race/sparks.h`) draws them in the one-player view (and the VR view from the head), after the cars and
  before the tyre smoke. Between race frames the particles are blended like the cars, and a streak starts where the particle
  was in eye space one race frame (1/60 s) earlier, from its last few drawn positions, so it spans one frame at any display
  rate; a cut of our camera (its mode, TV camera, director record or procedural cut, look-back) drops the saved positions.
  While the race stands still the streaks keep their last length (the original's fall to squares [I]).

## D. Differences to the repo notes / what to change

1. Licence result "NO PRIZE": original shows "Time Over" (slower than bronze; the time still
   shown at y = 140) or "Disqualified" (fail, no time), after a **1 s delay**, with a bluish wash and
   a split text that merges to y = 224. The medal word ("GOLD" etc.) and the model only exist for gold/silver/bronze.
2. Medal model: not "from 0.6 s". It starts after the jingle wait: 30 frames of dimming + 180 frames
   (gold) or 30 frames (silver/bronze), the animation then runs 0..120 fly-in and loops 120..320 (one turn / 200 frames).
   Model, word and time all fade in (60 / ~31 / 60 frames).
3. The whole scene behind keeps running; the HUD is not hidden; the dim reaches only 1/3 black.
4. Presenter total ~ 14.2 s (gold) / ~11.7 s (silver, bronze), last 5 s skippable with confirm; then
   quick menu with a looping replay in the preview box (32,58,280x210).
5. Licence fail presenter length: 60 + ~31 + 300 + 48 frames after the failure (jingle at once).
6. Time-trial fail: stop driver mode 2 at once, jingle at once, message after 60 frames, ~31 + 300
   frames of message, then 240 frames (4 s) unless confirm, then exit.
7. The live start shot (start.cam, §G5): the original shows it in every live view during a
   standing-start hold (C - 60 >= 0: races 600 frames from t 0.1, standing licence tests 240
   from t 0.7, machine tests their event's seconds; the 2P battle has no hold) and dissolves 1 s
   at its cuts (C - 60 = 120, 60) and at the cut to the driver's camera (C - 60 = 0).
   Implemented 2026-10-22 (ours had shown it in single-player races only, without dissolves).
