// Controller feedback shaping (input/force_feedback.h). Device output itself
// needs hardware; these check the effect values the devices are sent.

#include "test_framework.h"

#include "base/config.h"
#include "input/force_feedback.h"
#include "input/input_manager.h"

#include <algorithm>
#include <cmath>

using namespace gt3r;

TEST(ffb_dualsense_brake_trigger) {
    ForceFeedbackConfig c;
    ForceFeedbackState s;
    s.speed_kmh = 100;
    ffb::TriggerEffect e = ffb::brake_trigger_effect(s, c);
    CHECK(e[0] == 0x01);                 // continuous resistance
    CHECK(e[2] == u8(std::lround(0.6f * 255))); // trigger_force at speed
    s.speed_kmh = 0;
    CHECK(ffb::brake_trigger_effect(s, c)[2] < e[2]); // lighter when stopped
    s.abs_level = 1;
    ffb::TriggerEffect abs = ffb::brake_trigger_effect(s, c);
    CHECK(abs[0] == 0x06 && abs[1] > 0 && abs[2] == 63); // vibration while ABS works, by its level from zero
    s.abs_level = 0.25f;
    CHECK(ffb::brake_trigger_effect(s, c)[2] == 16);
    c.triggers = false;
    CHECK(ffb::brake_trigger_effect(s, c) == ffb::trigger_off());
    c.triggers = true;
    c.enabled = false;
    CHECK(ffb::brake_trigger_effect(s, c)[0] == 0x05);
}

TEST(ffb_dualsense_throttle_trigger) {
    ForceFeedbackConfig c;
    ForceFeedbackState s;
    CHECK(ffb::throttle_trigger_effect(s, c) == ffb::trigger_off()); // grip to spare: free
    s.traction = 0.2f;
    ffb::TriggerEffect light = ffb::throttle_trigger_effect(s, c);
    CHECK(light[0] == 0x06 && light[1] > 0 && light[2] == 13); // a buzz nearing the edge of grip, from zero
    s.traction = 1;
    CHECK(ffb::throttle_trigger_effect(s, c)[2] > light[2]); // stronger past it
    c.triggers = false;
    CHECK(ffb::throttle_trigger_effect(s, c) == ffb::trigger_off());
    c.triggers = true;
    c.enabled = false;
    CHECK(ffb::throttle_trigger_effect(s, c) == ffb::trigger_off());
}

// The trigger packet keeps the motors SDL plays, as SDL's own packets do (bytes as SDL_hidapi_ps5.c's
// DS5EffectsState_t): without the rumble bits the pad drops back to its audio haptics and the rumble stops.
TEST(ffb_dualsense_packet_keeps_rumble) {
    const ffb::TriggerEffect brake = ffb::trigger_resistance(0.2f, 0.6f), off = ffb::trigger_off();
    ffb::Ds5Packet p = ffb::ds5_trigger_packet(brake, off, 0, 0, true);
    CHECK(p[0] == (0x04 | 0x08) && p[2] == 0 && p[3] == 0 && p[38] == 0); // no rumble: triggers only
    CHECK(std::equal(brake.begin(), brake.end(), p.begin() + 21)); // left trigger block
    CHECK(std::equal(off.begin(), off.end(), p.begin() + 10));     // right trigger block
    // Firmware 2.24 on: the improved rumble emulation at full strength.
    p = ffb::ds5_trigger_packet(brake, off, 200, 100, true);
    CHECK(p[0] == (0x04 | 0x08 | 0x02) && p[38] == 0x04);
    CHECK(p[3] == 200 && p[2] == 100); // left (low-frequency) motor, right (high-frequency) motor
    // Older firmware: the rumble emulation, halved as SDL halves it.
    p = ffb::ds5_trigger_packet(brake, off, 200, 0, false);
    CHECK(p[0] == (0x04 | 0x08 | 0x02 | 0x01) && p[38] == 0);
    CHECK(p[3] == 100 && p[2] == 0);
}

