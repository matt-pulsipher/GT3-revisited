# Modding

Every asset the engine loads goes through a layered virtual filesystem
(`src/vfs/vfs.*`): **enabled mods, highest priority first, then the disc's
GT3.VOL**. A mod can therefore replace any file, or add new ones, without
touching the disc image.

## Installing mods

On the **Mods** screen:

- **Install from .zip** (Start) picks a .zip with the system's file picker and
  installs every mod in it. On a computer you can also drop the .zip onto the
  window while the Mods screen is open.
- **Open folder** (Select) shows the mods folder in the file manager. On
  Android the folder is in the app's private storage, so it opens in the
  **Files** app, where **GT3 Revisited** is listed with the other places
  (Internal storage, Downloads). Unzipped mod folders can be copied into
  `mods/` there.
- Cross turns the selected mod on or off. **Remove** (Square) deletes its
  folder after asking.
- The list is read again when you come back to the game from another app.

What a .zip may hold:

- One mod or several. A mod is a folder with a `mod.ini`. A .zip without any
  `mod.ini` is one mod when its top level, or its only top folder, has
  `files/`, `cars/`, `makers/`, `events/` or `textures/`.
- Each mod installs as `mods/<its folder's name>/`. A mod whose `mod.ini` is
  at the top of the .zip is named after the .zip file.
- Anything beside the mods (a README at the top, `__MACOSX/`) is left out.
- A mod already installed under the same name is replaced, after asking: its
  folder is deleted first, with any changes made to it.
- Standard (Deflate) or uncompressed .zip files only. Password-protected
  archives, other compression methods and paths leading out of the folder
  (`..`, absolute paths) are refused, and nothing changes until the whole
  archive has unpacked.

To share a mod, zip its folder (`my_mod/` with its `mod.ini` inside).

## Layout

Mods live in `mods/` in the user data folder (the Mods screen shows where;
**Open folder** opens it):

```
mods/
  my_mod/
    mod.ini
    cars/
      my_car.ini                   <- a new car (see "Adding cars")
    makers/
      my_maker.ini                 <- a new maker with a dealer (see "Dealers and makers")
    events/
      my_event.ini                 <- a GT Mode event (see "Adding events")
    wheels/
      my_rim.ini                   <- a new wheel at the wheel shop (see "Adding wheels")
    liveries/
      sunday_blue.ini              <- a new paint colour for a car (see "Paint colours")
      sunday_cup_12/               <- a livery: livery.ini and its pictures (see "Liveries")
    files/
      menu/license_bg.img          <- replaces this file from GT3.VOL
      bgm/race_a05.ads             <- replaces a music track
      music/dealer-01.flac         <- your own recording of a menu tune (see "Menu music")
    textures/
      tyres/tread-0123456789abcdef.png  <- replaces the texture with this key (see "Texture packs")
```

Paths under `files/` mirror the paths inside GT3.VOL, which you can see with
the in-app **Asset browser** or `gt3tool ls <iso> [dir]`. In the browser,
files supplied by a mod are shown in green with the mod's name as their
source. Files are stored **uncompressed**: even if the disc stores an entry
gzip-compressed, the replacement is the plain file (what `gt3tool extract`
produces).

`mod.ini`:

```ini
[mod]
name = Better License Backdrop
version = 1.0
author = you
description = Replaces the license test background.
priority = 10        ; higher wins when two mods provide the same file
discs = eu           ; optional: the discs it is for (below); absent: every disc
```

