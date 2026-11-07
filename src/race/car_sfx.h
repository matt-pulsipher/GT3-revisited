#pragma once

// Per-car race sound effects, following GT3's race sound code (research
// summary in docs/dev/RACE.md "Race sounds"): the tyre loops of every wheel
// (squeal / slide / dirt / kerb rumble / road roar / rain, sound §7.6), each
// placed at its wheel, collision one-shots, and the per-camera channel gains.
//
// Pure functions are exposed for tests; CarSfx drives one car's voices.

#include "audio/car_sound_model.h"
#include "audio/engine_sound.h"
#include "audio/loop_effect.h"
#include "audio/surround_cue.h"
#include "race/race_sfx.h"
#include "race/sound_view.h"
#include "sim/sim_types.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace gt3r::race {

// The engine's view gains are the table's first two columns, in the same view order.
inline EngineSound::View engine_view(SoundView v) { return EngineSound::View(int(v)); }

// Channel gains (table of the race sound code), by view and channel.
enum SfxChannel : int {
    kChEngine = 0, kChExhaust, kChTurbo, kChBlowOff, kChWhine, kChClash, kChRumble, kChInCar, kChWind,
    kChRoad, kChRain, kChSquealTarmac, kChUnused, kChSlide, kChDirt, kChCount
};
float sfx_gain(SoundView v, SfxChannel ch);

struct TyreSound {
    float vol = 0, pitch = 1;
};
// Road roar of a wheel on tarmac (sound §7.6): tyre width (m) and rim speed |w r| (m/s).
TyreSound road_noise(float tyre_width, float rim_speed);
// Collision strength 0..1 (< 0.2 ignored): wall |j| / 50000, car-car |j| / 7500.
float wall_strength(float impulse);
float car_strength(float impulse);
// Volume of a collision one-shot: channel gain x strength x 0.6 x 32/d, d = camera to car (m,
// 0.25 at least), capped at 1. In the cockpit d is about a metre, so any audible hit is full.
float clash_volume(SoundView view, float strength, float distance);

// The tyre loops: every car plays each of them once per wheel (sound §7.6).
enum TyreLoop : int { kLoopSqueal, kLoopSlide, kLoopDirt, kLoopRumble, kLoopRoad, kLoopRain, kTyreLoops };
// The se.inf effect a loop plays (its first region).
const char* tyre_loop_effect(TyreLoop loop);

// One wheel's inputs to its tyre loops after a physics step.
struct WheelSound {
    int surface = 0;         // tyre surface index (sim::surface_index, wet remap included)
    bool contact = true;     // false once the wheel has been off the ground for 6 steps (road roar)
    float smoke = 0;         // slip intensity, unclamped (VehicleTelemetry::smoke, physics §8.3)
    float pitch = 1;         // skid pitch (VehicleTelemetry::skid_pitch, physics §8.3c)
    float rim_speed = 0;     // |w| r, m/s (road roar)
    float tyre_width = 0.2f; // m (road roar)
};
// Volume and pitch of each loop of one wheel in view `v`: on the loop's surfaces, the loop's
// channel gain x the wheel's slip intensity (road roar: the gain x its own model), pitch =
// doppler x the skid pitch (road roar: doppler x its own pitch). Other loops are silent.
std::array<TyreSound, kTyreLoops> wheel_loops(SoundView v, const WheelSound& w, float doppler);

// Placement of a positional effect voice (sound §7.6): a source at (x, z) in the listener's
// frame (m; x right, z forward) played at volume `vol`. With d the horizontal distance (a
// source nearer than 0.25 m is first moved back along z by 0.25 - d), g = min(1, 32/d) x vol
// and the angle a = clamp((x/d + 1) pi/4, 0, pi/2): left = g cos a, right = g sin a. The
// voice's stealing priority is 255 (1 - min(1, |g|)): the louder, the fewer voices may take it.
struct Placement {
    float left = 0, right = 0;
    int priority = 255;
    float x_over_d = 0, z_over_d = 1; // for the surround cue
};
Placement place_voice(float x, float z, float vol);
// The distance gain of a car's engine units and loops at (x, z) in the listener's frame: min(1, 32/d),
// d the horizontal distance, at least 0.25 m. The same for every car, the followed one too.
float distance_gain(float x, float z);

