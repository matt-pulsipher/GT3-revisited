// The data of other games on the engine (Gran Turismo Concept): the files a disc's program names (disc/region.h), the
// car database's table layout told from its records (game/data/car_database.h) and the game config. The first tests
// need no disc and use INVENTED records; the concept_disc_* tests take GT3R_DISC_CONCEPT (disc images separated by ';').

#include "test_framework.h"

#include "concept_discs.h"
#include "disc/disc.h"
#include "disc/region.h"
#include "formats/db/gt_db.h"
#include "game/data/car_database.h"
#include "game/gcf.h"
#include "game/concept_game.h"
#include "game/gt_mode.h"
#include "sim/exact/race_inputs.h"
#include "sim/car_spec.h"
#include "vfs/vfs.h"

#include <cstring>
#include <deque>
#include <memory>
#include <set>
#include <sstream>

using namespace gt3r;
using data::TableId;

namespace {

Bytes program_with(std::initializer_list<const char*> strings) {
    Bytes p(64, 0xAB);
    for (const char* s : strings) {
        p.push_back(0);
        p.insert(p.end(), s, s + std::strlen(s));
        p.push_back(0);
        p.insert(p.end(), 16, 0xCD);
    }
    return p;
}

// 36 tables for table_layout: the part tables empty, the last six as `sizes` says, filled by the tests.
struct Tables {
    std::deque<Bytes> store;
    std::vector<db::Table> t{size_t(TableId::Count)};

    explicit Tables(std::initializer_list<u16> last_six) {
        size_t i = size_t(TableId::Car);
        for (u16 s : last_six) t[i++].record_size = s;
    }
    // A record of table `place`: its id, then ids at the given offsets.
    void add(size_t place, const char* label, std::initializer_list<std::pair<size_t, const char*>> refs = {}) {
        Bytes r(t[place].record_size, 0);
        auto put = [&](size_t o, u64 v) {
            for (int k = 0; k < 8; ++k) r[o + size_t(k)] = u8(v >> (8 * k));
        };
        put(0, db::id_hash(label));
        for (const auto& [o, l] : refs) put(o, db::id_hash(l));
        store.push_back(std::move(r));
        t[place].records.push_back(store.back());
    }
};

using test::concept_discs;

} // namespace

// The six tables whose places differ between databases are told apart from their records.
TEST(db_table_layout_by_records) {
    // GT3's order: car, variant, event, regulation, course, arcade car.
    {
        Tables x({264, 272, 504, 264, 16, 272});
        x.add(30, "car_a");
        x.add(31, "var_a", {{8, "car_a"}});
        x.add(35, "arc_a", {{8, "car_a"}});
        x.add(34, "course_a");
        x.add(33, "reg_a", {{8, "car_a"}});
        x.add(32, "event_a", {{8, "course_a"}, {0x10, "var_a"}, {0x18, "var_a"}, {0x20, "arc_a"}});
        const auto l = data::table_layout(x.t);
        for (size_t i = 0; i < l.size(); ++i) CHECK(l[i] == TableId(int(i)));
    }
    // The other order seen: variant, event, regulation, course, arcade car, car.
    {
        Tables x({272, 504, 264, 16, 272, 264});
        x.add(35, "car_a");
        x.add(35, "car_b");
        x.add(30, "var_a", {{8, "car_a"}});
        x.add(30, "var_b", {{8, "car_b"}});
        x.add(34, "arc_a", {{8, "car_a"}});
        x.add(33, "course_a");
        x.add(32, "reg_a", {{8, "car_a"}});
        x.add(31, "event_a", {{8, "course_a"}, {0x10, "var_a"}, {0x18, "var_b"}, {0x20, "arc_a"}});
        const auto l = data::table_layout(x.t);
        CHECK(l[30] == TableId::CarVariant && l[31] == TableId::Event && l[32] == TableId::Regulation);
        CHECK(l[33] == TableId::Course && l[34] == TableId::ArcadeCar && l[35] == TableId::Car);
        for (size_t i = 0; i < 30; ++i) CHECK(l[i] == TableId(int(i)));
    }
    // An order of neither game, with the arcade cars before the variants: still by the records.
    {
        Tables x({16, 272, 264, 272, 504, 264});
        x.add(32, "car_a");
        x.add(33, "var_a", {{8, "car_a"}});
        x.add(31, "arc_a", {{8, "car_a"}});
        x.add(30, "course_a");
        x.add(35, "reg_a");
        x.add(34, "event_a", {{8, "course_a"}, {0x10, "var_a"}, {0x18, "var_a"}});
        x.add(34, "event_b", {{8, "course_a"}, {0x10, "arc_a"}, {0x18, "var_a"}});
        const auto l = data::table_layout(x.t);
        CHECK(l[30] == TableId::Course && l[31] == TableId::ArcadeCar && l[32] == TableId::Car);
        CHECK(l[33] == TableId::CarVariant && l[34] == TableId::Event && l[35] == TableId::Regulation);
    }
    // Empty tables decide nothing: the sizes place the events and the courses, the pairs keep the file's order.
    {
        Tables x({272, 504, 264, 16, 272, 264});
        const auto l = data::table_layout(x.t);
        CHECK(l[31] == TableId::Event && l[33] == TableId::Course);
        CHECK(l[30] == TableId::CarVariant && l[34] == TableId::ArcadeCar && l[32] == TableId::Car && l[35] == TableId::Regulation);
    }
    // Other record sizes (a test's tables): GT3's places.
    {
        Tables x({0x110, 0x200, 0x200, 0x200, 0x200, 0x200});
        const auto l = data::table_layout(x.t);
        for (size_t i = 0; i < l.size(); ++i) CHECK(l[i] == TableId(int(i)));
    }
}

