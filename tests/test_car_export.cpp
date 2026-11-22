// Exported cars (game/data/car_export.h): a disc car written as a mod and loaded on another disc.
// The synthetic tests run everywhere (invented records); the disc test needs $GT3R_DISC (NTSC-U) and
// $GT3R_DISC_CONCEPT.

#include "test_framework.h"
#include "car_db_synth.h"
#include "concept_discs.h"

#include "base/config.h"
#include "base/file.h"
#include "disc/disc.h"
#include "game/data/car_database.h"
#include "game/data/car_export.h"
#include "game/data/mod_cars.h"
#include "sim/car_spec.h"
#include "vfs/vfs.h"

#include <filesystem>
#include <map>

using namespace gt3r;
using namespace gt3r::test;

namespace {

using data::TableId;

// Invented shared records and the drivetrain / gearbox the loader wants, on top of cardb::add_car's car.
void add_extras(data::CarDatabase& db, const std::string& label, u8 steer_lock) {
    auto rec = [&](TableId t, const std::string& l, size_t size, u64 car) {
        Bytes b(size);
        cardb::put64(b, 0, db::id_hash(l));
        cardb::put64(b, 8, car);
        db.add_label(l);
        return b;
    };
    const u64 car = db::id_hash(label);
    Bytes dt = rec(TableId::Drivetrain, "dt_" + label, 40, car);
    dt[0x14] = 0; // FR
    db.put_record(TableId::Drivetrain, dt);
    Bytes ge = rec(TableId::Gear, "ge_" + label, 48, car);
    ge[0x11] = 5;
    db.put_record(TableId::Gear, ge);
    Bytes steer = rec(TableId::Steer, "n", 64, 0);
    steer[0x11] = steer_lock;
    db.put_record(TableId::Steer, steer);
    Bytes c(db.record(TableId::Car, label)->begin(), db.record(TableId::Car, label)->end());
    cardb::put64(c, 8 + 8 * 2, db::id_hash("n"));
    cardb::put64(c, 8 + 8 * 13, db::id_hash("dt_" + label));
    cardb::put64(c, 8 + 8 * 18, db::id_hash("ge_" + label));
    db.put_record(TableId::Car, c);
}

} // namespace

TEST(car_records_roundtrip) {
    data::CarRecordFile f;
    data::CarRecordFile::Record r;
    r.table = TableId::Engine;
    r.label = "en_test";
    r.bytes.assign(88, 7);
    r.texts.push_back({0x38, data::CarRecordFile::Text::String, "1998cc"});
    f.records.push_back(r);
    const Bytes b = data::serialize_car_records(f);
    std::string err;
    auto back = data::parse_car_records(b, &err);
    CHECK(back && back->records.size() == 1);
    if (back) {
        CHECK(back->records[0].label == "en_test" && back->records[0].bytes == r.bytes);
        CHECK(back->records[0].texts.size() == 1 && back->records[0].texts[0].text == "1998cc" &&
              back->records[0].texts[0].kind == data::CarRecordFile::Text::String);
    }
    CHECK(!data::parse_car_records(ByteSpan(b).first(b.size() - 1), &err)); // cut short
    Bytes extra = b;
    extra.push_back(0);
    CHECK(!data::parse_car_records(extra, &err)); // trailing data
    CHECK(!data::parse_car_records(Bytes(16, 0), &err)); // not one
}

