# Race presentation

This document describes how GT3 Revisited presents a race: the start and finish
sequence, the HUD, the pause window, the results and prize screens, replays and
the Replay Theater / Analyzer, the cameras, lighting and sun glare, lamps, the
wet road, tire smoke and the other effects, and which race events trigger which
sounds. It covers the presentation side of `src/race/` (not the AI or physics)
and the race screen in `src/app/screen_race_*.cpp`.

Sections are lettered; code comments cite them as `presentation §X`.

Other topics are documented elsewhere: physics (`physics.md`: starts, the
spawn and race holds, tire wear, smoke emission, course-out), the AI
(`ai.md`), menus (`menus.md`), GT mode (`gt_mode.md`) and the music and sound
synthesis (`music.md` and the sound documents). This document only says *when*
a race asks for a sound or a music cue.

## Conventions

- **Frames.** Timelines run at 60 frames a second, the simulation's tick rate.
  "Frame f" counts from the event named (the finish, the result, the page
  opening). Seconds and frames are both used: 60 frames = 1 s.
- **The 640 x 448 frame.** The HUD, results, pause and menu layouts are laid out
  on the game's 640 x 448 frame (y down unless stated). The menus and overlays
  draw that frame into a centered 4:3 rectangle. The HUD instead uses the game's
  16:9 layout on the 640 x 360 UI, `x' = (x + 106.667) x 0.75`,
  `y' = y x 360 / 448`. An element whose alignment has the +4 flag is first pushed
  106.667 units outward (left-aligned ones left, right-aligned ones right). With
  `[video] original_res = on` the HUD is drawn in the 4:3 frame without the push
  (`HudData::four_three`). Settings → Display → Aspect ratio changes the game
  area: at 4:3 (480 x 360) the HUD is the game's 4:3 layout, without the
  push; with Fill screen the area takes the window's shape and the push is half
  of what it has beyond the 4:3 frame, so the pushed elements reach its sides.
- **Colors.** A game color is the drawn color. "Amber" is RGB (208, 128, 24),
  "HUD red" (240, 0, 0).
- **Ours.** Where the engine adds something the game does not have, or fills a
  gap the sources do not settle, the text says "ours". "Observed" means that the
  value was matched by playing the game, not taken from its data.
- **Approximations.** Where the code marks a model as a placeholder or an
  approximation, so does this document. Those parts are conventional models, not
  descriptions of the game.

---

## §A License tests

A license test is a single car on its test layout, judged every tick, with its
own result presentation. The race screen runs it (`src/app/screen_race_license.cpp`,
rules in `src/race/license.h` / `license.cpp`). The judge's rules are summarized
in §A2 because the presentation depends on them.

### §A1 No FINISH message

A license pass shows no `finish` image (§B5). A failed run shows none either.
The run goes straight to the result presenter (§A4). The HUD decides this in
`RaceScreen::hud_data`: `finish_t` is set only for races and time trials that did
not fail.

### §A2 The judge (summary)

From the event record (database table 32; offsets as in `license.h`):

| Field | Meaning |
|---|---|
| +0x19A | test kind: 1 stop test, 2 section, 3 lap(s) |
| +0x199 | laps (kind 3) |
| +0x1C4 / +0x1C8 / +0x1CC | gold / silver / bronze targets, each {u8 min, u8 s, u16 ms} |
| +0x1F3 | start type: 1 standing (4 s hold), 3 flying |

- **Clock.** The race clock counts in 1/3000 s, +50 per tick from GO. Checks see
  the value from before this tick's advance. An event inside a tick adds
  trunc(50 f), where f is the event's fraction of the tick. Then
  ms = floor(units / 3) - start. Standing tests start at GO (start 0). Flying
  tests start at the first line crossing, while the computer drives the run-up
  (AUTO DRIVE).
- **Ends.** The race judges the test itself (its license rules,
  `src/sim/exact/race_flow.cpp`). Kind 1 ends on the first tick on which all
  four wheels are on stop-zone triangles (road flag 0x04) and the body's 3-D
  speed is at most 1 km/h (1.5 km/h under the PAL release's rules). Kind 2 ends
  when the car crosses the course's split line. Kind 3 ends on the line crossing
  that completes the event's laps.
- **Failures** end the run at once. They are: all four wheels on grass, gravel or
  sand (not on a dirt course); all four wheels on the pit lane within its stop
  line; a wall hit that changes the speed by more than 10 m/s; or any touch of a
  pylon. Wrong way, car contact and stopping never fail a test.
- **Medal**: the judged time in whole milliseconds, compared with gold, then
  silver, then bronze (limits inclusive). A time slower than bronze ends the
  run as TIME EXPIRED: it finishes with its time but no medal. Only the failures
  above end as a FAIL, with no time. No license test on the disc has a time
  limit, so a time slower than bronze is the only way to TIME EXPIRED.

After the run the computer drives the car (the end-of-run driver, `ai.md` §1.1):
a stop test that ends in the zone (with a medal or TIME EXPIRED) brings the car
to a stop on the main line; any other test that reaches its end drives on, slowly,
on the main line; after a course-out or a hard hit the car is stopped with the
wheels straight.

### §A3 License HUD

The lap / time block shows "Best", the elapsed "Time" (y 112) and the three
target times at y 140, 168 and 196, each with its `mark_1st` / `mark_2nd` /
`mark_3rd` icon drawn at 75 % scale left of the time (`Hud::draw_times`; the
license layout, §D4). There is no lap count, no position and no message display
(no WRONG WAY or AUTO DRIVE, §B4); the map sits at (16, 32).

**Best** is the best judged time of the visit: every run that passes the test
with a time counts (a medal or TIME EXPIRED), from the tick it ends; a FAIL or a
run left with EXIT does not. Retries keep it; a new visit starts with dashes.
A license test's laps never count: its only "lap" is the judged time (§D4).
Replays and the demonstration add nothing.

The time shown follows the judge's clock:

- a standing test from GO;
- a flying test 0 until the first line crossing, then from it;
- frozen at the judged time once the run finishes (TIME EXPIRED included);
- dashes (`-'--.---`) after a FAIL (the HUD is given no time, which the time
  formatter shows as the font's long dashes, §D10).

### §A4 Result presenter

The race keeps running behind the result: the car is driven by the end-of-run
driver (§A2), and the race camera and HUD stay. Frames count from the tick the result is
decided (`RaceScreen::result_frame`, `LicenseResultOverlay`).

**Medal (gold, silver, bronze):**

| Frame | What happens |
|---|---|
| 7 .. 37 | black dim rising from 0 to 1/3 over 30 frames |
| 37 | the medal jingle starts (`RaceMusic::kMedalDelay` = 37/60 s) |
| M = 37 + 180 (gold) or 37 + 30 (silver, bronze) | the medal model `race/gold.prm`, `silver.prm` or `bronze.prm` fades in (composite weight over 60 frames); its own animation starts then (fly-in, then its loop; `render::PrizeView`) |
| M .. M + 30 | the word GOLD / SILVER / BRONZE fades in, sw_20 at 1.25 x, centered at (320, 336): a black shadow, then the color copy at (+2, -2) |
| M .. M + 60 | the time fades in, same color, centered at (320, 112) |
| M + 31 .. M + 331 | hold |
| M + 331 .. M + 631 | Circle or Cross ends the presentation |
| M + 631 (+2) | the presentation ends |

Medal colors: gold (255, 215, 0), silver (224, 238, 224), bronze (208, 150, 122).
Measured with a scratch program over the engine's constants: gold ends at frame
848 (14.1 s) and Circle / Cross work from frame 548; silver and bronze end at frame 698
(11.6 s) and Circle / Cross work from frame 398.

**No medal (TIME EXPIRED or FAIL):**

| Frame | What happens |
|---|---|
| 0 | the fail jingle (§M2) |
| 60 .. 90 | a (160, 160, 255) wash rising to about 50 % alpha |
| 90 .. 121 | the text in (160, 192, 64), two copies 30 units above and below y 224 that merge (offset floor((1 - t) x 30)), alpha reaching full half-way |
| 90 .. 150 | TIME EXPIRED only: the time fades in at (320, 140) |
| 121 .. 421 | hold |
| 421 .. 469 | 48 more frames; the license presentation cannot be skipped |

The text comes from `text/realtime.dat`: `RACEDISPLAY_ExamTimeOver` ("TIME
EXPIRED", with the judged time) for a run slower than bronze, and
`RACEDISPLAY_ExamFail` ("FAIL", no time line) for a run that left the course or
hit hard (`race::license_outcome`; tests `race_license_outcome`,
`exact_game_licence_over_the_times`). The wash rises to half alpha and stays there.

A time-trial FAIL (§B7) uses the same wash and text (`RACEDISPLAY_TimeTrialFail`)
with no time line, then 240 frames that Circle or Cross can skip: from frame 421 to 661.

When the presentation ends, the race leaves to the quick menu through §C1. A
license run plays no replay by itself (§A8).

### §A5 Recording a license run

- A run's medal goes into the GT-mode save at once
  (`game::save_license_run`), so it is kept even if the game closes first. A
  TIME EXPIRED run has no medal and saves nothing.
- The result to the menus is the last run, unless an earlier run of the visit
  earned a better medal (or the last run did not finish): then that earlier run
  is reported. So a Retry or a replay afterwards cannot lose a medal already
  earned (`RaceScreen::build_result`).
- A medal run goes into the test's ranking at the end of the race-exit
  transition (§C1); one that makes the ranking asks for a name over the quick
  menu just opened (§A8) (`RaceScreen::record_run`, `game::add_license_record`, a GT-mode
  presentation; §B7). The result posted to the menus adds no record.

### §A6 License quick menu

License tests open with `qm_gt_license1.pmb` before the first run and after
every run. The test's name ("B-3", from the event code, `license_title`), its
three target times and the test's description panel (the page layout named
after the event) fill the page.

### §A7 The preview box

The license quick menu draws the live race scene into the preview box at
(32, 58), 280 x 210 of the menu frame, and nothing behind the rest of the page
(`render_drive`). Before any run, the box shows the disc's demonstration of the
test (§G12), looping: it restarts 180 ticks after the demonstration's inputs run
out. After a run it replays the run just driven, looping (`start_preview`); after
a run left with the pause window's EXIT it shows the demonstration again (§A8).
The box is silent, like the race behind every quick menu: only the menu's music
and sounds play. What the other quick menus draw behind them is in §C5.

### §A8 After a license run

- The license presentation (§A4) ends through the race-exit transition (§C1).
  Then the quick menu opens at once: a license run plays no replay by itself,
  unlike the other runs (§C4). A run that made the test's ranking asks for the
  name over that menu (§A5).
- REPLAY on the menu plays the run once, like the automatic replays (§C4): only
  Start opens its pause, and its end or the pause's EXIT goes back to the menu
  through the §C1 transition.
- **A run left with EXIT.** After a run ended by the pause window's EXIT, the
  disc's demonstration takes the run's place: the preview box shows it (§A7), and
  REPLAY and SAVE REPLAY are disabled until the next run ends (§C2,
  `race::QuickMenuItems::after_run`). REPLAY does not play the demonstration.
- **The best run and the ghost** (§G11): a run that ends with a time (a medal or
  TIME EXPIRED) at or under the visit's best time becomes the visit's best run,
  and the ghost when it beats the ghost.

### §A9 The course line

Some license tests show a dashed driving line on the road along the test's
section: 0.4 m wide, about 0.1 m above the road, mostly blue, turning white and
then red at the braking zones (`docs/formats/course.md` "Course line"). The line
is part of the course file (slot 27), not built at run time: 13 license course
files have one. It is shown whenever the loaded course file has one, and there is
no other switch. The course file comes from the test's event, so the tests that
show it are B-3 .. B-8 and A-1, A-2, A-3, A-6, A-7, A-8 (12 of the 48 tests; test
`race_license_course_line_disc`).

- **Drawing.** `course::course_line_model()` reads it as a model of its own.
  `race::CourseScene` leaves it out of the scenery and draws it right after the
  scenery, the scenery's blended passes included: opaque (no blending),
  double-sided, without depth writes, keeping the frame alpha, never
  distance-culled (`CourseScene::add_items`; tests `course_line_all_us`,
  `course_line_synthetic`).
- **Where.** In every view of the course: live runs, demonstrations (§G12), the
  preview box (§A7) and replays in every view mode (§G2).

---

## §B Race start and finish

### §B1 Start types and holds

The event record's start type (+0x1F3, `race::event_start`) chooses the start:

| Type | Start | Hold | Countdown shown |
|---|---|---|---|
| 0 | demo / machine test | the event's countdown seconds (+0x1F2) for machine tests; demo races start moving at their speed with no hold | machine test: 3-2-1 |
| 1 | standing (license tests from rest) | 240 frames (4 s) | 3-2-1 |
| 2 | standing (every single race) | 600 frames (10 s) | 3-2-1 |
| 3 rolling (no initial speed) | GT endurance: every car leaves its slot at 80 km/h | none; the computer drives the player for 300 ticks (5 s, `rolling_runup`) | START at the hand-over |
| 3 flying (initial speed > 0) | time trials, free runs, flying license tests | none; the computer drives the run-up to the line (AUTO DRIVE, `flying_runup`) | START at the line |

The 2P battle has no countdown: the clock runs from the start. During a hold the
cars are held by the physics' race hold, and the lap clock starts at the signal.
In races the field's race time is negative during the hold and 0 at GO.

The start signal (GO) is the end of the hold; for a rolling start, the hand-over
after the 300 ticks (counted in ticks, so it cannot drift by a frame); for a flying
start, the start-line crossing that ends the computer's run-up
(`RaceScreen::flying_start_signal`). The free run's event (ATAF0001) starts at
80 km/h from the line itself. The free practice before a GT race is not a flying
start.

### §B2 Countdown and START

- The HUD shows the `count3`, `count2`, `count1` images over the last 3 s of the
  hold, centered at (320, 170). After the signal (§B1) it shows the `start` image
  for 1 s: from GO, from a rolling start's hand-over, or from a flying start's line
  crossing (`hud_data`).
- The countdown beeps (§M1): `count_d` when the count shows 3, 2 and 1, and
  `count_s` at the signal (rolling start: at the hand-over; flying start: at the
  line). There are none in replays. A flying start's signal (the `count_s`, the
  START image and the free-run notes below) comes in live runs only: not in
  replays, demonstrations or the attract demo.
- Races with a standing hold play the start jingle while the cars are held;
  the race music starts at the signal (§M2). The jingle starts on the hold's first
  frame, together with the pre-race shot (§B3). A run entered through its pre-race
  quick menu (or a Retry from the result menu) starts its hold when the menu
  closes: the menu music stops and the jingle starts on that frame. Replays re-run
  this sequence: the start jingle over the replayed hold, the race music from the
  replayed signal (the beeps and the START image stay live only).
- Rolling and flying starts have no jingle. The music stays silent through the
  computer's driving and the race music starts at the signal: at a rolling start's
  hand-over, at a flying start's line crossing (live runs and replays;
  demonstrations and the attract demo do not wait for the line).
- **Free-run notes.** A free run's flying signal also queues two notes in the
  event display (§D6): "Timer starts at start/finish line"
  (`RACEDISPLAY_FreeRunReady`) 2 s after the signal, then "Press START button to
  end session" (`RACEDISPLAY_FreeRunHelp`), 3 s each: from 2 s to 5 s and from 5 s
  to 8 s after the signal (`EventDisplay::after`, test `race_event_display_timing`).

### §B3 The pre-race shot

Every live run with a standing-start hold (§B1) opens on the disc's starting
shot (`cameras/start.cam`, a one-record camera script in the car's frame;
`CameraRig::set_start_shot`): races, license tests from rest and machine tests.
Replays, demonstrations, the attract demo and the menu backdrops do not play it
(`RaceScreen::live_view`), and the 2P battle has no hold. With C = the frames
left to the signal (`RaceScreen::frames_to_signal`), the shot is shown while C is
60 or more, so until one second before the signal, at

  t = 1 - (C - 60) / 600

(`race::start_shot_on`, `race::start_shot_t`; test `race_camera_views`). So a
race's 10 s hold starts the shot at t = 0.1 and a license test's 4 s hold at
t = 0.7; a machine test's starts by its countdown. The record's eye curve has
three cuts, checked by test `race_start_cam_disc`:

1. t 0 .. 0.8: a straight move from 30 m ahead (3.0, 2.0, 30.4) to about 6 m ahead.
2. t 0.8 .. 0.9: a low sweep across the back of the car, about 4 m behind it,
   from one side to the other.
3. t 0.9 .. 1: across the front, ending at the front corner (-1.6, 0.6, 4.0).

The target curves aim near the car's center. The zoom is 2.414 (45 degrees
across the 640 x 448 frame). Without the file the engine moves straight from
(3.04, 2.04, 30.44) to (-1.64, 0.59, 3.97) while looking at (0.65, 0.4, 0).

**Dissolves.** When C reaches 180 and 120 (t 0.8 and 0.9, the shot's own cuts)
and 60 (the cut to the player's view), a 1 s dissolve from the last picture
starts (§G16; `race::start_shot_dissolve`), in live views only. A hold shorter
than one of those points has fewer.

During the shot the camera counts as outside the car: the own car is drawn, the
chase HUD cluster is used and there is no mirror.

**The course title.** The course's name from the game's course table (license sections
have their own, such as GRAND VALLEY TEST SECTION), in capitals, is drawn over the start of
every live run and every replay, on its own clock (`race::TitleClock`, started by
`RaceScreen::start_hud_clocks`; test `race_title_clock`), not tied to the
pre-race shot or any camera. It is not drawn in the attract demo, the menu
backdrops or 2P, nor for a course without a name or whose name starts with "-". The clock advances
with each race step, so it stands still while the race is paused. In a live run
the countdown, START and FINISH take its place: once one of them shows, the title
is gone for the run. A race's 10 s hold lets the long form play out first; a
4 s license hold starts the count at 1 s, so the title is cut off at once.
Replays and demonstrations keep it. Times count
from the run's or replay's start; both fades are quadratic (the opacity is the
square of the linear ramp):

| Form | When (`RaceScreen::long_title`) | Shown | Fade in | Fade out |
|---|---|---|---|---|
| long | runs from rest under the race rules: standing-start races, license and machine tests from rest | 1.0 s to 5.0 s | 0.3 s | the last 1.0 s |
| short | moving starts (rolling, flying), and the time trial, free run and free practice | 0.2 s to 2.2 s | 0.1 s | the last 0.3 s |

Replays draw it whatever REPLAY OPTIONS ITEM says (`Hud::draw_title`). The name is the
table's game text: only the letters a to z are made capitals, and accented letters and
symbols keep their own glyphs (Côte d'Azur's ô is a Latin letter of the game font, its
apostrophe a character of the kanji font), so it reads CôTE D’AZUR. It is drawn
in sw_20 at 160 %, light gray (208, 208, 208) at the clock's opacity, centered on
x 320 with its baseline at the layout's title place (y 160; 150 in the free
practice). A name wider than the layout's title width is squeezed horizontally to
it: 500 units in races, the time trial, free runs and license tests, 480 in the
0-400 m / 0-1000 m tests and the free practice, 400 in the endurance layout and
the top-speed test (the Title row of §D4's layouts).

### §B4 AUTO DRIVE

"AUTO DRIVE" (amber sw_16, centered at (320, 305)) is shown while the computer
drives the player's car: in the pit lane, during a flying run-up, and on a
finished 2P player's half. It is shown only in the layouts with a message display
(§D4): never in replays or the attract demo, nor in license tests (so not during a
license test's flying run-up) or the top-speed machine test. WRONG WAY takes the
same place and wins over it.

### §B5 The finish

- A race, time-trial or machine-test finish shows the `finish` image for 4 s, centered
  horizontally with its top edge at y 160, the layout's title place (`Hud::draw`,
  `finish_t`). There is no
  image for license tests (§A1) or a failed time trial. A pause EXIT that counts
  as the run's finish (§E5) shows it too.
- After the finish the computer drives the player's car (the end-of-run driver,
  `ai.md` §1.1): after a race, a time trial or a machine test it drives on along
  the main line, more slowly.
- The laps stop counting at the finish (`lap_freeze`), but the HUD's lap clock
  keeps running.

### §B6 Results overlay (races)

Five seconds after the player finishes, the RACE RESULTS panel from
`menu/<LANG>/result1.pmb` (page "result") is drawn over the still-running race
(`ResultsOverlay::draw`, t = seconds since the finish):

| t (s) | Element |
|---|---|
| 5 | the title bar ("RACE RESULTS", top at y 44) fades in over 0.5 s |
| 6 + 0.12 i | rank bar i (0-based) fades in over 0.4 s and unrolls downwards out of the bar above |
| 6 + 0.12 n | the closing "OFFICIAL TIMER SEIKO" bar, fading in over 0.5 s |

The unrolling works like this: each bar's center approaches the point
(h_prev + h_own) / 2 below the previous bar's center, covering 19 % of the
remaining distance per frame. The engine evaluates that in closed form:
offset = 1 - 0.81^frames. Nothing is drawn above the previous bar's center line.

Each row shows the rank number (rank font at half size, centered at x 42), a car
picture (58 x 26 at x 66, §B8), the car's name (sw_16 at 87 % width, from x 144)
and the time column (right edge 600). The player's row is (255, 200, 0), other
rows (204, 204, 204). Names include the car's second name string (for example
"ALTO WORKS SUZUKI SPORTS LIMITED(J)"). A name is the database's game text, drawn
as it is: an accented letter is the game font's, and the R34s' middle dot
("SKYLINE GT-R V・spec(R34,J)") is the kanji font's (`race::result_car_name`, test
`race_result_car_name_disc`). The quick menus' lists, the replay's car name (§D9),
"<car> Pitting in..." (§D6), the records list and the Analyzer show the name the
same way. The time column (`result_time_text`, test `race_results_text`):

