#include "menu/flow/game_flow.h"

#include "base/log.h"
#include "formats/db/gt_db.h"
#include "formats/sound/mads.h"
#include "menu/flow/gt_mode_hooks.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gt3r::menu::flow {

namespace {

// Items of the title page "root" in .mbl order; the original focuses the
// item of the mode it comes back from.
enum TitleItem {
    kTitleArcade = 0,
    kTitleGtMode = 1,
    kTitleReplay = 2,
    kTitleLoad = 3,
    kTitleSave = 4,
    kTitleFile = 5,
    kTitleOption = 6
};

constexpr int kSwitchFrames = 40; // Fadeout has darkened the screen to ~2 % by then; the next control loads

const Layer* find_layer(const Page& p, std::string_view name) {
    for (const Layer& l : p.layers)
        if (l.name == name) return &l;
    return nullptr;
}

const Element* find_element(const Layer* l, const std::string& name) {
    if (!l) return nullptr;
    for (const Element& e : l->elements)
        if (e.name == name) return &e;
    return nullptr;
}

bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

bool is_code_page(const std::string& p) {
    return p == "ar/select_course" || p == "ar/select_class" || p == "ar/select_car" || p == "ar/select_car_2p" ||
           p == "ar/select_compulsory";
}

bool is_car_page(const std::string& p) { return p == "ar/select_car" || p == "ar/select_car_2p"; }

// Ours: the Key Configuration page shows a "Controller Settings" button, in the middle of its
// panel (bace_7: 64, 28, 512 x 324), in place of the original's DualShock 2 table: these
// elements of the page (layer defaultLayer) and of its view on the top page (layer m5), the
// action rows, their labels and the 1P / 2P headings, are not drawn.
constexpr SDL_FRect kControllerSettingsBox{190, 177, 260, 26};
bool key_config_hidden(const std::string& element) {
    static const char* const kHidden[] = {"bace1_7", "bace2_7", "bace3_7", "bace4_7", "bace5_7", "item2_7",
                                          "item3_7", "item4_7", "item5_7", "item6_7", "item7_7", "item8_7",
                                          "item9_7", "item10_7", "p3",      "p4"};
    return std::find(std::begin(kHidden), std::end(kHidden), element) != std::end(kHidden);
}

} // namespace

// Menu BGM by page, as the page build picks it (prefix tests in this
// order; a later match wins). The game passes an index into its music
// table; the music.inf names here are matched by meaning.
std::string page_music(const std::string& set, const std::string& p) {
    // The option pages match none of the prefixes: no BGM (menus §10; the music list's
    // previews are the only music there).
    if (set == "option") return "";
    std::string m = "\x01"; // keep current
    if (starts(p, "cd/") || p == "00cmn/cd_ride_sure") m = "dealer01";
    else if (starts(p, "at/") || p == "00cmn/ws_fit_sure") m = "wash01";
    if (starts(p, "hm/")) m = "home01";
    if (starts(p, "lc/")) m = "license01";
    else if (starts(p, "r/") || p == "00cmn/race_info") m = "gorace01";
    else if (p == "replay_theater") m = "bgm:26"; // 0x1A, the "Are You Gonna Go My Way" remix, looping
    else if (starts(p, "ts/")) m = "tune01";
    else if (p == "map") m = "gtmenu01";
    else if (starts(p, "mt/")) m = "machtest01";
    else if (starts(p, "ar/")) m = "arcade01";
    else if (p == "root") m = "";
    // Presentations: streamed BGM cues, stopped on close.
    if (p == "license_get") m = "bgm:33";        // 0x21 "champ-2"
    else if (p == "trophy_get") m = "bgm:24";    // 0x18 "champ-3"
    else if (p == "car_get") m = "bgm:25:once";  // 0x19 "getacar", played once
    else if (p == "arcade_mini" || p == "gtmode_mini" || p == "consolation_prize") m = "bgm:23"; // 0x17 "jg05"
    return m;
}

int title_focus_after(std::string_view command) {
    if (command == "go_arcade_menu") return kTitleArcade;
    if (command == "go_replay_theater") return kTitleReplay;
    if (command == "go_load_game_menu") return kTitleLoad;
    if (command == "go_save_game_menu") return kTitleSave;
    if (command == "go_file_operation") return kTitleFile;
    if (command == "go_option") return kTitleOption;
    return kTitleArcade;
}

RacePageHook race_page_hook(std::string_view page, const std::vector<MblItem>& items) {
    auto has = [&](const char* action) {
        return std::any_of(items.begin(), items.end(), [&](const MblItem& it) { return it.action == action; });
    };
    if (page.substr(0, 6) == "lc/lc_" && page.find('/', 6) == std::string_view::npos)
        return has("go_license") ? RacePageHook::License : RacePageHook::None;
    // r/<class>/<series>
    const size_t a = page.find('/'), b = a == std::string_view::npos ? a : page.find('/', a + 1);
    if (page.substr(0, 2) != "r/" || b == std::string_view::npos || page.find('/', b + 1) != std::string_view::npos)
        return RacePageHook::None;
    if (page.size() >= 6 && page.substr(page.size() - 6) == "_entry") return RacePageHook::None;
    if (std::any_of(page.begin(), page.end(), [](char c) { return c >= 'A' && c <= 'Z'; })) return RacePageHook::None;
    if (has("set_race_code_champ")) return RacePageHook::ChampionshipSeries;
    return has("set_race_code") ? RacePageHook::Series : RacePageHook::None;
}

int race_page_focus(RacePageHook hook, std::string_view code, int race_type) {
    // The code's last characters as digits; a character the code is too short for counts as
    // a negative digit, so the index is then 0.
    auto digit = [&](size_t from_end) -> int {
        if (code.size() < from_end) return -1000;
        return int(static_cast<signed char>(code[code.size() - from_end])) - '0';
    };
    switch (hook) {
    case RacePageHook::License: {
        const int i = digit(1) - 1;
        return i >= 0 && i <= 7 ? i : 0;
    }
    case RacePageHook::Series: return std::max(0, digit(1));
    case RacePageHook::ChampionshipSeries: {
        int i = digit(2) * 10 + digit(1);
        if (race_type != 2) ++i; // a single race: after the championship item
        return std::max(0, i);
    }
    default: return -1;
    }
}

GameFlow::GameFlow(FileReader reader, std::string lang) : reader_(std::move(reader)), lang_(std::move(lang)) {}

MenuSet& GameFlow::load_set(const std::string& name) {
    auto& s = sets_[name];
    if (!s) s = std::make_unique<MenuSet>(reader_, name, lang_);
    return *s;
}

void GameFlow::log(std::string msg) {
    LOG_INFO("menu flow: %s", msg.c_str());
    log_.push_back(std::move(msg));
}

void GameFlow::start() {
    title_focus_ = title_focus_after({}); // boot: Arcade
    open_control("gt_mode", "root", title_focus_);
}

bool GameFlow::start_presentations() {
    standalone_shows_ = true;
    exited_ = false;
    if (show_presentation()) return true;
    exited_ = true;
    return false;
}

void GameFlow::start(const std::string& set, const std::string& page) {
    exited_ = false;
    open_control(set, page, -1);
}

void GameFlow::start_save_game() {
    exited_ = false;
    standalone_save_ = true;
    open_save_page(SavePage::Mode::SaveGame, Target{}, Target{}, false);
}

void GameFlow::open_control(const std::string& set, const std::string& page, int focus) {
    MenuSet& s = load_set(set);
    if (control_ != set || !ctl_) {
        ctl_ = std::make_unique<MenuController>(s);
        set_ = &s;
        control_ = set;
    }
    // The control's initial focus hooks (menus §7.1, menus §4.2). The title always opens on the
    // control's current title focus, however it is reached.
    // The race series and licence pages open on the item of the stored race code (menus §4.2).
    std::string fpage = page;
    const MenuSet* gt = set == "gt_mode" ? &s : nullptr;
    ctl_->initial_focus = [this, gt, fpage, focus](const std::string& p) -> int {
        if (p == "ar/top") return arcade_game_;
        if (p == "ar/level") return std::min(difficulty_, 2);
        if (gt && p == "root") return title_focus_;
        if (gt)
            if (int f = race_page_focus_now(*gt, p); f >= 0) return f;
        return p == fpage ? focus : -1;
    };
    if (set == "gt_mode" || set == "setting" || set == "records" || presenting_) {
        const GtModeHooks& h = gt_mode_hooks();
        if (h.attach && !gt_attached_ && set == "gt_mode") {
            gt_attached_ = true;
            h.attach(reader_, lang_);
        }
        ctl_->intercept = h.intercept;
        ctl_->enabled = h.enabled;
        ctl_->listed = h.listed;
        ctl_->initial_item = h.initial_item;
    } else if (set == "arcade") {
        // Arcade pages the game decides on (ar/goodies "Goal", ar/select_ending items).
        const GtModeHooks& h = gt_mode_hooks();
        ctl_->intercept = h.intercept;
        ctl_->enabled = h.enabled;
    }
    if (set == "option")
        // The option pages' multi items show the stored values.
        for (const GameOption& o : game_options()) ctl_->set_choice(o.page, o.multi, option_index(options_, o));
    carousel_ = {};
    carousel_jump_ = -1;
    ++generation_;
    ctl_->open(page);
    if (is_code_page(page)) build_carousel(page);
    two_page_ = set == "arcade" && page == "ar/select_car_2p";
    if (two_page_) two_player_open();
}

int GameFlow::race_page_focus_now(const MenuSet& set, const std::string& page) {
    const MenuSet::PageRef ref = set.page(page);
    if (!ref.flow) return -1;
    const RacePageHook hook = race_page_hook(page, ref.flow->items);
    if (hook == RacePageHook::None) return -1;
    const int f = race_page_focus(hook, race_code_, race_type_);
    race_code_.clear(); // every call of a race page's hook clears the stored code
    return f;
}

void GameFlow::switch_control(const std::string& set, const std::string& page, int focus) {
    // Leaving a menu control: the menu's transition type (default 0 = Fadeout) over the last
    // frame while the next one loads; the next control appears as a cut (menus §6).
    transition_ = TransitionType::Fadeout;
    switch_frames_ = kSwitchFrames;
    next_set_ = set;
    next_page_ = page;
    next_focus_ = focus;
}

void GameFlow::request_race(GameRequest r) {
    // The menu control ends (Fadeout, the default type 0), then the race appears as a cut.
    transition_ = TransitionType::Fadeout;
    switch_frames_ = kSwitchFrames;
    next_set_.clear();
    pending_request_ = std::move(r);
}

void GameFlow::open_save_page(SavePage::Mode m, Target back, Target ok, bool transition, std::string title) {
    const GtModeHooks& h = gt_mode_hooks();
    SavePage::Backend b;
    b.list = h.save_list;
    b.run = h.save_run;
    save_page_.open(m, std::move(b), std::move(title));
    save_back_ = std::move(back);
    save_ok_ = std::move(ok);
    save_mode_ = true;
    name_mode_ = false;
    theater_mode_ = false;
    if (transition) switch_control("memorycard", "file_operation", -1);
    else open_control("memorycard", "file_operation", -1);
}

void GameFlow::switch_control_now(const Target& t) {
    if (t.set == "gt_mode" && t.page == "root") {
        switch_control(t.set, t.page, t.focus);
        return;
    }
    open_control(t.set, t.page, t.focus);
    if (const GtModeHooks& h = gt_mode_hooks(); h.take_popup && ctl_ && t.set == "setting")
        if (std::string p = h.take_popup(); !p.empty()) ctl_->popup(p);
}

void GameFlow::start_attract_demo() {
    const GtModeHooks& h = gt_mode_hooks();
    std::optional<GameRequest> r = h.attract_demo ? h.attract_demo() : std::nullopt;
    if (!r) {
        in_attract_ = false;
        return;
    }
    title_focus_ = title_focus_after({}); // the title idled without a command: back on Arcade
    resume_set_ = "gt_mode";
    resume_page_ = "root";
    resume_focus_ = title_focus_;
    request_race(std::move(*r));
}

void GameFlow::game_finished() {
    if (!waiting_game_) return;
    waiting_game_ = false;
    std::optional<GameResult> result = take_game_result();
    if (theater_resume_) {
        // Replay Theater / Analyzer: back to the page (no results, nothing to award).
        theater_resume_ = false;
        open_control(resume_set_, resume_page_, resume_focus_);
        return;
    }
    if (in_attract_) {
        // Attract mode: up to five demo races while nobody interrupts (a demo that
        // ends on its own reports finished), then the opening movie, then the title.
        if (attract_movie_ || !result || !result->finished) {
            in_attract_ = attract_movie_ = false;
            open_control("gt_mode", "root", title_focus_);
            return;
        }
        if (++attract_demos_ < 5) {
            start_attract_demo();
            if (in_attract_) return;
        } else {
            attract_movie_ = true;
            GameRequest m;
            m.kind = GameRequest::Kind::PlayMovie;
            m.mode = "movie";
            m.course = "stream/opening.pss";
            request_race(std::move(m));
            return;
        }
        open_control("gt_mode", "root", title_focus_);
        return;
    }
    if (resume_set_ == "arcade" && result)
        if (const GtModeHooks& h = gt_mode_hooks(); h.on_arcade_result) {
            GameResult r = *result;
            if (r.course.empty()) r.course = course_;
            h.on_arcade_result(r);
            // Presentations it queued (the Single Race cup), then the arcade menu.
            after_game();
            return;
        }
    if (resume_set_ == "gt_mode") {
        const GtModeHooks& h = gt_mode_hooks();
        if (result && h.on_result)
            for (std::string& m : h.on_result(*result)) log(std::move(m));
        after_game();
        return;
    }
    open_control(resume_set_, resume_page_, resume_focus_);
}

bool GameFlow::show_presentation() {
    const GtModeHooks& h = gt_mode_hooks();
    if (!h.presentation) return false;
    std::optional<GtModeHooks::Presentation> p = h.presentation();
    if (!p) return false;
    presenting_ = true;
    control_.clear(); // force a new controller for the presentation's set
    open_control(p->set, p->page, -1);
    if (ctl_)
        for (const std::string& l : p->layers) ctl_->set_layer(l, 1);
    if (ctl_ && p->take_controls) ctl_->take_control();
    return true;
}

