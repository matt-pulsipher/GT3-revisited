// Gran Turismo Concept's race display rows (the Concept spec; race/hud_layout.h): a Concept disc's own
// racemode.db / racedetail.db rows take the place of the built-in ones. The disc test runs on every disc of
// GT3R_DISC_CONCEPT (disc images separated by ';'; skipped without it).

#include "test_framework.h"

#include "concept_discs.h"
#include "disc/disc.h"
#include "race/hud_layout.h"
#include "vfs/vfs.h"

#include <memory>

using namespace gt3r;
using namespace gt3r::race;

// Tables that do not parse change nothing.
TEST(concept_hud_tables_refused) {
    const size_t race = hud_layout(HudMode::Race).size();
    const Bytes junk(64, 0x5A);
    CHECK(!set_hud_tables(junk, junk));
    CHECK(hud_layout(HudMode::Race).size() == race && race > 0);
    clear_hud_tables();
    CHECK(hud_layout(HudMode::Race).size() == race);
}

TEST(concept_disc_hud_rows) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    const std::vector<HudRow> gt3_license(hud_layout(HudMode::License).begin(), hud_layout(HudMode::License).end());
    const std::vector<HudRow> gt3_race(hud_layout(HudMode::Race).begin(), hud_layout(HudMode::Race).end());
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        Vfs vfs(&disc->volume());
        const std::optional<Bytes> modes = vfs.read("database/racemode.db"), rows = vfs.read("database/racedetail.db");
        CHECK(modes && rows);
        if (!modes || !rows) continue;
        CHECK(set_hud_tables(*modes, *rows));
        // The single race's rows are GT3's.
        const std::span<const HudRow> race_rows = hud_layout(HudMode::Race);
        CHECK(race_rows.size() == gt3_race.size());
        for (size_t i = 0; i < std::min(race_rows.size(), gt3_race.size()); ++i)
            CHECK(race_rows[i].part == gt3_race[i].part && race_rows[i].x == gt3_race[i].x && race_rows[i].y == gt3_race[i].y);
        // The license test's: GT3's rows, the three target lines with width 0.
        const std::span<const HudRow> lic = hud_layout(HudMode::License);
        CHECK(lic.size() == gt3_license.size());
        int targets = 0;
        for (const HudRow& r : lic)
            if (r.part == HudPart::Target) {
                ++targets;
                CHECK(r.w == 0);
            }
        CHECK(targets == 3);
        // The replay's: one car name row for every view at (16, 340), the steering monitor centred at (320, 420).
        const HudRow* name0 = hud_row(HudMode::Replay, HudPart::CarName, -1, 0);
        const HudRow* name1 = hud_row(HudMode::Replay, HudPart::CarName, -1, 1);
        CHECK(name0 && name0 == name1 && name0->x == 16 && name0->y == 340);
        const HudRow* steer = hud_row(HudMode::Replay, HudPart::Steering, -1, 1);
        CHECK(steer && steer->x == 320 && steer->y == 420);
        // The 2 player halves': GT3's parts where GT3 has them (so the halves keep their layout), the event display wider
        // top and bottom (400), a second pedal row (type 13 sub 1) in both.
        for (HudMode m : {HudMode::Battle, HudMode::BattleWide}) {
            for (HudPart part : {HudPart::Rank, HudPart::LapCount, HudPart::Map, HudPart::DigitalSpeed, HudPart::BarTach, HudPart::Gear}) {
                const HudRow *now = hud_row(m, part, -1, 1), *gt3 = hud_builtin_row(m, part, -1, 1);
                CHECK(now && gt3 && now->x == gt3->x && now->y == gt3->y && now->w == gt3->w && now->h == gt3->h);
            }
            CHECK(hud_row(m, HudPart::AccelBrake, 1, 1) && !hud_builtin_row(m, HudPart::AccelBrake, 1, 1));
        }
        const HudRow* events = hud_row(HudMode::Battle, HudPart::Events, -1, 1);
        CHECK(events && events->w == 400 && hud_builtin_row(HudMode::Battle, HudPart::Events, -1, 1)->w == 320);
        // The Analyzer's REPLAY (LOGGER): a whole-frame dashboard at the bottom (Hud::draw_logger): the steering centred at
        // (320, 420), the bar tachometer, the pedal boxes, the gear, a digital speed and the hybrid store monitor (type 13
        // sub 1, race capacity monitor).
        for (const HudRow& r : hud_layout(HudMode::Logger))
            std::printf("    logger row part %d sub %d (%d, %d) %dx%d align %d/%d\n", int(r.part), r.sub, r.x, r.y, r.w, r.h,
                        r.align, r.valign);
        CHECK(hud_layout(HudMode::Logger).size() == 6);
        const HudRow* lsteer = hud_row(HudMode::Logger, HudPart::Steering, -1, 1);
        CHECK(lsteer && lsteer->x == 320 && lsteer->y == 420 && lsteer->w == 100 && lsteer->h == 91);
        const HudRow* store = hud_row(HudMode::Logger, HudPart::AccelBrake, 1, 1);
        CHECK(store && store->x == 615 && store->y == 400 && store->w == 30 && store->h == 70 && !hud_builtin_row(HudMode::Logger, HudPart::AccelBrake, 1, 1));
        CHECK(hud_row(HudMode::Logger, HudPart::DigitalSpeed, -1, 1) && hud_row(HudMode::Logger, HudPart::BarTach, -1, 1));
        // The demonstration's: Tokyo-Geneva adds the view mode and a car name at (16, 431).
        const HudRow* demo_name = hud_row(HudMode::Demo, HudPart::CarName, -1, 1);
        const bool geneva = vfs.exists("database/paramdb_gtc_eu.db") && disc->info().serial.find("508") != std::string::npos;
        std::printf("  %s: %zu replay rows, demonstration car name %s\n", disc->info().serial.c_str(), hud_layout(HudMode::Replay).size(),
                    demo_name ? "shown" : "not shown");
        if (demo_name) {
            CHECK(demo_name->x == 16 && demo_name->y == 431 && hud_row(HudMode::Demo, HudPart::ViewMode, -1, 1));
        }
        if (geneva) {
            CHECK(demo_name != nullptr);
        }
        clear_hud_tables();
        CHECK(hud_layout(HudMode::License).size() == gt3_license.size() && hud_layout(HudMode::License)[0].w == gt3_license[0].w);
    }
    clear_hud_tables();
}
