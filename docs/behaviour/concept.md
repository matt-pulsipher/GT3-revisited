# Gran Turismo Concept: behavior

This page describes how GT3 Revisited runs the Gran Turismo Concept discs: the
releases, the front end (title, main menu, the set-up pages of each game, Game Status),
the progression and its presents, the races the menus start, the car settings, the
game save and what differs between the releases. Code comments cite its sections as
`concept §n`.

The Concept games are Gran Turismo 3's engine with GT Mode taken out and an
arcade-style front end in its place (`menu/concept.mbl`). Where a Concept disc does
what GT3 does, this page points at the GT3 page instead of repeating it:
[menus](menus.md), [gt_mode](gt_mode.md), [presentation](presentation.md),
[ai](ai.md), [physics](physics.md).

Numbers are used in order, with one exception: **§13 and §14 are not used.** §15
says where the code is and what this engine chooses where the game's behavior is
not established.

Code: `src/game/concept_state.*` (the progression rules, testable without a disc),
`src/game/concept_game.*` (the disc's lists, the garage cars), `src/game/concept_menu.*`
(the menus' game side), `src/game/concept_save.cpp` (the memory-card save),
`src/menu/flow/concept_flow.cpp` and `concept_hooks.h` (the front end), the race side
in `src/app/screen_race_*`, `src/race/` and `src/sim/exact/`. Tests:
`tests/test_concept_state.cpp`, `test_concept_save.cpp`, `test_concept_walk.cpp`,
`test_concept_race.cpp`, `test_race_presentation.cpp`.

The releases and the names used on this page:

| Name | Release | Serial | Game config | Save prefix |
|---|---|---|---|---|
| **Tokyo-Geneva** | 2002 Tokyo-Geneva, Europe (English, French, German, Spanish, Italian, Portuguese); 50 Hz | SCES-50858 | `gameconf/concept_eu.gcf` | `BESCES-50858` |
| **Tokyo-Geneva (Asia)** | 2002 Tokyo-Geneva, Asian release | SCPS-55902 (saves name SCPS-55903) | `gameconf/concept_tw.gcf` | `BISCPS-55903` |
| **Tokyo-Seoul** | 2002 Tokyo-Seoul, Korea, v1.02 and v2.00 | SCPS-56005 | `gameconf/concept_kr.gcf` | `BKSCPS-56005` |
| **2001 Tokyo** | 2001 Tokyo, Japan | SCPS-15010 | `gameconf/concept.gcf` | `BISCPS-15010` |

Unless a line says otherwise, a rule is the same on every release; §12 lists the
differences. Like every disc, a Concept disc's race runs 60 steps a second
(README "Conventions"); timings given in steps are at 60 a second.

---

## §1 Overview

- There is no career. The player has a title page, a six-item main menu (Course
  License, Single Race, 2P Battle, Free Run, Game Status, i.LINK Battle), the options,
  the Replay Theater and the memory-card pages. Everything else is reached from those.
- Progress is three small tables: ten **course license** records (medal and best runs
  per course), the **arcade results** (best place per course and level) and the
  **present flags** (one per prize: 50 cars, two movies, the pod race and the credit
  transfer). There is no money, day counter or license ladder to play through.
- Ten courses: five circuits and their reverses (`midfield`, `akasaka`, `smtsouth`,
  `tahitimaze`, `autumn`, then the `r_` forms), the same list in the game config's
  course lists SINGLE_RACE, TIME_ATTACK, TWO_PLAYER and I_LINK (section 8). Course *n*
  (0–9) is index *n* of SINGLE_RACE; the same index numbers the license tests and the
  prize keys.
- Three single-race levels: **Normal**, **Professional** (item name `hard`) and the
  hidden **Ace**.
- GT Mode is not reachable. `menu/gt_mode.mbl` is still on the discs, the same file as
  GT3's, and nothing opens it.

## §2 Disc contents the front end uses

### §2.1 Menu projects

New in Concept (every release unless marked):

| Project (`menu/<name>.mbl`) | Pages | Opened by | § |
|---|---|---|---|
| `concept` | 26 Tokyo-Geneva, 20 Tokyo-Seoul, 21 2001 Tokyo | the front end (with `stream/course.ipic`) | §4, §5 |
| `concept_qm_arc_single` | `root` | the single race and the one-player pod race | §9.5 |
| `concept_qm_license` | `root`, `ghost_dlg`, `lc000000`..`lc000009` | a course license test without the pace car | §9.5 |
| `concept_qm_license_lead` | the same 12 pages | a course license test with the pace car | §9.5 |
| `notice_present` | `root` | the race, after a run that granted a prize | §8 |
| `concept_car_get` | `concept_car_get` | the prize car presentation | §8 |
| `concept_moviea`, `concept_movieb`, `concept_ace`, `concept_pod`, `concept_billion` | one page each, named like the project | the milestone presentations | §8 |
| `premium_get`, `test_concept_car_get`, `option_limited` (Tokyo-Geneva, Tokyo-Seoul) | | nothing in the retail game | |

Shared with GT3 and still used: `option`, `setting`, `memorycard`, `records`,
`name_entry`, `replaysave`, `pause`, `result`, `logger`, `language` (Tokyo-Geneva),
`arcade_mini`, `qm_arc_tt_free`, `qm_arc_tt_ltd`, `qm_arc_2p_result`. GT3's `arcade.mbl`
is not used.

Layouts are `menu/<LANG>/<project><id>.pmb` as in GT3 (`docs/formats/menu.md`);
`concept.mbl` spreads its pages over pmb ids 1, 2, 3, 101, 102, 103, 104, 113, 201, 301,
401, 411, 501, 502 and 902. Language folders: Tokyo-Geneva BR FR GR IT SP PT (and unused
US, JP, KR, CN, TW folders); Tokyo-Seoul and 2001 Tokyo KR / JP and unused ones.

### §2.2 Pages the retail game does not open

`ar/push_start_limited`, `ar/top_limited`, `ar/status_limited`, `ar/select_category`,
`ar/select_category_normal` (Tokyo-Geneva), `ar/push_start_350z`, `ar/push_start_airtrek`,
`ar/push_start_sl500` (Tokyo-Geneva), `ar/push_start_honda`, `ar/select_class`,
`ar/cannot_ending`, `ar/select_ending` (2001 Tokyo): pages of show-floor and single-maker
builds, which the other game configs on the disc select (`concept-350z.gcf`,
`concept-honda.gcf`, `motorshow.gcf`, ...; their section 0 carries a title idle time,
the retail config's is 0). Tokyo-Geneva's retail game uses `ar/select_category2` and
`ar/select_car_2p2`; Tokyo-Seoul and 2001 Tokyo use `ar/select_category` and
`ar/select_car_2p`.

### §2.3 Movies

Full-screen movies:

| Row | Tokyo-Geneva | Tokyo-Seoul, 2001 Tokyo | Used for |
|---|---|---|---|
| 0 | `stream/opening.pss` | the same | boot, end of the attract sequence |
| 1 | `stream/opening_GTCJP.pss` | `stream/opening_GT3UK.pss` | present MOVIE0 (§7.4) |
| 2 | `stream/ending_GTC.pss` | `stream/ending_long_full.pss` | present MOVIE1 and the Professional milestone (§8) |
| 4 | none (the file is not on the disc) | `stream/demoGT3A.pss` | the title's A-spec demo item |
| 5, 6 | `stream/tvcm_GTCJP1.pss`, `stream/tvcm_GTCJP2.pss` | none | Tokyo-Geneva's attract (§3.3) |

Page background movies: `arcade_menu_gtc.pss` (title, main menu, level page),
`category_gtc.pss` (category), `car_gtc.pss` (car pages), `status_gtc.pss` (Game Status),
`<course>_full.pss` (the course page, by the focused course). The disc also holds
`stream/level_gtc.pss`, which the game never plays.

## §3 Boot, title and attract

### §3.1 Boot

1. Tokyo-Geneva and Tokyo-Seoul look for a game save on the memory card first and take
   its language. 2001 Tokyo does not.
2. Tokyo-Geneva without a save: the **language page** (`menu/language.mbl` page `root`,
   items `br`, `fr`, `it`, `gr`, `sp`, `pt`, as PAL GT3's). The Asian Tokyo-Geneva
   release opens page `root_tw` (items `tw`, `us`). The other releases have no language
   page.
3. The memory-card warning, as GT3: shown only without a memory card ("No Memory Card
   (PS2) detected." with one OK button).
4. The opening movie (row 0).
5. The boot auto-load, as GT3: with a game save on the card an "AUTOLOAD" panel with a
   progress bar and "COMPLETE" (page `autoload` of `memorycard.mbl`), about two seconds;
   the language is then the save's.
6. The title (§5.2).

Each release starts in its own language: Tokyo-Geneva British English, Tokyo-Seoul
Korean, the Asian Tokyo-Geneva Traditional Chinese, 2001 Tokyo Japanese. Units by
language: Portuguese km/h, CV, kgf.m; Korean and both Chinese km/h, PS, kgf.m; the
others as GT3's PAL and NTSC-J releases.

### §3.2 The title's choices

The title page's items either open the main menu or leave the front end:

| Title item (action) | What runs |
|---|---|
| Game Start (`go_arcade_menu`) | the main menu (§4.2) |
| Option (`go_option`) | the options (§10) |
| Replay Theater → `load_replay`, `demonstration`, `logger` | the replay file page, the demonstration list, the Analyzer, as GT3's (menus §13) |
| `load_game`, `save_game`, `file_operation` | the memory-card pages, as GT3's (menus §12); after a load the language is the save's |
| A-spec demo (`go_aspec_demo`; Tokyo-Seoul, 2001 Tokyo) | the movie `stream/demoGT3A.pss` |
| Credits (link `ar/credit`) | the credits page (§5.2) |

The main menu's games run one menu session, then the race it asks for (§9.1), then come
back to the menus.

### §3.3 Attract

The title times out after **20 seconds** without input (1000 frames on the 50 Hz
Tokyo-Geneva, 1200 on 2001 Tokyo); the main menu and the other pages never do. Any
input restarts the count. The game config's title idle time (section 0's first word)
is not used for this; it is 0 in the retail configs.

After a time-out the attract sequence runs, each step going back to the title as soon
as it is interrupted:

- Tokyo-Geneva (and the Asian release): demo race, `tvcm_GTCJP1.pss`, demo race,
  `tvcm_GTCJP2.pss`, demo race, `opening.pss`.
- Tokyo-Seoul v2.00: four demo races, `opening.pss`. Tokyo-Seoul v1.02 and 2001 Tokyo:
  three demo races, `opening.pss`.

A demo race is an arcade demonstration on one of the game config's demo events
(section 12; 17 `demo_gtc_*` events on Tokyo-Geneva), limited to section 0's second
word: 90 s (GT3: 105 s).

## §4 The menu session

The front end is GT3's arcade (menus §8) grown to hold the title and Game Status: each
game is a fixed sequence of pages, each opened, waited on and followed by the next.
There is no page stack for the set-up pages.

### §4.1 Session state

The front end remembers, while the game runs and not in the save: the game played
last (the main menu's focus), the level chosen last, the category chosen last, the
pace-car choice, the license test chosen last, the title sub-page shown last and its
focus, a pending game to go back to after a race, the Ace level (§7.5), the current
car (for the show room and the settings) and the car availability table (§7.3). For
each player the car page's choices: car, color, transmission and assist.

### §4.2 Session flow

1. The car availability table is built again (§7.3).
2. Unless a game is pending, the **title page** (§5.2).
3. Unless a game is pending, the **main menu** `ar/top` (§5.3). Its items only choose
   the game:

| Game | Action | Item on `ar/top` (label) |
|---|---|---|
| 0 | `single_race` | `single_race` (Single Race) |
| 1 | `compulsory` | none (GT3's time trial; `ar/select_compulsory` is not on Tokyo-Geneva or Tokyo-Seoul) |
| 2 | `license` | `license` (Course License) |
| 3 | `time_attack` | `time_attack` (Free Run) |
| 4 | `two_player_battle` | `two_player` (2P Battle) |
| 5 | `goodies` | none (`ar/goodies` is not on the discs) |
| 6 | `status` | `game_status` (Game Status) |
| 7 | `load_guest_garage` | none; nothing happens |
| 8 | `i_link_battle` | `ilink_battle` (i.LINK Battle) |

   Item order on the page: `license`, `single_race`, `two_player`, `time_attack`,
   `game_status`, `ilink_battle`. Cancel on `ar/top` (`cancel_select_game`) returns to the
   title.
4. A sequence that ends with a race (games 0–4 and 8, and the pod races) keeps the game
   as pending and starts the race (§4.4). After the race the menus find the pending game
   and open its sequence again, not the title or the main menu.

Sequences (each page is confirmed or backed out of; back goes one page back, back on the
first page returns to the main menu):

- **Single Race**: `ar/level` → `ar/select_course` (skipped when SINGLE_RACE has fewer
  than two courses) → `ar/select_category2` → `ar/select_car`.
- **Free Run**: `ar/select_course` → `ar/select_category2` → `ar/select_car`.
- **2P Battle**: `ar/select_course` → `ar/select_car_2p2`.
- **Course License**: `ar/select_license`; a test opens `ar/lead_car`, whose two items
  (`with_lead_car` / `without_lead_car`) set the pace-car choice and start the test.
- **i.LINK Battle**: as GT3's (menus §8): the engine's online lobby, with race code
  `ALNK0001` and up to 8 entries.
- **Game Status**: `ar/status` (§5.9). Its items either show a car (`set_concept_car` →
  `ar/show_room`), play a present (`go_present`, §4.3) or open `ar/podrace`, whose items
  `go_podrace_1p` / `go_podrace_2p` choose the pod race; the one-player race then opens
  `ar/select_car`, the two-player race `ar/select_car_2p2`.

(Tokyo-Seoul and 2001 Tokyo use `ar/select_category` and `ar/select_car_2p`, §12.)

### §4.3 Presents from Game Status

- `MOVIE0` plays movie row 1 and comes back to Game Status.
- `MOVIE1` plays movie row 2 and comes back.
- `TRANSFER` runs the credit transfer (§7.6) and comes back.
- `PODRACE` (through `ar/podrace`) starts the pod race.

### §4.4 The race request

As GT3's arcade request (menus §8): car, course, race code, color, transmission, tires
and the assist choice, and player 2's for a two-player race. Race codes:

| Game | Race code |
|---|---|
| Single Race | the chosen class entry's code (Tokyo-Geneva: `A_<car>_0001`, dirt entries `AD_<car>_0001`, the British lists `A_uk_<car>_0001`; Tokyo-Seoul and 2001 Tokyo: four-letter codes such as `AEHC0001`, dirt `AEUD0001`, from their game configs) with its **last character raised by the level**: `..1` Normal, `..2` Professional, `..3` Ace |
| Course License | the test's label from the config's CLASS_LICENSE list: `LCeu000n` Tokyo-Geneva, `LCtw000n` the Asian release, `LC00000n` Tokyo-Seoul and 2001 Tokyo |
| Free Run | `ATAF0001` |
| 2P Battle | `A2P0001` |
| i.LINK Battle | `ALNK0001` |
| Pod race, one player | CLASS_PODRACE's entry `APOD0001`, the course POD_RACE's `route5_pod`, the car `gtcmr_pod_tmv_01` |
| Pod race, two players | `APO20001` |

The car is the chosen class entry's garage car (§10) for Single Race, Free Run, 2P Battle
and the pod races; a license test drives the test's own car.

## §5 Pages

### §5.1 Widgets

Concept's pages use GT3's page engine (menus §4, §5) with these new button lists and
widgets: the title's scrolling selector `menu_top` (as GT3's title), `menu_game` (the
main menu), `menu_difficulty` (the level page), `menu_category`, `menu_license`,
`menu_map` (Game Status, a map list like GT3's GT-mode map), `menu_goodies`,
`menu_level`. New widget names: `achievement_ratio`, `arrow_dec`, `arrow_inc`,
`available_car_num_A/B/C/D/S`, `car_color_box`, `car_description`, `car_logo`,
`car_name`, `car_name_license`, `car_render_from`, `category_dirt`, `category_road`,
`color_name_box`, `connect_status`, `countdown`, `diagonal_in`, `diagonal_out`,
`flash_item`, `license_car`, `license_time_gold/silver/bronze`, `mpeg_class`,
`mpeg_course`, `show_status`, `spec_aspiration`, `spec_displacement`, `spec_drive_type`,
`spec_engine_type`, `spec_height`, `spec_length`, `spec_max_power`, `spec_max_torque`,
`spec_weight`, `spec_width`, and `blink` (the title's blinking arrows).

Initial focus beyond GT3's rules (menus §4.2): `ar/connect` opens on item 1;
`ar/lead_car` on YES when the pace car was chosen last, else on NO; the title on the
sub-page and item shown last; the level page on the level chosen last; the category
page on the category chosen last; the license page on the test chosen last; the main
menu on the game played last.

### §5.2 `ar/push_start`: the title

The GT Concept logo with the release line ("2002 TOKYO-GENEVA"), the Polyphony logo and
the copyright line over black, the background movie behind. The items sit in the
`menu_top` selector as on GT3's title, with two blinking arrows above and below.

Items (Tokyo-Geneva): `game_start` (`go_arcade_menu`), `load_game`, `save_game`,
`file_operation`, `replay_theater` (link `ar/replay_theater`), `option` (`go_option`),
`credit` (link `ar/credit`). Tokyo-Seoul and 2001 Tokyo add `aspec_demo`
(`go_aspec_demo`) between `replay_theater` and `option`.

- `ar/replay_theater`: `load_replay`, `demonstration`, `logger`, as GT3's page.
- `ar/credit`: 13 entries (`tip1`..`tip13`, 10 on Tokyo-Seoul and 2001 Tokyo), each showing
  its own layer of the credits.

### §5.3 `ar/top`: the main menu

"MAIN MENU" heading; six rows of a round icon and a label (Course License, Single Race,
2P Battle, Free Run, Game Status, i.LINK Battle); a help line per item (`layer1`..`layer6`);
the GT Concept logo bottom right behind two wipe widgets (`diagonal_in`, `diagonal_out`).
Opens on the game played last.

### §5.4 `ar/level`: the level

"LEVEL SELECTION" heading, a band across the middle holding the level buttons and a help
text per level (layers `normal2`, `pro`, `ace2`). Items `normal`, `hard`, `ace`; the level
is the focused item's index (the items' action strings are not used). The list leaves
the **last item (Ace) out** until the Ace level is revealed: holding **L1 and R1** while
the page is up adds it and reveals the level for the rest of the run (§7.5). Opens on
the level chosen last.

### §5.5 `ar/select_course`: the course

As GT3's course page (menus §9.1): "COURSE SELECTION", the focused course's logo, map and
description, "Total Length", left / right arrows, the strip of course icons and the
course's movie. Two additions:

- each course icon carries a **place badge** (`item_1st`..`item_6th`) for the best place
  at the chosen level (§7.1);
- in Single Race only the courses whose **course license has a medal** can be chosen.
  The other games offer all ten. With no medal at all the main menu refuses Single Race
  (it takes no Confirm).

Single Race's strip shows the wide icons (`_mini`, with the badge), cut at the strip's
ends; Free Run's and 2P Battle's the small ones (`_mini1`), all ten fitting.

### §5.6 `ar/select_category2`: the category

Heading "CAR SELECTION", or "RALLY CAR SELECTION" on a dirt course (`category_road` /
`category_dirt`); five round buttons: NEW CARS Pacific, NEW CARS Europe, Concept Cars,
Dream Cars, Racing Cars (items `C`, `B`, `A`, `S`, `D`; help layers `c`, `b`, `a`, `s`,
`d`). The categories are the game config's class lists CLASS_C, CLASS_B, CLASS_A,
CLASS_S and CLASS_DIRT, and in British English, where the disc has them (Tokyo-Geneva),
their `_UK` twins. CLASS_DIRT is the racing-car category, not a dirt class: every class
holds road entries (`gtcmr_<car>`) and dirt entries (`gtcmd_<car>`).

For each category the page counts the open cars (§7.3) of the course's kind (road or
dirt). A category with none shows the `question` art and cannot be chosen (except in
i.LINK Battle). Tokyo-Seoul and 2001 Tokyo use `ar/select_category`: the same five items
with the count (`available_car_num_*`) under each button.

### §5.7 `ar/select_car`, `ar/select_car_2p2`: the car

GT3's car page (menus §9.2) with Concept's art: "CAR SELECTION", the car picture and maker
logo, color chips, a drivetrain icon (`ff`, `fr`, `mr`, `rr`, `four_wd`), power and weight,
the strip of cars. New:

- three **bar gauges**, Handling, Speed and Acceleration (`handling_gauge`,
  `speed_gauge`, `acceleration_gauge`), each bar's art shown up to its value;
- the steps are transmission, tires and **driving assist**, but the tire step is always
  skipped on a Concept disc: no Concept car page asks RACING / DRIFT (the art is still in
  the layouts). The assist step offers three choices, `assist_0` (ASM and TCS on),
  `assist_1` (TCS only) and `assist_2` (neither); the race takes its ASM and TCS from it.
  The assist step is skipped on a dirt course.
- locked cars (§7.3) are left out of the strip;
- `ar/disable` explains a car that cannot be used on the course (`disable_road_doc`,
  `disable_dirt_doc`, `disable_no_car_doc`).

The two-player page has each player's own class row (`mini_classes`, `mini_classes_2p`),
car row and option buttons (the `*_2p` twins), as GT3's `ar/select_car_2p` (menus §9.6),
with each player's assist step (`assist_<n>_yes|no[_2p]`, `assist_title[_2p]`).

### §5.8 `ar/select_license`, `ar/lead_car`: the course license

"COURSE LICENSE" over a grey swoosh with the license emblem; a "TEST CAR" panel top right
(`car_name_license`, layers `layer1`..`layer10`), the tests in a strip (`menu_license`,
items `tt01_mini`..`tt10_mini`, arrows `arrow_dec` / `arrow_inc`), per test a description
(`license_doc<nn>`), the course logo and map (`layer11`..`layer20`), and a "Clear Time:"
bar with gold, silver and bronze cups and their target times from the event. Each test's
button shows the medal already won (`item_gold` / `item_silver` / `item_bronze`). All ten
tests are open from the start. Opens on the test chosen last.

`ar/lead_car`: two framed buttons, "Pace Car YES" (`with`) and "Pace Car NO" (`without`),
a help line each; back to `ar/select_license`.

### §5.9 `ar/status`, `ar/show_room`, `ar/podrace`: Game Status

A map of 1280 × 584 scrolled under a 640 × 448 window, the background movie behind: ten
course logos in five colored sectors, from each a chain of three round nodes with laurel
frames (`C<nn>L_stat`, `C<nn>N_stat`, `C<nn>H_stat`); in the middle "GAME STATUS",
"Achieved :" with the completion figure (§7.7) and a round Back button (`game_status`);
along the bottom a plate of gift boxes (`prize_plate`). Hidden art: 50 car pictures
(84 × 65, named by the car; 80 × 62 on Tokyo-Seoul; on 2001 Tokyo each button carries its
own picture), `license_gold/silver/bronze`, `rank_1`..`rank_6`, `pointer`,
`MOVIE1_middle`.

Items (45): `C<nn><L0|L1|N0|H0>` for nn = 00..09 (action `set_concept_car`, link
`ar/show_room`), `MOVIE0`, `MOVIE1`, `TRANSFER` (action `go_present`), `PODRACE` (link
`ar/podrace`) and `game_status`.

What is reached:

| Thing | Reached when |
|---|---|
| item `C<nn>L0` / `L1` | the course's license has a medal / a gold medal |
| item `C<nn>N0` / `H0` | the course is won at Normal (or above) / at Professional or Ace |
| `MOVIE0`, `TRANSFER` | every course is won at some level |
| `MOVIE1` | every course is won at Professional or Ace |
| `PODRACE` | all ten licenses have a medal |

What a node draws, centred on its box:

| Node | Art |
|---|---|
| `C<nn>L_stat` | `license_gold` / `license_silver` / `license_bronze` by the license's medal; nothing without one |
| `C<nn>N_stat` | `rank_1` once the course is won at any level; else nothing |
| `C<nn>H_stat` | `rank_1` once the course is won at Professional or Ace; else nothing |

`rank_2`..`rank_6` are not used here. A prize car or present that is reached is drawn as
its button, pulsing while focused; one not reached is drawn in black (a car's silhouette,
a black gift box) and takes no Confirm.

**The map's scroll.** The list keeps the `pointer` at the focused button. Each frame the
scroll moves towards the focused button by (target − value) × 0.1 × 1.2, clamped to the
map's room to move under the window (1280 − 640 across, 584 − 448 down), so the focused
button stays at the screen's centre until the map's edge is reached. The directions pick
the next button as GT3's GT-mode map does.

`ar/show_room` (back to `ar/status`): the chosen prize car in 3D, its description line,
maker logo, color box and color name, and a ten-line specification table (drive type,
weight, height, width, length, engine type, aspiration, displacement, max torque, max
power). `set_concept_car` makes the prize key's car the current car.

`ar/podrace` (back to `ar/status`): the maker's logo, a "pod Race" banner with two pods
from above, and two round buttons, "1P pod Race Entry" (`solitaire`, `go_podrace_1p`) and
"2P pod Race Entry" (`battle`, `go_podrace_2p`).

### §5.10 Other pages

- `ar/connect` (`connect_status`, `countdown`, `my_name`; items `i_link_play`,
  `i_link_live`) and `ar/wait_other`: GT3's link-cable pages; this engine opens its online
  lobby instead (menus §8).
- `ar/disable`: a one-button message page.

## §6 Actions

The front end's actions (the same 31 names on every release). "GT3" means the same as
GT3's arcade or title action (menus §7, §8).

| Action | Effect |
|---|---|
| `single_race`, `license`, `time_attack`, `two_player_battle`, `status`, `i_link_battle`, `compulsory`, `goodies`, `load_guest_garage` | choose the game (§4.2) |
| `cancel_select_game` | back to the title (GT3) |
| `go_arcade_menu` | leave the title for the main menu |
| `go_option` | the options |
| `go_replay_theater`, `go_demonstration`, `go_logger` | the replay file page, the demonstration list, the Analyzer |
| `go_file_operation`, `go_save_game`, `go_load_game` | the memory-card pages (GT3) |
| `go_aspec_demo` | the A-spec demo movie |
| `go_ending_easy`, `go_ending_normal` | GT3's arcade endings; no page the game opens has them |
| `i_link_play`, `i_link_live` | GT3 |
| `jump_to_event`, `set_garage_car` | no item uses them |
| `set_concept_car` | the item's name is a prize key; its car becomes the current car |
| `go_present` | play or run the item's present (§4.3) |
| `go_podrace_1p`, `go_podrace_2p` | the pod race for one or two players |
| `with_lead_car`, `without_lead_car` | the pace-car choice, then the test |

Handled by the page engine as in GT3: `set_focus_replay_theater`, the quick menus'
`qm_*` actions (menus §11), the settings actions (§10), `set_lang` and the records page's
`delete`.

## §7 Progression and unlocks

### §7.1 The records the rules read

- **License records**: ten of GT3's license record (gt_mode §21), one per course. Medal
  none, gold, silver or bronze; the best runs.
- **Arcade results**: one record per level and course (`level × 10 + course`), holding the
  best place, 0 = won. The level comes from the race code's last digit: 1 Normal, 2
  Professional, 3 Ace.
- **Present flags**: one per prize key, in the order of the game config's prize list
  (section 15), then `MOVIE0`, `MOVIE1`, `PODRACE`, `TRANSFER` at 70, 71, 72 and 73. All
  clear on a new game.

### §7.2 Prize keys

The game config's section 15 has 50 keys named `C<course 00-09><N0|H0|L0|L1>` plus ten
with the suffix `_S`. Each names one class slot, `CLASS_<X>#<index>`, that is one car. An
`_S` key names the **dirt entry** (`gtcmd_<car>`) of the car its base key names (`C00N0` →
`CLASS_B#002`, `gtcmr_mini_cooper_s_02`; `C00N0_S` → `CLASS_B#018`, `gtcmd_mini_cooper_s_02`),
so a car with both entries opens on both kinds of course at once. Tokyo-Geneva's `_S` keys:
`C00N0_S`, `C01L1_S`, `C02L1_S`, `C02N0_S` (another car's dirt entry), `C03L0_S`,
`C04H0_S`, `C05L0_S`, `C07H0_S`, `C08L1_S`, `C09L0_S`.

### §7.3 Which cars can be chosen

For each of the five categories each class slot is **locked** when a prize key names it
and that key's flag is clear; every other slot is open from the start. The table is built
again when the front end starts a session, on a new game and after a load. (Each class
entry also carries an "enabled" word and a level, 1 and 0 for every car on the retail
discs.)

### §7.4 The prize pass

Run after a single race or a license test, with the race code and the course. A code
starting `LC` is a license (test index from its last two digits); any other is a single
race at the level of its last digit. For every prize key of this course whose flag is
clear:

| Key | Granted when |
|---|---|
| `L0` | a license test, and the license now has a medal |
| `L1` | a license test, and the medal is gold |
| `N0` | a single race at any level, and the result just recorded is a win |
| `H0` | a single race at Professional or Ace, and the result is a win |

Then the four specials, each once:

| Key | Granted when | Present |
|---|---|---|
| `MOVIE0` | a single race, and every course is won at some level | movie row 1 from Game Status |
| `TRANSFER` | the same | the credit transfer to a GT3 save (§7.6) |
| `MOVIE1` | a single race, and every course is won at Professional or Ace | movie row 2 from Game Status |
| `PODRACE` | a license test, and all ten licenses have a medal | the pod race |

The result is a list of up to six names, each with a kind (car, pod race, transfer,
MOVIE0, MOVIE1), sorted by kind. Flagging a granted car sets the flag of **every key
whose name starts with it**: a base key and its `_S` twin are flagged together, so both
entries of the car open at once and the car is presented once.

The pass after a run tests the current flags (§8 step 2). The milestone sequence tests the
flags as they were when the race screen opened, so it finds again everything the visit
granted (§8 step 3).

### §7.5 The Ace level

Not a saved unlock: the level page shows two buttons until L1 + R1 are held on it (§5.4),
and every start of the game hides Ace again. The Professional milestone page (§8) tells
the player the button combination. Ace races are the `..3` event rows (§9.1).

### §7.6 Credit transfer to GT3

Game Status's `TRANSFER` edits a **GT3 game save on the memory card**; nothing is read
into Concept:

- Target: `BESCES-50294GAMEDATA` (GT3 PAL) on Tokyo-Geneva; `BISCPS-15009GAMEDATA` (GT3
  NTSC-J) on Tokyo-Seoul, 2001 Tokyo and the Asian Tokyo-Geneva.
- Refused with the read-error dialog when the save's check fails or its major version is
  not 7.
- Refused with "Money and licenses have already been transferred." when the save already
  holds the transfer mark (the value 0x12345678 in the scalar block at +0x12C): the mark
  lives **in the GT3 save**, and the item stays usable in Concept.
- Otherwise: write the mark; add the amount to the credits (a 64-bit word, no limit); set
  all six license-prize bits (+0xC0 |= 0x3F) and the bronze-license award bit
  (+0xC4 |= 0x80000); give every license test without a medal a bronze (its time is left
  alone); update the thumbnail's credits, license (set to 4) and completion (+0.1908 for
  each test changed, at most 100); write the file again with its check.
- Amount: **10,000,000 Cr** on Tokyo-Geneva, **1,000,000,000 Cr** on Tokyo-Seoul, 2001 Tokyo
  and the Asian Tokyo-Geneva (hence the page name `concept_billion`).

The texts are the memory-card page `transfer_money`: "Transfer money and licenses to
GT3A-spec data.", "OK to transfer money and licenses?" with OK / CANCEL, "There is no
GT3Aspec data present!", "Transferring...", "COMPLETE", "Money and licenses have already
been transferred.". This engine writes into its own GT3 saves (§15).

### §7.7 Completion figure

(arcade points + license points) / 40, at most 1: per course 1 point for a win at any
level and 1 more for a win at Professional or Ace; per license 1 point for a medal and 1
more for gold. Shown on Game Status ("Achieved :") and stored × 100 in the save's
thumbnail.

### §7.8 Where the numbers live

| Number | Source |
|---|---|
| courses, classes, cars per class, tests, prize keys | the game config, sections 8, 14, 13, 15 |
| laps, opponents, rubber band, medal times | the event rows (§9.1) |
| "won" = place 0; "has a result" = place below 6 | the game |
| the special keys' names, the transfer's amount and target | the game (§7.6) |
| the pod race's stop area (608 m, 33 m) | the game (§9.2) |
| the demo time limit (90 s), title idle time (0) | the game config, section 0 |

## §8 After a run

The single race and both kinds of license test do, in order:

1. Record the result: the arcade result (course, car and place), or the license medal and
   the license record's run with its name entry (menus §12.2; the records page lists the
   ten tests).
2. A won single race first shows GT3's arcade cup (presentation §F4; Normal, Professional
   and Ace show the cups GT3 numbers easy, normal and hard). Then the prize pass against
   the current flags; if it grants anything, **`notice_present`**: a black page with a
   500 × 72 window, a gift icon, the title "Info", the line "You will receive a prize after
   this session." and an OK button. The granted names are flagged right after it, so a
   later run of the visit gives no second notice. The run's replay follows, then the quick
   menu again.
3. When the player leaves the quick menu (after at least one finished run in the visit),
   the **milestone sequence** runs the prize pass again against the flags as they were
   when the race screen opened (§7.4), and shows, in this order:
   - each car: **`concept_car_get`**: an orange page, "Congratulations" arched across the
     top, the car as a 3D model (background `menu/prize_get.img`), a reason line from one
     of the layers `normal_clear`, `pro_clear`, `gold_record`, `bronze_record`, "You have
     won this car!" with the car's name, and a blinking OK button; one car after another;
   - MOVIE0: **`concept_moviea`** ("Normal Level has been cleared!", the movie is now in
     Game Status);
   - MOVIE1: **`concept_ace`** ("Pro Level has been cleared!" and the L1 + R1 hint), then
     **`concept_movieb`** (an "ENDING MOVIE" title card with no button), then movie row 2;
   - the pod race: **`concept_pod`** ("Now you can enter the Pod Race!");
   - the transfer: **`concept_billion`** (the credits for GT3 game data, the amount in the
     picture).

   Each of the last four is a full-screen picture (layer `delay`) with one OK button, 2
   frames apart.
4. The last name of the list is remembered, so Game Status opens on it.

The pod races, 2P Battle, Free Run and i.LINK Battle record nothing and grant nothing.

## §9 Race side

### §9.1 From menu choice to race

`racemode.db` is GT3's (the same 16 rows on every release). Each game runs as:

| Choice | Race | Quick menu | Display rows (racemode) |
|---|---|---|---|
| Single Race | GT3's arcade single race | `concept_qm_arc_single` | SINGLE |
| Course License, no pace car | GT3's license test | `concept_qm_license` | LICENSE |
| Course License, pace car | the license test with a pace car (§9.2) | `concept_qm_license_lead` | LICENSE |
| Free Run | GT3's free run | `qm_arc_tt_free` | FREE_TIME_TRIAL |
| 2P Battle | GT3's split-screen battle | `qm_arc_2p_result` | 2P_BATTLE / _WIDE |
| i.LINK Battle | the online race (menus §8) | | SINGLE |
| Pod race, one player | the arcade race with the pod rules (§9.2) | `concept_qm_arc_single` | SINGLE |
| Pod race, two players | the split-screen battle with the pod rules | `qm_arc_2p_result` | 2P_BATTLE |

GT3's time trial (`qm_arc_tt_ltd`) is still in the game but no menu reaches it.

Events (the database's event table: index 31 on the Concept discs, 32 in GT3; GT3's
504-byte rows, 531 of them in Tokyo-Geneva's `paramdb_gtc_eu.db`):

- **Single race** `A_<car>_000N`: N = 1 Normal, 2 laps; 2 Professional, 3 laps; 3 Ace, 5 laps
  (4 on dirt). 6 cars on tarmac, 2 on dirt (`AD_` rows). The row's course is a placeholder:
  the menu's course is used. Rubber band (ai §9.3; sampled on one car's rows): Normal slows
  computer cars ahead by 15 % between 20 and 40 m, Professional by 5 % between 40 and
  120 m, Ace not at all.
- **License** `LCeu000n` (Tokyo-Geneva; `LCtw000n` the Asian release, `LC00000n`
  Tokyo-Seoul and 2001 Tokyo; Tokyo-Geneva's database holds the other regions' rows too):
  1 lap, a flying start at 100 km/h from 99 m before the line (physics §9.1), gold / silver
  / bronze times at +0x1C4 / +0x1C8 / +0x1CC as GT3's (minutes, seconds, milliseconds;
  Midfield's 1'27, 1'30, 1'37). The entry pool is the test car twice, then a pace car
  `gtcpc_r34_gtr_v_gtcolor_pc_99_<400|450|650|800|dirt>`. Tokyo-Geneva's `LCeu000n` and the
  Asian `LCtw000n` rows hold the same times: a 50 Hz and a 60 Hz release with the same
  medal times.
- **Demonstrations**: `demo/<code>`, `demo/<code>_p` on the European Tokyo-Geneva (its
  50 Hz recordings), with GT3 PAL's `_uk` suffix rule. The Tokyo-Geneva discs hold
  `LCeu000n_p` (50 Hz), `LCtw000n` (60 Hz) and `LC00000n` / `_p`.
- **Pod race** `APOD0001`: `route5_pod` (680 m), 3 laps, 6 cars, all `gtcrr_pod_tmv_01`,
  test kind 5, a kind GT3 does not use.

### §9.2 Concept's own race rules

**The pace car.** A course license test with the pace car is the license test with a
second car:

- Entry 0 is the player; entry 1 is the event pool's third car (the pace car) in its pool
  paint, or the car's first paint when the pool names none. The screen is the license
  test's (target times, time, no race HUD; presentation §A).
- **Start**: the event's flying start; car *i* > 0 starts trunc(*i* × speed / 3.6) m further
  on, so the pace car starts **one second ahead at the start speed**: 27 m at the
  Tokyo-Geneva tests' 100 km/h (72 m before the line when the player is 99 m before it),
  at the same speed. The player's and the pace car's grid slots are not read.
- The run has no ghost car; it runs the whole collision step (cars against cars too), with
  the event's rubber band on and the single race's pass rule.
- **Judging**: only the player's car is judged; the pace car leaving the road or finishing
  does nothing.
- **Passing**: nothing reads the cars' order. The pace car is a computer car on the AI line,
  held back by the event's rubber band while it is ahead (ai §9.3: slow-down to 82 %, from
  20 m ahead to 60 m; 25 / 70 m on Akasaka, 15 m on Autumn); once passed it is an ordinary
  car behind. The medal, the record and the prize pass are the same as without it. The
  run saves a replay, not a ghost.

**The license test's rules** (with or without the pace car, on every Concept release):

- **Out of the course**: counts only for the player's car, and only with all four wheels
  on pit-lane ground inside the course's pit stretch. Grass, sand, dirt and curbs never end
  a Concept test (GT3's test also ends on soft ground).
- **Contact**: no contact, with a wall or with the pace car, fails a Concept license test
  (GT3's ends on a hard hit).

**The examination message** (the pod race's disqualification): GT3's time-trial FAIL
presentation (presentation §B7) with Concept's timings:

- When the race posts its own car's disqualification, the fail jingle plays; 60 steps
  later the message `RACEDISPLAY_Disqualification` starts ("FAIL" in the 2002 releases'
  English; font `sw_20`).
- The message fades in by 1/60 a step (61 steps), is held 180 steps and fades out over 61
  steps; 240 steps after it is gone, or on a confirm, the race ends as a run that did not
  finish, and the arcade race's replay and quick menu follow. So from the jingle: the
  message from step 61, gone at 363 (5 s on screen), the race over at 604 (10 s) without a
  confirm. The European Tokyo-Geneva's 50 Hz timers (50, 150, 200, a step of 0.02) give the
  same times within a step.
- Drawing: a wash (160, 160, 255) at alpha int(fade × 128) / 2 over the screen, then the
  text (160, 192, 64) at alpha int(fade × 128), `sw_20` at 1.5 × 1.5, centred on x 320, in
  two copies at y 224 + s and 224 − s (one when s is 0), s = int((1 − fade) × 30): the
  copies meet as the message fades in and part again as it fades out.
- In the game's wide mode the text's x scale is 0.75, so it keeps its 4:3 shape on the 16:9
  picture, and the wash covers the whole 16:9 screen, not a 4:3 box.

**The pod race**: a 3-lap, 6-car race on a 680 m course in which every car must **stop
once a lap** in a stop area. It is reached in a retail game by earning a medal in all ten
course licenses, then Game Status → pod race (§7.4). It is played with the pad; the
two-player race is ordinary split screen.

- **Notes**: the one-player race shows three green notes (`sw_16`) of 3 s each, the first
  2 s in, so they run through the start's countdown: the texts
  `RACEDISPLAY_PodRaceStart1`..`3`. The two-player race shows none.
- **Prompts**: car messages in WRONG WAY's place (presentation §D6): `RACEDISPLAY_PodSitDown`
  in amber and `RACEDISPLAY_PodOverrun` in red, each for as long as the rule sets it.
  `RACEDISPLAY_PodSitDownOK` is the note of a stop.
- **The stop area**: starts 608 m into the lap and is 33 m long. The rule is off when
  either is 0 or the area would cross the start line. Each car's stop count starts at 0.
- **Sitting**: a pod's sit counter goes up by one a step, to 29 at most, while the car is
  stopped (the handbrake fully on and the speed, forward and sideways together, at most
  1 km/h), and down by one a step otherwise. The pod **sits** at 29. The counter also
  lowers the wheels' travel as it rises. A restart sets it to 0 or 29.
- **Owed stops**: the car's lap count less its stop count.
- **Each step**, for every car that owes a stop:
  - the player's car: with all four wheels on stop-area ground a sitting pod **gets its
    stop**, and one not sitting yet shows PodSitDown; with a wheel off that ground and the
    car past the area's end, PodOverrun;
  - a computer-driven car: past the area's start a sitting pod gets its stop; otherwise
    PodSitDown with all four wheels on the area's ground.
- **A stop**: the count goes up (255 at most); for the player's car the note is posted.
- **At the line**: a car that owes two stops or more is **disqualified**: the computer takes
  it over to stop it, its lap is not counted, its pod's expression changes and the
  examination message above shows for the player's own car. Laps count from the first
  crossing of the line after the standing start, so a pod that misses one lap's stop is
  disqualified at the end of that lap.
- **Two players**: each half has its own car, prompts and message. There is no jingle, and
  each half's message is held after its 180 steps instead of fading; the race ends once
  both players are disqualified. With one player disqualified the race ends as a 2P battle
  does. The race has the Options' 2P laps (2 by default), not the event's 3, as the 2P
  battle takes them.
- **Texts**: the 2002 releases' text files hold the notes, prompts and the message in each
  language. 2001 Tokyo's file has none of them, so the game shows its built-in Japanese
  (the disqualification 失格です). The engine reads a race text from the language's file,
  then from the game's built-in texts, then in American English; the same rule gives GT3
  NTSC-J its built-in Japanese for the race messages its file lacks.
- Pods also show an expression (one of eleven, by what happens to the car) on the model,
  apart from the rules.

**The hybrid car's store monitor**: a display row (type 13, sub 1) of the 2 player and
LOGGER modes, drawn only for a car of drive layout 7 (the Honda Dualnote): a half-alpha
black box of the row's size, a dark inner box 2 in, the sign `meter_sign_dualnote` at its
bottom tinted blue, and above it the car's store over its capacity as a bar from the
bottom: blue (0, 0.2745, 0.8) while the flow display is 0 or above, (0.55, 0.1, 0.25)
while the store is drawn.

**Leaves and sparks**: Concept adds fallen leaves, thrown up by a wheel on ground whose
surface flag has bit 0x20 (only Autumn Ring's verges, 8 to 30 m off the center line, have
it; no GT3 course does), and sparks (presentation §L7; from Tokyo-Seoul v2.00 on a burst
starts from, and bounces on, the plane of the car body rather than the road under the
point). A leaf is a small plate of one of eight shapes, drawn with `race/particle.img`'s
pictures 1–8, lit by the course's car light 0 and by the ground shade under the wheel that
threw it.

### §9.3 HUD

The display rows come from the disc's `racedetail.db` (GT3 228 rows, Tokyo-Seoul and 2001
Tokyo 236, Tokyo-Geneva 238), read as GT3's (presentation §D):

- SINGLE, TIME_TRIAL, TIME_ATTACK, FREE_PRACTICE, ENDULANCE, MAX_SPEED_CHALLENGE, 0_400:
  GT3's rows. The single race, the one-player pod race and the free run use GT3's HUDs.
- LICENSE: GT3's rows, the three target-time lines with width 0 instead of 8.
- 2P_BATTLE, 2P_BATTLE_WIDE: 17 rows (GT3 16): the store monitor (type 13 sub 1) at
  (20, 212) in 4:3 and (413, 392) wide; message width 360; steering 27 × 25; pedals height
  50; several 4:3 positions moved.
- LOGGER (the Analyzer's REPLAY): 6 rows (GT3 4): a dashboard along the bottom edge
  (steering centred at (320, 420), the bar tachometer, the accel / brake boxes, gear, a
  digital speed, the store monitor). GT3 draws its display twice while the Analyzer plays,
  track A's car on the whole frame and track B's 560 to the right; Concept draws track A's
  car alone, on the whole frame, before the Analyzer's page, which covers most of it
  (presentation §G10).
- REPLAY: one car-name row for every view at (16, 340); meters type 3 sub 1 for every view;
  steering centred at (320, 420), 75 × 68; accel / brake boxes at (68, 435), 30 × 100; four
  of GT3's parts have no row and are not drawn.
- Concept's steering and accel / brake monitors are new drawings: the steering is the
  picture `replay_sign_gtforce` on the row's point, scaled by the row's size; the pedals are
  two boxes of the row's size 4 apart (brake, accel), each half-alpha black with a dark
  inner box, its sign (`replay_sign_brake`, `replay_sign_accel`) at the bottom, the bar
  solid up to the lower of the value now and a frame ago and fading out to the higher.
  (GT3's `steering`, `accel_gage` and `brake_gage` pictures are not on the Concept discs.)
- DEMO: Tokyo-Seoul and 2001 Tokyo as GT3; Tokyo-Geneva adds the view mode and a car name at
  (16, 431).
- The pod prompts use the car-message row (§9.2).

### §9.4 Results

- Single race: the place becomes the arcade result; the prize pass, the notice and, on
  leaving, the milestone sequence (§8). A first place shows the cup by the event's class.
- License (with or without the pace car): the medal, the record with its name entry, the
  prize pass, the notice, the milestone sequence. The race writes all of it, not the menu.
- 2P Battle and the two-player pod race: GT3's 2P result menu only. The one-player pod
  race: nothing is recorded.

### §9.5 Quick menus

- `concept_qm_arc_single` `root` ("Single Race" heading, a six-row list of car names
  `qm_list_car_name1`..`6`, a help line): `start`, `replay`, `save_replay`,
  `demonstration`, `setting`, `save_game`, `option`, `exit` (back: `qm_exit`).
- `concept_qm_license` `root` ("Course License Test" panel over a checkered emblem pattern,
  a "Clear Time:" bar with the three cups and times): `start`, `replay`, `demonstration`,
  `load_ghost`, `save_ghost`, `record`, `data_logger`, `save_game`, `option`, `exit`;
  `ghost_dlg`: `yes1` / `no1`. Pages `lc000000`..`lc000009`: the test's briefing in the
  panel, a course map on a grid with the course logo, and a scrolling line of tips (the
  `crs` text, a marquee).
- `concept_qm_license_lead` `root`: `start`, `replay`, `record`, `save_replay`,
  `save_game`, `option`, `exit`; the same dialog and briefing pages. No ghost, logger or
  demonstration.
- 2P Battle and the two-player pod race: GT3's `qm_arc_2p_result`. Free Run: GT3's
  `qm_arc_tt_free`.
- Replays are saved after a single race, the one-player pod race, a license test with the
  pace car and a 2P battle; a license test without the pace car and a free run save ghosts
  instead.

The actions are GT3's `qm_*` set (menus §11).

**The single race's loop.** The race is started with the computer driving behind the menu,
and the quick menu opens, **before the first race** too. START runs the race live, then the
cup, the record, the notice (§8) and the run's replay, and the same menu opens again;
REPLAY and DEMONSTRATION run the race in replay and demonstration mode full screen;
SETTING opens the car settings (§10) and, when they changed, the car is built again and
the menu is the one before a run (no replay of the old run).

**The license menus' box.** Without the pace car a race view is drawn in the box
(332, 62, 280, 242) of the 512-line frame, the right half of the panel (GT3's license
menu has it on the left). With the pace car the menu shows the briefing alone.

## §10 Car settings and options

**Settings** (the quick menu's `setting`, single race and free run only; GT3's settings
menu, gt_mode §14, on `menu/setting.mbl`). Tokyo-Geneva's file has 15 pages: `root`
(items `normal`, `item12` → `set_concept_custom` → page `custom`, `exit`), `custom`
(`custom`, `normal` → `set_concept_normal` → `root`, `height`, `suspension`, `damper`,
`brake_balance`, `aero_dynamics`, `gear_ratio` (final ratio only), `tire`, `reset_base`,
`button_load` / `button_save` (setting files, gt_mode §19), `exit`), GT3's slider pages
`set/adjast_*` (`commit_slider`, `retrieve_slider`), `set/reset` (`retrieve_all`), the notes
`dirt_tire_only`, `not_to_change`, `load_warning`, `your_car_is_CVT`, and `status`.
Tokyo-Seoul's and 2001 Tokyo's files keep GT3's 55 pages and add `custom`,
`set/adjast_gear`, `set/adjast_tir`, `set/reset` and `your_car_is_CVT` (60), with the same
items on `root` and `custom`.

- **The garage.** The game keeps GT3's garage (gt_mode §3.1): on a new game one garage car
  for each entry of the five categories' lists **in the game's language** (CLASS_C,
  CLASS_B, CLASS_A, CLASS_S, CLASS_DIRT in that order, the `_UK` twins in British English;
  200 at most; an entry the database has no car for makes none). A car is built as an
  arcade race builds it: the arcade row's parts with their own settings, then the row's
  own tune over them (final ratio, automatic level, LSD, downforce, dampers, camber, toe,
  stabilizers, ride height, springs, TCS, ASM and the engine output factor). It is painted
  in its first color, owns the goods of the parts it wears and of every part the database
  has for the car (a front tire's goods owns its rear twin), and wears the **Normal** set.
  It has no running-in. The cars are saved with the game (§11.2), so a car's settings stay
  with it.
- **The two sets** (goods numbered as GT3's, gt_mode §6.1): Normal fits goods 2, 50, 19, 56,
  67 (the plain brake controller, suspension, gearbox and front and rear tires); Custom
  fits 3, 51, 22, 56, 67 (the brake balance controller, the first suspension kit, the
  variable gearbox, the plain tires), each only when the car has a record of it. Fitting is
  GT3's (gt_mode §6.3): the part's settings go back to its own, each put into the part's
  range (some Concept records start outside their ranges, the GT40's ride height for
  one). On the discs every car has the Custom brake controller, suspension (stage 1) and
  gearbox (stage 3, of which only the final ratio can be set).
- **Which car.** Single Race, Free Run, 2P Battle (both players) and the pod races drive the
  chosen class entry's garage car in the chosen paint; player 1's becomes the current car,
  and the race takes its parts and settings.
- **Opening.** The menu opens on `root` when the current car has the plain brake
  controller (or none), else on `custom`.
- **Actions.** `set_concept_custom` and `set_concept_normal` fit their set on the current
  car at once; `retrieve_all` (the Reset page's OK) puts every setting back to its part's
  own and fits the Custom set again. The sliders are GT3's.
- **Tires** (`set/adjast_tir`: `slider_tire_f`, `slider_tire_r`): three positions per axle,
  each axle on its own. The position is the fitted tire's stage: stage 7 → 0, stage 0 → 1,
  stage 3 → 2, any other none; setting a position fits the first stage that has it. The
  databases give those stages the compounds `soft_gtc_n`, `normal` and `hard_gtc_n`. The
  texts are the SETTING group's `TIRE_SOFT`, `TIRE_NORMAL`, `TIRE_HARD`; `TIRE_DIRT` on a
  dirt course; "----" for another tire.
- **Gear ratio** (`set/adjast_gear`): the final ratio's slider; `gear_max_speed` is the top
  gear's speed at the rev limit, m/s × 3.6 (× 0.62139 in mph) × 100 cut to a whole number
  and printed with two decimals and km/h or mph; "----" for a continuously variable
  gearbox.
- **Refusals**: `set/adjast_gear` → `your_car_is_CVT` when the gearbox has a gear count of
  2; `set/adjast_tir` → `dirt_tire_only` in an arcade race on a dirt course.
- **Triangle** on the Normal and Custom pages ends the menu: their back name is `exit`,
  which this generation's page engine takes like an empty one (sound `return`, result 0).
- **The pages' buttons** are a map list (`menu_map_normal` / `menu_map_custom`): the
  directions pick the button that lies that way, as on Game Status.
- **In the race.** An arcade car's tires change only on a dirt course (its stage 12 tires)
  or, on a dry road course, when the car page chose DRIFT (stage 11, never on Concept);
  otherwise the car runs on the tires it wears: a Concept car's plain tires (stage 0,
  compound `normal`) or the Custom page's. (GT3's arcade rows wear their stage 10 tires.)
  The Custom page's texts stand half the font's size in from their box, so an axle's
  letter and its value stand side by side.

**Options**: GT3's options (menus §10), spread over more layout files, with one assist
choice gone (`assist_type4_off`). `option_limited.mbl` is not used.

**Records**: GT3's records page (menus §12.4), listing the ten license tests.

**Memory-card pages**: GT3's (menus §12) plus page `transfer_money` (§7.6); Tokyo-Geneva has
no `gameload_g`.

## §11 Save data

### §11.1 Card names

- The game save: directory and data file `<prefix>GAMEDATA`, with `icon.sys`, the icon
  (`icon0.ico`) and `thumbnail`, as GT3's (gt_mode §12.2). The prefix is a region code and
  the release's serial:

| Release | Prefix | Minor version | Icon (`menu/icon/`) |
|---|---|---|---|
| 2001 Tokyo | `BI` + `SCPS-15010` | 0 | `ICON_GTC_GT_concept.ico` |
| Tokyo-Seoul (v1.02 and v2.00) | `BK` + `SCPS-56005` | 3 | `ICON_GTC_concept_KR.ico` |
| Tokyo-Geneva, Europe | `BE` + `SCES-50858` | 2 | `ICON_GTC_concept_GNV.ico` |
| Tokyo-Geneva, Asia | `BI` + `SCPS-55903` | 5 | `ICON_GTC_concept_GNV.ico` |

- Other files: `<prefix>` + a type letter + 7 characters: `R` replay, `B` best-lap replay,
  `S` car setting (`G` is the game save). The 7 characters hold 42 bits, 6 a character, low
  bits first, alphabet `A-Za-z0-9+-`: 30 bits from the file, then a 12-bit counter started
  at random and stepped until the name is free.
- Other icons: `ICON_GTC_full.ico` (replay), `ICON_GTC_best.ico` (best lap),
  `ICON_GTC_license.ico`, `ICON_GTC_setting.ico`.
- `icon.sys` is the disc's `menu/icon/icon.sys` with the title at +0xC0 (Shift-JIS; the line
  break word at +6 is its length in bytes) and `icon0.ico` three times, as GT3's. The title
  is the text `MEMORYCARDTEXT_GTCGameData` in the game's language, or the game's built-in
  text where the language's is empty (2001 Tokyo).

### §11.2 Game save layout

The container is GT3's (gt_mode §12.2): a header with sizes, minor / major version, check
and name, the payload stored inverted. Major version **100** (GT3 7); a wrong major is
refused, the minor is not checked. The file is padded to a multiple of 64 bytes (the
padding is not part of the check).

The payload is 0xA68C + 0x204 × cars bytes, in this order (the same on every release):

| # | Block | Size |
|---|---|---|
| 1 | scalar block, GT3's (gt_mode §3) | 0x130 |
| 2 | cars, count at scalar +0x30 (GT3's garage records, one per class entry, with its settings, §10) | 0x204 each |
| 3 | course license records | 10 × 0x154 |
| 4 | arcade results | 144 × 0x14 |
| 5 | time-trial / free-run rankings | 44 × 0x300 |
| 6 | present flags | 0x4C |
| 7 | options | 0x888 |

GT3's wheel inventory, GT-mode license records, race results, course bests, arcade
time-trial challenges, machine-test records, prize flags and suspended championship are
not saved.

- **Scalar block** of a new game: day 1 (+0x00), the game config's `initial` credits
  (+0x10: 18,000; 1,800,000 on 2001 Tokyo), the next garage serial (+0x24), the current car
  −1 twice (+0x28, +0x2C), the car count (+0x30), 1 at +0x34 (GT3's wheel count; no wheels
  follow), the language (+0xC8, written at each save, in the release's own numbering, so
  Korean is 7 on Tokyo-Seoul; 2001 Tokyo does not write it) and the random state (+0xCC).
  The rest is zero. A save's garage serials count from 0, or from the car count when the
  garage was made twice before the save; nothing reads the difference.
- **A car record** is GT3's garage record with: +0x08 the paint's color id, +0x10 the kind
  word 3, the gear words as 16-bit values (no gear = 0x0000FC19, where GT3's hold
  0xFFFFFC19), +0x1B8 the row's engine output factor. A gearbox whose ratios are computed
  fills its own gears; the words after them are left as they were.
- **A license record** is GT3's (gt_mode §12.2): the medal (−1 none, 0 gold, 1 silver, 2
  bronze), then six runs' names (16 bytes each), times (ms; no time 0x157529FF), stamps and
  32-byte blocks (30 '-' when unused).
- **Arcade results**: record `level × 10 + course`, four "no time" words and the best place
  at +0x10 (8 = none).
- **Rankings**: GT3's 0x300-byte records, in the TIME_ATTACK list's order (the ten courses).
- **Present flags**: a byte per name (§7.1); 74 are used.
- **Options**: GT3's block. A new game's differs by release: 2001 Tokyo's and Tokyo-Seoul
  v1.02's are GT3's; Tokyo-Seoul v2.00's and both Tokyo-Geneva releases' start three
  switches the other way (+0x3C and +0x44 on, +0x5C off) and fill the four 0x20-byte wheel
  records after +0x7C8 ({1, 0, 0x400, 0x200, 0x29, 0xD5, 0x2B, 0xDA} twice, then the same
  with 0x25, 0xB4, 0x48, 0xDF); on the Tokyo-Geneva releases the value 0.1 at +0x114 and the
  music list's first shuffle round one bit differently. The music list holds the disc's 18
  race songs.
- **Thumbnail**: zero at +0x00, +0x08 (GT3's credits), +0x14 and +0x18; at +0x10 the number
  of open cars (§7.3: 66 of 116 in a new Tokyo-Geneva game); at +0x1C the completion × 100.

Saved: the ten license records, the arcade results, the present flags, the rankings, the
options, the language and the cars' parts and settings. Not saved: the Ace level and the
last choices of §4.1.

### §11.3 Replay, ghost and setting files

Replay, ghost, track and max-speed files have GT3's versions (major 4, minor 0) and GT3's
replay size (0x41940). Setting files: payload 0x1D0, major 1, minor 0x82D on Tokyo-Seoul
and 2001 Tokyo, 0x82E on Tokyo-Geneva (GT3 0x827).

### §11.4 Checked against the games' own saves

- A new game saved by each release (2001 Tokyo, Tokyo-Seoul v1.02 and v2.00, both
  Tokyo-Geneva releases): this engine's save of a new game is the same payload byte for
  byte, apart from the serials' start and the spare gear words, with the same `icon.sys`,
  icon and thumbnail words (test `concept_disc_ps2_save`). The two Tokyo-Seoul versions'
  new games differ in the options' defaults alone.
- A game with progress (ten license medals and their records, fifteen wins, their
  presents, a car on its Custom set) exported by this engine loads in the European
  Tokyo-Geneva: the auto-load takes it, Course License and Game Status show it (75 %), and
  its Save Game writes the same file back, header, check and every payload byte (test
  `concept_disc_ps2_demo_card` writes such a card for each release).

## §12 Differences between the releases

| | 2001 Tokyo | Tokyo-Seoul | Tokyo-Geneva |
|---|---|---|---|
| Language page at first boot | none | none | six languages |
| Title items | 8, with the A-spec demo | 8, with the A-spec demo | 7 |
| Category page | `ar/select_category` (with car counts) | `ar/select_category` | `ar/select_category2` (road / rally title) |
| 2P car page | `ar/select_car_2p` | `ar/select_car_2p` | `ar/select_car_2p2` |
| Class lists (section 14) | 11: nine with cars (CLASS_C, B, A, S, DIRT, COMPULSORY, LICENSE, PODRACE, PREVIEW) and GT3's two garage classes, empty | the same 11 | 15: the 11 and the `_UK` twins of C, B, A and DIRT for British English (the Asian release: 11) |
| License tests | `LC00000n` | `LC00000n` | `LCeu000n` (Asia `LCtw000n`) |
| Present movies | `opening_GT3UK`, `ending_long_full` | the same | `opening_GTCJP`, `ending_GTC` |
| Attract | 3 demos, opening | 4 demos, opening (v1.02: 3) | demo, TV spot, demo, TV spot, demo, opening |
| Transfer | 1,000,000,000 Cr to `BISCPS-15009` | the same | 10,000,000 Cr to `BESCES-50294` (Asia: as 2001 Tokyo) |
| Settings file | GT3's pages + 5 | the same | 15 pages |
| Save minor version | 0 | 3 | 2 (Asia 5) |
| Game step rate | 60 Hz | 60 Hz | 50 Hz (Europe; timers scaled) |

**Tokyo-Seoul v1.02** (the same serial as v2.00) is the 2001 generation's game: it names a
game config per language and loads its language's, Korean (`concept_kr.gcf`, the one v2.00
uses), and its attract runs three demo races. Its disc's data, save name and save version
are v2.00's; a new game's options are GT3's where v2.00's differ (§11.2).

**The Asian Tokyo-Geneva release** (disc SCPS-55902, saves SCPS-55903): Tokyo-Geneva's game
and data with the `concept_tw.gcf` config and `paramdb_gtc_tw.db`, no `_UK` lists, tests
`LCtw000n`, and the transfer of the Japanese releases.

The session flow, actions, prize pass, Game Status's tests and the races are the same on
every release; car and course lists differ by game config and database only.

## §15 This engine

Where the code is: the rules in `src/game/concept_state.*`, the disc's lists and the
garage cars in `src/game/concept_game.*`, the menus' game side in
`src/game/concept_menu.*`, the front end in `src/menu/flow/concept_flow.cpp` (the course
and car pages are the arcade pickers of `game_flow.cpp`). Progress is kept in the game's
own save file as three record kinds (`clicense`, `cplace`, `cpresent`) in the disc's own
save folder; the memory-card save of §11 is imported and exported by
`src/game/concept_save.cpp` (the save page's FROM A FILE and Export to PS2, `gt3tool
save-import` / `save-export`).

Everything above is implemented, with these choices where the game's behavior is not
established or where this engine does it its own way:

1. Attract: a demo race is the next of the game config's demo events in turn (GT3's rule);
   only the title page itself counts idle time.
2. After a race the game's sequence opens again at its first page, the last choices
   focused.
3. Single Race on a new game: no course has a license medal, so the main menu refuses
   Single Race (as the game does).
4. License records keep the better medal and the faster run (GT3's license record).
5. A prize pass that would grant more than six names keeps the first six in the order
   cars, MOVIE0, TRANSFER, MOVIE1, PODRACE.
6. The Professional milestone's ending movie plays after the last presentation page, not
   right after `concept_ace`; `concept_movieb` (a title card without a button) is not
   shown.
7. Car page: the gauges take GT3's values (handling and acceleration 0.7, speed = top
   speed / 350, menus §9.3) on Concept's bars; the car's name picture is shown, the maker
   logo is not; locked cars are left out; the assist row sits at x 160 / 320 / 480 and the
   row titles 46 units above their buttons.
8. The title list's rows are the item height + 8 apart; the level buttons sit 4 apart,
   centred in the band.
9. The prize car page's name line is the layout's text followed by the car's name; its car
   uses the "purchased" showroom camera.
10. Menu music: the title pages and `ar/replay_theater` are silent, every other page follows
    the arcade pages' rule (menus §7.3).
11. A page the movie table does not list keeps the running movie.
12. Game Status: the focused button is held at the screen's centre while the map can move;
    no opening zoom; the page opens on the present granted last, else on the button it was
    left on, else on Back; the pointer's tip sits on the focused button's centre. The
    scroll's per-frame rate is used at 60 frames a second.
13. 2P Battle: the five classes for both players on any course.
14. The Pace Car page opens on NO until YES was chosen. The pace car's grid slot is 1 and
    the player's 0 (the flying start does not read them).
15. The credit transfer goes into this engine's own saves of any Gran Turismo 3 release,
    chosen on the save page: the release the Concept release names first (the PAL
    release's for the European Tokyo-Geneva, the Japanese release's for the others), then
    the other two, each save's name followed by its release. The question starts on
    CANCEL; nothing is written before its OK. The mark is the save's `concepttransfer`
    record (scalar +0x12C in an exported PS2 save). The thumbnail's figures are not stored:
    the save page computes them.
16. The European Tokyo-Geneva disc's license demonstrations (the menu's box and
    DEMONSTRATION) play the same test's 60 Hz recording on the disc (`demo/LCtw000n`),
    since its own are 50 Hz recordings.
17. The notice is shown once per prize in a visit; the progress and flags are written when
    the visit ends (the game flags them at once; the result is the same list).
18. The pod race goes back to Game Status after it. On the two-player car page either
    player's Cancel on the car row goes back to the pod race's page.
19. A garage car is made when its entry is first raced, not all at once on a new game (the
    car is the same). The pods and the license cars have no settings menu.
20. Boot: no AUTOLOAD panel and no "no memory card" page, as on a Gran Turismo 3 disc: the
    game's save is simply the one played.
21. An exported save's garage serials start at 0 and its spare gear words are "no gear".
22. 50 Hz: the menus and the race step 60 times a second on every disc, and the durations
    used are seconds (the title's 20 s, the examination timers, the medal times).
23. Units: Italian and Spanish keep GT3's PAL units on Concept discs.
24. i.LINK Battle opens the engine's online lobby (menus §8).