void GameFlow::after_game() {
    if (starts(music_, "bgm:")) { // a presentation's music stops when it closes
        music_.clear();
        music_request_ = std::string();
    }
    if (show_presentation()) return;
    presenting_ = false;
    if (standalone_shows_) {
        exited_ = true;
        return;
    }
    const GtModeHooks& h = gt_mode_hooks();
    if (h.take_movie)
        if (std::string mv = h.take_movie(); !mv.empty()) {
            GameRequest r;
            r.kind = GameRequest::Kind::PlayMovie;
            r.mode = "movie";
            r.course = mv;
            request_race(std::move(r));
            return;
        }
    if (h.next_request)
        if (std::optional<GameRequest> next = h.next_request()) {
            // Championship: the next round starts without going back to the menu.
            request_race(std::move(*next));
            return;
        }
    open_control(resume_set_, resume_page_, resume_focus_);
}

std::vector<std::string> GameFlow::take_sounds() {
    std::vector<std::string> out = std::move(sounds_);
    sounds_.clear();
    if (ctl_)
        for (std::string& s : ctl_->take_sounds()) out.push_back(std::move(s));
    return out;
}

std::optional<std::string> GameFlow::take_movie() { return std::exchange(movie_request_, std::nullopt); }

std::optional<std::string> GameFlow::take_music() { return std::exchange(music_request_, std::nullopt); }

std::optional<GameRequest> GameFlow::take_request() { return std::exchange(request_, std::nullopt); }
std::optional<TransitionType> GameFlow::take_transition() { return std::exchange(transition_, std::nullopt); }
std::vector<std::string> GameFlow::take_log() { return std::exchange(log_, {}); }

bool GameFlow::tap(float x, float y) {
    if (!ctl_ || switching() || waiting_game_ || ctl_->busy() || ctl_->ignoring_control()) return false;
    if (control_ == "memorycard" && save_mode_ && !name_mode_ && !theater_mode_) { // the save page (code driven)
        MenuKeys press = 0;
        if (!save_page_.tap(ctl_->page(), x, y, press, sounds_)) return false;
        injected_ |= press;
        return true;
    }
    if (SDL_FRect area; music_list_active() && music_list_area(area)) {
        const bool used = music_page_.tap(area, x, y, sounds_);
        if (std::optional<int> p = music_page_.take_preview()) preview_request_ = p;
        return used;
    }
    if (gt_active())
        if (const GtModeHooks& h = gt_mode_hooks(); h.tap)
            if (int r = h.tap(*ctl_, x, y)) {
                if (r == 2) injected_ = key_bit(MenuKey::Confirm);
                return true;
            }
    if (key_config_page()) {
        // Our Controller Settings button: as Confirm (the page has no items).
        const SDL_FRect& b = kControllerSettingsBox;
        if (x >= b.x && x < b.x + b.w && y >= b.y - 6 && y < b.y + b.h + 6) {
            injected_ = key_bit(MenuKey::Confirm);
            return true;
        }
        return false;
    }
    if (ctl_->tap(x, y)) return true;
    if (!carousel_.entries.empty() && ctl_->items().empty()) {
        if (int k = carousel_icon_at(x, y); k >= 0) {
            if (k == carousel_.index) injected_ = key_bit(MenuKey::Confirm);
            else carousel_jump_ = k;
            return true;
        }
        injected_ = key_bit(x < 640.0f / 3 ? MenuKey::Left : x > 640.0f * 2 / 3 ? MenuKey::Right : MenuKey::Confirm);
        return true;
    }
    return false;
}

bool GameFlow::hover(float x, float y) {
    if (!ctl_ || switching() || waiting_game_ || ctl_->busy() || ctl_->ignoring_control()) return false;
    if (control_ == "memorycard" && save_mode_ && !name_mode_ && !theater_mode_)
        return save_page_.hover(ctl_->page(), x, y, sounds_);
    if (SDL_FRect area; music_list_active() && music_list_area(area)) {
        const bool used = music_page_.hover(area, x, y, sounds_);
        if (std::optional<int> p = music_page_.take_preview()) preview_request_ = p;
        return used;
    }
    if (gt_active())
        if (const GtModeHooks& h = gt_mode_hooks(); h.hover)
            if (int r = h.hover(*ctl_, x, y)) {
                if (r == 2) sounds_.push_back("select");
                return true;
            }
    // (Not the carousels' strip: it scrolls to centre the chosen icon.)
    return ctl_->hover(x, y);
}

void GameFlow::tick(MenuKeys held) {
    if (waiting_game_) return;
    // A tap's key: pressed for this tick only (the next tick releases it).
    held |= std::exchange(injected_, MenuKeys(0));
    if (switch_frames_ > 0) {
        if (--switch_frames_ == 0) {
            if (pending_request_) {
                request_ = std::move(pending_request_);
                pending_request_.reset();
                waiting_game_ = true;
                music_ = "";
                music_request_ = std::string(); // the race plays its own music
                movie_ = "";
                movie_request_ = std::string();
                ctl_.reset();
                control_.clear();
            } else {
                open_control(next_set_, next_page_, next_focus_);
            }
        }
        return;
    }
    if (!ctl_) return;
    held_ = held;
    if (control_ == "memorycard" && theater_mode_) {
        theater_tick(held);
        return;
    }
    if (control_ == "logger") {
        logger_tick(held);
        return;
    }
    if (control_ == "memorycard") {
        // The slot page is code driven: the controller only animates the page's layers.
        if (name_mode_) {
            // Edit file name: the name entry page's text box (TextBox: a key's character goes
            // in at the cursor, OK confirms). Triangle / Square: Back Space (never leaves the
            // page); Start: to the OK row; L1 / R1: the cursor.
            const MenuKeys pressed = held & MenuKeys(~held_name_);
            held_name_ = held;
            if ((pressed & key_bit(MenuKey::Cancel)) && !ctl_->busy() && name_box_.back_space())
                sounds_.push_back("cursor");
            if ((pressed & key_bit(MenuKey::Start)) && !ctl_->busy()) ctl_->set_focus("free_command");
            held &= MenuKeys(~(key_bit(MenuKey::Cancel) | key_bit(MenuKey::Start)));
            const bool cancel = false;
            ctl_->tick(held);
            if (const MenuKeys f = ctl_->fired(); f & (key_bit(MenuKey::L1) | key_bit(MenuKey::R1)))
                if (name_box_.move((f & key_bit(MenuKey::L1)) ? -1 : 1)) sounds_.push_back("cursor");
            for (const MenuEvent& e : ctl_->take_events()) {
                if (e.kind == MenuEvent::Kind::Action && e.action == "input_text") {
                    // The key's character: its layout element's text ("A", "chr1" = "[", ...;
                    // the space key has none).
                    if (ctl_->page())
                        for (const Element& it : ctl_->page().layout().items)
                            if (it.name == e.item) {
                                if (name_box_.insert(it.text ? it.text->text : std::string())) sounds_.push_back("cursor");
                                break;
                            }
                } else if (e.kind == MenuEvent::Kind::Exit || e.kind == MenuEvent::Kind::Finish) {
                    name_mode_ = false;
                    open_control("memorycard", "file_operation", -1);
                    save_page_.name_done(!cancel, name_box_.text);
                    return;
                }
            }
            return;
        }
        // The save page is code driven: the controller only animates the layers.
        ctl_->tick(0);
        if (!save_mode_) return;
        int r = save_page_.tick(held, sounds_);
        if (save_page_.wants_name()) {
            name_mode_ = true;
            name_box_.reset(save_page_.name_start(), size_t(SavePage::kNameMax));
            held_name_ = held;
            open_control("memorycard", "name_entry", -1);
            return;
        }
        if (r != 0) {
            // Back where the page was opened from (a standalone page, e.g. the race's
            // championship Save game: the screen closes); the settings menu gets its popup
            // (load_warning) after a load.
            save_mode_ = false;
            if (const GtModeHooks& h = gt_mode_hooks(); h.save_closed) h.save_closed();
            if (standalone_save_) {
                exited_ = true;
                return;
            }
            const Target& t = r > 0 ? save_ok_ : save_back_;
            if (t.set == "setting") switch_control_now(t);
            else switch_control(t.set, t.page, t.focus);
        }
        return;
    }
    // GT Mode map: the directions drive the pointer (game code), not the list.
    if (control_ == "gt_mode" && ctl_->has_page() && ctl_->depth() == 0 && ctl_->page_name() == "map")
        if (const GtModeHooks& h = gt_mode_hooks(); h.map_input) {
            const MenuKeys dirs =
                key_bit(MenuKey::Up) | key_bit(MenuKey::Down) | key_bit(MenuKey::Left) | key_bit(MenuKey::Right);
            if (!ctl_->busy()) h.map_input(*ctl_, held);
            held &= MenuKeys(~dirs);
        }
    // Record name entry (presentation page): Triangle / Square = Back Space only, Start
    // jumps to the OK row (the original's controls; only OK ends the page). L1 / R1 move the
    // text box's cursor (the GT-mode bindings' page frame).
    if (ctl_->has_page() && ctl_->page_name() == "name_entry") {
        const MenuKeys pressed = held & MenuKeys(~held_name_);
        held_name_ = held;
        if (!ctl_->busy()) {
            if (pressed & key_bit(MenuKey::Cancel))
                if (const GtModeHooks& h = gt_mode_hooks(); h.name_backspace && h.name_backspace()) sounds_.push_back("cursor");
            if (pressed & key_bit(MenuKey::Start)) ctl_->set_focus("free_command");
        }
        held &= MenuKeys(~(key_bit(MenuKey::Cancel) | key_bit(MenuKey::Start)));
    }
    ctl_->tick(held);
    // Options, the music list (the page has no items: its list takes the keys; Cancel is the
    // page's, handled with its back action below).
    if (music_list_active()) {
        music_page_.tick(ctl_->fired(), ctl_->pressed(), sounds_);
        if (std::optional<int> p = music_page_.take_preview()) preview_request_ = p;
    }
    // Options, 2P battle page: Left / Right on the laps slider change the laps (1..99).
    if (control_ == "option" && options_ && ctl_->has_page() && ctl_->page_name() == "split/split" && !ctl_->busy()) {
        int f = ctl_->focus();
        const auto& items = ctl_->items();
        const std::string name = f >= 0 && f < int(items.size()) && items[size_t(f)].element
                                     ? items[size_t(f)].element->name
                                     : std::string();
        MenuKeys k = ctl_->fired();
        int d = (k & key_bit(MenuKey::Right)) ? 1 : (k & key_bit(MenuKey::Left)) ? -1 : 0;
        if (d && (name == "slider_split_race_laps" || name == "slider_split_handicap")) {
            // Laps 1..99; handicap -150..150 m in steps of 5.
            if (name == "slider_split_race_laps")
                options_->set("options", "split_laps", std::to_string(std::clamp(option_laps(options_) + d, 1, 99)));
            else
                options_->set("options", "split_handicap",
                              std::to_string(std::clamp(option_handicap(options_) + 5 * d, -150, 150)));
            sounds_.push_back("cursor");
            if (options_changed_) options_changed_();
        }
    }
    // Title idle timer: attract mode after the configured idle time.
    if (control_ == "gt_mode" && ctl_->has_page() && ctl_->page_name() == "root" && ctl_->depth() == 0 &&
        !ctl_->busy()) {
        const GtModeHooks& h = gt_mode_hooks();
        int limit = h.attract_idle_frames ? h.attract_idle_frames() : 0;
        idle_frames_ = held ? 0 : idle_frames_ + 1;
        if (limit > 0 && idle_frames_ > limit && h.attract_demo) {
            idle_frames_ = 0;
            in_attract_ = true;
            attract_demos_ = 0;
            attract_movie_ = false;
            start_attract_demo();
            return;
        }
    } else {
        idle_frames_ = 0;
    }
    if (gt_active())
        if (const GtModeHooks& h = gt_mode_hooks(); h.on_tick)
            if (std::string se = h.on_tick(*ctl_); !se.empty()) sounds_.push_back(std::move(se));
    if (gt_active())
        if (const GtModeHooks& h = gt_mode_hooks(); h.on_frame && !ctl_->busy())
            if (std::string se = h.on_frame(*ctl_); !se.empty()) sounds_.push_back(std::move(se));
    // Code-drawn pickers: Left/Right move along the entries (the class list wraps, the
    // others stop at the ends with "disable").
    if (two_page_) {
        if (!ctl_->busy()) two_player_frame();
    } else if (!carousel_.entries.empty()) {
        int move = 0, jumped_from = -1;
        if (!ctl_->busy()) {
            MenuKeys f = ctl_->fired();
            int n = int(carousel_.entries.size());
            int d = (f & key_bit(MenuKey::Right)) ? 1 : (f & key_bit(MenuKey::Left)) ? -1 : 0;
            if (d && carousel_.step > 1) {
                // AUTOMATIC / MANUAL or RACING / DRIFT (clamped).
                int& v = carousel_.step == 2 ? transmission_ : tire_mode_;
                int next = v + d;
                if (next < 0 || next > 1) sounds_.push_back("disable");
                else {
                    v = next;
                    sounds_.push_back("cursor");
                }
                d = 0;
            }
            // A tapped icon of the strip: straight there (the cross moves one step's way).
            if (int k = std::exchange(carousel_jump_, -1); k >= 0 && k < n && k != carousel_.index && !d) {
                jumped_from = carousel_.index;
                move = k > carousel_.index ? 1 : -1;
                carousel_.index = k;
                sounds_.push_back("select");
            }
            if (d) {
                int next = carousel_.index + d;
                if (carousel_.wrap) next = (next + n) % n;
                if (next < 0 || next >= n || n < 2) {
                    sounds_.push_back("disable");
                } else {
                    carousel_.index = next;
                    move = d;
                    sounds_.push_back("select"); // the arcade pickers play "select" on a move (as the game does)
                }
            }
        }
        // Car page: Up / Down choose the paint (clamped; a new car starts at its first).
        if (move) car_paint_ = 0;
        if (is_car_page(carousel_.page) && carousel_.step == 1 && !ctl_->busy())
            if (const GtModeHooks& h = gt_mode_hooks(); h.arcade_colors) {
                MenuKeys f = ctl_->fired();
                int d = (f & key_bit(MenuKey::Down)) ? 1 : (f & key_bit(MenuKey::Up)) ? -1 : 0;
                if (d) {
                    int n = int(h.arcade_colors(carousel_.entries[size_t(carousel_.index)]).size());
                    int next = car_paint_ + d;
                    if (next < 0 || next >= n) {
                        sounds_.push_back("disable");
                    } else {
                        car_paint_ = next;
                        sounds_.push_back("cursor");
                    }
                }
            }
        animate_carousel(move, jumped_from);
    }
    const u32 gen = generation_;
    for (const MenuEvent& e : ctl_->take_events()) {
        handle(e);
        // Stop once a handler moved on to another page or control.
        if (switch_frames_ > 0 || !ctl_ || generation_ != gen) break;
    }
}

