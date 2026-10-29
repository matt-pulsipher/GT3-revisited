// On-screen touch controls (input/touch_controls.h): layout hit-testing, pad
// output, visibility rules and the merge into InputManager.

#include "test_framework.h"

#include "base/config.h"
#include "input/input_manager.h"
#include "input/touch_controls.h"
#include "sim/exact/pad_read.h"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>

using namespace gt3r;

namespace {

using Widget = TouchControls::Widget;

const Widget* find_widget(const TouchControls& t, Widget::Kind kind, PadButton b = PadButton::Count) {
    for (const Widget& w : t.widgets())
        if (w.kind == kind && (b == PadButton::Count || w.button == b)) return &w;
    return nullptr;
}

TouchControls driving(TouchConfig cfg = {}) {
    TouchControls t;
    t.configure(cfg);
    t.set_layout(TouchLayout::Driving);
    return t;
}

} // namespace

TEST(touch_hidden_until_touched) {
    TouchControls t = driving();
    CHECK(!t.visible());
    t.update(0.016, 0);
    CHECK(!t.pad().connected);
    const Widget* gas = find_widget(t, Widget::Kind::Pedal, PadButton::Cross);
    CHECK(gas != nullptr);
    // The first touch both shows the controls and acts.
    CHECK(t.finger_down(1, gas->x + gas->w / 2, gas->y + gas->h / 2, 1.0f));
    CHECK(t.visible());
    t.update(0.016, 0);
    CHECK(t.pad().connected);
    CHECK(t.pad().down(PadButton::Cross));
    // Gamepad / keyboard input hides them and releases everything.
    t.note_other_input();
    CHECK(!t.visible());
    t.update(0.016, 0);
    CHECK(!t.pad().down(PadButton::Cross));
    CHECK(!t.pad().connected);
}

TEST(touch_modes) {
    TouchConfig cfg;
    cfg.mode = TouchConfig::Mode::On;
    TouchControls on = driving(cfg);
    CHECK(on.visible());
    cfg.mode = TouchConfig::Mode::Off;
    TouchControls off = driving(cfg);
    off.note_touch();
    CHECK(!off.visible());
    CHECK(!off.finger_down(1, 600, 300, 1.0f));
    TouchControls none;
    none.note_touch();
    CHECK(!none.visible()); // TouchLayout::None
}

TEST(touch_pedal_pressure_from_position) {
    TouchControls t = driving();
    const Widget gas = *find_widget(t, Widget::Kind::Pedal, PadButton::Cross);
    t.finger_down(1, gas.x + gas.w / 2, gas.y + gas.h - 1, 1.0f); // bottom edge
    t.update(0.016, 0);
    u8 low = t.pad().press(PadButton::Cross);
    t.finger_move(1, gas.x + gas.w / 2, gas.y + 2, 1.0f); // top
    t.update(0.016, 0);
    u8 high = t.pad().press(PadButton::Cross);
    CHECK(low > 60 && low < 120); // pedal_min 0.35
    CHECK(high == 255);
    CHECK(t.pad().down(PadButton::Cross));

    TouchConfig full;
    full.pedals = TouchConfig::Pedals::Full;
    TouchControls f = driving(full);
    f.finger_down(1, gas.x + gas.w / 2, gas.y + gas.h - 1, 0.2f);
    f.update(0.016, 0);
    CHECK(f.pad().press(PadButton::Cross) == 255);

    TouchConfig area;
    area.pedals = TouchConfig::Pedals::Area;
    TouchControls a = driving(area);
    a.finger_down(1, gas.x + gas.w / 2, gas.y + 2, 0.6f);
    a.update(0.016, 0);
    CHECK(std::abs(int(a.pad().press(PadButton::Cross)) - 153) <= 1);
}

TEST(touch_slide_between_pedals) {
    TouchControls t = driving();
    const Widget gas = *find_widget(t, Widget::Kind::Pedal, PadButton::Cross);
    const Widget brake = *find_widget(t, Widget::Kind::Pedal, PadButton::Square);
    t.finger_down(7, brake.x + brake.w / 2, brake.y + brake.h / 2, 1.0f);
    t.update(0.016, 0);
    CHECK(t.pad().down(PadButton::Square));
    CHECK(!t.pad().down(PadButton::Cross));
    t.finger_move(7, gas.x + gas.w / 2, gas.y + gas.h / 2, 1.0f);
    t.update(0.016, 0);
    CHECK(t.pad().down(PadButton::Cross));
    CHECK(!t.pad().down(PadButton::Square));
    t.finger_up(7);
    t.update(0.016, 0);
    CHECK(t.pad().buttons == 0);
}

TEST(touch_drag_steering_and_multitouch) {
    TouchConfig cfg;
    TouchControls t = driving(cfg);
    const Widget gas = *find_widget(t, Widget::Kind::Pedal, PadButton::Cross);
    const float range = cfg.steer_range * 640.0f;
    CHECK(t.finger_down(1, 120, 260, 1.0f)); // steering zone (left half)
    CHECK(t.finger_down(2, gas.x + 5, gas.y + 5, 1.0f));
    t.update(0.016, 0);
    CHECK(t.pad().axis(PadAxis::LX) == 128); // origin = centre
    t.finger_move(1, 120 + range * 0.5f, 250, 1.0f);
    t.update(0.016, 0);
    CHECK(std::fabs(t.pad().axis_f(PadAxis::LX) - 0.5f) < 0.02f);
    CHECK(t.pad().down(PadButton::Cross)); // other finger still on the gas
    t.finger_move(1, 120 - range * 3, 250, 1.0f); // past full lock
    t.update(0.016, 0);
    CHECK(t.pad().axis(PadAxis::LX) == 0);
    // The steering finger never turns into a button press.
    t.finger_move(1, gas.x + 5, gas.y + 5, 1.0f);
    t.update(0.016, 0);
    CHECK(t.pad().axis(PadAxis::LX) == 255);
    t.finger_up(1);
    t.update(0.016, 0);
    CHECK(t.pad().axis(PadAxis::LX) == 128);
}

TEST(touch_tilt_steering) {
    TouchConfig cfg;
    cfg.steering = TouchConfig::Steering::Tilt;
    cfg.mode = TouchConfig::Mode::On;
    TouchControls t = driving(cfg);
    t.update(0.016, 0.02f); // inside the deadzone
    CHECK(t.pad().axis(PadAxis::LX) == 128);
    t.update(0.016, -1.0f);
    CHECK(t.pad().axis(PadAxis::LX) == 0);
    t.update(0.016, 0.52f);
    CHECK(std::fabs(t.pad().axis_f(PadAxis::LX) - 0.5f) < 0.02f);
    // A finger in the (unused) steering zone does nothing in tilt mode.
    t.finger_down(1, 100, 300, 1.0f);
    t.update(0.016, 0.0f);
    CHECK(t.pad().axis(PadAxis::LX) == 128);
}

// Button steering: Left / Right press the race pad's d-pad at full pressure, which GT3's pad read
// turns into its digital steering (kind 3, full lock).
TEST(touch_button_steering) {
    TouchConfig cfg;
    cfg.steering = TouchConfig::Steering::Buttons;
    TouchControls t = driving(cfg);
    CHECK(find_widget(t, Widget::Kind::Steering) == nullptr); // no drag zone
    const Widget left = *find_widget(t, Widget::Kind::Button, PadButton::Left);
    const Widget right = *find_widget(t, Widget::Kind::Button, PadButton::Right);
    CHECK(left.x + left.w <= right.x && right.x + right.w < 320 && left.y + left.h > 300); // side by side, bottom left
    CHECK(!t.finger_down(9, 160, 120, 1.0f)); // the rest of the left half isn't a control
    CHECK(t.finger_down(1, left.x + left.w / 2, left.y + left.h / 2, 1.0f));
    t.update(0.016, 0.5f); // tilt is ignored
    CHECK(t.pad().down(PadButton::Left) && t.pad().press(PadButton::Left) == 255);
    CHECK(!t.pad().down(PadButton::Right) && t.pad().axis(PadAxis::LX) == 128);

    InputManager input;
    input.set_overlay(0, t.pad());
    input.update(0.016);
    const PadState& race = input.race_pad(0);
    CHECK(race.down(PadButton::Left) && race.press(PadButton::Left) == 255 && race.axis(PadAxis::LX) == 128);
    namespace ex = gt3r::sim::exact;
    ex::PadReading r;
    r.channel[5] = race.press(PadButton::Left), r.channel[4] = race.press(PadButton::Right);
    ex::PadFrame f;
    ex::pad_dpad_pressure(f, r);
    CHECK(f.kind == 3 && f.steer == 4096 && (f.flags & 1)); // full lock to the left

    // The thumb rocks over to Right, then lets go.
    t.finger_move(1, right.x + right.w / 2, right.y + right.h / 2, 1.0f);
    t.update(0.016, 0);
    CHECK(t.pad().down(PadButton::Right) && !t.pad().down(PadButton::Left));
    t.finger_up(1);
    t.update(0.016, 0);
    CHECK(t.pad().buttons == 0);

    // Left-handed: on the right, Left still on the left; one placement moves both.
    cfg.left_handed = true;
    TouchControls m = driving(cfg);
    const Widget ml = *find_widget(m, Widget::Kind::Button, PadButton::Left);
    const Widget mr = *find_widget(m, Widget::Kind::Button, PadButton::Right);
    CHECK(ml.x > 320 && ml.x + ml.w <= mr.x && mr.x == 640 - left.x - left.w); // mirrored, the pair swapped
    cfg.left_handed = false;
    cfg.set_placement("drive.steer", TouchPlacement::parse("center 0 bottom 60 size 1.5"));
    TouchControls p = driving(cfg);
    const TouchRect b = p.group_bounds("drive.steer");
    CHECK(std::fabs(b.x + b.w / 2 - 320) < 0.5f && std::fabs(b.w - (left.w * 2 + 10) * 1.5f) < 0.5f);

    IniDocument ini;
    cfg.save(ini);
    CHECK(ini.get("touch", "steering") == std::optional<std::string>("buttons"));
    CHECK(TouchConfig::load(ini).steering == TouchConfig::Steering::Buttons);
    CHECK(TouchConfig::load(ini).placement("drive.steer").scale == 1.5f);
}

