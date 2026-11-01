#pragma once

#include "input/virtual_pad.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {

class IniDocument;

// What a binding reads from. Values are normalized: digital 0/1, full
// axes -1..1, half axes / triggers 0..1.
struct InputSource {
    enum class Kind : u8 {
        None,
        Key,           // code = SDL_Scancode
        PadButton,     // code = SDL_GamepadButton
        PadAxis,       // code = SDL_GamepadAxis
        JoyButton,     // code = button index (wheels, pedals, arcade sticks)
        JoyAxis,       // code = axis index
        JoyHat,        // code = hat index, dir = hat direction mask
        Tilt,          // gamepad accelerometer roll (or device tilt on mobile)
        Mouse,         // code = SDL mouse button
    };
    enum class Half : u8 { Full, Positive, Negative };

    Kind kind = Kind::None;
    int code = 0;
    u8 hat_dir = 0;
    Half half = Half::Full;
    // The device it is read from: empty = the player's Controller. Otherwise another device's tag
    // (device_tag), saved as the binding's `device=` option. A tagged source whose device isn't
    // connected reads nothing.
    std::string device = {};

    bool is_analog() const { return kind == Kind::PadAxis || kind == Kind::JoyAxis || kind == Kind::Tilt; }
    // A back paddle ("Right paddle", "Left paddle", "Right paddle 2", "Left paddle 2"); empty
    // for any other source. A paddle does one thing: binding it takes it off its old target.
    std::string_view paddle_name() const;
    std::string to_string() const;
    static bool parse(std::string_view token, InputSource& out);
    bool operator==(const InputSource&) const = default;
};

// Shapes a raw source value before it reaches the pad.
struct BindingOptions {
    float deadzone = 0.0f;   // inner deadzone (0..1)
    float saturation = 1.0f; // value at which output reaches full
    float curve = 1.0f;      // response exponent: >1 finer control near rest
    float scale = 1.0f;
    bool invert = false;
    bool pedal = false;      // map a full -1..1 axis to 0..1 (wheel pedals)
    float ramp = 0.0f;       // seconds to reach full value (keyboard "analog")
    float threshold = 0.0f;  // digital press threshold for analog -> button (0 = auto)
};

// Where a binding writes to.
struct PadTarget {
    enum class Kind : u8 { None, Button, Axis };
    Kind kind = Kind::None;
    PadButton button = PadButton::Cross;
    PadAxis axis = PadAxis::LX;
    InputSource::Half half = InputSource::Half::Full; // for axes: LX+ / LX-

    std::string to_string() const;
    static bool parse(std::string_view token, PadTarget& out);
    bool operator==(const PadTarget&) const = default;
};

struct Binding {
    PadTarget target;
    InputSource source;
    BindingOptions options;

    // "Cross = pad.righttrigger curve=1.5" -> one Binding per '|' alternative.
    static std::vector<Binding> parse_line(std::string_view target, std::string_view sources);
    std::string source_with_options() const;
};

// The dead zone a stick bound to an axis gets (Profile::bind).
constexpr float kStickDeadzone = 0.08f;

struct Profile {
    std::string name;
    std::vector<Binding> bindings;

    void add(std::string_view target, std::string_view sources);
    void remove_target(const PadTarget& target);
    // Device tags used by these bindings (InputSource::device).
    std::vector<std::string> devices() const;
    // An edit in the binding editor: `source` drives `target`, alone when `replace` (an axis
    // target drops its halves too). A stick on an axis gets a dead zone (kStickDeadzone); a
    // wheel's axis (`wheel`) none. Names the profile "custom".
    void bind(const PadTarget& target, const InputSource& source, bool replace, bool wheel = false);
    // No binding reads a controller (pad, joystick or tilt): only keys and mouse buttons drive it.
    bool keyboard_only() const;
};

// Built-in binding sets for the virtual DualShock 2 (menus, and races in GT3's layout), one per
// kind of device (InputManager picks it from the device in use; players don't choose it):
// "pad" is the DualShock 2 layout 1:1 (triggers -> L2/R2, the back paddles too: right R2, left
// L2, GT3's shift buttons); "wheel" is a typical USB wheel; "keyboard" is keyboard only. All of
// them also include the keyboard bindings, so a pad is never required.
// "quest" (VR builds' pad) is "pad" with B as back (Triangle) and Y as Circle.
Profile make_preset(std::string_view name);
std::string_view pad_preset(); // "pad"; "quest" in VR builds

