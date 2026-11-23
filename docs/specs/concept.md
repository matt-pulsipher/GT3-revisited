# Gran Turismo Concept — front end and game structure (specification)

Behavioral facts about the three Gran Turismo Concept releases, recovered from their programs
and disc data, for running them on top of the existing menu engine (`src/menu/flow/`) and race
code. The Concept games are GT3's engine with GT Mode removed and a new arcade-style front end
(`menu/concept.mbl`, control class `gt2k_menu::ConceptControl`) put in its place.

Confidence: **H** read from code or disc data, **M** inferred from code and data, **L** guess.
Every inference is marked. Addresses are program addresses (base 0x100000) for reference only.
Where GT3 already does the same thing this page points at [menus.md](menus.md) or
[gt_mode.md](gt_mode.md) instead of repeating it.

Releases and the names used on this page:

| Name | Release | Serial | Analysis | gcf the program loads | Save prefix |
|---|---|---|---|---|---|
| **Geneva** (primary) | 2002 Tokyo-Geneva, Europe (En, Fr, De, Es, It, Pt) | SCES-50858 | `cgeneu` | `gameconf/concept_eu.gcf` (0x21FED0) | `BESCES-50858` |
| **Seoul** | 2002 Tokyo-Seoul, Korea, v2.00 | SCPS-56005 | `cseoul2` | `gameconf/concept_kr.gcf` (0x223670) | `BKSCPS-56005` |
| **Tokyo** | 2001 Tokyo, Japan | SCPS-15010 | `c2001` | `gameconf/concept.gcf` (0x220978 also names `_eu`; `_us` / `_kr` strings unreferenced) | `BISCPS-15010` |

Unless a line says otherwise, an address is Geneva's and the rule is the same in all three
(the three programs were matched function by function; where a routine was only checked in
Geneva the line says so). §9 (race side) and §11 (save data) come from two separate passes
over the same analyses and were spot-checked, not re-derived, for this page.

## Changelog

- 2026-11-13: first version (research only, nothing implemented).
- 2026-11-13 (front end, first pass): §15 added: what is implemented, what the discs settled (the `_S` keys, the
  assist choices, the languages and units of each release, the license medal times), corrections to §4.4, §7.2 and
  §9.1 (class entry race codes on the 2001 Tokyo and Tokyo-Seoul discs), and the choices made where a question is
  still open.
- 2026-11-13 (front end, second pass): the race side read further: the license tests' medal times and demonstration
  files (§9.1), the pace car's rules (§9.2, question 3), the license displays (§9.2), the single-race driver's loop and
  the notice (§8, §9.5), the Game Status map's nodes and scrolling (§5.9, question 4), the transfer's table and each
  release's amount and target (§7.6), how a key's `_S` twin is flagged (§7.4, question 5). §15 lists what was
  implemented and what the race core still needs.
- 2026-11-13 (front end, third pass): the settings menu read and implemented (§10: the garage, the Normal and Custom
  sets, the tire sliders, the top speed line, the refusals, the tires a race gives the car); checklist 27 done. §15
  updated.
- 2026-11-13 (race side, third pass): the pace car is implemented (§9.2, checklist 21). Read again for it and corrected:
  the course license's own conductor answers (soft ground is not out of the course; a contact never ends a test: §9.2
  "Out of the course", "Contact"), in all four programs. The pod race's rules read to the end (§9.2 "Pod race": the stop
  area, the sit counter, the stop count, the prompts, the disqualification); not implemented. §15 updated.

- 2026-11-16 (front end, fourth pass): the memory-card game save read to the byte and checked against saves the four
  releases' originals wrote (§11: names, versions, every block, the garage cars' records, each release's new-game
  options; import and export, checklist 29); corrections to §10 (a garage car is its arcade row with the row's own tune;
  the lists follow the language; what it owns; the setters clamp), §5.7 (the car pages have no tire step; the assist
  step is skipped on a dirt course; the 2 player page has the assist step), §3.1 (the boot warning is the "no memory
  card" page; the AUTOLOAD panel), §5.9 (what is not reached is drawn black), §10 (Triangle on the Normal and Custom
  pages ends the menu), §12 (Tokyo-Seoul v1.02, the Asian Tokyo-Geneva release, the class lists). §15 updated.
- 2026-11-17 (pod race in the game): the pod display's disqualification read (§9.2 "The disqualification on screen",
  "Two players") and the pod race checked in the game from Game Status on Tokyo-Geneva Europe and 2001 Tokyo
  (checklist 24 done, §15).
- 2026-11-17 (front end, fifth pass): the race display's LOGGER mode read (§9.3: GT3's display twice over the Analyzer's
  race, Concept's once under its page; Concept's new steering and accel / brake drawings) and the store monitor
  (§9.2: `RaceCapacityMonitor` is the type 13 sub 1 row, the hybrid car's, not a part without a row); checklist 25 done.
  Tokyo-Seoul v1.02's attract (three demo races) and new-game options (GT3's) applied (§12). The leaves read as far as
  where they are made (§15: inside the race step, from the VU0 random register); checklist 31 still open.
- 2026-11-17 (pod race leftovers): the examination message's wide mode read and ported (§9.2: the wash reaches the
  whole 16:9 screen, not a 4:3 box); 2001 Tokyo's pod texts are its program's Japanese defaults (§9.2); the two-player
  pod race driven through the real menus, the pod race page over Game Status no longer moved by the map (§15).

## 1. Overview (H unless marked)

- The game has no career. The player sees: a title page, a six-item main menu (Course License,
  Single Race, 2P Battle, Free Run, Game Status, i.LINK Battle), options, replay theater and the
  memory-card pages. Everything else is reached from those.
- Progress is three small tables in the game status object: ten **course license** records
  (medal and best run per course), the **arcade results** (best finishing place per course and
  level) and a byte array of **present flags** (one per prize key: 50 cars, two movies, the pod
  race and the credit transfer). There is no money, garage, day counter or license ladder: those
  members still exist in memory (§11) but nothing in the Concept loop reads or writes them (M).
- 10 courses: five circuits and their reverses (`midfield`, `akasaka`, `smtsouth`, `tahitimaze`,
  `autumn`, then the `r_` forms), the same list in the gcf course lists SINGLE_RACE, TIME_ATTACK,
  TWO_PLAYER and I_LINK (tag 8). Course *n* (0..9) is index *n* in SINGLE_RACE; the same index
  numbers the license tests and the prize keys.
- Three single-race levels: **Normal**, **Professional** (item name `hard`) and the hidden **Ace**.
- GT3's GT-mode code (race drivers, dealers' data loaders, championship state) is still linked.
  The outer mode switch in the main loop (table 0x30E580, 34 cases) still dispatches modes 2-8 to
  GT3's GT race / championship / attract demo / GT license / machine test / free run drivers, but
  no Concept menu action stores those mode values (§6), so they are unreachable (M).
  `menu/gt_mode.mbl` is on the discs, byte-identical to GT3's, and no Concept program names it.

## 2. Disc contents the front end uses (H)

### 2.1 Menu projects

New in Concept (all three discs unless marked):

| Project (`menu/<name>.mbl`) | Pages | Opened by | § |
|---|---|---|---|
| `concept` | 26 Geneva, 20 Seoul, 21 Tokyo | `ConceptControl::vf8` (0x103AB8; with `stream/course.ipic`) | 4, 5 |
| `concept_qm_arc_single` | `root` | single-race and pod drivers (quick-menu table 0x2B74F0 +0x24) | 9.5 |
| `concept_qm_license` | `root`, `ghost_dlg`, `lc000000`..`lc000009` | license driver, no lead car (+0x30) | 9.5 |
| `concept_qm_license_lead` | the same 12 names | license driver with lead car (+0x3C) | 9.5 |
| `notice_present` | `root` | 0x1616C8 (NoticePresentControl), from the race drivers | 8 |
| `concept_car_get` | `concept_car_get` | `CarGetConceptControl::vf3` (0x168F48) | 8 |
| `concept_moviea`, `concept_movieb`, `concept_ace`, `concept_pod`, `concept_billion` | one page each, named like the project | 0x1699A0, 0x169B58, 0x169D10, 0x169EC8, 0x16A080 (SimplePageControl) | 8 |
| `premium_get` | `premium_get` | `PremiumGetControl::vf3`, reached only from GT3's license driver (0x12DCD8): dead (M) | - |
| `test_concept_car_get` | `concept_car_get` | no program names it | - |
| `option_limited` (Geneva, Seoul) | 15-20 option pages | no program names it | 10 |

Shared with GT3 and still used: `option`, `setting`, `memorycard`, `records`, `name_entry`,
`replaysave`, `pause`, `result`, `logger`, `language` (Geneva only names it), `arcade_mini`,
`qm_arc_tt_free`, `qm_arc_tt_ltd`, `qm_arc_2p_result`. `arcade.mbl` and `ArcadeControl` are
still linked; nothing in the Concept loop constructs `ArcadeControl` (M).

Layouts are `menu/<LANG>/<project><id>.pmb` as in GT3; `concept.mbl` spreads its pages over
pmb ids 1, 2, 3, 101, 102, 103, 104, 113, 201, 301, 401, 411, 501, 502, 902 (the number after
the page name in `gt3tool menu-mbl`). Language folders: Geneva BR FR GR IT SP PT (+ US, JP, KR,
CN, TW leftovers), Seoul and Tokyo KR / JP plus leftovers.

### 2.2 Pages of `concept.mbl` no retail program names

`ar/push_start_limited`, `ar/top_limited`, `ar/status_limited`, `ar/select_category`,
`ar/select_category_normal` (Geneva), `ar/push_start_350z`, `ar/push_start_airtrek`,
`ar/push_start_sl500` (Geneva), `ar/push_start_honda`, `ar/select_class`, `ar/cannot_ending`,
`ar/select_ending` (Tokyo): show-floor and single-maker builds, selected by the other gcf files
on the disc (`concept-350z.gcf`, `concept-honda.gcf`, `motorshow.gcf`, ...; their tag 0 carries a
title idle time, the retail gcf's is 0). Not part of the retail game (M: no string reference in
any of the three programs; the Geneva program names `ar/select_category2` and
`ar/select_car_2p2`, Seoul and Tokyo `ar/select_category` and `ar/select_car_2p`).

### 2.3 Movies

The full-screen movie table is built at start-up (Geneva 0x1B3FA8 → table 0x2BB298, Seoul
0x1B53C0 → 0x2BEC98, Tokyo 0x1B33F0 → 0x2BAB28; 0x18 bytes per row: path, 640, 512, flags):

| Row | Geneva | Seoul | Tokyo | Used for |
|---|---|---|---|---|
| 0 | `stream/opening.pss` | same | same | boot, end of attract |
| 1 | `stream/opening_GTCJP.pss` | `stream/opening_GT3UK.pss` | `stream/opening_GT3UK.pss` | present MOVIE0 (§7.4) |
| 2 | `stream/ending_GTC.pss` | `stream/ending_long_full.pss` | `stream/ending_long_full.pss` | present MOVIE1 and the Professional milestone (§8) |
| 3 | `stream/title_full.pss` | same | same | not on the Concept discs; unused (M) |
| 4 | `stream/demoGT3A.pss` | same | same | `go_aspec_demo` (Seoul, Tokyo; the file is not on Geneva) |
| 5, 6 | `stream/tvcm_GTCJP1.pss`, `..2.pss` | same strings, files absent | - | Geneva attract (§3.3) |

Page background movies (the `mpeg` widgets; table built by 0x1070E8): `arcade_menu_gtc.pss`
(title, main menu, level), `category_gtc.pss` (category), `car_gtc.pss` (car pages),
`status_gtc.pss` (Game Status), `<course>_full.pss` (course page, by focused course: the
`_full.pss` suffix is appended at 0x106038).

## 3. Boot, title and attract

### 3.1 Boot (main 0x1000D0, H)

1. Engine init (0x103278).
2. Geneva and Seoul: a **saved-language probe** (0x145830, Seoul 0x145E38) reads the game save
   on the card before anything is shown and returns its language index, or -1 without a save
   (M: read from its calls, not line by line). Tokyo has no such step. Seen in the original (Tokyo-Geneva): with a
   save on the card the language page does not come up.