| Car | Text |
|---|---|
| winner | " m'ss.mmm" (hours as "h:mm'ss.mmm") |
| finished on the winner's lap | "+m'ss.mmm" behind the winner |
| finished laps down | "+1 lap", "+N laps" |
| still racing | "Running..." |

The HUD's lap / time block is drawn over the panel and the rest of the HUD under
it (`HudData::layer`).

The panel is fully shown at 7 + 0.12 n s: 7.72 s for six cars
(`ResultsOverlay::fully_shown_time`). After that, Circle or Cross leaves. If every car has
finished, the race also leaves on its own 15 s after the last one did. Leaving
goes through §C1.

### §B7 Time trials and machine tests

- The arcade time trial is one timed lap (`race::kTimeTrialLaps`), whatever the
  event's lap count says: the race ends the run on the tick the car completes its
  first lap (a flying start's first line crossing only starts it;
  `race::solo_run_finished`, tests `race_time_trial_finish`,
  `race_time_trial_one_lap`, `exact_game_solo_modes`). The laps freeze there
  (`lap_freeze`) and the run ranks its best lap, that lap. Free runs (no laps)
  never finish.
- **Machine tests** (`race::MachineTest`, tests `race_machine_test_marks`,
  `race_machine_test_events_disc`). The event's lap-count byte (+0x199) is the
  test: 1 = 0-400 m, 2 = 0-1000 m, 3 = top speed; 4 or more reads as 0, which
  runs like the top-speed test. A machine test runs no laps.
  - Its timing points are the first 1, 2 or 3 of 400 m, 1000 m and 6845 m, track
    distances past the control line (the lap timer's track progress, not the
    distance driven). The last one is the goal: 400 m, 1000 m, or 6845 m for the
    top-speed test. The disc's three test courses are 10342.3 m a lap with the
    grid's pole 0.06 m before the control line, so every goal comes on the first
    lap.
  - Distances count only once the car is past the line. The split points are
    taken in order, at most one a tick, and start again after every forward line
    crossing; a tick that crosses the line times nothing. Line crossings
    themselves count for nothing (no lap, no lap time).
  - Times run from GO (the clock starts at the signal, §B9). A point passed within
    a tick is timed like a lap line: the clock before the tick plus the passed
    fraction of the tick (`LapTimer::crossing_ms`). These points time the
    pop-ups (§D6).
  - The race ends the run when the car crosses the course's split line, the
    test's goal (the laps freeze, `lap_freeze`; test `exact_game_solo_modes`).
    The time tests rank and record the race's time to it; the top-speed test
    ranks its top speed, the car's highest speed until then (or until a pause
    EXIT that counts as its finish, §E5).
  - The HUD (no lap count; "Best" and "Time", or the top-speed test's two speed
    lines) is in §D4, the pop-ups of the 0-400 m / 0-1000 m timing points in §D6.
    The top-speed test shows no pop-ups, WRONG WAY or AUTO DRIVE in live play;
    its replays show its split times and its top speed at the goal (§D6).
- Five seconds after the end the overlay (`TimeTrialOverlay`) shows, in amber with
  a black shadow at +2:
  - time trials: "Lap" (sw_16, at (200, 270)) and the best lap (sw_20 at 1.3 x,
    right edge 440);
  - 0-400 m / 0-1000 m: "Lap" and the time to the goal, in the same places;
  - the top-speed test: "Max Speed" (sw_16, at (160, 270)) and the top speed
    (sw_20 at 1.3 x, right edge 480) as "312.4km/h" or "124.2mph" (mph = km/h
    x 0.62139), whole units and tenths both truncated, "---.-" (the font's long
    dashes, §D10) and the unit for none (`race::record_speed_text`, test
    `race_record_rank`). The unit is
    the HUD's speed unit (§D1). Stored top speeds are shown the same way on GT
    mode's records page (there in the game data's unit); the quick menu's Top 3
    rounds them instead (§C2).
- **Records.** At the finish the run takes its place in its stored records
  (`RaceScreen::rank_finish`, `race::record_rank`, test `race_record_rank`): the
  GT-mode ranking of the arcade time trial or machine test, best first, 12 places.
  The place is the first one that is empty or whose entry is not better than the
  run, so a tie ranks ahead of the stored entry; past the 12th the run has no
  place. Times rank lowest first, the top-speed test's speeds highest first. Runs
  without a ranking (Quick race practice, debug runs) rank as in an empty table
  (ours). A free run finishes only by a pause EXIT after a completed lap (§E5):
  then its best lap takes its place in the course's ranking and the overlay shows.
  Otherwise it has no place or record overlay; its lap goes in when it ends
  (below).
