# Menu behaviour (gt2k_menu) — specification

Behavioural facts about how GT3 (US, PBPX-955.03) runs its menus, recovered
from the program for the independent reimplementation in `src/menu/flow/`.
File layouts are in [../formats/menu.md](../formats/menu.md). Frame counts are
60 Hz NTSC frames. Confidence: **H** read from code, **M** inferred from code
and data, **L** guess.

## Changelog

- 2026-11-02: §11 the race behind a quick menu: the computer's, driven on and on from the
  driver's camera, a GT race's field racing after its countdown (presentation changelog
  2026-11-02; ours had replayed the last run and let the car coast to a stop, and stood a field
  still on its grid).
- 2026-10-21 (quick-menu items): §11 "Item availability": the quick menus' root items are
  enabled by name from the drivers' result block (REPLAY / SAVE REPLAY after a run, SAVE
  GHOST / DATA LOGGER after a session record, SETTING, EXIT / SAVE GAME, FREE RUN locks); §5
  the disabled look in detail (the focused item's highlight at half brightness also under the
  `disable` widget). Implemented (ours enabled every quick-menu item).
- 2026-10-21 (post-run replay): §11 what is behind a quick menu: the race in play mode 2 for
  the time trial, free run, machine test and the GT pre-race menus, none for the result menus
  (implemented; ours had drawn the frozen last frame behind every menu).
- 2026-10-21 (later): §11 Triangle / Square on a race quick menu's root page (cancel flag
  0x100) is EXIT at once: SE `return`, the runners' preset result 1 = qm_exit; the
  championship pages and the popups ignore it (implemented; ours had rebuilt the page).
- 2026-10-21: §7 the name entry's text box has a cursor (gt_mode.md §17): keys insert at it,
  L1 / R1 move it, the caret is drawn there (implemented for the record and the file name
  entry).
- 2026-10-20: §5 where a button's alt (highlight) is drawn: at the button's own place and in
  its slot of the draw order; the alt's layout x / y / z are never used (implemented; the GT
  Auto page's Wheel Shop highlight had been drawn 10 units right of its button).
- 2026-10-20: §10.1 the Favorite Music List page (PlayList widget): keys, lifting / carrying
  a song, Order / Shuffle, the intro preview, the scroll, the looks, the commit on Cancel
  (none with a song lifted); §10 / §3 the option pages have no BGM (option01 has no caller).
  Implemented (`MusicListPage`).
- 2026-10-20: §1 / §6 the Replay Theater page's music is stream 0x1A (the "Are You Gonna
  Go My Way" remix, music.md §5), not theater01; the page indexes are BGM ids.
- 2026-10-20: §6 which transitions are used: menus always type 0, the race exit type 1 (its
  wash grey 0.1, implemented in the race screen), types 2 and 3 never; the pages that cut.
- 2026-10-20: §1 the title focus (one index the `root` hook always returns; how a session's
  start, `go_top` and `set_focus_replay_theater` set it); §3 the initial focus rule (the
  remembered {page, index} is never read), the GT-mode control's focus hooks, the stored race
  code and the series and licence hooks (implemented).

Implementation map: `menu/flow/menu_set.*` (loading), `menu/flow/menu_controller.*`
(page manager + button lists + animations), `menu/flow/screen_transition.*`
(loading transitions), `menu/flow/menu_sound.*` (effects), `menu/flow/game_flow.*`
(top-level flow), `menu/flow/theater_page.*` (Replay Theater file list), `menu/flow/game_hooks.h` (race hook), `app/screen_gt3menu.cpp`
(`gt3r --screen gt3menu`).

## 1. Top-level flow (H)

- Boot: game init, memory-card warning screen, opening movie
  `stream/opening.pss`, memory-card auto-load, then the main loop.
- **No language selection and no PUSH START in the US build.** The region
  index starts as 1 ("US") and only changes when a save is loaded (the
  language stored in the save). `menu/language.mbl` (br/fr/gr/it/sp,
  `set_lang`), `launch.mbl`, `default.mbl` are not referenced by the program;
  `ar/push_start` is only reachable from arcade entry points without callers.
- Region folders: 0 JP, 1 US, 2 BR, 3 FR, 4 GR, 5 IT, 6 SP.
- Main loop: run one menu session (GT-mode control on `menu/gt_mode.mbl`);
  then dispatch a mode value set by the chosen command; return to the menu
  session, which resumes at the saved page.
- **Title menu** = page `root` of `gt_mode.mbl` (pmb 3), drawn over the
  looping movie `stream/title_full.pss` (the page's `mpeg` widget). Items in
  order: `arcade` (`go_arcade_menu`), `gt_mode` (`go_sitemap`),
  `replay_theater` (link), `load_game`, `save_game`, `file_operation`,
  `option` (`go_option`). The items are parked off-screen in the layout (x = 693) and placed by code
  in the `menu_top` window as a vertical scrolling selector.
- **Title focus**: the GT-mode control keeps one title-focus index, and its
  focus hook for `root` always returns the current value, so the title never
  uses the "item leading back to the page we came from" rule (§3). It is set:
  - when a menu session starts, from the mode value that ended the previous
    session: Arcade (1) → 0, Replay Theater (11) → 2, Load (26) → 3, Save (25)
    → 4, File operation (24) → 5, Options (9) → 6, anything else → 0 (the
    theater's demonstrations 12 and Analyzer 13, races, boot). An idle title
    ends its session with result 0 and no command, so the mode stays 0 (it is
    cleared when each session starts) and the title after the attract demos
    and the opening movie is on Arcade (M: no write of the mode was found in
    the demo races, traced two call levels down);
  - by `go_top` (Cancel on `map`) → 1;
  - by the back action `set_focus_replay_theater` (Cancel on `replay_theater`)
    → 2; it runs before `root` is built.

  Net effect: the title always opens on the item we came back from; at boot on
  Arcade.
- Idle on `root` longer than a database value × 60 frames ends the session
  with result 0: attract mode (up to five arcade demo races, then the opening
  movie again). (Not reproduced.)
- `go_sitemap` switches the same control to the GT-mode phase, page `map`
  (pmb 101, import page `00cmn/status`). Cancel on `map` runs back action
  `go_top`: back to `root`, focus on "gt_mode".
- GT-mode race commands: `set_race_code` / `_spot` / `_info` store the item
  name as race code (`_champ`: the item name with its last character + 1);
  `go_race`, `go_license` (race code = item), `go_machine_test`,
  `go_free_run` (race code `ATAF0001`), `go_preview` start the race. After the
  race GT mode resumes at the page; an `..._entry` page resumes at its parent
  race list, except after `go_preview`: the entry page, on PREVIEW (gt_mode §11).
- Commands not reproduced yet (logged by the reimplementation): `go_setting`,
  arcade endings, `load_guest_garage`, i.LINK (the memory-card and Replay
  Theater commands are, see below and gt_mode.md §19).

### Replay Theater (H unless marked)

- Title item `replay_theater` links to page `replay_theater` (gt_mode.mbl, pmb 3,
  BGM 0x1A (stream str91a, looping), over the title movie; back `root`, back action
  `set_focus_replay_theater` = title focus 2). Its items end the menu with a mode
  value the main loop dispatches (jump table 0x3082B0): `go_replay_theater` 11,
  `go_demonstration` 12, `go_logger` 13.
- 11 / 12 (fn_00100838): the memory-card control (fn_0014AE00) on the replay
  file page (11: file_operation "Load replay"; 12: page `demo_loader`, the
  DEMONSTRATION list of the disc's demonstration runs: 48 licence tests then 10
  arcade time trials, 8 icons to a row, rows scrolling, info panel Size "----",
  Date "--/--/----", Mode "License B-1" / "Time Trial 1", File Name "B1_demo" /
  "TT1_demo", Car, Power, Course, Record). A loaded file is played once as a
  replay (race vf +0x150 with (1, 1), replay mode 1; Start = the replay pause),
  then the menu session resumes on `replay_theater` (captured: focus on its first
  item).
- 13 (fn_00136FC0, LoggerControl): menu/logger.mbl page `top` (logger1.pmb):
  TRACK A / TRACK B info panels (Car, Power, Course, Time, Date, Mode, File Name),
  buttons REPLAY, GRAPH, LOAD (A), LOAD (B), EXIT (first focus REPLAY; REPLAY /
  GRAPH dim and do nothing without a track); LOAD opens the memory-card page
  "Load track"; REPLAY / GRAPH open page `replay_logger`.
- Reimplementation (docs/dev/RACE.md "Replay Theater"): Load Replay and LOAD A / B
  use the DEMONSTRATION page's look over native replay files (no slots, no
  windows); Start deletes a replay after one "Delete OK?".

### Arcade (H)

- `ar/top` (arcade.mbl, page flags 0x1E6): the focus on entry is the game
  played last. Its actions only store the game index; internal names differ
  from the US labels:

| item | action | US label | screens before the race |
|---|---|---|---|
| item1 | `single_race` | Single Race | `ar/level` → `ar/select_course` → `ar/select_class` → `ar/select_car` |
| item2 | `compulsory` | Time Trial | `ar/select_compulsory` |
| item3 | `time_attack` | Free Run | course → class → car |
| item4 | `two_player_battle` | 2 Player Battle | course → `ar/select_car_2p` |
| item5 | `goodies` | Clear Status | `ar/goodies` |
| item6 | `load_guest_garage` | Load Garage | memory card |
| item7 | `i_link_battle` | i.LINK Battle | `ar/connect`, `ar/wait_other`, then course/class/car (reimplementation: the online lobby, docs/dev/ONLINE.md) |

- Back on any step goes one step back; Back on `ar/top` runs
  `cancel_select_game` and returns to the title (focus "arcade").
- Difficulty is the focused index on `ar/level` (0 easy, 1 normal, 2 hard;
  the page has three items); professional (3) is Hard confirmed while L1
  and R1 are held, not a progress unlock (gt_mode.md §16). The `diff_*`
  action strings are never read.
- Course / class / car / time-trial pages have no `.mbl` items: control code
  draws them from the page's `hidden` layer (`<id>_logo`, `<id>_doc`,
  `<id>` course map, `<id>_bg`; cars `<car>` picture and `<car>_mini`) with
  full-screen `stream/course.ipic` pictures. The reimplementation shows them
  as left/right carousels over that art (approximation: no picture
  archive, no class filtering, no car specs).
