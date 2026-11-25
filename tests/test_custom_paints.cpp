// Custom paint colours (race/custom_paints.h, gtm1::ComputedPaint; docs/dev/CUSTOM_PAINTS.md).
#include "test_framework.h"
#include "disc/disc.h"
#include "formats/db/gt_db.h"
#include "formats/model/car_model.h"
#include "formats/model/gltf.h"
#include "formats/model/gtm1.h"
#include "formats/model/wheel_spec.h"
#include "game/data/car_database.h"
#include "game/livery_package.h"
#include "game/mod_package.h"
#include "game/net_car.h"
#include "base/sha256.h"
#include "formats/png_reader.h"
#include "vfs/vfs.h"
#include "base/file.h"
#include "formats/png_writer.h"
#include "formats/texture_pack.h"
#include "race/custom_paints.h"
#include "race/livery_template.h"
#include "race/replay.h"
#include "replay_fixtures.h"

#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <algorithm>

using namespace gt3r;

namespace {

u32 rgb(int r, int g, int b) { return u32(r) | u32(g) << 8 | u32(b) << 16; }
int ch(u32 c, int k) { return int(c >> (8 * k) & 0xFF); }

} // namespace

TEST(custom_paint_texels_fit) {
    // Three paints; texel 0 is the body colour itself (s = 1), texel 1 a shadow of it (s = 1/2), texel 2 a highlight
    // (s = 1/2, h = 100), texel 3 trim (never changes) and texel 4 carries the reflectivity in alpha.
    const std::vector<u32> body = {rgb(200, 0, 0), rgb(0, 200, 0), rgb(0, 0, 200)};
    std::vector<std::vector<u32>> by;
    for (u32 c : body) {
        auto f = [&](double s, double h) { return rgb(int(ch(c, 0) * s + h), int(ch(c, 1) * s + h), int(ch(c, 2) * s + h)) | 0xFF000000u; };
        by.push_back({f(1, 0), f(0.5, 0), f(0.5, 100), rgb(30, 30, 30) | 0xFF000000u, (f(1, 0) & 0xFFFFFF) | (0x40u + u32(by.size())) << 24});
    }
    // Each paint given back.
    for (int k = 0; k < 3; ++k) {
        std::vector<u32> got = gtm1::computed_paint_texels(by, body, body[size_t(k)], k);
        for (size_t i = 0; i < got.size(); ++i)
            for (int c = 0; c < 3; ++c) CHECK(std::abs(ch(got[i], c) - ch(by[size_t(k)][i], c)) <= 1);
        CHECK(got[4] >> 24 == 0x40u + u32(k));
    }
    // A new colour.
    std::vector<u32> out = gtm1::computed_paint_texels(by, body, rgb(100, 150, 50), 1);
    CHECK(out.size() == 5);
    auto near = [](u32 c, int r, int g, int b) { return std::abs(ch(c, 0) - r) <= 1 && std::abs(ch(c, 1) - g) <= 1 && std::abs(ch(c, 2) - b) <= 1; };
    CHECK(near(out[0], 100, 150, 50));
    CHECK(near(out[1], 50, 75, 25));
    CHECK(near(out[2], 150, 175, 125));
    CHECK(out[3] == (rgb(30, 30, 30) | 0xFF000000u));
    CHECK(out[4] >> 24 == 0x41u); // the finish paint's alpha
    // Sizes that do not match: the finish paint as it is.
    CHECK(gtm1::computed_paint_texels(by, {body[0], body[1]}, rgb(1, 2, 3), 1) == by[1]);
}

TEST(custom_paint_body_colours) {
    // Texture 0 changes in texels 0 and 1, texture 1 never (one entry: skipped).
    std::vector<std::vector<std::vector<u32>>> t = {
        {{rgb(100, 0, 0), rgb(50, 0, 0), rgb(9, 9, 9)}, {rgb(0, 100, 0), rgb(0, 50, 0), rgb(9, 9, 9)}},
        {{rgb(1, 1, 1)}},
    };
    std::vector<u32> body = gtm1::paint_body_colours(t);
    CHECK(body.size() == 2 && body[0] == rgb(75, 0, 0) && body[1] == rgb(0, 75, 0));
    // The new colour's body colour: by the factor the labels show (here, half).
    gtm1::ComputedPaint p;
    p.paint_rgb = {rgb(150, 0, 0), rgb(0, 150, 0)};
    p.rgb = rgb(200, 100, 0);
    CHECK(gtm1::computed_body_colour(p, body) == rgb(100, 50, 0));
}

TEST(custom_paint_ini) {
    std::string err;
    auto p = race::parse_custom_paint("[livery]\nname = Sunday Blue ; a comment\nauthor = me\nmodel = AM0006\ncolor = #1E3A8A\nfinish = 4\n", &err);
    CHECK(p.has_value());
    if (!p) return;
    CHECK(p->name == "Sunday Blue" && p->author == "me" && p->model == "am0006");
    CHECK(p->rgb == rgb(0x1E, 0x3A, 0x8A) && p->finish == 4);
    CHECK(race::rgb_text(p->rgb) == "#1E3A8A");
    auto q = race::parse_custom_paint("[livery]\nmodel = am0006\ncolour = 102030\n", &err);
    CHECK(q && q->finish == -1 && q->name == "#102030");
    auto bare = race::parse_custom_paint("[livery]\nmodel = am0006\npaint = 2\n", &err); // (a livery's: its pictures on paint 2)
    CHECK(bare && !bare->colour && bare->finish == 2);
    CHECK(!race::parse_custom_paint("[livery]\ncolor = #102030\n", &err));
    CHECK(!race::parse_custom_paint("[livery]\nmodel = am0006\ncolor = #10203\n", &err));
    CHECK(!race::parse_custom_paint("[livery]\nmodel = ../x\ncolor = #102030\n", &err));
    CHECK(!race::parse_custom_paint("[livery]\nmodel = am0006\ncolor = #102030\nfinish = -1\n", &err));
    CHECK(!race::parse_rgb("#GG0000"));
}

