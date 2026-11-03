#include "app/updates.h"

#include "app/app.h"
#include "app/screen_online.h"
#include "base/log.h"
#include "net/session.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace gt3r {

namespace {

std::string utc_now() {
    char t[32];
    std::time_t now = std::time(nullptr);
    std::strftime(t, sizeof t, "%Y-%m-%d %H:%M UTC", std::gmtime(&now));
    return t;
}

net::GameVersion read_version(const IniDocument& cfg, const char* key) {
    std::optional<net::GameVersion> v = net::parse_game_version(cfg.get("online", key).value_or(""));
    if (!v) return {};
    v->suffix.clear(); // releases only
    return *v;
}

bool web_url(const std::string& u) { return u.rfind("https://", 0) == 0 || u.rfind("http://", 0) == 0; }

} // namespace

Updates& updates() {
    static Updates u;
    return u;
}

bool Updates::enabled(App& app) const {
    return to_lower(app.config().get("online", "update_check").value_or("on")) != "off";
}

void Updates::set_enabled(App& app, bool on) {
    app.config().set("online", "update_check", on ? "on" : "off");
    app.save_config();
}

void Updates::startup(App& app, bool network) {
    const IniDocument& cfg = app.config();
    latest_ = read_version(cfg, "update_latest");
    min_online_ = read_version(cfg, "update_min_online");
    dismissed_ = read_version(cfg, "update_dismissed");
    url_ = cfg.get("online", "update_url").value_or("");
    if (!web_url(url_)) url_.clear();
    checked_ = cfg.get("online", "update_checked").value_or("");
    update_due(); // the cached answer may already call for the prompt
    if (network && enabled(app)) start(app);
}

void Updates::check_now(App& app) { start(app); }

void Updates::start(App& app) {
    std::string server = app.config().get("online", "server").value_or(kDefaultServer);
    if (server.empty()) server = kDefaultServer;
    state_ = State::Checking;
    error_.clear();
    check_.start(server, net::this_game_version(), net::this_platform());
}

void Updates::poll(App& app) {
    std::optional<net::VersionCheck::Result> r = check_.take();
    if (!r) return;
    if (!r->answered) {
        state_ = State::Failed;
        error_ = r->error;
        LOG_INFO("Update check: %s", error_.c_str());
        return;
    }
    state_ = State::Done;
    latest_ = r->latest;
    min_online_ = r->min_online;
    url_ = r->url;
    checked_ = utc_now();
    store(app);
    update_due();
    const net::GameVersion& ours = net::this_game_version();
    LOG_INFO("Update check: this is %s; latest %s, online play needs %s%s", ours.to_string().c_str(),
             latest_.known() ? latest_.number().c_str() : "(none)", min_online_.known() ? min_online_.number().c_str() : "any",
             required() ? " (update required)" : available() ? " (update available)" : "");
}

// The startup prompt: once per release (not after Later for it, not twice in one run).
void Updates::update_due() {
    const net::GameVersion& ours = net::this_game_version();
    net::GameVersion v = net::prompt_version(latest_, min_online_);
    startup_due_ = net::show_startup_prompt(ours, latest_, min_online_, dismissed_) &&
                   (!shown_.known() || net::compare_core(v, shown_) > 0);
}

void Updates::store(App& app) {
    IniDocument& cfg = app.config();
    cfg.set("online", "update_latest", latest_.known() ? latest_.number() : "");
    cfg.set("online", "update_min_online", min_online_.known() ? min_online_.number() : "");
    cfg.set("online", "update_url", url_);
    cfg.set("online", "update_checked", checked_);
    app.save_config();
}

bool Updates::available() const { return net::update_available(net::this_game_version(), latest_); }

bool Updates::required() const { return !net::meets_minimum(net::this_game_version(), min_online_); }

net::UpdatePrompt Updates::prompt() const { return net::update_prompt(net::this_game_version(), latest_, min_online_); }

net::UpdateLinks Updates::links() const { return net::update_links(url_, net::this_release_asset()); }

std::string Updates::status_text() const {
    switch (state_) {
    case State::Checking: return "Checking...";
    case State::Failed:
        return "Could not check (" + error_ + ")" + (known() ? "; last known: " + latest_.number() : "");
    default: break;
    }
    if (!known()) return checked_.empty() ? "Not checked yet" : "The server announces no release";
    if (required()) return "Update required to play online: version " + latest_.number();
    if (available()) return "Version " + latest_.number() + " is available";
    return "Up to date";
}

