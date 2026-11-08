#include "input/input_manager.h"

#include "base/config.h"
#include "base/log.h"
#include "platform/device_tilt.h"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_sensor.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace gt3r {

namespace {

constexpr float kAxisMax = 32767.0f;
constexpr float kCaptureAxisDelta = 0.6f;
constexpr float kTiltRange = 0.785398f; // 45 degrees of roll = full lock

float normalize_axis(Sint16 v) { return std::clamp(static_cast<float>(v) / kAxisMax, -1.0f, 1.0f); }

std::string section_name(int player, const char* suffix = "") {
    return "player" + std::to_string(player + 1) + suffix;
}

// An analog source bound to a button presses it past kPressThreshold (a light touch of a stick or a
// trigger doesn't), a pedal on the race pad past kRacePedalPress: past its rest's noise, not the
// first 12 % of its travel (the pressure only counts while pressed; issue 134).
constexpr float kPressThreshold = 0.12f, kRacePedalPress = 0.02f;

float parse_range(const std::optional<std::string>& v, float fallback, float lo, float hi) {
    if (!v) return fallback;
    char* end = nullptr;
    const float f = std::strtof(v->c_str(), &end);
    return end == v->c_str() ? fallback : std::clamp(f, lo, hi);
}

float parse_curve(const std::optional<std::string>& v) {
    return parse_range(v, 1.0f, InputManager::kSteeringCurveMin, InputManager::kSteeringCurveMax);
}

float parse_deadzone(const std::optional<std::string>& v, float fallback) {
    return parse_range(v, fallback, 0.0f, InputManager::kSteeringDeadzoneMax);
}

std::string format_hundredths(float v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%.2f", double(v));
    return b;
}

bool contains_ci(const std::string& hay, const std::string& needle) {
    return to_lower(hay).find(to_lower(needle)) != std::string::npos;
}

// [playerN] race; before it existed, [race] buttons = gt3 | ours | auto (auto: GT3's layout
// with the classic preset) chose the race buttons for everyone.
std::string initial_race_layout(const IniDocument& cfg, const std::string& sec, const std::string& preset) {
    if (auto r = cfg.get(sec, "race")) return to_lower(*r) == "gt3" ? "gt3" : "modern";
    std::string b = to_lower(cfg.get("race", "buttons").value_or("auto"));
    return b == "gt3" || (b == "auto" && preset == "classic") ? "gt3" : "modern";
}

// The pad's layout, keyboard included: any_device_pad() reads every controller and the keyboard with it.
const Profile& pad_layout(bool vr_pad = false) {
    static const Profile prof = make_preset(pad_preset()), vr = make_preset("quest");
    return vr_pad ? vr : prof;
}

} // namespace

InputManager::InputManager() {
    for (int i = 0; i < kMaxPlayers; ++i) {
        players_[i].keyboard = (i == 0);
        players_[i].bindings.player2 = (i == 1); // player 2's keys apart from player 1's
    }
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&count)) {
        for (int i = 0; i < count; ++i) add_device(ids[i]);
        SDL_free(ids);
    }
    // Mobile devices: use the device's own accelerometer for tilt steering.
    if (SDL_SensorID* sensors = SDL_GetSensors(&count)) {
        for (int i = 0; i < count && !device_accel_; ++i)
            if (SDL_GetSensorTypeForID(sensors[i]) == SDL_SENSOR_ACCEL) device_accel_ = SDL_OpenSensor(sensors[i]);
        SDL_free(sensors);
    }
    assign_devices();
}

InputManager::~InputManager() {
    for (Device& d : devices_) {
        ffb_.stop(d.id);
        ffb_.forget(d.id); // haptics before their joystick
        if (d.gamepad) SDL_CloseGamepad(d.gamepad);
        else if (d.joystick) SDL_CloseJoystick(d.joystick);
    }
    if (device_accel_) SDL_CloseSensor(device_accel_);
    if (vibrator_) {
        SDL_CloseHaptic(vibrator_);
        SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    }
}

void InputManager::load(const IniDocument& cfg) {
    ffb_.configure(ForceFeedbackConfig::load(cfg));
    for (int i = 0; i < kMaxPlayers; ++i) {
        Player& p = players_[i];
        std::string sec = section_name(i);
        // Before the Controller choice, `preset` (racing / classic / wheel / keyboard, quest in VR
        // builds) chose the bindings and `device` the device; their one custom set was the preset's.
        const std::string preset = to_lower(cfg.get(sec, "preset").value_or("racing"));
        const std::string device = cfg.get(sec, "device").value_or("auto");
        const std::optional<std::string> controller = cfg.get(sec, "controller");
        p.bindings = PlayerBindings{};
        p.bindings.player2 = (i == 1);
        bool controller_inputs = false;
        if (controller) p.bindings.load(cfg, sec);
        else controller_inputs = p.bindings.load_old(cfg, sec);
        p.actions.load(cfg, sec);
        for (auto& d : p.chord_down) d.clear();
        p.bindings.race_layout = initial_race_layout(cfg, sec, preset);
        // keyboard: Keyboard only, unless its custom bindings had controller inputs (its device
        // stays). wheel with a named device: that device keeps the wheel layout whatever SDL says.
        p.device_filter = controller ? *controller
                          : preset == "keyboard" && !controller_inputs ? kKeyboardOnly : device;
        if (p.device_filter.empty()) p.device_filter = "auto";
        p.wheel_device = controller ? cfg.get(sec, "wheel_device").value_or("")
                         : preset == "wheel" && device != "auto" ? device : "";
        if (auto kb = cfg.get(sec, "keyboard")) p.keyboard = to_lower(*kb) == "on" || *kb == "1";
        if (i == 0) set_phone_vibration(to_lower(cfg.get(sec, "phone_vibration").value_or("off")) == "on");
        p.rumble_mode = to_lower(cfg.get(sec, "rumble").value_or("auto"));
        p.steering_curve = parse_curve(cfg.get(sec, "steering_curve"));
        p.wheel_steering_curve = parse_curve(cfg.get(sec, "wheel_steering_curve"));
        p.steering_deadzone = parse_deadzone(cfg.get(sec, "steering_deadzone"), kStickDeadzone);
        p.wheel_steering_deadzone = parse_deadzone(cfg.get(sec, "wheel_steering_deadzone"), 0.0f);
    }
    set_background(to_lower(cfg.get("input", "background").value_or("off")) == "on");
    xbox_rawinput_ = xbox_rawinput(cfg);
    assign_devices();
}

