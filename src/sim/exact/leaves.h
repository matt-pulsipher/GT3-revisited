#pragma once

// The later builds' leaves (Gran Turismo Concept, every build [concept §15; physics §14]): fallen leaves a wheel throws up
// off ground whose road triangle has flag 0x20 (Autumn Ring's). The tire effect emitter (Concept 2001 Tokyo 0x1ED4F0, 2002
// Tokyo-Geneva Europe 0x1ED4B8: GT3's 0x1DF150 with one more call) hands each wheel's request first to the smoke and then,
// on such ground, to the particle manager's leaf maker (Rule::Leaves: 2001 Tokyo 0x19C4E8, Europe 0x19C578), which draws the
// VU0 random register once and, when the wheel's slip speed scaled by 0.5 .. 0.95 is 2 m/s or more, makes one leaf with
// twelve draws more (its init, Europe 0x19D230, draws the last three). So the leaves, like the sparks, are part of the
// race's state: they share the particle manager's pool of 128 with the sparks (sparks.h), a leaf the pool has no room for is
// not made. A leaf (LeafParticle, its update LeafParticle::vf1: Europe 0x19D308) is a small rigid plate: one of eight shapes (squares of
// 0.12, 0.13, 0.14 and 0.1 m, 1 g), its turn by three angles kept in 0 .. 2 pi, its spin under Euler's equations with the
// air's torque, its fall under gravity with the air's force on its plane; it is gone once its center is below the ground's
// plane by its shape's diagonal. The update draws nothing. Each line of leaves.cpp is one EE operation in the program's
// order; 2002 Tokyo-Geneva's constants are folded toward zero (build_math.h).

#include "base/game_random.h"
#include "sim/exact/car_record.h"

#include <array>

namespace gt3r::sim::exact {

struct TyreEffect;

// A leaf's shape (the manager's +0xC + 0x74 k, k 0 .. 7, built by Europe 0x19CF10 from its constructor's mass and sizes).
struct LeafShape {
    F mass;          // +0x04 (kg)
    F w, h;          // +0x08 / +0x0C its sides (m)
    F inv_mass;      // +0x10
    F diag;          // +0x14 sqrt(w^2 + h^2): how far below the plane the leaf is gone
    F ix, iy, iz;    // +0x18 .. +0x20 the moments of inertia about its axes (y its normal)
    F lift;          // +0x24 -2 w h: the air's force along the normal per unit of the normal's speed
    F drag;          // +0x28 -2 (w + h) 0.005: ... along its plane
    F damp_x, damp_z; // +0x2C / +0x30 the air's torque per unit of spin about x / z
    std::array<Vec3, 4> corners; // +0x34's block: (-w/2, 0, -h/2), (w/2, 0, -h/2), (-w/2, 0, h/2), (w/2, 0, h/2)
};

// A leaf (the 0x50-byte LeafParticle record from +0x08; +0x00 the list link and +0x04 the class pointer are the pool's).
struct Leaf {
    Vec3 angle;      // +0x08 its turn: three angles (rad, 0 .. 2 pi) as leaf_matrix() takes them
    Vec3 pos;        // +0x14 (course frame: x, height, z)
    Vec3 vel;        // +0x20
    Vec3 spin;       // +0x2C angular velocity about its own axes
    Vec3 normal;     // +0x38 the ground plane it was made over (the emitter's request: raised 0.05 m)
    F plane_d;       // +0x44 ... its d plus the shape's diagonal
    F shade;         // +0x48 the ground's first vertex scalar under the wheel (the draw's light)
    u8 shape = 0;    // +0x4C the shape (a pointer in the original: shape k's record)
    // Ours, for drawing between frames: the turn and position before this frame's update.
    Vec3 last_angle, last_pos;
};

// The leaves' constants of a build: the shapes and the maker's and the update's numbers.
struct LeafRules {
    std::array<LeafShape, 8> shapes;
    F carry;         // the car's velocity share a leaf takes (0.85)
    F scatter;       // the position's spread (0.2 m)
    F yaw_spread, yaw_offset;     // the heading's spread: + 30 degrees x r - 15 degrees
    F pitch_spread, pitch_offset; // the throw's angle: 10 degrees x r - 85 degrees
    F two_pi, gravity, spin_keep; // 2 pi, 9.81, the spin kept each update (0.999)
    // The draw's (race/leaves.h): the diffuse light's share 0.3 + 0.7 x the ground's shade (Europe 0x333C64 / 0x333C68).
    F light_base, light_gain;
};
LeafRules leaf_rules(const Behaviour& b);

// Europe 0x19D070: the turn's matrix from the three angles (a, b, c): three rows of three (the fourth row and column 0, 0, 0, 1).
// Rows times a vector take a leaf's velocity into its own frame (0x19D1D8); columns, a force back (0x19D180).
std::array<Vec3, 3> leaf_matrix(const Vec3& angle);

// The leaf maker for one wheel's request `q` (its ground's flag byte has 0x20), in two parts with the pool's slot asked for
// between them (Sparks::leaf): leaf_amount() draws once and gives the throw's speed, false when it is below 2 m/s (no
// leaf); then, with a slot, leaf_throw() makes the leaf with twelve draws from the car's velocity `vel` (course frame: x,
// height, z, as the sparks' emitter has it).
bool leaf_amount(const TyreEffect& q, Vu0Random& rng, F& amount);
void leaf_throw(const TyreEffect& q, const Vec3& vel, F amount, const LeafRules& r, Vu0Random& rng, Leaf& out);
// The car's velocity as the emitter hands it to the leaf maker (car + 0x13D4 in the later builds' record): course frame.
inline Vec3 course_velocity(const Car& c) { return {c.vel.x, c.vel.z, -c.vel.y}; }
// LeafParticle::vf1 (Europe 0x19D308): one update of `dt`; false when the leaf is gone (below its plane).
bool leaf_step(Leaf& p, const LeafShape& s, const LeafRules& r, F dt);

} // namespace gt3r::sim::exact
