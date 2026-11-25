# GT Mode — specification

Behavioural facts about GT3's career mode (NTSC-U, PBPX-955.03 v1.10),
recovered from `CORE.GT3` and the disc data for the independent
reimplementation in `src/game/` (see [../dev/GT_MODE.md](../dev/GT_MODE.md)).
Confidence: **H** read from code, **M** inferred from code and data, **L**
guess. Addresses are program addresses (base 0x100000) for reference only.
Money is in Cr as stored on this disc; the Japanese release multiplies every
money value read from data (car and part prices, prizes, gcf constants) by
100 when the language index is 0 (H, `0x231680`, `0x21DCE0`; §21).

## Changelog

- 2026-11-12 (latest; public issue 305): §5 / §7 the purchase and wheel sounds. `buy_car` (0x125E68),
  `change_wheel` (0x126048) and `fit_wheel` (0x126160) each start a menu `Timer` (0x1B28F8: a
  counter of 30, counted down by `Timer::vf2` 0x1CCA88, firing when it drops below 0, so on the
  31st frame) that plays a menu effect by index into the effect-name table (0x2E9380: 0 `buy`,
  12 `wheel`) through the menu SE call (0x1C4F80).
  The NTSC-J program has the same counter; the PAL program, which steps at 50 Hz, 25 (the same
  time). Ours played neither; implemented (`SoundTimer`). The purchase also stops the dealer
  music: "Purchase?"'s YES is a go-to (item kind 0) to `00cmn/cd_bought`, a page built at level 0,
  whose name matches none of the page-music tests, so the page build (0x1B7268 → 0x1C4D88) asks for
  the rule's default, stop (music.md §5). Seen in PCSX2 (the music cut as "purchased" opens, silence
  over it and "Get in now?", the dealer music from its start on the next `cd/` page) and in RAM (page
  manager +0xAE4 level 0, +0xAF8 stored id 0x23). Ours kept the music on an unmatched page;
  corrected for the GT-mode set (`page_music`). §5 "Get in now?" is asked only with a ride car
  and opens at once with SE `ok` (ours asked whenever the garage held two cars, with the confirm
  feedback and SE `next`); §14 the part-change action's SE `ok` (ours played none). Corrected
  (`intercept`, `MenuController::confirm`, `GtModeHooks::take_event_sounds`).
- 2026-11-07: §20.4 the attract demo's / Preview's followed car (entry 0, no player car) without a
  pool paint is painted by the paint draw like the others. Ours kept it in paint 0 (its car was
  built before the draw); corrected (`RaceScreen::draw_player_paint_`, `build_player_visual`).
- 2026-11-07 (public issue 238): §8.1 / §20.4 a championship keeps its opponents' paints.
  Round 1 (`RaceChampionshipInformation::vf12` 0x18DF38 → 0x18E638, +0x290 clear) runs the pool
  draw and the paint draw (vf14 → vf13 0x179D70 → 0x176FD0), then 0x18E580 copies entries 1..n−1's
  whole 0x1E8-byte CarEquipments (colour id +0x08, index +0x0C) into the entrant records (+0x490,
  0x1F8 apart) and sets +0x290; later rounds copy them back (0x231B90) and the paint draw skips
  them (it fills only id 0xFFFFFFFF). "Save game" between rounds copies the same records
  (0x18EB58 → 0x2160C8). The pool entry's colour id is read by pool index (0x238550: +0x110 + 4 k),
  not by slot. Ours drew new paints for every round (and every build of one race: backdrop,
  practice, retry); corrected (`Championship::paints`, `RaceScreen::drawn_paints_`).
- 2026-11-04 (public issue 199): §11 Preview traced and checked in PCSX2: the title's demo
  race on the chosen event, endless (race info +0x7C), leaving on Start back to the entry page's
  PREVIEW. Ours drove the event's first car as a time trial; corrected. The title demo too ends
  on Start alone in the original; ours keeps any button there on purpose.
- 2026-11-04: §15 a demo's time limit is gcf section 0's second word (105 s, read through the race
  information into the countdown behind event 7), not the 1800 steps ours took for it. Seen in
  PCSX2: 6300 frames per demo, the next demo straight after. Only that limit counts as a finished
  demo; a race that ends by its laps returns to the title. Implemented
  (`GtData::demo_time_limit_seconds`, `GameRequest::time_limit_s`).
