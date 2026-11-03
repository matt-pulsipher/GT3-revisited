#pragma once

// What the race scene reads of a car and gives it: the plain types the track runtime (src/track/), the race scene
// (src/race/), audio and rendering exchange. The exact core (src/sim/exact/) publishes its cars in them through
// race/exact_car.h.
//
// World frame (matches decoded course data): right-handed, +Y up, metres.
// Car frame (matches decoded car models): +X right, +Y up, nose towards -Z,
// origin at the wheelbase midpoint on the ground. Note that decoded AI line
// points store X negated; src/formats/course/ai_lines.* already corrects it.

#include "base/bytes.h"

#include <array>
#include <algorithm>
#include <cmath>

namespace gt3r::sim {

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float length() const { return std::sqrt(dot(*this)); }
};

// Unit quaternion, rotating car frame -> world frame.
struct Quat {
    float w = 1, x = 0, y = 0, z = 0;
};

// Simulation rate: confirmed 60 Hz, one step per frame, no substeps.
// Keep code parameterised on it anyway.
constexpr double kDefaultTickHz = 60.0;

// What the driver asks for this tick, already mapped from the virtual pad
// (pressure buttons and analog sticks) by the race scene.
struct ControlInputs {
    float steer = 0;    // -1 full left .. +1 full right
    // Steering device kind (physics §4.4): 2 analog, 3 digital pad (the pad frame's kind,
    // race::exact_inputs_frame); 0 = not given.
    u8 controller_kind = 0;
    float throttle = 0; // 0..1 (e.g. Cross pressure / 255)
    float brake = 0;    // 0..1 (e.g. Square pressure / 255)
    bool handbrake = false;
    bool shift_up = false;   // edge-triggered requests (manual gearbox)
    bool shift_down = false;
    bool automatic = true;
};

// Result of probing the track surface below a point.
struct SurfaceHit {
    bool hit = false;
    float height = 0;          // world Y of the surface
    Vec3 normal{0, 1, 0};
    u32 surface = 0;           // raw GTRW surface value (meaning: see docs/formats/course.md)
    int segment = -1;          // road segment index, -1 if not on the road mesh
};

// Implemented by the track runtime (src/track/); consumed by physics.
class TrackQuery {
public:
    virtual ~TrackQuery() = default;
    // Highest surface at (x, z) at or below `y_max` (pass the wheel's
    // position plus a margin, so bridges/tunnels resolve correctly).
    virtual SurfaceHit probe(float x, float z, float y_max) const = 0;
};

struct WheelState {
    Vec3 contact;            // world contact point
    float steer_angle = 0;   // radians, positive = right
    float spin_angle = 0;    // radians, the wheel's rotation (for rendering; may wrap by whole turns)
    float spin_step = 0;     // radians the wheel turned over the last tick (0: not given; race::interpolate)
    float angular_velocity = 0; // rad/s
    float suspension = 0;    // compression in metres (0 = fully extended)
    float slip_ratio = 0;
    float slip_angle = 0;    // radians
    float load = 0;          // vertical load in N
    bool grounded = false;
    u32 surface = 0;
    float flap = 0;          // [physics §8.8] flap pendulum, -1..1 (car models' morph phase 0.5 + 0.5 flap)
    float camber = 0;        // radians, the drawn wheel's camber from its suspension travel (the wheel record's)
    float brake_heat = 0;    // [physics §8.5] 0..1, drawn as the brake rotor's glow
};

// A car's state after a step (race::exact_state). Rendering, camera, HUD and
// audio only read this (interpolating between two steps when rendering).
struct VehicleState {
    Vec3 position;           // world position of the car-frame origin
    Quat orientation;
    Vec3 velocity;           // world, m/s
    Vec3 angular_velocity;   // world, rad/s
    std::array<WheelState, 4> wheels; // FL, FR, RL, RR
    float engine_rpm = 0;
    int gear = 1;            // -1 reverse, 0 neutral, 1..N forward
    float throttle = 0;      // effective throttle after aids/limiter (for audio)
    float speed_kmh = 0;
    bool rev_limiter = false;
    float steer = 0;         // steering command (rad, + = right) before Ackermann and toe [physics §4.4]
};

