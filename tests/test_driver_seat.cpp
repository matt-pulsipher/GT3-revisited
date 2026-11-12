// The seat a car's driver sits in, read from its body (race/driver_seat.h): on made-up bodies,
// and over every car of a real disc (GT3R_DISC), where it is checked against cars whose side is
// plain to see and scored against the makers' countries.

#include "test_framework.h"

#include "disc/disc.h"
#include "formats/model/car_model.h"
#include "formats/model/wheel_spec.h"
#include "game/data/car_database.h"
#include "game/gt_catalog.h"
#include "race/driver_seat.h"
#include "render/math3d.h"
#include "vfs/vfs.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

using namespace gt3r;
using namespace gt3r::race;

namespace {

// An axis-aligned box of 12 triangles.
void add_box(scene::Mesh& m, render::Vec3 lo, render::Vec3 hi) {
    const u32 base = u32(m.vertices.size());
    for (int i = 0; i < 8; ++i) {
        scene::Vertex v;
        v.pos[0] = (i & 1) ? hi.x : lo.x, v.pos[1] = (i & 2) ? hi.y : lo.y, v.pos[2] = (i & 4) ? hi.z : lo.z;
        m.vertices.push_back(v);
    }
    static const int kFaces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (const auto& f : kFaces)
        for (int k : {0, 1, 2, 0, 2, 3}) m.indices.push_back(base + u32(f[k]));
}

// A car-sized body (x right, y up, z back: the nose at -z) with boxes for its occupants.
scene::Model body_with(std::initializer_list<render::Vec3> heads, float head = 0.28f) {
    scene::Model model;
    scene::Mesh mesh;
    mesh.name = "body";
    add_box(mesh, {-0.85f, 0.15f, -2.2f}, {0.85f, 1.25f, 2.2f});
    for (const render::Vec3& c : heads)
        add_box(mesh, {c.x - head / 2, c.y - head / 2, c.z - head / 2}, {c.x + head / 2, c.y + head / 2, c.z + head / 2});
    mesh.primitives.push_back({0, u32(mesh.indices.size())});
    model.meshes.push_back(mesh);
    return model;
}

} // namespace

TEST(driver_seat_from_body) {
    // No driver: no side.
    DriverSeat none = find_driver_seat(body_with({}));
    CHECK(none.side == 0 && none.left == 0 && none.right == 0);
    CHECK(find_driver_seat(scene::Model{}).side == 0);
    // A driver in one seat.
    DriverSeat right = find_driver_seat(body_with({{0.34f, 0.95f, 0.2f}}));
    CHECK(right.side == 1 && right.left == 0 && right.right > 0.2f && std::fabs(right.x - 0.34f) < 0.03f);
    DriverSeat left = find_driver_seat(body_with({{-0.34f, 0.95f, 0.2f}}));
    CHECK(left.side == -1 && left.right == 0 && std::fabs(left.left - right.right) < 1e-4f && std::fabs(left.x + 0.34f) < 0.03f);
    // The same figure in both seats (a driver and a co-driver alike), or one on the centre line
    // (a single-seater): no side.
    CHECK(find_driver_seat(body_with({{0.34f, 0.95f, 0.2f}, {-0.34f, 0.95f, 0.2f}})).side == 0);
    CHECK(find_driver_seat(body_with({{0.0f, 0.95f, 0.2f}}, 0.12f)).side == 0);
    // Two unlike figures: the larger must be clearly the larger.
    CHECK(find_driver_seat(body_with({{0.34f, 0.95f, 0.2f}, {-0.34f, 0.9f, 0.5f}})).side == 0);
    // One-sided parts that are no driver: under the bonnet, in the boot, below the waist, on
    // the door, or too small.
    CHECK(find_driver_seat(body_with({{0.34f, 0.95f, -1.6f}})).side == 0);
    CHECK(find_driver_seat(body_with({{0.34f, 0.95f, 1.9f}})).side == 0);
    CHECK(find_driver_seat(body_with({{0.34f, 0.45f, 0.2f}})).side == 0);
    CHECK(find_driver_seat(body_with({{0.70f, 0.95f, 0.2f}})).side == 0);
    CHECK(find_driver_seat(body_with({{0.34f, 0.95f, 0.2f}}, 0.08f)).side == 0);
    // Wheels, other detail levels and the reflection pass are not the body.
    for (int variant = 0; variant < 3; ++variant) {
        scene::Model m = body_with({});
        scene::Mesh extra;
        add_box(extra, {0.2f, 0.81f, 0.06f}, {0.48f, 1.09f, 0.34f});
        extra.primitives.push_back({0, u32(extra.indices.size())});
        if (variant == 0) extra.name = "wheel_fl";
        if (variant == 1) extra.name = "body", extra.lod = 1;
        if (variant == 2) extra.name = "body", extra.primitives[0].flags = scene::kEnvironmentMap;
        m.meshes.push_back(extra);
        CHECK(find_driver_seat(m).side == 0);
    }
    // A mesh's own transform places it.
    scene::Model moved = body_with({});
    scene::Mesh figure;
    figure.name = "body";
    add_box(figure, {-0.14f, -0.14f, -0.14f}, {0.14f, 0.14f, 0.14f});
    figure.primitives.push_back({0, u32(figure.indices.size())});
    figure.transform = render::translate({-0.34f, 0.95f, 0.2f}).m;
    moved.meshes.push_back(figure);
    CHECK(find_driver_seat(moved).side == -1);
}

