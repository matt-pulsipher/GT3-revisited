// gt3tool livery-template: a livery's template from this disc, or of a mod car from its mod
// (race/livery_template.h).
#include "tool.h"

#include "formats/png_writer.h"
#include "game/data/car_database.h"
#include "race/paint.h"
#include "race/livery_template.h"
#include "vfs/vfs.h"

#include <cstdio>
#include <string>

namespace gt3r::tool {

namespace {

int cmd_livery_template(Disc& disc, const Args& a) {
    const std::string& model = a[0];
    const std::string& out = a[1];
    int paint = 0, scale = 4;
    std::string mods;
    for (size_t i = 2; i < a.size(); ++i) {
        if (a[i].rfind("--paint=", 0) == 0) paint = std::stoi(a[i].substr(8));
        else if (a[i].rfind("--scale=", 0) == 0) scale = std::stoi(a[i].substr(8));
        else if (a[i].rfind("--mods=", 0) == 0) mods = a[i].substr(7);
        else throw FormatError("unknown option " + a[i]);
    }
    std::string err;
    std::optional<race::LiveryTemplate> t;
    if (model.rfind(race::kModPaintKey, 0) == 0) { // a mod car
        if (mods.empty()) throw FormatError("a mod car needs --mods=<mods folder>");
        Vfs vfs(&disc.volume());
        vfs.scan_mods(mods, nullptr);
        data::CarDatabase db;
        if (!db.load(vfs)) throw FormatError(db.player_error());
        for (const std::string& e : db.mod_car_errors()) std::printf("%s\n", e.c_str());
        t = race::make_livery_template(vfs, db, model, paint, scale, &err);
    } else {
        Bytes data = disc.volume().read(need(disc.volume(), "cars/day/" + model));
        t = race::make_livery_template(data, model, paint, scale, &err);
    }
    if (!t) {
        std::printf("%s\n", err.c_str());
        return 1;
    }
    bool ok = save(out + "/livery.ini", ByteSpan(reinterpret_cast<const u8*>(t->ini.data()), t->ini.size()));
    for (const race::LiveryTemplate::Picture& p : t->canvases) ok = save(out + "/" + p.file, encode_png(p.width, p.height, p.rgba)) && ok;
    for (const race::LiveryTemplate::Picture& p : t->guides)
        ok = save(out + "/guides/" + p.file, encode_png(p.width, p.height, p.rgba)) && ok;
    size_t body = 0;
    for (const race::LiveryTemplate::Picture& p : t->canvases) body += p.body;
    std::printf("%s in paint %d: %zu canvases (%zu the paints recolour), guides in guides/\n", model.c_str(), paint,
                t->canvases.size(), body);
    return ok ? 0 : 1;
}

} // namespace

GT3TOOL_COMMAND(livery_template, "livery-template", "<model | mod:label> <outdir> [--paint=N] [--scale=4] [--mods=<mods folder>]",
                "write a livery template for a car from this disc, or a mod car (canvases, guides, livery.ini)", 2, cmd_livery_template);

} // namespace gt3r::tool