// The files a program names: its database and game configs, whatever they are called.
TEST(region_program_files) {
    const ProgramFiles us = files_of_program(program_with({"menu/%s/", "gameconf/product.gcf", "database/paramstr_us.db", "database/paramdb_us.db"}));
    CHECK(us.db_suffix && *us.db_suffix == "_us" && us.gcf.size() == 1 && us.game() == GameKind::Gt3);
    CHECK(us.gcf_for(Language::US) == "gameconf/product.gcf");
    // GT3 PAL names three game configs and takes the language's.
    const ProgramFiles pal = files_of_program(program_with({"gameconf/product.gcf", "gameconf/product_us.gcf", "gameconf/product_eu.gcf", "database/paramdb_eu.db"}));
    CHECK(pal.gcf.size() == 3 && pal.gcf_for(Language::BR) == "gameconf/product_eu.gcf" && pal.gcf_for(Language::SP) == "gameconf/product_eu.gcf");
    CHECK(pal.gcf_for(Language::JP) == "gameconf/product.gcf" && pal.gcf_for(Language::US) == "gameconf/product_us.gcf");
    // The GT3 programs' files are their region's: nothing to add to the region.
    for (const auto& [files, region] : {std::pair{us, Region::America}, std::pair{pal, Region::Europe}}) {
        const RegionData rd = RegionData::of_program(region, files);
        CHECK(rd.game == GameKind::Gt3 && !rd.program_db_suffix && !rd.program_gcf);
        CHECK(rd.to_text() == region_key(region));
    }
    // Another game: a database and game configs of its own names.
    const ProgramFiles other = files_of_program(
        program_with({"gameconf/concept.gcf", "gameconf/concept_us.gcf", "gameconf/concept_eu.gcf", "database/paramstr_zz.db", "database/paramdb_zz.db"}));
    CHECK(other.db_suffix && *other.db_suffix == "_zz" && other.game() == GameKind::Concept);
    const RegionData jp = RegionData::of_program(Region::Japan, other), eu = RegionData::of_program(Region::Europe, other);
    CHECK(jp.game == GameKind::Concept && jp.db_suffix() == "_zz" && jp.gcf_path() == "gameconf/concept.gcf");
    CHECK(eu.db_suffix() == "_zz" && eu.gcf_path() == "gameconf/concept_eu.gcf");
    // A single game config is the one, whatever the language.
    const ProgramFiles one = files_of_program(program_with({"gameconf/concept_kr.gcf", "database/paramdb_kr.db"}));
    CHECK(one.gcf_for(Language::JP) == "gameconf/concept_kr.gcf" && one.game() == GameKind::Concept);
    // Not names: a path inside another string, a format, a program with neither.
    const ProgramFiles none = files_of_program(program_with({"old/database/paramdb_us.db", "gameconf/%s.gcf", "database/paramdb"}));
    CHECK(!none.db_suffix && none.gcf.empty() && none.game() == GameKind::Gt3 && none.gcf_for(Language::US).empty());
    const RegionData plain = RegionData::of_program(Region::America, none);
    CHECK(plain.db_suffix() == "_us" && plain.gcf_path() == "gameconf/product.gcf");

    // The region file carries the program's files to loaders that only get a reader.
    const std::optional<RegionData> back = RegionData::from_text(eu.to_text());
    CHECK(back && back->region == Region::Europe && back->game == GameKind::Concept && back->db_suffix() == "_zz" &&
          back->gcf_path() == "gameconf/concept_eu.gcf");
    CHECK(!RegionData::from_text("xx") && !RegionData::from_text(""));
    const std::string text = jp.to_text();
    CHECK(region_data([&](const std::string& p) -> std::optional<Bytes> {
              if (p != Vfs::kRegionFile) return std::nullopt;
              return Bytes(text.begin(), text.end());
          }).db_suffix() == "_zz");

    // Course folders: GT3's NTSC-J program has none; every other program goes by the language.
    CHECK(RegionData{Region::Japan}.course_path("x", Language::JP) == "courses/x");
    CHECK(jp.course_path("x", Language::JP) == "courses/JP/x" && eu.course_path("x", Language::FR) == "courses/EU/x");
}