void InputManager::save(IniDocument& cfg) const {
    ffb_.config().save(cfg);
    for (int i = 0; i < kMaxPlayers; ++i) {
        const Player& p = players_[i];
        std::string sec = section_name(i);
        cfg.set(sec, "controller", p.device_filter);
        if (!p.wheel_device.empty()) cfg.set(sec, "wheel_device", p.wheel_device);
        // Older builds choose by these: the kind in use as their preset.
        const bool kb_only = keyboard_only(i);
        cfg.set(sec, "preset", kb_only ? "keyboard" : p.kind == DeviceKind::Wheel ? "wheel"
                               : pad_preset() == "quest" ? "quest" : "racing");
        cfg.set(sec, "device", kb_only ? "auto" : p.device_filter);
        cfg.set(sec, "keyboard", p.keyboard ? "on" : "off");
        if (i == 0) cfg.set(sec, "phone_vibration", phone_vibration_ ? "on" : "off");
        cfg.set(sec, "rumble", p.rumble_mode);
        cfg.set(sec, "steering_curve", format_hundredths(p.steering_curve));
        cfg.set(sec, "wheel_steering_curve", format_hundredths(p.wheel_steering_curve));
        cfg.set(sec, "steering_deadzone", format_hundredths(p.steering_deadzone));
        cfg.set(sec, "wheel_steering_deadzone", format_hundredths(p.wheel_steering_deadzone));
        cfg.set(sec, "race", p.bindings.race_layout);
        p.bindings.save(cfg, sec);
        p.actions.save(cfg, sec);
    }
    cfg.set("input", "background", background_ ? "on" : "off");
#ifdef SDL_PLATFORM_WINDOWS
    cfg.set("input", "xbox_driver", xbox_rawinput_ ? "rawinput" : "xinput");
#endif
}

bool InputManager::xbox_rawinput(const IniDocument& cfg) {
    return to_lower(cfg.get("input", "xbox_driver").value_or("rawinput")) != "xinput";
}

bool InputManager::is_custom(int player) const {
    const Player& p = players_[player];
    return p.bindings.custom(p.kind, false);
}

bool InputManager::is_race_custom(int player) const {
    const Player& p = players_[player];
    return p.bindings.custom(p.kind, true);
}

void InputManager::refresh(Player& p) {
    p.menu = p.bindings.combined(p.kind, false);
    p.race_reads_menu = p.bindings.race_reads_menu(p.kind);
    p.race = p.race_reads_menu ? Profile{} : p.bindings.combined(p.kind, true);
    p.ramp_values.clear(); // other bindings: their ramps start at rest
    p.race_ramps.clear();
}

bool InputManager::has_custom(int player, bool race) const {
    const Player& p = players_[player];
    return p.bindings.has_custom(p.kind, race);
}

void InputManager::use_custom(int player, bool race) {
    Player& p = players_[player];
    p.bindings.use_custom(race);
    refresh(p);
}

void InputManager::reset_bindings(int player) {
    Player& p = players_[player];
    p.bindings.reset(p.kind, false);
    refresh(p);
}

bool InputManager::wheel_source(const Player& p, const InputSource& source) const {
    if (source.kind != InputSource::Kind::JoyAxis && source.kind != InputSource::Kind::PadAxis) return false;
    const Device* d = source.device.empty() ? find_device(p.device) : find_tagged(source.device, p.device);
    return d && d->wheel;
}

bool InputManager::bind(int player, const PadTarget& target, const InputSource& source, bool replace, float rest) {
    Player& p = players_[player];
    if (!p.bindings.bind(p.kind, false, target, source, replace, {wheel_source(p, source), rest})) return false;
    if (source.device.empty()) refresh(p);
    else assign_devices(); // the other player's Auto can't keep the device this binding names
    return true;
}

void InputManager::clear_target(int player, const PadTarget& target, BindingSet set) {
    Player& p = players_[player];
    p.bindings.clear(p.kind, false, target, set);
    assign_devices(); // a device it named may be free again (refreshes every player)
}

void InputManager::set_race_layout(int player, std::string_view layout) {
    Player& p = players_[player];
    p.bindings.set_race_layout(p.kind, layout);
    refresh(p);
}

const Profile& InputManager::race_profile(int player) const {
    const Player& p = players_[player];
    return p.race_reads_menu ? p.menu : p.race;
}

bool InputManager::bind_race(int player, const PadTarget& target, const InputSource& source, bool replace,
                             float rest) {
    Player& p = players_[player];
    if (!p.bindings.bind(p.kind, true, target, source, replace, {wheel_source(p, source), rest})) return false;
    if (source.device.empty()) refresh(p);
    else assign_devices(); // the other player's Auto can't keep the device this binding names
    return true;
}

void InputManager::clear_race_target(int player, const PadTarget& target, BindingSet set) {
    Player& p = players_[player];
    p.bindings.clear(p.kind, true, target, set);
    assign_devices();
}

void InputManager::add_device(SDL_JoystickID id) {
    if (find_device(id)) return;
#if GT3R_XR
    // A Meta headset's own controllers and buttons, as Android gamepads: the Touch controllers
    // come through OpenXR instead (xr::Session's virtual gamepad).
    if (SDL_GetJoystickVendorForID(id) == 0x2833) {
        LOG_INFO("Input device ignored: %s (headset)", SDL_GetJoystickNameForID(id) ? SDL_GetJoystickNameForID(id) : "?");
        return;
    }
#endif
    Device d;
    d.id = id;
    if (SDL_IsGamepad(id)) {
        d.gamepad = SDL_OpenGamepad(id);
        d.joystick = d.gamepad ? SDL_GetGamepadJoystick(d.gamepad) : nullptr;
        if (d.gamepad && SDL_GamepadHasSensor(d.gamepad, SDL_SENSOR_ACCEL))
            d.has_accel = SDL_SetGamepadSensorEnabled(d.gamepad, SDL_SENSOR_ACCEL, true);
    } else {
        d.joystick = SDL_OpenJoystick(id);
    }
    if (!d.joystick) {
        LOG_WARN("Failed to open input device %u: %s", unsigned(id), SDL_GetError());
        return;
    }
    const char* name = d.gamepad ? SDL_GetGamepadName(d.gamepad) : SDL_GetJoystickName(d.joystick);
    d.name = name ? name : "Unknown device";
    const SDL_JoystickType type = SDL_GetJoystickTypeForID(id);
    d.listed_wheel = type == SDL_JOYSTICK_TYPE_WHEEL;
    d.logitech_pedals = !d.gamepad && logitech_driver_pedals(SDL_GetJoystickVendorForID(id), SDL_GetJoystickProductForID(id),
                                                             SDL_GetJoystickGUIDForID(id).data[14]);
    d.wheel = is_wheel(type, type == SDL_JOYSTICK_TYPE_UNKNOWN && ForceFeedback::has_wheel_forces(d.joystick));
#if GT3R_XR
    d.vr_pad = SDL_IsJoystickVirtual(id); // (the game makes no other)
#endif
    d.dualsense = d.gamepad && SDL_GetGamepadType(d.gamepad) == SDL_GAMEPAD_TYPE_PS5;
    if (const char* serial = SDL_GetJoystickSerial(d.joystick)) d.serial = serial;
    if (const char* path = SDL_GetJoystickPath(d.joystick)) d.path = path;
    LOG_INFO("Input device connected: %s (%s)", d.name.c_str(), d.gamepad ? "gamepad" : "joystick");
    devices_.push_back(std::move(d));
}

