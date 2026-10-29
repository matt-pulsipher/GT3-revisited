// First-run screen (asks for the user's own Gran Turismo 3 disc image, or
// offers the recent ones when the remembered disc is gone), the home menu
// under the game (player screens) and Developer options (every other tool). Both lay out to the whole window (Screen::adaptive_ui) with
// finger-sized targets on phones.

#include "app/app.h"
#include "app/screen_registry.h"
#include "app/updates.h"

#include "base/file.h"
#include "gt3r_version.h"
#include "platform/recent_discs.h"

#include <algorithm>
#include <cmath>

namespace gt3r {

namespace {

// iOS apps do not quit themselves (Apple's guidelines; the app switcher closes them): no Quit
// item there, and Triangle on these top screens stays where it is.
#if defined(SDL_PLATFORM_IOS)
constexpr bool kCanQuit = false;
#else
constexpr bool kCanQuit = true;
#endif

bool disc_available(const std::string& path) {
    return path.find("://") != std::string::npos || host_path_exists(path); // URIs can't be checked cheaply
}

constexpr const char* kVersion = "v" GT3R_VERSION;

// Top bar shared by both screens; returns its bottom edge. When a newer release exists (or this
// version is too old for online play), a small badge beside the version opens the update dialog
// (app/updates.h): `badge` gets its rect (empty: none) for taps; `badge_focused` highlights it.
float title_bar(Ui& ui, std::string_view subtitle, UiRect* badge = nullptr, bool badge_focused = false,
                bool badge_pressed = false) {
    const UiRect& S = ui.safe();
    const float h = ui.touch() ? 30.0f : 22.0f;
    ui.fill(0, 0, ui.width(), S.y + h, colors::kPanel);
    ui.fill(0, S.y + h, ui.width(), 1, colors::kOutline);
    const float ty = std::floor(S.y + (h - Ui::kChar) / 2);
    ui.text(S.x + 12, ty, "GT3 REVISITED", colors::kAccent);
    if (!subtitle.empty()) ui.text(S.x + 12 + Ui::text_width("GT3 REVISITED") + 12, ty, subtitle, colors::kDim);
    ui.text_right(S.right() - 12, ty, kVersion, colors::kDim);
    if (badge) *badge = {};
    if (std::string b = updates().badge_text(); !b.empty() && badge) {
        const bool req = updates().required();
        const float bw = Ui::text_width(b) + 16, bh = h - (ui.touch() ? 6 : 6);
        const UiRect r{std::floor(S.right() - 12 - Ui::text_width(kVersion) - 10 - bw), std::floor(S.y + (h - bh) / 2), bw, bh};
        *badge = r;
        const Color c = req ? colors::kBad : colors::kAccent;
        ui.fill_round(r.x, r.y, r.w, r.h, r.h / 2, badge_pressed ? Color{62, 70, 100} : badge_focused ? colors::kHighlight
                                                                                      : Color{c.r, c.g, c.b, 40});
        ui.frame_round(r.x, r.y, r.w, r.h, r.h / 2, 1, badge_focused ? colors::kText : c);
        ui.text_center_in(r.x, r.w, ty, b, c);
    }
    return S.y + h + 1;
}

class SetupScreen : public Screen {
public:
    void on_event(App& app, const SDL_Event& e) override {
        switch (gesture_.feed(e)) {
        case Gesture::Result::Down:
            pressed_ = item_at(gesture_.x(), gesture_.y());
            badge_pressed_ = badge_.w > 0 && badge_.contains(gesture_.x(), gesture_.y());
            break;
        case Gesture::Result::Tap: {
            if (badge_pressed_ && badge_.contains(gesture_.x(), gesture_.y())) updates().open_dialog();
            badge_pressed_ = false;
            int i = pressed_;
            pressed_ = -1;
            if (i >= 0 && i == item_at(gesture_.x(), gesture_.y())) {
                sel_ = i;
                activate(app);
            }
            break;
        }
        case Gesture::Result::DragStart:
        case Gesture::Result::Up: pressed_ = -1; break;
        default: break;
        }
    }

