#include "input/force_feedback.h"

#include "base/config.h"
#include "base/log.h"

#include <SDL3/SDL_init.h>

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

// DualSense output report effects block (SDL_SendGamepadEffect payload for
// PS5 controllers; same layout as SDL's test/testcontroller.c DS5EffectsState_t).
struct Ds5Effects {
    u8 enable_bits1;     // 0x04 right trigger, 0x08 left trigger
    u8 enable_bits2;
    u8 rumble_right, rumble_left;
    u8 headphone_volume, speaker_volume, microphone_volume, audio_enable_bits;
    u8 mic_light_mode, audio_mute_bits;
    u8 right_trigger[11];
    u8 left_trigger[11];
    u8 unknown1[6];
    u8 led_flags;
    u8 unknown2[2];
    u8 led_anim, led_brightness, pad_lights, led_red, led_green, led_blue;
};
static_assert(sizeof(Ds5Effects) == 47, "DS5 effects block layout");

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
    return c;
}

void ForceFeedbackConfig::save(IniDocument& ini) const {
    ini.set("ffb", "enabled", enabled ? "on" : "off");
    ini.set("ffb", "triggers", triggers ? "on" : "off");
    ini.set("ffb", "wheel", wheel ? "on" : "off");
    ini.set("ffb", "strength", fmt(strength));
    ini.set("ffb", "spring", fmt(spring));
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
    if (!c.enabled || !c.triggers) return trigger_off();
    if (s.abs_active) return trigger_vibration(0.1f, 18, 1.0f); // ABS pulse under the finger
    // Firm pedal: resistance builds after the first 20 % of travel. Rolling
    // slowly it's lighter (less brake to feel).
    float speed = std::clamp(s.speed_kmh / 60.0f, 0.3f, 1.0f);
    return trigger_resistance(0.2f, c.trigger_force * speed);
}

int wheel_spring_coefficient(const ForceFeedbackState& s, const ForceFeedbackConfig& c) {
    if (!c.enabled || !c.wheel) return 0;
    // Light centring when stopped, full at 120 km/h and above.
    float k = c.spring * (0.15f + 0.85f * std::clamp(s.speed_kmh / 120.0f, 0.0f, 1.0f));
    return int(std::lround(std::clamp(k * c.strength, 0.0f, 1.0f) * 32767.0f));
}

int wheel_constant_level(const ForceFeedbackState& s, const ForceFeedbackConfig& c) {
    if (!c.enabled || !c.wheel) return 0;
    float f = std::clamp(s.steer_force, -1.0f, 1.0f) + std::clamp(s.jolt, 0.0f, 1.0f) * (s.steer_force < 0 ? -0.5f : 0.5f);
    if (c.invert) f = -f;
    return int(std::lround(std::clamp(f * c.strength, -1.0f, 1.0f) * 32767.0f));
}

float impulse_brake(const ForceFeedbackState& s, double t) {
    if (!s.abs_active) return 0.0f;
    return std::fmod(t * 16.0, 1.0) < 0.5 ? 0.8f : 0.0f; // ~16 Hz square pulse
}

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
                          const ForceFeedbackState& s, double time) {
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
        if (type == SDL_GAMEPAD_TYPE_PS5 && brake_trigger >= 0) {
            ffb::TriggerEffect e = ffb::brake_trigger_effect(s, cfg_);
            if (e != d.sent || brake_trigger != d.sent_trigger) {
                Ds5Effects pkt{};
                bool left = brake_trigger == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                pkt.enable_bits1 = 0x04 | 0x08; // update both: clears the other one
                std::memcpy(left ? pkt.left_trigger : pkt.right_trigger, e.data(), e.size());
                ffb::TriggerEffect off = ffb::trigger_off();
                std::memcpy(left ? pkt.right_trigger : pkt.left_trigger, off.data(), off.size());
                SDL_SendGamepadEffect(gamepad, &pkt, sizeof(pkt));
                d.sent = e;
                d.sent_trigger = brake_trigger;
            }
        } else if (brake_trigger >= 0 && cfg_.triggers) {
            // Xbox One/Series impulse triggers (no-op on pads without them).
            float v = ffb::impulse_brake(s, time);
            if (v > 0.0f || d.impulse_on) {
                float l = brake_trigger == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? v : 0.0f;
                float r = brake_trigger == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? v : 0.0f;
                SDL_RumbleGamepadTriggers(gamepad, Uint16(l * 65535), Uint16(r * 65535), 100);
                d.impulse_on = v > 0.0f;
            }
        }
        return;
    }
    if (joystick && cfg_.wheel) apply_wheel(d, joystick, s, resumed);
}

namespace {

// Both wheel effects act on the steering axis alone (DirectInput's single-axis form: one axis,
// Cartesian, direction 0) and play once for an infinite length. (Not SDL_HAPTIC_INFINITY plays:
// SDL's Logitech HIDAPI driver takes that count as -1 and stops the effect.)
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

void ForceFeedback::apply_wheel(Dev& d, SDL_Joystick* joystick, const ForceFeedbackState& s, bool restart) {
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
        if (features & SDL_HAPTIC_AUTOCENTER) SDL_SetHapticAutocenter(d.haptic, 0); // we centre ourselves
        if (features & SDL_HAPTIC_SPRING) d.spring_id = start_effect(d.haptic, wheel_spring(0), "spring");
        if (features & SDL_HAPTIC_CONSTANT) d.constant_id = start_effect(d.haptic, wheel_constant(0), "constant force");
    } else if (restart && d.haptic) {
        // Each time feedback resumes (a race, after a pause): anything that reset the device's
        // force feedback meanwhile (its driver, another program) stopped the effects.
        for (int id : {d.spring_id, d.constant_id})
            if (id >= 0) SDL_RunHapticEffect(d.haptic, id, 1);
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
    if (d.gamepad && d.sent_trigger >= 0) {
        Ds5Effects pkt{};
        pkt.enable_bits1 = 0x04 | 0x08;
        ffb::TriggerEffect off = ffb::trigger_off();
        std::memcpy(pkt.left_trigger, off.data(), off.size());
        std::memcpy(pkt.right_trigger, off.data(), off.size());
        SDL_SendGamepadEffect(d.gamepad, &pkt, sizeof(pkt));
    }
    if (d.gamepad && d.impulse_on) SDL_RumbleGamepadTriggers(d.gamepad, 0, 0, 0);
    d.sent_trigger = -2;
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
            apply_wheel(d, nullptr, idle);
            cfg_ = saved;
        }
    }
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
