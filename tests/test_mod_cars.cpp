// Mod cars (game/data/mod_cars.h), electric drive and the synthesised motor
// sound. Synthetic tests run everywhere; the end-to-end test needs $GT3R_DISC.

#include "test_framework.h"
#include "car_db_synth.h"

#include "audio/motor_sound.h"
#include "base/file.h"
#include "disc/volume.h"
#include "formats/model/car_model.h"
#include "formats/ps_adpcm.h"
#include "formats/sound/spu.h"
#include "formats/text/charset.h"
#include "formats/text/text_layout.h"
#include "game/data/car_database.h"
#include "game/data/mod_cars.h"
#include "exact_drive.h"
#include "race/exact_setup.h"
#include "sim/car_spec.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <tuple>
#include <vector>

using namespace gt3r;
using namespace gt3r::test;

namespace {

// Full throttle down the machine tests' straight: the speed (km/h) at each of `at` seconds after GO; empty when the race
// cannot be set up.
std::vector<float> straight_kmh(Disc& disc, const data::CarDatabase& db, const std::string& car, const std::vector<double>& at) {
    StraightRun run = straight_run(disc, db, car);
    if (!run.field) return {};
    sim::ControlInputs in;
    in.throttle = 1;
    in.controller_kind = 2;
    std::vector<float> out;
    for (int i = 0; i < 60 * 120 && out.size() < at.size(); ++i) {
        run.step(in);
        if (run.field->race_time() >= at[out.size()]) out.push_back(run.field->player().state().speed_kmh);
    }
    return out;
}

bool write_file(const std::string& path, std::string_view text) {
    return write_host_file(path, ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
}

const char* kMinimal = R"(
; a comment
[car]
label = test_ev
name = Test EV          ; trailing comment
donor = legacy_gtb_96
year = 2026

[powertrain]
type = electric
power_kw = 300
torque_nm = 600
max_rpm = 16000

[drivetrain]
drive = AWD
front_torque_pct = 35
gears = 9.04

[tires]
front = 255/35R21
rear = 275/35R21

[raw.chassis]
0x1D = 120
)";

} // namespace

TEST(modcar_tire_size) {
    auto t = data::parse_tire_size("255/35R21");
    CHECK(t && t->width_mm_5 == 51 && t->aspect_5 == 7 && t->rim_in == 21);
    auto z = data::parse_tire_size("225/60 ZR18");
    CHECK(z && z->width_mm_5 == 45 && z->aspect_5 == 12 && z->rim_in == 18);
    CHECK(!data::parse_tire_size("253/35R21")); // not a multiple of 5: the disc stores width / 5
    CHECK(!data::parse_tire_size("255-35-21"));
}

TEST(modcar_electric_curve) {
    auto c = data::electric_curve(600, 300, 16000);
    CHECK(c.size() >= 10 && c.size() <= 16);
    CHECK(c.front().first == 0 && std::fabs(c.front().second - 600) < 1e-3f);
    for (size_t i = 1; i < c.size(); ++i) {
        CHECK(c[i].first > c[i - 1].first);
        CHECK(c[i].second <= c[i - 1].second + 1e-3f); // never rises
        CHECK(std::fmod(c[i].first, 100.0f) == 0);    // the axis is stored in 100 rpm steps
    }
    CHECK(c.back().first == 16000);
    // Constant power above the base speed (300 kW / 600 N*m = 4775 rpm), 85 % at the top.
    float kw_top = c.back().second * c.back().first * 0.10471976f / 1000;
    CHECK(std::fabs(kw_top - 255) < 3);
}