// A database in the other table order, with the longer gear record, through CarDatabase: tables are reached by what
// they are, and a gear's price stays where GT3 has it.
TEST(db_other_table_order_loads) {
    auto put16 = [](Bytes& b, size_t o, u16 v) { b[o] = u8(v), b[o + 1] = u8(v >> 8); };
    auto put32 = [&](Bytes& b, size_t o, u32 v) { put16(b, o, u16(v)), put16(b, o + 2, u16(v >> 16)); };
    auto put64 = [&](Bytes& b, size_t o, u64 v) { put32(b, o, u32(v)), put32(b, o + 4, u32(v >> 32)); };
    const u16 last_six[] = {272, 504, 264, 16, 272, 264};
    std::vector<Bytes> tables;
    for (u32 i = 0; i < u32(TableId::Count); ++i) {
        Bytes t(16);
        std::memcpy(t.data(), "GTDT", 4);
        put16(t, 4, 0x1234); // an invented version: nothing reads it
        put16(t, 6, u16(i));
        put16(t, 10, i >= 30 ? last_six[i - 30] : i == u32(TableId::Gear) ? 56 : 0x200);
        tables.push_back(std::move(t));
    }
    auto add = [&](size_t place, Bytes r) {
        Bytes& t = tables[place];
        put16(t, 8, u16(rd16(t, 8) + 1));
        t.insert(t.end(), r.begin(), r.end());
        put32(t, 12, u32(t.size()));
    };
    Bytes car(264), var(272), ev(504), course(16), gear(56);
    put64(car, 0, db::id_hash("syn_car"));
    put64(var, 0, db::id_hash("syn_var")), put64(var, 8, db::id_hash("syn_car"));
    put64(course, 0, db::id_hash("syn_course"));
    put64(ev, 0, db::id_hash("syn_event")), put64(ev, 8, db::id_hash("syn_course"));
    for (size_t k = 0; k < 32; ++k) put64(ev, 16 + 8 * k, k < 2 ? db::id_hash("syn_var") : db::id_hash("-"));
    put64(ev, 0x190, db::id_hash("-"));
    put64(gear, 0, db::id_hash("syn_gear")), put64(gear, 8, db::id_hash("syn_car"));
    gear[0x11] = 5;
    put32(gear, 0x2C, 4321);
    gear[0x30] = 2;
    add(35, car), add(30, var), add(31, ev), add(33, course), add(size_t(TableId::Gear), gear);
    Bytes paramdb(16 + 4 * (tables.size() + 1));
    std::memcpy(paramdb.data(), "GTAR", 4);
    put32(paramdb, 4, u32(tables.size())), put32(paramdb, 8, u32(paramdb.size())), put32(paramdb, 12, 7);
    u32 at = 0;
    for (size_t i = 0; i < tables.size(); ++i) {
        put32(paramdb, 16 + 4 * i, at);
        at += u32((tables[i].size() + 15) / 16 * 16);
    }
    put32(paramdb, 16 + 4 * tables.size(), at);
    for (Bytes& t : tables) {
        t.resize((t.size() + 15) / 16 * 16);
        paramdb.insert(paramdb.end(), t.begin(), t.end());
    }
    Bytes stdb(16);
    std::memcpy(stdb.data(), "STDB", 4);
    put32(stdb, 12, 16);

    data::CarDatabase db;
    const bool ok = db.load([&](const std::string& p) -> std::optional<Bytes> {
        if (p == "database/paramdb_zz.db") return paramdb;
        if (p == "database/paramstr_zz.db" || p == "database/paramunistr_zz.db") return stdb;
        return std::nullopt;
    }, std::string("_zz"));
    if (!ok) std::printf("  %s\n", db.error().c_str());
    CHECK(ok);
    if (!ok) return;
    CHECK(db.place_of(TableId::Car) == 35 && db.place_of(TableId::CarVariant) == 30 && db.place_of(TableId::Event) == 31);
    CHECK(db.place_of(TableId::Gear) == size_t(TableId::Gear));
    CHECK(db.cars().size() == 1 && db.variants().size() == 1 && db.events().size() == 1 && db.courses().size() == 1);
    CHECK(db.arcade_cars().empty() && db.regulations().empty());
    CHECK(db.record(TableId::Car, "syn_car") && db.record(TableId::Course, "syn_course") && !db.record(TableId::Event, "syn_car"));
    CHECK(db.events()[0].entries.size() == 2 && db.table(TableId::Car).record_size == 264);
    const auto g = db.gearbox(db::id_hash("syn_gear"));
    CHECK(g && g->gears == 5 && g->part.price == 4321);
    const auto p = db.part(TableId::Gear, db::id_hash("syn_gear"));
    CHECK(p && p->price == 4321);
}

