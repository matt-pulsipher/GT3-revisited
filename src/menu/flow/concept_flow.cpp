// Gran Turismo Concept's front end (the Concept spec): the control "concept" of GameFlow (menu/concept.mbl).
// The title page ar/push_start with its sub-pages, the main menu ar/top and each game's set-up sequence (level,
// course, category, car; the Course License page; Game Status), ending in a race request. The course and car pages
// are the arcade pickers of game_flow.cpp; what the game knows (open courses and cars, race codes, progress) comes
// through GtModeHooks' arcade_* hooks and ConceptHooks.

#include "menu/flow/concept_hooks.h"
#include "menu/flow/game_flow.h"
#include "menu/flow/gt_mode_hooks.h"

#include <algorithm>
#include <cmath>

namespace gt3r::menu::flow {

namespace {

ConceptHooks& stored_hooks() {
    static ConceptHooks h;
    return h;
}

constexpr const char* kTitlePage = "ar/push_start";

const Layer* layer_named(const Page& p, std::string_view name) {
    for (const Layer& l : p.layers)
        if (l.name == name) return &l;
    return nullptr;
}

const Element* element_named(const Layer* l, const std::string& name) {
    if (!l) return nullptr;
    for (const Element& e : l->elements)
        if (e.name == name) return &e;
    return nullptr;
}

const Element* widget_named(const Page& p, std::string_view name) {
    for (const Layer& l : p.layers)
        for (const Element& w : l.widgets)
            if (w.name == name) return &w;
    return nullptr;
}

bool category_page(const std::string& p) { return p == "ar/select_category" || p == "ar/select_category2"; }

// The category page's items in the order of the categories (the page lists them C, B, A, S, D).
int category_of(const MenuSet::PageRef& ref, std::string_view item) {
    if (ref.flow)
        for (size_t i = 0; i < ref.flow->items.size(); ++i)
            if (ref.flow->items[i].name == item) return int(i);
    return -1;
}

} // namespace

void set_concept_hooks(ConceptHooks h) { stored_hooks() = std::move(h); }
const ConceptHooks& concept_hooks() { return stored_hooks(); }

std::string GameFlow::concept_page(const char* base) const {
    // Tokyo-Geneva's program names ar/select_category2 and ar/select_car_2p2, the earlier releases' the plain pages
    // (concept §2.2, §12); only Tokyo-Geneva's set has the "2" pages.
    const std::string two = std::string(base) + "2";
    auto it = sets_.find("concept");
    if (it != sets_.end() && it->second && it->second->page(two).flow) return two;
    return base;
}

// The pages' background movie (concept §2.3; the course page's is its focused course's, started by the picker).
// A page the table does not list keeps the movie that runs (M).
std::string GameFlow::concept_movie(const std::string& page) const {
    if (page == kTitlePage || page == "ar/top" || page == "ar/level") return "stream/arcade_menu_gtc.pss";
    if (category_page(page)) return "stream/category_gtc.pss";
    if (page == "ar/select_car" || page == "ar/select_car_2p" || page == "ar/select_car_2p2") return "stream/car_gtc.pss";
    if (page == "ar/status") return "stream/status_gtc.pss";
    if (page == "ar/select_course") return "";
    return movie_;
}

void GameFlow::concept_tick() {
    if (!ctl_ || !ctl_->has_page() || ctl_->busy() || ctl_->depth() != 0) return;
    const std::string& page = ctl_->page_name();
    // The level page: holding L1 and R1 adds the Ace button, for the rest of the run (concept §5.4, §7.5).
    const MenuKeys ace = key_bit(MenuKey::L1) | key_bit(MenuKey::R1);
    if (page == "ar/level" && !concept_ace_ && (held_ & ace) == ace) {
        concept_ace_ = true;
        difficulty_ = std::max(0, ctl_->focus());
        open_control("concept", "ar/level", -1);
        return;
    }
    // The license page: the test under the cursor is the one its pace car page starts.
    if (page == "ar/select_license") concept_test_ = std::max(0, ctl_->focus());
    // Game Status: the map follows the focused button (MapButtonList 0x1B6630: each frame the scroll moves by the
    // rate 0.1 x 1.2 of what is left, program constants 0x333E28 and 0x333EC0).
    if (page == "ar/status") {
        float x = 0, y = 0;
        if (concept_map_target(x, y)) {
            if (!concept_map_set_) concept_map_x_ = x, concept_map_y_ = y, concept_map_set_ = true;
            concept_map_x_ += (x - concept_map_x_) * 0.1f * 1.2f;
            concept_map_y_ += (y - concept_map_y_) * 0.1f * 1.2f;
        }
        ++concept_status_frames_;
        if (const MenuController::ItemState* f = ctl_->focused(); f && f->flow) concept_status_item_ = f->flow->name;
    } else {
        concept_map_set_ = false;
    }
}

// Game Status: a direction moves the focus to the button that lies best that way, as GT3's map list picks its next
// building (the same MapButtonList; game/gt_mode_menu/lists.cpp map_input: the greatest cos^2 / distance among the
// buttons ahead). The controller does not see the directions on this page.
MenuKeys GameFlow::concept_map_input(MenuKeys held) {
    const MenuKeys dirs = key_bit(MenuKey::Up) | key_bit(MenuKey::Down) | key_bit(MenuKey::Left) | key_bit(MenuKey::Right);
    // (Also the settings menu's Normal and Custom pages: their buttons are a map list too, the widgets
    // "menu_map_normal" / "menu_map_custom".)
    bool map = ctl_ && ctl_->has_page() && ctl_->depth() == 0 && control_ == "concept" && ctl_->page_name() == "ar/status";
    if (ctl_ && ctl_->has_page() && control_ == "setting" && (ctl_->page_name() == "root" || ctl_->page_name() == "custom"))
        if (const MenuSet::PageRef ref = ctl_->page_at(ctl_->depth()); ref)
            map = widget_named(ref.layout(), "menu_map_normal") || widget_named(ref.layout(), "menu_map_custom");
    if (!map) {
        concept_map_prev_ = held;
        return held;
    }
    const MenuKeys fired = held & MenuKeys(~concept_map_prev_);
    concept_map_prev_ = held;
    const float dx = (fired & key_bit(MenuKey::Right)) ? 1.0f : (fired & key_bit(MenuKey::Left)) ? -1.0f : 0.0f;
    const float dy = (fired & key_bit(MenuKey::Up)) ? 1.0f : (fired & key_bit(MenuKey::Down)) ? -1.0f : 0.0f;
    const std::vector<MenuController::ItemState>& items = ctl_->items();
    const int from = ctl_->focus();
    if ((dx != 0 || dy != 0) && !ctl_->busy() && from >= 0 && from < int(items.size()) && items[size_t(from)].element) {
        auto centre = [&](int i, float& x, float& y) {
            const Element& e = *items[size_t(i)].element;
            x = e.x + e.w * 0.5f, y = e.y + e.h * 0.5f;
        };
        float cx, cy, best = 0;
        centre(from, cx, cy);
        const float dl = std::sqrt(dx * dx + dy * dy);
        int pick = -1;
        for (int i = 0; i < int(items.size()); ++i) {
            if (i == from || !items[size_t(i)].element) continue;
            float bx, by;
            centre(i, bx, by);
            const float vx = bx - cx, vy = by - cy, dist = std::sqrt(vx * vx + vy * vy);
            if (dist < 1e-3f) continue;
            const float cosv = (vx * dx + vy * dy) / (dist * dl);
            if (cosv <= 0) continue;
            const float score = cosv * cosv / dist;
            if (score > best) best = score, pick = i;
        }
        if (pick >= 0) {
            ctl_->set_focus(pick);
            sounds_.push_back("cursor");
        }
    }
    return held & MenuKeys(~dirs);
}

// Where the map's scroll is heading: the focused button's centre from the screen's centre, held inside half the
// "menu_map" window's size either way (0x1B6630 clamps the pointer's place to the window's centre +- half its width and
// height; M: that the place is the button's centre measured from the screen's, which keeps the map's art over the
// screen at both ends).
bool GameFlow::concept_map_target(float& x, float& y) const {
    const MenuController::ItemState* f = ctl_ ? ctl_->focused() : nullptr;
    const MenuSet::PageRef ref = ctl_ ? ctl_->page_at(0) : MenuSet::PageRef{};
    if (!f || !f->element || !ref) return false;
    float hw = 320.0f, hh = 68.0f;
    if (const Element* w = widget_named(ref.layout(), "menu_map")) hw = w->w * 0.5f, hh = w->h * 0.5f;
    x = std::clamp(f->element->x + f->element->w * 0.5f - kMenuWidth * 0.5f, -hw, hw);
    y = std::clamp(f->element->y + f->element->h * 0.5f - kMenuHeight * 0.5f, -hh, hh);
    return true;
}

void GameFlow::handle_concept(const MenuEvent& e) {
    using K = MenuEvent::Kind;
    const std::string& page = e.page;
    const std::string& a = e.action;
    const Target title = title_target();
    auto to_title = [&](const char* command) {
        title_focus_ = title_focus_for(command);
        return Target{title.set, title.page, title_focus_};
    };
    // 2 player car page: both pads are read by two_player_frame().
    if ((page == "ar/select_car_2p" || page == "ar/select_car_2p2") &&
        (e.kind == K::Confirm || e.kind == K::Cancel || e.kind == K::Exit || e.kind == K::Finish)) {
        if (e.kind == K::Exit || e.kind == K::Finish) ctl_->resume();
        return;
    }
    if (page == "ar/status" && !e.item.empty()) concept_status_item_ = e.item; // (the page reopens on it)
    switch (e.kind) {
    case K::BackAction:
        // Cancel on the Replay Theater page: the title's focus goes to its item (back ar/push_start).
        if (a == "set_focus_replay_theater") title_focus_ = title_focus_for("go_replay_theater");
        break;
    case K::Finish:
        if (page == "ar/top") {
            concept_choose_game(e);
        } else if (a == "go_arcade_menu") {
            // The title ends with mode 0: the main menu, on the game played last.
            title_focus_ = title_focus_for(a);
            transmission_ = option_value(options_, "gear_auto") == "off" ? 1 : 0; // the Options menu's AT / MT
            // ... and its driving aids: ASM and TCS, TCS only, neither (the assist step's three buttons).
            assist_ = option_index(options_, *find_game_option("assist"));
            open_control("concept", "ar/top", -1);
        } else if (a == "go_save_game" || a == "go_load_game") {
            const Target t = to_title(a.c_str());
            open_save_page(a == "go_save_game" ? SavePage::Mode::SaveGame : SavePage::Mode::LoadGame, t, t, true);
        } else if (a == "go_file_operation") {
            const Target t = to_title(a.c_str());
            open_save_page(SavePage::Mode::Files, t, t, true);
        } else if (a == "go_option") {
            title_focus_ = title_focus_for(a);
            switch_control("option", "option", 0);
        } else if (a == "go_replay_theater" || a == "go_demonstration") {
            title_focus_ = title_focus_for("go_replay_theater");
            open_theater(a == "go_replay_theater" ? TheaterPage::Mode::LoadReplay : TheaterPage::Mode::Demonstration,
                         TheaterFrom::Theater);
        } else if (a == "go_logger") {
            title_focus_ = title_focus_for("go_replay_theater");
            logger_focus_ = 0;
            switch_control("logger", "top", -1);
        } else if (a == "go_aspec_demo") {
            // The 2001 Tokyo and Tokyo-Seoul title's item: the GT3 demonstration movie (movie row 4, the Concept spec
            // §2.3, §3.2), then the title again.
            title_focus_ = title_focus_for(a);
            GameRequest m;
            m.kind = GameRequest::Kind::PlayMovie;
            m.mode = "movie";
            m.course = "stream/demoGT3A.pss";
            resume_set_ = title.set;
            resume_page_ = title.page;
            resume_focus_ = title_focus_;
            theater_resume_ = true; // (nothing to record when it comes back)
            request_race(std::move(m));
        } else if (page == "ar/level") {
            difficulty_ = std::max(0, e.child); // the focused index: Normal, Professional, Ace
            arcade_step(arcade_pos_ + 1);
        } else if (category_page(page)) {
            concept_category_ = std::max(0, e.child);
            if (const ConceptHooks& h = concept_hooks(); h.category_class) car_class_ = h.category_class(concept_category_);
            arcade_step(arcade_pos_ + 1);
        } else if (a == "with_lead_car" || a == "without_lead_car") {
            concept_lead_ = a == "with_lead_car";
            concept_start_race();
        } else if (a == "go_present") {
            // Game Status: MOVIE0 / MOVIE1 play their movie and the page comes back; the credit transfer is not
            // available yet (concept §7.6).
            const ConceptHooks& h = concept_hooks();
            if (e.item == "TRANSFER" && h.status_reached && h.status_reached(e.item)) {
                // The credit transfer (concept §7.6): the save page over the Gran Turismo 3 saves it can go to, with
                // the disc's own messages (memorycard page "transfer_money"); Game Status again when it is left.
                SavePage::TransferTexts texts;
                std::string title;
                if (const MenuSet::PageRef tr = load_set("memorycard").page("transfer_money"); tr) {
                    auto text_of = [](const Element* el) { return el && el->text ? el->text->text : std::string(); };
                    const Layer* hidden = layer_named(tr.layout(), "hidden");
                    texts.confirm = text_of(element_named(hidden, "confirm_text_default"));
                    texts.no_file = text_of(element_named(hidden, "confirm_text_nofile"));
                    texts.complete = text_of(element_named(hidden, "working_text_complete"));
                    auto first_text = [&](const char* layer) {
                        if (const Layer* l = layer_named(tr.layout(), layer))
                            for (const Element& el : l->elements)
                                if (el.text && !el.text->text.empty()) return el.text->text;
                        return std::string();
                    };
                    texts.already = first_text("already");
                    title = first_text("root");
                }
                const Target back{"concept", page, e.child};
                open_save_page(SavePage::Mode::Transfer, back, back, true, title);
                save_page_.set_transfer_texts(std::move(texts));
                break;
            }
            const std::string movie = h.present_movie ? h.present_movie(e.item) : std::string();
            if (movie.empty()) {
                // (A present that is not reached yet.)
                sounds_.push_back("disable");
                open_control("concept", page, e.child);
                break;
            }
            GameRequest m;
            m.kind = GameRequest::Kind::PlayMovie;
            m.mode = "movie";
            m.course = movie;
            resume_set_ = "concept";
            resume_page_ = page;
            resume_focus_ = e.child;
            theater_resume_ = true;
            request_race(std::move(m));
        } else if (page == "ar/status" && e.item == "PODRACE") {
            // The pod race's page, once every course license has a medal (concept §5.9).
            const ConceptHooks& h = concept_hooks();
            if (h.status_reached && h.status_reached(e.item)) {
                open_control("concept", "ar/podrace", -1);
            } else {
                sounds_.push_back("disable");
                open_control("concept", page, e.child);
            }
        } else if (a == "go_podrace_1p" || a == "go_podrace_2p") {
            // The pod race (concept §4.4): one player picks the pod's color and options on the car page and races the
            // class's event; two players pick theirs on the 2 player car page and race the 2 player event.
            const ConceptHooks& h = concept_hooks();
            const ConceptHooks::Pod pod = h.pod ? h.pod() : ConceptHooks::Pod{};
            if (pod.course.empty() || pod.entry.empty()) {
                sounds_.push_back("disable");
                open_control("concept", "ar/status", -1);
                break;
            }
            concept_pod_ = a == "go_podrace_1p" ? 1 : 2;
            course_ = pod.course;
            car_class_ = pod.car_class;
            difficulty_ = 0;
            if (concept_pod_ == 1) {
                arcade_action_ = "single_race";
                arcade_steps_ = {"ar/select_car"};
                arcade_step(0);
            } else {
                // Two players: the 2 player car page (0x111070), each player's row holding the pod alone.
                arcade_action_ = "two_player_battle";
                arcade_steps_ = {concept_page("ar/select_car_2p")};
                arcade_step(0);
            }
        } else if (page == "ar/status") {
            // The page's Back button.
            arcade_pos_ = -1;
            open_control("concept", "ar/top", -1);
        } else {
            log("not implemented: " + (a.empty() ? e.item : a) + " (page " + page + ")");
            open_control("concept", page, e.child);
        }
        break;
    case K::Action:
        if (a == "set_concept_car") {
            concept_show_key_ = e.item;
            if (const ConceptHooks& h = concept_hooks(); h.set_car) h.set_car(e.item);
        }
        break;
    case K::Confirm:
        if (picker_confirm(page)) arcade_step(arcade_pos_ + 1);
        break;
    case K::Exit:
    case K::Cancel:
        if (page == kTitlePage || page == "ar/credit" || page == "ar/replay_theater") {
            // The title has no way back (the screen leaves from it): it stays.
            open_control(title.set, title.page, title_focus_);
        } else if (page == "ar/top") {
            // cancel_select_game: back to the title, on Game Start.
            const Target t = to_title("go_arcade_menu");
            arcade_pos_ = -1;
            open_control(t.set, t.page, t.focus);
        } else if (page == "ar/podrace") {
            sounds_.push_back("cancel");
            open_control("concept", "ar/status", -1);
        } else if (!picker_cancel(page)) {
            if (concept_pod_ && arcade_pos_ <= 0) { // the pod's car page: back to the pod race's page
                concept_pod_ = 0;
                arcade_pos_ = -1;
                sounds_.push_back("cancel");
                open_control("concept", "ar/podrace", -1);
            } else {
                arcade_back();
            }
        }
        break;
    default: break;
    }
}

// The main menu's actions store the game (concept §4.2); each game runs its sequence of pages.
void GameFlow::concept_choose_game(const MenuEvent& e) {
    // Single Race offers only courses whose license has a medal (concept §5.5); with none, the main menu refuses it
    // (the original, on a new game: the item takes no Confirm).
    if (e.action == "single_race")
        if (const ConceptHooks& h = concept_hooks(); h.license_medal) {
            bool any = false;
            for (int c = 0; c < 10 && !any; ++c) any = h.license_medal(c) >= 0;
            if (!any) {
                sounds_.push_back("disable");
                arcade_game_ = std::max(0, e.child);
                open_control("concept", "ar/top", -1);
                return;
            }
        }
    concept_pod_ = 0;
    arcade_game_ = std::max(0, e.child);
    arcade_action_ = e.action;
    const std::string& a = e.action;
    const std::string category = concept_page("ar/select_category"), two = concept_page("ar/select_car_2p");
    if (a == "single_race") arcade_steps_ = {"ar/level", "ar/select_course", category, "ar/select_car"};
    else if (a == "time_attack") arcade_steps_ = {"ar/select_course", category, "ar/select_car"};
    else if (a == "two_player_battle") arcade_steps_ = {"ar/select_course", two};
    else if (a == "license") arcade_steps_ = {"ar/select_license"};
    else if (a == "status") arcade_steps_ = {"ar/status"};
    else if (a == "i_link_battle") {
        // i.LINK Battle over IP: the online lobby (docs/dev/ONLINE.md), as on a GT3 disc; the main menu resumes
        // when it closes.
        GameRequest m;
        m.kind = GameRequest::Kind::StartRace;
        m.mode = "online_battle";
        resume_set_ = "concept";
        resume_page_ = "ar/top";
        resume_focus_ = -1;
        arcade_pos_ = -1;
        request_race(std::move(m));
        return;
    } else {
        // (compulsory, goodies, load_guest_garage: actions without an item on the page.)
        log("not implemented: " + a);
        arcade_steps_.clear();
    }
    if (arcade_steps_.empty()) open_control("concept", "ar/top", -1);
    else arcade_step(0);
}

// The race descriptor (concept §4.4): car, course, race code and the car page's choices.
void GameFlow::concept_start_race() {
    GameRequest r;
    const std::string& a = arcade_action_;
    r.page = arcade_steps_.empty() ? "ar/top" : arcade_steps_.back();
    if (a == "license") {
        r.mode = "go_license";
        r.lead_car = concept_lead_;
        if (const ConceptHooks& h = concept_hooks(); h.license_tests) {
            const std::vector<ConceptHooks::Test> tests = h.license_tests();
            if (concept_test_ >= 0 && concept_test_ < int(tests.size())) {
                r.race_code = tests[size_t(concept_test_)].code;
                r.course = tests[size_t(concept_test_)].course;
            }
        }
    } else {
        r.course = course_;
        r.car = car_;
        if (!r.car.empty() && r.car[0] == '_') r.car.erase(0, 1);
        r.car_class = car_class_;
        r.car_color = car_paint_;
        r.transmission = transmission_;
        r.tire_mode = tire_mode_;
        r.assist = assist_;
    }
    if (a == "single_race") {
        r.mode = "single_race";
        r.difficulty = difficulty_; // 0 Normal, 1 Professional, 2 Ace: the game raises the car's race code by it
    } else if (a == "time_attack") {
        r.mode = "free_run";
        r.race_code = "ATAF0001";
    } else if (a == "two_player_battle") {
        const TwoPlayer& p1 = two_[0];
        const TwoPlayer& p2 = two_[1];
        auto car_of = [](const TwoPlayer& t) {
            std::string c = t.car >= 0 && t.car < int(t.cars.size()) ? t.cars[size_t(t.car)] : std::string();
            if (!c.empty() && c[0] == '_') c.erase(0, 1);
            return c;
        };
        const ConceptHooks& h = concept_hooks();
        r.mode = "two_player_battle";
        r.race_code = concept_pod_ ? "APO20001" : "A2P0001"; // (program constants, concept §4.4)
        r.car = car_of(p1);
        r.car_class = h.category_class ? h.category_class(p1.cls) : std::string();
        r.car_color = p1.paint;
        r.transmission = p1.transmission;
        r.tire_mode = p1.tire_mode;
        r.car2 = car_of(p2);
        r.car2_class = h.category_class ? h.category_class(p2.cls) : std::string();
        r.car2_color = p2.paint;
        r.transmission2 = p2.transmission;
        r.tire_mode2 = p2.tire_mode;
        r.assist = p1.assist;
        r.assist2 = p2.assist;
        if (concept_pod_) r.car_class = r.car2_class = car_class_;
    }
    if (const ConceptHooks& h = concept_hooks(); h.prepare) h.prepare(r);
    if (a == "single_race" || a == "time_attack") race_code_ = r.race_code;
    // After the race the game's sequence opens again (concept §4.2 step 4), from its first page (open question 2:
    // which of its pages the original reopens was not established), the choices made last focused.
    resume_set_ = "concept";
    resume_page_ = arcade_steps_.empty() ? "ar/top" : arcade_steps_.front();
    resume_focus_ = -1;
    arcade_pos_ = arcade_steps_.empty() ? -1 : 0;
    if (concept_pod_) { // the pod race was started from Game Status: back there (M)
        resume_page_ = "ar/status";
        arcade_pos_ = -1;
        concept_pod_ = 0;
    }
    request_race(std::move(r));
}

// Code-drawn parts of Concept's pages on top of the page engine's draw list.
void GameFlow::concept_draws(std::vector<ElementDraw>& out) const {
    if (!ctl_ || !ctl_->has_page() || !ctl_->page()) return;
    const std::string& page = ctl_->page_name_at(0);
    const MenuSet::PageRef ref = ctl_->page_at(0);
    if (!ref) return;
    const ConceptHooks& h = concept_hooks();
    if (category_page(page)) {
        const bool dirt = h.course_dirt && h.course_dirt(course_);
        // The heading: "CAR SELECTION", or on a dirt course "RALLY CAR SELECTION" (widgets category_road /
        // category_dirt): only the course's one.
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&](const ElementDraw& d) {
                                     return d.element && d.element->widget_class &&
                                            *d.element->widget_class == (dirt ? "category_road" : "category_dirt");
                                 }),
                  out.end());
        // The earlier releases' count under each button (widgets available_car_num_<category>).
        for (ElementDraw& d : out) {
            if (!d.element || !d.element->widget_class || d.element->widget_class->rfind("available_car_num_", 0) != 0) continue;
            const int cat = category_of(ref, d.element->widget_class->substr(18));
            if (cat >= 0 && h.category_cars) d.text = std::make_shared<const std::string>(std::to_string(h.category_cars(cat, course_)));
        }
        // A category without a car for this course shows the "question" art (it cannot be chosen).
        if (const Element* q = widget_named(ref.layout(), "question"); q && h.category_cars)
            for (int i = 0; i < int(ctl_->items().size()); ++i) {
                if (h.category_cars(i, course_) > 0) continue;
                SDL_FRect box;
                if (!ctl_->item_box(i, box)) continue;
                ElementDraw d;
                d.scene = ref.scene;
                d.element = q;
                d.has_box = true;
                d.box = {box.x + (box.w - q->w) * 0.5f, box.y + (box.h - q->h) * 0.5f, q->w, q->h};
                d.z = q->z + 20.0f;
                out.push_back(d);
            }
        std::stable_sort(out.begin(), out.end(), [](const ElementDraw& x, const ElementDraw& y) { return x.z < y.z; });
    }
    if (page == "ar/select_license" && h.license_tests) {
        // Course License: each test's target times and car name (the focused test's layer holds its widgets
        // "LC00000<n>_gold" and "car_0<n>"), and the medal won so far over its button (LicenseButton).
        const std::vector<ConceptHooks::Test> tests = h.license_tests();
        auto test_of = [&](const std::string& name, size_t digit) -> const ConceptHooks::Test* {
            if (name.size() <= digit || name[digit] < '0' || name[digit] > '9') return nullptr;
            const size_t n = size_t(name[digit] - '0');
            return n < tests.size() ? &tests[n] : nullptr;
        };
        for (ElementDraw& d : out) {
            if (!d.element || !d.element->widget_class) continue;
            const std::string& cls = *d.element->widget_class;
            const int medal = cls == "license_time_gold" ? 0 : cls == "license_time_silver" ? 1 : cls == "license_time_bronze" ? 2 : -1;
            if (medal >= 0) {
                if (const ConceptHooks::Test* t = test_of(d.element->name, 7)) d.text = std::make_shared<const std::string>(t->times[size_t(medal)]);
            } else if (cls == "car_name_license") {
                if (const ConceptHooks::Test* t = test_of(d.element->name, 5)) d.text = std::make_shared<const std::string>(t->car);
            }
        }
        const Layer* hidden = layer_named(ref.layout(), "hidden");
        static const char* const kMedal[3] = {"item_gold", "item_silver", "item_bronze"};
        for (int i = 0; i < int(ctl_->items().size()) && i < int(tests.size()) && ctl_->depth() == 0; ++i) {
            const int m = tests[size_t(i)].medal;
            const Element* art = m >= 0 && m <= 2 ? element_named(hidden, kMedal[m]) : nullptr;
            SDL_FRect box, clip;
            if (!art || !ctl_->item_box(i, box, &clip)) continue;
            ElementDraw d;
            d.scene = ref.scene;
            d.element = art;
            d.has_box = true;
            d.box = {box.x + (box.w - art->w) * 0.5f, box.y + (box.h - art->h) * 0.5f, art->w, art->h};
            d.has_clip = true;
            d.clip = clip;
            d.z = art->z + 1000.0f;
            out.push_back(d);
        }
    }
    if (page == "ar/show_room") {
        // The show room's description line: the car's own text, kept with the page's hidden art ("<car>_doc"); a car
        // without one has no line.
        const Layer* hidden = layer_named(ref.layout(), "hidden");
        const std::string image = h.key_image ? h.key_image(concept_show_key_) : std::string();
        for (ElementDraw& d : out) {
            if (!d.element || !d.element->widget_class || *d.element->widget_class != "car_description") continue;
            const Element* doc = element_named(hidden, image + "_doc");
            if (!doc) doc = element_named(hidden, "_" + image + "_doc");
            d.text = std::make_shared<const std::string>(doc && doc->text ? doc->text->text : std::string());
        }
    }
    if (page == "ar/status") {
        const Layer* hidden = layer_named(ref.layout(), "hidden");
        // The map under the window: everything on the page moves with the scroll (not a page over it: the pod race's
        // page, its own layout).
        const float tx = -concept_map_x_, ty = -concept_map_y_;
        for (ElementDraw& d : out) {
            if (!d.element || d.scene != ref.scene) continue;
            d.dx += tx, d.dy += ty;
            if (d.has_clip) d.clip.x += tx, d.clip.y += ty;
            // The completion figure after "Achieved :" (widget class achievement_ratio).
            if (d.element->widget_class && *d.element->widget_class == "achievement_ratio" && h.completion_text)
                d.text = std::make_shared<const std::string>(h.completion_text());
        }
        auto put = [&](const Element* art, const Element& at, float z, float alpha) {
            if (!art) return;
            ElementDraw d;
            d.scene = ref.scene;
            d.element = art;
            d.has_box = true;
            d.box = {at.x + (at.w - art->w) * 0.5f + tx, at.y + (at.h - art->h) * 0.5f + ty, art->w, art->h};
            d.alpha = alpha;
            d.z = z;
            out.push_back(d);
        };
        // The nodes (widgets "C<nn><L|N|H>_stat", the factory 0x115E68): the course license's medal; a first-place
        // badge once the course is won at some level (N) and at Professional or Ace (H). Nothing before that.
        for (const Layer& l : ref.layout().layers)
            for (const Element& w : l.widgets) {
                if (!w.widget_class || *w.widget_class != "show_status" || w.name.size() < 4 || w.name[0] != 'C') continue;
                if (w.name[1] < '0' || w.name[1] > '9' || w.name[2] < '0' || w.name[2] > '9') continue;
                const int course = (w.name[1] - '0') * 10 + (w.name[2] - '0');
                const char kind = w.name[3];
                const Element* art = nullptr;
                if (kind == 'L') {
                    static const char* const kMedal[3] = {"license_gold", "license_silver", "license_bronze"};
                    const int m = h.license_medal ? h.license_medal(course) : -1;
                    if (m >= 0 && m <= 2) art = element_named(hidden, kMedal[m]);
                } else if ((kind == 'N' || kind == 'H') && h.course_won && h.course_won(course, kind == 'H')) {
                    art = element_named(hidden, "rank_1");
                }
                put(art, w, w.z, 1.0f);
            }
        // The prize cars (StatusButton 0x11A250) and the presents: a key that is reached shows its car's picture, the
        // focused one pulsing; one that is not reached shows it in black, and so does a present's gift box (as the
        // original's page shows them; its draw for them was not read to the end).
        const int focus = ctl_->depth() == 0 ? ctl_->focus() : -1;
        const std::vector<MenuController::ItemState>& items = ctl_->items();
        auto reached = [&](const std::string& name) { return h.status_reached && h.status_reached(name); };
        auto present = [](const MblItem& item) { return item.action == "go_present" || item.name == "PODRACE"; };
        // (The 2001 Tokyo release's buttons carry their pictures themselves; the presents' boxes are their items' art.)
        for (ElementDraw& d : out) {
            if (!d.element || d.kind != ElementDraw::Kind::Element) continue;
            for (const MenuController::ItemState& it : items) {
                if (!it.flow || !it.element || (it.flow->action != "set_concept_car" && !present(*it.flow))) continue;
                bool own = d.element == it.element;
                for (const Element& alt : it.element->alt) own = own || d.element == &alt;
                if (own && !reached(it.flow->name)) d.brightness = 0;
            }
        }
        for (int i = 0; i < int(items.size()); ++i) {
            const MenuController::ItemState& it = items[size_t(i)];
            if (!it.flow || !it.element || it.flow->action != "set_concept_car" || !h.key_image) continue;
            const std::string image = h.key_image(it.flow->name);
            const Element* art = element_named(hidden, image);
            if (!art) art = element_named(hidden, "_" + image);
            const float pulse = 0.75f + 0.25f * std::cos(float(concept_status_frames_) * 0.1f);
            put(art, *it.element, it.element->z + 20.0f, i == focus ? pulse : 1.0f);
            if (art && !reached(it.flow->name)) out.back().brightness = 0;
        }
        // The pointer on the focused button (the list's cursor: hidden art "pointer"), its tip at the button's centre.
        if (const MenuController::ItemState* f = ctl_->depth() == 0 ? ctl_->focused() : nullptr; f && f->element)
            if (const Element* ptr = element_named(hidden, "pointer")) {
                ElementDraw d;
                d.scene = ref.scene;
                d.element = ptr;
                d.has_box = true;
                d.box = {f->element->x + f->element->w * 0.5f + tx, f->element->y + f->element->h * 0.5f - ptr->h + ty, ptr->w, ptr->h};
                d.z = ptr->z + 1000.0f;
                out.push_back(d);
            }
        std::stable_sort(out.begin(), out.end(), [](const ElementDraw& x, const ElementDraw& y) { return x.z < y.z; });
    }
    if (page == "ar/select_course" && !carousel_.entries.empty() && arcade_action_ == "single_race" && h.course_place) {
        // The strip's place badges (MiniCourseCursor): the best place at the chosen level on each course's icon.
        const Layer* hidden = layer_named(ref.layout(), "hidden");
        const Element* strip = element_named(hidden, "mini_courses");
        static const char* const kBadge[6] = {"item_1st", "item_2nd", "item_3rd", "item_4th", "item_5th", "item_6th"};
        for (size_t k = 0; strip && k < carousel_.entries.size(); ++k) {
            const Element* icon = element_named(hidden, carousel_.entries[k] + "_mini");
            const int place = h.course_place(difficulty_, carousel_.entries[k]);
            if (!icon || place < 0 || place >= 6) continue;
            const Element* badge = element_named(hidden, kBadge[place]);
            if (!badge) continue;
            const float x = carousel_.scroll + float(k) * (icon->w + 4.0f), cy = strip->y + strip->h * 0.5f;
            if (x > 640 || x + icon->w < 0) continue;
            ElementDraw d;
            d.scene = ref.scene;
            d.element = badge;
            d.has_box = true;
            d.box = {x + (icon->w - badge->w) * 0.5f, cy - badge->h * 0.5f, badge->w, badge->h};
            d.alpha = carousel_.strip_alpha;
            d.has_clip = carousel_strip_window(d.clip);
            d.z = icon->z + 0.6f;
            out.push_back(d);
        }
        std::stable_sort(out.begin(), out.end(), [](const ElementDraw& x, const ElementDraw& y) { return x.z < y.z; });
    }
}

} // namespace gt3r::menu::flow
