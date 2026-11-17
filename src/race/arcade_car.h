#pragma once

// Arcade cars (database table 35 variants) for a race: built with their own parts, the tyres
// following the car page's RACING / DRIFT choice. GT3 switches both tyre parts to the car's
// stage-10 ("_k", racing) or stage-11 ("_l", drift) records of tables 28/29; dirt courses force
// stage 12 ("_m"), the wet Route 5 (course category 1) adds 13 (wet variants "_n".."_y").

#include "sim/car_spec.h"

#include <optional>
#include <string>

namespace gt3r::data {
class CarDatabase;
}

namespace gt3r::race {

struct ArcadeCar {
    std::string label, car; // variant label, base car
    int stage = 10;
    sim::CarSpec spec;
};

// `id` = the menu's car id (the variant label, with or without its leading '_'); tire_mode
// 0 racing / 1 drift (-1 = racing). nullopt when `id` is not an arcade car.
std::optional<ArcadeCar> arcade_car(const data::CarDatabase& db, const std::string& id, int tire_mode,
                                    const std::string& course, std::string* error = nullptr);
// The same on the base car's tyre stage `stage` (0..11: 10 racing, 11 drift, else a tune-shop set; online cars, docs/dev/
// ONLINE.md "Tires and settings"); the course turns it as above (dirt 12, wet + 13). A stage the car has no records for
// keeps the variant's own tyres.
std::optional<ArcadeCar> arcade_car_on(const data::CarDatabase& db, const std::string& id, int stage,
                                       const std::string& course, std::string* error = nullptr);

} // namespace gt3r::race
