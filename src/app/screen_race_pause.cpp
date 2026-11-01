// The pause window: GT3's pause / retry / replay pause (PauseMenu, REPLAY OPTIONS) and
// our longer list behind Select; online the race goes on behind it.
// (Race screen code map: docs/dev/RACE.md.)

#include "app/screen_race_impl.h"

namespace gt3r::race_screen {

std::vector<std::string> RaceScreen::pause_items() const {
    const char* exit = from_menu_ ? "Exit to menu" : "Exit to selection";
    if (replaying_) return {"Resume replay", "Restart replay", "Camera", "Save replay", "Stop replay (drive)", exit};
    // Online the race goes on behind the menu: nothing that restarts, replays or leaves the
    // race's time (retiring returns to the lobby).
    if (online_) return {"Resume", "Camera", "Gearbox", "Retire (back to lobby)"};
    return {"Resume", "Restart", "Watch replay", "Save replay", "Camera", "Gearbox", exit};
}

// GT3's pause window for this run (presentation §E): time trials,
// licence tests and machine tests have RETRY (race pause with retry), races / free runs /
// 2P do not (race pause), replays have REPLAY OPTIONS (replay pause).
PauseMenu::Kind RaceScreen::pause_kind() const {
    if (replaying_ || (theater_ && demo_)) return PauseMenu::Kind::Replay;
    bool machine = from_menu_ && launch_.request.mode == "go_machine_test";
    bool time_trial = !field_ && !two_player_ && !free_run_ && race_laps_ > 0;
    return !license_event_.empty() || machine || time_trial ? PauseMenu::Kind::Retry : PauseMenu::Kind::Race;
}

void RaceScreen::open_pause(App& app) {
    paused_ = true;
    pause_sel_ = 0;
    pause_more_ = false;
    if (!pause_loaded_) pause_loaded_ = true, pause_menu_.load(*app.vfs(), app.renderer(), hud_.fonts(), lang_);
    // A run that set a record keeps RETRY greyed out until the next run (presentation §E3).
    pause_menu_.set_retry_enabled(!retry_locked_);
    pause_menu_.open(pause_kind());
    silence(app);
}

void RaceScreen::update_pause(App& app, double dt) {
    const MenuInput& m = app.menu();
    // Ours: L1 / R1 skip the race song shown at the bottom (draw_song); the paused music stays
    // silent until the race goes on.
    if (!replay_opts_open_ && app.show_song()) {
        if (m.pressed(PadButton::R1)) skip_song(app, 1);
        else if (m.pressed(PadButton::L1)) skip_song(app, -1);
    }
    if (replay_opts_open_) {
        // REPLAY OPTIONS: 60 Hz ticks; each applied change takes effect (and is stored) at once.
        replay_opts_acc_ += dt;
        while (replay_opts_open_ && replay_opts_acc_ >= 1.0 / 60.0) {
            replay_opts_acc_ -= 1.0 / 60.0;
            bool open = replay_opts_.tick(menu_keys(app.input().pad(0)));
            for (const std::string& se : replay_opts_.take_sounds()) quick_sounds_.play(app.audio().voices(), se);
            if (replay_opts_.changed()) apply_replay_settings(app, replay_opts_.settings());
            if (!open) {
                replay_opts_open_ = false;
                pause_menu_.set_cursor(2); // back on REPLAY OPTIONS
            }
        }
        return;
    }
    if (!pause_more_) {
        // Select: our longer list (restart, replays, camera, gearbox, save); not in the
        // theater (its replays only play).
        if (m.pressed(PadButton::Select) && !theater_) {
            pause_more_ = true, pause_sel_ = 0;
            return;
        }
        if (!quick_sounds_tried_) {
            quick_sounds_tried_ = true;
            quick_sounds_.load(*app.vfs());
            quick_sounds_.set_enabled(menu::flow::option_value(&app.config(), "menu_se") != "off");
        }
        PauseMenu::Result r = pause_menu_.update(menu_keys(app.input().pad(0)), dt);
        for (const std::string& se : pause_menu_.take_sounds()) quick_sounds_.play(app.audio().voices(), se);
        switch (r) {
        case PauseMenu::Result::None: break;
        case PauseMenu::Result::Continue:
            if (!(replaying_ && player_.done())) paused_ = false;
            break;
        case PauseMenu::Result::Retry: // the run again from its start (presentation §E retry)
            paused_ = false;
            // GT3 tears the race down and sets it up again: the paused music stops (music §2.4) and
            // the new run starts its own cues (resumed, a fail jingle played over the whole retry).
            if (online_) toast("Online races cannot be restarted");
            else music_.stop(app.audio()), restart();
            break;
        case PauseMenu::Result::ReplayOptions: // GT3's REPLAY OPTIONS panel (replay_panel1/2.pmb)
            if (!replay_opts_loaded_)
                replay_opts_loaded_ = true, replay_opts_.load(*app.vfs(), app.renderer(), hud_.fonts(), lang_);
            replay_opts_.open(replay_settings_, two_player_);
            replay_opts_open_ = true;
            replay_opts_acc_ = 0;
            break;
        case PauseMenu::Result::Exit:
            paused_ = false;
            if (theater_) leave(app);
            else if (replaying_) end_replay(app); // at once: the transition, then the menu
            else if (attract_ || online_) leave(app);
            else pause_exit(); // the run ends (GT3's loader transition), or it is its finish
            break;
        }
        return;
    }
    std::vector<std::string> items = pause_items();
    int k = int(items.size());
    pause_sel_ = (pause_sel_ + m.vertical() + k) % k;
    if (m.pressed(PadButton::Circle) || (!replaying_ && m.pressed(PadButton::Triangle))) {
        if (!(replaying_ && player_.done())) paused_ = false;
        return;
    }
    if (!m.pressed(PadButton::Cross)) return;
    const std::string& it = items[size_t(pause_sel_)];
    // A run started over from here ends the paused run's music, as RETRY does (above).
    if (it == "Restart" || it == "Restart replay" || it == "Stop replay (drive)") music_.stop(app.audio());
    if (it == "Resume" || it == "Resume replay") {
        if (!(replaying_ && player_.done())) paused_ = false;
    } else if (it == "Restart") {
        restart();
    } else if (it == "Restart replay") {
        start_playback(Replay(replay_));
    } else if (it == "Watch replay") {
        watch_replay();
    } else if (it == "Save replay") {
        save_current_replay();
    } else if (it == "Camera") {
        next_view(camera_, 0);
    } else if (it == "Gearbox") {
        controls_.config().automatic = !controls_.config().automatic;
    } else if (it == "Stop replay (drive)") {
        stop_playback();
    } else if (it == "Exit to selection" || it == "Exit to menu" || it == "Retire (back to lobby)") {
        leave(app);
    }
}

// Online the pause window does not pause: say so above it.
void RaceScreen::online_pause_note(Ui& ui) {
    if (!online_) return;
    const char* note = "Online: the race goes on, your car has no input";
    float w = ui.text_width(note);
    ui.text(ui.width() / 2 - w / 2, 40, note, colors::kAccent);
}

void RaceScreen::render_pause(Ui& ui) {
    const float W = ui.width(), H = ui.height();
    const UiRect pic = ui.picture43();
    if (replay_opts_open_) {
        SDL_FRect dst = original_res_ ? frame_rect_ : SDL_FRect{pic.x, pic.y, pic.w, pic.h};
        Color dim{0, 0, 0, 126}; // panel_bg1 reaches past the 4:3 frame (860 wide)
        if (dst.x > 0) ui.fill(0, 0, dst.x, H, dim);
        if (dst.x + dst.w < W) ui.fill(dst.x + dst.w, 0, W - dst.x - dst.w, H, dim);
        replay_opts_.draw(dst, !app_ || !split_horizontal(*app_));
        return;
    }
    if (!pause_more_) {
        if (pause_menu_.loaded(pause_menu_.kind())) {
            SDL_FRect dst = original_res_ ? frame_rect_ : SDL_FRect{pic.x, pic.y, pic.w, pic.h};
            // The window's full-screen dim (pmb "bg1") also covers the picture outside the
            // 4:3 frame on wide screens.
            Color dim{0, 0, 0, 126};
            if (dst.x > 0) ui.fill(0, 0, dst.x, H, dim);
            if (dst.x + dst.w < W) ui.fill(dst.x + dst.w, 0, W - dst.x - dst.w, H, dim);
            pause_menu_.draw(ui.renderer(), dst);
            online_pause_note(ui);
        } else if (replaying_) {
            hud_.draw_replay_pause(ui.renderer(), pause_menu_.cursor(), original_res_ ? &frame_rect_ : nullptr, original_res_);
        } else {
            hud_.draw_pause(ui.renderer(), pause_menu_.cursor(), original_res_ ? &frame_rect_ : nullptr, original_res_);
            online_pause_note(ui);
        }
        draw_song(ui, true);
        return;
    }
    ui.fill(0, 0, W, H, Color{0, 0, 0, 120});
    std::vector<std::string> items = pause_items();
    int n = int(items.size());
    float x = W / 2 - 100, y = 110;
    ui.fill(x - 10, y - 24, 220, 24 + float(n) * 16 + 10, Color{20, 22, 28, 230});
    ui.text(x, y - 16,
            replaying_ ? (player_.done() ? "END OF REPLAY" : "REPLAY PAUSED") : online_ ? "MENU (RACE GOES ON)" : "PAUSED",
            colors::kAccent);
    for (int i = 0; i < n; ++i) {
        if (i == pause_sel_) ui.fill(x - 4, y + float(i) * 16 - 3, 208, 14, colors::kHighlight);
        std::string s = items[size_t(i)];
        if (s == "Camera") s += std::string(": ") + camera_mode_name(camera_.mode());
        if (s == "Gearbox") s += controls_.config().automatic ? ": AT" : ": MT";
        ui.text(x, y + float(i) * 16, s, colors::kText);
    }
    draw_song(ui, true);
}

namespace {

// What the player presses for menu button `b` (the pause screen's hints): the touch control's
// label while the touch controls show, else the first input their menu buttons bind to it, the
// keyboard's when no controller is in use.
std::string button_hint(App& app, PadButton b) {
    if (app.touch().controls().visible()) return std::string(pad_button_name(b));
    InputManager& in = app.input();
    for (BindingSet set : {BindingSet::Controller, BindingSet::Keyboard}) {
        if (set == BindingSet::Controller && in.kind(0) == DeviceKind::Keyboard) continue;
        for (const Binding& bd : in.bindings(0, false, set).bindings)
            if (bd.target.kind == PadTarget::Kind::Button && bd.target.button == b) {
                std::string name = in.describe(0, bd.source);
                if (name.rfind("Key ", 0) == 0) name.erase(0, 4); // "Q", not "Key Q"
                return name;
            }
    }
    return std::string(pad_button_name(b));
}

// |<< or >>| (Previous / Next), `h` units high, centred on (cx, cy).
void skip_icon(Ui& ui, float cx, float cy, float h, bool next, Color c) {
    const float tw = h * 0.55f, bw = std::max(1.0f, h * 0.16f), left = cx - (2 * tw + bw) / 2;
    auto x = [&](float v) { return next ? v : 2 * cx - v; };
    const SDL_FColor fc{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
    for (int i = 0; i < 2; ++i) {
        const float back = left + float(i) * tw;
        const SDL_Vertex v[3] = {{{x(back + tw), cy}, fc, {0, 0}}, {{x(back), cy - h / 2}, fc, {0, 0}}, {{x(back), cy + h / 2}, fc, {0, 0}}};
        SDL_RenderGeometry(ui.renderer(), nullptr, v, 3, nullptr, 0);
    }
    const float bar = x(left + 2 * tw);
    ui.fill(next ? bar : bar - bw, cy - h / 2, bw, h, c);
}

} // namespace

void RaceScreen::draw_song(Ui& ui, bool controls) {
    if (!app_ || !app_->show_song()) return;
    const std::optional<RaceMusic::SongInfo> song = music_.now_playing(app_->audio());
    if (!song) return;
    const float W = ui.width(), H = ui.height();
    const std::string prev = controls ? button_hint(*app_, PadButton::L1) : "", next = controls ? button_hint(*app_, PadButton::R1) : "";
    // The hints' width beside the song: "<button>  |<<  Previous" each side.
    const float side = controls ? std::ceil(std::max(Ui::text_width(prev + "  Previous"), Ui::text_width("Next  " + next))) + 22 : 0;
    const float text_w = std::max(Ui::text_width(song->title), Ui::text_width(song->artist));
    const float pw = std::min(W - 24, std::max(controls ? 300.0f : 160.0f, std::ceil(text_w) + 2 * side + 32));
    // Under the pause window (GT3's sit around the middle), clear of the HUD's gauges; after a
    // skip while driving, at the top, under the rear-view mirror's place.
    const float ph = song->artist.empty() ? 22.0f : 32.0f, px = std::floor((W - pw) / 2);
    const float py = std::floor(controls ? H * 0.58f : H * 0.2f);
    ui.fill_round(px, py, pw, ph, 6, Color{12, 14, 20, 215});
    ui.frame_round(px, py, pw, ph, 6, 1, Color{255, 255, 255, 40});
    const float mid = py + ph / 2, ty = std::floor(mid - Ui::kChar / 2.0f);
    if (controls) {
        const float lx = px + 10;
        ui.text(lx, ty, prev, colors::kAccent);
        const float ix = lx + Ui::text_width(prev) + 12;
        skip_icon(ui, ix, mid, 8, false, colors::kText);
        ui.text(ix + 10, ty, "Previous", colors::kDim);
        const float rx = px + pw - 10;
        ui.text_right(rx, ty, next, colors::kAccent);
        const float nx = rx - Ui::text_width(next) - 12;
        skip_icon(ui, nx, mid, 8, true, colors::kText);
        ui.text_right(nx - 10, ty, "Next", colors::kDim);
    }
    const float cw = pw - 2 * side - 16, cx = px + side + 8;
    if (song->artist.empty()) {
        ui.text_center_in(cx, cw, ty, Ui::elide(song->title, cw), colors::kText);
    } else {
        ui.text_center_in(cx, cw, py + 5, Ui::elide(song->title, cw), colors::kText);
        ui.text_center_in(cx, cw, py + 18, Ui::elide(song->artist, cw), colors::kDim);
    }
}

} // namespace gt3r::race_screen