void InputManager::remove_device(SDL_JoystickID id) {
    auto it = std::find_if(devices_.begin(), devices_.end(), [&](const Device& d) { return d.id == id; });
    if (it == devices_.end()) return;
    LOG_INFO("Input device disconnected: %s", it->name.c_str());
    ffb_.forget(id); // closes its haptic before the joystick
    if (it->gamepad) SDL_CloseGamepad(it->gamepad);
    else SDL_CloseJoystick(it->joystick);
    devices_.erase(it);
}

const InputManager::Device* InputManager::find_device(SDL_JoystickID id) const {
    for (const Device& d : devices_)
        if (d.id == id) return &d;
    return nullptr;
}

void InputManager::assign_devices() {
    std::vector<SDL_JoystickID> taken;
    const std::vector<DeviceId> ids_now = device_ids();
    auto is_taken = [&](SDL_JoystickID id) { return std::find(taken.begin(), taken.end(), id) != taken.end(); };
    // Named filters first, so "auto" players don't steal a named device.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < kMaxPlayers; ++i) {
            Player& p = players_[i];
            bool is_auto = p.device_filter == "auto" || p.device_filter.empty();
            if ((pass == 0) == is_auto) continue;
            p.device = 0;
            if (is_auto) {
                // Devices other players' tagged bindings use aren't handed out either.
                std::vector<std::string> others;
                for (int j = 0; j < kMaxPlayers; ++j)
                    if (j != i)
                        for (const std::string& tag : players_[j].bindings.devices()) others.push_back(tag);
                const std::vector<bool> reserved = reserved_devices(ids_now, others);
                std::vector<SDL_JoystickID> ids; // the devices no other player has
                std::vector<AutoRank> ranks;
                for (size_t k = 0; k < devices_.size(); ++k) {
                    const Device& d = devices_[k];
                    if (is_taken(d.id) || reserved[k]) continue;
                    ids.push_back(d.id);
                    ranks.push_back(d.listed_wheel ? AutoRank::Wheel : d.wheel ? AutoRank::LikelyWheel
                                    : d.vr_pad     ? AutoRank::VrControllers
                                    : d.gamepad    ? AutoRank::Gamepad : AutoRank::Other);
                }
                if (int at = pick_auto_device(ranks); at >= 0) p.device = ids[size_t(at)];
            } else if (!keyboard_only(i)) {
                for (const Device& d : devices_)
                    if (!is_taken(d.id) && contains_ci(d.name, p.device_filter)) {
                        p.device = d.id;
                        break;
                    }
            }
            const Device* d = find_device(p.device);
            p.kind = device_kind(d != nullptr, d && d->gamepad, d && d->wheel,
                                 !is_auto && !p.wheel_device.empty() && p.device_filter == p.wheel_device);
            p.bindings.vr_pad = d && d->vr_pad;
            if (p.kind == DeviceKind::Wheel && d && d->wheel && !d->listed_wheel) p.bindings.adopt_joystick_sets();
            refresh(p);
            if (p.device) taken.push_back(p.device);
            if (d && d->gamepad) SDL_SetGamepadPlayerIndex(d->gamepad, i);
        }
    }
}

bool InputManager::keyboard_fallback(int player) const {
    const Player& p = players_[player];
    if (player != 0 || p.keyboard) return false;
    if (p.menu.keyboard_only()) return true;
    // The wheel layout's buttons are the usual numbers: a wheel SDL doesn't list may have others.
    const Device* d = find_device(p.device);
    return p.kind == DeviceKind::Wheel && d && !d->listed_wheel && !p.bindings.controller_custom(p.kind, false);
}

void InputManager::set_device_filter(int player, std::string filter) {
    players_[player].device_filter = filter.empty() ? "auto" : std::move(filter);
    assign_devices();
}

std::vector<DeviceId> InputManager::device_ids() const {
    std::vector<DeviceId> out;
    for (const Device& d : devices_) out.push_back({d.name, d.serial, d.path});
    return out;
}

std::vector<std::string> InputManager::device_names() const {
    std::vector<std::string> out;
    for (const Device& d : devices_) out.push_back(d.name);
    return out;
}

std::string InputManager::device_name(int player) const {
    if (const Device* d = find_device(players_[player].device)) return d->name;
    return keyboard_active(player) ? "Keyboard" : "No device";
}

const InputManager::Device* InputManager::find_tagged(const std::string& tag, SDL_JoystickID controller) const {
    int own = -1;
    for (size_t i = 0; i < devices_.size(); ++i)
        if (controller && devices_[i].id == controller) own = int(i);
    const int at = resolve_device_tag(device_ids(), tag, own);
    return at < 0 ? nullptr : &devices_[size_t(at)];
}

std::string InputManager::tag_of(SDL_JoystickID id) const {
    for (size_t i = 0; i < devices_.size(); ++i)
        if (devices_[i].id == id) return device_tag(device_ids(), i);
    return {};
}

std::string InputManager::capture_tag(SDL_JoystickID id) const {
    return id == players_[capture_player_].device ? std::string() : tag_of(id);
}

const InputManager::Device* InputManager::tagged_steering(const Player& p) const {
    for (const Binding& b : (p.race_reads_menu ? p.menu : p.race).bindings)
        if (b.target.kind == PadTarget::Kind::Axis && b.target.axis == PadAxis::LX && b.source.is_analog() &&
            !b.source.device.empty())
            if (const Device* d = find_tagged(b.source.device, p.device); d && d->wheel) return d;
    return nullptr;
}

bool InputManager::wheel_steering(int player) const { return wheel_steering(players_[player]); }