- **Race start**: the arcade session ends with a race descriptor: player
  car, course, race code, race type (1 single, 2 time trial, 3 free run, 4 2P,
  5 i.LINK) and options. Race codes: single race `A{E,N,H,P}{C,B,A,S}T0001`
  (dirt `A?AD0001`), free run `ATAF0001`, 2P `A2P0001`, time trial = the
  car's compulsory code. After the race the arcade menu reopens on `ar/top`.

## 2. Input (H)

- Logical keys: D-pad or left stick beyond ±0x40 of centre = directions;
  **confirm = Circle or Cross, cancel = Triangle or Square** (no regional
  swap anywhere); Start, Select, L1/R1, L2/R2 separately.
- Auto-repeat (directions, L1/R1, L2/R2): an event on the press frame, the
  next one 31 frames later, then every 4 frames; one shared counter per pad,
  reset by any new press. Pressing a D-pad direction with pressure ≥ 250
  repeats every 2 frames (not reproduced: digital pads).
- Per frame: Up (else Down), and only if neither fired, Left (else Right).
  Confirm and cancel count on the press edge only and are ignored while any
  direction is held; cancel only if confirm did not fire.

## 3. Page manager (H)

- Page switch selects the `.mbl` page, loads `<set><N>.pmb` for its pmb
  number when needed, the import page from its own pmb, and the layout page
  of the same name. Only the `defaultLayer` is shown by default; other
  layers appear through focused items (§4) or control code. Elements of the
  `hidden` layer are templates for code.
- **Confirm** on the focused item: (1) a per-page intercept hook may open a
  popup instead; (2) the item's **action** runs through the control's action
  table (unknown names are ignored); (3) the **kind** (low nibble of the item
  u16 at +0x142) decides: 0 go to link (if any), 1 open link as a popup level
  (the page below stays drawn, its list inactive; from inside a popup the new
  page replaces the popup, there is only one level, so the tune shop's
  Purchase? -> Install parts now? -> Installed chain closes back to the
  part's page), 2 finish the menu (result
  1; the control code takes over), 3 same as cancel, 4 multi container
  (nothing). Confirm feedback (§5) runs before the link/finish/back.
- **Cancel** works only on pages with flag 0x100. In a popup: SE `return`,
  close the popup, the lower page keeps its focus. Else if `back` is set: SE
  `cancel`, run `back_action`, go to `back` (`"return"` = the page we came
  from). Else: SE `return`, run `back_action`, finish with result 0.
- **Initial focus** when a page is built: the control's focus hook where it
  has one for the page (e.g. `ar/top` → last game, `ar/level` → last
  difficulty); else the first item whose link is the page we came from (so
  backing out lands on the item that led there); else 0. The came-from lookup
  gives 0, not "none", when no item matches, so nothing further is tried. A
  page a menu session starts or resumes on is built with no previous page:
  its hook, else 0. On confirm the page manager also writes a single
  remembered {page, index} for pages where two items link to the same page,
  but never reads it. So a dealer's maker page (all cars link to its shared
  `_car` page) opens on its first car both when entered from the country page
  and when backing out of a car page (M: not checked in the emulator).
- **GT-mode control focus hooks**: the control's list holds 92 page names:
  `root` (§1); the six licence pages `lc/lc_a`, `lc_b`, `lc_d`, `lc_ia`,
  `lc_ib`, `lc_s`; 85 race-series pages `r/<class>/<series>` (not their
  `_entry` pages, not the class lists such as `r/beginner/beginner`). The list
  is matched case-sensitively and holds lower-case names only, while the menus
  spell nine endurance pages with a capital letter (`r/end/Eakasaka_n`,
  `Elaguna200_n`, `Emonaco_n`, `Eroadster_n`, `Erome2h_n`, `Eroute11_n`,
  `Eseattle100_n`, `Essw150_n`, `Etrialmt2h_n`), so those have no hook: 76
  series pages do (all with set_race_code items and a lower-case name; the 27
  with a championship item use the two-digit rule below). No dealer, shop or
  garage page has a hook.
- **The stored race code**: one string and a race type in the game state (not
  in the save). Written by `set_race_code` (the item name, type 4),
  `set_race_code_champ` (the item name with its last character + 1, type 2),
  `set_race_code_spot` (the item name, type 3), `set_race_code_info` (the item
  name without its last `_...` part: "Gsunday_e_0002_info" → "Gsunday_e_0002";
  the type is kept), `clear_race_code` (""), `go_license` and
  `go_machine_test` (the item name), `go_free_run` ("ATAF0001"), an arcade
  Single Race, Time Trial or Free Run (its race code; 2 Player and i.LINK
  battles do not), and a championship resumed at boot (its code + 1, type 2).
  A race does not change it. `clear_race_code` is the race information popup's
  back action, and a popup's back action never runs (closing a popup only
  closes it), so the information popup leaves its race's code stored.
- **The series and licence hooks** read the stored code's last characters
  and clear the code to "" on every call:
  - licence pages: the last digit − 1 ("L0B0003" → 2, that test); outside
    0..7 → 0;
  - a series page: the last digit ("Gsunday_e_0002" → 2, that race's item: the
    race information item is item 0); negative → 0;
  - a series page with a championship item (`_info`, the championship
    `_x00`, then `_x01`, `_x02`, ...): the last two digits, + 1 unless the
    type is 2 (a single race "...0502" → 3, that race; the championship
    "...0501" → 1, the championship item); negative → 0.
  A digit the code is too short for reads the bytes before the string (the
  high bytes of a page index that is -1 or small), which gives a negative
  index, so an empty code gives 0. A large index is not range-checked by the
  hook (the list clamps it to its last item, M). As these hooks always return
  0 or more, the came-from rule never runs on those pages. So: backing out of
  a race's `_entry` page lands on that race; after a race the session resumes
  on the series page (an `_entry` page resumes at its parent) on the race just
  run, after a championship on the championship item, after a licence test on
  that test; the first visit from the class list (code cleared by the last
  hook) lands on item 0, unless something stored a code since (the race
  information popup, a licence test or machine test, an arcade race): then its
  digits pick the item, whatever series it belongs to. The endurance pages
  without a hook use the came-from rule: back from the entry page on the race
  (item 1), after a race on the information item (0).
  Reimplementation (`GameFlow`, `race_page_hook` / `race_page_focus`): as
  above; the hook lists are derived from the pages (name and items), which
  gives the program's 6 + 76 pages on this disc (`menu_flow_disc_race_page_hooks`).
  Not reproduced: a championship resumed at boot (ours resumes it on entering
  GT mode and returns to the map).
