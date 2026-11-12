// What the game's language decides in GT Mode and the licences (game/language_rules.h): prize
// car labels, the Opel / Vauxhall dealers, the licence car and its demonstration, car and paint
// names. Disc-free on the synthetic world (gtmode_synth.h, with its regional twins); disc
// checks with GT3R_DISC (any region).

#include "test_framework.h"

#include "gtmode_synth.h"

#include "disc/disc.h"
#include "game/gt_mode.h"
#include "game/language_rules.h"
#include "menu/flow/menu_controller.h"
#include "menu/flow/menu_set.h"
#include "vfs/vfs.h"

#include <cstring>
#include <map>
#include <memory>
#include <set>

using namespace gt3r;
using namespace gt3r::game;
namespace synth = gt3r::test::gtsynth;

namespace {

constexpr Language kAll[] = {Language::JP, Language::US, Language::BR, Language::FR,
                             Language::GR, Language::IT, Language::SP};

// The synthetic world with its regional twins, in `lang` (its language file).
menu::flow::FileReader world_in(Language lang) {
    auto files = std::make_shared<std::map<std::string, Bytes>>(synth::gtmode_files(true));
    const char* code = text::language_code(lang);
    (*files)[Vfs::kLanguageFile] = Bytes(code, code + std::strlen(code));
    return [files](const std::string& p) -> std::optional<Bytes> {
        auto it = files->find(p);
        if (it == files->end()) return std::nullopt;
        return it->second;
    };
}

std::shared_ptr<GtData> data_in(Language lang) {
    auto d = std::make_shared<GtData>();
    if (!d->load(world_in(lang))) return nullptr;
    return d;
}

} // namespace

// The prize-car label table: by the language's bit, first match; Japanese keeps every label.
TEST(language_prize_car_labels) {
    struct Case {
        const char* from;
        const char* us;
        const char* br;
        const char* europe; // FR, GR, IT, SP
    };
    const Case cases[] = {
        {"opel_astra_race_00", "opel_astra_race_00", "vauxhall_astra_race_00", "opel_astra_race_00"},
        {"opel_speedster_00", "opel_speedster_00", "vauxhall_vx220_00", "opel_speedster_00"},
        {"opel_calibra_race_95", "opel_calibra_race_95", "vauxhall_calibra_race_95", "opel_calibra_race_95"},
        {"viper_gtsr_oreca_00", "dodge_viper_gtsr_oreca_00", "viper_gtsr_oreca_00", "viper_gtsr_oreca_00"},
        {"s2000_99", "s2000_a_99", "s2000_e_99", "s2000_e_99"},
        {"s2000_99_ext", "s2000_a_99_ext", "s2000_e_99_ext", "s2000_e_99_ext"},
        {"mclaren_mp4_4", "mclaren_mp4_4", "mclaren_mp4_4", "mclaren_mp4_4"},
        {"s2000_a_99", "s2000_a_99", "s2000_a_99", "s2000_a_99"}, // only the table's own labels
    };
    for (const Case& c : cases)
        for (Language l : kAll) {
            const char* want = l == Language::JP ? c.from
                               : l == Language::US ? c.us
                               : l == Language::BR ? c.br
                                                   : c.europe;
            bool swapped = true;
            CHECK(prize_car_label(c.from, l, &swapped) == want);
            CHECK(swapped == (std::string(want) != c.from));
        }
    CHECK(prize_car_label("", Language::US).empty());
}

// The dealer region pages: British English leaves out "opel", the others "vauxhall".
TEST(language_dealer_items) {
    for (Language l : kAll) {
        CHECK(dealer_item_listed("opel", l) == (l != Language::BR));
        CHECK(dealer_item_listed("vauxhall", l) == (l == Language::BR));
        CHECK(dealer_item_listed("dealer4", l) && dealer_item_listed("opel_speedster_00", l));
    }
}