- 2026-10-29: the free practice before a race (QUALIFY). §8.7 new: GT3 runs it with the
  race's whole field (0x1330E0, 0x133A60; the conductor gets a car per entry), every car keeps
  its best lap (a later practice starting from its entry's best so far), and after it the grid
  is set by those laps when the player set one (GT race `fn_0012E920`, championship
  `fn_0018DFA8`: counts of strictly faster cars, ties in entry order) and the GT race list
  refilled with each car's best lap (`fn_0012F160`); a championship's with the points only.
  Ours ran the player alone, kept the grid and showed no times; corrected. Its start and notes
  (checked against a PCSX2 capture): no countdown (start type -1; ours held the event's 10 s),
  the P.P. label until the first fastest lap, Lap 0 through the warm-up lap, and the warm-up,
  START, start-qualifying and pole notes; corrected.
- 2026-10-29: the prize car page (public issue 68). §10.1 new: car_get's carousel and
  roulette traced (CarsItem 0x13CE58 / vf2 0x13D498, the roulette 0x13B850..0x13B960, the
  carousel 0x13BA80 / 0x13BCE0, the camera 0x13C3A0, the draws 0x13D938 / 0x13DF30): the pick
  is the roulette's target, it stops only on it, the car added is the one it stopped on, and
  the page keeps the controls until then (0x1B7BD0); the cars are black silhouettes on the
  carousel (colour scale 0), the others fade out as the background is put back over them at
  1 − +0x1034, the car won lights up alone at +0x1038 (checked against a PCSX2 capture of the
  Japan Championship). Ours had a 2.5 s flash of the candidates in paint 0 with OK live from the
  first frame, so leaving early showed a car not given.
  §20.1 the car_get model turn is the carousel's headings (n + 1 shared draws).
- 2026-10-29: §8.6 new: the starting grid of GT races and championship rounds
  (`0x12E670`, `0x18E190`): whole kg over the whole power shown in the language's unit,
  truncated; lowest on pole, equal figures in entry order, the player last. Ours had compared
  exact kg-per-PS floats; corrected.
- 2026-10-28: results and marks (public issue 45). §8.4 a race counts only once
  started (the pre-race menu's START, 0x132E98); a championship round left by pause EXIT
  scores the others by their places and the player nothing (0x18EAB8); retiring after a round
  keeps it; the championship driver's end traced. §8.5 new: the series pages' per-button marks
  and logos (event_status_buttonNN) and the league pages' result rows (event_status). §10 the
  endings are checked with every prize-car check, both can play (0x13F508); a league key only
  with a code of its league (0x214868 / 0x214048). Ours corrected (all of these).
- 2026-10-26: money from the data. §2 the tag-10 lookup has no fallback (a missing
  entry is read through a null pointer); the regional values. §3 the 200-car / 200-wheel
  limits' addresses (the wheel slots include the stock-wheel placeholder). §4 the completion
  totals: only the GT part is counted from the gcf, the arcade, time-trial and licence totals
  (102, 10, 48) and the 0.99 cap are program constants; the arcade done count runs over the
  SINGLE_RACE list (whose size is the arcade results' stride). §5 the sale is a signed /4 of
  the scaled price; the dealer prices show "----" for a car not for sale; the garage list's
  price column is the sale price ("-" for flag bit 1). §8.4 the points table. §10 the
  achieve_* thresholds. §8.2 the race information page prints a 0 prize as "0". Ours
  corrected (sale column, "----", 0 prizes, the wheel slots) and reads every sum from the
  disc (no NTSC-U fallbacks). presentation.md §F: the money templates are program strings,
  only the prefix is per language.
- 2026-10-25: §21 the language rules (the "region index" is the game's language
  index; 2 is British English): prize-car label table, Opel / Vauxhall dealer buttons, the
  licence car swap and S-1's `_uk` demonstration, car and paint names by language. §16 / §20.4
  region byte corrected (1 / 2 British English, 5 / 6 the five European languages).
  Implemented (`game/language_rules.*`, `CarDatabase` names, `GtData` paint names).
- 2026-10-21: §17 the rankings are entered only by the drivers after each run; ours no
  longer enters a posted result's last lap from the menus (licence, machine test, time trial,
  free run). §16 the arcade time trial is one lap (presentation.md §B3).
- 2026-10-21 (later): §17 course time trials (the free-run driver for the arcade time
  attack and GT Mode's free run: the TIME_ATTACK course index, the best lap after every run
  with its name entry, the car, the quick menu's car list, the records page, the free-run
  result overlay and the HUD's Record Lap line, which reads the visit's best lap, not the
  table); §12 the 32-byte blocks (the arcade +0x120 field is one, not a car name), the
  machine-test speeds. Implemented, with the PS2 import / export of the arcade, course and
  machine-test records.
- 2026-10-21: §17 the name entry runs after every ranking run inside the machine-test and
  time-trial drivers too; the TextBox (cursor, insert, Back Space at the cursor, L1 / R1,
  Start to OK, 15 characters, the pad's logical keys); the records page (every row, dates
  from the stamps, the car and its icon on machine-test pages); §3 +0x70 = the last name
  entered; §4 a machine-test visit costs one day; §12 the 0x300 record layout corrected (car
  codes +0xF0, stamps +0x150, the 32-byte block +0x180); §15 the Top 3's own formats
  (speed "%4.1f %s", three rows always, the free time trial's car names). Implemented
  (ours had one record per visit, an appending name box, "day N" dates).
- 2026-10-20: §7 the car wash spins the car (`TumbleCar`: 31 frames, SE `car_wash`, 181
  frames of yaw pushes of 10,000 into the showroom camera, the render's dirt kept until the
  end); the progress popups' `ignore_control` (no input, no "Complete" button until the wash /
  oil change gives control back; the cause of the oil change ending before its refill in
  ours). Implemented (`MenuController::ignoring_control`, `GtModeHooks::on_tick`,
  `game::car_wash_spin`).
- 2026-10-20: §7 the oil page's gauge (`oil_change_cylinder`): a full cylinder whose colour
  blends the page's hidden `clean` / `dirty` colourings by the oil's age, not a bar of the
  oil's age; the oil change's drain / refill animation and its two `oil` effects; the
  condition constants' source bytes. Implemented (`GtMode::oil_age`, widget_art.cpp).
- 2026-10-20: §12 options block: the replay BGM / SE switches are at +0x54 / +0x58 (not
  +0x14 / +0x18); the music list at +0x228 is the race playlist (cursor, shuffle, seed,
  64 entries {id, played, in the list, order}; music.md §2.1); a new game's list is the
  shuffle of a fresh shared state's first draw (seed 0x456, as the original's).

- 2026-10-20: §10 the whole-game set `gtmode_complete` (one car, `mclaren_mp4_4`, random paint):
  when the car_get check runs and with which code, the GT-mode code classes, all 364 tag-4
  results won; not the 100 % completion rule. §7 the engine condition is carried through a run
  (seeded before, copied back after, odometer delta into the total distance) only by the
  arcade race, free run, machine test, GT race and championship drivers (physics.md §10.3).
- 2026-10-20: §14 a slider page opens on its first slider; OK is kind 3 (back) and so also
  runs the page's back action after the commit. §8.1 the stored race code (rules in menus.md
  §3).

## 1. Overview

- There is **no used-car dealer** in GT3: the menus have only new-car
  dealers (`cd/` pages) (H, data). Used cars arrive only by trade (another
  memory card's garage) and prizes.
- Sources of truth: `database/paramdb_us.db` (cars, parts, events,
  regulations; [../formats/database.md](../formats/database.md)),
  `gameconf/product.gcf` (constants, race-code list, prize cars, §2),
  `menu/gt_mode.mbl` (which cars each dealer sells, which races each series
  has, which tests each license has: the menu *is* the catalogue),
  `database/carcolor.db` (paint colours).

## 2. product.gcf (configuration tree) (H)

Layout: u32 section count, u32 offset of the section table (8); section
table = count × {u32 tag, u32 absolute offset}. A section = u32 entry count,
u32 offset of its offset table (8), u32 entry offsets (relative to the
section). An entry starts with u32 = offset of its NUL-terminated name
(relative to the entry), followed by tag-specific fields. Names are sorted
and looked up by binary search.

| Tag | Content |
|---|---|
| 10 | constants {name, u32 value at +4}: `car_wash` 50, **`initial` 18,000** (new-game credits), `oil_change` 250, `wheel` 500 |
| 4 | the 364 GT-mode result slots (every race code plus every championship code `…_0500/_0700/_1000`), u32 at +4 = unlock level (1 = needs the S license; only the `Gf1_h_*` codes) |
| 6 | 44 course names (per-course records) |
| 13 | 59 license tests + arcade time-trial codes |
| 15 | 101 prize keys: u32 +4 candidate count, u32 +8 offset of the candidate offset list; candidate = {s32 prize id, u32 name offset, s32 colour id (−1 default), s32 weight} (§8) |
| 14, 8, 12 | arcade classes, arcade modes, demo races |

The new-game balance is therefore data: `fn 0x218CF0` sets credits := gcf
`initial` (× 100 on JP). (H)

Tag-10 values are read by name through `0x21DCE0` (binary search `0x21DFC8`, ×100 when the
language test `0x231680` finds 0): new game `initial` (`0x218CF0`), `wheel` (shop guard
`0x11C0C8`, purchase `0x2196A8` / `0x2196F8`, price items `0x120E40`), `car_wash`
(`0x11C558`, `0x120E00`, the wash spin `0x126A80`) and `oil_change` (`0x11C4F8`,
`0x120DC0`, `0x126B98`). A name the section lacks returns no entry and `0x21DCB0` reads its
value through a null pointer: there is no default (ours refuses such a game config at load).
Values (H, data): NTSC-U `product.gcf` and PAL `product_eu.gcf` 18,000 / 50 / 250 / 500; NTSC-J
`initial` 15,000 (the others the same), shown ×100.

## 3. Game state (GameStatus, 0x35210 bytes; H unless noted)

Scalar block (saved raw, 0x130 bytes; offsets in the save payload):

| Off | Type | Meaning | New game |
|---|---|---|---|
| 0x00 | u32 | day | 1 |
| 0x04 | u32 | races run | 0 |
| 0x08 | u32 | a counter with only an add accessor (no caller found, L) | 0 |
| 0x0C | u32 | wins | 0 |
| 0x10 | s64 | credits | gcf `initial` |
| 0x18 | s64 | total prize money | 0 |
| 0x20 | u32 | total distance, 2 m units | 0 |
| 0x24 | u32 | car serial counter (acquisition order) | 0 |
| 0x28 | s32 | ride car = index in the garage list, −1 none | −1 |
| 0x2C | s32 | garage index of the car last handed to the race setup (`0x219840`; −1 = none or not a garage car; kept in step when a car is sold) (M) | −1 |
| 0x30 / 0x34 | u32 | car count / wheel-inventory count | 0 / 1 |
| 0x3C | u32 | arcade course level 0..6 (§16) | 0 |
| 0x40..0x6C | u32[12] | arcade car unlock flags, levels 1..12 (§16) | 0 |
| 0x70 | char[16] | the last name entered (the name entry's starting text, §17; GameStatus +0x78, C `0x128470`) | "" |
| 0xB0 | u32 | trophies: +1 for every GT cup page (places 1-3 with prize money), championship trophy and arcade Single Race win | 0 |
| 0xB4 / 0xB8 | u32 | short / long ending movie seen | 0 |
| 0xBC | u32 | prize cars received | 0 |
| 0xC0 | u32 | prize models received: 1 << license (B, A, IB, IA, S, R) when its card page is shown (`0x13A458`), 0x40 << i when trophy `champ_0i` is (`0x13A8D0`) | 0 |
| 0xC4 | u32 | award bits, set by the prize pages: 1 << ((cup class − 5) × 3 + place) a GT cup (class 5..8, places 0..2; `0x137DC0`), 0x1000 / 0x2000 / 0x4000 / 0x8000 an arcade race won at Easy / Normal / Hard / Professional, 0x10000 a consolation prize, 0x20000 / 0x40000 / 0x80000 a gold / silver / bronze license medal (with its jingle). No reader found | 0 |
| 0xC8 | s32 | language index (0 JP, 1 US, 2 BR, …; §21), restored on load | language |
| 0xCC | u32 | GT-mode random state (trophy and prize-car rolls, §20.2) | – |

Other fields of the block (+0x38, +0x80..+0xAF, +0xD0..+0x12C; all 0 on a new game,
`0x218CF0`) are not understood (L). The trophy count (+0xB0) is shared with arcade mode:
every arcade Single Race win adds one. Prize cars do not count as trophies.

- **Garage**: up to **200 cars** (`00cmn/max_car`; `slti 0xC8` in the dealer and trade
  guards `0x11BCAC` / `0x11BD78`, the car_get page `0x13F128`, the slot search `0x218FAC`;
  the same in PAL and NTSC-J). The list is ordered
  newest first (a new car is inserted at the head; the ride index shifts).
- **Wheel inventory**: 200 slots of 0x50 bytes (the slot search `0x219560`, `0x21959C`);
  entry 0 of a new game is a placeholder for the stock wheel (M), so 199 bought wheels fit.
  With no free slot the purchase `0x2196A8` adds nothing and charges nothing (the shop's guard
  does not test it; the disc has 179 wheels).
- **Licenses**: 6 licenses (B, A, IB, IA, S, R = rally) × 8 tests; per test
  the best medal (−1 none, 0 gold, 1 silver, 2 bronze; kept as the minimum)
  and a 6-entry best-time table.
- **Race results**: one s32 per gcf tag-4 code = best finishing place
  (0 = won), 8 = never raced.
- **Prize flags**: one byte per gcf tag-15 key: prize already given.
- **Suspended championship**: active flag, current race code, entrants
  (8 × {car equipments, points}; the whole 0x1E8-byte CarEquipments, paint included,
  H: `0x2160C8`); a championship can be saved between rounds and resumes at boot. An
  unused entrant has the CarEquipments constructor's unset paint (+0x08 id 0xFFFFFFFF,
  +0x0C index −1, `0x231BC0`).
- Records (arcade, time trial, machine test rankings) and options follow.

### 3.1 Garage car (0x230 bytes; save record 0x204)

| Off (record) | Meaning |
|---|---|
| 0x000 u64 | car id (table 30 code) |
| 0x008 u32 | paint: carcolor.db colour id (C, `gt_save_garage_car`) |
| 0x00C s32 | paint: the id's place in the model's carcolor list |
| 0x010 u16 | car category copy (car +0xF1: 0 production, 1 race, 2 rally) |
| 0x018..0x0E7 | 26 fitted part ids; slot → table: 0 brake, 1 brake ctrl, 3 chassis, 6 engine, 13 drivetrain, 17 gear, 18 suspension, 21 LSD, 28 front tire, 29 rear tire, 2 steer, 4 lightweight, 5 body, 7 port polish, 8 balance, 9 displacement, 10 computer, 11 NA tune, 12 turbo, 14 flywheel, 15 clutch, 16 shaft, 20 muffler, 19 intercooler, 23 ASCC, 22 TCSC |
| 0x0E8 u64 | fitted wheel (hash of the wheel label; 0 = stock) |
| 0x0F0..0x1CF | user settings, the CarEquipments words of [physics.md §6.6](physics.md); the gear words hold the record's u16 ("no gear" −999 = 0xFC19), and after a computed gearbox's own gears whatever the work area held |
| 0x1D0 | 87-bit **owned-parts** set (bit = goods id, §6) |
| 0x1DC u32 | odometer, **2 m units** (display ÷ 50 = km with one decimal) |
| 0x1E0 f32 | engine wear E (m) |
| 0x1E4 f32 | oil O (m) |
| 0x1E8 f32 | dirt 0..0.8 |
| 0x1FC / 0x200 | in use / acquisition serial |

The paint words are set together: `0x235F88` from a place in the list (the dealer), `0x235F30` from an
id (prizes, event entries). The menus use the place, a race the id. The race's colour draw (`0x176C28`,
from `RaceInformation::vf13`) takes every entry with an id other than 0xFFFFFFFF as it is and reads its
definition (`0x176900`, the lookup `0x2391A0`) without checking that there is one. No colour has the id
0, so a record with 0 there reads the address 0xC: a fault on the console, nothing on an emulator that
lets the read through (issue 308). The car's paint index in the race (`0x177430`) is then 0.

A new car owns the goods of its stock parts (0x237DB8, 0x237E18: one getter per
category, 0x234E00–0x2353D0). A getter takes the part's stage from the record's
+0x10 through a stage→goods table and returns the stage-0 goods when the slot
holds no part, so a car owns Normal in its empty slots. Goods 0x58 = none: racing
modification without a record (0x235000), the intercooler without a stock turbo
(0x235378) and the driveshaft of drivetrain +0x14 = 1, 3, 4 (0x235300). Steering,
chassis, engine and wheel have no goods. Each front-tire goods also owns the
matching rear goods (0x237A08); rally cars (category 2) also own dirt tires
(goods 66 and 77).

## 4. Days and counters (H)

- The day counter goes up by 1 when `go_race`, `go_license`,
  `go_machine_test` or `go_free_run` starts (a whole championship is one
  day; preview costs nothing). Machine tests (C): the `go_machine_test`
  action (`0x126308`) adds the day; the test's driver runs any number of
  runs without another, so a visit costs one day.
- races +1 per race (each championship round), wins +1 per 1st place,
  prize money += every prize.
- Status page: completion = (GT races won + arcade wins + arcade time
  trials cleared + license tests passed) / (364 + 102 + 10 + 48); exactly
  equal → 100 %, otherwise at most 99 %. (H, `0x219918`, four done / total pairs:) GT: tag-4
  slots at 0 (`0x216F80`) over the tag-4 entry count (`0x216FE0`, the only total read from
  data); arcade: per course of the gcf `SINGLE_RACE` list, won at E/N/H + won at N/H + won at
  H (`0x216980`; the results are records difficulty × list count + course, `0x216640`) over
  the immediate **102** (`0x216AE8`, not 3 × the list); time trials: the 10 records (0x2A4
  bytes each) whose best rank is 0 (`0x213D48`) over the immediate **10** (`0x213D80`);
  licences: 6 × 8 results ≠ −1 (`0x215B00`) over the immediate **48** (`0x215BA0`). Done =
  total → 1.0 (`0x2199CC`), else done / total (`div.s`) capped at **0.99** (`0x350924`). PAL
  and NTSC-J: the same constants (PAL `0x217A30` / `0x214CC4` / `0x216AEC`, cap `0x3522BC`;
  NTSC-J `0x213DE8` / `0x21108C` / `0x212EAC`, cap `0x34AB48`). Shown (C, `0x11D460`) as "100.0 %"
  when the ratio is 1, else per mille = trunc(ratio × 1000) (PS2 multiply),
  at most 999, as "%.1t %%" ("2.0 %" for 11 / 524 = 2.0992 %: truncated, not
  rounded). The save thumbnail holds ratio × 100 (f32, `0x21A46C`); the file
  lists show trunc(per cent × 10) as "%d.%01d%%" ("2.0%", `0x14BB88`).

## 5. Dealers and the garage (H)

- Dealer lists = the `set_car_code` items of the `cd/` pages: 8 regions
  (jp, us, gr, fr, it, uk, bel, aus) → makers → cars. 151 entries; the item
  name is the car label (a leading `_` is part of labels such as `_206_s16_00`).
  The region pages leave out the `opel` (British English) or `vauxhall` (other languages)
  dealer button (§21).
- Price = car record u32 +0xEC. Car flags = car record byte +0xF0: bit 0
  not for sale (shown, refused with `00cmn/not_for_buy`: 27 dealer entries,
  race/rally/concept cars), bit 1 cannot be sold or traded, bit 2 wheels
  cannot be changed. 124 dealer cars are buyable; 11 cost ≤ 18,000 Cr
  (cheapest 11,140).
- Buy checks in order: not for sale → no money → garage full (200).
  The same model can be bought any number of times. The paint is the index
  chosen in the colour box (default 0).
- The first car becomes the ride car automatically; later purchases ask
  `00cmn/cd_ride_sure`. Exactly (H): OK on "purchased" runs the check `0x11C5B8`: with a ride car
  (`0x218F40`: state +0x30 ≥ 0) SE 7 `ok` and the popup `00cmn/cd_ride_sure`, opened at once (the
  page manager's check path, `0x1B6D58`, skips the confirm feedback `0x1B6970`); with none, the new
  car becomes the ride car (`0x218F08`) and the item's go-to `cd/cd` runs. So a purchase with no
  ride car (after selling it) asks nothing.
- The purchase's sound (H): `buy_car` (0x125E68) starts a menu `Timer` (0x1B28F8, counter 30)
  that plays effect 0 `buy` on the 31st frame after the action, whatever the purchase did. The
  music: "Purchase?"'s YES goes to the page `00cmn/cd_bought` (item kind 0), which stops the
  BGM (no page-music test matches it, music.md §5); "Get in now?" opens over it as a popup (no
  music change), and the next dealer page (`cd/cd`) starts `dealer01` from its beginning.
- **Selling pays the catalogue price / 4** (integer), regardless of
  mileage, parts or wheels; only flag bit 1 refuses (`not_for_sell`). The
  ride car and the last car can be sold (the ride index becomes −1). (H) The price is the
  scaled one (`0x237BC0`: +0xEC, ×100 in Japanese), divided as a signed 32-bit value (`+3`
  when negative, `sra 2`): the sale `0x219368` (`0x2193B0`), the garage car page's
  `car_sell_price` `0x11CD28` ("----" for flag bit 1) and the garage list's price column
  (below). PAL `0x21A3D0`, NTSC-J `0x2166A0`: the same.
- Prices on the dealer pages (H): `goods_car_price` (`0x11D2F8`) "Cr. %s" (the string at
  `0x351178`) and the car page's `car_buy_price` (`0x11CC98`) the formatted price, both
  "----" when flag bit 0 (not for sale) is set.
- Garage list columns (`GarageButton::vf3` `0x1BE1F0`, kinds set by the header elements in
  `GarageButtonList` `0x1BD720`): 0 power "%d%s", 1 price = **the sale price** (price / 4,
  formatted, no "Cr."; "-" for flag bit 1), 2 maker, 3 distance "%.1t%s", 4 drive, 5
  aspiration. (Ours showed the catalogue price with " Cr.".)
- Trade (`go_trade`, `buy_guest_car`): a car from another save's garage at
  its full catalogue price, parts and condition copied, wheel reset.
- Garage sort keys: date, name, maker, colour, price, distance, power,
  drive, aspiration.

### Power figures (C: diff harness `gt_display_power`)

- **Computed power** (garage `car_max_power`, `spec_max_power_real`, the
  settings page's `car_current_power`, the shop's before/after, dealer
  `car_max_power` for the stock car): the menu car is built (CarEquipments
  copy, showroom build `0x1D93C0`) with the garage car's condition words
  (garage car +0x204 in memory: odometer, E, O; a dealer car: zeros), and
  `0x1E14E8` searches the full-throttle curve × the condition factor
  (physics.md §10.3) every 100 rpm from the first curve point (rounded to
  100 rpm) up to max(rev limit, last point), only rpm ≤ the rev limit
  counting: PS = (T / 9.8) · rpm / 716.2, the first strictly greater value
  wins; the rpm is kept. The text (`0x1D9680`, computed branch) converts
  the unit and **truncates**: NTSC-U shows HP = PS × 0.98619 (unit word 1;
  2 BHP and 3 CV use the same factor, 4 kW × 0.7355, 0 PS as is).
  Formats: `car_max_power` / `spec_max_power_real` "%d%s/%drpm"
  ("325HP/6800rpm"), `car_current_power` "%d %s", shop "%d%s".
- **Catalogue power** (`spec_max_power`): car record u16 +0x3A, converted
  and truncated, "%d%s/%s" with the catalogue rpm text.

## 6. Tune shop (H)

Parts are bought and fitted on the **ride car** only. A goods id 0..86
names a parts table and stage:

| Goods | Table | Goods | Table |
|---|---|---|---|
| 0–1 | 0 brake | 36–39 | 20 muffler |
| 2–3 | 1 brake controller | 40–43 | 11 NA tune |
| 4–7 | 15 clutch | 44–45 | 7 port polish |
| 8–9 | 10 computer | 46–47 | 16 propeller shaft |
| 10–12 | 9 displacement | 48–49 | 5 racing modify |
| 13–14 | 8 engine balance | 50–53 | 18 suspension |
| 15–18 | 14 flywheel | 54–55 | 22 TCS |
| 19–22 | 17 transmission | 56–66 | 28 front tires (stages 0–9, then 12 = dirt) |
| 23–25 | 19 intercooler | 67–77 | 29 rear tires (same) |
| 26–29 | 4 weight reduction | 78–82 | 12 turbo (stages 0–4) |
| 30–35 | 21 LSD (5 = AYC) | 83–84 | 23 ASCC |
| | | 85–86 | 13 drivetrain (1 = VCD) |

Stage = goods − first goods of the range. Menu items (ts/ pages) → goods:
`bkt_racing` 1, `bbl_on` 3, `sus_sports/semi_racing/racing` 51–53,
`muf_sports/semi_racing/racing` 37–39, `nat_1..3` 41–43, `ptp_on` 45,
`egb_on` 14, `dsp_on` 11, `cpu_on` 9, `clt_high/twin/triple` 5–7,
`fly_light/cr_mo/light_cr_mo` 16–18, `prp_on` 47,
`tms_close/super_close/variable` 20–22, `lsd_1` 31, `lsd_2` 32, `lsd_15` 33,
`lsd_variable` 34, `ayc_on` 35, `vcd_on` 86, `wtr_1..3` 27–29, `tbk_1..4`
79–82, `icl_sports/racing_big` 24–25, `tir_sports` 57,
`tir_super_hard … tir_super_soft` 58–64, `tir_control` 65, `tir_dirt` 66.
`tcs_on` 55 and `sms_on` 84 exist in the code but not in the US menus;
racing modification has no shop item.

- **Availability and price**: the part record of that table whose +0x08 is
  the car and +0x10 the stage; none → `ts/not_exist` ("----"). Price = the
  record's last u32 (tires +0x14; a tire set costs the front record, rear
  records cost 0).
- **Owning and fitting are separate**: buying sets the goods' owned bit and
  pays; `already_you_have` if that exact bit is set (owning a higher stage
  does not block a lower one; there is no refund). Buying a front tire also
  owns the rear one.
- **Weight reduction is bought in order** (the buttons' enable hooks, menus.md §5.3: table
  `0x2959E8`, page → one hook per button): on `ts/ts_ot` the second button (stage 2) needs goods
  27 owned (`0x119130`) and the third (stage 3) goods 28 (`0x119170`); without it the button is
  drawn at half brightness and OK plays SE `disable`. No other part has an order. PAL (`0x119218`,
  `0x119258`) and NTSC-J (`0x118FA8`, `0x118FE8`) run the same tests. The table's other page is
  `r/pro/pro` (the series' licence, `0x1190E0`).
- Flows: buy then "fit now?" for most parts; buy and fit at once for port
  polish, engine balance, displacement and weight reduction; dirt tires are
  bought only (never fitted by the shop). The questions are popups that
  replace each other (menus.md §3), so every answer ends on the part's page.
  `fit_parts` / `buy_fit_parts` keep the ride car's computed power before
  fitting; the engine parts' "Installed successfully" pages show it and the
  power now as "%d %s" (classes `car_previous_torque` /
  `car_current_torque`: power, despite the names).
- **Fitting** replaces the table's slot; stage-0 goods without a record =
  the car's stock part (back to stock). **Turbo and NA tune exclude each
  other**: fitting one puts the stock part back in the other slot. The
  fit itself (0x235CF8) writes one slot; the shop's fit (0x126730) then
  fits the matching rear goods for a front tire 56–65 (67–76), while the
  part-change action (0x167038) fits only the chosen end. Fitting a brake controller, body,
  drivetrain, gear, suspension, LSD, TCS or ASCC part re-derives that
  part's settings (M: they return to the new part's defaults).
- `buy_all_tires`: every racing compound (58–64) not owned, at the sum of
  their prices (`already_you_have_all_tires` when 0).
- The shop shows PS before/after: the ride car copied, the focused goods
  fitted, power recomputed.

## 7. Wheels, car wash, oil, condition (H)

- 179 wheels in 9 brand pages (`at/ws/ws_<brand>`, item `<label>_link`,
  label = table 24); **every wheel costs 500 Cr** (gcf `wheel`). Bought
  wheels go to the inventory (no duplicates: `00cmn/already_you_have`) and
  are never used up: one wheel can go on any number of cars. Cars with flag
  bit 2 refuse (`not_for_change_wheel`).
- The shop's "fit it?" after a purchase (`00cmn/ws_fit_sure`, action `fit_wheel` `0x126160`;
  PAL `0x126268`, NTSC-J `0x1259E0`, the same code): the ride car's wheel id (car +0xF8) := the
  hash of the focused wheel's name, the one the purchase was asked for, with no inventory, flag
  or money test. After a purchase refused for a full inventory (§3; unreachable with the disc's
  179 wheels) the car so wears, for free, a wheel the inventory does not hold; the garage's list
  then marks none of its buttons as the car's, and once changed the wheel cannot be put back.
  Like `change_wheel` (`0x126048`), it starts a menu `Timer` (counter 30) that plays effect 12
  `wheel` on the 31st frame (§5's purchase sound). `buy_wheel` (`0x126120`) plays none.
  Ours: `GtMode::fit_shop_wheel`; harness `gt_wheel_inventory` (the purchase and the fit on the
  game state, 0 mismatches).
- The garage's wheel list (`hm/hm_wheel`, `menu_wheel`: WheelButtonList `0x1C0D18`): one button
  per inventory entry in order, the stock-wheel placeholder first, drawn with the page's
  `default_wheel` tile (`0x1C10B8`; a bought wheel with its own tile). It starts on, and draws the
  page's `mark` over, the entry that is the car's wheel (`0x1C11C0`: the placeholder for a car on
  its stock wheels, car +0xF8 = 0). Focusing the placeholder previews the car's own wheel, named
  after its body model (`0x23B040`: the body part's +0x18 model id, e.g. `ni0137`; the disc has a
  `wheel/` model of that name for every car model but the test car `pd0006`); `change_wheel`
  (`0x126048`) on it sets the car's wheel to 0, back to stock. The stock wheels are not table 24
  entries: no car record names a wheel (+0xD8 is 0 on every disc) and no shop wheel is a car
  model's.
- Car wash 50 Cr: dirt := 0 (dirt is visual only, M). Oil change 250 Cr.
- **Engine condition** multiplies the torque curve:
  - break-in/wear E (m): a new car starts at E = −300,000 → factor
    1 − 0.03·E/(−300,000), i.e. **97 % new**, 100 % after 300 km; from
    E = 500,000 linear down to 98 % at 600,000 (E capped there): 98 % from
    900 km on;
  - oil O (m): × 1.05 while O < −100,000, then 1 + 0.05·O/(−100,000) up to
    O = 0, × 1 for O ≥ 0. An oil change sets O = −300,000 (+5 % for 200 km,
    fading out by 300 km). When O crosses 0 it jumps to 300,000 (worn; the
    race HUD warns at ≥ 300,000). New cars start at O = 0 (no bonus).
  - Distance per frame d = speed × dt is added to the odometer (in 2 m
    units with carry), E and O, **by the race car itself during the run**
    (physics.md §10.3); the factor is applied every tick from the current
    E and O.
- **Which runs carry the condition** (C; physics.md §10.3 has the
  addresses): the drivers of the arcade race (`0x12F840`), free run
  (`0x12FF48`, arcade and GT), machine test (`0x131690`), GT race
  (`0x132BA0`) and championship (`0x133388`) copy the car's eight condition
  words (garage car +0x204, in memory) into the race entry before **every**
  run (`0x12EE78`), copy them back after every run (`0x12EED8`) and add
  each run's odometer delta (2 m units) to the total distance (in-memory
  GameStatus +0x28 = save +0x20) when the driver ends. The car is the race
  setup's car pointer (+0x540 of GameStatus +0x307C8): a garage car itself
  when one was chosen (`0x219840`; the arcade car pick `0x114610` uses it
  for garage cars), else the setup's own CarEquipments buffer (+0xD8,
  built from the car code at each arcade car pick, `0x219800` →
  `0x217F20`), so an arcade car runs with the buffer's words (whether a car
  pick clears them is not traced, L: zero; they stay below 500 km, factor 1,
  unless the buffer keeps them over many runs) and only the total distance
  matters. Ours: an arcade car starts every run with zeros. The time
  trial (`0x130A50`), licence (`0x132030`) and 2 Player Battle
  (`0x133F48`) drivers never seed: their entries keep the zeros the entry
  constructor writes (factor 1), nothing is copied back and no distance is
  added, also for garage cars.