TEST(touch_layout_switch_releases) {
    TouchControls t = driving();
    const Widget* start = find_widget(t, Widget::Kind::Button, PadButton::Start);
    CHECK(start != nullptr);
    t.finger_down(3, start->x, start->y, 1.0f);
    t.update(0.016, 0);
    CHECK(t.pad().down(PadButton::Start));
    t.set_layout(TouchLayout::Menu); // pause menu opened
    t.update(0.016, 0);
    CHECK(!t.pad().down(PadButton::Start));
    t.finger_up(3); // the late "up" is harmless
}

TEST(touch_menu_dpad_and_face_buttons) {
    TouchControls t;
    t.set_layout(TouchLayout::Menu);
    const Widget pad = *find_widget(t, Widget::Kind::DPad);
    const float cx = pad.x + pad.w / 2, cy = pad.y + pad.h / 2;
    CHECK(t.finger_down(1, cx, cy - pad.h * 0.4f, 1.0f));
    t.update(0.016, 0);
    CHECK(t.pad().down(PadButton::Up));
    t.finger_move(1, cx + pad.w * 0.4f, cy + 2, 1.0f);
    t.update(0.016, 0);
    CHECK(t.pad().down(PadButton::Right));
    CHECK(!t.pad().down(PadButton::Up));
    t.finger_move(1, cx, cy, 1.0f); // centre: dead zone
    t.update(0.016, 0);
    CHECK(t.pad().buttons == 0);
    t.finger_up(1);

    const Widget cross = *find_widget(t, Widget::Kind::Button, PadButton::Cross);
    CHECK(t.hit(cross.x, cross.y));
    CHECK(t.finger_down(2, cross.x, cross.y, 1.0f));
    t.update(0.016, 0);
    CHECK(t.pad().down(PadButton::Cross));
    CHECK(t.pad().press(PadButton::Cross) == 255);
    // Taps away from the controls go to the screen underneath.
    CHECK(!t.hit(320, 180));
    CHECK(!t.finger_down(3, 320, 180, 1.0f));
}

TEST(touch_left_handed_mirrors) {
    TouchConfig cfg;
    cfg.left_handed = true;
    TouchControls t = driving(cfg);
    const Widget* gas = find_widget(t, Widget::Kind::Pedal, PadButton::Cross);
    const Widget* steer = find_widget(t, Widget::Kind::Steering);
    CHECK(gas->x < 200);
    CHECK(steer->x >= 319);
}

TEST(touch_config_roundtrip) {
    IniDocument ini = IniDocument::parse(
        "[touch]\nmode = on\nsteering = tilt\npedals = area\npedal_min = 0.2\nsteer_range = 9\nopacity = 0.8\n"
        "size = 1.2\nleft_handed = on\n");
    TouchConfig c = TouchConfig::load(ini);
    CHECK(c.mode == TouchConfig::Mode::On);
    CHECK(c.steering == TouchConfig::Steering::Tilt);
    CHECK(c.pedals == TouchConfig::Pedals::Area);
    CHECK(std::fabs(c.pedal_min - 0.2f) < 1e-6f);
    CHECK(c.steer_range == 0.5f); // clamped
    CHECK(c.left_handed);
    IniDocument out;
    c.save(out);
    TouchConfig d = TouchConfig::load(out);
    CHECK(d.mode == c.mode && d.steering == c.steering && d.pedals == c.pedals && d.left_handed);
    CHECK(std::fabs(d.size - 1.2f) < 1e-3f && std::fabs(d.opacity - 0.8f) < 1e-3f);
    TouchConfig def = TouchConfig::load(IniDocument{});
    CHECK(def.mode == TouchConfig::Mode::Auto && def.steering == TouchConfig::Steering::Drag);
}

TEST(touch_overlay_merges_into_player_pad) {
    InputManager input;
    PadState ov;
    ov.connected = true;
    ov.buttons = u16(1u << unsigned(PadButton::Cross));
    ov.pressure[size_t(PadButton::Cross)] = 100;
    ov.axes[size_t(PadAxis::LX)] = 0;
    input.set_overlay(0, ov);
    input.update(0.016);
    const PadState& p = input.pad(0);
    CHECK(p.down(PadButton::Cross));
    CHECK(p.press(PadButton::Cross) == 100);
    CHECK(p.axis(PadAxis::LX) == 0);
    CHECK(!input.pad(1).down(PadButton::Cross));
    input.set_overlay(0, PadState{}); // disconnected overlay: ignored
    input.update(0.016);
    CHECK(!input.pad(0).down(PadButton::Cross));
    CHECK(input.pad(0).axis(PadAxis::LX) == 128);
}

// Race controls per player: the layout and custom race bindings survive a save / load, and
// the overlay (GT3 meaning) reaches the race pad too.
TEST(input_race_layout_per_player) {
    IniDocument cfg = IniDocument::parse("[race]\nbuttons = ours\n[player2]\npreset = classic\n");
    InputManager input;
    input.load(cfg);
    CHECK(input.race_layout(0) == "modern" && input.race_layout(1) == "modern"); // [race] buttons = ours
    input.set_race_layout(1, "gt3");
    CHECK(input.race_uses_menu_pad(1) && !input.race_uses_menu_pad(0));
    CHECK(&input.race_profile(1) == &input.profile(1));
    PadTarget view;
    view.kind = PadTarget::Kind::Button;
    view.button = PadButton::R1;
    InputSource c_key, v_key;
    CHECK(InputSource::parse("key.C", c_key) && InputSource::parse("key.V", v_key));
    input.bind_race(0, view, v_key, true); // player 1 only (no device here: the keyboard's set)
    CHECK(input.is_race_custom(0) && !input.is_race_custom(1));
    IniDocument out;
    input.save(out);
    CHECK(out.get("player1", "race") == "modern" && out.get("player2", "race") == "gt3");
    CHECK(out.get("player1.keyboard.race", "R1") == "key.V");
    CHECK(!out.find("player2.keyboard.race") && !out.find("player1.race"));
    InputManager again;
    again.load(out);
    CHECK(again.is_race_custom(0) && again.race_layout(1) == "gt3" && again.race_uses_menu_pad(1));
    bool c_view = false;
    for (const Binding& b : again.race_profile(0).bindings) c_view |= b.source == c_key && b.target == view;
    CHECK(!c_view); // replaced
    // Old configs: [race] buttons = auto picks GT3's layout for the classic preset.
    InputManager old;
    old.load(IniDocument::parse("[player1]\npreset = classic\n"));
    CHECK(old.race_layout(0) == "gt3" && old.race_layout(1) == "modern");

    PadState ov;
    ov.connected = true;
    ov.buttons = u16(1u << unsigned(PadButton::Cross));
    ov.pressure[size_t(PadButton::Cross)] = 90;
    input.set_overlay(0, ov);
    input.update(0.016);
    CHECK(input.race_pad(0).press(PadButton::Cross) == 90);
}

// Keyboard only: no controller drives the player and the keyboard does, whatever the Keyboard
// toggle says (Settings hides it). Auto with nothing connected drives with the keyboard's bindings
// too, and keeps player 1's keyboard live even with the toggle off.
TEST(input_keyboard_only) {
    CHECK(make_preset("keyboard").keyboard_only());
    CHECK(!make_preset("pad").keyboard_only() && !make_preset("wheel").keyboard_only());
    InputManager input;
    input.load(IniDocument::parse("[player1]\ncontroller = keyboard\nkeyboard = off\n[player2]\ncontroller = keyboard\n"));
    CHECK(input.keyboard_only(0) && input.kind(0) == DeviceKind::Keyboard && input.device_name(0) == "Keyboard");
    CHECK(!input.keyboard_enabled(0) && input.keyboard_active(0));
    CHECK(input.keyboard_only(1) && input.keyboard_active(1)); // player 2 too, when chosen
    auto press = [&](int player, SDL_Scancode key) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = key;
        input.handle_event(e);
        input.update(0.016);
        return input.pad(player).down(PadButton::Cross);
    };
    auto press_return = [&](int player) { return press(player, SDL_SCANCODE_RETURN); };
    CHECK(press_return(0) && !press_return(1));  // Return = Cross for player 1
    CHECK(press(1, SDL_SCANCODE_O) && !press(0, SDL_SCANCODE_O)); // O = Cross for player 2
    IniDocument out;
    input.save(out);
    CHECK(out.get("player1", "controller") == "keyboard" && out.get("player1", "keyboard") == "off");
    CHECK(out.get("player1", "preset") == "keyboard" && out.get("player1", "device") == "auto"); // older builds
    // Auto with nothing connected: the keyboard's bindings; player 1 keeps the keyboard, player 2
    // (keyboard off) has nothing.
    input.set_device_filter(0, "auto");
    input.set_device_filter(1, "auto");
    CHECK(!input.keyboard_only(0) && input.kind(0) == DeviceKind::Keyboard && input.keyboard_fallback(0));
    CHECK(!input.keyboard_active(1));
    CHECK(press_return(0) && !press_return(1));
}