void GameFlow::handle(const MenuEvent& e) {
    if (e.kind == MenuEvent::Kind::PageChanged && ctl_ && ctl_->depth() == 0) {
        std::string m = page_music(control_, e.page);
        if (m != "\x01" && m != music_) {
            music_ = m;
            music_request_ = m;
        }
        // The title page runs over the looping title movie.
        std::string mv = control_ == "gt_mode" && (e.page == "root" || e.page == "replay_theater")
                             ? "stream/title_full.pss"
                         : control_ == "arcade" && e.page == "ar/top"   ? "stream/arcade_menu_full.pss"
                         : control_ == "arcade" && e.page == "ar/level" ? "stream/level_menu_full.pss"
                         : control_ == "arcade" && is_car_page(e.page)  ? "stream/menu_bg2.pss"
                                                                        : "";
        if (mv != movie_) {
            movie_ = mv;
            movie_request_ = mv;
        }
    }
    if (control_ == "gt_mode") {
        bool consumed = false;
        if (const GtModeHooks& h = gt_mode_hooks(); h.on_event) {
            std::vector<std::string> lines;
            consumed = h.on_event(e, lines);
            for (std::string& m : lines) log(std::move(m));
        }
        if (consumed && e.kind == MenuEvent::Kind::Finish) open_control("gt_mode", e.page, e.child);
        else handle_gt(e);
    } else if (presenting_) {
        if (const GtModeHooks& h = gt_mode_hooks(); h.on_event) {
            std::vector<std::string> lines;
            h.on_event(e, lines);
            for (std::string& m : lines) log(std::move(m));
        }
        // A presentation page ends with its OK button (kind 3, no back page).
        if (e.kind == MenuEvent::Kind::Exit || e.kind == MenuEvent::Kind::Finish) after_game();
    } else if (control_ == "setting" || control_ == "records") {
        // Load / Save Settings: the save page over the car's setting files.
        if (control_ == "setting" && e.kind == MenuEvent::Kind::Finish && (e.action == "go_load" || e.action == "go_save"))
            if (gt_mode_hooks().save_list) {
                Target t{"setting", e.page, e.child};
                open_save_page(e.action == "go_load" ? SavePage::Mode::LoadSettings : SavePage::Mode::SaveSettings, t, t,
                               false);
                return;
            }
        bool consumed = false;
        if (const GtModeHooks& h = gt_mode_hooks(); h.on_event) {
            std::vector<std::string> lines;
            consumed = h.on_event(e, lines);
            for (std::string& m : lines) log(std::move(m));
        }
        if (e.kind == MenuEvent::Kind::Exit) exited_ = true;
        else if (e.kind == MenuEvent::Kind::Finish && !consumed) {
            log("not implemented: " + e.action + " (page " + e.page + ")");
            open_control(control_, e.page, e.child);
        } else if (e.kind == MenuEvent::Kind::Finish) {
            open_control(control_, e.page, e.child);
            if (const GtModeHooks& h = gt_mode_hooks(); h.take_popup && ctl_)
                if (std::string p = h.take_popup(); !p.empty()) ctl_->popup(p);
        }
    } else if (control_ == "arcade") handle_arcade(e);
    else if (control_ == "option") handle_option(e);
    else if (control_ == "language") handle_language(e);
}

// The language page: each item ("br", "fr", "it", "gr", "sp") sets its language; an unknown
// one sets British English.
void GameFlow::handle_language(const MenuEvent& e) {
    if (e.kind != MenuEvent::Kind::Finish || e.action != "set_lang") return;
    std::string code = e.item;
    for (char& c : code) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (code != "BR" && code != "FR" && code != "GR" && code != "IT" && code != "SP") code = "BR";
    language_choice_ = code;
    exited_ = true;
}

void GameFlow::handle_gt(const MenuEvent& e) {
    using K = MenuEvent::Kind;
    switch (e.kind) {
    case K::Action:
        // The stored race code (and its type: 4 race, 2 championship, 3 spot race).
        if (e.action == "set_race_code" || e.action == "set_race_code_spot") {
            race_code_ = e.item;
            race_type_ = e.action == "set_race_code" ? 4 : 3;
        } else if (e.action == "set_race_code_info") {
            // The race information: the item name without its last "_..." part.
            race_code_ = e.item.substr(0, std::min(e.item.size(), e.item.rfind('_')));
        } else if (e.action == "go_license" || e.action == "go_machine_test") {
            race_code_ = e.item;
        } else if (e.action == "set_race_code_champ") {
            // Championship: the item name with its last character advanced by one.
            race_code_ = e.item;
            if (!race_code_.empty()) ++race_code_.back();
            race_type_ = 2;
        } else if (e.action == "clear_race_code") {
            race_code_.clear();
        } else if (e.action == "go_free_run") {
            race_code_ = "ATAF0001";
        }
        break;
    case K::Finish: {
        const std::string& a = e.action;
        if (a == "go_arcade_menu") {
            title_focus_ = title_focus_after(a);
            arcade_game_ = 0;
            // The car pages start on the Options menu's transmission (Race: AT / MT).
            transmission_ = option_value(options_, "gear_auto") == "off" ? 1 : 0;
            switch_control("arcade", "ar/top", -1);
        } else if (a == "go_sitemap") {
            // Same control: the GT-mode phase starts on the map (a cut).
            open_control("gt_mode", "map", -1);
            // A championship suspended in the save resumes straight away (the original
            // resumes it at boot).
            if (const GtModeHooks& h = gt_mode_hooks(); h.resume_request)
                if (std::optional<GameRequest> r = h.resume_request()) {
                    resume_set_ = "gt_mode";
                    resume_page_ = "map";
                    resume_focus_ = -1;
                    request_race(std::move(*r));
                }
        } else if (a == "go_save_game_menu" || a == "go_load_game_menu") {
            // Title Save / Load: the save page, then back to the title.
            title_focus_ = title_focus_after(a);
            Target t{"gt_mode", "root", title_focus_};
            open_save_page(a == "go_save_game_menu" ? SavePage::Mode::SaveGame : SavePage::Mode::LoadGame, t, t, true);
        } else if (a == "go_file_operation") {
            // Title File Operation: the save list (rename, copy, delete, export, import).
            title_focus_ = title_focus_after(a);
            Target t{"gt_mode", "root", title_focus_};
            open_save_page(SavePage::Mode::Files, t, t, true);
        } else if (a == "go_save_in_home") {
            Target t{"gt_mode", e.page, e.child};
            open_save_page(SavePage::Mode::SaveGame, t, t, true);
        } else if (a == "go_trade") {
            // Trade: a friend's game (any save file) as the guest garage, then its car list
            // (hm/hm_gr_trade); without a load back to the home page.
            open_save_page(SavePage::Mode::LoadGuest, Target{"gt_mode", e.page, e.child},
                           Target{"gt_mode", "hm/hm_gr_trade", -1}, true, "Trade");
        } else if (a == "go_option") {
            title_focus_ = title_focus_after(a);
            switch_control("option", "option", 0);
        } else if (a == "go_race" || a == "go_license" || a == "go_machine_test" || a == "go_free_run" ||
                   a == "go_preview") {
            GameRequest r;
            r.mode = a;
            r.race_code = a == "go_machine_test" ? e.item : race_code_;
            r.page = e.page;
            // Back from the race: GT mode resumes at the page, race entry
            // pages ("..._entry") at their parent race list.
            resume_set_ = "gt_mode";
            resume_page_ = e.page;
            if (resume_page_.size() > 6 && resume_page_.compare(resume_page_.size() - 6, 6, "_entry") == 0) {
                if (const MenuSet::PageRef p = set_->page(e.page); p.flow && !p.flow->back.empty())
                    resume_page_ = p.flow->back;
            }
            resume_focus_ = -1;
            if (const GtModeHooks& h = gt_mode_hooks(); h.prepare_request) h.prepare_request(r);
            request_race(std::move(r));
        } else if (a == "go_replay_theater") {
            title_focus_ = title_focus_after(a);
            open_theater(TheaterPage::Mode::LoadReplay, TheaterFrom::Theater);
        } else if (a == "go_demonstration") {
            title_focus_ = title_focus_after(a);
            open_theater(TheaterPage::Mode::Demonstration, TheaterFrom::Theater);
        } else if (a == "go_logger") {
            // The Analyzer (menu/logger.mbl): its own control, page "top".
            title_focus_ = title_focus_after(a);
            logger_focus_ = 0; // REPLAY (the original's first focus)
            switch_control("logger", "top", -1);
        } else {
            // Memory card, replays, logger, trade, ...: not reimplemented yet.
            log("not implemented: " + (a.empty() ? e.item : a) + " (page " + e.page + ")");
            if (e.page == "root" || e.page == "replay_theater") {
                title_focus_ = e.page == "root" ? e.child : kTitleReplay;
                switch_control("gt_mode", "root", title_focus_);
            } else {
                open_control("gt_mode", e.page, e.child);
            }
        }
        break;
    }
    case K::BackAction:
        // Cancel on the Replay Theater page: the title focus goes to Replay Theater before
        // the title is built (back "root").
        if (e.action == "set_focus_replay_theater") title_focus_ = kTitleReplay;
        break;
    case K::Exit:
        if (e.page == "hm/hm_gr_trade") {
            // The trade session ends (its list page has no back page): the home page.
            open_control("gt_mode", "hm/hm", -1);
        } else if (e.action == "go_top" || e.page == "map") {
            // go_top: back from GT mode to the title, focus on "Gran Turismo mode".
            title_focus_ = kTitleGtMode;
            open_control("gt_mode", "root", title_focus_);
        } else {
            open_control("gt_mode", "root", title_focus_);
        }
        break;
    default: break;
    }
}

// Code-filled option widgets: the key configuration icons, the music list, the 2P
// battle sliders (laps, handicap) and their texts.
void GameFlow::option_draws(std::vector<ElementDraw>& out) const {
    if (!ctl_ || !ctl_->page()) return;
    const MenuSet::PageRef ref = ctl_->page();
    const Layer* hidden = find_layer(ref.layout(), "hidden");
    if (!hidden)
        if (MenuSet::PageRef top = set_->page("option"); top) hidden = find_layer(top.layout(), "hidden");
    if (!hidden) return;
    std::vector<ElementDraw> add;
    auto put = [&](const ElementDraw& w, const Element* e, SDL_FRect box, const std::string* text = nullptr) {
        if (!e) return;
        ElementDraw d;
        d.scene = w.scene;
        d.element = e;
        d.alpha = w.alpha;
        d.dx = w.dx;
        d.dy = w.dy;
        d.has_box = true;
        d.box = box;
        d.z = w.z + 0.01f;
        if (text) d.text = std::make_shared<const std::string>(*text);
        add.push_back(d);
    };
    auto marker = [&](const ElementDraw& w, float x) {
        ElementDraw d;
        d.kind = ElementDraw::Kind::Fill;
        d.has_box = true;
        d.box = {x - 2, w.element->y, 4, w.element->h};
        d.dx = w.dx;
        d.dy = w.dy;
        d.fill = {1.0f, 0.8f, 0.2f, w.alpha}; // the yellow value marker (reference capture)
        d.z = w.z + 0.02f;
        add.push_back(d);
    };
    // Widgets without parts of their own (the key and music lists) are not in the draw
    // list: take them from the shown layers.
    std::vector<ElementDraw> partless;
    const Page& pg = ref.layout();
    for (size_t li = 0; li < pg.layers.size(); ++li) {
        if (!ctl_->layer_visible(int(li))) continue;
        for (const Element& e : pg.layers[li].widgets)
            if (e.widget_class && (e.type & 0x7) == 0) {
                ElementDraw d;
                d.scene = ref.scene;
                d.element = &e;
                d.widget = true;
                d.z = e.z;
                partless.push_back(d);
            }
    }
    auto scan = [&](ElementDraw& w) {
        if (!w.element || w.kind != ElementDraw::Kind::Element) return;
        const Element& e = *w.element;
        const std::string cls = e.widget_class ? *e.widget_class : e.name;
        if (cls == "key_config_1" || cls == "key_config_1_display") {
            // Ours, in place of the DualShock 2 table (key_config_hidden): the Controller Settings
            // button (Confirm, or a tap), to our settings; on the top page, its view.
            ElementDraw b;
            b.kind = ElementDraw::Kind::Fill;
            b.has_box = true;
            b.box = kControllerSettingsBox;
            b.dx = w.dx;
            b.dy = w.dy;
            // The colour of the layer's title bar (title1_7): orange on the page, grey in the view.
            b.fill = cls == "key_config_1" ? SDL_FColor{0.82f, 0.16f, 0.0f, w.alpha}
                                           : SDL_FColor{0.29f, 0.29f, 0.29f, w.alpha};
            b.z = w.z + 0.005f; // under its text
            add.push_back(b);
            const std::string text = "Controller Settings";
            put(w, find_element(hidden, "default_item"), kControllerSettingsBox, &text); // white, sw_12
        } else if (cls == "song_edit" || cls == "song_list") {
            // The music list (menus §10.1): the page's own list, or the top page's view of it.
            const MusicListPage& list = cls == "song_edit" && music_page_.is_open() ? music_page_ : music_view_;
            list.draw(hidden, w, add);
        } else if (cls == "slider_split_race_laps") {
            marker(w, e.x + float(option_laps(options_) - 1) / 98.0f * e.w);
        } else if (cls == "slider_split_handicap") {
            marker(w, e.x + e.w * 0.5f + float(option_handicap(options_)) / 150.0f * e.w * 0.5f);
        } else if (cls == "split_race_laps") {
            w.text = std::make_shared<const std::string>(std::to_string(option_laps(options_)) + " laps");
        } else if (cls == "split_handicap") {
            w.text = std::make_shared<const std::string>(std::to_string(option_handicap(options_)) + " m");
        } else if (cls == "handicap_1p" || cls == "handicap_2p") {
            // The head start: the yellow line's length; the car icon at its end.
            int h = option_handicap(options_);
            bool ahead = (cls == "handicap_1p") == (h < 0);
            float len = ahead ? std::min(1.0f, std::abs(float(h)) / 150.0f) * e.w : 0; // |v| x w / 150
            const Element* car = find_element(hidden, cls == "handicap_1p" ? "handi_1p" : "handi_2p");
            if (car) put(w, car, {e.x + len, e.y + e.h * 0.5f - car->h * 0.5f + (cls == "handicap_1p" ? 4.0f : -4.0f), car->w, car->h});
            w.has_box = true;
            w.box = {e.x, e.y, len, e.h};
            if (len <= 0) w.alpha = 0;
        }
    };
    for (ElementDraw& w : out) scan(w);
    for (ElementDraw& w : partless) scan(w);
    for (ElementDraw& d : add) out.push_back(std::move(d));
}