- The race HUD reads the race car's live words: the odometer (word 0 ÷ 50,
  ÷ 80 for miles, then ÷ 10) and the oil lamp (O ≥ 300,000, `0x1EB340` →
  `0x1D9E20`).
- The constants come from bytes at `0x2D2950` (× 10,000 m, built once by
  `0x1DAB50` into `0x2D2B00`): 30, 30, 50, 10, 20, 20, 50, 10, 30. E new =
  −b0 (−300,000); the oil life L = (b5 + b7) = 300,000 (`0x1DACC8`); the
  oil change sets O = −L (`0x1D9F80`); the +5 % edge −b7 = −100,000; the lamp
  b8 = 300,000 (`0x1DACF0`). Per-frame O step (`0x1D9D78`): O += d; if O was
  < 0 and is now ≥ 0, O = L; then O = min(O, L).
- **Oil page gauge** (H; page `at/ot/ot`, widget `oil_change_cylinder` at
  56, 68, 32 × 256; `0x123048` builds `OilChangeCylinder`, `0x116E00`):
  f = `0x1D9E68`(garage car +0x204) = min(1, (O < 0 ? O + L : O) / L) (1 if
  L = 0; no lower clamp): 0 right after a change and for a new car (O = 0),
  0.5 after 150 km, 1 from 300 km. f is read once, when the page is built.
  The gauge is **always a full cylinder** (level +0x114 = 1.0): its draw
  (`vf3`, `0x117170`) sets the drawn item's four corner colours to
  clean × (1 − f) + dirty × f, from the page's hidden elements `clean`
  (9eff89 / 003d00, alpha 0xBF: green) and `dirty` (664642 / 120e0a: brown),
  and its height to h × level, kept on the bottom edge; it sits at the
  `dirty` element's z (24), under the glass `cylinder1` (26) and the page
  frame. So fresh oil shows a full green cylinder, used oil a full brown one.
- **Oil change animation** (`vf2`, `0x117018`; started by the `oil_change`
  action `0x126B98` through `0x117008`, which also charges and sets O = −L
  at once): state 0 → counter 60; 1: counter − 1 each frame, below 0 →
  SE 6 (`oil`), state 2; 2: level −= 0.01 (f32) each frame, below 0 →
  level 0, counter 60 (101 frames); 3: counter, below 0 → f = 0 (the clean
  colour), SE `oil`; 4: level += 0.01 each frame, above 1 → level 1,
  `0x1B7BD0`(page, 0) (gives control back: the `ignore_control` widget of
  the progress popup `at/ot/ot_now` blocks input until then); 5 → idle.
  About 325 frames in all. The cylinder belongs to `at/ot/ot`, so it keeps
  drawing under the popup. Ours: the same, with the popup holding the controls
  (below).
- **Car wash** (H; action `wash_car` = `0x126A80`, which builds a `TumbleCar`,
  vtable `0x3122C0`, per-frame `vf2` `0x127220`): the action charges gcf
  `car_wash`, sets the ride car's dirt (condition word 3, garage car +0x210) to 0
  at once (`0x1D9F18`) and starts the spin (state 0, counter 30) on the page
  manager's object list (+0xAE0: it runs every frame, also during the button's
  confirm feedback). Each frame: state 0: counter − 1, below 0 (31 frames after
  the action) → SE 2 (`car_wash`), counter 180, state 1; state 1: adds 10,000
  (f32 `0x34FAE4`) to the yaw acceleration of the page's car render
  (`car_render_car_wash`: the `gt2k_menu::Car` built by `0x11E428`, kept at
  GameStatus+0x307C8 +0x53C; Car +0x174 = its `MenuCarCamera` +0xB0), counter − 1;
  below 0 (after 181 pushes, 212 frames after the action) → Car +0x2B8 = 0 (the
  render's own copy of the dirt, taken from the garage car when the widget was
  built, passed to the model on every draw `0x1B9068`), `0x1B7BD0`(pm, 0) (the
  controls come back, below), the object deletes itself. So the car spins
  looking as dirty as it came in and is shown clean as it stops.
  - The camera's angle springs (`0x1D1A38`, dt = 1/60 at `0x3500A4`): v += a·dt;
    a = 0; v −= v·c·dt; x += v·dt. Every frame (`0x20FB58`, while the car is
    loaded) the yaw's a gets the .fbi yaw acceleration (key 0x42) added and the
    yaw is taken modulo 360 (M: the library call `0x26E2C8` with 360). `showroom_carwash.fbi`: yaw acceleration 100, damping
    5 (20°/s at rest). With the pushes the turn rises within ~0.5 s (time
    constant 12 frames) to (10,100/5)·(1 − 5/60) ≈ 1,850°/s (≈ 31° a frame,
    ~5 turns a second), about 15 turns over the 3 s, then falls back to 20°/s,
    in the page's own turn direction. (Before the car has loaded, `0x1B8EC8`
    skips the camera, so pushes would pile up in a and land at once: L, not
    reproduced; ours applies one push per showroom step once the car is shown.)
  - Whether the object's first step falls on the action's frame or the next
    is not traced (I; ours: the next).
- **Progress popups** (H): the shop buttons (kind 1) open `at/cw/cw_now`
  ("Washing . . .") and `at/ot/ot_now` ("Changing oil . . .") after the 30-frame
  confirm feedback. They are the only pages (US menus) with an `ignore_control`
  widget; its builder (`0x123020`, GT-mode widget table `0x123270`) calls
  `0x1B7BD0`(pm, 1): pm+0xB44 = 1, and the current page's buttons get vf10(0)
  (`0x1C9DC0` → Button +0xAC = 0: `Button::vf3` `0x1B44E8` draws nothing). While
  pm+0xB44 is set the page manager's input step (`0x1B7C08`) returns at once: no
  key does anything, Cancel included. Only game code clears it (`0x1B7BD0`(pm, 0),
  which also shows the buttons again; also reset by the page manager's
  constructor / reset `0x1B6338`): `TumbleCar`'s last push, `OilChangeCylinder`
  state 4 (full). The popup's only item `end` ("Complete", over the base text at
  the same place) links to `at/at`, so nothing leaves the page before the wash or
  the oil change is over. Ours: `MenuController::ignoring_control` /
  `give_control` (a page with the widget takes the controls when built), the
  GT-mode `on_tick` hook steps both animations every frame and gives them back.

## 8. Races, championships, entry rules

### 8.1 Codes and series (H)

- Race codes are table-32 labels. `G…_00NN` single-race series events
  (1–4 races), `G…_RRNN` championship rounds (RR = round count 05/07/10),
  `…_RR00` the championship itself, `E…` endurance (single races), `D…`
  rally (3 one-on-one races per event), `L…` license tests, `M…` machine
  tests, `ATAF0001` free run.
- Series pages `r/<league>/<series>`: leagues beginner (20 series), amateur
  (23), professional (22), rally (10), endurance (10). A championship item
  (`set_race_code_champ`) runs all rounds in sequence; the race code is the
  item name with its last character + 1 (`…_0500` → `…_0501`). Rounds can
  also be raced singly (money and result, no bonus or prize car).
- The game state keeps one stored race code and race type (not saved): what
  writes it and how the series and licence pages' focus reads and clears it
  is in [menus.md §3](menus.md).
- Opponents: N − 1 picks (N = event +0x1B5, the player included) from the
  event's entry pool: random index, probe forward to the first unused entry;
  repeats only after the pool is exhausted (§20.4). A championship field is
  drawn once and kept, each car as built with its paint (H: round 1's 0x18E580 stores entries
  1..n−1's CarEquipments, colour id +0x08 and index +0x0C included, in the entrant records
  +0x490 + 0x1F8 k after the paint draw; later rounds copy them back in 0x18E638 / 0x18E738,
  and the paint draw 0x176FD0 only fills entries whose id is 0xFFFFFFFF).

### 8.2 Event record fields used by GT mode (table 32)

| Off | Meaning |
|---|---|
| 0x19A | license test kind: 1 stop, 2 section, 3 full lap(s) |
| 0x19B | **required license**: 0 none, 1 B, 2 A, 3 IB, 4 IA, 5 S, 6 R |
| 0x198 | rolling-start speed (km/h) on license tests (L), 0 = standing |
| 0x1B5 | cars in the race (6 races, 2 rally, 1 tests) |
| 0x1C4 / 0x1C8 / 0x1CC | license gold / silver / bronze time {u8 min, u8 s, u16 ms} |
| 0x1D2 | 9 = dirt tires required |
| 0x1D3 | trophy class (5 beginner, 6 amateur, 7 professional/endurance, 8 rally) |
| 0x1D4 | **6 × u16 prize for places 1–6, × 100 Cr** |
| 0x1E2 | u16 championship bonus × 100 Cr |
| 0x1E4 | time limit in minutes (2-hour endurance races) |
| 0x1E5 | league: 1 beginner, 2 amateur, 3 professional, 0 other |
| 0x1F4 | car class: 1 race/rally car only, 2 tuned only, 3 production car and not tuned |
| 0x1F5 | aspiration: 1 NA only, 2 turbo only (turbo part +0x11 ≠ 0) |
| 0x1F6 | drivetrain: 1 FF, 2 FR, 3 4WD, 4 MR, 5 RR |
| 0x190 | regulation (table 33: up to 32 allowed car labels) |