TEST(custom_paint_list) {
    // A carcolor.db with one model of two paints (ids 7 and 9).
    Bytes cc(0x100, 0);
    auto put32 = [&](size_t o, u32 v) { for (int k = 0; k < 4; ++k) cc[o + size_t(k)] = u8(v >> (8 * k)); };
    auto put64 = [&](size_t o, u64 v) { put32(o, u32(v)), put32(o + 4, u32(v >> 32)); };
    put32(0x08, 1), put32(0x0C, 0x40), put32(0x10, 0x60);
    put64(0x18, db::id_hash("am0006"));
    put32(0x20, 2), put32(0x24, 0);
    put32(0x40, 7), put32(0x44, 9);
    put32(0x60, 2);
    put32(0x64, 7), put32(0x70, rgb(200, 0, 0));
    put32(0x74, 9), put32(0x80, rgb(0, 0, 200));
    race::CarColors colors;
    CHECK(colors.parse(cc));
    race::CustomPaints paints(colors);
    race::CustomPaint p;
    p.model = "am0006", p.rgb = rgb(10, 10, 180), p.name = "Deep";
    int index = -1;
    CHECK(paints.add(p, nullptr, &index));
    CHECK(index == 2 && paints.disc_count("am0006") == 2);
    CHECK(paints.at("am0006", 2) && paints.at("am0006", 2)->finish == 1); // the nearest paint's finish
    CHECK(!paints.at("am0006", 1) && !paints.at("am0006", 3));
    auto c = paints.computed("am0006", 2);
    CHECK(c && c->rgb == p.rgb && c->finish == 1 && c->paint_rgb.size() == 2);
    CHECK(paints.index_of("am0006", p.rgb, 1) == 2 && paints.index_of("am0006", p.rgb, 0) == -1);
    p.finish = 2;
    CHECK(!paints.add(p));    // not one of its paints
    p.model = "zz0001", p.finish = -1;
    CHECK(!paints.add(p));    // no paints on this disc

    // Received online: added once for the session, found again after.
    race::set_custom_paints(std::make_shared<race::CustomPaints>(paints));
    CHECK(race::received_paint("am0006", rgb(10, 10, 180), 1) == 2);
    CHECK(race::received_paint("am0006", rgb(1, 2, 3), 0, "Pat") == 3);
    CHECK(race::received_paint("am0006", rgb(1, 2, 3), 0) == 3);
    CHECK(race::received_paint("am0006", rgb(1, 2, 3), 5) == -1);
    CHECK(race::custom_paints()->at("am0006", 3)->from == "Pat");
    CHECK(resolve_paint("am0006", 3).has_value() && resolve_paint("am0006", 3)->computed && !resolve_paint("am0006", 1));
    race::set_custom_paints(nullptr);
    CHECK(!resolve_paint("am0006", 3));
}

// Leave-one-out: each car's paint k computed from its other paints, against the disc's own paint k. Reports the mean
// error per channel (8-bit units) over the texels that change with the paint, per car and over every car.
TEST(custom_paint_leave_one_out_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Volume& vol = disc->volume();
    race::CarColors colors;
    {
        auto e = vol.find("database/carcolor.db");
        CHECK(e != nullptr);
        if (!e || !colors.parse(vol.read(*e))) return;
    }
    const char* only = std::getenv("GT3R_PAINT_CAR"); // one model, for a closer look
    double total = 0, worst = 0;
    long texels = 0;
    int cars = 0;
    std::string worst_car;
    std::vector<double> per_car;
    for (const std::string& name : vol.list_dir("cars/day/")) {
        std::string model = name.substr(name.rfind('/') + 1);
        if (only && model != only) continue;
        const std::vector<u32>* ids = colors.ids(model);
        if (!ids || ids->size() < 3) continue;
        auto e = vol.find("cars/day/" + model);
        if (!e) continue;
        Bytes data = vol.read(*e);
        CarFile f = CarFile::parse(data);
        if (f.body.empty()) continue;
        gtm1::Gtm1 g = gtm1::Gtm1::parse(f.body);
        if (g.texsets.empty()) continue;
        ByteSpan set = f.body.subspan(g.texsets[0]);
        const size_t n = ids->size();
        if (gtm1::paint_variants(set).size() != n) continue;
        std::vector<std::vector<scene::Texture>> by(n);
        for (size_t v = 0; v < n; ++v) gtm1::decode_texture_set(set, by[v], "", int(v), nullptr, false);
        double err = 0;
        long count = 0;
        std::vector<std::vector<std::vector<u32>>> tex(by[0].size());
        for (size_t t = 0; t < tex.size(); ++t) {
            for (size_t v = 0; v < n; ++v) tex[t].push_back(by[v][t].rgba);
            bool changes = false;
            for (size_t v = 1; v < n && !changes; ++v) changes = tex[t][v] != tex[t][0];
            if (!changes) tex[t].resize(1);
        }
        const std::vector<u32> body = gtm1::paint_body_colours(tex);
        for (size_t k = 0; k < n; ++k) {
            // Paint k from the others, given its body colour (what the player asks for).
            std::vector<size_t> rest;
            std::vector<u32> rest_body;
            for (size_t v = 0; v < n; ++v)
                if (v != k) rest.push_back(v), rest_body.push_back(body[v]);
            const int finish = race::nearest_paint(rest_body, body[k]);
            for (size_t t = 0; t < tex.size(); ++t) {
                if (tex[t].size() == 1) continue;
                std::vector<std::vector<u32>> texels;
                for (size_t v : rest) texels.push_back(tex[t][v]);
                std::vector<u32> got = gtm1::computed_paint_texels(texels, rest_body, body[k], finish);
                const std::vector<u32>& want = tex[t][k];
                for (size_t i = 0; i < want.size(); ++i) {
                    bool moves = false;
                    for (size_t v = 0; v < n && !moves; ++v) moves = (tex[t][v][i] & 0xFFFFFF) != (want[i] & 0xFFFFFF);
                    if (!moves) continue;
                    for (int c = 0; c < 3; ++c) err += std::abs(ch(got[i], c) - ch(want[i], c));
                    count += 3;
                }
            }
        }
        if (!count) continue;
        const double mean = err / double(count);
        if (only) std::printf("  %s: %zu paints, mean error %.2f\n", model.c_str(), n, mean);
        if (mean > worst) worst = mean, worst_car = model;
        total += err, texels += count, ++cars;
        per_car.push_back(mean);
    }
    CHECK(cars > 0);
    if (!cars) return;
    std::printf("  leave-one-out: %d cars, mean error %.2f per channel, worst %s %.2f\n", cars, total / double(texels),
                worst_car.c_str(), worst);
    std::sort(per_car.begin(), per_car.end());
    std::printf("  per car: median %.2f, 90th %.2f\n", per_car[per_car.size() / 2], per_car[per_car.size() * 9 / 10]);
    // US 1.10: 170 cars, mean 7.6, median car 4.1 (showing the nearest paint instead: mean 26.9, median 29.5). The race
    // cars whose paints are whole liveries (wl0001, ml0002) cannot be predicted from each other and set the mean.
    CHECK(total / double(texels) < 10);
    CHECK(per_car[per_car.size() / 2] < 6);
}

