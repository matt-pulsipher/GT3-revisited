#pragma once

// Mod cars: new cars defined in small text files that a mod ships under
// mods/<id>/cars/<label>.ini (format: docs/MODDING.md "Adding cars").
//
// A definition names a donor car on the user's disc. The loader clones the
// donor's records (stock parts and the upgrade parts the shops sell), gives
// them the new car's labels, and writes the values the file defines over
// them: dimensions, weight, engine or electric motor, gearing, drive layout,
// tyre sizes. Everything the file leaves out (suspension geometry, steering,
// tyre compounds, aids) stays the donor's, so a mod never has to contain
// any data taken from the game. The new records go into the ordinary
// database tables, which makes the car work everywhere a disc car does.

#include "game/data/car_database.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r {
class Vfs;
}

namespace gt3r::text {
class JisTable;
}

namespace gt3r::data {

// One parsed definition file. Unset optional values keep the donor's.
struct ModCarDef {
    std::string label, name, maker, donor, model;
    std::string mod; // providing mod id (filled by the loader)
    std::string file; // host path (filled by the loader)
    std::optional<u16> year;
    std::optional<u32> price;
    std::optional<u8> category; // 0 production, 1 race, 2 rally
    bool fit_body = true;       // stretch a borrowed shell to the car's dimensions

    // [body]
    std::optional<u16> weight_kg, length_mm, width_mm, height_mm, wheelbase_mm, front_track_mm, rear_track_mm;
    std::optional<u8> front_weight_pct;
    std::optional<float> cd, frontal_area_m2;
    std::optional<u16> ride_height_mm;
    bool scale_suspension = true; // springs/dampers/bars scaled by the weight ratio to the donor
    bool scale_brakes = true;     // brake torque scaled by the weight ratio to the donor

    // [powertrain]
    bool electric = false;
    std::optional<float> power_kw, torque_nm, max_rpm, idle_rpm, base_rpm;
    std::optional<float> rated_kw; // shown in the specs when the curve's power differs from the rating
    std::vector<std::pair<float, float>> curve; // rpm, N*m (explicit curve)
    std::optional<float> regen_kw, regen_torque_nm, regen_g;
    std::optional<u16> sound_id; // combustion: a disc engine sound bank
    std::string layout_text, aspiration_text, displacement_text;
    std::optional<float> motor_inertia; // kg*m^2
    std::optional<u8> output_pct; // engine +0x42: hidden output factor (GT3 rates the R34 at 118)

    // [drivetrain]
    std::optional<u8> drive; // DriveType
    std::optional<u8> front_torque_pct;
    std::vector<float> gears; // forward ratios (electric: one, or none = direct)
    std::optional<float> final_drive, reverse;

    // [tires]
    struct TireSize {
        u8 width_mm_5 = 0, aspect_5 = 0, rim_in = 0; // stored /5 like the disc
    };
    std::optional<TireSize> front_tire, rear_tire;
    // Street-tyre grip vs GT3's era street compound (percent): the car's own factory tyres
    // (summer > touring all-season > low-rolling-resistance). Applies to its stock and arcade
    // tyres, not to the shop's upgrade compounds.
    std::optional<float> grip_pct;

    // [model] a body of its own (binary glTF), relative to the mod folder
    std::string model_file;
    float model_lower_mm = 0; // [model] lower_mm: move the body down (negative = up)
    std::string wheel; // [model] wheel: a rim from the disc (wheel/<label>) instead of the donor's
    // [paints] name = #RRGGBB, in menu order
    std::vector<std::pair<std::string, std::array<float, 3>>> paints;

    // [dealer] GT Mode: for_sale (default yes), description, and the mod's pictures
    // (paths relative to the mod folder): picture (640x448), thumb (51x27), logo (256x80)
    bool for_sale = true;
    std::string description, picture, thumb, logo;

    // [arcade] class: "auto" (by power-to-weight, as the game ranks garage cars), c, b, a,
    // s, dirt or none (not offered); race_variant: the class's harder (0) or easier (1)
    // race, -1 = by power-to-weight; level: arcade unlock level (0 = always open)
    std::string arcade_class = "auto";
    int arcade_variant = -1;
    int arcade_level = 0;

    // [sound]
    std::optional<float> whine_order, whine_level, inverter_level;

    // [raw.<table>] byte pokes on the car's stock record of a table.
    struct Poke {
        TableId table;
        size_t offset;
        char type; // 'b' u8, 'h' u16, 'i' u32, 'f' f32
        double value;
    };
    std::vector<Poke> pokes;
};

// Parses a definition. Returns nullopt with *error on a hard error;
// warnings (unknown keys) go to *warnings.
std::optional<ModCarDef> parse_mod_car(std::string_view text, std::string* error,
                                       std::vector<std::string>* warnings = nullptr);

// Adds the car to the database (cloning the donor's records). Call
// db.rebuild() afterwards; load_mod_car_files does. The definition's display texts (UTF-8:
// name, maker, engine texts, description, paint names) go in as game text, like the disc's
// own (text::encode_game_text): Latin letters as the game's Latin escapes, anything else
// through `jis` (font/jis2uni.dat) as JIS, or '?' without it.
bool add_mod_car(CarDatabase& db, const ModCarDef& def, std::string* error, const text::JisTable* jis = nullptr);

// Loads definition files into the database (later files replace a car with the
// same label). CarDatabase::load does this for every enabled mod, in priority
// order, from the VFS's mod car list (Vfs::kModCarsList). Errors are logged and
// kept in db.mod_car_errors().
void load_mod_car_files(CarDatabase& db, const std::vector<std::string>& files, const std::string& mod = {},
                        const text::JisTable* jis = nullptr);

// Loads a maker definition (mods/<id>/makers/<id>.ini, docs/MODDING.md):
//   [maker] name, region, description, icon, background, logo
// The name and description go in as game text (add_mod_car).
void load_mod_maker_file(CarDatabase& db, const std::string& file, const std::string& mod = {},
                         const text::JisTable* jis = nullptr);

// "255/35R21" <-> the stored size. nullopt on malformed text or sizes the
// disc format cannot hold (width and aspect must be multiples of 5).
std::optional<ModCarDef::TireSize> parse_tire_size(std::string_view s);

// Electric torque curve (rpm, N*m): constant torque up to the base speed
// (power / torque), constant power above it, fading to 85 % power at max_rpm.
std::vector<std::pair<float, float>> electric_curve(float torque_nm, float power_kw, float max_rpm, float base_rpm = 0);

} // namespace gt3r::data
