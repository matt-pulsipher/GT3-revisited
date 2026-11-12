#pragma once

// Ours: the light painted into a menu rim's image, evened out round the wheel's axis for races
// (issue 272).
//
// The menus' rims (section 4 of menu/cars/<car>, menu/wheel/<label>) are drawn lit with
// ambient 1, diffuse 1 and no specular on nearly flat faces, so their light and shade is the
// image's: the stock rims' images have one side of the spokes painted brighter. The menus
// never turn a rim, so that light stays put. Races with showroom wheels turn the rim with the
// wheel, and the painted light would turn with it. Races take the rim with its images evened
// out: on each ring of texels about the axis the first angular harmonic of each colour channel
// (the part going as the cosine and sine of the angle round the axis: one bright side) is
// divided out (light scales what it falls on), keeping each ring's mean, the spokes and the
// alpha.
//
// Each texel's place round the axis comes from the triangles drawing it: the texel's position
// inside a triangle's UVs, carried to the triangle's corners in the model, gives the point of
// the rim it shows, and so its distance from the axis (the model's x axis, which the wheel
// turns about) and its angle round it. Most rims are laid out as a projection along the axis,
// with the axis at the image's centre; some are not, and this follows each layout as it is.

#include "scene/model.h"

#include <optional>
#include <vector>

namespace gt3r {

// Where each texel of a rim image lies round the wheel's axis.
struct RimTexelPlaces {
    struct Place {
        float radius = -1; // from the axis, in texels (the face's size in the image); < 0: not placed
        float cos = 1, sin = 0; // the angle round the axis
    };
    u32 width = 0, height = 0;
    std::vector<Place> texels; // row-major
};

// The places of the texels of `wheel`'s texture `texture` that its lit (non-reflection)
// triangles draw, and of their neighbours (which bilinear filtering and the mipmaps blend in).
// Texels two triangles put at different places (an image area used twice) are not placed, nor
// are those triangles sample as one spot's flat colour (a spoke's side, the barrel).
RimTexelPlaces rim_texel_places(const scene::Model& wheel, int texture);

// Divides out each ring's first angular harmonic of each colour channel, rings one texel wide.
// Alpha is not changed; nor are texels not placed, near the axis, or on rings too sparsely
// covered to measure.
void even_ring_light(scene::Texture& t, const RimTexelPlaces& places);

// One-sidedness of an image's brightness round the axis: the first angular harmonic of
// luminance (each texel against its ring's mean), over the mean luminance. 0 = even; the
// stock menu rims of NTSC-U 1.10 measure about 0.04, the wheel shop's about 0.01.
double ring_light_one_sidedness(const scene::Texture& t, const RimTexelPlaces& places);

// even_ring_light on every image `wheel`'s lit triangles draw. Returns how many it changed.
int even_rim_light(scene::Model& wheel);

// The one-sidedness of the image `wheel`'s lit triangles draw the most of; nullopt when none.
std::optional<double> rim_light_one_sidedness(const scene::Model& wheel);

} // namespace gt3r
