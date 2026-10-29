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

namespace gt3r {

namespace {

constexpr float kAxisMax = 32767.0f;
constexpr float kCaptureAxisDelta = 0.6f;
constexpr float kTiltRange = 0.785398f; // 45 degrees of roll = full lock

float normalize_axis(Sint16 v) { return std::clamp(static_cast<float>(v) / kAxisMax, -1.0f, 1.0f); }

std::string section_name(int player, const char* suffix = "") {
    return "player" + std::to_string(player + 1) + suffix;
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
const Profile& pad_layout() {
    static const Profile prof = make_preset(pad_preset());
    return prof;
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
    }
    set_background(to_lower(cfg.get("input", "background").value_or("off")) == "on");
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
        cfg.set(sec, "race", p.bindings.race_layout);
        p.bindings.save(cfg, sec);
    }
    cfg.set("input", "background", background_ ? "on" : "off");
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

bool InputManager::bind(int player, const PadTarget& target, const InputSource& source, bool replace) {
    Player& p = players_[player];
    if (!p.bindings.bind(p.kind, false, target, source, replace)) return false;
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

bool InputManager::bind_race(int player, const PadTarget& target, const InputSource& source, bool replace) {
    Player& p = players_[player];
    if (!p.bindings.bind(p.kind, true, target, source, replace)) return false;
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
    d.wheel = SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_WHEEL;
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
                    ranks.push_back(d.wheel ? AutoRank::Wheel : d.gamepad ? AutoRank::Gamepad : AutoRank::Other);
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
            refresh(p);
            if (p.device) taken.push_back(p.device);
            if (d && d->gamepad) SDL_SetGamepadPlayerIndex(d->gamepad, i);
        }
    }
}

bool InputManager::keyboard_fallback(int player) const {
    const Player& p = players_[player];
    return player == 0 && !p.keyboard && p.menu.keyboard_only();
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
            if (const Device* d = find_tagged(b.source.device, p.device); d && !d->gamepad && d->wheel) return d;
    return nullptr;
}

bool InputManager::wheel_steering(int player) const {
    const Player& p = players_[player];
    return tagged_steering(p) != nullptr || p.kind == DeviceKind::Wheel;
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
            // Esc and Android Back cancel the capture.
            if (e.key.scancode != SDL_SCANCODE_ESCAPE && e.key.scancode != SDL_SCANCODE_AC_BACK)
                captured_ = InputSource{InputSource::Kind::Key, int(e.key.scancode)};
            capturing_ = false;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        if (capturing_) {
            captured_ = InputSource{InputSource::Kind::PadButton, int(e.gbutton.button)};
            captured_->device = capture_tag(e.gbutton.which);
            capturing_ = false;
        }
        break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        // Gamepads report through the gamepad event above.
        if (capturing_ && !SDL_IsGamepad(e.jbutton.which)) {
            captured_ = InputSource{InputSource::Kind::JoyButton, int(e.jbutton.button)};
            captured_->device = capture_tag(e.jbutton.which);
            capturing_ = false;
        }
        break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION:
        if (capturing_ && !SDL_IsGamepad(e.jhat.which) && e.jhat.value != SDL_HAT_CENTERED) {
            u8 v = e.jhat.value;
            u8 dir = (v & SDL_HAT_UP) ? SDL_HAT_UP : (v & SDL_HAT_RIGHT) ? SDL_HAT_RIGHT
                   : (v & SDL_HAT_DOWN) ? SDL_HAT_DOWN : SDL_HAT_LEFT;
            captured_ = InputSource{InputSource::Kind::JoyHat, int(e.jhat.hat), dir};
            captured_->device = capture_tag(e.jhat.which);
            capturing_ = false;
        }
        break;
    default: break;
    }
}

void InputManager::begin_capture(int player) {
    capture_player_ = std::clamp(player, 0, kMaxPlayers - 1);
    capturing_ = true;
    captured_.reset();
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
                axes.push_back(normalize_axis(SDL_GetJoystickAxis(d.joystick, a)));
        capture_baseline_.emplace_back(d.id, std::move(axes));
    }
}

std::optional<InputSource> InputManager::poll_capture() {
    auto out = captured_;
    captured_.reset();
    return out;
}

void InputManager::update_tilt(Player& p, const Device* dev) {
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
    // Roll around the axis pointing at the player (clockwise = right).
    float roll = platform::steering_roll(accel);
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
        return normalize_axis(SDL_GetJoystickAxis(dev->joystick, src.code));
    case InputSource::Kind::JoyHat:
        if (!dev || src.code >= SDL_GetNumJoystickHats(dev->joystick)) return std::nullopt;
        return (SDL_GetJoystickHat(dev->joystick, src.code) & src.hat_dir) ? 1.0f : 0.0f;
    case InputSource::Kind::Tilt: return p.tilt;
    }
    return std::nullopt;
}

void InputManager::evaluate(Player& p, double dt) {
    const Device* dev = find_device(p.device);
    const bool keyboard = keyboard_active(int(&p - players_.data()));
    update_tilt(p, dev);
    evaluate_profile(p, dev, keyboard, p.menu, p.ramp_values, p.state, dt);
    if (p.race_reads_menu) p.race_state = p.state;
    else evaluate_profile(p, dev, keyboard, p.race, p.race_ramps, p.race_state, dt);
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
        evaluate_profile(none, &d, false, pad_layout(), ramps, s, dt);
        pads.push_back(s);
    }
    any_pad_ = merge_pads(pads);
}