3. Geneva only: with -1 the **language page** runs first (0x129370: `LangControl` on
   `menu/language.mbl` page `root`; items `br`, `fr`, `it`, `gr`, `sp`, `pt`, action `set_lang`,
   as PAL GT3's language page). Seoul and Tokyo call an empty routine here (0x1299C0 / 0x129560).
4. Memory-card warning screen (0x148630, `MemorycardWarningControl`), as GT3. Seen in the original (Tokyo-Geneva):
   it comes up only without a memory card: a "CAUTION" panel, "No Memory Card (PS2) detected.", "Please insert a
   Memory Card (PS2) with a free capacity of at least 130KB of free space or game progress cannot be saved.", "Do you
   still wish to continue?" and one OK button. With a card in the slot nothing is shown.
5. Movie row 0 (`opening.pss`).
6. Memory-card mode 0x1B, the boot auto-load, as GT3. After a load the language is set from the
   status (+0xD0; 0x1291B0). Seen in the original: with a game save on the card an "AUTOLOAD" panel with a progress bar
   and then "COMPLETE" (page `autoload` of `memorycard.mbl`), about two seconds; with a card that holds no save, or no
   card, nothing.
7. The main loop.

### 3.2 Main loop (H)

Each pass runs the Concept loop (0x1015F0, Seoul 0x101560, Tokyo 0x1014D8). It returns nonzero
when a **mode value** was stored in the status (+0x315C4) by a menu action; the main loop then
dispatches it (table 0x30E580) and comes back:

| Mode | Set by | What runs |
|---|---|---|
| 9 | `go_option` | options (0x13AAC0) |
| 0xB / 0xC | `go_replay_theater` / `go_demonstration` | replay file page / demonstration list (0x1009E0 with 0 / 1), as menus §1 "Replay Theater" |
| 0xD | `go_logger` | the Analyzer (0x136238) |
| 0x18 / 0x19 / 0x1A | `go_file_operation` / `go_save_game` / `go_load_game` | memory-card modes as GT3 (after 0x1A the language is set from the loaded status) |
| 0x1C | game index 7 (`load_guest_garage`, no item) | nothing |
| 0x20 | `go_aspec_demo` (Seoul, Tokyo title item) | Tokyo: movie row 4; Geneva: no case (M for Seoul: not read) |
| 0x21 | not found | `RaceLinkDemo` / `RaceArcadeDemo` (0x101040), §9.2 |
| 0xE / 0xF | `go_ending_easy` / `go_ending_normal` (Tokyo page `ar/select_ending`, unreferenced) | no case in Geneva |
| 2-8 | nothing | GT3's GT-mode drivers (unreachable, M) |

Inside the Concept loop a 10-state machine (jump table 0x30E620) runs one menu session
(state 1, 0x1035F0) and then the race the session asked for (states 2-9, §9.1).

### 3.3 Attract (H)

The title times out: the session (0x109768) gives the control an idle limit of **1000 frames** while the title page
runs (0x1097A4; 1200 in the 2001 Tokyo program, 0x10972C: 20 seconds at each release's frame rate) and -1 for the main
menu. The control's per-frame routine (`ControlBase::vf2` 0x1C8FB0) clears its idle counter on any input, counts it up
otherwise and, with a limit above 0, sets the "session ends" flag once the counter passes it. The gcf's idle time is not
read for this.

When the Concept loop returns 0 (no mode: the title timed out) the main loop runs, each step
going back to the title as soon as it is interrupted:

- Geneva (the Asian release's main loop has the same shape): demo race, `tvcm_GTCJP1.pss`, demo race,
  `tvcm_GTCJP2.pss`, demo race, `opening.pss`.
- Seoul: four demo races, `opening.pss`. Tokyo: three demo races, `opening.pss`.

A demo race is 0x100A90 (Seoul 0x100A70, Tokyo 0x1009C0): an arcade demo on one of the gcf's
tag-12 events (17 `demo_gtc_*` in Geneva), limited to gcf section 0's second word (90 s; GT3
105). Section 0's first word, GT3's title idle time (20 s), is **0** in the retail Concept gcfs
(153 / 183 in the show-floor ones); the title's limit is the program's constant above.

## 4. The menu session (`ConceptControl`)

`ConceptControl` (vtable 0x2FFDB8; constructor 0x1034C8) is GT3's `ArcadeControl` grown to hold
the title page and the status map. It has its own action table (§6), widget table (§5.1) and
focus hooks, and is driven by straight-line "sequence" routines that open one page, wait for it
to end, and move on: there is no page stack for the set-up screens, exactly like GT3's arcade.

### 4.1 Session state (H)

Control members: +0x3C6C **game index**, +0x3C70 **level** (0 Normal, 1 Professional, 2 Ace),
+0x3E08 **pod choice** (0 none, 1 one player, 2 two players), +0x38EC category selector,
+0x3904 car selector, +0x3A24.. color / transmission / tire / assist choices (+0x3A9C.. player 2's),
+0x27C / +0x280 idle counter and "session ends" flag.

Status members the session keeps between visits (not saved, M: outside the saved blocks of §11):

| Status offset | Meaning |
|---|---|
| +0x315C4 | mode value for the main loop (§3.2) |
| +0x314C0 | the "current car" record (set by `set_concept_car`, by the race descriptor) |
| +0x31ADC | level chosen last (the level page's focus) |
| +0x31AE4 | category chosen last |
| +0x31AEC | lead-car flag (1 with, 0 without) |
| +0x35F50, +0x35F54 | title sub-page shown last and its focus (the session reopens it) |
| +0x35F94.. | "resume" record: a pending game (0x210E80 tests it, 0x210E10 sets it, 0x210E00 clears it), +0x35F98 game index played last (main menu focus), +0x35FA0 license test chosen last, +0x35FB4 / +0x35FB8 a stored item name (0x210E60) |
| +0x35FF8 | Ace level revealed (§7.5) |
| +0x36000 | car availability table, 5 x 0x40 bytes + counts at +0x140 (§7.3) |

### 4.2 Session flow (0x109768 → 0x109490, H)

1. Rebuild the car availability table (0x211178).
2. Unless a game is pending in the resume record, run the **title page** (0x109A50, §5.2).
   It ends with `go_arcade_menu` (mode 0, go on) or with a mode value (leave the session).
3. Loop (0x109490): unless a game is pending, run the **main menu** `ar/top` (0x117AB8); its
   actions only store the game index (0x114D60):

| Game index | Action | Item on `ar/top` (label) | Sequence |
|---|---|---|---|
| 0 | `single_race` | `single_race` (Single Race) | 0x108E08 |
| 1 | `compulsory` | none | 0x108F68 → `ar/select_compulsory` (page absent on Geneva and Seoul): GT3's time trial, unreachable |
| 2 | `license` | `license` (Course License) | 0x108FD8 → 0x111258 |
| 3 | `time_attack` | `time_attack` (Free Run) | 0x109048 |
| 4 | `two_player_battle` | `two_player` (2P Battle) | 0x109158 |
| 5 | `goodies` | none | 0x117BB0 (`ar/goodies`, page absent) |
| 6 | `status` | `game_status` (Game Status) | `ConceptControl::vf7` 0x110D78 |
| 7 | `load_guest_garage` | none | mode 0x1C |
| 8 | `i_link_battle` | `ilink_battle` (i.LINK Battle) | 0x109260 |

   Item order on the page: `license`, `single_race`, `two_player`, `time_attack`, `game_status`,
   `ilink_battle`. Cancel on `ar/top` runs `cancel_select_game` (clears two globals) and returns
   to the title.
4. A sequence that ends with "go" for games 0-4 and 8 stores the game in the resume record and
   ends the session with the race descriptor (§4.4); games 5-7 loop back to the main menu.
   After the race the next session finds the pending game and re-enters its sequence directly,
   so the player comes back to the set-up screens, not the title (H for the mechanism, M for
   exactly which page reopens).

Sequences (each page returns "confirmed" or "back"; back goes one page back, back on the first
page returns to the main menu):

- **Single Race**: `ar/level` → `ar/select_course` (skipped when SINGLE_RACE has fewer than 2
  courses) → `ar/select_category2` → `ar/select_car`.
- **Free Run**: `ar/select_course` → `ar/select_category2` → `ar/select_car`.
- **2P Battle**: `ar/select_course` → `ar/select_car_2p2`.
- **Course License**: `ar/select_license`; an item links to `ar/lead_car`, whose two items
  (`with_lead_car` / `without_lead_car`) set the lead flag and start the test.
- **i.LINK Battle**: `ar/connect`, `ar/wait_other`, then course / category / car, as GT3's
  i.LINK flow (menus §1 "Arcade"); `RaceLinkBattle` with event `ALNK0001`, up to 8 entries.
- **Game Status**: `ar/status` (§5.9). Its items either show a car (`set_concept_car` →
  `ar/show_room`), end the session with a stored present name (`go_present`, §7.4), or open
  `ar/podrace`, whose items `go_podrace_1p` / `go_podrace_2p` set the pod choice; then 1 runs
  `ar/select_car` (0x111020) and 2 runs `ar/select_car_2p2` (0x111070) before the race.

### 4.3 After the session (0x1035F0, H)

If a present name was stored: `MOVIE0` plays movie row 1 and reopens the session; `MOVIE1`
plays row 2 and reopens it; `TRANSFER` runs memory-card mode 0x1E (§7.6) and reopens it;
`PODRACE` (stored by the pod actions) returns the session's result, the pod race.

### 4.4 The race descriptor (`ConceptControl::vf9` 0x108448, H)

As GT3's arcade descriptor (menus §1 "Race start"): car, course, race code, color,
transmission, tire and assist choices, player 2's for 2P. Race codes (0x114178):

| Game | Race code |
|---|---|
| Single Race | the chosen class entry's code (Tokyo-Geneva: `A_<car>_0001`, dirt entries `AD_<car>_0001`, UK lists `A_uk_<car>_0001`; 2001 Tokyo and Tokyo-Seoul: four-letter codes such as `AEHC0001`, dirt `AEUD0001`, from their game configs) with its **last character raised by the level**: `..1` Normal, `..2` Professional, `..3` Ace |
| Course License | the test's label from gcf CLASS_LICENSE: `LCeu000n` Geneva, `LC00000n` Seoul and Tokyo |
| Free Run | `ATAF0001` |
| 2P Battle | `A2P0001` |
| i.LINK | `ALNK0001` |
| Pod, 1 player | CLASS_PODRACE's entry code `APOD0001`, course = POD_RACE's `route5_pod`, car `gtcmr_pod_tmv_01` |
| Pod, 2 players | `APO20001` |

The session's result word 1..8 selects the Concept loop's states 2..9 (§9.1) (M: game index + 1
for games 0-4, then i.LINK, pod 1P, pod 2P; the mapping was read from the state table, not from
the code that stores the word).

## 5. Pages

"Plain" = items, layers and art only, the generic page engine runs it. "Code" = the control
draws or lays out part of it. Screens were rendered with `gt3tool menu-shot` from Geneva's
`menu/BR/`.

### 5.1 Widgets (H)

`ConceptControl`'s button-list table (0x114C20 → 0x2B5918): `menu_top` (0x1146A8, the title's
scrolling selector as GT3's), `menu_game` (0x1147E8, `GameButtonList` / `GameButton`),
`menu_difficulty` (0x1149E0, `DifficultyButtonList`), `menu_category` (0x114AC0,
`CategoryButtonList`), `menu_license` (0x114B70, `LicenseButtonList` / `LicenseButton`),
`menu_map` (0x1148A0, `StatusButtonList` / `StatusButton`, a `MapButtonList` like GT3's GT-mode
map), `menu_goodies`, `menu_level` (GT3's).

Its widget-class table (0x117288), new names: `achievement_ratio`, `arrow_dec`, `arrow_inc`,
`available_car_num_A/B/C/D/S`, `car_color_box` (`ColorBox2`), `car_description`, `car_logo`,
`car_name`, `car_name_license`, `car_render_from`, `category_dirt`, `category_road`,
`color_name_box`, `connect_status`, `countdown`, `diagonal_in`, `diagonal_out`, `flash_item`,
`license_car`, `license_time_gold/silver/bronze`, `mpeg_class`, `mpeg_course`, `show_status`,
`spec_aspiration`, `spec_displacement`, `spec_drive_type`, `spec_engine_type`, `spec_height`,
`spec_length`, `spec_max_power`, `spec_max_torque`, `spec_weight`, `spec_width`; `blink`
(`BlinkItem`) on the title. GT3's `end_button` / `end_message` are still registered.

Focus hooks (0x1129C8 → 0x2B58B8): `ar/connect` → item 1; `ar/lead_car` → item 0 when the
lead flag is set, else item 1.

### 5.2 `ar/push_start` — title (pmb 2; code: selector)

Art: the GT Concept logo with the release line ("2002 TOKYO-GENEVA"), the Polyphony logo and the
copyright line over black, behind it the `mpeg` movie. The items sit in the `menu_top` window
(at 140,75) as GT3's title selector, with two `blink` arrows above and below.

Items (Geneva): `game_start` (`go_arcade_menu`), `load_game`, `save_game`, `file_operation`,
`replay_theater` (link `ar/replay_theater`), `option` (`go_option`), `credit` (link `ar/credit`).
Seoul and Tokyo have `aspec_demo` (`go_aspec_demo`) between `replay_theater` and `option`.

- `ar/replay_theater` (pmb 3, plain): `load_replay`, `demonstration`, `logger`; back action
  `set_focus_replay_theater`; as GT3's page.
- `ar/credit` (pmb 2, plain): 13 tips (`tip1`..`tip13`, layers `layer1`..`layer13`; 10 on Seoul
  and Tokyo): the credits as a row of selectable entries, each showing its layer.

### 5.3 `ar/top` — main menu (pmb 1; plain with `menu_game`)

"MAIN MENU" heading; six rows, each a round icon and a label: Course License, Single Race,
2P Battle, Free Run, Game Status, i.LINK Battle; a help line per item (`layer1`..`layer6`); the
GT Concept logo bottom right behind two `diagonal_in` / `diagonal_out` wipe widgets. Opens
focused on the game played last (+0x35F98).

### 5.4 `ar/level` — level (pmb 101; code: hidden third button)

"LEVEL SELECTION" heading, a band across the middle (`menu_difficulty`) holding the level
buttons, a help text per level (layers `normal2`, `pro`, `ace2`). Items `normal`, `hard`, `ace`;
their action strings (`dif_easy`) are never read, the level is the focused index.
`DifficultyButtonList` (0x118B38 / 0x118CB8) leaves the **last item out** unless the Ace flag
(+0x35FF8) is set; while the page is up, holding **L1 and R1** (logical keys 0x2000 and 0x10000)
adds it (0x118E18) and sets the flag (0x10FF20). Opens on the level chosen last.

### 5.5 `ar/select_course` — course (pmb 102; code)

As GT3's course page (menus §9): "COURSE SELECTION", the focused course's logo, map and
description from the `hidden` layer (`<course>`, `_logo`, `_doc`, `_mini`, `_mini1`), "Total
Length" (`total_length`), left / right arrows, the icon strip (`mini_courses`, `cross_bar`), the
course movie (`mpeg_course`). New: the strip cursor is a `MiniCourseCursor` (0x1104B0) that
puts a **place badge** (`item_1st`..`item_6th`) on each course icon from the arcade result at
the chosen level (0x110748), and in Single Race the selector skips courses whose **course
license has no medal** (`SmartCourseIndex::vf4` 0x103FC0: game index 0 and course < 10 →
selectable only when the license record's medal is not -1). Other games offer all ten (H for the
test; M that a skipped course is simply not shown). Single Race's strip has the wide icons (`_mini`, with the badge),
cut at the strip's ends; Free Run and 2P Battle the small ones (`_mini1`), all ten fitting (seen in the original).

### 5.6 `ar/select_category2` — category (pmb 113; code: counts and locks)

Heading "CAR SELECTION" or, on a dirt course, "RALLY CAR SELECTION" (`category_road` /
`category_dirt` title widgets); five round buttons with labels: NEW CARS Pacific, NEW CARS
Europe, Concept Cars, Dream Cars, Racing Cars (items `C`, `B`, `A`, `S`, `D`; help layers
`c`, `b`, `a`, `s`, `d`). Category *k* maps to gcf class CLASS_C, CLASS_B, CLASS_A, CLASS_S,
CLASS_DIRT (0x21E8F8, 0x21E940, 0x21E988, 0x21E9D0, 0x21EA18); with language index 2 (UK
English) and the list present, the `_UK` twin (Geneva only). CLASS_DIRT is the racing-car
category, not a dirt class: every class holds road entries (`gtcmr_<car>`) and dirt entries
(`gtcmd_<car>`).

For each category the page counts the available cars of the course's kind (road or dirt;
0x211318). A category with none shows the `question` art and cannot be chosen (not applied in
i.LINK). Seoul and Tokyo use page `ar/select_category`, the same five items with a count
(`available_car_num_*`) under each button.

### 5.7 `ar/select_car`, `ar/select_car_2p2` — car (pmb 104, 411; code)

GT3's car page (menus §9) with Concept's art: "CAR SELECTION", the car picture and maker logo
(`tuner_<maker>`) from the `hidden` layer, color chips (up / down), a drivetrain icon (`ff`, `fr`,
`mr`, `rr`, `four_wd`), power and weight values (`ps`, `kgm`), the icon strip (`mini_cars`).
New: three **bar gauges** (`BarGauge`, 0x10E448: Handling, Speed, Acceleration over
`handling_gauge`, `speed_gauge`, `acceleration_gauge`) and an option step after the
transmission (`auto_*` / `manu_*`): **driving assist**, three choices `assist_0` / `assist_1` / `assist_2` (0x10C2A8).
The steps (the pages' step machine, 0x10C978 for a player of the 2 player page; H): transmission, tires, assist, but
the tire step's test (0x10FC50; the same two instructions in all five Concept programs, where GT3's PAL program tests
the course at 0x114640) always answers "skip", so **no Concept car page asks RACING / DRIFT** (the `racing_*` /
`dri_*` art is still in the layouts), and the assist step is skipped on a dirt course (0x107E98, the test GT3 uses
for its tire step). Locked cars (§7.3) are left out of the
strip (M). Messages `disable_road_doc`, `disable_dirt_doc`, `disable_no_car_doc` (0x1042F0, page
`ar/disable`) explain a car that cannot be used on the course.

The 2-player page has each player's own class row (`mini_classes`, `mini_classes_2p`), car row
and option buttons (`*_2p` twins), as GT3's `ar/select_car_2p`, with the assist step for each player
(`assist_<n>_yes|no[_2p]`, `assist_title[_2p]`; the `assist_on/off_*_2p` texts are not named by the program).

### 5.8 `ar/select_license`, `ar/lead_car` — course license (pmb 201, 1; code: list, medals, times)

"COURSE LICENSE" over a grey swoosh with the license emblem; a "TEST CAR" panel top right
(`car_name_license`, layers `layer1`..`layer10`), the test list in a strip (`menu_license`,
items `tt01_mini`..`tt10_mini`, arrows `arrow_dec` / `arrow_inc`), per test a description
(`license_doc<nn>`), the course logo and map (`layer11`..`layer20`), and a "Clear Time:" bar with
gold, silver and bronze cups and their target times (`license_time_gold/silver/bronze`, read
from the event). A `LicenseButton` (0x1192D0) shows the medal already won (`item_gold` /
`item_silver` / `item_bronze`). All ten tests are open from the start (H: the list is built from
CLASS_LICENSE's count with no condition). Opens on the test chosen last (+0x35FA0).

`ar/lead_car`: two framed buttons, "Pace Car YES" (`with`) and "Pace Car NO" (`without`), a help
line each; back to `ar/select_license`.

### 5.9 `ar/status`, `ar/show_room`, `ar/podrace` — Game Status (pmb 301, 1; code: map)

A map 1280 x 584 scrolled under a 640 x 448 window (`menu_map`, the `mpeg` movie behind):
ten course logos in five colored sectors, from each a chain of three round nodes with laurel
frames (`show_status` widgets `C<nn>L_stat`, `C<nn>N_stat`, `C<nn>H_stat`), in the middle
"GAME STATUS", "Achieved :" with the completion figure (`achievement_ratio`, §7.7) and a round
Back button (`game_status`), and along the bottom a plate of gift boxes (`prize_plate`).
Hidden art: 50 car thumbnails (84x65, named by car), `license_gold/silver/bronze`,
`rank_1`..`rank_6`, `pointer`, `MOVIE1_middle`.

Items (45): `C<nn><L0|L1|N0|H0>` for nn = 00..09 (action `set_concept_car`, link `ar/show_room`,
highlight layer `area01`..`05`, `area21`..`25`), `MOVIE0`, `MOVIE1`, `TRANSFER` (action
`go_present`), `PODRACE` (link `ar/podrace`), `game_status`. Item names are parsed by 0x119800
(`C%02d%c%d`, `C%02d%c`, `MOVIE%d`): kind L 0, N 1, H 2, MOVIE 3, T 4, P 5.

State tests (0x119958, H):

| Thing | Shown as reached when |
|---|---|
| node `C<nn>L` | the course's license has a medal |
| node `C<nn>N` / `C<nn>H` | (0x119958's test: a result recorded at Normal / Professional, place < 6; what the node draws is below) |
| item `C<nn>L0` / `L1` | the license has a medal / a gold medal |
| item `C<nn>N0` / `H0` | the course is won at Normal / at Professional |
| `MOVIE0`, `TRANSFER` | every course is won at some level (0x2180B0) |
| `MOVIE1` | every course is won at Professional or Ace (0x2180F8) |
| `PODRACE` | all ten licenses have a medal (0x216A40) |

What a node draws (the `show_status` widget factory 0x115E68, a switch on the name's kind letter, H):

| Node | Art (from the `hidden` layer), centred on the widget's box |
|---|---|
| `C<nn>L_stat` | `license_gold` / `license_silver` / `license_bronze` by the license's medal (0x2169D8: 0 / 1 / 2); nothing without a medal |
| `C<nn>N_stat` | `rank_1` (the name is built with a constant 1) once the course's place at Normal, Professional or Ace is 0; else nothing |
| `C<nn>H_stat` | `rank_1` once the place at Professional or Ace is 0; else nothing |

`rank_2`..`rank_6` are not used by this page. A prize-car button (`StatusButton::vf3` 0x11A250) draws through the item's
own draw when its "reached" member is set, pulsing while it is focused (alpha x 2 x (a blink value + 0.5)); the other
path, for a button that is not reached, is a long run of inline vertex work that was not read to the end. Seen in the
original (Tokyo-Geneva, a game with some prizes): a prize car that is not reached is its picture in black (a
silhouette), and a present that is not reached is its gift box in black. On Tokyo-Geneva and Tokyo-Seoul the car pictures are `hidden` elements named by the car
(84x65, Seoul 80x62); on 2001 Tokyo each button carries its picture itself.

The map list (`MapButtonList::vf2` 0x1B6630, H): the list keeps a pointer object at the focused button's place; each
frame the scroll's two values move towards that place, clamped to the list window's centre +- half its width and height
(the `menu_map` widget: 640 x 136), by `(target - value) x 0.1 x 1.2` (0x1CD298; constants 0x333E28, 0x333EC0), and are
clamped again; a third value runs from 0.5 to 1 the same way (0x119FA8, a zoom: not reproduced). The ranges equal the
map's room to move under the window (1280 - 640 and 584 - 448), so the focused button stays at a fixed point of the
screen until the map's edge is reached (M: the screen's centre; the buttons' draw was not read). The directions pick the
next button as GT3's GT-mode map does (the same class). The hidden `pointer` art is the list's cursor.

`ar/show_room` (back `ar/status`): the chosen prize car rendered in 3D (`car_render_from`), its
description line, maker logo, color box and color name, and a ten-line specification table
(drive type, weight, height, width, length, engine type, aspiration, displacement, max torque,
max power; `spec_*`). `set_concept_car` (0x115120) resolves the prize key to its car (0x21FA48)
and makes it the current car.

`ar/podrace` (back `ar/status`): the maker's logo, a "pod Race" banner with two pods from above,
and two round buttons "1P pod Race Entry" (`solitaire`, `go_podrace_1p`) and "2P pod Race
Entry" (`battle`, `go_podrace_2p`).

### 5.10 Other pages

- `ar/connect` (pmb 501, code as GT3: `connect_status`, `countdown`, `my_name`; items
  `i_link_play`, `i_link_live`), `ar/wait_other` (pmb 502).
- `ar/disable` (pmb 1): a one-button message page.

## 6. Actions of `ConceptControl` (H)

Table built at start-up: Geneva 0x1152B8 → 0x2B5958, Seoul 0x115560 → 0x2B9358, Tokyo 0x115528 →
0x2B5248; the same 31 names in all three. "GT3" = same behavior as GT3's arcade / title action
(menus §1).

| Action | Handler G / S / T | Effect |
|---|---|---|
| `single_race`, `license`, `time_attack`, `two_player_battle`, `status`, `i_link_battle`, `compulsory`, `goodies`, `load_guest_garage` | 0x114D40 / 0x114FE8 / 0x114FB0 | store the game index (§4.2) |
| `cancel_select_game` | 0x114EE8 / 0x115190 / 0x115158 | clear two session globals (GT3) |
| `go_arcade_menu` | 0x114F58 / 0x115200 / 0x1151C8 | mode 0: leave the title for the main menu |
| `go_option` | 0x1150A0 / 0x115348 / 0x115310 | mode 9 |
| `go_replay_theater`, `go_demonstration`, `go_logger` | 0x114F80, 0x114FB0, 0x114FE0 / 0x115228, 0x115258, 0x115288 / 0x1151F0, 0x115220, 0x115250 | modes 0xB, 0xC, 0xD |
| `go_file_operation`, `go_save_game`, `go_load_game` | 0x115070, 0x115040, 0x115010 / 0x115318, 0x1152E8, 0x1152B8 / 0x1152E0, 0x1152B0, 0x115280 | modes 0x18, 0x19, 0x1A |
| `go_aspec_demo` | 0x1150D0 / 0x115378 / 0x115340 | mode 0x20 |
| `go_ending_easy`, `go_ending_normal` | 0x114EF8, 0x114F28 / 0x1151A0, 0x1151D0 / 0x115168, 0x115198 | modes 0xE, 0xF (GT3's arcade endings; no item on a referenced page) |
| `i_link_play`, `i_link_live` | 0x114EB8 / 0x115160 / 0x115128 | GT3 |
| `jump_to_event` | 0x115100 / 0x1153A8 / 0x115370 | calls 0x103B48 (no item uses it) |
| `set_garage_car` | 0x125688 / 0x125B90 / 0x125678 | GT3's (no item uses it) |
| `set_concept_car` | 0x115120 / 0x1153C8 / 0x115390 | item name = prize key → its car becomes the current car; the name is stored (+0x35FB8) |
| `go_present` | 0x1151B8 / 0x115460 / 0x115428 | store the item name; the session then ends and §4.3 acts on it |
| `go_podrace_1p`, `go_podrace_2p` | 0x1151F0, 0x115228 / 0x115498, 0x1154D0 / 0x115460, 0x115498 | pod choice 1 / 2, store the name `PODRACE` |
| `with_lead_car`, `without_lead_car` | 0x115260, 0x115290 / 0x115508, 0x115538 / 0x1154D0, 0x115500 | lead flag 1 / 0 |

Not in the table, handled by the page managers as in GT3: `set_focus_replay_theater`, the
`qm_*` actions of the quick menus (0x166130, GT3's set), the settings actions (§10), `set_lang`,
the records page's `delete`. `dif_easy` on `ar/level` is never read.

## 7. Progression and unlocks

### 7.1 Records the rules read (H)

- **License records**: status +0x23510, 10 x 0x154 bytes, GT3's license record (gt_mode §9).
  Medal -1 none, 0 gold, 1 silver, 2 bronze (0x2169D8 reads it by course index).
- **Arcade results**: status +0x24900, 0x14-byte records indexed `level x courses + course`
  (courses = SINGLE_RACE's count, 10); the best place is at +0x10, 0 = won; the level index
  comes from the race code's last digit, 1 → 0, 2 → 1, 3 → 2 (0x217BC8). 0x217E10(level, course)
  reads the place.
- **Present flags**: status +0x2FBA8, one byte per key, 0 on a new game (0x216EC0); the byte
  index is the key's position in the gcf's tag-15 list, then 0x46 `MOVIE0`, 0x47 `MOVIE1`,
  0x48 `PODRACE`, 0x49 `TRANSFER` (M for "tag 15 order"; the four fixed indexes H). 0x217140
  tests a key, 0x213D20 / 0x217000 set a list of keys.

### 7.2 Prize keys (gcf tag 15, H)

50 entries named `C<course 00-09><N0|H0|L0|L1>` plus ten with the suffix `_S`; each names one
class slot `CLASS_<X>#<index>` (index into that gcf class list), i.e. one car. An `_S` key names the **dirt entry
(`gtcmd_<car>`) of the car its base key names** (`C00N0` → `CLASS_B#002` `gtcmr_mini_cooper_s_02`, `C00N0_S` →
`CLASS_B#018` `gtcmd_mini_cooper_s_02`; nine of Geneva's ten, `C02N0_S` names another car's dirt entry), so a car that
has both entries opens on both kinds of course at once (H, from the game configs). Geneva's `_S`
keys: `C00N0_S`, `C01L1_S`, `C02L1_S`, `C02N0_S`, `C03L0_S`, `C04H0_S`, `C05L0_S`, `C07H0_S`,
`C08L1_S`, `C09L0_S`. The status page builds the names with `C%02d%c%d`.

### 7.3 Which cars can be chosen (0x211058, H)

For each of the five categories the availability table holds one byte per class slot: **locked**
when some prize key names the slot (`%s#%03d` → 0x21FD20) and that key's present flag is clear;
every other slot is open from the start. The table is rebuilt when a session starts, on a new
game and after a load. (Each class entry also carries an "enabled" word and a level, 1 and 0 for
every car on the retail discs.)

### 7.4 The prize pass (0x2153C0; Seoul 0x218B08, Tokyo 0x2161C0, H)

Run after a single race or a license test with the race code and the course. The race code gives
a **mode** and a course index (0x215240): a code starting `LC` → mode 0, test index from its
last two digits; otherwise mode 1, 2 or 3 from its last digit and the course's index in
SINGLE_RACE. For every prize key whose flag is clear and whose course is this one:

| Key | Granted when |
|---|---|
| `L0` | mode 0 and the license now has a medal |
| `L1` | mode 0 and the medal is gold |
| `N0` | mode 1, 2 or 3 and the result just recorded for this code is a win |
| `H0` | mode 2 or 3 and that result is a win |

Then the four specials, each once:

| Key | Granted when | Present |
|---|---|---|
| `MOVIE0` | modes 1-3 and every course is won at some level | watch movie row 1 from Game Status |
| `TRANSFER` | the same condition | the credit transfer to a GT3 save (§7.6) |
| `MOVIE1` | modes 1-3 and every course is won at Professional or Ace | watch movie row 2 |
| `PODRACE` | mode 0 and all ten licenses have a medal | the pod race |

The result is a list of up to six names with a kind each (0 car, 1 pod, 2 transfer, 3 MOVIE0,
4 MOVIE1), sorted by kind, with a byte per name. Flagging the list (0x213D20) sets a name's own flag when its byte is
set (0x216FC8) and otherwise (0x217000) the flag of **every key whose name starts with it**: a base key and its `_S`
twin are flagged together (H), so both entries of the car open at once and the car is presented once.

The pass takes the flag array to test as its last argument: null = the status's own. The race drivers pass null after a
run and set the flags at once (§8 step 2); the milestone sequence passes a **copy of the flags taken when the driver
started**, so it finds again everything the visit opened (§8 step 3).

### 7.5 The Ace level (H)

Not a saved unlock. The level page shows two buttons until L1 + R1 are held on it (§5.4); the
flag lives outside the save, so it is cleared on every start (M). The Professional milestone
page (§8) tells the player the button combination. Ace races are the `..3` event rows (§9.1).

### 7.6 Credit transfer to GT3 (H; card side from the save pass)

Game Status item `TRANSFER` → memory-card mode 0x1E (in `WorkingMenu::vf8` 0x29BC98; Seoul
0x29F4F0, Tokyo 0x29B5A8). The game edits a **GT3 save on the card in place**; nothing is read
into Concept:

- Target save: `BESCES-50294GAMEDATA` (Geneva), `BISCPS-15009GAMEDATA` (Seoul, Tokyo), read
  with its `thumbnail` (descriptor 0x322F60).
- Rejected with the read-error dialog when the CRC fails or the save's major version is not 7.
- Rejected with `MemorycardAlreadyTransferedDialog` (0x146070, layer `already_window` of page
  `transfer_money`) when the save's scalar block already holds 0x12345678 at +0x12C: the
  "already transferred" mark lives **in the GT3 save**.
- Otherwise (0x21D860): write the mark; add the amount to the credits; set all six
  license-prize bits (+0xC0 |= 0x3F) and the bronze-license award bit (+0xC4 |= 0x80000); give
  every license test without a medal a bronze; update the thumbnail's credits, license and
  completion figures; reseal the file.
- Amount: **10,000,000** Geneva (0x29BFB8), **1,000,000,000** Seoul, Tokyo and the Asian Tokyo-Geneva release
  (0x29F814, 0x29B8CC, 0x29C4FC; hence the page name `concept_billion`). The Asian release's target is
  `BISCPS-15009GAMEDATA` too.
- Nothing is written to the Concept status; the item stays usable (M).

The routine is driven by a table (0x322F60, the same words in all four programs): major version 7; credits a 64-bit
word at scalar +0x10, added without a limit; the car count at +0x30 and the wheel count at +0x34 place the license
records (0x130 + 0x204 x cars + 0x44 x wheels); +0xC0 |= 0x3F; +0xC4 |= 0x80000; the mark at +0x12C; 6 x 8 records of
0x154 bytes, each with a medal word under 0 set to 2 (bronze; the record's time is left alone); the thumbnail's credits
(+0x08) copied, its license word (+0x14) set to 4, its completion (+0x1C) raised by 0.001908 x 100 for every test
changed, capped at 100. The card page `transfer_money` holds the texts: "Transfer money and licenses to GT3A-spec data."
(root), "OK to transfer money and licenses?" with OK / CANCEL, "There is no GT3Aspec data present!", "Transferring...",
"COMPLETE", "Money and licenses have already been transferred.".

### 7.7 Completion figure (0x21B508, H)

(arcade points + license points) / 40, capped at 1: per course 1 point for a win at any level
and 1 more for a win at Professional or Ace; per license 1 point for a medal and 1 more for
gold. Shown on Game Status and stored (x100) in the save thumbnail.

### 7.8 Where the numbers live

| Number | Place |
|---|---|
| courses, classes, cars per class, tests, prize keys | gcf tags 8, 14, 13, 15 |
| laps, opponents, rubber band, medal times per event | paramdb event rows (§9.1) |
| "won" = place 0; "has a result" = place < 6 | program (0x217980, 0x119958) |
| level lists for the specials | program: {0,1,2,3} at 0x322DB8, {1,2,3} at 0x322DC8 |
| special key names | program: 0x3368C0 `MOVIE0`, 0x3368D0 `MOVIE1`, 0x322900 `TRANSFER`, 0x3368E0 `PODRACE` |
| transfer amount, GT3 save name | program (§7.6) |
| pod stop area (608 m, 33 m) | program: 0x336818, 0x33681A (§9.2) |
| demo time limit 90 s, title idle 0 | gcf section 0 |

## 8. Presentations after a run (H unless marked)

The single-race driver (0x131730) and both license drivers (0x1300F8, 0x130CB0) do, in order:

1. Record the result: the arcade result (0x217D78 with course, car and place) or the license
   medal (0x12AA18 → 0x216A08; result code 2 / 3 / 4 = gold / silver / bronze) and the license
   record run with its name entry (`name_entry`, `RecordsMenuArgLicenseConcept`).
2. A won single race first shows GT3's arcade cup page (0x136AC0 with the event's class byte +0x1D3: 1, 2, 3 on the
   Normal, Professional and Ace rows, the classes GT3 numbers easy, normal, hard). Then the prize pass against the
   status's flags; if it grants anything, **`notice_present`** (0x161880): a black page with a 500 x 72 window, a gift
   icon, the title "Info", the one line "You will receive a prize after this session." and an OK button; the granted
   names are flagged right after it (0x213D20), so a later run of the visit gives no second notice. The run's replay
   follows, then the quick menu again.
3. When the player leaves the quick menu (and a race of the visit was finished), the **milestone sequence**
   (0x13A390; Seoul 0x13A9A0, Tokyo 0x13A448) runs the prize pass again, against the flags as they were when the driver
   started (§7.4), and shows, in this order:
   - kind 0, cars: **`concept_car_get`** (0x139EA8 → `CarGetConceptControl`): orange page,
     "Congratulations" arched across the top (`CongratulationMessageItem`), the car as a
     3D model (`render_model`, `CarsConceptItem`, background `menu/prize_get.img`), a reason line
     from one of the layers `normal_clear`, `pro_clear`, `gold_record`, `bronze_record`, "You
     have won this car!" with the car's name (`car_name_0`..`3`, one layer per car), and a
     blinking next / OK button (`NextMessageItem`): one car after another;
   - kind 3: **`concept_moviea`** ("Normal Level has been cleared!", the movie is now in Game
     Status);
   - kind 4: **`concept_ace`** ("Pro Level has been cleared!" and the L1 + R1 hint), then
     **`concept_movieb`** (an "ENDING MOVIE" title card, no button), then movie row 2;
   - kind 1: **`concept_pod`** ("Now you can enter the Pod Race!");
   - kind 2: **`concept_billion`** (the credits for GT3 game data, amount in the picture).
   Each is a `SimplePageControl` page: a full-screen picture on a layer named `delay` and one
   OK button (item `ok` / `count1`), 2 frames of wait between pages.
4. Store the list's last name in the session record (so Game Status opens on it, M) and flag the list in the copy.

The pod races and 2P races record nothing and grant nothing.

## 9. Race side

### 9.1 From menu choice to race (H)

`racemode.db` is unchanged from GT3 (the same 16 rows in all releases). The Concept loop's
states store an arcade kind (+0x315CC) and call one driver each:

| State | Choice | Kind | Driver G / S / T | Race class, conductor | Quick menu | racemode label |
|---|---|---|---|---|---|---|
| 2 | Single Race | 7 | 0x131730 / 0x131BB0 / 0x12D2C8 | `RaceArcade`, `RaceArcadeInformation` | `concept_qm_arc_single` | SINGLE |
| 3 | (time trial, unreachable) | 0 | 0x12C830 / 0x12CCA0 / 0x12EDE0 | `RaceTimeTrial` + `SimpleExamDisplay` | `qm_arc_tt_ltd` | TIME_TRIAL |
| 4 | Course License, no lead | 1 | 0x1300F8 / 0x130530 / 0x12BCD0 | `RaceLicense`, `DynamicsConductorLicense` | `concept_qm_license` | LICENSE |
| 4 | Course License, lead car | 1 | 0x130CB0 / 0x131130 / 0x12C870 | `RaceTraining`, `RaceTrainingInformation`, `DynamicsConductorTraining` | `concept_qm_license_lead` | LICENSE |
| 5 | Free Run | 8 | 0x12BD18 / 0x12C188 / 0x12E2D0 | `RaceFreeRun` | `qm_arc_tt_free` | FREE_TIME_TRIAL (M) |
| 6 | 2P Battle | 2 | 0x132068 / 0x1324E8 / 0x131F80 | `RaceSplitBattle` | `qm_arc_2p_result` | 2P_BATTLE / _WIDE |
| 7 | i.LINK Battle | 4 | inline | `RaceLinkBattle` (0x183518) + `Multilink` | - | SINGLE |
| 8 | Pod, 1 player | 7 | 0x12FA10 / 0x12FE48 / 0x12B5F0 | `RacePOD` + `RacePODDisplay` | `concept_qm_arc_single` | SINGLE |
| 9 | Pod, 2 players | 2 | 0x132800 / 0x132C80 / 0x132718 | `RacePOD2P` + `RacePOD2PDisplay` | `qm_arc_2p_result` | 2P_BATTLE |

Events (paramdb event table: index 31 on the Concept discs, 32 in GT3; still 504 bytes with
GT3's fields, 531 rows in Geneva's `paramdb_gtc_eu.db`):

- Single race `A_<car>_000N`: N = 1 Normal 2 laps, 2 Professional 3 laps, 3 Ace 5 laps (4 on
  dirt); 6 cars on tarmac, 2 on dirt (`AD_` rows). The row's course is a placeholder; the menu's
  course is used. Rubber band (sampled on one car's rows): Normal slows computer cars ahead by
  15 % between 20 and 40 m, Professional by 5 % between 40 and 120 m, Ace none.
- License `LCeu000n` (Geneva; `LC00000n` Seoul / Tokyo; the Geneva database also holds the other
  regions' rows with other cars and times): 1 lap, flying start at 100 km/h from 99 m before the
  line, medal times at +0x1C4 / +0x1C8 / +0x1CC as GT3: each {u8 minutes, u8 seconds, u16 milliseconds} (0x18FAE0 and
  four more readers; Midfield's 1'27, 1'30, 1'37). Entry pool: the test car twice, then a
  pace car `gtcpc_r34_gtr_v_gtcolor_pc_99_<400|450|650|800|dirt>`.
- Demonstration runs: `demo/<code><suffix>` (2001 Tokyo 0x1486F8, Tokyo-Seoul 0x148E70), `demo/<code><suffix>_p` on
  the European Tokyo-Geneva program (0x148818: its 50 Hz recordings), the suffix GT3 PAL's `_uk` rule (0x1487B8). The
  Tokyo-Geneva discs hold `LCeu000n_p` (50 Hz), `LCtw000n` (the Asian release's tests: the same cars, 60 Hz) and
  `LC00000n` / `_p`.
- `APOD0001`: `route5_pod` (680 m), 3 laps, 6 cars, all `gtcrr_pod_tmv_01`, test-kind byte
  (+0x19A) 5, a value GT3 does not use.

### 9.2 What the Concept-only classes add

**`RaceTraining`** (constructor 0x187B20; a `RaceSinglePlayer`, where `RaceLicense` is a `RaceSolitaire`: no ghost
recorders): the license run with a second car. Entry 0 is the player; entry 1 is the event pool's third car (the pace
car) in its pool paint, or the car's first paint when the pool names none (`RaceTrainingInformation::vf11` 0x187908).
It builds the same `RaceLicenseDisplay`, `LicensePause` and `RaceLicenseBGM` as `RaceLicense`: the screen of a pace-car
run is the license test's (target times, time, no race HUD).

`DynamicsConductorTraining` (vtable 0x307580, 45 slots) is `DynamicsConductorLicense` (0x30C6D0) with five slots
replaced (H; Concept's slots, two more than GT3's from slot 27 on):

| Slot | License | Training | Meaning |
|---|---|---|---|
| 5 | returns 1 | returns 0 | a ghost car: none with the pace car |
| 11 | 0 (base) | returns 1 | the event's rubber band is loaded (GT3's virtual 11) |
| 12 | 0x1E8CF8: each car against the walls (0x1FA6C8) | 0x1E8CC0: the whole collision step, cars against cars too (0x1FA2F8) | collisions |
| 26 | 0x1E8530 | 0x1E84A0 | control sources: a car with an input device is the player's (pad; the AI in play modes 2 and 3), a car without one is the computer's (source 2), as in a single race |
| 34 | base | 0x2A4390: the race configuration's +0x22 | the pass mode, as a single race reads it |

Everything else is the license conductor's:

- **Start** (slot 27, 0x1E61B0): the event's flying start (mode 2, the event's distance and speed, 0x1E86D8). When the
  conductor has no ghost (slot 5 = 0), car *i* > 0 starts further on by `trunc(i x speed / 3.6)` metres (0x3342AC =
  3.6): the pace car starts **one second ahead at the start speed**, 27 m at the Geneva tests' 100 km/h (72 m before
  the line when the player is 99 m before it), at the same speed.
- **Judging** (slot 14, 0x1E65D0: the per-car judge 0x1F4208 for every car): the course-out end (0x1F3F38) and the
  finish (0x1F3FE8) return at once for a car whose control source is 2, so only the player's car is judged; the pace
  car leaving the road or finishing does nothing.
- **Out of the course** (the base rule 0x1D5700, the conductor's slots 39 and 40): counts only for a pad-driven car
  (control source 0). The Concept license conductor answers **1 for the pit stretch and 0 for soft ground** (0x1E6658,
  0x1E6660; GT3's answers 1 and, off dirt, 1), and 0 for its pit mode (0x1E6648; GT3 3): a test ends "out of the course"
  only with all four wheels on pit-lane ground inside the course's pit stretch. Grass, sand and curbs never end it. The
  same answers in all four programs (2001 Tokyo's table has GT3's slot numbers: its slots 35 to 39 answer 0, 0, 1, 0, 0).
- **Contact** (the hit rule 0x1D5820, license and training alike): only car 0; a hit by another car (kind 1) never
  sets the flag, a wall hit sets it when its strength x the car's +0x5C is over 10, kind 2 always. But the Concept
  judge (0x1F4208; 2001 Tokyo 0x1F4808, Tokyo-Seoul 0x1F6F40, the Asian Tokyo-Geneva 0x1F41A0) **clears a hit flag and
  goes on** where GT3's (0x1EF780) ends the test: no contact, with a wall or with the pace car, fails a Concept
  license test.
- **Passing, stopping**: nothing reads the cars' order. The pace car is a computer car on the AI line held back by the
  event's rubber band while it is ahead (ai.md §9.3: slow-down 82 %, from 20 m ahead to 60 m; 25 / 70 m on Akasaka,
  15 m on Autumn); once passed it is an ordinary car behind. The medal, the record and the prize pass are the same
  calls as without it.

**`SimpleExamDisplay` / `SimpleExamMessages`**: GT3's time-trial display and examination
messages renamed, also polling race event 0xB; one message ("Disqualified", font `sw_20`,
jingle 0x20), stage timers 50 / 1500 / 200 frames at Geneva's 50 Hz (GT3 60 / 1800 / 240). Built by `RaceTimeTrial`
(unreachable) and, as its base, by `RacePODDisplay`: the pod races' display. The license runs do not use it.

**Pod race** (`RacePOD` = `RaceArcade` with another display; the rules are in the conductor,
active only for test kind 5): a 3-lap, 6-car race on a 680 m course in which every car must **stop once per lap** in a
stop area. The display's set-up (`RacePODDisplay` 0x1AF650) queues three notes when the race is live: green `sw_16`
event-display messages of 3 s each, the first 2 s in (so they run through the start's countdown): the realtime texts
`RACEDISPLAY_PodRaceStart1` .. `3` (the program's defaults are Japanese; the discs' text files hold each language's).
The two-player display queues none. The prompts are car messages, drawn where WRONG WAY is (the display's message draw
0x1907F0, a switch on the message number): **0x10** `RACEDISPLAY_PodSitDown` in amber (0x801880D0), **0x11**
`RACEDISPLAY_PodOverrun` in red (0x800000F0), each for as long as the rule sets it. `RACEDISPLAY_PodSitDownOK` is the
kind 7 information note of a stop, `RACEDISPLAY_Disqualification` the examination message. No special hardware: pad input, `RacePOD2P` is ordinary split screen. **Reachable in a retail game**:
earn a medal in all ten course licenses, then Game Status → pod race (§7.4). The rules (H, Tokyo-Geneva Europe's
addresses; the other programs were not read for this part):

- **Set-up** (0x1EE190, from the race's initialisation): with test kind 5 the area is the program's two constants, start
  608 m (s16 at 0x336818) and length 33 m (u8 at 0x33681A), the start wrapped into the lap; the rule is off when either
  is 0 or the area would cross the start line. Each car's stop count starts at 0.
- **Sitting** (0x1DC118 each step, for a car whose model is a pod, car +0x42 = 1): a counter (car record F +0x518) goes
  up by one a step, to 29 at most, while the car is stopped (0x1DC0A8: the handbrake fully on, F +0x434 at 1, and the
  speed, forward and sideways together, at most 0x3340D0 = 1 km/h), and down by one a step otherwise. The pod **sits**
  at 29. The counter also lowers the wheels' travel as it rises (0x1DC190). A restart sets it to 0 or 29 (0x1DC058).
- **Owed stops** (0x1EE598): the car's lap count (F +0x438) less its stop count.
- **Each step** (0x1EE7B8, from the single-race and split-screen conductors' clock) for every car that owes a stop:
  a pad-driven car that is not handed to the computer (F +0x538 clear) and has F +0x46B clear: with all four wheels on
  stop-area ground (ground flag bit 2) a sitting pod **gets its stop** (0x1EE600) and one not sitting yet is shown
  message 0x10; with a wheel off that ground and the car past the area's end, message 0x11. A computer-driven car
  (control source 1 or 2, or a pad car handed over): past the area's start, a sitting pod gets its stop; otherwise
  message 0x10 with all four wheels on the area's ground. Both messages have display time 0 in the message table
  (0x2BBA54): shown only while the rule sets them.
- **A stop** (0x1EE600): the count goes up (255 at most); for a pad-driven car (always in a two-car race) an information
  note of kind 7 is posted.
- **At the line** (0x1EE6E8, from the lap handlers before anything else): a car that owes two stops or more is handed to
  the computer to stop (0x1EF6B0: F +0x538 = 5), its lap count is put back by one, its pod's expression set to 7
  (0x1DB7E8) and race event 0xB posted for it: disqualified. The lap is not counted. The lap counts from the first
  crossing of the line after the standing start, so a pod that misses one lap's stop is disqualified at the end of
  that lap.
- **The disqualification on screen** (`SimpleExamDisplay`'s update 0x1869F8 and the two routines of its stage 2 / 3,
  0x186750, 0x1869A0; `SimpleExamMessages` 0x186178): every step the display looks through the race's events 0xA and
  0xB for its own car (the display's car byte, so another car's disqualification shows nothing); on one it posts the
  fail jingle (event 0x15, 0x20), waits 50 steps, starts the message `RACEDISPLAY_Disqualification` (sw_20; it fades
  in by 0.02 a step, is held 150 steps and fades out), then waits 200 steps or a confirm (pad bit 0x40) and posts
  event 1: the race ends, a run that did not finish, and the arcade race's replay and quick menu follow. These are
  GT3's time-trial FAIL steps at 50 steps a second (GT3: 60, 180 held, 240), so the game shows the time trial's FAIL
  presentation with the disc's text: "FAIL" in the 2002 releases' English. The car meanwhile has the stop driver.
  The message step by step (the 60 Hz builds' numbers; ours: `race::ConceptExamMessage`, §15 item 24):
  - **Display** (`SimpleExamDisplay`'s stage 2 routine, sub-stages at +0xA8FD, timer +0xA8F4): sub 0 sets the timer
    to 60 (Geneva Europe 50) and counts it down that step; at 0 or below (the 61st step) the timer is set to 1800
    (1500; nothing reads it after) and the message starts (0x186120: active, state 0, fade 0, the text); sub 2 runs
    the message's update and its timer's count each step until its done byte (+1) is set, then clears its active
    byte (no longer drawn), sets the timer to 240 (200) and goes to sub 3: a confirm, or the timer at 0, sets the
    display's stage 3, and the next step event 1.
  - **Message** (`SimpleExamMessages`, update 0x186178 with a jump table of five states, timer +8 counted down by
    0x1862D0 after it): state 0 sets the fade (+0x10, and +0x14 with it) to 0 and goes on into state 1 in the same
    step; state 1 adds the step (a float: 1/60 = 0x3C888889 in 2001 Tokyo and both Tokyo-Seoul builds, 0x3C888888 in
    the Asian Tokyo-Geneva, 0.02 in the European one) until the fade is 1 or more (61 steps in EE singles, the last
    one about 1.0167; Geneva Europe 51), then the timer is set to 180 (150); state 2 waits for it, then sets the fade
    to exactly 1 and, with the byte +0x1C set, flags the message held (+0x1D) and stays, else goes to state 3; state
    3 takes the step off until the fade is below 0 (61 steps: the EE's subtraction drops the step's low bits in the
    alignment, so the last fade drawn is just above 0; Geneva Europe 51), then sets the done byte, the timer to 120
    (100, not read) and state 4 (nothing).
  - **Drawing** (0x1862F0, while the message is active): the wash (160, 160, 255) with alpha (int(fade x 128) & 0xFF)
    >> 1 in the GS's 0x80 units over the screen, then the text (160, 192, 64) with alpha int(fade x 128) & 0xFF at
    `sw_20` 1.5 x 1.5, centred on x 320, in two copies at y 224 + s and 224 - s (one when s is 0), s = int((1 - fade)
    x 30) in doubles: the copies meet as the message fades in and part again as it fades out. The fade-in's last
    step draws alpha 130 (above the GS's 1). In the game's wide mode (the flag 0x2BDD9C, which the options' wide mode
    sets through 0x219DB0, and which the HUD's 106-unit push 0x198530 and the split battle's side-by-side halves read
    too) the text's x scale is 0.75 (0x188B70: 0.75 or 1, also the licence messages'), so the text keeps its 4:3
    shape on the stretched 16:9 picture, and the wash runs from x = w / 2 - (w / 2 x 4 / 3 + 1) for
    int(2 x (w / 2 x 4 / 3 + 1)), w the display's width +8: -107 for 855 on 640, the frame widened to the whole 16:9
    screen (the same reach as the HUD's pushed rows), not a 4:3 box. Ours (`ConceptExamMessage::draw`'s `wide`, on a
    screen wider than 4:3, where the HUD uses the 16:9 rules): the wash over the whole screen (or half) and the text in
    the 640 x 448 frame kept 4:3 in its middle, as `race::Hud` lays its frame out; a side-by-side half keeps the
    glyphs' full-screen size, as the original's font scale does not depend on the half.
  - So, at 60 steps a second from the jingle's step: the message from step 61, faded in at 122, faded out from 303,
    gone at 363 (5 s on screen); a confirm from step 364; event 1 at 604 without one (10 s). 2002 Tokyo-Geneva
    Europe's 50 Hz program: gone at 303, event 1 at 504: the same times within a step. GT3's
    `TimeTrialExaminationMessages` (NTSC-U 0x1884F8) has the same states (1/60, 180, 120) without the byte +0x1C;
    ours keeps GT3's time-trial FAIL as it was (the licence presenter's draw and hold).
- **Two players** (`RacePOD2PDisplay`, 0x1AF708 / 0x1AF9D0 / 0x1AF930): the display holds two pod displays, one per
  half, each with its own car, prompts and message. Its set-up marks both halves' messages to stay: no jingle, and the
  message is held after its 150 steps instead of fading, which flags the half; its update posts event 1 once both
  halves are flagged (both players disqualified). With one player disqualified the race ends as a 2P battle does.
- The prompts' and notes' texts are in the text file of the 2002 releases (`RACEDISPLAY_PodSitDown`, `PodOverrun`,
  `PodSitDownOK`, `PodRaceStart1` .. `3`, `Disqualification`); 2001 Tokyo's file has none of them, so the game shows
  its program's defaults: the realtime text group `RACEDISPLAY` (63 slots, registered by 0x1AC630 with the slot names
  at 0x2BA820 and the texts at 0x2BA920), Japanese in EUC (the disqualification 失格です, the notes and prompts as the
  2002 releases' Japanese pool words them). Ours: `RaceScreen::race_text` reads the language's value, then the
  program's default (`game::find_realtime_group`, the pattern GT mode already uses for `DATABASEPARTS`), then American
  English, for every text drawn as game text; the disqualification is drawn as game text now. Seen on 2001 Tokyo:
  `PodRaceStart1` .. `3`, `PodSitDown` (amber), `PodOverrun` (red), `PodSitDownOK` and 失格です. The same rule gives
  GT3 NTSC-J its program's Japanese for the game-text race messages its file lacks (the whole `RACEDISPLAY` group:
  `PolePosition`, `StartQualify`, `ReadyQualify`; `SHORTMENU_SESSION_BRFORE` / `AFTER` 第 / 戦). The free run's notes
  (`FreeRunReady`, `FreeRunHelp`), the time trial's and the licence's FAIL words (`TimeTrialFail`, `ExamFail`,
  `ExamTimeOver`) and the lap-down note (`LapDiffInfo`) are game text too now (the event display and the FAIL overlay
  draw game text, as the original's), so GT3 NTSC-J and 2001 Tokyo show their programs' Japanese for them (NTSC-J's
  18-slot `RACEDISPLAY` has the free run and FAIL words; it has no `LapDiffInfo` slot: its lap-down routine (NTSC-J 0x192EB8)
  prints a fixed "%d Laps", our fallback) and PAL's accented words keep their own bytes (they had been turned to UTF-8
  and drawn byte by byte by the game-text drawer). PAL's `LapDiffInfo` texts have no "%d" ("Laps", "Tours",
  "Runden", ...): its routine (PAL 0x195408) prints the text as its format, so the count is not shown, and ours now does
  the same. The two-player event display draws game text as the one-player one does.
- The computer cars' drivers read the same area and owed count (0x1F64A0, 0x1F72A0: not read).
- Pods also carry a mood (F +0x519.. , 0x1DB7E8 / 0x1DB980: one of eleven, by what happens to the car), separate from
  the rules, shown on the model and heard (public issue 403). Every frame RacePS2Base's frame hands each car's look to its
  model's callback (0x1A9B00: 0x1DC570 the wing, 0x1DBC68 the lamps, 0x1DBD30 the tail, 0x1DBDE0 / 0x1DBE20 the mood;
  the car model callback's ids 6-10, formats/car_model.md "Callback morphs") and plays the mood's CLUT group on the model's
  texture sets (0x220EE0 restarts the group when the mood changes, 0x220D38 plays it one frame). The pod (ty0150 on
  every disc) draws its tail's lean and nod, five lamp colours and a face per mood from them; its face set animates in
  groups 1-10. A new mood (special[6], 0x1DBE60) keys the car's mood voice (0x1A5860, the frame's +0x394): moods 1-10
  play `pod_wink`, `pod_genki`, `pod_kyoro`, `pod_okoru`, `pod_odoroku`, `pod_iya`, `pod_sabishii`, `pod_kowai1`,
  `pod_naku`, `pod_nemui` (the same switch in every build), at the car, placed min(1, 32/d) from the camera, no channel
  gain. Ours: `sim::exact::special_look`, `VehicleState::Special`, `race::car_callback_values`, `CarVisual::step_look`,
  `CarSfx`'s mood voice.

**`RaceSplitBattleBase`** (0x181B70): the part of GT3's split-screen race shared by
`RaceSplitBattle` and `RacePOD2P`.

**`Multilink`, `MultilinkInput`, `MultilinkLocalInput`**: GT3's i.LINK session code as classes,
created inside `RaceLinkBattle`; reachable through i.LINK Battle.

**`RaceLinkDemo`**: a `RaceLinkBattle` without pause and input, built only for mode 0x21
(0x101040); no setter of that mode was found: a linked attract race for show floors (L).

**`RaceCapacityMonitor`**: the hybrid system's store monitor (0x197CC0 in 2002 Tokyo-Geneva Europe; every later build
has it): the display's row of type 13 sub 1 (the display's builder gives type 13 sub 0 the accel / brake monitor, sub 1
this one), in the 2 player and LOGGER modes. Drawn for a car of drive layout 7 alone (0x1DA790: the car's layout byte),
a half-alpha black box of the row's size, a dark inner box 2 in, `meter_sign_dualnote` (the Honda Dualnote's sign) at its
bottom tinted blue, above it the store over its capacity (0x1DA7C0) as a bar from the bottom: blue (0, 0.2745, 0.8)
while the flow display (car + 0x14A1) is at 0 or above, (0.55, 0.1, 0.25) while the store is drawn (H).

**`LeafParticle`, `SparkParticle`**: new particle kinds in GT3's `ParticleManager`
(`race/particle.img`): leaves per wheel on road with surface flag bit 0x20, sparks from the emission 0x1A9788
(GT3's underbody spark points; from Tokyo-Seoul v2.00 on the car body's plane: physics §14.3 `SparkEmit`).

### 9.3 HUD (`racedetail.db`, H)

Rows: GT3 228, Seoul and Tokyo 236 (identical to each other), Geneva 238. By mode:

- SINGLE, TIME_TRIAL, TIME_ATTACK, FREE_PRACTICE, ENDULANCE, MAX_SPEED_CHALLENGE, 0_400: GT3's
  rows unchanged. Single race, pod 1P, free run use GT3's HUDs.
- LICENSE: GT3's rows, the three target-time lines (type 8) with width 0 instead of 8.
- 2P_BATTLE, 2P_BATTLE_WIDE: 17 rows (GT3 16): a new row of type 13 sub 1 at (20,212) in 4:3 and
  (413,392) wide (L: a second pedal bar); message width 360; steering 27x25; pedals height 50;
  several 4:3 positions moved.
- LOGGER (mode 15, the Analyzer's REPLAY): 6 rows (GT3 4): a dashboard along the bottom edge (steering centered at
  (320,420), the bar tachometer, the accel / brake boxes, gear, a digital speed, the store monitor, type 13 sub 1).
  GT3 (RaceSolitaire::vf46, 0x187D30) draws its display twice while the data logger plays, track A's car on the whole
  frame and track B's 560 to the right (its rows: the left edge, y 70 .. 225, over the race's view above the page's
  band); Concept (RaceSolitaire::vf47) draws track A's car alone, on the whole frame, before the Analyzer's page (the
  race's view is 13 + 256 lines, 0x159A98; the page's windows are drawn after the display and cover most of it).
- REPLAY: one car-name row for every view at (16,340); meters type 3 sub 1 for every view;
  steering centered at (320,420), 75 x 68; accel / brake boxes at (68,435), 30 x 100; four GT3 parts have no row and
  are absent.
- Concept's steering and accel / brake monitors are new drawings on new pictures: the steering
  `replay_sign_gtforce` (in 0x80D0D0D0) on the row's point, scaled by the row's size; the pedals two boxes of the row's
  size 4 apart (brake, accel), each half-alpha black with a dark inner box, its sign (`replay_sign_brake`,
  `replay_sign_accel`) at the bottom, the bar solid up to the lower of the value now and a frame ago and fading out to
  the higher (RaceABMonitor::vf5 0x196B48; GT3's `steering`, `accel_gage`, `brake_gage` are not on the discs).
- DEMO: Seoul and Tokyo = GT3; Geneva adds view mode and a car name at (16,431).
- Pod prompts use the existing car-message display.

### 9.4 Results (H)

- Single race: place → arcade result; prize pass; notice; on leaving, the milestone sequence
  (§8). The result scene is keyed by the event's class byte (+0x1D3) when first.
- License (both drivers): medal, record table entry with name entry, prize pass, notice,
  milestone sequence. All of it is written by the race driver, none by the menu.
- 2P, pod 2P: the 2P result menu only. Pod 1P: nothing recorded (its driver has none of the
  single-race driver's result calls).

### 9.5 Quick menus (H for the items; conditions open)

- `concept_qm_arc_single` `root` ("Single Race" heading, a six-row grid list with car names
  `qm_list_car_name1`..`6`, a help line): `start`, `replay`, `save_replay`, `demonstration`,
  `setting`, `save_game`, `option`, `exit` (back action `qm_exit`).
- `concept_qm_license` `root` ("Course License Test" panel over a checkered emblem pattern, a
  "Clear Time:" bar with the three cups and times `qm_license_time_gold/silver/bronz`): `start`,
  `replay`, `demonstration`, `load_ghost`, `save_ghost`, `record`, `data_logger`, `save_game`,
  `option`, `exit`. `ghost_dlg`: `yes1` / `no1`. Pages `lc000000`..`lc000009` (pmb 201-210): the
  briefing shown in the panel, a course map on a grid with the course logo and a scrolling text
  line (`ShortmenuConceptLicenseControl` / `Render`, 0x12A090).
- `concept_qm_license_lead` `root`: `start`, `replay`, `record`, `save_replay`, `save_game`,
  `option`, `exit`; the same dialog and briefing pages. No ghost, logger or demonstration.
- 2P and pod 2P: GT3's `qm_arc_2p_result`. Free run: GT3's `qm_arc_tt_free`.
- `qm_arc_result` is named only by a routine without callers.
- Replay saving: single race, pod 1P, lead-car license, 2P. Ghost load / save instead of replay
  save: license without lead car, free run.

The actions are GT3's `qm_*` set (menus §11).

The single-race driver's loop (0x131BF4 on, H): the race is restarted in play mode 2 (the computer drives, behind the
menu), the quick menu runs, and its choice is dispatched (table 0x3129F0): START runs the race live, then the cup, the
record, the notice (§8) and the run's replay (play mode 1), and the loop comes back to the same menu; REPLAY and
DEMONSTRATION run the race in play modes 1 and 2 full screen; SETTING opens the settings and, when they changed, clears
the driver's switches (no replay of the old run). The menu therefore opens **before the first race** too. The switches
(a replay exists after a run, ...) are the driver's locals handed to the menu, as GT3's.

The license menus' panel (`ShortmenuConceptLicenseRender::vf3` 0x1337F8): without the pace car a race view is drawn in
the box (332, 62, 280, 242) of Geneva's 512-line frame, the right half of the panel (GT3's license menu has it on the
left); the render skips it when its lead flag is set, so the pace-car menu shows the briefing alone. The briefing page's
`crs` text (the tips) is a marquee (style bit 0x10).

## 10. Car settings and options

- **Settings** (quick-menu `setting`, single race and free run only): `menu/setting.mbl` under
  `SettingControlConcept` (0x1490D8). Geneva's file has 15 pages: `root` (items `normal`,
  `item12` → `set_concept_custom` → page `custom`, `exit`), `custom` (`custom`, `normal` →
  `set_concept_normal` → `root`, `height`, `suspension`, `damper`, `brake_balance`, `aero_dynamics`,
  `gear_ratio` (final ratio only), `tire`, `reset_base`, `button_load` / `button_save` = setting
  files, `exit`), the slider pages `set/adjast_*` (GT3's, gt_mode §14: `commit_slider`,
  `retrieve_slider`), `set/reset` (`retrieve_all`), the notes `dirt_tire_only`, `not_to_change`,
  `load_warning`, `your_car_is_CVT`, and `status`. Seoul's and Tokyo's files keep GT3's 55 pages
  and add `custom`, `set/adjast_gear`, `set/adjast_tir`, `set/reset`, `your_car_is_CVT` (60); their `root` and
  `custom` pages hold the same items. The action table (0x153CB0 → 0x2B9A10) still lists all of GT3's part actions.
  The rules below are H, read in the European Tokyo-Geneva program; the tables were read in all four.
  - **The garage.** The game keeps GT3's garage: on a new game (0x21A868 → 0x21ABA8) one garage car for each car of
    the five categories' lists **in the game's language** (0x21EFE0: CLASS_C, CLASS_B, CLASS_A, CLASS_S, CLASS_DIRT
    in that order, the `_UK` twins in British English; 200 at most; an entry the database has no car for makes none).
    A car is built as an arcade race builds it (0x233CF0 -> 0x233E58: the arcade row's parts with their own settings,
    then the row's own tune over them, 0x2361D8: final ratio, auto level, LSD, downforce, dampers, camber, toe,
    stabilizers, ride height, springs, TCS, ASM and the engine output factor; its kind word +0x10 is 3), painted in
    its first color (0x2382C8: the paint's color id at +0x08), owns the goods of the parts it wears (0x2395C0 ->
    0x239620) and of every part the database has a record of for the car (0x238F70; a front tire's goods owns its
    rear twin), and is put in the **Normal** set. It has no running-in (the condition words are 0). The cars are block
    2 of the save (§11.2), so a car's settings stay with it. (Checked against the originals' new-game saves, §11.4:
    all 116 / 66 / 61 records.)
  - **The two sets** (0x238FD0 / 0x239030; tables 0x2BD440 / 0x2BD458, 2001 Tokyo 0x2BCC60 / 0x2BCC78, Tokyo-Seoul
    0x2C0E48 / 0x2C0E60, the Asian Tokyo-Geneva 0x2BD9E8 / 0x2BDA00; goods numbered as GT3's, gt_mode §6): Normal fits
    goods 2, 50, 19, 56, 67 (the plain brake controller, suspension, gearbox and front and rear tires); Custom fits
    3, 51, 22, 56, 67 (the brake balance controller, the first suspension kit, the variable gearbox, the plain tires),
    each only when the car has a record of it. Fitting is GT3's (0x237FA8): the goods' record for the car, else the
    car's own part of that table (0x237F08 / 0x236878), and the part's settings go back to its own through the part's
    setter, which puts each value into the record's range (0x234168 for the suspension: the Concept records hold
    starting values outside their ranges, e.g. the GT40's ride height).
    On the discs every car has the Custom brake controller, suspension (stage 1) and gearbox (stage 3: only its final
    ratio has a range).
  - **Which car.** The race descriptor (§4.4) takes the chosen class entry's garage car for Single Race, Free Run, 2P
    Battle (both players) and the pod races, sets its paint (0x2382C8) and makes player 1's the current car (status
    +0x31A40, 0x21B420). The single-race driver copies that car's equipment into the race (0x1803E0).
  - **Opening.** The menu opens on `root` when the current car's brake controller is the plain one (0x2390B0: its
    record's stage gives goods 2, or it has none), else on `custom`.
  - **Actions.** `set_concept_custom` (0x153BE0) and `set_concept_normal` (0x153C38) fit their set on the current car
    at once and mark the settings changed; `retrieve_all` (the Reset page's OK, 0x153AB0) puts every setting back to
    its part's own and fits the Custom set again. The sliders are GT3's working copy and `commit_slider`.
  - **Tires** (`set/adjast_tir`: sliders `slider_tire_f`, `slider_tire_r`, 0x151320 / 0x151440): three positions per
    axle, each axle on its own. Position by the fitted tire's stage (table 0x324560 by tire goods: stage 7 → 0,
    stage 0 → 1, stage 3 → 2, any other -1); setting a position fits the first stage that has it (0x239148 /
    0x2391A8). The databases give those stages the compounds `soft_gtc_n`, `normal` and `hard_gtc_n`. The texts
    (`parts_tir_f_concept` / `_r_`, 0x14BFE8 / 0x14C100) are the realtime text group SETTING's `TIRE_SOFT`,
    `TIRE_NORMAL`, `TIRE_HARD`; `TIRE_DIRT` when the race is on a dirt course; "----" for another tire.
  - **Gear ratio** (`set/adjast_gear`): the final ratio's slider; `gear_max_speed` (0x14DD88) is the gear chart's last
    line, the top gear's speed at the rev limit, m/s x 3.6 (x 0.62139 in miles per hour) x 100 cut to a whole number
    and printed "%.2t%s" with km/h or mph; "----" for a continuously variable gearbox.
  - **Refusals** (link prechecks 0x161020 / 0x160FB0): `set/adjast_gear` → `your_car_is_CVT` when the gearbox record's
    gear count is 2 (0x2365F8); `set/adjast_tir` → `dirt_tire_only` when the race is an arcade race on a dirt course
    (0x155F10: the race code starts with `A` and the course's surface is 2).
  - **Triangle** on the Normal and Custom pages ends the menu: their back name is `exit`, which this generation's page
    manager (0x1B7D90) takes like an empty one (sound `return`, the back action, result 0), as it takes `return` for
    the page it came from. GT3's page manager has no such name.
  - **The pages' buttons** are a map list (widgets `menu_map_normal` / `menu_map_custom`): the directions pick the
    button that lies that way, as on Game Status.
  - **In the race.** The car's build (0x1D5A00, the same code as GT3's 0x1DA308) changes an arcade car's tires only for
    a dirt course (its stage 12 tires) or, on a dry road course, when the car page's DRIFT tires were chosen (stage
    11); with RACING tires the car runs on the tires it wears: a Concept car's plain tires (stage 0, compound
    `normal`), or the Custom page's. (GT3's arcade rows wear their stage 10 tires themselves.) Back from the menu the
    single-race driver asks whether the settings changed (0x149238); if so it sets entry 0's equipment again and
    clears its switches, so the menu is the one before a run (§9.5).
- **Options**: GT3's `menu/option.mbl` and control (menus §10); the pages are spread over more
  pmb files and one assist choice is gone (`assist_type4_off`). `option_limited.mbl` is unused.
- **Records**: GT3's `records.mbl`; the license drivers pass `RecordsMenuArgLicenseConcept`
  (ten tests instead of GT3's license classes).
- **Memory-card pages**: GT3's `memorycard.mbl` plus page `transfer_money` (§7.6); Geneva drops
  `gameload_g`.

## 11. Save data (H; read from the programs and checked against the originals' own saves, §11.4)

### 11.1 Card names

- Game save: directory and data file `<prefix>GAMEDATA`, with `icon.sys`, the icon (`icon0.ico`) and `thumbnail` as
  GT3. The prefix is two strings the name builder joins (0x16C768): a region code and the release's serial.

| Release | Prefix | Minor version | Icon (`menu/icon/`) |
|---|---|---|---|
| 2001 Tokyo | `BI` + `SCPS-15010` | 0 | `ICON_GTC_GT_concept.ico` |
| Tokyo-Seoul (v1.02 and v2.00) | `BK` + `SCPS-56005` | 3 | `ICON_GTC_concept_KR.ico` |
| Tokyo-Geneva, Europe | `BE` + `SCES-50858` | 2 | `ICON_GTC_concept_GNV.ico` |
| Tokyo-Geneva, Asia | `BI` + `SCPS-55903` | 5 | `ICON_GTC_concept_GNV.ico` |

- Other files: `<prefix>` + type letter + 7 characters; `R` replay, `B` best-lap replay, `S`
  car setting (`G` the game save). The 7 characters encode 42 bits, 6 per character, low bits
  first, alphabet `A-Za-z0-9+-`: 30 bits from the file, then a 12-bit counter started at random
  and stepped until the name is free (0x16C958, 0x16C9E0).
- Other icons: `ICON_GTC_full.ico` replay, `ICON_GTC_best.ico` best lap, `ICON_GTC_license.ico`,
  `ICON_GTC_setting.ico`.
- `icon.sys` is the disc's `menu/icon/icon.sys` with the title at +0xC0 (Shift-JIS; the line break word +6 = its
  length in bytes) and `icon0.ico` three times, as GT3's. The title is the realtime text `MEMORYCARDTEXT_GTCGameData`
  in the game's language (the disc's text is EUC-JP; the program's built-in default where the language's is empty, as
  on 2001 Tokyo, whose table has GT3's keys only).

### 11.2 Game save layout

Container as GT3's (gt_mode §12): header with sizes, minor / major version, CRC, name; payload
stored inverted. Major **100** (GT3 7); a wrong major is refused, the minor is not checked. The file is padded to a
multiple of 64 bytes with whatever the game's buffer held (not part of the CRC).

Payload = 0xA68C + 0x204 x cars bytes (GT3: 0x112BC + 0x204 n + 0x44 m), in this order (the status object's writer
0x21BD80 and reader 0x21BA70; the same blocks, counts and strides in the five programs):

| # | Block | Size | Status offset |
|---|---|---|---|
| 1 | scalar block, GT3's (gt_mode §3) | 0x130 | +0x08 |
| 2 | cars, count at scalar +0x30 (GT3's garage records: one per class entry, with its settings, §10) | 0x204 each | +0x138 |
| 3 | course license records | 10 x 0x154 | +0x23510 |
| 4 | arcade results | 144 x 0x14 | +0x24900 |
| 5 | time-trial / free-run rankings | 44 x 0x300 | +0x26EA8 |
| 6 | present flags | 0x4C | +0x2FBA8 |
| 7 | options | 0x888 | +0x30C30 |

Dropped from GT3's save: the wheel inventory, the 48 GT-mode license records, race results,
course bests, arcade time-trial challenges, machine-test records, prize flags and the suspended
championship. Those members still exist in the status object (0x36148 bytes; GT3 0x35210), which
keeps GT3's layout and adds the session record and the availability table at its end.

- **Scalar block** of a new game: day 1 (+0x00), the game config's `initial` credits (+0x10: 18,000; 1,800,000 on
  2001 Tokyo, x100 in Japanese), the next garage serial (+0x24), the current car -1 twice (+0x28, +0x2C), the car
  count (+0x30), 1 at +0x34 (GT3's wheel count with its placeholder; no wheels follow), the language (+0xC8, written
  at each save: the language's number in the program's own order, so Korean is 7 on Tokyo-Seoul; 2001 Tokyo does not
  write it) and the random state (+0xCC). The rest is zero. The serials count from the first car made in the session:
  a save's cars are numbered from 0, or from the car count when the game made its garage twice before the save (seen
  once, on the European release; the cause was not established and nothing reads the difference).
- **A car record** is GT3's garage record with: +0x08 the paint's color id, +0x10 the kind word 3, the gear words as
  16-bit values (no gear = 0x0000FC19, where GT3's hold 0xFFFFFC19), +0x1B8 the row's engine output factor. A gearbox
  whose ratios are computed fills its own gears; the words after them hold what the game's work area held.
- **A license record** is GT3's (gt_mode §12): s32 medal (-1 none, 0 gold, 1 silver, 2 bronze), then six entries'
  names (16 bytes each), times (ms; no time 0x157529FF), stamps and 32-byte blocks (30 '-' when unused).
- **Arcade results**: record `level x 10 + course`, four "no time" words and the best place at +0x10 (8 = none).
- **Rankings**: GT3's 0x300-byte records, by the TIME_ATTACK list's order (the ten courses).
- **Present flags**: a byte per name. A prize key's is its place in the game config's list (0x216EF0 -> the list's
  lookup); `MOVIE0` 0x46, `MOVIE1` 0x47, `PODRACE` 0x48, `TRANSFER` 0x49 (0x216F40).
- **Options**: GT3's block. A new game's differs by release: 2001 Tokyo's and Tokyo-Seoul v1.02's are GT3's; Tokyo-Seoul
  v2.00's and both Tokyo-Geneva releases' start three switches the other way (+0x3C and +0x44 on, +0x5C off) and fill
  the four 0x20-byte wheel records after +0x7C8 ({1, 0, 0x400, 0x200, 0x29, 0xD5, 0x2B, 0xDA} twice, then the same with
  0x25, 0xB4, 0x48, 0xDF); the Tokyo-Geneva programs' 0.1 at +0x114 and their music list's first shuffle differ by
  their compiler's rounding (0x3DCCCCCC; the shared generator's integer draw scaled by the float below 2^31). The
  music list holds the disc's 18 race songs.
- **Thumbnail** (0x21BF58): zero at +0x00, +0x08 (GT3's credits), +0x14 and +0x18; at +0x10 the number of cars that
  can be chosen (the availability table's open slots, 0x211278: 66 of 116 in a new Tokyo-Geneva game); at +0x1C the
  completion x 100. The other words are not set.

**State an implementation must save:** the ten license records (medal, best runs), the arcade
results, the present flags (0x4A used), the rankings, the options, the language, the cars' parts and settings (§10).
Not saved: the Ace flag, the last choices of §4.1.

### 11.3 Replay, ghost, setting files

Replay, ghost, track and max-speed file versions are GT3's (major 4, minor 0); the replay
payload has GT3's size (0x41940). Setting file: payload 0x1D0, major 1, minor 0x82D Seoul /
Tokyo, 0x82E Geneva (GT3 0x827). Internal differences were not examined.

### 11.4 Checked against the originals

- A new game saved by each original (2001 Tokyo, Tokyo-Seoul v1.02 and v2.00, both Tokyo-Geneva releases, run in an
  emulator from the discs): ours from a new game is the same payload byte for byte on the four releases the tests run
  (but the serials' start and the spare gear words above), with the same `icon.sys` and icon and the thumbnail's set
  words (test `concept_disc_ps2_save` with `GT3R_CONCEPT_PS2_REF`). The two Tokyo-Seoul versions' new games differ in
  the options' defaults alone.
- A game with progress (ten license medals and their records, fifteen wins, their presents, a car on its Custom set)
  exported by us and loaded by the original European Tokyo-Geneva: the AUTOLOAD at the start takes it, Course License
  and Game Status show it (75 %), and Save Game from the original writes the same file back, header, CRC and every
  payload byte (test `concept_disc_ps2_demo_card` writes such a card for each release).

## 12. Differences between the releases

| | Tokyo (2001) | Seoul (2002) | Geneva (2002) |
|---|---|---|---|
| Language page at first boot | none | none | six languages |
| Title items | 8, with `aspec_demo` | 8, with `aspec_demo` | 7 |
| Category page | `ar/select_category` (with car counts) | `ar/select_category` | `ar/select_category2` (road / rally title) |
| 2P car page | `ar/select_car_2p` | `ar/select_car_2p` | `ar/select_car_2p2` |
| gcf class lists (tag 14) | 11 entries: nine with cars (CLASS_C, B, A, S, DIRT, COMPULSORY, LICENSE, PODRACE, PREVIEW) and GT3's two garage classes, empty | the same 11 | 15: the 11 and the `_UK` twins of C, B, A and DIRT for British English (the Asian release: 11) |
| License tests | `LC00000n` | `LC00000n` | `LCeu000n` |
| Present movies | `opening_GT3UK`, `ending_long_full` | the same | `opening_GTCJP`, `ending_GTC` |
| Attract | 3 demos, opening | 4 demos, opening | demo, TV spot, demo, TV spot, demo, opening |
| Transfer | 1,000,000,000 Cr to `BISCPS-15009` | the same | 10,000,000 Cr to `BESCES-50294` |
| Settings file | GT3's pages + 5 | the same | 15 pages |
| Save minor version | 0 | 3 | 2 (the Asian release 5) |
| Step rate | 60 Hz | 60 Hz | 50 Hz (timers scaled) |

**Tokyo-Seoul v1.02** (the same serial as v2.00): its program is the 2001 generation's: it names a game config for
each language and loads the one of its language, Korean (`concept_kr.gcf`, the config v2.00 names alone; 0x220FE0), and
its attract runs three demo races (v2.00 four). Its disc's data, save name and save version are v2.00's; a new game's
options are GT3's where v2.00's differ (§11.2). **The Asian Tokyo-Geneva release** (image serial SCPS-55902; its program
names SCPS-55903 for its saves): Tokyo-Geneva's program and data with the `concept_tw.gcf` config, no `_UK` lists, the
transfer of the Japanese releases.

The session flow, action table, prize pass, state tests and race drivers are the same code in
all three (function matches; the Tokyo prize pass is instruction-identical to Geneva's).
Car and course lists differ by gcf and database only.

## 13. Implementation checklist

Size classes: **S** a day or less, **M** a few days, **L** a week or more.

| # | Piece | On our engine | Size |
|---|---|---|---|
| 1 | Disc detection, gcf name per release, Concept paramdb (event table at index 31) | `disc.cpp`, `Gcf` unchanged (same tags); database loader (in progress elsewhere) | S |
| 2 | Boot: language page (Geneva), warning, opening movie, auto-load | reuses `GameFlow` boot and `handle_language` (add `pt`) | S |
| 3 | Title `ar/push_start` in the `concept` set | reuses the title selector (`menu_top`) and title commands; new set name and the `credit` link | S |
| 4 | Main menu `ar/top`, game index, back to title | like `handle_arcade`'s top page; `GameButtonList` is a plain vertical list | S |
| 5 | Sequences per game (level → course → category → car, ...) and resuming them after a race | like `arcade_steps_` with a new table; new: resume into the sequence | M |
| 6 | Level page with the hidden Ace button (L1 + R1) | new small control like GT3's Professional rule | S |
| 7 | Course page with place badges and license gating | reuses the course carousel; new badge draw and skip rule | S |
| 8 | Category page (five classes, road / dirt counts, locked look, UK lists) | new control like the class page | M |
| 9 | Car page: bar gauges, assist step, locked cars | extends the car carousel (`Carousel` steps); gauge values need their source (open question 6) | M |
| 10 | 2P car page `ar/select_car_2p2` | reuses `two_player_*` with the assist step | S |
| 11 | Course License page and Pace Car page | new code-drawn page (list, medals, times, test car); `ar/lead_car` plain | M |
| 12 | Game Status map (scrolling map, 30 nodes, 45 items, gift boxes, completion) | new code-drawn page, like the GT-mode map button list | L |
| 13 | Show room (3D car, spec table) | reuses the car-get / showroom model render and car spec text; new layout | M |
| 14 | Pod page `ar/podrace` | plain page + two actions | S |
| 15 | Progress state (licenses, arcade results, present flags), prize pass, availability table, completion | new `game/concept_*` module (pure rules, testable without a disc) | M |
| 16 | Presentations: `notice_present`, `concept_car_get`, five simple pages, movies | reuses the presentation queue (`start_presentations`); car-get like `car_get` without the roulette; simple pages plain | M |
| 17 | Present actions: MOVIE0 / MOVIE1 playback | reuses `GameRequest::PlayMovie` | S |
| 18 | Credit transfer into a GT3 save | reuses `save-import` / `save-export` code paths on our own GT3 save; new page `transfer_money` | M |
| 19 | Single race at three levels | reuses arcade single race; event rows `A_<car>_000N` | S |
| 20 | Course license without lead car | reuses the license race (`race/license.*`), flying start as GT3 tests | S |
| 21 | Course license with pace car | license race + one computer car with rubber band and contact (done: `Conductor::Training`) | M |
| 22 | Free run, 2P battle | reuse unchanged | S |
| 23 | i.LINK Battle | maps onto our online lobby as GT3's | S |
| 24 | Pod race (stop area, sit state, prompts, disqualification, AI stop) and pod 2P | new race rule set on the arcade race; pod car model and sit animation | L |
| 25 | HUD row changes (LICENSE, 2P, LOGGER, REPLAY, DEMO) | `hud_layout` reads the rows already; new type 13 sub 1 (the store monitor) | S |
| 26 | Quick menus `concept_qm_*` with the license briefing pages | reuses the quick-menu runner; new briefing panel | M |
| 27 | Settings menu (Normal / Custom, sliders) | reuses `setting` control with two new actions; Geneva's short file | M |
| 28 | Options, records, name entry, replay theater, Analyzer, memory-card pages | reuse unchanged (records: ten tests) | S |
| 29 | Save file: our own format for Concept state; import / export of an original Concept save | new block set in `gt_save`-style code (major 100) | M |
| 30 | Attract (demo races, TV spots) | reuses the attract loop with a per-release list | S |
| 31 | Leaf and spark particles | new particle kinds; the leaves are made inside the race step and draw the race's random register (§15), so a race-core port | M |
| 32 | 50 Hz timers on Geneva | existing PAL handling | S |

## 14. Open questions (need the original running, or a deeper read)

1. (Settled, §3.3: the title times out after 20 seconds; the gcf's idle time is not what the title reads.)
2. Which page a finished race returns to (the sequence's last page, or its first), and the
   focus there.
3. (Settled, §9.2: the pace car starts one second ahead; passing and contact change nothing.)
4. Game Status: what a prize-car button that is not reached yet draws, the point of the screen the focused button
   is held at, and the opening zoom (§5.9 has the rest).
5. (Settled, §7.4: a base key's flag sets its `_S` twin's too.)
6. Car page gauges: where the Handling / Speed / Acceleration values come from (car data or a
   table), and what the three assist levels change in the race.
7. Whether locked cars are hidden or shown as unavailable on the car page; the look of a locked
   category beyond the `question` art.
8. The pod race on screen: the sit state, the prompts, AI behavior, and the result page of
   pod 1P.
9. (Settled as far as the driver goes, §9.5.)
13. How a demo race's event is picked among the gcf's tag-12 events, and whether the title's sub-pages (Replay Theater,
    the credits) count towards its idle time.
10. Seoul's handling of mode 0x20 (`aspec_demo`) and the meaning of scalar-block fields GT3
    leaves unknown, if Concept uses any.
11. The 0x250-byte options sub-block and any option that differs from GT3's.
12. The session result word → state mapping (§4.4) and the setter of mode 0x21.

## 15. Implementation notes (2026-11-13)

Where the code is: the rules in `src/game/concept_state.*` (pure, unit-tested), the disc's lists in
`src/game/concept_game.*`, the menus' game side in `src/game/concept_menu.*`, the front end in
`src/menu/flow/concept_flow.cpp` (control "concept" of `GameFlow`; the course and car pages are the arcade pickers of
`game_flow.cpp`). The progress is three record kinds in the game's own save file (`clicense`, `cplace`, `cpresent`),
in the disc's own save folder.

Checklist status (§13): done 1, 2 (language page; the warning of §3.1 is the "no memory card" page, which a game
without memory cards never has cause to show, and the AUTOLOAD panel is not shown, as on a Gran Turismo 3 disc: the
game's save is simply the one played), 3, 4, 5, 6, 7, 8, 9 (bar gauges with GT3's values, the transmission and assist
steps), 10 (with each player's assist step), 11, 12 (the nodes, the prize cars, the pointer, the scroll; no zoom; what
is not reached is drawn in black, as the original shows it), 13, 14, 15, 16 (with `notice_present` after the run),
17, 18 (into our own Gran Turismo 3 saves), 19, 20, 21 (the pace car: conductor kind `Training` in `sim/exact`, below),
22, 23, 25 (fifth pass below), 26 (the three menus, the briefing pages, the license box), 27 (the settings menu, below), 28 (the records page
and the name entry of a license run checked on Tokyo-Geneva: the run goes into the test's ranking and shows on the
page), 29 (our own format, and the original's memory-card save both ways, fourth pass below), 30 (the attract
sequence, below), 32 (below), 24 (the pod race for one and two players, below), 31 (the leaves and the sparks, below).

Fifth pass of the front end:

- **The Analyzer's display (25)**: `Hud::draw_logger` draws the LOGGER mode's rows (`HudMode::Logger`, racemode 15;
  GT3's four built in, a Concept disc's six from its tables) over the Analyzer's REPLAY
  (`RaceScreen::draw_logger_display`): on Gran Turismo 3 track A's car (car 0 of the Analyzer's race) and track B's (car
  1, when loaded) 560 to the right, as RaceSolitaire::vf46 draws its display twice; on Concept track A's car alone,
  drawn before the Analyzer's page (RaceSolitaire::vf47 inside the race's draw, the page after it), the frame below the
  race's view (13 + 256 lines) clear, the page's sector box (`RawWindow_SectorBoxBG`) drawn. Each car's values come from
  the race (`race::exact_car_display`: its state, steering byte, pedals; a layout 7 car's store and flow byte). Concept's
  own drawings (§9.3): the `replay_sign_gtforce` steering, the accel / brake boxes with a frame's fade, the store monitor
  (§9.2), the bar tachometer, gear and digital speed; the same steering and pedal drawings now serve Concept's REPLAY
  items, which drew nothing before (GT3's pictures are not on its discs). Checked in the game: GT3 US 1.10 with one and
  two tracks, Tokyo-Geneva Europe with the Honda Dualnote (layout 7: the store monitor shows). As the program draws it,
  Concept's page covers most of the display: only its edges show between and below the TRACK A / B panels (a frame drawn
  without the page shows the whole dashboard). Not read: the steering routine's tick marks (eight points from a table,
  turned by the last two steering values) and the sprite draw's own scale (the picture is drawn at the row's size).
- **Tokyo-Seoul v1.02 (30, 29)**: `RegionData::gcf_per_language` (a Concept program naming a config per language, the
  2001 generation's) tells it from v2.00; its attract runs the three demo races of its program (0x100200 .. 0x100220,
  then the opening), and its new game's options are GT3's (`default_concept_options`): checked byte for byte against the
  new game the original v1.02 saved (`concept_disc_ps2_save` with its reference), as v2.00 against its own.
- **The 2 player pod prompt (24)**: RacePOD2PDisplay has no positions of its own (its vf1 / vf3 / vf17 set up the
  halves with the 2 player rows and end the race): the prompts are the halves' car message on the Messages row, where
  they meet the digital speed in the original too. Nothing changed.
- **The pod / time trial FAIL (24)**: ported as `race::ConceptExamMessage` (§9.2's disqualification on screen):
  `SimpleExamMessages` (0x186178; GT3's `TimeTrialExaminationMessages` renamed, with the byte +0x1C that keeps the
  two-player halves' message) is a fade, not a scale: the wash and the text fade in together over 61 steps (the text's
  two copies meeting), are held 180 steps and fade out over 61 (the copies parting again), the values in EE singles;
  the race ends 240 steps (or a confirm) after the message has gone: event 1 on the 604th step, 10.07 s (before, ours
  drew the licence presenter's FAIL and held it 11 s). The same states and times (in seconds) on all five builds; the
  two-player halves are flagged on the hold's 302nd step (was 5.0 s). Unit test `race_concept_exam_message`; seen in
  the app on 2002 Tokyo-Geneva Europe and 2001 Tokyo (one player: fading in, held, fading out, gone, the race over at
  +10.1 s) and on Tokyo-Geneva Europe with two players (both halves held, the race over 5.1 s after the second). Not
  checked against the original running: the step the display notices the event (counted as our step of the
  disqualification). The wide mode (§9.2): ported (the text in the 4:3 frame, the wash over the whole screen or half),
  seen on Tokyo-Geneva Europe with one player and with two side by side; read, not seen in the original.
- **The leaves (31)**: ported in `sim/exact/leaves.h` (physics §14.3, `Rule::Leaves`) and drawn by `race/leaves.h`.
  Concept's tire effect emitter (2002 Tokyo-Geneva Europe 0x1ED4B8, the place of GT3's 0x1DF150) hands each wheel's
  request to the smoke (0x18E598) and then, when the wheel's ground flag byte has 0x20, to the particle manager's leaf
  maker (0x18E5C0 -> 0x19C578, every later build: 2001 Tokyo 0x19C4E8): one draw of the VU0 random register (0x247610),
  and a leaf with twelve more when the slip is fast enough and the pool (the sparks' 128, one active list) has room. A leaf
  (`LeafParticle`, vf1 0x19D308) is a small rigid plate of one of eight shapes, its update draws nothing. Only Autumn
  Ring has such ground (`gt3tool course-survey` lists the triangles per flag bit: 1,310 of its 19,591, the verges 8 to
  30 m off the center line, on every later disc's `autumn` files, `r_autumn`, `autumn_d` and the demo / time trial
  variants; no GT3 course), so a race makes leaves only where a car runs wide. Checked bit for bit on all five later
  builds: `leaves_exact` (the maker, the update, the shapes against the program's own manager), `leaves_emit_exact` (the
  emitter on our race's car over the verges) and `race_tick_exact` (the emitter's leaf calls inside the race step, the
  default races and a `#leaves` pad race; DIFF_HARNESS.md). The draw (vf2 0x19D7E8, with the sparks in the one-player
  view): the shape's quad in its plane turned by its three angles (0x19D070), `race/particle.img`'s picture k + 1 (the
  sparks' glow is picture 0), UVs (0, 0) .. (1, 1), cut out by the picture's alpha, both faces. Its color (0x19D700) is
  lit by the course's Env0 car light 0 as the course file has it: the manager's light pointer (0x336058) is set by
  RacePS2Base::vf9 to the course's slot 32 block (the same pointer the race reads Env0's flags, far clip and lens from),
  and the draw reads +0xD0 (direction), +0xE0 (ambient) and +0xF0 (diffuse), with no flag bit gating it and no per-car
  shade. With n the leaf's normal (its matrix's second row), a the ground shade under the wheel that threw it and L the
  direction: c = -(L.n) / |L| x (0.3 + 0.7 a) (0.3 folded toward zero on 2002 Tokyo-Geneva: `LeafRules::light_base` /
  `light_gain`), negated when (pos - P).n >= 0, at least 0; each channel trunc((diffuse c + ambient a) x 128), at most
  0x80, alpha 0x80. P is the negated translation row of the view matrix the particle pass starts with: the camera's
  position in its own eye axes, not turned back to the world (the program's own side test; another of its routines,
  Europe 0x2512B8, does turn it back). So which face counts as lit follows that point, not strictly the face the camera sees.
  Autumn Ring (Europe): light 0 direction (0.653, 0.644, -0.399), ambient 0.35, diffuse 1.04; the verges' shade is 0
  under the trees by the start (dark leaves) and up to about 0.8 in the open. Ours (`race::leaf_color`, `LeafDraw`):
  the same, in host floats, from the sparks' view (the VR view's from the head). Seen in our app (a free run at Autumn
  Ring, the Elise onto the verge by the start); the color rule is read from the program, not compared with the
  original running.
- **The sparks (31)**: `sim/exact/sparks.h` (physics §14.3, `Rule::SparkEmit`, `SparkRules`). The burst, the update and
  the tuning block are GT3's code on every later build (SparkParticle::vf1 the per-spark step; the record GT3's with the
  particle kind's table at +0x04), 2002 Tokyo-Geneva's constants folded toward zero. 2001 Tokyo's and Tokyo-Seoul v1.02's
  emission is GT3's; from v2.00 on a burst starts 0.05 above, and bounces on, the plane of the car body's drawn ground
  point and attitude (0x1A9D50 on v2.00), not the road's under the point. Checked bit for bit on all five later builds:
  `sparks_burst_update_exact` and `sparks_emit_exact` (DIFF_HARNESS.md). Not checked in the game.

Fourth pass of the front end:

- **The memory-card save (29)**: `game/concept_save.cpp` (`import_ps2_save` / `export_ps2_save` and the file-level
  calls of `gt_save.h` hand over on a Concept disc; the save page's FROM A FILE and Export to PS2, `gt3tool
  save-import` / `save-export`). The import keeps the cars that are not their entry's new car (the others are made
  again when raced), the medals with their six runs as the test's ranking (`lic:<code>`), the results, the presents,
  the free-run rankings (`ct:<course>`), the name entered last and the options block as it is; the export writes
  every entry's car, ours or new. §11.4 says what was checked.
- **The garage car (27)**: `concept_new_car` builds the arcade row with its tune (it took the parts' own settings
  before, so a car whose row is tuned raced on other settings than its arcade row), the fits clamp as the setters do,
  and the race takes the row's engine output factor with a garage car (`race/exact_setup.cpp`: before, a garage car
  raced without it, so a car whose row raises its output lost that).
- **The car pages (9, 10)**: no tire step on a Concept disc, the assist step skipped on a dirt course, each player's
  assist step on the 2 player page (its `assist_<n>_yes|no[_2p]` buttons where the layout has them).
- **The 2 player display (25)**: the halves' parts move by what the disc's rows differ from GT3's (`Hud::draw_split`:
  on Concept the event display's width, 400 top and bottom); the rows' steering, tire and pedal parts are replay items
  there, not drawn live, as on GT3.
- **The settings menu's texts (27)**: the Custom page and its slider pages draw their left / right aligned texts half
  the font's size in from the box, the items' rule (0x1BA090), so an axle's letter and its value stand side by side
  (the two boxes overlap in the layout).
- **Triangle** on the Normal and Custom pages ends the menu (§10); the Replay Theater page asks for no music of its
  own (the page rule's `replay_theater` test is a prefix test, so `ar/replay_theater` is an `ar/` page to it; GT3's
  stream is not on the Concept discs).
- **Tokyo-Seoul v1.02** loads `concept_kr.gcf` like v2.00 (`RegionData::of_program`: a Concept program naming a config
  per language takes the one of the database it names); before it took the plain config and ran in Japanese with
  2001 Tokyo's lists. (Its attract and new-game options: fifth pass.)

Third pass of the front end:

- **Settings (27)**: `game/concept_game.*` keeps the garage cars (a `GarageCar` per class entry, `origin` = the entry,
  made the first time the entry is raced instead of all at once on a new game: the result is the same car) and the two
  sets; `concept_prepare` hands the car's parts and settings to the race for Single Race, Free Run and 2P Battle (the
  pods run as before); the settings menu is GT3's screen on `menu/setting.mbl` with the two actions, the Reset, the tire
  sliders, the top speed line and the two refusals; the quick menu's SETTING opens it for a single race and a free run,
  and a change builds the car again and puts the menu back to the one before a run. The cars are saved with the game.
  With RACING tires a Concept car now runs on the tires it wears (it ran on its stage 10 arcade tires before, which
  the program gives it for neither choice).
- **Display rows (25)**: `race::set_hud_tables` (the disc's racemode.db / racedetail.db) on a Concept disc.
- **Attract (30)**: the title's 20 seconds, then the release's steps (`game::concept_attract_steps`); a demo race is
  the next of the gcf's demo events in turn, as GT3's.
- **50 Hz (32)**: nothing in the front end counts the European program's frames: the menus and the race step 60 times
  a second on every disc and the durations used are seconds (the title's 20 s, the examination timers, the events'
  medal times, which the European rows do not retune, and the 60 Hz license demonstrations of choice 16).
- **Pod race (24), front end**: the three notes, the two prompts' display (shown when the race sets message 0x10 or
  0x11 on a player's car, in each half with two players; a test preset `GT3R_RACE pod_prompt=16|17` holds one up), the
  disqualification's presentation, the two player car page with each player's row holding the pod alone.
- **The two-player pod race through the real menus** (Tokyo-Geneva Europe and 2001 Tokyo, a save with every license
  medal): title, Game Start, Main Menu, Game Status (fifth item), down to the presents row and right to `PODRACE`, the
  pod race page, 2P pod Race Entry, the 2 player car page (both players confirming: `GT3R_GT3MENU_SCRIPT` takes
  `p2:<key>` for player 2), then the race `APO20001` on `route5_pod` with both players in the pod; both disqualified,
  both halves held, the race over 5 s after the second. Fixed on the way: the pod race page drawn over Game Status
  moved with the map's scroll (only the status page's own elements follow it now). The race has the Options' 2P laps
  (2 by default), not the event's 3: the pod's two-player set-up (0x132800) is the 2P battle's (0x132068) instruction
  for instruction but for the race class and the course (the request's instead of `yotsuya001`), so it takes its laps
  where the battle does.
- The menus walked on the four discs (`tests/test_concept_walk.cpp`): every title item and its sub-pages, the options'
  eleven pages, the six games and back, the hidden level, each offered language.

The race core (`sim/exact`):

- **Pace car (21)**: conductor kind `Training` (`race_inputs.h`): the license kind with the event's rubber band on and
  its pass from the configuration's +0x22, the whole collision step, the single races' control sources (entry 1, the
  event pool's third car in its pool paint, is a computer car) and the flying start with car *i* > 0 placed
  `trunc(i x start speed / 3.6)` m further on. The judge leaves a computer car alone (out of the course: nothing; at the
  finish: its run ends, no result, no event). The run saves a replay, not a ghost, and takes no ghost car.
- **The license conductor of the later builds** (both kinds, by the disc's program): soft ground is not out of the
  course, pit mode 0, and the judge clears a hit flag instead of ending the test (§9.2). GT3's discs keep GT3's answers.
- **Pod race (24)**: the rules of §9.2 "Pod race" run in the game's race (physics §14.5); checked in the game on
  Tokyo-Geneva Europe and 2001 Tokyo from Game Status's pod page: the player's pod drives, the computer pods stop
  each lap, the prompts 0x10 (amber, on the stop area until the pod sits) and 0x11 (red, past the area) show in the
  car message's place, a stop posts its note, three stops finish the race (the results, the replay, the quick menu,
  Game Status after EXIT), a missed stop disqualifies at the line with the FAIL presentation and the fail jingle, and
  the race ends 11 s later into the replay and the quick menu. Two players: each half shows its own car's prompts and
  its own held message; both disqualified ends the race 5 s after the second (the 2P pod display's update). The
  player's car on the menus' path is the class's `gtcmr_pod_tmv_01` (racing tires), the field five of the event's
  `gtcrr_pod_tmv_01`, drawn as Single Race draws (the pool has one entry). An early report that the player's pod could
  not move was the test driver's: `line=` asked for no throttle while the car stood at more than 0.25 rad to the line
  (a grid slot beside it); it now keeps at least a crawl. `GT3R_RACE pod_stops=1|2` makes it stop in the area (as the
  harness's `#pod` driver), `p2=1` drives player 2's car too (`pod_stops2=`).
- Scripted license runs: the test driver `auto=1` hands the car to the race's own driver (play mode 3), whose finished
  run keeps no result, in GT3 as here (the judge posts "finished" and stores nothing with the play mode set). A judged
  run is scripted with `GT3R_RACE line=<km/h>`, a driver on the player's pad that follows the car's line
  (`screen_race_drive.cpp`); `qm=start+exit` answers the menu each time it opens. Before the third pass the `auto=1`
  runs ended out of the course on the first lap, because GT3's soft-ground rule was applied to the Concept tests.

Settled from the discs and programs:

- **Languages** (§3.1): each program's language object starts as one language (constructor: Geneva 0x2469D8 stores 2,
  Seoul 0x24A568 stores 7, which is Korean in that program's order, the Asian Tokyo-Geneva program 0x247028 stores
  10, Traditional Chinese; Tokyo sets 0 at 0x100108). The Asian Tokyo-Geneva release (SCPS-55902, database
  `paramdb_gtc_tw.db`, `concept_tw.gcf`, tests `LCtw000n`) has a `LangControl` too and opens `menu/language.mbl` page
  `root_tw` (items `tw`, `us`).
- **Units** (the language-to-units table, Geneva 0x325C80, Asia 0x3263E0, Seoul 0x329520, unit sets by the switch
  0x246F18): Portuguese km/h, CV, kgf.m; Korean and both Chinese km/h, PS, kgf.m. The same table gives Italian
  power label index 5 (the string "PF") and Spanish CV, where GT3's PAL program gives both BHP: not applied (the
  power conversion of index 5 was not read), Italian and Spanish keep GT3's units on Concept discs.
- **License medal times** (§9.1): Geneva's `LCeu000n` rows and the Asian release's `LCtw000n` rows (same cars, a
  50 Hz and a 60 Hz release, both on each disc) hold identical gold / silver / bronze times, so Concept did not retune
  them for 50 Hz and nothing is substituted (GT3's PAL rule does not apply).
- **Driving assist** (§5.7, open question 6, first half): the three buttons' art shows ASM and TCS lit (`assist_0`),
  TCS only (`assist_1`), neither (`assist_2`); the race takes its ASM / TCS from the choice.
- **`_S` keys** (open question 5): see §7.2; the car is presented once (by its base key), both keys are flagged.
- The disc holds `stream/level_gtc.pss`, which no program names (the level page shows `arcade_menu_gtc.pss`).

- **Second pass** (the sections updated then): the medal times' layout and the demonstration files (§9.1); the pace
  car (§9.2); which display the license runs build (§9.2); the single-race driver's loop, the license menus' box
  (§9.5); the notice, the cup and when the flags are set (§8); the map's nodes and scroll (§5.9); the `_S` twins'
  flags (§7.4); the transfer's table, amounts and targets (§7.6). `premium_get` stays unreachable (§2.1).

Choices made where a question is open (each marked in the code):

1. Attract: a demo race is the next of the gcf's demo events in turn (GT3's rule; question 13); only the title page
   itself counts idle time.
2. After a race (question 2): the game's sequence reopens at its first page, the last choices focused.
3. Single Race on a new game: no course has a license medal, so the main menu refuses Single Race (seen in the original:
   the item takes no Confirm until a license has a medal).
4. License records keep the better medal and the faster run (GT3's license record).
5. A prize pass that would grant more than six names keeps the first six in the order cars, MOVIE0, TRANSFER,
   MOVIE1, PODRACE.
6. The ending movie of the Professional milestone plays after the last presentation page, not right after
   `concept_ace`; `concept_movieb` (a title card without a button) is not shown.
7. Car page: gauges take GT3's values (handling and acceleration 0.7, speed = top speed / 350) on Concept's bars; the
   car's name picture is shown, the maker logo (`tuner_<maker>`, same place) is not; locked cars are left out; the
   assist row sits at x 160 / 320 / 480 and the row titles 46 units above their buttons.
8. Title list rows are the item height + 8 apart; the level buttons sit 4 apart, centred in the band.
9. The prize car page's name line is the layout's text followed by the car's name; its car uses the "purchased"
   showroom camera.
10. Menu music: the title pages and `ar/replay_theater` silent (the title sequence stops the music before its page
    opens, 0x109A70; whether the page rule then starts the `ar/` music again was not established), every other page
    the arcade pages' rule.
11. A page the movie table does not list keeps the running movie.
12. Game Status: a prize car or present that is not reached is drawn in black (§5.9) and refuses Confirm; the focused button is held at the
    screen's centre while the map can move; no opening zoom; the page opens on the present granted last, else on the
    button it was left on, else on Back; the pointer's tip sits on the focused button's centre. At 60 steps a second the
    scroll uses the program's per-frame rate unchanged.
13. 2P Battle: the five classes for both players on any course.
14. The Pace Car page opens on NO until YES was chosen (the status's lead flag starts clear, M). The pace car's grid
    slot is 1 and the player's 0 (the flying start does not read them).
15. The credit transfer goes into our own saves of any Gran Turismo 3 release, chosen on the save page: the release the
    Concept release's program names first (the PAL release's library for the European Tokyo-Geneva disc, the Japanese
    release's for the others), then the other two, each save's name followed by its release. The original takes only
    the named release's save; the other releases' saves hold the words the transfer changes in the same places, and
    no GT3 program reads or writes the mark's word or limits the credits. The question starts on CANCEL; nothing is written before its OK. The mark is the save's
    `concepttransfer` record (scalar +0x12C in an exported PS2 save). The thumbnail's figures are not stored: our save
    page computes them.
16. The European Tokyo-Geneva disc's license demonstrations (the menu's box and DEMONSTRATION) play the same test's
    60 Hz recording on the disc (`demo/LCtw000n`), since its own are 50 Hz recordings.
17. The notice is shown once per prize in a visit; the progress and flags are written when the visit ends (the
    original flags them at once; the result is the same list).
18. The pod race: back to Game Status after it. On the two player car page either player's Cancel on the car row
    goes back to the pod race's page.
19. The settings menu: a garage car is made when its entry is first raced; the pods and the license cars have no
    settings menu.
20. The boot (§3.1): no AUTOLOAD panel and no "no memory card" page, as on a Gran Turismo 3 disc.
21. An exported save's garage serials start at 0 and its spare gear words are "no gear".
