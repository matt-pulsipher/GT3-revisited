# Game data tables (`database/`) 🔎

Code: `src/formats/db/gt_db.*` (containers), `src/game/data/car_database.*`
(typed records, lookup API), `tools/gt3tool/cmd_db.cpp` (`gt3tool db`,
`gt3tool car`), screen `database`, tests `tests/test_db.cpp`.

Confidence tags used below:

- **[spec]** confirmed by how the game uses the field (when it builds a
  car's physics parameters, the "car spec", or applies GT-mode and race
  rules) and checked against the original's observable behaviour. The code
  that reads these fields is `src/sim/car_spec.cpp` (physics) and
  `src/game/gt_mode.cpp` (rules).
- **[data]** confirmed by surveying all records and matching real-world
  values (e.g. the R34 GT-R, NSX, Civic Type R catalogue figures).
- **[guess]** plausible but unconfirmed.

All values little endian.

## Files

| File | Format | Loaded by the game | Contents |
|---|---|---|---|
| `paramdb{,_us,_eu}.db` | GTAR of 36 GTDT | `paramdb_us.db` | all car / part / race tables |
| `paramstr{,_us,_eu}.db` | STDB | `paramstr_us.db` | ASCII strings: labels, `"2568cc"` |
| `paramunistr{,_us,_eu}.db` | STDB | `paramunistr_us.db` | display strings in game text encoding (ASCII + EUC-JP, see text.md): car names, makers, `"6800rpm"`, `"TURBO"` |
| `.id_db_idx*.db` / `.id_db_str*.db` | IDDB / STDB | **not loaded** | id code → label, for every label used anywhere |
| `racemode.db` | GTDT (standalone) | yes | 16 race modes, each with its display layout |
| `racedetail.db` | GTDT (standalone) | yes | 228 race display (HUD) layout rows (labels not in any IDDB) |
| `carcolor.db` / `carcolor.sdb` | GT2K / STDB | yes | paint colours per car model |

This disc (PBPX-955.03) loads the `_us` set. The three regional sets have
the same table layout; `_us`/`_eu` have a few more part records (e.g. 244
vs 242 cars). Coverage: all 9 GTAR/GTDT/STDB/IDDB files of each set parse,
and every record of all 36 tables decodes (`gt3tool db`).

## Id codes [spec+data]

Records refer to each other by a 64-bit code computed from the label:

```
v = (sum of all bytes of the label) & 0xFFFF
for each byte b: v = rotate_left_64(v, 7) + b
```

Verified for all 20,714 / 20,899 / 20,909 labels of the three IDDBs. The
game only compares codes; labels are needed only for display/debugging. The
label `"-"` (`0x16AD`) marks an empty slot; `"0"`/zero code = no part.

## Containers

**STDB** (string table): `"STDB"`, u32 count, u32 flags (1 or 0xFFFF;
meaning unknown), u32 file size, u32 offsets[count]; each string = u16
length, bytes, NUL, padded to even. Lengths of `paramunistr` strings include
trailing NUL padding. Strings are sorted by length then bytes in the id
string tables.

**IDDB**: `"IDDB"`, u32 count, then count × {u64 code, u64 string index},
sorted by code.

