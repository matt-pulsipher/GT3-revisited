#include "race/results.h"

#include "base/log.h"
#include "formats/menu/mbl.h"
#include "game/data/car_database.h"
#include "menu/menu_text.h"
#include "race/machine_test.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gt3r::race {

namespace {

constexpr float kMenuW = 640, kMenuH = 448;

// Menu frame (640x448, y down) -> UI coordinates inside `dst`.
struct Frame {
    SDL_FRect dst;
    float x(float mx) const { return dst.x + mx * dst.w / kMenuW; }
    float y(float my) const { return dst.y + my * dst.h / kMenuH; }
    float sx() const { return dst.w / kMenuW; }
    float sy() const { return dst.h / kMenuH; }
};

std::string fmt_time(double s) {
    int ms = int(std::lround(s * 1000.0));
    char b[32];
    if (ms >= 3600000) std::snprintf(b, sizeof(b), "%d:%02d'%02d.%03d", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
    else std::snprintf(b, sizeof(b), "%d'%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return b;
}

// `game`: `s` is game text (a database name: escapes and JIS pairs, drawn as the game draws them).
constexpr bool kGameText = true;

void text(SDL_Renderer*, text::GameFonts& fonts, const Frame& f, float mx, float my, const std::string& s,
          const char* font, float scale_x, float scale_y, SDL_Color c, text::HAlign a, text::VAlign v = text::VAlign::Top,
          bool game = false) {
    text::TextStyle st;
    text::GameFont* g = fonts.resolve(font, st);
    if (!g) return;
    st.scale_x *= scale_x * f.sx();
    st.scale_y *= scale_y * f.sy();
    st.color = c;
    st.align = a;
    st.valign = v;
    if (game) g->draw_game(f.x(mx), f.y(my), s, st);
    else g->draw(f.x(mx), f.y(my), s, st);
}

// The element's box in menu units (pmb: x/y = bottom-left, y up) at a new top edge.
SDL_FRect box_at_top(const menu::Element& e, float y_top) { return {e.x, kMenuH - y_top - e.h, e.w, e.h}; }

const menu::Element* find(const menu::Page& p, const std::string& name) {
    for (const menu::Layer& l : p.layers) {
        for (const menu::Element& e : l.elements)
            if (e.name == name) return &e;
        for (const menu::Element& e : l.widgets)
            if (e.name == name) return &e;
    }
    for (const menu::Element& e : p.items)
        if (e.name == name) return &e;
    return nullptr;
}

} // namespace

std::string result_car_name(const data::CarDatabase& db, const std::string& label, bool full) {
    std::string car = db.car(label) ? label : "";
    if (car.empty())
        if (const data::CarVariant* v = db.variant(label)) car = v->car;
    if (car.empty())
        for (const data::CarVariant& v : db.arcade_cars())
            if (v.label == label) car = v.car;
    const data::Car* c = db.car(car);
    if (!c || c->latin_name.empty()) return label;
    std::string n = c->latin_name;
    const std::string& alt = c->latin_name_alt;
    if (full && !alt.empty() && alt.find_first_not_of(' ') != std::string::npos) n += " " + alt;
    return n;
}

std::string result_time_text(bool winner, bool finished, double time, double winner_time, int laps_behind) {
    if (!finished) return "Running...";
    if (winner) return " " + fmt_time(time);
    if (laps_behind > 0) return "+" + std::to_string(laps_behind) + (laps_behind < 2 ? " lap" : " laps");
    return "+" + fmt_time(std::max(0.0, time - winner_time));
}

// --- ResultsOverlay -------------------------------------------------------------------

bool ResultsOverlay::load(const Vfs& vfs, SDL_Renderer* r, const std::string& lang) {
    try {
        scene_ = menu::MenuScene::load(vfs, "menu/" + lang + "/result1.pmb");
    } catch (const std::exception& e) {
        LOG_WARN("Results: %s", e.what());
        scene_.reset();
        return false;
    }
    renderer_ = std::make_unique<menu::MenuRenderer>(r);
    renderer_->set_scene(&*scene_);
    return true;
}

void ResultsOverlay::draw(SDL_Renderer* r, text::GameFonts& fonts, const std::vector<ResultRow>& rows, double t,
                          const SDL_FRect& dst, std::vector<SDL_FRect>* icons) {
    if (icons) icons->assign(rows.size(), SDL_FRect{0, 0, 0, 0});
    if (!scene_ || t < 5.0 || scene_->file().pages.empty()) return;
    const menu::Page& page = scene_->file().pages[0];
    const menu::Element* title = find(page, "b_title_result");
    const menu::Element* bar = find(page, "b_bar");
    const menu::Element* strip = find(page, "z_8_black1");
    const menu::Element* term = find(page, "b_term");
    const menu::Element* term_strip = find(page, "z8_black8");
    const menu::Element* seiko = find(page, "z9_seiko");
    if (!title || !bar) return;
    Frame f{dst};
    size_t n = rows.size();
    // Bars: title (52 high) at y 44, then the rows and the term bar unroll downwards: each
    // bar's centre approaches (hPrev + hOwn) / 2 below the previous one by 0.19 per frame.
    // Without per-frame state we evaluate the closed form of that approach.
    auto slide = [](double since) {
        if (since <= 0) return 0.0f;
        double frames = since * 60.0;
        return float(1.0 - std::pow(1.0 - 0.19, frames));
    };
    auto fade = [](double since, double rate) { return float(std::clamp(since * rate, 0.0, 1.0)); };
    float title_top = 44;
    std::vector<menu::ElementDraw> list;
    auto add = [&](const menu::Element* e, float y_top, float alpha, float clip_top) {
        if (!e) return;
        menu::ElementDraw d;
        d.element = e;
        d.has_box = true;
        d.box = box_at_top(*e, y_top);
        d.alpha = alpha;
        d.has_clip = true;
        d.clip = {0, 0, kMenuW, kMenuH - clip_top}; // nothing above the previous bar's centre line
        list.push_back(d);
    };
    float title_alpha = fade(t - 5.0, 2.0);
    add(title, title_top, title_alpha, 0);
    float prev_centre = title_top + title->h / 2, prev_h = title->h;
    std::vector<float> row_top(n), row_alpha(n);
    for (size_t i = 0; i <= n; ++i) {
        bool is_term = i == n;
        double start = 6.0 + 0.12 * double(i);
        if (t < start) break;
        const menu::Element* e = is_term ? term : bar;
        if (!e) break;
        float target = (prev_h + e->h) / 2;
        float centre = prev_centre + target * slide(t - start);
        float top = centre - e->h / 2;
        float alpha = fade(t - start, is_term ? 2.0 : 2.5);
        add(e, top, alpha, prev_centre);
        add(is_term ? term_strip : strip, top, alpha, prev_centre);
        if (is_term) add(seiko, top + 2, alpha, prev_centre);
        else row_top[i] = top, row_alpha[i] = alpha;
        prev_centre = centre;
        prev_h = e->h;
    }
    // The title bar's text ("RACE RESULTS", sw_20#1) in the game font.
    if (fonts.loaded() && !drawer_set_) renderer_->set_text_drawer(menu::make_game_font_drawer(fonts)), drawer_set_ = true;
    renderer_->draw_elements(list, menu::RenderOptions{false, false, true, true}, dst);
    // Row texts (game fonts): rank, car name (t_car_name x 132 + the 12-unit text inset),
    // time (t_time right edge 612 - 12; the reference capture).
    for (size_t i = 0; i < n; ++i) {
        if (row_alpha[i] <= 0) continue;
        u8 a = u8(255 * row_alpha[i]);
        SDL_Color others{0xCC, 0xCC, 0xCC, a}, mine{0xFF, 0xC8, 0x00, a};
        SDL_Color c = rows[i].player ? mine : others;
        float y = row_top[i] + 18;
        text(r, fonts, f, 24 + 18, y, std::to_string(i + 1), "rank#1", 0.5f, 0.5f, others, text::HAlign::Center,
             text::VAlign::Middle);
        if (icons) (*icons)[i] = {f.x(66), f.y(y - 13), 58 * f.sx(), 26 * f.sy()}; // car picture at 66..124
        text(r, fonts, f, 144, y, rows[i].name, "sw_16#1", 0.87f, 1.0f, c, text::HAlign::Left,
             text::VAlign::Middle, kGameText);
        text(r, fonts, f, 600, y, rows[i].time, "sw_16#1", 0.87f, 1.0f, c, text::HAlign::Right, text::VAlign::Middle);
    }
}

// --- TimeTrialOverlay -------------------------------------------------------------------

int record_rank(const std::vector<double>& table, double value, bool higher_is_better) {
    for (int i = 0; i < kRecordPlaces; ++i) {
        if (size_t(i) >= table.size()) return i;
        const double e = table[size_t(i)];
        if (higher_is_better ? e <= value : e >= value) return i;
    }
    return -1;
}

bool sets_record(const RecordFinish& f) {
    if (!f.player || f.replay || f.failed || f.time < 0) return false;
    return f.session_best < 0 || std::llround(f.time * 1000.0) <= std::llround(f.session_best * 1000.0);
}

bool locks_retry(const RecordFinish& f) { return sets_record(f) && f.result != 0; }

const char* TimeTrialOverlay::record_label(int rank, bool speed) {
    if (rank < 0 || rank >= kRecordPlaces) return nullptr;
    return rank == 0 ? "NEW RECORD" : speed ? "RECORD SPEED" : "RECORD TIME";
}

void TimeTrialOverlay::draw(SDL_Renderer* r, text::GameFonts& fonts, double t, const Data& d, const SDL_FRect& dst) {
    if (t < kShowDelay) return;
    Frame f{dst};
    SDL_Color amber{208, 128, 24, 255}, shadow{0, 0, 0, 255}; // GS 0x801880D0
    auto both = [&](float x, float y, const std::string& s, const char* font, float sc, SDL_Color c, text::HAlign a) {
        text(r, fonts, f, x + 2, y + 2, s, font, sc, sc, shadow, a);
        text(r, fonts, f, x, y, s, font, sc, sc, c, a);
    };
    if (const char* label = record_label(d.rank, d.speed)) {
        static const char* const kRank[] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th", "9th", "10th", "11th", "12th"};
        both(320, 150, kRank[d.rank], "rank", 1, amber, text::HAlign::Center);
        // The label flickers every frame between orange and light grey.
        bool odd = int(t * 60) & 1;
        SDL_Color flick = odd ? SDL_Color{204, 102, 0, 255} : SDL_Color{208, 208, 208, 255};
        both(320, 200, label, "sw_20", 1, flick, text::HAlign::Center);
    }
    if (d.speed) { // the top-speed test: "Max Speed" further left, the speed further right
        both(160, 270, "Max Speed", "sw_16#1", 1, amber, text::HAlign::Left);
        both(480, 266, record_speed_text(d.value, d.imperial), "sw_20#1", 1.3f, amber, text::HAlign::Right);
        return;
    }
    both(200, 270, "Lap", "sw_16#1", 1, amber, text::HAlign::Left);
    both(440, 266, d.value >= 0 ? fmt_time(d.value) : "-'--.---", "sw_20#1", 1.3f, amber, text::HAlign::Right);
}

// --- LicenseResultOverlay ------------------------------------------------------------

int LicenseResultOverlay::skippable_from(int medal, bool time_trial) {
    if (medal >= 0) return model_start(medal) + kTextFrames + kHold;
    return time_trial ? fail_text_start() + kTextFrames + kHold : -1;
}

int LicenseResultOverlay::end_frame(int medal, bool time_trial) {
    if (medal >= 0) return model_start(medal) + kTextFrames + 2 * kHold;
    return fail_text_start() + kTextFrames + kHold + (time_trial ? 240 : 48);
}

float LicenseResultOverlay::model_weight(int frame, int medal) {
    return std::clamp(float(frame - model_start(medal)) / 60.0f, 0.0f, 1.0f);
}

void LicenseResultOverlay::draw_dim(SDL_Renderer* r, int frame, const SDL_FRect& dst) {
    float t1 = std::clamp(float(frame - kDimStart) / float(kDimFrames), 0.0f, 1.0f);
    if (t1 <= 0) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, u8(std::lround(255.0f * t1 / 3.0f)));
    SDL_RenderFillRect(r, &dst);
}

void LicenseResultOverlay::draw_medal(SDL_Renderer* r, text::GameFonts& fonts, int frame, int medal, double time,
                                      const SDL_FRect& dst) {
    if (medal < 0 || medal > 2) return;
    int s = model_start(medal);
    if (frame < s) return;
    Frame f{dst};
    static const SDL_Color kColors[] = {{255, 215, 0, 255}, {224, 238, 224, 255}, {208, 150, 122, 255}};
    static const char* const kWords[] = {"GOLD", "SILVER", "BRONZE"};
    float t2 = std::clamp(float(frame - s) / 30.0f, 0.0f, 1.0f); // the word: alpha 0 -> 0x80
    float ft = std::clamp(float(frame - s) / 60.0f, 0.0f, 1.0f); // the time's fader
    SDL_Color c = kColors[medal];
    SDL_Color shadow{0, 0, 0, u8(255 * t2)};
    c.a = u8(255 * t2);
    text(r, fonts, f, 320, 336, kWords[medal], "sw_20", 1.25f, 1.25f, shadow, text::HAlign::Center, text::VAlign::Middle);
    text(r, fonts, f, 322, 334, kWords[medal], "sw_20", 1.25f, 1.25f, c, text::HAlign::Center, text::VAlign::Middle);
    SDL_Color tc = kColors[medal];
    tc.a = u8(255 * ft);
    text(r, fonts, f, 320, 112, fmt_time(time), "sw_20", 1.25f, 1.25f, tc, text::HAlign::Center, text::VAlign::Middle);
}

void LicenseResultOverlay::draw_fail(SDL_Renderer* r, text::GameFonts& fonts, int frame, const std::string& msg, double time,
                                     const SDL_FRect& dst) {
    float t1 = std::clamp(float(frame - kFailDelay) / float(kWashFrames), 0.0f, 1.0f);
    if (t1 <= 0) return;
    // The wash: (160, 160, 255); the original's 8-bit alpha wraps to 0 at the end of the ramp
    // (a bug), so it is held at about half, as the spec recommends.
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 160, 160, 255, u8(std::lround(128.0f * t1)));
    SDL_RenderFillRect(r, &dst);
    int s = fail_text_start();
    if (frame < s) return;
    Frame f{dst};
    float t2 = std::clamp(float(frame - s) / 30.0f, 0.0f, 1.0f);
    float ft = std::clamp(float(frame - s) / 60.0f, 0.0f, 1.0f);
    SDL_Color c{160, 192, 64, u8(255 * std::min(1.0f, 2 * t2))}; // u8(t2 x 256) in 0x80 units: full from t2 = 0.5
    float split = float(int((1 - t2) * 30));
    text(r, fonts, f, 320, 224 - split, msg, "sw_20", 1.25f, 1.25f, c, text::HAlign::Center, text::VAlign::Middle);
    if (split > 0)
        text(r, fonts, f, 320, 224 + split, msg, "sw_20", 1.25f, 1.25f, c, text::HAlign::Center, text::VAlign::Middle);
    if (time >= 0) {
        SDL_Color tc{160, 192, 64, u8(255 * ft)};
        text(r, fonts, f, 320, 140, fmt_time(time), "sw_20", 1.25f, 1.25f, tc, text::HAlign::Center, text::VAlign::Middle);
    }
}