// The demonstration file: S-1's "_uk" run in British English only, the plain one when the disc
// lacks it.
TEST(language_demo_replays) {
    CHECK(demo_replay_name("L0S0001", Language::BR) == "L0S0001_uk");
    for (Language l : kAll)
        if (l != Language::BR) CHECK(demo_replay_name("L0S0001", l) == "L0S0001");
    CHECK(demo_replay_name("L0S0002", Language::BR) == "L0S0002");
    CHECK(demo_replay_name("L0S00011", Language::BR) == "L0S00011");
    std::set<std::string> disc = {"demo/L0S0001", "demo/L0S0001_uk", "demo/L0S0002"};
    auto exists = [&](const std::string& p) { return disc.count(p) > 0; };
    CHECK(demo_replay_path("L0S0001", Language::BR, exists) == "demo/L0S0001_uk");
    CHECK(demo_replay_path("L0S0001", Language::FR, exists) == "demo/L0S0001");
    CHECK(demo_replay_path("L0S0002", Language::BR, exists) == "demo/L0S0002");
    disc.erase("demo/L0S0001_uk");
    CHECK(demo_replay_path("L0S0001", Language::BR, exists) == "demo/L0S0001");
}

// Car names: +0xF2 / +0xF4 in Japanese, +0x100 / +0x102 in British English, else +0xE4 / +0xE6;
// the Latin-letter names are the +0xE4 ones in Japanese.
TEST(language_car_names) {
    CHECK(data::car_name_fields(Language::JP).name == 0xF2 && data::car_name_fields(Language::JP).second == 0xF4);
    CHECK(data::car_name_fields(Language::BR).name == 0x100 && data::car_name_fields(Language::BR).second == 0x102);
    for (Language l : {Language::US, Language::FR, Language::GR, Language::IT, Language::SP})
        CHECK(data::car_name_fields(l).name == 0xE4 && data::car_name_fields(l).second == 0xE6);
    struct Want {
        Language lang;
        const char *name, *alt, *latin, *latin_alt;
    };
    const Want wants[] = {
        {Language::US, "Speedster", "Turbo", "Speedster", "Turbo"},
        {Language::FR, "Speedster", "Turbo", "Speedster", "Turbo"},
        {Language::BR, "Speedster (UK)", "Turbo (UK)", "Speedster (UK)", "Turbo (UK)"},
        {Language::JP, "\xA5\xB9\xA5\xD4", "", "Speedster", "Turbo"},
    };
    for (const Want& w : wants) {
        data::CarDatabase db;
        CHECK(db.load(world_in(w.lang)));
        CHECK(db.language() == w.lang);
        const data::Car* c = db.car(synth::world::kTwinOpel);
        CHECK(c && c->name == w.name && c->name_alt == w.alt && c->latin_name == w.latin &&
              c->latin_name_alt == w.latin_alt);
        if (c) CHECK(db.summarize(*c).name == w.name);
    }
}

// The driver's side without a body to read it from (race/driver_seat.h): the side of the country
// GT Mode's dealer map puts the car's maker in, the same in every language; unknown without one.
TEST(language_driver_side) {
    using data::DriverSide;
    for (const char* c : {"jp", "uk", "aus"}) CHECK(data::country_drives_on_left(c) && data::country_driver_side(c) == DriverSide::Right);
    for (const char* c : {"us", "gr", "fr", "it", "bel"})
        CHECK(!data::country_drives_on_left(c) && data::country_driver_side(c) == DriverSide::Left);
    CHECK(data::country_driver_side("") == DriverSide::Unknown);
    for (Language l : kAll) {
        data::CarDatabase db;
        CHECK(db.load(world_in(l)));
        const data::Car* coupe = db.car(synth::world::kCoupe);
        const data::Car* vauxhall = db.car(synth::world::kTwinVauxhall);
        if (!coupe || !vauxhall) {
            CHECK(false);
            continue;
        }
        // Before the makers' countries are read: nothing to go by but the body.
        CHECK(db.maker_country(*coupe).empty() && db.driver_side(*coupe) == DriverSide::Unknown);
        CHECK(db.driver_side(*coupe, DriverSide::Left) == DriverSide::Left);
        // From the dealer map: the coupe's maker sells it on the "jp" page (every car of this
        // world has that maker). The body's own driver comes first.
        CHECK(load_maker_countries(world_in(l), db));
        CHECK(db.maker_country(*coupe) == "jp" && db.driver_side(*coupe) == DriverSide::Right);
        CHECK(db.driver_side(*vauxhall) == DriverSide::Right);
        CHECK(db.driver_side(*coupe, DriverSide::Left) == DriverSide::Left);
        CHECK(db.driver_side(*coupe, DriverSide::Right) == DriverSide::Right);
    }
    data::CarDatabase none;
    CHECK(!load_maker_countries([](const std::string&) { return std::optional<Bytes>(); }, none));
}