const std::vector<MusicTrack>& GameFlow::race_tracks() {
    if (!race_tracks_loaded_) {
        race_tracks_loaded_ = true;
        if (std::optional<Bytes> inf = reader_("bgm/ads.inf")) try {
                const sound::MadsIndex m = sound::MadsIndex::parse(*inf);
                const size_t g = size_t(0x14 - 0x12); // the race songs' group (bgm id 0x14)
                if (g < m.groups.size())
                    for (const sound::MadsTrack& t : m.groups[g])
                        race_tracks_.push_back({db::id_hash(t.file), t.title, t.artist});
            } catch (const std::exception& ex) {
                LOG_WARN("menu flow: bgm/ads.inf: %s", ex.what());
            }
    }
    return race_tracks_;
}

race::RacePlaylist GameFlow::music_playlist() {
    std::vector<u64> ids;
    for (const MusicTrack& t : race_tracks()) ids.push_back(t.id);
    std::optional<std::string> stored = options_ ? options_->get("options", "music_playlist") : std::nullopt;
    const bool valid = stored && race::RacePlaylist::from_text(*stored);
    race::RacePlaylist p = race::RacePlaylist::stored_or_new(valid ? stored : std::nullopt, ids);
    // The options always hold a list (the original makes it with a new game): a new one is
    // kept, so the race and the next visit see the same one.
    if (!valid && options_) options_->set("options", "music_playlist", p.to_text());
    return p;
}

void GameFlow::open_music_list(bool editable) {
    MusicListPage& page = editable ? music_page_ : music_view_;
    if (!ctl_ || !ctl_->page()) return;
    const MenuSet::PageRef ref = ctl_->page();
    const Layer* hidden = find_layer(ref.layout(), "hidden");
    const char* cls = editable ? "song_edit" : "song_list";
    float view_h = MusicListPage::Layout{}.view_h;
    for (const Layer& l : ref.layout().layers)
        for (const Element& w : l.widgets)
            if (w.widget_class && *w.widget_class == cls) view_h = w.h;
    page.open(music_playlist(), race_tracks(), editable, MusicListPage::Layout::from(hidden, view_h));
    if (editable)
        if (std::optional<int> p = page.take_preview()) preview_request_ = p;
}

bool GameFlow::music_list_active() const {
    return control_ == "option" && music_page_.is_open() && ctl_ && ctl_->has_page() && ctl_->depth() == 0 &&
           ctl_->page_name() == "music/music" && !ctl_->busy();
}

bool GameFlow::music_list_area(SDL_FRect& area) const {
    if (!ctl_ || !ctl_->page()) return false;
    for (const Layer& l : ctl_->page().layout().layers)
        for (const Element& w : l.widgets)
            if (w.widget_class && *w.widget_class == "song_edit") {
                area = {w.x, w.y, w.w, w.h};
                return true;
            }
    return false;
}

void GameFlow::handle_option(const MenuEvent& e) {
    using K = MenuEvent::Kind;
    // The music list (menus §10.1): built with its page; its edit is kept by the page's Cancel
    // (back action stop_music, which also stops the music) unless a song is lifted.
    if (e.kind == K::PageChanged) {
        music_page_.close();
        if (e.page == "music/music") open_music_list(true);
        else if (e.page == "option") open_music_list(false);
        return;
    }
    if (e.kind == K::BackAction && e.action == "stop_music") {
        if (music_page_.is_open() && !music_page_.lifted() && options_) {
            race::RacePlaylist p = music_playlist();
            music_page_.commit(p);
            options_->set("options", "music_playlist", p.to_text());
            if (options_changed_) options_changed_();
        }
        music_page_.close();
        preview_request_ = -1;
        return;
    }
    if (e.kind == K::Exit) {
        title_focus_ = title_focus_after("go_option");
        switch_control("gt_mode", "root", title_focus_);
    } else if (e.kind == K::Choice) {
        const GameOption* o = find_game_option_by_item(e.page, e.item);
        if (o && options_) {
            set_option(*options_, *o, e.child);
            if (options_changed_) options_changed_();
        } else {
            log("option " + e.page + ": " + e.item + " = choice " + std::to_string(e.child));
        }
    } else if (e.kind == K::Action && e.action == "set_default") {
        // Default settings: every option back to its default.
        if (options_) {
            for (const GameOption& o : game_options()) {
                set_option(*options_, o, o.def);
                ctl_->set_choice(o.page, o.multi, o.def);
            }
            if (options_changed_) options_changed_();
        }
    } else if (e.kind == K::Action && e.action != "commit") log("option action: " + e.action);
    else if (e.kind == K::Finish) open_control("option", e.page, e.child);
    else if (e.kind == K::Confirm && e.page == "key/key") {
        // The page has no items: Confirm is our Controller Settings button.
        sounds_.push_back("ok");
        controller_settings_ = true;
    }
}

bool GameFlow::key_config_page() const {
    return control_ == "option" && ctl_ && ctl_->has_page() && ctl_->depth() == 0 && ctl_->page_name() == "key/key" &&
           !ctl_->busy();
}

void GameFlow::handle_arcade(const MenuEvent& e) {
    using K = MenuEvent::Kind;
    const std::string& page = e.page;
    // 2 Player car page: both pads are read by two_player_frame().
    if (page == "ar/select_car_2p" &&
        (e.kind == K::Confirm || e.kind == K::Cancel || e.kind == K::Exit || e.kind == K::Finish)) {
        if (e.kind == K::Exit || e.kind == K::Finish) ctl_->resume(); // the page stays (same freeze as the 1P car page)
        return;
    }
    if (page == "ar/top") {
        if (e.kind == K::Exit) {
            // cancel_select_game: back to the title.
            title_focus_ = title_focus_after("go_arcade_menu");
            switch_control("gt_mode", "root", title_focus_);
        } else if (e.kind == K::Finish) {
            arcade_game_ = std::max(0, e.child);
            arcade_action_ = e.action;
            const std::string& a = e.action;
            if (a == "single_race") arcade_steps_ = {"ar/level", "ar/select_course", "ar/select_class", "ar/select_car"};
            else if (a == "compulsory") arcade_steps_ = {"ar/select_compulsory"};
            else if (a == "time_attack") arcade_steps_ = {"ar/select_course", "ar/select_class", "ar/select_car"};
            else if (a == "two_player_battle") arcade_steps_ = {"ar/select_course", "ar/select_car_2p"};
            else if (a == "goodies") arcade_steps_ = {"ar/goodies"};
            else if (a == "i_link_battle") {
                // i.LINK Battle over IP: the online lobby (docs/dev/ONLINE.md) instead of the
                // original's ar/connect page; the arcade menu resumes when it closes.
                GameRequest m;
                m.kind = GameRequest::Kind::StartRace;
                m.mode = "online_battle";
                resume_set_ = "arcade";
                resume_page_ = "ar/top";
                resume_focus_ = -1;
                arcade_pos_ = -1;
                request_race(std::move(m));
                return;
            }
            else if (a == "load_guest_garage") {
                // Load Garage: a friend's game as the guest garage (class "guest garage").
                Target t{"arcade", "ar/top", -1};
                open_save_page(SavePage::Mode::LoadGuest, t, t, true);
                return;
            } else {
                log("not implemented: " + a);
                arcade_steps_.clear();
            }
            course_.clear();
            car_class_.clear();
            car_.clear();
            if (arcade_steps_.empty()) open_control("arcade", "ar/top", -1);
            else arcade_step(0);
        }
        return;
    }
    switch (e.kind) {
    case K::Finish:
        if (page == "ar/level") {
            // Professional: Hard confirmed while L1 and R1 are held (no unlock).
            const MenuKeys pro = key_bit(MenuKey::L1) | key_bit(MenuKey::R1);
            difficulty_ = e.child == 2 && (held_ & pro) == pro ? 3 : std::max(0, e.child);
        }
        if (page == "ar/select_ending") {
            // The arcade endings: the short / long ending movie, then back to the arcade top.
            GameRequest m;
            m.kind = GameRequest::Kind::PlayMovie;
            m.mode = "movie";
            m.course = e.action == "go_ending_normal" ? "stream/ending_long_full.pss" : "stream/ending_short_full.pss";
            resume_set_ = "arcade";
            resume_page_ = "ar/top";
            resume_focus_ = -1;
            arcade_pos_ = -1;
            request_race(std::move(m));
            return;
        }
        if (page == "ar/connect") {
            log("not implemented: " + e.action);
            open_control("arcade", "ar/top", -1);
            return;
        }
        arcade_step(arcade_pos_ + 1);
        break;
    case K::Confirm:
        // Car page: OK goes through AUTOMATIC / MANUAL and RACING / DRIFT (the latter
        // skipped on a dirt course and for garage cars) before the race.
        if (page == "ar/select_car" && !carousel_.entries.empty() && carousel_disabled().empty() &&
            carousel_.step < 3) {
            bool skip_tires = carousel_.step == 2 && (car_class_ == "home_garage" || car_class_ == "guest_garage" ||
                                                      (gt_mode_hooks().arcade_disabled &&
                                                       !gt_mode_hooks().arcade_disabled("ar/select_class", course_,
                                                                                        "class_c").empty()));
            if (!skip_tires) {
                ++carousel_.step;
                sounds_.push_back("next");
                break;
            }
        }
        if (!carousel_.entries.empty() && !carousel_disabled().empty()) {
            sounds_.push_back("disable");
            break;
        }
        if (!carousel_.entries.empty()) {
            const std::string& v = carousel_.entries[size_t(carousel_.index)];
            if (page == "ar/select_course") course_ = v;
            else if (page == "ar/select_class") car_class_ = v;
            else if (page == "ar/select_compulsory") course_ = v;
            else car_ = v;
            sounds_.push_back("next");
        }
        arcade_step(arcade_pos_ + 1);
        break;
    case K::Exit:
    case K::Cancel:
        if (page == "ar/select_car" && carousel_.step > 1) {
            // Back from AUTOMATIC / MANUAL or RACING / DRIFT: one step back on the same page.
            // The page's cancel (flag 0x100, no back page) ended the controller: resume it, or
            // no further input is taken (the reported freeze).
            --carousel_.step;
            ctl_->resume();
            sounds_.push_back("cancel");
            break;
        }
        arcade_back();
        break;
    case K::Action:
        if (e.action.rfind("i_link", 0) == 0) log("not implemented: " + e.action);
        break;
    default: break;
    }
}

void GameFlow::arcade_step(int step) {
    if (step >= int(arcade_steps_.size())) {
        if (arcade_action_ == "goodies" || arcade_action_ == "i_link_battle") open_control("arcade", "ar/top", -1);
        else arcade_start_race();
        return;
    }
    arcade_pos_ = step;
    open_control("arcade", arcade_steps_[size_t(step)], -1);
}

void GameFlow::arcade_back() {
    sounds_.push_back("cancel");
    if (arcade_pos_ <= 0) {
        arcade_pos_ = -1;
        open_control("arcade", "ar/top", -1);
    } else {
        arcade_step(arcade_pos_ - 1);
    }
}

void GameFlow::arcade_start_race() {
    GameRequest r;
    r.page = arcade_steps_.empty() ? "ar/top" : arcade_steps_.back();
    r.course = course_;
    r.car = car_;
    if (!r.car.empty() && r.car[0] == '_') r.car.erase(0, 1);
    r.car_class = car_class_;
    r.car_color = car_paint_;
    if (arcade_steps_.size() && arcade_steps_.back() == "ar/select_car") {
        r.transmission = transmission_;
        r.tire_mode = tire_mode_;
    }
    if (arcade_action_ == "two_player_battle") {
        const TwoPlayer& a = two_[0];
        const TwoPlayer& b = two_[1];
        auto car_of = [](const TwoPlayer& t) {
            std::string c = t.car >= 0 && t.car < int(t.cars.size()) ? t.cars[size_t(t.car)] : std::string();
            if (!c.empty() && c[0] == '_') c.erase(0, 1);
            return c;
        };
        r.car = car_of(a);
        r.car_class = two_player_classes()[size_t(a.cls)];
        r.car_color = a.paint;
        r.transmission = a.transmission;
        r.tire_mode = a.tire_mode;
        r.car2 = car_of(b);
        r.car2_class = two_player_classes()[size_t(b.cls)];
        r.car2_color = b.paint;
        r.transmission2 = b.transmission;
        r.tire_mode2 = b.tire_mode;
    }
    if (arcade_action_ == "single_race") {
        r.mode = "single_race";
        r.difficulty = difficulty_;
        // A{E,N,H,P}{C,B,A,S}T0001, dirt A?AD0001 (cars may override it).
        static const char kLevel[] = "ENHP";
        std::string cls = car_class_.size() > 6 ? car_class_.substr(6) : "c";
        std::string code = "A";
        code += kLevel[std::clamp(difficulty_, 0, 3)];
        if (cls == "dirt") {
            code += "AD";
        } else {
            code += char(std::toupper(static_cast<unsigned char>(cls[0])));
            code += 'T';
        }
        r.race_code = code + "0001";
    } else if (arcade_action_ == "compulsory") {
        r.mode = "time_trial"; // race code comes from the car's compulsory class
    } else if (arcade_action_ == "time_attack") {
        r.mode = "free_run";
        r.race_code = "ATAF0001";
    } else if (arcade_action_ == "two_player_battle") {
        r.mode = "two_player_battle";
        r.race_code = "A2P0001";
    } else {
        r.mode = arcade_action_;
    }
    if (const GtModeHooks& h = gt_mode_hooks(); h.arcade_prepare) h.arcade_prepare(r);
    // Single Race, Time Trial and Free Run store their race code as the game's race code
    // (the GT-mode race pages' focus reads it; menus §4.2).
    if (arcade_action_ == "single_race" || arcade_action_ == "compulsory" || arcade_action_ == "time_attack")
        race_code_ = r.race_code;
    // After the race the arcade menu reopens on ar/top with the same game focused.
    resume_set_ = "arcade";
    resume_page_ = "ar/top";
    resume_focus_ = -1;
    arcade_pos_ = -1;
    request_race(std::move(r));
}

std::string GameFlow::carousel_disabled() const {
    if (carousel_.entries.empty()) return {};
    const GtModeHooks& h = gt_mode_hooks();
    return h.arcade_disabled ? h.arcade_disabled(carousel_.page, course_, carousel_.entries[size_t(carousel_.index)])
                             : std::string();
}

