#include "app/button_prompts.h"

#include "app/ui_font.h"
#include "input/input_manager.h"

#include <SDL3/SDL_keyboard.h>

namespace gt3r {

namespace {

// PromptFont's code points (glyphs.json in its release).
namespace pf {
constexpr u32 kXboxX = 0x21D0, kXboxY = 0x21D1, kXboxB = 0x21D2, kXboxA = 0x21D3;
constexpr u32 kSquare = 0x21E0, kTriangle = 0x21E1, kCircle = 0x21E2, kCross = 0x21E3;
constexpr u32 kWest = 0x21A4, kNorth = 0x21A5, kEast = 0x21A6, kSouth = 0x21A7; // face buttons by position
constexpr u32 kL1 = 0x21B0, kR1 = 0x21B1, kL2 = 0x21B2, kR2 = 0x21B3;
constexpr u32 kLB = 0x2198, kRB = 0x2199, kLT = 0x2196, kRT = 0x2197;
constexpr u32 kNinL = 0x219C, kNinR = 0x219D, kNinZL = 0x219A, kNinZR = 0x219B;
constexpr u32 kLeftStick = 0x21BA, kRightStick = 0x21BB;
constexpr u32 kSelect = 0x21F7, kStart = 0x21F8, kHome = 0x21F9;
constexpr u32 kXboxView = 0x21FA, kXboxMenu = 0x21FB, kMinus = 0x21FD, kPlus = 0x21FE;
// D-pads: left, up, right, down, left/right, up/down, all four.
constexpr u32 kDpad[7] = {0x219E, 0x219F, 0x21A0, 0x21A1, 0x21A2, 0x21A3, 0x21CE};
constexpr u32 kXboxDpad[7] = {0x227A, 0x227B, 0x227C, 0x227D, 0x227E, 0x227F, 0x2284};
// Sticks: left stick left, up, right, down, left/right, up/down; the right stick's.
constexpr u32 kLStick[6] = {0x21BC, 0x21BE, 0x21C0, 0x21C2, 0x21C4, 0x21C5};
constexpr u32 kRStick[6] = {0x21BD, 0x21BF, 0x21C1, 0x21C3, 0x21C6, 0x21F5};
// Keys.
constexpr u32 kKeyLeft = 0x23F4, kKeyRight = 0x23F5, kKeyUp = 0x23F6, kKeyDown = 0x23F7, kArrowKeys = 0x2424;
constexpr u32 kKeyAsciiBase = 0xFF00; // ASCII '!'..'~' as keycaps at 0xFF01.., capitals only
} // namespace pf

std::string g(u32 cp) { return ui_font::prompt_glyph(cp); }

enum Dir { kL, kU, kR, kD, kLR, kUD, kAll };

} // namespace

PadFamily pad_family(SDL_GamepadType type) {
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return PadFamily::PlayStation;
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return PadFamily::Xbox;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return PadFamily::Nintendo;
    default: return PadFamily::Generic;
    }
}

std::string pad_button_glyph(PadFamily family, SDL_GamepadType type, SDL_GamepadButton b, SDL_GamepadButtonLabel label) {
    const bool ps = family == PadFamily::PlayStation, nin = family == PadFamily::Nintendo, xbox = family == PadFamily::Xbox;
    const u32* dpad = xbox ? pf::kXboxDpad : pf::kDpad;
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
    case SDL_GAMEPAD_BUTTON_EAST:
    case SDL_GAMEPAD_BUTTON_WEST:
    case SDL_GAMEPAD_BUTTON_NORTH:
        switch (label) {
        case SDL_GAMEPAD_BUTTON_LABEL_A: return g(pf::kXboxA);
        case SDL_GAMEPAD_BUTTON_LABEL_B: return g(pf::kXboxB);
        case SDL_GAMEPAD_BUTTON_LABEL_X: return g(pf::kXboxX);
        case SDL_GAMEPAD_BUTTON_LABEL_Y: return g(pf::kXboxY);
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return g(pf::kCross);
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return g(pf::kCircle);
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return g(pf::kSquare);
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return g(pf::kTriangle);
        default:
            return g(b == SDL_GAMEPAD_BUTTON_SOUTH  ? pf::kSouth
                     : b == SDL_GAMEPAD_BUTTON_EAST ? pf::kEast
                     : b == SDL_GAMEPAD_BUTTON_WEST ? pf::kWest
                                                    : pf::kNorth);
        }
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return g(ps ? pf::kL1 : nin ? pf::kNinL : pf::kLB);
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return g(ps ? pf::kR1 : nin ? pf::kNinR : pf::kRB);
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return g(pf::kLeftStick);
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return g(pf::kRightStick);
    // The DualShock 4's and DualSense's small buttons by name: their glyphs (thin pills) can't be
    // told apart at text size.
    case SDL_GAMEPAD_BUTTON_BACK:
        if (type == SDL_GAMEPAD_TYPE_PS5) return "Create";
        if (type == SDL_GAMEPAD_TYPE_PS4) return "Share";
        return g(xbox ? pf::kXboxView : nin ? pf::kMinus : pf::kSelect);
    case SDL_GAMEPAD_BUTTON_START:
        if (type == SDL_GAMEPAD_TYPE_PS5 || type == SDL_GAMEPAD_TYPE_PS4) return "Options";
        return g(xbox ? pf::kXboxMenu : nin ? pf::kPlus : pf::kStart);
    case SDL_GAMEPAD_BUTTON_GUIDE: return g(pf::kHome);
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return g(dpad[kL]);
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return g(dpad[kU]);
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return g(dpad[kR]);
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return g(dpad[kD]);
    default: return {}; // paddles and the rest: by name
    }
}