TEST(custom_paint_online_car) {
    // A disc car in a custom colour: the colour after the lobby tires (flag 2), `paint` its finish.
    game::OnlineCar c;
    c.kind = game::OnlineCar::Kind::Disc;
    c.car = "r34_gtr_v_99";
    c.paint = 4;
    c.digest.emplace();
    c.digest->fill(0x11);
    game::OnlineCar plain = c;
    plain.tires = -1;
    c.color = rgb(0x1E, 0x3A, 0x8A);
    Bytes b = game::encode_online_car(c);
    std::optional<game::OnlineCar> d = game::decode_online_car(b);
    CHECK(d && d->color == c.color && d->paint == 4 && d->tires == -1 && !d->tune);
    // A game before colours reads the same bytes up to the settings: the bytes without the colour are a prefix, but for
    // the flags byte, so it sees the car in its finish paint.
    plain.tires = 0;
    Bytes old = game::encode_online_car(plain);
    c.tires = 0;
    b = game::encode_online_car(c);
    CHECK(b.size() == old.size() + 3);
    if (b.size() == old.size() + 3) {
        CHECK(std::equal(old.begin(), old.end() - 1, b.begin()));
        CHECK(old.back() == 0 && b[old.size() - 1] == 2);
    }
    // Garage cars wear their own paints: no colour sent.
    for (game::OnlineCar::Kind k : {game::OnlineCar::Kind::Garage}) {
        game::OnlineCar o = c;
        o.kind = k;
        o.package = std::string(64, 'a');
        d = game::decode_online_car(game::encode_online_car(o));
        CHECK(d && !d->color);
    }
}

// Replay v14: the custom colours among the paints, put back as this game's paint indices when read.
TEST(custom_paint_replay) {
    race::ReplayRecorder rec;
    rec.begin("trialmt", "r34_gtr_v_99", true, 9, {}, 60.0, "Gsunday_e_0001", {"gt_one_race_99", "vitz_rs15_00"});
    rec.set_opponent_paints({2, 11});
    rec.set_custom_colors({{0, "ni0137", true, rgb(1, 2, 3), 1, ""}, {2, "zz0001", true, rgb(9, 9, 9), 0, ""}});
    rec.record(sim::ControlInputs{});
    race::Replay recorded = rec.replay();
    recorded.exact = test::race_record_stub();
    const Bytes b = recorded.serialize();

    // No paints known (no disc): each colour's finish.
    race::set_custom_paints(nullptr);
    race::Replay q = race::Replay::parse(b);
    CHECK(q.paint == 1 && (q.opponent_paints == std::vector<int>{2, 0}) && q.custom_colors.size() == 2);

    // ni0137 with two paints and one installed colour; the replay's player colour added for the session after it.
    Bytes cc(0x100, 0);
    auto put32 = [&](size_t o, u32 v) { for (int k = 0; k < 4; ++k) cc[o + size_t(k)] = u8(v >> (8 * k)); };
    put32(0x08, 1), put32(0x0C, 0x40), put32(0x10, 0x60);
    const u64 h = db::id_hash("ni0137");
    put32(0x18, u32(h)), put32(0x1C, u32(h >> 32));
    put32(0x20, 2), put32(0x24, 0);
    put32(0x40, 7), put32(0x44, 9);
    race::CarColors colors;
    CHECK(colors.parse(cc));
    auto paints = std::make_shared<race::CustomPaints>(colors);
    race::CustomPaint mine;
    mine.model = "ni0137", mine.rgb = rgb(50, 50, 50), mine.finish = 0, mine.file = "x.ini";
    CHECK(paints->add(mine));
    race::set_custom_paints(paints);
    q = race::Replay::parse(b);
    CHECK(q.paint == 3 && race::custom_paints()->at("ni0137", 3) && !race::custom_paints()->at("ni0137", 3)->listed());
    q = race::Replay::parse(b); // (found again, not added twice)
    CHECK(q.paint == 3 && race::custom_paints()->of("ni0137").size() == 2);
    race::set_custom_paints(nullptr);
}

TEST(livery_overlay_coverage) {
    // A 2x1 texture: reflectivity 0x40 and a cut-out (alpha 0).
    scene::Texture t;
    t.width = 2, t.height = 1;
    t.rgba = {rgb(100, 100, 100) | 0x40000000u, rgb(10, 20, 30)};
    // Same size: the picture's alpha covers; the texture's alpha stays.
    texture_pack::Image pic;
    pic.width = 2, pic.height = 1;
    pic.rgba = {rgb(200, 0, 0) | 0xFF000000u, rgb(0, 0, 200) | 0x80000000u};
    scene::Texture a = t;
    gtm1::overlay_livery(a, pic);
    CHECK(a.width == 2 && a.rgba[0] == (rgb(200, 0, 0) | 0x40000000u));
    CHECK(a.rgba[1] >> 24 == 0 && std::abs(ch(a.rgba[1], 2) - 115) <= 1 && std::abs(ch(a.rgba[1], 0) - 5) <= 1);
    // An opaque canvas still showing the template covers nothing: the texture is left as it is (its size too).
    texture_pack::Image same;
    same.width = 8, same.height = 4;
    for (u32 y = 0; y < 4; ++y)
        for (u32 x = 0; x < 8; ++x) same.rgba.push_back((t.rgba[x / 4] & 0xFFFFFF) | 0xFF000000u);
    scene::Texture b = t;
    gtm1::overlay_livery(b, same, {t.rgba.data(), 2, 1});
    CHECK(b.width == 2 && b.rgba == t.rgba);
    // Painted over in one corner: only there, at the canvas's size; the alpha by nearest texel.
    same.rgba[0] = rgb(255, 255, 0) | 0xFF000000u;
    scene::Texture c = t;
    gtm1::overlay_livery(c, same, {t.rgba.data(), 2, 1});
    CHECK(c.width == 8 && c.height == 4 && !c.nearest);
    CHECK((c.rgba[0] & 0xFFFFFF) == rgb(255, 255, 0) && c.rgba[0] >> 24 == 0x40);
    CHECK(c.rgba[7] >> 24 == 0 && c.rgba[31] >> 24 == 0);
    CHECK(ch(c.rgba[9], 0) == 100 && std::abs(ch(c.rgba[10], 0) - 89) <= 1); // (the base enlarged and filtered where not painted)
}