void GameFlow::build_carousel(const std::string& page) {
    carousel_ = {};
    carousel_.page = page;
    MenuSet::PageRef ref = set_->page(page);
    if (!ref) return;
    const Layer* hidden = find_layer(ref.layout(), "hidden");
    if (!hidden) return;
    auto has = [&](const std::string& n) { return find_element(hidden, n) != nullptr; };
    for (const Element& e : hidden->elements) {
        const std::string& n = e.name;
        if (is_car_page(page)) {
            // Cars: a picture "<car>" with its strip icon "<car>_mini".
            if (has(n + "_mini") && (e.type & Element::kImage)) carousel_.entries.push_back(n);
        } else if (n.size() > 5 && n.compare(n.size() - 5, 5, "_logo") == 0) {
            // Courses, classes, time-trial events: "<id>_logo" + "<id>_doc".
            std::string id = n.substr(0, n.size() - 5);
            if (has(id + "_doc")) carousel_.entries.push_back(id);
        }
    }
    if (page == "ar/select_car_2p") raw_cars_ = carousel_.entries;
    if (const GtModeHooks& h = gt_mode_hooks(); h.arcade_entries)
        h.arcade_entries(page, arcade_action_, car_class_, carousel_.entries);
    // Remember the previous choice.
    const std::string& prev = page == "ar/select_course" || page == "ar/select_compulsory" ? course_
                              : page == "ar/select_class"                                 ? car_class_
                                                                                          : car_;
    auto it = std::find(carousel_.entries.begin(), carousel_.entries.end(), prev);
    if (it != carousel_.entries.end()) carousel_.index = int(it - carousel_.entries.begin());
    // Class page: on a dirt course the focus goes to Dirt, on a road course from Dirt to C.
    if (page == "ar/select_class" && !carousel_disabled().empty()) {
        const std::string& cur = carousel_.entries[size_t(carousel_.index)];
        const char* want = cur == "class_dirt" ? "class_c" : "class_dirt";
        auto w = std::find(carousel_.entries.begin(), carousel_.entries.end(), want);
        if (w != carousel_.entries.end()) carousel_.index = int(w - carousel_.entries.begin());
    }
    // The cross starts at its layout place; the strip glides in from the left.
    carousel_.wrap = page == "ar/select_class";
    if (const Element* cross = find_element(hidden, "cross_bar")) {
        carousel_.cross_x = cross->x + cross->w * 0.5f;
        carousel_.has_cross = true;
    }
    carousel_.cross_target = carousel_.cross_x;
    carousel_.art_offset = -carousel_.cross_target;
}

namespace {

// The strip element of a picker page and the icon size.
const char* strip_name(const std::string& page) {
    return page == "ar/select_course" ? "mini_courses" : page == "ar/select_class" ? "mini_classes" : "mini_cars";
}

float posmod(float a, float m) {
    float r = std::fmod(a, m);
    return r < 0 ? r + m : r;
}

} // namespace

std::string GameFlow::entry_image(const std::string& id) const {
    const GtModeHooks& h = gt_mode_hooks();
    std::string img = h.arcade_image ? h.arcade_image(id) : std::string();
    if (img.empty()) return id;
    if (ctl_ && ctl_->page())
        if (const Layer* hidden = find_layer(ctl_->page().layout(), "hidden");
            hidden && !find_element(hidden, img) && find_element(hidden, "_" + img))
            return "_" + img;
    return img;
}

std::vector<std::string> GameFlow::carousel_parts(const std::string& id) const {
    const std::string& page = carousel_.page;
    std::vector<std::string> parts;
    if (page == "ar/select_course") parts = {id, id + "_logo", id + "_doc", "arrow"};
    else if (page == "ar/select_class") parts = {id + "_logo", id + "_doc", "arrow"};
    else if (is_car_page(page)) parts = {entry_image(id), "handling", "speed", "acceleration"};
    else parts = {id, id + "_logo", id + "_doc", "arrow", "record", "first", "second", "third", "power",
                  "weight"}; // time trial (its _bg is drawn in place)
    // A disabled entry: its reason replaces the entry's text, with the "disabled" mark.
    if (const GtModeHooks& h = gt_mode_hooks(); h.arcade_disabled)
        if (std::string why = h.arcade_disabled(page, course_, id); !why.empty()) {
            for (std::string& p : parts)
                if (p == id + "_doc") p = why;
            parts.push_back("disabled");
        }
    return parts;
}

int GameFlow::carousel_icon_at(float x, float y) const {
    const Carousel& c = carousel_;
    if (c.entries.empty() || c.step > 1 || c.strip_alpha < 0.5f || !ctl_ || !ctl_->page()) return -1;
    const Layer* hidden = find_layer(ctl_->page().layout(), "hidden");
    const Element* strip = find_element(hidden, strip_name(c.page));
    if (!strip) return -1;
    // As drawn (draw_list): icon k at scroll + k (width + 4), centred on the strip.
    for (size_t k = 0; k < c.entries.size(); ++k) {
        const Element* icon = find_element(hidden, entry_image(c.entries[k]) + "_mini");
        if (!icon) continue;
        float ix = c.scroll + float(k) * (icon->w + 4.0f), cy = strip->y + strip->h * 0.5f;
        if (x >= ix && x <= ix + icon->w && y >= cy - icon->h * 0.5f && y <= cy + icon->h * 0.5f) return int(k);
    }
    return -1;
}

void GameFlow::animate_carousel(int move, int prev_index) {
    Carousel& c = carousel_;
    if (c.entries.empty()) return;
    const std::string& id = c.entries[size_t(c.index)];
    if (move) {
        // The old art slides out with the cross and fades over 16 frames; the new art
        // starts 680 px to the side.
        const int n = int(c.entries.size());
        const std::string& prev = c.entries[size_t(prev_index >= 0 ? prev_index : (c.index - move + n) % n)];
        c.old.push_back({prev, c.art_offset, std::min(1.0f, float(c.age) / 24.0f), 16});
        if (c.has_cross) c.cross_target -= float(move) * 680.0f; // no cross (2 players): swapped in place
        c.art_offset = -c.cross_target;
        c.age = 0;
        c.rest = 0;
        c.pulse = 1.0f;
    } else {
        c.pulse -= 1.0f / 15.0f;
        if (c.pulse < 0.25f) c.pulse = 1.0f;
    }
    // Cross spring (stiffness 600, damping 30, 60 Hz).
    float a = (c.cross_target - c.cross_x) * 600.0f;
    c.cross_v += a / 60.0f;
    c.cross_v -= c.cross_v * 30.0f / 60.0f;
    c.cross_x += c.cross_v / 60.0f;
    // Strip scroll: centred when everything fits, else the focused icon centred
    // (aligned at the ends); eased 30 % per frame.
    if (ctl_ && ctl_->page()) {
        const Layer* hidden = find_layer(ctl_->page().layout(), "hidden");
        const Element* strip = find_element(hidden, strip_name(c.page));
        const Element* icon = find_element(hidden, entry_image(c.entries.front()) + "_mini");
        if (strip && icon) {
            float W = strip->w, step = icon->w + 4.0f, n = float(c.entries.size()), r = float(c.index);
            float total = n * step - 4.0f, left = r * step, right = (n - r - 1) * step;
            float target = (640.0f - W) * 0.5f;
            if (total <= W) target += (W - total) * 0.5f;
            else if (left < W * 0.5f) target += 0;
            else if (right < W * 0.5f) target += W - total;
            else target += W * 0.5f - (left + icon->w * 0.5f);
            c.scroll += (target - c.scroll) * 0.3f;
        }
    }
    for (Carousel::Old& o : c.old) {
        o.alpha = std::max(0.0f, o.alpha - 4.0f / 60.0f);
        --o.frames;
    }
    c.old.erase(std::remove_if(c.old.begin(), c.old.end(), [](const Carousel::Old& o) { return o.frames <= 0; }),
                c.old.end());
    ++c.age;
    ++c.frames;
    // Car page: the colour column eases to the paint (30 % per frame; a new car starts at 0),
    // and the background blurs (radius 0 -> 2 at 4/s) while a step is open.
    if (move) c.paint_pos = 0;
    c.paint_pos += (float(car_paint_) - c.paint_pos) * 0.3f;
    c.blur = c.step > 1 ? std::min(2.0f, c.blur + 4.0f / 60.0f) : std::max(0.0f, c.blur - 4.0f / 60.0f);
    // Car page steps: the page springs up by 488 x (step - 1) + 134.4 px (as the cross).
    {
        float target = c.step > 1 ? 488.0f * float(c.step - 1) + 134.4f : 0.0f;
        float accel = (target - c.oy) * 600.0f;
        c.oy_v += accel / 60.0f;
        c.oy_v -= c.oy_v * 30.0f / 60.0f;
        c.oy += c.oy_v / 60.0f;
        float da = 2.0f / 60.0f; // the strip fades out (in) over 0.5 s
        c.strip_alpha = c.step > 1 ? std::max(0.0f, c.strip_alpha - da) : std::min(1.0f, c.strip_alpha + da);
    }
    // Course / class pages: the entry's looping movie once the cursor rested 6 frames.
    if (++c.rest == 6 && (c.page == "ar/select_course" || c.page == "ar/select_class") && id != "guest_garage") {
        std::string mv = "stream/" + id + "_full.pss";
        if (mv != movie_) {
            movie_ = mv;
            movie_request_ = mv;
        }
    }
}

std::vector<ElementDraw> GameFlow::draw_list() const {
    std::vector<ElementDraw> out = draw_list_impl();
    // Name entry: the text box's caret is the hidden red bar "cursor" (3 x 16, brightest
    // at its bottom), drawn at the text box's cursor: after the text before it (TextBox, menus §12.3;
    // the reference).
    if (ctl_ && ctl_->has_page() && ctl_->page_name() == "name_entry" && ctl_->page()) {
        int cursor = -1; // characters before the cursor (-1: the whole text)
        if (control_ == "memorycard" && name_mode_) cursor = int(name_box_.cursor);
        else if (const GtModeHooks& h = gt_mode_hooks(); h.name_cursor) cursor = h.name_cursor();
        // Left / right aligned text sits 12 units in from its box (the reference: help
        // labels, text box).
        apply_text_inset(out);
        const MenuSet::PageRef ref = ctl_->page(); // (a handle: the layout lives in the menu set)
        const Page& pg = ref.layout();
        const Element* caret = nullptr;
        for (const Layer& l : pg.layers)
            if (l.name == "hidden")
                for (const Element& e : l.elements)
                    if (e.name == "cursor") caret = &e;
        for (ElementDraw& d : out) {
            if (!caret || !d.element || !d.element->widget_class || *d.element->widget_class != "text_box") continue;
            // The caret right after the text before the cursor; the text starts 12 units in
            // (kTextInset).
            const Element& box = *d.element;
            std::string txt = d.text ? *d.text : box.text ? box.text->text : std::string();
            if (cursor >= 0 && size_t(cursor) < txt.size()) txt.resize(size_t(cursor));
            std::string font = box.text ? box.text->font : std::string("sw_20");
            float w = 0;
            if (measure_text) w = measure_text(font, txt);
            else w = float(txt.size()) * FontSpec::parse(font).size * 0.5f;
            ElementDraw c;
            c.scene = d.scene;
            c.element = caret;
            c.has_box = true;
            c.box = {box.x + 12.0f + w + 1.0f, box.y, caret->w, caret->h};
            c.z = d.z + 0.01f;
            out.push_back(c);
            break;
        }
    }
    return out;
}