A new mod is enabled. Enable or disable mods from the Mods screen. Each disc
keeps its own choices: they are stored in `config.ini` under `[mods.<disc key>]`
(`[mods.us]`, `[mods.eu]`, `[mods.SCEF-50294]`; disc keys: [COMPATIBILITY](COMPATIBILITY.md#the-disc-library)),
and a disc without a choice for a mod yet takes the one under `[mods]`, where
earlier versions kept them.

`discs` names the discs a mod is made for, separated by commas: disc keys (`us`,
`eu`, `jp`, or a serial of its own such as `SCEF-50294`) or serials, written
either way (`SCES_502.94` or `SCES-50294`). A serial names one release (`PBPX-95503`
is NTSC-U 1.10, not 1.00); `eu` means the PAL releases only, not a modified disc
under a serial of its own. A `!` leaves a disc out: `discs = !SCES-50858` is every disc but that one. On other discs the mod stays off, and the Mods screen
says which discs it is for. Give `files/` mods a `discs` line: a file replaced
for one release can break another disc (a PAL car list on a modified PAL disc
with cars of its own). Texture packs and mod cars rarely need one: a texture is
only replaced where its pixels are the same, and a mod car whose donor isn't on
the disc doesn't load.

## Workflow

```bash
gt3tool extract game.iso menu/license_bg.img work/
```
```bash
gt3tool tex game.iso menu/license_bg.img work/png/
```

Edit, re-encode and drop the result into `mods/<id>/files/...`. Then use
`gt3r --browse menu/license_bg.img` to check it quickly.

For textures, a texture pack (below) is easier: plain PNG pictures at any size, no PS2 formats.

## Texture packs

A mod's `textures/` folder holds PNG pictures that replace the game's textures as the game
loads them: car bodies and wheels, courses, prize models, menus, the race HUD and course map,
the game's fonts and the race effects (sparks, tyre smoke, the sun's glare, wet-road streaks,
course billboards). A picture can be any size up to 4096 x 4096: draw it at 2x or 4x the
original for sharper textures. A texture keeps its place and size on screen; only its pixels
change.

**Quick start.**

1. On the Mods screen, **Save textures**. The pictures go to `texture-dump/<serial>/` in the
   user data folder. The game never loads from there.
2. In `mods/`, make a folder for your pack with a `textures/` folder in it:
   `mods/my_textures/textures/`. That folder is a mod already: `mod.ini` is optional, and a new
   mod is enabled.
3. Copy in only the pictures you change, keeping the key at the end of each name. Every
   picture in an enabled pack is read, changed or not, so don't copy the whole dump.
4. Edit them, then restart the game, or come back to it from your editor with the Mods screen
   open: the game reads the mods folder again then. What loads next (a race, a menu page)
   shows your pictures.

**Names.** Each texture has a key, 16 hex digits made from its pixels. A picture replaces the
texture whose key its file name ends in, after `-`, `_`, `.` or a space:
`0123456789abcdef.png`, `tread-0123456789abcdef.png`. The rest of the name, and any folders
under `textures/`, are yours to choose. When two enabled mods replace the same texture, the
higher `priority` wins.

**Getting the textures.**

- **Save textures** on the Mods screen saves every texture of the disc, each once, as a PNG
  named with its key, in `texture-dump/<serial>/` in the user data folder: one folder per game
  file (`cars/day/am0006/`, `menu/US/gt_mode3.pmb/`...). It takes a minute or two and about
  400 MB. `textures.txt` there lists every key with each file and texture that uses it, so a
  texture shared by several files (saved under the first) can be found from the others.
- The **Asset browser** (Developer options) shows each texture's key, whether it animates, and
  the mod replacing it; **Square** saves the open file's textures into the same folder.
- `gt3tool tex-dump <iso> <outdir> [prefix]` saves them from the command line;
  `gt3tool tex-keys <iso> <vol-path>` lists a file's keys (`anim` after the animated ones).

Copy a saved picture into your mod's `textures/`, keep the key at the end of its name, and edit
or upscale it.

**Good to know:**

- Car colours: the textures a paint colour changes are saved once per colour (`-paint3` in the
  name), each with its own key; the rest are saved once.
- Cars have two models: the menus' (`menu/cars/`, `menu/wheel/`) and the race's (`cars/`,
  `wheel/`), with mostly different textures, each with its own key. A body or wheel picture changes
  only the models that use that texture (`textures.txt` lists them).
- Tires: each tire's picture is in the race files (`tire/`, `cars/`: 128x64) and in the menu files
  (`menu/tire/`, `menu/cars/`: 256x128), the tread in the top half and the sidewall below. A
  picture for either one shows in both places; with a picture for each, each shows its own (with
  "Showroom wheels in races" on, close cars in races use the menus').
- Animated textures (course lights and signs that change colour) are saved with their first
  colours and `-anim` in the name (`set2-14-anim-0123456789abcdef.png`; `textures.txt` too). A
  replaced one shows the picture and no longer animates, so leave them out of a pack (an
  upscale script can skip names with `-anim` in them) unless a still picture is what you want.
- Alpha 255 is opaque (the game's 0x80 is saved as 255).
- A change applies to what loads next: a race, a menu page, a model in a viewer.
- Movies, and the images the game composes while it runs (text set into video memory, screen
  copies), are not textures of the disc. Movies can be replaced as files through `files/`.

For tool makers, the key: FNV-1a (64-bit) over 32-bit little-endian words (the width, the
height, then the texels row by row as RGBA8 with R in the low byte), finished with SplitMix64's
mix (`h ^= h >> 30; h *= 0xBF58476D1CE4E5B9; h ^= h >> 27; h *= 0x94D049BB133111EB;
h ^= h >> 31`). The pixels are the whole texture as `gtm1::decode_texture_set` decodes it: its
full TEX0 size, the CLUT offset (CSA) applied, a car paint's palette, palette animations at
their start (`src/formats/texture_pack.h`).

### PCSX2 texture packs

**PCSX2 pack** on the Mods screen (on computers) turns a texture pack made for PCSX2's texture
replacement into a mod. Pick the pack's folder in PCSX2's `textures` folder (named after the
disc's serial, such as `SCUS-97102`), or that `textures` folder itself. Beside the pack's
`replacements/` it needs PCSX2's `dumps/` of the same pictures: PCSX2 names a picture after
hashes of the console's video memory, so each is recognised by its original in `dumps/`. If a
pack comes without them, turn on texture dumping in PCSX2 and play the parts of the game the
pack covers.

The pictures become the mod `pcsx2_<folder>`, with a `pcsx2-import.txt` listing the ones left
out:

- images PCSX2 dumped that the game composes while it runs (text set into video memory, screen
  copies), which are most of what PCSX2 dumps besides the disc's textures;
- textures of another release of the game, where they differ from the disc in use;
- DDS pictures (PNG only), and mipmap levels (`-mip1`...: the game makes its own).

PCSX2's alpha (128 = opaque) is converted. A picture of part of a texture (PCSX2's
`r<width>x<height>` names) is placed into a picture of the whole texture at the same scale.

From the command line: `gt3tool tex-pcsx2 <iso> <PCSX2 textures/<serial> folder> <mod dir>`.

## Exporting models

The **Car viewer** and **Course viewer** (Developer options) save the model shown as binary
glTF with **R2** (C on the keyboard): `exports/cars/<model>.glb` or
`exports/courses/<course>.glb` in the user data folder, at the level of detail shown (all
levels: one node per level). Blender (File > Import > glTF 2.0) and most 3D programs open it.

The file has glTF's axes (+Y up, the model facing +Z, metres), every texture embedded as PNG,
the vertex colours (course scenery is lit by them: its materials are unlit,
`KHR_materials_unlit`), and a node per mesh (a car's body, each wheel and tyre). Reflection
passes and the passes drawn over other surfaces are left out. Material names follow the car
model loader's (see "Car models"), so an exported body, edited and saved as `.glb` without its
wheel and tyre nodes, can come back as a mod car's own body.

From the command line: `gt3tool model <iso> <vol-path> <outdir> --gltf` (cars, wheels, prize
models) and `gt3tool course-obj <iso> <vol-path> <outdir> glb`.

## Exporting cars

A car from one of your discs can be exported whole, as a mod your other discs load: a Gran
Turismo Concept car in Gran Turismo 3, or a car one GT3 release has and another lacks. On the
**Car database** screen, **R2** exports the selected car and **L2** every car on the disc. From
the command line:

```bash
gt3tool car-export concept.iso r35_gtr_tmv_01 mods/
```
```bash
gt3tool car-export concept.iso all mods/
```

The cars go into one mod per disc, `mods/cars_<disc>/` (`cars_sces50858` for the European Tokyo-Geneva
disc). Its `mod.ini` says `discs = !<disc>`, so it stays off on the disc it came from and is on
everywhere else. Each car's label gets the disc's suffix (`r35_gtr_tmv_01_sces50858`), so an exported
car never replaces a car of the disc it is loaded on. A car brings everything it has on its disc:

| File | Holds |
|---|---|
| `cars/<label>.ini` | name, maker, year, price, category and its paints (the model's colour list, in order); a car file like any other, so `[dealer]`, `[arcade]` and `[raw.*]` work on it |
| `cars/<label>.records` | every database record of the car: the stock parts, the upgrade parts the shops sell (see "Tuning parts" below), the car record, and the shared records they use (steering, driving aids, tyre sizes and compounds). A shared record the other disc holds differently comes in under a label of the car's own |
| `files/cars/{day,eve,night}/<model>_<disc>`, `files/menu/cars/<model>_<disc>` | its models for races and the showroom, with their stock rims, lamps and camera points |
| `sounds/engine/<id>`, `sounds/<normal0-3, turbo0-3>/<id>` | its engine sounds |

**Tuning parts.** Gran Turismo Concept sells no tuning, so most of its cars have none of the parts
GT3's tune shop sells. The export gives such a car the shop of the most alike car on its disc that
has one: the same category, drive layout and aspiration, then the nearest power, weight and gear
count (a car with the same engine and chassis, such as the GT3 version of a Concept car, comes
first). Each part is fitted to the car: brakes in the ratio of the two cars' stock brakes, gearboxes
on its gear count and final drive, suspension kits scaled to its axle loads around its own ride
heights, tyres on its own sizes. Parts the car already has on its disc without a price keep their
values and take that car's price. The car's file names the car its shop came from. Cars exported
from a GT3 disc keep the shop they have (GT3's own cars without one, the F1 cars among them, stay
that way). `--shop=fill` gives every car without a shop one, `--shop=keep` none:

```bash
gt3tool car-export concept.iso all mods/ --shop=keep
```

The car then races on the physics of the disc it is loaded on. Gran Turismo Concept's longer chassis,
engine and gear records are cut to GT3's sizes (their added bytes are not read). What GT3 itself does
not have stays behind: a car with a stepless or automatic gearbox on its disc shifts as a manual one
in GT3 (its file says so in a comment). A maker GT3 has no dealer for (LEXUS, a few racing cars
without a maker) gets no dealer page: the car is in arcade mode and the quick race pickers, and joins
GT Mode once a mod adds the maker (see "Dealers and makers"). A car its disc gives no price is not
for sale.

The exported files are your disc's data: keep them for your own use. Online, an exported car is never
sent to another player.

## Adding cars

**Start from the example.** [`examples/mods/example_coupe`](../examples/mods/example_coupe/README.md)
is a complete car mod to copy: a car file with a comment on every value, its own
glTF body with materials named for the loader, a new maker with a dealer, dealer
pictures rendered with the game, the scripts that prepared the model, and the
`gt3tool modcar` results it was tuned to. Its second car,
`cars/gt3r_concept_coupe_e_24.ini`, is the same body as an electric car. Copy it into
your mods folder to try it, then rename it and change it into your own car.

A mod can add new cars as small text files in `mods/<id>/cars/<label>.ini`.
A car file contains only your own numbers (dimensions, weight, power, gearing,
tyre sizes). Everything it leaves out is borrowed at load time from a
**donor**, a car on the player's own disc: suspension geometry, steering, tyre
compounds, brakes, driving aids and, for now, the body shell. So a car mod never
contains anything taken from the game.

The loader (`src/game/data/mod_cars.*`) clones the donor's database records. That
includes the upgrade parts the shops sell: tyres, suspension kits, weight
reduction, brakes, LSD and, for engine cars, the engine parts. The clones get the
new car's labels, and the values from the file are written over them. The new
records go into the ordinary tables, so the car works wherever the engine
looks up a car by label: the physics, the race and the database screen. A mod
car bought in GT Mode finds its cloned upgrade parts in the tune shop.

```ini
[car]
label = tesla_model_y_perf_26  ; unique id: a-z, 0-9, _
name = Model Y Performance     ; the race results show it alone, without the maker
maker = TESLA                  ; matches an existing maker's name when there is one
year = 2026
price = 57490                  ; credits
category = production          ; production | race | rally
donor = legacy_gtb_96          ; required: a car on the disc (gt3tool car <iso> <label>)
model = sb0074                 ; optional: another body shell; default = the donor's
fit_body = yes                 ; stretch the borrowed shell to the dimensions below
driver_side = left             ; optional: left | right, the seat for VR's "Match the car" (default: read from the body)

[body]
weight_kg = 2033               ; curb weight (the game adds 120 kg for the driver)
front_weight_pct = 49
length_mm = 4796
width_mm = 1921
height_mm = 1610
wheelbase_mm = 2890
front_track_mm = 1646
rear_track_mm = 1630
cd = 0.23
frontal_area_m2 = 2.6
ride_height_mm = 140           ; optional (stock suspension setting)
scale_suspension = yes         ; springs, dampers and bars scaled by axle load vs the donor
scale_brakes = yes             ; brake torque scaled by weight vs the donor

[powertrain]
type = electric                ; electric | engine
power_kw = 460                 ; or power_hp: power of the curve
rated_kw = 380                 ; optional: shown in the specs when it differs from the curve
torque_nm = 695                ; or torque_lbft; at the motor (or crank)
max_rpm = 16850                ; motor limit: top speed = this through the reduction
; curve = 0:695 5000:695 ...   ; optional explicit torque curve, rpm:Nm (up to 16 points)
; base_rpm = 5200              ; electric: where full torque ends (default power / torque)
output_pct = 109               ; hidden output factor, like GT3's own (R34 GT-R: 118)
regen_g = 0.2                  ; electric: one-pedal lift-off deceleration (default 0.2 g)
; regen_kw = 80                ; power limit (default: enough for regen_g up to 70 km/h, <= 70 % of power)
; regen_torque_nm = 170        ; motor torque instead of regen_g
layout = Dual Motor            ; display text
; engine cars: idle_rpm, sound_id (a disc engine sound bank), aspiration, displacement

[drivetrain]
drive = AWD                    ; FF | FR | MR | RR | AWD
front_torque_pct = 35          ; AWD: fixed front share (electric: one motor per axle)
gears = 9.04                   ; electric: the single reduction; engine: up to 7 ratios
; final_drive = 4.1
; reverse = 3.2

[tires]
front = 255/35R21              ; or `size = ...` for both axles
rear = 275/35R21               ; width and aspect in steps of 5 (as the disc stores them)
grip_pct = 98                  ; optional: the street compounds' grip, %

[sound]                        ; electric: the synthesised motor
whine_order = 1.15             ; pitch multiplier
whine_level = 1.0
inverter_level = 1.0

[dealer]                       ; GT Mode (all optional)
for_sale = yes                 ; sold at its maker's dealer for [car] price
description = ...              ; the dealer page's scrolling text
picture = images/my_car.png    ; 640x448 dealer picture (a car on a dark backdrop, like GT3's)
thumb = images/my_car_thumb.png ; 51x27 strip button (also the arcade car picker's icon)
logo = images/my_car_logo.png  ; 256x80 name artwork (default: the name in the menu font)

[arcade]                       ; arcade mode (all optional)
class = auto                   ; auto (by power-to-weight, as GT3 ranks garage cars), c, b, a, s, dirt or none
race_variant = auto            ; the class's race for stronger (0) or weaker (1) cars
level = 0                      ; arcade unlock level (0 = always open)

[reference]                    ; real-world figures; only `gt3tool modcar` reads these
zero_to_60_s = 3.3             ; magazine tests (1 ft rollout)
quarter_mile_s = 11.5
top_speed_kmh = 250
skidpad_g = 0.88               ; 300 ft skidpad, kept for reference (also: zero_to_100_s, brake_60_0_ft)

[raw.chassis]                  ; advanced: bytes of the car's own record in any table
0x1D = 120                     ; 0x30:u16 = 1000, 0x44:f32 = 2.1 (docs/formats/database.md)
```

In arcade mode, a mod car appears at the end of its class's car list. It is
drawn in the turning showroom, with its own specs and its `[paints]` colors,
and races against that class's opponents. The icon strip shows its
`[dealer] thumb`. It races on the same stock settings (gearing, output,
suspension) in arcade mode, quick races and `gt3tool modcar` as when it is
bought new in GT Mode.

Mods load in priority order. A later mod's car replaces an earlier one with the
same label, but a mod car can never replace a disc car. The Mods screen lists
each mod's cars, along with the reason any of them failed to load.

Save car and maker files as UTF-8. Names, makers, display texts, descriptions
and paint names may use accented letters: `maker = Citroën` joins the disc's
Citroën dealer. They are drawn with the game's fonts, Japanese text included; a
character those fonts can't draw shows as `?`.

### Dealers and makers

A car for sale appears at its maker's dealer in GT Mode, at its `price` in
credits. The price is stored like the disc's own car prices, so it follows the
game's money rules: the US and European versions show it as written, the
Japanese version shows every price, prize and starting balance 100 times
larger, a mod car's too (`price = 57490` costs 5,749,000 Cr. there). Write the
price you want a US or European player to pay. Without a `price` the car costs
what its donor costs on the player's disc. Selling a car pays a quarter of its
price, as for any car.

Where the car is sold:

- If a disc dealer already sells cars from that maker (FORD, CHEVROLET...), the
  car joins that dealer's page.
- Otherwise a mod can add the maker in `mods/<id>/makers/<maker>.ini`. It gets a
  dealer on its region's page, after the disc's own dealers:

```ini
[maker]
name = TESLA                   ; matches the cars' [car] maker
region = us                    ; jp, us, gr, fr, it, uk, bel or aus
description = ...              ; the region page's scrolling text
icon = images/tesla_icon.png             ; 51x27 strip button
background = images/tesla_background.png ; 640x448 backdrop
logo = images/tesla_logo.png             ; 475x190 logo (transparent; default: the name)
```

These pages are the disc's own pages, extended in memory as the menus load
(`src/game/gt_mode_mods.cpp`, through `menu::flow::MenuPatches`). New entries
copy the position and animation of the page's first car or maker, then use the
mod's own pictures and text. Mods bundle every picture they show; text
is drawn with the game's fonts. The dealer showroom, the colour picker
(`[paints]`), buying the car and the garage work as they do for disc cars.

To make pictures with the game itself, render the car on flat magenta with no
UI, then key out the magenta:

```bash
GT3R_VIEWER="find=mod:my_car,clean=1,key=1,yaw=3.95,pitch=0.16,zoom=1.15" GT3R_VIEWER_PAINT=0 gt3r game.iso --screen cars --shot out.png
```

### Electric cars

GT3 has no electric cars, so `type = electric` switches on a few additions. Disc
cars never use them. The race runs GT3's own physics step, and an electric car
takes a path of its own through its engine, clutch and gearbox steps
(`ElectricDrive` in `src/sim/exact/car_record.h`, `tick_engine.cpp`); every other
part of the step (tyres, suspension, aids, the AI) is GT3's. The example mod's
Concept Coupe E (`examples/mods/example_coupe/cars/gt3r_concept_coupe_e_24.ini`) is a
complete electric car to start from.

- **Drive:** the motor drives the wheels directly through a single reduction.
  There is no idle, no clutch slip and no gear changes, and full torque is
  available from standstill. The default curve holds constant torque up to the
  base speed, then constant power, easing to 85 % at `max_rpm`. `max_rpm` also
  acts as the top-speed limiter: the drive fades out over its last 2 %. The car
  changes between forward and reverse only below 5 km/h. Asked for the other way
  above that (reverse while moving forward, or the accelerator while reversing),
  it brakes with the motor as if the pedal were up, then changes near standstill
  and drives off the other way. The HUD's tachometer
  has no red band and no shift light for it, since it never shifts.
- **AWD:** both axles get a fixed torque split (one motor per axle).
- **One-pedal driving:** lifting off brakes the car with the motor instead of
  engine braking: `regen_g` (default 0.2 g) at town speeds, power-limited at
  highway speeds like real cars. The regen fades in over the accelerator's first
  10 %, and eases off when the driven tyres slip past their best braking slip
  (as ABS does for the brakes), so on a slippery surface it slows the wheels
  without dragging them down. Below 5 km/h the friction brakes take over from it,
  and the car stops and holds while the accelerator is up.
- **Sound:** a motor whine and an inverter sound are synthesised as engine sound
  banks (`src/audio/motor_sound.*`). They play through the normal engine sound
  path: pitch follows the motor, the inverter sound swells under load, and
  Doppler and panning work as usual.
- **Replays and online:** the drive is not in the car's records, so a replay
  keeps its regen and a version of the drive in its data fingerprint. A replay,
  or a free run's ghost (played from its replay), of an electric car recorded by
  a game with another drive, or none, is refused rather than played
  differently. A whole-run ghost is not checked against the fingerprint (for any
  car). Online, a game without the drive builds the car on its donor's engine,
  so the lobby shows it as different there. Replays recorded before the drive
  existed still play on the donor's engine.

### Checking a car

```bash
gt3tool modcar game.iso mods/my_cars
```

The command loads each file and reports errors and warnings. It then prints the
resulting specs and test-drives the car on the game's own physics, on the
machine tests' course: 0-60 mph (standing start and with a 1 ft rollout, like
US magazine tests), 0-100 km/h, the quarter mile, top speed (GT3's top-speed
test, driven by the race's AI), 60-0 mph braking and lift-off deceleration,
each next to your `[reference]` figures. `disc:<label>` arguments run disc cars
through the same test for comparison. Fit `power_kw` to the quarter-mile trap
speed (top end) first, then `output_pct` to 0-60. GT3's physics loses a little
at speed (the disc Viper
traps ~5 mph under its magazine tests), so the curve power often ends up
10-15 % over the official rating. `rated_kw` keeps the rating on the specs
screen. To hear the motor sound, run
`gt3tool engine-wav game.iso ev:1.15 0 motor.wav`.

### Car models

By default a mod car uses its donor's body shell from the disc, stretched to
the car's real dimensions. Between the axles the shell scales by the wheelbase
ratio, so the arches land on the new wheel positions. The overhangs scale to
match the overall length, and width and height scale directly. The wheels and
tyres are built from the car's own tyre sizes and track.

A car can bring a body of its own instead, as a binary glTF file:

```ini
[model]
file = models/my_car.glb       ; relative to the mod folder
wheel = ozw004_b               ; optional: a rim from the disc (gt3r --browse wheel/) or a mod wheel's label
lower_mm = 10                  ; optional: move the body down (negative = up) to fine-tune ride height

[paints]                       ; in menu order; applied to the paint materials (pictures: "Liveries")
Pearl White = #E9E9E6
Ultra Red = #9B1B22
```

GT3 keeps two kinds of model for each car: the menus' (dealers, garage, arcade
car select) with an empty cockpit, and the races' with the driver in, one for
each time of day. A mod car can do the same:

```ini
[model]
menu = models/my_car_menu.glb  ; dealers, garage, car select: no driver
race = models/my_car_race.glb  ; races: with the driver in
night = models/my_car_night.glb ; optional: a race at night (also day, eve)
```

Every key is optional and any one is enough: `file` is the model for
everything, and the others fall back to it. A race takes its time of day's
model (`day`, `eve` or `night`), else `race`, else `file`, else another time of
day's, else `menu`. The menus take `menu`, else `file`, else the race's. So a
car with only `file` (or only `menu`, or only `race`) uses that one model
everywhere, and a `menu` + `race` pair covers both. The time of day is the
course's, as for the disc's cars.

The glTF loader is `src/formats/model/gltf.*`. Use glTF's own conventions:
metres, +Y up, the car facing +Z. Put the origin at the wheelbase midpoint on
the ground, like GT3's own car models. The loader handles triangle meshes,
node transforms, normals (computed when missing), one UV set, vertex
colours, base colour factors and embedded PNG textures. JPEG textures, and
textures kept in separate files beside the .glb, are not read: those surfaces
show white (tinted by their base colour); the Mods screen names each one
under its car (`texture "plate": JPEG, shows white (use PNG)`), and so does
the log. In Blender's glTF export, set **Images** to PNG rather than
Automatic, which can pick JPEG. Materials are
recognised by name:

| Name contains | Treated as |
|---|---|
| `paint`, `body` | car paint: takes the chosen paint colour, reflective |
| `glass`, `window` | glass, reflective; alpha blended when its alpha is under 1 |
| `chrome`, `metal` | its base colour, shiny and reflective |
| `brake`, `tail` | rear lamps, lit while braking |
| `head`, `lamp`, `light` | lamps, self-lit |
| anything else | lit with its base colour (trim, grille, tyres...) |

A material whose alpha mode is **Blend** is drawn see-through; one whose alpha
mode is **Mask** (Blender: "Alpha Clip") is cut out where its texture's alpha
is under the cutoff, for grilles and spokes.

The body is lit with GT3's car lighting and gets GT3's reflection pass on paint
and glass. Wheels, tyres, the ground shadow, lamp glows and camera points still
come from the donor's car file; the shadow is stretched to the car's size.

## Adding wheels

New rims for the wheel shop, or for a mod car to wear. Each one is a file in
the mod's `wheels/` folder with a model of the rim:

```ini
; mods/my_wheels/wheels/mesh_racer.ini
[wheel]
label = mesh_racer             ; optional: the file name without .ini
brand = BBS                    ; the wheel shop maker it is sold by
model = models/mesh_racer.glb  ; the rim, relative to the mod folder
picture = pictures/mesh_racer.png ; its 52 x 52 tile in the shop and the garage
```

- **The label** names the wheel in saves, replays and `[model] wheel`: up to
  32 lower case letters, digits and `_`. It must not be one of the disc's
  wheels (`gt3r --browse wheel/` lists them); when two mods give the same
  label, the higher priority one's wheel is used.
- **The brand** puts the wheel on that maker's page of the wheel shop: one of
  the disc's makers (Bridgestone, BBS, Dunlop, Falken, Enkei, OZ Racing, RAYS,
  Speedline, Yokohama; "OZ" is enough). Any other name opens a new maker in the
  shop, at the end of its list of makers, with the name written in the menu
  font. Give it a logo for its page (480 x 144 PNG, shown above the list):

  ```ini
  [brand]
  logo = pictures/my_brand.png
  ```

- **For sale:** `for_sale = no` keeps a wheel out of the shop, for a mod car
  that wears it (`[model] wheel = mesh_racer` in the car's file). Such a wheel
  needs no brand.
- **Price:** every wheel costs what the disc's do (500 Cr).

Bought mod wheels go to the garage's wheel list like the disc's, and the car
wears them in the menus, in races and in its replays. When the mod is off or
removed, the cars on its wheels show their own wheels again (a saved car keeps
the label, so turning the mod back on brings the wheel back). In online races,
players who do not have the wheel's mod see the car on its own wheels.

### The rim model

The rim is a binary glTF file (`.glb`), read as car bodies are (see "Car
models": PNG textures, materials recognised by name, Blend and Mask alpha).
The game draws it as it draws its own rims: it sizes it to each car's wheels,
puts the tyre around it, turns it and mirrors it for the right side. So model
it as the game's own rims are made:

- **Unit size:** 1 unit is about the rim's radius (the game scales it by the
  tyre's radius less three quarters of its sidewall; its own rims reach about
  1.02 units from the axle).
- **The axle along X**, the centre of the wheel at the origin, the outer face
  of the rim on the plane x = 0 facing +X, the rest of it, the spokes' depth
  and the barrel, from there towards -X (the game's own reach about 0.12 to
  0.18 units deep).

The easy way to get all of this right is to start from one of the disc's
rims: export it, change it, and save it as your own:

```
gt3tool model <iso> wheel/enw001_s out/ --gltf          # the races' rim
gt3tool model <iso> menu/wheel/enw001_s out/ --gltf     # the menus' rim, more detailed
```

The disc keeps two rims for each wheel: a detailed one for the menus and a
simpler one for races (flat faces with cut-out spokes). A mod wheel can do the
same; every key is optional and any one is enough:

```ini
model = models/rim.glb         ; everywhere
menu = models/rim_menu.glb     ; dealers, garage, wheel shop (and "Showroom wheels in races")
race = models/rim_race.glb     ; races
```

The menus take `menu`, else `model`, else `race`; races take `race`, else
`model`, else `menu`. Name a material `chrome` or `metal` to give it the shine
and reflections of polished rims, and `paint` or `body` for a painted, glossy finish in the colour you give it.
The tyre comes from the car, as with the disc's wheels.

The Mods screen lists each wheel under its mod, with its maker or why it did
not load (a missing model, a label the disc already has...).

## Adding events

GT Mode events of your own: one or more races, each on a course of the disc with
its laps and its opponents, entered as single races or run as a championship,
with entry rules, prize money and a prize car.

### The Event creator

**Event creator** on the main menu makes them without writing a file:

- **New event**, then its **title** and **description**, a **picture** (any PNG;
  it is cropped and scaled to 640 x 448 and fills the screen behind the event),
  an **icon** (any PNG; cropped to the league page's 51 x 27 strip buttons, the
  picture squeezed into them when there is none), the **league** GT Mode lists it in, single races or a **championship**, the
  license it needs, the **car rules**, the **prizes** (per place, the champion's
  bonus, a prize car in a color of your choice or a random one) and the **opponents**.
- **Car rules** are GT3's own: the car type (race and rally cars, tuned cars,
  stock road cars), the drivetrain, the engine (NA or turbo) and the cars
  allowed. The cars allowed are a list, as the disc's one-make and era series
  have: add cars one at a time, or set the online lobby's filters (class C, B,
  A, S or rally, a power limit, mod cars or not) and **Generate list** from the
  cars that pass as the dealers sell them. The page lists the disc's cars that
  may enter.
- Each **race** has its course, laps and **skill**: how its opponents drive,
  the league's own races' driving or the arcade's Easy, Normal, Hard or
  Professional. As in GT3, one skill drives the whole field of a race. A race
  draws its opponents from a pool of up to 32 cars, as GT3's races do: as many
  as it sets (up to 31, as the course's start grid has room), in the draw's
  order, so a bigger pool gives a different field each time; you start behind
  them. It can draw more cars than the pool has: once every car was drawn, the
  rest are picked from the pool at random, so a pool of one car makes a
  one-make race. A race uses the event's opponents (every round of a championship does)
  or, in single races, cars of its own.
- Each car of a pool is **stock**, a **version from the disc** (a tuned car as
  one of the disc's races fields it: "As in Sunday Cup"), or **your tune**:
  the parts the tune shops sell for that car, category by category. It has a
  colour of its own, or one drawn at the start. The page shows its power and
  weight as built.
- Every change is saved as you make it, in `mods/my_events/` (`events/<name>.ini`,
  `pictures/<name>.png`, `pictures/<name>_icon.png`): GT Mode lists the event the next time you go there.
  The pages that edit part of an event (a race, its cars, a car, the prizes,
  the car rules) have **Save** and **Cancel**: Cancel puts back what the page
  held when it opened.
  The title, the description and the prize amounts are text fields: a click or
  a tap types in place (a phone's own keyboard); chosen with a controller, or in
  VR, they open the game's keyboard, a number keypad for the amounts (Square
  deletes, Start finishes).
  **Export .zip** saves the event as a mod of its own; another player installs it
  with the Mods screen's **Install from .zip**.

The choices come off the disc that is open: its courses, its cars (mod cars
too) and the leagues of its GT Mode race map. Prizes are capped at what that
disc's own races pay.

### In GT Mode

An event shows on its league's page like the disc's series: its icon in the
strip, its picture full screen, its title and the results so far. Behind it are its race
page and entry page, copied from a disc series with room for its races, with the
course logos, race information, preview and results of the disc's own. You enter
with your active car, which must meet the license and car rules, checked as
GT3 checks its own events.

An event whose league the open disc doesn't have (or that names none) goes to
**Custom Events**, a league added under the disc's own on the race map.

Results are kept in the save. Prize money is paid like a disc race's. Prize
cars work like the disc's: one prize car is given once, for winning the
championship, or for winning every race of single races. Two to four go to the
roulette, which picks one on every win of a championship or of a one-race event,
as the disc's championships and endurance races do. Single races of more than
one race give one prize car. Your events never count for a league, the endings
or the game's completion.

### The file

`mods/<id>/events/<name>.ini`:

```ini
[event]
title = Hot Hatch Cup
description = Three rounds for front-wheel-drive hatchbacks.
picture = pictures/hot_hatch_cup.png   ; relative to the mod folder; 640x448 shown
icon = pictures/hot_hatch_cup_icon.png ; the strip button, 51x27 shown (default: the picture)
league = r/beginner/beginner           ; the league page on the race map (see below)
championship = yes                     ; no: single races
prizes = 5000, 3000, 2000, 1000, 500, 250 ; credits for 1st .. 6th
bonus = 10000                          ; the champion's bonus
prize_cars = r34_gtr_v_99:3, vitz_euro_00 ; optional: up to 4 car labels, each :paint id (none: a random paint)
points = 10, 6, 4, 3, 2, 1, 1, 1       ; optional, championships: points by place (default: GT3's 10, 6, 4, 3, 2, 1)
base = Galtezza_e_0501                 ; optional: a disc race whose rules fill the rest

[entry]
license = b            ; none, b, a, ib, ia, s, r
drivetrain = ff        ; any, ff, fr, 4wd, mr, rr
aspiration = any       ; any, na, turbo
cars = any             ; any, race, tuned, stock
allowed = vitz_euro_00, demio_glx_00   ; optional: only these cars may enter

[field]               ; the pool of every race without its own, and of a championship
opponents = vitz_euro_00:30, sun_e_demio_glx_00, tune:hot_mira:206   ; car:colour id (none: drawn at the start)

[tune:hot_mira]       ; a tuned car: tune-shop items, the rest the car's own
car = mira_tr_xx_avar_97
parts = sus_semi_racing, nat_2, wtr_1, tir_soft

[race1]
course = trialmt
laps = 2
skill = league         ; league, easy, normal, hard, professional
cars = 3               ; opponents it draws, 1 to 31, repeating the pool's cars when it has fewer (default: as many as the pool has, at most 5)
start = rolling        ; league, standing, rolling (default: league, the base race's)
tire_wear = on         ; league, off, on (default: league, the base race's)

[race2]
course = midfield
laps = 3
skill = hard
opponents = r34_gtr_v_99   ; a lineup of its own (single races only)
```

- Up to 10 races, `[race1]` to `[race10]`, and up to 31 opponents per race (32
  cars with yours; a pool holds up to 32 cars). The original's races have 6 cars;
  a bigger field lines up behind the course's own grid, two by two along its
  racing line. Places past 6th win no prize money, and no championship points
  unless the event's `points` give them some.
- A pool entry is a car label (`gt3tool car`, or the creator's car page; mod
  cars work too), a car variant of the disc (an opponent as its races field it,
  the labels in their pools), or `tune:<name>`. A colour is a `carcolor.db` id,
  as GT3's records keep each entry's paint (the creator writes them); without
  one the race start draws the paint.
- A tune's `parts` are tune-shop items by their menu names (`sus_sports`,
  `sus_semi_racing`, `sus_racing`, `nat_1`..`nat_3`, `tbk_1`..`tbk_4`,
  `wtr_1`..`wtr_3`, `tir_sports`..`tir_super_soft`, `muf_*`, `clt_*`, `fly_*`,
  `tms_*`, `lsd_*`, `icl_*`, `bkt_racing`, `cpu_on`...); a part the car has no
  record for is left out. The tune becomes a car variant (table 31) with the
  settings the car gets with those parts, as the disc's opponents are.
- Everything a race keeps is a field of GT3's own event record (table 32) or its
  regulation (table 33): a pool becomes the race's entry pool with its colours,
  `cars` its car count, a tune a car variant record,
  `allowed` a regulation (as many cars as a disc regulation holds). GT Mode runs
  the races as it runs the disc's: the same opponent draw, entry check, race
  information, prize pages, results and championship points. A race's `skill` copies the driving percentages of the
  arcade race of that difficulty (Easy `AEST0001` .. Professional `APST0001`, on
  dirt `A?AD0001`) into the race; `league` keeps the base race's.
- `start` and `tire_wear` copy the disc's own races' bytes: `rolling` an
  endurance race's start (the field sets off at 80 km/h and you take the wheel
  after 5 seconds), `standing` a series race's; `on` an endurance race's tire
  wear (GT3 has one wear curve), `off` a race without wear. Races on dirt never
  wear their tires, as in GT3.
- `prize_cars` is a prize set as the disc's: a car listed twice comes up twice
  as often in the roulette. An older file's `prize_car` and `prize_paint` still
  read as one prize car.
- `points` is the one value GT3's records can't hold: the original's points
  table is part of its program. It is ours, for fields bigger than GT3's six
  cars; places past the list score none.
- Credits are written as a US or European disc counts them; a Japanese disc pays
  them 100 times larger, like its own prizes. They are stored in hundreds.
- League pages are the disc's own names: `r/beginner/beginner`, `r/amateur/amateur`,
  `r/pro/pro`, `r/dirt/dirt`, `r/end/endurance` on the GT3 discs.
- Each race is a copy of a disc race (`base`, or a GT Mode race of the disc)
  with the file's values over it: its catch-up (and, with `league`, its start,
  tire wear and its opponents' driving) come from that race. A course with a dirt surface needs dirt tyres.
- A course or a car the open disc lacks: a car is left out of its lineup; an
  event whose course is missing, or with no opponents left, isn't listed.
- The race codes are `X<mod>_<name>_0001`... (a championship `..._0500`, its rounds
  `..._0501`...), so two mods can't clash. Renaming the file or the mod folder
  starts its results over.

## Paint colours

A mod can give any car new paint colours. A colour is a name, an RGB value and
the finish of one of the car's own paints: no pictures at all. The game makes
the car's paint textures in the new colour from the car's own paints when the
car loads, so panel lines, shading and highlights stay as the disc draws them.

```
mods/
  my_colours/
    liveries/
      sunday_blue.ini
      r34_orange.ini
```

```ini
[livery]
name = Sunday Blue      ; shown in the paint lists (none: the colour's #RRGGBB)
author = you
model = am0006          ; the body it is for: every car on that body gets it
color = #1E3A8A
finish = 4              ; optional: the car's paint (0 = its first) whose finish it takes
```

- **model** is the car's model, the name of its body file `cars/day/<model>`.
  The **Car viewer** (Developer options) lists every model with the name of a
  car on it (`ni0137  SKYLINE GT-R Vspec`). Several cars share a body (a car
  and its race versions); they all get the colour.
- For a car added by a mod, **model** is `mod:` and the car's label
  (`model = mod:my_car`): its colours follow its own `[paints]`, and **finish**
  is one of those (0 = the first). On a glTF body the colour is simply its paint
  materials' colour.
- **color** reads like the colours of the disc's own paint chips: the car's
  paints show on the body somewhat darker than their chips, and so does a new
  colour, by the same amount for that car.
- **finish** picks the material (gloss, metallic) and reflectivity of one of
  the car's paints. Without it the paint nearest in colour gives its finish.
- The colours follow the disc's paints in every list of a car's paints: Quick
  race (your car and the opponents), the arcade car select, the online car page.
  GT Mode's dealers keep the disc's paints.
- Online, a car in a custom colour sends its colour along, so other players see
  it without the file. Players on an older version see the finish paint.
- Replays keep the colours, even after the file is removed.
- A car whose paints are whole liveries (race cars with sponsors and stripes)
  recolours less well: its paints differ in more than their colour.

- **Sharing one**: the file holds no game data, so a zip with just
  `liveries/<name>.ini` (no `mod.ini` needed) is the whole mod; **Install from
  .zip** takes it. The colour goes on the player's own disc car.
- **With a texture pack**: the pack still replaces the car's textures in the
  disc's paints. In a custom colour the car shows its disc textures recoloured:
  the pack's pictures are keyed on the disc's paints and don't match them.

`gt3tool model <iso> cars/day/<model> <outdir> --color=#1E3A8A[,finish]` exports
the car in a colour (OBJ and PNGs) to try one out. A file the game refuses (no
model, a bad colour, a finish the car doesn't have, a model not on the disc)
is skipped, and the log says why.

## Liveries

A livery is pictures painted over a car's paint: stripes, numbers, logos, or a
whole new design. It goes on the player's own disc car, or on a car added by a
mod (see "On mod cars"), picked like a paint, and holds only what its author
painted.

1. **Make a template** from your disc: **New livery** on the Mods screen
   (choose the car and the paint to paint over). It writes a livery folder into
   the mod `my_liveries` (`liveries/<car>/`) and offers to open it. Or with the
   tool: `gt3tool livery-template <iso> <model> my_livery --paint=0 --scale=4`.
   The folder holds:
   - `body-set0-13-<key>.png`: the car's textures in that paint, enlarged 4×,
     one picture per texture. `body-` pictures are the ones the paint colours
     (on a car of one paint, all of them); `part-` ones are the rest (lamps,
     badges, glass).
   - `guides/`: the same pictures with the car's triangles drawn on them,
     filled by the side they face (blue left, orange right, green top, grey
     bottom, red front, purple rear). GT3 packs each texture with pieces from
     all over the car.
   - `livery.ini` to fill in.
2. **Paint** in any editor, at the template's size or larger (a multiple of
   it), on a layer over the picture or straight onto it. Leave the file names
   alone: they say which texture each picture covers. Open the Mods screen
   again (or restart) to see your changes on the car.
3. **Share** it by playing online (see below), or zip `liveries/<name>/` for
   **Install from .zip**. A zip carries the pictures as they are: delete the
   ones you didn't paint first, since they are the disc's own textures.

```ini
[livery]
name = Sunday Cup #12
author = you
model = ni0137
paint = 0              ; the paint the template was made in: keep it
; color = #1E3A8A      ; optional: a colour of your own under the pictures
```

- **What covers the car**: a picture's transparent parts show the paint, and
  so do the parts that still match the template. You can paint straight onto
  the template and only what you changed goes on the car. The car keeps its
  own reflections and cut-outs (wheel arches, grilles).
- **One texture, two sides**: many cars use one picture for both sides,
  mirrored (the guide shows both sides' triangles on top of each other). Text
  painted there reads backwards on one side.
- **Where it shows**: everywhere the car is drawn. The race car, its evening
  and night versions and the showroom model take the pictures directly (they
  share the textures); the simpler models the race draws at a distance get the
  livery carried over from the detailed body, so you paint it once.
- **Texture packs**: a livery goes over the pack's picture where there is one.
- **Online**: other players get your livery when you race them. Only what you
  painted is sent (the parts that still show the template are left out), each
  player's game checks it against its own disc, and it is kept in their mod
  `downloaded_liveries`. Settings → Online → **Other players' liveries**:
  Download, Ask (the default: a prompt for each one), or Off (their cars show the livery's paint).
- **Replays** keep the livery while it's installed.

### On mod cars

A mod car can have any number of liveries: one car, one model, and a livery per
team, each picked from the car's paint list after its `[paints]`. They can ship in
the car's own mod (`mods/my_car/liveries/<name>/`) or in another one, and new ones
can be added later without touching the car.

```ini
[livery]
name = Team Blue #7
author = you
model = mod:my_car     ; mod: and the car's label
paint = 0              ; one of the car's [paints] (0 = the first): what shows where nothing is painted
```

The template comes from the car's own model: choose the car in **New livery**, or

```bash
gt3tool livery-template game.iso mod:my_car my_livery --mods=mods
```

- A **glTF body**'s pictures are named `body-<texture>-<key>.png`: one per embedded
  PNG the model draws with texture coordinates, at its own size (small ones are
  enlarged), `body-` for the textures paint materials use. Paint materials without a
  texture get a blank 1024x1024 picture when their texture coordinates are a layout.
  The menu, race, evening and night models are all covered: a texture they share is
  painted once.
- A picture shows as it is painted: the paint colour tints only what the livery
  leaves unpainted. For a complete design (a car converted with several team
  textures), save each team's texture under the template's file name, at any size up
  to 4096x4096.
- The key is the embedded picture's, whatever the paint. A new version of the car
  whose texture changed needs new templates; changing `[paints]` or the geometry
  doesn't.
- A car on a **shell borrowed from the disc** (no `[model]`) takes liveries the way
  disc cars do, on `mod:<label>` and on that car only; the donor's own liveries stay
  on the donor.
- **Online**, the livery follows the car: a player without the car is offered the
  car first, then the livery. Games before this version show the car in the
  livery's `paint`.

## Menu music

The menu tunes aren't recordings: the game plays them from notes
(`music/music.seq` with the instruments in `music/music.ins`). A mod can put a
recording in place of any of them: a `.wav`, `.flac`, `.ogg` (Vorbis) or `.mp3` file in
`files/music/`, named after the tune.

```
mods/
  my_menu_music/
    files/
      music/
        main-01.flac               <- the title menu
        dealer-01.wav              <- the dealers
```

The names are the tunes' titles in the **Jukebox** (`main-01`, `tune-01`,
`lisence`, `dealer-01`, `wash-01`, `option`, `menu-03b`, `mapmenu`, `home2`);
the ids the game asks for (`gtmenu01`, `dealer01`...) work too. Capitals don't
matter. The Jukebox marks a tune with a recording `MOD`.

A recording loops like the tune it replaces. Without loop points the whole file
starts over at its end. To play an intro once and then loop the rest, give the
file loop points (in samples, counted from the start):

- **WAV**: a `smpl` chunk (the first loop is used), as audio editors and sample
  tools write it.
- **FLAC** and **Ogg**: the tags `LOOPSTART` and `LOOPLENGTH` (or `LOOPEND`, the first sample
  after the loop).

MP3 files always loop whole; their encoder padding leaves a short gap at the
loop, so use WAV, FLAC or Ogg for a seamless one. The recording plays at the menu
music's level and follows the Music volume; it isn't evened out, so match the
loudness of the tunes around it. A file that doesn't decode leaves the original
tune playing (the log says why).

## What can be modded today

| Kind | Status |
|---|---|
| Music (`.ads`) | Works now: replace tracks; the jukebox and the game pick them up. Titles, artists and the song markers that time the replays' Video-mode cuts still come from `bgm/ads.inf` (music §8); the Options music list previews the intro file `bgm/i_<file>` |
| Menu music | Works now: a WAV, FLAC, Ogg or MP3 recording in place of any sequenced menu tune, with its own loop points (see "Menu music") |
| Textures | Works now: PNG texture packs at any size (see "Texture packs"), PCSX2 packs converted; whole `.img` files through `files/` too |
| New cars (`cars/*.ini`) | Works now: data-only cars on a disc car's shell or their own glTF body, including electric ones, sold at GT Mode dealers, new makers included (see above) |
| Cars from your other discs | Works now: any disc car, Gran Turismo Concept's included, exported whole as a mod (see "Exporting cars") |
| Paint colours (`liveries/*.ini`) | Works now: new colours for any car, computed from its own paints, shown online and kept in replays (see "Paint colours") |
| Liveries (`liveries/<name>/`) | Works now: pictures painted over a disc or mod car on a template of it (New livery on the Mods screen), any size, shown at every distance and sent to the players you race online (see "Liveries") |
| Wheels (`wheels/*.ini`) | Works now: new rims as glTF models, sold at the wheel shop by its makers or new ones, or worn by mod cars (see "Adding wheels") |
| GT Mode events (`events/*.ini`) | Works now: your own series and championships on the disc's courses, with entry rules, prizes, prize cars (with the roulette), starts, tire wear and points, listed on a league page; made with the Event creator or by hand (see "Adding events") |
| Any other file | Overrides are loaded wherever the game reads the file (menus, models, courses, sounds, databases, movies) |

## Planned

- **Data patches**: small text files that change individual database
  records (e.g. one car's power or price) instead of replacing whole
  databases, so several mods can combine.
- **New content**: courses as new entries.
- **Scripting** (Lua) for rule sets.

## Rules

Mods should only contain your own work. Don't distribute files extracted
from the game (an exported car, a texture dump).