static bool open_url(const std::string& url) {
    if (url.empty()) return false;
    if (!SDL_OpenURL(url.c_str())) {
        LOG_WARN("Could not open %s: %s", url.c_str(), SDL_GetError());
        return false;
    }
    LOG_INFO("Opened %s", url.c_str());
    return true;
}

bool Updates::open_download() const { return open_url(links().download); }

bool Updates::open_notes() const { return open_url(links().notes); }

void Updates::note_required(App& app, const net::UpdateRequired& u) {
    // The relay's own word: our version is below its min_online (it does not say which).
    const net::GameVersion& ours = net::this_game_version();
    if (u.latest.known()) latest_ = u.latest;
    if (web_url(u.url)) url_ = u.url;
    if (net::meets_minimum(ours, min_online_)) { // make required() true: at least the next patch
        net::GameVersion next = ours;
        next.suffix.clear();
        ++next.patch;
        min_online_ = u.latest.known() && net::compare_core(u.latest, next) > 0 ? u.latest : next;
    }
    checked_ = utc_now();
    store(app);
}

// --- the dialog ---

void Updates::open_dialog() { open_dialog(prompt()); }

void Updates::open_dialog(net::UpdatePrompt kind) {
    if (kind == net::UpdatePrompt::None) return;
    if (!dialog_.open) dialog_.focus = 0; // Download update
    dialog_.open = true;
    dialog_.kind = kind;
    dialog_.pressed = -1;
    dialog_.gesture.cancel();
    shown_ = net::prompt_version(latest_, min_online_);
    startup_due_ = false;
}

void Updates::frame(App& app, bool screen_allows) {
    if (startup_due_ && screen_allows && !modal()) {
        LOG_INFO("Update dialog: %s", prompt() == net::UpdatePrompt::Required ? "update required" : "update available");
        open_dialog();
    }
    (void)app;
}

void Updates::close_dialog(App& app, bool later) {
    if (later) { // not again at startup until a newer release
        dismissed_ = net::prompt_version(latest_, min_online_);
        app.config().set("online", "update_dismissed", dismissed_.known() ? dismissed_.number() : "");
        app.save_config();
    }
    dialog_.open = false;
    dialog_.wait_release = true;
    dialog_.pressed = -1;
    app.menu().suppress();
}

void Updates::activate(App& app, int item) {
    switch (item) {
    case 0: // Download update
        open_download();
        close_dialog(app, false);
        break;
    case 1: close_dialog(app, true); break; // Later
    case 2: open_notes(); break;            // Release notes (the dialog stays)
    default: break;
    }
}

void Updates::dialog_update(App& app) {
    const MenuInput& m = app.menu();
    if (!dialog_.open) { // closed: wait until its button is let go
        bool held = false;
        for (PadButton b : {PadButton::Cross, PadButton::Circle, PadButton::Triangle, PadButton::Square, PadButton::Start})
            held = held || m.held(b);
        if (!held) dialog_.wait_release = false;
        return;
    }
    const bool notes = !links().notes.empty() && links().notes != links().download;
    if (int h = m.horizontal(); h && dialog_.focus < 2) dialog_.focus = h > 0 ? 1 : 0;
    if (int v = m.vertical()) dialog_.focus = v > 0 && notes ? 2 : v < 0 && dialog_.focus == 2 ? 0 : dialog_.focus;
    if (m.pressed(PadButton::Cross) || m.pressed(PadButton::Start)) activate(app, dialog_.focus);
    else if (m.pressed(PadButton::Circle) || m.pressed(PadButton::Triangle)) activate(app, 1);
}

bool Updates::dialog_clickable(float x, float y) const {
    for (const UiRect& r : dialog_.items)
        if (r.w > 0 && r.contains(x, y)) return true;
    return false;
}

