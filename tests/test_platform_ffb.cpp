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
    CHECK(ffb::throttle_trigger_effect(s, c) == ffb::trigger_off()); // no wheelspin: free
    s.wheelspin = 0.2f;
    ffb::TriggerEffect light = ffb::throttle_trigger_effect(s, c);
    CHECK(light[0] == 0x06 && light[1] > 0 && light[2] == 13); // a buzz while the wheels spin, from zero
    s.wheelspin = 1;
    CHECK(ffb::throttle_trigger_effect(s, c)[2] > light[2]); // stronger with more wheelspin
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

TEST(ffb_xbox_impulse_pulses_only_with_abs) {
    ForceFeedbackState s;
    CHECK(ffb::impulse_brake(s, 0.01) == 0.0f && ffb::impulse_throttle(s) == 0.0f);
    s.abs_level = 1;
    int on = 0;
    float peak = 0;
    for (int i = 0; i < 100; ++i) {
        on += ffb::impulse_brake(s, i / 100.0) > 0 ? 1 : 0;
        peak = std::max(peak, ffb::impulse_brake(s, i / 100.0));
    }
    CHECK(on > 40 && on < 60 && std::fabs(peak - 0.8f) < 1e-6f); // ~50 % duty
    s.abs_level = 0.5f;
    peak = 0;
    for (int i = 0; i < 100; ++i) peak = std::max(peak, ffb::impulse_brake(s, i / 100.0));
    CHECK(std::fabs(peak - 0.4f) < 1e-6f); // by the ABS level
    s.wheelspin = 0.6f;
    CHECK(std::fabs(ffb::impulse_throttle(s) - 0.3f) < 1e-6f); // the throttle trigger: half the wheelspin
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

TEST(ffb_config_roundtrip) {
    IniDocument ini = IniDocument::parse("[ffb]\nenabled = off\nwheel = off\nstrength = 5\nspring = 0.3\ninvert = on\n");
    ForceFeedbackConfig c = ForceFeedbackConfig::load(ini);
    CHECK(!c.enabled && !c.wheel && c.triggers && c.invert);
    CHECK(c.strength == 2.0f); // clamped
    IniDocument out;
    c.save(out);
    ForceFeedbackConfig d = ForceFeedbackConfig::load(out);
    CHECK(!d.enabled && !d.wheel && d.invert && std::fabs(d.spring - 0.3f) < 1e-3f);
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