bool InputManager::wheel_steering(const Player& p) const {
    if (tagged_steering(p) || p.kind == DeviceKind::Wheel) return true;
    const Device* d = find_device(p.device);
    return d && d->wheel; // one that maps as a gamepad: the pad's bindings
}

float InputManager::steering_curve(const Player& p) const {
    return wheel_steering(p) ? p.wheel_steering_curve : p.steering_curve;
}

float InputManager::steering_deadzone(const Player& p) const {
    return wheel_steering(p) ? p.wheel_steering_deadzone : p.steering_deadzone;
}

void InputManager::set_steering_deadzone(int player, float deadzone) {
    Player& p = players_[player];
    deadzone = std::clamp(std::round(deadzone * 100.0f) / 100.0f, 0.0f, kSteeringDeadzoneMax);
    (wheel_steering(p) ? p.wheel_steering_deadzone : p.steering_deadzone) = deadzone;
}

void InputManager::set_steering_curve(int player, float curve) {
    Player& p = players_[player];
    curve = std::clamp(std::round(curve * 100.0f) / 100.0f, kSteeringCurveMin, kSteeringCurveMax);
    (wheel_steering(p) ? p.wheel_steering_curve : p.steering_curve) = curve;
}

float InputManager::curved_steering(float x, float curve) {
    if (curve == 1.0f) return x;
    constexpr float a = 0.1f;
    const float base = std::pow(a, curve);
    return std::copysign((std::pow(std::fabs(x) + a, curve) - base) / (std::pow(1.0f + a, curve) - base), x);
}

std::string InputManager::describe(int player, const InputSource& src) const {
    // Another device's input names it first ("Wireless Controller 2: Right trigger").
    const Device* d = find_device(players_[player].device);
    std::string prefix;
    InputSource own = src;
    if (!src.device.empty()) {
        d = find_tagged(src.device);
        prefix = device_tag_label(device_ids(), src.device);
        if (prefix.size() > 24) prefix = prefix.substr(0, 22) + "..";
        prefix += ": ";
        own.device.clear();
    }
    if (std::string_view paddle = own.paddle_name(); !paddle.empty()) return prefix + std::string(paddle);
    const char* half = own.half == InputSource::Half::Positive ? " +" : own.half == InputSource::Half::Negative ? " -" : "";
    switch (own.kind) {
    case InputSource::Kind::Key: return std::string("Key ") + SDL_GetScancodeName(static_cast<SDL_Scancode>(own.code));
    case InputSource::Kind::PadAxis: {
        static const char* const kAxes[] = {"Left stick X", "Left stick Y", "Right stick X", "Right stick Y",
                                            "Left trigger", "Right trigger"};
        if (own.code >= 0 && own.code < 6) return prefix + kAxes[own.code] + half;
        break;
    }
    case InputSource::Kind::JoyAxis: return prefix + "Axis " + std::to_string(own.code) + half;
    case InputSource::Kind::JoyButton: return prefix + "Button " + std::to_string(own.code);
    case InputSource::Kind::PadButton:
        if (!d || !d->gamepad) break;
        switch (SDL_GetGamepadButtonLabel(d->gamepad, static_cast<SDL_GamepadButton>(own.code))) {
        case SDL_GAMEPAD_BUTTON_LABEL_A: return prefix + "A";
        case SDL_GAMEPAD_BUTTON_LABEL_B: return prefix + "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X: return prefix + "X";
        case SDL_GAMEPAD_BUTTON_LABEL_Y: return prefix + "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return prefix + "Cross";
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return prefix + "Circle";
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return prefix + "Square";
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return prefix + "Triangle";
        default: break;
        }
        break;
    default: break;
    }
    return prefix + own.to_string();
}

void InputManager::handle_event(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_JOYSTICK_ADDED:
        add_device(e.jdevice.which);
        assign_devices();
        break;
    case SDL_EVENT_JOYSTICK_REMOVED:
        remove_device(e.jdevice.which);
        assign_devices();
        break;
    case SDL_EVENT_KEY_DOWN:
        if (size_t(e.key.scancode) < size_t(SDL_SCANCODE_COUNT)) {
            if (key_latch_.empty()) key_latch_.assign(SDL_SCANCODE_COUNT, false);
            key_latch_[size_t(e.key.scancode)] = true;
        }
        if (capturing_ && !e.key.repeat) {
            // Esc and Android Back cancel the capture (a chord's before anything else is held).
            const bool back = e.key.scancode == SDL_SCANCODE_ESCAPE || e.key.scancode == SDL_SCANCODE_AC_BACK;
            if (chord_capture_) {
                if (!back) add_chord_input(InputSource{InputSource::Kind::Key, int(e.key.scancode)});
                else if (chord_inputs_.empty()) capturing_ = chord_capture_ = false;
                break;
            }
            if (!back) captured_ = InputSource{InputSource::Kind::Key, int(e.key.scancode)};
            capturing_ = false;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (capturing_) {
            InputSource s{InputSource::Kind::PadButton, int(e.gbutton.button)};
            s.device = capture_tag(e.gbutton.which);
            if (chord_capture_) {
                add_chord_input(s);
                break;
            }
            captured_ = s;
            capturing_ = false;
        }
        break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        // Gamepads report through the gamepad event above.
        if (capturing_ && !SDL_IsGamepad(e.jbutton.which)) {
            InputSource s{InputSource::Kind::JoyButton, int(e.jbutton.button)};
            s.device = capture_tag(e.jbutton.which);
            if (chord_capture_) {
                add_chord_input(s);
                break;
            }
            captured_ = s;
            capturing_ = false;
        }
        break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        if (capturing_ && !SDL_IsGamepad(e.jhat.which) && e.jhat.value != SDL_HAT_CENTERED) {
            u8 v = e.jhat.value;
            u8 dir = (v & SDL_HAT_UP) ? SDL_HAT_UP : (v & SDL_HAT_RIGHT) ? SDL_HAT_RIGHT
                   : (v & SDL_HAT_DOWN) ? SDL_HAT_DOWN : SDL_HAT_LEFT;
            InputSource s{InputSource::Kind::JoyHat, int(e.jhat.hat), dir};
            s.device = capture_tag(e.jhat.which);
            if (chord_capture_) {
                add_chord_input(s);
                break;
            }
            captured_ = s;
            capturing_ = false;
        }
        break;
    default: break;
    }
}

void InputManager::begin_chord_capture(int player) {
    begin_capture(player);
    chord_capture_ = true;
}

std::optional<Chord> InputManager::poll_chord() { return std::exchange(captured_chord_, std::nullopt); }

