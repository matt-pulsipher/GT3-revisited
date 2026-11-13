// Player-made GT Mode events (game/data/mod_events.h, game/gt_mode_events_menu.cpp) and the .zip the
// Event creator exports (vfs/mod_archive.h write_zip). Synthetic tests run everywhere; the disc tests
// need $GT3R_DISC (NTSC-U).

#include "test_framework.h"

#include "base/file.h"
#include "base/game_random.h"
#include "disc/volume.h"
#include "formats/menu/mbl.h"
#include "game/data/car_database.h"
#include "game/data/mod_events.h"
#include "game/gt_mode.h"
#include "game/gt_mode_mods.h"
#include "game/gt_parts.h"
#include "race/exact_setup.h"
#include "sim/exact/params_build.h"
#include "race/pool_draw.h"
#include "menu/flow/menu_set.h"
#include "vfs/mod_archive.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

using namespace gt3r;
using namespace gt3r::test;

namespace {

const char* kChampionship = R"(
[event]
title = Hot Hatch Cup      ; a comment
description = Three rounds for front-wheel-drive hatchbacks.
picture = pictures/hatch.png
league = r/beginner/beginner
championship = yes
prizes = 5000, 3000, 2000, 1000, 500, 250
bonus = 10000
prize_car = r34_gtr_v_99

[entry]
license = ib
drivetrain = ff
aspiration = na
cars = stock
allowed = vitz_euro_00, demio_glx_00, no_such_car_here

[field]
opponents = vitz_euro_00:{VITZ2}, mira_tr_xx_avar_97, demio_glx_00:0:1   ; (a third field, from older files, is ignored)

[race1]
course = trialmt
laps = 2
[race2]
course = midfield
laps = 3
skill = hard
[race3]
course = rome
laps = 4
)";

const char* kSingles = R"(
[event]
title = Sunday Sprint
league = r/nowhere/nowhere
prizes = 2000, 1000, 500, 0, 0, 0
prize_car = vitz_euro_00

[field]
opponents = vitz_euro_00:0:0, mira_tr_xx_avar_97:0:0, tune:hot_vitz, sun_e_demio_glx_00, sun_e_beetle_20_00, demio_glx_00

[tune:hot_vitz]
car = vitz_euro_00
parts = sus_racing, muf_racing, wtr_3, tir_soft, no_such_part

[race1]
course = super
laps = 1
cars = 3
[race2]
course = trialmt
laps = 1
opponents = r34_gtr_v_99:{R34_1}, no_such_car_here:0
)";

bool write_text(const std::filesystem::path& p, std::string_view text) {
    std::filesystem::create_directories(p.parent_path());
    return write_host_file(p.generic_string(), ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
}

} // namespace

// The files with colour ids in place of their {NAME}s.
std::string with_colors(std::string text, u32 vitz2, u32 r34_1) {
    auto put = [&](const std::string& key, u32 v) {
        if (size_t i = text.find(key); i != std::string::npos) text.replace(i, key.size(), std::to_string(v));
    };
    put("{VITZ2}", vitz2);
    put("{R34_1}", r34_1);
    return text;
}

