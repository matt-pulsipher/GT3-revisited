#include "input/force_feedback.h"

#include "base/config.h"
#include "base/log.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gt3r {

namespace {

bool parse_bool(const std::optional<std::string>& v, bool def) {
    if (!v) return def;
    std::string s = to_lower(*v);
    return s == "on" || s == "1" || s == "true" || s == "yes";
}

float parse_float(const std::optional<std::string>& v, float def, float lo, float hi) {
    if (!v) return def;
    char* end = nullptr;
    float f = std::strtof(v->c_str(), &end);
    return end == v->c_str() ? def : std::clamp(f, lo, hi);
}

std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.2f", v);
    return b;
}

u8 byte01(float v) { return u8(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

// An impulse trigger's small motor hardly turns at the bottom of its range, so a level starts above that.
float impulse_level(float level) { return level > 0 ? 0.3f + 0.7f * std::min(level, 1.0f) : 0.0f; }

// DualSense output report effects block (SDL_SendGamepadEffect payload for
// PS5 controllers; same layout as SDL's test/testcontroller.c DS5EffectsState_t).
struct Ds5Effects {
    u8 enable_bits1;     // 0x04 right trigger, 0x08 left trigger
    u8 enable_bits2;
    u8 rumble_right, rumble_left; // with enable_bits1 0x02 and 0x01 (halved) or enable_bits3 0x04
    u8 headphone_volume, speaker_volume, microphone_volume, audio_enable_bits;
    u8 mic_light_mode, audio_mute_bits;
    u8 right_trigger[11];
    u8 left_trigger[11];
    u8 unknown1[6];
    u8 enable_bits3;
    u8 unknown2[2];
    u8 led_anim, led_brightness, pad_lights, led_red, led_green, led_blue;
};
static_assert(sizeof(Ds5Effects) == sizeof(ffb::Ds5Packet), "DS5 effects block layout");

// Whether SDL plays a DualSense's motors with the improved rumble emulation of firmware 2.24 and newer,
// or the older one at half strength, decided as SDL_hidapi_ps5.c does: Sony's pads only, a DualSense
// Edge, a firmware it could not read (Bluetooth) or 2.24 on.
bool ds5_improved_rumble(SDL_Gamepad* g) {
    if (SDL_GetGamepadVendor(g) != 0x054C) return false;
    const Uint16 firmware = SDL_GetGamepadFirmwareVersion(g);
    return SDL_GetGamepadProduct(g) == 0x0DF2 || firmware == 0 || firmware >= 0x0224;
}

} // namespace

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

ForceFeedbackConfig ForceFeedbackConfig::load(const IniDocument& ini) {
    ForceFeedbackConfig c;
    auto get = [&](const char* k) { return ini.get("ffb", k); };
    c.enabled = parse_bool(get("enabled"), c.enabled);
    c.triggers = parse_bool(get("triggers"), c.triggers);
    c.wheel = parse_bool(get("wheel"), c.wheel);
    c.invert = parse_bool(get("invert"), c.invert);
    c.strength = parse_float(get("strength"), c.strength, 0.0f, 2.0f);
    c.spring = parse_float(get("spring"), c.spring, 0.0f, 1.0f);
    c.trigger_force = parse_float(get("trigger_force"), c.trigger_force, 0.0f, 1.0f);
    c.steering = parse_float(get("steering"), c.steering, 0.0f, 1.0f);
    c.understeer = parse_float(get("understeer"), c.understeer, 0.0f, 1.0f);
    const std::string mode = to_lower(get("wheel_mode").value_or(c.wheel_mode));
    if (mode == "modern" || mode == "gt3") c.wheel_mode = mode;
    return c;
}

void ForceFeedbackConfig::save(IniDocument& ini) const {
    ini.set("ffb", "enabled", enabled ? "on" : "off");
    ini.set("ffb", "triggers", triggers ? "on" : "off");
    ini.set("ffb", "wheel", wheel ? "on" : "off");
    ini.set("ffb", "wheel_mode", wheel_mode);
    ini.set("ffb", "strength", fmt(strength));
    ini.set("ffb", "steering", fmt(steering));
    ini.set("ffb", "spring", fmt(spring));
    ini.set("ffb", "understeer", fmt(understeer));
    ini.set("ffb", "trigger_force", fmt(trigger_force));
    ini.set("ffb", "invert", invert ? "on" : "off");
}

// ---------------------------------------------------------------------------
// Pure effect shaping
// ---------------------------------------------------------------------------

namespace ffb {

// DualSense trigger modes (community documentation of the DS5 output report):
// 0x05 off, 0x01 continuous resistance {start, force}, 0x06 vibration
// {frequency Hz, amplitude, start}.
TriggerEffect trigger_off() { return {0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; }

TriggerEffect trigger_resistance(float start, float force) {
    if (force <= 0.0f) return trigger_off();
    return {0x01, byte01(start), byte01(force), 0, 0, 0, 0, 0, 0, 0, 0};
}

TriggerEffect trigger_vibration(float start, int hz, float amplitude) {
    if (amplitude <= 0.0f || hz <= 0) return trigger_off();
    return {0x06, u8(std::clamp(hz, 1, 255)), u8(std::lround(std::clamp(amplitude, 0.0f, 1.0f) * 63.0f)),
            byte01(start), 0, 0, 0, 0, 0, 0, 0};
}

TriggerEffect brake_trigger_effect(const ForceFeedbackState& s, const ForceFeedbackConfig& c) {
    if (!c.enabled || !c.triggers || !s.triggers_on) return trigger_off();
    if (s.abs_level > 0) return trigger_vibration(0.1f, 18, s.abs_level); // the ABS under the finger, by its level
    // Firm pedal: resistance builds after the first 20 % of travel. Rolling
    // slowly it's lighter (less brake to feel).
    float speed = std::clamp(s.speed_kmh / 60.0f, 0.3f, 1.0f);
    return trigger_resistance(0.2f, c.trigger_force * speed);
}

TriggerEffect throttle_trigger_effect(const ForceFeedbackState& s, const ForceFeedbackConfig& c) {
    if (!c.enabled || !c.triggers || !s.triggers_on || s.traction <= 0.0f) return trigger_off();
    return trigger_vibration(0.1f, 35, std::min(s.traction, 1.0f)); // the edge of grip under the finger, by its level
}

namespace {

// Modern: the share of the wheel's force left in understeer (its setting takes that much off at full).
float understeer_left(const ForceFeedbackState& s, const ForceFeedbackConfig& c) {
    return 1.0f - c.understeer * std::clamp(s.understeer, 0.0f, 1.0f);
}

} // namespace

int wheel_spring_coefficient(const ForceFeedbackState& s, const ForceFeedbackConfig& c) {
    if (!c.enabled || !c.wheel || !s.wheel_on) return 0;
    float k = c.spring;
    if (!c.gt3_wheel()) {
        // Light centring when stopped, full at 120 km/h and above; a spring ignores the tyres, so it goes
        // with the steering force in understeer and out of the way of opposite lock in a slide.
        k *= 0.15f + 0.85f * std::clamp(s.speed_kmh / 120.0f, 0.0f, 1.0f);
        k *= std::min(understeer_left(s, c), 1.0f - std::clamp(s.oversteer, 0.0f, 1.0f));
    }
    return int(std::lround(std::clamp(k * c.strength, 0.0f, 1.0f) * 32767.0f));
}

int wheel_constant_level(const ForceFeedbackState& s, const ForceFeedbackConfig& c) {
    if (!c.enabled || !c.wheel || !s.wheel_on) return 0;
    float f = std::clamp(s.steer_force, -1.0f, 1.0f);
    if (!c.gt3_wheel()) f = f * understeer_left(s, c) + std::clamp(s.jolt, 0.0f, 1.0f) * (s.steer_force < 0 ? -0.5f : 0.5f);
    if (c.invert) f = -f;
    return int(std::lround(std::clamp(f * c.steering * c.strength, -1.0f, 1.0f) * 32767.0f));
}

float eased_indicator(float felt, float indicator, float dt) {
    constexpr float kSeconds = 0.25f; // no step above 6 % a tick on CPU laps of Trial Mountain and Laguna Seca
    return felt + (std::clamp(indicator, 0.0f, 1.0f) - felt) * std::clamp(dt / kSeconds, 0.0f, 1.0f);
}

bool wheel_rerun_due(double started, double now) {
#if defined(_WIN32) || defined(__APPLE__)
    return now - started >= kWheelRerunSeconds;
#else
    (void)started, (void)now;
    return false;
#endif
}

Ds5Packet ds5_trigger_packet(const TriggerEffect& left, const TriggerEffect& right, u8 low, u8 high, bool improved) {
    Ds5Effects pkt{};
    pkt.enable_bits1 = 0x04 | 0x08; // both triggers
    std::memcpy(pkt.left_trigger, left.data(), left.size());
    std::memcpy(pkt.right_trigger, right.data(), right.size());
    if (low || high) {
        pkt.enable_bits1 |= 0x02; // the motors, not the audio haptics
        if (improved) {
            pkt.enable_bits3 |= 0x04;
            pkt.rumble_left = low, pkt.rumble_right = high;
        } else {
            pkt.enable_bits1 |= 0x01;
            pkt.rumble_left = u8(low >> 1), pkt.rumble_right = u8(high >> 1);
        }
    }
    Ds5Packet out;
    std::memcpy(out.data(), &pkt, sizeof(pkt));
    return out;
}

// A throb, not on and off: the motor needs longer than a short pulse to spin up, and stopped it would start again from
// nothing. (Nor does SDL's Raw Input driver then move the pad's motors between XInput and WGI at every pulse.)
float impulse_brake(const ForceFeedbackState& s, double t) {
    if (!s.triggers_on) return 0.0f;
    const float level = impulse_level(s.abs_level);
    return std::fmod(t * 10.0, 1.0) < 0.5 ? level : 0.4f * level;
}

float impulse_throttle(const ForceFeedbackState& s) { return s.triggers_on ? impulse_level(s.traction) : 0.0f; }

} // namespace ffb

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

ForceFeedback::~ForceFeedback() {
    for (Dev& d : devs_) {
        stop(d);
        if (d.haptic) {
            SDL_CloseHaptic(d.haptic);
            SDL_QuitSubSystem(SDL_INIT_HAPTIC); // one init per opened wheel
        }
    }
}

ForceFeedback::Dev& ForceFeedback::dev(SDL_JoystickID id) {
    for (Dev& d : devs_)
        if (d.id == id) return d;
    devs_.push_back(Dev{});
    devs_.back().id = id;
    return devs_.back();
}

void ForceFeedback::apply(SDL_JoystickID id, SDL_Gamepad* gamepad, SDL_Joystick* joystick, int brake_trigger,
                          int throttle_trigger, const ForceFeedbackState& s, double time) {
    Dev& d = dev(id);
    const bool resumed = !d.active;
    d.gamepad = gamepad;
    d.active = true;
    if (!cfg_.enabled) {
        stop(d);
        return;
    }
    if (gamepad) {
        SDL_GamepadType type = SDL_GetGamepadType(gamepad);
        if (type == SDL_GAMEPAD_TYPE_PS5 && (brake_trigger >= 0 || throttle_trigger >= 0)) {
            // Both triggers in one packet: the brake's, the throttle's, else off.
            auto effect = [&](int axis) {
                if (axis == brake_trigger) return ffb::brake_trigger_effect(s, cfg_);
                if (axis == throttle_trigger) return ffb::throttle_trigger_effect(s, cfg_);
                return ffb::trigger_off();
            };
            ffb::TriggerEffect l = effect(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), r = effect(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            if (!d.sent_triggers || l != d.sent_left || r != d.sent_right) {
                send_triggers(d, l, r);
                d.sent_left = l, d.sent_right = r;
                d.sent_triggers = true;
            }
        } else if ((brake_trigger >= 0 || throttle_trigger >= 0) && cfg_.triggers) {
            // Xbox One/Series impulse triggers (no-op on pads without them): the ABS on the brake's trigger, the
            // edge of grip on the throttle's.
            auto level = [&](int axis) {
                if (axis == brake_trigger) return ffb::impulse_brake(s, time);
                if (axis == throttle_trigger) return ffb::impulse_throttle(s);
                return 0.0f;
            };
            const float l = level(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), r = level(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            if (l > 0.0f || r > 0.0f || d.impulse_on) {
                SDL_RumbleGamepadTriggers(gamepad, Uint16(l * 65535), Uint16(r * 65535), 100);
                d.impulse_on = l > 0.0f || r > 0.0f;
            }
        }
        return;
    }
    if (joystick && cfg_.wheel) {
        if (resumed) d.understeer = d.oversteer = 0, d.eased_at = time;
        ForceFeedbackState felt = s;
        const float dt = float(time - d.eased_at);
        felt.understeer = d.understeer = ffb::eased_indicator(d.understeer, s.understeer, dt);
        felt.oversteer = d.oversteer = ffb::eased_indicator(d.oversteer, s.oversteer, dt);
        d.eased_at = time;
        apply_wheel(d, joystick, felt, time, resumed);
    }
}

namespace {

// Both wheel effects act on the steering axis alone (DirectInput's single-axis form: one axis,
// Cartesian, direction 0) and play once for an infinite length. (Not SDL_HAPTIC_INFINITY plays:
// SDL's Logitech HIDAPI driver takes that count as -1 and stops the effect.) On Windows and macOS
// that length isn't infinite on the wheel: ffb::wheel_rerun_due.
SDL_HapticEffect wheel_spring(int coef) {
    SDL_HapticEffect e{};
    e.type = SDL_HAPTIC_SPRING;
    e.condition.type = SDL_HAPTIC_SPRING;
    e.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
    e.condition.length = SDL_HAPTIC_INFINITY;
    e.condition.right_sat[0] = e.condition.left_sat[0] = 0xFFFF;
    e.condition.right_coeff[0] = e.condition.left_coeff[0] = Sint16(coef);
    return e;
}

SDL_HapticEffect wheel_constant(int level) {
    SDL_HapticEffect e{};
    e.type = SDL_HAPTIC_CONSTANT;
    e.constant.type = SDL_HAPTIC_CONSTANT;
    e.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
    e.constant.length = SDL_HAPTIC_INFINITY;
    e.constant.level = Sint16(level);
    return e;
}

int start_effect(SDL_Haptic* h, SDL_HapticEffect e, const char* what) {
    int id = SDL_CreateHapticEffect(h, &e);
    if (id < 0 || !SDL_RunHapticEffect(h, id, 1))
        LOG_WARN("Force feedback: no %s effect: %s", what, SDL_GetError());
    return id;
}

} // namespace

bool ForceFeedback::drives_wheel(SDL_JoystickID id) const {
    for (const Dev& d : devs_)
        if (d.id == id) return d.haptic && (d.spring_id >= 0 || d.constant_id >= 0);
    return false;
}

bool ForceFeedback::has_wheel_forces(SDL_Joystick* joystick) {
    if (!SDL_IsJoystickHaptic(joystick) || !SDL_InitSubSystem(SDL_INIT_HAPTIC)) return false;
    bool forces = false;
    if (SDL_Haptic* h = SDL_OpenHapticFromJoystick(joystick)) {
        const Uint32 features = SDL_GetHapticFeatures(h);
        forces = (features & SDL_HAPTIC_SPRING) && (features & SDL_HAPTIC_CONSTANT);
        SDL_CloseHaptic(h);
    }
    SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    return forces;
}

void ForceFeedback::apply_wheel(Dev& d, SDL_Joystick* joystick, const ForceFeedbackState& s, double time, bool restart) {
    if (!d.haptic && !d.haptic_tried) {
        d.haptic_tried = true;
        if (!SDL_InitSubSystem(SDL_INIT_HAPTIC)) return;
        if (SDL_IsJoystickHaptic(joystick)) d.haptic = SDL_OpenHapticFromJoystick(joystick);
        if (!d.haptic) {
            SDL_QuitSubSystem(SDL_INIT_HAPTIC);
            return;
        }
        Uint32 features = SDL_GetHapticFeatures(d.haptic);
        LOG_INFO("Force feedback: %s (spring %s, constant %s)", SDL_GetHapticName(d.haptic),
                 (features & SDL_HAPTIC_SPRING) ? "yes" : "no", (features & SDL_HAPTIC_CONSTANT) ? "yes" : "no");
        // We centre ourselves, in GT3 original too: GT3 leaves a Logitech wheel's own spring on, but only the
        // spring effect plays the same on every wheel (SDL's DirectInput open turns a wheel's own off).
        if (features & SDL_HAPTIC_AUTOCENTER) SDL_SetHapticAutocenter(d.haptic, 0);
        if (features & SDL_HAPTIC_SPRING) d.spring_id = start_effect(d.haptic, wheel_spring(0), "spring");
        if (features & SDL_HAPTIC_CONSTANT) d.constant_id = start_effect(d.haptic, wheel_constant(0), "constant force");
        d.effects_at = time;
    } else if (d.haptic && (restart || ffb::wheel_rerun_due(d.effects_at, time))) {
        // Each time feedback resumes (a race, after a pause): anything that reset the device's
        // force feedback meanwhile (its driver, another program) stopped the effects. And every few
        // seconds where the wheel's driver ends an "infinite" effect: playing it again starts it
        // over with the same force (no envelope), before it runs out.
        for (int id : {d.spring_id, d.constant_id})
            if (id >= 0) SDL_RunHapticEffect(d.haptic, id, 1);
        d.effects_at = time;
    }
    if (!d.haptic) return;
    // Only talk to the device when a value moved (USB bandwidth): the spring, which follows the
    // speed, noticeably; the steering force at each of GT3's steps (1/127 of full force, 258 at
    // 100% strength: a threshold above one step left the smallest forces around the centre out).
    int coef = ffb::wheel_spring_coefficient(s, cfg_);
    if (d.spring_id >= 0 && std::abs(coef - d.spring_coef) > 300) {
        SDL_HapticEffect e = wheel_spring(coef);
        SDL_UpdateHapticEffect(d.haptic, d.spring_id, &e);
        d.spring_coef = coef;
    }
    int level = ffb::wheel_constant_level(s, cfg_);
    if (d.constant_id >= 0 && level != d.constant_level) {
        SDL_HapticEffect e = wheel_constant(level);
        SDL_UpdateHapticEffect(d.haptic, d.constant_id, &e);
        d.constant_level = level;
    }
}

void ForceFeedback::stop(SDL_JoystickID id) {
    for (Dev& d : devs_)
        if (d.id == id) stop(d);
}

void ForceFeedback::stop(Dev& d) {
    if (!d.active) return;
    d.active = false;
    if (d.gamepad && d.sent_triggers) send_triggers(d, ffb::trigger_off(), ffb::trigger_off());
    if (d.gamepad && d.impulse_on) SDL_RumbleGamepadTriggers(d.gamepad, 0, 0, 0);
    d.sent_triggers = false;
    d.impulse_on = false;
    if (d.haptic) {
        // Zero the forces but keep the effects allocated for the next race.
        ForceFeedbackState idle;
        ForceFeedbackConfig off = cfg_;
        off.spring = 0;
        d.spring_coef = d.constant_level = 0x7fffff;
        if (d.spring_id >= 0 || d.constant_id >= 0) {
            ForceFeedbackConfig saved = cfg_;
            cfg_ = off;
            apply_wheel(d, nullptr, idle, d.effects_at);
            cfg_ = saved;
        }
    }
}

// SDL builds the motors it plays into each effects packet it sends (SDL_hidapi_ps5.c, UpdateEffects): a
// packet without the rumble bits puts the pad back on its audio haptics, and the motors stop until SDL's
// next change. Ours carry them the same way, while SDL still plays them: it stops them 250 ms after the
// last request (by its clock), and motors put back after that would run on.
void ForceFeedback::send_triggers(Dev& d, const ffb::TriggerEffect& left, const ffb::TriggerEffect& right) {
    const bool playing = SDL_GetTicks() - d.rumble_ticks < 200;
    const ffb::Ds5Packet pkt = ffb::ds5_trigger_packet(left, right, playing ? d.rumble_low : 0, playing ? d.rumble_high : 0,
                                                       ds5_improved_rumble(d.gamepad));
    SDL_SendGamepadEffect(d.gamepad, pkt.data(), int(pkt.size()));
}

void ForceFeedback::rumbling(SDL_JoystickID id, u8 low, u8 high) {
    Dev& d = dev(id);
    d.rumble_low = low, d.rumble_high = high;
    d.rumble_ticks = SDL_GetTicks();
}

void ForceFeedback::forget(SDL_JoystickID id) {
    auto it = std::find_if(devs_.begin(), devs_.end(), [&](const Dev& d) { return d.id == id; });
    if (it == devs_.end()) return;
    if (it->haptic) {
        SDL_CloseHaptic(it->haptic);
        SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    }
    devs_.erase(it);
}

} // namespace gt3r
