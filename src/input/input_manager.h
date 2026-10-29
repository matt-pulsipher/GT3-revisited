#pragma once

#include "input/bindings.h"
#include "input/force_feedback.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_haptic.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r {

class IniDocument;

// Owns physical input devices and turns them into virtual DualShock 2
// states, one per player. Configuration lives in the main config file:
//
//   [player1]
//   controller = auto        ; auto, part of a device name ("DualSense"), or keyboard (only)
//   keyboard = on            ; the keyboard also drives the player
//   phone_vibration = off    ; Android: rumble on the device's own motor (see below)
//   rumble = auto            ; race rumble: auto | modern | gt3 (see gt3_rumble)
//
//   race = modern            ; race controls layout: modern | gt3 (the original)
//
//   [player1.bindings]       ; optional; the pad's inputs, replacing its layout's
//   Cross = pad.south
//
//   [player1.race]           ; optional; the pad's race inputs, replacing the race layout's
//   Cross = pad.righttrigger curve=1.4                     ; Cross = accelerate
//
//   [player1.wheel.bindings] / [player1.wheel.race]        ; the same for a wheel
//   [player1.keyboard.bindings] / [player1.keyboard.race]  ; the keyboard's, whatever the controller
//
// The kind of device in use (DeviceKind) picks the controller's bindings (PlayerBindings). Older
// configs chose them with `preset` (racing / classic / wheel / keyboard) and `device`; load()
// reads them once and save() still writes them for older builds. See docs/INPUT.md.

// Phone vibration ([player1] phone_vibration, off by default): player 1's rumble goes to the
// phone's or tablet's own vibration motor only while the device driving them has no rumble motor
// of its own (built-in handheld controls, touch, a controller without motors). A controller that
// can rumble always rumbles itself and the phone stays still.
bool use_phone_vibrator(bool enabled, int player, bool device_rumbles);
// The DS2 rumble as one motor's strength 0..1: the large motor's level, or the small (on/off)
// motor's 0.6 when stronger.
float phone_vibration_strength(const PadRumble& r);

// The race rumble ([playerN] rumble; race::RumbleModel): "gt3" GT3 original, "modern" ours, "auto"
// (or anything else) GT3 original on a DualSense, whose motors play GT3's levels as a DualShock 2
// did, and Modern on every other controller, where GT3's low levels buzz all the time.
bool gt3_rumble(std::string_view mode, bool dualsense);

// What a device is to Auto: one SDL identifies as a wheel, a gamepad SDL maps, or any other
// joystick (flight sticks, but also odd HID devices such as some keyboards, mice and macro pads).
enum class AutoRank : u8 { Wheel, Gamepad, Other };
// An "auto" player's device among the ones no other player has, in connection order: the first
// wheel, otherwise the first gamepad, otherwise the first other joystick. -1 = none.
int pick_auto_device(const std::vector<AutoRank>& devices);
// The kind of a player's device: none connected is the keyboard's; a gamepad SDL maps the pad's;
// a wheel (SDL's, or `named_wheel`: the device the player named was a 0.3 `preset = wheel`'s) the
// wheel's; any other joystick has no default bindings.
DeviceKind device_kind(bool connected, bool gamepad, bool wheel, bool named_wheel);

// Which connected devices (in connection order) other players' tagged bindings use (`tags`):
// Auto never hands those to this player.
std::vector<bool> reserved_devices(const std::vector<DeviceId>& devices, const std::vector<std::string>& tags);

// Player 1 can drive the menus with what is there: a readable input confirms (Cross) and one moves
// (Up / Down, or the left stick's Y). `pad`, `joystick`, `keys`: a gamepad, another joystick, a
// keyboard that drives player 1 is there. When not, the menus read any_device_pad() (menu_pad).
bool menus_drivable(const Profile& menu, bool pad, bool joystick, bool keys);