std::vector<ElementDraw> GameFlow::draw_list_impl() const {
    if (!ctl_ || switch_frames_ > 0 || waiting_game_) return {};
    std::vector<ElementDraw> out = ctl_->draw_list();
    // Name entry pages (menu/name_entry, memorycard name_entry): the page has a "menu" window,
    // so the list places its rows itself and the pmb positions are ignored: the seven rows
    // (six key rows, the OK row) stacked from the window's top, 32 high with 4 between; the 16
    // keys of a row centred at a 28 px pitch; OK left-aligned in its row (name_entry_controls.md).
    if (ctl_->has_page() && ctl_->page_name() == "name_entry" && ctl_->page()) {
        const MenuSet::PageRef ref = ctl_->page(); // (a handle: the layout lives in the menu set)
        const Page& pg = ref.layout();
        const Element* win = nullptr;
        for (const Layer& l : pg.layers)
            for (const Element& w : l.widgets)
                if (w.name == "menu") win = &w;
        const auto& rows = ctl_->items();
        if (win) {
            // Target box (bottom-left, y up) of each row and key element, by name.
            std::map<std::string, std::pair<float, float>> place; // name -> (x, y)
            const float top = win->y + win->h;
            for (size_t r = 0; r < rows.size(); ++r) {
                if (!rows[r].flow) continue;
                const float y = top - 32.0f - 36.0f * float(r);
                place[rows[r].flow->name] = {win->x - 16.0f, y}; // the row (544 wide, centred on the window)
                const auto& kids = rows[r].flow->children;
                const float pitch = 28.0f, kw = 24.0f;
                const float total = float(kids.size()) * kw + float(kids.size() - 1) * 4.0f;
                const float x0 = win->x + win->w * 0.5f - (kids.size() > 1 ? total * 0.5f : 223.0f);
                for (size_t k = 0; k < kids.size(); ++k)
                    place[kids[k].name] = {x0 + pitch * float(k), y + 4.0f}; // 24 high in the 32 px row
            }
            auto owner = [&](const Element* e) -> const Element* {
                for (const Element& it : pg.items) {
                    if (e == &it) return &it;
                    for (const Element& alt : it.alt)
                        if (e == &alt) return &it;
                }
                return nullptr;
            };
            for (ElementDraw& d : out) {
                if (!d.element) continue;
                const Element* it = owner(d.element);
                if (!it) continue;
                auto p = place.find(it->name);
                if (p == place.end()) continue;
                d.dx += p->second.first - it->x;
                d.dy += p->second.second - it->y;
            }
        }
    }
    if (control_ == "option") option_draws(out);
    if (control_ == "option" && ctl_->page()) {
        // Key Configuration (the page, or its view on the top page): our button only.
        const bool key_page = ctl_->page_name() == "key/key";
        const Layer* l = find_layer(ctl_->page().layout(), key_page ? "defaultLayer" : "m5");
        if (l && !l->elements.empty())
            out.erase(std::remove_if(out.begin(), out.end(),
                                     [&](const ElementDraw& d) {
                                         return d.element && d.element >= &l->elements.front() &&
                                                d.element <= &l->elements.back() && key_config_hidden(d.element->name);
                                     }),
                      out.end());
    }
    if (control_ == "option")
        // Option previews: only the art of the current values.
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&](const ElementDraw& d) {
                                     return d.widget && d.element && d.element->widget_class &&
                                            !option_preview_visible(options_, *d.element->widget_class);
                                 }),
                  out.end());
    if (control_ == "memorycard" && theater_mode_) {
        theater_.draw(ctl_->page(), set_ ? set_->page("file_operation") : MenuSet::PageRef{}, out);
        return out;
    }
    if (control_ == "logger") {
        logger_draws(out);
        return out;
    }
    if (control_ == "memorycard") {
        if (name_mode_) {
            // The name being entered in the text box, with the cursor.
            for (ElementDraw& d : out)
                if (d.element && d.element->widget_class && *d.element->widget_class == "text_box")
                    d.text = std::make_shared<const std::string>(name_box_.text);
            return out;
        }
        if (save_mode_) save_page_.draw(ctl_->page(), out);
        return out;
    }
    // ar/level: while L1 and R1 are held the Hard button shows the "pro" art and
    // the Professional description (layer4) replaces Hard's (layer3).
    const MenuKeys pro_keys = key_bit(MenuKey::L1) | key_bit(MenuKey::R1);
    if (control_ == "arcade" && ctl_->page_name() == "ar/level" && (held_ & pro_keys) == pro_keys && ctl_->page()) {
        const MenuSet::PageRef ref = ctl_->page();
        const auto& items = ref.layout().items;
        const Layer* hidden = find_layer(ref.layout(), "hidden");
        const Layer* l3 = find_layer(ref.layout(), "layer3");
        const Layer* l4 = find_layer(ref.layout(), "layer4");
        auto in_layer = [](const Layer* l, const Element* e) {
            return l && e >= l->elements.data() && e < l->elements.data() + l->elements.size();
        };
        for (ElementDraw& d : out) {
            if (!d.element) continue;
            if (items.size() > 2 && d.element == &items[2]) {
                if (const Element* p = find_element(hidden, "pro")) d.element = p;
            } else if (items.size() > 2 && !items[2].alt.empty() && d.element == &items[2].alt[0]) {
                if (const Element* p = find_element(hidden, "pro_a")) d.element = p;
            } else if (in_layer(l3, d.element)) {
                std::string n = d.element->name == "text_hard" ? "text_pro" : d.element->name;
                if (n.size() > 2 && n.compare(n.size() - 2, 2, "p3") == 0) n.back() = '4';
                if (const Element* p = find_element(l4, n)) d.element = p;
            }
        }
    }
    // The arcade Clear Status page's widgets are filled by the game too.
    if (gt_active() || (control_ == "arcade" && ctl_->page_name() == "ar/goodies"))
        if (const GtModeHooks& h = gt_mode_hooks(); h.widget_text) {
            const std::string& page = ctl_->page_name();
            for (ElementDraw& d : out) {
                if (d.element && d.kind == ElementDraw::Kind::Element && d.element->text)
                    if (std::optional<std::string> t = h.widget_text(page, *d.element))
                        d.text = std::make_shared<const std::string>(std::move(*t));
                if (h.hide_art && d.widget && d.element && h.hide_art(page, *d.element)) d.hide_parts = true;
            }
            if (h.widget_draws) {
                std::vector<ElementDraw> merged;
                merged.reserve(out.size());
                for (ElementDraw& d : out) {
                    merged.push_back(d);
                    if (!d.widget || !d.element || !d.element->widget_class) continue;
                    std::vector<ElementDraw> extra;
                    h.widget_draws(*ctl_, d, extra);
                    for (ElementDraw& x : extra) {
                        x.alpha *= d.alpha;
                        x.dx += d.dx;
                        x.dy += d.dy;
                        merged.push_back(std::move(x));
                    }
                }
                out = std::move(merged);
            }
            // Code-drawn content of the top page (drawn on top of the list).
            if (h.extra_draws) h.extra_draws(*ctl_, out);
        }
    if (two_page_ && ctl_->page()) {
        two_player_draw(out);
        std::stable_sort(out.begin(), out.end(), [](const ElementDraw& a, const ElementDraw& b) { return a.z < b.z; });
        return out;
    }
    if (!carousel_.entries.empty() && ctl_->page() && ctl_->page_name() == carousel_.page) {
        const MenuSet::PageRef ref = ctl_->page();
        const Layer* hidden = find_layer(ref.layout(), "hidden");
        const Carousel& c = carousel_;
        const std::string& id = c.entries[size_t(c.index)];
        const GtModeHooks& h = gt_mode_hooks();
        auto put = [&](const Element* e, float dx, float alpha, const std::string* text = nullptr,
                       bool child = true) {
            if (!e || alpha <= 0) return;
            ElementDraw d;
            d.scene = ref.scene;
            d.element = e;
            d.dx = dx;
            if (child) d.dy = c.oy; // cross children also follow the car page's steps
            d.alpha = alpha;
            d.z = e->z;
            if (text && !text->empty()) d.text = std::make_shared<const std::string>(*text);
            // Car page texts the game draws smaller than their style's font (M, from the
            // reference: the power / torque lines and the gauge labels).
            if (is_car_page(c.page)) {
                if (e->name == "ps" || e->name == "kgm") d.scale = 0.67f;
                else if (e->name == "handling" || e->name == "speed" || e->name == "acceleration") d.scale = 0.75f;
            }
            out.push_back(d);
        };
        // The entry's art rides on the cross: current (fading in over 24 frames) and the
        // ones sliding out.
        auto put_entry = [&](const std::string& eid, float offset, float alpha) {
            for (const std::string& n : carousel_parts(eid)) put(find_element(hidden, n), c.cross_x + offset, alpha);
            if (h.arcade_parts)
                for (const auto& [n, text] : h.arcade_parts(c.page, eid)) {
                    // The course group badge is a Single Race thing (the 2 Player page has none).
                    if (n.rfind("area_", 0) == 0 && arcade_action_ != "single_race") continue;
                    // Time trial values start after their right-aligned labels (the reference).
                    float vx = n.size() > 5 && (n.compare(n.size() - 5, 5, "_time") == 0 ||
                                                (n.size() > 6 && n.compare(n.size() - 6, 6, "_value") == 0))
                                   ? 22.0f
                                   : 0.0f;
                    put(find_element(hidden, n), c.cross_x + offset + vx, alpha, &text);
                }
        };
        put_entry(id, c.art_offset, std::min(1.0f, float(c.age) / 24.0f));
        for (const Carousel::Old& o : c.old) put_entry(o.id, o.offset, o.alpha);
        // Time trial: the challenge's full-screen picture, swapped in place.
        put(find_element(hidden, id + "_bg"), 0, 1, nullptr, false);
        // The cross: a band across the screen and a vertical bar that sweeps (680 px period).
        if (const Element* cross = find_element(hidden, "cross_bar")) {
            float half = cross->w * 0.5f, cx = posmod(c.cross_x + half, 680.0f) - half;
            const float by = cross->y + posmod(c.oy, 488.0f), top = by + cross->h;
            const SDL_FRect boxes[3] = {{0, by, 640, cross->h},
                                        {cx - half, top, cross->w, 448 - top},
                                        {cx - half, 0, cross->w, by}};
            for (const SDL_FRect& b : boxes) {
                ElementDraw d;
                d.scene = ref.scene;
                d.element = cross;
                d.has_box = true;
                d.box = b;
                d.z = cross->z;
                out.push_back(d);
            }
        }
        // The strip of icons: every entry, the focused one pulsing; disabled ones crossed out.
        const Element* strip = find_element(hidden, strip_name(c.page));
        const Element* xmark = find_element(hidden, "x");
        for (size_t k = 0; strip && k < c.entries.size(); ++k) {
            const Element* icon = find_element(hidden, entry_image(c.entries[k]) + "_mini");
            if (!icon) continue;
            float step = icon->w + 4.0f, x = c.scroll + float(k) * step;
            if (x > 640 || x + icon->w < 0) continue;
            float cy = strip->y + strip->h * 0.5f;
            ElementDraw d;
            d.scene = ref.scene;
            d.element = icon;
            d.has_box = true;
            d.box = {x, cy - icon->h * 0.5f, icon->w, icon->h};
            d.alpha = c.strip_alpha;
            d.z = icon->z;
            out.push_back(d);
            // The focused icon pulses: colour x p in PS2 units (0.502 = normal), i.e. up to
            // twice as bright; the part above normal is drawn as an additive second pass.
            if (int(k) == c.index) {
                float b = c.pulse / 0.502f;
                if (b > 1.0f) {
                    ElementDraw over = d;
                    over.additive = true;
                    over.brightness = b - 1.0f;
                    over.z += 0.01f;
                    out.push_back(over);
                } else {
                    out.back().brightness = b;
                }
            }
            if (xmark && h.arcade_disabled && !h.arcade_disabled(c.page, course_, c.entries[k]).empty()) {
                ElementDraw xd = d;
                xd.element = xmark;
                xd.box = {x + (icon->w - xmark->w) * 0.5f, cy - xmark->h * 0.5f, xmark->w, xmark->h};
                xd.brightness = 1;
                xd.z = icon->z + 0.5f;
                out.push_back(xd);
            }
        }
        // Car page steps: AUTOMATIC / MANUAL (step 2) and RACING / DRIFT (step 3) buttons,
        // centred at x 220 / 420, arriving with the page in the cross band (screen y ~214).
        if (c.page == "ar/select_car") {
            for (int k = 1; k <= 2; ++k) {
                float ys = 214.0f + (488.0f * float(k) + 134.4f - c.oy);
                if (ys < -60 || ys > 508) continue;
                int v = k == 1 ? transmission_ : tire_mode_;
                const char* left = k == 1 ? "auto" : "racing";
                const char* right = k == 1 ? "manu" : "dri";
                for (int side = 0; side < 2; ++side) {
                    std::string n = std::string(side == 0 ? left : right) + ((side == v) ? "_yes" : "_no");
                    if (const Element* b = find_element(hidden, n)) {
                        ElementDraw d;
                        d.scene = ref.scene;
                        d.element = b;
                        d.has_box = true;
                        float cxb = side == 0 ? 220.0f : 420.0f;
                        d.box = {cxb - b->w * 0.5f, 448.0f - ys - b->h * 0.5f, b->w, b->h};
                        d.z = 80.0f; // over the 3D car
                        out.push_back(d);
                    }
                }
            }
        }
        // Car pages: the 3D car (slot element "car"), drawn by the app's model renderer
        // (overlay); recreated on each move, without a slide.
        if (is_car_page(c.page) || c.page == "ar/select_compulsory")
            if (const Element* slot = find_element(hidden, "car")) {
                ElementDraw d;
                d.scene = ref.scene;
                d.element = slot;
                d.has_box = true;
                // The showroom camera covers the page; two players: player 1's half (M).
                d.box = c.page == "ar/select_car_2p" ? SDL_FRect{-45, 72, 440, 308} : SDL_FRect{0, 0, 640, 448};
                d.overlay = true;
                d.hide_parts = true;
                // The 3D car and its floor reflection at the slot's depth (5): over the
                // background and the spec ticker, under the paint name, the cross band, the icon
                // strip and the rest of the page.
                d.z = slot->z;
                out.push_back(d);
            }
        // Car page: the three circle gauges (riding on the cross with the car's art): a ring
        // 22..238 degrees in the background colour and the value arc from 230 degrees
        // clockwise, orange up to 80 % and red beyond; each new car sweeps to full (+0.04
        // per frame) and settles back (-0.02 per frame).
        if (is_car_page(c.page) && h.arcade_gauges) {
            std::array<float, 3> v = h.arcade_gauges(id);
            static const char* const kGauge[3] = {"handling_gauge", "speed_gauge", "acceleration_gauge"};
            float rise = std::min(1.0f, 0.04f * float(c.age));
            for (int g = 0; g < 3; ++g) {
                const Element* ge = find_element(hidden, kGauge[g]);
                if (!ge) continue;
                float f = c.age <= 25 ? rise : std::max(v[size_t(g)], 1.0f - 0.02f * float(c.age - 25));
                float gx = ge->x + ge->w * 0.5f + c.cross_x + c.art_offset, gy = ge->y + ge->h * 0.5f + c.oy;
                float alpha = std::min(1.0f, float(c.age) / 24.0f);
                auto arc = [&](float rx, float ry, float a0, float a1, float t, SDL_FColor col) {
                    ElementDraw d;
                    d.kind = ElementDraw::Kind::Arc;
                    d.has_box = true;
                    d.box = {gx - rx, gy - ry, 2 * rx, 2 * ry};
                    d.level = a0;
                    d.level2 = a1;
                    d.arc_thickness = t;
                    d.fill = col;
                    d.alpha = alpha;
                    d.z = ge->z;
                    out.push_back(d);
                };
                const SDL_FColor bg{0.2f, 0.2f, 0.2f, 1}, orange{1, 0.6f, 0, 1}, red{1, 0, 0, 1};
                arc(ge->w + 4, ge->h + 4, 22, 238, 0.5f, bg);
                if (f > 0) {
                    float end = 230.0f - 200.0f * f;
                    if (f < 0.8f) arc(ge->w, ge->h, end, 230, 0.3f, orange);
                    else {
                        arc(ge->w, ge->h, 70, 230, 0.3f, orange);
                        arc(ge->w, ge->h, end, 70, 0.3f, red);
                    }
                }
            }
        }
        // Car page: the colour column (the current paint filled, outlines for one above and
        // five below, arrows when there is more) and the paint's name.
        if (is_car_page(c.page) && h.arcade_colors) {
            std::vector<std::pair<std::string, u32>> cols = h.arcade_colors(id);
            const Element* chip = find_element(hidden, "color_chip");
            const float dx = c.cross_x + c.art_offset, alpha = std::min(1.0f, float(c.age) / 24.0f);
            if (chip && !cols.empty()) {
                const int cur = std::clamp(car_paint_, 0, int(cols.size()) - 1);
                const float cx = chip->x + chip->w * 0.5f + dx, cy = chip->y + chip->h * 0.5f + c.oy;
                // Clip band: one slot above and five below the centre slot, each chip filled with
                // its colour inside its outline; the column eases (30 % per frame).
                const SDL_FRect outline_band{cx - 20, cy - 5.5f * 18.0f, 40, 7 * 18.0f};
                auto fill = [&](float x, float y, float w, float hh, SDL_FColor col, const SDL_FRect& band) {
                    ElementDraw d;
                    d.kind = ElementDraw::Kind::Fill;
                    d.has_box = true;
                    d.box = {x, y, w, hh};
                    d.fill = col;
                    d.alpha = alpha;
                    d.has_clip = true;
                    d.clip = band;
                    d.z = chip->z;
                    out.push_back(d);
                };
                // Outlines in light grey: the column's background is as dark as the old 0.22
                // grey, which left only the filled chip visible.
                const SDL_FColor grey{0.66f, 0.66f, 0.70f, 1};
                for (int i = 0; i < int(cols.size()); ++i) {
                    float s = float(i) - c.paint_pos;
                    if (s < -2 || s > 6) continue;
                    float y = cy - s * 18.0f - chip->h * 0.5f, x = cx - chip->w * 0.5f;
                    fill(x, y, chip->w, 1, grey, outline_band);
                    fill(x, y + chip->h - 1, chip->w, 1, grey, outline_band);
                    fill(x, y, 1, chip->h, grey, outline_band);
                    fill(x + chip->w - 1, y, 1, chip->h, grey, outline_band);
                    u32 rgb = cols[size_t(i)].second;
                    fill(x + 1, y + 1, chip->w - 2, chip->h - 2,
                         {float(rgb & 0xFF) / 255.0f, float((rgb >> 8) & 0xFF) / 255.0f,
                          float((rgb >> 16) & 0xFF) / 255.0f, 1},
                         outline_band); // every visible chip shows its colour
                }
                auto arrow = [&](const char* name, float y) {
                    if (const Element* a = find_element(hidden, name)) {
                        ElementDraw d;
                        d.scene = ref.scene;
                        d.element = a;
                        d.has_box = true;
                        d.box = {cx - a->w * 0.5f, y - a->h * 0.5f, a->w, a->h};
                        d.alpha = alpha;
                        d.z = a->z;
                        out.push_back(d);
                    }
                };
                if (cur > 0) arrow("arrow_up", cy + 18.0f);
                if (cur + 1 < int(cols.size())) arrow("arrow_down", cy - 18.0f);
                const std::string& cname = cols[size_t(cur)].first;
                if (!cname.empty() && cname[0] != '-') put(find_element(hidden, "color_name"), dx, alpha, &cname);
            }
            put(find_element(hidden, "color_selection"), dx, alpha);
        }
        // Car pages: the spec ticker (ticker on "flow_spec"): a new line every second
        // at the element's place, fading in 0.5 s, held 3 s, fading out 0.5 s, drifting up
        // 40 px/s; label above value.
        if ((c.page == "ar/select_car" || c.page == "ar/select_compulsory") && h.arcade_specs)
            if (const Element* spec = find_element(hidden, "flow_spec")) {
                std::vector<std::pair<std::string, std::string>> lines = h.arcade_specs(id);
                const int n = int(lines.size());
                for (int j = std::max(0, c.age / 60 - 3); n > 0 && j <= c.age / 60; ++j) {
                    int t = c.age - j * 60;
                    if (t < 0 || t >= 240) continue;
                    float a = t < 30 ? float(t) / 30.0f : t > 210 ? float(240 - t) / 30.0f : 1.0f;
                    float y = spec->y + float(t) * 40.0f / 60.0f;
                    const auto& [label, value] = lines[size_t(j % n)];
                    for (int k = 0; k < 2; ++k) {
                        ElementDraw d;
                        d.scene = ref.scene;
                        d.element = spec;
                        d.has_box = true;
                        d.box = {spec->x, y - float(k) * 14.0f, spec->w, spec->h};
                        d.scale = 0.6f; // the ticker's small type
                        d.alpha = a;
                        d.z = spec->z;
                        d.text = std::make_shared<const std::string>(k == 0 ? label : value);
                        out.push_back(d);
                    }
                }
            }
        // Single Race breadcrumbs (ar/top's history_* icons): the difficulty, then the course
        // group, then the class, side by side at the top left; this page's one slides in
        // from the right (2,560 px/s for 0.25 s).
        if (arcade_action_ == "single_race")
            if (MenuSet::PageRef top = set_->page("ar/top")) {
                const Layer* th = find_layer(top.layout(), "hidden");
                static const char* const kLevel[] = {"easy", "normal", "hard", "pro"};
                std::vector<std::string> crumbs = {std::string("history_level_") + kLevel[std::clamp(difficulty_, 0, 3)]};
                if (c.page != "ar/select_course" && h.arcade_parts)
                    for (const auto& [n, text] : h.arcade_parts("ar/select_course", course_))
                        if (n.rfind("area_", 0) == 0) crumbs.push_back("history_" + n);
                if (c.page == "ar/select_car")
                    crumbs.push_back(car_class_.rfind("class_", 0) == 0 ? "history_" + car_class_
                                                                        : "history_class_" + car_class_);
                for (size_t k = 0; k < crumbs.size(); ++k)
                    if (const Element* e = find_element(th, crumbs[k])) {
                        ElementDraw d;
                        d.scene = top.scene;
                        d.element = e;
                        d.dx = 20.0f + float(k) * 66.0f - e->x;
                        if (k + 1 == crumbs.size()) d.dx += 640.0f * std::max(0.0f, 1.0f - float(c.frames) / 15.0f);
                        d.z = e->z;
                        out.push_back(d);
                    }
            }
        // Arrows: hidden at the ends of a list that does not wrap.
        if (c.wrap || c.index > 0 || c.step > 1) put(find_element(hidden, "arrow_l"), 0, 1, nullptr, false);
        if (c.wrap || c.index + 1 < int(c.entries.size()) || c.step > 1)
            put(find_element(hidden, "arrow_r"), 0, 1, nullptr, false);
        std::stable_sort(out.begin(), out.end(), [](const ElementDraw& a, const ElementDraw& b) { return a.z < b.z; });
    }
    return out;
}