// A livery's template, untouched, leaves every texture of the car as the disc draws it; a painted canvas changes the
// full-detail body's textures, the showroom model's that share them, and not the lower-detail ones.
TEST(livery_template_round_trip_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Volume& vol = disc->volume();
    Bytes car = vol.read(*vol.find("cars/day/ni0137"));
    std::string err;
    std::optional<race::LiveryTemplate> t = race::make_livery_template(car, "ni0137", 0, 4, &err);
    CHECK(t.has_value());
    if (!t) return;
    CHECK(t->canvases.size() >= 16 && t->canvases.size() == t->guides.size() && t->canvases[0].body);
    const std::filesystem::path dir = test::temp_dir("livery_round_trip");
    auto pack = std::make_shared<texture_pack::Pack>();
    for (const race::LiveryTemplate::Picture& p : t->canvases) {
        const Bytes png = encode_png(p.width, p.height, p.rgba);
        const std::string path = (dir / p.file).string();
        CHECK(write_host_file(path, png));
        CHECK(pack->add_file(*texture_pack::key_in_file_name(p.file), path));
    }
    auto textures = [&](ByteSpan file, std::shared_ptr<const texture_pack::Pack> livery) {
        CarModelOptions o;
        o.lod = -1;
        o.wheels = false;
        o.resolve_paint = false;
        o.livery = std::move(livery);
        std::vector<std::vector<u32>> out;
        for (const scene::Texture& x : load_car_model(file, o, "ni0137").textures) out.push_back(x.rgba);
        return out;
    };
    const auto plain = textures(car, nullptr);
    CHECK(textures(car, pack) == plain);
    // Paint the biggest body canvas yellow at its top-left corner.
    race::LiveryTemplate::Picture big = t->canvases[0];
    for (const auto& p : t->canvases)
        if (p.body && p.width * p.height > big.width * big.height) big = p;
    for (u32 y = 0; y < big.height / 4; ++y)
        for (u32 x = 0; x < big.width / 4; ++x) big.rgba[size_t(y) * big.width + x] = rgb(255, 230, 0) | 0xFF000000u;
    const std::string path = (dir / ("painted-" + big.file)).string();
    CHECK(write_host_file(path, encode_png(big.width, big.height, big.rgba)));
    auto painted = std::make_shared<texture_pack::Pack>();
    painted->add_file(*texture_pack::key_in_file_name(big.file), path);
    CarModelOptions o;
    o.lod = -1, o.wheels = false, o.resolve_paint = false, o.livery = painted;
    const auto t0 = std::chrono::steady_clock::now();
    const scene::Model m = load_car_model(car, o, "ni0137");
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    // Every level of detail shows it: the full-detail textures by key, the others carried through the body.
    std::set<int> lods;
    for (const scene::Mesh& mesh : m.meshes)
        for (const scene::Primitive& pr : mesh.primitives)
            if (pr.texture >= 0 && size_t(pr.texture) < plain.size() && m.textures[size_t(pr.texture)].rgba != plain[size_t(pr.texture)])
                lods.insert(mesh.lod);
    std::printf("  painted livery reaches LODs:");
    for (int l : lods) std::printf(" %d", l);
    std::printf(" (model loaded in %.0f ms)\n", ms);
    CHECK(lods.count(0) && lods.count(1) && lods.count(2));
    // A livery is a picture, often finer than the disc's texture: it keeps its mips (car textures have none).
    for (size_t i = 0; i < m.textures.size() && i < plain.size(); ++i)
        if (m.textures[i].rgba != plain[i]) CHECK(!m.textures[i].no_mips);
    if (std::optional<Bytes> menu = [&]() -> std::optional<Bytes> {
            const Volume::Entry* e = vol.find("menu/cars/ni0137");
            return e ? std::optional<Bytes>(vol.read(*e)) : std::nullopt;
        }()) {
        CHECK(textures(*menu, painted) != textures(*menu, nullptr)); // the showroom's model shares the texture
    }
}

TEST(livery_in_paint_lists) {
    Bytes cc(0x100, 0);
    auto put32 = [&](size_t o, u32 v) { for (int k = 0; k < 4; ++k) cc[o + size_t(k)] = u8(v >> (8 * k)); };
    put32(0x08, 1), put32(0x0C, 0x40), put32(0x10, 0x60);
    const u64 h = db::id_hash("ni0137");
    put32(0x18, u32(h)), put32(0x1C, u32(h >> 32));
    put32(0x20, 2), put32(0x24, 0);
    put32(0x40, 7), put32(0x44, 9);
    put32(0x60, 2), put32(0x64, 7), put32(0x70, rgb(200, 0, 0)), put32(0x74, 9), put32(0x80, rgb(0, 0, 200));
    race::CarColors colors;
    CHECK(colors.parse(cc));
    race::CustomPaints paints(colors);
    // A livery on paint 1, no colour of its own: the chip shows paint 1's colour; it resolves to its pictures on paint 1.
    std::string err;
    std::optional<race::CustomPaint> l = race::parse_custom_paint("[livery]\nname = Stripes\nmodel = ni0137\npaint = 1\n", &err);
    CHECK(l && !l->colour);
    if (!l) return;
    CHECK(!paints.add(*l, &err)); // no pictures and no colour: nothing to show
    l->pictures = std::make_shared<texture_pack::Pack>();
    l->id = "my_mod/stripes", l->file = "x/livery.ini";
    int index = -1;
    CHECK(paints.add(*l, &err, &index) && index == 2);
    const race::CustomPaint* p = paints.at("ni0137", 2);
    CHECK(p && p->rgb == rgb(0, 0, 200) && p->name == "Stripes" && p->listed());
    auto r = paints.resolved("ni0137", 2);
    CHECK(r && r->paint == 1 && !r->computed && r->livery == l->pictures);
    CHECK(paints.index_of_livery("ni0137", "my_mod/stripes") == 2 && paints.index_of_livery("ni0137", "x/y") == -1);
    CHECK(paints.index_of("ni0137", rgb(0, 0, 200), 1) == -1); // (a received plain colour never lands on a livery)
}