TEST(modcar_parse) {
    std::string err;
    std::vector<std::string> warns;
    auto d = data::parse_mod_car(kMinimal, &err, &warns);
    CHECK(d.has_value());
    CHECK(warns.empty());
    CHECK(d->label == "test_ev" && d->name == "Test EV" && d->donor == "legacy_gtb_96");
    CHECK(d->electric && d->max_rpm && *d->max_rpm == 16000);
    CHECK(d->drive && *d->drive == 2 && d->front_torque_pct && *d->front_torque_pct == 35);
    CHECK(d->gears.size() == 1 && std::fabs(d->gears[0] - 9.04f) < 1e-4f);
    CHECK(d->front_tire && d->rear_tire && d->rear_tire->width_mm_5 == 55);
    CHECK(d->pokes.size() == 1 && d->pokes[0].table == data::TableId::Chassis && d->pokes[0].offset == 0x1D);

    // Hard errors name the problem.
    CHECK(!data::parse_mod_car("[car]\nlabel = x\nname = X\n", &err) && err.find("donor") != std::string::npos);
    CHECK(!data::parse_mod_car("[car]\nlabel = Bad Label\nname = X\ndonor = y\n", &err));
    CHECK(!data::parse_mod_car("[car]\nlabel = x\nname = X\ndonor = y\n[powertrain]\ntype = electric\n", &err));
    // Unknown keys are warnings, not errors.
    warns.clear();
    CHECK(data::parse_mod_car("[car]\nlabel = x\nname = X\ndonor = y\nspoiler = big\n", &err, &warns).has_value());
    CHECK(warns.size() == 1);
}

