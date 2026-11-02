#pragma once

// View mode 1's procedural cameras (presentation §G15): the five kinds an avant-garde cut
// may pick instead of a course shot. Each plans its shot at the cut from its own generator
// (seeded per shot from a clock, so sequences differ from run to run) and the shot length x,
// and follows the cut's car only (never its battle partner):
//
//   0 Roadside: a still camera near the road (0.1 .. 1.1 m up). Variant A (r > 0.5) on the
//     car's straight-line path x k s ahead (k 0.5 .. 0.85 looking back at it, or 0.05 .. 0.2
//     looking on ahead), 1 m of scatter; variant B beside the car (0.7 .. 2.2 half widths out,
//     up to a length along), aimed so the car's place 1 .. 3 s on sits at a frame edge. FOV
//     55 .. 85 degrees, pitch -15 .. 5 (up), yaw +-10 off the aim, roll +-15. No road under
//     the point: 1 m above the car, along its motion, pitch +-10.
//   1 Hover: carried with the car (its yaw only), 1 .. 9 m away on a random bearing and
//     3 .. 7 m up, looking down at the car; FOV 55 .. 85, roll +-20; four springs wobble the
//     eye (+-1 m) and the aim (+-FOV / 14) like a hand-held camera.
//   2 Orbit: an orbit carried with the car, the body's outline + 0.2 .. 5.2 m out, sweeping
//     100 + 220 r min(x, 3) degrees over x seconds (eased: fast through the front and the
//     back, slow at the sides) to end within 30 degrees of behind the car, dropping from up
//     to 3.1 m to 0.1 .. 1.1 m, the orbit's centre sliding; FOV 40 .. 70, roll +-20.
//   3 Telephoto: a telephoto (25 .. 40 degrees) carried 2.5 .. 6.5 m past the front or rear
//     bumper, 0.1 .. 1.3 m up, looking back at the car; roll +-20, both 0 (centred, level)
//     with p 0.3; a shake growing with the speed (5 .. 40 km/h).
//   4 A camera mount of the car (its GTCI table): the eye at -pos in the model frame, the
//     mount's pitch / yaw / roll and zoom.
//
// Angles are GT3's (degrees): view = Rz(roll) Rx(pitch) Ry(yaw) T(-eye), the camera looking
// down -z with y up; yaw(v) = 180 - atan2(v.x, v.z) turns it onto the direction v,
// pitch(v) = -atan2(v.y, |v.xz|) (positive looks down). The car's yaw frame is
// Ry(-yaw) T(-P), P its body origin.

#include "base/bytes.h"
#include "base/game_random.h"
#include "race/replay_cameras.h"
#include "render/math3d.h"

#include <functional>
#include <optional>
#include <vector>

namespace gt3r::race {

// The followed car as the cameras see it.
struct CameraCar {
    render::Vec3 pos;      // the body origin (world)
    render::Vec3 velocity; // m/s
    float yaw = 0, pitch = 0; // radians: the nose along (-sin yaw, 0, -cos yaw), pitch nose up
    float front = 2.2f, rear = 2.2f, half_width = 0.85f; // the body's extents from the origin (m)
    render::Mat4 body;     // model frame (x right, y up, z back) -> world
};

// One of the car's camera mounts (GTCI +0x68 count, +0x6C records of 0x40 bytes: +0x00 pos,
// +0x0C pitch, +0x10 yaw, +0x14 roll (degrees), +0x18 zoom, +0x1C flags). 0 in-car, 1 and 2
// the chase view, then the replay mounts (mirrors, bumpers, roof, head, sills, outside points).
// Flags bit 1: a view from outside the car (the car drawn); without it an on-board view (in-car,
// the bumpers, the driver's head: the car not drawn). Bit 2: no in-car shake.
struct CarMount {
    render::Vec3 pos;
    float pitch = 0, yaw = 0, roll = 0;
    float zoom = 1.435f;
    u32 flags = 0;
    bool outside() const { return (flags & 2) != 0; }
};
// The car file's mounts (empty without a GTCI table).
std::vector<CarMount> car_camera_mounts(ByteSpan car_file);

// The road height under (x, z), from `y_from` down; nullopt off the road (or on a wall).
using GroundHeight = std::function<std::optional<float>(float x, float z, float y_from)>;

class ProceduralCamera {
public:
    enum Kind : int { Roadside, Hover, Orbit, Telephoto, Mount };

