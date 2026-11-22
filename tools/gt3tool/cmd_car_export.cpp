// gt3tool car-export: a disc car, whole, as a mod car another disc loads (game/data/car_export.h,
// docs/MODDING.md "Exporting cars").
//
//   gt3tool <iso> car-export <label | all> <mods dir>
//
// Writes into <mods dir>/cars_<disc>/ (one mod per source disc).

#include "tool.h"

#include "game/data/car_database.h"
#include "game/data/car_export.h"
#include "vfs/vfs.h"

#include <cstdio>

namespace gt3r::tool {

namespace {

int cmd_car_export(Disc& disc, const Args& a) {
    Vfs vfs(&disc.volume());
    data::CarDatabase db;
    if (!db.load(vfs)) {
        std::fprintf(stderr, "car database: %s\n", db.player_error().c_str());
        return 1;
    }
    const std::string key = vfs.disc_key();
    const std::string mod_dir = a[1] + "/" + data::export_mod_id(key);
    const std::string source = disc.info().description.empty() ? disc.info().serial : disc.info().description;
    auto read = [&](const std::string& p) { return vfs.read(p); };
    std::vector<std::string> labels;
    if (a[0] == "all") {
        for (const data::Car& c : db.cars())
            if (!db.mod_car(c.label)) labels.push_back(c.label);
    } else {
        labels.push_back(a[0]);
    }
    int failed = 0;
    for (const std::string& l : labels) {
        std::string err;
        std::optional<data::CarExport> e = data::export_car(db, read, l, mod_dir, key, source, &err);
        if (!e) {
            std::fprintf(stderr, "%s: %s\n", l.c_str(), err.c_str());
            ++failed;
            continue;
        }
        std::printf("%s -> %s (%zu files)\n", l.c_str(), e->label.c_str(), e->files.size());
        for (const std::string& n : e->notes) std::printf("  note: %s\n", n.c_str());
    }
    std::printf("%zu exported to %s\n", labels.size() - size_t(failed), mod_dir.c_str());
    return failed ? 1 : 0;
}

} // namespace

GT3TOOL_COMMAND(car_export, "car-export", "<label | all> <mods dir>", "export disc cars as a mod another disc loads", 2,
                cmd_car_export);

} // namespace gt3r::tool