    void update(App& app, double) override {
        rebuild(app);
        const MenuInput& m = app.menu();
        int n = int(items_.size());
        sel_ = std::clamp(sel_, 0, n - 1);
        sel_ = (sel_ + m.vertical() + n) % n;
        if (int h = m.horizontal()) { // between the columns: recent discs <-> buttons
            if (h < 0 && sel_ >= recent_count_ && recent_count_ > 0) sel_ = 0;
            if (h > 0 && sel_ < recent_count_) sel_ = recent_count_;
        }
        if (m.pressed(PadButton::Cross) || m.pressed(PadButton::Start)) activate(app);
        if (m.pressed(PadButton::Triangle) && kCanQuit) app.quit();
    }

    void render(App& app, Ui& ui) override {
        rebuild(app);
        const UiRect& S = ui.safe();
        const bool touch = ui.touch();
        const float top = title_bar(ui, "", &badge_, false, badge_pressed_ && gesture_.active());
        const float pad = touch ? 16.0f : 14.0f;
        const bool two_cols = S.w >= 460;
        const float bottom = S.bottom() - (touch ? 6 : 20);
        const float lx = S.x + pad, lw = two_cols ? std::floor(S.w * 0.47f) - pad : S.w - 2 * pad;
        rects_.assign(items_.size(), UiRect{});

        // Left column: what this is, and the main actions.
        float y = top + (touch ? 14 : 16);
        ui.text_big(lx, y, "Welcome", 2, colors::kText);
        y += 24;
        for (const std::string& line :
             Ui::wrap("An open reimplementation of the Gran Turismo 3 engine. No game data is included.", lw)) {
            ui.text(lx, y, line, colors::kText);
            y += 11;
        }
        y += 4;
        for (const std::string& line :
             Ui::wrap("To play, open a disc image (.iso) made from your own copy of Gran Turismo 3 A-Spec.", lw)) {
            ui.text(lx, y, line, colors::kDim);
            y += 11;
        }
        y += touch ? 10 : 10;
        const float bh = touch ? 40.0f : 28.0f, sh = touch ? 32.0f : 22.0f;
        for (size_t i = 0; i < items_.size(); ++i) {
            const Item& it = items_[i];
            if (it.kind == Item::Browse) rects_[i] = {lx, y, lw, bh};
            if (it.kind == Item::Settings) rects_[i] = {lx, y + bh + 8, kCanQuit ? std::floor((lw - 8) / 2) : lw, sh};
            if (it.kind == Item::Quit) rects_[i] = {lx + std::floor((lw - 8) / 2) + 8, y + bh + 8, std::floor((lw - 8) / 2), sh};
        }
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
        if (!touch) ui.text(lx, y + bh + 8 + sh + 10, "or drop the .iso onto this window", colors::kDim);
#endif

        // Right column (or below): a disc that could not be opened, recent discs.
        const float rx = two_cols ? S.x + std::floor(S.w * 0.47f) + pad : lx;
        const float rw = two_cols ? S.right() - pad - rx : lw;
        float ry = two_cols ? top + (touch ? 14 : 16) : y + bh + sh + 24;
        const float rtw = rw - 16; // text width inside the boxes
        if (!app.failed_disc_path().empty() || !app.last_error().empty()) {
            std::vector<std::string> lines;
            lines.push_back(app.failed_disc_path().empty() ? "Could not use that file:" : "Could not open the disc image:");
            if (!app.failed_disc_path().empty()) lines.push_back(Ui::elide(app.failed_disc_path(), rtw));
            // Disc errors often repeat the path; drop it to keep the reason readable.
            std::string err = app.last_error();
            if (const std::string& fp = app.failed_disc_path(); !fp.empty())
                if (size_t at = err.find(fp); at != std::string::npos) err = err.substr(0, at) + "(the file above)" + err.substr(at + fp.size());
            std::vector<std::string> why = Ui::wrap(err, rtw);
            if (why.size() > 2) {
                for (size_t k = 2; k < why.size(); ++k) why[1] += " " + why[k];
                why.resize(2), why[1] = Ui::clip(why[1], rtw);
            }
            lines.insert(lines.end(), why.begin(), why.end());
            const float h = 10 + 11 * float(lines.size());
            ui.fill_round(rx, ry, rw, h, 5, Color{60, 26, 26});
            ui.frame_round(rx, ry, rw, h, 5, 1, colors::kBad);
            for (size_t i = 0; i < lines.size(); ++i)
                ui.text(rx + 8, ry + 6 + 11 * float(i), lines[i], i == 0 ? colors::kBad : i == 1 && !app.failed_disc_path().empty() ? colors::kText : colors::kDim);
            ry += h + 10;
        }
        ui.text(rx, ry, "Recent disc images", colors::kAccent);
        ry += 14;
        const float rh = touch ? 40.0f : 34.0f;
        if (recent_count_ == 0) {
            ui.frame_round(rx, ry, rw, rh, 5, 1, Color{50, 54, 70});
            ui.text_center_in(rx, rw, std::floor(ry + (rh - Ui::kChar) / 2), "None yet", colors::kDim);
        }
        for (int i = 0; i < recent_count_; ++i) {
            if (ry + rh > bottom) break; // no room: the rest stays reachable by keys only
            rects_[size_t(i)] = {rx, ry, rw, rh - 4};
            ry += rh;
        }

        for (size_t i = 0; i < items_.size(); ++i) {
            const Item& it = items_[i];
            const UiRect& r = rects_[i];
            if (r.w <= 0) continue;
            const bool focused = int(i) == sel_, pressed = int(i) == pressed_ && gesture_.active();
            if (it.kind == Item::Recent) {
                ui.fill_round(r.x, r.y, r.w, r.h, 5, pressed ? Color{62, 70, 100} : focused ? colors::kHighlight : colors::kPanel);
                ui.frame_round(r.x, r.y, r.w, r.h, 5, 1, focused ? colors::kAccent : colors::kOutline);
                std::string name = platform::disc_file_name(it.path);
                std::string dir = it.path.substr(0, it.path.size() >= name.size() ? it.path.size() - name.size() : 0);
                const float mid = r.y + r.h / 2;
                ui.text(r.x + 10, std::floor(mid - 10), Ui::elide(name, rtw - 16), focused ? colors::kAccent : colors::kText);
                ui.text(r.x + 10, std::floor(mid + 2), Ui::elide(dir.empty() ? "(picked file)" : dir, rtw - 16), colors::kDim);
                ui.text_right(r.right() - 8, std::floor(mid - 4), ">", focused ? colors::kAccent : colors::kOutline);
            } else {
                ui.button(r, it.label, focused, pressed, it.kind == Item::Browse);
            }
        }
        if (!touch)
            ui.footer(kCanQuit ? "Up/Down: select    Cross/Enter: choose    Triangle/Esc: quit"
                               : "Up/Down: select    Cross/Enter: choose");
    }