TEST(driver_seat_all_models_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) {
        if (const char* configured = std::getenv("GT3R_DISC"); configured && *configured) {
            CHECK(false);
            return;
        }
        SKIP("needs GT3R_DISC");
    }
    Vfs vfs(&disc->volume());
    data::CarDatabase db;
    bool loaded = db.load(vfs);
    CHECK(loaded);
    if (!loaded) return;
    CHECK(game::load_maker_countries([&](const std::string& p) { return vfs.read(p); }, db));
    std::map<std::string, DriverSeat> seats; // by car label
    size_t shown = 0, with_country = 0, agree = 0;
    for (const data::Car& car : db.cars()) {
        const std::string model = db.model_of(car);
        std::optional<Bytes> bytes = vfs.read("cars/day/" + model);
        if (!bytes || bytes->empty() || !CarFile::is_car(*bytes)) continue; // (the European release lacks some models)
        try {
            CarModelOptions options;
            options.wheels = false, options.effects = false, options.lod = 0;
            options.wheel_spec = wheel_spec_from_database(db, model);
            std::vector<gtm1::MorphDraws> morphs;
            options.morphs = &morphs;
            const DriverSeat seat = find_driver_seat(load_car_model(*bytes, options, model));
            seats[car.label] = seat;
            CHECK(std::isfinite(seat.x) && seat.left >= 0 && seat.right >= 0);
            if (!seat.side) continue;
            ++shown;
            // The driver is inside the car, on the side found.
            CHECK(seat.x * float(seat.side) > 0.1f && std::fabs(seat.x) < 0.7f);
            const data::DriverSide country = data::country_driver_side(db.maker_country(car));
            if (country == data::DriverSide::Unknown) continue;
            ++with_country;
            const bool same = (country == data::DriverSide::Right) == (seat.side > 0);
            agree += same;
            if (!same)
                std::printf("  %s: the driver on the %s, the maker's country (%s) on the other\n", car.label.c_str(),
                            seat.side > 0 ? "right" : "left", std::string(db.maker_country(car)).c_str());
        } catch (const std::exception& e) {
            std::printf("  driver seat exception: %s (%s): %s\n", car.label.c_str(), model.c_str(), e.what());
            CHECK(false);
        }
    }
    std::printf("  driver seats: %zu of %zu bodies show their driver's side; the maker's country agrees on %zu of %zu\n", shown,
                seats.size(), agree, with_country);
    // Most bodies show it, and the makers' countries (the fallback) agree with most of those.
    CHECK(seats.size() > 200 && shown * 10 >= seats.size() * 8);
    CHECK(agree * 100 >= with_country * 85);
    // Cars whose side is plain to see in the game. The MX-5 is one model on every release, its
    // driver on the right; the two NSXs differ; a single-seater has no side.
    const auto side = [&](const char* label, int want) {
        if (auto it = seats.find(label); it != seats.end()) CHECK(it->second.side == want);
    };
    for (const char* l : {"r34_gtr_v_99", "roadster_rs_00", "griffith_500_00", "mini_cooper_13i_00", "nsx_r_92", "gto_tt_99"}) side(l, 1);
    for (const char* l : {"corvette_z06_00", "cobra_67", "acura_nsx_93", "viper_gts_99", "xkr_coupe_00", "zonda_c12_00"}) side(l, -1);
    for (const char* l : {"mclaren_mp4_4", "williams_fw11", "lotus_99t"}) side(l, 0);
}