TEST(ffb_xbox_impulse_throbs_only_with_abs) {
    ForceFeedbackState s;
    CHECK(ffb::impulse_brake(s, 0.01) == 0.0f && ffb::impulse_throttle(s) == 0.0f);
    s.abs_level = 1;
    int high = 0;
    float peak = 0, low = 1;
    for (int i = 0; i < 100; ++i) {
        const float v = ffb::impulse_brake(s, i / 100.0);
        high += v > 0.5f ? 1 : 0;
        peak = std::max(peak, v), low = std::min(low, v);
    }
    CHECK(high > 40 && high < 60 && std::fabs(peak - 1.0f) < 1e-6f); // half the time at the level
    CHECK(std::fabs(low - 0.4f) < 1e-6f);                            // the motor never stops
    s.abs_level = 0.05f;
    peak = 0;
    for (int i = 0; i < 100; ++i) peak = std::max(peak, ffb::impulse_brake(s, i / 100.0));
    CHECK(peak > 0.3f && peak < 0.4f); // a faint ABS still turns the motor
    s.traction = 0.05f;
    const float faint = ffb::impulse_throttle(s);
    CHECK(faint > 0.3f && faint < 0.4f);
    s.traction = 1;
    CHECK(std::fabs(ffb::impulse_throttle(s) - 1.0f) < 1e-6f);
}

TEST(ffb_wheel_spring_and_force) {
    ForceFeedbackConfig c;
    ForceFeedbackState s;
    int stopped = ffb::wheel_spring_coefficient(s, c);
    s.speed_kmh = 200;
    int fast = ffb::wheel_spring_coefficient(s, c);
    CHECK(stopped > 0 && fast > stopped * 4);
    CHECK(fast <= 32767);
    s.steer_force = 0.5f;
    CHECK(std::abs(ffb::wheel_constant_level(s, c) - 16384) <= 1);
    c.invert = true;
    CHECK(ffb::wheel_constant_level(s, c) < 0);
    c.invert = false;
    s.steer_force = 3.0f;
    CHECK(ffb::wheel_constant_level(s, c) == 32767);
    c.wheel = false;
    CHECK(ffb::wheel_constant_level(s, c) == 0 && ffb::wheel_spring_coefficient(s, c) == 0);
}

// GT3 original is GT3's force alone over a plain spring: nothing on impacts, in understeer or in a slide.
TEST(ffb_wheel_gt3_original) {
    ForceFeedbackConfig c;
    CHECK(!c.gt3_wheel());
    c.wheel_mode = "gt3";
    CHECK(c.gt3_wheel());
    ForceFeedbackState s;
    const int stopped = ffb::wheel_spring_coefficient(s, c);
    CHECK(stopped == int(std::lround(c.spring * 32767.0f)));
    s.speed_kmh = 200, s.understeer = 1, s.oversteer = 1;
    CHECK(ffb::wheel_spring_coefficient(s, c) == stopped); // no change with speed or grip
    s.steer_force = 0.5f, s.jolt = 1;
    CHECK(std::abs(ffb::wheel_constant_level(s, c) - 16384) <= 1);
}

// Modern's understeer: the force and the spring go by the setting's share at full understeer, the
// spring also out of a slide; a setting of 0 changes nothing. Bumps and hits stay.
TEST(ffb_wheel_modern_understeer) {
    ForceFeedbackConfig c;
    ForceFeedbackState s;
    s.speed_kmh = 200, s.steer_force = 0.8f;
    const int grip = ffb::wheel_constant_level(s, c), spring = ffb::wheel_spring_coefficient(s, c);
    s.understeer = 1;
    CHECK(std::abs(ffb::wheel_constant_level(s, c) - grip / 2) <= 1);
    CHECK(std::abs(ffb::wheel_spring_coefficient(s, c) - spring / 2) <= 1);
    s.steer_force = -0.8f;
    CHECK(std::abs(ffb::wheel_constant_level(s, c) + grip / 2) <= 1); // the same either way
    s.steer_force = 0.8f;
    c.understeer = 0;
    CHECK(ffb::wheel_constant_level(s, c) == grip && ffb::wheel_spring_coefficient(s, c) == spring);
    c.understeer = 0.5f;
    s.understeer = 0, s.oversteer = 1;
    CHECK(ffb::wheel_constant_level(s, c) == grip); // the slide's force is GT3's
    CHECK(ffb::wheel_spring_coefficient(s, c) == 0);
    s.oversteer = 0, s.steer_force = 0, s.jolt = 1;
    CHECK(ffb::wheel_constant_level(s, c) == 16384);
}