    TouchLayout touch_layout() const override { return TouchLayout::None; } // buttons are tapped directly
    bool adaptive_ui() const override { return true; }
    bool update_prompts() const override { return true; }

private:
    UiRect badge_; // the update badge (last frame's)
    bool badge_pressed_ = false;
    struct Item {
        enum Kind { Recent, Browse, Settings, Quit } kind;
        std::string label, path;
    };

    int item_at(float x, float y) const {
        for (size_t i = 0; i < rects_.size() && i < items_.size(); ++i)
            if (rects_[i].w > 0 && rects_[i].contains(x, y)) return int(i);
        return -1;
    }

    void rebuild(App& app) {
        items_.clear();
        // Recent discs that still exist (the failed one is shown above).
        for (const std::string& p : app.recent_discs())
            if (int(items_.size()) < 4 && disc_available(p) && !platform::same_disc_path(p, app.failed_disc_path()))
                items_.push_back({Item::Recent, "", p});
        recent_count_ = int(items_.size());
        items_.push_back({Item::Browse, "Open disc image", ""});
        items_.push_back({Item::Settings, "Settings", ""});
        if (kCanQuit) items_.push_back({Item::Quit, "Quit", ""});
    }

    void activate(App& app) {
        if (sel_ < 0 || sel_ >= int(items_.size())) return;
        const Item& it = items_[sel_];
        switch (it.kind) {
        case Item::Recent: app.request_disc(it.path); break;
        case Item::Browse: app.show_open_dialog(); break;
        case Item::Settings: app.push(make_settings_screen()); break;
        case Item::Quit: app.quit(); break;
        }
    }