// --- QuickMenu -------------------------------------------------------------------

QmAction quick_menu_action(std::string_view a) {
    static const struct {
        const char* name;
        QmAction action;
    } kActions[] = {
        {"qm_start", QmAction::Start},           {"qm_try_again", QmAction::Start},
        {"qm_free_run", QmAction::FreeRun},      {"qm_replay", QmAction::Replay},
        {"qm_demo", QmAction::Demo},             {"qm_save_replay", QmAction::SaveReplay},
        {"qm_save_ghost", QmAction::SaveGhost},  {"qm_load_ghost", QmAction::LoadGhost},
        {"qm_records", QmAction::Records},       {"qm_logger", QmAction::Logger},
        {"qm_option", QmAction::Option},         {"qm_setting", QmAction::Setting},
        {"qm_next_session", QmAction::NextSession}, {"qm_save_game", QmAction::SaveGame},
        {"qm_exit", QmAction::Exit},             {"qm_yes", QmAction::Yes},
        {"qm_no", QmAction::No},
    };
    for (const auto& k : kActions)
        if (a == k.name) return k.action;
    return QmAction::None;
}

QuickMenu::QuickMenu() = default;
QuickMenu::~QuickMenu() = default;

bool QuickMenu::load(const Vfs& vfs, SDL_Renderer* r, text::GameFonts& fonts, const std::string& pmb,
                     const std::string& lang) {
    return load(menu::flow::vfs_reader(vfs), r, fonts, pmb, lang);
}