void Updates::dialog_event(App& app, const SDL_Event& e) {
    if (!dialog_.open) return;
    auto hit = [&](float x, float y) {
        for (int i = 0; i < 3; ++i)
            if (dialog_.items[i].w > 0 && dialog_.items[i].contains(x, y)) return i;
        return -1;
    };
    switch (dialog_.gesture.feed(e)) {
    case Gesture::Result::Down: dialog_.pressed = hit(dialog_.gesture.x(), dialog_.gesture.y()); break;
    case Gesture::Result::Tap: {
        int i = dialog_.pressed;
        dialog_.pressed = -1;
        if (i >= 0 && i == hit(dialog_.gesture.x(), dialog_.gesture.y())) {
            dialog_.focus = i;
            activate(app, i);
        }
        break;
    }
    case Gesture::Result::DragStart:
    case Gesture::Result::Up: dialog_.pressed = -1; break;
    default: break;
    }
    // Hover moves the focus (mouse), like the frontend's buttons feel.
    if (e.type == SDL_EVENT_MOUSE_MOTION)
        if (int i = hit(e.motion.x, e.motion.y); i >= 0) dialog_.focus = i;
}

void Updates::dialog_render(Ui& ui) {
    for (UiRect& r : dialog_.items) r = {};
    if (!dialog_.open) return;
    const bool req = dialog_.kind == net::UpdatePrompt::Required;
    const bool touch = ui.touch();
    const std::string ours = net::this_game_version().to_string();
    std::string title = req ? "Update required to play online" : "Update available";
    net::GameVersion need = min_online_.known() ? min_online_ : latest_;
    std::string body = req ? "Online play needs GT3 Revisited " + need.number() + " or newer. You have " + ours +
                                 ". Offline play is not affected."
                           : "GT3 Revisited " + latest_.number() + " is available. You have " + ours + ".";
    net::UpdateLinks l = links();
    const bool notes = !l.notes.empty() && l.notes != l.download;

    ui.fill(0, 0, ui.width(), ui.height(), Color{0, 0, 0, 160}); // modal: the screen behind dims
    const float w = std::floor(std::min(ui.width() - 32, touch ? 440.0f : 340.0f));
    const float pad = touch ? 18.0f : 14.0f, bh = touch ? 38.0f : 24.0f, lh = touch ? 28.0f : 16.0f;
    std::vector<std::string> lines = Ui::wrap(body, w - 2 * pad);
    const float h = pad + 2 * Ui::kTitleCap + 12 + 11 * float(lines.size()) + 12 + bh + (notes ? 8 + lh : 0) + pad;
    const float x = std::floor((ui.width() - w) / 2), y = std::floor((ui.height() - h) / 2);
    ui.fill_round(x, y, w, h, touch ? 8.0f : 6.0f, colors::kPanel);
    ui.frame_round(x, y, w, h, touch ? 8.0f : 6.0f, 1, req ? colors::kBad : colors::kOutline);
    float ty = y + pad;
    ui.text_big(x + pad, ty, title, 2, req ? colors::kBad : colors::kAccent);
    ty += 2 * Ui::kTitleCap + 12;
    for (const std::string& line : lines) {
        ui.text(x + pad, ty, line, colors::kText);
        ty += 11;
    }
    ty += 12 - 11 + Ui::kChar;
    const float bw = std::floor((w - 2 * pad - 8) / 2);
    dialog_.items[0] = {x + pad, ty, bw, bh};
    dialog_.items[1] = {x + pad + bw + 8, ty, bw, bh};
    const bool down = dialog_.gesture.active();
    ui.button(dialog_.items[0], "Download update", dialog_.focus == 0, down && dialog_.pressed == 0, true);
    ui.button(dialog_.items[1], "Later", dialog_.focus == 1, down && dialog_.pressed == 1);
    if (notes) {
        const char* label = "Release notes";
        const float tw = Ui::text_width(label) + 16;
        dialog_.items[2] = {std::floor(x + (w - tw) / 2), ty + bh + 8, tw, lh};
        const UiRect& r = dialog_.items[2];
        const bool f = dialog_.focus == 2;
        if (f || (down && dialog_.pressed == 2)) ui.fill_round(r.x, r.y, r.w, r.h, 4, colors::kHighlight);
        const float cy = std::floor(r.y + (r.h - Ui::kChar) / 2);
        ui.text_center_in(r.x, r.w, cy, label, f ? colors::kAccent : colors::kDim);
        ui.fill(r.x + 8, cy + Ui::kChar + 2, r.w - 16, 1, f ? colors::kAccent : colors::kDim); // a link
    }
}

std::string Updates::badge_text() const {
    switch (prompt()) {
    case net::UpdatePrompt::Required: return "Update required for online";
    case net::UpdatePrompt::Available: return "Update available";
    default: return "";
    }
}

} // namespace gt3r