- Page flags (`.mbl` page u16 at +0x42): 0x1 horizontal list (Left/Right
  move), 0x2 wrap (else clamp), 0x4 fade from black on build, 0x8 fade to
  black on leave, 0x30 frame-buffer clear mode (0x10 on after a 30-frame
  timer, 0x20 on at once; meaning M), 0xC0 confirm feedback, 0x100 cancel
  allowed.
- Page change sequence: confirm feedback (blocking); if the old page has
  flag 0x8, a black full-screen quad fades in over 15 frames (blocking, then
  it holds 6 frames and is removed); the old page is removed (popups fade out
  per element); 2 frames wait; the new page is built (blocks until 10
  vblanks since the last wait); if it has flag 0x4 a black quad fades out
  over 30 frames (input allowed). There is no whole-page cross-fade.
- Menu BGM is chosen when a page is built at level 0, by page-name prefix
  (later tests win): `cd/` and `00cmn/cd_ride_sure`; `at/` and
  `00cmn/ws_fit_sure`; `hm/`; `lc/`; `r/` and `00cmn/race_info`;
  `replay_theater`; `ts/`; `map`; `mt/`; `ar/`; any other page (the title)
  stops the BGM. The game passes a BGM id (2, 0x10, 6, 7, 4, 0x1A, 0xF, 5,
  9, 0, stop; music.md §1) to its music player: the sequences dealer01,
  wash01, home01, license01, gorace01, the stream 0x1A (Replay Theater),
  tune01, gtmenu01, machtest01, arcade01. The option control's pages match no
  prefix: no BGM there (§10; option01 has no caller).

## 4. Button lists (H)

- Buttons are built for `.mbl` items that have a page-level `.pmb` element
  of the same name; others are skipped. Class by element-name prefix:
  `slider` → slider, `multi` / `free` / `concurrent` with kind 4 and
  children → nested lists, anything else a plain button (or a list-specific
  subclass).
- Moving the focus plays SE `select`; no sound when blocked at a clamped end.
  **Disabled items stay focusable** and refuse confirm with SE `disable`.
- On focus gain the item's layer str80 (+0x80) is instantiated (its
  elements fade in if flagged) and the previous one dismissed; the group
  layer str40 (+0x40) is instantiated with a slide: direction 1 (Up on a
  vertical list) starts 448 px above and the old one leaves 448 px below, 2
  the opposite, 3/4 640 px from the left/right. The motion is a spring
  (acceleration = 600 × distance, velocity damped by 30/60 per frame,
  integrated at 1/60 s) running 31 frames: about 90 % there after 10 frames,
  no overshoot.
- Multi buttons: a nested list of the children (child items are page items
  too: the chosen one is drawn in its alt look). Kind bit 0x10 = nested list
  horizontal, 0x20 = wrap. Left/Right change the value immediately (SE
  `select`); Up/Down still move the outer list; confirm activates the chosen
  child (its action, e.g. `commit`, and its kind, e.g. 3 = back).
- Sliders: Left/Right ± step, L1/R1 ± 10 steps, SE `param` on change,
  `disable` at the limit or when the value is rejected (not reproduced).