    std::vector<Item> items_;
    std::vector<UiRect> rects_; // last frame's, for taps
    int recent_count_ = 0;
    int sel_ = 0;
    int pressed_ = -1;
    Gesture gesture_;
};

// Home (developer_ false): the player screens, change disc, Developer
// options and Quit; the title bar's update badge (Up from the top row, or a
// tap) opens the update dialog. Developer options: the tools, and Back.
class HubScreen : public Screen {
public:
    explicit HubScreen(bool developer) : developer_(developer) {
        std::vector<const ScreenEntry*> entries;
        for (const ScreenEntry& e : screen_registry())
            if (e.player != developer) entries.push_back(&e);
        std::stable_sort(entries.begin(), entries.end(), [](auto* a, auto* b) { return a->order < b->order; });
        for (const ScreenEntry* e : entries) items_.push_back({e->title, e->help, Item::Screen, e});
        if (developer) {
            items_.push_back({"Back", "Return to the main menu.", Item::Back});
        } else {
            items_.push_back({"Change disc image...", "Open a different disc image (recent discs, or browse).", Item::Disc});
            items_.push_back({"Developer options", "Viewers, test screens and other tools for working on GT3 Revisited.",
                              Item::Developer});
            if (kCanQuit) items_.push_back({"Quit", "Leave GT3 Revisited.", Item::Quit});
        }
    }

    void on_event(App&, const SDL_Event& e) override {
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            scroll_ -= e.wheel.y * (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f) * cell_h_;
            follow_sel_ = false;
            return;
        }
        switch (gesture_.feed(e)) {
        case Gesture::Result::Down:
            pressed_ = item_at(gesture_.x(), gesture_.y());
            badge_pressed_ = badge_.w > 0 && badge_.contains(gesture_.x(), gesture_.y());
            press_scroll_ = scroll_;
            break;
        case Gesture::Result::DragStart:
        case Gesture::Result::Drag:
            if (list_.contains(gesture_.start_x(), gesture_.start_y())) {
                scroll_ = press_scroll_ - (gesture_.y() - gesture_.start_y());
                follow_sel_ = false;
            }
            pressed_ = -1;
            badge_pressed_ = false;
            break;
        case Gesture::Result::Tap:
            if (pressed_ >= 0 && pressed_ == item_at(gesture_.x(), gesture_.y())) {
                sel_ = pressed_;
                badge_focus_ = false;
                tapped_ = true;
            }
            if (badge_pressed_ && badge_.contains(gesture_.x(), gesture_.y())) updates().open_dialog();
            pressed_ = -1;
            badge_pressed_ = false;
            break;
        case Gesture::Result::Up:
            pressed_ = -1;
            badge_pressed_ = false;
            break;
        default: break;
        }
    }