// Each Gran Turismo Concept disc (GT3R_DISC_CONCEPT): the program names its own database and game config, the
// database loads with its tables told apart, every event's course and cars are in it, every car builds a race
// specification, the game config parses with the game's sums, and every arcade course has a file.
TEST(concept_disc_data) {
    const std::vector<std::string> discs = concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::printf("  %s\n", path.c_str());
        std::unique_ptr<Disc> disc;
        try {
            disc = Disc::open(path);
        } catch (const std::exception& e) {
            std::printf("    %s\n", e.what());
        }
        CHECK(disc != nullptr);
        if (!disc) continue;
        const RegionData& rd = disc->info().data;
        std::printf("    %s, %s: database/paramdb%s.db, %s\n", disc->info().serial.c_str(), game_name(rd.game), rd.db_suffix().c_str(),
                    rd.gcf_path().c_str());
        CHECK(rd.game == GameKind::Concept && rd.program_db_suffix && rd.program_gcf);
        Volume& vol = disc->volume();
        // The files it names are on the disc, with the string tables and the id index of the same name.
        for (const char* stem : {"database/paramdb", "database/paramstr", "database/paramunistr", "database/.id_db_idx", "database/.id_db_str"})
            CHECK(vol.find(stem + rd.db_suffix() + ".db") != nullptr);
        CHECK(vol.find(rd.gcf_path()) != nullptr);

        Vfs vfs(&vol);
        CHECK(vfs.region().db_suffix() == rd.db_suffix() && region_data([&](const std::string& p) { return vfs.read(p); }).gcf_path() == rd.gcf_path());
        data::CarDatabase db;
        const bool ok = db.load(vfs);
        if (!ok) std::printf("    database: %s\n", db.error().c_str());
        CHECK(ok);
        if (!ok) continue;
        std::printf("    %zu cars, %zu variants, %zu arcade cars, %zu events, %zu courses, %zu regulations; places: car %zu, event %zu\n",
                    db.cars().size(), db.variants().size(), db.arcade_cars().size(), db.events().size(), db.courses().size(),
                    db.regulations().size(), db.place_of(TableId::Car), db.place_of(TableId::Event));
        CHECK(db.cars().size() > 100 && db.variants().size() > 100 && db.arcade_cars().size() > 100 && db.events().size() > 100);
        CHECK(db.courses().size() > 100 && !db.regulations().empty());
        CHECK(db.table(TableId::Car).record_size == data::kCarRecordSize && db.table(TableId::Event).record_size == data::kEventRecordSize);

        // Every label resolves, every car has a name and a body, every variant a base car.
        int unnamed = 0, no_body = 0, no_base = 0;
        for (const data::Car& c : db.cars()) {
            unnamed += c.label.empty() || c.name.empty();
            no_body += db.model_of(c).empty();
        }
        for (const std::vector<data::CarVariant>* list : {&db.variants(), &db.arcade_cars()})
            for (const data::CarVariant& v : *list) no_base += v.label.empty() || !db.car(v.car);
        CHECK(unnamed == 0 && no_body == 0 && no_base == 0);

        // Every event: its course is in the course table, its entries are variants or arcade cars.
        std::set<std::string> courses;
        for (const data::Course& c : db.courses()) courses.insert(c.label);
        int no_course = 0, bad_entries = 0, entries = 0;
        std::string first_bad;
        for (const data::Event& e : db.events()) {
            if (!courses.count(e.course)) {
                ++no_course;
                if (first_bad.empty()) first_bad = e.label + " (course '" + e.course + "')";
            }
            for (const std::string& x : e.entries) {
                ++entries;
                const bool found = db.variant(x) || db.record(TableId::ArcadeCar, x);
                bad_entries += !found;
                if (!found && first_bad.empty()) first_bad = e.label + " (entry '" + x + "')";
            }
        }
        std::printf("    events: %d entries; %d without a course in the table, %d entries not found%s%s\n", entries, no_course, bad_entries,
                    first_bad.empty() ? "" : "; first: ", first_bad.c_str());
        // The 2002 Tokyo-Geneva databases hold events with an entry that is in neither table (under 2% of the entries)
        // and one event whose course is not in the course table; the 2001 Tokyo and Tokyo-Seoul ones hold none.
        CHECK(entries > 1000 && bad_entries * 50 < entries && no_course <= 1);

        // Every car builds the specification a race is set up from; so does every variant and arcade car.
        int spec_fail = 0;
        std::string first_err;
        auto build = [&](const std::string& label) {
            std::string err;
            if (sim::build_car_spec(db, label, &err)) return;
            ++spec_fail;
            if (first_err.empty()) first_err = label + ": " + err;
        };
        for (const data::Car& c : db.cars()) build(c.label);
        for (const data::CarVariant& v : db.variants()) build(v.label);
        for (const data::CarVariant& v : db.arcade_cars()) build(v.label);
        if (spec_fail) std::printf("    %d specifications fail; first: %s\n", spec_fail, first_err.c_str());
        CHECK(spec_fail == 0);

        // A longer gear record keeps GT3's fields: sensible ratios and prices on every gearbox.
        int gears_bad = 0;
        for (ByteSpan r : db.table(TableId::Gear).records) {
            const auto g = db.gearbox(rd64(r, 0));
            gears_bad += !g || g->gears < 1 || g->gears > 7 || g->final_drive <= 0 || g->part.price > 10000000;
        }
        CHECK(gears_bad == 0);

        // The game config: the same sections as GT3's, with the game's four sums.
        const std::optional<Bytes> raw = vfs.read(rd.gcf_path());
        CHECK(raw.has_value());
        if (!raw) continue;
        bool parsed = false;
        try {
            const game::Gcf gcf = game::Gcf::parse(*raw);
            parsed = true;
            for (const char* sum : {"initial", "car_wash", "oil_change", "wheel"}) CHECK(gcf.value(sum).has_value());
            size_t counts[16] = {};
            for (u32 tag : {4u, 6u, 8u, 12u, 13u, 14u, 15u})
                if (const game::Gcf::Section* s = gcf.section(tag)) counts[tag] = s->entries.size();
            std::printf("    game config: %zu race codes, %zu courses, %zu arcade modes, %zu demos, %zu tests, %zu arcade classes, %zu prize keys\n",
                        counts[4], counts[6], counts[8], counts[12], counts[13], counts[14], counts[15]);
            for (u32 tag : {4u, 6u, 8u, 12u, 13u, 14u, 15u}) CHECK(counts[tag] > 0);
        } catch (const std::exception& e) {
            std::printf("    game config: %s\n", e.what());
        }
        CHECK(parsed);

        // The arcade lists (GtData, which also wants GT Mode's menus: those may be missing, the lists not).
        game::GtData gd;
        const bool gt = gd.load([&](const std::string& p) { return vfs.read(p); });
        std::printf("    GT Mode data: %s\n", gt ? "loads" : gd.error().c_str());
        if (gt) {
            // A class lists arcade cars (CLASS_PREVIEW: cars). A class with no car in the database is another
            // release's, left in the file (the Tokyo-Geneva configs keep a GT3 class): reported, not counted.
            int cars = 0, missing_cars = 0, arcade_courses = 0, missing_files = 0, prizes_unresolved = 0, by_place = 0, no_event = 0;
            for (const auto& [cls, list] : gd.arcade_classes) {
                int found = 0;
                for (const game::GtData::ArcadeCar& c : list) found += db.record(TableId::ArcadeCar, c.label) || db.car(c.label);
                if (!found && !list.empty()) {
                    std::printf("    arcade class %s: none of its %zu cars is in the database\n", cls.c_str(), list.size());
                    continue;
                }
                cars += int(list.size()), missing_cars += int(list.size()) - found;
                // An arcade car's race is an event of the database.
                for (const game::GtData::ArcadeCar& c : list) no_event += !c.race_code.empty() && !db.event(c.race_code);
            }
            for (const auto& [mode, list] : gd.arcade_courses)
                for (const game::GtData::ArcadeCourse& c : list) ++arcade_courses, missing_files += course_file_path(vfs, c.course).empty();
            for (const game::PrizeSet& p : gd.prizes)
                for (const game::PrizeCar& c : p.cars) {
                    prizes_unresolved += !db.car(c.car) && !db.record(TableId::ArcadeCar, c.car);
                    by_place += c.arcade_index >= 0;
                }
            std::printf("    arcade: %d cars (%d not in the database, %d races not events), %d courses (%d without a file); %zu prize keys, "
                        "%d by class and place, %d not found\n",
                        cars, missing_cars, no_event, arcade_courses, missing_files, gd.prizes.size(), by_place, prizes_unresolved);
            CHECK(cars > 50 && missing_cars == 0 && no_event == 0 && arcade_courses > 20 && missing_files == 0);
            CHECK(!gd.prizes.empty() && prizes_unresolved == 0 && by_place > 0);
        }
        // Every course of the game config has a file in a folder the disc has.
        int course_files = 0;
        for (const data::Course& c : db.courses()) course_files += !course_file_path(vfs, c.label).empty();
        std::printf("    course files: %d of %zu courses in the database\n", course_files, db.courses().size());
        CHECK(course_files >= 10);
    }
}