**GTAR**: `"GTAR"`, u32 table count (36), u32 data offset (0xA8), u32 7
(GT3 clears these low bits of an offset; the discs' offsets are 16-byte
aligned), u32 offsets[count + 1] relative to the data offset. GT3 reads an
archive by its offsets alone, so the reader does not check the tag
(a modified game's tools write `"GTAr"`).

**GTDT**: `"GTDT"`, u16 version (0x0827 in paramdb, 0x0804 standalone),
u16 table index (= position in GTAR; 0xFFFF standalone), u16 record count,
u16 record size, u32 byte size (16 + count × size), records. Every record
starts with its u64 id code. The game looks records up by (table, code).

## Part records — common layout [spec+data]

Tables 0, 1, 4, 7–21, 28, 29 hold purchasable parts:

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u64 | id, label `<prefix>_<car>_<stage letter>` e.g. `tk_r34_gtr_v_99_c` |
| 0x08 | u64 | car this part fits (car label) |
| 0x10 | u8 | stage: 0 = stock; 1.. = sports / semi-racing / racing ... |
| last 4 | u32 | price in credits (tires: at 0x14; ×100 in Japanese) |

Prices match the in-game shop (e.g. R34: turbo stages 4,800 / 13,500 /
42,500 / 80,000; lightweight 1,200 / 5,500 / 22,000).

Many tuning parts store **adjustable settings** as (steps, min, max) byte
triplets. The game computes
`steps < 2 ? min : min + (max - min) * (setting - 1) / (steps - 1)` with the
user's setting (1..steps) from the car's settings (the values the GT-mode
Settings pages edit, stored per garage car).

## The car spec [spec]

The game builds a compact per-car parameter block (the "car spec";
`sim::CarSpec`, built by `build_car_spec()` in `src/sim/car_spec.cpp`) from
the car's installed parts plus user settings. It looks up each installed part
and copies/scales fields. Two multipliers start at 1000 (‰, the
"power factor") and 100 (%, a second factor); engine-upgrade parts multiply them:

| Table | Effect |
|---|---|
| 7 port polish, 9 displacement, 10 computer, 19 intercooler, 20 muffler | u8 +0x11 → ×% power factor, u8 +0x12 → ×% second factor |
| 8 engine balance, 11 NA tune | u8 +0x11 added to every rpm-axis byte (shifts the torque curve up), u8 +0x12 added to engine +0x45; u8 +0x13 / +0x14 → ×% factors |
| 12 turbo kit | u16 +0x1C → ×% power factor, u8 +0x1B → ×% second factor, u8 +0x11 → turbo flag and boost cap (cap = 1 + 0.1 × byte; 0 = NA), u8 +0x18/+0x19 added to engine +0x45/+0x46; +0x12..+0x17 the stock boost settings (peak, knee, rate for each of two stages); the race uses the user settings |
| 14 flywheel, 15 clutch, 16 propeller shaft | u8 +0x11.. → ×% the drivetrain u16s (+0x16, +0x20, +0x18, +0x1A, +0x1C, +0x1E); clutch +0x15 → copied into the car spec (meaning unknown) |
| 4 lightweight | u16 +0x12 → ×‰ weight; u8 +0x11 → ×% chassis byte +0x1C |
| 5 racing modify / body | see below |

## Table list (paramdb)

| # | Name | Rec | Label prefix | Notes |
|---|---|---|---|---|
| 0 | brake | 24 | `br_` | +0x11 / +0x12 front / rear maximum brake torque per wheel, +0x13 handbrake torque (all ×98 N·m) [spec]; stage 1 = racing brakes |
| 1 | brake controller | 32 | `bc_`, `a`, `n` | ABS strength: two (steps,min,max) triplets, front (+0x11 steps, +0x12/+0x13) and rear (+0x15, +0x16/+0x17); the setting's value ×0.1 is the ABS gain; +0x14 default setting [spec] |
| 2 | steer | 64 | `n`, `n1` | +0x11 steering lock, degrees; +0x1E..+0x23 six speeds (km/h) and +0x24..+0x29 six steering limits (×0.2°) of the speed-sensitive pad steering curve [spec]; the other bytes up to +0x35 are copied, meaning unknown |
| 3 | chassis | 32 | `ch_` | see below |
| 4 | lightweight | 24 | `lw_` | stages 1–3: +0x11 u8 %, +0x12 u16 ‰ (930/890/850) [spec] |
| 5 | racing modify / body | 72 | `rm_` | every car has one (stock body); see below |
| 6 | engine | 88 | `en_` | see below |
| 7 | port polish | 24 | `pp_` | |
| 8 | engine balance | 32 | `eb_` | |
| 9 | displacement up | 24 | `ds_` | only 13 cars |
| 10 | computer | 24 | `co_` | |
| 11 | NA tune | 32 | `nt_` | stages 1–3 |
| 12 | turbine kit | 40 | `tk_`, `na` | stage 0 = stock turbo; `na` = no turbo |
| 13 | drivetrain | 40 | `dt_` | see below |
| 14 | flywheel | 32 | `fw_` | |
| 15 | clutch | 32 | `cl_` | |
| 16 | propeller shaft | 32 | `ps_` | |
| 17 | gear | 48 | `ge_` | see below |
| 18 | suspension | 128 | `su_` | see below |
| 19 | intercooler | 24 | `ic_` | |
| 20 | muffler | 24 | `mu_` | |
| 21 | LSD | 48 | `ls_` | +0x11 / +0x1B front / rear differential type (an ASCII letter: `f`, `h`, `m`, `n`, `v`, `y`); default initial / acceleration / deceleration settings +0x12/+0x15/+0x18 (front), +0x1C/+0x1F/+0x22 (rear); the race uses the user settings [spec] |
| 22 | TCSC | 32 | `a`, `off`, `evo` | traction control: level (steps +0x12, min +0x15, max +0x16, %; 0 and 100 = off), +0x11 default setting, +0x13 gain (×0.1), +0x14 / +0x17 lateral-acceleration terms (×0.1 / ×0.01) [spec] |
| 23 | ASCC | 48 | `a`, `ne`, `off` | stability control: four adjustable gains (steps,min,max % at +0x11/+0x14/+0x15, +0x18/+0x19/+0x1A, +0x1E/+0x1F/+0x20, +0x23/+0x24/+0x25; default settings +0x12, +0x1B, +0x21, +0x26), thresholds +0x13 and +0x22 (×0.01), 2-bit mode flags from +0x16, +0x17, +0x1C, +0x1D [spec] |
| 24 | wheel | 16 | `bbw004_g` ... | rims; +0x08 u8 (100), +0x0C u8 (weight? 86–135) [guess]; not used by the car spec |
| 25 | tire size | 16 | `sz001`.. | u8 +0x08 rim diameter (inches), +0x09 aspect ratio / 5, +0x0A section width / 5 mm (sz053 = 225/45R17) [data: all 125 decode to real tire sizes]; sets the tyre radius and width per axle [spec] |
| 26 | tire force vol | 24 | `n` | single record; +0x08..+0x11 and +0x12..+0x14: grip in % of the compound's grip on road surfaces 0–9 and 12–14 (0 = 100 %) [spec] |
| 27 | tire compound | 88 | `normal`, `sports`, `hard`, `soft`, `shard`, `ssoft`, `f1`, `dirt`, `arc_*` ... | +0x08 peak grip µ ×0.01, +0x09 slip-angle range (degrees), +0x52 wear rate (×0.01); +0x0A..+0x51 are the grip curves the tyre model samples (lateral slip, longitudinal slip, load sensitivity) [spec]; individual curve points not documented here |
| 28/29 | front / rear tire | 96 | `ft_`, `rt_` | +0x14 u32 price, +0x18 tire size id, +0x20 4 × compound id, +0x40 4 × force-vol id; the car spec uses the first three of each [spec] |
| 30 | car | 264 | car label | see below |
| 31 | car variant | 272 | `ar_`, `dm_`, `gv300_`, `pdi_`, `trb_e_` ... | opponent / prize cars: base car + parts + settings |
| 32 | event | 504 | `Gsunday_*`, `ANAT0002`, `L_*`, `M400` ... | see below |
| 33 | regulation | 264 | `reg_*` | 32 × u64 allowed car labels (`-----` = empty) [data] |
| 34 | course | 16 | `rome`, `r_rome`, `L_rome1` ... | see below |
| 35 | arcade car | 272 | `arm_*`, `<car>_<n>` | same layout as 31; referenced by `gameconf/product.gcf` arcade classes [data] |

### Car (table 30, 264 bytes) [data]

| Off | Type | Meaning |
|---|---|---|
| 0x00 | u64 | car label, e.g. `r34_gtr_v_99` |
| 0x08 | 26 × u64 | default (stock) parts, in this order: brake, brake controller, steer, chassis, lightweight, racing modify (body), engine, port polish, engine balance, displacement, computer, NA tune, turbo, drivetrain, flywheel, clutch, propeller shaft, LSD, gear, suspension, intercooler, muffler, front tire, rear tire, ASCC, TCSC (0 = none) |
| 0xD8 | u64 | always 0 (wheel slot? [guess]) |
| 0xE0 | u16, u16 | maker id (two copies; e.g. Audi 3, Mazda 20) |
| 0xE4 | u16 | name → paramunistr (`"SKYLINE GT-R V・spec(R34,J)"`), shown in US English, FR, GR, IT, SP |
| 0xE6 | u16 | second name string (mostly blank) |
| 0xE8 | u16 | label → paramstr |
| 0xEA | u16 | model year |
| 0xEC | u32 | price, credits (R34 V-spec 55,980; S4 39,730; ×100 in Japanese, region notes); selling pays a quarter |
| 0xF0 | u8 | flags: bit 0 not for sale at the dealer, bit 1 cannot be sold / traded, bit 2 wheels cannot be changed [spec] |
| 0xF1 | u8 | category: 0 production, 1 race car, 2 rally car [spec+data] |
| 0xF2, 0xF4 | u16, u16 | Japanese name and second string → paramunistr (kana; what the game shows in Japanese) [spec] |
| 0xF6 | u8 | 1 on the F1 cars, else 0 [data] |
| 0xF7 | u8 | region byte: the languages the car is offered in (0 all, 1 / 2 only / not British English, 3 / 4 only / not US, 5 / 6 only / not the European languages, 7 / 8 only / not Japanese) |
| 0x100, 0x102 | u16, u16 | British English name and second string → paramunistr [spec] |
| 0x104 | u16 | maker name → paramunistr (`"NISSAN"`) |

The car model file is not in this record: it is the `model` field of the
car's body record (table 5), e.g. R34 → `ni0137`. 240 of the 288 models in
`cars/.list` are referenced; the rest are `*1xxx`/`*2xxx` race/alternate
bodies and unused models.

### Engine (table 6, 88 bytes) [spec+data]

| Off | Type | Meaning |
|---|---|---|
| 0x10 | u16 | layout string (paramunistr): `L4`, `L6`, `V6`, `Rotary`... [data] |
| 0x12 | u16 | valvetrain string: `DOHC`, `SOHC`, `OHV` [data] |
| 0x14 | u16 | aspiration string: `NA`, `TURBO`, `SuperCharger` [data] |
| 0x16 | u16 | **engine sound id**: banks `sound/engine/%05d` and `sound/<normal0-3|turbo0-3>/%05d` (all 235 ids exist) [data; from the sound workstream] |
| 0x18 | 16 × u16 | torque curve, 0.01 kgf·m [spec+data] |
| 0x38 | u16 | displacement string (paramstr, `"2568cc"`) [data] |
| 0x3A | u16 | catalogue max power, PS [data] |
| 0x3C | u16 | power rpm string (`"6800rpm"`) [data] |
| 0x3E | u16 | catalogue max torque, 0.01 kgf·m [data] |
| 0x40 | u16 | torque rpm string [data] |
| 0x42 | u8 | ×10 → the car spec's overall output factor (‰) unless overridden by the user settings; 100 for most cars, 118 for the R34 GT-R [spec]; probably a hidden output factor (R34 real output ≈ 280 × 1.18) [guess] |
| 0x43 | u8 | clutch bite rpm ×10: the clutch starts to take up drive above it (250 typical = 2,500 rpm) [spec] |
| 0x44 | u8 | idle rpm ×10 [spec] |
| 0x45 | u8 | rev limit ×100 rpm: equals the last curve rpm point in 226/235 engines [data]; raised by engine balance / NA tune / turbo parts [spec] |
| 0x46 | u8 | = +0x45 − 5 in 201/235 (500 rpm below the limit) [data]; raised by turbo parts [spec] |
| 0x47 | 16 × u8 | curve rpm axis, ×100 rpm [spec+data] |
| 0x57 | u8 | number of valid curve points (≤ 16) [spec] |

The curve reproduces the catalogue power: R34 peak 28.6 kgf·m × 7000 /
716.2 ≈ 280 PS. A cross-check of ~45 production cars against real-world
figures found power, displacement, dimensions, drive type, gear counts and
final drives correct, with no unit errors. The catalogue torque field disagrees
with the curve on 8 cars (e.g. Camaro SS: 35.7 stored vs. 48.5 from the curve),
so treat 0x3E as display text. Race cars often store 0 for power and torque,
and 42 cars have year 0. Unused tail points are padded (rpm 0xFF, torque 0 in the
car spec).

Engine sound variant (sound workstream, consistent with these tables): muffler
part (table 20) stage byte +0x10, plus 4 if the turbo kit record has a
non-zero +0x11 → `normal0..3` / `turbo0..3`. Exposed as
`CarDatabase::engine_sound()`.

### Chassis (table 3, 32 bytes)

| Off | Type | Meaning |
|---|---|---|
| 0x10 | u8 | front weight % [spec+data] |
| 0x11 | u8 | 0/1 [guess] |
| 0x12 | u8 | front axle grip factor: ((byte + 124) mod 256 + 900) / 1000 multiplies the front tyres' peak force [spec] |
| 0x14 | u16 | length mm [spec+data] |
| 0x16 | u16 | height mm [data] (not used by physics) |
| 0x18 | u16 | wheelbase mm [spec+data] |
| 0x1A | u16 | weight kg [spec+data] |
| 0x1C | u8 | yaw inertia parameter (×100), scaled by lightweight +0x11 % and body +0x38 % [spec] |
| 0x1D | u8 | centre-of-gravity height parameter, mm: CoG height = (this + front ride height) / 2 [spec] |
| 0x1E | u8 | rear axle grip factor, as +0x12 [spec] |

### Racing modify / body (table 5, 72 bytes)

| Off | Type | Meaning |
|---|---|---|
| 0x10 | u8 | stage (1 = racing-modified body) |
| 0x13 | u8 | default front downforce setting, % [spec] |
| 0x18 | u64 | car model id, e.g. `ni0137` [data] |
| 0x20 | 4 × f32 | collision box, mm (truncated to integers): front and rear extent from the car's centre, width, height; 0 = from the chassis length [spec] |
| 0x30 | u16 | ‰ multiplier on weight [spec] |
| 0x32 / 0x34 | u16 | front / rear tread, mm [spec+data] |
| 0x36 | u16 | width mm [data] |
| 0x38 | u8 | % multiplier on chassis +0x1C (yaw inertia) [spec] |
| 0x39 | u8 | drag coefficient Cd ×100 [spec] |
| 0x3C | u8 | default rear downforce setting, % [spec] |
| 0x3D / 0x3E | s8 | front / rear wheel offset, mm: the track used is tread − 2 × offset [spec] |
| 0x3F–0x40 | u8 | copied into the car spec, meaning unknown |
| 0x44 | f32 | frontal area, m² (quantised to 0.0001) [spec] |

### Drivetrain (table 13, 40 bytes)

| Off | Type | Meaning |
|---|---|---|
| 0x11–0x13 | u8 | front torque share, %: the race uses +0x13 (4WD front share and coupling) [spec]; 0x11/0x12 vary with the AWD part [guess: range] |
| 0x14 | u8 | drive type: 0 FR, 1 FF, 2 4WD, 3 MR, 4 RR [spec+data, all 283 records] |
| 0x15 | u8 | AWD system, 0xFE = none (3 = Skyline GT-R, 4 = Lancer Evo VII...) [data] |
| 0x16–0x21 | 6 × u16 | +0x16 engine friction (engine braking at the top of the torque curve, ×0.98 N·m), +0x18 / +0x1A front / rear wheel spin inertia (×0.01), +0x1C / +0x1E front / rear shaft inertia (×0.01), +0x20 engine inertia (×0.001); scaled by the flywheel, clutch and propeller-shaft parts [spec] |

### Gear (table 17, 48 bytes) [spec+data]

| Off | Type | Meaning |
|---|---|---|
| 0x10 | u8 | gearbox type (0 in stock gear records, presumably the stock box). Any non-zero value enables the race transmission whine (see sound.md, "Race car sounds") [spec] |
| 0x11 | u8 | forward gear count [spec] |
| 0x12 | s16 | reverse ratio ×1000 |
| 0x14 | 7 × s16 | gear ratios ×1000; -999 = not set (racing gearboxes) |
| 0x22 | s16 | final drive ×1000 |
| 0x24 / 0x26 | s16 | final drive max / min (adjustable gearboxes) |
| 0x28 | u8 | adjustable |
| 0x29–0x2B | u8 | auto-setting slider (steps?, min, max) [guess] |

The gear ratios used in a race come from the car's user settings (the
Settings pages), which are initialised from this record.