// Auto takes a device SDL identifies as a wheel, otherwise the first free gamepad, otherwise any
// other joystick: an odd HID device SDL lists as a joystick (flight stick, macro pad) never takes
// a player's gamepad. The kind of device then picks the buttons. With none connected nothing
// is pressed: the wheel's pedals are never read without a wheel (issue 23: they read as half-held
// Cross and Square).
TEST(input_auto_prefers_a_wheel) {
    // Free devices in connection order.
    using R = AutoRank;
    CHECK(pick_auto_device({R::Gamepad}) == 0);
    CHECK(pick_auto_device({R::Gamepad, R::Wheel}) == 1); // a wheel connects: it takes over
    CHECK(pick_auto_device({R::Wheel, R::Gamepad}) == 0);
    CHECK(pick_auto_device({R::Other, R::Gamepad}) == 1); // an unknown joystick never beats a gamepad
    CHECK(pick_auto_device({R::Gamepad, R::Other}) == 0);
    CHECK(pick_auto_device({R::Other, R::Wheel}) == 1);
    CHECK(pick_auto_device({R::Gamepad, R::Gamepad}) == 0);
    CHECK(pick_auto_device({R::Other}) == 0); // nothing better: still a controller
    CHECK(pick_auto_device({}) == -1);
    CHECK(device_kind_preset(DeviceKind::Pad) == pad_preset() && device_kind_preset(DeviceKind::Wheel) == "wheel" &&
          device_kind_preset(DeviceKind::Keyboard) == "keyboard");
    // The kind: only a wheel SDL knows (or the 0.3 wheel the player named) gets the wheel layout;
    // any other joystick (an arcade stick, its axes resting at 0) has no default bindings, so its
    // axes never read as the wheel's pedals half pressed.
    CHECK(device_kind(false, false, false, false) == DeviceKind::Keyboard);
    CHECK(device_kind(true, true, false, false) == DeviceKind::Pad);
    CHECK(device_kind(true, true, true, false) == DeviceKind::Pad); // a wheel SDL maps as a gamepad
    CHECK(device_kind(true, false, true, false) == DeviceKind::Wheel);
    CHECK(device_kind(true, false, false, true) == DeviceKind::Wheel);
    CHECK(device_kind(true, false, false, false) == DeviceKind::Joystick);
    PlayerBindings b;
    for (bool race : {false, true}) {
        CHECK(b.set(DeviceKind::Joystick, race, BindingSet::Controller).bindings.empty());
        for (const Binding& x : b.combined(DeviceKind::Joystick, race).bindings)
            CHECK(binding_set(x.source) == BindingSet::Keyboard);
    }
    // What the player binds on it is kept, in its own sections.
    PadTarget cross;
    InputSource button0;
    CHECK(PadTarget::parse("Cross", cross) && InputSource::parse("joy.button0", button0));
    CHECK(b.bind(DeviceKind::Joystick, false, cross, button0, true));
    IniDocument out;
    b.save(out, "player1");
    CHECK(out.get("player1.joystick.bindings", "Cross") == "joy.button0" && !out.find("player1.wheel.bindings"));
    PlayerBindings again;
    again.load(out, "player1");
    CHECK(again.set(DeviceKind::Joystick, false, BindingSet::Controller).bindings.size() == 1);
    InputManager input;
    input.load(IniDocument::parse("[player1]\npreset = wheel\n"));
    input.update(0.016);
    CHECK(input.device_filter(0) == "auto" && input.kind(0) == DeviceKind::Keyboard);
    CHECK(input.device_name(0) == "Keyboard");
    for (const PadState* s : {&input.pad(0), &input.race_pad(0)}) {
        CHECK(s->buttons == 0);
        CHECK(s->press(PadButton::Cross) == 0 && s->press(PadButton::Square) == 0);
    }
}

// Configs from before the Controller choice: `preset` chose the buttons. racing and classic are
// the pad, wheel is Auto (which prefers a wheel), keyboard is Keyboard only (unless its bindings
// had controller inputs: its device stays), and a named device stays. Their one custom profile per
// player splits by input type: pad inputs to the pad's set, raw joystick inputs to the wheel's,
// keys, mouse buttons and tilt to the keyboard's; a part that is just the layout's isn't kept.
TEST(input_old_preset_migration) {
    struct Case {
        const char* preset;
        const char* controller;
        const char* base; // where the pad inputs go
    };
    for (const Case& c : {Case{"racing", "auto", "player1"}, Case{"classic", "auto", "player1"},
                          Case{"wheel", "auto", "player1"}, Case{"keyboard", "auto", "player1"}}) {
        InputManager input;
        input.load(IniDocument::parse(std::string("[player1]\npreset = ") + c.preset +
                                      "\n[player1.bindings]\nCircle = pad.north | key.K\n"
                                      "[player1.race]\nR1 = pad.west | key.V\n"));
        CHECK(input.device_filter(0) == c.controller);
        CHECK(input.is_custom(0) && input.is_race_custom(0)); // no device here: the keyboard's set
        IniDocument out;
        input.save(out);
        CHECK(out.get("player1", "controller") == c.controller);
        CHECK(out.get(std::string(c.base) + ".bindings", "Circle") == "pad.north");
        CHECK(out.get(std::string(c.base) + ".race", "R1") == "pad.west");
        CHECK(out.get("player1.keyboard.bindings", "Circle") == "key.K");
        CHECK(out.get("player1.keyboard.race", "R1") == "key.V");
        InputManager again; // read back as a new config: the same
        again.load(out);
        IniDocument out2;
        again.save(out2);
        CHECK(out2.serialize() == out.serialize());
    }
    // keyboard with keys only: Keyboard only.
    InputManager keys_only;
    keys_only.load(IniDocument::parse("[player1]\npreset = keyboard\n[player1.bindings]\nCross = key.K\n"));
    CHECK(keys_only.keyboard_only(0));
    // A wheel's input under another preset goes to the wheel's set (a G29 named with racing).
    InputManager g29;
    g29.load(IniDocument::parse("[player1]\npreset = racing\ndevice = G29\n[player1.bindings]\n"
                                "Cross = joy.axis2 pedal invert | pad.east\n"));
    IniDocument g29_out;
    g29.save(g29_out);
    CHECK(g29.device_filter(0) == "G29" && !g29_out.get("player1", "wheel_device"));
    const std::string wheel_cross = g29_out.get("player1.wheel.bindings", "Cross").value_or("");
    CHECK(wheel_cross.rfind("joy.axis2", 0) == 0 && wheel_cross.find("pad.") == std::string::npos);
    CHECK(g29_out.get("player1.bindings", "Cross") == "pad.east");
    // wheel with a named device: that device keeps the wheel layout whatever SDL makes of it.
    InputManager named_wheel;
    named_wheel.load(IniDocument::parse("[player1]\npreset = wheel\ndevice = Thrustmaster\n"));
    IniDocument wheel_out;
    named_wheel.save(wheel_out);
    CHECK(wheel_out.get("player1", "controller") == "Thrustmaster");
    CHECK(wheel_out.get("player1", "wheel_device") == "Thrustmaster");
    InputManager wheel_again;
    wheel_again.load(wheel_out);
    IniDocument wheel_out2;
    wheel_again.save(wheel_out2);
    CHECK(wheel_out2.get("player1", "wheel_device") == "Thrustmaster");
    // Only controller inputs: no keyboard set of its own (the layout's stays).
    InputManager pad_only;
    pad_only.load(IniDocument::parse("[player1]\npreset = racing\n[player1.bindings]\nCross = pad.east\n"));
    IniDocument out;
    pad_only.save(out);
    CHECK(out.get("player1.bindings", "Cross") == "pad.east" && !out.find("player1.keyboard.bindings"));
    CHECK(!pad_only.is_custom(0));
    // A named device stays; classic's race layout was GT3's ([race] buttons = auto).
    InputManager named;
    named.load(IniDocument::parse("[player1]\npreset = classic\ndevice = DualSense\n"));
    CHECK(named.device_filter(0) == "DualSense" && named.race_layout(0) == "gt3");
    // A new config's controller wins over the preset it keeps for older builds.
    InputManager both;
    both.load(IniDocument::parse("[player1]\ncontroller = auto\npreset = keyboard\n"));
    CHECK(!both.keyboard_only(0));
}

