#pragma once

// Exporting a disc car as a mod car (docs/MODDING.md "Exporting cars"): the player's own disc's
// car, whole, as a mod that another disc loads. Its use is moving a car between the releases the
// player owns: a Gran Turismo Concept car into Gran Turismo 3, or a car one GT3 release has and
// another lacks.
//
// The mod (one folder per source disc, mods/cars_<disc>/) gets per car:
//   cars/<label>.ini       name, maker, year, price, category, its paints ([paints], the disc's
//                          colour list in the model's order) and where the rest is
//   cars/<label>.records   every record of the car (game/data/mod_cars.h CarRecordFile): stock and
//                          upgrade parts, the car record, and the shared records they refer to
//                          (steering, aids, tyre sizes, compounds)
//   files/cars/{day,eve,night}/<model>, files/menu/cars/<model>
//                          its models (race and showroom), stock rims included
//   sounds/engine/<id>, sounds/<normalN|turboN>/<id>
//                          its engine sound banks
// Labels and model names get the disc's suffix (`<label>_<suffix>`), so an exported car never
// replaces a car or a model of the disc it is loaded on, and two discs' exports never collide.
// Gran Turismo Concept's longer records are cut to GT3's sizes (their appended bytes are not read).

#include "game/data/car_database.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r::data {

struct CarExport {
    std::string label;              // the exported car's label (<label>_<suffix>)
    std::vector<std::string> files; // written, relative to the mod folder
    std::vector<std::string> notes; // what the car keeps of its source and the target may lack
};

// The label suffix of a disc's exports, from its disc key (Vfs::disc_key): lower-case letters and
// digits ("SCES-50858" -> "sces50858", "us" -> "us").
std::string export_suffix(std::string_view disc_key);
// The mod folder name of a disc's exports: "cars_<suffix>".
std::string export_mod_id(std::string_view disc_key);

// Writes car `label` of `db` (the source disc's database, read through `read`) into the mod folder
// `mod_dir` (created, and its mod.ini when missing; `source` names the disc there). Names are written
// as UTF-8 (through the disc's font/jis2uni.dat). nullopt with *error when the car is not a disc car
// of `db` or a file cannot be written.
std::optional<CarExport> export_car(const CarDatabase& db, const CarDatabase::Reader& read, const std::string& label,
                                    const std::string& mod_dir, std::string_view disc_key, const std::string& source,
                                    std::string* error = nullptr);

} // namespace gt3r::data
