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
//   (the race screen: R1 change view, L1 look back (held), Select respawn, Start pause)
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

// Rumble requests derived from what the car is doing. Feed it every tick;
// read rumble()/triggers() and pass them to InputManager.
class RumbleModel {
public:
    struct Events {
        bool kerb = false;          // a wheel is on a kerb / rumble strip
        bool offroad = false;       // on grass/gravel/dirt
        float impact = 0;           // collision delta-v this tick (m/s), 0 = none
        float tire_slip = 0;        // strongest grounded wheel's squeal intensity (telemetry §8.3)
    };

    void update(const sim::VehicleState& s, const Events& ev, const sim::ControlInputs& in, double dt);
    void reset();

    PadRumble rumble() const { return rumble_; }
    float trigger_left() const { return trig_l_; }  // brake trigger: ABS / lock-up
    float trigger_right() const { return trig_r_; } // throttle trigger: wheelspin

private:
    PadRumble rumble_;
    float trig_l_ = 0, trig_r_ = 0;
    float impact_timer_ = 0, impact_level_ = 0;
    double clock_ = 0;
};

} // namespace gt3r::race