std::string pad_axis_glyph(PadFamily family, SDL_GamepadAxis a, InputSource::Half half) {
    const bool ps = family == PadFamily::PlayStation, nin = family == PadFamily::Nintendo;
    switch (a) {
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return g(ps ? pf::kL2 : nin ? pf::kNinZL : pf::kLT);
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return g(ps ? pf::kR2 : nin ? pf::kNinZR : pf::kRT);
    case SDL_GAMEPAD_AXIS_LEFTX:
    case SDL_GAMEPAD_AXIS_LEFTY:
    case SDL_GAMEPAD_AXIS_RIGHTX:
    case SDL_GAMEPAD_AXIS_RIGHTY: {
        const u32* s = a == SDL_GAMEPAD_AXIS_LEFTX || a == SDL_GAMEPAD_AXIS_LEFTY ? pf::kLStick : pf::kRStick;
        const bool x = a == SDL_GAMEPAD_AXIS_LEFTX || a == SDL_GAMEPAD_AXIS_RIGHTX;
        if (half == InputSource::Half::Full) return g(s[x ? kLR : kUD]);
        const bool neg = half == InputSource::Half::Negative;
        return g(s[x ? (neg ? kL : kR) : (neg ? kU : kD)]);
    }
    default: return {};
    }
}

std::string key_glyph(SDL_Scancode sc) {
    // Keys with a word on them (Space, Tab, Shift...) by name: on their keycaps it can't be read at
    // text size.
    switch (sc) {
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return g(0x242E);
    case SDL_SCANCODE_ESCAPE: return g(0x242F);
    case SDL_SCANCODE_LEFT: return g(pf::kKeyLeft);
    case SDL_SCANCODE_RIGHT: return g(pf::kKeyRight);
    case SDL_SCANCODE_UP: return g(pf::kKeyUp);
    case SDL_SCANCODE_DOWN: return g(pf::kKeyDown);
    default: break;
    }
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return g(0x2460 + u32(sc - SDL_SCANCODE_F1));
    // Letters, digits and punctuation as the keyboard layout labels the key.
    SDL_Keycode k = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
    if (k >= 'a' && k <= 'z') k -= 'a' - 'A';
    if (k > ' ' && k <= '~' && !(k >= 'a' && k <= 'z')) return g(pf::kKeyAsciiBase + u32(k - ' '));
    return {};
}

std::string ps2_glyph(PadButton b) {
    switch (b) {
    case PadButton::Cross: return g(pf::kCross);
    case PadButton::Circle: return g(pf::kCircle);
    case PadButton::Square: return g(pf::kSquare);
    case PadButton::Triangle: return g(pf::kTriangle);
    case PadButton::L1: return g(pf::kL1);
    case PadButton::R1: return g(pf::kR1);
    case PadButton::L2: return g(pf::kL2);
    case PadButton::R2: return g(pf::kR2);
    case PadButton::L3: return g(pf::kLeftStick);
    case PadButton::R3: return g(pf::kRightStick);
    case PadButton::Start: return g(pf::kStart);
    case PadButton::Select: return g(pf::kSelect);
    case PadButton::Left: return g(pf::kDpad[kL]);
    case PadButton::Up: return g(pf::kDpad[kU]);
    case PadButton::Right: return g(pf::kDpad[kR]);
    case PadButton::Down: return g(pf::kDpad[kD]);
    default: return {};
    }
}