void InputManager::add_chord_input(const InputSource& s) {
    if (chord_inputs_.size() >= Chord::kMaxInputs) return;
    if (!chord_inputs_.empty() && binding_set(s) != binding_set(chord_inputs_.front())) return; // one set
    if (std::find(chord_inputs_.begin(), chord_inputs_.end(), s) == chord_inputs_.end()) chord_inputs_.push_back(s);
}

void InputManager::update_chord_capture() {
    // Axes: each moved past half way from where it rested (a trigger, a stick direction, a pedal).
    for (const auto& [id, base] : capture_baseline_) {
        const Device* d = find_device(id);
        if (!d) continue;
        for (size_t a = 0; a < base.size(); ++a) {
            const float v = d->gamepad ? normalize_axis(SDL_GetGamepadAxis(d->gamepad, SDL_GamepadAxis(a)))
                                       : normalize_axis(SDL_GetJoystickAxis(d->joystick, sdl_axis(*d, int(a))));
            const float delta = v - base[a];
            if (std::fabs(delta) < 0.5f) continue;
            InputSource s;
            s.kind = d->gamepad ? InputSource::Kind::PadAxis : InputSource::Kind::JoyAxis;
            s.code = int(a);
            const bool trigger = d->gamepad && (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
            s.half = trigger ? InputSource::Half::Full : delta > 0 ? InputSource::Half::Positive : InputSource::Half::Negative;
            s.device = capture_tag(id);
            if (chord_input_held(s, v)) add_chord_input(s);
        }
    }
    if (chord_inputs_.empty()) return;
    // Done when every input is let go.
    const Player& p = players_[capture_player_];
    const Device* dev = find_device(p.device);
    for (const InputSource& s : chord_inputs_)
        if (std::optional<float> v = read_source(p, dev, true, s); v && chord_input_held(s, *v)) return;
    captured_chord_ = Chord{chord_inputs_};
    chord_inputs_.clear();
    capturing_ = chord_capture_ = false;
}

void InputManager::evaluate_actions(Player& p, bool keyboard) {
    const Device* dev = find_device(p.device);
    p.actions_held = p.actions_pressed = 0;
    for (size_t a = 0; a < kPadActionCount; ++a) {
        const std::vector<Chord>& chords = p.actions.chords[a];
        std::vector<bool>& was = p.chord_down[a];
        was.resize(chords.size(), true); // a chord bound while held fires on its next press
        for (size_t c = 0; c < chords.size(); ++c) {
            bool down = !chords[c].inputs.empty();
            for (const InputSource& s : chords[c].inputs) {
                std::optional<float> v = read_source(p, dev, keyboard, s);
                down = down && v && chord_input_held(s, *v);
            }
            if (down) p.actions_held = u8(p.actions_held | (1u << a));
            if (down && !was[c]) p.actions_pressed = u8(p.actions_pressed | (1u << a));
            was[c] = down;
        }
    }
}

std::string InputManager::describe(int player, const Chord& c) const {
    std::string out;
    for (const InputSource& s : c.inputs) out += (out.empty() ? "" : " + ") + describe(player, s);
    return out;
}

void InputManager::begin_capture(int player) {
    capture_player_ = std::clamp(player, 0, kMaxPlayers - 1);
    capturing_ = true;
    chord_capture_ = false;
    chord_inputs_.clear();
    captured_chord_.reset();
    captured_.reset();
    captured_rest_ = 0.0f;
    // Record resting positions so pedals that rest at +/-1 and slightly
    // off-center sticks don't register as input.
    capture_baseline_.clear();
    for (const Device& d : devices_) {
        std::vector<float> axes;
        if (d.gamepad)
            for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
                axes.push_back(normalize_axis(SDL_GetGamepadAxis(d.gamepad, SDL_GamepadAxis(a))));
        else
            for (int a = 0; a < SDL_GetNumJoystickAxes(d.joystick); ++a)
                axes.push_back(normalize_axis(SDL_GetJoystickAxis(d.joystick, sdl_axis(d, a))));
        capture_baseline_.emplace_back(d.id, std::move(axes));
    }
}

std::optional<InputManager::CapturedInput> InputManager::poll_capture() {
    if (!captured_) return std::nullopt;
    CapturedInput out{*captured_, captured_rest_};
    captured_.reset();
    return out;
}

void InputManager::update_tilt(Player& p, const Device* dev, double dt) {
    float accel[3] = {0, 0, 0};
    bool ok = false;
    if (dev && dev->has_accel) {
        ok = SDL_GetGamepadSensorData(dev->gamepad, SDL_SENSOR_ACCEL, accel, 3);
    } else if (device_accel_ && SDL_GetSensorData(device_accel_, accel, 3)) {
        // Phone/tablet: the sensor doesn't rotate with the display.
        float raw[3] = {accel[0], accel[1], accel[2]};
        platform::accel_to_display(raw, accel, platform::display_rotation_degrees());
        ok = true;
    }
    if (!ok) return;
    // Roll around the axis pointing at the player (clockwise = right), smoothed:
    // unfiltered accelerometer jitter trembles the steering and the meter.
    float roll = p.tilt_filter.filter(platform::steering_roll(accel), dt);
    p.tilt_roll = roll;
    p.tilt = std::clamp(roll / kTiltRange, -1.0f, 1.0f);
}

std::optional<float> InputManager::read_source(const Player& p, const Device* dev, bool keyboard,
                                               const InputSource& src) const {
    if (!src.device.empty() && src.kind != InputSource::Kind::Key && src.kind != InputSource::Kind::Mouse &&
        src.kind != InputSource::Kind::Tilt) {
        dev = find_tagged(src.device, p.device); // another device than the Controller; not connected: nothing
        if (!dev) return std::nullopt;
    }
    switch (src.kind) {
    case InputSource::Kind::None: return std::nullopt;
    case InputSource::Kind::Key: {
        if (!keyboard) return std::nullopt;
        int n = 0;
        const bool* keys = SDL_GetKeyboardState(&n);
        bool latched = size_t(src.code) < key_latch_.size() && key_latch_[size_t(src.code)];
        return ((src.code < n && keys[src.code]) || latched) ? 1.0f : 0.0f;
    }
    case InputSource::Kind::Mouse:
        if (!keyboard) return std::nullopt;
        return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_MASK(src.code)) ? 1.0f : 0.0f;
    case InputSource::Kind::PadButton:
        if (!dev || !dev->gamepad || !SDL_GamepadHasButton(dev->gamepad, SDL_GamepadButton(src.code))) return std::nullopt;
        return SDL_GetGamepadButton(dev->gamepad, SDL_GamepadButton(src.code)) ? 1.0f : 0.0f;
    case InputSource::Kind::PadAxis:
        if (!dev || !dev->gamepad || !SDL_GamepadHasAxis(dev->gamepad, SDL_GamepadAxis(src.code))) return std::nullopt;
        return normalize_axis(SDL_GetGamepadAxis(dev->gamepad, SDL_GamepadAxis(src.code)));
    case InputSource::Kind::JoyButton:
        if (!dev || src.code >= SDL_GetNumJoystickButtons(dev->joystick)) return std::nullopt;
        return SDL_GetJoystickButton(dev->joystick, src.code) ? 1.0f : 0.0f;
    case InputSource::Kind::JoyAxis:
        if (!dev || src.code >= SDL_GetNumJoystickAxes(dev->joystick)) return std::nullopt;
        return normalize_axis(SDL_GetJoystickAxis(dev->joystick, sdl_axis(*dev, src.code)));
    case InputSource::Kind::JoyHat:
        if (!dev || src.code >= SDL_GetNumJoystickHats(dev->joystick)) return std::nullopt;
        return (SDL_GetJoystickHat(dev->joystick, src.code) & src.hat_dir) ? 1.0f : 0.0f;
    case InputSource::Kind::Tilt: return p.tilt;
    }
    return std::nullopt;
}