bool QuickMenu::load(const menu::flow::FileReader& read, SDL_Renderer* r, text::GameFonts& fonts,
                     const std::string& pmb, const std::string& lang) {
    ctl_.reset();
    set_.reset();
    texts_.clear();
    class_texts_.clear();
    inset_.reset();
    inset_renderer_.reset();
    sounds_.clear();
    ignore_ = 0xFFFF;
    acc_ = 0;
    // The root page's layout number is 1: "qm_arc_result1" -> "qm_arc_result",
    // "qm_gt_mt_4001" -> "qm_gt_mt_400" (the set name may end in digits itself).
    std::string set = pmb.substr(0, pmb.find('.'));
    if (!set.empty() && set.back() == '1') set.pop_back();
    set_name_ = set;
    try {
        set_ = std::make_unique<menu::flow::MenuSet>(read, set, lang);
    } catch (const std::exception& e) {
        LOG_WARN("Quick menu %s: %s", pmb.c_str(), e.what());
        return false;
    }
    ctl_ = std::make_unique<menu::flow::MenuController>(*set_);
    // The root page's items by name (menus §11); dialog pages have no rule.
    items_->reset();
    ctl_->enabled = [items = items_](const std::string& page, const menu::MblItem& it) {
        return page != "root" || !*items || (*items)->enabled(it.name);
    };
    if (!ctl_->open("root")) {
        ctl_.reset();
        set_.reset();
        return false;
    }
    renderer_ = std::make_unique<menu::MenuRenderer>(r);
    if (fonts.loaded()) renderer_->set_text_drawer(menu::make_game_font_drawer(fonts));
    return true;
}

