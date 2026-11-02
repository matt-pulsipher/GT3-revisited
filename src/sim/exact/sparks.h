#pragma once

// GT3's sparks (the race's particle system) in GT3's arithmetic: bursts of orange sparks from a car's
// underbody spark points (its model's GTCI +0x58 / +0x5C list) that dip below the road at 100 km/h or more, on tarmac, kerbs
// and the paved materials [presentation §L7]. Collisions make none of their own. Every particle made draws the VU0 random
// register six times and every particle in its kick window one more each frame: the same register the tyre smoke emitter and
// the flaps draw from in the race step, so the sparks are part of the race's state, not of its picture. A race frame runs
// the race step, then each car's emission (from the race's frame, the cars in race order, the points in their
// order), then the update (from the race's frame update, dt = 1/60); ExactRace::step does all three. The draw
// (streaks from last frame's eye-space position to this one's, race/particle.img, added) is the game's (race/sparks.h).

#include "base/game_random.h"
#include "sim/exact/car_record.h"

#include <array>
#include <span>

namespace gt3r::sim::exact {

class CourseQuery;

// A spark point of a car model (GTCI +0x5C, 16 bytes each: x, y, z in the model frame; the fourth word is not read).
struct SparkPoint {
    F x, y, z;
    bool operator==(const SparkPoint& o) const { return x.bits == o.x.bits && y.bits == o.y.bits && z.bits == o.z.bits; }
};

// A particle (the 0x54-byte record; the program fills it). Not kept: the flags word +0x04, whose bit 0 says +0x48 holds last
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

// The sine and cosine of an angle in degrees as the VU0 matrix library makes them (reduced into +-90 degrees,
// then the VU0 sine polynomial on both lanes) [presentation §L7].
void vu0_sin_cos(F degrees, F& s, F& c);

// The VU0 matrix library's current matrix (VU0 registers 28 .. 31: the columns' x, y and z; w is 0, 0, 0, 1 throughout),
// with the operations the spark emission uses, each in the library's multiply-add order.
struct Vu0Matrix {
    std::array<Vec3, 4> c;
    static Vu0Matrix identity();
    void translate(F x, F y, F z);              // column 3 = M (x, y, z, 1)
    void rotate_x(F degrees);
    void rotate_y(F degrees);
    void rotate_z(F degrees);
    Vec3 point(F x, F y, F z) const;            // translate's column 3 without keeping it (the push / pop around it)
};

class Sparks {
public:
    static constexpr int kPool = 128; // particles (0x2A00 bytes)

    Sparks() { reset(); }
    // every race's start, the race's start-up: every particle free (the free list in pool order), none active.
    void reset();
    // Car c's bursts this frame. `points` its model's spark points; `road` the course's single-point query; `drawn`
    // the car's draw mode is 2 or 3 (a hidden ghost makes none) [presentation §L7].
    void emit(const Car& c, std::span<const SparkPoint> points, const CourseQuery& road, bool drawn, Vu0Random& rng);
    // `count` particles at `pos` from velocity `vel`, bouncing on the plane (n, d) (fewer when the pool runs out:
    // a full pool makes none) [presentation §L7].
    void burst(const Vec3& pos, const Vec3& vel, const Vec3& n, F d, s32 count, Vu0Random& rng);
    // Every active particle one frame on, newest first; the ones whose life ran out are freed [presentation §L7].
    void update(F dt, Vu0Random& rng);

    // The active list: its first particle (a pool index; -1 none), each one's `next`, newest first.
    int first() const { return active_; }
    const std::array<Spark, kPool>& pool() const { return pool_; }
    int active() const;
    // Ours: the updates made since the reset (the draw's clock; not the race's state).
    u32 frames() const { return frames_; }
    // Every field of the pool and lists, for the race's hash (race_record.h).
    template <class Mix> void mix(Mix&& word) const;

private:
    int alloc();                  // the free list's head onto the active list's head; -1 when none is free
    void remove(int p, int prev); // p (after prev, -1 for the head) back onto the free list's head
    bool step(Spark& p, F dt, Vu0Random& rng); // ; false: its life ran out

    std::array<Spark, kPool> pool_{};
    s16 active_ = -1, free_ = 0;
    u32 serial_ = 0, frames_ = 0;
};

template <class Mix> void Sparks::mix(Mix&& word) const {
    word(u32(u16(active_)) | u32(u16(free_)) << 16);
    for (const Spark& p : pool_) {
        word(u32(u16(p.next)) | u32(u16(p.speed0)) << 16);
        for (F v : {p.pos.x, p.pos.y, p.pos.z, p.vel.x, p.vel.y, p.vel.z, p.inv_mass, p.size, p.normal.x, p.normal.y, p.normal.z,
                    p.plane_d, p.life, p.total})
            word(v.bits);
    }
}

} // namespace gt3r::sim::exact