// A player's keyboard and controller are separate sets of bindings: capturing a key replaces only
// the action's keyboard input, a controller input only its controller input, and clearing works
// per set. The keyboard's set is one per player, the same whatever controller is in use (Keyboard
// only too); the controller's is per kind (pad, wheel).
TEST(input_binding_sets) {
    auto src = [](const char* s) {
        InputSource out;
        CHECK((InputSource::parse(s, out)));
        return out;
    };
    auto target = [](const char* s) {
        PadTarget out;
        CHECK((PadTarget::parse(s, out)));
        return out;
    };
    auto sources = [](const Profile& p, const PadTarget& t) {
        std::vector<InputSource> out;
        for (const Binding& b : p.bindings)
            if (b.target == t) out.push_back(b.source);
        return out;
    };
    using V = std::vector<InputSource>;
    constexpr DeviceKind Pad = DeviceKind::Pad, Wheel = DeviceKind::Wheel, Alone = DeviceKind::Keyboard;
    constexpr BindingSet Ctl = BindingSet::Controller, Keys = BindingSet::Keyboard;
    const PadTarget accel = target("Cross"), cross = target("Cross"), circle = target("Circle");
    PlayerBindings b;
    CHECK((sources(b.set(Pad, true, Ctl), accel) == V{src("pad.righttrigger")}));
    CHECK((sources(b.set(Pad, true, Keys), accel) == V{src("key.W"), src("key.Up")}));
    b.bind(Pad, true, accel, src("pad.east"), true); // a controller input: the controller's only
    CHECK((sources(b.set(Pad, true, Ctl), accel) == V{src("pad.east")}));
    CHECK((sources(b.set(Pad, true, Keys), accel) == V{src("key.W"), src("key.Up")}));
    b.bind(Pad, true, accel, src("key.K"), true); // a key: the keyboard's only
    CHECK((sources(b.set(Pad, true, Ctl), accel) == V{src("pad.east")}));
    CHECK((sources(b.set(Pad, true, Keys), accel) == V{src("key.K")}));
    b.clear(Pad, true, accel, Keys);
    CHECK((sources(b.set(Pad, true, Keys), accel).empty() && sources(b.set(Pad, true, Ctl), accel) == V{src("pad.east")}));
    // The wheel keeps its own controller set, and shares the keyboard's.
    CHECK((sources(b.set(Wheel, true, Ctl), accel) == V{src("joy.axis1")}));
    CHECK((sources(b.set(Wheel, true, Keys), accel).empty() && b.custom(Wheel, true)));
    // Menus: a key bound with the pad in use drives the player with a wheel or the keyboard alone.
    b.bind(Pad, false, cross, src("key.K"), true);
    CHECK((sources(b.set(Wheel, false, Keys), cross) == V{src("key.K")}));
    CHECK((sources(b.set(Alone, false, Keys), cross) == V{src("key.K")}));
    CHECK((sources(b.combined(Pad, false), cross) == (V{src("pad.south"), src("key.K")})));
    CHECK((b.set(Alone, false, Ctl).bindings.empty())); // no controller with the keyboard alone
    // A controller input with no controller in use goes to the pad's set.
    b.bind(Alone, false, circle, src("pad.north"), true);
    CHECK((sources(b.set(Pad, false, Ctl), circle) == V{src("pad.north")}));
    // Saved and read back; each section holds its own set's inputs.
    IniDocument out;
    b.save(out, "player1");
    CHECK((out.get("player1.keyboard.bindings", "Cross") == "key.K" && out.get("player1.race", "Cross") == "pad.east"));
    PlayerBindings again;
    again.load(out, "player1");
    for (DeviceKind k : {Pad, Wheel, Alone})
        for (bool race : {false, true})
            for (BindingSet s : {Ctl, Keys})
                CHECK((sources(again.set(k, race, s), cross) == sources(b.set(k, race, s), cross)));
    // Choosing the layout again resets both sets.
    b.reset(Pad, true);
    CHECK((!b.custom(Pad, true) && sources(b.set(Pad, true, Keys), accel) == V{src("key.W"), src("key.Up")}));
}

// The back paddles shift by default (right up, left down) in both race layouts: the modern one
// binds them, and under GT3 original the race reads the menu pad, whose paddles press R2 / L2.
// Each is its own control: bound to another action it stops shifting. They read as "Right paddle".
TEST(input_paddles) {
    InputSource right, left, right2, left2, south;
    CHECK(InputSource::parse("pad.paddle1", right) && InputSource::parse("pad.paddle2", left));
    CHECK(InputSource::parse("pad.paddle3", right2) && InputSource::parse("pad.paddle4", left2));
    CHECK(InputSource::parse("pad.south", south));
    CHECK(right.paddle_name() == "Right paddle" && left.paddle_name() == "Left paddle");
    CHECK(right2.paddle_name() == "Right paddle 2" && left2.paddle_name() == "Left paddle 2");
    CHECK(south.paddle_name().empty());
    InputManager input;
    CHECK(input.describe(0, right) == "Right paddle" && input.describe(0, left2) == "Left paddle 2");

    PadTarget reverse, up, down;
    CHECK(PadTarget::parse("Triangle", reverse) && PadTarget::parse("R2", up) && PadTarget::parse("L2", down));
    auto targets = [](const Profile& p, const InputSource& s) {
        std::vector<PadTarget> out;
        for (const Binding& b : p.bindings)
            if (b.source == s) out.push_back(b.target);
        return out;
    };
    using T = std::vector<PadTarget>;
    for (const Profile& layout : {make_race_preset("modern", "pad"), make_preset("pad")}) {
        CHECK(targets(layout, right) == T{up} && targets(layout, left) == T{down});
        Profile p = layout;
        p.bind(reverse, right, true); // Reverse on the right paddle: it no longer shifts up
        CHECK(targets(p, right) == T{reverse} && targets(p, left) == T{down});
        CHECK(std::any_of(p.bindings.begin(), p.bindings.end(),
                          [&](const Binding& b) { return b.target == up; })); // shift up keeps its button
        p.bind(reverse, left, false); // added: the left paddle too
        CHECK(targets(p, left) == T{reverse} && targets(p, right) == T{reverse});
    }
    // A player's race controls under GT3 original start from the menu buttons, paddles included.
    PlayerBindings b;
    b.race_layout = "gt3";
    CHECK(b.race_reads_menu(DeviceKind::Pad));
    b.bind(DeviceKind::Pad, true, reverse, right2, true);
    CHECK(!b.race_reads_menu(DeviceKind::Pad));
    const Profile& race = b.set(DeviceKind::Pad, true, BindingSet::Controller);
    CHECK(targets(race, right2) == T{reverse} && targets(race, right) == T{up});
}

// Player 1 must always have a way through the menus: when nothing they have can confirm (Cross)
// and move (Up / Down or the left stick's Y), the menus outside races read every device
// (InputManager::menu_pad).
TEST(input_menus_drivable) {
    const Profile pad = make_preset("pad"), keys = make_preset("keyboard"), wheel = make_preset("wheel");
    CHECK(menus_drivable(pad, true, false, false));   // the gamepad
    CHECK(menus_drivable(pad, false, false, true));   // its layout's keys
    CHECK(!menus_drivable(pad, false, false, false)); // nothing connected, no keyboard
    CHECK(menus_drivable(keys, false, false, true));
    CHECK(!menus_drivable(keys, true, true, false));  // Keyboard only without a keyboard
    CHECK(menus_drivable(wheel, false, true, false)); // a wheel's buttons and hat
    CHECK(!menus_drivable(Profile{}, true, true, true)); // a joystick nothing is bound to yet
    Profile no_cross = pad;
    PadTarget cross;
    CHECK(PadTarget::parse("Cross", cross));
    no_cross.remove_target(cross);
    CHECK(!menus_drivable(no_cross, true, false, true));
}

// A capture is cancelled by Esc and Android Back (they are never bound), so a key meant for the
// game's back button can't take Cross's place by accident.
TEST(input_capture_cancel) {
    InputManager input;
    auto key = [&](SDL_Scancode k) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = k;
        input.handle_event(e);
    };
    for (SDL_Scancode cancel : {SDL_SCANCODE_ESCAPE, SDL_SCANCODE_AC_BACK}) {
        input.begin_capture();
        CHECK(input.capturing());
        key(cancel);
        CHECK(!input.capturing() && !input.poll_capture());
    }
    input.begin_capture();
    key(SDL_SCANCODE_K);
    InputSource k;
    CHECK(InputSource::parse("key.K", k));
    CHECK(!input.capturing() && input.poll_capture() == std::optional<InputSource>(k));
}

// A button or key on a whole stick axis (Steer, the menu sticks) could push it only one way: it is
// refused, and the axis keeps its inputs (the keyboard's A / D halves included). Tilt sits in the
// keyboard's set, so it is read whatever the controller, a phone with no pad included.
TEST(input_full_axis_and_tilt) {
    auto src = [](const char* s) {
        InputSource out;
        CHECK(InputSource::parse(s, out));
        return out;
    };
    PadTarget steer;
    CHECK(PadTarget::parse("LX", steer));
    PlayerBindings b;
    const std::string before = b.combined(DeviceKind::Pad, true).name;
    CHECK(!b.bind(DeviceKind::Pad, true, steer, src("key.K"), true));
    CHECK(!b.bind(DeviceKind::Pad, true, steer, src("pad.south"), true));
    CHECK(!b.custom(DeviceKind::Pad, true) && b.combined(DeviceKind::Pad, true).name == before);
    CHECK(b.bind(DeviceKind::Pad, true, steer, src("pad.rightx"), true)); // an analog input is fine
    // Tilt.
    CHECK(binding_set(src("tilt")) == BindingSet::Keyboard);
    InputManager phone;
    phone.load(IniDocument::parse("[player1]\npreset = racing\n[player1.bindings]\nLX = pad.leftx | tilt curve=1.5\n"));
    CHECK(phone.kind(0) == DeviceKind::Keyboard); // no pad
    bool tilt = false;
    for (const Binding& x : phone.profile(0).bindings) tilt |= x.source.kind == InputSource::Kind::Tilt;
    CHECK(tilt);
}

