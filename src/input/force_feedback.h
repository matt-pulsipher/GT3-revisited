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
//   triggers = on      ; DualSense brake resistance and throttle buzz / Xbox impulse triggers
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
    float abs_level = 0;    // 0..1 the ABS at work, or a wheel locking without it (VehicleTelemetry::abs_level)
    float steer_force = 0;  // -1..1 force the wheel should apply; positive turns it clockwise (right)
    float jolt = 0;         // 0..1 impact this frame (walls, cars)
    float wheelspin = 0;    // 0..1 the drive wheels spinning under throttle (race::RumbleModel::wheelspin)
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
// What the brake and throttle triggers should do for a state (pure; unit-tested).
TriggerEffect brake_trigger_effect(const ForceFeedbackState& s, const ForceFeedbackConfig& c);
TriggerEffect throttle_trigger_effect(const ForceFeedbackState& s, const ForceFeedbackConfig& c);
// The DualSense effects packet for both triggers (SDL_SendGamepadEffect's payload) with the motors SDL
// plays in it, 0..255 each (0, 0: none), the way SDL's own packets carry them: `improved` for the
// rumble emulation of firmware 2.24 and newer, else the older one at half strength.
using Ds5Packet = std::array<u8, 47>;
Ds5Packet ds5_trigger_packet(const TriggerEffect& left, const TriggerEffect& right, u8 low, u8 high, bool improved);

// Wheel: spring coefficient and constant-force level, both 0..32767 / +-32767.
int wheel_spring_coefficient(const ForceFeedbackState& s, const ForceFeedbackConfig& c);
int wheel_constant_level(const ForceFeedbackState& s, const ForceFeedbackConfig& c);

// Xbox impulse trigger strengths 0..1: the brake's at time `t` seconds (the ABS level, pulsed at
// ~16 Hz), the throttle's (the wheelspin).
float impulse_brake(const ForceFeedbackState& s, double t);
float impulse_throttle(const ForceFeedbackState& s);

} // namespace ffb

// Device side: owns haptic handles and avoids resending unchanged effects.
class ForceFeedback {
public:
    ~ForceFeedback();
    void configure(const ForceFeedbackConfig& c) { cfg_ = c; }
    const ForceFeedbackConfig& config() const { return cfg_; }

    // `brake_trigger`, `throttle_trigger`: SDL_GAMEPAD_AXIS_LEFT/RIGHT_TRIGGER bound to the brake
    // and the throttle, or -1. `joystick` is used for haptics when `gamepad` is null (wheels).
    void apply(SDL_JoystickID id, SDL_Gamepad* gamepad, SDL_Joystick* joystick, int brake_trigger,
               int throttle_trigger, const ForceFeedbackState& s, double time);
    void stop(SDL_JoystickID id);
    void forget(SDL_JoystickID id); // device removed (handles closed by SDL)
    // The motors SDL plays on a gamepad from now on (InputManager::set_rumble, 0..255 each): the
    // DualSense's trigger packets carry them.
    void rumbling(SDL_JoystickID id, u8 low, u8 high);
    // Whether the wheel's force effects are ours (a spring or constant force made). Joystick rumble
    // must not reach it then: SDL's DirectInput and macOS rumble resets the device's force
    // feedback the first time it runs, which removes those effects.
    bool drives_wheel(SDL_JoystickID id) const;

private:
    struct Dev {
        SDL_JoystickID id = 0;
        SDL_Gamepad* gamepad = nullptr;
        bool active = false;
        bool sent_triggers = false;  // a DS5 trigger packet is in effect
        ffb::TriggerEffect sent_left{}, sent_right{}; // its trigger blocks
        u8 rumble_low = 0, rumble_high = 0; // the motors SDL plays (rumbling), since rumble_ticks
        Uint64 rumble_ticks = 0;            // SDL_GetTicks()
        bool impulse_on = false;
        SDL_Haptic* haptic = nullptr;
        bool haptic_tried = false;
        int spring_id = -1, constant_id = -1;
        int spring_coef = -1, constant_level = 0x7fffffff;
    };
    Dev& dev(SDL_JoystickID id);
    void stop(Dev& d);
    void send_triggers(Dev& d, const ffb::TriggerEffect& left, const ffb::TriggerEffect& right);
    void apply_wheel(Dev& d, SDL_Joystick* joystick, const ForceFeedbackState& s, bool restart = false);

    ForceFeedbackConfig cfg_;
    std::vector<Dev> devs_;
};

} // namespace gt3r