Race information → Cars Permitted (popup `00cmn/car_reguration`): the list
widget `menu_regulated_car` (the garage's list kind) holds the regulation's
cars up to the first `-----` label (none when +0x190 is `-`), each as
"%s %s" (the car's name and second name, record +0xE4 / +0xE6); a string
that occurs again later in the list is skipped, so each appears once, at
its last place. An empty list shows the hidden `no_regulated_car` ("No
Limitations").

### 8.3 Entry check (RegurationResult, H)

Seven flags: dirt tires required and not owned; required license not held;
(unused); drivetrain; car not in the regulation list; aspiration; car
class. "Tuned" (0x2362B8, the same in PAL and NTSC-J) = the car record is
missing, or any fitted part differs from the stock list (0x236088), or any
of 21 parts is above its first stage (each helper maps the record's +0x10
stage through a goods table and compares with the stage-0 goods; a missing
record gives the stage-0 goods): brakes, brake controller, drivetrain,
gear, suspension, LSD, lightweight, racing modify, port polish, engine
balance, displacement, computer, NA tune, turbine kit, flywheel, clutch,
muffler, ASCC, TCSC; the propeller shaft unless drivetrain +0x14 is 1, 3
or 4; the intercooler only when the turbine kit's +0x11 is set. The
settings words are never read. Any flag → the race is
refused: `00cmn/license_violation` when the license flag is set, else
`00cmn/regulation_violation`. There are **no power or weight limits**. No
ride car → `00cmn/ride_car`. Free run on a dirt course needs owned dirt
tires (`hm/need_dirt_tire`).

The F1 series appears only with the S license (gcf tag-4 unlock level).

### 8.4 Money and points (H)

- A finished race pays the prize of the finishing place (1st–6th) **every
  time** it is run; the best place is kept per code.
- Prize money (H): `0x238950` = +0x1D4[place] × 100 (immediate at `0x238960`), ×100 again in
  Japanese; the bonus `0x238BC0` = +0x1E2 × 100 (`0x238BCC`; the championship driver
  `0x133388` computes it inline, `0x133CB0`), the same. PAL `0x2399D0`, NTSC-J `0x2358F0`.
  The race information page's six prizes (`0x122C30` …) print the formatted prize, "0" for 0.
- Championship points per round: 10, 6, 4, 3, 2, 1 (H: the signed bytes at `0x352D78`, read
  by place in `0x238A80`; zeros follow; PAL `0x354710`, NTSC-J `0x34CEF0`). The player is champion
  only with **strictly more** points than every other entrant (a tie
  loses). Each round pays its place prize; the champion gets the bonus
  (+0x1E2) and the championship prize car on **every** championship win;
  the result is recorded as place 0 under the championship code.
- **A race counts once started** (H): the GT race driver adds to the race count in the pre-race
  menu's START case (action 6, jump table 0x315FC0 → 0x132E98); leaving from that menu records
  nothing. A finished race keeps the best place (`min`, slot value 8 = none; 0x133030 →
  0x217050) and adds a win for place 0; a pause EXIT (car flag +0x1464 clear) records no place.
- **Championship rounds** (H, `fn_00133388`): races +1 at the round's start (0x133700). A
  finished round: the place prize page (0x133898), the best place under the round's own code
  (0x1338A4), wins +1 for place 0, points to every car by its place in the result list
  (0x18E838). A round left by pause EXIT still counts: every other car takes the points of its
  place in the list, the player none (0x18EAB8 skips car 0 without moving the places). After
  the last round only (round index +0x418 against the count +0x41C, 0x133910): a champion gets
  the trophy page with the bonus (0x133D30), then place 0 is written under the championship
  code (0x133D3C), then the prize-car check runs with that code (0x133D48); no championship
  standing other than the title is ever stored. Retiring ("exit_sure" → `qm_exit`, in both the
  pre-round and the result menus of `qm_gt_event_champ` / `qm_gt_result_champ`) ends the loop:
  rounds already run keep their results, money and wins; nothing more is written. After the
  last round the result menu's EXIT is locked (ours: `QuickMenuItems::exit_locked`). The
  quick-menu actions (setters registered by `fn_0012D8C0`): 1 qm_exit, 2 qm_replay, 3
  qm_try_again, 4 qm_setting, 5 qm_free_run, 6 qm_start / qm_go_race, 7 qm_next_session, 8
  qm_option, 9 qm_save_replay, 16 qm_save_game; the championship driver's table 0x315FF0, the
  GT race driver's 0x315FC0.
- **Championship quick menus** (C, the setters `fn_0012D8C0` registers, reading the driver's
  list block at global 0x3514F0: +0x2C laps, +0x30 round index, entries +0x38 + 0x18 n {u64 car,
  +8 points, +0xC time, +0x10 player flag}). Before every round (`fn_0012F290`, 0x133650 /
  0x1339F4, again after QUALIFY 0x133B84) entry n is the entrant on grid slot n (`fn_0018E3F0`
  finds the one whose +0x1EC is n; the grid `fn_0018E190`: the opponents by kg/PS, the player on
  slot 5, or a free practice's, §8.7), with its points so far: `qm_list_car_name<n>` the car name ("%s", "---" without a car),
  `qm_pt<n>` "%d %s" with SHORTMENU_POINT_SINGLE under 2 points, else POINT_PLURAL (US "0 pt",
  "10 pts"), both in 0x800080FF on the player's row; the list is the grid, not the standings.
  `qm_session_num` (both menus) "%s%d%s" = SESSION_BRFORE, round index + 1, SESSION_AFTER (US
  "#1 Race"); `qm_laps` "%d %s" with LAPS_SINGLE under 2, else LAPS_PLURAL (every widget of the
  class: the layout has a shadow copy). The texts are the realtime group SHORTMENU (slot names
  0x2B1B18, texts 0x2B1B30, 6 slots, `0x174568`), defaults EUC 第 / 戦, Lap / Laps, pt / pts. After
  a finished round the result menu's list is the finishing order (`fn_0012F070`, 0x1338DC: car,
  race time car +0x14B4, player flag); after a round left by pause EXIT it is not refilled
  (0x1338EC): the pre-round grid list, each row the car name and the no-time text (+0xC keeps
  0x157529FF from the entry reset `fn_0012F820`, which `fn_0012F290` never overwrites;
  `fn_0023B930` prints it as `\x7F\x7F'\x7F\x7F.\x7F\x7F\x7F` from 0x343A28, the font's long dash),
  the player's row still in 0x800080FF (the points stay in the entries; the layout shows none).
  A GT race left by pause EXIT clears the list instead (0x13306C: six entries car 0, no time, no
  flag: "---" and the no-time text; ours shows the running order). Ours draws the no-time text
  with the font's long dash too (`race::kNoTimeText`). Ours: `RaceScreen::fill_championship`,
  `race::quick_menu_column` (rows by widget class), `race::result_menu_list`,
  `GameRequest::champ_points`.
- **A championship in progress lives only in a save made between rounds** (H): "Save game"
  (action 16, 0x133C10) writes the progress into the game state (`fn_0018EB58`: active flag
  +0x2EF28 = 1, the round code, the round index and count, the eight entrants' cars and
  points), runs the memory-card save, then clears the flag again (`fn_0018EC48`). Loading
  that save resumes the championship at the next round (`fn_00133388` entered with a non-zero
  argument: `fn_0018EC78` reads the block, advances the code's last character, clears the
  flag). Retiring after such a save leaves the card's save as it was (it still resumes); the
  running game holds no championship, so the next save has none. Ours keeps the championship
  in the running game between rounds and its autosave writes it (§12; a closed game resumes
  it), a convenience the original does not have.
- Ours: `GameResult::started` (the race screen: not left from its pre-race menu); `on_result`
  records only a started race, records a run round before a retire ends the championship, and
  ends it when a round's pre-race menu is left; `GtMode::record_round` gives the player no
  points for an unfinished round.

### 8.5 Result marks on the race pages (H)

**Series pages** (`r/<league>/<series>`): widget classes `event_status_button01..11` (factory
`fn_001212E0`, table 0x297370) make one object per race button (ctor 0x1184B0, draw 0x118760).
NN is the button's index in the page's button list (mbl item order, items without a layout
element skipped; index 0 is the race information item); no object past the list (the Dream Car
pages' 09..11). The widget's own x/y/w/h are not used (editor leftovers: negative x, descending
y), only its z. The object reads, once when the page is built, the button's own item name: the
best place under it (`fn_00217080`; 8 = none) → hidden `mark_<place+1>`; the race record of
the name (none for a championship code) → its course's hidden logo (named after the course).
Each is drawn centred where its layout box would be if the button's top-left corner were the
page's: in layout units (y up, box = bottom-left), {b.x + e.x, b.y + b.h + e.y − 448, e.w,
e.h} for the button box b. On disc every series page has the medal (61, 412, 32, 32) → {b.x +
61, b.y + 4} and the logo (4, 413, 55, 42) → {b.x + 4, b.y + 5}, their own sizes; the medal
lands in the button image's round slot, also on the 152-wide last round of a championship
page (96 visible pixels plus padding) and in the SERIES RACE image's laurel (the
`set_race_code_champ` item: its …_0500 code, place 0 for a champion). Colours 0x34FA74..: full
(0x80) on the focused button, half (0x40) on the others; the elements' flags 0x1c fade them in
over 24 frames when the page is built. The focused race's `<course>_doc` text comes from the
`event_course_doc` widget of its focus layer (`fn_00121450`, the element's own 300×22 centred
on the widget box, z 33).

**League pages** (`r/<league>/<league>`): `event_status<N>` (ctor 0x117C58, draw 0x1180B8) in
the focus layer `s_<N>` of the item it belongs to, which is named after the series' first code
(`Gsunday_e_0001`) or the championship's own (`Gtt_e_0500`). Codes: the name's last two digits
counting up while `<prefix><%02d>` is a gcf section-4 result slot (0x117DD0..0x117E90); a
championship (name[len − 4] or name[len − 3] not '0') is its own code then its rounds (n = 6,
8 or 11 on disc), a series of races 1..4. Every element is drawn at its own size centred on (X,
the widget's centre y): f = `frame`.w, a = `frame_arrow`.w (31 and 43 on disc), cx the
widget's centre x. A series: `frame` + mark per race at X_i = cx − n (f + 4) / 2 + f / 2 + i
(f + 8). A championship: L = cx − (2f + (n − 1) a) / 2; its own `frame` + mark at L + f / 2;
then from x = L + 2f each round but the last a `frame_arrow` centred at x + a / 2 with its mark
at x + f / 2 (over the arrow's disc), x += a; the last round a `frame` + mark at x + f / 2. The
mark reads the best place each frame (place 0..5 → `mark_1..6`); full brightness; the frame
before its mark; the 24-frame fade-in each time the focus layer is built. `need_license<N>`
(`fn_0011DBF8`): the item's code (+1 when it ends in '0') → its race record's +0x19B → the
table 0x310738 (`license_unlimited`, `lc_mark_b`, `_a`, `_ib`, `_ia`, `_s`, `_r`; 0 = none),
the element at its own size centred on the widget.

Ours: `widget_draws` (`gt_mode_menu/widget_art.cpp`), `MenuController::layer_fade_alpha`.

### 8.6 Starting grid (H; C: diff harness `gt_grid_exact`)

GT Mode's races and championship rounds grid the field by a whole-number weight-to-power
figure. The GT race driver (`0x132BA0`) calls the race grid `0x12E670` (on the race
information: entry list +0x18, {+0 count, +8 entry pointers}); the championship driver
(`0x133388`, and the championship information's virtual 12 through `0x18E638`) calls the
championship grid `0x18E190` (championship information, race information) before each round. Both:

- Every entry, the player's (entry 0) included, is built by the showroom build `0x1D93C0`
  (§5 "Power figures") from its CarEquipments (entry +0x20) with a fresh engine (the condition
  words cleared by `0x1D9B10`).
- **Figure** = the spec's mass (show +0x2A, `lh`: the catalogue weight with the weight-reduction
  parts, without the converter's added mass) **div** the whole power `0x1D9680` gives for the
  peak (a3 = 0: show +0x1294 converted by `0x245A58` to the language's unit, HP / BHP / CV =
  PS × 0.98619, then truncated): integer division, truncating. So the figure is in kg per HP on
  NTSC-U and in the British, Italian, Spanish and French languages, kg per PS in German and
  Japanese (PAL: the same code, the units word set by the language; NTSC-J's `0x1D70C8` has no
  conversion, PS truncated). The quotient is stored as a float (exact for these values) and compared as one.
  A power of 0 would stop the program on the divide trap (no car on disc has one; ours puts
  such a car last).
- **Places**: for the opponents (entries 1 .. n − 1 only), each pair with different figures
  adds one to the higher figure's count; equal figures count nothing. Then in entry order each
  opponent takes its count as its place or, when an earlier opponent has it, the next free
  place. So the lowest figure is on pole and **equal figures keep entry order** (the order of
  the draw, §20.4). The player takes place n − 1, last.
- The race grid writes the place to entry +0x20E0 (the grid slot the race start reads); the
  championship grid works on six entries always, writes the places to its own records
  (championship information +0x484 + 0x1F8 k; the player's 5 first) and copies them into
  +0x20E0 (`0x18DF70`). The field and the cars do not change between rounds, so every round
  starts on the same grid.
- After a free practice (§8.7) with a lap set, the grid is made again by the best laps (`0x12E920`
  in a race, `0x18DFA8` in a championship); not part of this rule.

On the disc, the Japan Championship's field (`Gjapan_n_0505`, opponents in entry order 300ZX,
3000GT, Supra RZ, Lancer Evolution VI, NSX Type S Zero), each car as the draw builds it
(`0x238550`: its record's settings, the output factor +0x10A = 1230 / 1250 ‰, about 345 HP
each), gives 4, 4, 4, 3, 3 kg/HP: Lancer, NSX, 300ZX, 3000GT, Supra, the player [C, 2026-10-31:
the original's `0x238550`, `0x1D93C0` and `0x12E670` in the diff-harness interpreter, places
equal to ours]. An earlier PCSX2 run of the original showed Supra, Lancer, NSX, 300ZX, 3000GT
(5, 5, 4, 4, 4) on a card whose championship field our exporter had written with stock
settings (no output factor): the original's rule over our cars, not GT3's own field. Ours had
ranked exact floats of mass over the curve's peak torque × rpm in PS. Ours now:
`race::gt_grid_figure` / `gt_grid_places`, the figure from `race::exact_grid_figure`
(`sim::exact::showroom_build`, `shown_power`).

### 8.7 The free practice before a race (H)

The pre-race menus' FREE RUN item (`free_session`, action 5 `qm_free_run`; shown as QUALIFY)
in a GT race (`fn_00132BA0`, case 0x1330E0) and before every championship round
(`fn_00133388`, 0x133A60):

- **The whole field runs it.** The driver destroys the menu's backdrop race and runs a
  `RaceFreePractice` (0x182F40, `RACE_MODE_FREE_PRACTICE`; with its own 6 car slots) on the
  race information the race itself uses, with every entry: `RaceBase::vf9` gives the conductor
  (`DynamicsConductorFreePractice`, 0x1F2010) one car per entry (0x17BD28: count = the entry
  list's) and points entry k's +0x18 at car k (0x17BD68). Car 0 starts in its pit box, car k
  > 0 on the line 200 k + 300 m behind it at 120 km/h (0x1F2070, start mode 5); the AI drives
  them. Records are on (race info +0x6C = 0, 0x133150; the backdrop runs with 1). There is no
  lap count; the player ends it by pause EXIT; it has no replay.
- **The start** (C: PCSX2 capture of Sunday Cup race 1, 2026-10-30). No countdown: the
  conductor's virtual 9 (0x1F2698) returns 3, which `RaceBase::vf9` turns into start type -1
  (0x17BCB4: 0 and 3 -> -1, 1 the event's, 2 -> 3). The title shows from 0.2 s for 2 s with the
  event display blocked 3 s (presentation §K3 Title); car 0 waits out its 2 s pit-box start
  (spawn hold 120), then the computer drives it (driver kind 4) through the pit lane and the
  warm-up lap to the pit lane's exit point (on Super Speedway by the cones before the line), where
  the pit lane posts its exit (6). The HUD's Lap stays 0 until the first line crossing (the
  conductor's vf29: 0 while the lap counter is 0), which comes on the next lap.
- **The race display's notes** (Information events, `fn_001941C0`; green sw_16, 3 s, queued;
  texts by kind from the realtime group RACEDISPLAY): kind 4 posted by the display's own
  set-up in free practice (0x191FB8, D+0xA474): `ReadyQualify` "Qualifying starts after 1
  warm-up lap"; kind 6 (the pit lane's exit) for the display's own car: the START picture
  ("@start", vf19) and SE `count_s` (0x194324); kind 3 (the lap handler's first crossing,
  0x1F2248) for its own car: `StartQualify` "Start qualifying"; kind 2 (each new fastest lap of
  any car, 0x1F22C0) for every car: the P.P. picture (0x194420) and `PolePosition` "%s in pole
  position" with the car's name. The P.P. line keeps its label "P.P." with no picture until
  the first new fastest lap: the set-up's call (0x1943E8) finds no car pictures yet (+0x1C null,
  0x194430 returns; the capture shows the label, and after an AI car's lap its picture with no
  label).
- **Best laps.** Each car's best lap (car +0x16C4; 0x157529FF = none) starts as none at the run's
  initialisation (`DynamicsConductor::vf23` → 0x1DDAD0 → 0x1EC4B0), then the conductor's restart
  (vf22 0x1F20B0, called at the end of vf23) lowers it to its race entry's best lap so far
  (entry +0x11D8, the lap of the entry's best splits, 0x1EC4C0: kept if lower). The entry's
  record lives in the race information, which stays for the whole GT race / round: it is
  updated by every timed lap without a course-out (0x17AF10), so a later practice starts every
  car from its best lap of the earlier ones. Every lap a car completes lowers its own best
  (0x1EC620 / the lap handler 0x1F22C0, a course-out lap too). The cars are ranked by best lap
  (vf3 0x1F24C0: an insertion sort, strictly smaller ahead, so equal times keep the order).
- **The re-grid** after the run, only when the player's entry (entry 0) has a best lap (GT race:
  `fn_0012E920`'s own test 0x12E968, `0x157529FE < t` returns; championship: the caller's test
  0x133B40, `t == 0x157529FF` skips `fn_0018DFA8`). For every pair of entries i < j with
  different best laps, the slower one's count goes up (an unsigned compare, so no time is
  slowest); equal times (no time too) count nothing. Then in entry order each entry takes the
  slot of its count, or the next one not yet taken: equal times keep entry order. GT race: entry
  +0x20E0 = the slot. Championship (6 entrants): the entrant's slot (+0x484 + 0x1F8 i), then
  `fn_0018DF70` copies it to the race entries' +0x20E0. The race (and the next practice) starts
  on that grid; without a lap the grid stays as it was (the event's, or an earlier practice's).
  A championship's next round starts from its own grid again (`fn_0018E190`).
- **The pre-race list** after it. GT race (`fn_0012F160(info, list, 0)`, 0x133174; once at the
  driver's start with 1, 0x132DF4: every time none): list entry = entry +0x20E0, each with its
  car, the entry's +0x20FC, the car's best lap of the practice (+0xC: `qm_list_time<n>`,
  `fn_0023B930` "%d'%02d.%03d", from an hour "%d:%02d'%02d.%03d", none the long dashes) and
  the player flag on entry 0. The list is not refilled otherwise: the menu shows the last
  practice's times until the next practice. Championship (`fn_0012F290`, 0x133B84): the
  entrants by their (new) slots with their points; the times stay none (`fn_0012F820`), and
  the layout (`qm_gt_event_champ`) has no time column.
- Ours: the free practice runs the race's field (`RaceScreen::build_exact_solo`,
  `FieldConfig::opponents`; `FieldConfig::entry_best` the entries' best laps carried from the
  visit's earlier practices); at its end `RaceScreen::end_free_run` takes every car's best lap
  (`exact_best_laps`), `race::practice_regrids` / `race::practice_grid` the grid
  (`FieldConfig::grid_slots`, for the race and the next practice) and
  `race::practice_list_times` the GT race list; `race::demo_start_type` the start type. The
  practice's HUD (presentation §K4 mode 13): the player's place by best lap, the P.P. line's
  time (the session's best lap of any car) and picture (the car of each new fastest lap; none
  before), Lap 0 until the first crossing, the notes above (`RaceScreen::exact_solo_step`; the
  3 s block only for the warm-up note). Not reproduced: the event display's gaps (ours to the
  player's own best lap, GT3's to the session's).

## 9. Licenses (H)

- Order B, A, IB, IA, S, R(ally, menu `lc_d`). Pages A, IB, IA, S need all
  earlier licenses (`lc/no_license_<x>`); B and rally are always open. The
  8th test needs tests 1–7 passed (`lc/no_last_exam`).
- A test is judged on time against the event's gold/silver/bronze times; a
  failed or aborted run earns nothing. The best medal is kept.
- **A license is held when all 8 tests have a medal**; **all gold** awards
  the license prize car once: B `roadster_rs_00_ext`, A `rx8_01`, IB
  `z_detroitver_01`, IA `vanquish_00`, S `viper_gtsr_00`, R
  `impreza_wrc_prot_01` (gcf keys `L0B0000` … `L0R0000`).
- The test car is the event's entry 0 (a car variant with its own parts; its
  paint is the event's slot-0 colour id at +0x110, 0xFFFFFFFF = none); in British English
  entry 1 when entry 0's car is not offered there (§21); the course is the event's course. The 48 tests with their times
  are listed in the event records (e.g. B-1 `L_testcourse1`, Beetle 2.0:
  35.100 / 35.400 / 35.800).

## 10. Prize cars (H)

Prize sets (gcf tag 15) are matched to a result by name:

| Set name | Awarded when |
|---|---|
| `X_0000` (G and D series) | every race of series X won (once) |
| `X_0500` / `_0700` / `_1000` | championship X won (every time; 4 candidates) |
| `E…_0001` | that endurance race won (every time; 3–4 candidates) |
| `L0B0000` … `L0R0000` | license all gold (once) |
| `gtmode_<league>_complete`, `achieve_half/3_quoter/complete` | league / completion thresholds (H: completion ≥ 0.5 / 0.75 / 1.0, immediates in the handlers `0x214BB8` / `0x214C40` / `0x214CC8`, sub-types 0x12..0x14; PAL `0x215B3C` .., NTSC-J `0x211F04` ..) |
| `gtmode_complete` | every GT-mode result won, checked with a GT-mode code (below; once) |

Single-candidate sets are given once (prize flags); multi-candidate sets are
rolled with their weights (all 1 on disc: uniform; repeated models act as a
colour roulette). A full garage gets nothing. The colour id of a candidate
selects the paint; −1 (or an id not in the model's list) = a random paint
(§20.3). Each candidate's label first goes through the language's label table (§21; a
swapped candidate has no colour id).

Presentation (menu `car_get.mbl` / `license_get` in `lc_get.mbl`): the
car_get page names the reason with one of its hidden text elements
(`solo_race_clear` "Event Winner", `championship_win`, `enduro_win`,
`license_<x>_all_gold`, `gtmode_<league>_complete`, `achieve50/75/100`, …);
the license page shows the layers `<lic>_mess0` / `<lic>_mess1` (H data, M
use). The car_get page itself: §10.1.

**When the check runs** (C unless marked): the car_get control (`0x13F508`
→ `0x13EF40` → `0x214D50`) is opened with one code at the end of the arcade
race (`0x12F840`), time trial (`0x130A50`), licence (`0x132030`: after the
licence card, only when `0x215790` reports the licence all gold and a flag
in its frame is clear, M: newly all gold; with the test code), GT race
(`0x132BA0`: only when the last race was won, with its race code, frame
+0x44) and championship (`0x133388`: only for a champion after the last round,
with the championship code, 0x133D48; §8.4) drivers. The machine test and free
run drivers never open it. `0x214D50` scans every tag-15 key: a
single-candidate key is skipped once given (`0x215D80`, prize flags);
`0x2140F8` sorts the key into a handler by name (L licence keys, `_0000`
series, `…_RR00` championships, arcade keys, leagues, `gtmode_complete`,
`achieve_*`); at most one key per handler is collected per check.

**League keys** (H, `0x214868`): looked at only when the checked code is of that league:
by the code's class (`0x23A8B8`, jump table 0x340020), classes 5 / 6 (G codes) take the league
from the name (`0x214048`: "_e_" beginner → sub-type 0xD, `0x2172C8`; "_n_" amateur → 0xE,
`0x2172E8`; "_h_" professional → 0xF, `0x217308`), class 7 (E) the endurance key (0x11,
`0x217410`), class 8 (D) the rally key (0x10, `0x2173F0`); licences and other codes none. A
league key not given when the league was completed (a full garage) comes only with a later
result of that league. Ours: `GtMode::code_league`.

**Endings** (H, `fn_0013F508`, the wrapper every caller of the car_get check goes through:
arcade race, time trial, licence, GT race, championship): after the car_get control, if the
Beginner league is complete (`0x2172C8`) and the short ending not seen (+0xBC) it plays
`stream/ending_short_full.pss` and sets +0xBC; then, separately, if the Amateur or
Professional league is complete (`0x2172E8` / `0x217308`) and the long ending not seen (+0xC0)
it plays `stream/ending_long_full.pss` and sets +0xC0. Both can play after one result; a
championship won on points whatever its last round's place opens it, a championship lost does
not. Ours: `Award::prize_check` → `on_result` queues them (`MenuState::movies`) for GT-mode
results; the arcade drivers' check (an ending still pending) is not reproduced.

### 10.1 The prize car page (car_get) (H)

**The control** (`0x13EF40`, one per check): the stir (`0x211A68`), the keys (`0x214D50`), then
per key: a full garage (`0x219448` ≥ 200) opens `not_car_get` instead; else every candidate in
order is loaded with its paint (`0x13E918`: its colour id when in the model's list, else the
§20.3 paint draw; the 0x14-byte record: +0 label, +8 its model buffer, +0xC the paint), its
weight max(float(w), 1) kept; the page `car_get` (`menu/car_get.mbl`, `prize_get.img`) runs
(`0x13EEF8` → loop `0x13EDB0`: the GT-mode state stepped every frame, `0x211AC0`); then the car
in record [control +0xE94] is added (`0x2192E8`, label and +0x10) and a one-candidate key's
prize flag set (`0x215D38`). +0xE94 is written every frame as int(roulette position)
(`0x13D724`), and nothing can leave the page before the roulette has stopped, so the car added is
always the one the page shows and the weighted pick. At most 4 candidates (`0x13F010`).

**The page's widgets** (factory `0x13F648`): `render_model` → CarsItem (ctor `0x13CE58`,
`0x13E638`), `eventname` → EventNameItem (the reason line), `car_get_message` →
CarGetMessageItem (vf2 / vf3 `0x13E438` / `0x13E470`: update and draw only once control +0xED5
is set: the "Bonus car acquired" ticker starts then). The `after` layer (one black band,
`after_bg1`) is built by the loop on the first frame it sees +0xED5 (`0x13EE48`, the frame after
it is set). The OK item is the page's only item.

**CarsItem construction**: the candidates' models into slots of 0x36C (TheCar, `0x13C8F0`: the
model and its paint); total = Σ weights, x = total × unit (GT-mode state, `0x211B00`), the target
= the first i with x ≤ the running sum (none: 0) (+0x114); the roulette reset (`0x13B850`); the
controls taken (`0x1B7BD0`(pm, 1) at `0x13D24C`: no key does anything, the buttons not drawn);
+0x1034 = 1, +0x103C = +0x1038 = 0, timer +0x1030 = −1. Then (`0x13D368`) each candidate's size
from its built spec (`0x1C4248`; spec +0x1A the chassis length, +0x22 the body f32 +0x2C
truncated, both read as s16, mm): w_i = +0x1A × 0.001 × 0.5, h_i = +0x22 × 0.001; the carousel
(control +0xEA4, `0x13BA80`): side = max over i of (w_i + w_(i+1) mod n) × 1.1 (0x34FB38), tall
= max h_i × 1.1, radius = 0 (n = 1), side × 0.5 (2), (float)(side × 0.5773502691896257) in
double (3), side × 0.70710677 (4, 0x34FB3C); headings: g = 20 u (u a unit draw on the **shared**
state, `0x26C0F8`), then for each car g = 20 u + 80 + g, reduced below 360: car i's heading;
the carousel angle 0, its speed −0.20833333°/frame (0x34FB40), the cars' own turn 1°/frame.

**Each frame** (CarsItem::vf2 `0x13D498`), in this order:
1. The roulette (+0x100, `0x13B960`): {travel +0, position +4, speed +8, deceleration +0xC, n
   +0x10, target +0x14, state +0x18}. State 2 (slowing): speed += deceleration, travel −= the new
   speed, speed at least 1/60 (0x34FB34); travel ≤ 0 and int(position) = target: speed 0, state
   0. Then position += speed, reduced (position − int(position / n) n while ≥ n).
2. The carousel (`0x13BCE0`): once slowing (control +0xED0): |turn| > 0.35 (double compare,
   0x3192B0) → the cars' turn −1/720 (0x34FB44), else 0.35 (0x34FB48); the carousel speed +1/720
   (0x34FB4C), 0 once its product with −0.20833333 (0x34FB50) is negative. Then every heading +=
   the cars' turn and the angle += the carousel speed, each brought once into 0..360.
3. timer − 1.
4. Each car: current = (i = int(position)); its floor light (TheCar +0x360, `0x13CA30`): 1 when
   current, else − 0.05 (0x34FB80) down to 0.
5. The camera (CarsItem +0xED4, `0x13C3A0`, states at +0x14C, table 0x3192C0; go flag +0x150):
   state 0: far = side × 0.5 + radius; lift = (float)(tall + far × 0.57735) and back =
   (float)(lift × 5.67128) in double (0x3192D8, 0x3192E0); eye at (0, lift, 0), its target (0, 2
   tall, far × 2.4 (3.6 for one car)), k 0.2, damping 1.0; look-at at (0, 0, −back), its target
   the carousel's centre, k 1.9, damping 4 (0x34FB60..0x34FB64); state 1. States 1 / 3 wait for
   the go flag (→ 2 / 4), 2 → 3, 4 → 5; state 5 every frame: look-at target (x, 0.3 tall, z),
   eye target (x, 1.8 tall, z + 1.6 side), k 1.9, damping 4 (0x34FB6C..0x34FB74), (x, z) the
   current car's place. The springs (MenuCarCamera, `0x1D1A38`): force += (target − position) k,
   then v += force dt, v −= v damping dt, position += v dt, force = 0 (dt 1/60, 0x3500A4); the
   look-at's in `MenuCarCamera::vf9`, then the eye's. View (`0x13C668`): eye → look-at with up
   (0, −dz, dy) / max(|(dy, dz)|, 0.01); projection (`0x13D938`): ±320 × ±224 at 320 / tan(0.3490659)
   (a 40° horizontal field), near 0.1, far 1000, y × 1.098.
6. The page's state (+0x1045, table 0x319380):
   - 0: timer = 120 (210 for one candidate), state 1 (and on).
   - 1: when the timer has run out: the camera's go flag; one candidate → state 3; else the
     roulette spins (`0x13B880`: state 1, speed 0.1/frame, 0x34FB30), timer 60, state 2.
   - 2: when the timer has run out: the stop (`0x13B898`): to = target + 0.5 − position (+ n
     while < 0); travel = (12 div n) n + to; deceleration = −speed² / (2 (travel + 1)); the
     roulette's state 2; state 3.
   - 3: once the roulette's state is 0: state 4.
   - 4: +0x1034 −= 0.025 (0x34FB8C); below 0: 0, the carousel slows (+0xED0), the camera's go
     flag (it flies in on the car won), state 5.
   - 5: +0x103C −= 0.05 (0x34FB90); below −2 tall: +0xED5 = 1, the controls back
     (`0x1B7BD0`(pm, 0) at `0x13D6A8`), +0x103C = −2 tall, state 6.
7. +0x1038: 0 while +0x103C ≥ −tall, then + 1/30 (0x34FB94) a frame up to 1.
8. control +0xE94 = int(position).

The roulette therefore holds on candidate 0 for 120 frames, turns at 0.1 candidate a frame for
60, then slows over (12 div n) n whole candidates plus the way to the middle of the target and
stops on it: 379..432 frames after the page opens for 2..4 candidates (the target decides);
41 frames later the camera flies in, then the controls come back after the others' drop (2 tall
/ 0.05 frames: 58 for a 1.3 m car); a one-candidate page: 211 frames held, then the same. The
NTSC-J program holds the same values; the PAL program its 50 Hz ones (120 → 100, 210 → 175, 60 →
50, 0.1 → 0.12, 1/60 → 0.02, 0.025 → 0.03, 0.05 → 0.06 (drop and light), 1/30 → 0.04, 1/720 →
0.002, 0.35 → 0.42; the camera's dt stays 1/60).

**Draw** (vf3 `0x13D750`): while +0x103C ≥ −tall, `0x13D938`: the light model's ambient
0x49BE50 = (0, 0, 0, 1) (`fn_00255898`(0, …)); every car (the current one last: order 0x3516C0
with the current car swapped to the end, 0x13DE50) at Translate(place) RotY(heading) (place:
the polygon corner i 360 / n at the radius turned by the carousel angle, `0x13BF18`), the others
then Translate(0, +0x103C, 0) (they sink); under each its floor light (`0x13D798`: Translate(0,
−0.005, 0) RotY(−heading) Scale(side / 2, 1, side / 2), the unit square ±1 (`0x13C2D8`) with the
hit-mark texture (the widget's image), colour (1, 1, 1, +0x360)); then the car with the car
colour scale (`fn_0024F580`(0, s): VU scratchpad 0x70003AF0..AF8 = s, AFC = 1) at **0**
(0x13DD8C, back to 1 after it): every car on the carousel is a black silhouette, the floor
lights alone lit. The current car's draw starts (`0x13DC8C`..`0x13DD4C`) by putting the page's
own background (the menu's saved buffer [0x352758], `fn_00244AB8`) back over the whole frame
(`fn_00256188`, colour 0x80FFFFFF, `pgl_set_alpha`(0x64, 1 − +0x1034): FIX = int(clamp(a × 128,
0, 255)) at `0x2527E8`, 0x80 = 1), so everything drawn before it, the other cars and their
lights, stays at 1 − FIX / 128: unchanged during the roulette (+0x1034 = 1), faded out over the
41 frames of state 4, gone while they sink; no glow on the car. After that (`0x13DF30`): the
current car alone (its light first, `0x13D798` with Translate / RotY), the ambient = the page's
light 0 colour × +0x1038 (`fn_001BB400`(…, 0), 0x13E00C..0x13E044), the page's light 0 colours
× +0x1038 (`0x13CA78`: params 4 and 8, `fn_002557E8`(0x65, …)) and the car colour scale +0x1038
(0x13E2C8): it lights up from black over 30 frames.

Each car's draw (`0x13CC48`, from both `0x13D938` and `0x13DF30`) starts with its floor shadow
twice: `0x21F328` (the showrooms' node 6 with colour bias 0 and scale 1, menus §9 "Showroom floor
shadow"), then the race's `0x21F258` with the shadow colour `0x21F090`(0, 0, 0, 1) set at
`0x13D9FC` (bias 0, alpha scale 1: the same colours) after `0x21F0B0`'s 0.02 m lift; then the body
(`0x13CC78`) and the wheels (`0x13CCF8`). Ours: two in-order items ahead of the cars
(`draw_car_get`), the second translated 0.02 m up (the projection nudge left out: nothing else
on that floor is drawn in 3D).

So with the Japan Championship's cars (heights 1548, 1267, 1131, 1103 mm: tall 1.703): the others
are gone 41 frames after the roulette stops, the car won is alone 35 frames later (black), fully
lit 30 frames after that, and the controls come back 4 frames later (69 frames into the drop).

Ours: `CarGetRoulette` (`src/game/car_get_roulette.*`, EE singles; `others_opacity`, `shade`),
started when the page opens with the rolled candidates and paints (`GtMode::last_prize_rolls`, the
roll itself still at the result, the order of its draws unchanged), stepped by
`presentation_frame`; the page takes the controls (`GtModeHooks::Presentation::take_controls`)
and gives them back on the release; the ticker and the `after` band from the next frame, the band
fading in over its 24 frames; the app draws the others' lights and cars at `others_opacity`, then
the lit car's light and the lit car, every car with the colour scale `shade` (`DrawItem::tint`
and `reflection_scale`), through the page's camera (`draw_car_get`). Ours scales the lit car's
final colour once by +0x1038 (the program scales the light colours and the colour scale both;
what the VU does with the scale word is not traced); the background put back is ours as the
others' opacity, so where one of them covered the lit car ours shows the lit car (the program,
its depth kept, shows the background there).

**`gtmode_complete`** (C, handler `0x214AF8`, sub-type 0x15): eligible when
the checked code's class (`0x23A8B8`) is 5..17 and `0x217430` holds: the
number of tag-4 result slots that are 0 equals the number of tag-4 codes
(every one of the **364** GT-mode results won, including the 27
championship codes). Classes by the code: `A…` arcade 1..4 (`A2…` 2P,
`AT…` time trial, `ATA…` free run), `G…` 5 (`…_00NN`, a series race) or
6 (any other last four digits: championship rounds and codes), `E…` 7,
`D…` 8, `L` with third character `B`/`A`/`S`/`R` (second character `I`
for IB / IA) 9..14, `M4…`/`M1…`/`MM…` 15..17, anything else 0. So it comes after a GT race or championship win or
a licence turning all gold, never from an arcade result. One candidate,
`mclaren_mp4_4`, colour −1 (random paint, §20.3), given once (prize flag).
The car_get reason element is `gtmode_complete` ("Simulation Mode all
complete"). It is **not** the 100 % rule: `achieve_complete` (completion
ratio 1, §4) is a separate key. (Ours had no rule for the key, so the
loaded set was never eligible.)

Paint colours: carcolor.db colour definitions are {u32 id, u32 name index,
u32 second name index, u32 RGB}; the name indices point into
carcolor.sdb (631 names, e.g. id 1 = "Africa Grey Metallic"); Japanese shows the second
name, the other languages the first (§21, H).

## 11. Other modes (H)

- Machine test (`MMAX`, `M400`, `M1000`) and free run (6 courses on the
  home page): costs a day, records only, no money.
- Preview (`go_preview`, the PREVIEW button of a race's entry page) [H, checked in PCSX2
  2026-11-04, Sunday Cup race 1]: the command `fn_00126280` only sets the main loop's state to
  5, no day passes, and its runner `fn_00100538` runs the title's demo race on the chosen
  event: a `RaceArcadeDemoInformation` on the race code (GT +0x3080C, no car label, level 0:
  every slot from the pool draw, §15, slot 0 last on the grid, `fn_0017A858`), time limit 0
  (info +0x50, the demo's gcf word in the attract runner), a `RaceArcadeDemo`, played with
  `RacePS2Base::vf42(2, 1)`. Its one addition is race info +0x7C = 1, which `fn_001F35B8` turns
  into no laps and no time limit, so nothing ends the race but the player: seen racing on past
  8 minutes of a 3-lap event. The event's own start runs (the 10 s hold on the grid, seen with
  the TV cameras and no lights), no HUD, the replay mark on its timer. Differences from the
  attract demo, all from +0x7C (`fn_0017DD80` and 0x20B93C read it): the race songs from the
  playlist at the replay level instead of the demo track, no car change every 300 frames.
  Start ends it (`RaceBasic::vf29`: event 1 in play mode 2, no pause window); Cross toggles the
  driver's view; Circle, Triangle, Square, L1, R1, L2, R2, Select and the directions leave it
  running (the title demo in PCSX2: Start ended it, Cross, Circle, Triangle and Square did not).
  The menu comes back on the entry page with PREVIEW focused; nothing is recorded and the ride
  car is not used.

## 12. Memory-card save (H)

- Directory and file `BASCUS-97102GAMEDATA/BASCUS-97102GAMEDATA`, plus
  `icon.sys` (964 bytes), `icon0.ico` and a 0x60-byte `thumbnail`
  (credits s64 +0x08, car count +0x10, license level +0x14, day +0x18,
  completion f32 +0x1C, comment +0x28).
- Data file: 0x40-byte header {u32 header size 0x40, u32 payload size,
  u16 minor 1, u16 major 7, u32 CRC-32, name "Base"}; the payload bytes are
  **inverted** (XOR 0xFF) and the CRC is the standard zlib CRC-32 of the
  stored (inverted) payload; the file is padded to 64 bytes. Major ≠ 7 is
  rejected.
- Payload order: scalar block 0x130; n cars × 0x204; m wheels × 0x44
  ({u32 used, char[0x40] label}); licenses 0x3FC0 (6 × 8 × 0x154: s32
  medal, 6 × char[16] names, 6 × u32 times (ms, empty 0x157529FF), stamps,
  car names); race results 0x5F8 (s32 per tag-4 code); course bests 0xB0;
  arcade 0xB40; arcade time-trial challenges 0x1A68; time trial 0x8400;
  machine test 0x900; prize flags 0xC8 (byte per tag-15 key); suspended
  championship 0xF2C; options 0x888. Payload size = 0x112BC + 0x204·n +
  0x44·m.
- Checked against saves the original wrote in PCSX2 (H): the NTSC-U discs
  (SCUS-97102, PBPX-95503) both use `BASCUS-97102GAMEDATA`. Scalar +0x2C holds
  the ride index again (−1 in a new game). `icon0.ico` is the disc's
  `menu/icon/ICON_a_spec.ico`; `icon.sys` is the disc's `menu/icon/icon.sys` with
  the title "ＧＴ３　ＧＡＭＥ　ＤＡＴＡ" (Shift-JIS, 26 bytes, one line: +6 = 26) at +0xC0
  and "icon0.ico" at +0x104 / +0x144 / +0x184. Thumbnail: +0x14 highest license
  (−1 none, 0 = B), +0x1C completion in per cent (f32; the file list shows it
  truncated to one decimal), other words unset (stack contents). Files are
  written in the order icon0.ico, icon.sys, data, thumbnail; an overwrite does
  not truncate the data file. A save needs 94 KB on the card with no car
  (data 69 + icon.sys 1 + icon 20 + thumbnail 1 + directory 3 clusters).
- Ranking records: license 0x154 {s32 medal, char[16] name × 6, u32 ms × 6,
  u32 stamp × 6, 32-byte block × 6}; arcade time trial 0x2A4 {u32 ms × 12,
  char[16] name × 12, u32 stamp × 12 at +0xF0, 32-byte block × 12 at +0x120, s32
  best rank}; course time trial and machine test 0x300 {u32 value × 12, char[16]
  name × 12 at +0x30, u64 car code × 12 at +0xF0, u32 stamp × 12 at +0x150, 32-byte
  block × 12 at +0x180} (C: the accessors' strides, `0x212428` record 0x300; 44 + 3
  records: the course time trials in the gcf TIME_ATTACK list's order, §17, then
  M400, M1000, MMAX; the third machine-test record, top speed, is all zero in a new
  game). Empty: time 0x157529FF, the block 30 '-'. The block is written after the
  name entry by every driver (`fn_0012F3F0` → `fn_0021B5E8`: fields of the run
  bit-packed into 30 characters, I: the records page's ENTRY CODE): C for the 0x300
  records (+0x180, `fn_00212B40`) and the arcade ones (+0x120, `fn_00213620`; not a
  car name, the arcade records keep no car). Machine-test values: ms, or a speed as
  int(m/s × 1e5) (read × 1e-5). Stamps: seconds since 2000-01-01 00:00 on the PS2
  clock (`fn_0022BB08`; the records page converts them, §17). A new game seeds each
  arcade challenge i with the event's three times, names of one byte 0xFF − 3 i − k
  (the name marks of §16) and stamp 0x3264FD64 (the clock when the reference new game
  was made). Ours: the arcade, course time-trial and machine-test records are
  imported and exported with their blocks (`RankEntry::code`; ours make none: 30
  '-'); the licence records only as the best time.
- Championship block, new game: 8 bytes of 0xFF at +0x94 + 0x1D4 k (k < 8).
- Options (0x888, partly decoded): +0x00 screen (0 = 4:3); on/off switches
  (1 = on) +0x04 menu BGM, +0x08 menu SE, +0x0C race BGM, +0x10 race SE,
  +0x54 replay BGM, +0x58 replay SE (H: read by the menu BGM path 0x1D27F0,
  the race BGM switch 0x17E0D8; +0x14 / +0x18 are other words, 1 in a new
  game); +0x228 the music list, kept as the race playlist (0x410 bytes,
  music.md §2.1): u32 song count, u16 cursor, u8 shuffle, u8 no song listed,
  u16 seed, then at +0x238 + 16 k 64 entries {u64 id_hash("race_aNN.ads"),
  u8 played, u8 in the list, s16 order} (a new game lists the 25 race songs
  in a shuffled order, shuffle 1, seed 0x456: the shuffle of a fresh shared
  state's first draw, music.md §2.1); the button tables from +0x63C (M). A
  zeroed block loads: every sound off, an empty music list.

## 14. Car settings menu (`menu/setting.mbl`, SettingControl) (H unless noted)

- **Entry**: only from the pre-race quick menus (item `setting`, action
  `qm_setting`) of GT events, championships, machine tests and the arcade
  free time trial; it edits the race's player car (in GT mode the ride car's
  garage entry). `go_setting` exists in the GT-mode action table but no page
  uses it. Pages: `root` (change parts, settings, load, save, exit),
  `parts_setting` → `set/adjast_sus|brk|drv|oth` (status boxes) → slider
  pages; `change_parts` → `chg/change_*` (swap between owned parts).
- **Sliders** edit a working copy; ranges and initial values come from the
  committed car. Left/Right ±1, L1/R1 ±10 raw units, clamped (no wrap); SE
  "param" on a change, "disable" at an end or when refused. A change is
  refused when an event is selected and the car would fail its entry rules
  (gear sliders never refuse). OK (`commit_slider`) copies the working copy
  to the car; Back (`retrieve_slider`) discards it.
- **Focus**: a slider page opens on its first slider (item 0). The setting
  control has no focus hook and no focus memory, and on all 18
  `set/adjast_*_*` slider pages no item has a link (the sliders come first,
  then OK), so the page manager's default 0 applies (menus.md §3). OK is
  `commit_slider` with kind 3 (= back, like Cancel), so after the commit the
  page's back action `retrieve_slider` runs too, which copies the car just
  committed into the working copy (no change).
- **Markers**: `pinch` = current value, `pinch_prev` = value when the page
  opened (hidden after a commit), `pinch_fix` instead of `pinch` for a
  setting with fewer than 2 values. x = left end + (v − begin)·(W − w)/(N − 1).
- **Settings** (CarEquipments word ← range source in the fitted part record;
  display): spring 0x15C/0x160 ← suspension +0x21..22 / +0x24..25 (0.1
  kgf/mm, "7.2kgf/mm"); ride height 0x14C/0x150 ← u16 +0x50..52 / +0x56..58
  (mm); single damper level (sports / semi-racing kits) all four words of an
  axle ← steps +0x2B / +0x39; bound 0x16C,0x170 / 0x17C,0x180 and rebound
  0x174,0x178 / 0x184,0x188 (racing kit) ← steps +0x2B/+0x39 and +0x32/+0x40
  (front rebound has no physical effect in the original); camber 0x144/0x148
  ← +0x11..12 / +0x14..15 (0.1°); toe 0x154/0x158 ← +0x17..18 / +0x1A..1B
  (0.5° per step, 128 = 0); stabilizer 0x18C/0x190 ← steps +0x47 / +0x4B;
  brake balance 0x11C/0x120 ← brake controller steps +0x11 / +0x15; LSD
  initial / accel / decel 0x194..0x1A8 ← LSD +0x13..14 / +0x1D..1E, +0x16..17
  / +0x20..21, +0x19..1A / +0x23..24 (AYC = LSD stage 5 owns the rear initial
  word, range +0x1D..1E); gears 1–n 0xF4.. (×1000; full-custom gearbox only:
  final range u16 +0x26..+0x24 exclusive); auto level 0x114 ← +0x2A..2B;
  downforce 0x124/0x128 ← body +0x11..12 / +0x3A..3B ("0.66"); ASM 0x1BC ←
  steps +0x11 (shown as word − 1, 0x15EAA8: word 1 = "Level 0"); TCS 0x1B0 ←
  steps +0x12, reversed (Level 0 = off); VCD 0x118
  ← drivetrain +0x11..12 ("40 %").
- **Gear limits**: reference ratios g = the generator (physics §6.7) at the
  reference final EQ+0x1AC; gear 1 ≤ g1·9/8, top gear ≥ gn·7/8, neighbours
  meet at thirds ((2gi + gi+1)/3 and (gi + 2gi+1)/3), so gears never cross.
  Changing the auto level regenerates the ratios at the current final,
  clamps them into ranges from the old reference final, then the reference
  final follows the final drive. [C] The ranges live in the page's slider
  objects, all on the working car copy (control +0xE50, the auto level
  +0x114 and the reference +0x1AC included): built when the page opens
  (0x1654C8: 0x1E2C58 at the copy's level × 10 and +0x1AC) and rebuilt at
  every auto-level change (0x1662F0 → 0x1661B0: the ratios from 0x1E28A8
  at the new level and the current final, the ranges from 0x1E2C58 at the
  new level and +0x1AC as it is before 0x1662F0 sets it to the final; each
  gear slider gets its range, then its value, clamped, through its setter).
  So with a final changed since the reference was set, the first change
  ranges around the old reference, the next ones around that final.
- **Gear chart** (widget `gear_chart`, gt2k_menu::GearChart, ctor 0x1C62C8 / 0x1C5E50, handler
  0x15ECA8 = the control's car copy, none without a setting car) [C]. Its data (0x1E2538) is the
  spec builder 0x2325B8 over the car copy, the engine 0x1E10E8 (no power cap) and the driveline
  0x1E1C98 (physics §2.5), then 0x1E2450 per gear i = 0..n (D+2): low rpm = 1000 for i < 2, else
  D+0x30 + 2(i − 2) (the rpm after the shift into i); high rpm = the rev limit E+0x8C for i = 0
  and i = n, else the upshift rpm D+0x2E; each at speed rpm × 2π/60 (0x3503A0 / 0x3503A4) over
  D+0x5C + 4i (ratio × final / driven tyre radius; reverse negated), m/s. Every frame (vf2) the
  data is rebuilt and each gear word × final word (both / 1000, floats) compared with the last
  frame's: a change marks that gear for the frame (the constructor stores them unmarked). The
  draw (vf3), clipped to the box (0x1D1FF0): x = left + speed × W / 97.2222 (0x350078: 350 km/h,
  a fixed scale), y = bottom − rpm × H / (highest high rpm of gears 1..n); for gears 1..n (not
  reverse) a line from (low speed, low rpm) to (high speed, high rpm), 4 wide (0x23BEF0: a
  4-vertex strip, the ends shifted ±2·dy/length across and ±0.5 vertically), colour word
  0x8080FF80 (0x25A038, GS byte order R 0x80, G 0xFF, B 0x80, A 0x80: light green), a marked
  gear 0x80004080 (R 0x80, G 0x40, B 0: dark orange).
- **Locks** (status boxes open only when): spring/height/camber/toe ranges
  non-empty; damper: suspension stage 1–2; bound/rebound: stage 3;
  stabilizer, brake balance, ASM, TCS: more than one step; LSD groups: their
  ranges; gear: final min ≠ max; aero; AYC: LSD stage 5; VCD: range.
  Part-change items are enabled iff the car owns that goods (one bit test per
  item, 0x167840–0x1689F8); the part-change action (0x167038, every `set_*` but
  `set_brk_racing`) fits the part and plays SE 7 `ok`, or SE 4 `disable` when the setting car
  does not own it, nothing with no setting car (the items have no link: no confirm sound of the
  page's own); `set_brk_racing` (0x1670F8) only sets context +0x10BC = 1 and plays nothing; a change that breaks the selected event's rules
  shows `not_to_change`; tyre pages are closed for dirt events
  (`dirt_tire_only`). Port polish, balance, displacement and weight reduction
  have no `set_*` action (table 0x127BB0), so nothing takes them off.
- **Part rows** (`change_parts` layers m1–m7 and the `chg/change_*` pages:
  `status_parts_name_*` widgets, classes `parts_<cat>_<slot>`, one text
  routine each, factory 0x161930): with no setting car "----"; otherwise the
  name of the goods fitted on the control's car copy (+0xE50). Fitted goods =
  the category's stage→goods table at the part record's +0x10, the stage-0
  goods without a record (the same as `goods_id`). Name = realtime text group
  DATABASEPARTS (registered at 0x23B568: 80 slots, names 0x2F0F08, texts
  0x2F1048; key `DATABASEPARTS_PTEXT_*`) at slot u16 0x2EC3C0[goods]. The
  slot names, defaults and goods→slot table are the same in the NTSC-U, PAL
  (0x2F2538 / 0x2F2678 / 0x2ED9F0) and NTSC-J programs (0x2ED788 / 0x2ED8C8;
  goods→text pointers copied at start-up by 0x233190 into 0x2E8AF0). Stage-0
  goods read NORMAL "Normal", NOTHING "N/A", NO_EQUIP / NO_MODIFY "-";
  port polish and displacement fitted read MODIFY, engine balancing EQUIP
  (both "Installed"); NA tune, turbo and weight stages STAGE_n "Stage1"...
  Exceptions: **turbo** (`parts_eng_turbine`, 0x235B40): no stock turbo (the
  car record's own turbine record +0x11 = 0, 0x234080; a fitted kit does not
  count) and goods 78 → NOTHING; **intercooler** (0x235920, through 0x235378):
  no stock turbo → NO_EQUIP; **driveshaft** (0x235A30): drivetrain
  +0x14 = 1, 3, 4 (FF, MR, RR) → NO_EQUIP; **AYC** (`parts_oth_ayc_s`,
  0x15F9A8): LSD goods 35 → name(84) = EQUIP, else name(83) = NOTHING;
  **tyres** (0x15F6E8 / 0x15F848): when the current race's event record
  has +0x1D2 = 9 (dirt), owned goods 66 → its name (TIRE_DIRT), else
  "----"; otherwise the fitted front / rear tyre (stock 56 / 67). The NTSC-J
  disc's realtime.dat has 62 of the keys (no stage-suffixed NA / turbo /
  weight, no tyre names), so those slots show the program's defaults.
- Setting files ('S' type) copy a group only when the file's part for it
  matches the fitted part (else `load_warning`).

## 15. Records menu, attract mode (H unless noted)

- **Records** (`menu/records.mbl`, RecordsControl): opened from the race
  quick menus (`qm_records`) for the current event; one page
  `records_free_tt` with a code-drawn ranking (row template `record_bar`,
  hidden texts `record_rank`, `record_name`, `record_car`, `record_data`,
  `record_date`), a select panel (delete, entry code). Rankings live in the
  save: license tests 6 entries per test (times), machine tests 0-400 m /
  0-1000 m / top speed and the arcade time trials 12 entries each.
- **Top 3** on the time-trial (`qm_arc_tt_ltd`: `qm_rank_name1..3`,
  `qm_rank_time1..3`; the free time trial `qm_arc_tt_free`: `qm_list_car_name1..3`
  and `qm_rank_time1..3`) and machine-test (`qm_gt_mt_*`: `qm_rank_name`,
  `qm_rank_time`, MMAX `qm_rank_speed`) quick menus: the drivers read entries 0..2
  of the ranking record (time `0x213420`, name `0x2135E8`, a seeded mark resolved
  through the built-in name table; machine tests `fn_00212498` / `fn_00212528`,
  `fn_00212598`) before every quick menu, so the list shows before the first run and
  after each (C, time trial `0x130E48`). The tables always hold 12 entries (a new
  game's machine-test entries: no name, "no time" or speed 0, car 0, stamp 0), so the
  three rows are always filled: an empty entry shows no name and the time
  placeholder, or " 0.0 km/h". Speed (C, `0x12CD60`): "%4.1f %s" of the value in the
  Options unit (rounded; km/h, or mph = km/h × 0.62139), not the overlay's truncating
  `fn_0023BA08`. The free time trial's `qm_list_car_name<n>` (C, `0x12D040`) is the car
  name (US: car +0xE4) of its course ranking's entry n, "---" for no car; the
  name is not shown there. Each row is drawn in 0x800080FF when its list entry's flag
  is set; the drivers never set it for the Top 3 (presentation §B3).
- **Attract mode**: while the title page `root` is shown, idle frames are
  counted; past gcf section 0's first word × 60 frames (**20 s** on this
  disc, M: the word is read through the gcf object by the title loop) the
  menu ends with result 0. The main loop then runs a demo race
  (RaceArcadeDemo) from gcf section 12 (10 entries {demo event, course, …,
  level}; the index cycles; entries with level 1 — the two F1 demos — are
  skipped unless the S license is held), up to five in a row; entry words
  {+0 event, +4 course, +8 car label, +0x10 level}: the runner (`0x100A78`)
  stores the hash of the +8 label as the demo information's player car
  (+0x258); every demo on the disc has "" there (hash 0), so
  `RaceArcadeDemoInformation::vf12` calls `RaceInformation::vf14` with
  first slot 0: **all slots 0 .. cars − 1 come from the pool draw** (§20.4)
  (C for the code path; the field itself not compared in a capture).
  A demo's **time limit** is section 0's second word (`fn_0021D2B0`; section
  0 is these two words): **105 s** in the product gcf of every release (20,
  105 on NTSC-U, PAL and NTSC-J; `demoplay.gcf` 10, 10; `product_test.gcf`
  20, 90) [C]. The runner stores it at race info +0x50 (the Preview's runner
  0x100538 stores 0: no limit), `fn_001798A0` copies it to organisation
  +0x64, and `fn_0017A578` multiplies it into the countdown +0x68, armed
  (+0x60) when not 0: × 60, × 50 in the PAL program, which steps at 50 Hz,
  so it is the same time. `fn_0017A5E8` counts down every frame once the
  start hold is over (the demos have none: +0x1F2 and +0x1F3 are 0) and
  posts event 7 at 0x17A764; `RaceArcadeDemo::vf28` turns event 7 into
  `vf35(5)`: exit reason 5, and the race ends. The runner returns 0, the
  next demo, for reason 5 alone; anything else returns 1, the title: Start
  (event 1, play mode 2, `RaceBasic::vf29`) or the race's own end, 3 s after
  car 0's last lap (`fn_001F2AE8(…, 3)`: 181 steps under flow byte 2, then
  event 1 from `fn_001F2B18`, reason 0). All ten demos have 20 laps, so the
  limit comes first. `RaceArcadeDemo::vf29`'s counter +0x34580 is no demo
  limit: 1800 steps, reset every frame outside play mode 0 (in mode 0 by any
  button but the two masked ones), then event 1.
  Seen in PCSX2 (US 1.10, no memory card, 2026-11-04): idle on the title, then
  `demo_swiss`, `demo_r246`, `demo_wet`, `demo_stl` with no title between them
  (section 12's order from entry 1, the F1 entries skipped). First picture to
  freeze: 6300 frames at 59.94 Hz (105.105 s of video) on both demos measured
  whole, the last frame then held 11 frames, the wash, about 8 s of black.
  After five complete demos `stream/opening.pss` plays and the title returns.

## 16. Arcade progression (H unless noted)

The arcade state lives in the same game state (and save) as GT mode.

- **Course lists** (gcf tag 8): one list per arcade game, `SINGLE_RACE` (34
  courses), `TIME_ATTACK` (free run, 36), `TWO_PLAYER` (35), `I_LINK` (36).
  Entry: {u32 name offset, u8 selectable (1), u8 1, s16 **unlock level**
  0..5, u32 length (m)}; a course has the same level in every list. Groups
  of `SINGLE_RACE` (its index is the result slot): level 0 = `super`,
  `midfield`, `smtnorth`, `smtsouth`, `trialmt`, `r_midfield`; level 1 =
  indices 6–11; 2 = 12–17; 3 = 18–23; 4 = 24–28 (with `montecarlo`); 5 =
  29–33. `slalom80r` (4) and `r_slalom80r` (5) are only in the other lists
  (they open with the level but can never be won).
- **Course level** (scalar +0x3C, 0 on a new game): every course picker
  (Single Race, Free Run, 2 Player, i.LINK) offers a course iff its level ≤
  the course level. Locked courses are skipped by the left / right
  movement, not shown greyed (M for the look).
- **Car classes** (gcf tag 14): `CLASS_C`, `CLASS_B`, `CLASS_A`, `CLASS_S`,
  `CLASS_DIRT`, `CLASS_COMPULSORY` (time trial), `CLASS_PREVIEW` (attract
  list). Entry: {u32 label offset (table-35 arcade car), u32 race-code
  offset (only `CLASS_COMPULSORY`: `AT010001`…`AT100001`), u16 enabled (1),
  s16 **unlock level** 0..12, s32 race-code variant (added to the last
  character of the class code)}. The home / guest garage classes have no
  list and no locks. A car of level k needs car flag k (level 0 = always).
  A car is also hidden when its label is in neither the arcade-car (35) nor
  the car-variant (31) table of the region (NTSC-U: `vauxhall_vx220_00_147`,
  `vauxhall_astra_race_00_461`; the Opel twins remain); the original also
  tests the base car's region byte (H, `0x23ABF0`): the base car is the arcade-car or
  car-variant record's +8, else the label's own id; a label with no car (table 30) record is
  hidden; otherwise car +0xF7 decides, by the language index (§21; `0x23AC80`): 0
  everywhere, 1 British English only, 2 not British English, 3 US only, 4 not US, 5 the
  European languages (BR, FR, GR, IT, SP) only, 6 not those, 7 JP only, 8 not JP, > 8
  everywhere (e.g. `viper_gtsr_oreca_00` 4, the Opels 2, `vauxhall_*` 1, `s2000_e_99` 5,
  `diablo_gt_00` 7).
  The class lists of this disc are not affected beyond the Vauxhall labels. Open at the start on NTSC-U:
  C 10/11, B 10/12, A 10/14, S 11/21, Dirt 9/10.
- Locked cars: level 1 `arm_roadster_93` (C), 2 `arm_tt_00` (B), 3
  `arm_rx8_01` (A), 4 `arm_z_detroitver_01` (A), 5 `arm_r_gt_00`,
  `arm_tuscan_speed6_00` (A), 6 `arm_viper_gtsr_00`,
  `arm_cerbera_speed12_00` (S), 7 `arm_razo_silvia_01` (S), 8
  `arm_delta_hf_integrale_wrc_00` (Dirt), 9 `arm_vertigo_97` (S), 10
  `arm_zonda_race_00` (S), 11 `arm_panoz_esperante_race_98`,
  `arm_r390_race_98` (S), 12 `arm_gt_one_race_99`, `arm__787b_91` (S).
- **Class page** (`ar/select_class`, not progression-dependent): classes
  C..S are refused on a dirt course (database course kind 2) with
  `disable_road_doc`, Dirt on a road course with `disable_dirt_doc`, the
  home / guest garage classes when they hold no car with
  `disable_no_car_doc`. A garage car on a dirt course is refused unless it
  owns the dirt tires (owned-part bit 0x42).
- **Difficulty**: Easy, Normal, Hard are always offered (`ar/level`).
  **Professional** has no unlock: Hard confirmed while L1 and R1 are held
  (the Hard button shows the "pro" art while both are held). The page
  focuses min(last difficulty, Hard).
- **Results**: only Single Race records (Free Run, 2 Player and i.LINK
  never do). Race code `A{E,N,H,P}{C,B,A,S}T000n` / `A{E,N,H,P}AD0001`; its
  2nd character is the difficulty (E 0, N 1, H 2, P 3). Per (difficulty,
  course) the best place is kept (block of 144 × 0x14 bytes, index =
  difficulty × 34 + course, place at +0x10, 0 = won, 8 = none; the four
  time words are never written). Every difficulty and every class (garage
  cars too) records; the class is not part of the key. A win (1st) adds a
  trophy and sets the difficulty's award bit.
- **Unlock pass** (only when a record improves): (1) while every course of
  group L = course level has been won at Easy, Normal or Hard, L += 1 (up
  to 6; several at once possible; Professional wins do not count); (2) for
  each group g: all its courses won at exactly Normal → car flag g+1; (3)
  all won at exactly Hard → car flags g+1 and g+7. Flags are never cleared;
  a group won partly at Normal and partly at Hard sets neither.
- **Clear Status** (`ar/goodies`, the only page that reads the results):
  per course (widgets `course_logo_NN` / `course_name_NN`) the course's
  `<course>_mini` / `<course>_doc` art once it is open; `mark_easy_NN` →
  `clear_easy` if won at Easy, Normal or Hard; `mark_normal_NN` →
  `clear_normal` if won at Normal or Hard; `mark_hard_NN` → `clear_hard` if
  won at Hard; `mark_pro_NN` never. `prize_<diff>_a..f`: `prize_item_<diff>`
  when every course of group a..f is won at that difficulty or harder.
  `end_button` / `end_message` once a GT beginner, amateur or professional
  league is complete or the course level is 6. Item `Goal` → `ar/select_ending`,
  redirected to `ar/cannot_ending` unless a GT beginner / amateur / pro
  league is complete or all 34 courses are won at exactly one difficulty
  (Easy, Normal, Hard or Pro). On `ar/select_ending` the short ending
  (`go_ending_easy`) is always offered; the long one (`go_ending_normal`)
  needs a complete amateur or pro league, or all courses won at exactly
  Normal, Hard or Pro (H: `0x1031D8` registers the page's redirect and an enable
  predicate per item, item 0 `easy` always, item 1 `normal` `0x103040`).
- **Completion** (status page arcade term): Σ over the 34 courses of [won
  at E, N or H] + [won at N or H] + [won at H]; max 102.
- **Time Trial** (`ar/select_compulsory`): the 10 `CLASS_COMPULSORY` cars,
  all open from the start. A run is one timed lap (the AT events' laps byte 2 is
  never read; presentation.md §B3); its time is that lap. Rankings of 12 times per challenge (0x2A4-byte
  records: times, names, stamps, car names, best rank at +0x2A0, 8 =
  none), seeded on a new game with three records from the race record
  (table 32) +0x1C4 / +0x1C8 / +0x1CC, each {u8 min, u8 s, u16 ms} (AT01
  1'25.553 / 1'26.952 / 1'27.355 … AT10 4'52.716 / 4'57.795 / 5'07.919),
  dated at the new game, car name the placeholder of 30 dashes, and one-byte
  name marks. The seeding (CORE 0x2139D0) clears the ten records, then for
  challenge i = 0..9 (codes from the table at 0x2E10F8) inserts the three times
  in turn, each before the first time that is not faster (0x213430), and names
  each with a single byte that starts at 0xFF and is decremented after every
  record and not reset between challenges: record k of challenge i gets
  0xFF − 3 i − k (AT01 0xFF / 0xFE / 0xFD … AT10 0xE4 / 0xE3 / 0xE2; checked
  against a new game saved by the original). Display (0x2135E8): the name's
  first byte is read signed; when negative the shown name is entry ~byte of a
  table of 30 pointers to short "X.Surname" strings in the program's data
  (0x2E1080, 0x213210; no modulo or bound), so challenge i's records show
  entries 3 i, 3 i + 1, 3 i + 2 — three built-in names per challenge. The
  reimplementation reads that table from the disc's CORE.GT3 at load and shows
  "-" for a mark it has no entry for (no table, or a byte below 0xE2).
  A new time's rank = the number of times strictly faster; rank ≥ 12 is not
  entered; best rank = minimum; then name entry. **Cleared** = best rank 0
  (faster than the fastest record). All ten cleared →
  `arcade_compulsory_complete`: the GT-mode prize car
  `panoz_esperante_race_98` (once).
- Prize sets `arcade_{easy,normal,hard,pro}_complete` exist only in
  `product_test.gcf`, not in the retail `product.gcf`: never awarded on
  NTSC-U.

## 17. Record name entry (H unless noted)

A run that makes a ranking (license test with a medal, machine test, time
trial) runs the name entry right after the run, inside its driver, before the
quick menu comes back (C: licence `fn_00132030`, machine test `fn_00131410`,
time trial `0x1310FC`; presentation §A8, §A8b): every qualifying run of a
visit, not once per visit. `0x128540` runs `menu/name_entry.mbl` page
`name_entry` (`fn_001C1B88`) on a buffer: starting text = GameStatus +0x78 (the
last name entered, save scalar +0x70, §3; empty in a new game), at most 15
characters; on OK the buffer is copied to +0x78 and stored at the new entry's
rank (machine tests `fn_00212558`). The page: a text box (widget `text_box`,
TextBox) above `free_cursor` (the character grid; each child has action
`input_text`, its character the item's text in `name_entry1.pmb`) and
`free_command` (OK). TextBox (C, `0x1C3990`, vf2 `0x1C3CB0`, input `0x1C40F0`):
- the text is up to 128 characters with a cursor (0 .. limit, clamped); it starts
  at the end of the starting text;
- `input_text`: the key's first character (a 2-byte character when its first
  byte has bit 7; an empty key text gives a space) goes in at the cursor and the
  cursor moves right, only while the text is shorter than the limit (SE 3
  `cursor`; nothing when full);
- Back Space (logical key 0x80 = Triangle / Square): the
  cursor steps left (clamped) and the character at it is deleted, so at the start
  the first character goes; SE 3 when the text was not empty;
- L1 / R1 (logical 0x2000 / 0x10000, auto-repeating 30 / 3): the cursor left /
  right over the text (right only while there is a character at the cursor);
  SE 3 when it moved;
- Start (logical 0x10): the runner `fn_001C1B88` sets the page's focus to item 6,
  the OK row (`fn_001C9520(page, 6)`);
- cancel never leaves the page; OK on an empty box is allowed and stores "".
- Drawing (`fn_001C3EF0`): the caret is the page's hidden `cursor` element placed
  at the text origin + the width of the text before the cursor (vertically the
  text's middle); without that element a white 8 × 8 box.
The pad mapping (C, `PSPort::vf2`): Up 0x1, Down 0x2, Left 0x4, Right 0x8, Start
0x10, Select 0x20, Circle / Cross 0x40, Triangle / Square 0x80;
`MenuControl::vf10` adds L1 0x2000, L2 0x4000, L3 0x8000, R1 0x10000, R2 0x20000,
R3 0x40000. The memory card's file-name entry (`fn_0014A108`) runs the same page
and TextBox with 16 characters.

**Records page dates** (C, `RecordsButton::vf3` 0x173D20): every row of the table
is drawn (12; a licence test's 6), an empty one with no name and the value
placeholders (time `\x7F\x7F'\x7F\x7F.\x7F\x7F\x7F` from `fn_0023B930`, speed
`\x7F\x7F\x7F.\x7F` + unit). `record_date`: the entry's stamp (arg vf6) through
`fn_0025BB60` (seconds since 2000-01-01 → BCD clock: days in four-year cycles of
1461 from a leap year; the leap months table only when the year is 2000 itself,
later leap years use the common one; then `fn_00274F08`, I: the SDK's RTC → local
time) and `fn_0023BAF8` "%02d/%02d/%02d": US day / month / year, JP year /
month / day; stamp 0 → `\x7F\x7F/\x7F\x7F/\x7F\x7F`. When the arg's vf1 is 1
(machine tests, course time trials; not licences or arcade challenges):
`record_car` = the car's name (US +0xE4) + " " + its second name (US +0xE6),
empty for car 0 (`fn_00171CD0`), and `record_car_pict` (records1.pmb image 4, a
car icon and a colon) on every row. A new game stamps the arcade seeds with the
clock (the reference save: 0x3264FD64 = 2026-10-16 15:21:08).

Ours: `RaceScreen::record_run` + `game::add_*_record` (one name entry per ranking
run, before the quick menu; nothing is recorded from the result the race posts when it
is left, as no menu of the original reads one into a ranking), `menu::flow::TextBox`, `GtMode::add_record` (ties
ahead), `RankEntry::stamp` (local clock; seeds made by us carry none),
`record_date_text`. Not done: the records page's list does not scroll (the disc's
window shows 6 of the 12 rows).

**Course time trials** (C unless noted). The free-run driver `fn_0012FF48`
serves both the arcade time attack (`fn_00100DB8`, desc +4 = the arcade course, GT flag 0)
and GT Mode's free run (`fn_00100738`, the free-run course of the home page, the ride car,
GT flag 1); it runs `RACE_MODE_TIME_ATTACK` races and the `qm_arc_tt_free` quick menu.
- The table: GameStatus +0x26160, 44 records of 0x300 (§12); a course's record is its
  index in the gcf tag-8 list "TIME_ATTACK" (36 courses: super 0, midfield 1, … laguna 9,
  … r_slalom80r 35; `fn_00212EB0` → `fn_0021D630`, a string compare of the course name;
  a course not listed gives -1, i.e. the record before the table: never the case for the
  courses offered). Slots 36..43 are unused. GT Mode's six free-run courses (laguna,
  midfield, apricot, slalom80r, testcourse, smtsouth) are all listed.
- After every run (`vf42` returns, however it ended: Exit from the pause menu; no
  finished check), the car's best lap (+0x16C4, ms) is inserted when it is not "no time"
  and at most 0x3FFFFF (`fn_00212F30` → `fn_00212928`: the first place empty or not
  better, ties ahead, 12 places, the times shifted); a rank >= 0 runs the name entry at
  once (`0x130764`, as §17's machine tests), then stores the name (`fn_00212F90` →
  `fn_002129D0`: the names shifted, the new one cut to 15 characters, the stamps shifted
  and the new one = the clock), the car code (`fn_00213000`: desc +8's CarEquipments
  +0x10, a car-table code: the ride car / the arcade car's base car) and the block
  (`fn_00213070`). Then the driver plays the run's replay (`vf42(race, 1, 1)`) and loops
  to the quick menu.
- The quick menu (`qm_arc_tt_free`), read before every menu: `qm_list_car_name1..3` = the
  car names of entries 0..2 ("---" without a car), `qm_rank_time1..3` = their times; no
  names (§15). Its Records item opens `RecordsMenuArgFreeTT` (the course's 12 places;
  vf1 = 1: the car and its icon, §17 records page). Nothing else reads the table except
  the free-run result overlay below.
- `ResultFreeRun` (vtable 0x326C60): `ResultImplementRecords` with the rank (vf9 →
  `fn_00212F00`) of the car's best lap in the course's table, shown after 0.1 s (not 5 s)
  for 15 s or until Circle / Cross. Like every result it starts on the player's finish
  event (5, `RaceResultBase::vf4`); no free-run path found posts one (the free-run
  conductor's lap handler `DynamicsConductorFreeRun::vf18` does not), so it is not
  reproduced (I: never shown).
- The free-run HUD (`RACE_MODE_TIME_ATTACK`, mode 6): "Record Lap" (84), "Best" (112), the
  lap-times list (140), "Lap" (216). "Record Lap" (like "Record", "Fastest") is the display
  object D+0x17C, whose value is the race's best-lap reference R+0x8BC every frame
  (`RaceDisplay::vf3` 0x192ACC): "no time" when the race is set up (`fn_0017B778`, once
  per visit), then every faster allowed lap (`fn_0017B810`). The course table is never
  loaded into it (its setter 0x17B258 is a command-table method with no caller found), so
  it shows the best lap of the visit so far. Ours: the free run's HUD keeps "Best" /
  "Time" (not this layout), and the time-trial gap pop-ups of a free run start without a
  reference, as the original's.
- Ours: `GtMode::record_course_time_trial` ("ct:<course>"), the race screen's
  `record_run` at the exit transition (`course_time_trial()`: modes `free_run` /
  `go_free_run` from the menus), the Top 3 and the records page; the PS2 save keeps the
  table (§12). Not reproduced: the replay the driver plays after each run (also after the
  machine tests' and the time trials' runs), the mode-6 HUD layout.

## 18. License card and championship trophy presentations (H unless noted)

- **license_get** (`menu/lc_get.mbl`): the license index 0..5 (B, A, IB, IA,
  S, R) selects `menu/lic_{b,a,ib,ia,s,d}.prm` (`lic_d` = rally). Timeline:
  layer `<x>_mess0` for 4 s, then hidden; `<x>_mess1` 2 s later; OK
  accepted 1 s after that (about 7 s in). The card fades in from 2.0 s to
  3.0 s; its animation starts with the fade.
- **trophy_get** (`menu/trophy_get.mbl`): shown after every championship
  win, before the prize car. The trophy is not data-driven: a weighted
  random pick (§20.3) over `menu/champ_00..09.prm` with weights 400, 350, 250, 450,
  2, 160, 1, 500, 25, 120, each plus 165.333 × c³ (c = the completion
  ratio of the status page); the pick sets bit 0x40 << i of scalar +0xC0
  (see +0xC0). Page: background, `count` / `count_shadow` (the credits
  counting up by the bonus), `get_mess` box with what is left of it
  (`get_money_disp`), `opening_mess`. These are the race prize pages'
  PrizeCounter items ([presentation.md §F](presentation.md)): "Cr. 8,540"
  and "Prize Cr. 20,000", the count starting **360 frames** after the page
  opens (the cups: 120). Timeline (TrophyGetControl `0x13AB88`): `mess0`
  for 180 frames, 90 more, `get_mess` shown, 60 frames, then wait for the
  count; 15 frames after it `mess1` (which holds the OK button). The bonus
  enters the credits and the trophy count +1 when the page closes.
  The trophy fades in from 3.0 s to 4.5 s.
- Both draw the model full screen with the .prm's own camera, lights and
  animation; the `render_model` widget only sets the draw order (M). The
  trophy item draws 160 pixels left of centre (M). Each frame is rendered
  4 (trophy) or 6 (license) times with sub-pixel jitter and averaged; with
  header +0x38 (focus distance A) > 1 the camera also moves on a circle of
  radius +0x3C (B) per pass with the projection keeping the plane at
  distance A fixed: a depth-of-field blur (race prizes have A ≤ 0: none).
  License cards draw their shadow node; the trophies have none.

## 18b. Race prizes and the full-garage notice (H, docs/specs/presentation.md §F)

After a GT race with prize money the original shows the event's cup (`gtmode_mini`, places
1-3, `race/g_<class>_<medal>.prm` by trophy class +0x1D3 5..8) or the consolation prize
(`consolation_prize`, `race/consolation.prm`, places 4-6), each counting the money into the
credits, **before** the other presentations (licence card, championship trophy, prize car).
An arcade single-race win shows `arcade_mini` with `race/a_<difficulty>_gold.prm`. A prize
car that does not fit in a full garage shows `not_car_get` ("Garage is full"). Licence
tests ask for the record name right after each run with a medal (presentation.md §A8).

## 19. Trade, guest garage, memory-card pages, setting files (H unless noted)

- **Guest garage**: a second, complete game state (same class as the main
  one), reset to a new game at boot (0 cars). Loading a guest file reads a
  friend's normal game save in full into it; nothing is copied into the main
  game and the guest state is never saved. The arcade "Load Garage" and the
  home "Trade" share the one guest state.
- **Trade** (home item `sl`, action `go_trade`): memory-card load of the guest
  file (page `gameload_g`); after a load the GT-mode menu runs from
  `hm/hm_gr_trade` (back = "" ends the session at `hm/hm`, M for the key);
  without a load back to the home page. The list shows every guest car (no
  filter), sorted with the trade sort panel (7 keys: no drive / aspiration).
  `set_garage_car_trade` makes the row's guest car the focused car;
  `hm/hm_car_trade` shows it with the full catalogue price (`car_buy_price`).
- **buy_guest_car**: guard on entering `hm/hm_buy_sure`, in this order: the
  model's availability byte bit 1 (21 labels on the disc, e.g. `s2000_99`,
  `roadster_sspl_96`, `stratos_00`) → `00cmn/not_for_buy`; credits < catalogue
  price → `00cmn/no_money`; 200 cars → `00cmn/max_car`. No duplicate test.
  The action copies the whole guest car (parts, settings, owned bits,
  odometer, wear, oil, dirt) into a new garage slot with the **stock wheel**
  (the wheel is not in the buyer's inventory), a new acquisition serial, and
  takes the **full catalogue price**. The friend's car stays (a copy); the
  guest file is untouched; no autosave in the original (M).
- **Arcade guest class**: lists the guest garage's cars; refused with
  `disable_no_car_doc` while it has none (the boot state).
- **Memory-card slot pages** (`menu/memorycard.mbl` gamesave / gameload /
  gameload_g; the .mbl pages have no items, the code builds them from the pmb
  layers): two card icons (the focused one is the plain art, the other the
  `_sel` art, from the reference captures), "Select MEMORY CARD slot"; the
  last slot used is remembered per purpose (game save/load, arcade guest,
  trade). Choosing a slot opens a confirm window: load → "Load game?" /
  "Load Garage data?" + OK / Cancel, or "Cannot find GT3 data" + OK; save →
  "Save game?", or "GT3 data already exists" + "Overwrite OK?", or "FORMAT AND
  SAVE GAME" on an unformatted card; then the working window ("Loading..." /
  "Saving . . .", the warning line, a progress bar), "Complete" and an OK
  button 4 frames later. Load errors: "Loading failed" + "File is corrupt"
  (CRC / decode) or "INCORRECT DATA" (major ≠ 7); write error "Error!". The
  windows are placed by code: x 28, bottom y 160 (matches the reference
  captures of `gameload_g`). Title Save/Load (`go_save_game_menu` /
  `go_load_game_menu`) and the home Save (`go_save_in_home`) use these pages.
- **Setting files** (Load / Save Settings): the file list page
  (`file_operation`, title "Load settings" / "Save settings"), one 4 × 3 icon
  grid per card slot. Load lists only the setting files of this car model,
  newest first (M: the sort key is read as the modification time); the
  player picks one (Load → "Load?" → "Loading..." → "Complete"), the game
  never auto-picks. Save offers "Save" on the empty entry (a new file each
  time, several per car accumulate) and Overwrite on a file. Delete / Copy /
  Edit file name exist in the same menu. Info panel: Car (maker + name), File
  Name (comment), Date, Mode, Power, Size.
- **File Operation** (title item, and START on the Save game slot page): the same
  file list with every GT3 file of both slots (title "FILE OPERATION"); the
  operation menu of a file offers Copy (to the other slot; "Same file already
  exists" when the target holds one), Delete and Edit file name. Edit file name
  runs the `name_entry` page ("FILE NAME", 6 rows of characters, Back Space on
  Triangle / Square, `ok`); the name is the 16-character comment of the file
  (setting files) or of the game file's thumbnail. Confirm "Save edited file name?",
  then "Saving . . .".
- Operation menu (layers by file kind; buttons top to bottom at x 116, 32 px apart):
  op1 (a file of the mode's own type, not in File Operation): Load only when
  loading; Overwrite, Copy, Delete, Edit file name when saving. op2 (other GT3
  files): Copy, Delete, Edit file name. op3 (game saves, other PS2 / PS1 files):
  Copy, Delete. op4 (the empty entry, first in save modes): Save. The window is
  centred on the focused icon and kept inside that slot's 284 × 214 icon area.
- Copy goes to the other slot; a file of the same name there gives "Same file
  already exists" / "Overwrite OK?" and then overwrites; an unformatted target
  "FORMAT SLOT n AND COPY".
- Free-space line (`pt_free_slotN`): "" without a card, "Unformatted", or
  "<n>KB Free" with n = free KB − 2, comma-grouped (an empty 8 MB card:
  "7,998KB Free"). The slot bars and icon grid are drawn at full brightness for
  the slot whose list has the cursor and at 30 % for the other; icons of files
  that do not apply to the mode at half brightness. The cursor is the white
  64 × 64 quad added with alpha 0.25 × a sawtooth falling from 1 by 1/30 per frame.
- The prize presentation's car (car_get) uses camera record 0
  (`menu/showroom_single.fbi`).
- Game file info (`info_game`): License (highest held: B, A, IB, IA, S), Number of
  cars, Game Status "%d.%01d%%", Size "NKB", Date, Day, Money.
- Reimplementation: no memory cards. The saves are native files (any number,
  `<user data>/saves/`), listed on this page's art without slots or "working" /
  "Complete" windows (docs/dev/GT_MODE.md "Saves"); setting files live inside the
  current game's save (with write time, power and name); the Size shown is the
  native file's. The operation window is placed beside the focused icon and the
  icon cursor is a pulsing highlight (M, not in the captures).

## 20. Random numbers (H; C = verified by executing the original)

### 20.1 The generator (C)

One algorithm with several independent 32-bit states:

- **step**: x ← 17·x + 17 (mod 2³²); the output word is x XOR rotl(x, 16).
- **unit**: the low 23 bits of an output word as the mantissa of 1.m, minus
  1: a float in [0, 1) in steps of 2⁻²³ (exact).
- Helpers on a state: `range(lo, hi)` = lo + unit·(hi − lo); `chance(p)` =
  unit < p.
- Every state that starts with the program starts at **0x87654321**
  (static initializers; the camera objects, the camera manager and the
  replay director seed their own copies when they are built).

States (program addresses for reference):

| State | Users |
|---|---|
| shared (`0x3532E0`) | its integer draw int(unit·2³¹) feeds the GT-mode stir (§20.2); the race BGM pick (`0x21ABB0`); the car_get carousel's headings (n + 1 unit draws, §10.1) |
| race module (`0x351DE0`) | the opponent pool draw (§20.4) and the paint colours of the same race (docs/dev/RACE.md "Paint colours"); never reseeded, so it runs on across the races of a session; the i.LINK battle overwrites it with the session's shared seed |
| GT mode (`0x353250`) | §20.2–20.3; saved in the game state at +0xCC |
| per object | cameras, replay director (each from 0x87654321) |

The C library `rand()` (newlib, 1103515245·x + 12345) is used only by the
BGM file pick; the i.LINK battle has its own state (`0x352CA0`).

### 20.2 The GT-mode state (H; stir C)

- **Start-up**: x = the COP0 Random register (0..47, counts down every CPU
  cycle), then one step. Effectively unpredictable.
- **Load**: a save whose header version word (u16 minor | u16 major << 16)
  is greater than 0x00070000 (retail saves are 7.1) restores x from +0xCC;
  older saves keep the running state. **Save** writes x to +0xCC. A new game
  does not touch it.
- **Menu frames**: the menu controls' page loops (two shared loops, M: the
  GT-mode / arcade / other top-level controls) and the trophy_get / car_get
  pages step x once per frame, so the state depends on how long the player
  spends in the menus.
- **Stir**: the trophy_get and car_get controls step x n times when they
  open, n = (COP0 Random + the shared state's integer draw) mod 13, n = 0 →
  1 (1..12 steps).
- **Draw**: skip n − 1 steps, then unit (every caller uses n = 1).

### 20.3 Draws on the GT-mode state (C)

PS2 float arithmetic (multiplies and adds round towards zero, physics.md §6.7).

- **Trophy** (trophy_get, §18): after the stir; extra = ((c·165.333)·c)·c
  (c = completion ratio, constant word 0x4325553F); weight_i = table_i +
  extra if extra > 0, else table_i + 0; total = Σ weight_i in order;
  x = total·unit; the trophy is the first i with x ≤ the running sum of
  weights 0..i; none → trophy 0. (A forced index < 10 skips the draw.)
- **Prize cars** (car_get, §10): one control for all the prize sets of a
  result, opened with the stir. For each set in order (a full garage skips
  the set without draws): every candidate in order gets its paint — its
  colour id when that id is in the model's colour list, otherwise
  index = int(count·unit + 0.5), index ≥ count → 0 (count = the model's
  colour count; one draw) — then the candidate is picked: weights
  w_i = max(float(weight_i), 1); total = Σ w_i; x = total·unit; the first i
  with x ≤ w_0 + … + w_i; none → 0. Single-candidate sets draw too.
- The car_get page places its cars at headings drawn from the shared state
  (§10.1; presentation only). The page's roulette lands on the candidate picked
  here, and that is the car added.

### 20.4 Opponent pool draw (C)

Used by every race that fields cars from its event (GT-mode races, the
first round of a championship (later rounds reuse the stored field),
arcade Single Race, the attract demo), on the race-module state, before
the paint colours:

- Pool = the event's entries (+0x10) up to the first `-` (at most 32);
  n = pool size. An entry is **taken from the start** when it cannot race
  in the region: its label resolved through the arcade-car (35) and
  car-variant (31) tables (+0x08 each) to a car (30) record, missing → taken;
  else the car's byte +0xF7 against the language index (§21): 0 any, 1 / 2 only / not
  British English, 3 / 4 only / not US, 5 / 6 only / not the European languages (BR, FR,
  GR, IT, SP), 7 / 8 only / not JP, > 8 any. On NTSC-U no pool entry is region-locked.
- left = n (not reduced by the entries taken from the start). For each car
  slot to fill, in slot order (GT mode and arcade: slots 1 .. cars − 1;
  the attract demo from slot 0 when its info has no player car — always on
  this disc, §15 — else from slot 1 (C)):
  k = int(unit · float(n)); if left > 0, k moves forward (wrapping) to the
  first entry not taken, which is then taken, left − 1; otherwise k is used
  as is (repeats once the pool is used up).
- The car build (`0x238550`, from vf13 `0x179D70`) gives the slot's car entry k's colour id
  (+0x110 + 4 k, H: `0x2385BC`), so a fixed paint follows the pool entry. Then the paint draw
  (`0x176FD0`) fills every entry still at 0xFFFFFFFF, in entry order: in the attract demo and
  the Preview (no player car) entry 0, the followed car, as well (H: the loop starts at entry 0
  and tests only +0x08). No GT-mode or arcade event
  sets one on the US, PAL, NTSC-J or GT3 Concept mod data; 31 licence tests and the course / F1
  attract demos do (US: demo_tm, demo_stl, demo_ss11 with one entry unset, demo_f1_monac,
  demo_f1_stl).
- With region-locked entries and more cars than free entries the original
  loops forever (never happens with the disc data).

## 21. Language rules (H; NTSC-U addresses, PAL the same code)

The value older notes call the "region index" (US `0x352FD8`) is the game's **language
index**: 0 JP, 1 US English, 2 British English (BR), 3 FR, 4 GR (German), 5 IT, 6 SP. The
NTSC-U program runs in 1, the NTSC-J program in 0, PAL in 2..6 (chosen at the first start).
Predicates: `0x231680` (== 0), `0x231690` (== 1), `0x2316A0` (== 2), `0x2316B0` (≥ 2, any
European language). Besides the menus, units and money (disc/region.h), it decides:

- **Region byte** (car +0xF7, `0x23AC80`, returns "not offered"): §16, §20.4, and the licence
  car below.
- **Prize-car labels** (`0x21D8C0`, called by `0x21DB60` from the car_get control `0x13EF40`
  for every candidate of the set before the roll; `0x215DC8` also calls it but has no
  callers): a static table of {from, to, mask}, 8 entries in this order, built in code on
  first use: `opel_astra_race_00` → `vauxhall_astra_race_00`, `opel_speedster_00` →
  `vauxhall_vx220_00`, `opel_calibra_race_95` → `vauxhall_calibra_race_95` (mask 0x04);
  `viper_gtsr_oreca_00` → `dodge_viper_gtsr_oreca_00`, `s2000_99` → `s2000_a_99`,
  `s2000_99_ext` → `s2000_a_99_ext` (0x02); `s2000_99` → `s2000_e_99`, `s2000_99_ext` →
  `s2000_e_99_ext` (0x7C). The first entry with bit (mask >> language) & 1 whose label equals
  the candidate's (strcmp) replaces it. A replaced candidate gets **no colour id** (−1, a
  random paint, §20.3); its weight is unchanged. Japanese changes nothing. On the discs:
  NTSC-U swaps 2 candidates (`gtmode_amateur_complete` Viper Oreca → Dodge,
  `Gtype_r_n_0500` S2000 `_ext` → `_a_`), PAL BR 3 (the two Opel touring cars and the S2000),
  FR/GR/IT/SP 1 (the S2000). This covers every tag-15 set, `gtmode_complete` included (no
  table label there).
- **Dealer buttons** (`DealerButtonList::vf7` `0x11B138`, the cd/ region pages): with
  language 2 an item named exactly `opel` gets no button, with any other language an item
  named `vauxhall`; the following buttons take its place (the list's running offset only
  advances for built buttons). NTSC-U and PAL `gt_mode.mbl` name those items on `cd/cd_ger`
  and `cd/cd_eng`; the NTSC-J menus have no `vauxhall` item and call Opel `dealer4`.
- **Licence car** (licence driver `0x132030`): the event's entry 0 is loaded with its slot-0
  colour (`0x238550`: event +0x10 + 8·slot must be a car-variant record, colour +0x110 +
  4·slot). With language 2, if that car's region byte says "not offered" (`0x236A38`), entry
  1 is loaded instead (its slot-1 colour); the slot-1 colour is then applied when it is not
  −1 (so with no entry-1 variant the entry-0 car stays, in the slot-1 colour if set). Only
  PAL's `L0S0001` has an entry 1 (`s1_vauxhall_vx220_00_147` for `s1_opel_speedster_00_147`).
  The test (`0x236A38`) has no other caller: no other driver swaps.
- **Demonstration replays** (PAL `0x15CCB0`): `demo/%s%s_p` with the middle part `_uk` when
  the language is 2 and the code is exactly `L0S0001`; NTSC-U/J read `demo/%s`. (Ours reads the
  60 Hz files: `demo/L0S0001_uk`, else `demo/<code>`.)
- **Car names** (`0x2343A0` name, `0x234420` second string; `0x23A050` / `0x23A0D8` by label,
  the same offsets through a jump table, where an index ≥ 7 also reads +0xF2 / +0xF4): car
  +0xF2 / +0xF4 for language 0, +0x100 / +0x102 for language 2, +0xE4 / +0xE6 otherwise. Data: +0xF2 holds kana names (the NTSC-J program
  reads only these), +0x100 the British names (`… (R34)` without `,J`), +0xF6 is a flag byte
  (1 on the F1 cars), +0xF7 the region byte, +0xF8..+0xFF zero.
- **Paint names** (`0x239338`, all three programs): the carcolor.db definition's second name
  (+8; kana for some colours) for language 0, a negative index or ≥ 7; the first name (+4)
  for 1..6.

## 13. Open points

- Unknown scalar fields and award bitmasks. (The ending-movie conditions: §10, H.)
- Event bytes 0x19C–0x1C3 (AI strength / catch-up / tire wear, see the AI
  workstream) are not used by GT mode.