### Suspension (table 18, 128 bytes) [spec]

Adjustable groups (steps, min, max, default):

| Off | Steps | Ranges | Settings | Meaning |
|---|---|---|---|---|
| 0x2B | 1/10 | +0x2C/+0x2D, +0x2F/+0x30 | front, settings 1 & 2 | front bound damping: force at the two reference speeds (×49 N) |
| 0x32 | 1/10 | +0x33/+0x34, +0x36/+0x37 | front, settings 1 & 2 again | front rebound damping, same units |
| 0x39 | 1/10 | +0x3A/+0x3B, +0x3D/+0x3E | rear, settings 3 & 4 | rear bound damping |
| 0x40 | 1/10 | +0x41/+0x42, +0x44/+0x45 | rear, settings 5 & 6 | rear rebound damping |
| 0x47 | 1/4/7 | +0x48/+0x49 | front | anti-roll bar |
| 0x4B | 1/4/7 | +0x4C/+0x4D | rear | anti-roll bar |

Other fields (front / rear pairs) [spec]:

- +0x13 / +0x16 default camber setting (×0.1°), +0x19 / +0x1C default toe setting.
- +0x1D / +0x1E bump travel, +0x1F / +0x20 droop travel, mm.
- +0x21..0x26 spring rate (min, max, default per axle; e.g. full-racing
  40/200/72), 0.1 kgf/mm; +0x27 / +0x28 default motion-ratio setting (%).