- **Storing the run.** Every finished machine test, and every finished arcade
  arcade time-trial challenge (its best lap; not after a FAIL or an EXIT before
  the lap),
  is written into its ranking at the end of the race-exit transition (§C1), as is
  a license run with a medal (§A5) (`RaceScreen::record_run`,
  `game::add_machine_test_record`, `game::add_time_trial_record`). So is a free
  run (the arcade time attack, GT Mode's free run; `RaceScreen::course_time_trial`)
  with a best lap, however it ended (pause EXIT): that lap and the run's car go
  into its course's time-trial ranking, "ct:<course>", 12 places, a tie ranking
  ahead (`game::add_course_time_trial_record`). Only the courses of the game
  data's TIME_ATTACK list have one; on other courses nothing is stored. Replays,
  demos and practice runs record nothing, and a run is recorded once. Nothing
  else records a run: the result posted to the menus when the race screen is
  left adds no record (license tests, machine tests, time trials and free runs
  alike). A run that
  ranks asks for the name (a GT-mode presentation) over the race before the
  run's replay (§C4; a license run: over the quick menu just opened, §A8); the
  menu's Top 3 is read again before it opens, with the name in it (§C2).
- With a place, the overlay also shows the rank "1st" .. "12th" (rank font,
  (320, 150)) and a label (sw_20, (320, 200)) that flickers every frame between
  (204, 102, 0) and (208, 208, 208): "NEW RECORD" for the 1st place, otherwise
  "RECORD TIME", or "RECORD SPEED" in the top-speed test
  (`TimeTrialOverlay::record_label`).
- The stored records feed only this rank and label (`RaceScreen::stored_records`).
  Whether RETRY grays out is decided against the session's best lap instead
  (the session record, §E3); the session's best lap (Record Lap, P.P., Fastest,
  §D4, and the time trial's gap reference, §D6; `race::BestLapSplits`) holds only
  laps driven in this visit (not replay laps), and no stored record is loaded
  into it.
- The time trial or machine test leaves on Circle or Cross from 5 s after the
  end (once the overlay shows), or by itself 10 s later, at 15 s
  (`TimeTrialOverlay::ends_race`, test `race_time_trial_finish`). A free run
  finished by EXIT keeps these times; after the top-speed test's EXIT finish the
  overlay shows 0.1 s after the press and the run leaves by itself at 10.1 s
  (§E5). Leaving goes through §C1.
- **The posted result.** A time trial's result is finished only if the run
  finished at the line without a FAIL, and its time is the run's best lap. An
  EXIT before the lap posts an unfinished run and records nothing. A machine
  test is finished at its goal; the top-speed test also by an EXIT that counts as
  its finish.
- Time trials apply course-out (physics topic): the run fails, no lap is
  recorded, the car is stopped with the wheels straight (`ai.md` §1.1) and the
  FAIL presentation of §A4 runs. A pylon touch stops a time trial the same way. With
  the display option off (`hud=0`) the FAIL and record overlays are not drawn.

### §B8 2P battle finish

- Each half follows its own player. A finished player's half shows AUTO DRIVE,
  then the RACE RESULTS panel at full-screen size from the top of the half,
  clipped to it, while the other player races on. The panel's timeline (§B6)
  counts from the race's first finish, so the second player to finish may get
  it at once.
- The 2P rows show car names only (no highlighted player row), a car picture per
  row, and "Running..." for a car still racing. The pictures are the car's model
  rendered on the panel's blue, from the front left (`draw_car_icon`; ours).
  One-player races show the same pictures
  on the bars.
- There is no "PLAYER n WIN!" text: the split-screen HUD has none.
- The race ends when the panel is left (as in §B6), 15 s after it was fully shown,
  or by the pause window's EXIT, whether or not the other player has finished.
  Then come the §C1
  transition and the split-screen replay of the race. It plays every time,
  undimmed, and Cross or Circle do not skip it (only Start opens its pause, §C4).
  Then the 2P RESULTS quick menu opens.

### §B9 Race order and finish

The race order is the HUD position and the player's place. The race keeps it
itself, sorting the cars after every tick so that ties keep their previous
places (online races sort the standings by the same rules, `race::ranks_ahead`,
test `race_order_and_finish_rules`):

1. More laps completed first.
2. At equal laps, a running car ahead of a finished one.
3. Two running cars by race distance (laps x length + track distance).
4. Two finished cars by finish time.

The race decides the finish. A car finishes only at a start-line crossing, at its
first crossing at which one of these holds:

- it has completed the race's laps;
- the car ranked first has finished;
- the crossing is at or after the time limit (time-limited races, event +0x1E4
  minutes with 0 laps).

Cars are judged in race order, leader first, so a car crossing on the tick the
leader finishes also finishes. A lapped car, the player included, finishes at its
next crossing and is shown "+1 lap". Nothing ends at the time limit itself, and a
car that stops short never finishes. The finish time is the crossing's time, and
the car's laps stop counting there.

**Lap clock.** Lap times are the race's own (the player's laps reach the HUD
through `race::exact_lap_source`), on the race clock: 1/3000 s
units, a crossing timed at the clock from before the tick plus trunc(f x the
tick's units), where f is the tick's fraction at the line along track progress;
ms = floor(units / 3). Every time is whole milliseconds.

- Races time lap 1 from GO for every car, and the HUD's lap time runs from GO
  even before the car reaches the line.
- A standing license test also times from GO (shown once past the line).
- A machine test's clock also runs from GO (§B7); its HUD shows it from the
  start (0'00.000 during the hold, §D4).
- Time trials, free runs and flying tests time lap 1 from the first line crossing.

(`RaceScreen::reset_run_state`, `LapTimer::set_lap1_from_go`.)

**Flags.** There are no penalties, no lap invalidation and no "final lap"
message. WRONG WAY (red sw_16, centered at (320, 305); on a 2P half at its own
place) is shown while the lap timer reports the car going the wrong way, in the
layouts with a message display (§D4): never in license tests, the top-speed
machine test, replays or the attract demo.

### §B10 Loading the race

A race launched from the menus (or online) first shows the loading picture
(`RaceScreen::render_loading`; the menu side is `menus.md` §6): the menu's last
frame with the transition the menu left with still running on it, in the centered
4:3 rectangle over black. It is drawn on every frame, the set-up and then the
course and cars being loaded on later frames, so the picture is on screen before
each blocking part. A load that blocks drawing is caught up afterwards: the
transition advances by the frames the load took, at most 600 at once (test
`race_loading_picture_frames`). When no menu frame was handed over, the picture
is a fade-out over black. The race then appears as a cut.

**A race that cannot be built** (ours). When a race launched from the menus or
online cannot be set up (no program the race can run on, a run that cannot be
built), a black page says "The race cannot start." with the reason below it and
"Press any button to return"; Cross, Circle, Triangle or Start go back to the
menus (`RaceScreen::render`).

---

## §C Leaving a race

### §C1 Race to quick menu

The race-exit transition runs when a live run ends (results left, license
presentation over, or EXIT in the pause window), and again at the end of every
replay that goes back to a quick menu (its end, or its pause's EXIT): the
automatic replay (§C4) and the menu's REPLAY. The last frame stays up for 10
frames with nothing simulated. Then a very dark gray wash (12.8 / 255, the
game's 0.1) covers it, its alpha after n frames being 1 - 0.5^n, for 8 frames.
Then what follows cuts in: after a live run the steps of §C4, after a replay the
quick menu (`update_drive`, `begin_exit_transition`, `exit_transition_`,
`kExitHold` / `kExitWash`; the wash is the menus' delayed fade-out,
`ScreenTransition::delay_fadeout_cover`, test `race_exit_wash`). In VR the wash
covers the whole view. The live run's result is latched at that moment (§C3).
Online races go back to the lobby instead, and the free practice before a GT
race to its pre-race menu.

### §C2 Quick menus

The quick menus are the game's icon menus, run by the same page manager as the
other menus (`race::QuickMenu`). Some have the race behind them over the whole
frame, where their layouts are translucent; the result menus have none, and the
license menu has its preview box (§C5, §A7).

The layout depends on the mode (`RaceScreen::quick_menu_layout`). Some modes open
with it before the first run (`menu_before_run`):

| Mode | Before the run | After the run |
|---|---|---|
| arcade single race | - | `qm_arc_result1.pmb` |
| 2P battle | - | `qm_arc_2p_result1.pmb` |
| arcade time trial | `qm_arc_tt_ltd1.pmb` | the same |
| time attack / free run | `qm_arc_tt_free1.pmb` | the same |
| machine test | `qm_gt_mt_4001.pmb`, `qm_gt_mt_10001.pmb`, `qm_gt_mt_max1.pmb` | the same |
| license test | `qm_gt_license1.pmb` | the same |
| GT race | `qm_gt_event1.pmb` | `qm_gt_result1.pmb` |
| championship round | `qm_gt_event_champ1.pmb` | `qm_gt_result_champ1.pmb` |

- **Rows.** The pre-race menu lists every car in grid order with no time,
  "--'--.---" in the font's long dashes (§D10); after a free practice (FREE RUN) a
  GT race's lists each car's best lap of it in the new grid order (GT Mode page
  §22.5). The result menu lists the cars in race order with each finished car's
  own race time in the menus' format (§D10; not the gaps of §B6), and no time for
  a car still racing when the race ended. The player's row is (255, 116, 0).
- **Championship rounds.** The pre-race menu lists the cars in grid order (the
  player on the last slot, or the grid a free practice left), each with its points so far, "%d %s" with "pt" under
  2 points and "pts" from 2 ("0 pt", "10 pts"), in the player's color on the
  player's row. Both menus show the round, "#1 Race" on the NTSC-U disc (the
  realtime texts SHORTMENU_SESSION_BRFORE / _AFTER around the round number). The
  laps read "%d %s" with the realtime text's Lap (one lap) or Laps. The rows are
  filled by the layouts' widget classes (`race::quick_menu_column`).
- **Left by pause EXIT.** A GT race's result menu then lists every row as "---" with
  no time and no player color; a championship round's keeps its pre-race list (grid
  order, no times, the player's row in color) (`race::result_menu_list`).
- **Top 3.** Time trials, free runs (the course's ranking, §B7) and machine tests
  fill "Top 3" from the GT-mode ranking
  (`RaceScreen::fill_top3`), read again before every quick menu and after a run's
  name entry (§B7): name and time, or for the MMAX test the top speed as
  "%4.1f %s" in the HUD's unit, rounded ("218.8 mph"). There are
  always three rows; a place the ranking has not filled shows no name, no time
  ("--'--.---", §D10) and " 0.0" with the unit. The free-run menu (`qm_arc_tt_free1.pmb`) also
  shows each entry's car name, "---" for none.
- **Actions.** Items act on their `.mbl` action string (`race::quick_menu_action`):

  | Action | What it does |
  |---|---|
  | start / try again | the run again |
  | free run | the course alone before a GT race; on a course whose pit stop line has no pit box it is refused with "Free run is not available on this course." (GT Mode's such courses are its dirt courses, where FREE RUN is already off) |
  | replay | the run's replay (as in §C4), then the §C1 transition back to the menu |
  | demo | the disc's demonstration of the test (§G12), or without one the race's AI driving the car, from the TV cameras; REPLAY is disabled after it (below) |
  | save replay | write the run's replay file (a toast confirms) |
  | save ghost | write the replay of the visit's best run (a free run: the run of its best lap; §G11) |
  | load ghost | load a saved ghost at once (§G11) |
  | records | the run's GT-mode ranking (license, machine test, time trial; a free run's course ranking, with each entry's car name and icon); otherwise (Quick race practice) the best laps of the saved replays |
  | logger | the Analyzer over the session (§G10) |
  | option, setting | the option and setting panels |
  | next session | the next championship round |
  | save game | the save page |
  | exit | leave (championships ask "Retire from championship?" first) |

  Test `race_quick_menu_actions_disc` checks that every item of the disc's 12
  layouts has an action or a link (it reports 12 layouts, 97 items, 17 actions).
- **Disabled items** (`race::QuickMenuItems`, `RaceScreen::update_quick_menu_items`,
  `QuickMenu::set_items`; tests `race_quick_menu_items`,
  `race_quick_menu_disabled_item`, `race_quick_menu_disable_disc`). The root page's
  items are enabled by the visit's state (`menus.md` §11), reset when the race
  screen starts a visit and kept across its runs, retries and menus:

  | Items (`.mbl` names) | Disabled |
  |---|---|
  | REPLAY, SAVE REPLAY (`replay`, `save_replay`) | until a live run of the visit has ended; again after DEMONSTRATION (its run replaces the last one) and after a license run left with EXIT (§A8), until the next run ends |
  | SAVE GHOST, DATA LOGGER (`save_ghost`, `data_logger`) | until a run of the visit set a session record (§E3): a time trial's lap, a 0-400 m / 0-1000 m goal or a judged license run at or under the session's best, or a free-run lap under it (any first lap); races never enable them |
  | SETTING (`setting`, `save_setting`, `load_setting`) | always in time trials; in arcade races and free runs with a car that is not a garage car |
  | FREE RUN (`free_session`) | before a GT race on a dirt course (a championship round keeps it) |
  | EXIT, SAVE GAME (`exit`, `save_game`) | after a championship's last round has been run (NEXT then ends the championship) |

  A pause RETRY restarts within the run and changes nothing. Dialog pages have no
  rule. A disabled item stays focusable and the focus moves past it as usual. It is
  drawn under the layout's `disable` widget (black at alpha 0xB1, in the button's
  box), with its normal look unchanged; when focused, its highlight is drawn at half
  brightness under the shade. Confirm on it plays the menu SE "disable" and does
  nothing else. A scripted choice (`qm=<item>`) on a disabled item is refused the
  same way (logged "Quick menu: <item> is disabled"). Before a time trial's first
  run, REPLAY, SAVE REPLAY, SAVE GHOST, DATA LOGGER and SETTING are disabled.
- **Triangle / Square.** On a page with the cancel flag (0x100: the root page of
  every quick menu but the championship ones) they end the menu at once as its
  EXIT item, with the menu SE "return" (`QuickMenu::tick`, test
  `race_quick_menu_cancel`). The championship menus and the "Update Ghost?" and
  exit popups ignore them.
- **Music.** The pre-race, time-trial, license and machine-test menus play the
  short menu music; race and 2P result menus play the result music (§M2).

### §C3 The latched result

The result a run reports to the menus is taken when the live run ends
(`RaceResultLatch`, `RaceScreen::latch_result`), before any replay. This holds
for every run, as its replays and the menu's backdrop (§C5) re-run it (a license
result still reports the visit's best run, §A5). A race replay re-runs the whole
field, so the standings afterwards are the replay's, not the race's. A race's
result menu therefore keeps the rows latched with it.

Every way out (quick-menu EXIT, replay pause EXIT, leaving from a replay) posts
the latched run; a Retry drops it (test `race_result_latch_exit_paths`). A
finished GT single race is also written into the save when it is latched.

### §C4 After a run

When the live run's §C1 transition is over, the run's result is latched (§C3),
and then the steps of its mode follow, in this order (`RaceScreen::after_run`,
`race::post_run`, test `race_post_run_sequence`). They apply whether the run
finished, failed or was left with EXIT:

| Run | Steps |
|---|---|
| arcade single race | the cup, for a win (§F4) -> the replay -> the result menu |
| 2P battle | the replay (always) -> the 2P RESULTS menu (§B8) |
| GT race, championship round | the prize page, for a finish with a prize (§F1) -> the replay -> the result menu |
| time trial, free run, machine test | the name entry, if the run ranked (§B7) -> the replay -> the quick menu |
| license test | the quick menu, with the name entry over it if the run ranked; no replay (§A8) |
| free practice before a GT race | the GT race's pre-race menu |

- **Prizes and name entry** are GT-mode presentations shown over the race
  (`game::queue_arcade_trophy`, `game::queue_race_prize`, `RaceScreen::record_run`).
  The replay starts when they close (`start_auto_replay`). The race posts
  `GameResult::prize_shown`, so the menus do not show a prize page or cup again.
  Practice runs and races not started from the menus get no prize.
- **The automatic replay** plays once: the whole recorded run from its start to the
  frame the live run ended (or to a pause EXIT that counted as its finish, §E5),
  the end-of-run driving after the finish included. It shows no results panel,
  record overlay, FAIL or medal (§G7). Only Start opens its pause (the replay
  window, §E1); nothing skips it. It stops the run's music and starts its own,
  following the replay BGM switch (§G8). Its end or its pause's EXIT runs the §C1
  transition over its last frame; then the quick menu cuts in
  (`RaceScreen::return_to_menu`).

### §C5 The race behind the quick menu

What a quick menu shows behind its layout depends on the mode
(`race::menu_backdrop`, `RaceScreen::start_menu_backdrop`, tests
`race_menu_backdrop`, `race_quick_menu_backdrop_disc`):

| Menu | Behind it |
|---|---|
| time trial, free run, machine test (before and after runs) | the race (below) |
| GT race and championship pre-race menus (also after the free practice) | the race (below) |
| result menus: arcade race, 2P battle, GT race, championship | no race: the layout's own opaque checker background (the root page's `bg`) |
| license test | the preview box (§A7) |

- **The race.** The menu's race plays like a replay (play mode 2): records are
  off, and the view mode and target are picked as for the attract demo, drawing
  from the same generator and count (`pick_attract_view`, §G13). It is silent:
  no car or course sound plays behind a quick menu (records off mutes them). A
  single car replays its last run from the start, and the race never ends. Once
  the recording runs out, the car gets a cleared input frame (no
  steering, throttle or brake; its gearbox kept) and rolls on. Before any run
  the recording is empty, so the car stands at its start with no input. A field
  (the GT pre-race menus) stands on its grid, not simulated, under the TV
  director's cameras.
- Choosing a root item other than the option, records and logger items stops it.
  Back from an item that keeps the menu (save, load ghost, a dialog), it starts
  again from the start.

---

## §D HUD

`race::Hud` draws the in-race HUD from the disc's `race/<LANG>/display.imgs`
images and the game fonts. `RaceScreen::hud_data` fills a `HudData` from the
followed car each frame, including the run's layout (§D4). A part is drawn only
where that layout has a row for it in the current camera view (in-car or
outside); live play shows every part but the steering and accel / brake monitors,
replays the REPLAY OPTIONS ITEM parts (`HudData::items`, §D9).

### §D1 Meter cluster

The camera picks the cluster. In-car views (the eye in the car) use the onboard
cluster; the chase and TV cameras and the pre-race shot use the remote one.

**Round meters.** A face image stretched to its box, pivot = box center +
offset, needle angle a0 + 90 + clamp(v, 0, F + 8) / F x (a1 - a0) degrees
(screen y down). The needle is a tapered red quad from n0 r to n1 r. A segmented
band at 0.95 .. 0.98 r has segments every 500 rpm (1000 on the 18 k face), red
from the shift-up rpm (the setup's upshift rpm, else the rev limit - 500).

| Meter | Box (frame units) | Offset | r | a0 .. a1 (deg) | Needle n0 .. n1 |
|---|---|---|---|---|---|
| onboard tachometer `rev_NN` | 350 .. 520 x 326 .. 436 | (0, 23) | 75 | 72.5 .. 287.5 | -0.2 .. 0.95 |
| onboard speedometer `speed_*` | 120 .. 290 x 326 .. 436 | (0, 23) | 75 | 72.5 .. 287.5 | -0.1 .. 0.95 |
| remote tachometer `revb_NN` | 160 x 140, right edge 620 (pushed), centered on y 320 | (3, 4) | 73 | 29.85 .. 238.34 | 0.38 .. 0.9 |
| boost (turbo cars) `boost` | 72 x 64, bottom 436; onboard from x 530, remote right edge 475 (pushed) | (0, 0) | 32 | -45 .. 200.32 | -0.1 .. 0.95 |

- **Tachometer scale** F = floor((rev limit + 1500) / 1000) x 1000. The face is
  the first of 06 .. 12, 18 whose thousands cover F (`Hud::tach_scale`, test
  `race_audio_models`: 8000 rpm gives 9000).
- **Speedometer scale** N = clamp(int(v_top x 1.1 / 22.2222) + 1, 2, 4), with
  v_top in m/s estimated from the rev limit, the rear wheel radius, the top gear
  and the final drive (`Hud::speed_scale`, test `race_speedometer_scale`). The
  needle's full scale F is N x 80 km/h, or N x 50 mph. The face is the first of
  `speed_180km` / `speed_240km` / `speed_320km` (`speed_120mp` / `speed_160mp` /
  `speed_200mp` in mph) whose figure reaches F: N = 2, 3, 4 give 160, 240, 320 km/h
  on the 180, 240, 320 faces and 100, 150, 200 mph on the 120, 160, 200 faces.
  The band's segments are every 10 km/h or 5 mph.
- **Boost.** The needle shows the pressure ratio 1 + boost (1.0 = the face's
  "0 kg/cm2"), clamped to 0 .. 2.
- **Digital speed.** Speed font, "%3d", amber, with "km/h" or "mph" in sw_12.
  Onboard: right edge (350, 410). Remote: right edge x 580 (pushed), baseline 385.
- **AT / MT.** The meter font's `>` (automatic) or `=` (manual), amber.
- **Speed unit.** The game data's rule: mph for the NTSC-U data, and mph when no
  GT-mode session exists. `[race] units = kmh | mph` overrides it.

### §D2 Gear, shift light, recommended gear

- **Gear.** The digit is in the `gearr` font in amber, "R" in HUD red, "N" in
  sw_20, over an unlit "8" (black, alpha 96). While the clutch is not engaged
  the digit is drawn at half brightness. The onboard cluster has it in the gear
  panel centered on (320, 360); the remote cluster inside the tachometer.
- **Shift light.** A `:` above the digit, lit HUD red above the shift-up rpm,
  blinking with a counter that adds 42 per frame (3 frames on, 3 off).
- **Recommended gear.** "> n" after the digit, half size. A ghost "> 8" is always
  there; the recommended gear is lit red over it when the player's car is not
  slower than the coming target speed T, blinking red / black in braking zones.
  The race's AI keeps planning for the player's car while the pad drives it, and
  the hint is its gear (`ai.md` §16; `RaceScreen::update_gear_hint`). T is the
  next point's speed, or in a braking zone entered in a corner the lower of the
  current and next. The gear is the one the AI's plan picks for T. Live runs
  only: replays show no hint.
- **Back panels.** Flat black polygons at 50 %. Remote: an octagon with 3-unit
  cut corners over 484 .. 620 x 400 .. 430 (right-aligned, pushed). Onboard: the
  gear panel 78 x 31 centered on (320, 360), top corners cut 3, straight to 30 %
  of its height, then tapering to a 15 % inset.

### §D3 Indicator lamps and odometers

- **Lamps.** The odo font glyphs `!` ASM, `"` TCS, `#` hand brake and `$` oil,
  each followed by a space. Colors when lit: ASM green (73, 204, 43), TCS
  (255, 184, 20), the other two HUD red. Unlit lamps are black at 50 %. The run
  is centered at (435, 430) onboard; remote it starts at x 495 (pushed), y 410.
  - The ASM and TCS lamps show the car's own aid states, which the race sets
    every tick. ASM is lit while the car has ASM on (it does not blink). The
    Options menu's ASM / TCS off (arcade single race, free run, 2P battle) build
    the player's car for the race without that aid, so its lamp stays out.
  - TCS is lit while the car has TCS on, and blinks while it cuts the throttle.
    The blink counter adds 21 per frame and the lamp is lit while the counter is
    positive as a signed byte.
  - The hand-brake lamp follows the input.
  - The oil lamp lights once the car's oil counter (its engine condition,
    `physics.md` §10.3; in GT mode the garage car's, carried on by the run)
    reaches 300 km.
- **Odometers.** Odo font, cyan (98, 192, 192), centered, on the baseline. "%06d"
  is the car's odometer (km or miles), kept in the engine condition in 2 m units
  and advancing as the car drives (in GT mode starting from the garage car's), and
  "%04d" this race's trip in tenths. Onboard they sit at
  (205, 428) and (205, 370); remote at (605, 424) and (525, 424), pushed.
  Player 2's half has no cluster.

### §D4 Laps and times

**Layouts.** Each run uses one of the game's display layouts, which says which HUD
parts are shown and where (`race::hud_layout`; the layout a run uses is
`race::hud_mode`, picked in `RaceScreen::hud_data`; tests `race_hud_mode_select`,
`race_hud_layout_rows`, and `race_hud_layout_disc`, which checks every row
against the disc):

| Layout | Used for |
|---|---|
| race | single races, GT races and championship rounds, online races |
| endurance | any run but a free practice or 2P whose event has 10 laps or more or a time limit (on the disc, only races) |
| time trial | the arcade time trial and the other solo runs not listed here |
| free run | arcade and GT-mode free runs (the course alone, no laps to run) |
| free practice | the free run before a GT race |
| license | license tests (§A3) |
| machine test (time) | the 0-400 m and 0-1000 m tests |
| machine test (speed) | the top-speed test |
| 2P | the 2P battle, top / bottom or side by side (§D8) |
| replay | replays, including a 2P replay with SCREEN single (§D9) |

What else each layout shows (details in §D1-§D3 and §D5-§D7):

| Layout | Lap count | Position | Map | Message display | Tire gauge |
|---|---|---|---|---|---|
| race | yes | (40, 64) | (16, 80) | yes | yes |
| endurance | yes | (40, 60) | (16, 96) | yes | yes |
| time trial | yes | - | (16, 32) | yes | yes |
| free run | yes | - | (16, 16) | yes | yes |
| free practice | yes | (40, 60) | (16, 96) | yes | yes |
| license | - | - | (16, 32) | - | - |
| machine test (time) | - | - | (16, 16) | yes | - |
| machine test (speed) | - | - | (16, 16) | - | yes |
| replay | ITEM time | - | (16, 16), ITEM map | - | ITEM tires |

The message display is WRONG WAY and AUTO DRIVE (§B4). Every layout but the
top-speed test's has the event display (§D6). Every live layout has the meter
cluster (§D1): the onboard one in the in-car views, the remote one outside;
replays differ (§D9).

**The lines.** All are right-aligned at x 615 (pushed), in amber. The lap count
is the value in sw_20 with "Lap" in sw_16, its right edge 10 units left of the
value's. A time line is the value in sw_16 with its label in sw_12, the label's
right edge max(value width, 90) + 10 units left of the value's
(`Hud::draw_times`):

| Layout | y 48 | y 84 | y 112 | y 140 | y 216 |
|---|---|---|---|---|---|
| race | "Lap n/N" | Best | Last | Lap | - |
| endurance | "Lap n/N" or "Lap n" | Total | Best | lap-time list | Lap |
| time trial | "Lap n/N" or "Lap n" | Best | Time (the run's total) | - | - |
| free run | "Lap n" | Record Lap | Best | lap-time list | Lap |
| free practice | "Lap n" | P.P. | Best | lap-time list | Lap |
| license | - | Best | Time | targets at 140, 168, 196 (§A3) | - |
| machine test (time) | - | Best | Time | - | - |
| machine test (speed) | - | Record (speed) | Best (speed) | - | - |
| replay (ITEM time) | "Lap n/N" or "Lap n" | Lap | - | - | - |

- **Lap count.** "n/N" where the run has a lap total: the event's laps in races,
  the arcade time trial's one lap ("Lap 1/1", §B7);
  otherwise "n" alone (free runs, the free practice, time-limited races). The lap
  number is shown from the grid (lap 0 counts as 1) and never above N. A
  time-limited race shows no time remaining.
- **Lap** is the current lap's time; in races it runs from GO (§B9); in the free
  practice it stays at 0'00.000 until the car first crosses the line. Once the
  followed car has finished, the Lap line goes in the layouts that have Last or
  the lap-time list (race, endurance, free run, free practice).
- **Best** is the best lap, **Last** the last lap. In the solo runs (time trials,
  free runs, machine tests, license tests) Best is the visit's best (lap, time to
  the goal, judged time), kept across retries: dashes until the first one (in a
  free run it so shows the same time as Record Lap).
- **Total** (endurance) and **Time** (time trial) are the sum of the followed
  car's recorded laps and the current lap; once the car has finished, the
  recorded laps alone. A time trial stopped by a course-out or hit (FAIL) has
  not finished: its lap time runs on with the clock, so Time shows the recorded
  laps plus the running lap.
- **Lap-time list.** The followed car's last three recorded laps, oldest at the
  top, dashes for lines not yet run, lines 0.8 x the sw_16 line height apart.
  Each line has its lap number in sw_12, right-aligned max(widest time, 90) + 10
  units left of the times.
- **Record Lap** (free run) and **P.P.** (free practice) show the session's
  fastest lap: in the free run the player's best lap driven so far, in the free
  practice the fastest lap of any car without a course-out; no stored
  record (§B7) is loaded into it, so both show dashes until a lap is done. The
  session's fastest lap (every car's in races) starts with no time when the
  course and car are loaded (`RaceScreen::reset_session_best`) and lasts across
  retries (TRY AGAIN, START, the pause window's RETRY) and replays until the next
  visit. After the practice's first fastest lap, P.P.'s label is replaced by a 64 x 32
  picture of the car that set each new fastest lap (the pictures of §B8; before it
  the label shows), its right edge 99 units left of the label's
  place and its center 4 units above the line's baseline
  (`RaceScreen::draw_event_icons`).
- **Machine tests** (§B7) show no lap count.
  - 0-400 m / 0-1000 m: Time is the clock from GO (§B9), shown from the start
    (0'00.000 during the hold), then the time to the goal once it is reached.
    Best is the visit's best time to the goal: dashes until the first.
  - The top-speed test shows two speed lines instead of the times: the speed in
    sw_16 with the unit ("km/h" or "mph", §D1) in sw_12 right-aligned after it,
    and the sw_12 label left of both (at least 90 units from the right edge, as
    for the times). "Record" always shows dashes. "Best" is the highest speed so
    far, taken every tick of the run from its start, past the goal too (not
    in demonstrations, where it shows dashes; replays keep it too, for the
    goal's pop-up of §D6, but draw the replay layout). The value is "%.1f",
    rounded, in mph x 0.62139, at most 999.9, and "---.-" (the font's long
    dashes, §D10) for 0 or less (`Hud::speed_text`, test `race_hud_speed_lines`). This is not the recorded
    top speed of §B7, which stops at the goal.

### §D5 Mini-map and position

- **Map.** The course map is the course's slot 33 (`GTCM`): image, center, scale
  and rotation (see `docs/formats/course.md`). A car at world (x, z) is drawn at
  (W/2, H/2) + scale R(rot) (x - ox, z - oz), north up. The map's top-left
  corner sits at the layout's place (§D4), pushed left: (16, 80) in races,
  (16, 96) in the endurance layout and the free practice, (16, 32) in the time
  trial and license tests, (16, 16) in free runs, machine tests and replays.
  Opponents are `ecar_dot`, the player is `1pcar_dot` on top, both 7 x 7 units.
- **Position.** "1st" .. "12th" (the suffix is part of the string; nothing outside
  1 .. 12; no "/N") in the rank font, 3 units of glyph spacing, amber. It is
  drawn at (40, 64) in races and (40, 60) in the endurance layout and the free
  practice, after a black 75 % shadow at (+4, +4). It appears only in those
  layouts, in live play, with more than one car. The game draws it with no
  16:9 push; ours pushes it 106.667 units left whatever its alignment (UI x 30
  on 16:9), like the lap count on the right (`Hud::draw_rank`).

### §D6 Messages

- Countdown and START (§B2), FINISH (§B5), the course title over the start (§B3).
- WRONG WAY, AUTO DRIVE (§B4).
- **The event display** (pop-ups in the middle of the screen; below).
- **Toasts** (ours): "Replay saved", "Settings applied" and similar, for 3 s.

**The event display** (`race::EventDisplay`, `RaceScreen::lap_events`; tests
`race_event_display_timing`, `race_passing_order_gaps`, `race_best_lap_splits`,
`race_lap_event_texts`, `race_replay_lap_event_rules`,
`race_replay_followed_car_events`, `race_replay_event_display_rows`). Messages are
centered on the anchor (320, 130) of the frame, with the pen on the baseline, at
full alpha, each for 3 s. A text wider than the layout's limit is squeezed
horizontally to it: 320 units in the endurance layout, 400 in free runs and
replays; the other layouts set none (§D4). The display is drawn only with the
time item on (`Hud::event_row`): live play always has it, replays only with
REPLAY OPTIONS ITEM time (default off, §D9):

| Message | Text | Font, color | Place | Timing |
|---|---|---|---|---|
| lap time | "m'ss.mmm" (§D10) | sw_20, green (73, 204, 43) | the anchor | queued |
| split time | "T<n> m'ss.mmm", the time since the lap's start | sw_16, green | the anchor | queued |
| gap | "+m'ss.mmm" behind, the font's dash glyph and the time ahead | sw_16, red (240, 0, 0) behind (gap ≥ 0), light blue (128, 128, 255) ahead | 25 below the anchor | at once |
| laps down | "+N Laps" (`RACEDISPLAY_LapDiffInfo`) | sw_16, red | 25 below the anchor | at once |
| pit-in | "<car> Pitting in..." (the name as on the results, §B6) | sw_16, green | the anchor | queued |
| top speed (top-speed test replays) | "312.4km/h" / "124.3mph": the top speed so far, "%.1f" rounded, mph = km/h x 0.62139, no space (`race::machine_speed_message`) | sw_16, green | the anchor | queued |

- **Queue.** A queued message starts when the messages queued before it have run
  out, so lap and split times show one after the other. Gaps and laps down start
  at once, alongside the current one. There are eight message slots; a message
  arriving while all are busy is dropped.
- **Order within a tick:** lap times, split times, laps down, gaps, the top
  speed, then the pit-in messages.
- **Car picture.** A gap with a car picture (64 x 32, the car's model as on the
  results panel, §B8) moves the text 37 units right (behind) or left (ahead) and
  puts the picture 62 units to the other side, 20 below the anchor. The picture is
  the car in race position 1 when behind, position 2 when ahead; without such a
  car the text stays centered. It blinks off for 12 frames three times (frames
  1-12, 25-36 and 49-60), then stays for the rest of the 3 s.
- **Split points** (`track::display_checkpoints`, test `race_split_points_disc`):
  the course's road markers of kind 2 whose second field is 0, in table order, at
  most 3; every race course has 2 or 3. With the line they are the lap's timing
  points. They count only on timed laps, in order: a car that misses one gets no
  further splits that lap (test `race_lap_timer_split_points`).

What each mode shows:

- **Races** (one or two players, online). The followed player's lap time at
  every line crossing (the finishing one too) and split times. Gaps: per timing
  point, the first car through on the lead lap starts the point's order. Every
  later car on that lap gets its gap to that first car (not to the car ahead), and
  when the second car passes, the first car gets its lead over the second. A car
  passing on an earlier lap gets "+N Laps" instead. Up to 8 cars are kept per
  point and lap. Only the followed players' own gaps are shown.
- **Time trials and free runs.** The lap and split times, and the gap to the
  session's best lap so far and its split times (`race::BestLapSplits`, test
  `race_best_lap_splits`), taken before the lap updates it. The session starts
  with no best lap and no stored record (§B7) is loaded into it, so the visit's
  first lap and its split points show no gap. The best lap lasts across retries
  (each run starts with no split times of its own, `BestLapSplits::begin_run`)
  until the next visit (`BestLapSplits::reset`). A split point the best lap did
  not pass in its run shows no gap. When slower the picture is the
  player's own car; when faster there is none. A free run also shows its two
  notes after the start signal (§B2).
- **License tests.** Split times only, numbered on through the laps; no lap
  time.
- **Machine tests 0-400 m / 0-1000 m** (§B7; test `race_machine_test_popups`,
  `race::machine_test_message`). At the 400 m split point of the 0-1000 m test
  "T1 m'ss.mmm" with the time since GO, as a split time; at the goal the time to
  it as a lap time (sw_20). All green and queued. Nothing at the line, and no
  gaps.
- **None:** in the live top-speed machine test (its HUD has no message display:
  no pop-ups, WRONG WAY or AUTO DRIVE), after a time-trial FAIL, and in
  demonstrations and the attract demo (ours; the game's demonstration layout has
  the display in the in-car view only, `Hud::event_row`).
- **Replays** (`race::lap_event_rules`, `race::event_display_of`). The re-run
  posts what the live run did, for the car the cameras follow (§G3: MANUAL's
  chosen car, else the car TOP, AUTO or a view-mode-1 cut is on), on the single
  view's display at the anchor, in every camera view. The differences:
  - no gaps to the session's best lap (time trials, free runs), and replay laps
    do not count for the session (its best lap and split times, Record Lap, P.P.,
    Fastest);
  - a license test's time, as a lap time, at the moment its live run ended
    (a medal or TIME EXPIRED; nothing after a FAIL), besides its split
    times; a replay without its live run (e.g. from the theater) shows no such
    time (ours);
  - the machine tests re-run their timing points (the result stays the live
    run's); the top-speed test shows its split times "T<n> m'ss.mmm" and, at
    the goal, its top speed (the table above);
  - a solo run's car posts nothing after its end: its laps done at the line, or
    the end-of-run driver taking over once the timing began. In races a
    finished car's timer stops.

  The gap's car picture is drawn in replays as live (`draw_replay_items`).
- **Pit-in** messages come for the player and every opponent that enters the pit
  lane.
- **2P.** Each half has its own display, its own player's messages, and the
  pit-in messages on both. The anchor is (320, 75) of the 640 x 224 half with the
  text and picture at 70 %, or (213, 165) of the 426.667 x 448 half; a text wider
  than 320 (384) units is squeezed to fit.

### §D7 Tire gauge

When the player's car has tire wear, the `tire_gage` image is drawn at the bottom
left (left edge 20 pushed, bottom 435) in the layouts that have it (§D4): not
in license tests or the 0-400 m / 0-1000 m machine tests, and in replays only
with ITEM tires. Its four tire bodies (the image's alpha codes) are
tinted by each wheel's wear level w (-63 .. 127) (`Hud::tire_color`), with the
outlines in dark gray:

| w | Color |
|---|---|
| <= -64 | black |
| -63 .. -1 | (0, 252 - 4 abs(w), 4 abs(w)): blue towards green (scrubbing in) |
| 0 .. 31 | (8 w, 252, 0): green to yellow |
| 32 .. 63 | (252, 4 (95 - w), 0): yellow to orange |
| 64 .. 127 | (252, 2 (127 - w), 0): orange to red |

### §D8 Split-screen HUD (2P)

Each half has its own HUD (`Hud::draw_split`): rank, "Lap n/N", Fastest / Last /
Lap times, digital speed, gear, the `meter_mini` bar tachometer (drawn at alpha
70/255 and lit from the left in proportion to rpm / F) with AT / MT, the
countdown, WRONG WAY, and the course map at 70 % with both players' dots. There
are no round meters, lamps or odometers.

- **Rank.** "1st" .. "12th" in the rank font at 80 %, the pen on the baseline at
  the layout's place, after a black 75 % shadow at (+4, +4), in both layouts.
- **Fastest** is the race's fastest lap so far, by any car (either player or an
  opponent); **Last** and **Lap** are the half's own player's. The Lap line goes
  once that player has finished.

Two layouts exist:

| Split | Half | Notes |
|---|---|---|
| horizontal (4:3 top / bottom, the PS2's) | 640 x 224 | fonts at 70 %; rank at (24, 48) with the map below; "Lap n/N" (sw_20, right edge 615, y 36); Fastest / Last / Lap as a small label and a large time top right (y 64, 88, 112); speed, then a dark panel (484 .. 610 x 181 .. 210) with the gear, a ghost "> 8", AT / MT, the bar tachometer and a red shift-light dot; AUTO DRIVE next to the speed; no countdown images |
| vertical (16:9, side by side) | 426.667 x 448 | the 3-D view letterboxed to (x0, H/6, W/2, 8H/11); rank at (16, 60), map at (10, 105); the text drawn as on the full screen (§D4): the lap count's value (sw_20) right-aligned at x 310 pushed right like every lap count, "Lap" (sw_16) 10 units left of it, at y 60; Fastest / Last / Lap at y 96, 120, 144, values (sw_16) right-aligned at x 413 with the labels (sw_12) max(value width, 90) + 10 left of them |

`[race] split = auto | horizontal | vertical` chooses. Auto means top / bottom
with the wide-mode option off, side by side with it on.

### §D9 Replays and the HUD

In replays the HUD is replaced by the replay mark (while no item is on) and the
REPLAY OPTIONS ITEM parts (§G4, §G7), drawn on the replay layout
(`RaceScreen::draw_replay_items`). There is no position, message display,
countdown or FINISH. The course title over the replay's start (§B3) is drawn
whatever the items say. Each part is drawn only while its item is on:

| Item | In-car view | Outside views |
|---|---|---|
| easy meter | the onboard cluster (§D1-§D3) | a small cluster: a black 50 % octagon 70 x 30 (left edge 210, bottom 436), the digital speed right-aligned at x 170 (baseline 433) with its unit (sw_12) 5 units right of it, and the gear with its shift light and recommended gear (§D2) centered about (250, 420); all pushed left |
| car name | the name as on the results (§B6), light gray (208, 208, 208) sw_16, left edge 16 (pushed), baseline 290, squeezed to at most 300 units | the same, right edge 615 (pushed), baseline 433, at most 320 units |
| time | the lap count at y 48 and the Lap line at y 84 (§D4), and the event display of the followed car (§D6) at (320, 130), squeezed to 400 units, with the gap's car picture | the same |
| map | at (16, 16) (§D5) | the same |
| steering | a steering monitor pivoting on (40, 350), pushed left | the same |
| tires | the tire gauge (§D7) | the same |
| accel / brake | two gauges standing on y 435: brake (`brake_gage`) at x 68, accel (`accel_gage`) at x 88, pushed left | the same |

- **Steering monitor.** A light gray tick from 24 to 43 units above the pivot, and
  the `steering` mark on the circle of radius 43 about the pivot, moved round it
  and turned with the steering input, up to 90 degrees either side.
- **Accel / brake.** Each gauge is dark gray (32, 32, 32), filled from the bottom
  in proportion to the input, brake red (240, 0, 0) and accel green
  (73, 204, 43), with the gauge art on top.

A theater demonstration draws the items on its run's own layout instead (§G7).

### §D10 Time format

Times are printed in one of two formats, as the game prints them
(`race/time_text.h`):

- **The race display** (`Hud::format_time`, test `race_hud_time_format`): the
  HUD's time lines and lap-time list (§D4), the pop-ups (§D6) and the license
  result's time (§A4). "m'ss.mmm", with minutes unpadded ("0'35.100",
  "2'05.007"), clamped at 999'59.999; `-'--.---` for negative times and for the
  "no time" value 359,999,999 ms.
- **The menus** (`race::menu_time_text`, test `race_time_text_format`): the
  quick menus' rows and Top 3 (§C2), the time-trial result's "Lap" (§B7), the
  results overlay's times and gaps (§B6), GT mode's records pages, the Replay
  Theater's Record (§G9) and the Analyzer (§G10). "m'ss.mmm", and
  "h:mm'ss.mmm" from an hour on ("1:02'03.400"); `--'--.---` (two dashes for
  the minutes) for "no time": exactly 359,999,999 ms, or a time we do not have.

The dashes of these placeholders, of the no-speed `---.-` (§B7, §D4) and of a
record's missing date `--/--/--` (GT mode's records pages) are the game font's
long dash, character 0x7F, never the hyphen: in sw_16 the dash is 12 pixels
wide, the hyphen 7. The texts are game text, and the UTF-8 path draws 0x7F
with the same glyph (tests `race_time_text_disc_program`, which checks them
against the game program on the disc, and `race_time_text_disc_fonts`, which
lays them out with the disc's fonts).

### §D11 Touch layouts

All of this is ours. The uncovered area (`HudData::safe_area`) is the screen
less the visible controls (the steering zone does not count): controls
centered in the top quarter push its top down, the others its left or right
side. While the controls are hidden nothing moves. Distances here are in units
of the 640 x 360 UI.

- **Meters.** The cluster moves into the area's bottom-right corner. When the
  area is narrow it is also scaled by clamp(width / 560, 0.7, 1) about its
  bottom-right corner.
- **Tire gauge.** It moves up to the area's bottom; it does not move sideways.
- **Laps / times, rank, mini-map** (`Hud::draw`, `HudData::touch_controls`,
  the controls' drawn boxes from `TouchControls::control_bounds`; test
  `hud_top_blocks_beside_touch_controls`). Each stays in its corner, as without
  touch controls, unless a control covers it. The block's texts and pictures
  (the map's dots aside) are checked with a 4-unit margin. A control whose top
  is within 24 units of the block's bottom line does not count: it is left to
  cover that line (the license targets and the lap list's last line sit over
  the shift buttons). A covered block moves sideways toward the middle until it
  is 4 units clear of every control: the times left, the rank and the map
  right, each on its own. If that move is larger than the area's inset on that
  side, the block instead goes into the area (moved in by the inset and down to
  the area's top). When the rank goes into the area, the map goes with it.
  On the driving layout at its normal size, on 16:9 and 20:9, no control is
  in the top corners, so the blocks keep their places (test
  `touch_control_bounds`).

---

## §E Pause window

The pause window (`race::PauseMenu`, `src/app/screen_race_pause.cpp`) uses the
disc's `menu/<LANG>/pause1..3.pmb`: a row of image buttons from the page's
hidden layer, `m_<name>` in orange and `m_<name>_sel` in light orange over the
selected one. Every window dims the whole picture (the page's `bg1`, extended
past the 4:3 frame on wide screens) and darkens its band (`bg`).

### §E1 Which window

`RaceScreen::pause_kind`:

| Window | File | Buttons | Used for |
|---|---|---|---|
| race | `pause1.pmb` | CONTINUE, EXIT | races, free runs, 2P, the attract demo |
| retry | `pause3.pmb` | CONTINUE, EXIT, RETRY | the arcade time trial (and debug runs with `laps=N`), license tests, machine tests |
| replay | `pause2.pmb` | CONTINUE, EXIT, REPLAY OPTIONS | replays and the theater's demonstrations |

### §E2 Controls

- **Opening.** Start opens the window, in replays too (only Start: no other
  button opens it or skips a replay). Opening
  silences the car sounds and rumble, and the music pauses.
- **Keys.** Keys held when it opens count only after they are released.
- **Moving.** Left / Up and Right / Down move the cursor: once on the press, then
  every 3 frames after 30 frames. The cursor stops at the ends and cannot step
  onto a disabled button.
- **Choosing.** Circle or Cross chooses. Triangle or Square means CONTINUE in the
  button row and CANCEL in the question.
- **Sounds.** The menu SE "cursor" on a move, "ok" on a choice, "cancel" on
  Triangle / Square.
- **Blink.** The selected button's light copy fades from alpha 1 to 0 over 30
  frames and starts again (a 0.5 s sawtooth). A disabled button is drawn at half
  alpha.
- **Ours.** Start also closes the window.

### §E3 EXIT and RETRY

- **EXIT in a race** asks "Exit race?" with OK / CANCEL, the cursor on CANCEL.
  CANCEL or Triangle / Square go back to the button row with the cursor on
  CONTINUE (test `race_pause_exit_question`). OK ends the run through §C1 and
  what follows it (§C4), or, in the cases of §E5, counts as the run's finish
  (`RaceScreen::pause_exit`). The free practice before a GT race goes back to its
  pre-race menu after the transition. The attract demo and online races leave
  straight away instead.
- **EXIT in a replay** leaves at once. The automatic replay (§C4) and a replay
  opened from a quick menu end with the §C1 transition over their frame, then
  that menu; otherwise the player drives again. In the Replay Theater it goes
  back to the theater.
- **RETRY** restarts the run from its start (online races refuse with a toast).
- **RETRY after a session record.** A finished run that sets a session record
  grays RETRY out (half alpha; the cursor cannot reach it); it is available again
  at every run start (`RaceScreen::rank_finish`, `race::sets_record`,
  `race::locks_retry`, `set_retry_enabled`, test `race_retry_lock`). A session
  record needs all of:
  - the player's car, not a replay or a demonstration, and not a failed run;
  - a time at or under the session's best lap before it (§D4, §D6), compared in
    whole milliseconds, so a tie counts; with no best lap yet (the visit's first
    time) any time is one. The stored records (§B7) take no part;
  - a time trial's best lap, a 0-400 m / 0-1000 m machine test's time, or a
    license test's judged time (a medal or TIME EXPIRED run has one, a FAIL none,
    §A2). The top-speed test is never checked.
  A TIME EXPIRED license run (no medal) can set the session record but leaves
  RETRY available. After the check, a license or machine-test time joins the session's best lap
  (a time trial's laps join it as they are driven, §D6).
- **REPLAY OPTIONS** opens the panel of §G4. Closing it puts the cursor back on
  REPLAY OPTIONS.

### §E4 The longer list (ours)

Select in the pause window opens a plain list with more options:

- in a race: Resume, Restart, Watch replay, Save replay, Camera, Gearbox, Exit;
- in a replay: Resume replay, Restart replay, Camera, Save replay,
  Stop replay (drive), Exit;
- online: Resume, Camera, Gearbox, Retire (back to lobby).

It is not available in the theater. Online, the pause window does not stop the
race: the car runs on with no input, the camera keeps following it, and a note
says so.

### §E5 EXIT as the run's finish

EXIT in a live run ends it (§E3), with two exceptions where it is the run's
finish instead (`race::pause_exit_finishes`, `RaceScreen::exit_as_finish`, test
`race_pause_exit_finish`):

- a free run (the arcade time attack, GT Mode's free run on a course; not the
  free practice before a GT race) with at least one completed lap;
- the top-speed machine test once the car has moved (top speed over 1 m/s) and
  before its goal.

Then the laps freeze and the run takes its place in its ranking (the free run's
best lap, the top speed; §B7). The `finish` image shows (§B5). The race ends
the car's run at the press and the end-of-run driver (`ai.md` §1.1) takes the
car: in a free run it stops the car with the wheels straight if it is on the pit
surface, else drives on along the main line without slowing; in the top-speed test
it drives on more slowly, as at the test's goal (`race::exact_exit_finish`, test
`exact_game_exit_as_finish`). The race's recordings close at the press (the
run's, and a free run's lap recording), so the run's replay ends there. The
record overlay follows as after
a finish, 5 s later (the top-speed test: 0.1 s later,
`race::exit_finish_overlay_delay`), and the run leaves as in §B7. A second EXIT
ends the run at once. Every other run, and these before the condition holds, end
at once.

---

## §F GT-mode race finish and prize pages

### §F1 The sequence after a GT race

After a GT single race or a championship round (not practice), the §C1
transition is followed by these steps (`RaceScreen::after_run`, §C4):

1. The result is latched (§C3); a finished GT single race is also written into
   the save.
2. **The race prize page**, for a finish with a prize (`game::queue_race_prize`),
   opens over the race as a GT-mode presentation, in championship rounds too:
   - places 1-3: the event's cup;
   - places 4-6: the consolation prize;
   - none if the event has no trophy class 5 .. 8 or the place pays no prize money.
   A race left with EXIT gets none.
3. **The race's replay** starts by itself (§C4), finished or not.
4. When the replay ends (or on its pause's EXIT), the §C1 transition runs again,
   then the GT result quick menu opens (`qm_gt_result1.pmb`,
   `qm_gt_result_champ1.pmb`) with the latched rows and no race behind it (§C5).

`GameResult::prize_shown` tells GT mode that the prize page was already shown,
so it is not shown again. The prize money is added to the credits when the
result reaches GT mode; the page counts it in from the old balance.

### §F2 The prize page

| Place | Page | Model |
|---|---|---|
| 1-3 | `gtmode_mini` | `race/g_<class>_<gold|silver|bronze>.prm`, class easy / normal / hard / special from the event's trophy class 5 .. 8 |
| 4-6 | `consolation_prize` | `race/consolation.prm` |

- The model fades in over the page's first 30 frames, and its animation starts
  with the fade.
- The page shows the place ("1st" ...), the balance "Cr. <balance>" counting up
  and the amount left, "<prefix> Cr. <left>", counting down (§F3). Both
  amounts are grouped by commas in every language ("Prize Cr. 991",
  "Cr. 18,000").
- The prefix is the language's race display text `PRIZE_RT_MSG_BONUS_PREFIX`
  in `text/realtime.dat`: "Prize" in English, "Prix" in French, "Preis" in
  German, "Premio" in Italian, "Precio" in Spanish (tests
  `gtmode_synth_world_loads`, `region_disc_money_from_data`). Where that text
  is empty or missing, as on the Japanese disc, the page uses the game's own
  built-in word, "prize money" in kanji (`GtData::prize_prefix`).
- The amount-left line fades in over 60 frames from the page's start, stays
  until 61 frames after the count finishes, then fades out over 60 frames.
- OK is accepted, and drawn, only once the count is done.
- The championship trophy page (`trophy_get`, `gt_mode.md` §18) counts its
  bonus in with the same two lines, the same prefix and the same counter
  (§F3); its amount-left line fades in from frame 270.
- In Japanese the prize money and the championship bonus are 100 times the
  values the event stores, so the balance and the amount left show those
  sums. The counter counts them with the same steps (§F3).

Bookkeeping (GT-mode topic, noted here for the cited code): a cup adds a trophy
and sets award bit 1 << ((class - 5) x 3 + place), place counted from 0; the
consolation prize sets
bit 0x10000 (`GtMode::note_race_prize`).

### §F3 The prize counter

`step_prize_count_up` in `src/game/prize_count_up.cpp` (the page state in
`src/game/gt_mode_menu/presentations.cpp`, `run_counter`), once per frame from the
page's first frame (frame 0). The arithmetic is single precision, so the credits
arrive on the same frames as in the game:

1. **Wait.** Frames 0 .. 121 only wait (120 frames, then two more; the
   championship trophy page waits 360 + 2). The first step is on frame 122.
2. **Creep.** The first 91 steps each count 0.2380952 (5/21) Cr. In single
   precision the 5th credit arrives on the 22nd step, not the 21st.
3. **Accelerate.** From the 92nd step the acceleration a grows by 5.2720418 per
   step. Once a has reached 474.48376 it snaps back to that value and stops
   growing, so it is over the cap for one step. Each step is
   (v + a) x speed, and that step becomes the new v. Once v has reached
   142345.125 it stays there and a drops to 0, so every step is the cap.
   The step's whole part, with the fraction carried over, goes to the balance.
4. **Done** on the step that reaches the target: the balance is set to the
   target.

- **Confirm** is read only while counting; presses during the wait do nothing.
  The first press sets the speed to 3 for the rest of the count. The tripled step
  is stored as v, so while accelerating v triples again on every step. The second
  press completes the count on its frame.
- **Beep** (the menu SE "cursor"). Every step except the finishing one adds to a
  beep sum. The beep plays on the frame of the step on which the sum has reached 1
  and at least 3 steps have passed since the last beep; then the sum and the step
  count reset. If the sum reaches 1 sooner, it is held at 1. No beep plays on the
  finishing step, and a beep never holds up the count. An effect queued by other
  menu code waits for a frame on which the page plays no beep.
- **Examples** (tests `gtmode_synth_prize_counter_steps`,
  `gtmode_synth_prize_counter_timeline`): 500 Cr ends on the 99th step (frame 220)
  with 20 beeps, on steps 5, 10, .. 90 and then 93 and 96 (every 5th step while
  creeping, every 3rd while accelerating). 1000 Cr takes 101 steps with 21 beeps,
  and 5000 Cr 108 steps with 23 beeps.
- **Approximation.** The frames match the game's up to at least 100,000 Cr. A
  2,000,000 Cr count shows a few different middle digits on some frames.

### §F4 Arcade cup

An arcade single race win shows the difficulty's cup (`arcade_mini`,
`race/a_<easy|normal|hard|special>_gold.prm`, the difficulty from the race
code's second letter E / N / H / P) right after the race, after the §C1
transition and before the race's replay (`game::queue_arcade_trophy`, §C4; test
`gtmode_synth_prizes_before_the_replay`). OK is accepted after 180 frames. It is
shown for a win only, and once: the result posted with `GameResult::prize_shown`
adds no second cup.

---

## §G Replays

### §G1 Recording and playback

Every run is recorded (`race::ReplayRecorder`; replay format 11, described in
`src/race/replay.h`). The file holds the race itself as a race record
(`src/sim/exact/race_record.h`, record version 3): what the race was built from
(the event's race settings, the course, every car's entry with its car, parts,
settings, engine condition, pedal style, wheel and ASM / TCS switches, the
grid; a solo run's ghost and best-lap recordings as the run started with them),
the disc release, rules and game data it ran on, and every player's pad input for
every step, plus the game's own changes between steps (such as the pause menu's
gearbox change or the AI taking a car over). The CPU cars need nothing recorded:
the race drives them again. Around the record the file keeps the players'
inputs per tick for the replay's HUD, what the theater shows (course, car,
kind, laps, times, power, save time), the paint draw's seed, and for a 2P battle
the Options it was raced with (laps, catch-up, handicap, tire damage, player
2's model, wheel and paint).

Playback builds the same race from the record and steps it with the recorded
pad inputs and changes, so the run, the whole field and every contact play
again exactly (test `exact_replay_round_trip`). A replay plays only on the disc
release it was recorded on, with its rules, and with the same car and course
data; otherwise it is not played and a toast says why ("This replay was
recorded with another version of the game and cannot be played.", or "This
replay cannot be played: " and the reason; `RaceScreen::can_play`). Files saved
before format 11 cannot be played: the theater lists them (§G9). A replay starts
like the live run: moving starts moving, otherwise the car is at rest on the
grid.

Playback switches the camera to the TV mode (§H6), remembering the player's own
view for Cross (§G3), and sets up the director (`ReplayDirector::setup`: the
replay's first shot, §H8), the cut clock (§G14) and the followed car (§G3). It starts the title's and the mark's clocks (§B3, §G7) and a
0.5 s dissolve from the last picture shown (§G16). The replay re-runs the start
sequence's music: the start jingle over the hold, the race music from the signal
(§B2). Only Start opens a replay's pause (§E2). When the replay ends:

- the automatic replay after a run (§C4) and a replay opened from a quick menu
  return to the quick menu through the §C1 transition;
- a theater replay returns to the theater;
- otherwise the replay pause opens with "End of replay".

**Saving.** Save replay writes `<user data>/replays/<course>_<car>_<stamp>.gt3replay`
at once (a toast, no slot page). Two saves in the same second take the next free
second's stamp. That folder is all the theater lists (§G9).

### §G2 View modes

A replay plays in one of four view modes, chosen only in REPLAY OPTIONS (§G4). No
button changes the mode during the replay.

| Mode | Name (icon) | Manual's name | Allowed targets | Default target | BGM |
|---|---|---|---|---|---|
| 0 | traditional (`mode_traditional`) | Normal | MANUAL | MANUAL | per option |
| 1 | avant-garde (`mode_avantgarde`) | Video | AUTO | AUTO | forced on (the BGM button is disabled); choosing the mode stores BGM on |
| 2 | broadcast (`mode_broadcast`) | Race | MANUAL, TOP, AUTO | AUTO | per option |
| 3 | course line (`mode_crs`) | Training | MANUAL | MANUAL | per option |

The allowed-target bits per mode are 1, 4, 7, 1 (MANUAL = bit 0, TOP = bit 1,
AUTO = bit 2). A stored target the mode does not allow falls back to the mode's
default (`ReplaySettings::current_target`).

The modes choose the TV shots differently (`race::ReplayDirector`; tests
`race_replay_broadcast_onboard_cut`, `race_replay_avant_garde`). A change of mode
is the view's reset (§H8; in mode 1 a cut at once).

- **Modes 0 and 3** follow the course's replay cameras with the shot picker of
  §H8: a shot ends as soon as it runs out. The engine reproduces no other
  difference between them: they look the same. Mode 3 (Training, "course line")
  draws nothing extra: the license course line (§A9) shows in every mode on the
  courses that have one, and on no other course in mode 3.
- **Mode 2** takes its shots from the same picker, but a shot that has run out
  waits for the camera manager's gate (§H8, §G14). At every new shot (a cut, a
  turn on board or a re-arm, §H8) the director draws, and with p = 0.07, while
  the followed car has a battle partner (§G3), the camera turns on board the
  partner's car (a bumper view: 0.62 m up, 0.15 m behind the nose, vertical FOV
  0.9 rad). That on-board cut lasts until the shot ends (§H8): the course record
  under it runs out and the gate opens, or a new target is set (a new followed
  car or partner, §G3). Without a partner it never cuts on board. It frames the
  followed car with its partner (§G3).
- **Mode 1** cuts on the race music's cut markers, not by the car's position
  (§G14), and each shot holds until the next cut. Its handler
  (`RaceScreen::avant_garde_frame`, once per 60 Hz frame) cuts on the cut clock's
  request (none during a standing-start hold), on the view's reset (the replay's
  start, every REPLAY OPTIONS apply) or on the pad (Cross, Circle, L1, R1, §G3).
  The shot length x is the time to the next marker (§G14). At each cut
  (`ReplayDirector::avant_garde_cut`):
  1. With TARGET AUTO in a field replay (not 2P), a random car other than the
     followed one is picked first.
  2. From that car on, the first car with a free course shot is followed: a
     record containing D = k L + s (variants from the car's own lap variant
     k of §H6, at least 1, then the next ones, past nvar back to 1) at
     u in [0, 0.8], not the last course shot, lasting at least x s more at the
     car's speed (taken as at least 3 m/s), and whose camera starts at least 15 m
     (horizontally) from the last course shot's camera. Without one, the car
     picked first is followed.
  3. A course shot gets a random roll of -10 .. 10 degrees about the view axis;
     with p = 0.17 a wide zoom (1.0, 90 degrees across); with p = 0.1 a roll
     swing starting at -100 .. 100 degrees that a damped spring brings back to the
     shot's roll (v += (-30 x - 10 v) / 60, |v| at most 900, x += v / 60, at
     60 Hz), only on records that look at the car.
  4. With p = 0.25, or when no car has a free shot, a procedural camera 0 .. 4
     (§G15) replaces it, with p 0 / 0.3 / 0.2 / 0.2 / 0.3 for x under 3 s and
     0.35 / 0.25 / 0.1 / 0.15 / 0.15 otherwise. During a standing-start hold it is
     always camera 4, the car mount.
  5. The shot's look is rolled (§G6), with the dissolve and the trail (§G16).

  The cut follows its car with its battle partner, if any (§G3), except a
  countdown cut (request 1, §G14) whose first car tried had a course shot: that
  follows the car alone. So music cuts and pad cuts take the partner, timer cuts
  the car alone. The procedural cameras always follow the cut's car alone.
- **The director's generator.** Mode 1's cuts, mode 2's on-board cuts, the shot
  look (§G6) and the attract demo's view (§G13) draw from one generator, seeded
  from a clock value when a replay or demonstration starts
  (`RaceScreen::restart_director`), so these choices differ from one viewing to
  the next. It also steps once every 60 Hz frame (§G14). Each procedural camera
  (§G15) has a generator of its own, seeded from a clock value at its cut. The
  interest scores (§G3) draw from a generator of their own, seeded from a clock
  value whenever the target is set.

### §G3 The followed car (TARGET)

In field replays and the attract demo the cameras follow one car, and in view
modes 1 and 2 may frame it together with its battle partner
(`RaceScreen::update_replay_focus`, `step_replay_focus`, `race::ReplayFocus`,
stepped at 60 Hz; tests `replay_focus_manual`, `replay_focus_top`,
`replay_focus_auto_scores`, `replay_focus_battles`, `replay_focus_switch`). The
cars are counted the player's first, then the opponents. A replay or attract
demo starts on the player's car alone, with every car held 40 frames. 2P replays
keep each player's car: none of this applies to them.

**The pad** (`RaceScreen::replay_pad`). During playback only these act (Start
opens the pause, §E2):

- **Up / Down** (modes 0 and 3, and mode 2 with TARGET MANUAL): the next /
  previous car, wrapping, with a new shot for the new car (below, §H8).
- **Cross** (modes 0, 2 and 3): the driver's view on the followed car (the view
  the player last drove with) and back. Back on the replay cameras the view's
  reset re-arms the shot (§H8). A new or restarted replay starts on the replay cameras.
- **Cross, Circle, L1, R1** (mode 1): a new cut (§G2).
- **Up / Down** (mode 1): the race-sound level inside the sections the music
  marks, +0.1 / -0.1, kept in 0 .. 1 (`CutClock::step_level`). Outside such
  sections the level is 1; the disc's songs mark none, so this has no audible
  effect.
- **Right / Left** (mode 1): the music-timed cuts on / off (+1.1 / -1.1 on a
  value kept in 0 .. 1, on from 0.5; `CutClock::step_music_cuts`). On, the cuts
  take request 4, off request 3 (§G14); on the disc both tracks hold the same
  markers, so only the dissolve's odds differ (§G16). No indicator is shown.

Mode 1's level and music-timed cuts are stored at once in `[options]`
`replay_level` ("0.1", the default) and `replay_music_cuts` ("on" / "off", default
off), and read back at every replay's start and REPLAY OPTIONS apply. The attract
demo and the menu backdrops use music-timed cuts off and store nothing.

In modes 0, 2 and 3 Left / Right do nothing; L2 / R2, Square and Triangle never do.
These are the pad's own buttons (the menu pad), not the race layout's actions: a
trigger bound to accelerate is still R2 in a replay.

**By view mode.**

- **Modes 0 and 3.** MANUAL only, never a partner. A new car is aimed at over
  15 frames and held 15 frames. Up / Down re-arm the shot for the new car (§H8):
  a shot that has run out for it gives way to the record at the new car's D
  whatever its progress, else the camera turns on board; for 15 frames after the
  change a cut may take a record at any progress. The attract demo in mode 0 (and the
  menu backdrops) moves to the next car once 300 frames (5 s) have passed and
  the current shot's gate is open (§H8: in mode 0, once the shot has run out
  and any 3 s hold is over), then counts the 300 frames again; its new shot is
  searched at once, the current record included (§H8). Each of these car
  changes dissolves over 1 s (§G16).
- **Mode 1.** The cuts choose the car (§G2). When the followed car's battle
  begins or ends, the aim moves to it with (or without) its partner over 60
  frames.
- **Mode 2.** MANUAL: Up / Down change the car; the new car is framed with its
  partner, aimed at over 60 frames and held 120 frames, and its new shot is
  searched at once (§H8), the current record included, at any progress for the
  next 120 frames. TOP / AUTO: when the interest scores want another car (below),
  the cameras switch to it once the course camera's shot (or its turn on board)
  is 180 frames (3 s) old (`ReplayDirector::camera_fresh`), the followed car's
  hold is over, and the switch gate is open: the shot is over, or the shot length
  x is 1.3 s or more, or a cut request is pending (`ReplayDirector::switch_gate`,
  §G14). The aim moves to the new car (and its partner) over 60 frames, and the
  new car is held 120 frames (2 s). The switch sets a new target: a shot the new
  car is outside of, an on-board shot or a 0x400 record ends and a new one is
  searched at once (§H8); otherwise the shot goes on. When the followed car's
  battle begins or ends, the aim moves likewise, the car is held 120 frames and
  the new target ends an on-board or 0x400 shot (re-armed, §H8).

A held car's battle state stands still, and in mode 2 a held car is not left
for a TOP / AUTO pick. The aim moves from the last target eased by 3t^2 - 2t^3.
Setting a MODE or TARGET in REPLAY OPTIONS (§G4) restarts the scores for the
target and, in modes 0, 1 and 3, keeps the followed car but drops its partner.

**Interest (TOP / AUTO).** Every car has a score, recomputed every frame:

- MANUAL: 1200 for the followed car, 0 for the others (so it never switches).
- TOP: 1200 for the leader, 0 for the others.
- AUTO: when the target is set, 400 / 300 / 200 / 100 / 100 / 100 by race
  position (1st .. 6th and below). Each frame: the followed car and its partner
  lose 1/30; every other car gains 10 with p = 1/60, 1/100, 1/150, 0.0057,
  0.0047, 1/300 by position; a place gained adds 150 and a place lost takes
  100; the score is kept in 0 .. 1200. A bonus by battle state is added
  (candidate 100, battle 400, after a battle 200) times a factor by position
  (1.5, 0.95, 0.85, 0.75, 0.7, 0.65).

The highest score + bonus is looked for never in the first 240 frames (4 s)
after the target was set or the last switch, then every 30 frames with
p = (frames - 240) / 1560, and every frame after 1800 frames (30 s). A different
car becomes the pending pick. On the switch the new car's score gains 200, every
other car's is multiplied by 0.98 / 0.84 / 0.95 when the new car is a candidate /
in a battle / after a battle, the car left goes back to its position's starting
score, and the 4 s count restarts.

**Battles.** Two cars are related when within 10 m of each other. Positions and
battles stand still before the start.

- A car with no partner, once its hold is over, waits 240 frames (4 s), then
  takes the best-placed car within 10 m as its candidate.
- A candidate is dropped when the cars are more than 10 m apart or while the
  shot is settling. If the lowest-placed car within 10 m is another car, a
  battle with that car begins. Otherwise, after 121 frames (2 s) together, the
  pair forms in the "after a battle" state.
- A battle ends beyond 30 m; after 240 frames it becomes "after a battle".
- "After a battle" ends beyond 30 m or after 241 more frames (counted only while
  the shot is settled); a different lowest-placed car within 10 m starts a
  battle with it.
- An ended pairing waits the 4 s again.

The partner counts (for the aim, the zoom, mode 2's on-board cut and the
-1/30 above) in the battle and after-a-battle states. "Settling" is a course
shot's first 2 s from its cut or re-arm (§H8); a camera turned on board and the
driver's view count as settled.

**A pair on screen** (modes 1 and 2). The camera aims at the point midway between
the followed car and its partner. A course shot's horizontal angle
2 atan(1 / zoom) is widened when needed to 1.1 x the angle between the two cars
seen from the eye, plus atan(5 m / x), x being the partner's distance (when the
partner is the nearer car: that less 60 x the difference, at least 1 m), at most
70 degrees; the record's zoom limits then apply (`ReplayDirector::pair_angle`,
test `race_replay_pair_zoom`).

