#pragma once

// The collision step [physics §5.3] in GT3's own arithmetic: EE single precision (base/ee_float.h, the
// program's sinf / cosf / sincosf from base/ee_math.h) in the program's operation order, on a mirror of its body
// record, so the same inputs give the same bits. The race step (sim/exact) runs every collision through this.
//
// Frame: the plane (X, Y) = (x, -z) of the world, heading h turning a local point (lx, ly) (x lateral, y forward)
// to (X + cos h lx - sin h ly, Y + sin h lx + cos h ly); "height" is world y. Walls are the course's wall point
// records (x, height, z, link, flags) as the program reads them.

#include "base/ee_float.h"
#include "sim/program.h"

#include <array>
#include <vector>

namespace gt3r::sim::collide::exact {

using ee::F;

// A course wall point record (GTRW wall points, docs/formats/course.md): world x, height, z, the link (the first
// point of a closed polyline holds its point count, the last minus the count, the others 0) and the flags (bit 0
// corner vertex, bit 1 sensor edge). A wall segment runs from a record to the next one of its polyline.
struct WallPt {
    F x, h, z;
    s16 link = 0;
    u16 flags = 0;
};
int wall_next(const WallPt* pts, int i); // the next record of i's polyline (its first after the last)
int wall_prev(const WallPt* pts, int i); // the previous one (the last before the first)

// What the step needs of the course (GT3's road block, body +0x478).
class Course {
public:
    virtual ~Course() = default;
    virtual const WallPt* walls() const = 0;
    // The wall records whose segments are candidates for a body box (world x range, world z range) and
    // the body's road segment hint, in the order the program visits them.
    virtual void wall_candidates(F x0, F z0, F x1, F z1, int hint, std::vector<int>& out) const = 0;
    // Whether segment b is a neighbour of segment a (two bodies on different segments meet only then).
    virtual bool linked(int a, int b) const = 0;
    // For one point (world x, z): the road segment under it, filtered by the hint as the walls are (-1: none).
    // A car's segment hint is this at its front-left wheel after each step, from the hint before.
    virtual int road_segment(F x, F z, int hint) const {
        (void)x, (void)z, (void)hint;
        return -1;
    }
};

// The point record (0x44 bytes).
struct Point {
    F lx, ly;          // +0x00 local position
    F ux, uy;          // +0x08 unit direction from the CoG
    F sx, sy;          // +0x10 start-world position
    F ex, ey;          // +0x18 end-world position
    u32 flags = 0;     // +0x20 bit 0 held on a wall (`wall`, tangent tx/ty), bit 1 held on another body's edge
    bool hit = false;  // +0x24 a wall crossing this round
    F t;               // +0x28 its sweep fraction
    int wall = -1;     // +0x2C a wall record (the held wall, or this round's crossing)
    F tx, ty;          // +0x30 that wall's unit tangent
    int other = 0;     // +0x3C bit 1: the other body
    int edge = 0;      // +0x40 bit 1: its edge
};

// The edge record (0x2C bytes): edge k runs from point k to point k + 1.
struct Edge {
    F sx, sy, ex, ey;  // +0x00 local end points
    F nx, ny, d;       // +0x10 outward normal and offset (inside: n.q + d < 0)
    bool hit = false;  // +0x20 a wall corner crossing this round
    F t;               // +0x24 its sweep fraction
    int wall = -1;     // +0x28 the corner's wall record
};

// The bodies a race holds (sim::exact::kMaxCars); GT3's body record has the pair tables of 8 (kGt3Bodies).
constexpr int kMaxBodies = 32;
constexpr int kGt3Bodies = 8;

// The body record (0x4F0 bytes). The committed state, `hint`, `dt_in`, `steer` and `ghost` are the inputs of a
// step; the committed state holds its results. Everything else carries over between steps as in the original.
struct Body {
    F im, ii;                     // +0x000 1 / m, 1 / (10 I_z)
    std::array<Point, 8> p;       // +0x008
    std::array<Edge, 8> e;        // +0x228
    F radius;                     // +0x388
    F ts, sx, sy, sz, sh;         // +0x38C sweep start time and pose (x, y, height, heading)
    F te, ex, ey, ez, eh;         // +0x3A0 end time (1) and pose
    F dx, dy, dz, dh;             // +0x3B4 displacement
    F vx, vy, vz, w;              // +0x3C4 velocity (plane, vertical) and yaw rate
    F bx0, by0, bx1, by1, bw, bh; // +0x3D4 the swept box
    bool have_points = false;     // +0x3EC the start-world points come from the last step's end-world points
    bool pair = false;            // +0x3F0 this round's hit: a car's vertex on this body's edge
    bool wpoint = false;          // +0x3F4 a point on a wall
    bool wcorner = false;         // +0x3F8 a wall corner on an edge
    F t;                          // +0x3FC the hit time
    bool sensor = false;          // +0x400 a sensor crossing
    F sensor_t;                   // +0x404 its fraction
    int hit_edge = 0;             // +0x408 pair: this body's edge
    int other = 0;                // +0x40C pair: the other body
    int vertex = 0;               // +0x410 pair: its vertex
    F cpx, cpy;                   // +0x414 pair: the crossing point in this body's frame
    int wk = 0;                   // +0x41C point hit: the point
    F ux, uy;                     // +0x420 point hit: the wall's unit tangent (kept for the wall hold)
    int wseg = -1;                // +0x428 point hit: the wall record
    int we = 0;                   // +0x42C corner hit: the edge
    bool enabled = false;         // +0x430 takes part in car-car contact
    bool front = false, rear = false;                           // +0x434 / +0x438 the AI's collision sides
    bool wall_hit = false, car_hit = false, sensor_hit = false; // +0x43C / +0x440 / +0x444
    F strength;                   // +0x448 the step's summed |j| / 15000 (car) and / 1e5 (wall), at most 1
    int car = 0;                  // +0x44C this body's index
    F cx, cy, cz;                 // +0x454 committed position
    F cvx, cvy, cvz;              // +0x460 committed velocity
    F ch, cw;                     // +0x46C committed heading, yaw rate
    int hint = -1;                // +0x474 the road segment under the car (-1: none)
    const Course* course = nullptr; // +0x478 the course (null: no walls, every pair of hints linked)
    F mass;                       // +0x488
    std::array<F, kMaxBodies> rsum, r2; // +0x49C / +0x4BC per body: the radius sum and its square
    F dt;                         // +0x4DC the car's step length
    // The impact effects' state: the rumble's steps left and its strength, the hit sound's hold and its
    // level (decaying while held).
    s32 rumble_steps = 0;         // +0x4E0
    F rumble_level;               // +0x4E4
    s32 sound_steps = 0;          // +0x4E8
    F sound_level;                // +0x4EC
    // Car fields the step reads: the step length (car +0xEC0), the steering input in signed percent (car
    // +0x14A7) and the ghost flag (car +0x136E: no car-car contact); the later builds' stuck nudge also the control source
    // (car +0x136D).
    F dt_in;
    s8 steer = 0;
    bool ghost = false;
    s8 control = 0;
    // The car field the step writes: the last wall hit's strength for the rumble (car +0x1474; null: none).
    F* rumble = nullptr;
    // Not in the record (build() keeps it): how the race runs (sim/program.h, the car's Behaviour): NTSC-J's commit, wall
    // friction and push-out (Rule::CollisionCommit, WallFriction, PushSegment), and at PAL's 50 steps a second a hit's rumble
    // lasts 20 steps (NTSC 24).
    Behaviour behaviour;
};

// The body of a car with the CoG -> front / rear extents, width, mass and I_z: an octagon with
// corners chamfered by 0.2 x width. Clears the carried-over state.
void build(Body& b, F front, F rear, F width, F mass, F iz, int car);
// Every body's pair tables.
void pair_tables(std::vector<Body>& bodies);

// The responses of a step, for sounds, effects and contact events (not read back by the step).
struct Response {
    enum Kind : u8 { Point, Corner, Car } kind = Point;
    int body = 0;         // the body answered (Car: the one whose edge was struck)
    int other = -1;       // Car: the body whose vertex struck it
    int feature = 0;      // Point: the point; Corner: the edge; Car: the struck edge
    int vertex = 0;       // Car: the striking vertex
    double x = 0, y = 0;  // the contact point (plane)
    double nx = 0, ny = 0; // the normal (plane): Point: the wall's, into the track; Corner, Car: the struck edge's, outward
    double j = 0;         // |j|
    double t = 0;         // the sweep fraction
    F mag;                // |j| as the program has it (the hit's report to the race)
};
using Trace = std::vector<Response>;

// The step of all bodies (walls and car-car contact). `rounds` (at most 2, the program's) caps the hits
// answered; `trace` (optional) gets the responses.
void step_all(std::vector<Body>& bodies, Trace* trace = nullptr, int rounds = 2);
// The walls-only step of body i (car-car contact switched off).
void step_walls(std::vector<Body>& bodies, int i, Trace* trace = nullptr, int rounds = 2);

// The step's functions, each callable on its own.
namespace fn {
void init(Body& b, F dt);
void enable(Body& b, bool on);
void separate(std::vector<Body>& bd, int t, int o);
void persist(std::vector<Body>& bd, int i);
void hold(Body& b);
void end_pose(Body& b);
void reset_hit(Body& b);
bool wall_sweep(Body& b);
bool pair_sweep(std::vector<Body>& bd, int a, int b);
void sensor_check(Body& b, F t);
void advance(Body& b, F t);
void point_response(Body& b, Trace* trace = nullptr);
void corner_response(Body& b, Trace* trace = nullptr);
void pair_response(std::vector<Body>& bd, int a, Trace* trace = nullptr);
void stuck(Body& b, F last);
void push_out(Body& b);
void commit(Body& b);
void effects(Body& b);                                      // (its sound and pad calls are empty)
F wrap_heading(F h, const Behaviour& bh);                   // (bh: 2002 Tokyo-Geneva's constants)
} // namespace fn

} // namespace gt3r::sim::collide::exact
