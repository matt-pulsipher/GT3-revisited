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
  `files/`, `cars/` or `makers/`.
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
    files/
      menu/license_bg.img          <- replaces this file from GT3.VOL
      bgm/race_a05.ads             <- replaces a music track
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
```

A new mod is enabled. Enable or disable mods from the Mods screen; the choice is
stored in `config.ini` under `[mods]`.

## Workflow

```bash
gt3tool extract game.iso menu/license_bg.img work/
```
```bash
gt3tool tex game.iso menu/license_bg.img work/png/
```

Edit, re-encode and drop the result into `mods/<id>/files/...`. Then use
`gt3r --browse menu/license_bg.img` to check it quickly.

## Adding cars

**Start from the example.** [`examples/mods/example_coupe`](../examples/mods/example_coupe/README.md)
is a complete car mod to copy: a car file with a comment on every value, its own
glTF body with materials named for the loader, a new maker with a dealer, dealer
pictures rendered with the game, the scripts that prepared the model, and the
`gt3tool modcar` results it was tuned to. Copy it into your mods folder to try it,
then rename it and change it into your own car.

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
name = Model Y Performance
maker = TESLA                  ; matches an existing maker's name when there is one
year = 2026
price = 57490                  ; credits
category = production          ; production | race | rally
donor = legacy_gtb_96          ; required: a car on the disc (gt3tool car <iso> <label>)
model = sb0074                 ; optional: another body shell; default = the donor's
fit_body = yes                 ; stretch the borrowed shell to the dimensions below

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
cars never use them.

**For now an electric car races with its donor car's engine, gearbox and
drivetrain** (the stock ones): the race runs GT3's own physics, which has no
electric drive yet. The mods screen and the log say so, and the car still
sounds like its motor. The drive described below returns when the race has it.

- **Drive:** the motor drives the wheels directly through a single reduction.
  There is no idle, no clutch slip and no gear changes, and full torque is
  available from standstill. The default curve holds constant torque up to the
  base speed, then constant power, easing to 85 % at `max_rpm`. `max_rpm` also
  acts as the top-speed limiter.
- **AWD:** both axles get a fixed torque split (one motor per axle).
- **One-pedal driving:** lifting off brakes the car with the motor instead of
  engine braking: `regen_g` (default 0.2 g) at town speeds, power-limited at
  highway speeds like real cars. Near walking pace the friction brakes blend in,
  and the car stops and holds while neither pedal is pressed. A partly pressed
  accelerator blends between regen and drive, so there is a coast point at
  roughly 15-20 % pedal.
- **Sound:** a motor whine and an inverter sound are synthesised as engine sound
  banks (`src/audio/motor_sound.*`). They play through the normal engine sound
  path: pitch follows the motor, the inverter sound swells under load, and
  Doppler and panning work as usual.

### Checking a car

```bash
gt3tool modcar game.iso mods/my_cars
```

The command loads each file and reports errors and warnings. It then prints the
resulting specs and test-drives the car on the game's own physics, on the
machine tests' course: 0-60 mph (standing start and with a 1 ft rollout, like
US magazine tests), 0-100 km/h, the quarter mile, top speed (GT3's top-speed
test, driven by the race's AI), 60-0 mph braking and lift-off deceleration,
each next to your `[reference]` figures. An electric car is driven on its
donor's engine (above). `disc:<label>` arguments run disc cars through the same
test for comparison. Fit `power_kw` to the quarter-mile trap
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
wheel = ozw004_b               ; optional: a rim from the disc (gt3r --browse wheel/)
lower_mm = 10                  ; optional: move the body down (negative = up) to fine-tune ride height

[paints]                       ; in menu order; applied to the paint materials
Pearl White = #E9E9E6
Ultra Red = #9B1B22
```

The glTF loader is `src/formats/model/gltf.*`. Use glTF's own conventions:
metres, +Y up, the car facing +Z. Put the origin at the wheelbase midpoint on
the ground, like GT3's own car models. The loader handles triangle meshes,
node transforms, normals (computed when missing), one UV set, vertex
colours, base colour factors and embedded PNG textures. Materials are
recognised by name:

| Name contains | Treated as |
|---|---|
| `paint`, `body` | car paint: takes the chosen paint colour, reflective |
| `glass`, `window` | glass, reflective; alpha blended when its alpha is under 1 |
| `brake`, `tail` | rear lamps, lit while braking |
| `head`, `lamp`, `light` | lamps, self-lit |
| anything else | lit with its base colour (trim, grille, tyres...) |

The body is lit with GT3's car lighting and gets GT3's reflection pass on paint
and glass. Wheels, tyres, the ground shadow, lamp glows and camera points still
come from the donor's car file; the shadow is stretched to the car's size.

## What can be modded today

| Kind | Status |
|---|---|
| Music (`.ads`) | Works now: replace tracks; the jukebox and the game pick them up. Titles, artists and the song markers that time the replays' Video-mode cuts still come from `bgm/ads.inf` (music §8); the Options music list previews the intro file `bgm/i_<file>` |
| Textures in `.img` files | Works now (needs a Tex1 encoder; a PNG import tool is planned) |
| New cars (`cars/*.ini`) | Works now: data-only cars on a disc car's shell or their own glTF body, including electric ones, sold at GT Mode dealers, new makers included (see above) |
| Any other file | Overrides are loaded wherever the game reads the file (menus, models, courses, sounds, databases, movies) |

## Planned

- **PNG texture replacement packs** keyed by decoded-texture hash, so
  artists never have to touch PS2 formats.
- **Data patches**: small text files that change individual database
  records (e.g. one car's power or price) instead of replacing whole
  databases, so several mods can combine.
- **New content**: courses and events as new entries.
- **Scripting** (Lua) for custom events and rule sets.

## Rules

Mods should only contain your own work. Don't distribute files extracted
from the game.