    // The shot's plan at the cut.
    void start(const ProceduralShot& shot, const CameraCar& car, const GroundHeight& ground,
               const std::vector<CarMount>& mounts);
    // One 60 Hz frame of its motion (springs, shake, the orbit's clock).
    void step(const CameraCar& car);
    // The view (world -> camera) for the car where it is drawn now, `frac` (0..1) of the next
    // 60 Hz frame on; eye() and zoom() follow it.
    render::Mat4 view(const CameraCar& car, const GroundHeight& ground, float frac = 0);
    render::Vec3 eye() const { return eye_; }
    float zoom() const { return zoom_; }
    int kind() const { return kind_; }
    // The depth of field's focus distance (presentation §G17), as of view(): the roadside camera
    // and car mounts 100 m; the hover camera its eye's distance from the car's origin (wobble
    // included); the orbit its eye's from the orbit's centre; the telephoto, drawn at the cut
    // (p 0.5), its distance past the bumper, else its distance from the car's origin.
    float focus() const { return focus_; }
    // An on-board car mount (CarMount::outside clear): the car itself is not drawn.
    bool on_board() const { return kind_ == Mount && !mount_.outside(); }

    // GT3's angle helpers (degrees).
    static float yaw_of(const render::Vec3& v);
    static float pitch_of(const render::Vec3& v);
    // Rz(roll) Rx(pitch) Ry(yaw) (degrees).
    static render::Mat4 rotation(float roll, float pitch, float yaw);
    // A horizontal field of view (degrees) as a zoom.
    static float zoom_of(float fov) { return 1.0f / std::tan(fov * 0.5f * 3.14159265f / 180.0f); }

    // A spring of the wobble / shake: with p `p` a new target 256 r - 128, then
    // v += (k (target - x) - 10 v) / 60, x += v / 60.
    struct Spring {
        float x = 0, target = 0, v = 0;
        void step(GameRandom& rng, float k, float p);
        float unit() const { return x / 128.0f; }
    };
    // A vibration damper: v += ((u - u_last) - 12 v) / 60, x += v / 60.
    struct Damper {
        float x = 0, v = 0, u_last = 0;
        void step(float u);
    };

private:
    float draw();
    void start_roadside(const CameraCar& car, const GroundHeight& ground, float x);
    render::Mat4 car_frame(const CameraCar& car) const;

    int kind_ = -1;
    GameRandom rng_;
    float zoom_ = 1, focus_ = 100;
    render::Vec3 eye_;
    float roll_ = 0, fov_ = 60;
    // Roadside: its fixed view.
    render::Mat4 fixed_;
    // Hover: the offset and the four springs (aim pitch, aim yaw, eye x, eye z).
    render::Vec3 offset_;
    Spring wobble_[4];
    // Orbit.
    float extra_ = 0, end_az_ = 0, sweep_ = 0, dir_ = 1, x_end_ = 0, x_start_ = 0, z_end_ = 0, z_start_ = 0;
    float h_end_ = 0, h_rise_ = 0, total_ = 1, left_ = 1;
    // Telephoto.
    bool rear_ = false;
    float dist_ = 4, height_ = 0.5f, lateral_ = 0, aim_height_ = 0.4f, speed_factor_ = 0;
    Spring shake_[2];
    Damper vibe_[2];
    // Mount.
    CarMount mount_;
};

// The orbit's easing per quadrant: an angle in [0, 360) to the eased one
// (g(a) = 90 h(a / 90), h(t) = ((0.1285715 t - 0.557143) t + 1.4285715) t).
float orbit_ease(float degrees);

} // namespace gt3r::race