- +0x29 / +0x2A bump-stop stiffness (×98 kN/m).
- +0x54 / +0x5A u16 default ride height, mm.
- +0x5C / +0x5D unsprung mass (×0.25 kg); +0x5E / +0x5F copied, meaning unknown.
- +0x60..0x6F camber curves: four angle codes then four multiplier codes per axle.
- +0x70..0x77 damper reference speeds (bound 1, bound 2, rebound 1, rebound 2;
  front then rear; ×0.01 m/s), the forces at them come from the damper settings above.
- +0x78 / +0x79 knee gap, mm; +0x7A / +0x7B bump damping (×98 N·s/m).

### Event (table 32, 504 bytes)

| Off | Type | Meaning |
|---|---|---|
| 0x008 | u64 | course label (table 34) [data] |
| 0x010 | 32 × u64 | entry pool: car variant labels (table 31), `-` = end. Opponents are drawn from it at random without repeats until it runs out; license and machine tests: entry 0 = the car the player drives [spec] |
| 0x110 | 32 × u32 | per-entry paint: carcolor.db colour id, 0xFFFFFFFF = chosen at race start (36 events set some) [spec+data; race workstream, docs/dev/RACE.md] |
| 0x190 | u64 | regulation (table 33) or `-` [data] |
| 0x198 | u8 | initial speed in km/h for rolling/flying starts (0 = from rest; 50–220 on license tests, 80 on endurance rolling starts) [spec; race workstream] |
| 0x199 | u8 | laps [spec]; machine tests: the test, 1 = 0-400 m (M400), 2 = 0-1000 m (M1000), 3 = top speed (MMAX) [spec+data] |
| 0x19A | u8 | license test kind: 1 stop test, 2 section, 3 full lap(s); rally 6 [spec] |
| 0x19B | u8 | **required license**: 0 none, 1 B, 2 A, 3 IB, 4 IA, 5 S, 6 R (rally) [spec] |
| 0x19C | 20 × u8 | percentages (100 × 8, then groups of 4) copied to the race-rule block [spec; meaning: AI side] |
| 0x1B5 | u8 | cars in the race including the player (6 races, 2 rally, 1 tests) [spec] |
| 0x1C4 / 0x1C8 / 0x1CC | u8 min, u8 s, u16 ms | license **gold / silver / bronze** target times [spec] |
| 0x1D0 | u16 | always 0 |
| 0x1D2 | u8 | special requirement: 9 = dirt tyres required [spec] |
| 0x1D3 | u8 | trophy / result-screen class: arcade 1–4, GT 5 beginner, 6 amateur, 7 professional and endurance, 8 rally [spec] |
| 0x1D4 | 6 × u16 | prize for places 1st–6th in units of 100 Cr (×100 again in Japanese, region notes) [spec; Sunday Cup stores 10/9/8/7/6/5 → 1,000 Cr for 1st, the retail payout; Seattle 100 Miles 1500 → 150,000 Cr] |
| 0x1E0 | u8 | 4 on professional / endurance / long amateur events [unknown] |
| 0x1E2 | u16 | championship bonus, units of 100 Cr [spec] |
| 0x1E4 | u8 | time limit in minutes (the 2-hour endurance races) [spec] |
| 0x1E5 | u8 | league: 1 beginner, 2 amateur, 3 professional, 0 other [spec] |
| 0x1E6 | s16 | flying-start distance: metres before the start line (license tests, negative) [spec; race workstream] |
| 0x1E8 | u64 | race mode label (`RACE_MODE_SINGLE`, ...) [data] |
| 0x1F0 | u16 | per-event number [guess] |
| 0x1F2 | u8 | countdown seconds (start type 0 only) [spec; race workstream] |
| 0x1F3 | u8 | start type: 0 demo / machine test, 1 standing start with a 240-frame hold, 2 standing start with a 600-frame hold, 3 rolling (endurance) or flying (when +0x198 > 0) [spec; race workstream] |
| 0x1F4 | u8 | car-class rule: 1 race or rally car only (car +0xF1 ≠ 0), 2 tuned car only, 3 production car (car +0xF1 = 0) and not tuned [spec] |
| 0x1F5 | u8 | aspiration rule: 1 NA only, 2 turbo only (turbo part +0x11 ≠ 0) [spec] |
| 0x1F6 | u8 | drivetrain rule: 1 FF, 2 FR, 3 4WD, 4 MR, 5 RR [spec] |