// A Concept program that names a game config for each language loads the one of its release's language, the language
// of the database it names (Tokyo-Seoul v1.02: paramdb_kr.db and six configs, as v2.00 names concept_kr.gcf alone);
// 2001 Tokyo, whose database has no config of its name, loads the plain one.
TEST(concept_program_config_by_database) {
    ProgramFiles seoul;
    seoul.db_suffix = "_kr";
    seoul.gcf = {"gameconf/concept.gcf", "gameconf/concept_us.gcf", "gameconf/concept_kr.gcf", "gameconf/concept_cn.gcf",
                 "gameconf/concept_tw.gcf", "gameconf/concept_eu.gcf"};
    const RegionData a = RegionData::of_program(Region::Japan, seoul);
    CHECK(a.game == GameKind::Concept && a.gcf_path() == "gameconf/concept_kr.gcf" && a.default_language() == Language::KR);
    ProgramFiles tokyo;
    tokyo.db_suffix = "_bs";
    tokyo.gcf = {"gameconf/concept.gcf", "gameconf/concept_eu.gcf"};
    const RegionData b = RegionData::of_program(Region::Japan, tokyo);
    CHECK(b.gcf_path() == "gameconf/concept.gcf" && b.default_language() == Language::JP);
    ProgramFiles later;
    later.db_suffix = "_kr";
    later.gcf = {"gameconf/concept_kr.gcf"};
    CHECK(RegionData::of_program(Region::Japan, later).gcf_path() == a.gcf_path());
    // The two Tokyo-Seoul programs are told apart by it (v1.02's attract and new-game options are the 2001 program's), and
    // the region file keeps it.
    CHECK(a.gcf_per_language && !b.gcf_per_language && !RegionData::of_program(Region::Japan, later).gcf_per_language);
    const std::optional<RegionData> back = RegionData::from_text(a.to_text());
    CHECK(back && back->gcf_per_language && back->gcf_path() == a.gcf_path());
    CHECK(!RegionData::from_text(RegionData::of_program(Region::Japan, later).to_text())->gcf_per_language);
    // GT3's PAL program (two configs, the "_eu" database) is not touched by the rule.
    ProgramFiles pal;
    pal.db_suffix = "_eu";
    pal.gcf = {"gameconf/product.gcf", "gameconf/product_eu.gcf"};
    CHECK(RegionData::of_program(Region::Europe, pal).gcf_path() == RegionData(Region::Europe).gcf_path());
}

