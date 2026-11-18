#pragma once

#include "app/ui.h"
#include "input/bindings.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <string>

namespace gt3r {

class InputManager;

// Button prompts (Ui::set_prompts): what "{Cross}" and the other tokens draw for player 1 now,
// as PromptFont glyphs (third_party/fonts/promptfont).
//
// Each token shows the input the player's menu bindings put on that pad button, on the device
// they use: their controller's button as its family marks it (PlayStation shapes, Xbox or
// Nintendo letters and shoulder names; face buttons by the label SDL reports for that pad), or,
// after a key press while the keyboard drives them, the key. Touch controls (drawn as a
// DualShock's buttons) and a button bound to nothing show the PlayStation button; a wheel's or
// joystick's input shows its name ("Button 5").
enum class PadFamily : u8 { PlayStation, Xbox, Nintendo, Generic };
PadFamily pad_family(SDL_GamepadType type);

// One input's glyph (or its name where a glyph would be unreadable), or empty when PromptFont has
// none for it: a gamepad button or axis (`half`
// for a stick's direction) on a pad of `family` (`type` tells the DualShock 4 and DualSense
// buttons apart; `label` is the face button's label), a key.
std::string pad_button_glyph(PadFamily family, SDL_GamepadType type, SDL_GamepadButton b, SDL_GamepadButtonLabel label);
std::string pad_axis_glyph(PadFamily family, SDL_GamepadAxis a, InputSource::Half half);
std::string key_glyph(SDL_Scancode sc);
// The DualShock 2's own button (touch controls, nothing bound).
std::string ps2_glyph(PadButton b);

// `touch_controls`: they show; `touch_ui`: a touch screen's layout (App::touch_ui), which shows
// the PlayStation buttons while no controller is in use and no key was pressed.
PromptTable button_prompts(const InputManager& in, bool touch_controls, bool touch_ui);

} // namespace gt3r
