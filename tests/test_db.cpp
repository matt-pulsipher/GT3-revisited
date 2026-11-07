// Database container formats and the typed car database.
// Synthetic tests run everywhere; disc tests need $GT3R_DISC.

#include "test_framework.h"

#include "formats/db/gt_db.h"
#include "game/data/car_database.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace gt3r;
using namespace gt3r::test;

namespace {

void put16(Bytes& b, size_t o, u16 v) { std::memcpy(b.data() + o, &v, 2); }
void put32(Bytes& b, size_t o, u32 v) { std::memcpy(b.data() + o, &v, 4); }
void put64(Bytes& b, size_t o, u64 v) { std::memcpy(b.data() + o, &v, 8); }

} // namespace

TEST(db_id_hash) {
    // Short labels pack 7 bits per character under the byte sum.
    CHECK(db::id_hash("a") == 0x30E1);
    CHECK(db::id_hash("n") == 0x376E);
    CHECK(db::id_hash("-") == 0x16AD);
    CHECK(db::id_hash("f1") == 0x25F331);
    CHECK(db::id_hash("sz001") == 0x00000BF73F4C1831ull);
    // Long labels wrap around (rotate, not shift).
    CHECK(db::id_hash("r34_gtr_v_99") == 0x5FCFD39641962993ull);
    CHECK(db::id_hash("tk_r34_gtr_v_99_a") == 0x09DA57358A6507D8ull);
    // The seed sum is truncated to 16 bits (the original's behaviour; docs/formats/database.md "Id codes").
    CHECK(db::id_hash(std::string(600, '\xFF')) == 0xC1D8B00E1C3870DFull);
}

