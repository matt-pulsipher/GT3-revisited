#pragma once

// Times, speeds and dates as GT3's own formatters print them (presentation §D10), and the
// placeholders they print for "none". The placeholders are drawn with the game fonts' long
// dash, character 0x7F (wider than the hyphen), never with '-': the strings here are game text
// for the game fonts. The UTF-8 path draws them the same (U+007F is the fonts' code 0x7F); the
// app's own interface font has no such glyph.

#include "base/bytes.h"

#include <string>
#include <string_view>

namespace gt3r::race {

// The game fonts' long dash.
inline constexpr char kDash = '\x7F';

// The menus' "no time" (results lists, Top 3, records, the time-trial overlay, the Replay
// Theater and the Analyzer): two dashes for the minutes.
inline constexpr std::string_view kNoTimeText = "\x7F\x7F'\x7F\x7F.\x7F\x7F\x7F";
// The race display's (the HUD's time lines and pop-ups, Hud::format_time): one dash for the
// minutes.
inline constexpr std::string_view kHudNoTimeText = "\x7F'\x7F\x7F.\x7F\x7F\x7F";
// No speed (a speed of 0 or less), before the unit: the HUD's speed lines and a record's top
// speed.
inline constexpr std::string_view kNoSpeedText = "\x7F\x7F\x7F.\x7F";
// A record without a date (records pages).
inline constexpr std::string_view kNoDateText = "\x7F\x7F/\x7F\x7F/\x7F\x7F";

// The menus' time format: kNoTimeText for the "no time" value 359,999,999 ms, "h:mm'ss.mmm"
// from an hour on, else "m'ss.mmm" (minutes unpadded).
std::string menu_time_text(u32 ms);
// The same of seconds rounded to the millisecond; a negative time (or NaN: none) is "no time".
std::string menu_time_text(double seconds);

} // namespace gt3r::race
