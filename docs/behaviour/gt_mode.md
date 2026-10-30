# GT Mode: rules and behavior

This document describes what GT Mode does in GT3 Revisited: money and prizes,
dealers, the garage, the tune shop, car condition, licenses, races and championships,
records, game progress, endings and what a save holds. It also covers the arcade
progression, because the game keeps it in the same game state and save as GT Mode.

Code comments cite its sections as `gt_mode §n`; a few comments in the same files cite
other behavior pages (physics, presentation, menus).
Numbers 1, 2, 3 and so on are used in order, with these exceptions:

- **§9 and §11 are not used.** In the race code those numbers name sections of
  other documents.
- **§6.6 and §6.7 belong to the physics description.** That is where the car's
  user-setting words and the gear-ratio generator are described.
- **§18b** follows §18 (the "Garage is full" page).

Everything here can be checked against `src/game/` or by running the tests:

```
TMPDIR=$PWD/build/tmp GT3R_DISC=disc/GT3.iso ./build/gt3r_tests gtmode
```

The test names quoted below (`gtmode_disc_*`, `gtmode_menu_disc_*`, `gtmode_synth_*`,
`save_library_*`, `random_*`, `race_*`, `exact_game_*`, `engine_condition_rules`) are in
`tests/test_gtmode.cpp`, `tests/test_gtmode_menu.cpp`, `tests/test_gtmode_menu_synth.cpp`,
`tests/test_save_library.cpp`, `tests/test_random.cpp`, `tests/test_race_presentation.cpp`,
`tests/test_race_lap_events.cpp`, `tests/test_exact_solo.cpp` and `tests/test_car_spec.cpp`.
Figures marked "on this disc" come from the NTSC-U data set (`GT3.iso`, PBPX-95503
1.10) that the tests run on.

---

## §1 Scope, data and conventions

The engine reads GT Mode's rules from the disc's data. Nothing is hard-coded except
where noted. `GtData::load` (`src/game/gt_mode.cpp`) reads:

| Source | What GT Mode takes from it |
|---|---|
| `database/paramdb_us.db` (+ string tables) | cars (price, flags, category, region), parts, events, regulations, courses (`docs/formats/database.md`) |
| `gameconf/product.gcf` (`src/game/gcf.*`) | constants (`initial`, `car_wash`, `oil_change`, `wheel`), section 0 (title idle time), section 4 (the 364 GT-mode result codes and their unlock level), section 12 (attract demos), section 15 (prize sets), sections 8 and 14 (arcade course and car lists) |
| `menu/gt_mode.mbl` (`src/game/gt_catalog.*`) | what GT Mode offers: dealer cars (`cd/` pages, action `set_car_code`), race series (`r/<tier>/<series>` pages, `set_race_code` / `set_race_code_champ`), license tests (`lc/lc_<id>` pages, `go_license`), tune-shop pages. The game has no separate list of these: its menus are the list. |
| `database/carcolor.db` / `.sdb` | paint lists per body model and paint names (§7) |
| `bgm/ads.inf` | the race songs, for the options block of a PS2 save (§12) |
| the game program on the disc | the 30 built-in record holder names (§16) |

The data set is the US one. Its menus show miles, HP and ft-lb (`GtData::imperial`),
and its region index is 1 (US). A refusal is reported by its menu page, for example
`00cmn/no_money`. Every rule that can refuse returns a `Check` (`src/game/gt_mode.h`),
and §3.3 lists the pages.

Units: credits (Cr) are whole numbers held as 64-bit integers. Distances are in meters,
times in seconds and frames at 60 Hz. Money is printed with thousands separators
("18,000", `format_credits`) and times as m'ss.mmm ("1'02.400", `format_time_ms`).
Some menu figures are computed in single precision with the console's rounding (the
products truncate towards zero, `base/ps2_float.h`), so that truncated figures match
the screens.

## §2 New game, money and days

**New game** (`GtMode::new_game`): credits = gcf `initial`, which is 18,000 on this
disc. The day is 1, the garage is empty, there is no ride car, and every record,
license and result is cleared. The GT-mode random state is kept, not reset (§20.2).
`gtmode_disc_constants` checks these values.

**Fixed prices** (gcf constants `car_wash`, `oil_change` and `wheel`; on this disc):

| Item | Price |
|---|---|
| Car wash | 50 Cr |
| Oil change | 250 Cr |
| Wheel (any) | 500 Cr |

The starting money and these three prices come only from the disc's game configuration;
the engine has no values of its own. When the configuration lacks one of the four,
`GtData::load` refuses the data, naming the missing value, and GT Mode does not open
(`gtmode_synth_game_config_sums`).

**Japanese.** In Japanese every sum of money is 100 times the value the disc stores: the
starting money and fixed prices, car and part prices, race prizes and championship
bonuses (`GtData::money`). The Japanese disc's starting money of 15,000 therefore shows
as 1,500,000 Cr. The other languages show the stored values
(`region_disc_money_from_data` in `tests/test_region.cpp`).

Car and part prices come from the database (§5–§7). Money comes in from race prizes
(§22), championship bonuses (§22.2) and selling cars (§5). The balance never goes
negative, because every purchase is checked against it first.

**Days.** The day counter rises by 1 each time the player starts a race, a license test,
a machine test or a free run (the menu's `go_race`, `go_license`, `go_machine_test`
and `go_free_run`; `GtMode::enter`). Starting a championship counts once. Later rounds
do not cost a day, whether they follow on directly or resume from a save. Shopping,
selling and settings never cost a day. The status bar shows "day N".

**Status page figures** (`widget_text`, `status_*` classes):

| Figure | Rule |
|---|---|
| Wins / Races | race wins / races entered (championship rounds count as races) |
| Win ratio | wins / races × 100, one decimal; "0 %" before any race |
| Total distance | all distance driven, shown in miles (meters / 1609.344) on NTSC-U |
| Prize money | total prize money won, bonuses included |
| Trophies | see §3.2 |
| Cars, total value | garage size; sum of the cars' catalog prices |
| Prize cars | prize cars received |
| License medals | counts of gold, silver and bronze medals over the 48 tests |
| Game status | completion (§4) |

## §3 The game state

`GameState` (`src/game/gt_state.h`) holds everything GT Mode remembers. All of it is
saved (§12).

| Field | Meaning |
|---|---|
| credits, day | money, day counter (§2) |
| races, wins, prize money, distance | status counters (§2) |
| garage, ride | up to 200 cars, newest first; index of the ride car, or none (§5) |
| wheels | wheel inventory (§7) |
| license tests | 6 × 8 records: best medal and best time (§21) |
| race results | best finishing place per race code; "never raced" is place 8 |
| prizes given | names of the prize sets already awarded (§23) |
| championship | the championship in progress, if any (§22.2) |
| setting files | saved car setups (§19) |
| machine test bests, records rankings, last name entered | §17 |
| endings seen | short and long ending flags (§10) |
| trophies, prize cars, award bits, prize-model flags | §3.2 |
| GT-mode random state | §20.2 |
| arcade progression | §16 |
| file name, PS2 options block | §12 |

### §3.1 A garage car

Each car (`GarageCar`) records:

- the database car label;
- a serial number in order of acquisition (the "date" sort key);
- its origin (the prize set it came from, empty for a bought car);
- its paint index into its model's paint list;
- the 27 fitted part ids (same slot order as the database car record);
- the set of owned tune-shop goods (§6.1);
- the user settings (the physics description's setting words);
- the reference final drive the gear sliders are built from (§14);
- the fitted wheel (empty = stock);
- its condition (§13).

The goods owned belong to the car, not to the player. A part bought for one car is not
available on another. Wheels are the exception: they are kept in a player-wide
inventory (§7).

### §3.2 Trophies, award bits and prize-model flags

The **trophy count** goes up by one for:

- a GT-mode cup, meaning a 1st, 2nd or 3rd place that paid prize money in an event of
  trophy class 5–8 (event +0x1D3);
- a championship title;
- an arcade Single Race win.

It does not go up for prize cars or for the consolation prize (4th–6th).

The **award bits** are one 32-bit word that records which prize scenes the player has
earned:

| Bits | Set when |
|---|---|
| 1 << ((class − 5) × 3 + place) | a GT-mode cup: trophy class 5–8, place 0–2 (0 = 1st) |
| 0x10000 | a GT-mode consolation prize (places 4–6 with prize money) |
| 0x1000 << difficulty | an arcade Single Race won at Easy (0), Normal (1), Hard (2) or Professional (3) |
| 0x20000 << medal | a license medal: gold (0), silver (1), bronze (2) |

The **prize-model flags** are a second word:

- bit 1 << license (B, A, IB, IA, S, R) is set when that license card is presented,
  that is, when the license is obtained (§21);
- bit 0x40 << *i* is set when championship trophy `champ_0i` is won (§18).

For example, finishing 3rd in a Sunday Cup race (trophy class 5) sets award bit 0x4
and adds a trophy. A later 4th place adds bit 0x10000 and no trophy
(`gtmode_disc_races_and_prizes`).

### §3.3 Pre-checks and refusal pages

Before the menus follow an item, GT Mode checks it (`intercept` in
`src/game/gt_mode_menu/prechecks.cpp`). If the check fails, a popup page opens instead of the
item. The checks:

| Item | Check, in order | Pages |
|---|---|---|
| Buy a dealer car | not-for-sale flag (car +0xF0 bit value 1), money, garage full (200) | `00cmn/not_for_buy`, `no_money`, `max_car` |
| Sell a car | cannot-sell flag (bit value 2) | `00cmn/not_for_sell` |
| Tune shop pages, GT Auto (`at/at`), machine test, the garage's free run, `go_free_run` | a ride car exists | `00cmn/ride_car` |
| Wheel shop; change wheel in the garage | ride car (wheel shop only); the car allows wheel changes (bit value 4) | `00cmn/ride_car`, `not_for_change_wheel` |
| Buy a wheel | ride car, wheel flag, already in the inventory, money | as above, `00cmn/already_you_have`, `no_money` |
| Buy a part | §6.2 | `ts/already_you_have`, `ts/not_exist`, `00cmn/no_money` |
| Buy all tires | §6.4 | `ts/already_you_have_all_tires`, `no_money` |
| Free run on a dirt course | ride car owns dirt tires | `hm/need_dirt_tire` |
| Enter a race | ride car, then the entry rules (§8) | `00cmn/ride_car`, `license_violation`, `regulation_violation` |
| Open a license page / take a test | §21 | `lc/no_license_<id>`, `lc/no_last_exam` |
| Car wash / oil change | ride car, money | `00cmn/ride_car`, `no_money` |
| After buying a car with others in the garage | — | `00cmn/cd_ride_sure` ("ride the new car?") |
| Trade: buy a guest car | §19 | `not_for_buy`, `no_money`, `max_car` |

Race series whose gcf section-4 unlock level is 1 (the F1 series, `Gf1_h_1000`… on this
disc) are disabled until the S license is held (`GtMode::series_unlocked`).

### §3.4 What a race result records

`GtMode::record_race` handles a single race. Only a race that was started counts:
leaving from the pre-race menu without starting it records nothing, not even the race
count (`GameResult::started`).

1. Add one to the race count. A retired car (no place) stops here.
2. Pay the place prize, places 1–6, every time the race is run. The event's six prizes
   are at +0x1D4, in units of 100 Cr.
3. Update the trophy count and award bits for the race prize page (§3.2,
   `note_race_prize`).
4. Add the prize to the credits and to the total prize money.
5. For a win, add one to the win count.
6. Keep the best place for this race code.
7. For a win, check the prize sets (§23).

The Sunday Cup pays 1,000 / 900 / 800 / 700 / 600 / 500 Cr. Finishing 3rd there pays
800 Cr (`gtmode_disc_races_and_prizes`).

## §4 Game completion

The completion ratio (`GtMode::completion`) counts four kinds of clear:

  c = (W + A + T + L) / (364 + 102 + 10 + 48)

| Term | Meaning | Total |
|---|---|---|
| W | GT-mode result codes won (gcf section 4: the 337 series races plus the 27 championships' own codes) | the number of gcf section-4 entries: 364 on this disc |
| A | arcade Clear Status count over the courses of the gcf `SINGLE_RACE` list (§16) | fixed 102 |
| T | arcade time trials cleared (best rank 0) | 10 |
| L | license tests with any medal | 48 |

The total is 524 on this disc. The ratio is exactly 1 only when everything is cleared.
Otherwise it is capped at 0.99, so a nearly complete game never shows 100 %.

How the ratio is shown (all values are truncated, never rounded):

- **Status page:** the ratio × 1000 in single precision, truncated to per mille and
  capped at 999, printed "2.0 %". "100.0 %" appears only when the game is complete.
- **Save thumbnail:** the percent value (ratio × 100) as a float.
- **File lists:** that percent value truncated to one decimal, "2.0%".

With 11 of 524 clears (2.0992 %) every text reads "2.0", never "2.1"
(`gtmode_disc_completion_texts`). The ratio also sets the `achieve_*` prize thresholds
of 0.5, 0.75 and 1.0 (§23).

## §5 Garage

- **Capacity** 200 cars (`kMaxGarageCars`). A new car (bought, prize or traded) goes to
  the head of the list and gets the next serial number. The ride car stays the same car.
- **The first car** in an empty garage becomes the ride car (so does any car added while
  there is no ride car). After buying a car when
  the garage already has one or more, the dealer asks whether to ride the new one.
- **Selling** pays a quarter of the catalog price (car +0xEC), whatever the car's
  condition or parts. Cars with the cannot-sell flag (bit value 2: prize-only models)
  are refused, and their sell price shows "----". Selling the ride car leaves the
  player without one, and races are then refused (`gtmode_disc_dealer_and_garage`:
  11,140 Cr car → 2,785 Cr).
- **Sort keys** (Start on the garage list; `GtMode::garage_order`). Every sort is
  stable. The header shows the key and each row its value (`sort_value`):

| Key | Order | Row value |
|---|---|---|
| 0 date | serial, newest first | — |
| 1 name | car name A–Z | — |
| 2 maker | maker, then name | maker |
| 3 color | paint index | paint name |
| 4 price | catalog price, highest first | the sale price (a quarter of the catalog price), digits only ("13,995"); "-" for a car that cannot be sold |
| 5 distance | odometer, highest first | "N.N km" |
| 6 power | power figure (below), highest first | "325 HP" |
| 7 drive | drive type of the fitted drivetrain | FF / FR / … |
| 8 aspiration | NA before turbo (fitted turbine kit +0x11 ≠ 0) | "NA" / "TURBO" |

**The list.** Up / Down move the cursor, stopping at the ends, with the `cursor` sound
when it moves. The list scrolls to keep the cursor in view, and choosing a sort key
puts the cursor back on the first car. A hovering pointer moves the cursor to the row
under it, a tap on another row moves the cursor there, and a tap on the cursor's row
opens the car. How the rows and the scroll bar are drawn is in menus §9.7
(`gtmode_menu_disc_vertical_lists`, `gtmode_synth_garage_list_scroll`).

**Power figures.** The garage, tune shop and settings menu show the car's computed
peak power, not the catalog figure (`GtMode::power_peak`, `spec_power_peak` in
`src/game/net_car.cpp`). The engine builds the car's menu description (`sim::CarSpec`,
`GtMode::car_spec`) from its fitted parts and settings; it holds no engine condition. It samples the
torque curve every 100 rpm, from the first curve point rounded to 100 rpm up to the
rev limit, and multiplies each looked-up torque by the car's condition factor f (§13)
in single precision:

  P(rpm) = (f · T(rpm) / 9.8) × rpm / 716.2   (PS; T in N·m)

The highest value wins (strictly greater) and its rpm is shown with it. The NTSC-U
menus convert to HP as PS × 0.98619 and truncate ("325HP/6800rpm").

A new car is running in, so it shows 97 % of its full figure
(`gtmode_disc_garage_power`). The dealer pages show the stock car's computed figure,
and `spec_max_power` the catalog text. The garage mileage is shown in miles on
NTSC-U.

## §6 Tune shop

The tune shop always works on the ride car.

### §6.1 Goods

Every part category the shop deals in has a goods id 0–86. A goods id names a database
parts table and a stage (part record +0x10). The ids are consecutive per table
(`goods_info` / `goods_id`, `src/game/gt_parts.cpp`):

| Goods | Table | Stages |
|---|---|---|
| 0–1 | brakes (0) | stock, racing |
| 2–3 | brake controller (1) | off, on |
| 4–7 | clutch (15) | stock, single, twin, triple |
| 8–9 | computer (10) | stock, racing chip |
| 10–12 | displacement (9) | stock, up 1, up 2 |
| 13–14 | engine balance (8) | stock, balanced |
| 15–18 | flywheel (14) | stock, light, chromoly, light chromoly |
| 19–22 | transmission (17) | stock, close, super close, fully customizable |
| 23–25 | intercooler (19) | stock, sports, racing |
| 26–29 | weight reduction (4) | stock, stages 1–3 |
| 30–35 | LSD (21) | stock, 1-way, 2-way, 1.5-way, fully customizable, AYC |
| 36–39 | muffler (20) | stock, sports, semi-racing, racing |
| 40–43 | NA tune (11) | stock, stages 1–3 |
| 44–45 | port polish (7) | stock, polished |
| 46–47 | propeller shaft (16) | stock, carbon |
| 48–49 | racing modification (5) | stock, modified |
| 50–53 | suspension (18) | stock, sports, semi-racing, racing |
| 54–55 | traction control (22) | stock, on |
| 56–66 | front tires (28) | stages 0–9 (stock, sports, super hard … super soft, control), then dirt = stage 12 |
| 67–77 | rear tires (29) | as the front, +11 |
| 78–82 | turbine kit (12) | stock / NA, stages 1–4 |
| 83–84 | stability control (23) | stock, on |
| 85–86 | drivetrain (13) | stock, VCD |

Only the ids the US menu offers can be bought (§6.4). Racing modification (49),
traction control (55) and stability control (84) are not among them.

**Stock goods.** A new car owns the goods of its stock parts. Owning a front tire also
owns the matching rear tire. Rally cars (car category 2, car +0xF1) also own dirt
tires (`set_stock_ownership`).

### §6.2 Buying a part

`GtMode::can_buy_part` checks, in order:

1. there is a ride car (`00cmn/ride_car`);
2. the car does not already own the goods (`ts/already_you_have`). A part is bought
   once per car. After that it is swapped in the settings menu (§14);
3. the database has a part of that table and stage made for this car. The part record
   +0x08 must equal the car (`ts/not_exist`). A turbo car, for example, has no NA tune;
4. the credits cover the part's price (`00cmn/no_money`).

Buying takes the price and marks the goods owned. A front tire also marks its rear
tire. A tire set costs the front record's price, and the rear record counts as free.
The shop's price widgets show "Purchased" for goods the car owns and "----" for parts
that do not exist. Measured on an R34 GT-R: turbo stages 1–4 cost 4,800 / 13,500 /
42,500 / 80,000 Cr, and the soft racing tire set 35,000 Cr (`gtmode_disc_tune_shop`).

### §6.3 Fitting a part

`GtMode::fit_part` fits goods the car owns. The steps:

1. The slot of the part's table gets the car's part for that stage. Stage-0 goods
   without a record of their own restore the car's stock part, which is how "back to
   stock" works.
2. Turbo kits and NA tunes exclude each other. Fitting one restores the other's stock
   part.
3. The settings that belong to the part's table go back to their stock values for the
   car with its new parts (`refresh_settings`). Settings of other tables are kept:

   | Table fitted | Settings reset |
   |---|---|
   | brake controller | front / rear brake balance |
   | racing modification | front / rear downforce |
   | NA tune, turbine kit | turbo words |
   | drivetrain | front torque split |
   | transmission | gear ratios, final drive, auto-level slider; the reference final drive follows the new final (§14) |
   | suspension | camber, ride height, toe, springs, motion ratio, dampers, anti-roll bars |
   | LSD | LSD words |
   | traction control | TCS level |
   | stability control | ASCC words |

4. A front tire also fits the matching rear tire.

A fitted change counts as "tuned" (§8). The shop shows the power before and after a
part (`power_with`: the same peak computation on a copy with the part fitted). After
fitting, the "Installed" page shows the power the car had before and has now.

### §6.4 Shop items and flows

Each shop menu item maps to goods and a flow (`shop_items`). The flows are:

- **buy, then ask** "install now?": most parts;
- **buy and fit at once**: port polish, engine balance, displacement, weight reduction
  stages;
- **buy only**: dirt tires. The shop never fits them. Owning them is what dirt events
  ask for (§8).

**Buy all tires** buys every racing compound from super hard to super soft (goods
58–64) that the car does not own and that exists for it, each with its rear set. The
price is the sum of their front prices. When none is left to buy, the shop refuses with
`ts/already_you_have_all_tires`.

## §7 Dealers, paints and wheels

**Dealers.** The dealer catalog is every `set_car_code` item on the `cd/` pages,
grouped by region page (jp, us, gr, fr, it, uk, bel, aus). An item name that is not a
car label but becomes one without its leading `_` is resolved to that label. On this
disc there are 151 dealer cars. 124 of them can be bought (no not-for-sale flag), and
11 are affordable with the starting 18,000 Cr. The cheapest costs 11,140 Cr
(`gtmode_disc_catalog`). The price is the catalog price (car +0xEC). A car whose
not-for-sale flag (car +0xF0 bit 0) is set shows "----" as its price, both on the
dealer's list and on the dealer's car page.

**Paint.** On a dealer car page, Up / Down picks a paint from the car's model list in
`carcolor.db`. The index resets to 0 whenever the focused car changes, and the car is
bought in the chosen paint, clamped to the list. Paint names come from
`carcolor.sdb`: each color definition {id, name, alternate name, RGB} gives the name
index (`GtData::load`). Mod cars for sale join their maker's dealer page, or a dealer of
their own on the region page (`src/game/gt_mode_mods.cpp`, `docs/MODDING.md` "Dealers and
makers"); those with their own paints number those paints themselves.
A prize car gets its prize set's fixed color when the model's list has it, or a random
paint otherwise (§20.3).

**Wheels.** Every wheel costs the gcf `wheel` price (500 Cr on this disc, §2). A bought
wheel goes into a player-wide inventory and is never used up. The inventory has 200
slots and the stock-wheel placeholder takes one, so 199 wheels can be bought. A wheel
already in the inventory cannot be bought again. Any garage car that allows wheel
changes can wear any wheel in the inventory, or go back to stock. Cars whose flag bit
value 4 is set never change wheels (§3.3). The fitted wheel shows on menu renders and
is used in races.

The **wheel shop** works on the ride car. A brand page opens on the wheel the ride car
wears when that brand sells it, otherwise on its first wheel. The focused wheel is
previewed on the car. After a purchase the shop asks whether to fit the wheel, and yes
puts it on the ride car at no charge. When no slot is free, the purchase adds nothing
and charges nothing, but the shop still asks, and yes puts the wheel on the ride car
for free without it entering the inventory. The car keeps that wheel, in native and
PS2 saves, until its wheel is changed. After that it cannot be put back, because the
garage offers only the inventory's wheels (`gtmode_menu_disc_wheel_inventory_full`).

**Garage > car > Wheel** shows the wheels the chosen garage car can wear, in one row:
the stock-wheel button first (the car's own wheels), then the bought wheels in the
order they were bought. A mark is drawn on the entry the car wears and the cursor
starts there. A car wearing a wheel the inventory does not hold has no mark, and the
cursor starts on the first button. Left / Right move the cursor, stopping at the ends,
with the `cursor` sound when it moves, and the car on the page shows the wheel under
the cursor. A hovering pointer or a tap moves the cursor to the button under it.
Confirm, or a tap on the cursor's button, asks whether to equip the wheel, and yes
fits it; the stock-wheel button puts the car back on its own wheels. The row's layout
and scroll bar are in menus §9.7 (`gtmode_menu_disc_garage_wheel_list`,
`gtmode_synth_garage_wheel_list`).

## §8 Event entry rules

`GtMode::check_entry` tests one car against one event's rules. All the fields are
event-record bytes (`docs/formats/database.md`, table 32). Each rule fails on its own:

| Rule | Field | The car fails when |
|---|---|---|
| Dirt tires | +0x1D2 = 9 | it does not **own** dirt tires (goods 66) |
| License | +0x19B: 1 B … 5 S, 6 R | the player does not hold that license |
| Drivetrain | +0x1F6: 1 FF, 2 FR, 3 4WD, 4 MR, 5 RR | the fitted drivetrain's type differs (values above 5 always fail) |
| Regulation | +0x190 → table 33 list | the car label is not in the list |
| Aspiration | +0x1F5: 1 NA only, 2 turbo only | its fitted turbine kit makes it turbo (record +0x11 ≠ 0) when NA is asked, or NA when turbo is asked |
| Car class | +0x1F4: 1 race car, 2 tuned, 3 production | 1: a production car (category 0); 2: not tuned; 3: a race / rally car, or tuned |

A car is **tuned** when any fitted part (the wheel slot aside) differs from its stock
part, or when its settings differ from the stock settings of its stock parts.

`GtMode::can_enter` turns the result into one popup:

1. no ride car gives `00cmn/ride_car`;
2. a license failure gives `license_violation` (it wins over the others);
3. any other failure gives `regulation_violation`.

The race information page shows each rule ("No Limitations" when the field is 0). The
"Cars Permitted" list shows each car of the regulation as name plus second name; a
string that occurs more than once is shown only at its last place. Up / Down move its
cursor, stopping at the ends, and the list scrolls to keep the cursor in view; a
hovering pointer or a tap moves the cursor, and a tap does nothing more (layout:
menus §9.7; `gtmode_menu_disc_vertical_lists`).

The same rules guard the settings menu during a race's pre-race setup (§14).
`gtmode_disc_entry_rules` checks examples: an FF series refuses an R34 on drivetrain
and license, and the Altezza races refuse it on regulation but show the license popup.

## §10 Endings

The endings are checked whenever a result opens the prize check (§23): a race win, a
championship title (whatever the last round's place), a license turning all gold
(`Award::prize_check`, `on_result`):

- if the short ending has not been seen and the Beginner league is complete, it plays
  `stream/ending_short_full.pss` and marks it seen;
- then, if the long ending has not been seen and the Amateur or Professional league is
  complete, it plays `stream/ending_long_full.pss` and marks it seen.

Both can play after one result, the short one first, after the result pages. Each plays
once per save. A championship lost on points, or a round won on the way, checks nothing.

A league is complete (`GtMode::league_complete`) when every series of that tier is
won. For a championship series this means the championship's own code (round code
ending `00`) has place 0. For other series every race must have place 0. The arcade
mode's "Goal" button has its own availability rule (§16).

## §12 Saves

### §12.1 Native saves

GT3 Revisited saves GT Mode as text files (`src/game/gt_save.*`, `save_library.*`;
the format is in `docs/dev/GT_MODE.md` "Save format"). There can be any number of
saves, and one of them is the game being played. That save is written again
automatically after every GT-mode result and every finished arcade result, after an
unfinished arcade run that carried a garage car's engine condition (§13.1) or recorded
runs (§17), after a record's name entry, after buying, selling or trading a car, after a
committed settings change or part change in the settings menu, and after every setting-file
operation (§19). The race screen also writes some results before the menus see them
(§12.3).
Tune-shop, wheel, wash and oil purchases are kept in memory and written with the next
of these saves. Writes are atomic (temporary file, then rename). Unknown lines are skipped, and
newer save versions are refused. A car's settings line (and a setting file's) is read from
the front: a save written before a setting was added has fewer values, and the missing
settings keep their defaults. Extra values from a newer build are ignored. A value that is
not a number makes the save unreadable (`gtmode_save_settings_line_tolerant`). A ranking
entry is saved with its name and clock stamp (§17); entries from saves written before the
stamps load with none. After the stamp an entry can carry one more token, the record's
32-byte block from an imported PS2 save (§12.2); an entry without it loads with none.

**Unreadable saves** (damaged, or from a newer version) are never written over. When the
game to continue cannot be read, a new game starts in its place. Before anything is
written, the file is moved aside to `<file>.unreadable-<yyyymmdd>-<hhmmss>`, with "-2",
"-3" … when that name is taken. If it cannot be moved it is copied, so its bytes are
kept either way, and a warning is logged (`GtModeSession::load`, `set_aside_save`). The
save library does not list the moved-aside copies. With `GT3R_GTMODE_NOSAVE` set nothing
is moved (`save_library_unreadable_kept`, `save_library_disc_unreadable_attach`).

### §12.2 The PS2 game file

Saves can be exported to, and imported from, the original game's memory-card save
(`import_ps2_save`, `export_ps2_save`). Import reads the data file alone (under any
name), the save folder, a folder memory card, a `.psu` or a `.ps2` / `.mcd` card image;
export writes a `.psu`, a card image, the data file alone (`.raw`) or a folder
(`import_ps2_save_path`, `export_ps2_save_path`).

On the save page, Export to PS2 first asks for the format, the same on every platform:
"PS2 save (.psu)", "PS2 card (.ps2)" (a new memory-card image holding only this save)
or "Raw save (.raw)" (the game data file alone, the file PS2 save editors open). The
file dialog then only picks the place. It starts on the save's name with the format's
extension, a name typed without an extension gets that extension, and the chosen format
is written whatever the file is called (`SavePage::kExportFormats`). `gt3tool
save-export` picks the format from the file name instead: `.psu`, `.ps2` / `.mcd`,
`.raw`, anything else a folder (`gtmode_ps2_export_format`,
`gtmode_disc_ps2_save_containers`). Import recognizes the data file by its header,
whatever it is called. A data file changed in a save editor imports when its CRC-32
is right, also when it was put back into its `.psu` padded with zeros, and is refused
when the CRC-32 does not match (`gtmode_disc_ps2_raw_save_edited`).

The card directory `BASCUS-97102GAMEDATA` holds four files, written in this order:

| File | Contents |
|---|---|
| `icon0.ico` | the disc's `menu/icon/ICON_a_spec.ico` |
| `icon.sys` | the disc's `menu/icon/icon.sys` with the title "GT3 GAME DATA" and the icon names filled in |
| `BASCUS-97102GAMEDATA` | the game data: header (version 7.1), every payload byte inverted, CRC-32 (`docs/FORMATS.md`) |
| `thumbnail` | 0x60 bytes the file list reads without loading the game: credits, number of cars, highest license (−1 none), day, completion percent (§4), file name |

The game data holds:

- the scalars (including the GT-mode random state);
- cars and wheels;
- license records;
- race results in gcf section-4 order;
- prize flags in gcf section-15 order;
- the last name entered (scalar block +0x70, up to 15 characters and a 0; longer names
  are cut), which the name entry starts with (§17.1);
- the arcade progression and time-trial rankings, each record with its clock stamp
  (§17). A built-in record without a stamp is written with the stamp of the reference
  new game (0x3264FD64, which reads 16/10/26), and imported records keep the stamp
  stored on the card. Each record also has a 32-byte block at +0x120, which records set
  in the original game carry. The engine makes no block for its own records and
  writes 30 '-' there. A block read from a card is kept with the record and written
  back. The block does not name the car, so an imported player record gets the
  challenge's base car (§16);
- the course time-trial rankings (§17): 44 tables of 0x300 bytes after the arcade
  time trials, one per slot. A course's slot is its place in the `TIME_ATTACK` list
  (§16), so the 36 listed courses fill slots 0–35 and slots 36–43 stay empty;
- the three machine-test rankings after them (0–400 m, 0–1000 m, top speed), with the
  same layout.

Each course and machine-test table holds 12 entries: u32 values, 16-byte names at
+0x30, u64 car codes (the id hash of the car label) at +0xF0, u32 clock stamps at
+0x150 and 32-byte blocks at +0x180 (kept and written back as above, 30 '-' for the
engine's own records). Times are stored in ms, and an empty time entry holds the "no
time" value 0x157529FF with 30 '-' in its block. The top-speed test stores int(m/s ×
10⁵), so 300 km/h is 8333333, and its empty entries are all zero. On import a table
is read up to its first empty entry, and each machine test's best (§17) becomes the
first entry of its ranking. Slots of courses not in the list are not read
(`gtmode_disc_course_time_trials_and_save`).

A new game saved by the original game imports and exports back byte for byte
(`gtmode_disc_ps2_new_game_layout`).

Blocks GT Mode does not track are written the way a new game has them:

- the idle championship;
- the options, unless the save came from the PS2.

**The options block.** A save imported from the PS2 keeps its options block unchanged,
so exporting it again restores the player's options, music list and buttons.
Otherwise the engine writes a new game's options (`default_ps2_options`,
`src/game/gt_save.cpp`):

- +0x00 screen mode (0 = 4:3);
- the sound switches, all on (1): +0x04 menu BGM, +0x08 menu SE, +0x0C race BGM,
  +0x10 race SE, +0x54 replay BGM, +0x58 replay SE;
- the music list at +0x228, written as a new game's race playlist
  (`race::RacePlaylist`, music page §2.3): u32 song count, u16 cursor, u8 shuffle (on),
  u8 "no song listed", u16 seed, then 64 entries of 16 bytes {u64 id hash of the song
  file, u8 played, u8 in the list, s16 order}. It holds the race songs of
  `bgm/ads.inf`, every entry marked as in the list, in an order shuffled from the first
  integer draw of the shared generator in its start-up state (§20.1). That leaves the
  seed 0x456, the value a new game saved by the original game stores
  (`gtmode_disc_ps2_new_game_layout`);
- everything else (vibration, assists, 2 Player Battle, ghost, GT FORCE, the button
  tables at +0x63C) is a copy of a new game's block, and the code marks its meaning as
  not confirmed.

GT3 Revisited's own options, its race playlist included, are kept in `config.ini`
`[options]`; they are not read from this block or written into it.

**Not carried** from a PS2 save:

- a championship suspended between rounds (it restarts);
- license rankings beyond the best time;
- the prize-car origin;
- the endings-seen flags and the file name;
- the setting files, which are separate card files (§19).

The total distance is stored in 2 m units, so it can lose up to 2 m on export.

### §12.3 Saves from the race screen

A few results are written while the race screen is still open, so they are kept if the
game is closed before it returns to the menus. Each is a copy of the game with the result
recorded. The running game is recorded from the result when the race screen closes, and
that autosave replaces the copy (`src/game/gt_mode_menu/session.cpp`):

- **A GT single race** (not a championship round, not a practice run) that finished
  classified is written as its result is taken, before its replay: the place, the prize
  and the best place, as §3.4 records them (`save_race_run`).
- **A license test** run with a medal (not a practice run or the demonstration) is
  written as soon as it is judged (`save_license_run`).
- **A championship round's "Save game"** (the result quick menu) opens the save page over
  a snapshot of the game with this round recorded: the engine condition, the dirt, the
  place and the points (`begin_race_game_save`). It can be saved as a new save or over an
  existing one. The running game and its current save stay as they are, and the snapshot
  is dropped when the page closes (`end_race_game_save`;
  `gtmode_menu_disc_arcade_garage_cars`). After the last round "Save game" and EXIT are
  locked.

## §13 Car condition

Each garage car carries four condition values (`CarCondition`). They are changed only
by driving, an oil change or a wash:

| Value | Starts at | Driving *d* meters |
|---|---|---|
| odometer | 0 | + *d*, counted in 2 m units (below) |
| engine wear *E* | −300,000 m (a new car is running in) | *E* = min(*E* + *d*, 600,000) |
| oil *O* | 0 (no bonus) | *O* + *d*. If *O* goes from negative to ≥ 0 it jumps to 300,000. Capped at 300,000 |
| dirt | 0 | set from the run's result, 0–0.8 (below) |

The odometer counts whole 2 m units (`sim::odometer_step`, the same rule as the race
car's own odometer): the meters driven forward are truncated and halved, and the odd meter
is carried on within the run. It stops at 0xEE6B27A6 units. *E* and *O* grow with the
distance driven in either direction; reversing adds nothing to the odometer.
`engine_condition_rules` checks the steps. The total distance of the game grows by 2 m for
every unit the run added. Which runs drive the car is in §13.1.

**Dirt.** The race car gets dirty as it drives (physics page §8.3b), up to 0.8. After every
GT-mode run (races, championship rounds, license tests, machine tests, free runs) the ride
car takes the dirt the race car ended with, at most 0.8 (`gtmode_synth_hook_branches` checks
the cap). In races, machine tests and free runs the race car starts with the ride car's
dirt. A license test drives the event's car, which starts
clean, so the ride car comes back with that car's dirt.

**Engine factor.** The car's whole torque curve is multiplied by f = f_E × f_O
(`GtMode::engine_factor`, which uses `sim::engine_condition_factor`; the physics
description owns the formula):

| *E* | f_E |
|---|---|
| ≤ −300 km | 0.97 |
| −300 … 0 km | rises linearly from 0.97 to 1.00 |
| 0 … 500 km | 1.00 |
| 500 … 600 km | falls linearly from 1.00 to 0.98 |
| ≥ 600 km | 0.98 |

| *O* | f_O |
|---|---|
| ≤ −100 km | 1.05 |
| −100 … 0 km | falls linearly to 1.00 |
| ≥ 0 | 1.00 |

In practice:

- **Running in.** A new car has 97 % power. It reaches 100 % after 300 km and loses
  2 % between 800 and 900 km on the odometer.
- **Oil change** (250 Cr) sets *O* = −300,000 m. That gives +5 % for the next 200 km,
  fading to nothing at 300 km. A new car's oil gives no bonus until it is changed.
- **Car wash** (50 Cr) sets dirt to 0. Dirt only changes how the car looks: menu and
  race renders reflect 1 − dirt.

Measured with `GtMode::engine_factor` (and checked by `gtmode_engine_factor`,
`gtmode_engine_factor_floor` and `engine_condition_rules`): f_E =
0.97, 0.985, 1.0, 0.99 and 0.98 at −300, −150, 0, 550 and 600 km, and f_O = 1.05,
1.025 and 1.0 at −100, −50 and 0 km. The race HUD reads the race car's odometer and oil
counter for its odometer and oil lamp (the lamp is lit at the worn value, *O* ≥ 300,000).

**Oil age** (`GtMode::oil_age`), how used the oil is from 0 (fresh) to 1 (used), as the
oil change page shows it (§13.2): after an oil change (*O* < 0) it is the share of the
300 km driven, (*O* + 300,000) / 300,000; from *O* = 0 (a new car's oil, or the worn
value after the jump) it is *O* / 300,000, at most 1. So a new car's oil and just-changed
oil both read 0, 150 km after a change reads 0.5, and worn oil reads 1
(`gtmode_oil_age`).

### §13.1 Engine condition through a run

A run of a garage car starts with the car's condition and hands it back at the end
(`GtMode::carries_engine_condition`, `run_condition`, `end_run`; `gt_menu::fill_condition`
and `apply_condition` in `src/game/gt_mode_menu/races.cpp`). The race car starts with the
condition and advances the odometer, *E* and *O* every step as it drives, and its power
follows the factor f as it changes during the run (physics page §10.3). The menus apply f to
their power figures (§5). Within one visit to the race screen the condition carries on from
run to run: a retry, and the race after a free practice (§22.5), start where the last live
run left the car; a replay changes nothing.

| Run | Condition |
|---|---|
| GT races and championship rounds (`go_race`), machine tests, free runs | the ride car's; handed back |
| arcade Single Race and free run with a home-garage car | that car's; handed back |
| arcade Single Race and free run with an arcade car | a fresh engine (all zero); only the total distance grows |
| arcade run with a guest-garage car (§19) | the guest car's; nothing is handed back to it, only the total distance grows |
| license tests, time trials, 2 Player Battle | a fresh engine, also with a garage car; nothing is handed back and no distance is counted |

The condition goes back into the garage car before any prize car is added to the garage.
A GT-mode run hands it back whether it finished or not, including a championship round
that is quit (§22.2). An arcade run hands it back finished or not; an unfinished arcade
run that carried a condition is then saved (§12.1). The attract demo leaves the ride car's
dirt alone (§13). `gtmode_engine_condition_run` checks the words and the hand-back.

### §13.2 Car wash and oil change pages

GT Auto's car wash (`at/cw/cw`) and oil change (`at/ot/ot`) pages each have a button that
pays, applies the change at once (dirt 0, or *O* = −300,000 m) and opens a progress popup,
"Washing . . ." (`at/cw/cw_now`) or "Changing oil . . ." (`at/ot/ot_now`). When the popup
page is built (30 frames after the press, after the button's confirm feedback) its
`ignore_control` widget takes the controls away: no key or tap does anything and the
popup's "Complete" button is not drawn. The controls come back when the wash or oil
change is over, and "Complete" then leads back to GT Auto (`at/at`). Both run on every
menu frame, also during confirm feedback (`on_tick`). Frames below count from the frame
of the press (`gtmode_menu_disc_wash_and_oil`, `gtmode_synth_car_wash_and_touch`):

**Car wash** (`wash_spin_at`, `step_car_wash`, `src/game/gt_mode_menu/widget_art.cpp`):

- frames 1–30: wait;
- frame 31: the `car_wash` effect plays, and the car's showroom turntable starts being
  pushed round: for 181 frames it gets an extra yaw acceleration of `kWashSpinPush`
  (10,000 °/s², on top of the camera's own) once a frame, so the car spins fast;
- frame 212: the last push ends the wash. The controls come back.

The car is shown with the dirt it came in with until the wash is over, then clean.

**Oil change** (`oil_cylinder_at`, `step_oil_change`):

- frames 1–61: wait;
- frame 62: an `oil` effect; the oil drains by 0.01 of the cylinder a frame and is empty
  at frame 163;
- 61 frames' wait; at frame 224 a second `oil` effect, the new oil is in (the gauge takes
  the fresh color) and it fills back by 0.01 a frame;
- frame 325: full. The controls come back.

If the gauge is no longer on screen the animation stops; the oil change itself is
already done. A page that takes the controls away with nothing running to give them back
gives them back once it has settled.

**The oil gauge** (`oil_change_cylinder`, drawn by `oil_gauge_draws`) is a cylinder that
is always full of oil outside an oil change. Its color shows the oil age (§13): each
corner of the page's hidden `clean` coloring is blended into the hidden `dirty` one by
the oil age, so fresh oil has the clean color and 300 km-old oil the dirty one. During
an oil change the old oil keeps its color while it drains. The gauge is drawn at its
widget's depth, under the cylinder's glass and the page frame, on the oil page and under
its progress popup.

## §14 Car settings menu

The settings menu (`menu/setting.mbl`; `src/game/gt_setting.*`) edits the ride car.

**Working copy.** Opening the menu makes a working copy of the car. Sliders change the
copy, OK commits it to the garage car (and saves), and Back discards every uncommitted
change. Slider ranges and lock states come from the committed car's fitted parts. The
value shown is the working copy's. When a slider page opens, each slider's marker
remembers the value it had then.

**Ranges.** Each slider's range is read from the fitted part records (`slider_range`):

| Sliders | Range from | Shown as |
|---|---|---|
| spring rate F / R | suspension +0x21–0x22 / +0x24–0x25 | 0.1 kgf/mm, "7.2kgf/mm" |
| ride height F / R | suspension +0x50–0x52 / +0x56–0x58 (u16) | mm |
| damper, bound F / R | 1 … suspension +0x2B / +0x39 | "Level n" |
| rebound F / R | 1 … suspension +0x32 / +0x40 | "Level n" |
| camber F / R | suspension +0x11–0x12 / +0x14–0x15 | 0.1°, one decimal |
| toe F / R | suspension +0x17–0x18 / +0x1A–0x1B | 0.5 (v − 128) degrees, one decimal (128 = neutral) |
| anti-roll bar F / R | 1 … suspension +0x47 / +0x4B | "Level n" |
| brake balance F / R | 1 … brake controller +0x11 / +0x15 | "Level n" |
| LSD initial / accel / decel F, R | LSD record ranges | plain number |
| AYC | LSD +0x1D–0x1E (the rear initial word; the rear-initial slider is then fixed) | plain number |
| final drive | transmission +0x26 … +0x24 | ×0.001, three decimals |
| auto-level (computed gearboxes) | transmission +0x2A–0x2B | "Level n" |
| gears 1–n | see below | three decimals |
| downforce F / R | body +0x11–0x12 / +0x3A–0x3B | ×0.01 |
| ASCC | 1 … stability control +0x11 | "Level n" |
| TCS | traction control +0x12 steps, reversed | "Level 0" = TCS off |
| VCD | drivetrain +0x11–0x12 | "%" |

**Locks.** A setting group is locked when its part gives it no range. For example,
springs are locked when both axles have min = max, dampers need a suspension stage of
1 or 2, and bound / rebound need stage 3. A locked value shows "----", its `disable_*`
overlay covers it, and its status box on the page cannot be entered
(`setting_locked`, `setting_page_item_enabled`).

**Gear sliders.** The reference ratios v₁ … vₙ are the gear generator's ratios
(physics description §6.7) at the car's **reference final drive**. Each gear *i* may
only move inside [lo_i, hi_i]:

  hi₁ = 9/8 · v₁,  lo_i = (2 v_i + v_{i+1}) / 3,  hi_{i+1} = (v_i + 2 v_{i+1}) / 3,  lo_n = 7/8 · v_n

So each gear can never pass the gear next to it. The auto-level slider regenerates the
ratios at the current final drive and clamps each into its range. Then the reference
final drive becomes the current one, and the next ranges are built from it. Fitting a
transmission resets the reference to its stock final (§6.3).

**Event guard.** When the menu is opened from a race's pre-race setup, a non-gear
slider move that would make the car fail the event's entry rules (§8) is refused and
the old value is kept. A part change that would break them opens `not_to_change`, and
tires cannot be changed for a dirt event (`dirt_tire_only`).

**Part change pages.** The `chg/` pages list the parts the car owns. Choosing one fits
it at once (§6.3) and saves. Items for goods the car does not own are disabled. The
`attach_*` marks show what is fitted. A stage-0 item also counts as fitted when the
car has its stock part.

**Quirks kept.** The front rebound slider is stored but has no effect on the car,
because the physics reads the front second damper stage from the bump words. The
drivetrain-split word is stored but ignored by the physics.

The setting files are in §19.

## §15 Attract demo field

After 20 s idle on the title (gcf section 0), the title plays demo races from gcf
section 12 in turn. The F1 demos (level 1) play only once the save holds the S license.
There are 10 demos on this disc, two of them F1. Every demo on the disc has no car of
its own (word +8 is empty). The engine therefore draws the **whole** field, slots 0 to
cars − 1, from the event's pool with the race generator (§20.4). Slot 0 is the car the
cameras follow. A demo that names its own car keeps it in slot 0 and draws only slots
1 to cars − 1 (`screen_race_launch.cpp`; `gtmode_disc_attract_demo_field`). The demo is a
race in which the computer drives every car, slot 0 included; it records nothing and any
button ends it.

Known difference: the followed car keeps its default paint, because the engine builds
its look before the paint draw.

## §16 Arcade progression

The arcade mode keeps its progress in the GT-mode state (`ArcadeProgress`; the
`GtMode::arcade_*` rules). It is saved with GT Mode and exported to PS2 saves.

**Lists** (gcf tag 8 courses, tag 14 cars):

- `SINGLE_RACE`: 34 courses in six groups, unlock levels 0–5 (6, 6, 6, 6, 5, 5);
- `TIME_ATTACK` (free run): 36 courses, each with a course time-trial ranking (§17);
- `TWO_PLAYER`: 35 courses;
- car classes C, B, A, S, Dirt, Compulsory (the ten time-trial cars) and Preview.

Each car has an unlock level 0–12 (0 = always open). A course is open when its level is
at most the course level, which starts at 0 and goes up to 6. A car is open when its
level is 0 or its car flag is set. The pickers list the open entries in list order and
leave out cars not sold in the region.

**Region filter** (`arcade_car_region_hidden`). The car is resolved through the arcade
car (35) or car variant (31) record to its base car (30). A missing base car is hidden.
Otherwise the base car's byte +0xF7 decides:

| Value | Shown |
|---|---|
| 0 | everywhere |
| 1 / 2 | only in EU / not in EU |
| 3 / 4 | only in US / not in US |
| 5 / 6 | only outside JP and US / in JP and US |
| 7 / 8 | only in JP / not in JP |

On NTSC-U the class lists lose only the Vauxhall twins of the Opels (they have no car
record). The Preview list loses ten cars, among them the Vauxhalls and the Oreca Viper
(region byte 4, not in the US) (`gtmode_disc_arcade_data`).

**Single Race results** (`arcade_single_race`). The difficulty comes from the race
code's second letter (E, N, H, P → 0–3). The engine keeps the best place for each
difficulty × course. A win also adds a trophy and award bit 0x1000 << difficulty, and
shows the difficulty's cup (`race/a_<easy|normal|hard|special>_gold.prm`): the race
shows it right after the race, before the race's replay (`queue_arcade_trophy`; §18). Only a
result that improves a best place runs the unlock pass.

**Unlock pass** (`arcade_unlock_pass`):

1. **Courses.** While every course of the current group has been won at Easy, Normal
   or Hard, the course level goes up by one, so several groups can open at once.
   Professional wins never open courses.
2. **Car flags.** If every course of group *g* is won at exactly Normal, flag *g* + 1
   is set. If every course is won at exactly Hard, flags *g* + 7 and *g* + 1 are set.
   A group won partly at Normal and partly at Hard sets neither (a quirk kept from the
   game). Flags are never cleared.

`gtmode_disc_arcade_unlocks` walks through these cases.

**Clear Status count** (`arcade_completion`, 0–102 on this disc: three per course of the
`SINGLE_RACE` list). For each course and for each of
Easy, Normal and Hard, the course counts if it was won at that difficulty or a harder
one up to Hard. Winning at Hard therefore counts three times.

**Endings ("Goal").** An ending is available when a GT league (Beginner, Amateur or
Professional) is complete, or when every course has been won at one single difficulty.
Otherwise "Goal" opens `ar/cannot_ending`. The long ending needs the Amateur or
Professional league, or every course won at Normal, Hard or Professional. The Clear
Status page shows its end button once a GT league is complete or all course groups are
open. Professional is chosen by confirming Hard with L1 + R1 held.

**Time trials** (`arcade_time_trial`). There are ten challenges, `AT010001` …
`AT100001`, each driven with its Compulsory-class car.

- Each challenge has a 12-place ranking. A new game seeds it with the three record
  times of the event (+0x1C4 / +0x1C8 / +0x1CC; the first challenge's are 1'25.553,
  1'26.952 and 1'27.355).
- A run is one timed lap, whatever lap count the event gives (2 for the challenges is
  not read; `race::kTimeTrialLaps`). The race's time-trial rules end the run when that lap
  is done: the flying start's first line crossing starts the lap and the next one finishes
  it. Leaving the course fails the run (`exact_game_solo_modes`). The record overlay then
  follows as before.
- Every finished run of a challenge is recorded with its best lap as the run ends,
  before its replay, and one that makes the ranking asks for a name (§17, §17.1); the
  record's car is the challenge's base car. A run left with EXIT before its lap is
  done records nothing (§17).
- A new time is ranked by the number of strictly faster times, so it goes before an
  equal time. Rank 0 means the record is beaten and the challenge is cleared.
- The best rank per challenge is kept.
- Clearing all ten awards the `arcade_compulsory_complete` prize car once (a Panoz
  Esperante race car on this disc; `gtmode_disc_arcade_time_trials_and_save`).

**Built-in record names.** The seeded records carry no car. Their name is a one-byte
mark, 0xFF − 3*i* − *k* for record *k* of challenge *i*. A name stored as one byte of
0x80 or more is shown as entry ~byte (= 3*i* + *k*) of the 30 built-in names
("X.Surname"), which the engine reads from the game program on the disc. When the
table has no such entry, or could not be found, it shows "-". Older native saves that
stored "-", nothing, or 0xFF / 0xFE / 0xFD in every challenge get the right marks when
loaded.

**Garage classes.** The arcade car pages can also list the home garage's cars and a
loaded guest garage's cars (§19; not in Single Race), each as tuned. A garage car on a dirt course needs
dirt tires. Road classes are refused on dirt courses and the Dirt class on road
courses, and a garage class without cars is refused. Mod cars with an arcade class join
the end of that class's list (`docs/MODDING.md`).

### §16.1 Garage cars in arcade races

A garage car races as it is in the garage: its parts, settings, paint and wheel, and in
Single Race and free run its engine condition (§13.1). In Single Race the event is chosen
from the car, keeping the difficulty letter of the race code (`arcade_prepare`,
`src/game/gt_mode_menu/arcade.cpp`):

- on a dirt course, the dirt race (`A?AD0001`, `?` the difficulty);
- on a road course, by its weight per power, kg / PS: the mass of the car's menu description
  (§5) less 120 kg, over the car's computed power (§5):

  | kg / PS below | 2.1 | 3.0 | 3.5 | 5.0 | 5.8 | 6.3 | 9.0 | else |
  |---|---|---|---|---|---|---|---|---|
  | Race | S | S + 1 | A | A + 1 | B | B + 1 | C | C + 1 |

  The letter is the code's class (`A?ST0001` …) and "+ 1" its next race (`…0002`).

The race then draws its field from that event as any Single Race (§20.4)
(`gtmode_synth_hook_branches`). In 2 Player Battle each player's garage car is likewise
the car as tuned (`gtmode_menu_disc_arcade_garage_cars`).

## §17 Records and name entry

GT Mode keeps rankings (`GtMode::add_record`):

| Key | Entries | Order | Filled by |
|---|---|---|---|
| `lic:<test>` | 6 | fastest first | finished license tests with a medal; the car is the test's car |
| `mt:M400`, `mt:M1000` | 12 | fastest first | machine tests (0–400 m, 0–1000 m) |
| `mt:MMAX` | 12 | highest speed first | the top-speed machine test |
| `at:<code>` | 12 | fastest first | arcade time trials (§16) |
| `ct:<course>` | 12 | fastest first | free runs on the course: the arcade time attack and GT Mode's free run |

A new value goes to the first place that is empty or whose value is not better, so it
ranks **ahead of** an equal value, as in the time trials (§16). A value of 0 or less is
not recorded. Each entry keeps the value, the car, the GT-mode day, the name and a clock
stamp: the local time in seconds since 2000-01-01 00:00, never 0 (`record_stamp_now`;
0 means none, as in entries from saves written before the stamps).

**When a run is recorded.** A license run with a medal, a finished machine test (the
top-speed test also when a pause EXIT once its car had moved was its finish), a
finished, not failed arcade time-trial challenge and a free run with a best lap go into
their ranking as soon as the run ends, when its exit transition is over and before its
replay (`RaceScreen::record_run`, `src/app/screen_race_quick_menu.cpp`, called from
`RaceScreen::after_run`, `src/app/screen_race_post_run.cpp`; `add_license_record`,
`add_machine_test_record`, `add_time_trial_record`, `add_course_time_trial_record`).
Every such run counts, not only the last of the visit. A run that makes the ranking
opens the name entry there, before the run's replay (a license test has none) and the
quick menu (§18), and the quick menu's Top 3 is read
again when it closes. Only the race records runs. The result posted after the race
screen is left records nothing in any ranking and queues no name entry, whether it is
an arcade or a GT-mode result (time trial, free run, license test, machine test, GT
Mode's free run). This holds even when its time is better than every run's
(`gtmode_synth_machine_test_runs`, `gtmode_synth_course_time_trials`,
`gtmode_menu_disc_arcade_class_and_time_trial`). `GameResult::records_done` only marks
that the race recorded runs during the visit: an arcade result that did not finish then
still saves those records. An arcade time trial left with EXIT before its lap is
done is not recorded, and its result does not record it either. A time-trial result
that finished carries the run's best lap. Practice runs, replays and demos are not
recorded.

**Machine tests** (`GtMode::record_machine_test`): 0–400 m and 0–1000 m record the time,
the top-speed test the top speed in km/h, each with the ride car. Besides the ranking,
the machine-test page keeps the single best of each test with its car (a strictly better
value replaces it).

**Course time trials** (`GtMode::record_course_time_trial`). Each course of the arcade
`TIME_ATTACK` list (§16) has a 12-place ranking `ct:<course>`
(`GtMode::course_time_trial_index`: the course's place in the list, which is also its
PS2 save slot, §12.2). A course not in the list has no ranking, and its runs are not
recorded. The rankings start empty; there are no built-in records. A free run is
recorded however it ended, with its best lap rounded to whole ms; a run without a lap
(or with a best lap over 0x3FFFFF ms, 4194.303 s) adds nothing. The entry's car is the run's
car: the ride car in GT Mode's free run, the chosen car in the arcade time attack. Ties
rank ahead, as in the other rankings (`gtmode_disc_course_time_trials_and_save`).

### §17.1 The name entry

The name entry page's text box (`menu::flow::TextBox`, `src/menu/flow/text_box.*`) starts
with the last name entered (empty in a new game), cut to 15 characters, with the cursor
after it:

- a key inserts its character at the cursor and moves the cursor past it (a key without
  text types a space), while the name is shorter than 15 characters; a full name
  ignores keys;
- Triangle / Square is Back Space: the cursor steps one to the left and the character
  there is deleted, so at the start of the name the first character goes. It plays the
  cursor sound when the name was not empty, and never leaves the page;
- L1 / R1 move the cursor one character left / right over the name (auto-repeating),
  with the cursor sound when it moved; it stops at either end;
- Start jumps to the OK row; only OK ends the page.

The caret (the page's red "cursor" bar) is drawn at the cursor, after the text before it.
Each change is stored at once as the record's name and as the last name entered, so the
next name entry starts with it (`name_backspace`, `name_cursor` in
`src/game/gt_mode_menu/presentations.cpp`). The memory card's file-name entry uses the
same text box (`GameFlow`, `src/menu/flow/game_flow.cpp`).

### §17.2 How records are shown

**Race quick menus.** Time-trial, machine-test and free-run quick menus show the
ranking's Top 3 (`RaceScreen::fill_top3`; a free run its course's `ct:` ranking), read
again before every quick menu, so before the first run and after each. An arcade time
trial with no ranking yet shows its seeded records (§16).
There are always three rows; a place with no entry shows no name, no time ("--'--.---" in
the font's long dashes, presentation §D10) and a speed of " 0.0 km/h" (or " 0.0 mph"). A top speed is printed "%4.1f %s", rounded to tenths, in
the race screens' unit (the game data's, mph on NTSC-U, km/h × 0.62139; the `[race]
units` setting, Settings → Display → Speed units, overrides it): 200 km/h reads
"124.3 mph". The free time trial's list shows each entry's car name in its car column,
"---" for an entry without a car. The
quick menu's Records item opens the run's ranking on the records page (a free run: its
course's).

**Records page** (`draw_records`, `src/game/gt_mode_menu/extra_draws.cpp`). The page draws
every place of the table, 12 (a license test's 6), each with its rank, in a list that
scrolls: a license test's six places fill the window, and the twelve scroll. The cursor
starts on the first place each time a ranking is opened, and its bar is drawn
highlighted. Up / Down move it over all the places, stopping at the ends, with the
`select` sound, and the list scrolls to keep it in view; a hovering pointer moves it
too. The bars are 60 apart (layout: menus §9.7; `gtmode_menu_disc_vertical_lists`). A place with no
entry has no name, the time "--'--.---" (a top speed "---.-mph") and the date "--/--/--",
each dash the font's long dash, character 0x7F (presentation §D10). Columns:

- the name as stored (a built-in record shows its built-in name, §16);
- the value: a time as m'ss.mmm (from an hour on h:mm'ss.mmm, presentation §D10); a top speed (`mt:MMAX`) in the game data's unit with
  whole units and tenths both truncated and no space, so 200 km/h reads "124.2mph"
  (`race::record_speed_text`, `src/race/machine_test.cpp`);
- the date from the stamp, "DD/MM/YY" on the US data (`record_date_text`); an entry
  without a stamp shows "--/--/--". The dates follow the game's calendar, in which only
  2000 has a 29 February: in later leap years every date from 29 February on reads one
  day later ("01/03/28" for 29 February 2028, "32/12/28" for 31 December 2028;
  `gtmode_record_dates`);
- on the machine-test and course time-trial pages only, the car icon and the car's name
  ("<name> <second name>", nothing for an entry without a car). License and arcade
  time-trial pages show no car.

**The race's record table.** A license test, machine test or arcade time trial started
from the menus (not a practice run) reads its ranking, up to 12 entries, as the run's
stored records (`RaceScreen::records_key`, `stored_records` in
`src/app/screen_race_quick_menu.cpp`): `lic:<test>`, `mt:<code>` or `at:<code>`. An
arcade time trial named by its course instead of its challenge uses that course's
challenge ("tt01" → `AT010001`). At the finish a time trial or machine test is placed in
that table for the result overlay (`race::record_rank`): the first place that is empty or
whose entry is not better, so a tie ranks ahead of the stored entry, as in the ranking
itself; past the 12th place it has none. Times rank lowest first, the top-speed test
highest first. A free run's stored records are its course's ranking (`ct:<course>`); its
finish is a pause EXIT after at least one lap (§22.4), and its best lap is placed the same
way. Races and runs without a GT-mode session have no stored records. No stored record
takes part in the session record that locks RETRY (presentation page §E3) or in the lap
and split gaps of time trials and free runs: both are measured against the session's
best lap, and the first lap of a visit has no gap.

## §18 Prize presentations

A run and its result can queue presentation pages. They are shown in this order:

1. the race prize cup or consolation prize of a finished GT race or championship round
   (presentation description §F; `queue_race_prize`), or the arcade single race's win
   cup (§16; `queue_arcade_trophy`). The race shows it right after the run, before the
   race's replay. A run that did not finish (left with pause EXIT) gets none but still
   replays. When the race did not show it (`GameResult::prize_shown` unset), the result
   queues it ahead of the pages below;
2. name entry (§17.1; queued by the race, not by a result: after a time trial, free run
   or machine test it runs before the run's replay, after a license test, which has no
   replay, before the quick menu, §17);

The race then plays the run's replay (none after a license test) and opens its result
or quick menu. The pages below come after the race screens, when the result is posted:

3. the license card;
4. the championship trophy;
5. each prize car;
6. "Garage is full" (§18b).

Times are in frames at 60 Hz, counted from the page's first frame. The model drawn on
each page is the disc's prize scene file (`.prm`) with its own camera, lights and
animation. The animation starts when the model begins to fade in.

**License card** (`license_get`, `menu/lic_<b|a|ib|ia|s|d>.prm`):

- the model fades in over frames 120–180;
- message 0 shows until frame 240;
- message 1 shows from frame 360;
- OK is accepted from frame 420.

The card is drawn in six passes, each with a small sub-pixel offset. When the scene's
focus distance is above 1, each pass also moves the eye around a circle of the scene's
aperture radius (depth of field). Pass *i* is blended over the passes before it at
1 / (*i* + 1), so every pass counts equally in the average.

**Championship trophy** (`trophy_get`, `menu/champ_NN.prm`, NN = the picked trophy
00–09, §20.3):

- message 0 shows for 180 frames;
- the model fades in over frames 180–270;
- the bonus box appears at frame 270;
- from frame 360 the credits count up by the bonus, using the same prize counter as
  the race prize pages (presentation description §F; `step_prize_count_up`,
  `src/game/prize_count_up.h`). The counter waits 360 frames instead of the race pages'
  120, and its first step comes two frames after that. It runs in single precision, but
  rounds to nearest where the console truncates, so the figures arrive on the same frames
  up to at least 100,000 Cr, while a 2,000,000 Cr count differs in a few middle digits on
  some frames (`docs/dev/GT_MODE.md`). The first Confirm
  during the count triples its speed for the rest of the count, and a second Confirm
  completes it. Presses during the wait are ignored. The count beeps (SE `cursor`) on
  the frame of a step, never on the step that finishes (`gtmode_synth_prize_counter_steps`);
- 15 frames after the count ends, and not before frame 345, message 1 and OK appear.

The trophy is drawn in four passes. It sits 160 pixels left of the page center, an
offset the code marks as measured rather than confirmed. Winning a trophy sets its
prize-model flag (§3.2).

**Prize car** (`car_get`):

- the reason line comes from the prize set (table below);
- for a set with several candidates, the candidates flash by, a new one every 6
  frames, for 150 frames (2.5 s), and then the car won is shown in its paint.

The reason line is a hidden text element of the `car_get` page, chosen by the prize set
(`prize_reason`):

| Prize set | Reason element (text on NTSC-U where known) |
|---|---|
| license `L??0000` | `license_<id>_all_gold`, id b, a, ib, ia, s or r ("B License all gold") |
| a series' `_0000` set | `solo_race_clear` ("Event Winner") |
| endurance `E…` | `enduro_win` |
| championship (also `arcade_compulsory_complete`) | `championship_win` ("Championship winner") |
| `gtmode_*_complete`, `gtmode_complete` | the set's name; `gtmode_ama_complete` for the amateur league |
| `achieve_half` / `_3_quoter` / `_complete` | `achieve50` / `achieve75` / `achieve100` |

### §18b Garage is full

A prize car that would be the 201st car is not given (§23). After the result's other
pages, one `not_car_get` page ("Garage is full") appears, however many cars were
refused. A single-car prize refused this way is not marked as given, so it can be won
again once there is room. The random state is still stirred (§20.3).

## §19 Trade and setting files

**Trade.** Home → Trade loads a guest garage from any save file, native or PS2. Its
cars are shown in the garage list (§5) and can be viewed and copies bought
(`GtMode::can_buy_guest_car`, `buy_guest_car`):

- **Refusals, in order:** models with the cannot-sell flag (bit value 2: the S2000
  variants, Stratos, 996 GT3 and other prize-only models) give `not_for_buy`; then the
  money (the model's catalog price) gives `no_money`; then a full garage gives
  `max_car`.
- **The copy** keeps the car's parts, owned goods, settings, paint and condition. It
  gets a new serial and the stock wheel, because the wheel is not in the buyer's
  inventory.
- **The friend's save is not changed** (`gtmode_disc_trade`).

The arcade "Load Garage" loads the same guest garage for the guest-garage class (§16).

**Setting files.** "Save Settings" stores the ride car's committed settings with its
fitted parts, its reference final drive and the day, as a new file or over a chosen one
(which then counts as the newest). The save page sets no limit per car. "Load Settings" applies a
file only to the same car model (`apply_setting_file`). It copies each settings group
only when the file's part for that group is the part fitted now:

| Part | Group |
|---|---|
| transmission | gears, final drive, auto-level, reference final |
| drivetrain | front torque split |
| brake controller | brake balance |
| racing modification | downforce |
| turbine kit | turbo words |
| suspension | all suspension settings |
| LSD | LSD words |
| stability control | ASCC |
| traction control | TCS |

If any group is skipped, the `load_warning` popup appears. The original game's
memory-card setting file (`BASCUS-97102S…`) can be imported and exported: its header
version is 1.0x827 and its payload is one 0x1D0-byte car setup record, with the same
inversion and CRC as the game data (`import_ps2_setting`, `export_ps2_setting`;
`gtmode_disc_setting_files`).

## §20 Randomness

### §20.1 The generator

GT Mode's draws use one generator algorithm (`base/game_random.h`) on several independent
states (the race's car effects also use a second generator, `Vu0Random`, from the same file):

  x ← 17 x + 17 (mod 2³²),  output = x ⊕ rotl₁₆(x)

The **unit** draw takes the low 23 output bits as a mantissa, giving [0, 1) in steps
of 2⁻²³. Each draw steps the state once. The states are:

- the shared library state;
- the race module's state;
- the GT-mode state;
- per-object states (cameras, the replay director).

Except for the GT-mode state, all of them start at the boot seed 0x87654321.

### §20.2 The GT-mode state

- **At start-up** the GT-mode state is a hardware random value (0–47), stepped once.
- **A new game keeps the running state.** The state is saved with the game.
- **The menus step it once per frame** while the GT-mode, settings and records menus
  run (`on_frame`). The arcade menus do not step it.
- **A stir before each draw.** The trophy page and the prize-car page open with a stir
  of (hardware value + shared state's integer draw) mod 13 steps, with 0 counting as 1,
  so 1–12 steps. The integer draw is int(unit × 2³¹).

In the game this value changes constantly. The engine takes it from the
high-resolution clock modulo 48, so the draws are unpredictable in the same way but
not the same values. With the same states and the hardware source fixed, every draw
equals the game's (`random_gtmode_disc`, `random_stir_range`).

The engine draws when the result is recorded, so the frames spent on the presentation
pages step the state after the draws rather than before. That changes only which
values come out, not their distribution.

### §20.3 Draws on the GT-mode state

The draws are made in single precision with the console's rounding:

- **Prize paint** (a candidate without a fixed color in its model's list): int(*n* ×
  u + 0.5) for a list of *n* paints, with *n* mapped to 0. Every paint is equally likely.
- **Prize car pick:** weights (at least 1) are summed in list order. The first
  candidate whose running sum reaches total × u wins.
- **Championship trophy** (`pick_trophy_index`): ten trophies with base weights

  | Trophy | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
  |---|---|---|---|---|---|---|---|---|---|---|
  | Weight | 400 | 350 | 250 | 450 | 2 | 160 | 1 | 500 | 25 | 120 |

  Each weight is increased by 165.333 c³ for completion c (§4), and the pick uses the
  same running-sum rule. At c = 0, trophies 4 and 6 are rare (about 0.1 % and 0.04 %).
  At c = 1 each is about 4 % (`random_prize_and_trophy_draws`,
  `gtmode_disc_trophy_pick`).

The order of draws matters for reproducing the game:

- **Prize cars.** One award check stirs once, before the first eligible prize set, even
  when the garage is full. Then, for each set, every candidate's paint is drawn in list
  order (candidates with a fixed color draw nothing), and then the pick is made.
- **Championship win.** The trophy (stir, then pick) is drawn before the prize-car
  award, as the trophy page comes before the car page.

### §20.4 Opponent draw

The opponents come from the event's **entry pool**: the car variant labels at +0x10,
up to the first "-", at most 32. They are drawn with the **race module's** state
(`race::draw_opponents`, `src/race/pool_draw.cpp`), which also gives the race's paint
colors. For each of the *k* cars to draw:

1. index = trunc(u × pool size);
2. move forward from it, wrapping round, to the first entry not taken yet;
3. mark that entry taken.

Entries not sold in the region count as taken from the start. The test is the base
car's +0xF7 byte, as in §16 (a missing base car is unavailable too). When no free entry
is left, further draws are plain random indices and may repeat. (When unavailable
entries cause this, the game hangs; the engine repeats instead.)
On this disc no pool entry is region-locked (`random_gtmode_disc`).

Who draws:

- **A GT-mode race** draws slots 1 to cars − 1 afresh each time (slot 0 is the player,
  cars = event +0x1B5; `GtMode::pick_opponents`).
- **A championship** draws its field once, from round 1's event, and keeps it for every
  round (§22.2).
- **Arcade Single Race** draws the same way (`screen_race_launch.cpp`).
- **The attract demo** draws every slot (§15).

## §21 Licenses

There are six licenses, each with 8 tests: B, A, IB, IA, S and R (rally; menu id `d`).
Test codes are `L0B000t`, `L0A`, `LIB`, `LIA`, `L0S` and `L0R`, with *t* = 1–8.

**Access** (`can_open_license_page`, `can_take_test`):

- B and R are always open. A needs B; IB needs B and A; and so on up to S, which needs
  B, A, IB and IA.
- A page whose license is not yet reachable opens `lc/no_license_<id>`.
- Test 8 needs a medal in tests 1–7 (`lc/no_last_exam`).

**Judging.** The race judges the run on its own clock (`docs/dev/RACE.md` "Licence tests";
`exact_game_solo_modes`): its time in whole ms is compared with the event's gold / silver /
bronze targets as the disc being played stores them (+0x1C4 / +0x1C8 / +0x1CC, minutes,
seconds and ms). At or under gold gives gold, and so on. A run slower than bronze ends as
TIME EXPIRED: it finishes with its time but no medal, so it records nothing here. A run that
leaves the course (or hits hard) fails with no time. The rules follow the disc's release: on a PAL disc a stop
test's car counts as stopped at up to 1.5 km/h instead of 1 km/h. B-1's targets are
35.100 / 35.400 / 35.800 s on this disc (`gtmode_disc_licenses`). GT Mode takes the race's
medal and logs any difference from its own judgment (`GtMode::judge`: an unfinished test
gets no medal, otherwise the same comparison on the time rounded to whole ms). A visit
posts its best run: a retry that does worse keeps the medal already earned.

**Recording** (`record_license_medal`):

- a medal sets its award bit (§3.2);
- the best medal and the best time of the test are kept;
- when the eighth medal of a license arrives, the license is **obtained**. The license
  card is presented and its prize-model flag set;
- when every test of a license is gold and the new medal is gold, the license's prize
  set (`L0B0000`, …) is checked (§23). The B all-gold car is a Roadster RS on this disc.

The **highest license** (status page, file list, thumbnail) is the highest of B … S
held. Rally does not count. A license test drives the event's entry-0 car in the
event's slot-0 color, not a garage car. A finished test with a medal also enters the
license ranking (§17).

## §22 Races, championships and endurance

### §22.1 Series and single races

The race pages are `r/<tier>/<series>`. On this disc there are 85 series:

| Tier | Series |
|---|---|
| beginner | 20 |
| amateur | 23 |
| professional (`pro`) | 22 |
| rally (`dirt`) | 10 |
| endurance (`end`) | 10 |

Of these, 27 are championships: 6 beginner, 11 amateur and 10 professional. A race
code is the event label, for example `Gsunday_e_0001`. The race information page shows
the six prizes (a prize of 0 as "0") and the rules (§8).

Each race fields the event's car count (6 for races). The opponents are a fresh draw
(§20.4). The result is recorded as in §3.4. GT Mode lines the opponents up by
power-to-weight with the strongest car on pole, and the player starts behind them; the
grid itself belongs to the race description.

After a race, the ride car takes back the engine condition the race advanced (§13.1),
and its dirt is set from the race (§13). Before the race the pre-race menu offers a free
practice (§22.5).

**Results on the race pages** (`widget_draws`; tests `gtmode_synth_race_and_prizes`,
`gtmode_synth_championship`). Each placed result shows its best place as one of the hidden
`mark_1` … `mark_6` images.

- A series page has one `event_status_buttonNN` widget per race button: NN counts the
  page's buttons in order (00 is the race information), and the widget's own box is not
  used. It shows the best place under its button's own code, and, for a race, the course
  logo (the hidden image named after the course). Each image keeps its own size and its
  layout position, taken as if the button's top-left corner were the page's: on the disc's
  pages the mark lands in the button's round slot (61 units in, also on the wider last
  round of a championship) and the logo in its dark slot. A championship's own button
  (SERIES RACE) shows the championship's result, which is only ever place 0 (§22.2), in
  its laurel, and no logo. Buttons without the focus draw them at half brightness; they
  fade in with the page.
- A league page's `event_statusN` widget belongs to the item whose focus layer is `s_N`,
  named after the series' first code (`Gsunday_e_0001`) or the championship's own code
  (`Gtt_e_0500`). The codes from that one on, while they are result codes, each get a slot,
  every image at its own size centered on the widget's height. With f the hidden `frame`'s
  width and a `frame_arrow`'s (31 and 43 on the disc) and n codes: a series of races has a
  `frame` and its mark per race, centered at x = c − n(f + 4)/2 + f/2 + i(f + 8) for the
  widget's center c; a championship has its own `frame` and mark at L + f/2, with
  L = c − (2f + (n − 1)a)/2, then from L + 2f a `frame_arrow` per round (its mark f/2 in,
  over the arrow's disc) and a plain `frame` for the last round. These fade in with the
  focus layer.

### §22.2 Championships

A championship series has a `set_race_code_champ` item named after its own code
(`…_0500`, `_0700` or `_1000`). Its rounds are `…_0501`, `…_0502` and so on.

1. **Start.** Choosing the championship starts round 1, the item's code + 1. It costs
   one day (§2). The field, cars − 1 opponents, is drawn once from round 1's pool, and
   every entrant's points start at 0 (`start_championship`).
2. **Each round** pays its own place prize, sets cup bits and keeps best places like a
   single race, but gives no prize car (`record_round`). Points of 10, 6, 4, 3, 2 and 1
   go to the first six of the finishing order. If only the player's place is known,
   the others fill the remaining places in entry order. The next round follows at once,
   without returning to the menu.
3. **The end.** After the last round the player is **champion** only with strictly more
   points than every opponent; a tie loses. The champion:
   - gets the last round's bonus (event +0x1E2 × 100 Cr) on top of the round's prize;
   - wins a trophy (§3.2, §18);
   - has place 0 recorded for the championship's own code;
   - gets the prize check for that code (§23).
4. **Saving.** A championship in progress is saved between rounds, and a round's "Save
   game" saves a copy with that round in it (§12.3). Entering GT Mode
   resumes it with the next round.
5. **Leaving.** A round left with pause EXIT still counts: the race is counted, the
   player gets no prize and no points, and every other car takes the points of its place
   in the order (no one moves up). Answering yes to "Retire from championship?" ends the
   championship: a round already run keeps its place, prize and race count; leaving a
   round's pre-race menu records nothing for that round. Only the engine condition the
   runs advanced goes back into the car (§13.1). The standings are dropped, so the title
   needs a new championship from round 1. After the last round EXIT is locked
   (`gtmode_synth_result_rules`).

`gtmode_disc_championship` runs the five Altezza rounds with a win and with a loss.
The beginner Altezza title pays a 10,000 Cr bonus (log: "Champion! bonus 10,000 Cr.").

### §22.3 Endurance races and pit stops

The endurance tier holds ten single-race series (`E…_n_0001`). All of them have 6 cars
and trophy class 7. Their first prizes run from 100,000 to 500,000 Cr on this disc.
Two of them, Rome 2 h and Trial Mountain 2 h, have no lap count and a 120-minute time
limit instead (event +0x1E4). Endurance races use a rolling start.

GT Mode records an endurance result exactly like a single race (§3.4). Each endurance
race is its own prize set with several candidates, so every win rolls a prize car
(§23). Winning all ten completes the endurance league (`gtmode_enduro_complete`).

Tire wear, the pit lane and pit stops (the computer drives the car through the lane and
fits fresh tires) are race behavior, described in `docs/dev/RACE.md` "Pit stops and tyre
wear" and the physics page §8. From GT Mode's side, wear is on in single races and free
practice on courses that are not dirt, using the event's wear bytes.

### §22.4 Machine tests and free runs

Machine tests (`M400`, `M1000`, `MMAX`) use the ride car. Starting a test costs one day,
however many runs are driven before leaving it (`gtmode_synth_machine_test_runs`). Every
finished run updates the best of its test and the ranking (§17). In the top-speed test a
pause EXIT once the car has moved, before its goal, is the run's finish (a second EXIT
ends the run; `race::pause_exit_finishes`, `race_pause_exit_finish`). The car's engine
condition is handed back (§13.1). A free run on a course also costs a day. It needs dirt
tires on a dirt course and hands the condition back in the same way. Its pause EXIT after
at least one lap is the run's finish, with the record overlay. Each free run with a best
lap goes into its course's time-trial ranking (§17).

A free run's best lap and its ghost car last the whole visit, through every restart, until
the player leaves. The best lap of the visit becomes the ghost (at once with the Ghost
option on, otherwise after "Update Ghost?"), and at each line crossing the ghost car is put
back on the start of that lap and drives it again (`exact_game_free_run_visit`). A visit's
first run starts without a ghost and gets one from its own laps, as above.

### §22.5 Free practice before a race

The pre-race menu of a GT race or championship round has FREE RUN, a free practice on the
race's course: the ride car alone, no opponents and no lap count (`RaceScreen::start_free_run`,
`src/app/screen_race_quick_menu.cpp`). The car starts in its pit box on the course's pit stop
line and the computer drives it out of the pits (physics page §9.4); it then runs lap after
lap, keeping the session's best lap (`exact_game_solo_modes`). Tire wear and pit stops work
as in the race (§22.3). Pause EXIT returns to the pre-race menu with the race's field set up
again. Free practice costs no day, records nothing and has no replay; the engine condition and dirt it leaves carry on into the race (§13.1).

- On a dirt course the menu turns FREE RUN off (`race::QuickMenuItems`).
- On a course without a pit box (a pit stop line with fewer than 4 points,
  `race::exact_has_pit_box`) free practice is refused: the message "Free run is not
  available on this course." appears and the pre-race menu stays. GT Mode's courses
  without a pit box are its dirt courses (they have no pit stop line), where FREE RUN is
  already off, so GT Mode's own races never show the message.

## §23 Prize cars

The prize sets are in gcf section 15. On this disc there are 101 sets with 211
candidate cars. 37 sets have several candidates (all 27 championships and all 10
endurance races), and 64 have one. Each set is a name and a list of {car, color id,
weight}.

**When a set is eligible** (`GtMode::award_prizes`). A check is made:

- after a race win, with that race's code;
- after a championship title, with its own code;
- after a gold medal in a license whose eight tests are then all gold, with the
  license's set name;
- after every arcade time-trial run the race records (§17).

Every set is examined each time:

| Set | Eligible when |
|---|---|
| named exactly as the code being checked: a championship, endurance race or license set | the code is won (place 0); for a license set, all 8 tests gold |
| `<series>_0000` (a non-championship series) | every race of that series is won |
| `gtmode_beginner/amateur/pro/dirt/enduro_complete` | the code being checked is of that league (a `G…` code by `_e_`, `_n_` or `_h_`; `E…` endurance; `D…` rally; `GtMode::code_league`) and the league is complete (§10) |
| `gtmode_complete` (the whole game) | the code being checked is a GT-mode code, and every GT-mode result code (gcf section 4: 364 on this disc) is won (`all_races_won`) |
| `achieve_half`, `achieve_3_quoter`, `achieve_complete` | completion ≥ 0.5 / 0.75 / 1.0 (§4) |
| `arcade_compulsory_complete` | all ten time trials cleared (§16) |

**How often.** A set with one candidate is given once per save; its name goes into
"prizes given". A set with several candidates is given only when its own code is being
checked, and then on every win. So every championship title and every endurance win
rolls a car, while league and achievement sets come once.

**The whole game's set.** A GT-mode code (`GtMode::gt_mode_code`) is a series race or
championship (`G…`), an endurance race (`E…`), a rally race (`D…`), a license test or
license set (`L` with B, A, R or S as its third character: `L0B…`, `LIA…`, `L0S…`,
`L0R…`), or a machine test (`M400`, `M1000`, `MMAX`). Arcade codes (`A…`) and anything
else are not. So `gtmode_complete` can come with a race win, a championship title or a
license turning all gold, never with a time-trial run. It is not the 100 % completion
rule (`achieve_complete`, §4), which also needs the arcade and the licenses. On this
disc the set has one car, the F688/S (label `mclaren_mp4_4`), with no fixed color, so
it comes once, in a random paint (`gtmode_disc_game_complete_prize`,
`gtmode_synth_game_complete_prize`).

**Giving the car.**

- The candidate's paint and the pick follow §20.3.
- The car is new: stock parts, stock settings, running-in condition.
- It goes to the head of the garage, the prize-car count goes up, and the set is
  recorded as its origin.
- If the garage is full the car is not given (§18b).

The Sunday Cup's set gives a Sprinter Trueno GT-APEX once, the first time all its races
are won (`gtmode_disc_races_and_prizes`, `gtmode_menu_disc_buy_and_race`).

---

## Open questions

- The engine takes the hardware random value from the clock (§20.2). The game's exact
  timing of that value is not modeled.
- The championship trophy's 160-pixel offset and the options-block defaults beyond the
  screen mode, sound switches and music list are marked in the code as measured rather
  than confirmed (§12, §18).
