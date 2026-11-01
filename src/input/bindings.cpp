#include "input/bindings.h"

#include "base/config.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>

namespace gt3r {

namespace {

struct ButtonAlias {
    const char* name;
    SDL_GamepadButton button;
};

// Positional names, so bindings mean the same on Xbox, PlayStation, Switch
// and generic pads. SDL's own names ("a", "dpup", ...) are accepted too.
constexpr ButtonAlias kButtonAliases[] = {
    {"south", SDL_GAMEPAD_BUTTON_SOUTH},    {"east", SDL_GAMEPAD_BUTTON_EAST},
    {"west", SDL_GAMEPAD_BUTTON_WEST},      {"north", SDL_GAMEPAD_BUTTON_NORTH},
    {"cross", SDL_GAMEPAD_BUTTON_SOUTH},    {"circle", SDL_GAMEPAD_BUTTON_EAST},
    {"square", SDL_GAMEPAD_BUTTON_WEST},    {"triangle", SDL_GAMEPAD_BUTTON_NORTH},
};

std::string with_underscores(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c == ' ') c = '_';
    return out;
}

std::string with_spaces(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c == '_') c = ' ';
    return out;
}

bool parse_int(std::string_view s, int& out) {
    auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

// Splits a trailing '+' / '-' half selector.
InputSource::Half take_half(std::string_view& s) {
    if (!s.empty() && s.back() == '+') {
        s.remove_suffix(1);
        return InputSource::Half::Positive;
    }
    if (!s.empty() && s.back() == '-') {
        s.remove_suffix(1);
        return InputSource::Half::Negative;
    }
    return InputSource::Half::Full;
}

const char* half_suffix(InputSource::Half h) {
    return h == InputSource::Half::Positive ? "+" : h == InputSource::Half::Negative ? "-" : "";
}

constexpr const char* kHatDirs[] = {"up", "right", "down", "left"};

} // namespace

std::string InputSource::to_string() const {
    switch (kind) {
    case Kind::None: return "none";
    case Kind::Key: return "key." + with_underscores(SDL_GetScancodeName(static_cast<SDL_Scancode>(code)));
    case Kind::PadButton:
        for (const auto& a : kButtonAliases)
            if (a.button == code) return std::string("pad.") + a.name;
        return std::string("pad.") + SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(code));
    case Kind::PadAxis:
        return std::string("pad.") + SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(code)) + half_suffix(half);
    case Kind::JoyButton: return "joy.button" + std::to_string(code);
    case Kind::JoyAxis: return "joy.axis" + std::to_string(code) + half_suffix(half);
    case Kind::JoyHat: {
        std::string dir = "up";
        for (int i = 0; i < 4; ++i)
            if (hat_dir == (1 << i)) dir = kHatDirs[i];
        return "joy.hat" + std::to_string(code) + "." + dir;
    }
    case Kind::Tilt: return std::string("tilt") + half_suffix(half);
    case Kind::Mouse:
        return code == SDL_BUTTON_RIGHT ? "mouse.right" : code == SDL_BUTTON_MIDDLE ? "mouse.middle" : "mouse.left";
    }
    return "none";
}

bool InputSource::parse(std::string_view tok, InputSource& out) {
    out = InputSource{};
    std::string t = to_lower(tok);
    std::string_view s = t;
    if (s.rfind("key.", 0) == 0) {
        // Scancode names are case-insensitive in SDL; keep original case.
        SDL_Scancode sc = SDL_GetScancodeFromName(with_spaces(tok.substr(4)).c_str());
        if (sc == SDL_SCANCODE_UNKNOWN) return false;
        out.kind = Kind::Key;
        out.code = sc;
        return true;
    }
    if (s.rfind("pad.", 0) == 0) {
        s.remove_prefix(4);
        for (const auto& a : kButtonAliases)
            if (s == a.name) {
                out.kind = Kind::PadButton;
                out.code = a.button;
                return true;
            }
        std::string name(s);
        SDL_GamepadButton b = SDL_GetGamepadButtonFromString(name.c_str());
        if (b != SDL_GAMEPAD_BUTTON_INVALID) {
            out.kind = Kind::PadButton;
            out.code = b;
            return true;
        }
        out.half = take_half(s);
        name = std::string(s);
        SDL_GamepadAxis ax = SDL_GetGamepadAxisFromString(name.c_str());
        if (ax == SDL_GAMEPAD_AXIS_INVALID) return false;
        out.kind = Kind::PadAxis;
        out.code = ax;
        return true;
    }
    if (s.rfind("joy.button", 0) == 0) {
        out.kind = Kind::JoyButton;
        return parse_int(s.substr(10), out.code);
    }
    if (s.rfind("joy.axis", 0) == 0) {
        s.remove_prefix(8);
        out.half = take_half(s);
        out.kind = Kind::JoyAxis;
        return parse_int(s, out.code);
    }
    if (s.rfind("joy.hat", 0) == 0) {
        s.remove_prefix(7);
        size_t dot = s.find('.');
        if (dot == std::string_view::npos || !parse_int(s.substr(0, dot), out.code)) return false;
        std::string_view dir = s.substr(dot + 1);
        for (int i = 0; i < 4; ++i)
            if (dir == kHatDirs[i]) {
                out.kind = Kind::JoyHat;
                out.hat_dir = u8(1 << i); // matches SDL_HAT_UP/RIGHT/DOWN/LEFT
                return true;
            }
        return false;
    }
    if (s.rfind("tilt", 0) == 0) {
        s.remove_prefix(4);
        out.half = take_half(s);
        out.kind = Kind::Tilt;
        return s.empty();
    }
    if (s.rfind("mouse.", 0) == 0) {
        s.remove_prefix(6);
        out.kind = Kind::Mouse;
        out.code = s == "right" ? SDL_BUTTON_RIGHT : s == "middle" ? SDL_BUTTON_MIDDLE : SDL_BUTTON_LEFT;
        return s == "left" || s == "right" || s == "middle";
    }
    return false;
}

