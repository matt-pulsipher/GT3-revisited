#pragma once

// GT3's wheel effects in a race (docs/formats/car_model.md "Brake parts" and "Wheel spin
// blur"): each wheel's detail level from its size on screen, which wheels show their brake
// parts, and the main view's spin blurs. The blur covers the wheel's turn over a time that
// shrinks as the wheel's centre moves across the screen; the camera's car blurs its tyres
// (shifted copies of the tread texture) and its rims (rotated copies of the rim image), the
// other cars' rims switch to the wheel file's spinning image above 3 degrees a frame.

#include "base/bytes.h"

#include <array>

namespace gt3r::scene {
struct Model;
}

namespace gt3r::race {

// The course's values (Env0, course slot 32).
struct WheelEnv {
    float blur_time = 0.8f;      // +0xEA0: the blur's time (in steps) for a wheel that stays put on screen
    float blur_falloff = -20.0f; // +0xEA4: its falloff with the wheel centre's movement on screen
    float glow_gain = 0;         // +0x4C: the brake rotors' glow, heat^2 x this
    static WheelEnv parse(ByteSpan env0); // defaults when it is not an Env0 block
};

// The wheel's detail level from its size on screen: s = proj_x x bounding radius / depth (proj_x:
// the projection's x scale; the bounding radius sqrt(R^2 + W^2 / 4)); 0 above 0.09, 1 above 0.03,
// 2 above 0.015, else 3. Levels 0 and 1 have the brake parts and the tread blur, 0..2 the rim.
int wheel_detail(float proj_x, float bound_radius, float depth);

// The blur's turn (radians): the wheel's spin from `time` x exp(falloff x moved) steps back to now,
// i.e. -spin_step x that, from its last step's turn and the distance its centre moved on screen
// (normalised device coordinates) since the last frame.
float wheel_blur_turn(float spin_step, float moved, const WheelEnv& env);

// Whether the tyre's texture is blurred (GTTR +0x14 flags): bit 0 asks for it; a tyre without
// bit 1 (its inner side black) only when the wheel's outer face is in view.
bool tread_blur_wanted(u32 tyre_flags, bool outer_face_seen);

// The tread blur's three shifts in texture S (1 = the texture's width, 128 texels), each pass
// half the last: trunc(turn / 2 x repeats x 1024 / pi / 2^k) & 0x7FF in 1/16 texels, k = 1..3,
// the turn negated on right wheels. The texture becomes the mean of the 8 copies shifted by
// every sum of a subset of them.
std::array<float, 3> tread_blur_shifts(float turn, u32 repeats, bool right);

// The rim blur's four angles (radians): turn / 2, / 4, / 8, / 16, negated on right wheels. The rim
// image becomes the mean of the 16 copies turned about its centre by every sum of a subset.
std::array<float, 4> rim_blur_angles(float turn, bool right);

// The mip level the rim blur reads: GT3 draws the copies into a square of min(width, 64) texels.
float rim_blur_lod(u32 texture_width);

// Showroom wheels (ours): the flat disc that stands for a modelled rim in the mask of its spin
// blur's filter (render::DrawItem::spin_mask_shape), measured once from the rim's geometry in the
// hub frame (the axle along x, each mesh's transform applied). `radius`: the farthest any vertex lies
// from the axle; `min_x`, `max_x`: the rim's two faces along it. A rim without vertices: radius 0.
struct RimDisc {
    float radius = 0;
    float min_x = 0, max_x = 0;
};
RimDisc rim_disc(const scene::Model& rim);

// The tyre's inner ring (the sidewall's inner edge, where it meets the rim), in the hub frame like
// rim_disc, from its lit pieces only (the black unlit faces close the inner side with a disc down
// to the axle): `radius`, the least any of their vertices lies from the axle; `min_x`, `max_x`: how
// far along the axle the ones within kTyreRingBand of that radius reach on each side. A tyre
// without lit pieces: radius 0.
inline constexpr float kTyreRingBand = 0.01f; // (metres)
struct TyreRing {
    float radius = 0;
    float min_x = 0, max_x = 0;
};
TyreRing tyre_inner_ring(const scene::Model& tyre);

// The shape a spinning rim's filter mask draws for the face on the axle's -x side (`minus_x`) or
// its +x side, in the hub frame: the rim's disc (rim_disc) `out` beyond the rim's farthest vertex on
// that side, and a sidewall from the disc's edge to the tyre's inner ring on that side (a cylinder,
// or a shallow cone where the two radii differ), so that a rim seen from the side, its lip beyond
// the disc's outline, is covered to the tyre. `segments` round; one mesh, double-sided,
// untextured and unlit (the mask keeps only its coverage). Without a ring (radius 0), or with the
// ring only across the rim's middle from that face: the disc alone.
scene::Model spin_mask_shape(const RimDisc& rim, const TyreRing& tyre, bool minus_x, float out, int segments = 48);

// The other cars' rims show the wheel's spinning image when the blur's turn passes 3 degrees.
bool rim_spinning_image(float turn);

} // namespace gt3r::race