void InputManager::evaluate_profile(const Player& p, const Device* dev, bool keyboard, const Profile& prof,
                                    std::vector<float>& ramps, PadState& s, double dt) const {
    float button_val[kPadButtonCount] = {};
    bool button_digital[kPadButtonCount] = {};
    float axis_val[kPadAxisCount] = {};
    if (ramps.size() != prof.bindings.size()) ramps.assign(prof.bindings.size(), 0.0f);

    for (size_t bi = 0; bi < prof.bindings.size(); ++bi) {
        const Binding& b = prof.bindings[bi];
        const BindingOptions& o = b.options;
        // A source that isn't there is left out, not read as 0: `pedal` and `invert` would
        // turn that 0 into a press (the wheel preset with no wheel held Cross and Square).
        std::optional<float> read = read_source(p, dev, keyboard, b.source);
        if (!read) {
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
            float m = std::fabs(v);
            float range = std::max(o.saturation - o.deadzone, 1e-4f);
            m = m <= o.deadzone ? 0.0f : std::min((m - o.deadzone) / range, 1.0f);
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
            float thr = o.threshold > 0.0f ? o.threshold : (b.source.is_analog() ? 0.12f : 0.0f);
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
        for (size_t i = 0; i < kPadAxisCount; ++i) axis_val[i] += ov.axis_f(static_cast<PadAxis>(i));
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
    for (size_t i = 0; i < kPadAxisCount; ++i) {
        float v = std::clamp(axis_val[i], -1.0f, 1.0f);
        s.axes[i] = u8(std::lround((v + 1.0f) * 127.5f));
    }
}

void InputManager::update(double dt) {
    if (capturing_) {
        // Analog capture: the axis that moved furthest from its resting value.
        float best = kCaptureAxisDelta;
        for (const auto& [id, base] : capture_baseline_) {
            const Device* d = find_device(id);
            if (!d) continue;
            for (size_t a = 0; a < base.size(); ++a) {
                float v = d->gamepad ? normalize_axis(SDL_GetGamepadAxis(d->gamepad, SDL_GamepadAxis(a)))
                                     : normalize_axis(SDL_GetJoystickAxis(d->joystick, int(a)));
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
            }
        }
        if (captured_) capturing_ = false;
    }
    for (Player& p : players_) evaluate(p, dt);
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
            if (p.ffb_age < 0.25) ffb_.apply(d->id, d->gamepad, d->joystick, d == steer ? -1 : brake_trigger(p), p.ffb, time_);
            else ffb_.stop(d->id);
        }
        p.ffb_age += dt;
    }
}

void InputManager::set_force_feedback(int player, const ForceFeedbackState& state) {
    players_[player].ffb = state;
    players_[player].ffb_age = 0;
}

int InputManager::brake_trigger(const Player& p) const {
    for (const Binding& b : (p.race_reads_menu ? p.menu : p.race).bindings)
        if (b.target.kind == PadTarget::Kind::Button && b.target.button == PadButton::Square &&
            b.source.kind == InputSource::Kind::PadAxis && b.source.device.empty() &&
            (b.source.code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || b.source.code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
            return b.source.code;
    return -1;
}

bool use_phone_vibrator(bool enabled, int player, bool device_rumbles) {
    return enabled && player == 0 && !device_rumbles;
}

float phone_vibration_strength(const PadRumble& r) { return std::max(r.large / 255.0f, r.small ? 0.6f : 0.0f); }

bool gt3_rumble(std::string_view mode, bool dualsense) {
    if (mode == "gt3") return true;
    if (mode == "modern") return false;
    return dualsense;
}

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
    for (AutoRank rank : {AutoRank::Wheel, AutoRank::Gamepad, AutoRank::Other})
        if (auto it = std::find(devices.begin(), devices.end(), rank); it != devices.end())
            return int(it - devices.begin());
    return -1;
}

PadState merge_pads(const std::vector<PadState>& pads) {
    PadState out;
    auto off_rest = [](u8 v) { return std::abs(int(v) - 128); };
    for (const PadState& p : pads) {
        out.connected |= p.connected;
        out.buttons |= p.buttons;
        for (size_t i = 0; i < kPadButtonCount; ++i) out.pressure[i] = std::max(out.pressure[i], p.pressure[i]);
        for (size_t i = 0; i < kPadAxisCount; ++i)
            if (off_rest(p.axes[i]) > off_rest(out.axes[i])) out.axes[i] = p.axes[i];
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
    // DS2: large motor -> low-frequency rumble, small (on/off) -> high frequency.
    Uint16 low = Uint16(r.large * 257);
    Uint16 high = r.small ? 0xA000 : 0;
    if (d->gamepad) SDL_RumbleGamepad(d->gamepad, low, high, 250);
    else SDL_RumbleJoystick(d->joystick, low, high, 250);
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

void InputManager::set_trigger_rumble(int player, float left, float right) {
    const Device* d = find_device(players_[player].device);
    if (!d || !d->gamepad) return;
    SDL_RumbleGamepadTriggers(d->gamepad, Uint16(std::clamp(left, 0.0f, 1.0f) * 65535),
                              Uint16(std::clamp(right, 0.0f, 1.0f) * 65535), 250);
}

} // namespace gt3r
