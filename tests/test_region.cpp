// Regional releases (disc/region.h): telling them apart and the files each one reads. Only
// region_disc_data_set and region_us_medal_times need a disc (any region's).

#include "test_framework.h"

#include "disc/disc.h"
#include "disc/region.h"
#include "formats/db/gt_db.h"
#include "game/data/car_database.h"
#include "game/gt_mode.h"
#include "vfs/vfs.h"

#include <cstring>
#include <memory>

using namespace gt3r;

namespace {

// A program image holding `strings` as NUL-terminated strings among other data.
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

} // namespace

TEST(region_of_program_by_its_database) {
    CHECK(region_of_program(program_with({"menu/%s/", "database/paramdb_us.db"})) == Region::America);
    CHECK(region_of_program(program_with({"database/paramdb_eu.db", "gameconf/product_eu.gcf"})) == Region::Europe);
    CHECK(region_of_program(program_with({"courses/", "database/paramdb.db"})) == Region::Japan);
    CHECK(!region_of_program(program_with({"database/paramdb_xx.db"})));
    // Only whole strings count: a name inside a longer one is not the program's file name.
    CHECK(!region_of_program(program_with({"old/database/paramdb_us.db"})));
}

TEST(region_of_serial_by_prefix) {
    CHECK(region_of_serial("SCES_502.94") == Region::Europe);
    CHECK(region_of_serial("SCPS_150.09") == Region::Japan);
    CHECK(region_of_serial("SCUS_971.02") == Region::America);
    CHECK(region_of_serial("PBPX_955.03") == Region::America);
    CHECK(!region_of_serial("PBPX_955.01"));
    CHECK(!region_of_serial(""));
}

TEST(region_data_files) {
    const RegionData jp{Region::Japan}, us{Region::America}, eu{Region::Europe};
    CHECK(std::string(jp.db_suffix()).empty() && std::string(us.db_suffix()) == "_us" &&
          std::string(eu.db_suffix()) == "_eu");
    CHECK(std::string(jp.gcf_path()) == "gameconf/product.gcf" && std::string(us.gcf_path()) == "gameconf/product.gcf" &&
          std::string(eu.gcf_path()) == "gameconf/product_eu.gcf");
    CHECK(jp.languages() == std::vector<Language>{Language::JP} && us.languages() == std::vector<Language>{Language::US});
    CHECK(eu.languages() ==
          (std::vector<Language>{Language::BR, Language::FR, Language::GR, Language::IT, Language::SP}));
    CHECK(jp.default_language() == Language::JP && us.default_language() == Language::US &&
          eu.default_language() == Language::BR);
    for (Region r : {Region::Japan, Region::America, Region::Europe}) CHECK(region_from_key(region_key(r)) == r);
    CHECK(!region_from_key("uk"));
}

// Each release runs in its own languages only, though its disc has every language's menu
// folders (issue 63: a US disc kept in Spanish or Japanese by the config).
TEST(region_playable_languages) {
    const RegionData jp{Region::Japan}, us{Region::America}, eu{Region::Europe};
    auto all = [](Language) { return true; };
    CHECK(playable_languages(us, all) == std::vector<Language>{Language::US});
    CHECK(playable_languages(jp, all) == std::vector<Language>{Language::JP});
    CHECK(playable_languages(eu, all) == eu.languages());
    // A disc with none of its release's menus (another game on this engine): all it has, US first.
    auto jp_br = [](Language l) { return l == Language::JP || l == Language::BR; };
    CHECK(playable_languages(us, jp_br) == (std::vector<Language>{Language::JP, Language::BR}));
    CHECK(playable_languages(eu, jp_br) == std::vector<Language>{Language::BR});
    CHECK(playable_languages(us, [](Language) { return false; }).empty());

    // The config's choice only when the disc runs in it, else the release's own.
    const std::vector<Language> us_only = playable_languages(us, all), eu_all = playable_languages(eu, all);
    CHECK(choose_language(us, us_only, Language::SP) == Language::US);
    CHECK(choose_language(us, us_only, Language::JP) == Language::US);
    CHECK(choose_language(us, us_only, std::nullopt) == Language::US);
    CHECK(choose_language(eu, eu_all, Language::IT) == Language::IT);
    CHECK(choose_language(eu, eu_all, Language::US) == Language::BR);
    CHECK(choose_language(eu, eu_all, std::nullopt) == Language::BR);
    CHECK(choose_language(us, {Language::JP, Language::BR}, std::nullopt) == Language::JP);
    CHECK(choose_language(us, {}, Language::SP) == Language::US);
}

// The course folder follows the language (the NTSC-J disc has one folder for all).
TEST(region_course_folder_by_language) {
    const RegionData jp{Region::Japan}, us{Region::America}, eu{Region::Europe};
    CHECK(jp.course_path("super", Language::JP) == "courses/super");
    CHECK(us.course_path("super", Language::US) == "courses/US/super");
    CHECK(eu.course_path("super", Language::BR) == "courses/UK/super");
    for (Language l : {Language::FR, Language::GR, Language::IT, Language::SP})
        CHECK(eu.course_path("super", l) == "courses/EU/super");
    CHECK(us.course_path("super", Language::JP) == "courses/JP/super");
}

