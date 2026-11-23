// gt3tool livery-template: a livery's template from this disc (race/livery_template.h).
#include "tool.h"

#include "formats/png_writer.h"
#include "race/livery_template.h"

#include <cstdio>
#include <string>

namespace gt3r::tool {

namespace {

int cmd_livery_template(Disc& disc, const Args& a) {
    const std::string& model = a[0];
    const std::string& out = a[1];
    int paint = 0, scale = 4;
    for (size_t i = 2; i < a.size(); ++i) {
        if (a[i].rfind("--paint=", 0) == 0) paint = std::stoi(a[i].substr(8));
        else if (a[i].rfind("--scale=", 0) == 0) scale = std::stoi(a[i].substr(8));
        else throw FormatError("unknown option " + a[i]);
    }
    Bytes data = disc.volume().read(need(disc.volume(), "cars/day/" + model));
    std::string err;
    std::optional<race::LiveryTemplate> t = race::make_livery_template(data, model, paint, scale, &err);
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
    std::printf("%s in paint %d: %zu canvases (%zu the paints recolour) at %dx, guides in guides/\n", model.c_str(), paint,
                t->canvases.size(), body, scale);
    return ok ? 0 : 1;
}

} // namespace

GT3TOOL_COMMAND(livery_template, "livery-template", "<model> <outdir> [--paint=N] [--scale=4]",
                "write a livery template for a car from this disc (canvases, guides, livery.ini)", 2, cmd_livery_template);

} // namespace gt3r::tool