// A livery's package carries only what was painted; this disc's checks take it; it installs and is found by its hash.
TEST(livery_package_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    Bytes car = disc->volume().read(*disc->volume().find("cars/day/ni0137"));
    std::optional<race::LiveryTemplate> t = race::make_livery_template(car, "ni0137", 0, 2);
    CHECK(t.has_value());
    if (!t) return;
    const std::filesystem::path dir = test::temp_dir("livery_package");
    for (const race::LiveryTemplate::Picture& p : t->canvases) CHECK(write_host_file((dir / p.file).string(), encode_png(p.width, p.height, p.rgba)));
    race::CustomPaint lp;
    lp.name = "Pkg test", lp.author = "me", lp.model = "ni0137", lp.finish = 0, lp.colour = false, lp.id = "m/pkg";
    lp.file = (dir / "livery.ini").string();
    lp.pictures = std::make_shared<texture_pack::Pack>();
    std::string err;
    CHECK(!game::build_livery_package(lp, vfs, &err) && err == "nothing painted"); // an untouched template: the disc's pixels
    // Paint a block on the first canvas.
    race::LiveryTemplate::Picture p = t->canvases[0];
    for (u32 y = 0; y < p.height / 2; ++y)
        for (u32 x = 0; x < p.width / 2; ++x) p.rgba[size_t(y) * p.width + x] = rgb(255, 0, 255) | 0xFF000000u;
    CHECK(write_host_file((dir / p.file).string(), encode_png(p.width, p.height, p.rgba)));
    std::optional<Bytes> blob = game::build_livery_package(lp, vfs, &err);
    CHECK(blob.has_value());
    if (!blob) return;
    std::optional<game::LiveryPackage> pkg = game::parse_livery_package(*blob, &err);
    CHECK(pkg && pkg->files.size() == 2); // livery.ini and the one painted picture
    if (!pkg) return;
    CHECK(game::validate_livery_package(*pkg, vfs, "ni0137", &err));
    CHECK(!game::validate_livery_package(*pkg, vfs, "am0006", &err)); // not the car the lobby named
    const PngImage img = decode_png(pkg->files[1].data);
    size_t painted = 0, clear = 0;
    for (u32 c : img.rgba) (c >> 24) ? ++painted : ++clear;
    CHECK(painted == size_t(p.width / 2) * (p.height / 2) && clear == img.rgba.size() - painted); // no disc pixels
    // A picture for a texture this car does not have, or a file of another kind: refused.
    game::LiveryPackage bad = *pkg;
    bad.files[1].name = "p-0123456789abcdef.png";
    CHECK(!game::validate_livery_package(bad, vfs, "ni0137", &err));
    bad = *pkg;
    bad.files.push_back({"notes.txt", Bytes{'h', 'i'}});
    CHECK(!game::validate_livery_package(bad, vfs, "ni0137", &err));
    CHECK(!game::parse_livery_package(Bytes(blob->begin(), blob->end() - 1), &err));
    // Installed as downloaded_liveries/liveries/dl-<hash>/, with who it came from.
    const std::string hash = sha256_hex(*blob);
    const std::filesystem::path mods = test::temp_dir("livery_package_mods");
    std::optional<std::string> at = game::install_livery_package(*pkg, hash, mods.string(), "Pat", &err);
    CHECK(at && *at == mods.string() + "/downloaded_liveries/liveries/dl-" + hash.substr(0, 12));
    std::optional<Bytes> ini = read_host_file(*at + "/livery.ini");
    CHECK(ini && std::string(ini->begin(), ini->end()).find("from = Pat") != std::string::npos);
    CHECK(game::downloaded_livery_id(hash) == "downloaded_liveries/dl-" + hash.substr(0, 12));
    // The picture on disk is our own encoding of it, not the sender's bytes.
    const PngImage sent = decode_png(pkg->files[1].data);
    std::optional<Bytes> kept = at ? read_host_file(*at + "/" + pkg->files[1].name) : std::nullopt;
    CHECK(kept && *kept == encode_png(sent.width, sent.height, sent.rgba));
}

TEST(livery_online_car) {
    game::OnlineCar c;
    c.kind = game::OnlineCar::Kind::Disc;
    c.car = "r34_gtr_v_99";
    c.paint = 2;
    c.digest.emplace();
    c.digest->fill(0x22);
    c.livery = std::string(64, 'e');
    c.livery_size = 123456;
    std::optional<game::OnlineCar> d = game::decode_online_car(game::encode_online_car(c));
    CHECK(d && d->livery == c.livery && d->livery_size == 123456 && !d->color && d->paint == 2);
    c.color = rgb(1, 2, 3);
    d = game::decode_online_car(game::encode_online_car(c));
    CHECK(d && d->livery == c.livery && d->color == c.color);
    c.livery = "not a hash";
    d = game::decode_online_car(game::encode_online_car(c));
    CHECK(d && d->livery.empty()); // (not sent)
}