- Scrolling lists ease towards the focus by 30 % of the remaining distance
  per frame; confirm feedback waits until less than 1 px remains. A horizontal ButtonList
  eases its row offset (ButtonList::vf2 `0x1C9CA8`: offset += (target - offset) x 0.3, the
  factor at `0x350084`), the target from vf8 `0x1C9B58`: the row centred while it fits the
  window, else left-aligned while the focused button starts less than half a window from the
  row's start (`0x1C9938`), right-aligned while less than half a window follows it
  (`0x1C99B8`), else the focused button centred. Buttons are their elements' widths apart
  plus the list's gap (+0xF4), 4.0 for every list (`0x1C8B90`, `0x1C88D0`'s callers) but
  `menu_ride_sure` (54). Ours: `menu::flow::row_layout`, `MenuController::Level::row_off`.
  A vertical ButtonList is placed the same way along y (the heights at +0x3C: `0x1C98A8`
  total, `0x1C9A48` the focused button's top, `0x1C9AC8` what follows it), except that a list
  that fits its window stays at the window's top (vf8 centres only a fitting horizontal row);
  its offset (+0x108) eases the same way, and ButtonList::vf10 (`0x1C93D0`, the garage's
  rebuild after a sort) places it at once. The code-drawn vertical lists are such lists: the
  garage and trade lists (GarageButtonList `0x1BD720`, gap 4: 24-unit rows 28 apart), Cars
  Permitted (`0x1B5E88` -> `0x1C88D0` with gap 4: 20-unit rows 24 apart) and the records
  (RecordsButtonList `0x1735A0`, gap 4: 56-unit bars 60 apart; a licence test's six fill the
  356-unit window; focus moves play SE `select`, ButtonList::vf13 `0x1C9E38`). Ours:
  `menu::flow::column_layout`, `ListRows` (gt_mode_menu/lists.cpp; rows clipped to the
  window; a hovering pointer moves the cursor without scrolling, as the music list).
- ScrollBar (`0x1C34C8`, widget class `scroll_bar`, GT Mode only: the league pages, the
  garage and trade lists, Cars Permitted, the garage wheel list): nothing, track included,
  while the list is no longer than the bar (`0x1C9818`: the buttons' widths and gaps); else
  the track and the hidden `pinch` at bar x - offset x bar w / list w, window w x bar w /
  list w wide (`0x1C9700`: the offset is the row's start from the window's left edge). A
  vertical bar likewise down the bar (top-down): top = bar top - offset x bar h / list h,
  window h x bar h / list h high, the bar's width.

## 5. Visuals and animation (H)

- Elements fade in when their flag bits 20-23 are non-zero (only 0/1 occur):
  alpha 0 → 1 linearly over 24 frames from the frame after creation. Flag bits
  24-27 (boolean): on dismiss alpha 1 → 0 over 9 frames, otherwise the element
  vanishes at once; a dismissed container dies after 9 frames.
- Element animation in the original is a small per-object script language
  (wait, move, spring, approach, colour/alpha rates, blur…; rates are per
  second applied as 1/60 per frame; a timed step of T seconds lasts
  trunc(T × 60) + 1 frames).
- **Focus look** (element type bits 12-15): with an alt (highlight) child
  the alt is drawn *instead of* the normal look. Style 0x2000: a pulse p goes
  0.5 → 0 by 1/60 per frame and wraps (30-frame sawtooth), and the normal
  look is drawn over the highlight with alpha 2 × (0.5 − p) (the highlight
  gradually fades out and pops back every 0.5 s). Style 0x1000: p = 0.5
  constant. Unfocused or inactive list: p = 0.25. Without an alt child: an
  additive grey quad of level p over the button. A page widget named `focus`
  is drawn at the focused button with alpha p + 0.5; one named `disable` over
  disabled buttons, else disabled buttons are drawn at half brightness. In detail (C,
  2026-10-21): a button is enabled unless its enable hook (+0x140, context +0x144, called with
  the item name +0x118; `fn_001B3F18`) says no. The focused button's alt gets the dim flag
  (+0xFC = !enabled, `fn_001B41F8`): a disabled focused item's highlight is always at half
  brightness (RGB x 0.5, alpha kept: `fn_001BA5B8`); the normal look is dimmed only when the
  list has no `disable` widget. The list's `disable` item (+0x124, found by name with `focus`
  +0x120 at build, `fn_001C87E0`) is drawn right after the button (`fn_001B4288`) at the
  button's x / y with the button's w / h, focused or not. The race quick menus' widget is an
  88 x 76 white mask (transparent corners) in black 000000B1: about 69 % black over the icon.
- **Where the alt is drawn** (Button draw `0x1B44E8`; every Button class, the multi / free /
  concurrent buttons' choices included): each frame the button stores its place, its
  layout x / y (plus the list's window offset and scroll in a scrolling list, `0x1B4008`
  → +0x158 / +0x15C), as its own x / y and **copies the same x / y into its alt**
  (`0x1B4130`); the alt keeps its own w / h. The alt is not in the page's z-sorted draw list
  (sorted insert `0x1BF578`): the button draws it itself (`0x1B41F8`), then the normal look
  over it (`0x1B4288`), so the alt is drawn in the button's slot of the draw order and its
  own z is unused. The `focus` widget is placed the same way, at the button's x / y (not
  centred; no US page has one). The layout files' alt x / y / z are therefore free: of the
  1,454 page buttons with an alt on the US disc, 1,199 have an alt at another x / y or z
  (the GT Auto page's Wheel Shop alt 10 units right, the dealer car pages' Spec alt at x 904,
  the Credits page's last three dots' alts at 0, 0 with z 0, the name entry's OK alt at z 5
  under its panels, the option pages' choices 1.9 units up).
- **Confirm feedback** (page flags & 0xC0), blocking:

| value | sound | effect |
|---|---|---|
| 0x00 | `ok` | none |
| 0x40 | `next` | DecideEffect: additive white strip over the button (bright centre column, dimmer edges); centre alpha 0 → 0.4 in 3 frames, edges 0 → 0.4 in frames 5-10, centre back to 0 in frames 12-17, edges in 19-24, ends at frame ~26 |
| 0x80 | `next` | 30 frames flicker: q cycles 0.4, 0.6, 0.8, 1.0 per frame; the normal look over the highlight with alpha 1 − q (no alt: button alpha q) |
| 0xC0 | `next` | 30 frames blink: button and alt alpha cycle 1.0, 0.8, 0.6, 0.4, 0.2 |

- **Text inset** (M, measured on references): left / right aligned pmb text is drawn
  about 12 units in from its box edge (race menu entry list: names +12.6, times -15; the
  name entry's help labels +12; its caret ~14 units into the empty text box). Applied so
  far to the race quick menus and the name entry (`menu::apply_text_inset`); other pages
  were tuned against captures without it and still need a pass.
- Text styles (text `align` bits 4-7): 0x10 marquee, scrolls left by one
  box width every 240 frames and restarts after the text has passed; 0x20
  typewriter, one more character per frame (max 255).
- Widgets of class `blink` (e.g. the title's arrows): 30-frame blink (up in
  6 frames, down in 24). Class `mpeg`: the background movie.
- Blur2D (arcade): full-screen blur, radius 0 → 2 over 30 frames. DecayItem:
  90-frame fade-out on dismiss. (Not reproduced.)

## 6. Loading transitions (TransitionObject) (H)

Four global objects, started with a type and drawn every frame by the
loading thread **on top of the last picture without clearing**, so their
effects accumulate. Screen space 640×448, y down.

| type | name | per frame |
|---|---|---|
| 0 | Fadeout | a full-screen wash of colour 0x0C at alpha 0x0C/0x80 every frame (exponential fade to black) |
| 1 | DelayFadeout | nothing for 10 frames, then a full-screen wash of colour 0.1 at alpha 0.5 every frame |
| 2 | Lineout | opaque black 1-px columns: even columns from the left edge and odd columns from the right edge, 16 px further each frame (frame k covers k × 16 px), i.e. a striped wipe |
| 3 | Combout | 256 row pairs (2 px high), each with a start position random in [−320, 0); every frame each row is filled from x = 0 to its position with colour (0x20, 0x20, 0x20) at alpha 0x60/0x80, then the position advances by 640/24 px. Covers the screen in about 36 frames. |

Type numbers: `fn_00102228(type, flag)` branches 0 → `FadeoutTransitionObject`, 1 → Delay-
Fadeout, 2 → Lineout, 3 → Combout (objects checked against the constructor `fn_00102328`;
docs/specs/presentation.md §C1). An earlier version of this table had the numbering reversed.
All four: after 180 frames a 16×16 square at (20, 20) blinks between grey
0.9 and black every 32 frames (the "still loading" indicator).
Use: leaving a menu control (title → arcade / options / race, arcade → race or back) = the
menu's type, RenderBase +0x40 (default **0, Fadeout**; the capture of the title being left shows
the even darkening); race → menu = **type 1, DelayFadeout** (`RacePS2Base::vf42`), drawn by the
race over its last frame. Entering a menu or a race is a cut: no fade-in (the page's own
elements fade in by their flags). The menu BGM is not tied to these fades (it stops / starts
on its own; docs/specs/music.md).

Users [C]: every menu control starts type 0 (the controls' set-ups at 0x119908, 0x114090,
0x12C668, 0x13A130 leave RenderBase +0x40 at its default; the setter `fn_001CB908` is only a
vtable slot and is never called); type 1 only at the race exit (`RacePS2Base::vf42`, the call at
0x17D470), whose wash colour `[0x34FA08]` is grey 0.1 on all three channels; types 2 (Lineout)
and 3 (Combout) are never started. The data logger, the race prize pages, the memory-card
warning and the Records pages start no transition (a cut). Ours: `GameFlow` asks for Fadeout
when a control is left; the race screen draws the DelayFadeout's accumulated wash over its held
last frame (`ScreenTransition::delay_fadeout_cover`; it was black before).

## 7. Sound effects (H)

Named effects from `sound/se.inf` (bank `gt3sys.ins`): `select` focus moved
in a button list; `cursor` other widgets (map, garage, text boxes, code-drawn
pickers); `ok` / `next` confirm (by page flags); `disable` confirm on a
disabled item / slider limit; `cancel` back to the `back` page; `return`
close a popup or leave the menu; `param` slider change. The player is gated
by the "menu SE" option.

## 9. Arcade pickers and code-drawn menu screens (H unless noted)

Course (`ar/select_course`), class (`ar/select_class`), car (`ar/select_car`)
and time-trial (`ar/select_compulsory`) pickers share one design:

- **Icon strip** (MiniCursor) on the page's hidden strip element
  (`mini_courses` / `mini_classes` / `mini_cars`, x 20..620, 32 high): one
  `<entry>_mini` icon per **available** entry (locked entries take no slot),
  4 px apart (course 55 x 42, class 40 x 40, car 51 x 27, time trial
  120 x 50). Scroll target: total = N·step − 4; if total ≤ 600 the strip is
  centred and still; else it is left-aligned while the focused icon is in
  the first half, right-aligned in the last half, otherwise the focused icon
  is centred. The scroll eases 30 % per frame and starts at 0 (the strip
  glides in). The focused icon pulses: brightness p (PS2 units, 0.502 =
  normal) is set to 1.0 on a move and falls by 1/15 per frame, back to 1.0
  below 0.25 (12-frame sawtooth). Disabled entries get the `x` overlay at
  the icon centre. No cursor frame, no scaling.
- **Cross** (`cross_bar`, a translucent colour quad): drawn as a band across
  the screen at the element's height plus a vertical bar of its width that
  sweeps with the cross (x wrapped with a 680 px period). Each move sets the
  cross target 680 px to the left (Right) or right (Left); the cross
  springs there (acceleration (target − x)·600, damping 30, 60 Hz; 55 % in 5
  frames, 99 % in 20, no overshoot). The entry's art (course map `<course>`,
  `_logo`, `_doc`, `arrow` marker, `total_length` / `length`, the
  `area_a..f` group badge, `dirt` / `tarmac`; class `_logo`, `_doc`; car
  picture `<car>`, drive badge, `ps` / `kgm`; time trial `_logo`) rides on
  the cross: new art starts 680 px to the side and fades in over 24 frames,
  old art slides out and fades over 16 frames.
- **Ends**: course, car and time-trial lists clamp (SE `disable` at an end,
  `arrow_l` / `arrow_r` hidden at the ends); the class list wraps (both
  arrows shown).
- **Background**: course / class pages show a still of the entry
  (`stream/course.ipic`, swapped at once) and, once the cursor has rested 6
  frames, the looping movie `stream/<entry>_full.pss` (`class_<x>_full`,
  `home_garage_full`; none for the guest garage) in the `mpeg_course` /
  `mpeg_class` widget. The car pages run `stream/menu_bg2.pss`, `ar/top`
  `stream/arcade_menu_full.pss`, `ar/level` `stream/level_menu_full.pss`.
  The time trial's `ttNN_bg` is swapped in place.
- **Car page** (`ar/select_car`):
  - 3D car (recreated on each move, fading in over 1 s), camera from
    `menu/showroom_single.fbi`: look-at (0, 0.5 m, 0) (springing up from
    0.25), 10 degrees above, 53.13 degrees horizontal field over the 640 px
    screen (focal 640 px), near 0.1, far 1000; on each new car the distance
    springs 2 -> 6 m (k 6, damping 6) and the yaw starts at 190 degrees with
    −10 per frame acceleration and 0.5/s damping (steady 20 degrees/s). The
    body is lifted by the ride height q (wheels on the floor). A floor
    reflection is drawn under the car.
  - CircleGauges `handling_gauge`, `speed_gauge`, `acceleration_gauge`
    (centres (367.5, 402), (457.5, 402), (537.5, 402) on screen): background
    ring 22..238 degrees, radii (w + 4, h + 4) = (29, 20), thickness 0.5,
    #333333; value arc from 230 − 200·f to 230 degrees, radii (25, 16),
    thickness 0.3, #FF9900, the part beyond f = 0.8 in #FF0000. Rebuilt per
    car: f rises 0.04 per frame to 1, then falls 0.02 per frame to the value.
    Values [C, `0x116048`] read the page's ShowRoomCar (spec at +0, converter
    C at +0x3E4): handling = spec byte 0x0C × 0.1, acceleration = spec byte
    0x0D × 0.1 (7 when 0; the builder never writes them, so 0.7 for every
    car, confirmed on the reference screens); speed = the top speed in km/h /
    350, at most 1 (7 when not positive). The km/h is spec u16 0x08 if
    non-zero (never written), else trunc(C+0xDF8 × 3.6).
  - Showroom top speed C+0xDF8 [C, `0x1DB498` / `0x1DB2C8`]: start value
    v_max = rev limit (C+0x1C4) × 2π/60 / g_N, where g_k (C+0x254 + 4k) is
    engine rad/s per m/s in gear k and N the gear count. Then for
    s = 0, d, 2d, … < trunc(v_max × 3.6) km/h, d = that / 500 + 1 (integer),
    starting in gear 1: v = s / 3.6, ω = v·g_k; if
    RR(v)·rr[0]·(m·9.8 + (C_front + C_rear)·v²) + C_drag·v² < T_wot(ω)·g_k
    (rolling curve and tarmac coefficient of physics §3.5, m with the 120
    kg, aero coefficients of physics §6.4, full-throttle torque without the
    event power cap) the top speed becomes v; then up one gear if k < N and
    trunc(ω·60/2π) > UP. The reference screens match the aero coefficients
    at air density 1.2 (NSX-S Zero 297, R34 270, Xsara WRC 250 km/h).
    **Open:** the original routine run in the diff harness (density 1.2,
    base and arcade-variant labels alike) gives higher drag-limited speeds
    than the screens (NSX-S Zero 307, R34 280; gear-limited rally cars
    agree), and the reimplementation, built from the same description,
    lands on the screen values. The cause (gauge arc scale assumed 200°,
    a menu-side input, or a sim input) is not found yet. Handling and
    acceleration: spec bytes 0x08, 0x0C and 0x0D are 0 for all 234 cars
    after the original pipeline (harness), so 0.7 everywhere is not a
    class adjustment (checked on NTSC-U 1.10 only).
  - Colour column (SelectBox): 18 px pitch around (59, 206); only the
    current paint is filled (its RGB); outlines for one slot above and five
    below; `arrow_up` / `arrow_down` in the neighbour slots unless at an end;
    eased 30 % per frame. Up / Down change the paint (SE `cursor`, `disable`
    at the clamped ends); each new car starts at its first paint. Paints:
    carcolor.db for the car's model; `color_name` shows the paint's name
    (carcolor.sdb; a name starting with `-` shows nothing).
  - Spec ticker (TransitiveEX): "Length\n%dmm", "Height\n%dmm",
    "Width\n%dmm", "WheelBase\n%dmm" (always mm), one every second (4.5 s
    cycle), black `sw_20#1`, placed at (100, 250) and rising 40 px/s; fade in
    0.5 s, hold 3 s, fade out 0.5 s.
  - `ps` = power + unit + "/" + rpm string, `kgm` = torque with two decimals
    + unit + "/" + rpm; NTSC-U: "276HP/7300rpm", "224.21ft.lb/5300rpm".
  - Back on states 2 / 3 steps back on the same page (the reimplementation resumes the page
    controller there: the page's own cancel had ended it and input stopped).
  - States: 1 car and colour; OK → 2 AUTOMATIC / MANUAL (`auto_*` /
    `manu_*`); OK → 3 RACING / DRIFT (`racing_*` / `dri_*`; skipped on dirt
    courses and for garage cars); entering 2 fades the strip out, blurs the
    background (radius 0 → 2 over 0.5 s) and springs the page up so the
    button row sits at y ≈ 214 (buttons at x 220 / 420).
- The two-player car page has two strips and no cross.
- **Units**: the NTSC-U menus show miles ("1.494miles", three decimals), HP
  and ft-lb; the format is chosen by region (no option found).
- Single Race breadcrumbs: the `history_level_*`, `history_area_*`,
  `history_class_*` icons sit side by side at the top left (x 20, 86, 152;
  M from the reference).
- Single Race breadcrumbs (M): difficulty, course-group and class icons
  (`history_*`) slide in from the right (2,560 px/s for 0.25 s) as each step
  is entered and back out on Back.

Other screens:

- **Title `root`** (`menu_top`, TopButtonList): the focused item is centred
  in the window; the other items fade with their distance d from the window
  centre, alpha 1 − d / (1.8 × item height), invisible beyond (so one faded
  neighbour above and below). `arrow_up` / `arrow_down` blink.
- **`ar/top`, `ar/level`**: `diagonal_in` / `diagonal_out` logo pair
  cross-fades corner-diagonal by corner-diagonal on a ~10 s loop (in: 2 s
  hidden, 1 s rising, 4 s shown, 1 s falling, 2 s hidden; out is the
  complement). `ar/level`: Hard shows `pro` / `pro_a` while L1 + R1 are held;
  one `flash_item` widget (phase +8 per frame, wraps at 240; its drawing was
  not found, L).
- **GT Mode `map`** (MapButtonList): a pointer (`pointer`, 21 x 32, tip at
  the point) starts at the screen centre; a direction picks, among the
  buildings within 90 degrees of it from the current one, the one that
  maximises cos² / distance; the pointer eases there 30 % per frame and the
  building takes the focus (active art and its `s_N` description ticker in
  the base bar; the hidden `name_<id>` labels are not shown) once within 12 px. A pan / zoom is computed but not
  applied (M).

GT Mode screens (compared with the reference captures; H for the code facts, M for the
measured placements):

- **Status bar** (import page `00cmn/status`): the day counter is the `user_day_unit`
  widget's own template filled with the day (`0x11CB60`: "day %d" -> "day 12"), grey
  `cccccc` with its black shadow widget; the hidden `user_day` / `day` elements are not
  shown. The widget is 24 units wide and right-aligned; the text is not squeezed into it and
  ends 10 units before the box's right edge (the reference "day 1"; M: possibly the width
  of the template string is what is aligned).
