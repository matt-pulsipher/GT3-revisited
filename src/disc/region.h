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
// Gran Turismo Concept runs on the same engine and names its own files (GameKind below): each
// release's program names one database (database/paramdb_bs.db, _kr, _gtc_eu, _gtc_tw, with
// the matching paramstr / paramunistr files) and its game configs (gameconf/concept*.gcf),
// while its volume also carries other releases' files, GT3's among them. So a disc's files are
// the ones its program names (ProgramFiles), whatever the release; the table above is what the
// three GT3 programs name.
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

// The game a disc's program is. The engine shipped as Gran Turismo 3 and as Gran Turismo
// Concept (2001 Tokyo, 2002 Tokyo-Seoul, 2002 Tokyo-Geneva); a program says which it is by the
// game configuration it loads: gameconf/product*.gcf (GT3) or gameconf/concept*.gcf (Concept).
// What follows the kind: the course folders (RegionData::course_path). The database layout
// does not: the tables are told apart from their records (game/data/car_database.h).
enum class GameKind : int { Gt3 = 0, Concept = 1 };
// "Gran Turismo 3", "Gran Turismo Concept".
const char* game_name(GameKind g);

// "jp", "us", "eu": folder and config names.
const char* region_key(Region r);
// "NTSC-J", "NTSC-U", "PAL".
const char* region_name(Region r);
std::optional<Region> region_from_key(std::string_view key);

// The release whose program `program` is (CORE.GT3 unpacked, CoreImage::program): the
// car database it loads names the region.
std::optional<Region> region_of_program(ByteSpan program);
// The data files a program names (strings in its data, each a whole NUL-terminated string).
struct ProgramFiles {
    // "database/paramdb<suffix>.db": the car database it loads; the paramstr, paramunistr and
    // id index files carry the same suffix. None when the program names no such file. (A
    // program naming several: the first of "_eu", "_us", "" it names, as region_of_program;
    // else the first in the program.)
    std::optional<std::string> db_suffix;
    // Every "gameconf/<name>.gcf" it names, in the program's order.
    std::vector<std::string> gcf;

    // Concept when the game configs are named concept*, else GT3.
    GameKind game() const;
    // The game config the program loads in `lang`. A program naming one loads that one. One
    // naming several picks by the language (GT3, Concept Tokyo: the
    // same test): language 0 (JP) the plain name, 1 (US) "<name>_us", any other "<name>_eu"
    // (Concept: Korean "<name>_kr"); the plain one when it does not name that. Empty when it
    // names none.
    std::string gcf_for(Language lang) const;
};
ProgramFiles files_of_program(ByteSpan program);

// The release a boot file name suggests ("SCES_502.94" -> PAL), for discs whose program
// cannot be read.
std::optional<Region> region_of_serial(std::string_view serial);

// The files of one region's data set.
struct RegionData {
    Region region = Region::America;
    GameKind game = GameKind::Gt3;
    // What the disc's program names (ProgramFiles), when that is not the region's GT3 files:
    // the database suffix and the game config.
    std::optional<std::string> program_db_suffix;
    std::optional<std::string> program_gcf;
    // The program names a game config for each language and loads its own language's (a Concept program of the 2001
    // generation: Tokyo-Seoul v1.02, whose config is v2.00's; its own constants are the older program's, concept §12).
    bool gcf_per_language = false;

    RegionData() = default;
    RegionData(Region r) : region(r) {} // a region's GT3 data set

    // A disc's data set: `region`, with the files `files` (of its program) names.
    static RegionData of_program(Region region, const ProgramFiles& files);

    // database/paramdb<suffix>.db, paramstr<suffix>.db, paramunistr<suffix>.db: the program's,
    // else the region's ("", "_us", "_eu").
    std::string db_suffix() const;
    // The program's game configuration file: its own, else the region's.
    std::string gcf_path() const;
    // The languages the release offers, and the one it starts in (JP, US; PAL: BR). A Concept release: its
    // program's own language (Japanese, Korean; Tokyo-Geneva: British English, its Asian release Traditional
    // Chinese) and what its language page offers (Europe: BR, FR, GR, IT, SP, PT; Asia: TW, US).
    std::vector<Language> languages() const;
    Language default_language() const;
    // The page of menu/language.mbl the release's first start shows: "root", Concept's Asian release "root_tw".
    std::string language_page() const;
    // A course file in `lang`: GT3 NTSC-J courses/<label>; otherwise (every Concept release
    // too) the language's folder, courses/JP, courses/US, courses/UK (BR) or courses/EU (FR,
    // GR, IT, SP). A disc may lack the folder (Concept Tokyo-Geneva has courses/UK and
    // courses/EU only, its Asian release courses/JP only): course_file_path (vfs/vfs.h) then
    // takes the folder the disc has.
    std::string course_path(std::string_view label, Language lang) const;

    // The text of Vfs::kRegionFile: the region key, then for a disc whose program names other
    // files than the region's GT3 ones a line each: "game=concept", "db=<suffix>",
    // "gcf=<path>", "gcfs=per-language". from_text reads it back (none when the key is unknown).
    std::string to_text() const;
    static std::optional<RegionData> from_text(std::string_view text);
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
// US: mph, HP, ft.lb. BR, IT, SP: km/h, BHP. FR, PT: km/h, CV. GR, JP, KR, CN, TW: km/h, PS. Torque is
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