TEST(car_export_synthetic) {
    // The source disc: car "alpha" on model "zz0001", steering record "n" with a 30 degree lock.
    auto src = cardb::empty_database();
    auto dst = cardb::empty_database();
    CHECK(src && dst);
    if (!src || !dst) return;
    cardb::add_car(*src, "alpha", 2000, 75, "zz0001");
    add_extras(*src, "alpha", 30);
    src->rebuild();
    // The target disc: a car of its own, with another "n" (a 40 degree lock).
    cardb::add_car(*dst, "beta", 1500, 70, "yy0001");
    add_extras(*dst, "beta", 40);
    dst->rebuild();

    const std::map<std::string, Bytes> files = {{"cars/day/zz0001", Bytes(32, 1)}, {"menu/cars/zz0001", Bytes(16, 2)}};
    auto read = [&](const std::string& p) -> std::optional<Bytes> {
        auto it = files.find(p);
        return it == files.end() ? std::nullopt : std::optional<Bytes>(it->second);
    };
    const std::filesystem::path mods = temp_dir("car_export");
    const std::string mod = (mods / data::export_mod_id("SCES-50858")).string();
    CHECK(data::export_mod_id("SCES-50858") == "cars_sces50858");
    std::string err;
    auto e = data::export_car(*src, read, "alpha", mod, "SCES-50858", "Test disc", &err);
    CHECK(e.has_value());
    if (!e) return;
    CHECK(e->label == "alpha_sces50858");
    CHECK(host_path_exists(mod + "/mod.ini"));
    {
        // The mod stays off on the disc it came from, and is on everywhere else.
        auto ini = IniDocument::load(mod + "/mod.ini");
        CHECK(ini.has_value());
        std::vector<std::string> discs;
        if (ini) discs.push_back(ini->get("mod", "discs").value_or(""));
        CHECK(!mod_for_disc(discs, "SCES-50858", "SCES_508.58"));
        CHECK(mod_for_disc(discs, "us", "PBPX_955.03"));
    }
    CHECK(host_path_exists(mod + "/files/cars/day/zz0001_sces50858") && host_path_exists(mod + "/files/menu/cars/zz0001_sces50858"));
    CHECK(!data::export_car(*src, read, "nothing", mod, "SCES-50858", "Test disc", &err));

    data::load_mod_car_files(*dst, {mod + "/cars/alpha_sces50858.ini"}, "cars_sces50858");
    CHECK(dst->mod_car_errors().empty());
    const data::Car* c = dst->car("alpha_sces50858");
    CHECK(c != nullptr);
    if (!c) return;
    const data::ModCar* m = dst->mod_car("alpha_sces50858");
    CHECK(m && m->exported && m->donor.empty());
    CHECK(dst->model_of(*c) == "zz0001_sces50858");            // its own model, under the exported name
    CHECK(dst->model_of(*dst->car("beta")) == "yy0001");       // the target's car untouched
    // Its steering: the source's record, under a label of its own since the target's "n" differs.
    const ByteSpan* steer = dst->record(TableId::Steer, c->parts[2]);
    CHECK(steer && (*steer)[0x11] == 30);
    CHECK(c->parts[2] != db::id_hash("n"));
    CHECK((*dst->record(TableId::Steer, "n"))[0x11] == 40);
    auto spec = sim::build_car_spec(*dst, "alpha_sces50858", &err);
    CHECK(spec.has_value());
    if (spec) CHECK(spec->peak_torque() > 0);
    // Loading it again (another database load) gives the same car.
    data::load_mod_car_files(*dst, {mod + "/cars/alpha_sces50858.ini"}, "cars_sces50858");
    CHECK(dst->mod_car_errors().empty());
}

// Every car of each Gran Turismo Concept disc, exported and loaded on the NTSC-U disc: each loads and builds a car spec.
TEST(car_export_concept_to_gt3) {
    Disc* us = test_disc();
    const std::vector<std::string> discs = concept_discs();
    if (!us || discs.empty()) SKIP("GT3R_DISC (NTSC-U) and GT3R_DISC_CONCEPT not set");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> d = Disc::open(path);
        Vfs vfs(&d->volume());
        data::CarDatabase src;
        CHECK(src.load(vfs));
        const std::string mod = (temp_dir("car_export_concept") / data::export_mod_id(vfs.disc_key())).string();
        auto read = [&](const std::string& p) { return vfs.read(p); };
        std::vector<std::string> inis;
        std::string err;
        for (const data::Car& c : src.cars()) {
            auto e = data::export_car(src, read, c.label, mod, vfs.disc_key(), "Concept", &err);
            CHECK(e.has_value());
            if (e) inis.push_back(mod + "/cars/" + e->label + ".ini");
        }
        data::CarDatabase db;
        CHECK(db.load(us->volume()));
        const size_t before = db.cars().size();
        data::load_mod_car_files(db, inis, "concept");
        for (const std::string& m : db.mod_car_errors()) std::printf("    %s\n", m.c_str());
        CHECK(db.mod_car_errors().empty());
        CHECK(db.cars().size() == before + inis.size());
        int specs = 0;
        for (const data::ModCar& m : db.mod_cars()) {
            auto spec = sim::build_car_spec(db, m.label, &err);
            if (spec) ++specs;
            else std::printf("    %s: %s\n", m.label.c_str(), err.c_str());
        }
        std::printf("  %s: %zu cars exported, %d build a car spec on NTSC-U\n", path.c_str(), inis.size(), specs);
        CHECK(specs == int(inis.size()));
        std::filesystem::remove_all(temp_dir("car_export_concept"));
    }
}
