# Example Coupe: a complete mod car

This folder is a working mod that adds the **GT3R Concept Coupe**: a
front-engined, rear-drive 2+2 with a 3.0-liter V6, a six-speed manual and
22-inch wheels. Its electric version, the **Concept Coupe E**, shares the body. It
is meant to be copied. Every file is small and commented, so you can see how each
part of a car mod works and then replace it with your own.

It shows:

- a car defined as data (`cars/gt3r_concept_coupe_24.ini`): dimensions, weight,
  engine curve, gearing, tires, paints, dealer and arcade settings, with a comment
  on every value
- an electric car (`cars/gt3r_concept_coupe_e_24.ini`): the same body with a motor
  on each axle and one reduction gear, commented where it differs (see "The
  electric version")
- a body of its own (`models/concept_coupe.glb`, binary glTF) with materials
  named for the game's loader, and wheels drawn by the engine from the tire sizes
- a new maker with its own dealer (`makers/gt3r.ini`)
- dealer pictures rendered with the game itself (`images/`)
- the scripts that turned a downloaded model into the game-ready body
  (`source/`)
- performance fitted to real-world targets with `gt3tool modcar`

The format is described in [docs/MODDING.md](../../../docs/MODDING.md) ("Adding cars").

```
example_coupe/
  mod.ini                          the mod's name, author and priority
  cars/gt3r_concept_coupe_24.ini   the car
  cars/gt3r_concept_coupe_e_24.ini its electric version
  makers/gt3r.ini                  a maker the disc doesn't have, with a dealer
  models/concept_coupe.glb         the body, shared by both (CC-BY 4.0, see Credits)
  images/concept_coupe.png         dealer picture, 640x448
  images/concept_coupe_thumb.png   strip button and arcade icon, 51x27
  images/concept_coupe_e*.png      the same for the electric version
  source/                          how the body and pictures were made (not loaded)
```

## Install

The example is not installed with the game; add it yourself when you want to
try modding. Zip the `example_coupe` folder and choose **Install from .zip** on the
Mods screen, or copy the whole folder into the `mods` folder of your user data folder
(the Mods screen shows where it is; **Open folder** opens it):

- Windows: `%APPDATA%\GT3Revisited\GT3Revisited\mods\`
- macOS: `~/Library/Application Support/GT3Revisited/GT3Revisited/mods/`
- Linux: `~/.local/share/GT3Revisited/GT3Revisited/mods/`
- Android: in the Files app, under **GT3 Revisited** > `mods`

A new mod is enabled (the Mods screen reads the folder again when you come back to
the game; Cross turns a mod off and on). Select **Example Coupe** there: the screen
lists the cars, or the reason one failed to load.

## Try it

- **Time trial** or **Quick race**: pick the GT3R Concept Coupe (or Concept Coupe E)
  from the car list.
- **GT Mode**: both are for sale at the GT3R dealer, on the Europe > Germany page of
  the car dealers, for 52,000 and 78,000 credits. The color picker offers the six
  `[paints]`.
- **Arcade**: each joins the class its power-to-weight ranks it in, at the end of the
  car list.
- **Car viewer** (`gt3r --screen cars`): the entries `mod:gt3r_concept_coupe_24` and
  `mod:gt3r_concept_coupe_e_24`.

## Make it yours

1. Copy the folder and rename it (the folder name is the mod's id).
2. Give the car a new `label` (and rename the `.ini` to match). Labels must be
   unique: a mod car can never replace a disc car, and two mods with the same label
   replace each other.
3. Change the numbers. Anything you delete is borrowed from the `donor`.
4. Replace the body, or delete the `[model]` section to use the donor's shell,
   stretched to your `[body]` dimensions.
5. Check it with `gt3tool modcar` (below), then look at it in the car viewer and on
   track.

## The body

The model is Khronos' CarConcept sample (see Credits), prepared with the scripts in
`source/`:

| Step | Script |
|---|---|
| Delete the wheels, tires, brakes, axles, engine, under-hood panels, pedals, small interior parts, the wiper blades and the steering-wheel emblem; decimate (213,347 to 40,063 triangles); join into one mesh; put the origin at the wheelbase midpoint on the ground; rename the materials | `prepare.py` (Blender, `blender -b --factory-startup --python prepare.py -- CarConcept.glb stage1.glb`) |
| Set each material's color, drop the PBR extensions and textures the game doesn't use, draw a plain "GT3R" license plate, repack the GLB (11.8 MB to 1.3 MB) | `finish.py` (Python 3, no dependencies) |
| Measure what goes into `[body]` | `measure_glb.py` |

The loader's conventions (docs/MODDING.md "Car models"): meters, +Y up, the car
facing +Z, the origin at the wheelbase midpoint on the ground. Materials are
recognized by name:

| Material | Role |
|---|---|
| `body_paint` | takes the chosen `[paints]` color, reflective |
| `window_glass` | tinted, alpha blended (alpha 0.62), reflective |
| `headlamp` | self-lit |
| `taillamp` | lights up under braking |
| `trim_gloss_black`, `trim_black`, `mirror_chrome`, `chassis_dark`, `interior_*`, `indicator_amber`, `plate`... | lit with their own base color; the roof, sills and hood vent stay black whatever the paint |

Avoid the role words in other names: a material called `underbody` would be painted.

The game uses base color factors as they are (no linear-to-sRGB conversion), so
`finish.py` sets them to the colors you want to see, not to PBR values. Wheels and
tires come from the engine: their size from `[tires]`, their positions from the
wheelbase and tracks, and the rim from `[model] wheel` (a disc rim). That is why
the body has none.

### Measuring the model

`measure_glb.py models/concept_coupe.glb` prints the body's extent. The modeled
wheels were measured in `prepare.py` before they were deleted: wheelbase 2,799 mm,
tracks 1,952 / 1,964 mm, tires 768 mm across on 22-inch rims. The `[tires]` sizes
were chosen to match that diameter, so the engine's wheels fill the arches.

## Tuning

```bash
gt3tool modcar game.iso examples/mods/example_coupe
```

loads each car, prints its specs and test-drives it on the game's own physics, on
the machine tests' course, next to the `[reference]` targets (magazine figures for
cars of its class: ~300 PS, 1,420 kg, rear drive, summer tires). Results:

| Test | Game | Target |
|---|---|---|
| 0-60 mph (1 ft rollout) | 5.08 s | 5.1 s |
| Quarter mile (1 ft rollout) | 13.48 s @ 107 mph | 13.5 s |
| Top speed (GT3's top-speed test) | 270 km/h | 270 km/h |
| 60-0 mph | 88 ft | |

How the values were chosen: with the first guesses (`output_pct` 112, `grip_pct` 102)
the car held 1.09 g on a 300 ft skidpad, far more than a road car, because the
donor's street tires on a car this wide grip like semi-slicks. `grip_pct = 88`
brought it to 0.94 g, but less grip also slowed the launch, so `output_pct` went up
to 118 (the R34 GT-R uses the same value). That put 0-60 and the quarter mile on
target, but the car topped out at 263 km/h, so the drag came down a little: `cd`
0.31 to 0.29 and the frontal area 2.10 to 2.06 m². The skidpad figures were measured
with an earlier version of the tool, which no longer drives a skidpad. Fit the trap
speed and top speed with power and drag first, then 0-60 with `output_pct`, and
repeat once.

## The electric version

`cars/gt3r_concept_coupe_e_24.ini` is the same car with `type = electric`
(docs/MODDING.md "Electric cars"). It shares the body file, the tires and the donor,
and its file comments only what changes:

- `[powertrain]` gives the motors' power, torque and top speed (`power_kw`,
  `torque_nm`, `max_rpm`) instead of an engine curve. The game builds the curve: full
  torque from standstill up to power / torque, constant power above it.
- `[drivetrain]` has one ratio, the reduction, and an AWD split (one motor per axle).
- The race drives it with the electric drive: no gear changes, regen when you lift
  off, and it stops and holds at a standstill. The tune shop sells it no engine,
  gearbox or driveline parts.

`gt3tool modcar` drives both cars. The electric one, against its `[reference]`
targets (its class: ~540 PS, 1,850 kg, two motors):

| Test | Game | Target |
|---|---|---|
| 0-60 mph (1 ft rollout) | 3.33 s | 3.3 s |
| Quarter mile (1 ft rollout) | 11.62 s @ 121 mph | 11.6 s |
| Top speed (GT3's top-speed test) | 250 km/h | 250 km/h |
| 60-0 mph | 93 ft | |
| Lift-off (regen) | 0.16 g from 100 to 50 km/h, 0.21 g from 50 to 10 | |

The first guesses landed on the targets, so nothing was fitted. `max_rpm` sets the
top speed directly: 15,800 rpm through the 9.0 reduction on the 768 mm tires is
250 km/h.

## Pictures

The dealer picture is a car viewer screenshot on the viewer's own dark backdrop,
cropped and scaled by `source/pictures.py` (Python 3 with Pillow):

```bash
GT3R_VIEWER="find=mod:gt3r_concept_coupe_24,clean=1,yaw=2.35,pitch=0.14,zoom=0.75" gt3r game.iso --screen cars --shot shot.png
python3 source/pictures.py shot.png images/concept_coupe.png images/concept_coupe_thumb.png
```

The electric version's, in its first paint, from a little further back (`zoom=1.0`,
so the crop stays inside a 2560x1440 screenshot):

```bash
GT3R_VIEWER="find=mod:gt3r_concept_coupe_e_24,clean=1,yaw=2.35,pitch=0.14,zoom=1.0" gt3r game.iso --screen cars --shot shot.png
python3 source/pictures.py shot.png images/concept_coupe_e.png images/concept_coupe_e_thumb.png
```

Add `key=1` to `GT3R_VIEWER` for a flat magenta background you can key out instead.

## Credits and license

**Body:** [CarConcept](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CarConcept)
by Eric Chadwick, Darmstadt Graphics Group (© 2024 Darmstadt Graphics Group GmbH),
licensed under [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/)
([legal code](https://creativecommons.org/licenses/by/4.0/legalcode)). It is derived
from ["FREE Concept Car 004"](https://sketchfab.com/3d-models/free-concept-car-004-public-domain-cc0-4cba124633eb494eadc3bb0c4660ad7e)
by Unity Fan (CC0). Modified for GT3 Revisited: the Khronos trademarks removed (the
license-plate logo replaced by a plain plate, the steering-wheel emblem deleted, the
tires with their sidewall logos deleted), the wheels, brakes, engine, wipers and most
of the interior removed, decimated, materials simplified and textures removed.
The Khronos logos are not part of the CC-BY license and are not included.

`models/concept_coupe.glb` is licensed under CC-BY 4.0, and so are the pictures in
`images/`, which show it. If you reuse them, keep this credit. Everything else in
this folder (the car data, the scripts and this text) is part of GT3 Revisited and
MIT-licensed like the rest of the repository.