// The steering force and the spring each go to 0 without the other, for the spring or the force alone.
TEST(ffb_wheel_force_and_spring_apart) {
    ForceFeedbackConfig c;
    ForceFeedbackState s;
    s.speed_kmh = 200, s.steer_force = 0.5f;
    const int level = ffb::wheel_constant_level(s, c), spring = ffb::wheel_spring_coefficient(s, c);
    c.steering = 0;
    CHECK(ffb::wheel_constant_level(s, c) == 0 && ffb::wheel_spring_coefficient(s, c) == spring);
    s.jolt = 1;
    CHECK(ffb::wheel_constant_level(s, c) == 0); // no kick either
    s.jolt = 0;
    c.steering = 1, c.spring = 0;
    CHECK(ffb::wheel_constant_level(s, c) == level && ffb::wheel_spring_coefficient(s, c) == 0);
    c.spring = 0.6f, c.strength = 0.5f;
    CHECK(std::abs(ffb::wheel_constant_level(s, c) - level / 2) <= 1);
    CHECK(std::abs(ffb::wheel_spring_coefficient(s, c) - spring / 2) <= 1);
}

// As GT3, the GT FORCE option switches the wheel's force and Vibration doesn't; Vibration the triggers.
TEST(ffb_options_switch_wheel_and_triggers_apart) {
    ForceFeedbackConfig c;
    ForceFeedbackState s;
    s.speed_kmh = 100, s.steer_force = 0.5f, s.abs_level = 1, s.traction = 1;
    s.triggers_on = false;
    CHECK(ffb::wheel_constant_level(s, c) != 0 && ffb::wheel_spring_coefficient(s, c) != 0);
    CHECK(ffb::brake_trigger_effect(s, c) == ffb::trigger_off() && ffb::throttle_trigger_effect(s, c) == ffb::trigger_off());
    CHECK(ffb::impulse_brake(s, 0) == 0 && ffb::impulse_throttle(s) == 0);
    s.triggers_on = true, s.wheel_on = false;
    CHECK(ffb::wheel_constant_level(s, c) == 0 && ffb::wheel_spring_coefficient(s, c) == 0);
    CHECK(ffb::brake_trigger_effect(s, c) != ffb::trigger_off() && ffb::impulse_throttle(s) > 0);
}

TEST(ffb_indicators_eased) {
    CHECK(ffb::eased_indicator(0, 0.65f, 0) == 0);
    float felt = ffb::eased_indicator(0, 1, 1.0f / 60);
    CHECK(felt > 0.06f && felt < 0.07f);                       // a one-tick spike barely moves it
    CHECK(ffb::eased_indicator(felt, 0, 1.0f / 60) < felt);
    CHECK(ffb::eased_indicator(0, 1, 1) == 1 && ffb::eased_indicator(0, 3, 1) == 1); // long gaps land, in range
    for (int i = 0; i < 120; ++i) felt = ffb::eased_indicator(felt, 0.5f, 1.0f / 60);
    CHECK(std::fabs(felt - 0.5f) < 0.01f);
}

TEST(ffb_config_roundtrip) {
    IniDocument ini = IniDocument::parse("[ffb]\nenabled = off\nwheel = off\nstrength = 5\nspring = 0.3\ninvert = on\n"
                                         "wheel_mode = GT3\nsteering = 0.25\nundersteer = 7\n");
    ForceFeedbackConfig c = ForceFeedbackConfig::load(ini);
    CHECK(!c.enabled && !c.wheel && c.triggers && c.invert);
    CHECK(c.strength == 2.0f && c.understeer == 1.0f); // clamped
    IniDocument out;
    c.save(out);
    ForceFeedbackConfig d = ForceFeedbackConfig::load(out);
    CHECK(!d.enabled && !d.wheel && d.invert && std::fabs(d.spring - 0.3f) < 1e-3f);
    CHECK(d.gt3_wheel() && std::fabs(d.steering - 0.25f) < 1e-3f && d.understeer == 1.0f);
    CHECK(ForceFeedbackConfig::load(IniDocument::parse("[ffb]\nwheel_mode = sideways\n")).wheel_mode == "auto");
    CHECK(ForceFeedbackConfig::load(IniDocument::parse("[ffb]\nwheel_mode = modern\n")).wheel_mode == "modern");
}