    void update(App& app, double) override {
        const MenuInput& m = app.menu();
        const int n = count();
        const bool badge = !developer_ && !updates().badge_text().empty();
        if (!badge) badge_focus_ = false;
        if (badge_focus_) { // the title bar's update badge
            if (m.vertical() > 0) badge_focus_ = false;
            else if (m.pressed(PadButton::Cross)) updates().open_dialog();
            if (m.pressed(PadButton::Triangle) && kCanQuit) app.quit();
            return;
        }
        if (int v = m.vertical(); v < 0 && badge && sel_ < cols_) { // up from the top row
            badge_focus_ = true;
            return;
        }
        if (int v = m.vertical()) {
            int to = sel_ + v * cols_;
            sel_ = to < 0 ? (sel_ % cols_ + ((n - 1 - sel_ % cols_) / cols_) * cols_) : to >= n ? sel_ % cols_ : to;
            follow_sel_ = true;
        }
        if (int h = m.horizontal()) {
            sel_ = (sel_ + h + n) % n;
            follow_sel_ = true;
        }
        if (m.pressed(PadButton::Triangle)) {
            if (developer_) app.pop();
            else if (kCanQuit) app.quit();
            return;
        }
        if (!m.pressed(PadButton::Cross) && !tapped_) return;
        tapped_ = false;
        const Item& it = items_[size_t(sel_)];
        switch (it.kind) {
        case Item::Screen: app.push(it.entry->factory()); break;
        case Item::Disc: app.push(make_settings_screen(kSettingsTabDisc)); break;
        case Item::Developer: app.push(make_developer_screen()); break;
        case Item::Back: app.pop(); break;
        case Item::Quit: app.quit(); break;
        }
    }

    void render(App& app, Ui& ui) override {
        const UiRect& S = ui.safe();
        const bool touch = ui.touch();
        float y = title_bar(ui, developer_ ? "Developer options" : "", developer_ ? nullptr : &badge_, badge_focus_,
                            badge_pressed_ && gesture_.active());

        // Disc line.
        const DiscInfo& d = app.disc()->info();
        const float line_h = touch ? 22.0f : 18.0f;
        ui.fill(0, y, ui.width(), line_h, Color{20, 22, 29});
        const float ty = std::floor(y + (line_h - Ui::kChar) / 2);
        ui.text(S.x + 12, ty, "Disc", colors::kDim);
        std::string extra = d.serial + "  v" + d.version + "  " + d.video;
        const float ex = S.right() - 12 - Ui::text_width(extra);
        ui.text(S.x + 12 + 48, ty, Ui::elide(d.problem.empty() ? d.description : d.problem, ex - S.x - 72),
                !d.problem.empty() ? colors::kBad : d.verified || d.reported ? colors::kGood : colors::kAccent);
        ui.text(ex, ty, extra, colors::kDim);
        y += line_h + 6;

        // Grid of tools.
        const float bottom_h = (touch ? 30.0f : 26.0f) + (touch ? 0.0f : 16.0f);
        list_ = {S.x + 8, y, S.w - 16, ui.height() - bottom_h - y - (touch ? ui.height() - S.bottom() : 0) - 4};
        cols_ = std::max(1, int(list_.w / 196));
        cell_h_ = touch ? 34.0f : 20.0f;
        const float cw = std::floor((list_.w - 6) / float(cols_));
        const int n = count(), rows = (n + cols_ - 1) / cols_;
        const float content = float(rows) * cell_h_;
        if (follow_sel_) {
            float top = float(sel_ / cols_) * cell_h_;
            if (top < scroll_) scroll_ = top;
            if (top + cell_h_ > scroll_ + list_.h) scroll_ = top + cell_h_ - list_.h;
        }
        scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, content - list_.h));
        cells_.assign(size_t(n), UiRect{});
        SDL_Rect clip{int(list_.x), int(list_.y), int(std::ceil(list_.w)), int(list_.h)};
        SDL_SetRenderClipRect(ui.renderer(), &clip);
        for (int i = 0; i < n; ++i) {
            UiRect r{list_.x + float(i % cols_) * cw, list_.y + float(i / cols_) * cell_h_ - scroll_, cw - 6, cell_h_ - 4};
            cells_[size_t(i)] = r;
            if (r.bottom() < list_.y || r.y > list_.bottom()) continue;
            const bool s = i == sel_ && !badge_focus_, pressed = i == pressed_ && gesture_.active();
            const Item& it = items_[size_t(i)];
            const bool primary = it.entry && std::string_view(it.entry->id) == "boot";
            const char* title = it.title;
            ui.fill_round(r.x, r.y, r.w, r.h, touch ? 5.0f : 3.0f,
                          pressed ? Color{62, 70, 100} : s ? colors::kHighlight : Color{24, 26, 34});
            if (s) ui.fill_round(r.x, r.y + 3, 3, r.h - 6, 1.5f, colors::kAccent);
            Color c = s ? colors::kAccent : primary ? colors::kGood : it.kind != Item::Screen ? colors::kDim : colors::kText;
            ui.text(r.x + 12, std::floor(r.y + (r.h - Ui::kChar) / 2), Ui::clip(title, r.w - 18), c); // keep the start
        }
        SDL_SetRenderClipRect(ui.renderer(), nullptr);
        ui.scrollbar({list_.x, list_.y, list_.w + 6, list_.h}, content, scroll_);

