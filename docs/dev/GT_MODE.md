# GT Mode (milestone M4) — implementation notes

The career mode: credits, garage, dealers, tune shop, wheels, car wash and
oil, licenses, races and championships, prize money and prize cars, car
settings, records, saves; plus the arcade-mode progression that the
original keeps in the same game state.
The rules and save data are described in
[docs/behaviour/gt_mode.md](../behaviour/gt_mode.md) ("gt_mode §n" below names
its sections); this page describes the code.

## Layout

| File | Role |
|---|---|
| `src/game/gcf.*` | `gameconf/product.gcf` reader (constants such as the 18,000 Cr start, race-code list, prize sets) |
| `src/game/gt_catalog.*` | what GT mode offers, read from `menu/gt_mode.mbl`: dealer cars, race series (single / championship), license tests, tune-shop page → item |
| `src/game/gt_parts.*` | tune-shop goods ids (0..86 → table, stage), the shop menu items, part lookup and prices |
| `src/game/gt_state.h` | `GameState` / `GarageCar` / `CarCondition` / `ArcadeProgress`: plain data |
| `src/game/gt_setting.*` | car settings menu rules: slider ranges / values / texts, locks, setting files |
| `src/game/gt_mode.*` | `GtData` (disc data, loaded once) and `GtMode` (all rules; deterministic) incl. the bridge to `sim::CarSpec` |
| `src/game/language_rules.*` | what the game's language decides: prize-car labels, Opel / Vauxhall dealer buttons, the licence car and S-1's `_uk` demonstration, which paint name shows (car names: `data::car_name_fields`) |
| `src/game/gt_save.*` | native save (text, versioned) and import/export of the original PS2 GAMEDATA file |
| `src/game/save_library.*` | the save library: any number of native saves in `<user data>/saves/`, the current one, migration of older saves |
| `src/menu/flow/save_page.*` | the save page (title Load / Save / File Operation, home Save, Trade, Load Garage, Load / Save Settings) on the original's file list art |
| `src/game/gt_mode_menu.h`, `src/game/gt_mode_menu/` | binds GT mode to the interactive menus through `menu::flow::GtModeHooks` (files: "Menu integration" below) |
| `src/menu/flow/gt_mode_hooks.*` | the hook interface (menu side; no dependency on `src/game`) |
| `src/app/screen_gtmode.cpp` | installs the hooks; menu 3D renders of cars and wheels (`MenuModelRenderer`); `gt3r --screen gtmode` = save overview |
| `src/app/screen_gtmode_setting.cpp` | the car settings menu and records screens (`make_gt_setting_screen`, `make_records_screen`) |
| `tests/test_gtmode*.cpp` | logic tests and the menus on a small invented world (`test_gtmode_menu_synth.cpp`, data in `tests/gtmode_synth.h`), both without the disc; disc tests |

## Rules (`GtMode`)

- **New game**: credits = gcf `initial` (18,000 on this disc), day 1, empty
  garage, no ride car.
- **Money**: every sum read from the disc (car and part prices, prizes and bonuses, the gcf
  tag-10 sums) goes through `GtData::money` (×100 in Japanese, `money_scale`); there are no
  built-in amounts, and `GtData::load` refuses a game config that lacks one of the tag-10
  values. The prize pages' texts are `credits_text` / `prize_remain_text`, the latter with
  `GtData::prize_prefix` (`text/realtime.dat` in the game's language, else the game's
  built-in word). Mod car prices are stored like disc prices (docs/MODDING.md).
- **Cars**: `buy_car` (checks not-for-sale flag, money, 200-car garage;
  newest first; the first car becomes the ride car), `sell_car` (price / 4,
  refused for flag bit 1), `ride`, `add_prize_car`. A new car gets the
  stock parts, stock settings (`sim::stock_settings`), the stock goods
  owned (front tires also own the rear set; rally cars own dirt tires) and
  the break-in condition (97 % power).
