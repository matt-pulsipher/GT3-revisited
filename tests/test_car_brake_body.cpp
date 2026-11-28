// The brake body made along with the car is the one a load of its own makes, and the wheels alone are the wheels of a
// whole car; cars built together, sharing what their bytes decide, are the cars built alone.
#include "test_framework.h"
#include "formats/model/build_cache.h"
#include "base/parallel_for.h"
#include "disc/disc.h"
#include "formats/model/car_model.h"
#include "formats/model/gltf_export.h"
#include "formats/model/wheel_spec.h"
#include "game/data/car_database.h"
#include "vfs/vfs.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <set>

using namespace gt3r;

namespace {

bool same_mesh(const scene::Mesh& x, const scene::Mesh& y) {
    if (x.name != y.name || x.lod != y.lod || x.node != y.node || x.vertices.size() != y.vertices.size() || x.indices != y.indices ||
        x.primitives.size() != y.primitives.size())
        return false;
    if (!x.vertices.empty() &&
        std::memcmp(x.vertices.data(), y.vertices.data(), x.vertices.size() * sizeof(scene::Vertex)) != 0)
        return false;
    for (size_t k = 0; k < x.primitives.size(); ++k) {
        const scene::Primitive &p = x.primitives[k], &q = y.primitives[k];
        if (p.first_index != q.first_index || p.index_count != q.index_count || p.texture != q.texture || p.flags != q.flags ||
            p.alpha_ref != q.alpha_ref || p.under_ref != q.under_ref || p.under_alpha != q.under_alpha ||
            p.env_rows != q.env_rows || p.env_kind != q.env_kind ||
            std::memcmp(p.env_c0, q.env_c0, sizeof p.env_c0) != 0 || std::memcmp(p.env_c1, q.env_c1, sizeof p.env_c1) != 0 ||
            std::memcmp(p.uv_region, q.uv_region, sizeof p.uv_region) != 0 ||
            std::memcmp(&p.material, &q.material, offsetof(scene::Material, valid)) != 0 ||
            p.material.valid != q.material.valid || p.material.specular_local != q.material.specular_local)
            return false;
    }
    return true;
}

bool same_model(const scene::Model& a, const scene::Model& b) {
    if (a.meshes.size() != b.meshes.size() || a.textures.size() != b.textures.size()) return false;
    for (size_t i = 0; i < a.textures.size(); ++i) {
        const scene::Texture &s = a.textures[i], &t = b.textures[i];
        if (s.name != t.name || s.width != t.width || s.height != t.height || s.clamp_u != t.clamp_u || s.clamp_v != t.clamp_v ||
            s.no_mips != t.no_mips || s.nearest != t.nearest || s.rgba != t.rgba)
            return false;
    }
    for (size_t i = 0; i < a.meshes.size(); ++i)
        if (!same_mesh(a.meshes[i], b.meshes[i])) return false;
    return true;
}

bool body_only(const scene::Mesh& m) { return m.name.rfind("node", 0) != 0; }

} // namespace