// Paint names: the second name (+8) in Japanese, the first (+4) in the other languages.
TEST(language_color_names) {
    CHECK(color_name_field(Language::JP) == 8);
    for (Language l : {Language::US, Language::BR, Language::FR, Language::GR, Language::IT, Language::SP})
        CHECK(color_name_field(l) == 4);
    for (Language l : {Language::JP, Language::US, Language::BR}) {
        std::shared_ptr<GtData> d = data_in(l);
        CHECK(d != nullptr);
        if (!d) continue;
        const u32 chalk = d->color_id(synth::world::kWagon, 2), night = d->color_id(synth::world::kWagon, 1);
        CHECK(d->color_name(chalk) == (l == Language::JP ? "\xA5\xDB\xA5\xEF\xA5\xA4\xA5\xC8" : "Chalk White"));
        CHECK(d->color_name_latin(chalk) == "Chalk White");
        CHECK(d->color_name(night) == "Night Blue" && d->color_name_latin(night) == "Night Blue");
    }
}

// The licence car: in British English a test whose entry-0 car is not offered there drives
// entry 1 in its slot-1 colour (S-1: the Opel -> the Vauxhall); without an entry 1 the car stays,
// in the slot-1 colour. Other languages, and tests with an offered car, keep entry 0.
TEST(language_license_car) {
    for (Language l : kAll) {
        data::CarDatabase db;
        CHECK(db.load(world_in(l)));
        const data::Event* s1 = db.event("L0S0001");
        const data::Event* s2 = db.event("L0S0002");
        const data::Event* b1 = db.event("L0B0001");
        CHECK(s1 && s2 && b1);
        if (!s1 || !s2 || !b1) continue;
        const LicenseCar c1 = license_car(db, *s1, l), c2 = license_car(db, *s2, l), cb = license_car(db, *b1, l);
        if (l == Language::BR) {
            CHECK(c1.entry == "s1_vauxhall" && c1.color == 0x102 && c1.slot == 1);
            CHECK(c2.entry == "s1_opel" && c2.color == 0x102 && c2.slot == 0);
        } else {
            CHECK(c1.entry == "s1_opel" && c1.color == 0x101 && c1.slot == 0);
            CHECK(c2.entry == "s1_opel" && c2.color == 0x101 && c2.slot == 0);
        }
        CHECK(cb.entry == "syv_lic_b" && cb.slot == 0); // the coupe: offered everywhere
    }
}

// Prize cars: the candidate's label in the language at the car_get roll (a swapped one has no
// fixed colour: a random paint); the menus' candidate list shows the same labels.
TEST(language_prize_cars_awarded) {
    for (Language l : {Language::US, Language::BR, Language::FR}) {
        std::shared_ptr<GtData> data = data_in(l);
        CHECK(data != nullptr);
        if (!data) continue;
        for (PrizeSet& p : data->prizes)
            if (p.name == "Gsyn_e_0000") p.cars = {PrizeCar{7, synth::world::kTwinOpel, 0x102, 1}};
        const PrizeCar cand = data->prize_candidate(data->prize_set("Gsyn_e_0000")->cars[0]);
        const bool br = l == Language::BR;
        CHECK(cand.car == (br ? synth::world::kTwinVauxhall : synth::world::kTwinOpel));
        CHECK(cand.color_id == (br ? -1 : 0x102) && cand.weight == 1 && cand.id == 7);
        GtMode gt(data);
        gt.record_race("Gsyn_e_0001", 0);
        Award a = gt.record_race("Gsyn_e_0002", 0); // the series won: its set
        CHECK(a.new_cars.size() == 1);
        if (a.new_cars.size() != 1) continue;
        const GarageCar& g = gt.state().garage[size_t(a.new_cars[0])];
        CHECK(g.car == cand.car && g.origin == "Gsyn_e_0000");
        if (!br) CHECK(g.color == 1); // the fixed colour 0x102 (the model's second paint)
    }
}