- **Tune shop** (ride car): `part_offer`, `can_buy_part` (owned → already,
  no record → not exist, money), `buy_part`, `fit_part` (owned goods only;
  stage 0 = stock; turbo ↔ NA tune exclusive; front tires fit the rear set;
  the part's settings group is re-initialised), `buy_all_tires`,
  `power_with` (before/after PS).
- **Wheels / wash / oil**: inventory of bought wheels (500 Cr each, never
  used up), `fit_wheel` (the garage's change: inventory wheels or stock),
  `fit_shop_wheel` (the shop's "fit it?": the focused wheel, held or not), `wash` (50),
  `change_oil` (250). `engine_factor()`
  is the condition's power multiplier (physics §10.3) the menus' power figures use.
- **Engine condition through a run** (gt_mode §13): GT races and championships,
  machine tests, free runs and arcade races carry the car's condition
  (`carries_engine_condition`); the request gets `run_condition()` (odometer in 2 m
  units, E, O; an arcade car: zeros), the race car advances the words tick by tick
  with the factor applied live, and `end_run()` puts the result's words back
  into the garage car and adds the odometer units to the total distance
  (`gt_menu::fill_condition` / `apply_condition`; arcade results count their
  distance too, finished or not). Licence tests, time trials and 2 Player
  Battle run with a fresh engine and hand nothing back, also with a garage car.
  `driven()` advances the words in one piece (tests, `GT3R_GTMODE_FAKE_RACE`).
- **Licenses**: `judge` (time vs. the event's gold/silver/bronze),
  `record_license` (best medal kept; license held when 8 medals; all gold →
  the license prize car once), `can_open_license_page` (B → A → IB → IA → S;
  B and rally always open), `can_take_test` (8th test needs 1–7).
- **Races**: `rules(code)` decodes the event record fields GT mode needs;
  `check_entry` reproduces reguration result (dirt tires, license,
  drivetrain, regulation list, NA/turbo, car class/tuned); `can_enter` maps
  it to the license / regulation popups. `record_race` pays the place prize
  (every time), keeps the best place, counts races/wins and awards prize
  cars. `start_championship` + `record_round` run a championship: field
  drawn once, points 10-6-4-3-2-1, champion only with strictly more points,
  bonus and prize car on every championship win. `enter()` = one day.
- **Prize cars**: `award_prizes(code)` scans the gcf prize sets (series all
  won, championship, endurance, license all gold, league complete,
  completion %); single-car sets once, multi-car sets on every win, picked
  by weight with the state's generator. The whole game's set
  (`gtmode_complete`, gt_mode §23) is eligible when the checked code is a
  GT-mode one (`gt_mode_code`: series, championship, endurance, rally,
  licence, machine test; not arcade) and every gcf section-4 result is won
  (`all_races_won`: 364 on this disc); it is not the 100 % completion
  rule (`achieve_complete`). Prize sets are checked after a race win, a
  championship title and a licence turning all gold, as in the game.
- **Language**: `GtData::prize_candidate` puts each candidate's label through
  `prize_car_label` before the paints and the pick (a swapped one gets a random paint), also for
  the car_get candidate list; the cd/ region pages leave out the `opel` / `vauxhall` button
  (`gt_menu::listed` → `MenuController::listed`); licence tests drive `license_car` (race launch,
  the quick race, the GT-mode request, the theater) and load `demo_replay_path`'s run.
  `Car::name` / `name_alt` and `GtData::color_name` are in the language (game text: kana in
  Japanese, drawn by the game fonts). `Car::latin_name` / `color_name_latin` are game text
  too: the race screens draw them with the game fonts (`race::result_car_name`), the app's
  own screens convert them to UTF-8 or cut them to ASCII, and online descriptors cut them to
  ASCII. A mod's texts go in as game text (`data::add_mod_car`).
- **Bridge**: `car_spec(GarageCar)` = `sim::build_car_spec(db, car, parts,
  settings)`; the engine condition is not in the spec (the race car carries it,
  above). `power_peak()` / `power_ps()` are the original's computed menu figure
  (gt_mode §5 "Power figures": `game::spec_power_peak(spec, engine_factor)`, the
  100-rpm sweep of the built curve, each looked-up torque × the condition
  factor); `power_text()` prints it the menus'
  way (`display_power`: truncated, HP = PS × 0.98619 on NTSC-U data).
  `completion_text()` / `completion_percent()` / `completion_file_text()`
  are the status page's and the file lists' truncated texts. Settings screens that move the gear
  slider must call `sim::apply_gear_slider` (computed gearboxes); the
  drivetrain-split word is stored but ignored, as in the original.

Randomness follows the original (gt_mode §20; `base/game_random.h`): one
generator (x = 17x + 17, output x ^ rotl16(x)) on several states. The
trophy pick, prize-car pick and the paint of prize cars without a fixed
colour use the GT-mode state `GameState::rng` (saved, kept by a new game;
`pick_trophy_index`, `pick_weighted`, `prize_paint_index`); `record_round`
picks the trophy before the prize cars (`Award::trophy`), as trophy_get
comes before car_get. Each presentation opens with the original's "stir"
(1..12 steps from a hardware random value + the shared state, gt_mode §20.2; ours
takes that value from the clock, `hardware_random()`), and `on_frame` steps the state once per
menu frame like the original's menu loops, so the rolls are unpredictable
the same way. Opponents are drawn by `race::draw_opponents` on the race
module's state (`race_info_random()`), shared with the paint colours of the
race; arcade Single Race draws its field the same way (`screen_race_launch.cpp`).
Given the same states, every draw equals the original's (verified against
the original's behaviour, 0 mismatches).

## Menu integration (`src/game/gt_mode_menu/`)

The bindings are the adapter between the generic menu runtime (`menu/flow/`, which
does not depend on `src/game`) and the rules (`GtMode`). They stay on the game side:
the public header `game/gt_mode_menu.h` is also the session's API (`GtModeSession`,
the save library, the race screens' saves), used by `app/` and `game/` itself, and
`game/` already builds on `menu/flow/`. One file per concern, sharing the private
`internal.h`:

| File | What lives there |
|---|---|
| `internal.h` | Private: `MenuState` (everything the module keeps between hook calls: disc data, session, queued presentations, sounds / popup / movie for the flow, map pointer, wheel-shop pin, save library, snapshot, attract cycle; `menu_state()`), `Show`, the presentation timeline constants, the list geometry; declarations grouped by file |
| `install.cpp` | `install_gt_mode_menu()`: wires each hook to its file; `attach` |
| `common.cpp` | `menu_state()`, `reset_gt_mode_menu_for_tests()` (a fresh `MenuState`), shared helpers (names, prices, settings working copy, autosave, layout lookups) |
| `session.cpp` | `GtModeSession`, `start_session` (the one place a session is created: `attach`, `ensure_gt_mode_session`), save library, snapshot, the race screens' direct saves, `begin_setting` / `begin_records` |
| `actions.cpp` | `on_event`: the GT-mode control's actions |
| `prechecks.cpp` | `intercept` (pre-check popups), `enabled` |
| `races.cpp` | `prepare_request`, `on_result`, `next_request`, `resume_request`, attract demos |
| `presentations.cpp` | prize pages, license card, trophy, prize car, name entry: queueing, timelines, the prize counter's page state (its step: `game/prize_count_up.h`), `presentation_model` |
| `widget_text.cpp` | `widget_text` |
| `widget_art.cpp` | image widgets from hidden art (`widget_draws`), `hide_art`, `car_for_widget`, the oil change's and the car wash's timelines (`oil_cylinder_at`, `wash_spin_at`, `car_wash_spin`) |
| `lists.cpp` | `on_tick` (every frame, busy or not: the car wash and the oil change), `on_frame` (per-frame input and timelines; a queued effect plays on a frame the page plays none and never holds the page's code), map pointer, garage / permitted-car / wheel-shop lists, tap and hover, `permitted_car_names` |
| `extra_draws.cpp` | `extra_draws`, one helper per page element it adds |
| `arcade.cpp` | the arcade hooks (pickers, refusals, entry art, garage-class races, arcade results) |
| `save_backend.cpp` | the save page's files and operations (`save_list` / `save_run`) |

`install_gt_mode_menu()` registers `GtModeHooks`; `GameFlow` calls them for
the `gt_mode`, `setting` and `records` controls and the presentations
(prize car, license, name entry); the arcade control uses only the arcade
hooks, `intercept` / `enabled`, and the widget hooks on `ar/goodies`
(Phase 7). The options flow is untouched.

- `attach`: loads `GtData` from the disc and the current save of the save
  library (new game if none) when the title menu opens — the original's
  boot-time memory-card load.
- `on_event`: the GT-mode control's action table — `set_car_code`, `buy_car`,
  `ride_car`, `sell_car`, `set_garage_car`, `set_focus_parts`,
  `buy_parts`, `fit_parts`, `buy_fit_parts`, `buy_all_tires`,
  `set_focus_wheel_code`, `buy_wheel`, `fit_wheel`, `change_wheel`,
  `wash_car`, `oil_change`, `set_race_code*`; Finish actions `go_race`,
  `go_license`, `go_machine_test`, `go_free_run` cost a day (`go_race` on a
  championship item starts the championship); `go_save_in_home` /
  `go_save_game_menu`, `go_load_game_menu`, `go_file_operation`, `go_trade`
  and the arcade `load_guest_garage` open the save page ("Saves" below).
- `intercept`: the pre-check popups (`00cmn/no_money`, `not_for_buy`,
  `max_car`, `not_for_sell`, `ride_car`, `not_for_change_wheel`,
  `ts/already_you_have`, `ts/not_exist`, `already_you_have_all_tires`,
  `hm/need_dirt_tire`, `license_violation`, `regulation_violation`,
  `lc/no_license_*`, `lc/no_last_exam`, `cd_ride_sure`). This needed a
  small `MenuController::intercept` hook.
- `enabled`: the F1 series needs the S license.
- `widget_text`: code-filled text widgets by class — status bar
  (`user_money`, `user_day`, `user_day_unit`, `user_carname`), dealer
  (`goods_car_price`, `car_buy_price`, `focused_carname`), garage
  (`car_sell_price`, `car_max_power`, `car_kilometrage`, `car_drive_type`),
  tune shop (`price_<item>`, `disp_ps_befor/after`; on the engine parts'
  "Installed successfully" pages `car_previous_torque` / `car_current_torque`,
  which despite their names show the power before the part, kept by
  `fit_parts` / `buy_fit_parts`, and now, "%d %s"), wheel shop / wash / oil
  prices, licenses (`disp_time_gold/silver/bronze`, `license_car_name`,
  `license_launch_speed`), race info (`race_prize_1..6`, `regulation_*`),
  status page (`status_*`), spec pages (`spec_*`). This needed a text
  override in `menu::ElementDraw` (renderer).
- `extra_draws` / `on_frame`: the code-drawn garage list (`hm/hm_gr`,
  Up/Down), the race information's Cars Permitted list (popup
  `00cmn/car_reguration`: `permitted_car_names`, each car of the event's
  regulation as name + second name, a string once at its last place, rows of
  the `link_menu` template in `menu_regulated_car`, Up/Down; "No Limitations"
  without a list), the records page's places (`records_free_tt`, Up/Down); these
  three are GT3's vertical button list (`ListRows`, `menu::flow::column_layout`:
  rows 4 apart, the cursor row centred, the offset eased in `MenuState::list_off`, the
  scroll bar by `list_scroll_bar`), the status bar day number (hidden-layer widget), and the wheel
  inventory cursor. Dealer pages (maker and car lists) are tile strips in the
  controller's scrolling window (`menu_car_goods`, and `menu_dealer` on the England and
  Germany pages, are among its window widgets; items parked off the screen vertically,
  as on the Toyota list, count as parked).
- **Car wash / oil change progress popups** (gt_mode §13): the shop buttons open
  "Washing . . ." / "Changing oil . . ." popups whose `ignore_control` widget takes the
  controls away when the page is built (`MenuController::ignoring_control`: no key or tap
  does anything and the popup's "Complete" button is not drawn) until game code gives them
  back (`give_control`). `GtModeHooks::on_tick` runs every frame, also during the confirm
  feedback, and steps both: the oil gauge's drain and refill (~325 frames) and the car wash
  (`wash_spin_at`: its effect after 31 frames, then 181 frames in which the car's turntable
  gets an extra `kWashSpinPush` of yaw acceleration a frame, about 15 fast turns; the car
  keeps its old dirt until the last push). The app's showroom applies each push once
  (`car_wash_spin`: a serial per wash and the pushes so far), as its real-time steps allow.
  Before this the "Complete" button could be chosen at once, and leaving the page ended the
  oil change before its refill.
- Popups: one level. A popup item confirmed inside a popup replaces it
  (`MenuController::build`), so the tune shop's Purchase? → Install parts
  now? → Installed (and the trade's Buy? → Bought) close back to the page
  they were opened from. A part is bought once per car (`can_buy_part`:
  `ts/already_you_have`); owned parts are swapped in the settings' Change
  Parts pages.
- Focus (menus §4.2): the GT-mode control keeps the stored race code, so a race series
  or licence page opens on the race entered or just driven (`race_page_focus`); other
  pages open on the item leading back to where you came from, else the first (nothing
  remembers the item confirmed last).
- `prepare_request` → `GameRequest`: `event`, `race_code`, the garage car
  as `car_spec` (+ `car`, `car_model`, `car_color`) and `opponents`
  (championship field or a fresh draw). License tests drive the event's
  entry-0 car; free run sets `course`.
- `on_result` ← `GameResult`: the engine condition back into the garage car,
  prize money / championship round / license medal, prize cars, then an autosave. `next_request` chains the
  next championship round without reopening the menu.

Phase 2 additions:

- **Status bar**: the day counter is drawn as one string ("12 days") in the
  import page's hidden `user_day` widget, widened to end before the Cr icon;
  the layout's `user_day_unit` widget is left empty.
- **Paint picker**: on a dealer car page Up/Down choose the paint (the
  page's buttons move with Left/Right); swatches are drawn in the
  `car_color_box` row from `race::CarColors` (carcolor.db), the name in
  `color_name_box` from carcolor.sdb (definition word +4 = name index, M).
  The index is reset when the focus car changes and used by `buy_car`.
- **Garage sorting**: START on `hm/hm_gr` opens `hm/hm_gr_panel`
  (`MenuController::popup`); `sort_*` picks the key; `GtMode::garage_order`
  sorts (date newest first, name, maker, colour, price, distance, power,
  drive, aspiration), the header shows the key's hidden label and each row
  its value in the hidden `info` column.
- **Image widgets** (no parts of their own; drawn from the page's hidden
  layer into the widget's box): license cups `disp_license_cup_<lic><n>` →
  `cup_gold/silver/bronze`; league pages `event_status<N>` → the result row of
  the item behind focus layer `s_<N>` (`frame` / `frame_arrow` slots with their
  `mark_<best place>`, a championship's own slot first), `need_license<N>` →
  `lc_mark_<license>`; series pages `event_status_buttonNN` → button NN's
  `mark_<best place>` and course logo at their layout places from the button's
  corner (gt_mode §22.1); `car_logo` → the hidden element named after the car.
- **3D cars**: `car_render_*` widgets (dealer, garage, shops) and the
  prize presentation's `render_model` are drawn by the app
  (`screen_gtmode.cpp`) through `GtModeWidgetDrawer`, called by the menu
  renderer's new overlay hook after the widget's own art. The car turns on
  a turntable; because the model shader's alpha is not coverage, it is
  rendered over black (B) and over white (W) and composited exactly as
  page × (W − B) + B with SDL blend modes.
- **Presentations**: a result that obtains a license or awards prize cars
  queues `lc_get/license_get` (layers `<lic>_mess0/1`) and
  `car_get/car_get` (reason line = the page's hidden text element for the
  prize set: "Event Winner", "Championship winner", "B License all gold",
  ...). `GameFlow` shows them (`GtModeHooks::presentation`) before the next
  championship round or the menu.
- **Prize car page** (gt_mode §10.1): the roll stays at the result (`award_prizes`; its rolls,
  `GtMode::last_prize_rolls`, carry each candidate's paint and the pick into the `Show`); the
  page's `CarGetRoulette` (`src/game/car_get_roulette.*`, the program's state machine in EE
  singles: hold, spin, slowdown onto the pick, the camera's fly-in, the others' drop) is started
  by `next_presentation` and stepped by `presentation_frame`. The page takes the controls when
  built (`GtModeHooks::Presentation::take_controls` → `MenuController::take_control`; `on_frame`'s
  automatic give-back skips it, `car_get_holding`) and gets them back on the release, with the
  `after` band and the `car_get_message` ticker (its marquee restarted from then,
  `menu::flow::text_effects`; the band's 24-frame fade from then). The app draws the others'
  floor lights and cars at `CarGetRoulette::others_opacity` (the original puts the page's
  background back over them), then the lit car's light and the lit car, every car with the colour
  scale `shade` (black silhouettes, then the car won lighting up: `DrawItem::tint` and
  `reflection_scale`), through the page's camera over the full frame (`screen_gtmode.cpp`
  `draw_car_get`; the floor lights are the widget's hit-mark image as projected quads).
- **Championship resume**: a championship saved between rounds resumes
  with the next round when GT mode is entered (`resume_request`).
  GameRequests of rounds carry `champ_round`, `champ_rounds`, `champ_last`;
  `GameResult::abandoned` ends the championship, an unfinished round counts
  as a retirement.
- Dev aid: `GT3R_GTMODE_FAKE_RACE=1` wins every race at once (screenshots
  of the result path without driving; machine tests and time trials get a
  60 s time).

Phase 4: **car settings menu** (`src/game/gt_setting.*`,
`src/app/screen_gtmode_setting.cpp`, gt_mode §14). GameFlow runs the original
`setting.mbl` pages as control "setting" (`GameFlow::start("setting",
"root")`, `exited()`); the screen `gtsetting` hosts it
(`make_gt_setting_screen(event)` in `app/screen_gtmode.h`, for the race's
pre-race quick menu; `gt3r --screen gtsetting` with `GT3R_GTSETTING_EVENT`).
`begin_setting(event)` makes the working copy of the ride car; sliders
(`slider_range` / `slider_set`) edit it, OK commits it to the garage car
(saved), Back discards it; the values feed `sim::CarSettings`, the race's
parameter block (the ride car's parts and settings) and the menus' `build_car_spec`
through `GtMode::car_spec`. `GarageCar::ref_final` holds the gear reference final
(garage car settings word +0x1AC, gt_mode §3.1; saved). Value texts, locks (`disable_*` overlays, status boxes),
fitted-part marks (`attach_*`), slider markers and the part-change pages
(`set_*`, with the `not_to_change` / `dirt_tire_only` guards) are bound in
`src/game/gt_mode_menu/`. (Setting files and the gear chart followed in Phase 5.)

Also: garage rows react to taps (`GtModeHooks::tap`); race pages show the
course logo and result mark inside each race button and the course name in
the text bar; `GameRequest::racing_gearbox`; `GameResult::license_medal` /
`failure` (race-judged license tests, cross-checked by `GtMode::judge`).

Phase 5:

- **Setting files**: "Save Settings" stores the car's committed settings as
  a setting file (`GameState::setting_files`, at most 10 per car, in the
  native save); "Load Settings" applies the newest file of that car with the
  original's rule (a group is copied only if its part is still fitted;
  otherwise `load_warning`). The original's memory-card 'S' file
  (one car's saved settings, gt_mode §19) is read / written by
  `import_ps2_setting` / `export_ps2_setting`. The files are picked on the
  save page (Load / Save Settings: the current game's files, no card slots).
- **Gear chart** (`gear_chart`): rpm against road speed for every gear (the
  shift sawtooth up to the rev limit) from the working copy's car spec.
- **Widgets**: status-page license marks, the license-violation popup's
  license letter, the tune shop's maker logo, the oil gauge (a full
  cylinder in the page's hidden `clean` colouring blended into its `dirty`
  one by `GtMode::oil_age`; an oil change drains and refills it, also under
  its progress popup, with its two `oil` effects), torque before/after, spec
  treads and torque/weight ratio.
- **Machine test records** (`GameState::machine`: best 0-400 m, 0-1000 m,
  top speed, saved) with the rankings, run by run (`GtMode::record_machine_test`).
- **Endings**: after a win that completes the Beginner league the short
  ending plays once, Amateur or Professional the long one
  (`GameRequest::Kind::PlayMovie`, played by the menu screen).


Phase 6:

- **Wheel shop**: the brand pages' wheels as turning 3D models (wheel/<label>)
  in a row in the `menu_wheel_shop` window, the focused one larger; drawn by
  the app's model renderer (`ElementDraw::overlay`), which now caches cars
  and wheels and composites any number per frame. (Replaced by the page's wheel
  photographs in the presentation pass below.)
- **Records** (`make_records_screen(key)`, `gt3r --screen gtrecords`):
  rankings in `GameState::rankings` (native save), filled from license,
  machine-test and arcade time-trial results.
- **Attract mode** (GameFlow): 20 s idle on the title → up to five
  `attract_demo` races (gcf demo list) → opening movie → title; any
  interrupted demo returns to the title.

Phase 7:

- **Arcade progression** (gt_mode §16): `GtData::arcade_courses` / `arcade_classes`
  (gcf tags 8 / 14), `GameState::arcade` (`ArcadeProgress`: course level,
  12 car flags, award bits, best place per difficulty × course, time-trial
  best ranks) and the `GtMode::arcade_*` rules (unlock pass, Clear Status
  predicates, completion, endings, time-trial ranking seeded with the race
  record's three default times, `arcade_compulsory_complete`). GameFlow
  extensions: `GtModeHooks::arcade_entries` filters / orders the arcade
  pickers (open courses in the game's list order; the chosen class's open
  cars); `GameResult::course` (filled by the flow) feeds
  `on_arcade_result`; the arcade set gets the `intercept` / `enabled` hooks
  (Clear Status "Goal" → `ar/cannot_ending`, the long ending item);
  `ar/goodies` uses the widget hooks (course art, clear marks, group prize
  icons, end button); Professional = Hard confirmed with L1 + R1 held;
  `ar/select_ending` plays the ending movies. Saved natively (`arcade`,
  `abest`, `atrank` lines) and in the PS2 export/import (scalar +0x3C,
  +0x40.., +0xC4, the arcade and time-trial blocks).
- **Wheels on the car**: the garage car's fitted wheel, and in the wheel
  shop the focused wheel, are drawn on the 3D car (`car_model.h`
  `CarModelOptions::wheel_override`), and races get it via
  `GameRequest::car_wheel`.
- **Record name entry** (gt_mode §17): a run that makes a ranking opens
  `name_entry/name_entry` as a presentation. Licence runs with a medal,
  finished machine tests and finished time-trial challenges are recorded by
  the race screen right after each run (`RaceScreen::record_run` at the exit
  transition: `game::add_license_record` / `add_machine_test_record` /
  `add_time_trial_record`), which pushes `make_presentation_screen()` before
  the run's automatic replay, or before the quick menu, whose Top 3 it then
  refreshes when it closes (`top3_pending_`). `on_result` / `on_arcade_result`
  never record from a result (the original records only in its drivers); the GameResult's
  `records_done` (runs were recorded this visit) only makes the arcade
  result save when the last run did not finish. The text box is a
  `menu::flow::TextBox` (`GtModeSession::name_box`, the memory card's name
  entry `GameFlow::name_box_`): starts with the stored name (the last name
  entered), the grid's key goes in at the cursor (texts of
  `name_entry1.pmb`, 15 characters at most), Cancel is Back Space
  (`GtModeHooks::name_backspace`), L1 / R1 move the cursor (the bindings'
  page frame), the caret is drawn at the cursor (`GtModeHooks::name_cursor`);
  OK stores the name (`RankEntry::name`, `GameState::player_name`). Ranking
  entries carry the GT day and a clock stamp (`RankEntry::stamp`, seconds
  since 2000-01-01 local time; the records page's date, `record_date_text`).
- **Course time trials** (gt_mode §17): `ct:<course>` rankings (12 places) for
  the courses of the arcade TIME_ATTACK list (`GtMode::course_time_trial_index`,
  the PS2 save's slot), entered with a free run's best lap in whole ms and the
  run's car (`GtMode::record_course_time_trial`): the arcade time attack (mode
  `free_run`) and GT Mode's free run (`go_free_run`), from the race screen
  only (`add_course_time_trial_record`, the run's car). The free-run quick
  menu's Top 3 and its Records page (with the car) read them.
- **Rankings in the PS2 save**: the arcade time trials' records (their 32-byte
  block at +0x120 kept in `RankEntry::code`, the car = the challenge's), the
  44 course time-trial records (TIME_ATTACK order) and the three machine-test
  ones (`read_ranking` / `write_ranking` in `gt_save.cpp`: ms, a top speed as
  int(m/s x 1e5), names, car codes, stamps, blocks) are imported and exported;
  the machine tests' bests (`GameState::machine`) follow their rankings on
  import. Licence test records stay best-time only.

Phase 8:

- **Professional art**: on `ar/level`, while L1 and R1 are held the Hard
  button draws the hidden `pro` / `pro_a` art and the Professional text
  layer (`layer4`) replaces Hard's (GameFlow, `held_`).
- **Time-trial name entry**: an arcade time-trial challenge run that makes
  its ranking (its best lap) queues the `name_entry` presentation (key
  `at:<code>`, car = the challenge's base car), shown by the race screen
  after the run (see "Record name entry"); the arcade result adds none.
  GameFlow shows arcade presentations (the Single Race cup) through
  `after_game()` and then reopens `ar/top`. The race screen shows a win's cup itself right
  after the race, before its replay (`game::queue_arcade_trophy`, presentation §F), and posts
  `GameResult::prize_shown` so `on_arcade_result` does not queue it again; GT races and
  championship rounds likewise (`game::queue_race_prize`).
- **Arcade class refusals** (`GtModeHooks::arcade_disabled`): on
  `ar/select_class` the road classes on a dirt course (database course
  surface 2) show `disable_road_doc`, the dirt class on a road course
  `disable_dirt_doc`, the home garage with no car and the guest garage
  (never loaded) `disable_no_car_doc`, each with the `disabled` element;
  Confirm plays "disable" and stays.
- **Status completion** now counts the arcade terms: (GT races won +
  Clear Status count + time trials cleared + license tests passed) /
  (364 + 102 + 10 + 48), which also drives the `achieve_*` prize cars.

Phase 8 follow-up, prize models (gt_mode §18):

- **License card** (`license_get`) and **championship trophy** (`trophy_get`,
  new: queued after every championship win, before the prize car; trophy =
  `GtMode::pick_trophy()`, weighted random, flag saved as
  `GameState::trophy_flags`) are drawn by `render::PrizeView` (shared with
  the race's medals) inside the menu model renderer's black/white
  compositing, full page, with the file's camera, lights and animation; the
  trophy 160 px left of centre. `presentation_model(page, &serial, &fade)`
  gives the model, its restart serial and fade-in. The presentations follow
  the original timelines (message layers, fade-in, OK gated by `enabled`;
  the trophy's credits count-up and bonus box).
- **Sphere maps on the GPU** (`DrawItem::sphere_map`, shader mode 4 in
  `model.frag`): textured kEnvironmentMap primitives of .prm models sample
  their own texture from the view-space normal; additive layers get full
  weight (GS ALPHA 0x58 = Cs × Ad + Cd with Ad = 1 on these models). Cars and
  courses are unchanged.
- Dev aids: `GT3R_GTMODE_PRIZE_FRAME=<n>` starts presentations n frames in;
  `GT3R_GTMODE_FAKE_TROPHY=<NN>` shows trophy NN after any race win; with
  `GT3R_GTMODE_FAKE_RACE=1` license tests are won with gold.

Menu fidelity pass (arcade pickers and code-drawn screens; menus §9):

- `GameFlow` pickers (`Carousel`, `animate_carousel`, `draw_list`): the icon
  strip of every available entry (centred / scrolled, pulse), the sliding
  cross band and bar carrying the entry's art (spring, 680 px, slide in /
  out with fades), clamped course / car / time-trial lists with the end
  arrows hidden and "disable", the wrapping class list, crossed-out
  disabled icons. Game data per entry through `GtModeHooks::arcade_parts`
  (course area / surface badges and length, car drive badge, PS, kgm).
- Backgrounds: per-entry stills (`stream/course.ipic`) then the entry's
  movie after 6 frames of rest, the arcade top / level / car page movies;
  movie widgets are modulated by their corner colours and no longer draw
  a white quad of their own (`screen_gt3menu.cpp`).
- Car picker: the 3D car (menu model renderer, page `ar/select_car:<entry>`).
- Title list: distance fade of the neighbours (`menu_top`).
- `ar/top` / `ar/level`: `diagonal_in` / `diagonal_out` logo cross-fade.
- GT Mode map: the pointer (`GtModeHooks::map_input`; direction choice by
  cos² / distance, 30 % easing, focus on arrival) and the `name_<id>` label.

Compared with captures of the original (not in the repository), then added:
miles / HP / ft-lb (`GtData::imperial`), the breadcrumb icons, the game's
class order (Single Race without the guest garage) and the dirt / road
focus rule, the car page's gauges (`MenuRenderer` arc primitive), colour
column with Up / Down paints, colour name, spec ticker, the showroom camera
(`MenuModelRenderer::showroom`) and the ride-height pose (also the dealer /
garage turntables and the asset car viewer), the over-bright focused strip
icon, and `sw_16` as the default menu font (style without a font name).

Then (second comparison round): the showroom lights of `showroom_*.fbi` (global
ambient + key + two fills in eye space, `DrawOptions::fill_*`; the
reflection env is now the real map, see the end of this file) with GT3 material shading, the mirrored floor reflection
(added at 0.65 x fade), the vertical aspect fix, the car page's AUTOMATIC /
MANUAL and RACING / DRIFT steps (`GameRequest::transmission`, `tire_mode`;
the page springs to the button row), the dealer showroom camera (whole
page; the car_render widgets' own image is the env map, not art),
vertical dealer paint swatches, the status bar's "day %d" template, miles
on the status page and garage, the garage MAKE column and ride-car mark,
horizontal selectors (league / series pages) laid out as a row with the
yellow visible-share bar, and the map without the name labels. The
course picker slide was checked against the original by capturing frames
right after a press: the art is mid-slide (and fading) with the cross bar
displaced, as implemented.

Third round: the showroom cameras now take showroom_*.fbi's values
exactly (`ShowroomCamera`: single = look-at 0.25 -> 0.5 m, 10 degrees, yaw
190 with -10 / 0.5, distance 2 -> 6 m, 53.13 degrees; dealer = look-at 0 ->
0.3 m, 7 degrees, yaw 270 with -100 / 6, distance 4 -> 12 m, 25 degrees),
with the game's yaw used directly (180 = in front: head-on at ~2 s, side-on
at ~6 s, matching the timed captures); the logo pair's corner-diagonal fade
(`ElementDraw::corner_alpha`); the colour column easing (30 % per frame,
clip bands); the 2-D blur behind the car page steps (3 x 3 offset copies of
the background movie, radius 0 -> 2); the step buttons over the car; the 2
Player car page's player-1 half (gauges, colour column, badges, small 3D
car); no AREA badge outside Single Race.

Fourth round: the 2 Player car page as the original runs it (menus §9.6):
no separate class page; on `ar/select_car_2p` each
player, on their own pad (`GameFlow::set_player2_keys`, fed from
`InputManager::pad(1)`), goes class row (7 classes, wrapping) -> car and
paint -> AUTOMATIC / MANUAL -> RACING / DRIFT -> ready, simultaneously; both
ready starts the race (`GameRequest::car2`, `car2_class`, `car2_color`,
`transmission2`, `tire_mode2`); a class-row Cancel by either player returns
to the course. Layout per player (P2 elements `_2p`, logos and name plates at
0.75 scale, P2 mirrored), the class rows / car strips at y 364 / 403, and the
two 3D cars with `showroom_split.fbi`'s camera (look-at x -5 -> -0.5 m, fixed
yaw 160 / 200, distance 3 -> 9 m, view-space shift and 12 degree turn,
mirrored for P2). The original's quirk is kept: the RACING / DRIFT skip for
garage classes reads player 1's class. The showroom intro (camera springs,
1 s fade-in) now steps with the menu frames (exact screenshots).
`GameResult::car_dirt` (race) is copied to the ride car.

(The level page's `flash_item`, the showroom car's multi-pass anti-aliasing and the
loading delay before the car appears followed later; see below.)

Completion push, batch 1 (systematic comparison with the reference captures):
- Text formats as the NTSC-U screens: times "1'02.400", money "Cr. 14,660" on the
  dealer list and the plain number after a Cr. icon elsewhere (status bar, prices,
  status page, race prizes), "No Limitations" in the race information, "0mph"
  start speed, "0 %" win ratio before any race, "114HP/6500rpm" and "0.0miles" on
  the garage page, marquee tickers entering from the right.
- League pages: one black slot per race with its best-result mark (since replaced by GT3's
  own row layout, gt_mode §22.1).
- Every car render is a showroom with its own `menu/showroom_<kind>.fbi` camera and
  lights (single, split, compulsory, dealer, garage, wheel, wheelshop, carwash,
  oilchange, buycar), read from the disc (`MenuModelRenderer::showroom_camera`);
  the car appears after a ~25-frame load and fades in (first visible 5 frames after,
  full after 88).
- Wheel shop: wheel tiles in a centred row, the focused one light grey.
- Time trial picker: the top three records, power and weight, the 3D car.
- Prize models: the license cards' shadow node (light 0) and the multi-pass draw
  (license 6, trophy 4 passes: accumulation jitter, depth of field from the file's
  focus / aperture) (`PrizeView::Pass`).
- flash_item: confirmed it draws as a plain image (the phase is never used).

Completion push, batch 2:
- Trade (gt_mode §19): Home → Trade loads a guest garage (any save file, native or
  PS2, on the save page; originally the `gameload_g` slot page), lists its cars
  on `hm/hm_gr_trade`, shows them on `hm/hm_car_trade` and buys copies
  (`GtMode::can_buy_guest_car` / `buy_guest_car`); the arcade "Load Garage"
  loads the same guest garage and enables the guest-garage class.
- Title Save / Load, home Save, Trade and Load Garage, and Load / Save Settings
  use the save page ("Saves" below; the memory-card slot pages of this batch
  were replaced by native save files): setting files keep their write time,
  power and name.
Completion push, batch 3:
- Options menu (menus §10): values in config.ini `[options]`, the
  option pages and the top page's previews show them (lit art of the current
  value, music list, key icons, 2P laps / handicap); Menu BGM / SE apply at once,
  Race BGM, 1P vibration and 2P laps at the next race, AT / MT for the arcade car
  pages.
- Options music list (menus §10): the page `music/music` has no items, so its list is
  code driven (`menu/flow/music_list_page.*`, `MusicListPage`, ticked by `GameFlow` on
  that page with the controller's fired / pressed keys): Up / Down rows (the column
  carried), Left / Right check mark / title or Order / Shuffle, Confirm toggles a song or
  lifts it (Up / Down then carry it, Confirm puts it down), the focused listed song's intro
  as a preview (`GameFlow::take_preview`, played by the menu screen). Cancel commits into
  `[options] music_playlist` (`RacePlaylist::commit`: the race's next pass), not while a
  song is lifted. Hover / tap work on the rows. The option pages play no BGM.
- Arcade garage classes: the car pages list the home / guest garage's cars
  (`hg<i>` / `gg<i>` entries) with their own colour and wheel and the picture and
  strip icon of their car label (`GtModeHooks::arcade_image`; the original names a
  car page entry's art by the car's label for every class, menus §9,
  and the arcade layout holds art for every car); a garage car on a
  dirt course needs dirt tires; the race uses the car as tuned (both players in 2
  Player Battle: `GameRequest::car2_spec` / `car2_model` / `car2_wheel`) and Single
  Race picks the race code from its weight / power. 2 Player Battle keeps a class
  row per player with the same rules for both (menus §9: road classes crossed out
  on dirt, Dirt on road, a garage class without cars), each player's car strip
  holding that class's cars.
- File Operation (title): Copy and Edit file name (name entry page) in the file
  list; game files listed with their info.
- Showroom: the GT-mode car renders fade in with the fast spring (38 frames), the
  floor reflection is 1 − dirt of the car, the .fbi light-enable mask and each
  light's ambient term are used (the "purchased" popup: key light only, brighter
  ambient).

- Showroom: dealer car pages use `showroom_dealer.fbi` and the "purchased"
  popup `showroom_buycar.fbi` (the mapping was swapped), the popup has no
  floor mirror, and menu cars load `menu/cars/<model>` (e.g. the open Miata;
  `cars/day/` has the race hardtop).

Presentation pass (compared with the reference captures;
gt_mode §3 / gt_mode §16 / gt_mode §18, presentation §F, menus §9.7 "GT Mode screens"):
- Championship trophy: the credits and the bonus box are the race prize pages'
  prize counter (`run_counter`): "Cr. <balance>" counting up from 360 frames after the page
  opened, "Prize Cr. <left>" in the `get_mess` box (now drawn from the forced layer's own
  widgets, `MenuController` draws code-filled widgets of forced layers), mess1 and OK 15
  frames after the count. The prize line ("Prize Cr.") of the cups and the trophy fades in
  over 60 frames and out 61 frames after the count (`remain_alpha`).
- Prize counter (`game/prize_count_up.h`, `step_prize_count_up`; presentation §F): the
  original's step in single precision (the tripled step stored back as the velocity after the
  first Confirm, the velocity cap dropping the acceleration to 0), stepped on every frame of the
  page; its beep (SE "cursor") is played on the frame of its step, never on the finishing step.
  500 Cr: done 220 frames after the page's first frame, 20 beeps. Approximate: our floats round
  to nearest, the console's toward zero (the same frames up to at least 100,000 Cr; a 2,000,000
  Cr count differs in a few middle digits on some frames).
- Trophies and award bits: the trophy count rises for GT cups (places 1-3 with prize
  money), championship wins and arcade wins, not for prize cars (`note_race_prize`); the
  award bits (+0xC4) and prize-model bits (+0xC0, license cards) are kept and exported.
- Time-trial records: seeded with the original's name marks, byte 0xFF − 3 i − k for
  record k of challenge i (`GtMode::time_trial_seed_ranking`, `time_trial_seed_name`),
  shown as entry ~byte (= 3 i + k) of the program's built-in name table of 30 names
  (`GtMode::record_name`), which `GtData::load` reads from the disc's CORE.GT3
  (`Vfs::kProgramFile`, served by `Volume::read_program`; known offset in the file checked first,
  else a search; "-" when not found) — no names in the source. Older native saves (seeded
  names "-" / empty, or 0xFF / 0xFE / 0xFD in every challenge) get the challenge's marks
  on load; seeded entries show the placeholder car name.
- Status bar: "day N" in the `user_day_unit` widget's own style, ending 10 units before
  its box (matches the reference exactly).
- Dealer car page: paint column placement, shadows and the chosen-paint marker; price and
  paint name one space in. Garage car page: the 12-unit text inset. Both checked against
  the reference pixel rows.
- Wheel shop: tiles are the page's wheel photographs (and "sold out"), no 3D wheels; the
  car's fitted wheel in menus is `menu/wheel/<code>`.
- Garage wheel list (`hm/hm_wheel`, `WheelList`): a row of the page's 60 x 60 `wheel` frames
  in `menu_wheel`, the stock-wheel button (`default_wheel`) first, then the bought wheels' tiles;
  the `mark` on the wheel the car wears, where the cursor starts; the scroll bar only for a row
  longer than the window. A wheel shop brand page opens on the wheel the ride car wears
  (`GtModeHooks::initial_item`).
- Showroom cars: accumulation anti-aliasing (4 / 2 jittered passes, mirror 2 / 1); one pass
  on phones and tablets (`platform::is_mobile`), where each pass's render and composite cost
  ~15 ms (5 fps → 75 fps on a Galaxy A15).
- Menu 3D memory: `MenuModelRenderer` frees its cars, views, targets, reflection maps and prize
  model when the menus start a race (`gt_mode_widget_release`); phones cache 3 menu cars (13 on
  desktop); its size-keyed targets are freed 8 frames after their last use (the car zoom).
- Arcade car filter: the base car's region byte (`GtMode::arcade_car_region_hidden`; no
  change on the NTSC-U lists); ending items confirmed.

## What is left against the original

GT mode, its menus and the arcade progression are complete except for:

- **Memory-card pages**: intentionally not replicated (a design decision): saves are
  native files on the save page ("Saves" below), which reuses the original's file
  list art and was not matched against captures. PS2 save interop is verified
  (Save format below).
- **Presentation details**: the showroom's pass reordering while a car fades in; the
  wheel shop's focus wash is measured on captures (its tile spacing is GT3's button gap:
  56); a
  two-digit status-bar day was not captured (the 10-unit right inset is measured on
  "day 1"). The trophy page itself was not captured on the original (its timeline and
  texts follow gt_mode §18 and presentation §F). The prize counter rounds its floats to
  nearest, the console toward zero (multi-million counts show some digits on other frames).
- **Randomness** (gt_mode §20): the GT-mode state is not stepped in the arcade
  menus (ours steps it on the GT-mode, setting and records controls only) and
  the frames spent on the presentation pages step it after our rolls (ours
  rolls when the result is recorded); both only add entropy. The attract
  demo draws its whole field from the pool (slot 0 included, the followed
  car, gt_mode §15) on the race state; its slot-0 car keeps the default paint
  (ours builds the followed car's visual before the paint draw).
- **Unverified readings**: game state scalars +0x08 (an add-only counter), +0x38, the
  byte +0x70 and +0xD0.. (L); +0x2C's meaning (M).
- Outside GT mode: the Replay Theater and its Analyzer (docs/dev/RACE.md "Replay Theater"). i.LINK Battle is the online lobby (docs/dev/ONLINE.md).

## Saves

GT3 Revisited does not model the PS2's memory cards: GT-mode saves are plain
files, any number of them (`game/save_library.*`):

- **Library**: `<user data dir>/saves/*.sav` (`SDL_GetPrefPath("GT3Revisited",
  "GT3Revisited")`, or the executable's folder in portable mode, or `GT3R_DATA_DIR`). A save's
  name is its `fcomment` (the original's file name, Edit file name) or its file
  name without `.sav`; file names follow the names (sanitised, made unique with
  " 2", " 3"...). `saves/current.txt` holds the file name of the game being
  played, which autosaves into it after every race (and on shop actions) as
  before; the next launch continues it.
- **Regions**: each release has its own library, as each had its own memory
  card folder: `saves/` (NTSC-U), `saves/jp/`, `saves/eu/`
  (`SaveLibrary::user_dir(Region)`). Changing discs drops the GT-mode session
  and data (`close_gt_mode`). Versions up to 0.3 kept every disc's games in
  `saves/` (on the NTSC-U data set, which the PAL set extends: the same cars and
  wheels plus the Vauxhall variants), so the first time a PAL disc's library is
  used, `saves/eu/` starts with copies of `saves/*.sav` and its current save
  (`SaveLibrary::copy_library`; once, the originals stay). A native save records
  no region; the PS2 export does (the language word at +0xC8, the GAMEDATA
  minor version, the folder name). PS2 import / export uses the release's folder
  name, GAMEDATA minor version (7.0 NTSC-J, 7.1 NTSC-U, 7.2 PAL) and language
  word (+0xC8); see docs/COMPATIBILITY.md "Regions".
- **Migration**: when `saves/` does not exist yet, the single save of earlier
  versions (`<user data dir>/gtmode.sav`) and its second "memory card slot"
  (`gtmode_2.sav`) are copied into it (the first becomes current). The
  originals are left where they were.
- `GT3R_GTMODE_SAVE=<path>` plays that file, its folder is the library and
  nothing is remembered (tests, screenshots); `GT3R_GTMODE_NOSAVE=1` writes
  nothing.
- **Unreadable saves** (damaged, or from a newer version) are never written
  over. The list shows them as Corrupt File (Delete is the only operation).
  When the game to continue cannot be read, a new game starts in its place and
  the file is first moved aside to `<file>.unreadable-<yyyymmdd>-<hhmmss>`
  (`set_aside_save`, a warning in the log; not listed, its bytes unchanged:
  rename it back to `.sav` in a build that reads it).

The save page (`menu/flow/save_page.*`, `SavePage`) is drawn on the original's
file list art (`memorycard.mbl` page `file_operation`): the left panel lists the
saves (12 visible, rows scroll; "New Save" first where it applies), the right
panel "FROM A FILE" opens the platform file dialog (`SDL_ShowOpenFileDialog`;
the system picker on Android, content:// URIs; on macOS our own sheet,
platform/mac_dialog.h, which makes the window key again when it closes: after
SDL's the game could stay without keyboard focus, no keys and no controller,
until a click), the info panel shows the
focused save (size, date, day, credits, license, cars, Game Status %) and the
bottom bar its name ("(playing)" on the current one). There is no slot choice,
no "working" / "Complete" progress and no memory-card wording; only Overwrite (of
another save) and Delete ask once. The "working" window art is ours twice: while
a file dialog is open ("Waiting for the file window...", CANCEL: any key or a tap
gives the dialog up, FileDialogs::cancel, which closes it on macOS and drops its
late answer) and after an export ("Exported as <file>", OK). The page takes mouse
/ touch itself (SavePage::hover / tap). The original's operation menu (op1 art,
stretched to five buttons) holds:

| Page | Opened from | Operations |
|---|---|---|
| Load game | title Load | Load (the game left is saved first), Edit file name, Copy, Delete, Export to PS2; "New Save" = a new game (name entry); Import |
| Save game | title / home Save, a championship result's quick menu (Save game) | Save (current) / Overwrite (another, then it is the current save), Edit file name, Copy, Delete, Export; "New Save" = save as a new file (name entry) |
| Trade / Load Garage | home Trade, arcade Load Garage | a save loads at once as the guest garage; or any file (native or PS2) from the dialog |
| FILE OPERATION | title File Operation | Edit file name, Copy, Delete, Export; Import |
| Load / Save settings | car settings menu | the current game's setting files: Load / Overwrite, Edit file name, Copy, Delete; "New Save" saves at once |

The save being played is never deleted. Import copies a save from elsewhere
into the library (`read_any_save`: native by its magic, else any PS2 container
below; a `.psu` is also recognised by its first entry for paths without an
extension; the data file is read under any name, e.g. a save editor's `.raw`).
Export to PS2 first asks for the format in the page itself, the same on every
platform (`SavePage::kExportFormats`: a window in the operation menu's art with
"PS2 save (.psu)", "PS2 card (.ps2)", "Raw save (.raw)"): a `.psu`, a new `.ps2`
card image or the data file alone (the raw save PS2 save editors such as
GT3SaveEditor open). The backend runs the format chosen (`OpExportPsu` /
`OpExportCard` / `OpExportRaw`, `export_ps2_save_container`) whatever the file is
called; the platform dialog only picks the place, starting on "<save
name>.<ext>" (no file types; the browser downloads that name), and a name typed
without an extension gets the format's. Import recognises files by their
contents (a card image, the data file's header, a `.psu`'s first entry), so a
renamed file still imports; an Android import is named after the document's
display name (`FileDialogs::name`). `game::begin_snapshot_save(state)` +
`GameFlow::open_save_game(...)` (or `GameFlow::start_save_game()` for the page on
its own) save a given state as a new or existing save without touching the running
game: the championship result quick menu's Save game (`make_game_save_screen`,
`begin_race_game_save`: the game with the round just finished recorded) uses it.

### Save format

UTF-8 text, written atomically (`.tmp` + rename):

```
GT3R-GTMODE 1                       magic and version (newer versions are refused)
credits 18000                       s64
day 1 / races / wins / prize_money / distance (m) / next_serial / trophies / prize_cars / rng / ride
car <serial> <label> <colour> <wheel|-> <origin|->     one block per garage car, list order
 parts <27 hex u64>                 fitted part ids in data::Car::parts order (slot 26 = wheel slot, 0)
 owned <22 hex digits>              owned goods, 4 bits per digit, goods 0 first
 settings <52 ints>                 sim::CarSettings in declaration order (new fields appended)
 ref_final <x1000>                  the gear reference final (GarageCar::ref_final)
 cond <odometer m> <wear m> <oil m> <dirt>
wheel <label>                       wheel inventory
license <lic 0-5> <test 0-7> <medal -1..2> <best seconds>
race <code> <best place 0-based>
prize <prize key>                   prizes already given
champ <series> <next> <n> <codes…> <m> <entrants…> <points (m+1)…>
setfile <car> <day> <ref_final>     a setting file, followed by
 sparts <27 hex u64> / ssettings <52 ints> / sinfo <write time> <power PS> <name>
mtest 400|1000|max <value> <car>    best machine-test results
rank <key> <value> <car> <day> <name> <stamp> <code>   records rankings (lic:, mt:, at:, ct:)
trophyflags <bits>                  prize models received (GameState::trophy_flags)
player <name>                       last name entered
fcomment <name>                     the save's name (Edit file name)
endings <short seen> <long seen>
arcade <course level> <car flag bits> <award bits>
abest <difficulty*34+course> <best place>    arcade Single Race results (non-default only)
atrank <challenge 0-9> <best rank>           time-trial best ranks (non-default only)
ps2opt <hex>                        options block of an imported PS2 save (0x888 bytes)
end
```

Strings escape space, `%`, control characters and bytes ≥ 0x80 as `%XX`; `-` = empty.
A seeded time-trial record (`at:` ranking, no car) has as its name the original's
one-byte mark 0xFF − 3 i − k (challenge i, record k), e.g. `rank at:AT020001 107.968 - 0
%FC 0 -`; loading converts the names older saves gave them (`-`, empty, or 0xFF / 0xFE /
0xFD in every challenge) to the challenge's marks by their order.
Unknown keys are skipped, so later versions can add records. A `settings` /
`ssettings` line with fewer values (written before a field was appended) leaves the
rest at their defaults; extra values (a newer build's) are ignored. Neither needs a
version change; a newer version is refused rather than read as far as it goes.

PS2 import/export (`import_ps2_save`, `export_ps2_save`): the original
`BASCUS-97102GAMEDATA` data file (gt_mode §12): header check (version 7),
CRC-32, byte inversion, then the scalar block, cars (part slots remapped to
our order, settings, owned bits, condition), wheels, license medals and best
times, race results (gcf tag-4 order), prize flags (tag-15 order), the
arcade progression (scalar +0x3C / +0x40.. / +0xC4, the Single Race result
block), the arcade time-trial rankings (`at:` rankings, built-in records
included: their one-byte name marks are kept as the names and written back unchanged
with the new-game stamp; a challenge without a ranking is written with the new game's
seeds, `GtMode::time_trial_seed_ranking`) and the course time-trial and machine-test
rankings (`ct:` / `mt:`, Phase 7 "Rankings in the PS2 save"). Blocks GT mode does not
track are written as a new game has them: the licence rankings beyond the best time,
the idle championship and, unless the state came from a PS2 save, the options
(`default_ps2_options`: all sound on, the race songs of `bgm/ads.inf` in the music
list, shuffled as a new game's race playlist, the button tables). An
imported save keeps its 0x888-byte options block (`GameState::ps2_options`,
native `ps2opt` line) so an export restores the player's options. Our own options,
the race playlist (the block's music list, `race::RacePlaylist`) included, live in
config.ini `[options]` and are not read from or written into that block.

File level (`import_ps2_save_path`, `export_ps2_save_path`, `gt3tool
save-import` / `save-export`, user steps in the README): the data file (under
any name), its save folder, a PCSX2 folder memory card, a `.psu` and an 8 MB
card image (`.ps2` / `.mcd`, with or without ECC) are read; `.psu`, a new card
image, the data file alone (`.raw`) and a folder are written. A save editor's
changes (GT3SaveEditor: values in place, cars added or removed with the payload
size and CRC-32 updated, the 64-byte padding kept) import like the game's own
file (`gtmode_disc_ps2_raw_save_edited`). The folder holds what the game writes
(`export_ps2_save_files`): `icon0.ico` (the disc's `menu/icon/ICON_a_spec.ico`),
`icon.sys` (the disc's `menu/icon/icon.sys` with the title and icon names
filled in), the data file and the 0x60-byte `thumbnail`. The containers are in
`src/formats/ps2/memcard.*`: the card writer formats like the PS2 BIOS (same
superblock, FAT, directory entries and page ECC as a card the BIOS formatted in
PCSX2, apart from the time stamps). `.max` / `.cbs` are not supported.

Verified in PCSX2 (original game, folder and `.ps2` cards, October 2026):
- A new game saved by the original imports and exports back byte for byte.
- A GT3 Revisited save (3 cars incl. a prize car, paint, a fitted wheel, a
  turbo kit, odometers, B license medals, Sunday Cup won, 123,456 Cr, day 12)
  exported as a folder card and as a `.ps2` card loads in the original with
  the same status bar, garage (order, ride car, paint names, wheel, mileage,
  values), Game Status (wins / races, prize total, license and medal counts,
  cars, bonus cars, trophies) and B license medals; the original re-saving it
  writes the same data file, which imports unchanged.
- A car sold in the original imports with the new credits, garage and ride
  index. The original overwrites the data file without truncating it (the
  header's payload size counts).
- Fixed by this check: the options (a zeroed block turned every sound off and
  emptied the music list), the thumbnail's completion (per cent, not a
  fraction), the ride index copy at scalar +0x2C, the new-game contents of the
  record blocks and the championship.

Not carried: the memory card's setting files (`BASCUS-97102S…` folders; the
'S' data file itself is `import_ps2_setting` / `export_ps2_setting`), license
rankings beyond the best time, a championship saved between rounds (restarts)
and the prize-car origin. The
original's Game Status shows the total distance (scalar +0x20, 2 m units) as
km labelled "miles" (169.0 for 169 km) while car mileage is converted; GT3
Revisited converts both (M: an original display quirk).

## Tests and screenshots

```bash
./build-gtmode/gt3r_tests gtmode                     # logic and synthetic menus (no disc)
GT3R_DISC="$PWD/PBPX-95503 (1.10).iso" ./build-gtmode/gt3r_tests gtmode
GT3R_GTMODE_DEMO_SAVE=/tmp/x/demo.sav GT3R_DISC=… ./build-gtmode/gt3r_tests gtmode_disc_demo_save
GT3R_GTMODE_SAVE=/tmp/x/demo.sav GT3R_GTMODE_NOSAVE=1 GT3R_GT3MENU_SCRIPT="down,cross,wait:90" \
  GT3R_GT3MENU_FREEZE=1 ./build-gtmode/gt3r.app/Contents/MacOS/gt3r "PBPX-95503 (1.10).iso" --screen gt3menu --shot map.png
```

Disc tests check: gcf constants (18,000 / 50 / 250 / 500, 364 codes); 151
dealer cars, 124 buyable, 11 affordable at the start, cheapest 11,140; the
league sizes 20/23/22/10/10 and every race code resolving to an event; 6 ×
8 license tests; prize-set cars all exist; buying/selling/ride rules; R34
turbo prices 4,800 / 13,500 / 42,500 / 80,000, turbo fit and back to stock,
tire sets; Sunday Cup prizes 1,000…500 and its prize car (once); entry
rules (license, drivetrain, NA, regulation); license judging (B-1 35.100 /
35.400 / 35.800), license obtained and the B all-gold car; a 5-round
championship with bonus; the car-spec bridge (97 % new, final drive
setting reaches the spec); PS2 export → import round trip and CRC; and the
whole menu path title → map → dealer → buy → Sunday Cup → GameRequest →
GameResult → prize money (`test_gtmode_menu.cpp`); the car wash and oil change
popups holding the controls until the wash (212 frames) / the refill (325); the arcade data (34
courses in six groups, class sizes, the NTSC-U open-car counts, the AT01
defaults), the unlock pass (Easy opens groups, Normal / Hard car flags, the
split-difficulty quirk, Pro not counting), time-trial ranks and the prize
car, both save formats; and through the menus the filtered pickers, L1 + R1
Professional and its art, Clear Status marks and the Goal refusal, the class
refusals and the record name entry after machine tests and time trials.

Without the disc, `gtmode_synth_*` drive the menus through `GameFlow` on the invented
world of `tests/gtmode_synth.h` (made-up cars, races, licences, arcade courses and menu
sets): title and map focus, dealer, garage, tune shop, races and prize pages, the
whole-game prize, a championship, licences and name entry, the prize counter's steps,
machine tests, course time trials and records, settings and setting files, the save
page, arcade Single Race and 2 Player, options and the music list, attract mode, the car
wash. They never write a save. `save_library_*` cover the save library (unreadable saves
kept).

- Showroom reflections: the image of the element a car is drawn on (the
  `car_render_*` widgets, the arcade pickers' `car` element) is the
  showroom's reflection map, a 512 × 512 sphere map in eye space. Every
  showroom shares the dark ceiling with spotlights (e.g. `gt_mode1.pmb`
  image 1, `arcade1.pmb` image 154) except the garage (`gt_mode2.pmb` image
  17, a glass-walled hall). `MenuModelRenderer` uploads it once
  (`DrawOptions::env_sphere`) and draws the reflection passes at full
  strength × (1 − dirt). Checked against the arcade NSX capture: the
  windscreen band and the bonnet's spotlight sheen now appear. `gt3tool
  menu-image <pmb> <index> <out.png>` exports such an image.
