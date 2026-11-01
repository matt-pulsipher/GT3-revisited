#pragma once

// A car as the menus describe it, built from the car database with its parts and settings (build_car_spec): its weight
// and size, its engine's torque curve, its gearing, its tyres' size, its stock ride height and its aero coefficients, for
// the car pages' figures (power and torque, the showroom top speed, the gear chart), the showroom pose, GT mode's grid order
// and the online plausibility checks. The race runs on the parameter block the exact core builds from the same records
// (sim/exact/params_build.h). Also the garage settings (CarSettings) the GT-mode setting screens edit, which both read.
// [db] = decoded database field. See docs/dev/PHYSICS.md.

#include "base/bytes.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gt3r::data {
class CarDatabase;
}

namespace gt3r::sim {

enum class Drive : u8 { FR = 0, FF = 1, AWD = 2, MR = 3, RR = 4 }; // = data::DriveType

// [physics §2.3] Two-stage turbo (verified on all 113 turbo cars): the torque curve is capped by it.
struct TurboSpec {
    bool on = false;           // the cap and stage-0 peak, knee, rate bytes all non-zero
    float cap = 1;             // 1 + 0.1 x turbo part +0x11
    struct Stage {
        bool active = false;   // stage 1: its peak, knee, rate bytes all non-zero
        float peak = 0;        // A = byte x 0.1 (turbo +0x12 / +0x15)
        float knee_rpm = 0;    // N = byte x 100 (+0x13 / +0x16)
        float rate = 0;        // R = byte x 0.1 per second (+0x14 / +0x17)
    };
    std::array<Stage, 2> stage;
};

struct EngineSpec {
    std::vector<float> curve_rpm;      // [db] torque curve axis (rpm, ascending)
    std::vector<float> curve_torque;   // [db] N*m at full throttle, after power factors
    float idle_rpm = 900;              // [physics §2.1] engine +0x44 x10 rpm
    float rev_limit_rpm = 7000;        // [physics §2.1] engine +0x45 x100 (+ parts), >= +0x46 x100 + 200
    float upshift_rpm = 0;             // [physics §2.6] UP = engine +0x46 x100 (0 = rev limit - 500)
    TurboSpec turbo;                   // [physics §2.3]
    bool electric = false;             // a mod car's electric motor (it races with its donor's engine for now)
};

struct GearboxSpec {
    std::vector<float> ratios;   // [db] forward gears, 1st first
    float reverse = 3.3f;        // [db] (stored positive)
    float final_drive = 4.0f;    // [db]
};

struct AxleSpec {
    float track = 1.5f;          // [db] m, wheel centreline to centreline
    float tire_radius = 0.32f;   // [db] m, from tire size
    float tire_width = 0.225f;   // [db] m
    // [physics §4.2] the stock ride height q (the front ride-height setting x 0.001 m, both axles) when the car has a
    // suspension record with a spring; 0 otherwise. The showroom pose lifts the body by it.
    float ride_height = 0;
};

struct AeroSpec {
    // [physics §5.2] drag = C_d v^2, downforce C_front / C_rear v^2 (N / (m/s)^2).
    float c_drag = 0, c_front = 0, c_rear = 0;
};

struct CarSpec {
    std::string label;          // database label ("r34_gtr_v_99")
    std::string name;           // display name
    float mass = 1400;          // [physics §6.4] kg: chassis weight x lightweight x body factors + 120 kg
    float front_weight = 0.55f; // [db] static front axle share
    float wheelbase = 2.6f;     // [db] m
    float length = 4.5f, width = 1.75f, height = 1.35f; // [db] m (body box)
    float front_extent = 0, rear_extent = 0; // [physics §5.3/6.2] CoG->front/rear (m), body f32 +0x20/+0x24 mm
    float power_ps = 0;          // [db] catalogue power
    std::array<AxleSpec, 2> axle;  // 0 front, 1 rear
    EngineSpec engine;
    GearboxSpec gearbox;
    Drive drive = Drive::FR;     // [db] drivetrain +0x14
    AeroSpec aero;

