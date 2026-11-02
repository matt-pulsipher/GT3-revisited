#pragma once

// The brake parts GT3 draws inside a race car's wheels, seen through the spokes: a rotor
// image, a caliper image and the rotor's glow when it is hot. race/brake.bin holds the
// images; each car file's GTCI holds which ones its axles use and where. The menus' wheels
// have none. See docs/formats/car_model.md "Brake parts".
//
//   race/brake.bin ("GTBR", little endian):
//     0x00 "GTBR"   0x04 u32 0 (the relocation base at run time)   0x0C u32 file size
//     0x10 u32 -> Tex1 set
//     0x14 u32 first rotor texture (1)   0x18 u32 first caliper texture (8)   0x1C u32 glow texture (0)
//     0x20 u32 -> rotor table, 16-byte records up to 0x24's table: u32 flags (bit 0: the rotor glows)
//     0x24 u32 -> caliper table, 32-byte records up to the Tex1 set: f32 radial half size, tangential
//                 half size, radial centre, tangential centre (in brake radii)
//
//   GTCI +0x14 (front axle) and +0x28 (rear): 0x14-byte records
//     +0x00 u32 caliper: record (value - 2) of the caliper table; 0 or 1 = none
//     +0x04 u32 rotor: record (value - 1) of the rotor table
//     +0x08 f32 brake radius (m)
//     +0x0C f32 the parts' offset along the axle from the hub centre (m, negative = towards the outer face)
//     +0x10 f32 caliper angle (degrees)

#include "scene/model.h"

#include <array>
#include <vector>

namespace gt3r {

// One axle's brake record from the car file's GTCI block.
struct BrakeRecord {
    int caliper = -1;  // caliper table record, -1 = none
    int rotor = -1;    // rotor table record, -1 = none
    float radius = 0;  // m
    float offset = 0;  // m along the axle from the hub centre; negative = outward
    float angle = 0;   // radians
};

// The front and rear records (GTCI +0x14 and +0x28); both empty when the block is too short.
std::array<BrakeRecord, 2> parse_brake_records(ByteSpan gtci);

// race/brake.bin.
struct BrakeParts {
    std::vector<scene::Texture> textures; // the Tex1 set, decoded (clamped, no mipmaps, as the GS samples them)
    int first_rotor = 1, first_caliper = 8, glow = 0;
    std::vector<u32> rotor_flags;                    // rotor table: bit 0 = the glow quad
    std::vector<std::array<float, 4>> caliper_place; // caliper table: radial / tangential half size, centre

    static bool is_brake_bin(ByteSpan data);
    static BrakeParts parse(ByteSpan data);
    bool has_rotor(int k) const { return k >= 0 && size_t(k) < rotor_flags.size() && texture(first_rotor + k); }
    bool has_caliper(int k) const {
        return k >= 0 && size_t(k) < caliper_place.size() && texture(first_caliper + k);
    }
    bool rotor_glows(int k) const { return has_rotor(k) && (rotor_flags[size_t(k)] & 1) && texture(glow); }

private:
    bool texture(int i) const { return i >= 0 && size_t(i) < textures.size(); }
};

// The rotor and caliper of one wheel in its hub frame: the car's axes (+X right, +Y up, +Z to
// the back), the origin at the hub centre, turned by the wheel's steering and camber but not its
// spin (the game counter-rotates them so they stay put). Mesh "brake": the rotor (texture
// first_rotor + k, alpha tested GREATER 0x40), then the caliper (first_caliper + k, blended
// (Cs - Cd) As + Cd, alpha tested GREATER 0x7F with failing fragments still blended without
// depth), lit with the tyre material (`shininess`: GTTR +0x18), facing out of the wheel.
// Empty without a rotor. `right`: a right-hand wheel (its outer face towards +X).
scene::Model build_brake_parts(const BrakeParts& bin, const BrakeRecord& rec, bool right, float shininess);

// The rotor's glow (rotor table bit 0): a square of the glow texture over the rotor, 1 mm
// outward of it, in the wheel's spinning frame (as the tyre: the hub frame turned by the
// spin). The game adds it unlit, Cs x FIX + Cd, without depth writes (brake_glow_fix).
// Empty when the rotor does not glow.
scene::Model build_brake_glow(const BrakeParts& bin, const BrakeRecord& rec, bool right);

// The glow's weight: GS FIX = trunc(clamp(128 heat^2 x gain, 0, 255)), returned / 128 (0 .. 255/128).
// `heat` is the wheel's brake heat (0..1), `gain` the course's Env0 +0x4C.
float brake_glow_fix(float heat, float gain);

} // namespace gt3r