void InputManager::note_moved_axes() {
    constexpr int kMoved = 1638; // 5 % of the way to the axis' end
    for (Device& d : devices_) {
        const int n = std::min(SDL_GetNumJoystickAxes(d.joystick), 32);
        for (int a = 0; a < n; ++a) {
            Sint16 rest = 0; // stays 0 until the axis first reports
            SDL_GetJoystickAxisInitialState(d.joystick, sdl_axis(d, a), &rest);
            if (std::abs(int(SDL_GetJoystickAxis(d.joystick, sdl_axis(d, a))) - rest) > kMoved) d.moved_axes |= 1u << a;
        }
    }
}

// A `pedal` binding on an axis that has sat in the middle since it connected: nothing is there, not
// a pedal half way down. The wheel layout's pedals are axes 1 and 2, and a wheel base can have
// something else there (a Simucube's Y axis idles at the centre: gas and Cross held half way).
// Until it moves: some pedals report the middle until they are first pressed.
bool InputManager::idle_centred(const Player& p, const Device* dev, const InputSource& source) const {
    if (source.kind != InputSource::Kind::JoyAxis || source.code >= 32) return false;
    if (!source.device.empty()) dev = find_tagged(source.device, p.device);
    if (!dev || (dev->moved_axes >> source.code & 1)) return false;
    Sint16 rest = 0; // stays 0 until the axis first reports
    SDL_GetJoystickAxisInitialState(dev->joystick, sdl_axis(*dev, source.code), &rest);
    return std::fabs(normalize_axis(rest)) < 0.5f;
}

void InputManager::evaluate(Player& p, double dt) {
    const Device* dev = find_device(p.device);
    const bool keyboard = keyboard_active(int(&p - players_.data()));
    update_tilt(p, dev, dt);
    evaluate_profile(p, dev, keyboard, p.menu, p.ramp_values, p.state, dt);
    evaluate_profile(p, dev, keyboard, p.race_reads_menu ? p.menu : p.race, p.race_ramps, p.race_state, dt, true);
    // The steering curve: the race's steering only (the menus read the stick as it is).
    if (p.steering_curve != 1.0f || p.wheel_steering_curve != 1.0f)
        if (const float c = steering_curve(p); c != 1.0f)
            p.race_state.set_axis(PadAxis::LX, curved_steering(p.race_state.axis_fine(PadAxis::LX), c));
}

void InputManager::evaluate_any_device(double dt) {
    static const Player none{}; // no overlay, no tilt
    std::vector<PadState> pads = {players_[0].state};
    PadState s;
    evaluate_profile(none, nullptr, true, pad_layout(), any_ramps_, s, dt); // the keyboard
    pads.push_back(s);
    std::vector<float> ramps; // the controller bindings have none
    for (const Device& d : devices_) {
        if (!d.gamepad) continue;
        evaluate_profile(none, &d, false, pad_layout(d.vr_pad), ramps, s, dt);
        pads.push_back(s);
    }
    any_pad_ = merge_pads(pads);
}

void InputManager::evaluate_profile(const Player& p, const Device* dev, bool keyboard, const Profile& prof,
                                    std::vector<float>& ramps, PadState& s, double dt, bool race) const {
    float button_val[kPadButtonCount] = {};
    bool button_digital[kPadButtonCount] = {};
    float axis_val[kPadAxisCount] = {};
    if (ramps.size() != prof.bindings.size()) ramps.assign(prof.bindings.size(), 0.0f);
    const float steer_deadzone = race ? steering_deadzone(p) : 0.0f;

    for (size_t bi = 0; bi < prof.bindings.size(); ++bi) {
        const Binding& b = prof.bindings[bi];
        const BindingOptions& o = b.options;
        // A source that isn't there is left out, not read as 0: `pedal` and `invert` would
        // turn that 0 into a press (the wheel preset with no wheel held Cross and Square).
        std::optional<float> read = read_source(p, dev, keyboard, b.source);
        if (!read || (o.pedal && idle_centred(p, dev, b.source))) {
            ramps[bi] = 0.0f;
            continue;
        }
        float v = *read;

        if (b.source.is_analog()) {
            if (o.invert) v = -v;
            if (o.pedal) v = (v + 1.0f) * 0.5f;
            else if (b.source.half == InputSource::Half::Positive) v = std::max(v, 0.0f);
            else if (b.source.half == InputSource::Half::Negative) v = std::max(-v, 0.0f);
            // Radial shaping on magnitude, sign preserved.
            const bool steer = race && b.target.kind == PadTarget::Kind::Axis && b.target.axis == PadAxis::LX &&
                               b.source.kind != InputSource::Kind::Tilt && !o.pedal;
            const float deadzone = steer ? steer_deadzone : o.deadzone;
            float m = std::fabs(v);
            float range = std::max(o.saturation - deadzone, 1e-4f);
            m = m <= deadzone ? 0.0f : std::min((m - deadzone) / range, 1.0f);
            if (o.curve != 1.0f) m = std::pow(m, o.curve);
            v = std::copysign(m, v);
        } else if (o.invert) {
            v = 1.0f - v;
        }
        v *= o.scale;

        if (o.ramp > 0.0f) {
            float& cur = ramps[bi];
            float rate = static_cast<float>(dt) / o.ramp;
            cur = v > cur ? std::min(v, cur + rate) : std::max(v, cur - rate * 2.0f); // release faster
            v = cur;
        }

        if (b.target.kind == PadTarget::Kind::Button) {
            size_t i = static_cast<size_t>(b.target.button);
            float bv = std::clamp(v, 0.0f, 1.0f);
            button_val[i] = std::max(button_val[i], bv);
            float thr = o.threshold > 0.0f   ? o.threshold
                        : !b.source.is_analog() ? 0.0f
                        : race && o.pedal       ? kRacePedalPress
                                                : kPressThreshold;
            if (bv > thr && (b.source.is_analog() || o.ramp > 0.0f || bv >= 1.0f)) button_digital[i] = true;
        } else if (b.target.kind == PadTarget::Kind::Axis) {
            size_t i = static_cast<size_t>(b.target.axis);
            if (b.target.half == InputSource::Half::Negative) v = -std::fabs(v);
            else if (b.target.half == InputSource::Half::Positive) v = std::fabs(v);
            axis_val[i] += v;
        }
    }

    if (const PadState& ov = p.overlay; ov.connected) {
        for (size_t i = 0; i < kPadButtonCount; ++i) {
            if (!(ov.buttons & (1u << i))) continue;
            button_digital[i] = true;
            button_val[i] = std::max(button_val[i], float(ov.pressure[i]) / 255.0f);
        }
        for (size_t i = 0; i < kPadAxisCount; ++i) axis_val[i] += ov.axis_fine(static_cast<PadAxis>(i));
    }

    s.connected = dev != nullptr || p.keyboard || p.overlay.connected;
    s.buttons = 0;
    for (size_t i = 0; i < kPadButtonCount; ++i) {
        bool pressed = button_digital[i];
        if (pressed) s.buttons |= u16(1u << i);
        if (is_pressure_sensitive(static_cast<PadButton>(i)))
            s.pressure[i] = pressed ? u8(std::max(1.0f, std::round(button_val[i] * 255.0f))) : 0;
        else
            s.pressure[i] = pressed ? 255 : 0;
    }
    for (size_t i = 0; i < kPadAxisCount; ++i) s.set_axis(static_cast<PadAxis>(i), axis_val[i]);
}