The replay's pop-ups (ITEM time) are the followed car's, changing with it (§D6).

### §G4 REPLAY OPTIONS

The panel is `menu/<LANG>/replay_panel1.pmb` (`replay_panel2.pmb` in 2P), opened
from the replay pause (`race::ReplayOptions`). Rows, top to bottom:

| Row | Cells | Effect |
|---|---|---|
| MODE | traditional, avant-garde, broadcast, course line | the view mode (§G2); the only way to change it. Applying avant-garde also stores BGM on (it stays on in the other modes) |
| TARGET | MANUAL, TOP, AUTO | the mode's target; cells the mode does not allow are disabled and skipped |
| ITEM (1) | easy meter, car name, time | HUD parts shown in replays, toggled (time: the lap count, the Lap line and the pop-ups, §D9) |
| ITEM (2) | map, steering, tires, accel / brake | toggled |
| SOUND | BGM, SE | replay music and effects, toggled; BGM disabled in mode 1 |
| SCREEN (2P only) | single, split (vertical or horizontal art) | a 2P replay as player 1's view only, or both halves |

- **Keys.** Up / Down choose the row. Between the two ITEM rows the column is
  kept, clamped to the shorter row. Left / Right move along the row, skipping
  disabled cells. Circle / Cross apply (MODE, TARGET, SCREEN) or toggle (ITEM,
  SOUND). A disabled cell plays "disable". Triangle / Square close the panel.
  Directions repeat after 30 frames, then every 3. SE "cursor" on a move, "ok" on
  a change.