TEST(mod_event_parse) {
    std::string err;
    std::vector<std::string> warns;
    const std::string champ = with_colors(kChampionship, 1234, 77), singles = with_colors(kSingles, 1234, 77);
    auto d = data::parse_mod_event(champ, "hatch", &err, &warns);
    CHECK(d.has_value());
    if (!d) return;
    CHECK(d->id == "hatch" && d->title == "Hot Hatch Cup" && d->picture == "pictures/hatch.png");
    CHECK(d->league == "r/beginner/beginner" && d->championship);
    CHECK(d->prizes[0] == 5000 && d->prizes[5] == 250 && d->bonus == 10000 && d->prize_car == "r34_gtr_v_99");
    CHECK(d->license == 3 && d->drivetrain == 1 && d->aspiration == 1 && d->car_class == 3);
    CHECK(d->allowed.size() == 3 && d->allowed[1] == "demio_glx_00");
    CHECK(d->field.size() == 3 && d->field[0].color == 1234 && d->field[1].label == "mira_tr_xx_avar_97");
    CHECK(d->field[1].color == 0xFFFFFFFFu && d->field[2].color == 0);
    CHECK(d->races.size() == 3 && d->races[2].course == "rome" && d->races[2].laps == 4);
    CHECK(d->races[0].skill == -1 && d->races[1].skill == 2);
    CHECK(warns.empty());
    // What the creator writes reads back the same.
    auto again = data::parse_mod_event(data::format_mod_event(*d), "hatch", &err);
    CHECK(again.has_value());
    if (again) {
        CHECK(again->title == d->title && again->description == d->description && again->league == d->league);
        CHECK(again->prizes == d->prizes && again->bonus == d->bonus && again->prize_car == d->prize_car);
        CHECK(again->license == d->license && again->drivetrain == d->drivetrain && again->car_class == d->car_class);
        CHECK(again->allowed == d->allowed);
        CHECK(again->field.size() == 3 && again->field[2].label == "demio_glx_00" && again->field[0].color == 1234 &&
              again->field[1].color == 0xFFFFFFFFu);
        CHECK(again->races.size() == 3 && again->races[1].laps == 3 && again->races[1].skill == 2 && again->races[0].skill == -1);
    }
    // A race of its own lineup, its place "last" by default.
    auto s = data::parse_mod_event(singles, "sprint", &err);
    CHECK(s && !s->championship && s->races[1].opponents.size() == 2 && s->races[1].opponents[0].color == 77);
    // Unknown words are warnings; no race is an error.
    warns.clear();
    CHECK(data::parse_mod_event("[event]\ntitle = x\n[entry]\nlicense = q\n[race1]\ncourse = rome\n", "x", &err, &warns));
    CHECK(warns.size() == 1);
    CHECK(!data::parse_mod_event("[event]\ntitle = x\n", "x", &err) && !err.empty());
}

TEST(mod_event_series_names) {
    CHECK(data::mod_event_series("my_events", "hatch_cup") == "Xmy_events_hatch_cup");
    CHECK(data::mod_event_series("My Mod", "Cup-1") == "Xmy_mod_cup_1");
    const std::string a = data::mod_event_series("a_very_long_mod_name_for_events", "an_even_longer_event_file_name");
    const std::string b = data::mod_event_series("a_very_long_mod_name_for_events", "an_even_longer_event_file_name_2");
    CHECK(a.size() <= 40 && b.size() <= 40 && a != b && a[0] == data::kModEventPrefix);
}

TEST(write_zip_installs) {
    // The creator's export: a mod folder in a .zip, read back by the Mods screen's installer.
    std::vector<std::pair<std::string, Bytes>> files;
    const std::string ini = "[event]\ntitle = Zip Cup\n[race1]\ncourse = rome\n";
    Bytes picture(5000);
    for (size_t i = 0; i < picture.size(); ++i) picture[i] = u8(i * 7);
    files.emplace_back("zip_cup/mod.ini", Bytes{'[', 'm', 'o', 'd', ']', '\n'});
    files.emplace_back("zip_cup/events/zip_cup.ini", Bytes(ini.begin(), ini.end()));
    files.emplace_back("zip_cup/pictures/zip_cup.png", picture);
    const std::filesystem::path zip = temp_path("zip_cup.zip");
    CHECK(write_host_file(zip.generic_string(), write_zip(files)));
    std::string err;
    std::unique_ptr<ZipArchive> z = ZipArchive::open(zip.generic_string(), &err);
    CHECK(z != nullptr);
    if (!z) return;
    CHECK(z->entries().size() == 3);
    for (size_t i = 0; i < files.size(); ++i) {
        CHECK(z->entries()[i].name == files[i].first);
        std::optional<Bytes> b = z->read_all(z->entries()[i], 1 << 20, &err);
        CHECK(b && *b == files[i].second);
    }
    const std::filesystem::path mods = temp_dir("zip_cup_mods");
    std::optional<ModArchivePlan> plan = plan_mod_install(*z, "zip_cup.zip", mods.generic_string(), &err);
    CHECK(plan && plan->mods.size() == 1 && plan->mods[0].id == "zip_cup");
    if (plan) CHECK(install_mod_archive(*z, *plan, mods.generic_string(), temp_path("zip_cup_stage").generic_string(), nullptr, &err));
    CHECK(std::filesystem::exists(mods / "zip_cup" / "events" / "zip_cup.ini"));
    // A folder with only events/ (no mod.ini) is a mod too.
    const std::filesystem::path bare = temp_path("bare.zip");
    CHECK(write_host_file(bare.generic_string(), write_zip({{"events/cup.ini", Bytes(ini.begin(), ini.end())}})));
    std::unique_ptr<ZipArchive> zb = ZipArchive::open(bare.generic_string(), &err);
    CHECK(zb && plan_mod_install(*zb, "bare.zip", mods.generic_string(), &err).has_value());
}