GT-mode use of these fields (entry checks, prizes, licenses) is implemented
in `src/game/gt_mode.cpp` and described in [../dev/GT_MODE.md](../dev/GT_MODE.md).

### Course (table 34, 16 bytes) [data]

u8 +0x08 surface/category (0 tarmac, 2 dirt, 1 on six courses), u16 +0x0A
label (paramstr), u16 +0x0C display name (paramunistr, `"Rome Circuit"`,
`-` for hidden), u16 +0x0E constant. Label prefixes: `r_` reverse, `L_`
license sections, `render_` rendering/test variants.

### Car variant / arcade car (tables 31, 35; 272 bytes) [data]

u64 label, u64 base car, 26 part ids at +0x10 (same order as the car
table), u64 0 at +0xE0, then 40 bytes of tuning settings at +0xE8 (raw,
not decoded).

## Standalone files

**racemode.db** (GTDT, 16 × 272): u64 mode label (`RACE_MODE_*`, hashed),
u8 +0x08 mode number (the race display mode, `race::HudMode`), 7 × u8 1,
then at +0x10 32 × u64 racedetail ids, `(Null)` = end [data].

**racedetail.db** (GTDT, 228 × 40): u64 id (label unknown — not in any IDDB), then one row of the race display's layout:
u16 +0x08 HUD part (0 = none), u16 +0x0A sub-part, u8 +0x0C vertical and
u8 +0x12 horizontal alignment, s16 +0x18 view (0 in-car, 1 outside, 127
both), s16 +0x1A / +0x1C x / y and +0x1E / +0x20 w / h in the 640 × 448
frame [data]. A mode shows only the parts it has rows for. The engine's
layout table (`src/race/hud_layout.*`) is these rows; test
`race_hud_layout_disc` checks it against the disc.