// Several pads as one, for menus any device drives (InputManager::any_device_pad): a button is
// down when it is down on any of them (the strongest pressure), and each axis comes from the pad
// holding it furthest from rest, so two sticks never cancel out.
PadState merge_pads(const std::vector<PadState>& pads);

class InputManager {
public:
    static constexpr int kMaxPlayers = 2; // GT3 supports 2-player split screen

    InputManager();
    ~InputManager();

    void load(const IniDocument& config);
    void save(IniDocument& config) const;

    void handle_event(const SDL_Event& e);
    void update(double dt); // call once per frame

    const PadState& pad(int player) const { return players_[player].state; }
    // The pad a race reads while driving: buttons in GT3's race meaning (race_action_name),
    // driven by the player's race layout. With the "gt3" layout it is pad() itself.
    const PadState& race_pad(int player) const { return players_[player].race_state; }
    // Player 1's pad merged with every connected gamepad read with the pad layout's buttons and
    // the keyboard with its default keys, whatever player 1's controller, bindings or keyboard
    // setting: the menus of our Settings read it (Screen::menu_input_any_device), so controls
    // that leave player 1 without a working controller can always be changed back.
    const PadState& any_device_pad() const { return any_pad_; }
    // What the menus outside races read for player 1 (ours and the game's): their pad, or
    // any_device_pad() while nothing player 1 has can drive the menus (menus_drivable), so no
    // controller choice or bindings can leave the game without a way back to Settings.
    const PadState& menu_pad() const { return menu_rescue_ ? any_pad_ : players_[0].state; }
    // Extra input merged into a player's pad on the next update(): buttons
    // are OR'ed (pressure: the stronger wins), axes are added. Used by the
    // on-screen touch controls; a state with connected=false is ignored.
    void set_overlay(int player, const PadState& overlay) { players_[player].overlay = overlay; }
    // Device/controller roll for this player, -1..1 = full lock (the `tilt` source).
    float tilt(int player) const { return players_[player].tilt; }
    void set_rumble(int player, const PadRumble& rumble);
    // Phone vibration (see use_phone_vibrator). Available on Android, where SDL exposes the
    // device's vibrator as a haptic device.
    static bool phone_vibration_available();
    bool phone_vibration() const { return phone_vibration_; }
    void set_phone_vibration(bool on);
    // [playerN] rumble (auto | modern | gt3), whether the player's device is a DualSense, and
    // whether their race rumble is GT3 original now (gt3_rumble with the device in use).
    const std::string& rumble_mode(int player) const { return players_[player].rumble_mode; }
    void set_rumble_mode(int player, std::string mode) { players_[player].rumble_mode = std::move(mode); }
    bool dualsense(int player) const;
    bool gt3_rumble(int player) const { return gt3r::gt3_rumble(players_[player].rumble_mode, dualsense(player)); }
    // Impulse triggers on Xbox One/Series pads (0..1 each). No-op elsewhere.
    void set_trigger_rumble(int player, float left, float right);
    // Driving feedback for this frame (call every frame while driving):
    // DualSense brake-trigger resistance and ABS pulse, Xbox impulse-trigger
    // ABS pulse, wheel spring + steering force. Effects stop by themselves
    // ~0.25 s after the last call. See docs/INPUT.md "Force feedback".
    void set_force_feedback(int player, const ForceFeedbackState& state);
    const ForceFeedbackConfig& ffb_config() const { return ffb_.config(); }
    void set_ffb_config(const ForceFeedbackConfig& c) { ffb_.configure(c); }

