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
//                          upgrade parts (ExportOptions: given a shop when its disc sells it
//                          none), the car record, and the shared records they refer to
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
    std::string shop_from;          // the car whose tuning parts it was given (shop_parts), else empty
    int shop_parts = 0;             // how many
};

// Tuning parts for a car its disc sells none for. Gran Turismo Concept sells no tuning, so its cars
// come without the parts GT3's tune shop sells. The export then gives such a car the shop of the
// most alike car on its disc that has one (the same category, drive layout and aspiration, then the
// nearest power, weight and gear count; a car with the same engine and chassis first): each table
// the car sells nothing in gets that car's upgrades, relabelled to the car and fitted to it (brakes
// scaled to its own, gearboxes on its gear count, final drive and reverse, suspension kits scaled to
// its axle loads around its own ride heights, tyres on its own sizes). Parts the car already holds
// without a price (Concept's own arcade tuning parts) keep their values and take that car's price.
struct ExportOptions {
    // Auto: on Gran Turismo Concept discs only, so a GT3 car moved between GT3 releases (and GT3's own
    // cars without a shop, the F1 cars among them) stays as GT3 made it.
    enum class Shop : u8 { Auto, Fill, Keep } shop = Shop::Auto;
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
                                    std::string* error = nullptr, const ExportOptions& options = {});

} // namespace gt3r::data