TEST(adpcm_encode_roundtrip) {
    std::vector<s16> pcm(28 * 200);
    for (size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = s16(12000 * std::sin(i * 0.05) + 6000 * std::sin(i * 0.31));
    Bytes enc = ps_adpcm_encode(pcm.data(), pcm.size(), nullptr);
    CHECK(enc.size() == 200 * 16);
    PsAdpcmDecoder dec;
    double sig = 0, err = 0;
    s16 out[28];
    for (size_t f = 0; f < 200; ++f) {
        dec.decode_frame(enc.data() + f * 16, out);
        for (size_t i = 0; i < 28; ++i) {
            double s = pcm[f * 28 + i], e = out[i] - s;
            sig += s * s;
            err += e * e;
        }
    }
    CHECK(10 * std::log10(sig / std::max(err, 1.0)) > 30); // dB
}

TEST(motor_sound_banks) {
    MotorBanks b = make_motor_banks({});
    CHECK(b.whine && b.inverter);
    for (const auto* bank : {b.whine.get(), b.inverter.get()}) {
        CHECK(bank->layers.size() == 3);
        for (size_t i = 0; i < bank->layers.size(); ++i) {
            if (i) CHECK(bank->layers[i].base_rpm > bank->layers[i - 1].base_rpm);
            sound::SpuSample s = bank->decode_layer(i);
            CHECK(s.looped && s.loop_start == 28); // silent frame, then the loop
            int peak = 0;
            for (s16 v : s.pcm) peak = std::max(peak, std::abs(int(v)));
            CHECK(peak > 10000);
        }
    }
}

TEST(body_fit_maps_axles) {
    scene::Model m;
    scene::Mesh body;
    body.name = "body";
    for (float z : {-1.3f, 1.3f, -2.3f, 2.3f}) {
        scene::Vertex v;
        v.pos[0] = 0.85f, v.pos[1] = 1.4f, v.pos[2] = z;
        v.normal[1] = 1;
        body.vertices.push_back(v);
    }
    scene::Mesh wheel = body;
    wheel.name = "wheel_fl";
    m.meshes = {body, wheel};
    // 4600 x 1700 x 1400, wheelbase 2600 -> 4800 x 1900 x 1600, wheelbase 2900.
    fit_body(m, {4600, 2600, 1700, 1400, 4800, 2900, 1900, 1600});
    const auto& v = m.meshes[0].vertices;
    CHECK(std::fabs(v[0].pos[2] + 1.45f) < 1e-4f && std::fabs(v[1].pos[2] - 1.45f) < 1e-4f); // axles
    CHECK(std::fabs(v[2].pos[2] + 2.40f) < 1e-3f); // front overhang 1.0 m -> 0.95 m (1900 / 2000)
    CHECK(std::fabs(v[0].pos[0] - 0.85f * 1900 / 1700) < 1e-4f);
    CHECK(std::fabs(v[0].pos[1] - 1.6f) < 1e-4f);
    CHECK(m.meshes[1].vertices[0].pos[2] == -1.3f); // wheels untouched
}

// A mod's texts (UTF-8 in its files) go into the database as game text, as the disc's own names: Latin letters as the
// game's Latin escapes, so a maker spelled as the disc spells it finds that maker. INVENTED database (car_db_synth.h).
TEST(modcar_texts_game_text) {
    auto db = cardb::empty_database();
    CHECK(db != nullptr);
    if (!db) return;
    cardb::add_car(*db, "syn_donor", 300, 75, "zz0001");
    // A disc maker with an accent (Ë), as game text; and the drivetrain and gearbox records a donor needs.
    const std::string disc_maker = "CITRO\x81\xCBN";
    const ByteSpan* rec = db->record(data::TableId::Car, "syn_donor");
    CHECK(rec != nullptr);
    if (!rec) return;
    Bytes car(rec->begin(), rec->end());
    for (const auto& [t, slot, l] : {std::tuple{data::TableId::Drivetrain, 13, "dt_syn_donor"},
                                     std::tuple{data::TableId::Gear, 18, "ge_syn_donor"}}) {
        db->add_label(l);
        Bytes r(0x40);
        cardb::put64(r, 0, db::id_hash(l));
        cardb::put64(r, 8, db::id_hash("syn_donor"));
        db->put_record(t, r);
        cardb::put64(car, size_t(8 + 8 * slot), db::id_hash(l));
    }
    cardb::put16(car, 0xE0, 77); // maker id
    cardb::put16(car, 0x104, db->add_unistring(disc_maker));
    db->put_record(data::TableId::Car, car);
    db->rebuild();

    std::string err;
    auto def = data::parse_mod_car("[car]\nlabel = syn_mod\nname = Caf\xC3\xA9 \xE2\x80\x99 Racer\nmaker = CITRO\xC3\x8BN\n"
                                   "donor = syn_donor\n[powertrain]\nlayout = Moteur \xC3\xA0 plat\n"
                                   "[dealer]\ndescription = Tr\xC3\xA8s vite\n[paints]\nBleu \xC3\x89lectrique = #2040C0\n",
                                   &err);
    CHECK(def.has_value());
    if (!def) return;
    CHECK(def->name == "Caf\xC3\xA9 \xE2\x80\x99 Racer"); // the definition keeps its UTF-8
    CHECK(data::add_mod_car(*db, *def, &err));
    db->rebuild();
    const data::Car* c = db->car("syn_mod");
    const data::ModCar* m = db->mod_car("syn_mod");
    CHECK(c && m);
    if (!c || !m) return;
    CHECK(c->name == "Caf\x81\xE9 \x80\x92 Racer"); // é Latin-1, ’ the Latin font's (Windows-1252) code
    CHECK(c->latin_name == c->name);
    CHECK(c->maker == disc_maker && c->maker_id == 77); // the disc's maker found
    CHECK(m->description == "Tr\x81\xE8s vite");
    CHECK(m->paints.size() == 1 && m->paints[0].first == "Bleu \x81\xC9lectrique");
    auto e = db->engine(c->parts[6]);
    CHECK(e && e->layout == "Moteur \x81\xE0 plat");
    const ByteSpan name(reinterpret_cast<const u8*>(c->name.data()), c->name.size());
    std::vector<text::GlyphRef> g = text::map_game_text(name);
    CHECK(g.size() == 12 && std::none_of(g.begin(), g.end(), [](const text::GlyphRef& r) { return r.kanji; }));
    // Bytes that are not UTF-8 (a file saved in Latin-1) draw as '?', not as stray kanji.
    def->label = "syn_mod2";
    def->name = "Citro\xEBn";
    CHECK(data::add_mod_car(*db, *def, &err));
    db->rebuild();
    CHECK(db->car("syn_mod2") && db->car("syn_mod2")->name == "Citro?n");
    // A maker file: its name and description as game text too (matched with the cars' makers).
    // (Joined with '/', as the mod scan joins its paths: the maker's id is the file name after the last '/'.)
    const std::string maker_file = temp_dir("modcar_texts").generic_string() + "/citroen.ini";
    CHECK(write_file(maker_file, "[maker]\nname = CITRO\xC3\x8BN\nregion = fr\ndescription = \xC3\x80 Paris\n"));
    data::load_mod_maker_file(*db, maker_file, "test");
    CHECK(db->mod_makers().size() == 1);
    if (!db->mod_makers().empty()) {
        CHECK(db->mod_makers()[0].name == disc_maker);
        CHECK(db->mod_makers()[0].description == "\x81\xC0 Paris");
    }
}

// Through CarDatabase::load with a mods list: the disc's JIS table (font/jis2uni.dat) encodes Japanese in a mod's names
// as the kanji font's pairs; a maker spelled as the disc spells it ("Citroën") is that maker.
TEST(modcar_texts_disc_jis) {
    Disc* disc = test_disc();
    if (!disc) SKIP("GT3R_DISC not set");
    const std::filesystem::path dir = temp_dir("modcar_texts_jis");
    std::filesystem::create_directories(dir / "cars");
    const std::string file = dir.generic_string() + "/cars/jp_test_ev.ini";
    CHECK(write_file(file, "[car]\nlabel = jp_test_ev\nname = \xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88 EV\n"
                           "maker = Citro\xC3\xABn\ndonor = legacy_gtb_96\n"));
    Volume& vol = disc->volume();
    const std::string list = "test\t" + file + "\n";
    data::CarDatabase db;
    CHECK(db.load(
        [&](const std::string& p) -> std::optional<Bytes> {
            if (p == Vfs::kModCarsList) return Bytes(list.begin(), list.end());
            const Volume::Entry* e = vol.find(p);
            return e ? std::optional<Bytes>(vol.read(*e)) : std::nullopt;
        },
        std::string("_us")));
    CHECK(db.mod_car_errors().empty());
    const data::Car* c = db.car("jp_test_ev");
    const data::Car* xsara = db.car("xsara_wrc_99");
    CHECK(c && xsara);
    if (!c || !xsara) return;
    CHECK(c->name == "\xA5\xC6\xA5\xB9\xA5\xC8 EV"); // テスト: JIS 0x2546 0x2539 0x2548
    CHECK(xsara->maker == "Citro\x81\xEBn");
    CHECK(c->maker == xsara->maker && c->maker_id == xsara->maker_id);
}

TEST(modcar_disc_end_to_end) {
    Disc* disc = test_disc();
    if (!disc) SKIP("GT3R_DISC not set");
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    size_t cars = db.cars().size();
    std::string err;
    auto def = data::parse_mod_car(kMinimal, &err);
    CHECK(def.has_value());
    CHECK(data::add_mod_car(db, *def, &err));
    db.rebuild();
    CHECK(db.cars().size() == cars + 1);
    const data::Car* c = db.car("test_ev");
    CHECK(c && c->name == "Test EV" && c->year == 2026);
    CHECK(db.model_of(*c) == db.model_of(*db.car("legacy_gtb_96"))); // borrowed shell
    CHECK(db.car_by_model(db.model_of(*c))->label == "legacy_gtb_96"); // the model still maps to the disc car
    auto spec = sim::build_car_spec(db, "test_ev", &err);
    CHECK(spec.has_value());
    CHECK(spec->engine.electric && spec->gearbox.ratios.size() == 1);
    CHECK(std::fabs(spec->gearbox.ratios[0] * spec->gearbox.final_drive - 9.04f) < 0.01f);
    CHECK(std::fabs(spec->peak_torque() - 600) < 6);
    CHECK(std::fabs(spec->axle[1].tire_width - 0.275f) < 1e-4f);
    // Upgrade parts were cloned for the new car (tyres, suspension), engine parts were not.
    CHECK(!db.parts_for(data::TableId::FrontTire, "test_ev").empty());
    CHECK(db.parts_for(data::TableId::TurbineKit, "test_ev").empty());
    CHECK(db.mod_car("test_ev") && db.mod_car("test_ev")->fit.active());
    // The game's physics has no EV drive yet: an electric mod car races on its donor's engine, engine parts, driveline and
    // gearbox (and their settings), on its own body and tyres.
    using T = data::TableId;
    std::optional<sim::exact::Equipment> ev = race::exact_equipment(db, {"test_ev"});
    std::optional<sim::exact::Equipment> donor = race::exact_equipment(db, {"legacy_gtb_96"});
    CHECK(ev && donor);
    if (ev && donor) {
        for (T t : {T::Engine, T::Drivetrain, T::Gear}) CHECK(ev->part[size_t(t)] == donor->part[size_t(t)]);
        CHECK(ev->word(0x110) == donor->word(0x110) && ev->word(0xF4) == donor->word(0xF4)); // final drive, first gear
        CHECK(ev->part[size_t(T::FrontTire)] != donor->part[size_t(T::FrontTire)]);
    }
    const std::vector<float> v = straight_kmh(*disc, db, "test_ev", {6.0});
    CHECK(v.size() == 1 && v[0] > 60); // (it drives, on the donor's engine)
    // A second definition with a disc car's label is refused.
    def->label = "legacy_gtb_96";
    CHECK(!data::add_mod_car(db, *def, &err));
}

// --- custom bodies: JSON, PNG and binary glTF -------------------------------------

#include "formats/json.h"
#include "formats/model/gltf.h"
#include "formats/png_reader.h"
#include "formats/png_writer.h"

#include <cstring>

TEST(json_parse) {
    auto v = json::Value::parse(R"({"a": [1, 2.5, -3e2], "s": "xé\n", "t": true, "n": null, "o": {"k": 1}})");
    CHECK(v["a"].size() == 3 && v["a"][2].number() == -300);
    CHECK(v["s"].string() == "x\xC3\xA9\n");
    CHECK(v["t"].boolean() && v["n"].is_null() && v["o"]["k"].integer() == 1);
    CHECK(v["missing"]["deeper"].is_null()); // lookups chain safely
    bool threw = false;
    try {
        json::Value::parse("{\"a\": [1, 2}");
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(png_roundtrip) {
    std::vector<u32> px(7 * 5);
    for (size_t i = 0; i < px.size(); ++i) px[i] = u32(i * 2654435761u) | 0xFF000000u;
    PngImage img = decode_png(encode_png(7, 5, px));
    CHECK(img.width == 7 && img.height == 5 && img.rgba == px);
}

namespace {
// One triangle with a "paint" material, facing +Z (the glTF front), 1 m in front of the origin.
Bytes tiny_glb() {
    float pos[9] = {0, 0, 1, 1, 0, 1, 0, 1, 1};
    u16 idx[4] = {0, 1, 2, 0};
    Bytes bin(sizeof(pos) + sizeof(idx));
    std::memcpy(bin.data(), pos, sizeof(pos));
    std::memcpy(bin.data() + sizeof(pos), idx, sizeof(idx));
    std::string js =
        R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"material":0}]}],)"
        R"("materials":[{"name":"Body Paint","pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1]}}],)"
        R"("buffers":[{"byteLength":44}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},)"
        R"({"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},)"
        R"({"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}]})";
    while (js.size() % 4) js += ' ';
    Bytes out(12);
    auto put32 = [&](u32 v) { size_t o = out.size(); out.resize(o + 4); std::memcpy(out.data() + o, &v, 4); };
    std::memcpy(out.data(), "glTF", 4);
    u32 v2 = 2, total = u32(12 + 8 + js.size() + 8 + bin.size());
    std::memcpy(out.data() + 4, &v2, 4);
    std::memcpy(out.data() + 8, &total, 4);
    put32(u32(js.size()));
    put32(0x4E4F534A);
    out.insert(out.end(), js.begin(), js.end());
    put32(u32(bin.size()));
    put32(0x004E4942);
    out.insert(out.end(), bin.begin(), bin.end());
    return out;
}
} // namespace