namespace {

// A mod car's body: a paint material and a plain one (base colour 1, 0.5, 0.5) sharing one 4x4 picture, and a
// textureless paint material, each one triangle with UVs.
std::vector<u32> glb_picture() {
    std::vector<u32> px(16);
    for (size_t i = 0; i < px.size(); ++i) px[i] = rgb(200, 100 + int(i), 50) | 0xFF000000u;
    return px;
}
Bytes livery_glb(const std::vector<u32>& px = glb_picture()) {
    const float pos[9] = {0.5f, 0.2f, 1, 1, 0.2f, 1, 0.5f, 1, 1}, uv[6] = {0.1f, 0.1f, 0.9f, 0.1f, 0.1f, 0.9f};
    const u16 idx[4] = {0, 1, 2, 0};
    const Bytes png = encode_png(4, 4, px);
    Bytes bin(68 + png.size());
    std::memcpy(bin.data(), pos, 36), std::memcpy(bin.data() + 36, uv, 24), std::memcpy(bin.data() + 60, idx, 8);
    std::memcpy(bin.data() + 68, png.data(), png.size());
    while (bin.size() % 4) bin.push_back(0);
    const std::string attr = R"({"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2,"material":)";
    std::string js = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[)" +
                     attr + "0}," + attr + "1}," + attr + "2}" +
                     R"(]}],"materials":[{"name":"car_paint","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},)"
                     R"({"name":"trim","pbrMetallicRoughness":{"baseColorFactor":[1,0.5,0.5,1],"baseColorTexture":{"index":0}}},)"
                     R"({"name":"roof paint","pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1]}}],)"
                     R"("textures":[{"source":0}],"images":[{"name":"skin main","mimeType":"image/png","bufferView":3}],"buffers":[{"byteLength":)" +
                     std::to_string(bin.size()) + R"(}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},)"
                     R"({"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":6},)"
                     R"({"buffer":0,"byteOffset":68,"byteLength":)" + std::to_string(png.size()) +
                     R"(}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
                     R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},)"
                     R"({"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    while (js.size() % 4) js += ' ';
    Bytes out;
    auto put32 = [&](u32 v) { for (int k = 0; k < 4; ++k) out.push_back(u8(v >> (8 * k))); };
    out.insert(out.end(), {'g', 'l', 'T', 'F'});
    put32(2), put32(u32(28 + js.size() + bin.size())), put32(u32(js.size())), put32(0x4E4F534A);
    out.insert(out.end(), js.begin(), js.end());
    put32(u32(bin.size())), put32(0x004E4942);
    out.insert(out.end(), bin.begin(), bin.end());
    return out;
}

bool write_text(const std::filesystem::path& p, const std::string& t) {
    std::filesystem::create_directories(p.parent_path());
    return write_host_file(p.string(), Bytes(t.begin(), t.end()));
}

} // namespace

// Issue 445: a livery on a mod car's glTF body (GlbOptions::livery).
TEST(livery_glb_textures) {
    const Bytes glb = livery_glb();
    const std::array<float, 3> blue{0, 0.5f, 1};
    const u64 key = texture_pack::key_of(4, 4, glb_picture().data());
    // The template's textures: the picture as the paint draws it (one for both its materials) and the textureless paint.
    const std::vector<GlbLiveryTexture> ts = glb_livery_textures(glb, blue);
    CHECK(ts.size() == 2);
    if (ts.size() != 2) return;
    CHECK(ts[0].key == key && ts[0].paint && !ts[0].blank && ts[0].name == "skin main" && ts[0].triangles.size() == 2);
    CHECK((ts[0].rgba[0] & 0xFFFFFF) == rgb(0, 50, 50));
    CHECK(ts[1].blank && ts[1].paint && ts[1].name == "roof paint" && (ts[1].rgba[0] & 0xFFFFFF) == rgb(0, 128, 255));
    // glTF's front (+Z) is the nose, -Z in GT3's axes.
    CHECK(ts[0].triangles[0].normal[2] < -2.9f);
    std::string err;
    std::optional<race::LiveryTemplate> t = race::make_glb_livery_template({glb, glb}, "mod:my_car", blue, 2, 4, &err);
    CHECK(t && t->canvases.size() == 2 && t->guides.size() == 2);
    if (!t) return;
    CHECK(t->canvases[0].file == "body-skin-main-" + texture_pack::key_name(key) + ".png" && t->canvases[0].width == 16);
    CHECK(t->canvases[1].width == race::kGlbCanvas);
    CHECK(t->ini.find("model = mod:my_car\npaint = 2 ") != std::string::npos);
    // Filled as front faces (red), not rear ones (purple).
    CHECK(std::any_of(t->guides[0].rgba.begin(), t->guides[0].rgba.end(),
                      [](u32 c) { return (c >> 24) && ch(c, 0) > ch(c, 2) + 100; }));

    auto pack_of = [&](const std::vector<race::LiveryTemplate::Picture>& pics, const char* name) {
        const std::filesystem::path dir = test::temp_dir(name);
        auto pack = std::make_shared<texture_pack::Pack>();
        for (const race::LiveryTemplate::Picture& p : pics) {
            CHECK(write_host_file((dir / p.file).string(), encode_png(p.width, p.height, p.rgba)));
            CHECK(pack->add_file(*texture_pack::key_in_file_name(p.file), (dir / p.file).string()));
        }
        return pack;
    };
    GlbOptions o;
    o.paint = blue;
    const scene::Model plain = load_glb(glb, o);
    o.livery_paint = blue;
    // The untouched template changes nothing.
    auto untouched = pack_of(t->canvases, "livery_glb_untouched");
    o.livery = untouched.get();
    scene::Model m = load_glb(glb, o);
    CHECK(m.textures.size() == plain.textures.size());
    for (size_t i = 0; i < m.meshes[0].vertices.size(); ++i) CHECK(m.meshes[0].vertices[i].color == plain.meshes[0].vertices[i].color);
    // Painted yellow over its left half: that half is yellow whatever the paint, the rest the paint's.
    race::LiveryTemplate::Picture p = t->canvases[0];
    for (u32 y = 0; y < p.height; ++y)
        for (u32 x = 0; x < p.width / 2; ++x) p.rgba[size_t(y) * p.width + x] = rgb(255, 230, 0) | 0xFF000000u;
    auto painted = pack_of({p}, "livery_glb_painted");
    o.livery = painted.get();
    m = load_glb(glb, o);
    const scene::Mesh& b = m.meshes[0];
    const scene::Texture& paint_tex = m.textures[size_t(b.primitives[0].texture)];
    const scene::Texture& trim_tex = m.textures[size_t(b.primitives[1].texture)];
    CHECK(paint_tex.width == 16 && (paint_tex.rgba[0] & 0xFFFFFF) == rgb(255, 230, 0));
    CHECK(ch(paint_tex.rgba[15], 0) == 0 && ch(paint_tex.rgba[15], 2) == 50 && ch(paint_tex.rgba[15], 1) >= 50);
    CHECK((trim_tex.rgba[0] & 0xFFFFFF) == rgb(255, 230, 0) && ch(trim_tex.rgba[15], 0) == 200 && ch(trim_tex.rgba[15], 2) == 25);
    CHECK(b.vertices[0].color == 0xFFFFFFFFu && b.vertices[3].color == 0xFFFFFFFFu); // (in the texture now)
    CHECK(b.primitives[2].texture == 0 && b.vertices[6].color == plain.meshes[0].vertices[6].color); // the other paint: as it was
    // The same pictures over another colour (`color =`): the unpainted parts in it.
    o.paint = std::array<float, 3>{1, 0, 0};
    m = load_glb(glb, o);
    const scene::Texture& red = m.textures[size_t(m.meshes[0].primitives[0].texture)];
    CHECK((red.rgba[0] & 0xFFFFFF) == rgb(255, 230, 0) && (red.rgba[15] & 0xFFFFFF) == rgb(200, 0, 0));
    // A textureless paint's picture.
    race::LiveryTemplate::Picture roof = t->canvases[1];
    roof.rgba[0] = rgb(1, 2, 3) | 0xFF000000u;
    auto roofs = pack_of({roof}, "livery_glb_roof");
    o.paint = blue, o.livery = roofs.get();
    m = load_glb(glb, o);
    const scene::Texture& rt = m.textures[size_t(m.meshes[0].primitives[2].texture)];
    CHECK(rt.width == race::kGlbCanvas && (rt.rgba[0] & 0xFFFFFF) == rgb(1, 2, 3) && (rt.rgba[1] & 0xFFFFFF) == rgb(0, 128, 255));
    CHECK(m.meshes[0].primitives[0].texture == plain.meshes[0].primitives[0].texture);
}

