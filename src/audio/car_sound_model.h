#pragma once

// GT3's per-car race sound models besides the engine/exhaust banks (facts in
// docs/formats/sound.md "Race car sounds"): wind/road noise on the shared
// ENGN bank sound/rn_01.es, the in-car loop, turbo spool, blow-off and
// transmission whine. Pure functions and a small per-car state; the race
// code feeds physics values and plays the results with EngineUnit (wind)
// and its looping/one-shot effect player (the others, by se.inf name).
//
// Gains here are before the per-camera channel gain and the distance
// attenuation; pitch factors multiply the effect region's own pitch.

#include "base/bytes.h"

#include <utility>

namespace gt3r::car_sound {

// se.inf names of the effects the models drive.
constexpr const char* kTurboSpool = "turbine";
constexpr const char* kWhine = "turbine2";
constexpr const char* kInCar = "in_car";
constexpr const char* kBlowOff = "b_off";

// Tuning from the static sound parameter block in the program (derived
// values; see the formats doc for the raw bytes).
namespace tuning {
constexpr float kWindCoef = 0.001f;            // gain per (m/s)^2 x C_d
constexpr float kWindSpeedLo = 10.0f / 3.6f;   // m/s
constexpr float kWindSpeedHi = 450.0f / 3.6f;  // m/s
constexpr float kWindRpmLo = 2500, kWindRpmHi = 7500;
constexpr float kWindGainMax = 0.5f;
constexpr float kInCarCoef = 0.004f;
constexpr float kInCarSpeedLo = 10.0f / 3.6f, kInCarSpeedHi = 320.0f / 3.6f;
constexpr float kInCarPitchLo = 0.9f, kInCarPitchHi = 1.5f;
constexpr float kInCarGainMax = 2.0f;
constexpr float kWhineGainLo = 0.3f, kWhineGainHi = 1.0f;
constexpr float kWhineTorqueMax = 490.0f;      // N m (50 kgf m)
constexpr float kWhinePitchLo = 0.3f, kWhinePitchHi = 3.5f;
constexpr float kWhineShaftMax = 1047.1976f;   // rad/s (10000 rpm)
constexpr float kTurboLevelScale = 0.2f;       // per-frame scale of the raw level
constexpr float kBlowOffScale = 2.5f;          // per-frame scale of the trigger
constexpr float kTurboPitchDiv = 6144.0f;
} // namespace tuning

struct Loop {
    float gain = 0;  // 0 = silent (key off)
    float pitch = 1; // multiplier of the region pitch
};

struct Wind {
    int rpm = 2500;  // rpm input of the rn_01.es unit
    float gain = 0;
};

// Wind/road noise on rn_01.es. u = forward speed (m/s), y = slipstream
// factor 0..1 (physics §5.2), cd = the car's drag coefficient C_d (body
// record +0x39 / 100, e.g. 0.35; physics §4.7).
Wind wind(float u, float y, float cd);
// The "in_car" loop (heard from the in-car view).
Loop in_car(float u, float y, float cd);
// Transmission whine ("turbine2"). Silent unless the car has a non-stock
// gearbox (`enabled`) and the clutch is not disengaged (state != 0).
// torque = drive torque (N m), ratio/first_ratio = current and 1st gear
// ratios, shaft = driven axle speed (rad/s).
Loop whine(bool enabled, int clutch_state, float torque, float ratio, float first_ratio, float shaft);

// Turbo spool level/pitch and blow-off trigger, per car. step() runs every
// physics step with the turbo state and tick() right after it (GT3's 60 Hz car
// sound step, before the sounds read the outputs); the sounds read them once per
// displayed frame.
class Turbo {
public:
    // turbo: the car has a turbo model; throttle 0..1: the processed throttle
    // (after TCS and the shift cut, so 0 through an upshift); prev_load / load:
    // boost load before / after this step (physics §2.3);
    // torque: this step's engine torque (N m); lag0/lag1: stage lag states
    // (physics §2.3).
    void step(bool turbo, float throttle, float prev_load, float load, float torque, int lag0, int lag1);
    // The level's scale (once per 60 Hz step, whatever the display rate) and the step's blow-off.
    void tick();
    void reset() { *this = Turbo{}; }

    // Spool loop ("turbine"): gain level/128, pitch spool/6144.
    Loop spool() const { return {level_ / 128.0f, spool_ / tuning::kTurboPitchDiv}; }
    // Blow-off one-shot ("b_off") gain: the strongest firing since the last call, played once;
    // 0 = nothing to play.
    float take_blow_off() { return std::exchange(blow_off_, 0.0f); }
    int level() const { return level_; }

private:
    u8 level_ = 0;
    u16 spool_ = 0;
    bool fired_ = false; // blow-off played; re-armed above 90% throttle
    float trigger_ = 0;  // raw blow-off trigger of the step
    float blow_off_ = 0; // waiting for take_blow_off()
};

} // namespace gt3r::car_sound