TEST(glb_import) {
    GlbOptions o;
    o.paint = std::array<float, 3>{1.0f, 0.0f, 0.0f};
    scene::Model m = load_glb(tiny_glb(), o, "tiny");
    CHECK(m.meshes.size() == 2 && m.meshes[0].name == "body" && m.meshes[1].name == "body_env");
    const scene::Mesh& b = m.meshes[0];
    CHECK(b.vertices.size() == 3 && b.indices.size() == 3 && b.primitives.size() == 1);
    // glTF faces +Z; GT3 noses point to -Z and +X is the car's right: a half turn about Y.
    CHECK(b.vertices[1].pos[0] == -1 && b.vertices[0].pos[2] == -1);
    CHECK(b.primitives[0].flags & scene::kTintable);
    CHECK(b.vertices[0].color == 0xFF0000FFu); // the paint colour (RGBA8 in memory order R, G, B, A)
    CHECK(b.vertices[0].normal[2] < -0.99f);   // computed normal, turned with the model (faces -Z now)
    CHECK(m.meshes[1].primitives[0].flags & scene::kEnvironmentMap);
}

// --- the shipped example mod (examples/mods/example_coupe) --------------------------
// Loads it the way the game does, checks its body and drives it, so the example that
// docs/MODDING.md points modders to can't silently stop working.