// A config with no custom sections shows no Custom row, whatever the kind in use (a playtest config
// with a DualSense showed Race controls = Custom).
TEST(input_no_false_custom) {
    // An old version's config set to Modern with no custom sections, and the same after a save.
    for (const char* controller : {"", "controller = auto\n"}) {
    const IniDocument cfg = IniDocument::parse(
        std::string("[player1]\npreset = racing\ndevice = auto\nkeyboard = on\nrace = modern\nphone_vibration = off\n") +
        controller + "[player2]\npreset = racing\ndevice = auto\nkeyboard = off\nrace = modern\n" + controller);
    InputManager input;
    input.load(cfg);
    for (int p = 0; p < InputManager::kMaxPlayers; ++p) {
        CHECK(!input.is_custom(p) && !input.is_race_custom(p) && input.race_layout(p) == "modern");
        IniDocument out;
        input.save(out);
        for (const char* s : {".bindings", ".race", ".wheel.bindings", ".keyboard.race"})
            CHECK(!out.find("player" + std::to_string(p + 1) + s));
    }
    for (const char* sec : {"player1", "player2"}) {
        PlayerBindings b;
        if (*controller) b.load(cfg, sec);
        else b.load_old(cfg, sec);
        for (DeviceKind k : {DeviceKind::Pad, DeviceKind::Wheel, DeviceKind::Joystick, DeviceKind::Keyboard})
            CHECK(!b.has_custom(k, false) && !b.has_custom(k, true) && !b.custom(k, true));
    }
    }
}

// Choosing a layout keeps the custom set: it stays saved and comes back with Custom, for the race
// controls and the menu buttons alike.
TEST(input_custom_kept) {
    PadTarget view, circle;
    InputSource rb, k;
    CHECK(PadTarget::parse("R1", view) && PadTarget::parse("Circle", circle));
    CHECK(InputSource::parse("pad.rightshoulder", rb) && InputSource::parse("key.K", k));
    auto has = [](const Profile& p, const PadTarget& t, const InputSource& src) {
        return std::any_of(p.bindings.begin(), p.bindings.end(), [&](const Binding& b) { return b.target == t && b.source == src; });
    };
    PlayerBindings b;
    CHECK(b.bind(DeviceKind::Pad, true, view, rb, true) && b.bind(DeviceKind::Pad, false, circle, k, true));
    CHECK(b.custom(DeviceKind::Pad, true) && b.custom(DeviceKind::Pad, false));
    b.set_race_layout(DeviceKind::Pad, "modern"); // Modern chosen
    b.reset(DeviceKind::Pad, false);              // the menu layout chosen
    CHECK(!b.custom(DeviceKind::Pad, true) && b.has_custom(DeviceKind::Pad, true));
    CHECK(!b.custom(DeviceKind::Pad, false) && b.has_custom(DeviceKind::Pad, false));
    CHECK(!has(b.combined(DeviceKind::Pad, true), view, rb) && !has(b.combined(DeviceKind::Pad, false), circle, k));
    IniDocument out;
    b.save(out, "player1");
    CHECK(out.get("player1.race", "R1") == "pad.rightshoulder" && out.get("player1", "race_custom") == "off");
    CHECK(out.find("player1.keyboard.bindings") && out.get("player1", "menu_custom") == "off");
    PlayerBindings again;
    again.load(out, "player1");
    CHECK(again.has_custom(DeviceKind::Pad, true) && !again.custom(DeviceKind::Pad, true));
    again.use_custom(true); // Custom chosen again
    again.use_custom(false);
    CHECK(has(again.combined(DeviceKind::Pad, true), view, rb) && has(again.combined(DeviceKind::Pad, false), circle, k));
    // Through InputManager (no device here: the keyboard's set).
    InputManager input;
    CHECK(input.bind_race(0, view, k, true) && input.is_race_custom(0));
    input.set_race_layout(0, "gt3");
    CHECK(!input.is_race_custom(0) && input.has_custom(0, true) && input.race_uses_menu_pad(0));
    input.use_custom(0, true);
    CHECK(input.is_race_custom(0) && !input.race_uses_menu_pad(0) && has(input.race_profile(0), view, k));
}

// GT3 original is a starting point: editing a race control under it makes custom race controls
// from its mapping (the menu buttons' inputs, both sets) plus the edit, replacing a kept custom set
// (Settings asks first).
TEST(input_gt3_start_custom) {
    PadTarget view, accel;
    InputSource rb, south, ret, east;
    CHECK(PadTarget::parse("R1", view) && PadTarget::parse("Cross", accel));
    CHECK(InputSource::parse("pad.rightshoulder", rb) && InputSource::parse("pad.south", south));
    CHECK(InputSource::parse("key.Return", ret) && InputSource::parse("pad.east", east));
    auto has = [](const Profile& p, const PadTarget& t, const InputSource& src) {
        return std::any_of(p.bindings.begin(), p.bindings.end(), [&](const Binding& b) { return b.target == t && b.source == src; });
    };
    PlayerBindings b;
    CHECK(b.bind(DeviceKind::Pad, true, accel, east, true)); // a custom set made under Modern
    b.set_race_layout(DeviceKind::Pad, "gt3");
    CHECK(b.race_reads_menu(DeviceKind::Pad) && b.has_custom(DeviceKind::Pad, true));
    CHECK(b.bind(DeviceKind::Pad, true, view, rb, true)); // edited under GT3 original
    CHECK(b.custom(DeviceKind::Pad, true) && !b.race_reads_menu(DeviceKind::Pad));
    const Profile race = b.combined(DeviceKind::Pad, true);
    CHECK(has(race, view, rb));                                // the edit
    CHECK(has(race, accel, south) && has(race, accel, ret));   // GT3's Cross, from the menu buttons
    CHECK(!has(race, accel, east));                            // the old custom set replaced
}

// Player 2's default keys sit around I / J / K / L, apart from player 1's: no key is in both
// players' defaults (race and menu buttons), and player 1's right stick is off that cluster.
TEST(input_player_keys_apart) {
    auto keys = [](const Profile& p) {
        std::vector<int> out;
        for (const Binding& b : p.bindings)
            if (b.source.kind == InputSource::Kind::Key) out.push_back(b.source.code);
        return out;
    };
    PlayerBindings p1, p2;
    p2.player2 = true;
    std::vector<int> one, two;
    for (bool race : {false, true}) {
        for (int c : keys(p1.set(DeviceKind::Keyboard, race, BindingSet::Keyboard))) one.push_back(c);
        for (int c : keys(p2.set(DeviceKind::Keyboard, race, BindingSet::Keyboard))) two.push_back(c);
    }
    for (int c : two) CHECK(std::find(one.begin(), one.end(), c) == one.end());
    // Every one of player 2's keys parsed: 14 menu buttons, 11 race controls.
    CHECK(keys(p2.set(DeviceKind::Keyboard, false, BindingSet::Keyboard)).size() == 14);
    CHECK(keys(p2.set(DeviceKind::Keyboard, true, BindingSet::Keyboard)).size() == 11);
    // Race: I accelerates, K brakes, J / L steer.
    InputSource i, k, j;
    CHECK(InputSource::parse("key.I", i) && InputSource::parse("key.K", k) && InputSource::parse("key.J", j));
    PadTarget accel, brake, left;
    CHECK(PadTarget::parse("Cross", accel) && PadTarget::parse("Square", brake) && PadTarget::parse("LX-", left));
    const Profile& race = p2.set(DeviceKind::Keyboard, true, BindingSet::Keyboard);
    auto on = [&](const PadTarget& t, const InputSource& s) {
        return std::any_of(race.bindings.begin(), race.bindings.end(), [&](const Binding& b) { return b.target == t && b.source == s; });
    };
    CHECK(on(accel, i) && on(brake, k) && on(left, j));
    // Player 1's right stick: Home / End / Page Up / Page Down.
    InputSource home;
    CHECK(InputSource::parse("key.Home", home));
    PadTarget rx;
    CHECK(PadTarget::parse("RX-", rx));
    const Profile& menu1 = p1.set(DeviceKind::Keyboard, false, BindingSet::Keyboard);
    CHECK(std::any_of(menu1.bindings.begin(), menu1.bindings.end(), [&](const Binding& b) { return b.target == rx && b.source == home; }));
    // InputManager gives player 2 their own set.
    InputManager input;
    CHECK(input.bindings(1, true, BindingSet::Keyboard).bindings.size() == race.bindings.size());
}