- **Drawing.** The cells are the page's white icon art tinted by its color
  quads: `color_on` (yellow) for values in force, `color_off` (dark yellow) for
  the others, `color_disable` for disabled ones. The focused cell is lightened by
  a pulsing white: 0.36 x (0.6 + 0.4 x (1 - (frame mod 31) / 30)) added to each
  channel. This pulse was matched to captures of the game.
- **Storage.** Every change applies at once and is stored in `[options]`:
  `replay_mode`, `replay_targets` ("0,2,2,0", one per mode), `replay_items` (bit
  mask, default 0 = nothing), `replay_bgm`, `replay_se`, `replay_screen` (and
  mode 1's pad values `replay_level`, `replay_music_cuts`, §G3). Every apply
  restarts the cut clock's countdown (§G14), clears the shot look (§G6) and is
  the view's reset (§H8: modes 0, 2 and 3 re-arm the shot, mode 1 cuts at
  once). A new MODE or TARGET in a field replay (not 2P) sets the target as
  in §G3.

### §G5 Mode indicator

There is none. No button changes the view mode during a replay (only REPLAY
OPTIONS does, §G4), and no mode indicator is drawn.

### §G6 Shot color filter (view mode 1)

In view mode 1 only (a replay in that mode, or an attract demo that picked it,
§G13), at every cut, the director rolls the shot's look (`race::ShotLook`,
`ReplayDirector::roll_look`; test `race_shot_look`), from the director's
generator (§G2), in this order:

- Levels, with p = 0.25: black = 0.175 r, white = 0.2 + 0.8 r'. In 5 % of those
  the two are swapped, giving a negative in 1.25 % of shots.
- The trail and the dissolve (§G16).
- Then the color filter. If on, it stays on with p = 0.8. If off, it turns on
  with p = 0.1, choosing:
  - a saturation: 1 (p = 0.7), random in 0 .. 1 (p = 0.15) or 0 (p = 0.15);
  - a curve preset, each equally likely: red (1, 0, 0), blue (0, 0, 1),
    cyan (0, 1, 1), (0.3, 0.3, 0.3) or (1, 1, 1).

A replay's start and every REPLAY OPTIONS apply clear the look; leaving mode 1 or
the TV cameras clears it too. On a course without a camera script, mode 1 uses
the fallback cameras (§H7) and rolls the look at each of their cuts (ours).
Modes 0, 2 and 3 roll none.

**The grade pass** (`RaceScreen::apply_post_pass`, `render::DrawOptions::post_grade`;
test `render_post_grade_formula`). The grade is a full-screen pass over the
finished 3D picture, before the HUD, the sun glare and the lamp glows (all 2D,
drawn after it). It is on:

- in replays, demonstrations, the attract demo and the menu backdrops only while
  the shot has levels or the color filter. The half the shot does not set takes
  the course's own values (Env0 black / white +0x98C / +0x990; curve
  +0x980 .. +0x988 and saturation +0x994), or no change without Env0. So the
  course's own grade is not drawn in replays outside such shots;
- in live runs when the course's Env0 has the grade flag (0x80), with the
  course's values (§I1).

The pass works in the game's 8-bit arithmetic (`src/render/shaders/model.frag`,
`gt3_grade`; `render::post_grade_pixel` is the same on the CPU). Per output
channel c a 256-entry table maps an input value i:

1. levels: t = (i / 255 - black) / (white - black), clamped to 0 .. 1 except for
   entry 0;
2. curve: L_c[i] = trunc(255 (t^2 + 2 g_c t (1 - t))), clamped to 0 .. 255
   (g = 0.5 is no change).

