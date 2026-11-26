#pragma once

// An app's own controls beyond the DualShock 2's buttons: binding targets "Extra1" .. "Extra4"
// (PadTarget::Kind::Extra), bound like a button on any device and kept in the same binding sets,
// read per frame from the player's pads (PadState::extra_down). An app names the ones it uses and
// may give the built-in layouts default bindings for them. GT3 Revisited uses none.

#include "input/bindings.h"

#include <string>
#include <string_view>

namespace gt3r {

constexpr size_t kExtraControlCount = 4;

// The binding target of extra control `i` (0 .. kExtraControlCount - 1).
PadTarget extra_control(size_t i);
// The name an app gives it for its rows ("" until named).
void name_extra_control(size_t i, std::string_view name);
std::string_view extra_control_name(size_t i);
// Default bindings for it in a built-in layout: `preset` is a make_preset name ("pad", "wheel",
// "keyboard", "quest"); `race` adds them to make_race_preset's "modern" layout for that kind of
// device (else to make_preset's, which the menus and the "gt3" race layout read). `sources` as
// in a config line ("joy.button6 | key.N"). Add them before the bindings are loaded.
void add_extra_control_default(size_t i, std::string_view preset, bool race, std::string_view sources);
// Every name and default dropped.
void clear_extra_controls();
// Counts the changes to the defaults: the built-in layouts made before the last one are made again.
unsigned extra_controls_generation();

} // namespace gt3r