// A car's static geometry for the renderer and the camera, in the car frame (+X right, +Y up,
// nose -Z, origin at the wheelbase midpoint on the ground; race::exact_setup).
struct VehicleSetup {
    struct Wheel {
        Vec3 rest_center; // wheel centre at static ride height
        // WheelState::suspension at the static ride height. The wheel centre in the car frame is
        // rest_center.y + (WheelState::suspension - static_suspension) (+ = compressed, the wheel
        // rises into the arch; airborne = max droop) [physics §4.2].
        float static_suspension = 0;
        float ride_height = 0; // [physics §4.2] q = the front ride-height setting x 0.001 m (both axles read the front's)
        float radius = 0.3f;
        float width = 0.2f;
    };
    std::array<Wheel, 4> wheels; // FL, FR, RL, RR
    // The CoG in the car frame, and the body extents measured from it: what the
    // original game uses for the collision octagon, proximity and the grid placement (physics §5.3, physics §9.2).
    Vec3 cog_offset{};
    float cog_front_extent = 2.25f, cog_rear_extent = 2.25f;
    float wheelbase = 2.6f;
    float length = 4.5f, width = 1.75f, height = 1.35f;
    float steer_lock = 0.55f; // full lock (rad) [physics §6.4]: the formula cars' steering morph
    int forward_gears = 5;
    float idle_rpm = 900;
    float rev_limit_rpm = 7000;   // fuel cut (tach redline)
    float max_rpm = 8000;         // sensible tach scale end
    float upshift_rpm = 6800;     // first-gear full-throttle shift point (shift light)
    bool has_turbo = false;
    float max_boost = 0;          // cap - 1 (physics §2.3), same unit as VehicleTelemetry::boost
};