#include "base/file.h"
#include "formats/model/wheel_spec.h"

TEST(example_mod_car) {
    Disc* disc = test_disc();
    if (!disc) SKIP("GT3R_DISC not set");
    const std::string dir = std::string(GT3R_SOURCE_DIR) + "/examples/mods/example_coupe";
    const std::string label = "gt3r_concept_coupe_24";
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));

    // The definition parses without errors or warnings (every key is one the loader knows).
    auto text = read_host_file(dir + "/cars/" + label + ".ini");
    CHECK(text.has_value());
    if (!text) return;
    std::string err;
    std::vector<std::string> warns;
    auto def = data::parse_mod_car(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()), &err, &warns);
    CHECK(def.has_value() && warns.empty());
    for (const std::string& w : warns) std::printf("    warning: %s\n", w.c_str());
    CHECK(def && def->paints.size() == 6 && !def->model_file.empty() && !def->wheel.empty());

    data::load_mod_maker_file(db, dir + "/makers/gt3r.ini", "example_coupe");
    data::load_mod_car_files(db, {dir + "/cars/" + label + ".ini"}, "example_coupe");
    db.rebuild();
    CHECK(db.mod_car_errors().empty());
    const data::Car* c = db.car(label);
    CHECK(c != nullptr);
    if (!c) return;
    auto s = db.summarize(*c);
    CHECK(s.maker == "GT3R" && s.weight_kg == 1420 && s.wheelbase_mm == 2799);
    CHECK(db.mod_makers().size() == 1 && db.mod_makers()[0].region == "gr");
    const data::ModCar* m = db.mod_car(label);
    CHECK(m && m->for_sale && !m->picture.empty() && !m->thumb.empty());
    if (!m) return;
    for (const std::string* png : {&m->picture, &m->thumb}) {
        auto b = read_host_file(*png);
        CHECK(b.has_value());
        if (b) {
            PngImage img = decode_png(*b);
            CHECK((img.width == 640 && img.height == 448) || (img.width == 51 && img.height == 27));
        }
    }

    // The body: a glTF the loader reads, sized like the [body] numbers, with paint and glass.
    auto glb = read_host_file(m->model_file);
    CHECK(glb.has_value() && glb->size() < 4u * 1024 * 1024);
    if (!glb) return;
    scene::Model body = load_glb(*glb, GlbOptions{}, label);
    size_t tris = 0;
    bool paint = false, glass = false;
    for (const scene::Mesh& mesh : body.meshes) {
        if (mesh.name != "body") continue;
        tris += mesh.indices.size() / 3;
        for (const scene::Primitive& p : mesh.primitives) {
            paint = paint || (p.flags & scene::kTintable);
            glass = glass || (p.flags & scene::kAlphaBlend);
        }
    }
    CHECK(tris > 20000 && tris < 60000);
    CHECK(paint && glass);
    // GT3 axes: nose towards -Z, ground at y = 0, origin at the wheelbase midpoint.
    CHECK(std::fabs((body.bounds_max[2] - body.bounds_min[2]) - 4.357f) < 0.02f);
    CHECK(std::fabs((body.bounds_max[0] - body.bounds_min[0]) - 2.34f) < 0.03f); // with the mirrors
    CHECK(body.bounds_min[2] < -2.2f && body.bounds_max[2] < 2.1f);               // longer front overhang
    CHECK(body.bounds_min[1] > 0.05f && body.bounds_min[1] < 0.15f && std::fabs(body.bounds_max[1] - 1.149f) < 0.02f);
    // The engine's wheels match the modelled ones (768 mm across).
    auto ws = wheel_spec_for_car(db, label);
    CHECK(ws.has_value() && std::fabs(ws->radius[0] - 0.384f) < 0.01f && std::fabs(ws->radius[1] - 0.384f) < 0.01f);

    // It drives on the game's physics: full throttle down the machine tests' straight passes 100 km/h in under 8 s and keeps
    // pulling.
    auto spec = sim::build_car_spec(db, label, &err);
    CHECK(spec.has_value());
    const std::vector<float> v = straight_kmh(*disc, db, label, {8.0, 20.0});
    CHECK(v.size() == 2);
    if (v.size() < 2) return;
    std::printf("    %s: %.0f km/h at 8 s, %.0f km/h at 20 s\n", label.c_str(), v[0], v[1]);
    CHECK(v[0] > 100);
    CHECK(v[1] > 180 && v[1] < 290);
}

