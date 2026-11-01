#pragma once

// Native controller feedback beyond DS2 rumble: DualSense adaptive triggers,
// Xbox impulse triggers and wheel force feedback (SDL haptics).
//
// The race feeds a ForceFeedbackState every frame through
// InputManager::set_force_feedback(); InputManager routes it to the player's
// device. Effects stop by themselves when no state arrives for a moment
// (pause, menus, screen change). Configuration, [ffb] in config.ini:
//
//   enabled  = on      ; master switch
//   triggers = on      ; DualSense resistance / Xbox impulse triggers on the brake trigger
//   wheel    = on      ; wheel force feedback (spring + steering force)
//   strength = 1.0     ; overall wheel force (0..2)
//   spring   = 0.6     ; self-centring spring at speed (0..1)
//   trigger_force = 0.6 ; DualSense brake resistance (0..1)
//   invert   = off     ; flip the wheel's steering force direction

#include "base/bytes.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_joystick.h>

#include <array>
#include <vector>

namespace gt3r {

class IniDocument;

struct ForceFeedbackState {
    float speed_kmh = 0;    // vehicle speed
    float brake = 0;        // brake pedal as applied, 0..1
    bool abs_active = false;
    float steer_force = 0;  // -1..1 force the wheel should apply; positive turns it clockwise (right)
    float jolt = 0;         // 0..1 impact this frame (walls, cars)
};

struct ForceFeedbackConfig {
    bool enabled = true, triggers = true, wheel = true, invert = false;
    float strength = 1.0f, spring = 0.6f, trigger_force = 0.6f;

    static ForceFeedbackConfig load(const IniDocument& ini);
    void save(IniDocument& ini) const;
};

namespace ffb {

// DualSense trigger effect blocks (11 bytes, SDL's DS5 effects packet layout).
using TriggerEffect = std::array<u8, 11>;
TriggerEffect trigger_off();
// Constant resistance from `start` (0..1 of the travel) with force 0..1.
TriggerEffect trigger_resistance(float start, float force);
// Vibration at `hz` with amplitude 0..1, from `start`.
TriggerEffect trigger_vibration(float start, int hz, float amplitude);
// What the brake trigger should do for a state (pure; unit-tested).
TriggerEffect brake_trigger_effect(const ForceFeedbackState& s, const ForceFeedbackConfig& c);

// Wheel: spring coefficient and constant-force level, both 0..32767 / +-32767.
int wheel_spring_coefficient(const ForceFeedbackState& s, const ForceFeedbackConfig& c);
int wheel_constant_level(const ForceFeedbackState& s, const ForceFeedbackConfig& c);

// Xbox impulse trigger strength 0..1 for the brake trigger at time `t` seconds
// (ABS pulses at ~16 Hz).
float impulse_brake(const ForceFeedbackState& s, double t);

} // namespace ffb

// Device side: owns haptic handles and avoids resending unchanged effects.
class ForceFeedback {
public:
    ~ForceFeedback();
    void configure(const ForceFeedbackConfig& c) { cfg_ = c; }
    const ForceFeedbackConfig& config() const { return cfg_; }

    // `brake_trigger`: SDL_GAMEPAD_AXIS_LEFT/RIGHT_TRIGGER bound to the brake,
    // or -1. `joystick` is used for haptics when `gamepad` is null (wheels).
    void apply(SDL_JoystickID id, SDL_Gamepad* gamepad, SDL_Joystick* joystick, int brake_trigger,
               const ForceFeedbackState& s, double time);
    void stop(SDL_JoystickID id);
    void forget(SDL_JoystickID id); // device removed (handles closed by SDL)
    // Whether the wheel's force effects are ours (a spring or constant force made). Joystick rumble
    // must not reach it then: SDL's DirectInput and macOS rumble resets the device's force
    // feedback the first time it runs, which removes those effects.
    bool drives_wheel(SDL_JoystickID id) const;

private:
    struct Dev {
        SDL_JoystickID id = 0;
        SDL_Gamepad* gamepad = nullptr;
        bool active = false;
        int sent_trigger = -2;       // which trigger got the last DS5 packet
        ffb::TriggerEffect sent{};   // last DS5 trigger block
        bool impulse_on = false;
        SDL_Haptic* haptic = nullptr;
        bool haptic_tried = false;
        int spring_id = -1, constant_id = -1;
        int spring_coef = -1, constant_level = 0x7fffffff;
    };
    Dev& dev(SDL_JoystickID id);
    void stop(Dev& d);
    void apply_wheel(Dev& d, SDL_Joystick* joystick, const ForceFeedbackState& s, bool restart = false);

    ForceFeedbackConfig cfg_;
    std::vector<Dev> devs_;
};

} // namespace gt3r