std::string PadTarget::to_string() const {
    if (kind == Kind::Button) return std::string(pad_button_name(button));
    if (kind == Kind::Axis) return std::string(pad_axis_name(axis)) + half_suffix(half);
    return "none";
}

bool PadTarget::parse(std::string_view tok, PadTarget& out) {
    out = PadTarget{};
    if (auto b = pad_button_from_name(tok)) {
        out.kind = Kind::Button;
        out.button = *b;
        return true;
    }
    out.half = take_half(tok);
    if (auto a = pad_axis_from_name(tok)) {
        out.kind = Kind::Axis;
        out.axis = *a;
        return true;
    }
    return false;
}

std::vector<Binding> Binding::parse_line(std::string_view target_tok, std::string_view sources) {
    std::vector<Binding> out;
    PadTarget target;
    if (!PadTarget::parse(trim(target_tok), target)) return out;
    for (std::string_view alt : split(sources, '|')) {
        Binding b;
        b.target = target;
        bool first = true;
        bool ok = true;
        for (std::string_view word : split(trim(alt), ' ')) {
            word = trim(word);
            if (word.empty()) continue;
            if (first) {
                ok = InputSource::parse(word, b.source);
                first = false;
                continue;
            }
            size_t eq = word.find('=');
            std::string key = to_lower(word.substr(0, eq));
            float v = eq == std::string_view::npos ? 1.0f : std::strtof(std::string(word.substr(eq + 1)).c_str(), nullptr);
            if (key == "deadzone") b.options.deadzone = v;
            else if (key == "saturation") b.options.saturation = v;
            else if (key == "curve") b.options.curve = v;
            else if (key == "scale") b.options.scale = v;
            else if (key == "ramp") b.options.ramp = v;
            else if (key == "threshold") b.options.threshold = v;
            else if (key == "invert") b.options.invert = v != 0.0f;
            else if (key == "pedal") b.options.pedal = v != 0.0f;
            else if (key == "device" && eq != std::string_view::npos) {
                // %20 for spaces, %25 for '%', %7C for '|' (source_with_options).
                std::string dev;
                const std::string_view enc = word.substr(eq + 1);
                for (size_t i = 0; i < enc.size(); ++i) {
                    if (enc[i] == '%' && i + 2 < enc.size()) {
                        dev += char(std::strtol(std::string(enc.substr(i + 1, 2)).c_str(), nullptr, 16));
                        i += 2;
                    } else {
                        dev += enc[i];
                    }
                }
                b.source.device = dev;
            }
        }
        if (ok && !first) out.push_back(b);
    }
    return out;
}

std::string Binding::source_with_options() const {
    std::string s = source.to_string();
    const BindingOptions def;
    auto num = [](float f) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", f);
        return std::string(buf);
    };
    if (options.deadzone != def.deadzone) s += " deadzone=" + num(options.deadzone);
    if (options.saturation != def.saturation) s += " saturation=" + num(options.saturation);
    if (options.curve != def.curve) s += " curve=" + num(options.curve);
    if (options.scale != def.scale) s += " scale=" + num(options.scale);
    if (options.ramp != def.ramp) s += " ramp=" + num(options.ramp);
    if (options.threshold != def.threshold) s += " threshold=" + num(options.threshold);
    if (options.invert) s += " invert";
    if (options.pedal) s += " pedal";
    if (!source.device.empty()) {
        s += " device=";
        for (char c : source.device) {
            if (c == ' ') s += "%20";
            else if (c == '%') s += "%25";
            else if (c == '|') s += "%7C";
            else s += c;
        }
    }
    return s;
}

std::vector<std::string> Profile::devices() const {
    std::vector<std::string> out;
    for (const Binding& b : bindings)
        if (!b.source.device.empty() && std::find(out.begin(), out.end(), b.source.device) == out.end())
            out.push_back(b.source.device);
    return out;
}