// --- a mod car races the same from its arcade record as from its own parts ----------
// Arcade mode, quick races and gt3tool modcar find a mod car's label among the arcade cars first (table 35, arcade_equipment);
// a GT Mode garage car races on its parts and settings. The arcade record holds the car's stock settings, not the donor's
// arcade tune, so the mod's own final drive, output factor and suspension reach the race either way.

#include "sim/exact/car_fields.h"

namespace {

const char* kArcadeTuned = R"(
[car]
label = test_arcade_tuned
name = Arcade Tuned
donor = rx8_01            ; its arcade record has its own ride height and springs
[body]
weight_kg = 1350
ride_height_mm = 130
[powertrain]
type = engine
curve = 2000:180 5000:220 8000:200
max_rpm = 8500
output_pct = 112
[drivetrain]
gears = 3.20 2.10 1.50 1.15 0.95 0.80
final_drive = 4.30
)";

const char* kArcadeNone = R"(
[car]
label = test_arcade_none
name = Arcade None
donor = legacy_gtb_96     ; no arcade record of its own
[powertrain]
type = engine
output_pct = 105          ; on the donor's curve
max_rpm = 7200
[drivetrain]
final_drive = 4.10
)";

u16 rec16(const data::CarDatabase& db, data::TableId t, const std::string& label, size_t o) {
    const ByteSpan* r = db.record(t, label);
    return r && r->size() >= o + 2 ? u16((*r)[o] | (*r)[o + 1] << 8) : 0;
}

} // namespace

