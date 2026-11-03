#if GT3R_XR

// Touch controllers as an SDL virtual gamepad, so the input system (bindings, remapping, the
// race layouts, rumble) treats them like any other pad: thumbsticks, triggers (analog), A B X Y,
// grips as shoulders, stick clicks, the menu button as Start. With [vr] steering = wheel the
// grips grab an invisible steering wheel instead and the hands' roll steers.

#include "xr/xr_internal.h"

#include "base/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r::xr {

namespace {

constexpr float kPi = 3.14159265f;

// The virtual gamepad's buttons, in SDL_GamepadButton order (SDL numbers a virtual pad's
// buttons in the order of its button_mask bits).
const SDL_GamepadButton kButtons[] = {
    SDL_GAMEPAD_BUTTON_SOUTH,      SDL_GAMEPAD_BUTTON_EAST,        SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,      SDL_GAMEPAD_BUTTON_START,       SDL_GAMEPAD_BUTTON_LEFT_STICK,
    SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
};
int button_index(SDL_GamepadButton b) {
    for (int i = 0; i < int(SDL_arraysize(kButtons)); ++i)
        if (kButtons[i] == b) return i;
    return -1;
}

Sint16 stick_axis(float v) { return Sint16(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); }
// Triggers on a virtual pad span the whole axis: SDL_JOYSTICK_AXIS_MIN is released.
Sint16 trigger_axis(float v) { return Sint16(std::lround(-32768.0f + std::clamp(v, 0.0f, 1.0f) * 65535.0f)); }

float wrap(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

render::Vec3 forward(const Pose& p) {
    // rotate(q, (0, 0, -1))
    return {-2 * (p.qx * p.qz + p.qw * p.qy), -2 * (p.qy * p.qz - p.qw * p.qx), -(1 - 2 * (p.qx * p.qx + p.qy * p.qy))};
}

bool SDLCALL pad_rumble(void* user, Uint16 low, Uint16 high) {
    auto* in = static_cast<Input*>(user);
    in->rumble[0] = float(low) / 65535.0f;
    in->rumble[1] = float(high) / 65535.0f;
    return true;
}

} // namespace

bool Session::Impl::create_input() {
    Input& in = input;
    auto path = [&](const char* s) {
        XrPath p = XR_NULL_PATH;
        xrStringToPath(instance, s, &p);
        return p;
    };
    in.hand[0] = path("/user/hand/left");
    in.hand[1] = path("/user/hand/right");

    auto si = xr_init<XrActionSetCreateInfo>(XR_TYPE_ACTION_SET_CREATE_INFO);
    std::strcpy(si.actionSetName, "gameplay");
    std::strcpy(si.localizedActionSetName, "Gameplay");
    if (!check(xrCreateActionSet(instance, &si, &in.set), "xrCreateActionSet")) return false;

    auto action = [&](XrAction& a, const char* name, const char* label, XrActionType type, bool both_hands) {
        auto ci = xr_init<XrActionCreateInfo>(XR_TYPE_ACTION_CREATE_INFO);
        ci.actionType = type;
        std::strcpy(ci.actionName, name);
        std::strcpy(ci.localizedActionName, label);
        if (both_hands) {
            ci.countSubactionPaths = 2;
            ci.subactionPaths = in.hand;
        }
        return check(xrCreateAction(in.set, &ci, &a), "xrCreateAction");
    };
    bool ok = action(in.trigger, "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, true) &&
              action(in.squeeze, "squeeze", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, true) &&
              action(in.stick, "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, true) &&
              action(in.stick_click, "thumbstick_click", "Thumbstick press", XR_ACTION_TYPE_BOOLEAN_INPUT, true) &&
              action(in.a, "button_a", "A", XR_ACTION_TYPE_BOOLEAN_INPUT, false) &&
              action(in.b, "button_b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT, false) &&
              action(in.x, "button_x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT, false) &&
              action(in.y, "button_y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT, false) &&
              action(in.menu, "menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, false) &&
              action(in.grip_pose, "grip_pose", "Hand", XR_ACTION_TYPE_POSE_INPUT, true) &&
              action(in.aim_pose, "aim_pose", "Pointer", XR_ACTION_TYPE_POSE_INPUT, true) &&
              action(in.haptic, "haptic", "Vibration", XR_ACTION_TYPE_VIBRATION_OUTPUT, true);
    if (!ok) return false;

    std::vector<XrActionSuggestedBinding> b;
    auto bind = [&](XrAction a, const char* p) { b.push_back({a, path(p)}); };
    for (const char* hand : {"/user/hand/left", "/user/hand/right"}) {
        std::string h = hand;
        bind(in.trigger, (h + "/input/trigger/value").c_str());
        bind(in.squeeze, (h + "/input/squeeze/value").c_str());
        bind(in.stick, (h + "/input/thumbstick").c_str());
        bind(in.stick_click, (h + "/input/thumbstick/click").c_str());
        bind(in.grip_pose, (h + "/input/grip/pose").c_str());
        bind(in.aim_pose, (h + "/input/aim/pose").c_str());
        bind(in.haptic, (h + "/output/haptic").c_str());
    }
    bind(in.a, "/user/hand/right/input/a/click");
    bind(in.b, "/user/hand/right/input/b/click");
    bind(in.x, "/user/hand/left/input/x/click");
    bind(in.y, "/user/hand/left/input/y/click");
    bind(in.menu, "/user/hand/left/input/menu/click");
    auto sb = xr_init<XrInteractionProfileSuggestedBinding>(XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING);
    sb.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
    sb.countSuggestedBindings = u32(b.size());
    sb.suggestedBindings = b.data();
    if (!check(xrSuggestInteractionProfileBindings(instance, &sb), "xrSuggestInteractionProfileBindings")) return false;

    auto ai = xr_init<XrSessionActionSetsAttachInfo>(XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO);
    ai.countActionSets = 1;
    ai.actionSets = &in.set;
    if (!check(xrAttachSessionActionSets(session, &ai), "xrAttachSessionActionSets")) return false;

    for (int h = 0; h < 2; ++h) {
        auto ci = xr_init<XrActionSpaceCreateInfo>(XR_TYPE_ACTION_SPACE_CREATE_INFO);
        ci.subactionPath = in.hand[h];
        ci.poseInActionSpace = {{0, 0, 0, 1}, {0, 0, 0}};
        ci.action = in.grip_pose;
        check(xrCreateActionSpace(session, &ci, &in.grip_space[h]), "xrCreateActionSpace");
        ci.action = in.aim_pose;
        check(xrCreateActionSpace(session, &ci, &in.aim_space[h]), "xrCreateActionSpace");
    }

    // The gamepad the rest of the game sees.
    SDL_VirtualJoystickDesc d;
    SDL_INIT_INTERFACE(&d);
    d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    d.naxes = SDL_GAMEPAD_AXIS_COUNT;
    d.nbuttons = Uint16(SDL_arraysize(kButtons));
    for (SDL_GamepadButton btn : kButtons) d.button_mask |= 1u << btn;
    d.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY) | (1u << SDL_GAMEPAD_AXIS_RIGHTX) |
                  (1u << SDL_GAMEPAD_AXIS_RIGHTY) | (1u << SDL_GAMEPAD_AXIS_LEFT_TRIGGER) |
                  (1u << SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    d.name = "Meta Quest Touch";
    d.userdata = &in;
    d.Rumble = pad_rumble;
    in.pad_id = SDL_AttachVirtualJoystick(&d);
    if (in.pad_id) in.pad = SDL_OpenJoystick(in.pad_id);
    if (!in.pad) LOG_WARN("VR: virtual gamepad: %s", SDL_GetError());
    return true;
}

void Session::Impl::destroy_input() {
    Input& in = input;
    if (in.pad) SDL_CloseJoystick(in.pad);
    if (in.pad_id) SDL_DetachVirtualJoystick(in.pad_id);
    for (int h = 0; h < 2; ++h) {
        if (in.grip_space[h]) xrDestroySpace(in.grip_space[h]);
        if (in.aim_space[h]) xrDestroySpace(in.aim_space[h]);
    }
    if (in.set) xrDestroyActionSet(in.set);
    in = Input{};
}

void Session::update_input(double dt, bool pointer_allowed) {
    (void)dt;
    Impl& x = *impl_;
    Input& in = x.input;
    if (!in.set || !in.pad) return;
    // Out of focus (system menu) the actions read as idle: release everything.
    const bool live = x.running && x.state == XR_SESSION_STATE_FOCUSED;
    if (live) {
        XrActiveActionSet active{in.set, XR_NULL_PATH};
        auto si = xr_init<XrActionsSyncInfo>(XR_TYPE_ACTIONS_SYNC_INFO);
        si.countActiveActionSets = 1;
        si.activeActionSets = &active;
        x.check(x.xrSyncActions(x.session, &si), "xrSyncActions");
    }
    auto get_f = [&](XrAction a, XrPath sub) {
        auto gi = xr_init<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
        gi.action = a;
        gi.subactionPath = sub;
        auto st = xr_init<XrActionStateFloat>(XR_TYPE_ACTION_STATE_FLOAT);
        return live && XR_SUCCEEDED(x.xrGetActionStateFloat(x.session, &gi, &st)) && st.isActive ? st.currentState : 0.0f;
    };
    auto get_b = [&](XrAction a, XrPath sub) {
        auto gi = xr_init<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
        gi.action = a;
        gi.subactionPath = sub;
        auto st = xr_init<XrActionStateBoolean>(XR_TYPE_ACTION_STATE_BOOLEAN);
        return live && XR_SUCCEEDED(x.xrGetActionStateBoolean(x.session, &gi, &st)) && st.isActive && st.currentState;
    };
    for (int h = 0; h < 2; ++h) {
        in.trigger_v[h] = get_f(in.trigger, in.hand[h]);
        in.squeeze_v[h] = get_f(in.squeeze, in.hand[h]);
        in.click[h] = get_b(in.stick_click, in.hand[h]);
        auto gi = xr_init<XrActionStateGetInfo>(XR_TYPE_ACTION_STATE_GET_INFO);
        gi.action = in.stick;
        gi.subactionPath = in.hand[h];
        auto st = xr_init<XrActionStateVector2f>(XR_TYPE_ACTION_STATE_VECTOR2F);
        bool ok = live && XR_SUCCEEDED(x.xrGetActionStateVector2f(x.session, &gi, &st)) && st.isActive;
        in.stick_v[h][0] = ok ? st.currentState.x : 0.0f;
        in.stick_v[h][1] = ok ? st.currentState.y : 0.0f;
        // Hand poses in the seated space at the frame's display time.
        const XrSpaceLocationFlags tracked =
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
        auto loc = xr_init<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
        in.grip_valid[h] = live && in.grip_space[h] &&
                           XR_SUCCEEDED(x.xrLocateSpace(in.grip_space[h], x.local, x.frame.predictedDisplayTime, &loc)) &&
                           (loc.locationFlags & tracked) == tracked;
        if (in.grip_valid[h]) in.grip[h] = from_xr(loc.pose);
        loc = xr_init<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
        in.aim_valid[h] = live && in.aim_space[h] &&
                          XR_SUCCEEDED(x.xrLocateSpace(in.aim_space[h], x.local, x.frame.predictedDisplayTime, &loc)) &&
                          (loc.locationFlags & tracked) == tracked;
        if (in.aim_valid[h]) in.aim[h] = from_xr(loc.pose);
    }
    in.btn_a = get_b(in.a, XR_NULL_PATH);
    in.btn_b = get_b(in.b, XR_NULL_PATH);
    in.btn_x = get_b(in.x, XR_NULL_PATH);
    in.btn_y = get_b(in.y, XR_NULL_PATH);
    in.btn_menu = get_b(in.menu, XR_NULL_PATH);

    // Steering wheel: both grips squeezed; the roll of the line between the hands (seen from the
    // seat, level = straight ahead) turns it, right hand down = right.
    float steer = in.stick_v[0][0];
    const bool grab = settings_.wheel && in.squeeze_v[0] > 0.6f && in.squeeze_v[1] > 0.6f && in.grip_valid[0] &&
                      in.grip_valid[1];
    if (grab) {
        render::Vec3 d = in.grip[1].pos - in.grip[0].pos;
        float raw = std::atan2(-d.y, d.x);
        // Unwrapped from frame to frame, so the wheel turns past half a turn.
        in.wheel_angle = in.wheel_held ? in.wheel_angle + wrap(raw - wrap(in.wheel_angle)) : raw;
        in.wheel_held = true;
        const float half_lock = float(settings_.wheel_degrees) * 0.5f * kPi / 180.0f;
        steer = std::clamp(in.wheel_angle / half_lock, -1.0f, 1.0f);
    } else {
        in.wheel_held = false;
    }

    // The menus' pointer: the right aim ray against the canvas where the last frame showed it,
    // the virtual screen, or in a stereo race (its pause window) the HUD panel (end_frame: its
    // centre `drop` below the eyes, tilted to face them).
    pointer_on_ = false;
    const float dist = hud_panel_ ? settings_.hud_distance : settings_.screen_distance;
    const float drop = hud_panel_ ? settings_.hud_drop : 0.0f;
    const float w = hud_panel_ ? settings_.hud_width : settings_.screen_width;
    const float h = w * float(canvas_h_) / float(canvas_w_);
    float u = 0, v = 0;
    if (pointer_allowed && in.aim_valid[1]) {
        const Pose& p = in.aim[1];
        const render::Vec3 dir = forward(p);
        const float tilt = -std::atan2(drop, dist);
        const render::Vec3 c{0, -drop, -dist}, ex{1, 0, 0}, ey{0, std::cos(tilt), std::sin(tilt)},
            n{0, -std::sin(tilt), std::cos(tilt)};
        auto dot = [](const render::Vec3& a, const render::Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
        const float dn = dot(dir, n);
        if (dn < -1e-3f) {
            const float t = dot(c - p.pos, n) / dn;
            const render::Vec3 d = p.pos + dir * t - c;
            u = dot(d, ex) / w + 0.5f;
            v = 0.5f - dot(d, ey) / h;
            pointer_on_ = t > 0 && u >= 0 && u <= 1 && v >= 0 && v <= 1;
        }
    }
    // A pressed over something the page takes clicks on (a header's Back, a tab, a row) clicks it
    // as the trigger; elsewhere, and while driving, it is the pad's A. Decided as it goes down.
    if (in.btn_a != a_held_) a_held_ = in.btn_a, a_click_ = in.btn_a && pointer_on_ && a_clicks_;
    // Pulling the trigger tips the controller (on the default screen a degree moves the ray ~10
    // UI units, past the pages' drag distance), and a hand shakes: from the start of a pull (or
    // an A click) the pointer holds still where it was, so the click lands there, until the ray
    // moves 3 degrees away (a drag: a list, a slider).
    const float trigger = in.trigger_v[1];
    if (!pointer_on_ || (trigger < 0.1f && !a_click_)) {
        pull_ = pull_free_ = false;
    } else if (!pull_) {
        pull_ = true, pull_free_ = false;
        pull_u_ = u, pull_v_ = v;
    } else if (!pull_free_) {
        pull_free_ = std::hypot((u - pull_u_) * w, (v - pull_v_) * h) > dist * std::tan(3.0f * kPi / 180.0f);
    }
    const bool held = pull_ && !pull_free_;
    pointer_u_ = held ? pull_u_ : u;
    pointer_v_ = held ? pull_v_ : v;
    // Hysteresis on the trigger's click.
    trigger_down_ = pointer_on_ && trigger > (trigger_down_ ? 0.3f : 0.5f);
    pointer_pressed_ = trigger_down_ || (a_click_ && pointer_on_);

    SDL_Joystick* j = in.pad;
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTX, stick_axis(steer));
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFTY, stick_axis(-in.stick_v[0][1])); // SDL: down positive
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHTX, stick_axis(in.stick_v[1][0]));
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHTY, stick_axis(-in.stick_v[1][1]));
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, trigger_axis(in.trigger_v[0]));
    SDL_SetJoystickVirtualAxis(j, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, trigger_axis(pointer_on_ ? 0.0f : in.trigger_v[1]));
    auto set = [&](SDL_GamepadButton b, bool down) { SDL_SetJoystickVirtualButton(j, button_index(b), down); };
    set(SDL_GAMEPAD_BUTTON_SOUTH, in.btn_a && !a_click_);
    set(SDL_GAMEPAD_BUTTON_EAST, in.btn_b);
    set(SDL_GAMEPAD_BUTTON_WEST, in.btn_x);
    set(SDL_GAMEPAD_BUTTON_NORTH, in.btn_y);
    set(SDL_GAMEPAD_BUTTON_START, in.btn_menu);
    set(SDL_GAMEPAD_BUTTON_LEFT_STICK, in.click[0]);
    set(SDL_GAMEPAD_BUTTON_RIGHT_STICK, in.click[1]);
    // Grips as shoulder buttons, with hysteresis; held on the wheel they belong to it.
    const bool wheel_mode = settings_.wheel;
    set(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, !wheel_mode && in.squeeze_v[0] > 0.55f);
    set(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, !wheel_mode && in.squeeze_v[1] > 0.55f);

    // Rumble, renewed every frame while the game asks for it (SDL stops it with zeros). Both hands play the
    // stronger motor: a controller in each hand turns any split between the motors into a side.
    const float amp = live ? std::clamp(std::max(in.rumble[0], in.rumble[1]), 0.0f, 1.0f) : 0.0f;
    for (int h = 0; h < 2 && amp > 0.01f; ++h) {
        auto hi = xr_init<XrHapticActionInfo>(XR_TYPE_HAPTIC_ACTION_INFO);
        hi.action = in.haptic;
        hi.subactionPath = in.hand[h];
        auto v = xr_init<XrHapticVibration>(XR_TYPE_HAPTIC_VIBRATION);
        v.amplitude = amp;
        v.duration = XrDuration(60'000'000); // 60 ms, renewed next frame
        v.frequency = XR_FREQUENCY_UNSPECIFIED;
        x.xrApplyHapticFeedback(x.session, &hi, reinterpret_cast<const XrHapticBaseHeader*>(&v));
    }
}

void Session::rumble(float low, float high, Uint32 ms) {
    Input& in = impl_->input;
    if (in.pad) SDL_RumbleJoystick(in.pad, Uint16(std::clamp(low, 0.0f, 1.0f) * 65535), Uint16(std::clamp(high, 0.0f, 1.0f) * 65535), ms);
}

bool Session::pointer(float& u, float& v, bool& pressed) const {
    u = pointer_u_, v = pointer_v_, pressed = pointer_pressed_;
    return pointer_on_;
}

} // namespace gt3r::xr

#endif // GT3R_XR