- **Dealer car page**: the paint column (code-drawn from `car_color_box`, 16 x 10): the
  first paint on top, swatches 12.5 units apart, the column ending one step above the
  widget (whose own white box is not drawn); each swatch over a soft black drop shadow
  (+3, +3); the chosen paint marked by a white 8 x 10 square 4 units to the left of its
  swatch. The price and the paint name start one space in (" 17,000", " Crystal White").
  The 3D car's size and height match the reference side views.
- **Garage car page** (`hm/hm_car`): left / right aligned texts sit 12 units in from their
  boxes (`menu::apply_text_inset`; VALUE, drive, power, mileage, the paint name).
- **Wheel shop** (WheelShopButtonList / WheelShopButton): each wheel tile is the page's
  hidden 52 x 52 image named after the wheel (photographs, not 3D renders), with the
  hidden `sold_out` image over it when the wheel is already owned (`0x219778`, the
  inventory); a ButtonList row of the 52 x 52 items with the gap 4 (56 from tile to tile;
  the 56.5 measured on captures before had no source in the program), a soft shadow (+3, +3),
  the focused one
  washed with white at 0.38 (M). Wheels on cars in the menus load `menu/wheel/<code>`
  (detailed models; `0x21F608` with the menu flag), races `wheel/<code>`. A brand page opens on
  the wheel the ride car wears when the page sells it (WheelShopButtonList `0x1B56D0` compares
  each wheel with the car's wheel name, `0x23B068`; a car on its own wheels never matches, its
  stock wheel being named after its body model), else on its first wheel.