TEST(livery_mod_car_names) {
    std::string err;
    std::optional<race::CustomPaint> p = race::parse_custom_paint("[livery]\nname = Team A\nmodel = mod:my_wtcc_car_2007\npaint = 1\n", &err);
    CHECK(p && p->model == "mod:my_wtcc_car_2007" && p->finish == 1 && !p->colour);
    CHECK(!race::parse_custom_paint("[livery]\nmodel = mod:\npaint = 0\n", &err));
    CHECK(!race::parse_custom_paint("[livery]\nmodel = mod:a/../b\npaint = 0\n", &err));
    CHECK(!race::parse_custom_paint("[livery]\nmodel = mod:" + std::string(41, 'a') + "\npaint = 0\n", &err));
    CHECK(!race::parse_custom_paint("[livery]\nmodel = not:a_mod\npaint = 0\n", &err));
    // Not installed: refused, by its name.
    race::CustomPaints none;
    CHECK(!none.add(*p, &err) && err == "mod car my_wtcc_car_2007: not installed");
    // Online: a mod car's colour and livery are flags of their own, which older games pass over.
    game::OnlineCar c;
    c.kind = game::OnlineCar::Kind::Mod;
    c.car = "my_wtcc_car_2007", c.donor = "r34_gtr_v_99";
    c.package = std::string(64, 'a'), c.package_size = 10;
    c.paint = 1;
    c.tires = 0;
    c.digest.emplace();
    c.digest->fill(0x33);
    const Bytes old = game::encode_online_car(c);
    c.color = rgb(9, 8, 7);
    c.livery = std::string(64, 'b'), c.livery_size = 99;
    const Bytes b = game::encode_online_car(c);
    CHECK(b.size() > old.size() && std::equal(old.begin(), old.end() - 1, b.begin()) && old.back() == 0 && b[old.size() - 1] == 24);
    std::optional<game::OnlineCar> d = game::decode_online_car(b);
    CHECK(d && d->color == c.color && d->livery == c.livery && d->livery_size == 99 && d->paint == 1 && d->tires == 0);
    Bytes bad = b;
    bad[old.size() - 1] = 2; // (a disc car's flags)
    CHECK(!game::decode_online_car(bad));
}