bool QuickMenu::add_page_layout(const Vfs& vfs, SDL_Renderer* r, const std::string& mbl_page, const std::string& lang) {
    if (!set_) return false;
    const menu::MblFile& mbl = set_->flow();
    int mp = mbl.find_page(mbl_page);
    if (mp < 0 || !mbl.pages[size_t(mp)].unk40) return false;
    std::string path = "menu/" + lang + "/" + set_name_ + std::to_string(mbl.pages[size_t(mp)].unk40) + ".pmb";
    try {
        inset_ = menu::MenuScene::load(vfs, path);
    } catch (const std::exception& e) {
        LOG_WARN("Quick menu %s: %s", path.c_str(), e.what());
        return false;
    }
    inset_renderer_ = std::make_unique<menu::MenuRenderer>(r);
    inset_renderer_->set_scene(&*inset_);
    return true;
}

void QuickMenu::reopen_root(const std::string& focus_item) {
    if (!ctl_) return;
    ctl_->open("root");
    if (!focus_item.empty()) ctl_->set_focus(focus_item);
}

// The page's EXIT item (action qm_exit) as a choice (a menu ended without a choice reports
// Exit, whether the page has the item or not).
QuickMenu::Choice QuickMenu::exit_choice(const std::string& page_name) const {
    const menu::MblFile& mbl = set_->flow();
    if (int mp = mbl.find_page(page_name); mp >= 0)
        for (const menu::MblItem& it : mbl.pages[size_t(mp)].items)
            if (quick_menu_action(it.action) == QmAction::Exit) return {it.name, page_name, it.action};
    return {"exit", page_name, "qm_exit"};
}