// The disc's own data: the prize sets' swapped labels name cars of the disc's database, S-1
// in British English drives the Vauxhall on the PAL disc, and the dealer pages list Opel or
// Vauxhall by the language.
TEST(language_rules_disc) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    const RegionData rd = vfs.region();
    for (Language l : rd.languages()) {
        vfs.set_language(l);
        GtData d;
        CHECK(d.load([&](const std::string& p) { return vfs.read(p); }));
        int swapped = 0;
        for (const PrizeSet& p : d.prizes)
            for (const PrizeCar& c : p.cars) {
                const PrizeCar x = d.prize_candidate(c);
                if (x.car == c.car) continue;
                ++swapped;
                std::printf("  %s: %s -> %s\n", p.name.c_str(), c.car.c_str(), x.car.c_str());
                CHECK(d.db.car(x.car) != nullptr && x.color_id == -1);
            }
        std::printf("  %s %s: %d prize candidates swapped\n", region_name(rd.region), text::language_code(l), swapped);
        // NTSC-U: the Viper Oreca and the S2000 _ext; PAL British English: the two Opel touring
        // cars and the S2000; the other European languages: the S2000.
        if (rd.region == Region::America) CHECK(swapped == 2);
        if (rd.region == Region::Europe) CHECK(swapped == (l == Language::BR ? 3 : 1));
        if (l == Language::JP) CHECK(swapped == 0);
        if (const data::Event* s1 = d.db.event("L0S0001")) {
            const LicenseCar c = license_car(d.db, *s1, l);
            const data::CarVariant* v = d.db.variant(c.entry);
            CHECK(v != nullptr);
            if (v && rd.region == Region::Europe)
                CHECK(v->car == (l == Language::BR ? "vauxhall_vx220_00" : "opel_speedster_00"));
            if (rd.region == Region::Europe && l == Language::BR)
                CHECK(demo_replay_path("L0S0001", l, [&](const std::string& p) { return vfs.exists(p); }) ==
                      "demo/L0S0001_uk");
        }
        // Car names: every car has one in the language.
        int unnamed = 0;
        for (const data::Car& c : d.db.cars()) unnamed += c.name.empty() || c.latin_name.empty();
        CHECK(unnamed == 0);
        // The makers' countries on the dealer map, and the side they give a car whose body shows
        // no driver: the same in every language and on every release.
        CHECK(load_maker_countries([&](const std::string& p) { return vfs.read(p); }, d.db));
        using data::DriverSide;
        const auto side = [&](const char* label, const char* country, DriverSide want) {
            if (const data::Car* c = d.db.car(label)) CHECK(d.db.maker_country(*c) == country && d.db.driver_side(*c) == want);
        };
        side("r34_gtr_v_99", "jp", DriverSide::Right);
        side("roadster_rs_00", "jp", DriverSide::Right);
        side("griffith_500_00", "uk", DriverSide::Right);
        side("falcon_xr8_race_00", "aus", DriverSide::Right);
        side("acura_nsx_93", "us", DriverSide::Left);
        side("corvette_z06_00", "us", DriverSide::Left);
        side("opel_speedster_00", "gr", DriverSide::Left);
        side("_206_s16_00", "fr", DriverSide::Left);
        side("zonda_c12_00", "it", DriverSide::Left);
        side("vertigo_97", "bel", DriverSide::Left);
        side("mini_cooper_13i_00", "", DriverSide::Unknown); // no dealer on the map
        side("mclaren_mp4_4", "", DriverSide::Unknown);      // no maker
        // The dealer region pages' buttons.
        menu::flow::MenuSet set(menu::flow::vfs_reader(vfs), "gt_mode", text::language_code(l));
        menu::flow::MenuController ctl(set);
        ctl.listed = [l](const std::string&, const menu::MblItem& it) { return dealer_item_listed(it.name, l); };
        for (const char* page : {"cd/cd_eng", "cd/cd_ger"}) {
            if (!ctl.open(page)) continue;
            std::string names;
            for (const auto& it : ctl.items()) names += it.flow->name + " ";
            std::printf("  %s: %s\n", page, names.c_str());
            for (const auto& it : ctl.items())
                CHECK(it.flow->name != (l == Language::BR ? "opel" : "vauxhall"));
        }
    }
}