namespace {

// GT Mode data from the disc with two events in a mod of the test's own, the menus' flow patched as the
// game patches it (the events on their league pages).
struct EventWorld {
    std::shared_ptr<game::GtData> data;
    menu::MblFile flow;
    std::string error;
};

u32 g_vitz2 = 0, g_r34_1 = 0; // the colour ids the files name (the Vitz's third paint, the R34's second)

EventWorld event_world(Disc& disc) {
    EventWorld w;
    const Volume& vol = disc.volume();
    {
        game::GtData plain;
        plain.load([&vol](const std::string& p) -> std::optional<Bytes> {
            if (p == Vfs::kProgramFile) return vol.read_program();
            const Volume::Entry* e = vol.find(p);
            if (!e) return std::nullopt;
            return vol.read(*e);
        });
        g_vitz2 = plain.color_id("vitz_euro_00", 2), g_r34_1 = plain.color_id("r34_gtr_v_99", 1);
    }
    const std::filesystem::path mod = temp_dir("mod_events_disc") / "my_events";
    write_text(mod / "events" / "hatch.ini", with_colors(kChampionship, g_vitz2, g_r34_1));
    write_text(mod / "events" / "sprint.ini", with_colors(kSingles, g_vitz2, g_r34_1));
    const std::string list = "my_events\t" + (mod / "events" / "hatch.ini").generic_string() + "\nmy_events\t" +
                             (mod / "events" / "sprint.ini").generic_string() + "\n";
    menu::flow::FileReader read = [&vol, list](const std::string& p) -> std::optional<Bytes> {
        if (p == Vfs::kModCarsList) return Bytes(list.begin(), list.end());
        if (p == Vfs::kProgramFile) return vol.read_program();
        const Volume::Entry* e = vol.find(p);
        if (!e) return std::nullopt;
        return vol.read(*e);
    };
    game::install_mod_menu_patches();
    game::forget_mod_menu_plan();
    w.data = std::make_shared<game::GtData>();
    if (!w.data->load(read)) w.error = w.data->error();
    if (std::optional<Bytes> mb = read("menu/gt_mode.mbl")) {
        w.flow = menu::MblFile::parse(*mb);
        menu::flow::menu_patches().flow("gt_mode", read, w.flow);
    }
    return w;
}

const menu::MblPage* page(const menu::MblFile& f, const std::string& name) {
    int i = f.find_page(name);
    return i < 0 ? nullptr : &f.pages[size_t(i)];
}

bool has_item(const menu::MblPage* p, const std::string& name) {
    return p && std::any_of(p->items.begin(), p->items.end(), [&](const menu::MblItem& it) { return it.name == name; });
}

} // namespace