TEST(concept_disc_cpu_driver) {
    // Our online CPU cars on a Concept disc (game::concept_cpu_driver): at each single-race level a car takes the driver
    // overrides of its own class entry's race at that level, found by its arcade car or its database car; its entry of the
    // course's kind, else its other one; a car without a class entry has none.
    const std::vector<std::string> discs = concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        Vfs vfs(&disc->volume());
        game::GtData gd;
        CHECK(gd.load([&](const std::string& p) { return vfs.read(p); }));
        int entries = 0, by_car = 0, differ = 0, dirt = 0;
        for (const char* cls : {"CLASS_C", "CLASS_B", "CLASS_A", "CLASS_S", "CLASS_DIRT"}) {
            const auto it = gd.arcade_classes.find(cls);
            if (it == gd.arcade_classes.end()) continue;
            for (const game::GtData::ArcadeCar& c : it->second) {
                if (c.race_code.empty()) continue;
                std::array<std::optional<std::array<u8, 16>>, 3> got;
                for (int level = 0; level < game::kConceptLevels; ++level) {
                    const data::Event* ev = gd.db.event(game::concept_race_code(c.race_code, level));
                    CHECK(ev != nullptr);
                    if (!ev) continue;
                    const bool on_dirt = game::concept_dirt_entry(c.label);
                    got[size_t(level)] = game::concept_cpu_driver(gd, c.label, "", level, on_dirt);
                    CHECK(got[size_t(level)] == sim::exact::driver_overrides(sim::exact::race_config(ev->bytes)));
                }
                ++entries, dirt += game::concept_dirt_entry(c.label);
                differ += got[0] != got[2];
                // By its database car alone: some entry of that car (the same car can have several entries).
                std::string base;
                for (const data::CarVariant& v : gd.db.arcade_cars())
                    if (v.label == c.label) base = v.car;
                if (!base.empty()) by_car += game::concept_cpu_driver(gd, "", base, 1, false).has_value();
            }
        }
        std::printf("  %s: %d class entries (%d dirt), %d found by their car, %d with Ace unlike Normal\n",
                    disc->info().serial.c_str(), entries, dirt, by_car, differ);
        CHECK(entries > 0 && differ > 0 && by_car == entries);
        CHECK(!game::concept_cpu_driver(gd, "", "no_such_car", 0, false).has_value());
        // A car no class lists whose races the disc holds under Tokyo-Geneva's names (Tokyo-Geneva keeps the 106 S16's).
        if (gd.db.event("A__106_s16_01_0001")) {
            CHECK(game::concept_car_race(gd, "", "_106_s16_01", 0, false) == "A__106_s16_01_0001");
            CHECK(game::concept_car_race(gd, "gtcmr__106_s16_01", "", 2, false) == "A__106_s16_01_0003");
        }
    }
}
