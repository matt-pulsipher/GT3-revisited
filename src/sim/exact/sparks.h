#pragma once

// GT3's sparks (the race's particle system) in GT3's arithmetic: bursts of orange sparks from a car's
// underbody spark points (its model's GTCI +0x58 / +0x5C list) that dip below the road at 100 km/h or more, on tarmac, kerbs
// and the paved materials [presentation §L7]. Collisions make none of their own. Every particle made draws the VU0 random
// register six times and every particle in its kick window one more each frame: the same register the tyre smoke emitter and
// the flaps draw from in the race step, so the sparks are part of the race's state, not of its picture. A race frame runs
// the race step, then each car's emission (0x1AADD0, from RacePS2Base::vf30, the cars in race order, the points in their
// order), then the update (0x19F698, from RacePS2Base::vf29, dt = 1/60); ExactRace::step does all three. The draw (0x19F738:
// streaks from last frame's eye-space position to this one's, race/particle.img, added) is the game's (race/sparks.h).
// The later builds' particle manager has a second kind in the same pool and lists, the leaves (leaves.h): made inside the
// race step by the tyre effect emitter, updated with the sparks (each active particle's own update, newest first; a leaf's
// draws nothing), so a pool full of leaves leaves no room for sparks and the other way round.

#include "base/game_random.h"
#include "sim/exact/car_record.h"
#include "sim/exact/leaves.h"

#include <array>
#include <span>

namespace gt3r::sim::exact {

class CourseQuery;

// A spark point of a car model (GTCI +0x5C, 16 bytes each: x, y, z in the model frame; the fourth word is not read).
struct SparkPoint {
    F x, y, z;
    bool operator==(const SparkPoint& o) const { return x.bits == o.x.bits && y.bits == o.y.bits && z.bits == o.z.bits; }
};

// A particle (the 0x54-byte record; 0x19E6F0 fills it). Not kept: the flags word +0x04, whose bit 0 says +0x48 holds last
// frame's eye-space position (the draw's), and the colour word +0x44 (0xFF229EFF from a burst), which nothing reads.
struct Spark {
    s16 next = -1;      // +0x00 the next particle of its list (a pool index; -1 none)
    s16 speed0 = 0;     // +0x06 the speed it was made with x 1024 (truncated), at most 0x7FFF
    Vec3 pos;           // +0x08 position (course frame: x, height, z); +0x14 is 1
    Vec3 vel;           // +0x18 velocity
    F inv_mass;         // +0x24 1 / mass
    F size;             // +0x28 half-width w (m)
    Vec3 normal;        // +0x2C the plane it bounces on: the road under the car at its burst
    F plane_d;          // +0x38
    F life, total;      // +0x3C life remaining, +0x40 its whole life (s)
    // Ours, for drawing between frames: the position before this frame's update (a new particle's burst point), and which
    // particle the slot holds (each one made gets the next number).
    Vec3 last;
    u32 serial = 0;
};

// 0x257A44: the sine and cosine of an angle in degrees as the VU0 matrix library makes them (reduced into +-90 degrees,
// then the VU0 sine polynomial on both lanes) [presentation §L7].
void vu0_sin_cos(F degrees, F& s, F& c);

// The VU0 matrix library's current matrix (VU0 registers 28 .. 31: the columns' x, y and z; w is 0, 0, 0, 1 throughout),
// with the operations the spark emission uses, each in the library's multiply-add order.
struct Vu0Matrix {
    std::array<Vec3, 4> c;
    static Vu0Matrix identity();                // 0x257B30
    void translate(F x, F y, F z);              // 0x257EB4: column 3 = M (x, y, z, 1)
    void rotate_x(F degrees);                   // 0x257F60
    void rotate_y(F degrees);                   // 0x257FA0
    void rotate_z(F degrees);                   // 0x257FE0
    Vec3 point(F x, F y, F z) const;            // translate's column 3 without keeping it (the push / pop around it)
};

// The particle manager's frame time (RaceBase::vf2's argument to the update): GT3's 1/60 (0x34FCD8); the later builds' their
// own at the race's rate (2002 Tokyo-Geneva Europe's disc 1/50 at its 50 steps a second; Asia's 1/60 folded toward zero).
F particle_dt(const Behaviour& b);

// A build's spark constants and rules [physics §14.3 SparkEmit]: the tuning block 0x2C03B0 as its static constructor 0x19F898
// fills it, the burst's pi / 180 and the emission's 1 / 3.6^2, 180 / pi and lift. The later builds' code is GT3's (their
// particle record 4 bytes longer: the particle kind's table at +0x04); 2002 Tokyo-Geneva's constants folded toward zero;
// from 2002 Tokyo-Seoul v2.00 on the emission (Rule::SparkEmit) puts a burst on the car body's plane, not the road's.
struct SparkRules {
    F drag, gravity, bounce, spread, speed_min, speed_max, rise, size_min, size_max, density, life_min, life_max, bounce_life;
    F kick_from, kick_to, kick_chance, kick, min_speed, per_metre;
    s32 min_count = 5, max_count = 20;
    F deg_to_rad, kmh_sq, rad_to_deg, lift;
    bool body_plane = false; // a burst's height and plane from the car body's drawn ground point and attitude (0x1A9D50)
};
SparkRules spark_rules(const Behaviour& b);

class Sparks {
public:
    static constexpr int kPool = 128; // particles (0x2A00 bytes)