// Any binding can come from any connected device: an input captured from another device than the
// player's Controller names it (InputSource::device), and reads only that device; untagged ones read
// the Controller. Two devices of one name are told apart ("#2"), a named device that isn't connected
// presses nothing, and Auto never hands the other player a device someone's bindings name.
TEST(input_binding_devices) {
    // No serial or port path reported: the name, and the order among devices of that name.
    const std::vector<DeviceId> names = {{"Wireless Controller", "", ""}, {"G29 Pedals", "", ""},
                                         {"Wireless Controller", "", ""}};
    CHECK(device_tag(names, 0) == "Wireless Controller" && device_tag(names, 1) == "G29 Pedals");
    CHECK(device_tag(names, 2) == "Wireless Controller#2");
    CHECK(find_device_tag(names, "Wireless Controller#2") == 2 && find_device_tag(names, "Wireless Controller") == 0);
    CHECK(find_device_tag(names, "G29 Pedals") == 1);
    CHECK(find_device_tag(names, "Wireless Controller#3") == -1 && find_device_tag(names, "Missing") == -1); // not connected
    CHECK(find_device_tag({}, "G29 Pedals") == -1);
    CHECK(device_tag_label(names, "Wireless Controller#2") == "Wireless Controller 2");
    CHECK(device_tag_label(names, "G29 Pedals") == "G29 Pedals" && device_tag_label({}, "Pad#2") == "Pad 2");
    // Serials: two identical pads keep their bindings whichever connects first.
    const std::vector<DeviceId> first = {{"DualSense Wireless Controller", "aa:01", "p1"},
                                         {"DualSense Wireless Controller", "bb:02", "p2"}};
    const std::vector<DeviceId> reversed = {first[1], first[0]};
    const std::string second_pad = device_tag(first, 1);
    CHECK(second_pad == "DualSense Wireless Controller@s:bb:02");
    CHECK(find_device_tag(first, second_pad) == 1 && find_device_tag(reversed, second_pad) == 0);
    CHECK(device_tag_label(first, second_pad) == "DualSense Wireless Controller 2"); // never the serial
    CHECK(device_tag_label(reversed, second_pad) == "DualSense Wireless Controller");
    // The name first: a serial on a device of another name doesn't match.
    CHECK(find_device_tag({{"Other Pad", "bb:02", ""}}, second_pad) == -1);
    // Pad B switched off: its binding reads nothing, never the look-alike pad A (whose R2 may
    // accelerate). The same for a port path.
    CHECK(find_device_tag({first[0]}, second_pad) == -1);
    CHECK(find_device_tag({{"Pad", "", "usb-1"}}, "Pad@p:usb-2") == -1);
    // Never the player's own Controller: its inputs are untagged, so a tag landing on it reads nothing.
    CHECK(resolve_device_tag(first, second_pad, 0) == 1 && resolve_device_tag(first, second_pad, 1) == -1);
    CHECK(resolve_device_tag(names, "Wireless Controller#2", 2) == -1 && resolve_device_tag(names, "G29 Pedals", -1) == 1);
    // Port paths when there's no serial, or the serial doesn't tell them apart.
    const std::vector<DeviceId> ports = {{"Pad", "", "usb-1"}, {"Pad", "", "usb-2"}};
    CHECK(device_tag(ports, 1) == "Pad@p:usb-2");
    CHECK(find_device_tag({ports[1], ports[0]}, "Pad@p:usb-2") == 0);
    const std::vector<DeviceId> same_serial = {{"Pad", "000", ""}, {"Pad", "000", ""}};
    CHECK(device_tag(same_serial, 1) == "Pad#2" && find_device_tag(same_serial, "Pad#2") == 1);
    // A tag saved before a serial was known still finds its device by name and order.
    CHECK(find_device_tag(first, "DualSense Wireless Controller#2") == 1);
    // Auto: a device the other player's bindings name is reserved.
    CHECK((reserved_devices(names, {"Wireless Controller#2"}) == std::vector<bool>{false, false, true}));
    CHECK((reserved_devices(names, {"Missing"}) == std::vector<bool>{false, false, false}));
    CHECK((reserved_devices(reversed, {second_pad}) == std::vector<bool>{true, false}));
    // Saved as the binding's device option, and read back.
    auto bindings = Binding::parse_line("Square", "pad.lefttrigger device=Wireless%20Controller#2 | pad.lefttrigger");
    CHECK(bindings.size() == 2);
    if (bindings.size() != 2) return;
    CHECK(bindings[0].source.device == "Wireless Controller#2" && bindings[1].source.device.empty());
    CHECK(!(bindings[0].source == bindings[1].source)); // a tagged input is another input
    CHECK(bindings[0].source_with_options() == "pad.lefttrigger device=Wireless%20Controller#2");
    auto again = Binding::parse_line("Square", bindings[0].source_with_options());
    CHECK(again.size() == 1 && again[0].source == bindings[0].source);
    // Brake on a second pad's trigger, added (Square) beside the player's own trigger.
    PlayerBindings b;
    PadTarget brake;
    CHECK(PadTarget::parse("Square", brake));
    CHECK(b.bind(DeviceKind::Pad, true, brake, bindings[0].source, false));
    std::vector<InputSource> on_brake;
    for (const Binding& x : b.set(DeviceKind::Pad, true, BindingSet::Controller).bindings)
        if (x.target == brake) on_brake.push_back(x.source);
    CHECK(on_brake.size() == 2 && on_brake[0] == bindings[1].source && on_brake[1] == bindings[0].source);
    CHECK(b.devices() == std::vector<std::string>{"Wireless Controller#2"});
    IniDocument out;
    b.save(out, "player1");
    CHECK(out.get("player1.race", "Square") == "pad.lefttrigger | pad.lefttrigger device=Wireless%20Controller#2");
    PlayerBindings loaded;
    loaded.load(out, "player1");
    CHECK(loaded.devices() == b.devices());
    // Shown with the device's name; with no such device connected it reads nothing (no device here).
    InputManager input;
    CHECK(input.describe(0, bindings[0].source) == "Wireless Controller 2: Left trigger");
    CHECK(input.describe(0, bindings[1].source) == "Left trigger");
    InputSource axis2;
    CHECK(InputSource::parse("joy.axis2", axis2));
    axis2.device = "G29 Pedals";
    CHECK(input.describe(0, axis2) == "G29 Pedals: Axis 2");
    CHECK(!input.wheel_steering(0));
}

// Settings reads every controller and the keyboard with the default buttons as well as player 1's
// pad, so a controller choice or bindings that leave player 1's controller dead (Keyboard only,
// custom bindings, the keyboard switched off) can always be changed back. Player 1's pad, which the
// game's menus and races read, is unchanged.
TEST(input_settings_menu_any_device) {
    auto bit = [](PadButton b) { return u16(1u << unsigned(b)); };
    PadState a, b;
    a.connected = true;
    a.buttons = bit(PadButton::Cross);
    a.pressure[size_t(PadButton::Cross)] = 40;
    a.axes[size_t(PadAxis::LY)] = 0; // stick up
    b.connected = true;
    b.buttons = bit(PadButton::Down) | bit(PadButton::Cross);
    b.pressure[size_t(PadButton::Cross)] = 200;
    b.axes[size_t(PadAxis::LY)] = 200; // less far from rest
    b.axes[size_t(PadAxis::LX)] = 250;
    PadState m = merge_pads({a, b, PadState{}});
    CHECK(m.connected && m.down(PadButton::Cross) && m.down(PadButton::Down) && !m.down(PadButton::Up));
    CHECK(m.press(PadButton::Cross) == 200);
    CHECK(m.axis(PadAxis::LY) == 0 && m.axis(PadAxis::LX) == 250 && m.axis(PadAxis::RX) == 128); // no cancelling
    CHECK(!merge_pads({}).connected && merge_pads({}).buttons == 0);

    auto press = [](InputManager& in, SDL_Scancode key) {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = key;
        in.handle_event(e);
        in.update(0.016);
    };
    // Custom bindings: player 1's own (K = Cross) and the default keys both work there.
    InputManager custom;
    custom.load(IniDocument::parse("[player1]\ncontroller = auto\n[player1.keyboard.bindings]\nCross = key.K\n"));
    press(custom, SDL_SCANCODE_K);
    CHECK(custom.pad(0).down(PadButton::Cross) && custom.any_device_pad().down(PadButton::Cross));
    press(custom, SDL_SCANCODE_DOWN);
    CHECK(custom.any_device_pad().down(PadButton::Down) && !custom.pad(0).down(PadButton::Down));
    press(custom, SDL_SCANCODE_ESCAPE);
    CHECK(custom.any_device_pad().down(PadButton::Triangle) && !custom.pad(0).down(PadButton::Triangle));
    custom.update(0.016);
    CHECK(custom.any_device_pad().buttons == 0);
    // Player 1's touch controls are part of player 1's pad.
    PadState ov;
    ov.connected = true;
    ov.buttons = bit(PadButton::Circle);
    custom.set_overlay(0, ov);
    custom.update(0.016);
    CHECK(custom.any_device_pad().down(PadButton::Circle));
}

// [input] background: off by default, read and saved. With the game's window unfocused a
// controller's press reaches the game only while it is on, or held on (a macOS file dialog);
// with the focus it always does. SDL's dummy video driver gives a shown window the focus and a
// hidden one none.
TEST(input_background_controllers) {
    IniDocument out;
    InputManager off;
    off.load(IniDocument::parse("[player1]\ncontroller = auto\n"));
    off.save(out);
    CHECK(!off.background() && out.get("input", "background") == "off");
    InputManager on;
    on.load(IniDocument::parse("[input]\nbackground = on\n"));
    on.save(out);
    CHECK(on.background() && out.get("input", "background") == "on");
    on.set_background(false); // SDL's default again
#if GT3R_XR
    SKIP("VR builds always read controllers in the background");
#endif

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    const bool init = SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
    SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
    if (!init) SKIP("no SDL dummy video driver");
    SDL_Window* window = SDL_CreateWindow("unfocused", 64, 64, SDL_WINDOW_HIDDEN);
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons = 1;
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    SDL_Joystick* pad = id ? SDL_OpenJoystick(id) : nullptr;
    CHECK(window && pad && !SDL_GetKeyboardFocus());
    if (window && pad) {
        auto press = [pad] { // the button's state after a press, released again
            SDL_SetJoystickVirtualButton(pad, 0, true);
            SDL_UpdateJoysticks();
            const bool down = SDL_GetJoystickButton(pad, 0);
            SDL_SetJoystickVirtualButton(pad, 0, false);
            SDL_UpdateJoysticks();
            return down;
        };
        InputManager in;
        CHECK(!press());
        in.set_background(true);
        CHECK(press());
        in.set_background(false);
        CHECK(!press());
        in.hold_background(true); // the dialog's hold, the setting off
        CHECK(press());
        in.hold_background(false);
        CHECK(!press());
        in.set_background(true); // the setting on: the hold's end keeps it
        in.hold_background(true);
        in.hold_background(false);
        CHECK(press());
        in.set_background(false);
        SDL_ShowWindow(window); // the window in front
        CHECK(SDL_GetKeyboardFocus() == window && press());
    }
    if (pad) SDL_CloseJoystick(pad);
    if (id) SDL_DetachVirtualJoystick(id);
    if (window) SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
}

