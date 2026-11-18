// Gran Turismo Concept's attract sequence (the Concept spec): the title left alone for 20 seconds runs the
// release's demo races and movies, and any interruption goes back to the title. The disc test runs on every disc of
// GT3R_DISC_CONCEPT (disc images separated by ';'; skipped without it).

#include "test_framework.h"

#include "concept_discs.h"
#include "disc/disc.h"
#include "game/concept_game.h"
#include "game/gt_mode_menu.h"
#include "menu/flow/concept_hooks.h"
#include "menu/flow/game_flow.h"
#include "vfs/vfs.h"

#include <cstdlib>
#include <memory>

using namespace gt3r;
using namespace gt3r::menu;
using namespace gt3r::menu::flow;

namespace {

void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

RegionData release(const char* gcf) {
    RegionData rd{Region::Japan};
    rd.game = GameKind::Concept;
    rd.program_gcf = std::string("gameconf/") + gcf;
    return rd;
}

void settle(GameFlow& f) {
    for (int i = 0; i < 300; ++i) {
        if (!f.switching() && f.controller() && !f.controller()->busy()) break;
        f.tick(0);
    }
    f.tick(0);
}

// The title left alone until something is asked of the game (at most `frames` steps).
std::optional<GameRequest> idle(GameFlow& f, int frames) {
    for (int i = 0; i < frames && !f.waiting_for_game(); ++i) f.tick(0);
    return f.take_request();
}

} // namespace

TEST(concept_attract_steps) {
    const std::vector<std::string> geneva = game::concept_attract_steps(release("concept_eu.gcf"));
    CHECK(geneva.size() == 6 && geneva[0].empty() && geneva[1] == "stream/tvcm_GTCJP1.pss" && geneva[2].empty() &&
          geneva[3] == "stream/tvcm_GTCJP2.pss" && geneva[4].empty() && geneva[5] == "stream/opening.pss");
    CHECK(game::concept_attract_steps(release("concept_tw.gcf")) == geneva);
    const std::vector<std::string> seoul = game::concept_attract_steps(release("concept_kr.gcf"));
    const std::vector<std::string> tokyo = game::concept_attract_steps(release("concept.gcf"));
    CHECK(seoul.size() == 5 && seoul[3].empty() && seoul[4] == "stream/opening.pss");
    CHECK(tokyo.size() == 4 && tokyo[2].empty() && tokyo[3] == "stream/opening.pss");
    // Tokyo-Seoul v1.02 (v2.00's config, named among one per language): the 2001 program's three demo races.
    RegionData v102 = release("concept_kr.gcf");
    v102.gcf_per_language = true;
    CHECK(game::concept_attract_steps(v102) == tokyo);
    CHECK(kConceptTitleIdleFrames == 1200);
}

TEST(concept_disc_attract) {
    const std::vector<std::string> discs = test::concept_discs();
    if (discs.empty()) SKIP("needs GT3R_DISC_CONCEPT");
    for (const std::string& path : discs) {
        std::unique_ptr<Disc> disc = Disc::open(path);
        set_env("GT3R_GTMODE_SAVE", (test::temp_dir("concept-attract") / "game.sav").string().c_str());
        set_env("GT3R_GTMODE_NOSAVE", "1");
        game::reset_gt_mode_menu_for_tests();
        game::install_gt_mode_menu();
        Vfs vfs(&disc->volume());
        GameFlow f(vfs_reader(vfs), text::language_code(vfs.language()));
        f.start();
        settle(f);
        CHECK(f.on_title_page() && !f.attract());
        const std::vector<std::string> steps = concept_hooks().attract_steps ? concept_hooks().attract_steps() : std::vector<std::string>();
        CHECK(steps.size() >= 4);
        // A button keeps the title up: nothing is asked while it is pressed now and then.
        for (int i = 0; i < 3 * kConceptTitleIdleFrames && !f.waiting_for_game(); ++i) f.tick(i % 600 == 599 ? key_bit(MenuKey::L2) : 0);
        CHECK(!f.waiting_for_game() && f.on_title_page());
        // Left alone: the first step, a demo race on one of the disc's demo events, with the demos' time limit.
        std::optional<GameRequest> rq = idle(f, kConceptTitleIdleFrames + 300);
        CHECK(rq && f.attract() && rq->mode == "attract_demo" && !rq->race_code.empty() && rq->time_limit_s > 0);
        if (!rq) continue;
        std::printf("  %s: %zu steps, first demo %s on %s, %d s\n", disc->info().serial.c_str(), steps.size(), rq->race_code.c_str(),
                    rq->course.c_str(), int(rq->time_limit_s));
        // Each step that plays to its end is followed by the next one, in the release's order; then the title.
        for (size_t i = 1; i < steps.size(); ++i) {
            GameResult done;
            done.mode = rq->mode, done.finished = true;
            post_game_result(std::move(done));
            f.game_finished();
            rq = idle(f, 300);
            CHECK(rq.has_value() && f.attract());
            if (!rq) break;
            if (steps[i].empty()) CHECK(rq->mode == "attract_demo");
            else CHECK(rq->kind == GameRequest::Kind::PlayMovie && rq->course == steps[i]);
        }
        GameResult last;
        last.mode = "movie", last.finished = true;
        post_game_result(std::move(last));
        f.game_finished();
        settle(f);
        CHECK(!f.attract() && f.on_title_page() && !f.waiting_for_game());
        // An interrupted step (no finished result) goes straight back to the title.
        rq = idle(f, kConceptTitleIdleFrames + 300);
        CHECK(rq && rq->mode == "attract_demo");
        f.game_finished();
        settle(f);
        CHECK(!f.attract() && f.on_title_page() && !f.waiting_for_game());
    }
    game::reset_gt_mode_menu_for_tests();
}