        // Help for the highlighted entry, and engine status.
        const float by = list_.bottom() + 4;
        ui.fill(0, by, ui.width(), ui.height() - by, Color{22, 24, 31});
        ui.fill(0, by, ui.width(), 1, colors::kOutline);
        const float tw = S.w - 24;
        ui.text(S.x + 12, by + 5,
                Ui::clip(badge_focus_ ? std::string("Opens the update dialog: download the new version, or later.")
                                      : std::string(items_[size_t(sel_)].help),
                         tw),
                colors::kText);
        std::string status = "Mods loaded: " + std::to_string(app.vfs()->mods().size());
        if (developer_)
            if (std::string more = status + "    Tools for developers; not part of the game"; Ui::text_width(more) <= tw)
                status = more;
        ui.text(S.x + 12, by + 16, status, colors::kDim);
        if (!touch)
            ui.footer(developer_ ? "Arrows: select    Cross/Enter: open    Triangle/Esc: back"
                                 : "Arrows: select    Cross/Enter: open    Triangle/Esc: quit");
    }

    TouchLayout touch_layout() const override { return TouchLayout::None; }
    bool adaptive_ui() const override { return true; }
    bool update_prompts() const override { return !developer_; }

private:
    UiRect badge_; // the update badge (last frame's)
    bool badge_pressed_ = false, badge_focus_ = false;
    struct Item {
        const char* title;
        const char* help;
        enum Kind { Screen, Disc, Developer, Back, Quit } kind;
        const ScreenEntry* entry = nullptr;
    };

    int count() const { return int(items_.size()); }
    int item_at(float x, float y) const {
        if (!list_.contains(x, y)) return -1;
        for (size_t i = 0; i < cells_.size(); ++i)
            if (cells_[i].contains(x, y)) return int(i);
        return -1;
    }

    bool developer_;
    std::vector<Item> items_;
    std::vector<UiRect> cells_; // last frame's, for taps
    UiRect list_;
    int cols_ = 2;
    float cell_h_ = 20, scroll_ = 0, press_scroll_ = 0;
    bool follow_sel_ = true;
    int sel_ = 0;
    int pressed_ = -1;
    bool tapped_ = false;
    Gesture gesture_;
};

} // namespace

std::unique_ptr<Screen> make_setup_screen() { return std::make_unique<SetupScreen>(); }
std::unique_ptr<Screen> make_hub_screen() { return std::make_unique<HubScreen>(false); }
std::unique_ptr<Screen> make_developer_screen() { return std::make_unique<HubScreen>(true); }

std::vector<ScreenEntry>& screen_registry() {
    static std::vector<ScreenEntry> r;
    return r;
}

} // namespace gt3r