// Race controls. While driving, the race reads a second virtual pad per player whose buttons
// always mean what they meant in GT3 (Cross accelerate, Square brake, Triangle reverse, R2/L2
// shift, R1 view, L1 look back, Start pause, ...); see race_action_name(). A race layout
// says which physical inputs drive each of those actions:
//   "modern" (default)  a current racing-game layout: triggers throttle/brake, Cross/Square
//                       shift, Circle handbrake, Triangle view, R3 look back, L1 reverse,
//                       WASD-style keyboard driving
//   "gt3"               the original: the race reads the menu pad itself (identity), so every
//                       button does what it did on a DualShock 2
// make_race_preset("gt3", ...) returns an empty profile named "gt3" (= use the menu pad).
Profile make_race_preset(std::string_view layout, std::string_view device_preset);
const std::vector<std::string_view>& race_layout_names();
// The race action a race-pad button / axis stands for ("Accelerate" for Cross); empty = unused.
std::string_view race_action_name(const PadTarget& target);

// A connected device as tags know it: its name, and its serial number and USB port path when the
// system reports them (empty otherwise).
struct DeviceId {
    std::string name, serial, path;
};
// A connected device's tag (InputSource::device), so identical controllers never swap: its name
// with its serial ("Wireless Controller@s:<serial>"; Sony pads report their Bluetooth address),
// otherwise its USB port path ("@p:<path>", while it stays in that port), otherwise "#N" for the Nth
// device of that name in connection order. A serial or path another device of that name shares
// isn't used. find_device_tag: the index of the device a tag names, or -1: the name must match,
// then the serial or port path must (none connected with it: nothing), or the order number.
// resolve_device_tag: the same, but never the player's own Controller (`controller`, -1 for none):
// its inputs are untagged, so a tag landing on it reads nothing.
std::string device_tag(const std::vector<DeviceId>& devices, size_t index);
int find_device_tag(const std::vector<DeviceId>& devices, const std::string& tag);
int resolve_device_tag(const std::vector<DeviceId>& devices, const std::string& tag, int controller);
// A tag as players read it: the name, numbered among connected devices of that name
// ("Wireless Controller 2"); never the serial or path.
std::string device_tag_label(const std::vector<DeviceId>& devices, const std::string& tag);

// The kind of device driving a player, from the device chosen or picked automatically
// (InputManager): it picks the controller's bindings. Pad: a gamepad SDL maps. Wheel: a device SDL
// identifies as a wheel. Joystick: any other joystick (arcade and flight sticks, odd HID devices):
// no default bindings, nothing pressed until the player binds its inputs. Keyboard: no device
// drives the player ("Keyboard only", or none connected).
enum class DeviceKind : u8 { Pad, Wheel, Keyboard, Joystick };
constexpr size_t kDeviceKindCount = 4;
// Its make_preset name: pad_preset(), "wheel", "keyboard"; "joystick" has no preset.
std::string_view device_kind_preset(DeviceKind k);

// The two sets of bindings a player has: the controller's and the keyboard's. The keyboard's
// also holds tilt (the phone's or the pad's own roll), so it is read whatever the controller.
// Editing one never touches the other.
enum class BindingSet : u8 { Controller, Keyboard };
BindingSet binding_set(const InputSource& s); // Keyboard for keys, mouse buttons and tilt

// Ours: things a player does that are none of the DualShock 2's buttons, each bound to chords
// (Settings -> Controls, [playerN.actions]; none by default). The race's song controls.
enum class PadAction : u8 { NextSong, PreviousSong, Count };
constexpr size_t kPadActionCount = static_cast<size_t>(PadAction::Count);
std::string_view pad_action_key(PadAction a);  // its config key: "next_song", "previous_song"
std::string_view pad_action_name(PadAction a); // as Settings names it: "Next song", "Previous song"

// Inputs held together: one to four of one set (keys, or one controller's buttons, triggers and
// stick directions). It fires when the last of them goes down; a chord may share inputs with
// other controls, which then also do their own thing. "pad.back+pad.rightshoulder".
struct Chord {
    static constexpr size_t kMaxInputs = 4;
    std::vector<InputSource> inputs;

    BindingSet set() const { return inputs.empty() ? BindingSet::Controller : binding_set(inputs.front()); }
    std::string to_string() const;
    static bool parse(std::string_view text, Chord& out); // false: empty, too long, mixed sets or unknown
    bool operator==(const Chord&) const = default;
};
// Whether a source's value (InputManager's, normalised) holds it down as part of a chord:
// buttons and keys pressed, a trigger or stick half past half way.
bool chord_input_held(const InputSource& s, float value);

