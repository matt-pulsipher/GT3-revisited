// GT-mode menu bindings: the presentations a result queues (the race prize pages, the license
// card, the championship trophy, the prize car, "garage full", the record name entry), their
// timelines and the prize counter, and the prize models the app draws on them.

#include "game/gt_mode_menu/internal.h"

#include "game/prize_count_up.h"

#include <algorithm>
#include <cstdlib>

namespace gt3r::game {

namespace gt_menu {

namespace {

// Starts / steps the presentation's prize counter (prize_count_up.h; once per frame from the
// page's first frame); true when it beeps on this frame.
bool run_counter(Show& sh, const GameState& st, bool confirm) {
    Show::Counter& k = sh.counter;
    if (!k.started) {
        k.cur = sh.credited ? st.credits - sh.money : st.credits;
        k.target = k.cur + sh.money;
        k.started = true;
    }
    const bool beep = step_prize_count_up(k, confirm && k.state != 3);
    if (k.state == 3 && k.done_frame < 0) k.done_frame = menu_state().show_frames;
    return beep;
}

} // namespace

bool counter_done(const Show& sh) { return sh.counter.state == 3; }
bool trophy_ok() {
    const MenuState& ms = menu_state();
    return ms.show && ms.show->counter.done_frame >= 0 &&
           ms.show_frames >= std::max(ms.show->counter.done_frame, kTrophyMoney + 60) + kTrophyOkAfter;
}
// The remaining-prize line ("Prize Cr. <left>", presentation §F): fades in over 60 frames from
// `shown` (when its layer appears), stays 61 frames after the count is done, then fades out
// over 60 frames.
float remain_alpha(const Show& sh, int frame, int shown) {
    float a = std::clamp(float(frame - shown) / 60.0f, 0.0f, 1.0f);
    if (sh.counter.done_frame >= 0) a = std::min(a, std::clamp(1.0f - float(frame - sh.counter.done_frame - 61) / 60.0f, 0.0f, 1.0f));
    return a;
}

// The race prize page for a GT race result (presentation §F): the event's cup for places
// 1-3, the consolation prize further back; none without a trophy class.
std::optional<Show> race_prize_show(const GtMode& gt, const std::string& code, int place, s64 money) {
    auto rr = gt.rules(code);
    if (!rr || rr->trophy_class < 5 || rr->trophy_class > 8 || place < 0 || money <= 0) return std::nullopt;
    static const char* const kClass[] = {"easy", "normal", "hard", "special"};
    static const char* const kMedal[] = {"gold", "silver", "bronze"};
    Show sh{};
    sh.money = money;
    sh.rank = ordinal(place);
    if (place < 3) {
        sh.set = sh.page = "gtmode_mini";
        sh.model = std::string("race/g_") + kClass[rr->trophy_class - 5] + "_" + kMedal[place] + ".prm";
    } else {
        sh.set = sh.page = "consolation_prize";
        sh.model = "race/consolation.prm";
    }
    return sh;
}

// The car_get page's roulette is running: the page keeps the controls (its OK hidden, no key
// does anything) until it gives them back.
bool car_get_holding() {
    const MenuState& ms = menu_state();
    return ms.show && ms.show->page == "car_get" && !ms.show->car_get.roulette.released;
}

bool car_get_ok() { return !car_get_holding(); }

namespace {

// One frame of the car_get page (car get control's loop and the cars item): the "after"
// band is built at the top of the frame after the one that gave the controls back;
// the roulette steps; the controls come back on its release.
void car_get_frame(MenuController& ctl, Show& sh) {
    CarGetRoulette& rl = sh.car_get.roulette;
    if (rl.released) ctl.set_layer("after", 1);
    ++menu_state().show_frames;
    const bool held = !rl.released;
    rl.step();
    if (held && rl.released) {
        sh.car_get.released_frame = menu_state().show_frames;
        ctl.give_control();
    }
}

// The candidates' sizes the page reads from each car's built spec: +0x1A the
// chassis length, +0x22 the body's height extent (body f32 +0x2C truncated), in mm.
CarGetRoulette::Size car_get_size(const GtMode& gt, const std::string& label) {
    CarGetRoulette::Size z;
    const data::Car* c = gt.db().car(label);
    if (!c) return z;
    if (auto ch = gt.db().chassis(c->parts[3])) z.length_mm = ch->length_mm;
    if (auto b = gt.db().body(c->parts[5])) z.height_mm = int(b->inertia[3]);
    return z;
}

} // namespace

void start_car_get(Show& sh) {
    std::array<CarGetRoulette::Size, CarGetRoulette::kMaxCars> sizes{};
    if (GtModeSession* s = S())
        for (size_t i = 0; i < sh.candidates.size() && i < sizes.size(); ++i)
            sizes[i] = car_get_size(s->gt(), sh.candidates[i]);
    sh.car_get.roulette.start(int(sh.candidates.size()), sh.car_get.pick, sizes, shared_random());
    sh.car_get.released_frame = -1;
}

bool presentation_frame(MenuController& ctl, GtModeSession& s, const std::string& page, std::string& se) {
    MenuState& ms = menu_state();
    const bool confirm = (ctl.pressed() & menu::flow::key_bit(menu::flow::MenuKey::Confirm)) != 0;
    if (page == "car_get" && ms.show && ms.show->page == "car_get") {
        car_get_frame(ctl, *ms.show);
        return true;
    }
    if (page == "car_get" || page == "not_car_get" || is_mini_prize(page)) {
        ++ms.show_frames;
        if (ms.show && (page == "gtmode_mini" || page == "consolation_prize") &&
            run_counter(*ms.show, s.gt().state(), confirm))
            se = "cursor";
        return true;
    }
    // License card: "<x>_mess0" for 4 s, nothing for 2 s, then "<x>_mess1"; OK after 7 s.
    if (page == "license_get" && ms.show && !ms.show->layers.empty()) {
        int f = ++ms.show_frames;
        std::string id = ms.show->layers[0].substr(0, ms.show->layers[0].find("_mess"));
        ctl.set_layer(id + "_mess0", f < kLicMess0End ? 1 : 0);
        ctl.set_layer(id + "_mess1", f >= kLicMess1 ? 1 : 0);
        return true;
    }
    // Championship trophy: "mess0" for 3 s, the bonus box 1.5 s later, then 1 s; the
    // credits count up (the prize counter, from 6 s); 15 frames after that "mess1" and OK.
    if (page == "trophy_get") {
        int f = ++ms.show_frames;
        if (ms.show && run_counter(*ms.show, s.gt().state(), confirm)) se = "cursor";
        ctl.set_layer("mess0", f < kTrophyMess0End ? 1 : 0);
        ctl.set_layer("get_mess", f >= kTrophyMoney ? 1 : 0);
        ctl.set_layer("mess1", trophy_ok() ? 1 : 0);
        return true;
    }
    // Record name entry: L1 / R1 move the text box's cursor, auto-repeating (Back Space:
    // GtModeHooks::name_backspace).
    if (page == "name_entry") {
        const menu::flow::MenuKeys f = ctl.fired();
        const int dir = (f & key_bit(MenuKey::L1)) ? -1 : (f & key_bit(MenuKey::R1)) ? 1 : 0;
        if (dir && named_entry() && s.name_box.move(dir)) se = "cursor";
        return true;
    }
    return false;
}

std::optional<menu::flow::GtModeHooks::Presentation> next_presentation() {
    MenuState& ms = menu_state();
    // A record's name entry has closed: save the name with it (the result itself was saved
    // before the presentations).
    if (ms.show && !ms.show->record_key.empty()) autosave(nullptr);
    ms.show.reset();
    if (ms.shows.empty()) return std::nullopt;
    ms.show = ms.shows.front();
    ms.shows.erase(ms.shows.begin());
    ms.show_frames = 0;
    ++ms.show_serial;
    // The prize car page: its roulette over the set's candidates (car_get_roulette.h).
    if (ms.show->page == "car_get") start_car_get(*ms.show);
    // Screenshots: GT3R_GTMODE_PRIZE_FRAME=<n> starts presentations n frames in.
    if (const char* env = std::getenv("GT3R_GTMODE_PRIZE_FRAME")) {
        ms.show_frames = std::max(0, std::atoi(env));
        if (ms.show->page == "car_get") {
            const int n = ms.show_frames;
            ms.show_frames = 0;
            CarGetRoulette& rl = ms.show->car_get.roulette;
            while (ms.show_frames < n) {
                ++ms.show_frames;
                rl.step();
                if (rl.released && ms.show->car_get.released_frame < 0) ms.show->car_get.released_frame = ms.show_frames;
            }
        }
        if (S() && (ms.show->page == "gtmode_mini" || ms.show->page == "consolation_prize" ||
                     ms.show->page == "trophy_get")) {
            int n = ms.show_frames;
            ms.show_frames = 0;
            while (ms.show_frames < n) {
                ++ms.show_frames;
                run_counter(*ms.show, S()->gt().state(), false);
            }
        }
    }
    // A record's name entry: the text box starts with the name stored with the new entry (the
    // last name entered), the cursor after it (gt_mode §17.1).
    if (GtModeSession* s = S())
        if (const RankEntry* r = named_entry()) s->name_box.reset(r->name, kRecordNameMax);
    // The prize car page takes the controls when it is built ((pm, 1)) and
    // gives them back when its roulette is over (car_get_frame); with the controls given (a
    // screenshot started past the release) the page keeps them.
    const bool take = ms.show->page == "car_get" && !ms.show->car_get.roulette.released;
    return menu::flow::GtModeHooks::Presentation{ms.show->set, ms.show->page, ms.show->layers, take};
}

// Record name entry: Triangle / Square is Back Space (never leaves the page).
bool name_backspace() {
    GtModeSession* s = S();
    RankEntry* r = named_entry();
    if (!s || !r) return false;
    const bool sound = s->name_box.back_space();
    r->name = s->gt().state().player_name = s->name_box.text;
    return sound;
}

int name_cursor() {
    const GtModeSession* s = S();
    return s && named_entry() ? int(s->name_box.cursor) : -1;
}

} // namespace gt_menu

using namespace gt_menu;

bool queue_race_prize(const std::string& code, int place) {
    GtModeSession* s = S();
    if (!s || place < 0 || place >= 6) return false;
    auto rr = s->gt().rules(code);
    if (!rr) return false;
    std::optional<Show> sh = race_prize_show(s->gt(), code, place, s64(rr->prize[size_t(place)]));
    if (!sh) return false;
    sh->credited = false; // the race's result adds it when the race screen closes
    menu_state().shows.push_back(std::move(*sh));
    return true;
}

int add_license_record(const std::string& code, double seconds) {
    GtModeSession* s = S();
    if (!s || seconds <= 0) return -1;
    GtMode& gt = s->gt();
    std::string test_car = code;
    if (auto rr = gt.rules(code); rr && !rr->entries.empty())
        if (const data::CarVariant* v = gt.db().variant(rr->entries[0])) test_car = v->car;
    int rank = gt.add_record("lic:" + code, seconds, test_car, true, 6);
    if (rank >= 0)
        menu_state().shows.push_back({"name_entry", "name_entry", {}, "", "", 0, {}, "lic:" + code, rank, "", 0, "", true, {}, {}});
    return rank;
}

int add_course_time_trial_record(const std::string& course, double best_lap, const std::string& car) {
    GtModeSession* s = S();
    if (!s) return -1;
    int rank = s->gt().record_course_time_trial(course, best_lap, car);
    if (rank >= 0)
        menu_state().shows.push_back({"name_entry", "name_entry", {}, "", "", 0, {}, "ct:" + course, rank, "", 0, "", true, {}, {}});
    return rank;
}

int add_machine_test_record(const std::string& code, double value) {
    GtModeSession* s = S();
    if (!s) return -1;
    const GarageCar* ride = s->gt().state().ride_car();
    int rank = s->gt().record_machine_test(code, value, ride ? ride->car : std::string());
    if (rank >= 0)
        menu_state().shows.push_back({"name_entry", "name_entry", {}, "", "", 0, {}, "mt:" + code, rank, "", 0, "", true, {}, {}});
    return rank;
}

std::string presentation_model(const std::string& page, int* serial, float* fade) {
    const MenuState& ms = menu_state();
    if (serial) *serial = ms.show_serial;
    if (fade) *fade = 0;
    if (!ms.show || ms.show->page != page || ms.show->model.empty()) return {};
    // The model fades in; its animation starts with the fade.
    int from = page == "trophy_get" ? kTrophyFadeIn : is_mini_prize(page) ? 0 : kLicFadeIn;
    int to = page == "trophy_get" ? kTrophyFadeEnd : is_mini_prize(page) ? kMiniFadeFrames : kLicFadeEnd;
    if (ms.show_frames < from) return {};
    if (fade) *fade = std::min(1.0f, float(ms.show_frames - from) / float(to - from));
    return ms.show->model;
}

} // namespace gt3r::game
