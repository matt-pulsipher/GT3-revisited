# Custom paint colours

New paint colours for any car, defined by a name, an RGB value and the finish of one of the
car's paints (mods' `liveries/*.ini`, docs/MODDING.md "Paint colours"), and liveries: pictures
painted over a car's textures (`liveries/<name>/`, "Liveries"). Public discussion 381.

## Computing a colour (`gtm1::ComputedPaint`)

A car's paints are Tex1 paint variants (`gtm1::paint_variants`): each changes only which CLUT
a texture reads (TEX0 CBP, CSA, CPSM), never the texels. So a texel is the same palette entry
under every paint, and the set of a texel's colours across the paints says how that entry
follows the paint.

`decode_texture_set` with a computed paint decodes every texture of the set under every paint,
then:

1. **Body colour of each paint** (`paint_body_colours`): the mean of the texels that change
   with the paint, over the whole set.
2. **The new colour's body colour** (`computed_body_colour`): carcolor.db's colours are only
   labels. On the R34 V-spec (ni0137) the paint labelled #D5D5D5 has its most common body texels
   between #444443 and #727270, and the one labelled #797979 between #191919 and #5B5B5B. The new colour is read as such a label and scaled by the car's
   own ratio: the paints' summed body luminance over their summed label luminance (clamped to
   0.2 to 2).
3. **Each changing texel** (`computed_paint_texels`): texel = s × body colour + h, with one s
   and h for its three channels (the shading under the paint and a grey highlight), fitted by
   least squares over every paint and channel, each paint weighted by 1 / (d² + 400), d its body
   colour's distance from the new one. Texels that never change (trim, lamps, glass) keep their
   value; alpha (reflectivity, cut-outs) is the finish paint's.

The finish also picks the material set (`Gtm1::materials_for`). Eve and night car files and the
menus' models have their own textures and variants: each set is fitted on its own, so their
darker palettes follow.

## Measured on the disc (`custom_paint_leave_one_out_disc`)

Each car's paint k computed from its other paints, given paint k's body colour, against the
disc's own paint k. Mean absolute error per channel (8-bit) over the texels that change, US 1.10,
170 cars with 3 or more paints:

| Model | Mean | Median car | 90th percentile car |
|---|---|---|---|
| The nearest paint as it is | 26.9 | 29.5 | 50.4 |
| Per-channel slope on carcolor.db colours | 20.5 | | |
| Shared s, h on carcolor.db colours | 16.8 | | |
| Shared s, h on body colours | 8.1 | 4.2 | 15.3 |
| The same, nearer paints weighted (used) | 7.6 | 4.1 | 14.5 |

The worst cars (wl0001, ml0002, da0032) are race cars whose paints are whole liveries: what
one paint draws cannot be predicted from the others.

## Liveries (`gtm1::overlay_livery`, `race/livery_template.h`)

A livery folder is a scoped texture pack: `livery.ini` and PNGs named by key, loaded into a
`texture_pack::Pack` of its own. The keys are the textures' in the livery's paint (`paint = N`),
so `decode_texture_set` computes each texture's key in that paint (the computed colour's finish
when the livery has a colour) and lays the picture over the texture as decoded, after the global
pack's picture: rgb mixed by coverage, alpha the texture's (reflectivity, cut-outs), the larger
size per side (the texture enlarged: colour bilinear unless point-sampled, alpha nearest).

Coverage is the picture's alpha faded out where it still shows the template (the texture in the
livery's paint, nearest texel; 6 to 18 levels of difference per channel), so a canvas painted
over the opaque template covers only what was painted. A picture covering nothing leaves the
texture as it is: an untouched template changes no texel of the car
(`livery_template_round_trip_disc`).

The template (`make_livery_template`): the full-detail body's textures (LOD 0 of
`cars/day/<model>`) in the paint, one per key, enlarged nearest and opaque, "body-" for the ones
the paints recolour; guides with the UV triangles filled by facing (+X left, +Y up, +Z front).

Measured on the R34 V-spec (ni0137), paint 0: the template's 32 canvases (16 the paints
recolour) are, all 16 body ones and 31 of the 32, the same textures in the evening, night and
showroom files, so one painted set covers those four. The two lower-detail race sets share almost
none (their own layouts). The main side texture serves both sides, mirrored.

The lower-detail race models (`formats/model/livery_transfer.h`): `load_car_model` with a livery
builds the full-detail body with it as a reference (its textures carry `Texture::livery_cover`)
and fills every lower-detail texture the livery does not cover by key: each texel at twice the
texture's size, inside or within a texel of one of its triangles, takes its 3D point, the nearest
full-detail triangle with a livery that faces within 60° (a 6 cm grid; at most 6 cm away), and the
livery's colour and coverage at that point. The side texture serving both sides comes out right on
both, being found in 3D. R34, three levels of detail: 156 ms for the whole model with the transfer
(`livery_template_round_trip_disc`).

The app's **New livery** (Mods screen, `app/screen_new_livery.cpp`) writes the template into the
mod `my_liveries`. Online: `game/livery_package.h`, docs/dev/ONLINE.md "Liveries".

## Paint indices

Paint index disc count + k of a model is its custom colour k (`race::CustomPaints`): installed
ones first (mods in priority order, files by name), then those added for the session (received
online, or a replay's). Every paint list shows the installed ones after the disc's
(`CarPicker::paints`, `arcade_colors`), so the pickers, configs and race requests carry plain
indices. `load_car_model` asks the resolver `race::set_custom_paints` installs to turn an index
past the disc's paints into a `ComputedPaint`. The race's paint draw sees a custom colour as
its finish paint, so the others' colours keep away from it, and gets the custom index back
after. Replays (v14) store each custom paint's RGB, finish and livery id, which parsing turns into
this game's index (the installed livery, else the colour, else the finish). Online, see docs/dev/ONLINE.md "Custom colours".

Not yet: GT Mode (dealers and the garage keep the disc's paints: a garage car's colour is
written to PS2 saves), mod cars (their own `[paints]`), texture packs (a computed colour's
pixels are new, so pack pictures keyed on the disc's paints do not match them).