    // The kind of device driving the player now; it picks the controller's bindings.
    DeviceKind kind(int player) const { return players_[player].kind; }
    // The menu bindings in use: the controller's (for the kind in use) and the keyboard's.
    const Profile& profile(int player) const { return players_[player].menu; }
    // One set of them, as in use (PlayerBindings::set); `race`: the race controls'.
    const Profile& bindings(int player, bool race, BindingSet set) const {
        return players_[player].bindings.set(players_[player].kind, race, set);
    }
    bool is_custom(int player) const;  // custom menu bindings in use
    void reset_bindings(int player); // the layout chosen; custom bindings kept
    // Custom bindings kept (in use or not) and chosen again (PlayerBindings::use_custom).
    bool has_custom(int player, bool race) const;
    void use_custom(int player, bool race);
    // Edits (PlayerBindings::bind): the source's own set takes it, the other keeps its bindings.
    // Saved to [playerN.bindings] (the pad's), [playerN.wheel.bindings], [playerN.keyboard.bindings].
    bool bind(int player, const PadTarget& target, const InputSource& source, bool replace); // PlayerBindings::bind
    void clear_target(int player, const PadTarget& target, BindingSet set);
    // Race controls (see make_race_preset), one layout per player for whichever kind is in use.
    // Editing a race binding makes that set's race bindings custom ([playerN.race] ...); under
    // "gt3" it starts from the menu bindings.
    const std::string& race_layout(int player) const { return players_[player].bindings.race_layout; }
    void set_race_layout(int player, std::string_view layout);
    bool race_uses_menu_pad(int player) const { return players_[player].race_reads_menu; }
    bool is_race_custom(int player) const;
    const Profile& race_profile(int player) const; // the effective one (gt3: the menu profile)
    bool bind_race(int player, const PadTarget& target, const InputSource& source, bool replace);
    void clear_race_target(int player, const PadTarget& target, BindingSet set);
    std::string device_name(int player) const; // "Keyboard" (or "No device") without one
    // Races steer with GT3's wheel path: the device Steer (LX) is bound to is a wheel (a tagged
    // binding's device, otherwise the Controller's kind).
    bool wheel_steering(int player) const;
    // [playerN] controller: "auto" (pick_auto_device), part of a device name (any device,
    // whatever its type), or kKeyboardOnly. Reassigns devices.
    static constexpr const char* kKeyboardOnly = "keyboard";
    const std::string& device_filter(int player) const { return players_[player].device_filter; }
    void set_device_filter(int player, std::string filter);
    bool keyboard_only(int player) const { return players_[player].device_filter == kKeyboardOnly; }
    // [playerN] keyboard: whether the keyboard also drives this player (always with Keyboard only).
    bool keyboard_enabled(int player) const { return players_[player].keyboard; }
    void set_keyboard(int player, bool on) { players_[player].keyboard = on; }
    // Player 1 with the keyboard off but bindings only the keyboard can drive (no device in use,
    // or custom bindings with no controller source): the keyboard still drives them, so the game
    // can't be left with no way to control it.
    bool keyboard_fallback(int player) const;
    // The keyboard drives this player: enabled, Keyboard only, or the fallback above.
    bool keyboard_active(int player) const {
        return players_[player].keyboard || keyboard_only(player) || keyboard_fallback(player);
    }
    // Names of the connected gamepads, wheels and joysticks.
    std::vector<std::string> device_names() const;
    // Human label for a source on this player's device (e.g. "Cross" on a
    // PlayStation pad, "A" on Xbox, "Right paddle").
    std::string describe(int player, const InputSource& src) const;