TEST(touch_tap_shorter_than_a_frame) {
    TouchControls t;
    t.set_layout(TouchLayout::Menu);
    const Widget cross = *find_widget(t, Widget::Kind::Button, PadButton::Cross);
    t.finger_down(1, cross.x, cross.y, 1.0f);
    t.finger_up(1); // down and up between two frames
    t.update(0.016, 0);
    CHECK(t.pad().down(PadButton::Cross)); // reported once...
    t.update(0.016, 0);
    CHECK(!t.pad().down(PadButton::Cross)); // ...then released
}

TEST(touch_controls_use_side_bars) {
    // 20:9 phone: the 16:9 game area leaves ~80 UI units on each side.
    TouchControls t = driving();
    t.set_screen(-80, 0, 720, 360);
    const Widget gas = *find_widget(t, Widget::Kind::Pedal, PadButton::Cross);
    CHECK(gas.x + gas.w > 690); // anchored to the right edge of the screen
    const Widget steer = *find_widget(t, Widget::Kind::Steering);
    CHECK(steer.x == -80.0f);
    CHECK(t.finger_down(1, -60, 300, 1.0f)); // a touch in the left bar steers
    t.finger_move(1, -60 + 0.14f * 640 * 0.5f, 300, 1.0f);
    t.update(0.016, 0);
    CHECK(std::fabs(t.pad().axis_f(PadAxis::LX) - 0.5f) < 0.02f);
    // A smaller "screen" than the game area is ignored.
    t.set_screen(10, 10, 600, 300);
    CHECK(find_widget(t, Widget::Kind::Pedal, PadButton::Cross)->x + gas.w <= 640.0f);
}

TEST(touch_controls_avoid_cutout_insets) {
    // Phone with a display cutout on the left (27 UI units) and small
    // rounded-corner insets elsewhere.
    TouchControls t;
    t.set_layout(TouchLayout::Menu);
    t.set_screen(-70, 0, 710, 360);
    const float pad_x = find_widget(t, Widget::Kind::DPad)->x;
    t.set_insets(27, 11, 11, 11);
    CHECK(find_widget(t, Widget::Kind::DPad)->x >= -70 + 27 + 4 - 0.01f); // clear of the cutout
    CHECK(find_widget(t, Widget::Kind::DPad)->x > pad_x);
    CHECK(find_widget(t, Widget::Kind::Button, PadButton::L1)->x >= -70 + 31 - 0.01f);
    // Left-handed: the pedals move to the cutout side and keep clear of it.
    TouchConfig lh;
    lh.left_handed = true;
    TouchControls d = driving(lh);
    d.set_screen(-70, 0, 710, 360);
    d.set_insets(27, 11, 11, 11);
    const Widget gas = *find_widget(d, Widget::Kind::Pedal, PadButton::Cross);
    CHECK(gas.x >= -70 + 31 - 0.01f && gas.x < 0);
    // Right-handed: the pedals keep their normal margin from the right edge.
    TouchControls r = driving();
    r.set_screen(-70, 0, 710, 360);
    const float gx = find_widget(r, Widget::Kind::Pedal, PadButton::Cross)->x;
    r.set_insets(27, 11, 11, 11);
    CHECK(std::fabs(find_widget(r, Widget::Kind::Pedal, PadButton::Cross)->x - (gx - 1)) < 0.01f); // 11 + 4 vs 14
}

TEST(touch_safe_area) {
    TouchControls t = driving();
    TouchRect hidden = t.safe_area();
    CHECK(hidden.x == 0 && hidden.y == 0 && hidden.w == 640 && hidden.h == 360);
    t.note_touch();
    TouchRect a = t.safe_area();
    const Widget brake = *find_widget(t, Widget::Kind::Pedal, PadButton::Square);
    CHECK(a.x == 0);                      // the steering zone doesn't count
    CHECK(a.x + a.w < brake.x);           // right edge clear of the pedal cluster (incl. handbrake)
    CHECK(a.y > 30 && a.y < 60);          // below the pause/camera row
    CHECK(a.y + a.h == 360);
    // With wide side bars most of the cluster leaves the game area.
    t.set_screen(-80, 0, 720, 360);
    TouchRect wide = t.safe_area();
    CHECK(wide.x + wide.w > a.x + a.w + 60);
    // Menu layout: D-pad on the left, face buttons on the right.
    t.set_layout(TouchLayout::Menu);
    t.set_screen(0, 0, 640, 360);
    TouchRect m = t.safe_area();
    CHECK(m.x > 100 && m.x + m.w < 540);
}

TEST(touch_game_area_sizes) {
    // Settings -> Display -> Aspect ratio: the game area 800 wide (fill on a 20:9 phone) or
    // 480 (4:3, with the window's bars beside it).
    TouchControls t = driving();
    t.set_game_area(800, 360);
    TouchRect hidden = t.safe_area();
    CHECK(hidden.x == 0 && hidden.w == 800 && hidden.h == 360);
    CHECK(t.screen().w == 800); // the screen is never smaller than the game area
    t.note_touch();
    const Widget gas = *find_widget(t, Widget::Kind::Pedal, PadButton::Cross);
    CHECK(gas.x + gas.w > 760); // at the game area's right edge
    TouchRect a = t.safe_area();
    CHECK(a.x + a.w <= find_widget(t, Widget::Kind::Pedal, PadButton::Square)->x);
    CHECK(a.x + a.w > 400 && a.y + a.h == 360);
    // 4:3 on a 20:9 phone: the window reaches 160 units past each side; the controls sit there.
    t.set_game_area(480, 360);
    t.set_screen(-160, 0, 640, 360);
    TouchRect c = t.safe_area();
    CHECK(c.x >= 0 && c.x + c.w <= 480);
    CHECK(c.w > 400); // the pedals are mostly in the right-hand bar
}

TEST(touch_control_bounds) {
    TouchControls t = driving();
    CHECK(t.control_bounds().empty()); // hidden
    t.note_touch();
    const std::vector<TouchRect> b = t.control_bounds();
    CHECK(b.size() + 1 == t.widgets().size()); // all but the steering zone
    const Widget gas = *find_widget(t, Widget::Kind::Pedal, PadButton::Cross);
    const Widget up = *find_widget(t, Widget::Kind::Button, PadButton::R2);
    bool gas_seen = false, up_seen = false;
    for (const TouchRect& r : b) {
        gas_seen |= r.x == gas.x && r.y == gas.y && r.w == gas.w && r.h == gas.h;
        up_seen |= r.x == up.x - up.w && r.y == up.y - up.w && r.w == 2 * up.w && r.h == 2 * up.w; // circle's box
    }
    CHECK(gas_seen && up_seen);
    // The race HUD's corner blocks keep their places beside the driving layout at its normal
    // size (16:9 and 20:9): the top corners hold no control down to the shift buttons.
    for (float x1 : {640.0f, 710.0f}) {
        t.set_screen(640 - x1, 0, x1, 360);
        for (const TouchRect& r : t.control_bounds()) {
            const bool top_left = r.x < 200 && r.y < 150, top_right = r.x + r.w > 500 && r.y < 150;
            CHECK(!top_left && !top_right);
        }
    }
}

TEST(touch_clear_above_moves_buttons_off_controls) {
    // The title menu's "GT3R menu" button, bottom of the left bar, on a 20:9 phone.
    const TouchRect button{6, 314, 68, 22};
    for (bool left_handed : {false, true}) {
        TouchConfig cfg;
        cfg.left_handed = left_handed; // face buttons in that corner instead of the D-pad
        for (float size : {0.6f, 1.0f, 1.6f}) {
            cfg.size = size;
            TouchControls t;
            t.configure(cfg);
            t.set_layout(TouchLayout::Menu);
            t.set_screen(-70, 0, 710, 360);
            TouchRect hidden = t.clear_above(button);
            CHECK(hidden.x == button.x && hidden.y == button.y); // hidden controls: left alone
            t.note_touch();
            TouchRect r = t.clear_above(button);
            CHECK(r.x == button.x && r.w == button.w && r.h == button.h);
            CHECK(r.y < button.y && r.y > 50); // above the corner cluster, below L1
            // Nowhere on it is a touch taken by the controls.
            for (float x = r.x; x <= r.x + r.w; x += 2)
                for (float y = r.y; y <= r.y + r.h; y += 2) CHECK(!t.hit(x, y));
        }
    }
    // Already clear: unchanged.
    TouchControls t;
    t.set_layout(TouchLayout::Menu);
    t.note_touch();
    TouchRect mid = t.clear_above({300, 150, 40, 20});
    CHECK(mid.x == 300 && mid.y == 150);
}