TEST(mod_event_disc_records) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    EventWorld w = event_world(*disc);
    CHECK(w.error.empty());
    const data::CarDatabase& db = w.data->db;
    CHECK(db.mod_events().size() == 2);
    const data::ModEvent* hatch = db.mod_event("Xmy_events_hatch_0502");
    CHECK(hatch && hatch->champ == "Xmy_events_hatch_0500" && hatch->races.size() == 3);
    const data::ModEvent* sprint = db.mod_event("Xmy_events_sprint_0001");
    CHECK(sprint && sprint->champ.empty() && sprint->races.size() == 2);
    if (!hatch || !sprint) return;
    // A car this disc lacks drops out of the lineup.
    CHECK(sprint->def.races[1].opponents.size() == 1);
    game::GtMode gt(w.data);
    std::optional<game::EventRules> r = gt.rules("Xmy_events_hatch_0502");
    CHECK(r.has_value());
    if (!r) return;
    CHECK(r->course == "midfield" && r->laps == 3 && r->cars == 4);
    CHECK(r->prize[0] == 5000 && r->prize[5] == 300 && r->champ_bonus == 10000); // (stored in hundreds, rounded: 250 -> 300)
    CHECK(r->required_license == 3 && r->drivetrain == 1 && r->aspiration == 1 && r->car_class == 3 && !r->dirt_tires);
    CHECK(r->league == 0);
    CHECK(r->regulation == "Xmy_events_hatch_reg"); // (the cars this disc has: two)
    // A race's skill: the arcade race's driving (event +0x19C .. +0x1B3), else the base race's.
    const data::Event* hard = db.event("AHST0001");
    const data::Event* r2 = db.event("Xmy_events_hatch_0502");
    const data::Event* r1 = db.event("Xmy_events_hatch_0501");
    const data::Event* base = db.event(hatch->def.base.empty() ? "Galtezza_e_0501" : hatch->def.base);
    CHECK(hard && r2 && r1);
    if (hard && r2 && r1) {
        CHECK(std::equal(r2->bytes.begin() + 0x19C, r2->bytes.begin() + 0x1B4, hard->bytes.begin() + 0x19C));
        if (base && r1->bytes.size() >= 0x1B4) {
            const Bytes a(r1->bytes.begin() + 0x19C, r1->bytes.begin() + 0x1B4);
            bool from_base = false;
            for (const data::Event& e : db.events()) // (the loader's base: a G race of the disc)
                if (!e.label.empty() && e.label[0] == 'G' && e.bytes.size() >= 0x1B4 &&
                    Bytes(e.bytes.begin() + 0x19C, e.bytes.begin() + 0x1B4) == a)
                    from_base = true;
            CHECK(from_base);
        }
    }
    // Its own lineup: race 2 of the sprint has two cars racing (the player and the R34).
    CHECK(gt.rules("Xmy_events_sprint_0002")->cars == 2);
    CHECK(w.data->result_code("Xmy_events_hatch_0500") && w.data->result_code("Xmy_events_sprint_0002"));
    CHECK(!game::GtMode::gt_mode_code("Xmy_events_sprint_0001")); // (no league, ending or completion counts it)
}

TEST(mod_event_disc_menus) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    EventWorld w = event_world(*disc);
    std::string map;
    const std::vector<std::string> leagues = game::league_pages(w.flow, &map);
    CHECK(map == "r/r" && leagues.size() >= 5 && leagues.front() == "r/beginner/beginner");
    // The championship on the Beginner League: its item, then its series and entry pages.
    CHECK(has_item(page(w.flow, "r/beginner/beginner"), "Xmy_events_hatch_0500"));
    const menu::MblPage* s = page(w.flow, "r/beginner/xmy_events_hatch");
    CHECK(s && s->back == "r/beginner/beginner");
    CHECK(has_item(s, "Xmy_events_hatch_0500") && has_item(s, "Xmy_events_hatch_0501_info") && has_item(s, "Xmy_events_hatch_0503"));
    CHECK(!has_item(s, "Xmy_events_hatch_0504"));
    if (s)
        for (const menu::MblItem& it : s->items)
            if (it.action == "set_race_code") CHECK(it.link == "r/beginner/xmy_events_hatch_entry");
    CHECK(page(w.flow, "r/beginner/xmy_events_hatch_entry") != nullptr);
    // The other names a league this disc does not have: the Custom Events league, on the race map.
    CHECK(has_item(page(w.flow, "r/custom/custom"), "Xmy_events_sprint_0001"));
    CHECK(has_item(page(w.flow, "r/r"), "custom_events"));
    CHECK(page(w.flow, "r/custom/xmy_events_sprint") != nullptr);
    // GT Mode's catalog sees the championship (start_championship needs it).
    const game::RaceSeries* rs = w.data->catalog.series_for("Xmy_events_hatch_0501");
    CHECK(rs && rs->championship() && rs->races.size() == 3);
}