TEST(mod_car_arcade_matches_garage) {
    Disc* disc = test_disc();
    if (!disc) SKIP("GT3R_DISC not set");
    std::optional<Bytes> core = disc->volume().read_program();
    CHECK(core.has_value());
    if (!core) return;
    namespace ex = sim::exact;
    using T = data::TableId;
    const ex::ProgramData d = ex::ProgramData::load(CoreImage::decode(*core));
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    const std::string coupe = "gt3r_concept_coupe_24";
    data::load_mod_car_files(db, {std::string(GT3R_SOURCE_DIR) + "/examples/mods/example_coupe/cars/" + coupe + ".ini"}, "example_coupe");
    std::string err;
    for (const char* text : {kArcadeTuned, kArcadeNone, kMinimal}) {
        auto def = data::parse_mod_car(text, &err);
        CHECK(def && data::add_mod_car(db, *def, &err));
    }
    db.rebuild();
    CHECK(db.mod_car_errors().empty());

    // The mods' own final drive (x 1000) and output factor.
    const std::pair<std::string, std::pair<int, int>> cars[] = {
        {coupe, {3910, 118}}, {"test_arcade_tuned", {4300, 112}}, {"test_arcade_none", {4100, 105}}, {"test_ev", {0, 0}}};
    for (const auto& [label, want] : cars) {
        const data::Car* c = db.car(label);
        CHECK(c != nullptr);
        if (!c) continue;
        bool arcade = false;
        for (const data::CarVariant& v : db.arcade_cars()) arcade = arcade || v.label == label;
        CHECK(arcade); // (so the label alone races its arcade record)
        const std::optional<ex::Equipment> by_label = race::exact_equipment(db, {label});
        const std::optional<ex::Equipment> by_parts = race::exact_equipment(db, {label, c->parts});
        const std::optional<ex::Equipment> garage = race::exact_equipment(db, {label, c->parts, sim::stock_settings(db, label, c->parts)});
        CHECK(by_label && by_parts && garage);
        if (!by_label || !by_parts || !garage) continue;
        CHECK(by_label->car == by_parts->car && by_label->part == by_parts->part);
        for (size_t k = 0; k < by_label->setting.size(); ++k)
            if (by_label->setting[k] != by_parts->setting[k] || by_label->setting[k] != garage->setting[k]) {
                std::printf("    %s: EQ +0x%zX arcade %d, stock %d, garage %d\n", label.c_str(), 0xF0 + 4 * k, by_label->setting[k],
                            by_parts->setting[k], garage->setting[k]);
                CHECK(false);
            }
        ex::ConvertInputs in;
        in.base = ex::kParams;
        const ex::ParamImage p = ex::race_params(db, d, *by_label, 0, true, true, in);
        CHECK(p == ex::race_params(db, d, *by_parts, 0, true, true, in));
        CHECK(p == ex::race_params(db, d, *garage, 0, true, true, in));
        if (want.first) {
            CHECK(by_label->word(0x110) == want.first);
            const ByteSpan* en = db.record(T::Engine, c->parts[6]);
            CHECK(en && en->size() > 0x42 && (*en)[0x42] == want.second);
            // Every gearbox the shop sells for it shares the stock final drive, as on the disc.
            for (const data::PartInfo& g : db.parts_for(T::Gear, label)) {
                CHECK(rec16(db, T::Gear, g.label, 0x22) == want.first);
                CHECK(rec16(db, T::Gear, g.label, 0x26) <= want.first && rec16(db, T::Gear, g.label, 0x24) >= want.first);
            }
        }
    }
    // Without a curve the rev limit applies to the donor's.
    const data::Car* none = db.car("test_arcade_none");
    const ByteSpan* en = none ? db.record(T::Engine, none->parts[6]) : nullptr;
    CHECK(en && en->size() > 0x45 && (*en)[0x45] == 72);
}
