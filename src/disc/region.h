#pragma once

// Regional releases and the game's language (docs/COMPATIBILITY.md "Regions").
//
// Each release's program was built for one region: it reads that region's data set, while
// GT3.VOL holds several regions' files side by side. The engine reads the files the disc's
// own program reads:
//
//   region  database (+ paramstr, paramunistr)  game config               languages
//   NTSC-J  database/paramdb.db                 gameconf/product.gcf      JP
//   NTSC-U  database/paramdb_us.db              gameconf/product.gcf      US
//   PAL     database/paramdb_eu.db              gameconf/product_eu.gcf   BR, FR, GR, IT, SP
//
// Everything else follows the game's language (text::Language, in the game's order JP, US,
// BR (British English), FR, GR, IT, SP; its index is the one the arcade car filter compares
// with a car's region byte): the menu/<code>/ and race/<code>/ folders, the text block, the
// course folder, the units, the dates and, in Japanese, money x100. The NTSC-U and NTSC-J
// programs only run in their own language; PAL asks for one at the first start.

#include "base/bytes.h"
#include "formats/text/mltx.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r {

enum class Region : int { Japan = 0, America = 1, Europe = 2 };
using Language = text::Language;

// "jp", "us", "eu": folder and config names.
const char* region_key(Region r);
// "NTSC-J", "NTSC-U", "PAL".
const char* region_name(Region r);
std::optional<Region> region_from_key(std::string_view key);

// The release whose program `program` is (CORE.GT3 unpacked, CoreImage::program): the
// car database it loads names the region.
std::optional<Region> region_of_program(ByteSpan program);
// The release a boot file name suggests ("SCES_502.94" -> PAL), for discs whose program
// cannot be read.
std::optional<Region> region_of_serial(std::string_view serial);

// The files of one region's data set.
struct RegionData {
    Region region = Region::America;

    // "", "_us", "_eu": database/paramdb<suffix>.db, paramstr<suffix>.db, paramunistr<suffix>.db.
    const char* db_suffix() const;
    // The program's game configuration file.
    const char* gcf_path() const;
    // The languages the release offers, and the one it starts in (JP, US; PAL: BR).
    std::vector<Language> languages() const;
    Language default_language() const;
    // A course file in `lang`: NTSC-J courses/<label>; otherwise the language's folder,
    // courses/JP, courses/US, courses/UK (BR) or courses/EU (FR, GR, IT, SP).
    std::string course_path(std::string_view label, Language lang) const;
};

// --- Choosing the language ---

// The languages a disc runs in: its release's (RegionData::languages) that it has menus for
// (`has_menus`: menu/<code>/ on the disc), in that order. NTSC-U and NTSC-J discs also carry
// other languages' menu folders, which their program never reads (on NTSC-U most of their
// text is missing): those are left out. Only a disc with none of its release's menus (another
// game on this engine) runs in every language it has menus for, US first.
std::vector<Language> playable_languages(const RegionData& rd, const std::function<bool(Language)>& has_menus);
// The language to start in: `wanted` (the player's choice, from the config) when it is one of
// `playable`; else the release's own when it is; else the first playable one; with none, the
// release's own.
Language choose_language(const RegionData& rd, const std::vector<Language>& playable, std::optional<Language> wanted);

// --- What the language decides ---

// The units the menus and race screens show.
struct Units {
    bool mph = false;          // speeds in mph and distances in miles (else km/h and km)
    const char* power = "PS";  // "PS", "HP", "BHP" or "CV"
    bool power_hp = false;     // HP, BHP and CV figures are PS x 0.98619
    bool torque_ftlb = false;  // torque in ft.lb (kgf.m x 7.2329), else kgf.m
};
// US: mph, HP, ft.lb. BR, IT, SP: km/h, BHP. FR: km/h, CV. GR and JP: km/h, PS. Torque is
// kgf.m in every language but US.
Units language_units(Language lang);
// The player's speed unit (Settings > Display > Speed units, [race] units = kmh | mph): true =
// mph and miles, false = km/h and km, in every menu and race whatever the language; unset (the
// default), the language's (Units::mph). Not in the original, which follows its language only.
void set_speed_unit(std::optional<bool> mph);
std::optional<bool> speed_unit();
// A [race] units value: "kmh" (or "km/h") or "mph", any case; anything else is unset.
std::optional<bool> parse_speed_unit(std::string_view value);
// Money as the game reads its figures (prices, prizes, the game config's sums): x100 in
// Japanese ("initial" 15,000 is a new game's 1,500,000 Cr.), else as stored. Program
// constant: every reader multiplies by 100 when the language test (language == 0)
// holds: the game config, car prices, part
// prices, race prizes, championship bonuses
// (the PAL and NTSC-J programs have the same code).
int money_scale(Language lang);
// One of the European languages (BR, FR, GR, IT, SP): the program's "not 0 and not 1" test
bool is_european(Language lang);

} // namespace gt3r