// A player's chords, per action ("next_song = pad.back+pad.rightshoulder | key.rightbracket").
struct ActionBindings {
    std::array<std::vector<Chord>, kPadActionCount> chords;

    const std::vector<Chord>& of(PadAction a) const { return chords[static_cast<size_t>(a)]; }
    // `c` for `a`: added, or with `replace` instead of the chords of its own set (the other set's stay).
    void bind(PadAction a, const Chord& c, bool replace);
    void clear(PadAction a, BindingSet s);
    void load(const IniDocument& cfg, const std::string& sec); // [<sec>.actions]
    void save(IniDocument& cfg, const std::string& sec) const;
};

// A player's bindings: the controller's, one set per kind of controller (pad, wheel, joystick),
// and the keyboard's, one set whatever the controller; menus and races each. A set is its layout's
// part (make_preset, make_race_preset "modern"; player 2's keyboard keys around I / J / K / L)
// until edited, then custom. Custom sets are kept while a layout is chosen instead
// (race_custom_on / menu_custom_on off) and come back when Custom is chosen again. The kind in use
// picks the controller's set; DeviceKind::Keyboard has none. Under the "gt3" race layout the race
// sets are the menu's, and the race reads the menu pad itself.
struct PlayerBindings {
    std::array<std::optional<Profile>, 3> controller_menu, controller_race; // [pad, wheel, joystick]
    std::optional<Profile> keyboard_menu, keyboard_race;
    std::string race_layout = "modern";
    bool race_custom_on = true, menu_custom_on = true; // [playerN] race_custom / menu_custom
    bool player2 = false; // the keyboard's layout around I / J / K / L, apart from player 1's

    const Profile& set(DeviceKind kind, bool race, BindingSet s) const;
    bool has_custom(DeviceKind kind, bool race) const; // custom sets kept (either set)
    bool custom(DeviceKind kind, bool race) const;     // and in use
    bool race_reads_menu(DeviceKind kind) const;        // "gt3", no custom race sets in use
    Profile combined(DeviceKind kind, bool race) const; // both sets: what drives the player
    // An edit: `source` goes into its own set (binding_set), alone there with `replace`; the
    // other set keeps its bindings. A controller input with no controller in use goes to the
    // pad's set (a raw joystick input to the wheel's). Refused (false): a button or key on a whole
    // stick axis, which could only push it one way (keys steer with Steer left / right).
    // Editing while a layout is chosen makes new custom sets from both sets as they stand (under
    // "gt3" the menu's) plus the edit, replacing kept ones (Settings asks first). `wheel`: the
    // source is a wheel's (Profile::bind: no dead zone).
    bool bind(DeviceKind kind, bool race, const PadTarget& target, const InputSource& source, bool replace,
              bool wheel = false);
    void clear(DeviceKind kind, bool race, const PadTarget& target, BindingSet s);
    void reset(DeviceKind kind, bool race); // the layout chosen; custom sets kept
    void use_custom(bool race);             // the kept custom sets chosen again
    void set_race_layout(DeviceKind kind, std::string_view layout); // chosen; custom race sets kept

    std::vector<std::string> devices() const; // device tags any of the sets use
    // [<sec>.bindings] / [<sec>.race] (the pad's), [<sec>.wheel.*], [<sec>.joystick.*] and
    // [<sec>.keyboard.*]. The wheel's sets drop the stick dead zone (kStickDeadzone) earlier
    // versions gave their axes: a dead zone around the wheel's centre.
    void load(const IniDocument& cfg, const std::string& sec);
    // Configs from before the two sets: one custom profile in [<sec>.bindings] / [<sec>.race],
    // keys and controller inputs together. Each input goes to its type's set: pad inputs the
    // pad's, raw joystick inputs the wheel's, keys, mouse buttons and tilt the keyboard's; a part
    // that is just its layout's isn't kept. True when it had controller inputs.
    bool load_old(const IniDocument& cfg, const std::string& sec);
    void save(IniDocument& cfg, const std::string& sec) const;

private:
    std::optional<Profile>& slot(DeviceKind kind, bool race, BindingSet s);
    void begin_custom(DeviceKind kind, bool race); // custom sets in use, made as they stand if not
};

} // namespace gt3r