void InputManager::update(double dt) {
    if (capturing_ && chord_capture_) {
        update_chord_capture();
    } else if (capturing_) {
        // Analog capture: the axis that moved furthest from its resting value.
        float best = kCaptureAxisDelta;
        for (const auto& [id, base] : capture_baseline_) {
            const Device* d = find_device(id);
            if (!d) continue;
            for (size_t a = 0; a < base.size(); ++a) {
                float v = d->gamepad ? normalize_axis(SDL_GetGamepadAxis(d->gamepad, SDL_GamepadAxis(a)))
                                     : normalize_axis(SDL_GetJoystickAxis(d->joystick, sdl_axis(*d, int(a))));
                float delta = v - base[a];
                if (std::fabs(delta) <= best) continue;
                best = std::fabs(delta);
                InputSource s;
                s.kind = d->gamepad ? InputSource::Kind::PadAxis : InputSource::Kind::JoyAxis;
                s.code = int(a);
                bool trigger = d->gamepad && (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
                s.half = trigger ? InputSource::Half::Full
                                 : (delta > 0 ? InputSource::Half::Positive : InputSource::Half::Negative);
                s.device = capture_tag(id);
                captured_ = s;
                captured_rest_ = base[a];
            }
        }
        if (captured_) capturing_ = false;
    }
    note_moved_axes();
    for (Player& p : players_) {
        evaluate(p, dt);
        if (capturing_) p.actions_held = p.actions_pressed = 0; // the capture takes every input
        else evaluate_actions(p, keyboard_active(int(&p - players_.data())));
    }
    evaluate_any_device(dt);
    {
        const Device* d = find_device(players_[0].device);
        menu_rescue_ = !menus_drivable(players_[0].menu, d && d->gamepad, d && !d->gamepad,
                                       keyboard_active(0) && SDL_HasKeyboard());
    }
    std::fill(key_latch_.begin(), key_latch_.end(), false);

    time_ += dt;
    for (Player& p : players_) {
        // The Controller, and a wheel Steer is bound to on another device (its forces).
        const Device* steer = tagged_steering(p);
        for (const Device* d : {find_device(p.device), steer}) {
            if (!d || (d == steer && d->id == p.device)) continue;
            if (p.ffb_age < 0.25)
                ffb_.apply(d->id, d->gamepad, d->joystick, d == steer ? -1 : pedal_trigger(p, PadButton::Square),
                           d == steer ? -1 : pedal_trigger(p, PadButton::Cross), p.ffb, time_);
            else ffb_.stop(d->id);
        }
        p.ffb_age += dt;
    }
}

void InputManager::set_force_feedback(int player, const ForceFeedbackState& state) {
    players_[player].ffb = state;
    players_[player].ffb_age = 0;
}

int InputManager::pedal_trigger(const Player& p, PadButton pedal) const {
    for (const Binding& b : (p.race_reads_menu ? p.menu : p.race).bindings)
        if (b.target.kind == PadTarget::Kind::Button && b.target.button == pedal &&
            b.source.kind == InputSource::Kind::PadAxis && b.source.device.empty() &&
            (b.source.code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || b.source.code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
            return b.source.code;
    return -1;
}

bool use_phone_vibrator(bool enabled, int player, bool device_rumbles) {
    return enabled && player == 0 && !device_rumbles;
}

float phone_vibration_strength(const PadRumble& r) { return std::max(r.large, r.small) / 255.0f; }

PadRumble dualsense_rumble(const PadRumble& r) {
    PadRumble out = r;
    out.small = u8(std::min(255.0f, r.small * 1.75f));
    return out;
}

bool gt3_rumble(std::string_view mode) { return mode == "gt3"; }

bool InputManager::dualsense(int player) const {
    const Device* d = find_device(players_[player].device);
    return d && d->dualsense;
}

std::vector<bool> reserved_devices(const std::vector<DeviceId>& devices, const std::vector<std::string>& tags) {
    std::vector<bool> out(devices.size(), false);
    for (const std::string& tag : tags)
        if (int at = find_device_tag(devices, tag); at >= 0) out[size_t(at)] = true;
    return out;
}

bool is_wheel(SDL_JoystickType type, bool wheel_forces) {
    return type == SDL_JOYSTICK_TYPE_WHEEL || (type == SDL_JOYSTICK_TYPE_UNKNOWN && wheel_forces);
}

bool logitech_driver_pedals(u16 vendor, u16 product, u8 driver) {
    // G29 (native mode, which the driver switches it to), G27, G25: SDL_hidapi_lg4ff.c's axes.
    return vendor == 0x046d && (product == 0xc24f || product == 0xc29b || product == 0xc299) && driver == 'h';
}

int logitech_driver_axis(int axis) {
    switch (axis) {
    case 1: return 2; // gas
    case 2: return 3; // brake
    case 3: return 1; // clutch
    default: return axis;
    }
}

DeviceKind device_kind(bool connected, bool gamepad, bool wheel, bool named_wheel) {
    if (!connected) return DeviceKind::Keyboard;
    if (gamepad) return DeviceKind::Pad;
    return wheel || named_wheel ? DeviceKind::Wheel : DeviceKind::Joystick;
}

bool menus_drivable(const Profile& menu, bool pad, bool joystick, bool keys) {
    auto readable = [&](const InputSource& s) {
        if (!s.device.empty()) return false; // another device's: not counted
        switch (s.kind) {
        case InputSource::Kind::Key:
        case InputSource::Kind::Mouse: return keys;
        case InputSource::Kind::PadButton:
        case InputSource::Kind::PadAxis: return pad;
        case InputSource::Kind::JoyButton:
        case InputSource::Kind::JoyAxis:
        case InputSource::Kind::JoyHat: return joystick;
        default: return false;
        }
    };
    auto bound = [&](auto is_target) {
        return std::any_of(menu.bindings.begin(), menu.bindings.end(),
                           [&](const Binding& b) { return is_target(b.target) && readable(b.source); });
    };
    const bool confirm = bound([](const PadTarget& t) { return t.kind == PadTarget::Kind::Button && t.button == PadButton::Cross; });
    const bool move = bound([](const PadTarget& t) {
        return t.kind == PadTarget::Kind::Button ? t.button == PadButton::Up || t.button == PadButton::Down
                                                 : t.axis == PadAxis::LY;
    });
    return confirm && move;
}

int pick_auto_device(const std::vector<AutoRank>& devices) {
    for (AutoRank rank : {AutoRank::Wheel, AutoRank::LikelyWheel, AutoRank::Gamepad, AutoRank::VrControllers, AutoRank::Other})
        if (auto it = std::find(devices.begin(), devices.end(), rank); it != devices.end())
            return int(it - devices.begin());
    return -1;
}

PadState merge_pads(const std::vector<PadState>& pads) {
    PadState out;
    for (const PadState& p : pads) {
        out.connected |= p.connected;
        out.buttons |= p.buttons;
        for (size_t i = 0; i < kPadButtonCount; ++i) out.pressure[i] = std::max(out.pressure[i], p.pressure[i]);
        for (size_t i = 0; i < kPadAxisCount; ++i) {
            const PadAxis a = static_cast<PadAxis>(i);
            if (std::fabs(p.axis_fine(a)) > std::fabs(out.axis_fine(a))) out.axes[i] = p.axes[i], out.fine[i] = p.fine[i];
        }
    }
    return out;
}

void InputManager::set_rumble(int player, const PadRumble& r) {
    Player& p = players_[player];
    p.rumble = r;
    const Device* d = find_device(p.device);
    // Android reports a vibrator on the controller's input device as the rumble capability.
    const bool rumbles = d && SDL_GetBooleanProperty(SDL_GetJoystickProperties(d->joystick),
                                                     SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false);
    if (player == 0)
        vibrate_phone(use_phone_vibrator(phone_vibration_, player, rumbles) ? phone_vibration_strength(r) : 0.0f);
    if (!d) return;
    // Large motor -> low-frequency rumble (the left grip), small -> high frequency (the right grip).
    const PadRumble m = d->dualsense ? dualsense_rumble(r) : r;
    Uint16 low = Uint16(m.large * 257);
    Uint16 high = Uint16(m.small * 257);
    // None to a wheel whose force effects are ours (ForceFeedback::drives_wheel): it would reset them.
    if (d->gamepad) {
        const bool sent = SDL_RumbleGamepad(d->gamepad, low, high, 250);
        ffb_.rumbling(d->id, sent ? m.large : 0, sent ? m.small : 0); // for the DualSense's trigger packets
    } else if (!ffb_.drives_wheel(d->id)) SDL_RumbleJoystick(d->joystick, low, high, 250);
}

bool InputManager::phone_vibration_available() {
#if defined(SDL_PLATFORM_ANDROID)
    return true;
#else
    return false;
#endif
}

void InputManager::set_phone_vibration(bool on) {
    if (!on) vibrate_phone(0);
    phone_vibration_ = on;
}

void InputManager::set_background(bool on) {
    background_ = on;
    apply_background();
}

void InputManager::hold_background(bool on) {
    background_held_ = on;
    apply_background();
}

void InputManager::apply_background() const {
#if !GT3R_XR // (always on there: App::init)
    if (background_ || background_held_) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    else SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
#endif
}

void InputManager::vibrate_phone(float strength) {
    if (strength <= 0) {
        if (vibrator_ && vibrator_level_ > 0) SDL_StopHapticRumble(vibrator_);
        vibrator_level_ = 0;
        return;
    }
    if (!vibrator_ && !vibrator_tried_ && phone_vibration_available()) {
        vibrator_tried_ = true;
        // SDL's Android backend lists the system vibrator as a haptic device of this name.
        if (SDL_InitSubSystem(SDL_INIT_HAPTIC)) {
            int count = 0;
            if (SDL_HapticID* ids = SDL_GetHaptics(&count)) {
                for (int i = 0; i < count && !vibrator_; ++i) {
                    const char* name = SDL_GetHapticNameForID(ids[i]);
                    if (name && std::string_view(name) == "VIBRATOR_SERVICE") vibrator_ = SDL_OpenHaptic(ids[i]);
                }
                SDL_free(ids);
            }
            if (vibrator_ && !SDL_InitHapticRumble(vibrator_)) {
                SDL_CloseHaptic(vibrator_);
                vibrator_ = nullptr;
            }
            if (!vibrator_) SDL_QuitSubSystem(SDL_INIT_HAPTIC);
        }
        LOG_INFO("Phone vibration: %s", vibrator_ ? "device vibrator" : "no vibrator found");
    }
    if (!vibrator_) return;
    // The race asks every frame: send on a change, and again before the last one runs out.
    if (std::fabs(strength - vibrator_level_) > 0.1f || time_ - vibrator_sent_ > 0.2) {
        SDL_PlayHapticRumble(vibrator_, strength, 300);
        vibrator_level_ = strength;
        vibrator_sent_ = time_;
    }
}

} // namespace gt3r