QuickMenu::Choice QuickMenu::tick(u16 held) {
    Choice out;
    if (!ctl_) return out;
    ignore_ &= held; // a key held from before the menu opened counts once released
    ctl_->tick(menu::flow::MenuKeys(held & ~ignore_));
    for (std::string& s : ctl_->take_sounds()) sounds_.push_back(std::move(s));
    for (const menu::flow::MenuEvent& e : ctl_->take_events()) {
        using K = menu::flow::MenuEvent::Kind;
        if (e.kind == K::Finish && !e.item.empty()) {
            out = {e.item, e.page, e.action};
            // The menu stays up behind whatever the item opens (panels, toasts); a page
            // change (start, replay, exit) replaces it anyway.
            reopen_root(e.page == "root" ? e.item : std::string());
        } else if (e.kind == K::Action && e.page != "root" && !e.item.empty()) {
            out = {e.item, e.page, e.action}; // dialog yes / no (kind 3 closes the popup)
        } else if (e.kind == K::Exit) {
            // Triangle / Square on a page that allows it (flag 0x100: every race quick menu's
            // root but the championship ones): the menu ends at once, as EXIT (menus §11).
            out = exit_choice(e.page);
        }
    }
    return out;
}

QuickMenu::Choice QuickMenu::update(u16 held, double dt) {
    Choice out;
    acc_ += dt;
    int n = 0;
    while (acc_ >= 1.0 / 60.0 && n < 8) {
        acc_ -= 1.0 / 60.0;
        ++n;
        Choice c = tick(held);
        if (!c.empty() && out.empty()) out = c;
        if (!out.empty()) break; // the caller acts on it before the next frame
    }
    if (n >= 8) acc_ = 0;
    return out;
}