Then the saturation s mixes them: with s' = min(128 s, 255), m = max(128 - s', 0)
and w = (0.30, 0.59, 0.11), the weight of input channel k in output c is
trunc(s' + m w_k) when c = k, else trunc(m w_k), and
out_c = min(255, sum_k trunc(L_c[in_k] weight / 128)). Each output channel's
curve applies to every input channel, so the red preset at saturation 0 turns a
pure red pixel gray (75, 75, 75).

### §G7 What a replay shows

- **Single-view replays** (and the attract demo, the theater's demonstrations):
  no race HUD, only the `gt_logo_mini` mark at the bottom right (right edge 620,
  pushed; bottom 420), the course title over the start (§B3; not in the attract
  demo), and the ITEM parts
  on the replay layout (§D9): easy meter, car name, time (with the followed car's
  pop-ups, §D6), map, steering, tires and accel / brake. A theater demonstration
  is not a replay: it draws its items on its run's own layout (§D4), still with
  no position, messages or pop-ups.

  The mark is drawn only while no ITEM is on; the attract demo (which shows no
  items) always draws it. Its opacity follows its own clock from the start of the
  replay or demonstration (`race::ReplayMarkTimer`; test
  `race_replay_mark_timer`): hidden 3 s, fading in over 2 s, shown 30 s, fading
  out over 2 s, hidden 30 s, fading in again, and so on (linear fades). The clock
  advances with each race step, so it stands still while the replay is paused.
  In replays with the display option off (`hud=0`) neither the mark nor the
  items are drawn; the attract demo and the theater's demonstrations ignore that
  option.
- **2P replays** draw both halves with no HUD and no mark, or only player 1's
  view with SCREEN single.
- **Not in replays:** the rear-view mirror, the ghost car, the results overlays,
  rumble and force feedback.

### §G8 Replay sound

- **Effects.** In replays, demonstrations and the attract demo the effects follow
  the replay SE switch (REPLAY OPTIONS SOUND / the option "replay_se"); live
  play follows the race SE option (`se_on`).
- **Music** follows the replay BGM switch, forced on in mode 1 (choosing mode 1
  also stores the switch on, §G4). With the switch off the replay's music
  requests are still made, silently, so the songs' cut markers keep timing the
  cuts (§G14). Replays re-run the start sequence's music (§B2), and a field
  replay's finish jingle comes when the followed car (§G3) finishes. The automatic
  replay after a run (§C4) stops the run's music first and starts its own songs.
- **The race behind a quick menu** (§C5) and the license menu's preview box (§A7)
  are silent.
- **Collision one-shots** are not played in replays: the impulses are passed as
  zero.
- **Sound view.** Engine notes use the replay view gains (§M3).

### §G9 Replay Theater

The theater's pages are menus (the menu and GT-mode topics). What the race screen
does for them:

- **Load Replay** (mode `theater_replay`): the chosen file plays once. Start opens
  the replay pause; EXIT or the end of the replay returns to the theater.
- **Demonstration** (mode `theater_demo`): the disc's demonstration of a license
  test or arcade time trial plays as its run (§G12), with TV cameras (the REPLAY
  OPTIONS view mode, opening as any replay, §H8) and only the mark. It ends 180 ticks after its inputs run out, or by EXIT.
- **Files.** The theater lists `<user data>/replays/*.gt3replay`, newest first,
  with each file's header (`race::list_replay_entries`). The info panel shows
  Size ("123KB"), Date ("YYYY/MM/DD", the saved time or else the file name's
  stamp), Mode (`replay_kind_name`: Time Trial, Arcade, 2P Battle, GT Mode,
  License, Practice, Online), File Name (the comment, else the save date and
  time from the file name's stamp), Car, Power, Course, Record and Laps. There are no slots and no capacity
  limit: the native files are a deliberate replacement for the memory-card pages.
  A file saved before replay format 11 is listed with dashes, its Date and Size,
  and "Recorded with an older version" as its File Name: it can be deleted, but
  choosing it only plays the menu SE "disable" (`race::ReplayTooOld`).

### §G10 The Analyzer (data logger)

`race::Analyzer` on `menu/<LANG>/logger1.pmb` page `replay_logger`. It compares
two laps, TRACK A (red) and TRACK B (blue). It is reached two ways:

- **From the theater** (mode `logger`, GRAPH or REPLAY): the replays' race is
  played again with the data logger on. The logger times the course's logger
  marks and keeps a table entry per mark (the car where the sector starts, the
  time from the lap's start to the sector's end). After every step the car's
  distance into its first lap (from the line) is put into bins of the lap's
  length / 16383 (at least half a meter): per bin the speed, throttle and brake,
  each averaged over the bin (taken as straight lines between two steps), the
  gear and the car's position where the bin ends; at the end every bin's speed
  and pedals are smoothed with their neighbors' (1 2 1) (`race::LoggerBinner`;
  test `exact_game_logger_pass`). SECTOR TIME is the logger's time for the
  whole sector (SECTOR AVER. SPEED its length over that time). The bins' times
  and the frames they end in (for LAP TIME at the cursor and REPLAY's seeks) are
  ours.
  - Time trial, license test and machine test replays are analyzed in one race
    (`RaceScreen::exact_analysis_pass`, test `exact_game_analysis`): track A's
    car drives its run again from its record, and track B's car (TRACK B's
    replay, on the same course) is the race's ghost car, driven by its run's
    recorded controls; the logger records both.
  - Other replays (races, free runs) get a pass of their own each
    (`RaceScreen::exact_logger_track`).
  - TRACK B on another course is left out.
- **From the time-trial quick menu** (DATA LOGGER): A is the session's last lap,
  B its best (GRAPH).

What it shows and how it works:

- **Sectors.** From the theater, the course's logger marks on the race's own
  distance (at most 40 sectors). From the quick menu, the points of the
  course's `logger/<course>` map (LGM0), converted to track distances.
- **Bottom band.** Per track: SPEED, LAP TIME, SECTOR TIME and SECTOR AVER. SPEED
  at the cursor, and the sector bar with up to ten labels in view and the
  cursor's sector lit.
- **GRAPH.** Speed, accel and brake traces of A and B in a window of a quarter
  of the lap (at least 400 m) with the cursor at its first third; the LGM0
  picture with both cars at the cursor. A world point (x, z) lands on the
  picture at (w/2 + s(cos r x - sin r z - ox), h/2 + s(sin r x + cos r z - oz)),
  y down: the map's origin is taken off after the rotation, unlike the HUD map's
  GTCM (§D5), so the sector points and cars land on the outline of turned maps too
  (`LoggerMap::project`; test `analyzer_logger_maps_disc`).
  - Left / Right move the cursor 2 m per frame, 6 m after 30 frames held, 15 m
    after 90.
  - L1 / R1 (L2 / R2) jump a sector. Back jumps to this sector's start, or the
    previous one's when within 5 m of it.
  - Circle / Cross open EXPRESSION: SPEED / ACCEL / BRAKE on or off, DISPLAY
    A / B / AB.
- **REPLAY.** Track A's lap plays in a loop with B as the translucent ghost.
  START / STOP / SLOW (x 0.25) / CAMERA.
  - Runs analyzed in one race: REPLAY is that race, with track B's car as its
    ghost car. A sector jump puts both cars back where the logger saw them at
    the start of the cursor's sector, and the race runs on from there at once,
    driven by the runs' recorded controls, so the cars drive their runs again
    tick for tick (no replay from the lap's start; `RaceScreen::exact_analysis_jump`).
  - Other replays: B's lap is drawn with A's model in step with A's, and a
    sector jump plays A's replay again from its start to that point.
- **Leaving.** Triangle / Square leave.

The cursor speeds, the window and the sector-jump rule were matched by playing
the game. The drawing of the traces, dots and sector bar is ours.

### §G11 Ghost

As the original's: license tests, time trials, machine tests and free runs have a
ghost; races, 2P battles and the free practice before a GT race have none. The
ghost is a car of the race: the player's car once more, driven by a recorded
run's pad inputs (`src/sim/exact/control_record.h`; the race's
`sim::exact::SoloRecorders`). It touches nothing: the player's car drives
through it. It is drawn as a translucent car (alpha 0.4) in live runs only (not
in replays, demonstrations or the quick menus' backdrops), and not while the
computer drives it (such as a flying start's run-up to the line). A visit
starts without one: it comes from the visit's runs or from LOAD GHOST (nothing
is loaded by itself).

**License tests, time trials, machine tests: the best run.** The ghost is a whole
run, in step with the current run from its first tick: its car drives the run
again from that run's recorded pad inputs, every tick's but the run's last
(test `exact_replay_ghost`). During a standing start's hold it stands on the
start with the player's car, and the in-car view shows its body around the
camera. After each live run (`RaceScreen::solo_run_ghost`):

- a run that set the session record (§E3: a license test's judged time, with a
  medal or TIME EXPIRED, a time trial's lap or a 0-400 m / 0-1000 m time, at or under the visit's best)
  becomes the visit's **best run**;
- a run with a time (the judged time, the time trial's lap, the time to the goal;
  none after a FAIL or EXIT, nor in the top-speed test, which so never gets a
  ghost) under the ghost's (whole ms; a tie keeps the ghost), or with no ghost yet,
  gives the ghost the best run;
- a run of 10 minutes (36,000 ticks) or more sets no record and no ghost
  (`race::ghost_recording_kept`, `race::replaces_ghost`).

**Free runs: the best lap.** The ghost is a lap: at each line crossing the
ghost car is put back on the ghost lap's start and drives it again, in step
with the current lap. The race keeps the best lap and the ghost: on every lap
(live, its recording under 36,000 ticks) strictly under the visit's best lap,
that lap becomes the best lap; under the ghost's too (or with no ghost yet) it
gives the ghost the best lap (`race::takes_best_lap`). Both last the visit: a
restarted run's ghost car takes the visit's ghost lap at its first crossing
(test `exact_game_free_run_visit`).

**When the ghost changes.** With the Options menu's Ghost on (the default) a
better run replaces the ghost at once (a free run's lap: from the next line
crossing); with it off the ghost stays, and the quick menu after the run opens on
"Update Ghost?", whose YES makes the best run (lap) the ghost (`ghost_dlg`). The
option never hides the ghost.

**Select** (the controller's own, as the original's pad) shows or hides the ghost
for the rest of the visit (shown at first).

**LOAD GHOST and SAVE GHOST** (ours: replay files stand in for the original's
memory-card ghost files). LOAD GHOST loads at once, with no question, from the
newest ten saved replays of the course and car that this disc can play
(`RaceScreen::load_saved_ghost`): in a free run the fastest best lap of a saved
free run; in the other runs the fastest saved run of the same test (course,
car, kind and start), whole, whose car becomes the next run's ghost car. SAVE
GHOST saves the best run's replay (a free run: the run that drove the best
lap), not the last run's.

No "GHOST" time is shown (ours had one at the top center). In online races a
"ghosted" remote car is drawn at alpha 0.5.

### §G12 Demonstrations

The disc's `demo/<event>` runs (48 license tests, 10 arcade time trials) are
replay saves of a demonstration drive: the car, its parts and settings, and its
controller inputs (`race::DemoReplay`). Steering is ±4096, positive to the
left. Frame 0 is the race's first step, countdown included. Each save becomes a
race record (`race::demo_record`, `src/race/exact_demo.h`): the event's race
run as its mode (license tests as license tests, arcade time trials as
time trials), the save's car as the race's only entry, and the recorded inputs
for its car, then 180 empty steps. It plays as every replay plays (§G1), on the
US release's rules on every disc, and nothing takes the car over: it runs as
the inputs drive it. Only some end on the time the save records;
the others end with no time or, in the arcade time trials, without completing
their lap (test `exact_demo_records`). Without a demo save, the quick menu's
DEMONSTRATION has the race's AI drive the car instead.

- **Run-up.** The flying run-up is driven by the race's AI, and the recorded
  frames are ignored until the line.
- **Viewing.** Demonstrations are watched from the TV cameras in the REPLAY
  OPTIONS view mode, and open as every replay does (§H8). Start, Cross or
  Circle end a quick-menu demonstration.

### §G13 The attract demo's view

The title attract demo is a race of CPU cars only: the car in the player's slot
is driven by the race's AI like the others (`ai.md` §1.2), on the US release's
rules on every disc. It does not use the stored REPLAY OPTIONS view mode. It
picks its own view mode and target when it starts (`race::pick_attract_view`,
`RaceScreen::pick_attract_view`, test `race_attract_view_pick`), from one draw r
in [0, 1) of the director's generator (§G2):

- r < 0.33: mode 0, MANUAL (from the demo's car, the next car after 5 s once the
  shot's gate is open, §G3);
- r < 0.66: mode 2, target int(100 r) mod 3 (MANUAL, TOP or AUTO);
- otherwise: mode 1, AUTO.

Mode 1 is forced when the three demos before it (since start-up) or the two
demos since the last mode-1 demo did not use it. The count is kept for the
session. The pick is not stored. Its mode decides the shots (§G2), the shot
look (§G6) and the music switch (forced on in mode 1, §G8); its target the
followed car (§G3). In mode 1 its music-timed cuts are off (§G3).

### §G14 Cut timing (the camera manager)

Every 60 Hz frame, in live runs and replays alike, the camera manager
(`RaceScreen::update_camera_manager`) runs in this order: the race music's
markers step (`race::MusicMarkers`), the cut clock gives the shot length
(`race::CutClock`), view mode 1's handler runs (§G2; only in mode 1 on the TV
cameras of a course with a camera script), then the clock makes the next frame's
cut request. Tests `race_cut_clock`, `race_music_markers`.

- **Markers.** The race songs and the attract demo's track carry cut markers
  (the music topic, `music.md` §8); jingles, license runs, an empty playlist and
  silence carry none. They are read at the song's audible position plus 1/30 s.
  Marker track 3 holds the cut points with music-timed cuts off, track 4 with
  them on (§G3); the disc's songs have the same markers in both. A new song fires
  every marked track once on its first frame.
- **Shot length x.** In mode 1, the time to the next marker of track 4 (on) or 3
  (off). In modes 0, 2 and 3, the time to the next marker of track 3 or, if
  sooner, of track 2 (the music's tracks 5 .. 7, unmarked on the disc, would
  change which apply). Without a next marker within 80 s (no marked song), the
  countdown / 60. Mode 1's procedural pick, its course shot's minimum length
  (§G2) and its procedural cameras' moves (§G15) use x; so do mode 2's gates
  (§G3, §H8).
- **Requests.** Each frame the request is first 0. When no relevant marker track
  is active (track 4 with the cuts on, else tracks 2 and 3; active = a next
  marker within 80 s), a countdown falls by 1 a frame and, below 0, gives
  request 1 and restarts at 120. During a standing-start hold (the cars held
  before the signal) the countdown is held at 120. Then in mode 1 a track-4
  marker gives request 4 (cuts on) or a track-3 marker request 3 (off); in
  modes 0, 2 and 3 a track-3 marker gives request 3 and a track-2 marker
  request 2.
- **Set-up.** A replay's or demonstration's start sets request 1 and x = 4 s and
  the countdown to 60; every REPLAY OPTIONS apply restarts the countdown at 60.
  Without a marked song the first cut therefore comes at once, the next 61
  frames later, then every 121 frames.
- **During the hold** the handler drops the requests: mode 1 cuts there only on
  the view's reset or the pad, and those cuts' procedural cameras are always the
  car mount (§G15).

The director's generator (§G2) steps once every frame here. With the replay BGM
switch off the songs still play silently, so the markers keep timing the cuts
(§G8). Modes 0, 2 and 3 do not cut on the requests: their shots follow the car's
position (§H8). Each frame, before mode 1's handler, the shot length x and
whether a request is pending (the one made the frame before) are handed to the
director (`ReplayDirector::set_cut_gate`): mode 2's cut gate and TOP / AUTO
switch gate open on them (§H8, §G3).

### §G15 Procedural cameras (view mode 1)

A mode-1 cut may use one of five procedural cameras instead of a course shot
(§G2; `race::ProceduralCamera`, test `race_procedural_cameras`). Each plans its
shot at the cut from its own generator (seeded from a clock value) and the shot
length x (§G14), moves at 60 Hz, and follows the cut's car only, never its
battle partner. Angles are in degrees; "up" is above the road or the car.

| Kind | Name | The shot |
|---|---|---|
| 0 | Roadside | A still camera 0.1 .. 1.1 m above the road. With p 0.5 on the car's straight-line path: x k s ahead, k 0.5 .. 0.85 looking back at the car or 0.05 .. 0.2 looking on along its travel, with 1 m of scatter. Otherwise beside the car (0.7 .. 2.2 half widths out, up to a car's length along), aimed so the car's place 1 .. 3 s on sits at the frame's edge. FOV 55 .. 85, pitch -15 .. 5, yaw ±10 off the aim, roll ±15. With no road under the point: 1 m above the car, along its motion, pitch ±10, level. |
| 1 | Hover | Carried with the car (its heading only), 1 .. 9 m away on a random bearing and 3 .. 7 m up, looking down at the car; FOV 55 .. 85, roll ±20. Four springs wobble the eye (±1 m) and the aim (±FOV / 14) like a hand-held camera. |
| 2 | Orbit | An orbit carried with the car, 0.2 .. 5.2 m beyond the body's outline, sweeping 100 + 220 r min(x, 3) degrees over the x seconds (eased: fast through the front and back, slow at the sides) to end within 30 degrees of behind the car, dropping from up to 3.1 m to 0.1 .. 1.1 m while its center slides; FOV 40 .. 70, roll ±20. Kept above the road. |
| 3 | Telephoto | 25 .. 40 degrees, carried 2.5 .. 6.5 m past the front or rear bumper, 0.1 .. 1.3 m up and up to 1.5 m aside, looking back at the car; roll ±20. With p 0.3 dead center and level. A shake grows with the speed from 5 to 40 km/h. Kept above the road. |
| 4 | Car mount | One of the car's camera mounts (its GTCI camera table, all 20 records): a random one from the fourth on (0, the in-car point, with three or fewer). The eye is at the mount's position in the car's frame, with its pitch, yaw, roll and zoom. |

FOVs are horizontal, converted as in §H6. A car mount without the "outside" flag
(GTCI flags bit 1; the in-car point, the bumpers, the driver's head) is on board:
the followed car is not drawn while it is used (`CameraRig::replay_on_board`). The
outside mounts show the car. The road under a point comes from a probe of the
course's surface (none off the road or on a wall).

### §G16 Dissolves and the trail

A dissolve freezes the last picture shown and fades it out over the new one
(`race::DissolveClock`, `RaceScreen::start_dissolve`; test `race_dissolve_clock`).
Over d seconds, x starts at 1 - 1 / (60 d) and falls by 1 / (60 d) a 60 Hz frame
(from the frame after the freeze); the frozen picture's weight is
trunc(128 x^3) / 128. A 0.5 s dissolve lasts 29 frames. Dissolves come:

| When | Time |
|---|---|
| a mode-1 cut, with p 0.4 (0.1 for a request-3 cut, §G14) | 0.5 s |
| every replay or demonstration start, from the live run's last picture (§G1) | 0.5 s |
| the pre-race shot's two cuts and its cut to the player's view (§B3) | 1 s |
| a car change in modes 0 and 3: Up / Down, or the attract demo's (§G3) | 1 s |
| a cut of the shot picker in modes 0, 2 and 3 when a shot ends (§H8; test `race_replay_record_dissolve`) | the outgoing shot's: its record's own time (+0x64, §H6), 1 s from an on-board record or a turn on board, at most 4 s; none for 0. None for the cuts of a re-arm (the replay's first shot among them) and for a turn on board after the first since the set-up, outside a car change that searches at once (§H8) |
| a cut of the fallback cameras in mode 1 (ours, §G6), with p 0.4 | 0.5 s |

**The trail** (mode 1). With p = 0.1 at a cut (0.6 when the cut took the orbit
camera, §G15) the shot gets a trail: the last picture shown is drawn over the new
one at weight min(timer, 0.8) (rounded down to 1/128), so moving things smear;
the last picture holds its own trail. The timer is 0.5 .. 2.0 at the cut; with
p = 0.5 it falls by 0.02 a frame and the trail ends once it is below 0, otherwise
it stays.

Both are drawn by the full-screen pass (§G6) after the grade: the graded picture,
then the frozen picture at the dissolve's weight, then the last picture at the
trail's weight; the HUD is drawn after them.

---

## §H Cameras

`race::CameraRig`. The views are evaluated every frame on the interpolated car
state. The smoothing and lag filters step at 60 Hz.

### §H1 Player views

The two views use the car's camera points 0 (in-car) and 1 (chase) from its GTCI
block (`car_camera_points`). If a car has none the fallbacks are (0, -0.8, 0)
and (0, -1.5, -6). The eye sits at -p in a frame at the body origin (the car's
position raised by the front ride height, 0.13 m if unknown). The view looks
along that frame's forward axis. Both views are 60 degrees across the 640 x 448
frame: vertical FOV = 2 atan(224 / (320 sqrt 3)) = 44 degrees. Every camera keeps
its vertical FOV, so wide screens see more at the sides (Hor+).

| View | Attitude | Notes |
|---|---|---|
| in-car (point 0) | the body's attitude | own car not drawn, rear-view mirror, in-car vibration (§H3), onboard HUD and sounds |
| chase (point 1) | lagged heading and pitch, no roll | no lag in position |

**Chase lag.** At 60 Hz with h = 1/60:

- Heading: acc = 30 e - 10 w, with e the wrapped heading error;
  w += 0.5 (acc + acc_prev) h (trapezoid); heading += w h.
- Pitch: w += (20 e - 10 w) h; pitch += w h (semi-implicit Euler).

Between 60 Hz steps (frame rates above 60) the angles carry on at their rates, so
the view does not judder.

### §H2 Change view and look back

- **R1 changes view.**
  - Default cycle: in-car with mirror → in-car without mirror → chase →
    far chase → in-car.
  - `[race] views = gt3` keeps the game's two: in-car ↔ chase.
  - A new session starts in-car with the mirror. Each player's last choice (in-car,
    chase or far chase, with the mirror state) carries over to the next race;
    ours keeps it for the session only.
- **L1 held looks back.**
  - Chase: heading + π, pitch negated.
  - In-car: the forward axis reversed and the eye's z mirrored.
  - No mirror while looking back.
- **Rear-view mirror** (in-car views, not in replays, not while looking back):
  - **Placement.** A 240 x 64 rectangle at (200, 20) of the frame (pushed by
    106.667 in 16:9) with a 1-unit black outline. On a 4:3 2P half it is
    240 x 55 at (200, 16) of the 640 x 224 half. Side-by-side 2P halves have
    none.
  - **View.** The camera's view with z flipped (mirrored), 1.3 x zoom, far plane
    120 m, the player's own car left out.

### §H3 In-car vibration

Only in the in-car view, and also while looking back. Once per 60 Hz update:

1. Amplitude A = sum over the wheels of (0.3 |slip ratio| + |slip angle| / (π/2)),
   at most 3.
2. Speed factor S = speed clamped to 10 .. 120 km/h, divided by 120 km/h.
3. On about half the updates each axis gets a kick u = (r - 0.5) A 250.
4. Each axis then steps v += ((u - u_prev) - 12 v) / 60, x += v / 60.
5. The view turns about the eye by 8 S clamp(x, -1, 1) degrees of pitch and
   2 S clamp(x, -1, 1) of yaw. There is no roll and no eye movement.

The random numbers come from a fixed generator (s = 17 s + 17,
out = s xor rotl16(s), seed 0x87654321, two steps per draw), with extra draws
each update so the sequence matches the game's. `[race] camera_shake = off` (ours) disables
it. Test `race_camera_shake`: no slip, no shake; with slip, at most 8 degrees;
never in the chase view.

### §H4 Our extra views

All ours:

- **Far chase.** Behind the car along a smoothed heading: rate
  3.5 + 0.05 min(v, 40) per second, height smoothed at 6 per second. The eye is
  7.5 m + half the car's length back and 2.4 m up, looking at a point 1 m up and
  2.5 m ahead. Vertical FOV 0.72 rad.
- **Roof.** 0.2 m above the roof, 0.2 m back; vertical FOV 0.85 rad.
- **Free orbit.** Right stick or mouse; L2 / R2 or the wheel zoom from 2 to 400 m.

Roof and free orbit are not in the R1 cycle: only the debug presets and the
Camera row of the quick menu's option panel select them.

### §H5 Pre-race shot

See §B3.

### §H6 Replay (TV) cameras

The course's camera script (slot 34, `GTRC`; `race::GtrcScript`,
`race::ReplayDirector`) drives the TV view in replays, demonstrations and the
attract demo.

- **Records.** Each record has a kind, a start and end track distance, zoom
  limits and curves (piecewise linear in t, linearly extrapolated) for zoom,
  camera x / y / z and target x / y / z.
- **Kind bits.**
  - kind & 3 is the mode: 0 trackside (world coordinates), 3 on-board,
    1 car-local.
  - kind & 0xC set: look at the car (+0.4 m up) instead of the target curves.
  - kind & 0x10 set: zoom = distance x zoom(0) instead of zoom(t).
- **Lap variants.** Records are laid out by D = k L + s, where s is the car's
  distance along the lap, L the lap length, and k the car's lap variant, with
  nvar = max(1, int(T / L) - 1 - int(first start / L)) variants. k comes from
  the car's lap counter (`race::lap_counter`: 0 before its first crossing of the
  start line, then the lap it is on, 1 during lap 1; `RaceScreen::lap_count` for
  the player's car, `RaceField::opponent_lap_count` for the others): k = 0
  before the start line, so D = s and the script's first records, from the first
  start on, cover the start and run on across the line; then
  k = ((lap - 1) mod nvar) + 1 (`GtrcScript::lap_variant`). This gives
  consecutive laps different cuts. Trial Mountain has 66 records and 3 variants
  (test `race_replay_cameras`).
- **The records at D** (`GtrcScript::records_at`, test `race_replay_lap_variants`).
  Only the last record starting at or before D counts, when D is before its end
  (an earlier record that also contains D is not found). When that is the list's
  last record, the search goes on from the first record with D - nvar L, taking
  those that start at or before it and contain it (at most 32 in all).
  `GtrcScript::record_at(D, u_max)` takes the first of them with
  u = (D - start) / (end - start) in [0, u_max], u taken with D itself (so a
  record found past the end of the list never qualifies).
  `GtrcScript::other_variant(k, s)` tries the other lap variants k + 1, k + 2, ...
  (past nvar back to 1; nvar tries) and takes the first record_at(v L + s, 0.6).
- **Choosing a shot.** View modes 0, 2 and 3 choose their shots with the shot
  picker of §H8; mode 1 cuts on its own (§G2). A shot keeps its own D while it
  runs: the car's D, plus nvar L once the car's variant has wrapped from nvar
  back to 1; for a shot of another lap variant v, v L + s, plus L once the car
  has crossed the line since the cut.
- **Evaluating a shot.** A record that aims at its target curves (kind & 0xC
  clear) and has a duration (+0x0C) of 1 s or more advances by time:
  t = clamp(seconds since the cut or re-arm / duration, 0, 1), so its camera
  moves even while the car stands still; it runs out when its time is over,
  wherever the car is (§H8). Every other record (looking at the car, or under
  1 s) advances with the car: t = clamp(u, 0, 1), and runs out when u leaves
  0 .. 1 (`ReplayDirector::shot_t`, test `race_replay_record_timing`).
  - Eye and target come from the curves (or the car, see the kind bits).
  - The zoom is clamped to the record's limits (when max > min) and kept at 0.2
    or more.
  - The zoom is smoothed towards that value at 4 per second, snapping on a cut.
    The rate is approximate: the game's is not known.
- **Field of view.** The zoom is 1 / tan of the horizontal half-angle across the
  640-pixel frame; on the 448-line frame that gives vertical FOV
  = 2 atan(0.7 / zoom) (`ReplayDirector::zoom_fovy`), as for the player views
  (§H1). Mode 1's procedural cameras convert their zoom the same way (§G15).
- **Dissolve** (record +0x64). The seconds of dissolve when a shot of the record
  ends and the picker cuts away from it in modes 0, 2 and 3 (§H8, §G16).
- **On-board records** (kind & 3 = 3) put the camera at the followed car's
  camera mount named by the record (+0x18; the car's GTCI table, as mode 1's car
  mount, §G15), and the camera's on-board turns (§H8) at its mount 0, the in-car
  point; without a table, the in-car point (0, -0.8, 0) at zoom 1.435. A mount
  without the "outside" flag hides the followed car (§G15). Mode 2's on-board
  cut is a bumper view on the partner's car instead (§G2).
- **Roll.** Mode 1's course shots are rolled about the view axis by the cut's
  roll and roll swing (§G2); the other modes' shots are level.

The TV view follows the followed car of §G3: the shots are chosen by its track
distance, and "the car" they look at is its position, or with a partner the
point midway between the two, with the shot widened (§G3). Mode 1's procedural
cameras follow the cut's car alone (§G15). The test reports 197 shots over a
Trial Mountain lap (lap 1) sampled every 20 m, 190 of them within 160 m of the
car.

### §H7 Fallback TV cameras

The engine uses these when a course has no script (in view mode 1 too, rolling
the shot look at their cuts, §G6); they are a placeholder (`build_tv_cameras`). It places max(4, L / 90) cameras round the lap, alternately
12 m to either side of the road and 4 m above it. The camera whose point is
nearest to 20 m ahead of the car is used. Its vertical FOV is
clamp(2 atan(6 / d), 0.08, 0.9), with d the distance to the car, so the car keeps
a similar size. With no cameras at all, a high chase is used instead: 14 m back,
6 m up, vertical FOV 0.7 rad.

### §H8 The shot picker (view modes 0, 2 and 3)

Modes 0, 2 and 3 share one shot picker (`ReplayDirector::update`), run every
frame on the followed car's D = k L + s (§H6). Mode 2 adds a gate and its
on-board cut (§G2). Tests `race_replay_start_shot`, `race_replay_shot_gate`,
`race_replay_broadcast_onboard_cut`, `race_replay_record_dissolve`,
`race_replay_record_timing`, `race_replay_demo_openings_disc`.

**Turning on board.** Where no record fits, the camera turns on board: the
followed car's in-car view (its camera mount 0, §H6). The record the camera was
on stays its record: the on-board shot runs out when the car's D leaves that
record.

**Re-arming.** The view's reset (`ReplayDirector::reset`: a replay's set-up,
every REPLAY OPTIONS apply, a change of view mode, Cross back from the driver's
view) and a new followed car (`ReplayDirector::car_changed`) re-arm the shot at
the next frame. A new target (`ReplayDirector::target_set`: a new followed car or
battle partner, §G3) re-arms it too when the shot is on board, its record has
kind 0x400, or D is outside the record's range. A re-arm takes the camera off
board and restarts the shot's clock on its record. If the shot has then run out
for the car (D outside the record; a timed record does not run out at once), the
camera cuts to the first record at D whatever its progress (record_at(D, 1)),
else turns on board. Otherwise the shot goes on, unless the gate below is already
open, in which case the search below follows at once.