TEST(mod_event_disc_results) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    EventWorld w = event_world(*disc);
    game::GtMode gt(w.data);
    const float completion = gt.completion();
    // The championship's field is drawn from its pool as a disc championship's: every car of it, in the
    // draw's order; each in its entry's paint (the Vitz's third; the others' drawn at the race start).
    CHECK(gt.start_championship("Xmy_events_hatch_0501"));
    const game::Championship& ch = gt.state().championship;
    std::vector<std::string> field = ch.entrants;
    std::sort(field.begin(), field.end());
    CHECK(field == std::vector<std::string>({"demio_glx_00", "mira_tr_xx_avar_97", "vitz_euro_00"}));
    const std::vector<int> paints = gt.pool_paints("Xmy_events_hatch_0501", ch.entrants);
    CHECK(paints.size() == 3);
    for (size_t i = 0; i < paints.size() && i < ch.entrants.size(); ++i)
        CHECK(paints[i] == (ch.entrants[i] == "vitz_euro_00" ? 2 : -1));
    // A race with its own lineup draws it the same way, its paint with it.
    std::vector<int> drawn;
    CHECK(gt.pick_opponents("Xmy_events_sprint_0002", race_info_random(), &drawn) == std::vector<std::string>({"r34_gtr_v_99"}));
    CHECK(drawn == std::vector<int>({1}));
    const s64 credits = gt.state().credits;
    game::Award a;
    for (int round = 0; round < 3; ++round) a = gt.record_round(0, {0, 1, 2, 3});
    CHECK(a.championship_over && a.champion && a.bonus == 10000);
    CHECK(gt.state().credits == credits + 3 * 5000 + 10000);
    CHECK(a.new_cars.size() == 1);
    if (!a.new_cars.empty()) CHECK(gt.state().garage[size_t(a.new_cars[0])].car == "r34_gtr_v_99");
    CHECK(gt.state().best_position("Xmy_events_hatch_0500") == 0);
    // Once only.
    CHECK(gt.start_championship("Xmy_events_hatch_0501"));
    for (int round = 0; round < 3; ++round) a = gt.record_round(0, {0, 1, 2, 3});
    CHECK(a.champion && a.new_cars.empty());
    // Single races: the prize car comes with the last race won.
    CHECK(gt.record_race("Xmy_events_sprint_0001", 0).new_cars.empty());
    a = gt.record_race("Xmy_events_sprint_0002", 0);
    CHECK(a.money == 2000 && a.new_cars.size() == 1);
    // No completion, league or ending from them.
    CHECK(gt.completion() == completion);
    CHECK(!gt.league_complete("beginner"));
    for (const game::RaceSeries& s : w.data->catalog.series) {
        if (s.tier != "beginner" || s.races.front()[0] == data::kModEventPrefix) continue;
        if (s.championship()) gt.state().race_best[s.races.front().substr(0, s.races.front().size() - 2) + "00"] = 0;
        else
            for (const std::string& code : s.races) gt.state().race_best[code] = 0;
    }
    gt.state().race_best.erase("Xmy_events_hatch_0500");
    CHECK(gt.league_complete("beginner")); // (the mod championship on its page is not the league's)
}

TEST(mod_event_disc_entry) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    EventWorld w = event_world(*disc);
    game::GtMode gt(w.data);
    const data::CarDatabase& db = w.data->db;
    // GT3's own entry check on the event's record and regulation: the list (a car the disc lacks left out), the
    // drivetrain, the engine and the car type.
    auto garage = [&](const std::string& label) {
        game::GarageCar g;
        g.car = label;
        if (const data::Car* c = db.car(label)) g.parts = c->parts;
        return g;
    };
    const game::EntryResult r34 = gt.check_entry("Xmy_events_hatch_0501", garage("r34_gtr_v_99"));
    CHECK(r34.regulation && r34.drivetrain && r34.aspiration);
    CHECK(!gt.check_entry("Xmy_events_hatch_0501", garage("vitz_euro_00")).regulation);
    CHECK(gt.check_entry("Xmy_events_hatch_0501", garage("mira_tr_xx_avar_97")).regulation); // (not on the list)
}