std::vector<std::string> QuickMenu::take_sounds() { return std::exchange(sounds_, {}); }

const std::string& QuickMenu::focused() const {
    static const std::string none;
    if (!ctl_ || !ctl_->has_page()) return none;
    const menu::flow::MenuController::ItemState* s = ctl_->focused();
    return s && s->flow ? s->flow->name : none;
}

bool QuickMenu::focus(const std::string& item) { return ctl_ && ctl_->set_focus(item); }

bool QuickMenu::item_enabled(const std::string& item) const {
    return page() != "root" || !*items_ || (*items_)->enabled(item);
}

bool QuickMenu::open_page(const std::string& name) {
    if (!ctl_) return false;
    if (name == "root") {
        std::string f = ctl_->depth() == 0 ? focused() : std::string();
        if (ctl_->depth() > 0 || !ctl_->has_page()) reopen_root(f);
        return true;
    }
    if (!set_->page(name)) return false;
    return ctl_->popup(name);
}

bool QuickMenu::on_root() const { return !ctl_ || ctl_->depth() == 0; }

const std::string& QuickMenu::page() const {
    static const std::string none;
    return ctl_ && ctl_->has_page() ? ctl_->page_name() : none;
}

bool QuickMenu::busy() const { return ctl_ && ctl_->busy(); }

std::string QuickMenu::link(const std::string& item) const {
    if (!set_) return {};
    const menu::MblFile& mbl = set_->flow();
    int mp = mbl.find_page(page());
    if (mp < 0) return {};
    for (const menu::MblItem& it : mbl.pages[size_t(mp)].items)
        if (it.name == item) return it.link;
    return {};
}

std::string QuickMenu::action(const std::string& item) const {
    if (!set_) return {};
    const menu::MblFile& mbl = set_->flow();
    int mp = mbl.find_page(page());
    if (mp < 0) return {};
    for (const menu::MblItem& it : mbl.pages[size_t(mp)].items)
        if (it.name == item) return it.action;
    return {};
}

void QuickMenu::draw(SDL_Renderer*, const SDL_FRect& dst) {
    if (!ctl_ || !renderer_) return;
    std::vector<menu::ElementDraw> list = ctl_->draw_list();
    // Code-filled widgets: the result list (rank<i>_name / _time) and named texts.
    for (menu::ElementDraw& d : list) {
        if (!d.element || d.kind != menu::ElementDraw::Kind::Element) continue;
        const std::string& n = d.element->name;
        if (auto it = texts_.find(n); it != texts_.end()) {
            d.text = std::make_shared<const std::string>(it->second);
            continue;
        }
        if (d.element->widget_class && (d.element->type & menu::Element::kText))
            if (auto it = class_texts_.find(*d.element->widget_class); it != class_texts_.end()) {
                d.text = std::make_shared<const std::string>(it->second);
                continue;
            }
        if (n.size() > 6 && n.compare(0, 4, "rank") == 0) {
            size_t us = n.find('_');
            int i = std::atoi(n.c_str() + 4) - 1;
            if (us == std::string::npos || i < 0) continue;
            std::string kind = n.substr(us);
            if (kind != "_name" && kind != "_time") continue;
            d.text = std::make_shared<const std::string>(
                size_t(i) < rows_.size() ? (kind == "_name" ? rows_[size_t(i)].name : rows_[size_t(i)].time) : std::string());
            // The player's row in orange (reference captures: about (255, 116, 0)).
            if (size_t(i) < rows_.size() && rows_[size_t(i)].player) {
                d.has_text_color = true;
                d.text_color = {1.0f, 116.0f / 255.0f, 0.0f, 1.0f};
            }
        }
    }
    menu::apply_text_inset(list);
    menu::RenderOptions ro{false, false, true, true};
    renderer_->draw_elements(list, ro, dst);
    if (inset_ && on_root() && !inset_->file().pages.empty()) {
        menu::SceneOptions io;
        io.show_items = false;
        inset_renderer_->draw(0, io, ro, dst);
    }
}

} // namespace gt3r::race