- **Menu car brake parts** (`MenuTireModel`, vtable 0x3410E4; draw `0x224260`, from the showroom car
  `0x1CABE0` and `CarsItem` `0x13CCF8`): the wheel records (0xC8 bytes, `0x1B8B50` / `0x13C8F0`) hold
  +0xC4 = the car's body GTM1. Per wheel: RotY(steer, + 180 for a right wheel), RotX(spin), then vf2
  (`0x2241A8`) sets +0x80 when the eye, taken into that frame (`0x258078` inverts the rigid matrix,
  the origin transformed), satisfies (x + W/2) / |(x + W/2, y, z)| < 0.05 (`0x3509B8`): outboard of
  the rim face. Then T(−W/2, 0, 0), S(b), the rim (GTTW bit 2: S(1, 1, −1) first), and with +0x80
  the body's node 2 (axle 0) or 3 (axle 1) via `0x225090`, after S(1, 1, −1) for a right wheel whose
  rim is not mirrored or a left one whose rim is (net: every right wheel's parts mirrored in z), the
  GS cull flipped for the mirrored matrix (`0x251918`). The race tyre draw (`0x2227E0`) never runs body
  nodes 2 / 3 (its brake parts are `race/brake.bin`).
- **Garage wheel list** (`hm/hm_wheel`, widget `menu_wheel` -> FreeAgent `0x11A700` ->
  WheelButtonList `0x1C0D18` / WheelButton `0x1C10B8`): one button per wheel inventory entry,
  the stock-wheel placeholder first, as a horizontal ButtonList (gap 4, `0x1C8B90`; no wrap,
  SmartIndexT `0x1CC9A0`; the cursor sound only on a move) centred on the window. Each button
  draws the page's `wheel` item (60 x 60, focus look `wheel1`), then the hidden `mark` (orange
  frame) when the entry is the car's wheel (`0x1C11C0`: the placeholder for a car on its own
  wheels, else the entry of the same name), then the entry's 52 x 52 tile: `default_wheel`
  for the placeholder, the wheel's own name otherwise. The list starts on the entry the car
  wears, else on the agent's start index (0). A focus change previews the entry on the car
  (`0x1C0FC0`; the placeholder: the car's own wheel, named after its body model, `0x23B040`).
  The 183 hidden tiles named after car models and `not_exist` are never looked up on this page
  (the only names the list asks for are the placeholder's and the bought wheels'; the other
  users of the car's wheel name, `0x1060B8` / `0x10A608` / `0x11E428`, hand it to the 3D car).
  ScrollBar (`0x1C34C8`): nothing, track included, while the row is no longer than the bar;
  else the track and the hidden `pinch` over the visible share. Ours: `WheelList`
  (gt_mode_menu/lists.cpp, extra_draws.cpp).
- **Showroom cars** (gt2k_menu::Car, `0x1B9068` / `0x1CAEF8`): accumulation
  anti-aliasing, the car in 4 passes (2 for the split and compulsory showrooms) and the
  floor reflection in 2 (1 for those kinds and while the intro fade is under 0.8), each
  pass with the projection shifted by the OpenGL jitter table entry (j2 / j4, table
  `0x2CCFC0`) × 0.6 pixels and blended at weight 1 / (pass + 1). While the car fades in the
  original also reorders the passes (not reproduced).
- **Showroom floor shadow** (public issue 192): each car pass (`0x1CABE0`) without the
  mirror flag calls `0x21F328` before the body (`0x21EED0`) and the ride-height translate:
  colour bias (`0x255C50`, scratchpad +0xA50) (0, 0, 0, 0), colour scale (`0x255C30`,
  +0xA40) (1, 1, 1, 1), then the body GTM1's node 6 (`0x225090`) in the car's floor frame
  (no lift, no projection nudge). The mirror pass skips it. In `menu/cars` files node 6 is
  a textured quad at y = 0 (the 4th texture set's soft footprint), ALPHA blended, no Z
  writes, so the colour is texel x vertex colour. The race draws the same node through
  `0x21F258` (bias = the course shadow colour 0x49D150, alpha scale +0x49D15C, lifted
  0.02 m by `0x21F0B0`, 0x350930, with a 0.005 projection nudge, 0x35092C). Ours:
  `CarModelOptions::floor_shadow`, `MenuModelRenderer` draws it as an in-order item ahead
  of the car in the car passes only.
- **Records** (time trials): see [gt_mode.md](gt_mode.md) §16 for the seeded names.

## 10. Options menu (`menu/option.mbl`, OptionControl) (H unless noted)

- Top page `option`: a row of 11 round icons (Screen Size, Sound, Favorite Music
  List, Vibration, Key Configuration, Driver's Assist / transmission, 2P Battle,
  Ghost, GT FORCE 1P, GT FORCE 2P, Default); the focused one pulses with its pale
  alt art. Each icon's layer `m1`..`m11` previews the page with widgets the game
  fills by class:
  - `<option>_<value>_<on|off>[_1p|_2p]` (e.g. `wide_mode_off_on`,
    `menu_bgm_on_off`, `assist_type1_on_1p`, `split_boost_low_on`): the `_on`
    widget (yellow) is drawn for the current value, the `_off` (dark) one for the
    others;
  - `<option>_<on|off>` placed on both buttons (`gear_auto_on/off`,
    `assist_asm_on/off`, `assist_tcs_on/off`, `ghost_on/off`,
    `force_feedback_on/off_1p`): the widgets of the current state;
  - `song_list` / `song_edit`: the race playlist (music.md §2.1), one 24 px row per
    song from the top of the list: `check_on` / `check_off` mark + "title / artist"
    (green); all in the list for a new game (M for the text start x 66). `song_list`
    (layer m3) is the view, `song_edit` the music list page (below);
  - `key_config_1_display` / `_2_display`: per action row (`steering_1`,
    `acceleration_1`, `brake_1`, `back_1`, `side_brake_1`, `shift_up_1`,
    `shift_down_1`, `rear_view_1`, `change_view_1`) the button icon (`ana_l_lr`,
    `batu`, `sikaku`, `sankaku`, `maru`, `r2`, `l2`, `l1`, `r1` for the GT3 layout);
    the 2P column is empty without a second pad (capture);
  - the page itself (`key/key`, flags 0x123, no mbl items) is an editor (L, from its
    layout): widgets `key_config_1` / `key_config_2`; its hidden layer holds the same
    row places for both columns, art for every input an action can take (the face
    buttons, `l1` `l2` `r1` `r2`, the D-pad's left / right `cross_lr`, each stick's
    directions `ana_l_l/r/u/d`, `ana_r_l/r/u/d` and whole axes `ana_l_lr`, `ana_r_lr`),
    `arrow_l` / `arrow_r`, a red `bad` box with the text `warning` "Button Assignment
    Conflicts!", and `default_1` / `default_2` places with a "Default" text
    (`default_item`): per player, each action steps through DualShock 2 inputs, clashes
    are flagged, and each column resets to its default. The GT FORCE pages have their
    own `gt_force1/key` / `gt_force2/key` pages;
  - 2P battle: `split_race_laps` "%d laps" (1..99), `split_handicap` "%d m"
    (−150..150 in steps of 5), a yellow marker on the two sliders (handicap centre =
    0), the `handicap_1p/2p` lines of length |value| × width / 150 (negative grows
    the 1P line, positive the 2P line; eased 15 % per frame in the original) with the
    `handi_1p/2p` car icons at their right ends.
- Defaults (new game, and the captures): 4:3, all BGM / SE on, vibration on, AT,
  ASM on, TCS on, 2P laps 2, 2P tire damage off, handicap 0 m, slow car boost LOW,
  ghost update on, steering assist type 1 (Beginner), force feedback on. Race SE /
  replay SE switch the race's SE factor between 1 and 0.
- Reimplementation: values in config.ini `[options]` (`wide_mode`, `menu_bgm`,
  `menu_se`, `race_bgm`, `race_se`, `replay_bgm`, `replay_se`, `vib1`, `vib2`,
  `gear_auto`, `assist_asm`, `assist_tcs`, `split_tire_damage`, `split_boost`,
  `ghost`, `assist_1p/2p`, `force_feedback_1p/2p`, `split_laps`,
  `split_handicap`). Menu BGM / SE apply at once; Race BGM, 1P vibration and the 2P
  lap count at the next race; the arcade car pages start on the chosen AT / MT. The
  others are stored only (no 16:9 picture mode, no GT FORCE assist, 2P boost / damage /
  handicap and the ASM / TCS defaults are not passed to the race yet). The Key
  Configuration page and its view (layer m5) show our "Controller Settings" button in
  the panel's middle instead of the DualShock 2 table (the rows, labels and 1P / 2P
  headings are not drawn): our controls go beyond a DualShock 2 (triggers, keyboard,
  wheels, several inputs per action, two race layouts), so the original's editor is
  not reproduced; Confirm or a tap opens our Settings (Controls) over the menus.
- **No BGM in the options** (C): the page build's music (0x1C4D88, run at level 0: the
  option control never sets the skip flag +0xAFC, whose setter 0x1B8128 has no caller)
  matches no prefix for any option page: id 0x23, stop. No caller passes 0x0C (option01)
  to the BGM layer (callers of 0x1D27F0, 0x1D2858, 0x22B860, 0x22B9B0 checked). The music
  list's previews are the only music; its back action `stop_music` (0x142688) stops them.

### 10.1 Favorite Music List page (`music/music`) (C unless marked)

Page flags 0x163, no items, back `option`, back action `stop_music`. Its widget `song_list`
of class `song_edit` (factory 0x16ADC0 with arg 0; `song_list` on the top page is arg 1)
is a PlayListBox (RawWindow over the widget box, 580 x 308) holding a PlayList
(0x16A6B0): a vertical RawList, no wrap, centring scroll (+0x224 = 1), its viewport the
box, built from the options' playlist (music.md §2.1, menu context +0x1080):
- `count` PlayMusicBox rows (template `label_check_box`, 580 x 24): a horizontal RawList
  (no wrap) of a RawCheckBox (`check_box`) and a RawLabel (`label_box`), both fixed at
  their template offsets (check 0, title 28); the row keeps a copy of its 16-byte entry
  (+0x2C8). Text "%s / %s" (0x169E20): title / artist of the bgm/ads.inf group-0x14
  record whose file's id code is the entry's (0x21AB08; none: the group's first record).
- then a PlayModeBox (`mode_select_box`, 580 x 32): RawListItems `button_straight`
  ("Order", 0) and `button_shuffle` ("Shuffle", 1) at their template offsets (169 / 317,
  102 x 22); the focused button is the choice (PlayModeBox::vf30 0x16A488 marks it
  selected), first focused = the shuffle switch.
- a PlayMusicBoxPop (`pop_label_check_box` 588 x 32 at x −4, `pop_check_box`,
  `pop_label_box` font *110), hidden until a song is lifted.
The arg-0 box takes the pad at construction (0x16AE80 → vf15): the first row's check mark
has the cursor and its preview starts on entry. The arg-1 view has no cursor.

**Input.** RawWindow::vf17 (0x1D65A0) turns the pad into one event a frame, in this
order: confirm 1, cancel 2, up 3, down 4, left 5, right 6. The windows holding the pad
are offered the input deepest first (0x1D6028); an event bubbles to the parents
(vf20 / vf19) until one takes it. So:
- Up / Down (PlayList, RawList::vf18 0x1D42B8 → vf27 / vf28): the cursor row, no wrap:
  SE 3 `cursor` and a scroll when it moved, SE 4 `disable` at an end. Between two song
  rows the new row takes the old row's column (check / title: PlayList::vf31 0x16ABD8,
  RawList::vf32); to or from the mode row nothing is carried.
- Left / Right: a song row's check mark / title, or Order / Shuffle; SE 3, SE 4 at an end.
- Confirm on a check mark (RawCheckBox::vf17 0x1D2DC0, event 7 → PlayMusicBox::vf18
  0x169A38): toggled, SE 7 `ok`, the row's in-list byte, preview on / off.
- Confirm on a title (event 8 → PlayList::vf18 0x16A928): the song is **lifted**: the pop
  copies the entry and takes the pad, the rows lose it (+0x244 = 0), SE 7 `ok`. While
  lifted: Confirm puts it down (event 9: SE 0xA `select`, the row gets the pad back);
  Left / Right are swallowed (events 5 / 6 return 0, no SE); Up swaps the cursor row with
  the previous one (remove 0x1D4EB0 + insert 0x1D4D88), focuses the passed row (it takes
  the lifted row's column) and then falls through to the plain move up, which lands on the
  lifted song again: song and cursor move together, SE 3, scroll; at the first song the
  move fails (SE 4). Down the same with the next row, refused (SE 4, nothing else) when
  the next row is the mode row (index + 2 ≥ row count).
- Confirm on the mode row: nothing.
- Cancel (event 2 on the PlayList): not lifted: the commit (0x16AD00) and the page's own
  back (stop_music); lifted: no commit, the page still goes back, so the visit's edit is
  lost (I: the page manager's cancel is not gated by the widget; 0x16B128 "lifted?" has
  no caller).
- Commit: entry i = row i's 16 bytes for i < count (the page's order and marks; played /
  order as they were), then 0x21ACE0 with shuffle = (the mode box's focus index ≠ 0): a
  rebuild, so every visit ends with a new pass (cursor 0, a new order from the seed).

**Preview.** PlayMusicBox::vf21 (0x169D98, on any change of a row's pad): unless the pop is
shown, a listed song plays 0x22B860 (play if different) of BGM 0x14 with index | 0x1000
(the intro file `bgm/i_race_aNN.ads`), play flag 1 (loops), level 1.0, straight to the BGM
layer (no Menu BGM gate); an unlisted one stops the BGM (0x22BA18). A move runs it for the
old row and then the new one, so the new row decides (an intro request always restarts).
The mode row stops the BGM when it gets the pad (PlayModeBox::vf21 0x16A450). The pop's
vf21 (0x16A0E0) only sets its highlight: a lifted song goes on playing; putting it down
restarts it.

**Scroll** (0x1D4848, after each accepted move; the lifted swap scrolls through its plain
move). With s = how far the list is scrolled, V the window (308), H the list (24 × count +
32), the row's top t and height h: above = t − s, below = V − (t − s + h); moving up
above −= (V − h) / 2, moving down below −= (V − h) / 2; delta = min(0, below) −
min(0, above) clamped to [−max(0, H − s − V), max(0, s)]; the new s = s − delta. So the row
is centred in the direction of the move, never past the list's ends. RawAnimScroll
(0x1D2BB8 / 0x1D2A98): step = (target − current) / 4 a frame, snapped on arrival (4
frames), stepped before the frame's input.

**Looks.** The row with the pad: `label_check_focus` (fill 0x333333) × (1 + p) instead of
`label_check_box` (0x181818); p (+0x2D8) is 0 while the row has no pad, else −1/60 a frame
(f32 0x34FC30) and back to 0.5 below 0 (a new row starts at 0.5). Its focused child:
`check_on_focus` / `check_off_focus` (RawCheckBox looks +0x20C..+0x218 indexed focus × 4 +
checked × 8: off, off_focus, on, on_focus) or `label_focus` (green fill, black text), × the
same factor (?: the check box has no focus element name (+0x160 = 0), so 0x169B50 multiplies
its colours each frame without resetting them, i.e. they compound until the next look
change; how values above 255 pack was not traced; ours pulses it like the title). The mode
row with the pad: `label_check_focus` × (1 + p) (0x16A510); the chosen button its `_focus`
look (yellow), the other the plain one. The pop: its base colours × (2p + 0.5)
(0x16A0E8), centred on the cursor row (PlayList::vf10 0x16A8D8). The rows are clipped to
the box.

Reimplementation: `menu/flow/music_list_page.*` (`MusicListPage`: keys from
`MenuController::fired` / `pressed`, one a frame in the order above); `GameFlow` opens it
on the page's PageChanged (the view on `option`), commits on the back action
`stop_music` into `[options] music_playlist` (a missing list is made and kept there), and
hands the preview to the screen (`take_preview`: `bgm::Player::play_if_different(kRace,
index | kIntro, loop, 1.0)` or stop). Our addition: the pointer (hover moves the cursor
without scrolling, tap = move / confirm, a tap carries a lifted song). Not reproduced:
the compounding check-mark colour.

## 11. Race quick menus and the name-entry grid (H unless noted)

- **Quick menus** (`menu/qm_*.mbl` + `menu/<LANG>/qm_*1.pmb`, ShortmenuControl /
  ShortmenuLicenseControl): ordinary page-manager pages. The `root` page (flags 0x1E1:
  horizontal, confirm blink 0xC0 with SE `next`, cancel allowed; championship pages 0xE9
  / 0xE1 without cancel) parks its icons above the screen (y >= 448) and the list places
  them as a horizontal row in the `menu` window (x 20, w 600): 88-px icons 4 px apart
  (92 px pitch), centred when the row fits (5 icons: 456 px), else left-aligned at the
  start and scrolling with the focus (the licence / time-trial menus' 9 icons: START at
  x 20, the 7th cut by the window). Moving plays SE `select`; the focused icon shows its
  alt look with the focus pulse; its `str80` help layer (`layer<N>`, the line under the
  band) is shown. Dialog pages (`ghost_dlg`, `exit_sure`) are popups over `root`.
  The race is drawn behind the layout where the driver passes one (C, presentation §A8c):
  the time trial's, free run's and machine test's menus and the GT race's / championship's
  pre-race menus over the whole frame (ShortmenuRender; the race in play mode 2 with records
  off: driven by the computer from its start, on and on, from the driver's camera, a GT race's
  field after its countdown; `bg` layers translucent), the licence menu only in its preview
  box (32, 58, 280 x 210); the arcade race, 2P, GT race and championship result menus get
  none, their opaque checker `bg` fills the frame. After a run the driver's own steps (prize,
  name entry, the run's replay) come before the menu (presentation §A8c).
  Pre-race `qm_gt_event`: the entry list in `rank<i>_name` / `rank<i>_time` (classes
  `qm_list_car_name<i>` / `qm_list_time<i>`, sw_16#1; the times "no time",
  `\x7F\x7F'\x7F\x7F.\x7F\x7F\x7F` in the font's long dash: presentation changelog
  2026-10-29) and `laps` ("3 Laps"); after a FREE RUN (free practice) the list in the new
  grid order with each car's best lap of it (gt_mode.md §8.7). `qm_gt_event_champ` lists the
  grid with the points (no time column), re-ordered the same way after a free practice.
- **When** (C, the race drivers' loops around the quick-menu runner fn_0012E2C8 /
  fn_0012E480): the arcade race (fn_0012F840) and 2P battle (fn_00133F48) start with the
  action preset to Start, so the race runs first and the menu (`qm_arc_result`,
  `qm_arc_2p_result`, its descriptor set only after the first run) follows; the time
  trial (fn_00130A50, `qm_arc_tt_ltd`), free run (fn_0012FF48, `qm_arc_tt_free`),
  machine test (fn_00131690, `qm_gt_mt_*`), licence (fn_00132030, `qm_gt_license`) and
  GT race (fn_00132BA0, `qm_gt_event` then `qm_gt_result`) open with the menu, and the
  same menu returns after every run. The menu's result is the item's action
  (qm_start / qm_try_again, qm_replay, qm_save_*, ...; the strings fn_0012D8C0 registers).
- **Triangle / Square = EXIT** (C): on a page with the cancel flag 0x100 (every `root`
  but the championship ones, 0xE1 / 0xE9; the popups `ghost_dlg` 0xC1 and `exit_sure`
  0xE1 have none) cancel goes to the page manager's cancel (`fn_001B7C08`, flag test
  0x1B7DC8), which finds no back page, plays SE `return` and ends the menu
  (`fn_001B6F78` -> `fn_001B79B8`). The runners preset the result to 1 (0x12E328 in
  fn_0012E2C8, 0x12E4E0 in fn_0012E480), the value `qm_exit` writes (fn_0012CB80), and
  every driver's action 1 is Exit: the run's EXIT at once, no confirm blink, no question.
  Ours: `QuickMenu::tick` reports the page's `qm_exit` item (test
  `race_quick_menu_cancel`); it had rebuilt the root page (the menu faded out and in).
  Championship "Save game" (fn_00133388) runs the memory-card save (operation 0x19)
  between two championship-information calls.
- **Item availability** (C, 2026-10-21). The page manager installs per-page enable hooks from
  a sorted table {page name, hooks, count} (+0xB00..+0xB08, `fn_001B8018`, `fn_001C91B0`: hook
  i for button i, the last for the rest); the quick-menu controls (`fn_0012C5C8`, the licence's
  `fn_001349A8`) register {"root", 0x314848, 18}: every root button gets `fn_0012C430`, dialog
  pages none. It reads the runner's result block (a1 of `fn_0012E2C8` / `fn_0012E480`, global
  0x3514F0) by item name: `replay`, `save_replay` need +0x20; `save_ghost` +0x1C;
  `data_logger` +0x18; `setting`, `save_setting`, `load_setting` are off when +0x8 is set;
  `exit`, `save_game` off when +0x4; `free_session` off when +0xC; every other item on. A
  disabled item stays focusable and its confirm plays SE 4 `disable` (`fn_001CA150`), nothing
  else. The drivers zero +0x4..+0x20 when they start (one driver call = the visit) and set:
  - +0x20 = 1 after every live run (`vf42(race, 0, 1)` returns): time trial 0x130FF0, free run
    0x1306A8, machine test 0x131C90, licence 0x132574, arcade race 0x12FC90, GT race 0x132F6C,
    championship 0x133810, 2P 0x134410; cleared by the time trial's and licence's
    DEMONSTRATION (0x1311C0, 0x132984), a licence run that did not end (0x132648, presentation
    §A8: the car's +0x1464 clear; no byte store to it was found, so whether a FAIL sets it is
    open [?]; ours counts a FAIL as ended, as for the preview box), and a confirmed SETTING page in the free run / machine test (0x130880, 0x131E04:
    with +0x18 / +0x1C; when `fn_0015CD60` returns non-zero [?: on a change or on any OK, not
    traced]; not reproduced).
  - +0x18 and +0x1C = 1 when a run's result block has its record flag R+0x4C (time trial
    0x131060, free run 0x130708, machine test 0x131D20, licence 0x1325C4): the session record
    check (presentation §B3: a time at or under the session's best; licence passes with or
    without a medal); the free run's lap handler sets it itself for a lap strictly under the
    recorded best, car 0, not a replay, when its recorder checks pass (`fn_00210690`, a count
    under 36000 = 10 min of frames [I: the run's recording]) (0x1F17F0; the recorder checks
    not reproduced). Never cleared but by SETTING.
  - +0x8 = 1: the time trial always (0x130C0C); the arcade race and free run when the race
    setup's car is not a garage car (GameStatus +0x307C8 +0xD0 == 0: the car is built from
    the request's code, 0x12F9C0 / 0x130160); others 0.
  - +0xC = 1: the GT race on a dirt course (course table +0x08 = 2, `fn_002389A0`, 0x132D54);
    the championship never.
  - +0x4 = 1 in the championship after a run when its info's +0x20 >= +0x24 (0x133910..
    0x13392C); NEXT then ends the championship (0x133958) [I: the last round run].
  So before the first run REPLAY, SAVE GHOST and DATA LOGGER are disabled in the time trial,
  free run, machine test and licence menus; the arcade race's, 2P's and the GT result menus
  only come after runs. Ours: `race::QuickMenuItems` (`visit`, `after_run`,
  `after_demonstration`), `QuickMenu::set_items` (the controller's enable hook for "root"),
  `RaceScreen::update_quick_menu_items`; the drawing is the page manager's (§5).
- **Name entry** (`menu/name_entry.mbl`): the seven rows are FreeButtons (kind 0x34:
  nested horizontal wrapping list). There is **one key cursor**: only the focused row
  shows its key in the highlight look (pulsing like a focused button); the other rows
  show plain keys. Up / Down keep the column: after a Left / Right, or coming from a row
  with at least as many keys, the key under the previous key's centre (keys 24 wide,
  28 apart; the one-key OK row's button centre is 223 px in, giving column 8); else the
  row keeps its last key (`FreeButton::vf14`, globals 0x352648..0x352650).
  The text box (TextBox) draws its widget gradient and the hidden red `cursor` bar (3 x 16)
  with the layout's corner colours, which run from the bottom-left (formats/menu.md): the box
  is lighter at the top, the caret brightest at its bottom, which sits on the box's bottom
  edge (reference capture).
  The caret stands at the text box's cursor (after the text before it): keys insert there,
  Back Space deletes before it, L1 / R1 move it, Start focuses OK (gt_mode.md §17, C).

## 8. Open points

- The meaning of the item id (+0x140, read only by control code) and of the
  frame-buffer clear globals (page flags 0x30).
- Map (MapButtonList) and garage lists use their own navigation (and SE
  `cursor`); the reimplementation uses the plain list.
- The title selector's exact layout (only the window and item order are
  known), the arcade carousel layout (TopButtonList fades buttons with their
  distance from the centre).
- Mapping of the game's BGM indices to music.inf names (by meaning only).
