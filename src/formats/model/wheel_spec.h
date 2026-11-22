#pragma once

// Wheel placement and tyre size for a car model, from the parameter database
// (database/paramdb*.db): the car files themselves have no wheel positions.
//
//   wheelbase    table 3  (ch_) u16 mm at +0x18
//   front/rear tread  table 5 (rm_) u16 mm at +0x32 / +0x34, less twice the
//                per-side offset, s8 mm at +0x3D / +0x3E (the track, hub to hub)
//   tyre size    tables 28/29 (ft_/rt_) u64 size id at +0x18 -> table 25 (sz...):
//                u8 +0x08 rim diameter (inches), +0x09 aspect / 5, +0x0A width / 5 (mm)
//   width = w mm, sidewall = width * aspect %, outer radius = rim * 25.4 mm / 2 + sidewall
//
// Wheel centres: x = ±track / 2, y = outer radius, z = ∓wheelbase / 2 (front at -Z).

#include "formats/model/car_model.h"

#include <optional>
#include <string>

namespace gt3r {

namespace data {
class CarDatabase;
enum class CarScene : u8;
}

// Spec for the first car in the database that uses `model` (e.g. "am0006").
std::optional<WheelSpec> wheel_spec_from_database(const data::CarDatabase& db, const std::string& model);
// Spec for one car by label (use this when known: mod cars share their donor's model).
std::optional<WheelSpec> wheel_spec_for_car(const data::CarDatabase& db, const std::string& car_label);
// How a mod car's body differs from its car file: the stretch of a borrowed
// shell, or a glTF body of its own (read from the mod) with the paint
// colour for `paint` (an index into the mod's paint list, wrapped). `scene` picks the glTF
// body a mod with several draws (the menus' or a race's time of day; ModCar::model_files).
CarBodyMod body_mod_for_car(const data::CarDatabase& db, const std::string& car_label, int paint,
                            data::CarScene scene);

} // namespace gt3r
