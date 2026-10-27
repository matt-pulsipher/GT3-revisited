#include "race/controls.h"

#include <algorithm>
#include <cmath>

namespace gt3r::race {

float stick_to_steer(u8 axis, const ControlConfig& cfg) {
    float v = (float(axis) - 128.0f) / (axis < 128 ? 128.0f : 127.0f);
    float a = std::fabs(v);
    if (a <= cfg.stick_deadzone) return 0;
    a = std::min(1.0f, (a - cfg.stick_deadzone) / (1.0f - cfg.stick_deadzone));
    a = std::pow(a, cfg.stick_exponent);
    return v < 0 ? -a : a;
}

float pressure_to_pedal(u8 pressure, const ControlConfig& cfg) {
    float p = float(pressure);
    if (p <= cfg.pedal_deadzone) return 0;
    return std::min(1.0f, (p - cfg.pedal_deadzone) / (255.0f - cfg.pedal_deadzone));
}

void ControlMapper::reset() {
    digital_steer_ = 0;
    prev_up_ = prev_down_ = false;
    reverse_ = false;
}

sim::ControlInputs ControlMapper::update(const PadState& pad, double dt) {
    sim::ControlInputs in;
    auto pedal = [&](PadButton b) {
        if (!pad.down(b)) return 0.0f;
        u8 p = pad.press(b);
        return p == 0 ? 1.0f : pressure_to_pedal(p, cfg_); // held but no pressure report: full
    };
    in.throttle = pedal(PadButton::Cross);
    in.brake = pedal(PadButton::Square);
    in.handbrake = pad.down(PadButton::Circle);

    int dir = (pad.down(PadButton::Right) ? 1 : 0) - (pad.down(PadButton::Left) ? 1 : 0);
    float stick = stick_to_steer(pad.axis(PadAxis::LX), cfg_);
    // A held d-pad with the stick centred is the game's digital pad (kind 3): full lock is
    // requested and the physics turns the wheel in at its digital rate (physics §4.4). The
    // stick is the analog pad (kind 2).
    if (dir != 0 && std::fabs(stick) < 0.05f) {
        in.steer = float(dir);
        kind_ = 3;
    } else {
        in.steer = stick;
        if (in.steer != 0) kind_ = 2;
    }
    (void)dt;

    bool up = pad.down(PadButton::R2);
    bool down = pad.down(PadButton::L2);
    in.shift_up = up && !prev_up_;
    in.shift_down = down && !prev_down_;
    prev_up_ = up;
    prev_down_ = down;
    in.automatic = cfg_.automatic;
    // Reverse ("back"): Triangle.
    float back = pedal(PadButton::Triangle);
    reverse_ = back > 0 && back >= in.throttle;
    if (reverse_) {
        if (in.automatic) in.brake = std::max(in.brake, back), in.throttle = 0; // AT reverse: the brake drives
        else in.throttle = back;
    }
    return in;
}

void RumbleModel::reset() {
    rumble_ = {};
    trig_l_ = trig_r_ = 0;
    impact_timer_ = impact_level_ = 0;
}

void RumbleModel::update(const sim::VehicleState& s, const Events& ev, const sim::ControlInputs& in, double dt) {
    clock_ += dt;
    float speed = s.speed_kmh / 3.6f;
    if (ev.impact > 1.5f) {
        impact_level_ = std::max(impact_level_, std::min(1.0f, ev.impact / 8.0f));
        impact_timer_ = 0.25f;
    }
    impact_timer_ = std::max(0.0f, impact_timer_ - float(dt));
    if (impact_timer_ == 0) impact_level_ = 0;

    // Wheel slip (sim convention: slip_ratio > 0 braking / locking, < 0 wheelspin).
    float spin = 0, lock = 0;
    for (const sim::WheelState& w : s.wheels) {
        if (!w.grounded) continue;
        spin = std::max(spin, -w.slip_ratio);
        lock = std::max(lock, w.slip_ratio);
    }
    float spin_amt = std::clamp((spin - 0.15f) / 0.5f, 0.0f, 1.0f) * std::min(1.0f, in.throttle * 2);
    float lock_amt = std::clamp((lock - 0.2f) / 0.5f, 0.0f, 1.0f) * std::min(1.0f, in.brake * 2);

    float large = impact_level_;
    if (ev.offroad && speed > 3) large = std::max(large, 0.18f + std::min(0.2f, speed / 150.0f));
    large = std::max(large, 0.25f * spin_amt);
    // The same per-wheel slip intensity drives the game's tyre squeal loops. Let a
    // pronounced squeal add a gentle low-motor rumble, while ordinary cornering stays quiet.
    float grip_loss = std::clamp((ev.tire_slip - 0.35f) / 0.9f, 0.0f, 1.0f);
    large = std::max(large, 0.35f * grip_loss);
    // Kerbs: the small (on/off) motor, pulsed at a rate that follows the speed.
    bool kerb_pulse = ev.kerb && speed > 2 && std::fmod(clock_ * (4.0 + speed * 0.4), 1.0) < 0.5;
    rumble_.small = kerb_pulse;
    rumble_.large = u8(std::clamp(large, 0.0f, 1.0f) * 255.0f);
    // Impulse triggers: ABS chatter on the brake trigger, wheelspin buzz on the throttle trigger.
    trig_l_ = lock_amt > 0 ? (std::fmod(clock_ * 15.0, 1.0) < 0.5 ? 0.6f * lock_amt + 0.2f : 0.0f) : 0.0f;
    trig_r_ = 0.5f * spin_amt;
}

} // namespace gt3r::race