TEST(ffb_input_manager_without_devices) {
    InputManager input; // no devices: must be a harmless no-op
    ForceFeedbackState s;
    s.abs_level = 1;
    input.set_force_feedback(0, s);
    input.update(0.016);
    input.update(0.5); // times out
    CHECK(input.ffb_config().enabled);
    input.set_rumble(0, PadRumble{160, 200}); // no device: nothing to rumble
    ForceFeedback ffb;
    CHECK(!ffb.drives_wheel(1)); // no wheel effects made: joystick rumble still goes to the device
}

TEST(phone_vibration_only_without_controller_rumble) {
    // Off by default; on, only player 1 and only when the device driving them has no rumble motor.
    CHECK(!use_phone_vibrator(false, 0, false));
    CHECK(use_phone_vibrator(true, 0, false));  // built-in controls / touch / a pad without motors
    CHECK(!use_phone_vibrator(true, 0, true));  // a controller that rumbles itself
    CHECK(!use_phone_vibrator(true, 1, false)); // player 2 never shakes the phone
    CHECK(phone_vibration_strength(PadRumble{}) == 0);
    CHECK(std::fabs(phone_vibration_strength(PadRumble{0, 255}) - 1.0f) < 1e-6f);
    CHECK(std::fabs(phone_vibration_strength(PadRumble{153, 0}) - 0.6f) < 1e-6f);
    CHECK(std::fabs(phone_vibration_strength(PadRumble{160, 200}) - 200 / 255.0f) < 1e-6f);
}

TEST(dualsense_rumble_right_grip) {
    // A DualSense gets the small motor (the right grip) 1.75 times stronger, at most full; the large motor as sent.
    const PadRumble a = dualsense_rumble(PadRumble{100, 120});
    CHECK(a.small == 175 && a.large == 120);
    const PadRumble b = dualsense_rumble(PadRumble{200, 0});
    CHECK(b.small == 255 && b.large == 0);
    const PadRumble c = dualsense_rumble(PadRumble{});
    CHECK(c.small == 0 && c.large == 0);
    InputManager input;
    CHECK(!input.phone_vibration());
    input.load(IniDocument::parse("[player1]\nphone_vibration = on\n"));
    CHECK(input.phone_vibration());
    input.set_rumble(0, PadRumble{160, 200}); // no vibrator off Android: a harmless no-op
    input.set_rumble(0, PadRumble{});
    IniDocument out;
    input.save(out);
    CHECK(out.get("player1", "phone_vibration") == "on" && !out.get("player2", "phone_vibration"));
    InputManager fresh;
    IniDocument def;
    fresh.save(def);
    CHECK(def.get("player1", "phone_vibration") == "off");
}

TEST(race_rumble_mode) {
    // Auto: Modern on every controller, the DualSense too; gt3 is GT3 original.
    CHECK(!gt3_rumble("auto") && !gt3_rumble("modern") && gt3_rumble("gt3"));
    CHECK(!gt3_rumble("")); // an unknown value reads as auto
    // Per player, auto by default; no device is no DualSense.
    InputManager input;
    CHECK(input.rumble_mode(0) == "auto" && input.rumble_mode(1) == "auto");
    CHECK(!input.dualsense(0) && !input.gt3_rumble(0));
    input.load(IniDocument::parse("[player1]\nrumble = GT3\n[player2]\nrumble = modern\n"));
    CHECK(input.rumble_mode(0) == "gt3" && input.gt3_rumble(0));
    CHECK(input.rumble_mode(1) == "modern" && !input.gt3_rumble(1));
    input.set_rumble_mode(1, "auto");
    IniDocument out;
    input.save(out);
    CHECK(out.get("player1", "rumble") == "gt3" && out.get("player2", "rumble") == "auto");
}