TEST(touch_gt3_race_buttons) {
    // The driving layout presses the race pad's buttons in GT3's meaning.
    TouchControls t = driving();
    CHECK(find_widget(t, Widget::Kind::Button, PadButton::R2)->label == "UP");
    CHECK(find_widget(t, Widget::Kind::Button, PadButton::L2)->label == "DOWN");
    CHECK(find_widget(t, Widget::Kind::Button, PadButton::R1)->label == "CAM");
    CHECK(find_widget(t, Widget::Kind::Button, PadButton::Select) == nullptr); // no respawn, as in GT3
    CHECK(find_widget(t, Widget::Kind::Button, PadButton::Triangle) == nullptr);
}

namespace {

float cx(const TouchRect& r) { return r.x + r.w / 2; }
float cy(const TouchRect& r) { return r.y + r.h / 2; }

} // namespace

TEST(touch_placement_parse_and_format) {
    TouchPlacement p = TouchPlacement::parse("right 40 bottom 66 size 1.2");
    CHECK(p.ax == TouchPlacement::AnchorX::Right && p.ay == TouchPlacement::AnchorY::Bottom);
    CHECK(p.x == 40 && p.y == 66 && std::fabs(p.scale - 1.2f) < 1e-6f && !p.hidden);
    CHECK(TouchPlacement::parse(p.to_string()) == p);
    TouchPlacement q = TouchPlacement::parse("Centre -12.5, Top 20, hidden");
    CHECK(q.ax == TouchPlacement::AnchorX::Centre && q.x == -12.5f && q.ay == TouchPlacement::AnchorY::Top && q.hidden);
    CHECK(q.to_string() == "center -12.5 top 20 hidden");
    // Half a position, junk, out-of-range sizes.
    TouchPlacement h = TouchPlacement::parse("left 30 size 9 banana");
    CHECK(!h.moved() && h.scale == TouchPlacement::kMaxScale);
    CHECK(TouchPlacement::parse("").is_default());
    CHECK(TouchPlacement::parse("hidden").to_string() == "hidden");
}

TEST(touch_placement_config_roundtrip) {
    IniDocument ini = IniDocument::parse("[touch]\nsize = 1.0\ndrive.gas = left 30 bottom 40\ndrive.camera = hidden\n"
                                         "menu.buttons = size 1.5\nmenu.unknown = left 1 top 1\ndrive.brake = \n");
    TouchConfig c = TouchConfig::load(ini);
    CHECK(c.placements.size() == 3); // unknown keys and default placements are dropped
    CHECK(c.placement("drive.gas").ax == TouchPlacement::AnchorX::Left);
    CHECK(c.placement("drive.camera").hidden);
    CHECK(c.customized(TouchLayout::Driving) && c.customized(TouchLayout::Menu));
    c.set_placement("drive.camera", TouchPlacement{}); // back to default: dropped
    c.reset_placements(TouchLayout::Menu);
    CHECK(!c.customized(TouchLayout::Menu) && c.placements.size() == 1);
    c.save(ini);
    CHECK(ini.get("touch", "drive.gas") == std::optional<std::string>("left 30 bottom 40"));
    CHECK(!ini.get("touch", "drive.camera") && !ini.get("touch", "menu.buttons") && !ini.get("touch", "drive.brake"));
    CHECK(ini.get("touch", "menu.unknown")); // not ours: left alone
    TouchConfig d = TouchConfig::load(ini);
    CHECK(d.placements == c.placements);
}

TEST(touch_placement_moves_and_resizes) {
    TouchConfig cfg;
    TouchControls def = driving(cfg);
    const TouchRect gas0 = def.group_bounds("drive.gas");
    // The gas pedal 60 units in from the left edge, 80 up from the bottom, half again as big.
    cfg.set_placement("drive.gas", TouchPlacement::parse("left 60 bottom 120 size 1.5"));
    TouchControls t = driving(cfg);
    const TouchRect gas = t.group_bounds("drive.gas");
    CHECK(std::fabs(gas.w - gas0.w * 1.5f) < 0.01f && std::fabs(gas.h - gas0.h * 1.5f) < 0.01f);
    CHECK(std::fabs(cx(gas) - (14 + 60)) < 0.01f && std::fabs(cy(gas) - (346 - 120)) < 0.01f);
    // Everything else stays where the layout puts it.
    CHECK(t.group_bounds("drive.brake").x == def.group_bounds("drive.brake").x);
    // It still drives, over the steering zone (controls take touches first).
    t.note_touch();
    CHECK(t.finger_down(1, cx(gas), cy(gas), 1.0f));
    t.update(0.016, 0);
    CHECK(t.pad().down(PadButton::Cross));
    CHECK(t.pad().axis_f(PadAxis::LX) == 0.0f);
    // Too big to fit where it was put: pushed back inside the margin.
    cfg.set_placement("drive.gas", TouchPlacement::parse("left 0 bottom 0 size 2"));
    TouchControls big = driving(cfg);
    const TouchRect g2 = big.group_bounds("drive.gas");
    CHECK(g2.x >= 14 - 0.01f && g2.y + g2.h <= 346 + 0.01f);
}

TEST(touch_placement_follows_screen_edges) {
    // A control placed 50 units in from the right edge and 30 from the top keeps those
    // distances on screens of other shapes (16:9, 20:9 phone, 4:3 tablet).
    TouchConfig cfg;
    cfg.set_placement("drive.camera", TouchPlacement::parse("right 50 top 30"));
    for (TouchRect s : {TouchRect{0, 0, 640, 360}, TouchRect{-80, 0, 800, 360}, TouchRect{0, -60, 640, 480}}) {
        TouchControls t = driving(cfg);
        t.set_screen(s.x, s.y, s.x + s.w, s.y + s.h);
        const TouchRect b = t.group_bounds("drive.camera");
        CHECK(std::fabs(cx(b) - (s.x + s.w - 14 - 50)) < 0.01f);
        CHECK(std::fabs(cy(b) - (s.y + 14 + 30)) < 0.01f);
    }
}

TEST(touch_placement_at_roundtrip) {
    // What the editor does: drop a control at a point, save, rebuild; it is there.
    for (bool left_handed : {false, true}) {
        TouchConfig cfg;
        cfg.left_handed = left_handed;
        TouchControls t = driving(cfg);
        t.set_screen(-70, 0, 710, 360);
        t.set_insets(27, 11, 11, 11);
        for (auto [x, y] : {std::pair{100.0f, 200.0f}, std::pair{330.0f, 60.0f}, std::pair{600.0f, 300.0f}}) {
            TouchPlacement p = t.placement_at("drive.handbrake", x, y);
            CHECK(p.moved());
            cfg.set_placement("drive.handbrake", p);
            t.configure(cfg);
            const TouchRect b = t.group_bounds("drive.handbrake");
            CHECK(std::fabs(cx(b) - x) <= 0.5f && std::fabs(cy(b) - y) <= 0.5f);
        }
        // Off the screen or under the cutout: kept inside the area the controls keep to.
        cfg.set_placement("drive.handbrake", t.placement_at("drive.handbrake", -200, 500));
        t.configure(cfg);
        const TouchRect b = t.group_bounds("drive.handbrake");
        CHECK(b.x >= -70 + 15 - 0.5f && b.y + b.h <= 360 - 15 + 0.5f);
    }
}

TEST(touch_placement_groups_and_hidden) {
    // The face buttons move and resize as one; hidden controls draw nothing and take no touches.
    TouchConfig cfg;
    TouchControls def;
    def.configure(cfg);
    def.set_layout(TouchLayout::Menu);
    const TouchRect g0 = def.group_bounds("menu.buttons");
    cfg.set_placement("menu.buttons", TouchPlacement::parse("left 60 top 60 size 0.8"));
    cfg.set_placement("menu.select", TouchPlacement::parse("hidden"));
    TouchControls t;
    t.configure(cfg);
    t.set_layout(TouchLayout::Menu);
    t.note_touch();
    const TouchRect g = t.group_bounds("menu.buttons");
    CHECK(std::fabs(g.w - g0.w * 0.8f) < 0.01f);
    CHECK(std::fabs(cx(g) - (14 + 60)) < 0.01f && std::fabs(cy(g) - (14 + 60)) < 0.01f);
    const Widget cross = *find_widget(t, Widget::Kind::Button, PadButton::Cross);
    const Widget tri = *find_widget(t, Widget::Kind::Button, PadButton::Triangle);
    CHECK(cross.y > tri.y && std::fabs(cross.x - tri.x) < 0.01f); // still a diamond
    const Widget sel = *find_widget(t, Widget::Kind::Button, PadButton::Select);
    CHECK(sel.hidden && !t.hit(sel.x + sel.w / 2, sel.y + sel.h / 2));
    CHECK(t.key_at(sel.x + sel.w / 2, sel.y + sel.h / 2) == "menu.select"); // the editor still finds it
    CHECK(t.control_bounds().size() + 1 == t.widgets().size());            // all but the hidden one
    CHECK(!t.finger_down(1, sel.x + sel.w / 2, sel.y + sel.h / 2, 1.0f));
    CHECK(t.key_at(320, 200).empty());
}