TEST(language_units_money) {
    const Units us = language_units(Language::US), br = language_units(Language::BR);
    CHECK(us.mph && us.power_hp && us.torque_ftlb && std::string(us.power) == "HP");
    CHECK(!br.mph && br.power_hp && !br.torque_ftlb && std::string(br.power) == "BHP");
    CHECK(std::string(language_units(Language::FR).power) == "CV" && std::string(language_units(Language::SP).power) == "BHP");
    for (Language l : {Language::JP, Language::GR}) {
        const Units u = language_units(l);
        CHECK(!u.mph && !u.power_hp && !u.torque_ftlb && std::string(u.power) == "PS");
    }
    CHECK(money_scale(Language::JP) == 100 && money_scale(Language::US) == 1 && money_scale(Language::BR) == 1);
    CHECK(!is_european(Language::JP) && !is_european(Language::US) && is_european(Language::BR) &&
          is_european(Language::SP));
}

// The player's speed unit (Settings > Display > Speed units) over the language's, in menus
// (GtData::imperial) and races.
TEST(speed_unit_preference) {
    CHECK(parse_speed_unit("kmh") == std::optional<bool>(false) && parse_speed_unit(" KM/H ") == std::optional<bool>(false));
    CHECK(parse_speed_unit("mph") == std::optional<bool>(true) && parse_speed_unit("MPH") == std::optional<bool>(true));
    CHECK(!parse_speed_unit("") && !parse_speed_unit("auto"));
    game::GtData gd;
    gd.units = language_units(Language::BR);
    CHECK(!speed_unit() && !gd.imperial());
    set_speed_unit(true);
    CHECK(gd.imperial());
    gd.units = language_units(Language::US);
    set_speed_unit(false);
    CHECK(!gd.imperial());
    set_speed_unit(std::nullopt);
    CHECK(gd.imperial());
}

// Loaders that only get a reader learn the region from the VFS's region file; without a disc
// (synthetic readers) it is NTSC-U.
TEST(region_data_through_a_reader) {
    auto reader = [](const char* key) {
        return [key](const std::string& p) -> std::optional<Bytes> {
            if (p != Vfs::kRegionFile || !key) return std::nullopt;
            return Bytes(key, key + std::strlen(key));
        };
    };
    CHECK(region_data(reader("eu")).region == Region::Europe);
    CHECK(region_data(reader("jp")).region == Region::Japan);
    CHECK(region_data(reader(nullptr)).region == Region::America);
    CHECK(region_data(reader("xx")).region == Region::America);
    CHECK(region_data(nullptr).region == Region::America);
    Vfs none(nullptr);
    CHECK(none.region().region == Region::America && !none.read(Vfs::kRegionFile) && !none.read(Vfs::kLanguageFile));
    CHECK(none.language() == Language::US);
    none.set_language(Language::FR);
    CHECK(language_of([&](const std::string& p) { return none.read(p); }) == Language::FR);
    // Without a language file a reader's language is its region's own.
    CHECK(language_of(reader("eu")) == Language::BR && language_of(reader("jp")) == Language::JP);
}

// Any region's disc (GT3R_DISC): the region is recognised, the GT Mode data loads from the
// region's own files in each of its languages, every arcade course resolves, and a new game
// starts with the region's money.
TEST(region_disc_data_set) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    const RegionData rd = vfs.region();
    std::printf("  %s disc: %s data\n", disc->info().serial.c_str(), region_name(rd.region));
    for (Language lang : rd.languages()) {
        vfs.set_language(lang);
        game::GtData gd;
        const bool ok = gd.load([&](const std::string& p) { return vfs.read(p); });
        if (!ok) std::printf("  %s: %s\n", text::language_code(lang), gd.error().c_str());
        CHECK(ok && gd.region == rd.region && gd.language == lang);
        CHECK(gd.starting_credits() == (rd.region == Region::Japan ? 1500000u : 18000u));
        CHECK(gd.imperial() == (lang == Language::US));
        int courses = 0, missing = 0;
        for (const auto& [game, list] : gd.arcade_courses)
            for (const game::GtData::ArcadeCourse& c : list) {
                ++courses;
                if (course_file_path(vfs, c.course) != rd.course_path(c.course, lang)) ++missing;
            }
        CHECK(courses > 100 && missing == 0);
    }
}