// The disc's own GT Mode races fix no paint in their pools (+0x110): their opponents' paints stay the race start's draw,
// as before pool paints were read (pick_opponents leaves `paints` empty for them). The records that fix paints are other
// kinds of event (demos, tests).
TEST(gtmode_pool_paints_disc) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    EventWorld w = event_world(*disc);
    game::GtMode gt(w.data);
    int races = 0, other = 0;
    for (const data::Event& e : w.data->db.events()) {
        if (e.label.empty() || e.bytes.size() < 0x190 || e.label[0] == data::kModEventPrefix) continue;
        bool fixed = false;
        for (int k = 0; k < 32; ++k) fixed = fixed || race::pool_color_id(e, k) != 0xFFFFFFFFu;
        if (!w.data->result_code(e.label)) {
            other += fixed;
            continue;
        }
        ++races;
        GameRandom rng{GameRandom::kBootSeed};
        std::vector<int> paints;
        const std::vector<std::string> field = gt.pick_opponents(e.label, rng, &paints);
        CHECK(!fixed && paints.empty());
        CHECK(gt.pool_paints(e.label, field).empty());
    }
    std::printf("  %d GT Mode races, none with pool paints; %d other events with them\n", races, other);
    CHECK(races > 300 && other > 0);
}

// A tuned opponent: a variant record whose settings are the ones the car gets with those parts; a pool bigger than the
// field: the race draws its number of cars from it.
TEST(mod_event_disc_tunes_pools) {
    Disc* disc = test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    EventWorld w = event_world(*disc);
    const data::CarDatabase& db = w.data->db;
    // variant_record reads back as the stock build of its parts, for every car with each upgrade the shops sell it.
    int checked = 0;
    for (const data::Car& c : db.cars()) {
        if (db.mod_car(c.label)) continue;
        for (const game::ShopItem& si : game::shop_items()) {
            std::optional<game::PartOffer> p = game::find_part(db, c.label, si.goods);
            const int slot = game::part_slot(game::goods_info(si.goods).table);
            if (!p || slot < 0) continue;
            std::array<u64, 27> parts = c.parts;
            parts[size_t(slot)] = p->id;
            const Bytes rec = sim::exact::variant_record(db, "test_variant", c.label, parts);
            data::CarVariant v;
            v.label = "test_variant", v.car = c.label, v.parts = parts, v.bytes = rec;
            const sim::exact::Equipment a = sim::exact::variant_equipment(db, v), b = sim::exact::stock_equipment(db, c.label, parts);
            CHECK(a.car == b.car && a.part == b.part && a.setting == b.setting);
            if (a.setting != b.setting) {
                for (size_t k = 0; k < a.setting.size(); ++k)
                    if (a.setting[k] != b.setting[k])
                        std::printf("  %s + %s: EQ +0x%zX %d != %d\n", c.label.c_str(), si.item, 0xF0 + 4 * k, a.setting[k], b.setting[k]);
                return;
            }
            ++checked;
        }
    }
    std::printf("  %d car and part pairs\n", checked);
    CHECK(checked > 1000);
    // The event's tune: a variant of the Vitz with the parts it names (an unknown one left out).
    const data::ModEvent* sprint = db.mod_event("Xmy_events_sprint_0001");
    CHECK(sprint != nullptr);
    if (!sprint) return;
    const data::CarVariant* hot = db.variant("Xmy_events_sprint_hot_vitz");
    CHECK(hot && hot->car == "vitz_euro_00");
    if (hot) {
        const data::Car* vitz = db.car("vitz_euro_00");
        int changed = 0;
        for (size_t i = 0; i < hot->parts.size(); ++i) changed += hot->parts[i] != vitz->parts[i];
        CHECK(changed == 5); // suspension, muffler, weight, front and rear tyres
    }
    CHECK(sprint->def.field.size() == 6 && sprint->def.field[2].label == "Xmy_events_sprint_hot_vitz");
    // Race 1 draws 3 of the 6; race 2 its own pool's one.
    game::GtMode gt(w.data);
    CHECK(gt.rules("Xmy_events_sprint_0001")->cars == 4);
    CHECK(gt.rules("Xmy_events_sprint_0002")->cars == 2);
    GameRandom rng{GameRandom::kBootSeed};
    const std::vector<std::string> drawn = gt.pick_opponents("Xmy_events_sprint_0001", rng);
    CHECK(drawn.size() == 3);
    for (const std::string& d : drawn)
        CHECK(std::any_of(sprint->def.field.begin(), sprint->def.field.end(), [&](const data::ModEventCar& c) { return c.label == d; }));
    // The tuned car races as built: its spec is the variant's.
    CHECK(race::exact_equipment(db, {"Xmy_events_sprint_hot_vitz"}).has_value());
}