TEST(db_containers_synthetic) {
    // STDB with two strings.
    Bytes s(32, 0);
    std::memcpy(s.data(), "STDB", 4);
    put32(s, 4, 2);
    put32(s, 8, 1);
    put32(s, 12, 32);
    put32(s, 16, 24);
    put32(s, 20, 28);
    put16(s, 24, 1);
    s[26] = 'x';
    put16(s, 28, 2);
    s[30] = 'y';
    s[31] = 'z';
    db::StringTable st = db::StringTable::parse(s);
    CHECK(st.size() == 2);
    CHECK(st.at(0) == "x");
    CHECK(st.at(1) == "yz");
    CHECK(st.at(9).empty());

    // GTDT with two 16-byte records inside a GTAR.
    Bytes t(16 + 32, 0);
    std::memcpy(t.data(), "GTDT", 4);
    put16(t, 4, 0x0827);
    put16(t, 6, 0);
    put16(t, 8, 2);
    put16(t, 10, 16);
    put32(t, 12, u32(t.size()));
    put64(t, 16, db::id_hash("a"));
    put64(t, 32, db::id_hash("n"));
    Bytes ar(0x18, 0);
    std::memcpy(ar.data(), "GTAR", 4);
    put32(ar, 4, 1);
    put32(ar, 8, 0x18);
    put32(ar, 12, 7);
    put32(ar, 16, 0);
    put32(ar, 20, u32(t.size()));
    ar.insert(ar.end(), t.begin(), t.end());
    auto tabs = db::parse_archive(ar);
    CHECK(tabs.size() == 1);
    db::Table tab = db::Table::parse(tabs[0]);
    CHECK(tab.size() == 2 && tab.record_size == 16 && tab.version == 0x0827);
    CHECK(tab.id_of(1) == db::id_hash("n"));
    // As GT3 reads an archive: by its offsets alone, another tag taken (a modified game's "GTAr").
    Bytes other = ar;
    std::memcpy(other.data(), "GTAr", 4);
    auto other_tabs = db::parse_archive(other);
    CHECK(other_tabs.size() == 1 && other_tabs[0].data() == other.data() + 0x18 && other_tabs[0].size() == t.size());

    bool threw = false;
    try {
        db::Table::parse(ByteSpan(t).first(20));
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(db_disc_tables) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Volume& vol = disc->volume();
    // The id index resolves every one of its own strings through id_hash.
    for (const char* sfx : {"", "_us", "_eu"}) {
        auto idx = vol.read(*vol.find(std::string("database/.id_db_idx") + sfx + ".db"));
        auto str = vol.read(*vol.find(std::string("database/.id_db_str") + sfx + ".db"));
        db::IdIndex ix = db::IdIndex::parse(idx);
        db::StringTable st = db::StringTable::parse(str);
        CHECK(ix.size() == st.size());
        size_t ok = 0;
        for (size_t i = 0; i < st.size(); ++i) {
            auto j = ix.find(db::id_hash(st.at(i)));
            ok += j && *j == i;
        }
        CHECK(ok == st.size());
    }
    // All three regional paramdbs parse into 36 tables of whole records.
    for (const char* sfx : {"", "_us", "_eu"}) {
        Bytes b = vol.read(*vol.find(std::string("database/paramdb") + sfx + ".db"));
        auto tabs = db::parse_archive(b);
        CHECK(tabs.size() == 36);
        for (size_t i = 0; i < tabs.size(); ++i) {
            db::Table t = db::Table::parse(tabs[i]);
            CHECK(t.index == i);
            CHECK(16 + t.size() * t.record_size == tabs[i].size());
        }
    }
    for (const char* f : {"database/racemode.db", "database/racedetail.db"}) {
        db::Table t = db::Table::parse(vol.read(*vol.find(f)));
        CHECK(t.index == 0xFFFF && t.size() > 0);
    }
}

TEST(db_disc_cars) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    data::CarDatabase d;
    CHECK(d.load(disc->volume()));
    if (!d.error().empty()) return;
    CHECK(d.cars().size() == 244);
    CHECK(d.events().size() == 454);
    CHECK(d.courses().size() == 136);
    CHECK(d.variants().size() == 725);

    // Every car resolves its engine, chassis, body, drivetrain and gearbox.
    size_t complete = 0;
    for (const data::Car& c : d.cars())
        complete += d.engine(c.parts[6]) && d.chassis(c.parts[3]) && d.body(c.parts[5]) &&
                    d.drivetrain(c.parts[13]) && d.gearbox(c.parts[18]);
    CHECK(complete == d.cars().size());

    // Skyline GT-R V-spec (R34): well-known catalogue figures.
    const data::Car* c = d.car("r34_gtr_v_99");
    CHECK(c != nullptr);
    if (!c) return;
    auto s = d.summarize(*c);
    CHECK(s.model == "ni0137");
    CHECK(s.year == 1999);
    CHECK(s.power_ps == 280);
    CHECK(std::fabs(s.torque_kgfm - 40.0f) < 0.01f);
    CHECK(s.displacement == "2568cc");
    CHECK(s.weight_kg == 1560);
    CHECK(s.wheelbase_mm == 2665);
    CHECK(s.drive == "4WD");
    CHECK(s.gears == 6);
    CHECK(std::fabs(s.final_drive - 3.545f) < 0.001f);
    CHECK(s.ratios.size() == 6 && std::fabs(s.ratios[0] - 3.827f) < 0.001f);
    auto e = d.engine(c->parts[6]);
    CHECK(e && e->rpm.size() == 15 && e->rpm.front() == 1500 && e->rpm.back() == 8500);
    // The curve integrates back to the catalogue power within a few percent.
    CHECK(e && std::fabs(e->curve_peak_ps() - 280) < 15);
    // Engine sound ids name existing banks for every engine record.
    size_t sounds = 0;
    for (ByteSpan r : d.table(data::TableId::Engine).records) {
        char path[32];
        std::snprintf(path, sizeof(path), "sound/engine/%05u", unsigned(d.engine(rd64(r, 0))->sound_id));
        sounds += disc->volume().find(path) != nullptr;
    }
    CHECK(sounds == d.table(data::TableId::Engine).size());
    CHECK(d.car_by_model("ho0030") && d.car_by_model("ho0030")->label == "civic_r_98");
    CHECK(d.parts_for(data::TableId::TurbineKit, "r34_gtr_v_99").size() == 5);

    const data::Event* ev = d.event("Gpolyphony_h_1002");
    CHECK(ev && ev->course == "r_midfield" && ev->race_mode == "RACE_MODE_SINGLE" && !ev->entries.empty());
}