**The first shot.** A replay's or demonstration's set-up
(`ReplayDirector::setup`, §G1) leaves the camera on the script's last record with
the view's reset. So the first frame re-arms: unless the car is inside the last
record (or that record is timed), the first shot is the record at D whatever its
progress, or the camera turns on board. Before the start line (k = 0, D = s) that is one of the script's
first records once the car is past the first record's start, and it runs on
across the line. Further back the first frame is on board, and the next frame
searches: another lap variant's record (u 0.6 or less) or on board again. The
arcade time-trial demonstrations open so (test `race_replay_demo_openings_disc`,
the start's distance before the line, then the first frame's record and the
second's):

| Demonstration | Before the line | First frame | Second frame |
|---|---|---|---|
| AT05 Laguna Seca | 149 m | record 0 | record 0 |
| AT01 Midfield | 199 m | record 5 (its start records reach 1217 m back) | record 5 |
| AT03 Akasaka | 199 m | on board | record 25 (variant 1's, at the lap's end) |
| AT02 Trial Mountain | 129 m | on board | record 21 |
| AT04 Deep Forest | 199 m | on board | on board (no variant's record at u 0.6 or less) |

**When a shot ends.** A shot runs out when its timed record's time is over, or
else when its u leaves 0 .. 1 (§H6). Its gate (`ReplayDirector::shot_state`,
`ReplayDirector::gate_open`) then decides when the next shot comes:

- Modes 0 and 3: at once.
- Mode 2: once the shot length x is 1.3 s or more or a cut request is pending
  (§G14; `ReplayDirector::set_cut_gate`). A record of kind 0x400 (not on board)
  ends at once when it runs out, and in its last tenth (t past 0.9; before 0.1
  while the car runs backwards through a record looking at it) it may already
  end on that gate.
- A shot of another lap variant, and a turn on board, that came from a search
  hold at least 3 s (180 frames) first (0x400 records excepted).

**The search** (after a gate opens, or at once for a search-first car change):

1. The first record at D with u 0.6 or less (`GtrcScript::record_at`), unless it
   is the current record.
2. Else another lap variant's record (`GtrcScript::other_variant`), if it is not
   the current one; this shot then holds 3 s.
3. Else the camera turns on board, holding 3 s; while nothing fits it searches
   again every 3 s (modes 0 and 3; mode 2 also waits for its gate).

For a while after a manual car change (Up / Down: 15 frames in modes 0 and 3,
120 frames in mode 2) step 1 takes a record at any progress and may take the
current record again. A search-first car change (the attract demo's car change
in modes 0 and 3, a manual change in mode 2) searches on its next frame instead
of re-arming, the current record included. So does mode 2's TOP / AUTO switch
(`ReplayDirector::target_switched`) when the new target ends the shot.

**Dissolves** (§G16). Each cut of the search dissolves from the last picture for
the outgoing shot's time: its record's own (+0x64), 1 s from an on-board record
or from on board, at most 4 s, none for 0. A turn on board from the search
dissolves so only when it is the camera's first turn on board since the set-up,
or at a search-first car change. The cuts of a re-arm, and so a replay's first
shot, have none (beyond the replay's own start dissolve, §G1).

**Mode 2's on-board cut.** Each new shot of mode 2 (a cut, a turn on board or a
re-arm) draws once from the director's generator: with p 0.07, while the
followed car has a battle partner, the camera turns on board the partner's car
(§G2), until that shot ends (gate, search) or a new target re-arms it. A shot
counts as fresh for its first 180 frames (3 s) from its cut, re-arm or turn on
board (`ReplayDirector::camera_fresh`); TOP / AUTO do not switch cars meanwhile
(§G3).

---

## §I Lighting

Course lighting comes from the course's environment block (slot 32, `Env0`;
offsets in `docs/formats/course.md`), read by `race::SunParams::from_env0`.

### §I1 Fog, clear color and color grade

- **Clear.** The race view is cleared to black.
- **Fog.** The course's fog is visibility = b + (1 - b) min(1, A / z) towards the
  fog color (+0x60 RGB, +0x70 A, +0x74 b). It is on when b < 0.999 and A > 0.
  Example values: Trial Mountain A 120, b 0.70; Seattle 55 / 0.65; Rome
  75 / 0.98; Laguna Seca b 1.00 (no fog). A course without Env0 gets a
  placeholder linear fog (350 .. 3000 m, at most 55 %, the sky's horizon color).
  `GT3R_NO_COURSE_FOG=1` forces the placeholder.
- **Draw distance.** 2500 m.
- **Color grade** (flag 0x80): the levels black / white (+0x98C / +0x990),
  per channel the curve x^2 + 2 g x (1 - x) (+0x980 .. +0x988; g = 0.5 is no
  change), then the saturation (+0x994) around luma (0.30, 0.59, 0.11). It is a
  full-screen pass over the finished 3D picture, before the HUD, in the game's
  8-bit arithmetic (§G6). Live runs use it on the courses with the flag (Monte
  Carlo, Trial Mountain, Akasaka, reverse Deep Forest, reverse Grand Valley);
  where the white level is below 1 the picture is brighter. Replays use it only
  under a view-mode-1 shot's levels or filter, with these values as the defaults
  (§G6).

### §I2 Car lighting and road shade

- **Car lights.** Cars are lit with the course's car lights (`day_car_lights`).
  Env0 +0x10 bits 0 .. 3 enable car-light records 0 .. 3 (+0xD0 + 0x80 n). The
  first enabled one with a direction is the main light; up to three more are fill
  lights. Ambient = the global ambient (+0xEF0) plus the enabled records'
  ambients. The shading is texel x min(Me + La Ma + Ld Md N.L + Ls Ms (N.H)^p, 2).
- **Course lights.** The course's own lit packets use the Env0 scene lights
  (+0xAD0 + 0x50 n: direction, ambient +0x10, diffuse +0x20, specular +0x30),
  enabled by +0x10 bits 11 .. 14, with no global ambient: the enabled lights'
  ambients add up, and those with a diffuse or specular color become the
  directional lights, in light order (`SunParams::course_lights`, test
  `race_course_scene_lights`).
- **Road shade.** The road's vertices carry two attributes, A (direct sun) and B
  (open sky, 0 in tunnels and under bridges), each u16 / 65535 and interpolated
  over the highest road triangle under the point (`race::road_shade`). The
  engine averages them over the four wheels and scales the car's ambient,
  diffuse and specular by
  s = P2 + (1 - P2) b (1 - (1 - P1)(1 - a)).
  (P1, P2) comes from +0xAC8 / +0xACC for ambient, +0xAB4 / +0xABC for diffuse
  and +0xAC0 / +0xAC4 for specular.
- **Light shadow factor.** The light shadow's factor is
  F = (F_shade + (F_lit - F_shade) a) b, with F_lit = +0xAB0 and
  F_shade = +0xAB8; F_lit when no wheel is over the road.
- Test `race_road_shade_disc` checks the shade lookup.

### §I3 Night point-light courses

Point-light courses (GTRW byte +0x21 = 1: Route 5 with 281 lamps, Route 11 with
273, the circle tests) light each car with lamps from the GTRW lamp list
(`race::CoursePointLights`):

- **Candidates.** The front-left wheel's road attribute A holds the nearest
  lamp's index. The nine lamps around it (from index - 4) are candidates.
- **Weights.** A directional record (kind 0) has weight 1. A point lamp has
  w = 1 / (c0 + c1 d + c2 d^2). With a spot cutoff (≠ 180 degrees), w is
  multiplied by max(0, l.axis)^exp inside the cutoff cone and set to 0 outside.
- **Lighting.** The four strongest become directional lights along car → lamp
  with the lamp type's colors x w; their ambients add to the global ambient.
- **Shadows.** One car shadow per lamp: sheared by -(l.x, 0, l.z) x the shadow
  length (+0xA78), alpha = the type's shadow alpha x w, in lamp type 0's shadow
  color.

### §I4 Car shadows

From the car model's nodes 1 (the light shadow) and 6 (the contact shadow),
drawn in the car's ground frame (the plane through the four wheel contacts, the
car's heading; `CarShadowParams`):

- **Node 1** is sheared along the light: shift = (sin α, 0, cos α) x length x
  cos ε per meter of height, with α = -azimuth (+0xA74), ε = -elevation (+0xA70)
  and length +0xA78.
- **Color and alpha.** Color +0xA60; alpha = vertex alpha x +0xA6C, times F
  for node 1 (§I2).
- **Placement.** Both are lifted 2 cm, with a 2 % pull towards the eye, blended
  without depth writes, before the car's body (§I7), so the body and wheels cover
  them.
- **Headlamp pool** (node 5): drawn additively in the ground frame, its alpha
  scaled by Env0 +0x58. 0 means not drawn (for example Trial Mountain); Seattle
  uses 0.5 and Route 5 1.0.
- `[video] car_shadows = off` turns the car shadows off.

### §I5 Car reflections and dirt

- **Reflection map.** A 128-pixel cube map is rendered every frame from 0.8 m
  above the player's car, with scenery within 200 m (`update_env_cube`).
  Courses that have the game's reflection scene (slots 23 and 5) render exactly
  that: the dark sky dome turned by Env0 +0x28 degrees about +Y, then +0x48
  about +X (right-handed: its painted glow then lies within a few degrees of the
  sun's azimuth on Rome, Midfield, Seattle and Super Speedway), and the slot 5
  scenery, on black. It is unfogged and ungraded, and
  quantized to 5 bits per channel. Other courses render the drawn course.
- **Lookup.** The map is read with a view-dependent lookup like the game's
  (`DrawOptions::env_gt3_lookup`), with a Fresnel-like weight 1 - 0.8 cos.
  `[video] car_reflections = off` uses a sky / ground color gradient instead.
- **Lamp discs on point-light courses.** Of the same nine candidate lamps (§I3),
  the six strongest point lamps are drawn into the map as discs: a 10-wedge fan at
  the lamp's direction, radius = the record's disc size (+0x40) / distance,
  center color +0x70 .. +0x7C with alpha a sqrt(w), fading to 0 at the rim.
  Only the player's car gets them.
- **Dirt.** The player's car's reflection is multiplied by 1 - dirt. Dirt
  (0 .. 0.8) is the car's own dirt level as the race keeps it, starting from the
  run's dirt and growing with the smoke emission of the wheels on loose and wet
  surfaces (`physics.md` §8.3b; `update_dirt`).
  - The wet Route 5 never dims the reflection.
  - Other cars are clean.
  - Dirt does not change in replays.
  - The final dirt goes back to GT mode (`GameResult::car_dirt`).

### §I6 Sun glare

`race::SunGlare`. The sun is on when Env0 flag 0x400 is set and the glare size
k (+0x9D0) is above 0.

- **Direction.** (sin a cos e, -sin e, cos a cos e) with e = -elevation (+0x9DC),
  a = -azimuth (+0x9E0).
- **Visibility.** 3 x 3 rays through an angular box around the sun direction are
  cast against the course's opaque scenery and backdrop triangles (up to 6 km),
  and the open fraction is used (`sun_visibility`, `race::Occluder`). The box's
  half-angle is max(1, N) x 0.5 x vertical FOV / 448, with N = +0x9E8 (the
  game's occlusion box in pixels); the rays approximate it.
- **Drawing.** One additive, screen-aligned billboard of `race/glare.img` is
  drawn at the sun after the scene, with angular half-size atan k. Its color is
  sun RGB (+0x9C0) x clamp(W, 0, 1) x visibility, with W = +0x9CC.
- **What it lacks.** No ghosts, no falloff towards the screen center, no bloom.
  The sprite is drawn for every style (+0x9E4); the procedural styles and the
  sun disc are not drawn.

Values decoded from the disc:

| Course | Elevation | Azimuth | k | W | N |
|---|---|---|---|---|---|
| Trial Mountain | 31.0 | 92.0 | 0.40 | 0.50 | 2.1 |
| Seattle | 16.8 | 220.2 | 0.45 | 0.65 | 10 |
| Laguna Seca | 11.6 | -31.6 | 1.10 | 1.53 (clamped to 1) | 10 |
| Rome | 22.6 | 328.0 | 0.20 | 1.13 (clamped to 1) | 10 |

Route 5, Route 5 dry and Midfield have the sun flag off. Route 11 has it on with
k = 0, so no glare is drawn.

### §I7 Draw order of the scenery and the cars

- **Scenery.** The course scenery (slot 1, the visible nodes) is drawn in one
  pass in file order, its opaque and blended packets interleaved, after the sky,
  the sun and the backdrop, then the billboards. The wet-road pass (§K) follows,
  then the cars.
- **Consequence.** A blended pass without depth writes (decals, ground overlays,
  the road sheen) never covers a car, even where its coarse mesh floats above the
  road: at Laguna Seca the dirt overlay of node 575 (texture set 11, image 5) lies
  10 to 20 cm above the road around x = -323, z = -292, across the lower body of a
  car seen from a trackside camera. Blended packets that do write depth hide a car
  behind them; the foliage's failing texels (AFAIL RGB_ONLY) write none.
- **Each car** is drawn after its shadows: node 1 (once per lamp on point-light
  courses, §I3), the headlamp pool (node 5), node 6, then the body and wheels,
  which cover them.
- **Level of detail.** The body, wheels and moving parts are drawn at one of the
  model's levels of detail. GT3 picks the level from the projected size of a 3 m
  sphere around the car; ours approximates it by the distance from the camera:
  full detail under 30 m, the second level under 80 m, the third beyond.
  `[video] full_car_detail = on` (the default except on phones and tablets) draws
  every car in full detail at any distance.
- Ours: `DrawItem::in_order` draws an item whole at its place (its opaque
  meshes, then its blended ones in model order) before later items' opaque
  meshes: the course scenery, the wet-road frame passes and each car shadow.
  Billboards and tire smoke stay in the depth-sorted blended pass.

---

## §J Lamps

### §J1 Course lamps

Street, stadium and tunnel lamps (`race::LampGlows`) are drawn when Env0 flag
0x200000 is set. Lamps come from the course's GTFX block (slot 29): type, position,
facing and blink phase. There are eight lamp types at Env0 +0xC60 (0x40 each):
RGB, intensity, glow size (x 10), halo size, halo gain, exponent n, attenuation
c / l / q, maximum distance, blink period P.

GTFX holds one lamp list per scenery node. Each frame the lists of the nodes
visible from the camera (the slot 2 visibility list of the view point nearest
the camera; every list without one) are visited in that order, and at most 100
lamps are kept: those in front of the camera, within the maximum distance, whose
halo-sized square is on screen. Per kept lamp:

1. **Directional factor.** D = max(0, facing . (camera → lamp))^n, n the
   exponent rounded; D = 1 for n = 0. Facing away drops the glow, not the halo.
2. **Intensity.** I = intensity / (c + l z + q z^2), times
   0.5 + 0.5 sin(2π (t / P + phase)) for blinking lamps.
3. **Visibility.** The fraction of a 10 x 10 pixel box (of the 448-line frame)
   at the lamp, 0.2 m nearer than it, that passes the depth test, doubled and
   capped at 1; the engine casts a 3 x 3 grid of rays against the scenery.
4. **Glow.** The disc's `race/flare.bin` (three untextured meshes: a 0.425 m disc
   and three thin rays 60 degrees apart, a six-point star) at the lamp pulled
   0.5 m towards the camera, scaled by the glow size, not depth-tested: vertex
   color x RGB x I x D (clamped), vertex alpha x visibility, added at
   Cs As / 128. With `[video] original_res = on` the stars are drawn at the
   448-line resolution and filtered up, so the thin rays break up as on the PS2.
5. **Halo.** Beyond 1 m, a depth-tested disc of radius halo at the pulled point,
   color RGB x I x gain x 0.5 (not D or the visibility).

Without `race/flare.bin` the glow is a procedural stand-in sprite of radius
glow x 1.17 m (ours). Decoded from the disc:
Seattle has 69 lamps (stadium lights), Route 5 has 298 and Route 11 has 363
(tunnel lamps). Trial Mountain and Deep Forest have none: their tunnel lamps are
painted into the scenery.

### §J2 Car lamps

- **Brake lamps.** Every car keeps a 4-frame history of "brake pedal > 0" (the
  player, player 2, the opponents), the pedal being the car's own in the race
  (in replays, the re-run race's). k = frames on (`BrakeLamps`).
  - Lamp materials add their emission x k / 4.
  - The model's lit-lens geometry is switched on while the pedal is pressed now.
  - The start hold's brake does not light them.
- **Car time of day.** Env0 flag 0x80000 loads `cars/eve/`, else 0x8000
  `cars/night/`, else `cars/day/`.
- **Lamp glows** (`race::CarLampGlows`, Env0 flag 0x8000: night and dusk courses)
  are drawn from the car's GTCI lamp lists:

  | List | When | Drawn as |
  |---|---|---|
  | headlamps (+0x40) | always | glow and halo, I = 1 |
  | brake lamps (+0x50) | while k > 0 | I = k / 4 for records with flag bit 0, else 1 while the pedal is down now |
  | tail lamps (+0x48) | always | halo only |

  Each list draws with the single lamp type at Env0 +0x9F0: color x intensity,
  glow (x 8), halo, gain, attenuation, maximum distance.
- **Per lamp record** (at most 100 a frame pass the screen test: the record's test
  point in front, within the maximum distance, a square of halo x its larger size
  on screen):
  1. **Directional factor.** D = (viewing axis . (camera → test point))^n; a
     negative product gives no glow (not for tail lamps).
  2. **Attenuation.** 1 / (c + l z + q z^2).
  3. **Pull.** The glow and halo sit at the lamp, moved along the view ray to
     depth z + 0.5 x the viewing axis' view-space z.
  4. **Glow.** The flare star of §J1, scaled by glow x the lamp's size, color lamp
     RGB x type color x attenuation x D x I, its visibility from the box test of
     §J1 at the test point. Bar lamps, whose two sizes differ, get no glow and an
     elliptical halo along their projected x axis instead.
  5. **Halo.** Depth-tested, gain x 0.5, not scaled by I or D.

  There are no reversing lamps and no headlight switch.

### §J3 Lamp streaks on wet roads

See §K.

---

## §K Wet road

`race::WetRoad`, on courses with a slot 9 model: Route 5, Route 5 dry, Route 11,
their reverse layouts and the wet circle tests. Test `race_wet_road_models_disc`.
In the main view only (in 2P, player 1's half; not player 2's half or the
mirror), after the scenery and before the cars:

1. **Mask.** The frame's alpha is cleared to 0, then slot 13 (alpha-only
   packets, depth-tested, no depth write) writes the reflectivity mask:
   texel x vertex alpha.
2. **Buffer.** A small buffer (Env0 +0xC24 x +0xC28) is cleared to black. It
   receives:
   - slot 9 (the scenery stored already mirrored below the road) with the main
     view and projection, and its own fog (Env0 +0xC40 block, laid out like +0x60);
   - with flag 0x10000, the cars mirrored about the road plane under each
     (body and wheels);
   - with flag 0x20000, the lamp streaks below.
3. **Blur.** Env0 +0xC2C passes copy the buffer onto itself, bilinear, shifted
   -0.5 / +0.5 texel alternately.
4. **Composite.** The buffer is stretched over the frame (bilinear, net -0.5
   texel) with Env0 +0xC20: 0x54 is (Cs - Cd) Ad + Cd, 0x58 is Cs Ad + Cd. The
   frame alpha is clamped to 1.

**Streaks**, per car, at each lamp's mirror image:

- Headlamps use I = 1; brake lamps use k / 4 or 1 as in §J2; tail lamps 0.3.
- c = cos(lamp axis, eye → lamp). A lamp with c < 0, or behind the eye, gets no
  streak.
- The streak is a camera-facing quad 0.5 m nearer the eye:
  - half-width (E74 + (E70 - E74) c) x lamp size;
  - from min(E78, lamp height) above the image down to E7C below it.
- Texture `race/reflection.img`; color lamp RGB x I x c x (E60 .. E68) x E6C;
  added, with no attenuation or occlusion.

Values decoded from the disc:

| Course | +0xC20 | Buffer | Blur | Buffer fog | Mirrored cars | Streak width head-on / side, cap, length, gain |
|---|---|---|---|---|---|---|
| Route 5 (wet) | 0x54 | 64 x 128 | 1 | none (b 1) | yes | 0.30 / 0.30, 1.0, 1.8, 1.0 |
| Route 5 dry | 0x58 | 64 x 128 | 6 | constant, visibility b 0.27 | no | 0.50 / 0.50, 1.0, 0.5, 0.3 |
| Route 11 | 0x58 | 64 x 128 | 1 | A 4.5, b 0.17 | no | 0.63 / 0.03, 0.6, 0.2, 0.07 |

---

## §L Smoke and other effects

### §L1 Tire smoke and dust

`race::TyreSmoke`: one pool of 256 puffs shared by every car.

**Emitting.** Each tick, every grounded wheel of every car whose smoke-emission
intensity (a physics output) is above 0.001 emits one puff at its contact point,
with no horizontal velocity. The surface index picks the class:

| Surface index | Class |
|---|---|
| 0, 1, 7 .. 11 | 0 tarmac |
| 2 | 1 grass |
| 3 | 2 gravel |
| 4 | 3 sand |
| 5 | 4 dirt |
| 6, 12+ | 5 wet spray |

A class whose opacity is below 0.01 emits nothing. The new puff takes a free slot,
or replaces the weakest (strength x weight). With a random k in 0 .. 7:

- strength s = min(1, intensity) x T1[k], with
  T1 = {0.9953, 0.0044, 0.1186, 0.5101, 0.0353, 0.7672, 0.2763, 0.9419};
- color = class RGB x (floor + (1 - floor) sun), where floor = Env0 +0x910 and
  sun = the road shade A under the wheel;
- size = class size x s;
- rise speed = rise x (1 + (r - 0.2) 0.3);
- weight = s + (1 - s) alphamix;
- fade multiplier min(255, 1 + r (n + 0.2)); growth multiplier 1 + (r - 0.5) 0.2.

**Each tick:**

- spin: rotation ± spin x T2[k] / min(max(size, 0.001), 1) per second (+ for
  right wheels, - for left), with
  T2 = {0.0066, 1.0, 0.7265, 0.4041, 0.9955, 0.177, 0.6997, 0.0529};
- size += growth x multiplier x dt;
- strength -= fade x multiplier x dt;
- rise speed -= 9.81 gravity dt;
- the puff dies when its strength reaches 0 or its top (y + 1.2071 size) sinks
  below the ground.

**Drawing.**

- **Shape and order.** Camera-facing quads of half-size `size` (turned about the
  vertical, up = world up), sorted back to front, normal alpha blend, depth test
  on, no depth write, no fog.
- **Alpha and texture.** Alpha = min(1, 1.99 x strength x weight x opacity) x
  texel. The texture is the course's slot 26 (four 32 x 64 frames, frame k & 3).
- **Cull.** A puff is drawn only while its center's view depth d is inside
  (near, far) and it is under two screen widths across (P0 x size < 2 d, P0 = the
  projection's x scale). A camera inside or next to a puff does not see it (test
  `race_tyre_smoke_cull`).
- **Ground split.** A puff up to 3 m that crosses the ground line (ground -
  0.05 m) is split along it. The part above is drawn as is; the part below fades
  to alpha 0 one puff size down.
- **Route 5.** Env0 +0x914 / +0x91C are buffer divisors (2 / 2 on Route 5; values
  ≤ 0 or > 64 count as 1). With divisors other than 1 the puffs are added to the
  frame instead, with no ground split, so the spray never darkens. The game's
  spray there is at half resolution; the engine adds it at full resolution.

The random numbers come from the engine's own generator, so individual puffs
differ from the game's.

Class parameters decoded from Env0 +0x4D0 (6 x 0x44) for two courses (opacity,
size, growth, rise, spin, fade, n):

| Class | Trial Mountain | Seattle |
|---|---|---|
| 0 tarmac | 0.25, 0.3, 2.4, 0.9, 114, 0.6, 0 | 0.50, 0.3, 2.4, 0.9, 114, 0.6, 0 |
| 1 grass | 0.50, 0.3, 1.2, 0.54, 48, 0.48, 30 | 0.35, 0.5, 2.0, 0.2, 30, 0.3, 60 |
| 2 gravel | 1.00, 0.1, 0.6, 0.06, 12, 0.6, 0 | 0.70, 0.6, 2.6, 0.15, 30, 0.1, 60 |
| 3 sand | 1.00, 0.2, 0.6, 0.06, 12, 0.6, 0 | 1.00, 0.6, 2.7, 0.15, 30, 0.1, 60 |
| 4 dirt | 1.00, 0.7, 3.0, 0.15, 30, 0.1, 32 | 0.40, 0.7, 3.0, 0.15, 30, 0.1, 32 |
| 5 wet | 0 (off) | 0 (off) |

Route 5's wet class has opacity 0.09 with gravity 0.51.

### §L2 Billboards

The course's sprites (slot 30, `BLBd`: trees, spectators; `race::Billboards`)
are each a quad of the sprite type's width x height, standing on its position.
The quads turn about the vertical to face the camera, and are re-faced only when
the camera's right axis changes. They are alpha-tested and unlit, with vertex
color x 2 (0x80 = 1.0).

### §L3 Pylons

Pylons and cones are drawn after their planar shadows. The shadow is the type's
node 1 projected along Env0 scene light 0 (+0xAD0) onto the road under the body,
with the car shadows' color and alpha. The body physics and the license rule
belong to the physics and license topics (§A2).

### §L4 Paint colors

At race start every car without a fixed color draws one (`race::assign_paints`).
The fixed colors are the player's choice and the event's color ids at +0x110,
0xFFFFFFFF = choose. Each unset car is handled in entry order, player first:

1. Its model's color list (`database/carcolor.db`) gives weights 0.984375^j,
   normalized.
2. Each candidate's weight is divided by 1 + the sum of its similarities to
   every other candidate (its own car's other colors included), then the
   weights are normalized. A car with a single candidate (a fixed color) weighs
   10000 sim^64 instead. Similarity is
   (√3 - |Δrgb|) / √3 x the cosine of the colors.
3. The candidates are sorted by weight, lowest first. Each in turn is taken with
   probability equal to its weight; if none is taken, the heaviest is.

The draw uses the race module's random state, which continues from race to race
and from the opponent draw. A replay stores the state the draw started from and
repeats it (test `race_paint_assignment`: the player's color is avoided, and
the draw is deterministic).

### §L5 Course animations

Courses animate parts of their scenery (`race::CourseScene`, `race::ClutAnimations`;
the formats in `docs/formats/course.md`):

- **Color animations** of the texture palettes: flashing lights (the camera
  flashes in Route 11's grandstands), signs and the start-signal lights. They play
  in the scenery, the slot 5 reflection scenery, the wet-road reflection (§K), the
  backdrop and the billboards (§L2).
- **Moving nodes** driven by the course's animators: flags, spinning parts, and
  flip-book crowds and screens.

Every run and every replay starts them again from their first frame
(`RaceScreen::reset_run_state`). The start-signal lights run on to their hold and
are set back by the start hold's length, so they reach it as the countdown ends
and play on through the signal. All animations advance 1/60 s with each race
step, so they stand still while the race is paused, and run in replays, the menu
backdrops and demonstrations alike.

---

## §M Race sounds: what triggers them

The engine sound and effect synthesis is the sound topic. This section lists when
the race asks for them.

### §M1 Effects

Named effects from `sound/se.inf` (`race::RaceSfx`):

| Trigger | Effect |
|---|---|
| countdown shows 3, 2, 1 | `count_d` |
| start signal (rolling start: the hand-over; flying start: the line crossing ending the run-up, §B2) | `count_s` |
| wall hit of a followed car, strength ≥ 0.2 | `clash` |
| car-car hit of a followed car, strength ≥ 0.2 | `clash2` |
| turbo blow-off | `b_off`, gain x blow-off x 32 / d (not capped) |
| pause, REPLAY OPTIONS, quick menus, Analyzer | menu SE "cursor", "ok", "cancel", "disable" |
| prize counter beep (§F3), on the frame of its step | "cursor" |

- **Collisions** (`CarSfx::update`, test `race_tyre_and_collision_sounds`):
  - wall strength = min(1, |j| / 50000), car-car = min(1, |j| / 7500), below
    0.2 ignored; the stronger of the two plays, centered;
  - volume = channel gain x strength x 0.6 x 32 / d, at most 1, with d the
    camera-to-car distance (at least 0.25 m), so in the cockpit every audible
    hit is full volume;
  - it is keyed again on every tick with such a hit; there is no cooldown;
  - only the followed cars' hits are heard (player 1 and player 2), and none in
    replays.
- **Tire loops** (`CarSfx`, `RaceScreen::update_tyre_sounds`), for every car:
  each wheel plays each loop as its own voice, placed at the wheel as seen from
  the camera (volume min(1, 32 / d)), with the followed cars' gains or the "other
  car" gains (§M3):
  - tarmac / paving (indices 0, 7, 8, 9): `on_skill` squeal and `on_road` roar;
  - curbs (1, 13): `rumble`;
  - grass, gravel, sand (2, 3, 4): `off_skill`;
  - dirt (5): `off_roadnoiz`;
  - wet (6, 12, 14): `rain_noiz`.

  The road roar keeps sounding for 6 steps after the wheel leaves the ground.
  The details are the sound topic.
- **Continuous car sounds** of the followed cars: the wind / road-noise unit, the
  in-car loop, the transmission whine (non-stock gearbox only) and the turbo
  spool.
- **Opponents.** The nearest five opponents get an engine note; the nearest two
  also get the wind unit. Volume is min(1, 32 / d) over the horizontal distance,
  with Doppler (c + v_listener) / (c + v_source), c = 331.45 m/s
  (`OpponentEngines`, test `race_audio_models`).
- **Reverb.** Each tick the reverb depth moves by at most 1/36 towards a target
  set by the surface flags under the car: 0.5 for (flags & 0x18) = 0x08 (tunnels,
  covered sections), 0.25 for 0x10 or 0x18, else 0.125 (`ReverbAmbience`, test
  `race_reverb_ambience`).
- **Course ambience.** The course's CRSS emitters (slot 35) play with one
  listener per screen, using every car's track distance.
- **Switches.** The race SE option gates all of this in live play, and the replay
  SE switch in replays, demonstrations and the attract demo. Everything is
  silenced on pause, on the quick menus and when the race is left (`silence`).

### §M2 Music cues

`RaceMusic::decide` (test `sound_race_music_cues`); the tracks are the music
topic:

| Race event | Cue |
|---|---|
| hold starts (also when a quick menu closes onto a held run, §B2; replays too), start type 1 (4 s) | the short start jingle |
| hold starts, start type 2 (10 s) | the long start jingle |
| start signal after a jingle | race music (license tests, or no song in the race playlist: the music stops) |
| rolling start's hand-over, flying start's line crossing (live runs and replays; silent before, §B2) | race music |
| race music ends | the next race song from the race playlist |
| any finish (2P: the first finish; a field replay: the followed car's finish, §G8) | the finish jingle; license passes have none |
| license FAIL, license TIME EXPIRED (slower than bronze), voided time trial | the fail jingle |
| license medal, 37 frames after the result (§A4) | the gold, silver or bronze jingle |
| pause / continue | music pauses / resumes |
| quick menu opens | short menu music (pre-race, time trial, license, machine test) or result music (race and 2P results), looping; closing it stops the music |
| attract demo | the demo track, once |
| license replay or demonstration ends | the finish jingle after a medal; the fail jingle without one (FAIL or TIME EXPIRED), as in the live run (test `sound_race_licence_jingles`) |

Race music needs the race BGM option. Replays, demonstrations and the attract
demo use the replay BGM switch instead (forced on in view mode 1). With either
switch off the cues are still requested, silently (the playlist advances and the
cut markers keep timing the replay cuts, §G14). The quick
menus' music follows the Menu BGM switch, not the race's. The race songs come
from the race playlist (`music.md` §2.3), which carries over from race to race.
Each request has its level (race songs lower than jingles, louder in replays);
the levels are the music topic (test `sound_music_request_levels`).

### §M3 Sound view

The camera chooses the gain column (`SoundView`, `sfx_gain`):

- in-car view: "in-car, target car";
- chase view: "chase, target car";
- TV camera or any replay: "other camera".

Opponents use the "other car" columns, and their engines the replay view in
replays. Engine notes are panned from the car's offset in the viewing camera's
frame; the own car is centered in the in-car view.

Gains per view and channel (`kGains` in `src/race/car_sfx.cpp`):

| View | engine | exhaust | turbo | blow-off | whine | clash | rumble | in-car | wind | road | rain | squeal | slide | dirt |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| in-car, target | 3.0 | 2.5 | 0.5 | 0.2 | 0.8 | 0.3 | 1.4 | 0.6 | 0 | 0.9 | 0.9 | 0.6 | 0.8 | 1.4 |
| in-car, other | 0.3 | 1.3 | 0 | 0 | 0 | 0.3 | 0.2 | 0 | 0.3 | 0.3 | 0.6 | 0.3 | 0.6 | 1.0 |
| chase, target | 1.0 | 3.0 | 0 | 0 | 0.3 | 0.5 | 1.0 | 0 | 1.4 | 0.6 | 0.9 | 0.6 | 0.6 | 1.0 |
| chase, other | 0.3 | 1.3 | 0 | 0 | 0 | 0 | 0.2 | 0 | 0.3 | 0.3 | 0.6 | 0.3 | 0.6 | 1.0 |
| other camera | 0.5 | 3.3 | 0 | 0 | 0.2 | 0.5 | 0.2 | 0 | 0.8 | 0.9 | 1.0 | 0.5 | 0.3 | 1.0 |

The table's thirteenth column is unused and is left out here.