// Any region's disc: the money the game shows comes from the disc's own data, x100 in
// Japanese: a new game's credits and the shop prices (game config), car, sale and part prices
// (database), race prizes and championship bonuses (events), and the prize page's word before
// "Cr." (realtime text, else the program's default). The expected sums are each disc's own:
// the NTSC-J disc stores 15,000 starting credits and its own prizes (Sunday Cup 6 hundreds,
// not 10), the PAL and NTSC-J discs another price for one car.
TEST(region_disc_money_from_data) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    const RegionData rd = vfs.region();
    const bool jp = rd.region == Region::Japan;
    const u32 k = jp ? 100 : 1;
    for (Language lang : rd.languages()) {
        vfs.set_language(lang);
        auto data = std::make_shared<game::GtData>();
        CHECK(data->load([&](const std::string& p) { return vfs.read(p); }));
        game::GtMode gt(data);
        CHECK(data->money_scale == int(k) && gt.state().credits == (jp ? 1500000 : 18000));
        CHECK(data->starting_credits() == (jp ? 1500000u : 18000u));
        CHECK(data->car_wash_price() == 50 * k && data->oil_change_price() == 250 * k && data->wheel_price() == 500 * k);
        // Cars: R34 V-spec 55,980 everywhere (x100 in Japanese), its sale a quarter; the RSX
        // concept 30,000 on the NTSC-U disc, 300,000 on the PAL and NTSC-J discs.
        game::GarageCar r34;
        r34.car = "r34_gtr_v_99";
        CHECK(gt.car_price("r34_gtr_v_99") == 55980 * s64(k) && gt.sell_price(r34) == 13995 * s64(k));
        CHECK(gt.car_price("rsx_detroitver_01") == (rd.region == Region::America ? 30000 : 300000) * s64(k));
        std::optional<game::PartOffer> muffler = game::find_part(data->db, "r34_gtr_v_99", 37);
        CHECK(muffler && muffler->price == 1600 && gt.part_price(muffler->price) == 1600 * k);
        // Prizes (hundreds x 100) and a championship bonus.
        auto rules = [&](const char* code) { return gt.rules(code).value_or(game::EventRules{}); };
        CHECK(rules("Gsunday_e_0001").prize[0] == (jp ? 600u : 1000u) * k);
        CHECK(rules("Gsunday_e_0001").prize[5] == (jp ? 100u : 500u) * k);
        CHECK(rules("G_80sports_e_0001").prize[0] == (jp ? 2000u : 2500u) * k);
        CHECK(rules("Galtezza_e_0501").champ_bonus == 10000 * k);
        // The prize page's texts in the language.
        const char* prefix = "Prize";
        if (jp) prefix = "\xBE\xDE\xB6\xE2"; // the program's default (the disc's text has none)
        else if (lang == Language::FR) prefix = "Prix";
        else if (lang == Language::GR) prefix = "Preis";
        else if (lang == Language::IT) prefix = "Premio";
        else if (lang == Language::SP) prefix = "Precio";
        CHECK(data->prize_prefix == prefix);
        CHECK(game::prize_remain_text(data->prize_prefix, 600 * k) ==
              std::string(prefix) + (jp ? " Cr. 60,000" : " Cr. 600"));
        CHECK(game::credits_text(gt.state().credits) == (jp ? "Cr. 1,500,000" : "Cr. 18,000"));
    }
    // Without the realtime text every release falls back to the program's built-in word.
    game::GtData bare;
    CHECK(bare.load([&](const std::string& p) -> std::optional<Bytes> {
        if (p == "text/realtime.dat") return std::nullopt;
        return vfs.read(p);
    }));
    CHECK(bare.prize_prefix == "\xBE\xDE\xB6\xE2");
}

// A PAL disc's events carry the US database's medal times (+0x1C4 .. +0x1CF; races run at 60 Hz on every disc) and
// otherwise the PAL database's own bytes; the other discs keep their own records (their times are the US ones already).
TEST(region_us_medal_times) {
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    const Volume& vol = disc->volume();
    data::CarDatabase db, us;
    CHECK(db.load(vol));
    const bool has_us = vol.find("database/paramdb_us.db") != nullptr; // not on the NTSC-J disc
    if (has_us) CHECK(us.load(vol, std::string("_us")));
    const std::string own = std::string("database/paramdb") + RegionData{vol.region()}.db_suffix() + ".db";
    const Volume::Entry* e = vol.find(own);
    CHECK(e != nullptr);
    if (!e) return;
    const Bytes raw = vol.read(*e);
    const db::Table events = db::Table::parse(db::parse_archive(raw)[size_t(data::TableId::Event)]);
    const bool pal = vol.region() == Region::Europe;
    int checked = 0, retuned = 0, differ = 0;
    for (size_t i = 0; i < events.size(); ++i) {
        const ByteSpan* mine = db.record(data::TableId::Event, events.id_of(i));
        const ByteSpan* theirs = has_us ? us.record(data::TableId::Event, events.id_of(i)) : nullptr;
        const ByteSpan disc_own = events.records[i];
        if (!mine || mine->size() < 0x1D0 || (has_us && !theirs)) {
            ++differ;
            continue;
        }
        ++checked;
        if (theirs && !std::equal(disc_own.begin() + 0x1C4, disc_own.begin() + 0x1D0, theirs->begin() + 0x1C4)) ++retuned;
        for (size_t k = 0; k < mine->size(); ++k)
            if ((*mine)[k] != (pal && k >= 0x1C4 && k < 0x1D0 ? (*theirs)[k] : disc_own[k])) {
                ++differ;
                break;
            }
    }
    std::printf("  %s: %d events, %d with other medal times than the US set's%s\n", own.c_str(), checked, retuned,
                has_us ? "" : " (no US set on this disc)");
    CHECK(checked > 400 && differ == 0);
    CHECK(retuned == (pal ? 39 : 0));
}
