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
#include "race/paint.h"
#include "race/results.h"
#include "sim/car_spec.h"
#include "sim/exact/race_flow.h"
#include "sim/exact/race_record.h"
#include "sim/exact/start.h"
#include "sim/exact/tick.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
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
    // The driver's side: left or right, unset without the key.
    CHECK(!d->driver_side);
    auto side = data::parse_mod_car("[car]\nlabel = x\nname = X\ndonor = y\ndriver_side = Right\n", &err);
    CHECK(side && side->driver_side && *side->driver_side == data::DriverSide::Right);
    CHECK(!data::parse_mod_car("[car]\nlabel = x\nname = X\ndonor = y\ndriver_side = middle\n", &err) &&
          err.find("driver_side") != std::string::npos);
    // Unknown keys are warnings, not errors.
    warns.clear();
    CHECK(data::parse_mod_car("[car]\nlabel = x\nname = X\ndonor = y\nspoiler = big\n", &err, &warns).has_value());
    CHECK(warns.size() == 1);
}

TEST(modcar_model_scenes) {
    // Issue 412: a menu model (no driver) and race models (with one), each falling back so
    // any single file serves every scene.
    using data::CarScene;
    auto files = [](const std::string& model) {
        std::string err;
        auto d = data::parse_mod_car("[car]\nlabel = x\nname = X\ndonor = y\n[model]\n" + model, &err);
        CHECK(d.has_value());
        return d ? data::resolve_model_files(*d) : std::array<std::string, data::kCarScenes>{};
    };
    auto is = [](const std::array<std::string, data::kCarScenes>& f, const char* menu, const char* day,
                 const char* eve, const char* night) {
        return f[size_t(CarScene::Menu)] == menu && f[size_t(CarScene::Day)] == day &&
               f[size_t(CarScene::Eve)] == eve && f[size_t(CarScene::Night)] == night;
    };
    CHECK(is(files("wheel = a\n"), "", "", "", ""));
    CHECK(is(files("file = a.glb\n"), "a.glb", "a.glb", "a.glb", "a.glb"));
    CHECK(is(files("menu = m.glb\n"), "m.glb", "m.glb", "m.glb", "m.glb"));
    CHECK(is(files("race = r.glb\n"), "r.glb", "r.glb", "r.glb", "r.glb"));
    CHECK(is(files("menu = m.glb\nrace = r.glb\n"), "m.glb", "r.glb", "r.glb", "r.glb"));
    CHECK(is(files("file = a.glb\nrace = r.glb\n"), "a.glb", "r.glb", "r.glb", "r.glb"));
    CHECK(is(files("file = a.glb\nmenu = m.glb\n"), "m.glb", "a.glb", "a.glb", "a.glb"));
    CHECK(is(files("menu = m.glb\nrace = r.glb\nnight = n.glb\n"), "m.glb", "r.glb", "r.glb", "n.glb"));
    // Only some times of day: the others take the first given, then the menus do too.
    CHECK(is(files("night = n.glb\n"), "n.glb", "n.glb", "n.glb", "n.glb"));
    CHECK(is(files("eve = e.glb\nnight = n.glb\nmenu = m.glb\n"), "m.glb", "e.glb", "e.glb", "n.glb"));
    std::string err;
    auto d = data::parse_mod_car("[car]\nlabel = x\nname = X\ndonor = y\n[model]\nfile = a.glb\nmenu = a.glb\nday = d.glb\n", &err);
    CHECK(d && data::model_files_of(*d) == std::vector<std::string>({"a.glb", "d.glb"}));
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
    // One name: a blank second name, so the results and records show it once. The same in every language's fields
    // (car_name_fields: US +0xE4 / +0xE6, Japanese +0xF2 / +0xF4, British English +0x100 / +0x102).
    CHECK(c->name_alt.find_first_not_of(' ') == std::string::npos);
    CHECK(race::result_car_name(*db, "syn_mod", true) == c->latin_name);
    const ByteSpan* r = db->record(data::TableId::Car, "syn_mod");
    CHECK(r != nullptr);
    if (r) {
        auto text = [&](size_t o) { return db->unistrings().at(rd16(*r, o)); };
        for (size_t o : {0xE4, 0xF2, 0x100}) CHECK(text(o) == c->name);
        for (size_t o : {0xE6, 0xF4, 0x102}) CHECK(text(o).find_first_not_of(' ') == std::string::npos);
    }
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
    def->driver_side = data::DriverSide::Right;
    CHECK(data::add_mod_car(*db, *def, &err));
    db->rebuild();
    CHECK(db->car("syn_mod2") && db->car("syn_mod2")->name == "Citro?n");
    // The driver's side: the file's own, whatever the body shows; without it the body's (and
    // unknown here with no body: no makers' countries were read).
    CHECK(db->driver_side(*db->car("syn_mod")) == data::DriverSide::Unknown);
    CHECK(db->driver_side(*db->car("syn_mod"), data::DriverSide::Left) == data::DriverSide::Left);
    CHECK(db->car("syn_mod2") && db->driver_side(*db->car("syn_mod2"), data::DriverSide::Left) == data::DriverSide::Right);
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

// Issue 413: a mod car's own paints ([paints]) are listed for the race under the car (mod cars may share a body), with
// the ids GT Mode gives them, so a chosen paint holds and "draw at start" draws from the car's own list, not paint 0.
TEST(modcar_race_paints) {
    auto db = cardb::empty_database();
    CHECK(db != nullptr);
    if (!db) return;
    cardb::add_car(*db, "syn_donor", 300, 75, "zz0001");
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
    db->put_record(data::TableId::Car, car);
    db->rebuild();
    std::string err;
    auto plain = data::parse_mod_car("[car]\nlabel = syn_plain\nname = Plain\ndonor = syn_donor\n", &err);
    auto painted = data::parse_mod_car("[car]\nlabel = syn_painted\nname = Painted\ndonor = syn_donor\n"
                                       "[paints]\nRed = #FF0000\nGreen = #00FF00\nBlue = #0000FF\n",
                                       &err);
    CHECK(plain && painted);
    if (!plain || !painted) return;
    CHECK(data::add_mod_car(*db, *plain, &err) && data::add_mod_car(*db, *painted, &err));
    db->rebuild();
    const std::string model = db->model_of(*db->car("syn_painted"));
    CHECK(model == "zz0001"); // the donor's body
    CHECK(race::paint_key(*db, "syn_donor", model) == model);
    CHECK(race::paint_key(*db, "syn_plain", model) == model); // no paints of its own: the body's list
    const std::string key = race::paint_key(*db, "syn_painted", model);
    CHECK(key == "mod:syn_painted");
    size_t mod_no = size_t(db->mod_car("syn_painted") - db->mod_cars().data());
    race::CarColors colors;
    colors.add_mod_cars(*db);
    const std::vector<u32>* ids = colors.ids(key);
    CHECK(ids && ids->size() == 3);
    CHECK(colors.ids(model) == nullptr); // the disc's list (none here) is left alone
    CHECK(colors.id_at(key, 1) == race::mod_paint_id(mod_no, 1) && colors.index_of(key, race::mod_paint_id(mod_no, 2)) == 2);
    CHECK(colors.rgb(colors.id_at(key, 0)) == 0x0000FFu && colors.rgb(colors.id_at(key, 2)) == 0xFF0000u); // bytes R, G, B
    // A chosen paint holds; unset ones are drawn from the car's own three, so the field is not all paint 0.
    std::vector<race::PaintEntry> e(4, race::PaintEntry{key});
    e[1].id = colors.id_at(key, 2);
    race::PaintRng rng;
    std::vector<int> p = race::assign_paints(colors, e, rng);
    CHECK(p.size() == 4 && p[1] == 2);
    bool all_first = true;
    for (int k : p) {
        CHECK(k >= 0 && k < 3);
        all_first = all_first && k == 0;
    }
    CHECK(!all_first);
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
    // An electric mod car races on its own motor, drivetrain and reduction, with the electric drive; its equipment with
    // its donor's engine in it (a race recorded before the drive existed) races on that engine.
    using T = data::TableId;
    std::optional<sim::exact::Equipment> ev = race::exact_equipment(db, {"test_ev"});
    std::optional<sim::exact::Equipment> donor = race::exact_equipment(db, {"legacy_gtb_96"});
    CHECK(ev && donor);
    if (ev && donor) {
        for (T t : {T::Engine, T::Drivetrain, T::Gear}) CHECK(ev->part[size_t(t)] != donor->part[size_t(t)]);
        const sim::exact::ElectricDrive d = sim::exact::electric_drive(&db, *ev);
        CHECK(d.on && d.regen_torque.host() > 0 && d.regen_power.host() > 0);
        sim::exact::Equipment old = *ev;
        old.part[size_t(T::Engine)] = donor->part[size_t(T::Engine)];
        CHECK(!sim::exact::electric_drive(&db, old).on);
        CHECK(!sim::exact::electric_drive(&db, *donor).on);
    }
    const std::vector<float> v = straight_kmh(*disc, db, "test_ev", {6.0});
    CHECK(v.size() == 1 && v[0] > 60);
    // A second definition with a disc car's label is refused.
    def->label = "legacy_gtb_96";
    CHECK(!data::add_mod_car(db, *def, &err));
}

TEST(modcar_regen_not_negative) {
    std::string err;
    std::string bad = std::string(kMinimal) + "\n[powertrain]\nregen_g = -0.2\n";
    CHECK(!data::parse_mod_car(bad, &err) && err.find("regen") != std::string::npos);
}

// The electric drive's steps (sim/exact/tick_engine.cpp, tick_gear.cpp) on a hand-built car, without a disc. The
// numbers are INVENTED: a flat 500 N m motor curve, 400 N m / 100 kW of regen, a 9:1 reduction, a 10,000 rpm top speed,
// rear drive, tyres whose best braking slip is 0.1.
TEST(modcar_electric_steps) {
    namespace ex = sim::exact;
    auto approx = [](float a, float b) { return std::fabs(a - b) < 0.05f; };
    auto car = std::make_unique<ex::Car>();
    ex::Car& c = *car;
    c.ev = {true, ex::F(400.0f), ex::F(100000.0f)};
    c.p.d.layout = 0; // rear drive: the motor turns with the rear axle
    c.gear = 1;
    c.p.d.ratio[1] = ex::F(9.0f);
    c.p.d.gear_speed[1] = ex::F(27.0f); // motor rad/s per m/s: 5 km/h is 37.5 rad/s at the motor
    c.p.e.rev_limit = 10000;            // 1,047 rad/s at the motor
    c.p.e.torque.n = 2;
    c.p.e.torque.x[0] = ex::F(0.0f), c.p.e.torque.y[0] = ex::F(500.0f);
    c.p.e.torque.x[1] = ex::F(2000.0f), c.p.e.torque.y[1] = ex::F(500.0f);
    c.p.tire[0].slip_brake = ex::F(0.1f); // (every wheel's tyre record, on every surface)
    c.p.brake_slip_scale = {ex::F(1.0f), ex::F(1.0f)};
    // The motor's torque with the rear axle at `axle` rad/s and the accelerator at `pedal`.
    auto out_f = [&](float axle, float pedal) {
        c.axle_speed = {ex::F(axle), ex::F(axle)};
        c.throttle_pedal = ex::F(pedal);
        return ex::engine_output(c, ex::F(pedal));
    };
    auto out = [&](float axle, float pedal) { return out_f(axle, pedal).host(); };
    CHECK(approx(out(0, 1), 500)); // full torque from standstill
    CHECK(approx(out(10, 0.1f), 50) && approx(out(10, 1), 500));
    // Lift-off regen against the motor's turning: its torque at town speeds, capped by its power at speed, faded in over
    // the accelerator's first 10 % and out below the motor's speed at 5 km/h.
    CHECK(approx(out(10, 0), -400));            // 90 rad/s at the motor
    CHECK(approx(out(60, 0), -100000.0f / 540)); // 540 rad/s: 100 kW
    CHECK(approx(out(10, 0.05f), 25 - 200));     // half the regen, and 5 % of the drive
    CHECK(approx(out(2, 0), -400 * 18 / 37.5f)); // 18 rad/s: below 5 km/h
    CHECK(approx(out(-10, 0), 400));             // rolling against the gear: still against the turning
    CHECK(out(0, 0) == 0);
    // ... eased off as the driven tyres slip past their best braking slip, none at twice it. The front tyres are not
    // driven: their slip does not count.
    c.w[0].slip_ratio = ex::F(0.5f);
    CHECK(approx(out(10, 0), -400));
    c.w[2].slip_ratio = ex::F(0.15f);
    CHECK(approx(out(10, 0), -200));
    c.w[3].slip_ratio = ex::F(0.25f);
    CHECK(out(10, 0) == 0);
    for (ex::Wheel& w : c.w) w.slip_ratio = ex::F();
    // The top speed: the drive fades out over the last 2 % below the motor's limit.
    const float top = 10000 * 6.2831853f / 60 / 9; // the axle's speed there
    CHECK(approx(out(top * 0.99f, 1), 250) && c.rev_limiter == 1);
    CHECK(approx(out(top * 1.01f, 1), 0));
    CHECK(approx(out(top * 0.9f, 1), 500) && c.rev_limiter == 0);
    // Nothing while the race holds the car.
    c.race_hold = 1;
    CHECK(out(10, 1) == 0);
    c.race_hold = 0;
    // The clutch stays locked: the motor is never disconnected (GT3's opens it when nothing asks for it).
    c.clutch = 1, c.clutch_req = ex::F();
    ex::clutch_engage(c);
    CHECK(c.clutch == 1);
    // The stop and hold: below 5 km/h with the accelerator up, the friction brakes take over the regen's torque at the
    // wheels (400 x 9 = 3,600 N m, a quarter on each), fading in as the regen fades out.
    auto hold_f = [&](float kmh, float pedal) {
        c.speed = ex::F(kmh / 3.6f);
        c.throttle_pedal = ex::F(pedal);
        ex::brakes_and_clutch(c);
        return c.t[0].brake_torque;
    };
    auto hold = [&](float kmh, float pedal) { return hold_f(kmh, pedal).host(); };
    CHECK(approx(hold(0, 0), 900) && c.t[3].brake_torque.host() == c.t[0].brake_torque.host());
    CHECK(approx(hold(2.5f, 0), 450));
    CHECK(hold(6, 0) == 0 && hold(0, 0.01f) == 0);
    // Forward and reverse (gear 0) only below 5 km/h, the clutch locked through the change: the automatic gearbox asked
    // for reverse at 150 km/h keeps the gear; at 3 km/h it changes. Back to forward the same way, rolling backwards.
    // While the change waits, the pedal that asked for it (in GT3's automatic the reverse button is the reverse
    // accelerator) brakes like a lifted accelerator instead of driving the car on the way it is going.
    const ex::GearSettings gs;
    c.throttle_pedal = ex::F();
    c.throttle = ex::F();
    c.reverse_req = 1;
    c.speed = ex::F(150.0f / 3.6f);
    ex::gear_step(c, gs);
    CHECK(c.gear == 1 && c.ev.waiting == 1);
    CHECK(approx(out(10, 1), -400)); // the reverse button's pedal, rolling forwards in gear 1: regen, no drive
    c.speed = ex::F(3.0f / 3.6f);
    ex::gear_step(c, gs);
    CHECK(c.gear == 0 && c.clutch == 1 && c.ev.waiting == 0);
    c.reverse_req = 0;
    c.throttle = ex::F(1.0f);
    c.speed = ex::F(-20.0f / 3.6f);
    ex::gear_step(c, gs);
    CHECK(c.gear == 0 && c.ev.waiting == 1);
    c.gear = 1; // (the reverse ratio here: gear 1's)
    CHECK(approx(out(-10, 1), 400)); // the accelerator, rolling backwards in reverse: regen against it, no drive
    c.gear = 0;
    c.speed = ex::F(-3.0f / 3.6f);
    ex::gear_step(c, gs);
    CHECK(c.gear == 1 && c.clutch == 1 && c.ev.waiting == 0);
    CHECK(approx(out(-2, 1), 500)); // changed: the accelerator drives forwards again
    c.throttle = ex::F();
    // Nothing asked: nothing waits.
    c.ev.waiting = 1;
    c.speed = ex::F(50.0f / 3.6f);
    ex::gear_step(c, gs);
    CHECK(c.gear == 1 && c.ev.waiting == 0);
    // Pinned: the drive's outputs over speeds, pedals and slips, and the hold. A change to the drive that moves them must
    // also change kElectricDriveVersion (race/exact_setup.cpp), so that replays recorded with the old drive are refused
    // rather than played differently; then update the value here.
    u64 h = 1469598103934665603ull; // FNV-1a over the outputs' bits
    auto mix = [&](ex::F v) { h = (h ^ v.bits) * 1099511628211ull; };
    for (float slip : {0.0f, 0.12f, 0.3f}) {
        c.w[2].slip_ratio = c.w[3].slip_ratio = ex::F(slip);
        for (float axle : {-30.0f, -2.0f, 0.0f, 2.0f, 10.0f, 60.0f, 115.5f, 117.0f})
            for (float pedal : {0.0f, 0.03f, 0.1f, 1.0f}) mix(out_f(axle, pedal));
    }
    for (float kmh : {0.0f, 1.0f, 4.9f}) mix(hold_f(kmh, 0));
    std::printf("    drive outputs %016llx\n", static_cast<unsigned long long>(h));
    CHECK(h == 0xc3bcf97606a3f908ull);
    // Off, the car is GT3's: its clutch opens when nothing asks for it, and a change into reverse slips it.
    c.ev.on = false;
    c.clutch = 1, c.clutch_req = ex::F();
    ex::clutch_engage(c);
    CHECK(c.clutch == 0);
    c.clutch = 1, c.gear = 1, c.speed = ex::F(), c.reverse_req = 1;
    ex::gear_step(c, gs);
    CHECK(c.gear == 0 && c.clutch == 2);
}

// A snapshot keeps its car's electric drive: a restore takes it along with the car's parameters, so a free run's ghost
// recorded on another car, or on a donor's engine, drives as it was recorded; and a replay file keeps it (record
// version 6). Without a disc.
TEST(modcar_electric_snapshot) {
    namespace ex = sim::exact;
    auto car = std::make_unique<ex::Car>();
    car->ev = {true, ex::F(400.0f), ex::F(100000.0f)};
    car->ghost = 2, car->index = 1, car->control = 1;
    auto snap = std::make_unique<ex::Snapshot>();
    snap->car.p.e.rev_limit = 7000; // the recorded car's own parameters, without the drive
    ex::restore_car(*car, *snap, 0);
    CHECK(!car->ev.on && car->p.e.rev_limit == 7000);
    CHECK(car->ghost == 2 && car->index == 1 && car->control == 1);
    snap->car.ev = {true, ex::F(123.45f), ex::F(67000.0f), 1}; // (a change of direction waiting)
    ex::restore_car(*car, *snap, 0);
    CHECK(car->ev.on && car->ev.regen_torque.bits == snap->car.ev.regen_torque.bits && car->ev.waiting == 1);
    ex::RaceRecord r;
    r.entries.resize(1);
    r.pads.resize(1);
    auto rec = std::make_shared<ex::SoloRecorders>(true);
    rec->snapshot[1].car.ev = snap->car.ev;
    r.recorders = rec;
    const ex::RaceRecord back = ex::RaceRecord::parse(r.serialize());
    CHECK(back.recorders != nullptr);
    if (!back.recorders) return;
    const ex::ElectricDrive& d = back.recorders->snapshot[1].car.ev;
    CHECK(d.on && d.regen_torque.bits == snap->car.ev.regen_torque.bits);
    CHECK(d.regen_power.bits == snap->car.ev.regen_power.bits && d.waiting == 1);
    CHECK(!back.recorders->snapshot[0].car.ev.on && back.recorders->snapshot[0].car.ev.waiting == 0);
}

namespace {

// A database with the minimal electric car (kMinimal), changed by `edit`.
template <class Edit>
bool electric_db(Disc& disc, data::CarDatabase& db, Edit&& edit) {
    std::string err;
    auto def = data::parse_mod_car(kMinimal, &err);
    if (!def || !db.load(disc.volume())) return false;
    edit(*def);
    if (!data::add_mod_car(db, *def, &err)) return false;
    db.rebuild();
    return true;
}

} // namespace

// The electric drive on the race's physics, the minimal electric car on the machine tests' straight: the clutch locked
// at rest and through the launch (the donor's, GT3's, is not at rest), its one gear, lift-off regen, the stop and hold,
// the top speed held at the motor's limit.
TEST(modcar_electric_drive) {
    Disc* disc = test_disc();
    if (!disc) SKIP("GT3R_DISC not set");
    data::CarDatabase db;
    // max_rpm 9000: 136 km/h through 9.04 on the 275/35R21 rears (r 0.363 m), within the car's power.
    CHECK(electric_db(*disc, db, [](data::ModCarDef& d) { d.max_rpm = 9000; }));
    sim::ControlInputs in;
    in.controller_kind = 2;
    {
        StraightRun donor = straight_run(*disc, db, "legacy_gtb_96");
        CHECK(donor.field != nullptr);
        if (donor.field) {
            while (donor.field->race_time() < 1.0) donor.step(in);
            const sim::VehicleTelemetry* t = donor.field->player().telemetry();
            std::printf("    at rest: the donor's clutch state %d\n", t ? t->clutch_state : -1);
            CHECK(t && t->clutch_state != 1);
        }
    }
    StraightRun run = straight_run(*disc, db, "test_ev");
    CHECK(run.field != nullptr);
    if (!run.field) return;
    const sim::VehicleState& s = run.field->player().state();
    const sim::VehicleTelemetry* t = run.field->player().telemetry();
    CHECK(t != nullptr);
    if (!t) return;
    auto drive = [&](double seconds) { // true when it stayed in gear 1 with the clutch locked
        const double end = run.field->race_time() + seconds;
        bool locked = true;
        while (run.field->race_time() < end) {
            run.step(in);
            locked = locked && s.gear == 1 && t->clutch_state == 1;
        }
        return locked;
    };
    while (run.field->race_time() < 0) run.step(in); // the countdown
    CHECK(drive(1.0) && s.speed_kmh < 0.5f);
    in.throttle = 1;
    CHECK(drive(0.25) && s.speed_kmh > 5); // full torque from standstill, nothing to slip
    CHECK(drive(1.5));
    // Lift-off at town speeds: the motor brakes at about the default 0.2 g.
    in.throttle = 0;
    drive(0.5);
    const float v0 = s.speed_kmh;
    drive(1.0);
    const float g = (v0 - s.speed_kmh) / 3.6f / 9.81f;
    std::printf("    lift-off from %.2f km/h: %.4f g\n", double(v0), double(g));
    CHECK(v0 > 49.5f && v0 < 50.5f && g > 0.177f && g < 0.183f); // (0.180 g from 50.1 km/h)
    // Below 5 km/h the regen has faded out and GT3's rolling resistance is 0 under 4 km/h (physics §3.5): the hold
    // stops the car in about half a second (without it, about 3 s), and keeps it stopped.
    while (s.speed_kmh >= 5 && run.field->race_time() < 60) run.step(in);
    const double slow = run.field->race_time();
    while (s.speed_kmh >= 0.05f && run.field->race_time() < slow + 10) run.step(in);
    std::printf("    from 5 km/h to a stop: %.2f s\n", run.field->race_time() - slow);
    CHECK(run.field->race_time() - slow < 1.0);
    drive(3.0);
    CHECK(s.speed_kmh < 0.05f);
    // Top speed: the motor's limit; the drive fades out over its last 2 %.
    in.throttle = 1;
    CHECK(drive(30.0));
    std::printf("    top speed %.1f km/h at %.0f rpm\n", double(s.speed_kmh), double(s.engine_rpm));
    CHECK(s.engine_rpm > 8930 && s.engine_rpm < 8950 && s.speed_kmh > 130);
}

// A change of direction asked at speed, the way GT3's automatic asks for it: the reverse button (the reverse
// accelerator) at 40 km/h, then the accelerator while reversing at about 15 km/h. Each waits until 5 km/h and brakes the car
// down to it, instead of driving it on the way it is going; then the car drives off the other way.
TEST(modcar_electric_direction_change) {
    Disc* disc = test_disc();
    if (!disc) SKIP("GT3R_DISC not set");
    data::CarDatabase db;
    CHECK(electric_db(*disc, db, [](data::ModCarDef&) {}));
    StraightRun run = straight_run(*disc, db, "test_ev");
    CHECK(run.field != nullptr);
    if (!run.field) return;
    const sim::VehicleState& s = run.field->player().state();
    sim::ControlInputs in;
    in.controller_kind = 2;
    while (run.field->race_time() < 0) run.step(in);
    in.throttle = 1;
    while (s.speed_kmh < 40 && run.field->race_time() < 20) run.step(in);
    // Reverse at 40 km/h: the car slows in gear 1 (never faster than it was), takes reverse near standstill, and reverses.
    // (speed_kmh is the speed either way. Each pedal is let go half a second before the next is pressed: the car still
    // gains a little speed for a few steps after a pedal is let go. The gearbox decides after the drive in a step, as in
    // GT3, so the pressed pedal drives for one step before the change waits: about 1 km/h.)
    in.throttle = 0;
    for (const double end = run.field->race_time() + 0.5; run.field->race_time() < end;) run.step(in);
    run.field->player().set_reverse_request(true);
    float v = s.speed_kmh, fastest = v, slowest = v;
    double t0 = run.field->race_time();
    while (s.gear == 1 && run.field->race_time() < t0 + 20) {
        run.step(in);
        fastest = std::max(fastest, s.speed_kmh), slowest = std::min(slowest, s.speed_kmh);
    }
    while (s.speed_kmh < 15 && run.field->race_time() < t0 + 20) run.step(in);
    std::printf("    reverse from %.1f km/h: at most %.1f km/h before reverse, at %.1f km/h; reversing at 15 km/h after %.2f s\n",
                double(v), double(fastest), double(slowest), run.field->race_time() - t0);
    CHECK(fastest <= v + 2 && slowest < 5.5f); // (the change at 5 km/h of the car's own forward speed)
    CHECK(s.gear == -1 && s.speed_kmh >= 15 && run.field->race_time() - t0 < 8);
    // The accelerator while reversing at about 15 km/h: the car slows in reverse, takes gear 1 near standstill, and drives off.
    run.field->player().set_reverse_request(false);
    for (const double end = run.field->race_time() + 0.5; run.field->race_time() < end;) run.step(in);
    CHECK(s.gear == -1); // (still in reverse, rolling backwards)
    in.throttle = 1;
    v = s.speed_kmh, fastest = v, slowest = v;
    t0 = run.field->race_time();
    while (s.gear == -1 && run.field->race_time() < t0 + 20) {
        run.step(in);
        fastest = std::max(fastest, s.speed_kmh), slowest = std::min(slowest, s.speed_kmh);
    }
    while (s.speed_kmh < 15 && run.field->race_time() < t0 + 20) run.step(in);
    std::printf("    forward from %.1f km/h reversing: at most %.1f km/h before gear 1, at %.1f km/h; 15 km/h after %.2f s\n",
                double(v), double(fastest), double(slowest), run.field->race_time() - t0);
    CHECK(fastest <= v + 2 && slowest < 5.5f); // (the change at 5 km/h of the car's own forward speed)
    CHECK(s.gear == 1 && s.speed_kmh >= 15 && run.field->race_time() - t0 < 8);
}

// Regen far past the driven axle's grip (rear drive, 2,000 N m at the motor, 1 MW): it eases off as the rear tyres
// slip, so they keep turning near the road's speed instead of being dragged down while the car slides on.
TEST(modcar_electric_regen_grip) {
    Disc* disc = test_disc();
    if (!disc) SKIP("GT3R_DISC not set");
    data::CarDatabase db;
    CHECK(electric_db(*disc, db, [](data::ModCarDef& d) {
        d.drive = u8(data::DriveType::FR), d.front_torque_pct.reset();
        d.regen_torque_nm = 2000.0f, d.regen_kw = 1000.0f;
    }));
    StraightRun run = straight_run(*disc, db, "test_ev");
    CHECK(run.field != nullptr);
    if (!run.field) return;
    const sim::VehicleState& s = run.field->player().state();
    sim::ControlInputs in;
    in.controller_kind = 2;
    while (run.field->race_time() < 0) run.step(in);
    in.throttle = 1;
    while (s.speed_kmh < 100 && run.field->race_time() < 20) run.step(in);
    in.throttle = 0;
    const float v0 = s.speed_kmh;
    float slip = 0;
    for (const double end = run.field->race_time() + 2.0; run.field->race_time() < end;) {
        run.step(in);
        slip = std::max({slip, s.wheels[2].slip_ratio, s.wheels[3].slip_ratio});
    }
    std::printf("    lift-off from %.0f km/h: %.0f km/h after 2 s, the rear tyres' slip at most %.3f\n", double(v0),
                double(s.speed_kmh), double(slip));
    CHECK(v0 >= 100 && slip < 0.3f && s.speed_kmh < v0 - 20);
}

// The replay's data fingerprint: an electric car on its own motor adds its regen and the drive's version, which no
// record holds; the same car on its donor's engine (a race recorded before the drive) is its records' alone, as before.
TEST(modcar_electric_fingerprint) {
    Disc* disc = test_disc();
    if (!disc) SKIP("GT3R_DISC not set");
    data::CarDatabase a, b;
    CHECK(electric_db(*disc, a, [](data::ModCarDef&) {}));
    CHECK(electric_db(*disc, b, [](data::ModCarDef& d) { d.regen_g = 0.3f; })); // the same records, more regen
    std::optional<sim::exact::Equipment> ev = race::exact_equipment(a, {"test_ev"});
    std::optional<sim::exact::Equipment> donor = race::exact_equipment(a, {"legacy_gtb_96"});
    CHECK(ev && donor);
    if (!ev || !donor) return;
    sim::exact::StartEntry own, old;
    own.eq = *ev;
    old.eq = *ev;
    old.eq.part[size_t(data::TableId::Engine)] = donor->part[size_t(data::TableId::Engine)];
    auto print = [](const data::CarDatabase& db, const sim::exact::StartEntry& e) {
        return race::exact_data_fingerprint(db, std::span<const sim::exact::StartEntry>(&e, 1), {});
    };
    CHECK(sim::exact::electric_drive(&a, own.eq).on && !sim::exact::electric_drive(&a, old.eq).on);
    CHECK(print(a, own) != print(b, own)); // the regen is in it
    CHECK(print(a, old) == print(b, old)); // not without the drive
    CHECK(print(a, own) != print(a, old));
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
    auto glb = read_host_file(m->model_file(data::CarScene::Menu));
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
    // One `file`: every scene draws it.
    for (size_t i = 1; i < data::kCarScenes; ++i) CHECK(m->model_files[i] == m->model_files[0]);
    {
        // A menu model and a race model (issue 412): the menus draw one, races of every time
        // of day the other.
        const std::filesystem::path dir2 = test::temp_path("scene_models");
        std::filesystem::create_directories(dir2 / "cars");
        const std::string race_glb = (dir2 / "race.glb").string();
        CHECK(write_host_file(race_glb, *glb));
        std::string ini(text->begin(), text->end());
        const size_t at = ini.find("\nfile = ");
        CHECK(at != std::string::npos);
        if (at == std::string::npos) return;
        ini.replace(at, ini.find('\n', at + 1) - at, "\nmenu = " + m->model_files[0] + "\nrace = race.glb");
        const std::string file2 = (dir2 / "cars" / (label + ".ini")).string();
        CHECK(write_host_file(file2, Bytes(ini.begin(), ini.end())));
        data::CarDatabase db2;
        CHECK(db2.load(disc->volume()));
        data::load_mod_maker_file(db2, dir + "/makers/gt3r.ini", "example_coupe");
        data::load_mod_car_files(db2, {file2}, "scene_models");
        db2.rebuild();
        CHECK(db2.mod_car_errors().empty());
        const data::ModCar* m2 = db2.mod_car(label);
        CHECK(m2 && m2->model_file(data::CarScene::Menu) == m->model_files[0]);
        if (!m2) return;
        for (data::CarScene sc : {data::CarScene::Day, data::CarScene::Eve, data::CarScene::Night})
            CHECK(m2->model_file(sc) == race_glb);
        CarBodyMod menu_body = body_mod_for_car(db2, label, 0, data::CarScene::Menu);
        CarBodyMod race_body = body_mod_for_car(db2, label, 0, data::CarScene::Night);
        CHECK(menu_body.glb && race_body.glb && menu_body.glb != race_body.glb);
    }
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

// The example's electric car: the same body on its own motor (docs/MODDING.md "Electric cars").
TEST(example_mod_car_electric) {
    Disc* disc = test_disc();
    if (!disc) SKIP("GT3R_DISC not set");
    const std::string dir = std::string(GT3R_SOURCE_DIR) + "/examples/mods/example_coupe";
    const std::string coupe = "gt3r_concept_coupe_24", label = "gt3r_concept_coupe_e_24";
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));

    auto text = read_host_file(dir + "/cars/" + label + ".ini");
    CHECK(text.has_value());
    if (!text) return;
    std::string err;
    std::vector<std::string> warns;
    auto def = data::parse_mod_car(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()), &err, &warns);
    CHECK(def.has_value() && warns.empty() && def->electric);

    data::load_mod_maker_file(db, dir + "/makers/gt3r.ini", "example_coupe");
    data::load_mod_car_files(db, {dir + "/cars/" + coupe + ".ini", dir + "/cars/" + label + ".ini"}, "example_coupe");
    db.rebuild();
    CHECK(db.mod_car_errors().empty());
    const data::ModCar* m = db.mod_car(label);
    const data::ModCar* c = db.mod_car(coupe);
    CHECK(m && c && m->electric && !c->electric && m->model_files == c->model_files);
    if (!m) return;
    CHECK(m->for_sale && !m->picture.empty() && !m->thumb.empty());
    for (const std::string* png : {&m->picture, &m->thumb}) {
        auto b = read_host_file(*png);
        CHECK(b.has_value());
        if (b) {
            PngImage img = decode_png(*b);
            CHECK((img.width == 640 && img.height == 448) || (img.width == 51 && img.height == 27));
        }
    }

    // It races on its own motor: the electric drive is on, it passes 100 km/h well before the coupe does (3.8 s in
    // gt3tool modcar), and its motors' top speed holds it at 250 km/h.
    std::optional<sim::exact::Equipment> eq = race::exact_equipment(db, {label});
    CHECK(eq.has_value() && sim::exact::electric_drive(&db, *eq).on);
    const std::vector<float> v = straight_kmh(*disc, db, label, {4.5, 30.0});
    CHECK(v.size() == 2);
    if (v.size() < 2) return;
    std::printf("    %s: %.0f km/h at 4.5 s, %.0f km/h at 30 s\n", label.c_str(), v[0], v[1]);
    CHECK(v[0] > 100);
    CHECK(v[1] > 235 && v[1] < 252);
}

// --- a mod car races the same from its arcade record as from its own parts ----------
// Arcade mode, quick races and gt3tool modcar find a mod car's label among the arcade cars first (table 35, variant_equipment);
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