namespace {

// The Nth device of its name (1-based), in connection order.
int name_number(const std::vector<DeviceId>& devices, size_t index) {
    int n = 0;
    for (size_t i = 0; i <= index; ++i) n += devices[i].name == devices[index].name;
    return n;
}

// `field` (serial or path) tells devices[index] apart from the other devices of its name.
bool unique_among_name(const std::vector<DeviceId>& devices, size_t index, std::string DeviceId::*field) {
    const std::string& value = devices[index].*field;
    if (value.empty()) return false;
    for (size_t i = 0; i < devices.size(); ++i)
        if (i != index && devices[i].name == devices[index].name && devices[i].*field == value) return false;
    return true;
}

// A tag's parts: the name, then what tells same-name devices apart (a serial, a port path, or the
// order number "#N", 1 without one).
struct TagParts {
    std::string name, serial, path;
    int nth = 1;
};

TagParts parse_tag(const std::string& tag) {
    TagParts t;
    t.name = tag;
    if (const size_t at = tag.find("@s:"); at != std::string::npos) {
        t.name = tag.substr(0, at);
        t.serial = tag.substr(at + 3);
    } else if (const size_t at2 = tag.find("@p:"); at2 != std::string::npos) {
        t.name = tag.substr(0, at2);
        t.path = tag.substr(at2 + 3);
    } else if (const size_t hash = tag.rfind('#'); hash != std::string::npos && hash + 1 < tag.size() &&
               std::all_of(tag.begin() + std::ptrdiff_t(hash) + 1, tag.end(),
                           [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        t.name = tag.substr(0, hash);
        t.nth = std::atoi(tag.c_str() + hash + 1);
    }
    return t;
}

} // namespace

std::string device_tag(const std::vector<DeviceId>& devices, size_t index) {
    const DeviceId& d = devices[index];
    if (unique_among_name(devices, index, &DeviceId::serial)) return d.name + "@s:" + d.serial;
    if (unique_among_name(devices, index, &DeviceId::path)) return d.name + "@p:" + d.path;
    const int nth = name_number(devices, index);
    return nth > 1 ? d.name + "#" + std::to_string(nth) : d.name;
}

int find_device_tag(const std::vector<DeviceId>& devices, const std::string& tag) {
    // The name first; serial, port path or order only choose among devices of that name.
    TagParts t = parse_tag(tag);
    auto named = [&](const std::string& name) {
        std::vector<size_t> out;
        for (size_t i = 0; i < devices.size(); ++i)
            if (devices[i].name == name) out.push_back(i);
        return out;
    };
    std::vector<size_t> same = named(t.name);
    if (same.empty() && t.name != tag) { // a name with "#" or "@" in it
        same = named(tag);
        t = TagParts{tag, "", "", 1};
    }
    if (same.empty()) return -1;
    if (!t.serial.empty() || !t.path.empty()) { // that very device, or nothing (never a look-alike)
        for (size_t i : same)
            if (t.serial.empty() ? devices[i].path == t.path : devices[i].serial == t.serial) return int(i);
        return -1;
    }
    return t.nth >= 1 && t.nth <= int(same.size()) ? int(same[size_t(t.nth - 1)]) : -1;
}

int resolve_device_tag(const std::vector<DeviceId>& devices, const std::string& tag, int controller) {
    const int at = find_device_tag(devices, tag);
    return at == controller ? -1 : at;
}

std::string device_tag_label(const std::vector<DeviceId>& devices, const std::string& tag) {
    if (const int at = find_device_tag(devices, tag); at >= 0) {
        const int nth = name_number(devices, size_t(at));
        return devices[size_t(at)].name + (nth > 1 ? " " + std::to_string(nth) : "");
    }
    const TagParts t = parse_tag(tag); // not connected: the name as saved
    return t.name + (t.nth > 1 ? " " + std::to_string(t.nth) : "");
}

void Profile::add(std::string_view target, std::string_view sources) {
    for (Binding& b : Binding::parse_line(target, sources)) bindings.push_back(b);
}

std::string_view InputSource::paddle_name() const {
    if (kind != Kind::PadButton) return {};
    switch (code) {
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "Right paddle";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "Left paddle";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "Right paddle 2";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "Left paddle 2";
    default: return {};
    }
}

void Profile::remove_target(const PadTarget& target) {
    std::erase_if(bindings, [&](const Binding& b) { return b.target == target; });
}

void Profile::bind(const PadTarget& target, const InputSource& source, bool replace) {
    // Clear both the full axis and its halves when replacing an axis binding.
    if (replace) {
        remove_target(target);
        if (target.kind == PadTarget::Kind::Axis && target.half == InputSource::Half::Full) {
            PadTarget t = target;
            t.half = InputSource::Half::Positive;
            remove_target(t);
            t.half = InputSource::Half::Negative;
            remove_target(t);
        }
    }
    // A back paddle does one thing: bound to an action it leaves the one it had (shift).
    if (!source.paddle_name().empty())
        std::erase_if(bindings, [&](const Binding& b) { return b.source == source && !(b.target == target); });
    Binding b;
    b.target = target;
    b.source = source;
    // A full stick axis bound to a full pad axis drops the captured half.
    if (target.kind == PadTarget::Kind::Axis && target.half == InputSource::Half::Full && source.is_analog())
        b.source.half = InputSource::Half::Full;
    if (source.kind == InputSource::Kind::PadAxis || source.kind == InputSource::Kind::JoyAxis)
        if (target.kind == PadTarget::Kind::Axis) b.options.deadzone = 0.08f;
    bindings.push_back(b);
    name = "custom";
}

bool Profile::keyboard_only() const {
    return std::none_of(bindings.begin(), bindings.end(), [](const Binding& b) {
        return b.source.kind != InputSource::Kind::Key && b.source.kind != InputSource::Kind::Mouse &&
               b.source.kind != InputSource::Kind::None;
    });
}

std::string_view pad_preset() {
#if GT3R_XR
    return "quest";
#else
    return "pad";
#endif
}

namespace {

void add_keyboard(Profile& p) {
    p.add("Up", "key.Up");
    p.add("Down", "key.Down");
    p.add("Left", "key.Left");
    p.add("Right", "key.Right");
    p.add("Cross", "key.Return | key.W ramp=0.12");
    p.add("Square", "key.S ramp=0.08");
    p.add("Circle", "key.Space");
    p.add("Triangle", "key.Escape | key.Backspace | key.AC_Back"); // AC_Back = Android back
    p.add("L1", "key.Q");
    p.add("R1", "key.E");
    p.add("L2", "key.Z");
    p.add("R2", "key.C");
    p.add("L3", "key.F");
    p.add("R3", "key.G");
    p.add("Start", "key.P | key.Pause");
    p.add("Select", "key.Tab");
    p.add("LX-", "key.A ramp=0.2");
    p.add("LX+", "key.D ramp=0.2");
    p.add("RX-", "key.Home"); // the right stick (camera look) off I / J / K / L, player 2's
    p.add("RX+", "key.End");
    p.add("RY-", "key.PageUp");
    p.add("RY+", "key.PageDown");
}

// `b_back`: the right face button (B on Xbox and Quest controllers) is Triangle (back) and the
// top one Circle, as Quest players expect; otherwise the DualShock 2's positions.
void add_pad_common(Profile& p, bool b_back = false) {
    p.add("Up", "pad.dpup");
    p.add("Down", "pad.dpdown");
    p.add("Left", "pad.dpleft");
    p.add("Right", "pad.dpright");
    p.add("Circle", b_back ? "pad.north" : "pad.east");
    p.add("Triangle", b_back ? "pad.east" : "pad.north");
    p.add("L1", "pad.leftshoulder");
    p.add("R1", "pad.rightshoulder");
    p.add("L3", "pad.leftstick");
    p.add("R3", "pad.rightstick");
    p.add("Start", "pad.start");
    p.add("Select", "pad.back");
    p.add("LX", "pad.leftx deadzone=0.08");
    p.add("LY", "pad.lefty deadzone=0.08");
    p.add("RX", "pad.rightx deadzone=0.08");
    p.add("RY", "pad.righty deadzone=0.08");
}

void add_wheel(Profile& p) {
    // Typical USB wheel: axis 0 steering, pedals on axes 1/2 resting at +1.
    // Wheels differ; remap in the controller screen if needed.
    p.add("LX", "joy.axis0");
    p.add("Cross", "joy.axis1 pedal invert");
    p.add("Square", "joy.axis2 pedal invert");
    p.add("R1", "joy.button4");
    p.add("L1", "joy.button5");
    p.add("Circle", "joy.button1");
    p.add("Triangle", "joy.button3");
    p.add("Start", "joy.button9");
    p.add("Select", "joy.button8");
    p.add("Up", "joy.hat0.up");
    p.add("Down", "joy.hat0.down");
    p.add("Left", "joy.hat0.left");
    p.add("Right", "joy.hat0.right");
}

// Modern race layout, targets in GT3's race meaning (see race_action_name).
void add_modern_race_pad(Profile& p) {
    p.add("Cross", "pad.righttrigger");             // accelerate
    p.add("Square", "pad.lefttrigger");             // brake
    p.add("R2", "pad.south | pad.paddle1");         // shift up
    p.add("L2", "pad.west | pad.paddle2");          // shift down
    p.add("Circle", "pad.east");                    // handbrake
    p.add("R1", "pad.north");                       // change view
    p.add("L1", "pad.rightstick");                  // look back
    p.add("Triangle", "pad.leftshoulder");          // reverse
    p.add("Start", "pad.start");                    // pause
    p.add("Left", "pad.dpleft");
    p.add("Right", "pad.dpright");
    p.add("Up", "pad.dpup");
    p.add("Down", "pad.dpdown");
    p.add("LX", "pad.leftx deadzone=0.08");
    p.add("LY", "pad.lefty deadzone=0.08");
    p.add("RX", "pad.rightx deadzone=0.08");
    p.add("RY", "pad.righty deadzone=0.08");
}

void add_modern_race_keyboard(Profile& p) {
    p.add("Cross", "key.W ramp=0.12 | key.Up ramp=0.12");
    p.add("Square", "key.S ramp=0.08 | key.Down ramp=0.08");
    p.add("LX-", "key.A ramp=0.2");
    p.add("LX+", "key.D ramp=0.2");
    p.add("Left", "key.Left");
    p.add("Right", "key.Right");
    p.add("Circle", "key.Space");
    p.add("R2", "key.E");
    p.add("L2", "key.Q");
    p.add("Triangle", "key.R");
    p.add("R1", "key.C");
    p.add("L1", "key.X");
    p.add("Start", "key.Escape | key.P | key.Pause | key.AC_Back");
    p.add("RX-", "key.Home"); // the right stick (camera look) off I / J / K / L, player 2's
    p.add("RX+", "key.End");
    p.add("RY-", "key.PageUp");
    p.add("RY+", "key.PageDown");
}

} // namespace

Profile make_preset(std::string_view name) {
    Profile p;
    p.name = std::string(name);
    if (name == "wheel") {
        add_wheel(p);
    } else if (name != "keyboard") { // "pad", "quest" (VR headsets)
        // The DualShock 2 1:1: analog triggers drive the pressure-sensitive L2/R2.
        add_pad_common(p, name == "quest");
        p.add("Cross", "pad.south");
        p.add("Square", "pad.west");
        p.add("L2", "pad.lefttrigger");
        p.add("R2", "pad.righttrigger");
        p.add("R2", "pad.paddle1"); // back paddles shift, like the triggers: right up, left down
        p.add("L2", "pad.paddle2");
    }
    add_keyboard(p);
    return p;
}

const std::vector<std::string_view>& race_layout_names() {
    static const std::vector<std::string_view> names = {"modern", "gt3"};
    return names;
}

Profile make_race_preset(std::string_view layout, std::string_view device_preset) {
    Profile p;
    p.name = layout == "gt3" ? "gt3" : "modern";
    if (p.name == "gt3") return p; // the menu pad itself
    if (device_preset == "wheel") { // pedals and buttons keep their wheel layout
        add_wheel(p);
        PadTarget select;
        PadTarget::parse("Select", select);
        p.remove_target(select); // Select has no race action (no respawn, as in GT3)
    } else if (device_preset != "keyboard") {
        add_modern_race_pad(p);
    }
    add_modern_race_keyboard(p);
    return p;
}

std::string_view race_action_name(const PadTarget& t) {
    if (t.kind == PadTarget::Kind::Axis) return t.axis == PadAxis::LX ? "Steer" : "";
    if (t.kind != PadTarget::Kind::Button) return "";
    switch (t.button) {
    case PadButton::Cross: return "Accelerate";
    case PadButton::Square: return "Brake";
    case PadButton::Circle: return "Handbrake";
    case PadButton::Triangle: return "Reverse";
    case PadButton::R2: return "Shift up";
    case PadButton::L2: return "Shift down";
    case PadButton::R1: return "Change view";
    case PadButton::L1: return "Look back";
    case PadButton::Start: return "Pause";
    case PadButton::R3: return "Perf. overlay";
    case PadButton::Left: return "Steer left";
    case PadButton::Right: return "Steer right";
    default: return "";
    }
}

} // namespace gt3r

namespace gt3r {

namespace {

Profile load_profile(const IniDocument& cfg, const std::string& section) {
    Profile custom;
    custom.name = "custom";
    if (const IniDocument::Section* b = cfg.find(section))
        for (const auto& [target, sources] : b->entries) custom.add(target, sources);
    return custom;
}

void save_profile(IniDocument& cfg, const std::string& section, const Profile* prof) {
    auto& sections = cfg.sections();
    std::erase_if(sections, [&](const IniDocument::Section& s) { return s.name == section; });
    if (!prof) return;
    // Group alternatives for the same target onto one line.
    IniDocument::Section& out = cfg.get_or_add(section);
    for (const Binding& b : prof->bindings) {
        std::string key = b.target.to_string();
        auto it = std::find_if(out.entries.begin(), out.entries.end(), [&](auto& kv) { return kv.first == key; });
        if (it == out.entries.end())
            out.entries.emplace_back(key, b.source_with_options());
        else
            it->second += " | " + b.source_with_options();
    }
}

Profile part_of(const Profile& prof, BindingSet s) {
    Profile out;
    out.name = prof.name;
    for (const Binding& b : prof.bindings)
        if (binding_set(b.source) == s) out.bindings.push_back(b);
    return out;
}

bool same_bindings(const Profile& a, const Profile& b) {
    auto lines = [](const Profile& p) {
        std::vector<std::string> out;
        for (const Binding& x : p.bindings) out.push_back(x.target.to_string() + "=" + x.source_with_options());
        std::sort(out.begin(), out.end());
        return out;
    };
    return lines(a) == lines(b);
}

size_t controller_index(DeviceKind k) { return k == DeviceKind::Wheel ? 1 : k == DeviceKind::Joystick ? 2 : 0; }

// The controller set a controller input belongs to by its type: raw joystick inputs the wheel's.
DeviceKind controller_kind(const InputSource& s) {
    return s.kind == InputSource::Kind::JoyAxis || s.kind == InputSource::Kind::JoyButton ||
                   s.kind == InputSource::Kind::JoyHat
               ? DeviceKind::Wheel
               : DeviceKind::Pad;
}

// Player 2's keyboard keys: the I / J / K / L cluster and the keys around it, none of them player
// 1's (test input_player_keys_apart). Menus and race.
Profile player2_menu() {
    Profile p;
    p.name = "player2";
    p.add("Up", "key.I");
    p.add("Down", "key.K");
    p.add("Left", "key.J");
    p.add("Right", "key.L");
    p.add("Cross", "key.O");    // confirm
    p.add("Triangle", "key.U"); // back
    p.add("Circle", "key.;");
    p.add("Square", "key.H");
    p.add("L1", "key.,");
    p.add("R1", "key..");
    p.add("L2", "key.N");
    p.add("R2", "key.M");
    p.add("Start", "key.'");
    p.add("Select", "key./");
    return p;
}

Profile player2_race() {
    Profile p;
    p.name = "player2";
    p.add("Cross", "key.I ramp=0.12"); // accelerate
    p.add("Square", "key.K ramp=0.08"); // brake
    p.add("LX-", "key.J ramp=0.2");
    p.add("LX+", "key.L ramp=0.2");
    p.add("R2", "key.O");       // shift up
    p.add("L2", "key.U");       // shift down
    p.add("Circle", "key.M");   // handbrake
    p.add("Triangle", "key.H"); // reverse
    p.add("R1", "key..");       // change view
    p.add("L1", "key.,");       // look back
    p.add("Start", "key.;");    // pause
    return p;
}

// A set's layout: the kind's preset, or its modern race layout, from that set's inputs; the
// keyboard's around I / J / K / L for player 2.
const Profile& layout(DeviceKind kind, bool race, BindingSet s, bool player2) {
    static const Profile none;
    static const std::array<Profile, 8> layouts = [] {
        auto menu = [](DeviceKind k) { return make_preset(device_kind_preset(k)); };
        auto racing = [](DeviceKind k) { return make_race_preset("modern", device_kind_preset(k)); };
        return std::array<Profile, 8>{part_of(menu(DeviceKind::Pad), BindingSet::Controller),
                                      part_of(menu(DeviceKind::Wheel), BindingSet::Controller),
                                      part_of(menu(DeviceKind::Keyboard), BindingSet::Keyboard),
                                      part_of(racing(DeviceKind::Pad), BindingSet::Controller),
                                      part_of(racing(DeviceKind::Wheel), BindingSet::Controller),
                                      part_of(racing(DeviceKind::Keyboard), BindingSet::Keyboard),
                                      player2_menu(),
                                      player2_race()};
    }();
    if (s == BindingSet::Keyboard) return layouts[player2 ? (race ? 7 : 6) : (race ? 5 : 2)];
    if (kind == DeviceKind::Keyboard || kind == DeviceKind::Joystick) return none; // no defaults
    return layouts[(race ? 3 : 0) + controller_index(kind)];
}

} // namespace

std::string_view device_kind_preset(DeviceKind k) {
    switch (k) {
    case DeviceKind::Wheel: return "wheel";
    case DeviceKind::Keyboard: return "keyboard";
    case DeviceKind::Joystick: return "joystick";
    default: return pad_preset();
    }
}

namespace {

// A chord input's device tag in the config: %20 for spaces, %25 '%', %7C '|', %2B '+'.
std::string escape_tag(std::string_view tag) {
    std::string out;
    for (char c : tag) {
        if (c == ' ') out += "%20";
        else if (c == '%') out += "%25";
        else if (c == '|') out += "%7C";
        else if (c == '+') out += "%2B";
        else out += c;
    }
    return out;
}

std::string unescape_tag(std::string_view enc) {
    std::string out;
    for (size_t i = 0; i < enc.size(); ++i) {
        if (enc[i] == '%' && i + 2 < enc.size()) {
            out += char(std::strtol(std::string(enc.substr(i + 1, 2)).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += enc[i];
        }
    }
    return out;
}

} // namespace

std::string_view pad_action_key(PadAction a) { return a == PadAction::NextSong ? "next_song" : "previous_song"; }
std::string_view pad_action_name(PadAction a) { return a == PadAction::NextSong ? "Next song" : "Previous song"; }

std::string Chord::to_string() const {
    std::string out;
    for (const InputSource& s : inputs) out += (out.empty() ? "" : "+") + s.to_string();
    return out;
}

bool Chord::parse(std::string_view text, Chord& out) {
    out = Chord{};
    for (std::string_view part : split(text, '+')) {
        part = trim(part);
        // A device tag (" device=<tag>") goes with the input it follows.
        std::string device;
        if (size_t d = part.find(" device="); d != std::string_view::npos) {
            device = unescape_tag(trim(part.substr(d + 8)));
            part = trim(part.substr(0, d));
        }
        InputSource s;
        if (part.empty() || !InputSource::parse(part, s) || s.kind == InputSource::Kind::None) return false;
        s.device = device;
        if (!out.inputs.empty() && binding_set(s) != out.set()) return false;
        if (std::find(out.inputs.begin(), out.inputs.end(), s) == out.inputs.end()) out.inputs.push_back(s);
        if (out.inputs.size() > kMaxInputs) return false;
    }
    return !out.inputs.empty();
}

bool chord_input_held(const InputSource& s, float v) {
    if (!s.is_analog()) return v >= 0.5f;
    if (s.half == InputSource::Half::Negative) return v <= -0.5f;
    return v >= 0.5f; // a trigger (0..1) or a stick's positive half
}

void ActionBindings::bind(PadAction a, const Chord& c, bool replace) {
    std::vector<Chord>& v = chords[static_cast<size_t>(a)];
    if (replace) std::erase_if(v, [&](const Chord& x) { return x.set() == c.set(); });
    if (std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
}

void ActionBindings::clear(PadAction a, BindingSet s) {
    std::erase_if(chords[static_cast<size_t>(a)], [&](const Chord& x) { return x.set() == s; });
}

void ActionBindings::load(const IniDocument& cfg, const std::string& sec) {
    for (auto& v : chords) v.clear();
    const IniDocument::Section* section = cfg.find(sec + ".actions");
    if (!section) return;
    for (const auto& [key, value] : section->entries)
        for (size_t i = 0; i < kPadActionCount; ++i)
            if (to_lower(key) == pad_action_key(PadAction(i)))
                for (std::string_view alt : split(value, '|')) {
                    Chord c;
                    if (Chord::parse(alt, c)) bind(PadAction(i), c, false);
                }
}

void ActionBindings::save(IniDocument& cfg, const std::string& sec) const {
    const std::string name = sec + ".actions";
    std::erase_if(cfg.sections(), [&](const IniDocument::Section& s) { return s.name == name; });
    for (size_t i = 0; i < kPadActionCount; ++i) {
        if (chords[i].empty()) continue;
        std::string line;
        for (const Chord& c : chords[i]) {
            std::string part;
            for (const InputSource& s : c.inputs)
                part += (part.empty() ? "" : "+") + s.to_string() + (s.device.empty() ? "" : " device=" + escape_tag(s.device));
            line += (line.empty() ? "" : " | ") + part;
        }
        cfg.get_or_add(name).entries.emplace_back(std::string(pad_action_key(PadAction(i))), line);
    }
}

BindingSet binding_set(const InputSource& s) {
    return s.kind == InputSource::Kind::Key || s.kind == InputSource::Kind::Mouse || s.kind == InputSource::Kind::Tilt
               ? BindingSet::Keyboard
               : BindingSet::Controller;
}

std::optional<Profile>& PlayerBindings::slot(DeviceKind kind, bool race, BindingSet s) {
    if (s == BindingSet::Keyboard) return race ? keyboard_race : keyboard_menu;
    return (race ? controller_race : controller_menu)[controller_index(kind)];
}

const Profile& PlayerBindings::set(DeviceKind kind, bool race, BindingSet s) const {
    static const Profile none;
    if (s == BindingSet::Controller && kind == DeviceKind::Keyboard) return none;
    const std::optional<Profile>& c = const_cast<PlayerBindings*>(this)->slot(kind, race, s);
    if (c && (race ? race_custom_on : menu_custom_on)) return *c;
    if (race && race_layout == "gt3") return set(kind, false, s);
    return layout(kind, race, s, player2);
}

bool PlayerBindings::has_custom(DeviceKind kind, bool race) const {
    const std::optional<Profile>& keys = race ? keyboard_race : keyboard_menu;
    if (keys) return true;
    return kind != DeviceKind::Keyboard && (race ? controller_race : controller_menu)[controller_index(kind)];
}

bool PlayerBindings::custom(DeviceKind kind, bool race) const {
    return has_custom(kind, race) && (race ? race_custom_on : menu_custom_on);
}

void PlayerBindings::begin_custom(DeviceKind kind, bool race) {
    if (custom(kind, race)) return;
    // Both sets as they stand (the layout's; under "gt3" the menu's) become the custom ones.
    Profile keys = set(kind, race, BindingSet::Keyboard);
    std::optional<Profile> controller;
    if (kind != DeviceKind::Keyboard) controller = set(kind, race, BindingSet::Controller);
    keys.name = "custom";
    slot(kind, race, BindingSet::Keyboard) = std::move(keys);
    if (controller) {
        controller->name = "custom";
        slot(kind, race, BindingSet::Controller) = std::move(controller);
    }
    (race ? race_custom_on : menu_custom_on) = true;
}

bool PlayerBindings::race_reads_menu(DeviceKind kind) const { return race_layout == "gt3" && !custom(kind, true); }

Profile PlayerBindings::combined(DeviceKind kind, bool race) const {
    Profile out;
    out.name = custom(kind, race) ? "custom" : race ? race_layout : std::string(device_kind_preset(kind));
    for (BindingSet s : {BindingSet::Controller, BindingSet::Keyboard}) {
        const Profile& p = set(kind, race, s);
        out.bindings.insert(out.bindings.end(), p.bindings.begin(), p.bindings.end());
    }
    return out;
}

bool PlayerBindings::bind(DeviceKind kind, bool race, const PadTarget& target, const InputSource& source,
                          bool replace) {
    if (target.kind == PadTarget::Kind::Axis && target.half == InputSource::Half::Full && !source.is_analog())
        return false;
    begin_custom(kind, race);
    const BindingSet s = binding_set(source);
    if (s == BindingSet::Controller && kind == DeviceKind::Keyboard) kind = controller_kind(source);
    std::optional<Profile>& c = slot(kind, race, s);
    if (!c) c = set(kind, race, s); // as it stands (under "gt3", the menu's)
    c->bind(target, source, replace);
    return true;
}

void PlayerBindings::clear(DeviceKind kind, bool race, const PadTarget& target, BindingSet s) {
    if (s == BindingSet::Controller && kind == DeviceKind::Keyboard) return;
    begin_custom(kind, race);
    std::optional<Profile>& c = slot(kind, race, s);
    if (!c) c = set(kind, race, s);
    c->remove_target(target);
    c->name = "custom";
}

void PlayerBindings::reset(DeviceKind, bool race) { (race ? race_custom_on : menu_custom_on) = false; }

void PlayerBindings::use_custom(bool race) { (race ? race_custom_on : menu_custom_on) = true; }

void PlayerBindings::set_race_layout(DeviceKind kind, std::string_view layout_name) {
    race_layout = layout_name == "gt3" ? "gt3" : "modern";
    reset(kind, true);
}

void PlayerBindings::load(const IniDocument& cfg, const std::string& sec) {
    // Each section keeps its own set's inputs only.
    auto read = [&](const std::string& section, BindingSet s) -> std::optional<Profile> {
        Profile p = part_of(load_profile(cfg, section), s);
        if (p.bindings.empty()) return std::nullopt;
        p.name = "custom";
        return p;
    };
    controller_menu = {read(sec + ".bindings", BindingSet::Controller), read(sec + ".wheel.bindings", BindingSet::Controller),
                       read(sec + ".joystick.bindings", BindingSet::Controller)};
    controller_race = {read(sec + ".race", BindingSet::Controller), read(sec + ".wheel.race", BindingSet::Controller),
                       read(sec + ".joystick.race", BindingSet::Controller)};
    keyboard_menu = read(sec + ".keyboard.bindings", BindingSet::Keyboard);
    keyboard_race = read(sec + ".keyboard.race", BindingSet::Keyboard);
    race_custom_on = to_lower(cfg.get(sec, "race_custom").value_or("on")) != "off";
    menu_custom_on = to_lower(cfg.get(sec, "menu_custom").value_or("on")) != "off";
}

bool PlayerBindings::load_old(const IniDocument& cfg, const std::string& sec) {
    bool controller_inputs = false;
    for (bool race : {false, true}) {
        const Profile old = load_profile(cfg, sec + (race ? ".race" : ".bindings"));
        // Split by input type: the keyboard's, the pad's, the wheel's.
        for (auto [s, kind] : {std::pair{BindingSet::Keyboard, DeviceKind::Keyboard},
                               std::pair{BindingSet::Controller, DeviceKind::Pad},
                               std::pair{BindingSet::Controller, DeviceKind::Wheel}}) {
            Profile p;
            for (const Binding& b : old.bindings)
                if (binding_set(b.source) == s && (s == BindingSet::Keyboard || controller_kind(b.source) == kind))
                    p.bindings.push_back(b);
            if (p.bindings.empty()) continue;
            controller_inputs |= s == BindingSet::Controller;
            if (same_bindings(p, layout(kind, race, s, player2))) continue;
            p.name = "custom";
            slot(kind, race, s) = std::move(p);
        }
    }
    return controller_inputs;
}

std::vector<std::string> PlayerBindings::devices() const {
    std::vector<std::string> out;
    auto add = [&](const std::optional<Profile>& p) {
        if (!p) return;
        for (const std::string& d : p->devices())
            if (std::find(out.begin(), out.end(), d) == out.end()) out.push_back(d);
    };
    for (const auto& sets : {&controller_menu, &controller_race})
        for (const std::optional<Profile>& p : *sets) add(p);
    return out;
}

void PlayerBindings::save(IniDocument& cfg, const std::string& sec) const {
    auto put = [&](const std::string& section, const std::optional<Profile>& p) {
        save_profile(cfg, section, p ? &*p : nullptr);
    };
    put(sec + ".bindings", controller_menu[0]);
    put(sec + ".race", controller_race[0]);
    put(sec + ".wheel.bindings", controller_menu[1]);
    put(sec + ".wheel.race", controller_race[1]);
    put(sec + ".joystick.bindings", controller_menu[2]);
    put(sec + ".joystick.race", controller_race[2]);
    put(sec + ".keyboard.bindings", keyboard_menu);
    put(sec + ".keyboard.race", keyboard_race);
    // Kept custom sets stay saved while a layout is chosen.
    cfg.set(sec, "race_custom", race_custom_on ? "on" : "off");
    cfg.set(sec, "menu_custom", menu_custom_on ? "on" : "off");
}

} // namespace gt3r