**carcolor.db** (`"GT2K"`): u32 0, u32 288 (= model count), u32 three
section offsets (0x1218, 0x26C0) and the file size; at 0x18, 288 × {u64
id_hash(model id), u32 colour count, u32 offset} sorted by code (286 match
`cars/.list` models; 1321 colours total). Colour bodies are not decoded.
**carcolor.sdb** holds the 631 colour names (`"British Racing Green"`).

## Other data-like files surveyed

| Files | Finding |
|---|---|
| `tire/n*`, `menu/tire/n*` (`GTTR`) | tire **textures**: 32-byte header (+0x0C size, +0x18 f32 8.0, +0x1C offset 0x20) + one `Tex1` set. No grip data — that is in tables 25–29. |
| `*.prm` (`PRZ0`, `PRM0`, gzip) | trophy / prize scenes (GTM1 model plus key-framed camera/model matrices and lights), decoded: see [prm.md](prm.md). `race/{gold,silver,bronze}.prm`, `race/[ag]_<difficulty>_<medal>.prm`, `menu/champ_NN.prm`, `menu/lic_*.prm`. |
| `gameconf/*.gcf` | tagged tree (u32 tag / count / offset nodes with inline NUL-terminated strings): arcade car classes (`CLASS_A`...) listing table-35 labels (`corvette_z06_00_385`, `arm_rx8_01`) [not decoded further] |
| `cars/.list`, `courses/.list` | plain text, one model / course id per line |