class CarSfx {
public:
    // One race step of a car: its state and telemetry (no sound without them), the view's gains, and the listener (camera
    // position and axes) the wheels are placed from.
    struct Step {
        const sim::VehicleState* state = nullptr;
        const sim::VehicleSetup* setup = nullptr;
        const sim::VehicleTelemetry* telemetry = nullptr;
        SoundView view = SoundView::ChaseTarget;
        sim::Vec3 eye, right{1, 0, 0}, forward{0, 0, 1};
        float doppler = 1;
        // The followed cars' hits of the step (other cars' contacts are not heard).
        float wall_impulse = 0, car_impulse = 0;
    };
    // One tyre voice of the last update, as sent: side gains, pitch factor, priority.
    struct Voice {
        float left = 0, right = 0, pitch = 1;
        int priority = 255;
    };

    // Binds the loops to their effects' regions (RaceSfx::banks / index); update() does it on
    // first use. False when an effect is missing (those loops stay silent).
    bool bind(const std::vector<std::shared_ptr<const sound::InstBank>>& banks,
              const std::vector<sound::SoundEffect>& index);
    void update(RaceSfx& sfx, VoiceMixer& mixer, const Step& s);
    void stop(VoiceMixer& mixer);

    const std::array<WheelSound, 4>& wheels() const { return wheels_; }
    const Voice& voice(int wheel, TyreLoop loop) const { return out_[size_t(wheel)][size_t(loop)]; }

private:
    bool bound_ = false;
    std::array<int, 4> airborne_{}; // steps of grace after a wheel leaves the ground
    std::array<WheelSound, 4> wheels_{};
    std::array<std::array<LoopEffect, kTyreLoops>, 4> loops_{};
    std::array<std::array<SurroundCue, kTyreLoops>, 4> cues_{};
    std::array<std::array<Voice, kTyreLoops>, 4> out_{};
};

// The car's sounds besides the engine and tyres (docs/formats/sound.md "Race car
// sounds besides the engine"): the rn_01.es wind unit, the in-car loop, the
// transmission whine and the turbo spool / blow-off. The three loops are the car's
// first effect slots, played and placed like its tyre loops (place_voice, LoopEffect).
class CarExtraSounds {
public:
    // Per physics step (turbo model).
    struct Step {
        bool turbo = false;
        float throttle = 0, load = 0, torque = 0;
        int lag0 = 0, lag1 = 0;
    };
    // Per game frame.
    struct Frame {
        float u = 0;           // forward speed, m/s
        float slipstream = 0;  // filtered slipstream factor 0..1
        float cd = 0.35f;      // drag coefficient C_d (body record +0x39 / 100)
        bool whine = false;    // non-stock gearbox (gear part +0x10 != 0)
        int clutch = 1;        // 0 = disengaged
        float torque = 0;      // N m
        float ratio = 1, first_ratio = 1;
        float shaft = 0;       // driven axle speed, rad/s
        // The units' and loops' position in listener space (m: right, forward; the target's sit
        // at the camera in the cockpit, sound §6.5) and the car's own (the blow-off's distance).
        float x = 0, z = 0;
        float car_x = 0, car_z = 0;
        float doppler = 1;     // the car's Doppler factor (sound §8); the spool's pitch has none
        bool wind = true;      // the wind unit (other cars: OpponentEngines plays theirs)
        SoundView view = SoundView::ChaseTarget;
    };
    bool load(Vfs& vfs);
    // Binds the loops to their effects' first regions (as CarSfx::bind); update() does it on first use.
    bool bind(const std::vector<std::shared_ptr<const sound::InstBank>>& banks, const std::vector<sound::SoundEffect>& index);
    // The turbo model's inputs after a physics step of `v` (sound §7.5): the processed
    // throttle (after TCS and the shift cut, so 0 through an upshift), the turbo load,
    // the engine torque and the stage lag states.
    static Step step_inputs(const sim::VehicleTelemetry& t, bool turbo);
    // A physics step: the turbo model's step and tick.
    void step(const Step& s);
    void update(RaceSfx& sfx, VoiceMixer& mixer, const Frame& f);
    void stop(VoiceMixer& mixer);
    void reset() { turbo_.reset(), prev_load_ = 0; }
    const car_sound::Turbo& turbo() const { return turbo_; }

    // The loops, in slot order: the whine (turbine2), the spool (turbine), the in-car loop.
    enum Loop : int { kWhine, kSpool, kInCar, kLoops };
    // A loop's voice as last sent (CarSfx::Voice).
    const CarSfx::Voice& voice(Loop l) const { return out_[size_t(l)]; }

private:
    bool bound_ = false;
    EngineUnit wind_;
    std::array<LoopEffect, kLoops> loops_{};
    std::array<SurroundCue, kLoops> cues_{};
    std::array<CarSfx::Voice, kLoops> out_{};
    car_sound::Turbo turbo_;
    float prev_load_ = 0;
};

} // namespace gt3r::race