// Issue 445, end to end: a mod car with two liveries in its paint list, drawn, shared and downloaded.
TEST(livery_mod_car_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    const std::filesystem::path mods = test::temp_dir("livery_mod_car");
    data::CarDatabase plain_db;
    CHECK(plain_db.load(disc->volume()));
    const data::Car* donor = nullptr;
    for (const data::Car& c : plain_db.cars())
        if (!donor && plain_db.model_of(c) == "ni0137") donor = &c;
    CHECK(donor != nullptr);
    if (!donor) return;
    const std::string head = "name = Livery Car\ndonor = " + donor->label + "\n";
    CHECK(write_text(mods / "cars_mod/cars/livery_car.ini",
                     "[car]\nlabel = livery_car\n" + head + "[model]\nfile = models/body.glb\n[paints]\nWhite = #FFFFFF\nBlue = #0080FF\n"));
    CHECK(write_text(mods / "cars_mod/cars/livery_shell.ini", // a borrowed shell, in the donor's paints, stretched
                     "[car]\nlabel = livery_shell\n" + head + "[body]\nlength_mm = 4800\nwheelbase_mm = 2800\n"));
    std::filesystem::create_directories(mods / "cars_mod/models");
    CHECK(write_host_file((mods / "cars_mod/models/body.glb").string(), livery_glb()));
    Vfs vfs(&disc->volume());
    vfs.scan_mods(mods.string(), nullptr);
    data::CarDatabase db;
    CHECK(db.load(vfs));
    CHECK(db.mod_car("livery_car") && db.mod_car("livery_shell"));
    if (!db.mod_car("livery_car") || !db.mod_car("livery_shell")) return;
    const std::string key = race::paint_key(db, "livery_car", "ni0137"), shell = race::paint_key(db, "livery_shell", "ni0137");
    CHECK(key == "mod:livery_car" && shell == "mod:livery_shell");

    // Two liveries of the glTF car and one of the shell, painted on their templates.
    std::string err;
    auto paint = [&](const std::string& model, int base, const std::string& folder, u32 colour) {
        std::optional<race::LiveryTemplate> t = race::make_livery_template(vfs, db, model, base, 2, &err);
        CHECK(t && !t->canvases.empty());
        if (!t) return;
        race::LiveryTemplate::Picture p = t->canvases[0];
        for (u32 y = 0; y < p.height; ++y)
            for (u32 x = 0; x < p.width / 2; ++x) p.rgba[size_t(y) * p.width + x] = colour | 0xFF000000u;
        CHECK(write_text(mods / "team_liveries/liveries" / folder / "livery.ini", t->ini));
        for (const race::LiveryTemplate::Picture& c : t->canvases) // (the whole template, one canvas painted)
            CHECK(write_host_file((mods / "team_liveries/liveries" / folder / c.file).string(),
                                  encode_png(c.width, c.height, c.file == p.file ? p.rgba : c.rgba)));
    };
    paint(key, 0, "a_team", rgb(255, 230, 0));
    paint(key, 1, "b_team", rgb(220, 0, 120));
    paint(shell, 0, "c_team", rgb(255, 230, 0));
    vfs.scan_mods(mods.string(), nullptr);
    std::shared_ptr<race::CustomPaints> paints = race::load_custom_paints(vfs);
    CHECK(paints->errors().empty());
    CHECK(paints->disc_count(key) == 2 && paints->of(key).size() == 2);
    const int disc_paints = paints->disc_count("ni0137");
    CHECK(paints->disc_count(shell) == disc_paints && paints->of(shell).size() == 1 && paints->of("ni0137").empty());
    race::set_custom_paints(paints);

    // Drawn: paint 2 and 3 are the liveries, 0 and 1 the paints.
    const Bytes file = disc->volume().read(*disc->volume().find("cars/day/ni0137"));
    auto shows = [&](const std::string& car, int index, u32 colour) {
        CarModelOptions o;
        o.lod = -1, o.wheels = false, o.paint = index;
        o.body_mod = body_mod_for_car(db, car, index, data::CarScene::Day);
        const scene::Model m = load_car_model(file, o, "ni0137");
        std::set<int> lods;
        for (const scene::Mesh& mesh : m.meshes)
            for (const scene::Primitive& pr : mesh.primitives)
                if (pr.texture >= 0)
                    if (const scene::Texture& t = m.textures[size_t(pr.texture)]; t.livery_cover)
                        for (u32 c : t.rgba)
                            if ((c & 0xFFFFFF) == colour) {
                                lods.insert(mesh.lod);
                                break;
                            }
        return lods;
    };
    CHECK(shows("livery_car", 0, rgb(255, 230, 0)).empty() && shows("livery_car", 1, rgb(220, 0, 120)).empty());
    CHECK(shows("livery_car", 2, rgb(255, 230, 0)).count(0) && shows("livery_car", 2, rgb(220, 0, 120)).empty());
    CHECK(shows("livery_car", 3, rgb(220, 0, 120)).size() == 3);
    const std::set<int> on_shell = shows("livery_shell", disc_paints, rgb(255, 230, 0));
    CHECK(on_shell.count(0) && on_shell.count(1) && on_shell.count(2)); // carried over to the stretched body's other levels
    const CarBodyMod b1 = body_mod_for_car(db, "livery_car", 3, data::CarScene::Menu);
    CHECK(b1.livery && b1.paint && (*b1.paint)[0] == 0 && (*b1.paint)[2] == 1); // on its paint 1

    // Online: the car as its paint and its livery's package, which the other side checks against its copy of the car.
    game::OnlineCar oc = game::online_car_from_db(db, "livery_car", 0);
    CHECK(oc.kind == game::OnlineCar::Kind::Mod);
    game::set_online_car_paint(db, oc, 3);
    CHECK(oc.paint == 1 && !oc.color);
    const race::CustomPaint* lp = paints->at(key, 3);
    CHECK(lp && lp->id == "team_liveries/b_team");
    if (!lp) return;
    std::optional<Bytes> blob = game::build_livery_package(*lp, vfs, &err, &db);
    std::optional<game::LiveryPackage> pkg = blob ? game::parse_livery_package(*blob, &err) : std::nullopt;
    CHECK(pkg && pkg->files.size() == 2); // livery.ini and the one painted picture
    if (!pkg) return;
    const bool ok = game::validate_livery_package(*pkg, vfs, key, &err, &db);
    if (!ok) std::printf("  refused: %s\n", err.c_str());
    CHECK(ok);
    CHECK(!game::validate_livery_package(*pkg, vfs, shell, &err, &db));  // another car's
    CHECK(!game::validate_livery_package(*pkg, vfs, key, &err, &plain_db)); // the car is not installed
    // The car as another player gets it (its images encoded again): the same textures.
    const Bytes sent = reencode_glb_images(livery_glb(), 2048);
    CHECK(glb_livery_textures(sent, std::nullopt)[0].key == glb_livery_textures(livery_glb(), std::nullopt)[0].key);
    // A changed picture on their copy: refused.
    std::vector<u32> other = glb_picture();
    other[0] ^= 0xFF;
    CHECK(write_host_file((mods / "cars_mod/models/body.glb").string(), livery_glb(other)));
    CHECK(!game::validate_livery_package(*pkg, vfs, key, &err, &db));
    // Found again by its package's hash, downloaded.
    const std::string hash = sha256_hex(*blob);
    CHECK(game::install_livery_package(*pkg, hash, mods.string(), "Pat", &err).has_value());
    vfs.scan_mods(mods.string(), nullptr);
    race::set_custom_paints(race::load_custom_paints(vfs));
    oc.livery = hash, oc.livery_size = u32(blob->size());
    const int here = game::online_car_local_paint(db, oc);
    CHECK(race::custom_paints()->at(key, here) && race::custom_paints()->at(key, here)->id == game::downloaded_livery_id(hash));
    // Not installed: its paint. A colour: added for the session.
    oc.livery = std::string(64, 'c');
    CHECK(game::online_car_local_paint(db, oc) == 1);
    oc.livery.clear(), oc.color = rgb(10, 20, 30);
    const int colour = game::online_car_local_paint(db, oc, "Pat");
    const CarBodyMod b2 = body_mod_for_car(db, "livery_car", colour, data::CarScene::Day);
    CHECK(colour >= 4 && b2.paint && std::abs((*b2.paint)[2] - 30 / 255.0f) < 1e-6f && !b2.livery);
    race::set_custom_paints(nullptr);
}
