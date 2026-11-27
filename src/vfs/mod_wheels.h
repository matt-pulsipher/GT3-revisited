#pragma once

// Mod wheels: mods/<id>/wheels/*.ini (docs/MODDING.md "Adding wheels"). Each file is a rim of
// the mod's own, a binary glTF model, under a label of its own:
//
//   [wheel]
//   label = my_rim          ; optional: the file name without .ini
//   brand = BBS             ; the wheel shop's maker: one of the disc's, or a new one
//   model = models/rim.glb  ; the rim, in the menus and the races
//   menu = models/rim_hi.glb ; optional: the menus' rim (else model, else race)
//   race = models/rim_lo.glb ; optional: the races' rim (else model, else menu)
//   picture = pictures/rim.png ; the wheel shop's and the garage's 52 x 52 tile
//   for_sale = yes          ; no: not at the wheel shop (a mod car's own, [model] wheel)
//   [brand]
//   logo = pictures/logo.png ; a new maker's 480 x 144 logo on the shop's list of makers
//
// The Vfs serves each loaded wheel's rims as the disc's wheels are kept: wheel/<label> (the
// races') and menu/wheel/<label> (the menus'), so everything that fits a wheel by label (GT
// Mode's garage and wheel shop, races, replays, a mod car's [model] wheel) draws it.
// formats/model/wheel_model.h loads a .glb rim as it loads the disc's GTTW ones.

#include "base/bytes.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {

struct ModWheelDef {
    std::string file;   // host path of the .ini
    std::string mod_id; // the mod holding it
    std::string label;  // lower case letters, digits and '_'
    std::string brand;  // as written
    std::string model, menu, race; // host paths (empty when not given)
    std::string picture;           // host path of the tile (empty: none)
    std::string brand_logo;        // host path ([brand] logo; empty: the name in the menu font)
    bool for_sale = true;
    std::string error; // why it does not load (empty: it does)

    // The rims the races and the menus draw (host paths, with the fallbacks above).
    const std::string& race_file() const { return !race.empty() ? race : !model.empty() ? model : menu; }
    const std::string& menu_file() const { return !menu.empty() ? menu : !model.empty() ? model : race; }
};

// Reads one definition (`root`: the mod folder its paths are relative to). A definition that
// cannot load keeps its label (when it has one) and says why in `error`.
ModWheelDef load_mod_wheel(const std::string& file, const std::string& mod_id, const std::string& root);

// Whether `label` can name a mod wheel: 1-32 lower case letters, digits and '_'.
bool valid_wheel_label(const std::string& label);

// The brand key the wheel shop matches makers by: lower case letters and digits ("OZ Racing" ->
// "ozracing"); `first_word`: of the first word only ("oz").
std::string wheel_brand_key(const std::string& brand, bool first_word = false);

// The enabled mods' wheels, through a reader (Vfs::kModWheelsList), in ascending mod priority;
// those that do not load included (ModWheelDef::error). A label given twice: the higher
// priority's (the other one's error says so).
using ModWheelReader = std::function<std::optional<Bytes>(const std::string&)>;
std::vector<ModWheelDef> mod_wheels(const ModWheelReader& read);

} // namespace gt3r