    // Remap support: arms capture; the next deliberate input on any device is returned once by
    // poll_capture(). Esc and Android Back cancel it instead (capturing() turns false, nothing
    // returned). Mouse buttons aren't captured.
    // `player`: an input from another device than that player's Controller is tagged with it
    // (InputSource::device); their own Controller's stays untagged.
    void begin_capture(int player = 0);
    void cancel_capture() { capturing_ = false; }
    bool capturing() const { return capturing_; }
    std::optional<InputSource> poll_capture();

private:
    struct Device {
        SDL_JoystickID id = 0;
        SDL_Gamepad* gamepad = nullptr;   // set when SDL recognizes a gamepad
        SDL_Joystick* joystick = nullptr; // always set
        std::string name;
        bool has_accel = false;
        bool wheel = false; // SDL identifies it as a wheel (Auto takes it first)
        bool dualsense = false; // a PlayStation 5 controller (the Auto rumble's GT3 original)
        std::string serial, path; // for device tags (DeviceId); empty when not reported
    };
    struct Player {
        std::string device_filter = "auto"; // [playerN] controller
        std::string wheel_device; // [playerN] wheel_device: a named device that is a wheel (0.3 preset = wheel)
        bool keyboard = true;
        std::string rumble_mode = "auto"; // [playerN] rumble
        SDL_JoystickID device = 0; // 0 = none
        DeviceKind kind = DeviceKind::Keyboard; // of `device` (assign_devices)
        PlayerBindings bindings;
        Profile menu, race; // in use: both sets for `kind` together (refresh)
        bool race_reads_menu = false; // "gt3" with nothing custom: the race reads `state`
        PadState state;
        std::vector<float> ramp_values; // parallel to the menu profile's bindings
        std::vector<float> race_ramps;  // parallel to the race profile's bindings
        PadState race_state;
        PadRumble rumble;
        float tilt = 0.0f;
        PadState overlay; // set_overlay()
        ForceFeedbackState ffb;
        double ffb_age = 1e9; // seconds since set_force_feedback()
    };

    void add_device(SDL_JoystickID id);
    void remove_device(SDL_JoystickID id);
    void assign_devices();
    const Device* find_device(SDL_JoystickID id) const;
    std::vector<DeviceId> device_ids() const; // the connected devices, for device tags
    // InputSource::device: the device a tag names; never `controller` (the player's own device),
    // whose inputs are untagged.
    const Device* find_tagged(const std::string& tag, SDL_JoystickID controller = 0) const;
    std::string tag_of(SDL_JoystickID id) const;
    std::string capture_tag(SDL_JoystickID id) const; // empty for the capturing player's Controller
    const Device* tagged_steering(const Player& p) const; // a tagged wheel Steer is bound to
    // The source's value, or nothing when it can't be read: its device, axis, button or hat
    // isn't connected, or it is a key (or mouse button) and `keyboard` is off.
    std::optional<float> read_source(const Player& p, const Device* dev, bool keyboard, const InputSource& src) const;
    void refresh(Player& p); // menu, race and race_reads_menu after a change of kind or bindings
    void evaluate(Player& p, double dt);
    // `keyboard`: keys and mouse buttons drive `out` (a player's keyboard_active).
    void evaluate_profile(const Player& p, const Device* dev, bool keyboard, const Profile& prof,
                          std::vector<float>& ramps, PadState& out, double dt) const;
    void evaluate_any_device(double dt); // any_device_pad()
    void update_tilt(Player& p, const Device* dev);
    int brake_trigger(const Player& p) const; // SDL trigger axis bound to the brake, or -1
    void vibrate_phone(float strength);       // 0 stops it

    std::vector<Device> devices_;
    std::array<Player, kMaxPlayers> players_;
    PadState any_pad_; // any_device_pad()
    bool menu_rescue_ = false; // menu_pad()
    std::vector<float> any_ramps_; // its keyboard bindings' ramps
    SDL_Sensor* device_accel_ = nullptr; // phone/tablet accelerometer
    ForceFeedback ffb_;
    double time_ = 0;
    bool phone_vibration_ = false;
    SDL_Haptic* vibrator_ = nullptr; // the device's own motor, opened on first use
    bool vibrator_tried_ = false;
    float vibrator_level_ = 0;      // strength last sent (0 = stopped)
    double vibrator_sent_ = -1e9;   // time_ it was sent
    // Keys pressed since the last update(), so a press and release within one
    // frame (fast taps, Android's back key, injected events) still registers.
    std::vector<bool> key_latch_;

    bool capturing_ = false;
    int capture_player_ = 0;
    std::optional<InputSource> captured_;
    std::vector<std::pair<SDL_JoystickID, std::vector<float>>> capture_baseline_;
};

} // namespace gt3r