// --- 2 Player car page -------------------------------------------------------

const std::vector<std::string>& GameFlow::two_player_classes() {
    static const std::vector<std::string> k = {"class_c", "class_b",    "class_a",     "class_s",
                                               "class_dirt", "home_garage", "guest_garage"};
    return k;
}

void GameFlow::two_player_open() {
    const GtModeHooks& h = gt_mode_hooks();
    for (TwoPlayer& t : two_) {
        int cls = t.cls;
        t = TwoPlayer{};
        t.cls = cls;
        t.transmission = transmission_; // the Options menu's AT / MT
        // On entering the class row: a dirt course forces Dirt, a road course moves off Dirt.
        bool dirt = h.arcade_disabled && !h.arcade_disabled("ar/select_class", course_, "class_c").empty();
        if (dirt && t.cls < 4) t.cls = 4;
        if (!dirt && t.cls == 4) t.cls = 0;
    }
}

namespace {
// Menu key repeat: a press, then after 30 frames every 3 frames.
MenuKeys fire(MenuKeys held, MenuKeys& prev, int& repeat) {
    MenuKeys pressed = held & MenuKeys(~prev);
    MenuKeys dirs = key_bit(MenuKey::Up) | key_bit(MenuKey::Down) | key_bit(MenuKey::Left) | key_bit(MenuKey::Right);
    if (held & dirs & prev) {
        ++repeat;
        if (repeat >= 30 && (repeat - 30) % 3 == 0) pressed |= held & dirs;
    } else {
        repeat = 0;
    }
    prev = held;
    return pressed;
}
} // namespace

void GameFlow::two_player_frame() {
    MenuKeys f1 = fire(held_, two_[0].prev, two_[0].repeat);
    MenuKeys f2 = fire(held2_, two_[1].prev, two_[1].repeat);
    if (two_player_step(0, f1)) return;
    two_player_step(1, f2);
}

// One player's frame; true when the page ended.
bool GameFlow::two_player_step(int p, MenuKeys f) {
    TwoPlayer& t = two_[p];
    const TwoPlayer& other = two_[1 - p];
    const GtModeHooks& h = gt_mode_hooks();
    const auto& classes = two_player_classes();
    auto on = [&](MenuKey k) { return (f & key_bit(k)) != 0; };
    const int lr = on(MenuKey::Right) ? 1 : on(MenuKey::Left) ? -1 : 0;
    const int ud = on(MenuKey::Down) ? 1 : on(MenuKey::Up) ? -1 : 0;
    const bool ok = on(MenuKey::Confirm), cancel = on(MenuKey::Cancel);
    auto clamp_move = [&](int& v, int n) {
        int next = v + lr;
        if (next < 0 || next >= n) {
            sounds_.push_back("disable");
            return false;
        }
        v = next;
        sounds_.push_back("select");
        return true;
    };
    const bool dirt = h.arcade_disabled && !h.arcade_disabled("ar/select_class", course_, "class_c").empty();
    ++t.age;
    t.pulse -= 1.0f / 15.0f;
    if (t.pulse < 0.25f) t.pulse = 1.0f;
    t.paint_pos += (float(t.paint) - t.paint_pos) * 0.3f;
    switch (t.state) {
    case 2: // class row (wraps)
        if (lr) {
            t.cls = (t.cls + lr + int(classes.size())) % int(classes.size());
            t.age = 0;
            t.pulse = 1;
            sounds_.push_back("select");
        } else if (ok) {
            const std::string& c = classes[size_t(t.cls)];
            if (h.arcade_disabled && !h.arcade_disabled("ar/select_class", course_, c).empty()) {
                sounds_.push_back("disable");
            } else {
                sounds_.push_back("next");
                t.cars = raw_cars_;
                if (h.arcade_entries) h.arcade_entries("ar/select_car", arcade_action_, c, t.cars);
                t.car = 0, t.paint = 0, t.age = 0, t.scroll = 0, t.pulse = 1;
                t.state = t.cars.empty() ? 2 : 4;
            }
        } else if (cancel) {
            // Either player cancelling on the class row leaves the page (back to the course).
            arcade_back();
            return true;
        }
        break;
    case 4: // car + paint
        if (lr) {
            if (clamp_move(t.car, int(t.cars.size()))) t.paint = 0, t.paint_pos = 0, t.age = 0, t.pulse = 1;
        } else if (ud && h.arcade_colors) {
            int n = int(h.arcade_colors(t.cars[size_t(t.car)]).size());
            int next = t.paint + ud;
            if (next < 0 || next >= n) sounds_.push_back("disable");
            else {
                t.paint = next;
                sounds_.push_back("cursor");
            }
        } else if (ok) {
            sounds_.push_back("next");
            t.state = 6;
        } else if (cancel) {
            sounds_.push_back("cancel");
            t.state = 2;
            t.age = 0;
        }
        break;
    case 6: // AUTOMATIC / MANUAL
        if (lr) clamp_move(t.transmission, 2);
        else if (ok) {
            sounds_.push_back("next");
            // As the original: the garage test reads player 1's class for both players.
            const std::string& c1 = classes[size_t(two_[0].cls)];
            t.state = dirt || c1 == "home_garage" || c1 == "guest_garage" ? 9 : 8;
        } else if (cancel) {
            sounds_.push_back("cancel");
            t.state = 4;
        }
        break;
    case 8: // RACING / DRIFT
        if (lr) clamp_move(t.tire_mode, 2);
        else if (ok) {
            sounds_.push_back("next");
            t.state = 9;
        } else if (cancel) {
            sounds_.push_back("cancel");
            t.state = 6;
        }
        break;
    case 9: // ready: both ready starts the race
        if (cancel) {
            sounds_.push_back("cancel");
            t.state = dirt ? 6 : 8;
        } else if (other.state == 9) {
            arcade_start_race();
            return true;
        }
        break;
    default: break;
    }
    return false;
}