    float peak_torque() const;            // N*m
    float peak_power_rpm() const;         // rpm of max power on the curve
    float peak_power_kw() const;
};

// User settings of a car (physics §6.6: the setting words 0xF0..0x1C8 of the saved garage car),
// raw as the game stores them. stock_settings() gives the defaults; garage
// tuning edits a copy and passes it to build_car_spec.
struct CarSettings {
    std::array<int, 8> gears{};       // 0xF0 reverse, 0xF4.. gears 1-7 (x1000; -999 = none)
    int final_drive = 0;              // 0x110, x1000
    int gear_slider = 0;              // 0x114: computed gearboxes, design top speed / 10 km/h (see apply_gear_slider)
    int front_torque = 0;             // 0x118: drivetrain split (stored; the game reads drivetrain +0x13)
    int brake_front = 1, brake_rear = 1; // 0x11C / 0x120: brake-controller indices (1-based)
    int downforce_front = 0, downforce_rear = 0; // 0x124 / 0x128: percent of A_f
    std::array<int, 6> turbo{};       // 0x12C..0x140: stage 0 peak, knee, rate, stage 1 peak, knee, rate (raw bytes)
    std::array<int, 2> camber{};      // 0x144 / 0x148 -> the camber bytes (front, rear)
    std::array<int, 2> ride_height{}; // 0x14C / 0x150, mm
    std::array<int, 2> toe{};         // 0x154 / 0x158 (128 = neutral)
    std::array<int, 2> spring{};      // 0x15C / 0x160, 0.1 kgf/mm
    std::array<int, 2> motion_ratio{}; // 0x164 / 0x168, percent
    std::array<int, 8> damper{};      // 0x16C..0x188 indices: front bump 1, bump 2, rebound 1, rebound 2 (unused:
                                      // the front second stage reuses 0x16C/0x170), rear bump 1, bump 2, rebound 1, rebound 2
    std::array<int, 2> arb{};         // 0x18C / 0x190 indices
    std::array<int, 6> lsd{};         // 0x194..0x1A8: front initial, rear initial, front accel, rear accel, front decel, rear decel
    int tcs = 0;                      // 0x1B0 TCS index
    std::array<int, 4> ascc{};        // 0x1BC..0x1C8 ASCC indices (A, D, C, E)
};

// [menus §9.3] The top speed the menus show (m/s): from
// 0 km/h to the rev limit in top gear, in steps of (that km/h) / 500 + 1, shifting
// up past the upshift rpm, the last speed where the full-throttle drive force beats
// the tarmac rolling resistance and the aero drag.
float showroom_top_speed(const CarSpec& spec);

// [physics §6.7] The program's ratio generator for computed (adjustable racing) gearboxes, in its own EE
// arithmetic. n = forward gears, rev_byte = the rev byte (rev limit / 100), final_word = the final-drive word (final drive
// x 1000), driven tyre size codes (rim inches, width / 5 mm, aspect / 5 %), slider = design top speed / 10 km/h. out[0] =
// reverse, out[1..n] = gears, all x 1000 (s16). `drive` / `awd_system` = drive type / AWD system B3: the caller passes the
// codes of the driven tyre, which is the front one for FF and for AWD system 0 (rear otherwise).
void generate_gear_ratios(int n, int rev_byte, int final_word, int rim_in, int width_code, int aspect_code, int slider,
                          s16 out[8], int drive, int awd_system);

// The generator's inputs for a car's parts [physics §6.7]: the gear count (gear +0x11), the rev byte, the driven tyre's
// size codes, the drive type and AWD system. nullopt for an ordinary (stored-ratio) gearbox.
struct GearGenerator {
    int n = 0, rev = 0, rim = 0, width_code = 0, aspect_code = 0, drive = 0, awd_system = 0;
};
std::optional<GearGenerator> gear_generator(const data::CarDatabase& db, const std::array<u64, 27>& parts);
void generate_gear_ratios(const GearGenerator& in, int final_word, int slider, s16 out[8]);

// Regenerates the gear words of a computed (adjustable) gearbox from
// settings.gear_slider and final_drive [physics §6.7] - what the game does when the
// slider moves; build_car_spec, like the race, uses the gear words as given. Returns
// false for ordinary gearboxes.
bool apply_gear_slider(const data::CarDatabase& db, const std::array<u64, 27>& parts, CarSettings& settings);

// Stock (default) settings of a car with its installed parts [physics §6.6]
// (computed gearboxes get their generated ratios).
CarSettings stock_settings(const data::CarDatabase& db, const std::string& car_label, const std::array<u64, 27>& parts);

// Builds the spec of a car (table 30 label, e.g. "r34_gtr_v_99") or a car
// variant (tables 31/35, e.g. an opponent car) with its installed parts.
// Returns nullopt with *error set when the label or a required part is missing.
std::optional<CarSpec> build_car_spec(const data::CarDatabase& db, const std::string& label,
                                      std::string* error = nullptr);
// Same, for an explicit part list (Car::parts order, 0 = none / stock).
std::optional<CarSpec> build_car_spec(const data::CarDatabase& db, const std::string& car_label,
                                      const std::array<u64, 27>& parts, std::string* error = nullptr);
// With explicit user settings (the overloads above use stock_settings()).
std::optional<CarSpec> build_car_spec(const data::CarDatabase& db, const std::string& car_label,
                                      const std::array<u64, 27>& parts, const CarSettings& settings,
                                      std::string* error = nullptr);

} // namespace gt3r::sim