    Sparks() { reset(); }
    // 0x19EFF8 (every race's start, RacePS2Base::vf9): every particle free (the free list in pool order), none active.
    void reset();
    // The spark constants and rules of build `b` (GT3's until set).
    void set_rules(const Behaviour& b) { sr_ = spark_rules(b); }
    const SparkRules& rules() const { return sr_; }
    // The later builds' leaves in this pool, with the constants of build `b` (GT3's programs have none: leaves stay off).
    void set_leaves(const Behaviour& b);
    bool leaves_on() const { return leaves_on_; }
    // The leaf maker (Rule::Leaves) for one wheel's request whose ground has flag 0x20, the car's velocity `vel` (course
    // frame: x, height, z): one draw, and when the throw is fast enough and the pool has room, a leaf (twelve more).
    void leaf(const TyreEffect& q, const Vec3& vel, Vu0Random& rng);
    // 0x1AADD0: car c's bursts this frame. `points` its model's spark points; `road` the course's single-point query; `drawn`
    // the car's draw mode (0x1E0668) is 2 or 3 (a hidden ghost makes none) [presentation §L7]; the bursts on the car body's
    // plane where the build's rules say (SparkRules::body_plane).
    void emit(const Car& c, std::span<const SparkPoint> points, const CourseQuery& road, bool drawn, Vu0Random& rng);
    // 0x19F450: `count` particles at `pos` from velocity `vel`, bouncing on the plane (n, d) (fewer when the pool runs out:
    // a full pool makes none) [presentation §L7].
    void burst(const Vec3& pos, const Vec3& vel, const Vec3& n, F d, s32 count, Vu0Random& rng);
    // 0x19F698: every active particle one frame on, newest first; the ones whose life ran out are freed [presentation §L7]
    // (a leaf: leaf_step(), gone below its plane).
    void update(F dt, Vu0Random& rng);

    // The active list: its first particle (a pool index; -1 none), each one's `next`, newest first.
    int first() const { return active_; }
    const std::array<Spark, kPool>& pool() const { return pool_; }
    int active() const;
    // Whether pool slot k holds a leaf (its record in leaves(); else a spark in pool()), and the leaves' shapes.
    bool is_leaf(int k) const { return leaf_[size_t(k)] != 0; }
    const std::array<Leaf, kPool>& leaves() const { return leaves_; }
    const LeafRules& leaf_rules() const { return rules_; }
    // Each slot's particle number (every one made gets the next; the draw tells a new particle in a slot by it).
    u32 serial(int k) const { return leaf_[size_t(k)] ? leaf_serial_[size_t(k)] : pool_[size_t(k)].serial; }
    // Ours: the updates made since the reset (the draw's clock; not the race's state).
    u32 frames() const { return frames_; }
    // Every field of the pool and lists, for the race's hash (race_record.h).
    template <class Mix> void mix(Mix&& word) const;

private:
    int alloc();                  // 0x19F640: the free list's head onto the active list's head; -1 when none is free
    void remove(int p, int prev); // 0x19F670: p (after prev, -1 for the head) back onto the free list's head
    bool step(Spark& p, F dt, Vu0Random& rng); // 0x19E7C8; false: its life ran out

    std::array<Spark, kPool> pool_{};
    s16 active_ = -1, free_ = 0;
    u32 serial_ = 0, frames_ = 0;
    bool leaves_on_ = false;
    std::array<u8, kPool> leaf_{}; // 1: the slot holds a leaf
    std::array<Leaf, kPool> leaves_{};
    std::array<u32, kPool> leaf_serial_{};
    LeafRules rules_{};
    SparkRules sr_ = spark_rules(Behaviour{});
};

template <class Mix> void Sparks::mix(Mix&& word) const {
    word(u32(u16(active_)) | u32(u16(free_)) << 16);
    for (size_t k = 0; k < pool_.size(); ++k) {
        const Spark& p = pool_[k];
        word(u32(u16(p.next)) | u32(u16(p.speed0)) << 16);
        if (leaves_on_ && leaf_[k]) { // (a later build's leaf in the slot)
            const Leaf& l = leaves_[k];
            word(0x1EAFu | u32(l.shape) << 16);
            for (const Vec3* v : {&l.angle, &l.pos, &l.vel, &l.spin, &l.normal})
                for (F f : {v->x, v->y, v->z}) word(f.bits);
            word(l.plane_d.bits), word(l.shade.bits);
            continue;
        }
        for (F v : {p.pos.x, p.pos.y, p.pos.z, p.vel.x, p.vel.y, p.vel.z, p.inv_mass, p.size, p.normal.x, p.normal.y, p.normal.z,
                    p.plane_d, p.life, p.total})
            word(v.bits);
    }
}

} // namespace gt3r::sim::exact