void GameFlow::two_player_draw(std::vector<ElementDraw>& out) const {
    const MenuSet::PageRef ref = ctl_->page();
    const Layer* hidden = find_layer(ref.layout(), "hidden");
    const GtModeHooks& h = gt_mode_hooks();
    const auto& classes = two_player_classes();
    auto el = [&](const std::string& n) { return find_element(hidden, n); };
    auto put_box = [&](const Element* e, SDL_FRect box, float alpha, float z, const std::string* text = nullptr) {
        if (!e || alpha <= 0) return false;
        ElementDraw d;
        d.scene = ref.scene;
        d.element = e;
        d.has_box = true;
        d.box = box;
        d.alpha = alpha;
        d.z = z;
        if (text) d.text = std::make_shared<const std::string>(*text);
        out.push_back(d);
        return true;
    };
    // An element at 0.75 scale centred at screen (cx, cy) (y down).
    auto put_scaled = [&](const Element* e, float cx, float cy, float alpha) {
        if (!e) return;
        float w = e->w * 0.75f, hh = e->h * 0.75f;
        put_box(e, {cx - w * 0.5f, 448.0f - cy - hh * 0.5f, w, hh}, alpha, e->z);
    };
    for (int p = 0; p < 2; ++p) {
        const TwoPlayer& t = two_[p];
        const std::string sfx = p == 1 ? "_2p" : "";
        const float mirror = p == 1 ? 1.0f : 0.0f;
        auto mx = [&](float x) { return mirror > 0 ? 640.0f - x : x; };
        const float fade = std::min(1.0f, float(t.age) / 24.0f);
        // The row: class icons on the class row, then the car icons.
        const Element* strip = el("mini_cars" + sfx);
        const bool class_row = t.state == 2;
        const std::vector<std::string>& row = class_row ? classes : t.cars;
        const int focus = class_row ? t.cls : t.car;
        if (strip && (class_row || t.state == 4)) {
            auto image = [&](const std::string& id) { return class_row ? id : entry_image(id); };
            const Element* icon0 = row.empty() ? nullptr : el(image(row.front()) + "_mini");
            float step = icon0 ? icon0->w + 4.0f : 44.0f, total = float(row.size()) * step - 4.0f;
            float left = float(focus) * step, W = strip->w, off;
            if (total <= W) off = (W - total) * 0.5f;
            else if (left < W * 0.5f) off = 0;
            else if (total - left - step < W * 0.5f) off = W - total;
            else off = W * 0.5f - (left + step * 0.5f);
            for (size_t k = 0; k < row.size(); ++k) {
                const Element* icon = el(image(row[k]) + "_mini");
                if (!icon) continue;
                float x = strip->x + off + float(k) * step;
                if (x > 640 || x + icon->w < 0) continue;
                float cy = strip->y + strip->h * 0.5f;
                ElementDraw d;
                d.scene = ref.scene;
                d.element = icon;
                d.has_box = true;
                d.box = {x, cy - icon->h * 0.5f, icon->w, icon->h};
                d.z = icon->z;
                out.push_back(d);
                if (int(k) == focus && t.pulse / 0.502f > 1.0f) {
                    ElementDraw o = d;
                    o.additive = true;
                    o.brightness = t.pulse / 0.502f - 1.0f;
                    o.z += 0.01f;
                    out.push_back(o);
                }
                if (class_row && h.arcade_disabled && !h.arcade_disabled("ar/select_class", course_, row[k]).empty())
                    if (const Element* xm = el("class_x"))
                        put_box(xm, {x + (icon->w - xm->w) * 0.5f, cy - xm->h * 0.5f, xm->w, xm->h}, 1, icon->z + 0.5f);
            }
        }
        if (class_row) {
            put_scaled(el(classes[size_t(t.cls)] + "_logo"), mx(172.5f), 160.5f, fade);
            continue;
        }
        if (t.cars.empty()) continue;
        const std::string& id = t.cars[size_t(t.car)];
        put_scaled(el(entry_image(id)), mx(192.0f), 70.0f, 1);
        if (t.state == 4) {
            if (h.arcade_parts)
                for (const auto& [n, text] : h.arcade_parts("ar/select_car_2p", id))
                    if (const Element* e = el(n + sfx)) {
                        if (put_box(e, {e->x, e->y, e->w, e->h}, fade, e->z, text.empty() ? nullptr : &text) &&
                            (n == "ps" || n == "kgm"))
                            out.back().scale = 0.67f; // as the 1 player page
                    }
            for (const char* n : {"handling", "speed", "acceleration"})
                if (const Element* e = el(std::string(n) + sfx)) {
                    if (put_box(e, {e->x, e->y, e->w, e->h}, fade, e->z)) out.back().scale = 0.75f;
                }
            if (h.arcade_gauges) {
                std::array<float, 3> v = h.arcade_gauges(id);
                static const char* const kGauge[3] = {"handling_gauge", "speed_gauge", "acceleration_gauge"};
                float rise = std::min(1.0f, 0.04f * float(t.age));
                for (int g = 0; g < 3; ++g) {
                    const Element* ge = el(kGauge[g] + sfx);
                    if (!ge) continue;
                    float fv = t.age <= 25 ? rise : std::max(v[size_t(g)], 1.0f - 0.02f * float(t.age - 25));
                    float gx = ge->x + ge->w * 0.5f, gy = ge->y + ge->h * 0.5f;
                    auto arc = [&](float rx, float ry, float a0, float a1, float th, SDL_FColor col) {
                        ElementDraw d;
                        d.kind = ElementDraw::Kind::Arc;
                        d.has_box = true;
                        d.box = {gx - rx, gy - ry, 2 * rx, 2 * ry};
                        d.level = a0, d.level2 = a1, d.arc_thickness = th;
                        d.fill = col;
                        d.alpha = fade;
                        d.z = ge->z;
                        out.push_back(d);
                    };
                    arc(ge->w + 4, ge->h + 4, 22, 238, 0.5f, {0.2f, 0.2f, 0.2f, 1});
                    float end = 230.0f - 200.0f * fv;
                    if (fv < 0.8f) arc(ge->w, ge->h, end, 230, 0.3f, {1, 0.6f, 0, 1});
                    else {
                        arc(ge->w, ge->h, 70, 230, 0.3f, {1, 0.6f, 0, 1});
                        arc(ge->w, ge->h, end, 70, 0.3f, {1, 0, 0, 1});
                    }
                }
            }
            // Colour column (no name on this page).
            if (h.arcade_colors)
                if (const Element* chip = el("color_chip" + sfx)) {
                    auto cols = h.arcade_colors(id);
                    const float cx = chip->x + chip->w * 0.5f, cy = chip->y + chip->h * 0.5f;
                    const SDL_FRect outline_band{cx - 20, cy - 5.5f * 18.0f, 40, 7 * 18.0f};
                    auto fill = [&](float x, float y, float w, float hh, SDL_FColor col, const SDL_FRect& band) {
                        ElementDraw d;
                        d.kind = ElementDraw::Kind::Fill;
                        d.has_box = true;
                        d.box = {x, y, w, hh};
                        d.fill = col;
                        d.alpha = fade;
                        d.has_clip = true;
                        d.clip = band;
                        d.z = chip->z;
                        out.push_back(d);
                    };
                    for (int i = 0; i < int(cols.size()); ++i) {
                        float s = float(i) - t.paint_pos;
                        if (s < -2 || s > 6) continue;
                        float y = cy - s * 18.0f - chip->h * 0.5f, x = cx - chip->w * 0.5f;
                        const SDL_FColor grey{0.66f, 0.66f, 0.70f, 1}; // light outlines (see the 1P page)
                        fill(x, y, chip->w, 1, grey, outline_band);
                        fill(x, y + chip->h - 1, chip->w, 1, grey, outline_band);
                        fill(x, y, 1, chip->h, grey, outline_band);
                        fill(x + chip->w - 1, y, 1, chip->h, grey, outline_band);
                        u32 rgb = cols[size_t(i)].second;
                        fill(x + 1, y + 1, chip->w - 2, chip->h - 2,
                             {float(rgb & 0xFF) / 255.0f, float((rgb >> 8) & 0xFF) / 255.0f,
                              float((rgb >> 16) & 0xFF) / 255.0f, 1},
                             outline_band);
                    }
                    auto arrow = [&](const char* name, float y) {
                        if (const Element* a = el(name)) put_box(a, {cx - a->w * 0.5f, y - a->h * 0.5f, a->w, a->h}, fade, a->z);
                    };
                    if (t.paint > 0) arrow("arrow_up", cy + 18.0f);
                    if (t.paint + 1 < int(cols.size())) arrow("arrow_down", cy - 18.0f);
                }
        }
        // AUTOMATIC / MANUAL, RACING / DRIFT: the layout's buttons, "yes" on the choice.
        if (t.state == 6 || t.state == 8) {
            int v = t.state == 6 ? t.transmission : t.tire_mode;
            const char* l = t.state == 6 ? "auto" : "racing";
            const char* r = t.state == 6 ? "manu" : "dri";
            for (int side = 0; side < 2; ++side) {
                std::string n = std::string(side == 0 ? l : r) + (side == v ? "_yes" : "_no") + sfx;
                if (const Element* b = el(n)) put_box(b, {b->x, b->y, b->w, b->h}, 1, 80.0f);
            }
        }
        // The 3D car (the app's model renderer; player 2's box is shifted by a page width
        // so the drawer can tell the players apart).
        if (const Element* slot = el("car")) {
            ElementDraw d;
            d.scene = ref.scene;
            d.element = slot;
            d.has_box = true;
            d.box = {p == 1 ? 640.0f : 0.0f, 0, 640, 448};
            d.overlay = true;
            d.hide_parts = true;
            d.z = 30.0f + float(p) * 0.01f;
            out.push_back(d);
        }
    }
}

} // namespace gt3r::menu::flow

// ---------------------------------------------------------------------------
// Replay Theater: the file list (Load Replay, Demonstration, the Analyzer's LOAD A / B)
// and the Analyzer's top page. Replays live in native storage (the app's TheaterBackend);
// the pages are the original's (docs/dev/RACE.md "Replay Theater").

namespace gt3r::menu::flow {

void GameFlow::open_theater(TheaterPage::Mode m, TheaterFrom from) {
    const TheaterBackend& b = theater_backend();
    std::vector<TheaterEntry> entries;
    if (b.list) entries = b.list(m == TheaterPage::Mode::Demonstration ? 1 : 0);
    theater_.open(m, std::move(entries));
    theater_from_ = from;
    theater_mode_ = true;
    save_mode_ = name_mode_ = false;
    switch_control("memorycard", "demo_loader", -1);
}

void GameFlow::theater_tick(MenuKeys held) {
    ctl_->tick(0); // the page's own layers only
    const int r = theater_.tick(held, sounds_);
    if (r == 0) return;
    const bool from_logger = theater_from_ != TheaterFrom::Theater;
    if (r < 0) {
        theater_mode_ = false;
        if (from_logger) open_control("logger", "top", -1);
        else switch_control("gt_mode", "replay_theater", theater_.mode() == TheaterPage::Mode::Demonstration ? 1 : 0);
        return;
    }
    const TheaterEntry* e = theater_.chosen();
    if (!e) return;
    if (from_logger) {
        // LOAD A / LOAD B: the track's file and info, back to the Analyzer.
        LoggerTrack& t = logger_tracks_[theater_from_ == TheaterFrom::LoggerA ? 0 : 1];
        t.path = e->id;
        t.texts = e->texts;
        theater_mode_ = false;
        open_control("logger", "top", -1);
        return;
    }
    // Play: the replay (or the disc's demonstration) once, then the theater page again
    // (the original returns to the menu session at replay_theater, focus on its first item).
    GameRequest rq;
    rq.page = "replay_theater";
    if (theater_.mode() == TheaterPage::Mode::Demonstration) {
        rq.mode = "theater_demo";
        rq.race_code = rq.event = e->id;
    } else {
        rq.mode = "theater_replay";
        rq.replay_file = e->id;
    }
    theater_mode_ = false;
    theater_resume_ = true;
    resume_set_ = "gt_mode";
    resume_page_ = "replay_theater";
    resume_focus_ = 0;
    request_race(std::move(rq));
}

void GameFlow::logger_tick(MenuKeys held) {
    ctl_->tick(0);
    ++logger_frames_;
    const MenuKeys pressed = held & MenuKeys(~logger_prev_);
    logger_prev_ = held;
    auto key = [&](MenuKey k) { return (pressed & key_bit(k)) != 0; };
    if (key(MenuKey::Left) || key(MenuKey::Right)) {
        int f = std::clamp(logger_focus_ + (key(MenuKey::Right) ? 1 : -1), 0, 4);
        if (f != logger_focus_) logger_focus_ = f, sounds_.push_back("cursor");
        return;
    }
    const bool exit = key(MenuKey::Cancel) || (key(MenuKey::Confirm) && logger_focus_ == 4);
    if (exit) {
        sounds_.push_back(key(MenuKey::Cancel) ? "cancel" : "ok");
        switch_control("gt_mode", "replay_theater", 2);
        return;
    }
    if (!key(MenuKey::Confirm)) return;
    if (logger_focus_ == 2 || logger_focus_ == 3) {
        sounds_.push_back("ok");
        open_theater(TheaterPage::Mode::LoadTrack, logger_focus_ == 2 ? TheaterFrom::LoggerA : TheaterFrom::LoggerB);
        return;
    }
    // REPLAY / GRAPH: needs a loaded track (the original does nothing without one); a second
    // track on another course is left out.
    const LoggerTrack* a = logger_tracks_[0].path.empty() ? nullptr : &logger_tracks_[0];
    const LoggerTrack* b = logger_tracks_[1].path.empty() ? nullptr : &logger_tracks_[1];
    if (!a && !b) {
        sounds_.push_back("disable");
        return;
    }
    auto course = [](const LoggerTrack* t) {
        for (const auto& [k, v] : t->texts)
            if (k == "course_id") return v;
        return std::string();
    };
    sounds_.push_back("ok");
    GameRequest rq;
    rq.mode = "logger";
    rq.page = "top";
    rq.logger_view = logger_focus_ == 1 ? 1 : 0;
    rq.replay_file = a ? a->path : b->path;
    if (a && b && course(a) == course(b)) rq.replay_file2 = b->path;
    theater_resume_ = true;
    resume_set_ = "logger";
    resume_page_ = "top";
    resume_focus_ = -1;
    request_race(std::move(rq));
}

void GameFlow::logger_draws(std::vector<ElementDraw>& out) const {
    if (!ctl_ || !ctl_->page()) return;
    const MenuSet::PageRef ref = ctl_->page();
    const Page& p = ref.layout();
    auto item = [&](const std::string& n) -> const Element* {
        for (const Element& e : p.items)
            if (e.name == n) return &e;
        for (const Layer& l : p.layers) {
            for (const Element& e : l.elements)
                if (e.name == n) return &e;
        }
        return nullptr;
    };
    // The page's code-placed items are not buttons here: drop any the controller drew.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const ElementDraw& d) {
                                 // (the progress bar shows only while the original processes a track)
                                 return d.element && (d.element->name == "RawWindow_TrackSelect" ||
                                                      d.element->name.rfind("LoggerProgressBar", 0) == 0 ||
                                                      d.element->name.rfind("progress_", 0) == 0);
                             }),
              out.end());
    float z = 1000;
    auto put = [&](const Element* e, float dx, float dy, const std::string* text = nullptr, float brightness = 1) {
        if (!e) return;
        ElementDraw d;
        d.scene = ref.scene;
        d.element = e;
        d.dx = dx, d.dy = dy;
        d.z = z += 1;
        d.brightness = brightness;
        if (text) d.text = std::make_shared<const std::string>(*text);
        out.push_back(d);
    };
    // Track info panels: the LoggerTrackInfo template's parts moved onto TRACK A / TRACK B.
    const Element* tmpl = item("LoggerTrackInfo");
    static const char* const kFields[] = {"car", "power", "course", "time", "date", "mode", "comment"};
    for (int t = 0; t < 2 && tmpl; ++t) {
        const Element* inst = item(t == 0 ? "LoggerTrackInfo_TrackA" : "LoggerTrackInfo_TrackB");
        if (!inst) continue;
        const float dx = inst->x - tmpl->x, dy = inst->y - tmpl->y;
        if (const Element* slot = item("item_load"))
            if (const Element* g = item(t == 0 ? "gadget_load_a" : "gadget_load_b")) put(g, slot->x - g->x + dx, slot->y - g->y + dy);
        const LoggerTrack& tr = logger_tracks_[t];
        for (const char* f : kFields) {
            put(item(std::string("list_track_") + f), dx, dy);
            std::string v;
            for (const auto& [k, val] : tr.texts)
                if (k == f) v = val;
            put(item(std::string("list_track_") + f + "_info"), dx, dy, &v);
        }
    }
    // The mode buttons: REPLAY and GRAPH dim while no track is loaded (unless focused).
    static const char* const kButtons[] = {"RawListItemFixed_Replay", "RawListItemFixed_Graph", "RawListItemFixed_TrackA",
                                           "RawListItemFixed_TrackB", "RawListItemFixed_Exit"};
    const bool any = !logger_tracks_[0].path.empty() || !logger_tracks_[1].path.empty();
    for (int i = 0; i < 5; ++i) {
        float b = i == logger_focus_ ? 1.0f : (i < 2 && !any) ? 0.35f : 0.75f;
        put(item(kButtons[i]), 0, 0, nullptr, b);
        if (i == logger_focus_)
            if (const Element* e = item(kButtons[i])) {
                // Focus: the button pulses brighter (a soft additive pass, M).
                put(e, 0, 0, nullptr, 1);
                out.back().additive = true;
                out.back().alpha = 0.25f + 0.15f * float(std::sin(double(logger_frames_) * 0.1));
            }
    }
}

} // namespace gt3r::menu::flow
