#pragma once

// Driver input for the race scene: virtual pad -> sim::ControlInputs, and
// force feedback (rumble) derived from the vehicle state.
//
// The pad is the player's race pad (InputManager::race_pad), whose buttons always mean what
// they meant in GT3 (its default "Type A" layout); the race layout chooses which physical
// inputs drive them (input/bindings.h, make_race_preset):
//   Cross pressure   throttle (0..255 -> 0..1)
//   Square pressure  brake
//   Circle           handbrake
//   Triangle         reverse (pressure)
//   left stick X     steering (D-pad Left/Right also steers, ramped)
//   R2 / L2          shift up / down (edge-triggered, manual gearbox)
//   (the race screen: R1 change view, L1 look back (held), Start pause; ours: the right stick
//   looks around, look_back_held below)
//
// Reverse (GT3's "back" action, a pressure input like the throttle): while its value is at
// least the throttle's, the car requests reverse gear and the value drives it (the throttle in
// MT; in AT reverse the sim swaps the pedals, so it goes to the brake).
//
// Pressure buttons from non-pressure devices report 255 while held, so
// keyboards and ordinary pads get full throttle/brake.

#include "input/virtual_pad.h"
#include "sim/sim_types.h"

namespace gt3r::race {

struct ControlConfig {
    float stick_deadzone = 0.06f;    // fraction of half the stick range ignored around the centre
    float stick_exponent = 1.25f;    // >1 softens small inputs (fine control near centre)
    float pedal_deadzone = 8.0f;     // pressure units (0..255) ignored at the bottom of the travel
    float digital_steer_rate = 2.8f; // D-pad: full lock per second while held
    float digital_return_rate = 5.0f;// D-pad: return to centre per second when released
    bool automatic = true;
};

// Look back while held: L1. Under GT3 original, where the race reads the menu pad itself, also R3
// (the right stick's click, Look back's in the Modern layout), which no race action uses there.
inline bool look_back_held(const PadState& pad, bool menu_pad) {
    return pad.down(PadButton::L1) || (menu_pad && pad.down(PadButton::R3));
}

// Stick axis byte (0..255, 128 = centre) -> -1..1 with deadzone and response curve.
float stick_to_steer(u8 axis, const ControlConfig& cfg = {});
// Pressure byte -> 0..1 with a bottom deadzone, rescaled to full range.
float pressure_to_pedal(u8 pressure, const ControlConfig& cfg = {});

class ControlMapper {
public:
    explicit ControlMapper(ControlConfig cfg = {}) : cfg_(cfg) {}

    // Call once per simulation tick with the current pad state.
    sim::ControlInputs update(const PadState& pad, double dt);
    void reset();

    ControlConfig& config() { return cfg_; }
    // The pad record kind of the last update (physics §4.4): 2 analog stick, 3 digital (d-pad).
    int kind() const { return kind_; }
    // Reverse requested by the last update() (pass to RaceVehicle::set_reverse_request).
    bool reverse() const { return reverse_; }
    const ControlConfig& config() const { return cfg_; }

private:
    ControlConfig cfg_;
    float digital_steer_ = 0;
    int kind_ = 2;
    bool prev_up_ = false, prev_down_ = false;
    bool reverse_ = false;
};

// The controller's motors in a race, two ways; each player's Rumble setting picks one
// (InputManager::gt3_rumble: Auto is Modern).
// - GT3 original, as GT3 drives a DualShock 2 (physics §1.2, phase 12's pad vibration): each race
//   step's vibration level (VehicleTelemetry::pad_rumble, 0..127) goes to the large motor as it is
//   and the small motor never runs. Most controllers' motors play its low levels much stronger
//   than a DualShock 2's, so it buzzes all the time there.
// - Modern (ours): what the car meets, each on its own, on both motors with a lean: the body on
//   the large (low-frequency, left) motor and the tyres on the small (high-frequency, right) one,
//   the other motor at 0.35 of it, or 0.7 on a DualSense, whose motors play in separate grips where
//   a split reads as a side. A wall or car hit pulses for 0.25 s by its speed change and off-road
//   holds, leaning large; wheelspin, a slide (the tyre squeal's slip intensity, physics §8.3) and
//   locked wheels hold, and curbs pulse at a rate that follows the speed, leaning small.
// Either way a car the pad does not drive (VehicleTelemetry::pad_output: an AI run-up, after the
// finish) sends nothing, so its motors stop. Ours too: the wheelspin for the throttle triggers
// (ForceFeedbackState::wheelspin), which a DualShock 2 does not have. Feed it after every step;
// pass rumble() to InputManager::set_rumble.
class RumbleModel {
public:
    // What the car met this step, for the Modern rumble.
    struct Events {
        bool kerb = false;    // a grounded wheel is on a curb
        bool offroad = false; // on grass / gravel, or off the road
        float impact = 0;     // the step's strongest wall or car hit as a speed change (m/s; RaceVehicle::last_impact)
    };

    // The step's car state, its telemetry (null: none, the motors stop), what it met and its inputs.
    void update(const sim::VehicleState& s, const sim::VehicleTelemetry* t, const Events& ev, const sim::ControlInputs& in,
                double dt);
    void reset();

    // The motors: GT3 original's (`gt3`) or Modern's, mixed for a DualSense (`dualsense`) or another pad.
    PadRumble rumble(bool gt3, bool dualsense) const;
    float wheelspin() const { return spin_; } // 0..1: the drive wheels spinning under throttle

private:
    PadRumble gt3_;
    float body_ = 0, tyres_ = 0; // Modern's levels, 0..1
    float spin_ = 0;
    float impact_timer_ = 0, impact_level_ = 0;
    double clock_ = 0;
};

} // namespace gt3r::race