// A step's values beyond the drawn state, as the sounds, smoke, dirt, HUD lamps and gauges, rumble and force
// feedback read them (race::exact_telemetry, from the car's record).
struct VehicleTelemetry {
    float engine_torque = 0;      // N*m through the clutch
    float clutch = 1;             // 0 open .. 1 closed
    bool clutch_locked = true;
    int clutch_state = 1;         // [physics §2.4] link state at the end of the step: 0 open, 1 locked, 2 slipping, 3 shift
    float pitch = 0, roll = 0, pitch_rate = 0, roll_rate = 0; // [physics §4.3] body attitude (rad, nose up / right side down) and rates
    float steer_angle = 0;        // rad, average front road-wheel angle
    float steer_rate = 0;         // rad/s, the steering follower's rate (+ = right)
    float g_over_mu = 0, yaw_acc = 0;  // [physics §4.1] lateral g / mu, yaw acceleration
    std::array<float, 4> beta{};       // [physics §3.2] tyre slip angle beta per wheel
    std::array<float, 4> fx{}, fy{}; // tyre forces in the wheel frame, N
    std::array<float, 4> peak_force{}; // Fp (N) [physics §3.1]
    std::array<float, 4> slip_gain{};  // g [physics §3.4]
    std::array<float, 4> brake_pressure{}; // brake pressure after ASCC and ABS (0..1) [physics §4.5]
    std::array<float, 4> brake_torque{};
    float drag_force = 0;         // N
    float downforce = 0;          // N
    // The ABS at work (physics §4.5), 0..1, the strongest wheel's: a braked wheel nearing the tyre's best braking
    // slip (from 85 % of it) up to 0.4, then past it 0.4 + 0.6 x the share of the pressure the ABS takes off (slip
    // over the target x G g; a car without it, G 0: how far past that slip the wheel is locking, over 0.5). Not a
    // pressure below the pedal: GT3 limits that by the tyre's peak force too, at a standstill as well.
    float abs_level = 0;
    bool tcs_active = false;
    float understeer = 0, oversteer = 0; // ASCC indicators [physics §4.6]
    float throttle_processed = 0;     // throttle after the pedal shaping and TCS, before the turbo lag
    // Turbo boost of the lagged stages: Bcap(lag0, lag1) - 1 (physics §2.3), i.e. the
    // torque multiplier above 1. Read as gauge pressure in kgf/cm^2 for the HUD.
    float boost = 0;
    float slipstream = 0;              // filtered slipstream factor y, 0..1 [physics §5.2]
    float turbo_load = 0;              // turbo load after the step, x * Bcap(lag) [physics §2.3]
    std::array<int, 2> turbo_lag{};    // stage lag states, s16 rpm [physics §2.3]
    // Tyre wear [physics §8] (wear 0 / grip 1 when wear is off).
    std::array<float, 4> tire_wear{};      // wear units (fresh set -20000, worn out 210000)
    std::array<float, 4> wear_grip{1, 1, 1, 1}; // grip factor from wear (physics §8.4)
    std::array<int, 4> wear_level{};       // tyre gauge level -63..127 (physics §8.4)
    std::array<float, 4> smoke{};          // slip intensity (skid smoke / squeal, physics §8.3)
    std::array<float, 4> smoke_emit{};     // smoke-emission intensity 0..1 (physics §8.3b): tyre smoke / dust puffs, car dirt
    std::array<float, 4> skid_pitch{1, 1, 1, 1}; // second slip channel (physics §8.3c): tyre loops' pitch (sound §7.6)
    std::array<float, 4> brake_heat{};     // brake-disc heat 0..1, visual only (physics §8.5)
    bool tires_worn = false;               // some wheel wear >= 0 and grip < threshold (physics §8.7)
    bool on_pit_surface = false;           // a wheel on a pit-lane triangle (flag bit 0, physics §8.6)
    std::array<bool, 2> diff_locked{}; // clutch-pack lock flag per axle
    float wall_impulse = 0;       // the collision step's wall impulse on the car this step, N*s
    // The aid lamps (F+0x521 TCS, F+0x520 ASM: 0 off, 1 on, 2 acting), the pad's vibration level (car +0x16D5, 0..127), the
    // steering wheel's force (car +0x16D6, 0x80 centred), and whether the race has tyre wear (the HUD's tyre gauges).
    int tcs_status = -1, asm_status = -1;
    int pad_rumble = -1, wheel_force = -1;
    // The steering in percent of full lock, left positive (F+0x523), and the throttle and brake pedals 0..1 (F+0x428 /
    // +0x42C): the replay's steering and accel / brake monitors. steer_pct -1000 = none.
    int steer_pct = -1000;
    float throttle_pedal = 0, brake_pedal = 0;
    // Whether the race sends the car's pad vibration and wheel force: its control source is not the AI (1, 2) and no
    // driver kind is set (a car the AI drives, as in a run-up or after the finish, sends nothing).
    bool pad_output = false;
    bool wear_on = false;
    float dirt = -1; // the car's dirt (engine condition word 3, F+0x404; 0..0.8)
};

// [physics §3.6] The surface index 0..15 of a raw SurfaceHit::surface value (material | flags << 8): with flags & 2
// (wet), material 0 / 7 -> 12, 1 -> 13, 9 -> 14.
inline int surface_index(u32 raw) {
    const int m = int(raw & 0xFF), f = int((raw >> 8) & 0xFF);
    if (f & 2) {
        if (m == 0 || m == 7) return 12;
        if (m == 1) return 13;
        if (m == 9) return 14;
    }
    return std::clamp(m, 0, 15);
}

// [physics §10.3] The engine condition a car carries through a run: the odometer in 2 m
// units and the running-in / wear (E) and oil (O) counters in metres. All zero is a fresh
// engine (torque factor exactly 1); a GT-mode garage car starts a run with its own values
// and takes the advanced ones back afterwards.
struct EngineCondition {
    u32 odometer = 0; // 2 m units
    float wear_m = 0; // E
    float oil_m = 0;  // O
    bool operator==(const EngineCondition&) const = default;
};

} // namespace gt3r::sim