TEST(car_brake_body_is_made_along_with_the_car_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    std::set<std::string> seen;
    size_t cars = 0, bad_lit = 0, bad_car = 0, bad_wheels = 0;
    for (const data::Car& c : db.cars()) {
        const std::string model = db.model_of(c);
        if (!seen.insert(model).second) continue;
        std::optional<Bytes> file = vfs.read("cars/day/" + model);
        if (!file || file->empty() || !CarFile::is_car(*file)) continue;
        ++cars;
        // Every 20th car also as a mod car's: stretched, and with a glTF body (its own, exported and put back).
        std::vector<CarBodyMod> mods(1);
        if (cars % 20 == 1) {
            CarBodyMod stretched;
            stretched.fit = BodyFit{4000, 2500, 1700, 1300, 4400, 2700, 1800, 1250};
            CarModelOptions plain;
            plain.wheels = false;
            CarBodyMod replaced;
            replaced.glb = std::make_shared<const Bytes>(export_glb(load_car_model(*file, plain, model)));
            replaced.glb_ground = 0.3f;
            mods.push_back(stretched);
            mods.push_back(replaced);
        }
        for (const auto& [paint, mod] : [&] {
                 std::vector<std::pair<int, CarBodyMod>> runs;
                 for (const CarBodyMod& m : mods) runs.push_back({0, m});
                 if (cars % 3 == 0) runs.push_back({2, {}});
                 return runs;
             }()) {
            CarModelOptions opt;
            opt.lod = -1, opt.wheels = true, opt.effects = true, opt.paint = paint, opt.even_menu_rim_light = true;
            opt.body_mod = mod;
            opt.wheel_spec = wheel_spec_from_database(db, model).value_or(WheelSpec{});
            std::vector<gtm1::MorphDraws> morphs, morphs_with;
            std::vector<gtm1::ClutSet> cluts, cluts_with;
            // The car, then its brake body by a load of its own.
            opt.morphs = &morphs, opt.clut_sets = &cluts;
            const scene::Model car = load_car_model(*file, opt, model);
            CarModelOptions bo = opt;
            bo.brake = true, bo.wheels = false, bo.effects = false, bo.clut_sets = nullptr;
            std::vector<gtm1::MorphDraws> lit_morphs;
            bo.morphs = &lit_morphs;
            const scene::Model alone = scene::extract(load_car_model(*file, bo, model), body_only);
            // Both in one load.
            scene::Model lit;
            opt.morphs = &morphs_with, opt.clut_sets = &cluts_with, opt.brake_body = &lit;
            const scene::Model car_with = load_car_model(*file, opt, model);
            const scene::Model along = scene::extract(lit, body_only);
            if (!same_model(alone, along)) ++bad_lit, std::fprintf(stderr, "  %s paint %d: brake body differs\n", model.c_str(), paint);
            bool same = same_model(car, car_with) && morphs.size() == morphs_with.size() && cluts.size() == cluts_with.size();
            for (size_t i = 0; same && i < morphs.size(); ++i) {
                const auto a = morphs[i].meshes(gtm1::kCarCallbackDefaults), b = morphs_with[i].meshes(gtm1::kCarCallbackDefaults);
                same = a.size() == b.size();
                for (size_t k = 0; same && k < a.size(); ++k) same = same_mesh(a[k], b[k]);
            }
            for (size_t i = 0; same && i < cluts.size(); ++i) same = cluts[i].first == cluts_with[i].first;
            if (!same) ++bad_car, std::fprintf(stderr, "  %s paint %d: car differs\n", model.c_str(), paint);
            // The wheels alone.
            CarModelOptions ro = opt;
            ro.lod = 0, ro.effects = false, ro.morphs = nullptr, ro.clut_sets = nullptr, ro.brake_body = nullptr;
            auto wheel = [](const scene::Mesh& m) { return m.name.rfind("wheel_", 0) == 0; };
            const scene::Model whole = scene::extract(load_car_model(*file, ro, model), wheel);
            ro.body = false, ro.body_mod = {}, ro.resolve_paint = false; // (as CarVisual asks for them)
            const scene::Model only = scene::extract(load_car_model(*file, ro, model), wheel);
            if (!same_model(whole, only) || only.meshes.empty())
                ++bad_wheels, std::fprintf(stderr, "  %s paint %d: wheels differ (%zu meshes)\n", model.c_str(), paint, only.meshes.size());
        }
    }
    CHECK(cars > 200);
    CHECK(bad_lit == 0);
    CHECK(bad_car == 0);
    CHECK(bad_wheels == 0);
}

TEST(cars_built_together_are_the_cars_built_alone_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    CHECK(db.load(vfs));
    // Every sixth model, each in two paints (one model's cars share the most).
    struct Build {
        std::string model;
        Bytes file;
        int paint = 0;
    };
    std::vector<Build> builds;
    std::set<std::string> seen;
    for (const data::Car& c : db.cars()) {
        const std::string model = db.model_of(c);
        if (!seen.insert(model).second || seen.size() % 6 != 0) continue;
        std::optional<Bytes> file = vfs.read("cars/day/" + model);
        if (!file || file->empty() || !CarFile::is_car(*file)) continue;
        builds.push_back({model, *file, 0});
        builds.push_back({model, *file, 1}); // (a copy: another address)
    }
    CHECK(builds.size() > 60);
    auto load = [&](const Build& b, gtm1::BuildCache* cache) {
        CarModelOptions opt;
        opt.lod = -1, opt.wheels = true, opt.effects = true, opt.paint = b.paint, opt.even_menu_rim_light = true;
        opt.wheel_spec = wheel_spec_from_database(db, b.model).value_or(WheelSpec{});
        opt.cache = cache;
        return load_car_model(b.file, opt, b.model);
    };
    gtm1::SharedBuilds shared;
    std::vector<scene::Model> together(builds.size());
    parallel_for(builds.size(), 5, [&](size_t i) {
        gtm1::BuildCache cache(&shared);
        together[i] = load(builds[i], &cache);
    });
    size_t differ = 0;
    for (size_t i = 0; i < builds.size(); ++i)
        if (!same_model(load(builds[i], nullptr), together[i]))
            ++differ, std::fprintf(stderr, "  %s paint %d differs\n", builds[i].model.c_str(), builds[i].paint);
    CHECK(differ == 0);
}