PromptTable button_prompts(const InputManager& in, bool touch_controls, bool touch_ui) {
    enum class Mode { Ps2, Keys, Pad };
    const bool key = in.keyboard_active(0) && in.last_input_was_key();
    const Mode mode = touch_controls                                   ? Mode::Ps2
                      : key                                            ? Mode::Keys
                      : in.kind(0) != DeviceKind::Keyboard ? Mode::Pad
                      : touch_ui                                       ? Mode::Ps2 // a phone with no controller
                                                                       : Mode::Keys;
    SDL_Gamepad* pad = mode == Mode::Pad ? in.gamepad(0) : nullptr;
    const SDL_GamepadType type = pad ? SDL_GetGamepadType(pad) : SDL_GAMEPAD_TYPE_UNKNOWN;
    const PadFamily family = pad ? pad_family(type) : PadFamily::PlayStation;
    const Profile& menu = in.bindings(0, false, mode == Mode::Keys ? BindingSet::Keyboard : BindingSet::Controller);

    auto one = [&](PadButton b) -> std::string {
        if (mode == Mode::Ps2) return ps2_glyph(b);
        const InputSource* src = nullptr; // the first input bound to it on the player's own device
        for (const Binding& bd : menu.bindings)
            if (bd.target.kind == PadTarget::Kind::Button && bd.target.button == b && bd.source.device.empty()) {
                src = &bd.source;
                break;
            }
        if (!src) return ps2_glyph(b);
        std::string s;
        switch (src->kind) {
        case InputSource::Kind::Key:
            s = key_glyph(SDL_Scancode(src->code));
            if (s.empty()) s = SDL_GetKeyName(SDL_GetKeyFromScancode(SDL_Scancode(src->code), SDL_KMOD_NONE, false));
            break;
        case InputSource::Kind::PadButton:
            if (pad) {
                const auto sb = SDL_GamepadButton(src->code);
                s = pad_button_glyph(family, type, sb, SDL_GetGamepadButtonLabel(pad, sb));
            }
            break;
        case InputSource::Kind::PadAxis:
            if (pad) s = pad_axis_glyph(family, SDL_GamepadAxis(src->code), src->half);
            break;
        default: break;
        }
        return s.empty() ? in.describe(0, *src) : s;
    };

    PromptTable t;
    t.keyboard = mode == Mode::Keys;
    for (size_t i = 0; i < size_t(Prompt::UpDown); ++i) {
        // Prompt and PadButton name the same buttons.
        static constexpr PadButton kButton[] = {
            PadButton::Cross, PadButton::Circle, PadButton::Square, PadButton::Triangle, PadButton::L1,
            PadButton::R1,    PadButton::L2,     PadButton::R2,     PadButton::L3,       PadButton::R3,
            PadButton::Start, PadButton::Select, PadButton::Up,     PadButton::Down,     PadButton::Left,
            PadButton::Right,
        };
        t.text[i] = one(kButton[i]);
    }
    const std::string& up = t.text[size_t(Prompt::Up)];
    const std::string& down = t.text[size_t(Prompt::Down)];
    const std::string& left = t.text[size_t(Prompt::Left)];
    const std::string& right = t.text[size_t(Prompt::Right)];
    // A d-pad's pair or all four as one glyph, the arrow keys as one; other inputs side by side.
    const u32* dpad = family == PadFamily::Xbox ? pf::kXboxDpad : pf::kDpad;
    const bool dpad_ud = up == g(dpad[kU]) && down == g(dpad[kD]), dpad_lr = left == g(dpad[kL]) && right == g(dpad[kR]);
    const bool keys = up == g(pf::kKeyUp) && down == g(pf::kKeyDown) && left == g(pf::kKeyLeft) && right == g(pf::kKeyRight);
    t.text[size_t(Prompt::UpDown)] = dpad_ud ? g(dpad[kUD]) : up + down;
    t.text[size_t(Prompt::LeftRight)] = dpad_lr ? g(dpad[kLR]) : left + right;
    t.text[size_t(Prompt::Arrows)] = dpad_ud && dpad_lr ? g(dpad[kAll]) : keys ? g(pf::kArrowKeys) : up + down + left + right;
    return t;
}

} // namespace gt3r