## Open questions

- The remaining unexplained bytes of steer (2), tire compound (27, the
  individual curve points) and suspension (18): they are copied into the
  car spec; `src/sim/car_spec.cpp` shows how each one is used.
- Event opponent-strength percentages (+0x19C), +0x1B6..+0x1C3 / +0x1E0, and `racedetail.db` labels.
- carcolor colour records, variant settings block, gameconf tree.

## Region notes

- This disc (`PBPX_955.03`) is the NTSC-U release. The `paramdb` regional sets on it
  (plain, `_us`, `_eu`) hold identical prices.
- Money is stored in the same units on every disc; the game shows and pays every sum 100×
  in Japanese: car prices (+0xEC), part prices, event prizes and championship bonuses
  (already in hundreds of credits, so ×10,000 there), and the game config's sums
  (`gameconf/product.gcf` tag 10: `initial`, the new game's balance, and the `wheel`,
  `car_wash` and `oil_change` prices). The NTSC-U and PAL discs store `initial` 18,000; the
  Japanese disc 15,000, so its new game starts with 1,500,000 Cr.
- Prizes were retuned too, not just scaled: Sunday Cup 1st stores 10 hundreds (1,000 Cr) in
  the international releases, 6 hundreds on the Japanese disc (×100 ×100 = 60,000 Cr). The
  PAL and Japanese discs price the RSX concept (`rsx_detroitver_01`) at 300,000, the NTSC-U
  disc at 30,000.
