// Survey of the small data files the other surveys do not open:
//
//   misc-survey      database/carcolor.db + .sdb, race/particle.img, cameras/*.cam, demo/*, replay/*,
//                    every .mdl model and .prm scene
//
// Each file goes through the loader the engine uses for it; the summary says what it holds, so
// two discs' files can be compared.

#include "tool.h"

#include "formats/db/gt_db.h"
#include "formats/gs_memory.h"
#include "formats/model/gtm1.h"
#include "formats/model/prm_model.h"
#include "formats/tex1.h"
#include "race/demo_replay.h"
#include "race/paint.h"
#include "race/replay_cameras.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>

namespace gt3r::tool {

namespace {

// carcolor.db: every model's colour ids have a definition; the definitions' name words index carcolor.sdb.
void survey_carcolor(Volume& vol) {
    const Volume::Entry* e = vol.find("database/carcolor.db");
    if (!e) return;
    Bytes d = vol.read(*e);
    race::CarColors colors;
    if (d.size() < 0x18 || !has_magic(d, "GT2K") || !colors.parse(d)) {
        std::printf("database/carcolor.db: FAILED (%zu bytes)\n", d.size());
        return;
    }
    u32 models = rd32(d, 0x08), lists = rd32(d, 0x0C), defs = rd32(d, 0x10);
    size_t listed = 0, undefined = 0, max_list = 0;
    std::set<u32> def_ids;
    u32 ndefs = defs + 4 <= d.size() ? rd32(d, defs) : 0;
    u32 max_name = 0;
    for (u32 i = 0; i < ndefs && defs + 4 + 16ull * (i + 1) <= d.size(); ++i) {
        def_ids.insert(rd32(d, defs + 4 + 16 * i));
        max_name = std::max({max_name, rd32(d, defs + 8 + 16 * i), rd32(d, defs + 12 + 16 * i)});
    }
    for (u32 m = 0; m < models && 0x18 + 16ull * (m + 1) <= d.size(); ++m) {
        u32 count = rd32(d, 0x18 + 16 * m + 8), off = lists + (rd32(d, 0x18 + 16 * m + 12) & ~3u);
        max_list = std::max<size_t>(max_list, count);
        for (u32 k = 0; k < count && off + 4ull * (k + 1) <= d.size(); ++k, ++listed)
            undefined += !def_ids.count(rd32(d, off + 4 * k));
    }
    std::printf("database/carcolor.db: %u models, %zu colours listed (at most %zu a model, %zu without a definition), "
                "%zu definitions\n",
                models, listed, max_list, undefined, def_ids.size());
    if (const Volume::Entry* s = vol.find("database/carcolor.sdb")) try {
            db::StringTable names = db::StringTable::parse(vol.read(*s));
            std::printf("database/carcolor.sdb: %zu strings; the definitions' name words reach %u (%s)\n", names.size(),
                        max_name, max_name < names.size() ? "in range" : "OUT OF RANGE");
        } catch (const std::exception& ex) {
            std::printf("database/carcolor.sdb: FAILED %s\n", ex.what());
        }
}

void survey_image(Volume& vol, const char* path) {
    const Volume::Entry* e = vol.find(path);
    if (!e) return;
    Bytes d = vol.read(*e);
    try {
        Tex1 t = Tex1::parse(d);
        GsMemory gs;
        t.upload(d, gs);
        std::printf("%s: %zu bytes, %zu textures:", path, d.size(), t.textures.size());
        for (size_t i = 0; i < t.textures.size(); ++i) {
            Tex1::Image im = t.decode(gs, i);
            size_t lit = 0;
            for (u32 c : im.rgba) lit += (c & 0xFFFFFF) != 0;
            std::printf(" %ux%u (%zu%% lit)", im.width, im.height, im.rgba.empty() ? 0 : lit * 100 / im.rgba.size());
        }
        std::printf("\n");
    } catch (const std::exception& ex) {
        std::printf("%s: FAILED %s\n", path, ex.what());
    }
}

int cmd_misc_survey(Disc& disc, const Args&) {
    Volume& vol = disc.volume();
    survey_carcolor(vol);
    survey_image(vol, "race/particle.img");

    size_t demos = 0, demo_ok = 0, thumbs = 0, prm = 0, prm_ok = 0, mdl = 0, mdl_ok = 0, mdl_skinned = 0;
    std::map<std::string, size_t> demo_errors;
    std::map<size_t, size_t> thumb_sizes;
    struct PrmKind {
        size_t files = 0, keys_min = ~size_t(0), keys_max = 0, tris_min = ~size_t(0), tris_max = 0;
    };
    std::map<std::string, PrmKind> prm_kinds;
    for (const auto& e : vol.entries()) {
        if (e.path.empty() || e.path.back() == '/') continue;
        try {
            if (e.path.rfind("cameras/", 0) == 0) {
                Bytes d = vol.read(e);
                if (!race::GtrcScript::is_gtrc(d)) {
                    std::printf("%s: not a GTRC script (%zu bytes)\n", e.path.c_str(), d.size());
                    continue;
                }
                race::GtrcScript s = race::GtrcScript::parse(d);
                std::map<u32, size_t> kinds;
                for (const race::GtrcRecord& r : s.records) kinds[r.kind]++;
                std::printf("%s: %zu records, %zu stills; kinds", e.path.c_str(), s.records.size(), s.stills.size());
                for (auto& [k, n] : kinds) std::printf(" 0x%X x%zu", k, n);
                std::printf("\n");
            } else if (e.path.rfind("demo/thumbnail/", 0) == 0) {
                ++thumbs, thumb_sizes[e.size]++;
            } else if (e.path.rfind("demo/", 0) == 0 || e.path.rfind("replay/", 0) == 0) {
                ++demos;
                std::string err;
                if (race::DemoReplay::parse(vol.read(e), &err)) ++demo_ok;
                else demo_errors[err.empty() ? "not a replay" : err]++, std::printf("%s: %s\n", e.path.c_str(), err.c_str());
            } else if (e.path.size() > 4 && e.path.compare(e.path.size() - 4, 4, ".mdl") == 0) {
                // Plain GTM1 models (menu objects; other games' bike/ folder with its skinned riders).
                ++mdl;
                Bytes d = vol.read(e);
                if (!gtm1::Gtm1::is_gtm1(d)) {
                    std::printf("%s: not a GTM1 model (%zu bytes)\n", e.path.c_str(), d.size());
                    continue;
                }
                gtm1::Gtm1 g = gtm1::Gtm1::parse(d);
                size_t vertices = 0;
                for (const gtm1::Chunk& c : g.chunks)
                    for (const gtm1::Packet& pk : c.packets) vertices += g.decode_packet(pk).vertices.size();
                if (gtm1::decode_gtm1(d).meshes.empty()) throw FormatError("no geometry");
                ++mdl_ok;
                if (!g.inverse_bind.empty())
                    ++mdl_skinned, std::printf("%s: skinned, %zu bones, %zu vertices\n", e.path.c_str(), g.inverse_bind.size(), vertices);
            } else if (e.path.size() > 4 && e.path.compare(e.path.size() - 4, 4, ".prm") == 0) {
                ++prm;
                Bytes d = vol.read(e);
                prm::PrmFile f = prm::PrmFile::parse(d);
                scene::Model m = prm::load_prm_model(d);
                size_t tris = 0;
                for (const scene::Mesh& mesh : m.meshes) tris += mesh.indices.size() / 3;
                // One line per kind of scene: the name without its language folder and digits.
                std::string kind = e.path;
                for (const char* folder : {"menu/", "race/"})
                    if (kind.rfind(folder, 0) == 0 && kind.find('/', 5) != std::string::npos)
                        kind = folder + ("*" + kind.substr(kind.find('/', 5)));
                for (char& c : kind)
                    if (c >= '0' && c <= '9') c = '#';
                // race/<class>_<level>_<medal>.prm: the 24 trophies.
                if (std::count(kind.begin(), kind.end(), '_') == 2 && kind.find("race/") == 0)
                    kind = kind.substr(0, kind.rfind('/') + 1) + "<class>_<level>_<medal>.prm";
                PrmKind& k = prm_kinds[kind];
                ++k.files;
                k.keys_min = std::min(k.keys_min, f.model_keys.size() + f.view_keys.size());
                k.keys_max = std::max(k.keys_max, f.model_keys.size() + f.view_keys.size());
                k.tris_min = std::min(k.tris_min, tris), k.tris_max = std::max(k.tris_max, tris);
                ++prm_ok;
            }
        } catch (const std::exception& ex) {
            std::printf("%s: FAILED %s\n", e.path.c_str(), ex.what());
        }
    }
    std::printf("demo/ and replay/: %zu of %zu files read as replays", demo_ok, demos);
    for (auto& [k, n] : demo_errors) std::printf("; %zu x %s", n, k.c_str());
    std::printf("\ndemo/thumbnail/: %zu files", thumbs);
    for (auto& [sz, n] : thumb_sizes) std::printf(", %zu of %zu bytes", n, sz);
    std::printf("\n.mdl models: %zu of %zu read, %zu skinned", mdl_ok, mdl, mdl_skinned);
    std::printf("\n.prm scenes: %zu of %zu read\n", prm_ok, prm);
    for (auto& [kind, k] : prm_kinds)
        std::printf("  %-28s %3zu files, %zu-%zu keys, %zu-%zu triangles\n", kind.c_str(), k.files, k.keys_min, k.keys_max,
                    k.tris_min, k.tris_max);
    return 0;
}

} // namespace

GT3TOOL_COMMAND(misc_survey, "misc-survey", "",
                "read the paint lists, particle image, camera scripts, demo runs and prize scenes", 0, cmd_misc_survey);

} // namespace gt3r::tool
