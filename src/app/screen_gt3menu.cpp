// Interactive GT3 menus (gt3r --screen gt3menu): the title menu, arcade mode,
// GT mode and options driven by the original .mbl flow and .pmb layouts
// through src/menu/flow (page manager, button lists, focus/feedback
// animations, menu sounds, loading transitions).
//
// Input (virtual pad, as in the US release): D-pad / left stick move,
// Circle or Cross confirm, Triangle or Square go back. Cancel on the title
// menu leaves the screen, as does the "GT3R menu" button drawn beside the
// title menu (Select, or a click / tap): our main menu is underneath. A mouse's
// right click is Cancel. With the debug overlay on, Select cycles the menu
// language (disc folders).
//
// Starting a race raises a menu::flow::GameRequest: a handler registered with
// set_game_request_handler() runs it; otherwise the "race" screen (time trial)
// is opened if it exists.
//
// Scripted screenshots:
//   GT3R_GT3MENU_SCRIPT="down,cross,wait:40,down"  keys run at start-up, one
//       60 Hz frame each (plus a release frame); wait:N idles N frames
//   GT3R_GT3MENU_FREEZE=1   stop the menu clock after the script (so --shot
//       captures exactly the scripted frame, e.g. mid-transition)
//   GT3R_LANG=FR            menu language folder
//   GT3R_GT3MENU_OPEN=gt_mode:cd/cd_usa_ford   open a page directly

#include "game/replay_theater.h"
#include "app/app.h"
#include "app/save_dialogs.h"
#include "base/config.h"
#include "app/screen_movies.h"
#include "app/screen_registry.h"
#include "base/log.h"
#include "menu/flow/game_flow.h"
#include "menu/flow/gt_mode_hooks.h"
#include "menu/flow/menu_sound.h"
#include "menu/menu_renderer.h"
#include "menu/menu_scene.h"
#include "menu/menu_text.h"
#include "text/game_font.h"
#include "video/ipu.h"
#include "video/movie_player.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace gt3r {

namespace {

using namespace gt3r::menu;
using namespace gt3r::menu::flow;

constexpr double kFrame = 1.0 / 60.0;

// GameFlow::measure_text with the game fonts (menu units at the font's own size).
std::function<float(const std::string&, const std::string&)> font_measure(text::GameFonts& fonts) {
    return [&fonts](const std::string& font, const std::string& s) -> float {
        text::TextStyle st;
        text::GameFont* f = fonts.loaded() ? fonts.resolve(font, st) : nullptr;
        if (!f || s.empty()) return 0.0f;
        return f->measure_game(ByteSpan(reinterpret_cast<const u8*>(s.data()), s.size()), st).x;
    };
}

MenuKeys keys_from_pad(const PadState& pad) {
    MenuKeys k = 0;
    auto set = [&](MenuKey key, bool on) {
        if (on) k |= key_bit(key);
    };
    // Left stick doubles the D-pad (threshold +-0x40 around the centre).
    set(MenuKey::Up, pad.down(PadButton::Up) || pad.axis(PadAxis::LY) < 0x40);
    set(MenuKey::Down, pad.down(PadButton::Down) || pad.axis(PadAxis::LY) > 0xC0);
    set(MenuKey::Left, pad.down(PadButton::Left) || pad.axis(PadAxis::LX) < 0x40);
    set(MenuKey::Right, pad.down(PadButton::Right) || pad.axis(PadAxis::LX) > 0xC0);
    set(MenuKey::Confirm, pad.down(PadButton::Circle) || pad.down(PadButton::Cross));
    set(MenuKey::Cancel, pad.down(PadButton::Triangle) || pad.down(PadButton::Square));
    set(MenuKey::Start, pad.down(PadButton::Start));
    set(MenuKey::Select, pad.down(PadButton::Select));
    set(MenuKey::L1, pad.down(PadButton::L1));
    set(MenuKey::R1, pad.down(PadButton::R1));
    return k;
}

MenuKeys key_from_name(const std::string& s) {
    if (s == "up") return key_bit(MenuKey::Up);
    if (s == "down") return key_bit(MenuKey::Down);
    if (s == "left") return key_bit(MenuKey::Left);
    if (s == "right") return key_bit(MenuKey::Right);
    if (s == "cross" || s == "circle" || s == "x" || s == "ok") return key_bit(MenuKey::Confirm);
    if (s == "triangle" || s == "square" || s == "back") return key_bit(MenuKey::Cancel);
    if (s == "start") return key_bit(MenuKey::Start);
    if (s == "select") return key_bit(MenuKey::Select);
    return 0;
}

class Gt3MenuScreen : public Screen {
public:
    // `language_page`: PAL's first start, the language page (menu/language.mbl); the choice
    // goes to the config and the game starts (opening movie, title menu).
    explicit Gt3MenuScreen(bool language_page = false) : language_page_(language_page) {}

    ~Gt3MenuScreen() override {
        movie_.stop();
        renderer_.reset();
        if (frame_) SDL_DestroyTexture(frame_);
        for (auto& [name, t] : stills_)
            if (t) SDL_DestroyTexture(t);
    }

    // Mouse clicks and touch taps (SDL turns taps that miss the on-screen
    // touch controls into left-button events). A press and release close
    // together is a tap; update() hands it to the menu.
    void on_event(App&, const SDL_Event& e) override {
        // A pointer that hovers (a mouse or a VR controller, not a finger: SDL marks the mouse
        // events it makes from touches) focuses what it is over; update() hands it to the menu.
        if (e.type == SDL_EVENT_MOUSE_MOTION && e.motion.which != SDL_TOUCH_MOUSEID && !e.motion.state)
            hover_ = SDL_FPoint{e.motion.x, e.motion.y};
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            press_ = SDL_FPoint{e.button.x, e.button.y};
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT && press_) {
            float dx = e.button.x - press_->x, dy = e.button.y - press_->y;
            if (dx * dx + dy * dy <= 12.0f * 12.0f) taps_.push_back(*press_);
            press_.reset();
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT &&
                   e.button.which != SDL_TOUCH_MOUSEID) {
            back_click_ = true; // a right click goes back, as in our own screens
        }
    }

    bool follows_aspect() const override { return true; }

    // UI point -> menu layout coordinates (640x448, y up), matching the 4:3
    // picture placement in render(). False outside the picture.
    static bool ui_to_menu(const Ui& ui, SDL_FPoint p, float& x, float& y) {
        const UiRect pic = ui.picture43();
        if (p.x < pic.x || p.x >= pic.right() || p.y < 0 || p.y >= pic.h) return false;
        x = (p.x - pic.x) * 640.0f / pic.w;
        y = 448.0f - p.y * 448.0f / pic.h;
        return true;
    }

    void update(App& app, double dt) override {
        if (!flow_ && !init_tried_) init_tried_ = true, init(app); // once: a failure shows its error (no retry every frame)
        if (!flow_) {
            if (app.menu().pressed(PadButton::Triangle)) app.pop();
            return;
        }
        if (language_page_) {
            if (std::optional<std::string> code = flow_->take_language()) {
                app.set_language(*code);
                app.pop();
                app.push_game_start();
                return;
            }
        } else if (app.language() != lang_ && !in_game_) {
            rebuild_flow(app); // chosen in the settings meanwhile
        }
        if (in_game_) {
            // Back from gameplay (the pushed screen was popped).
            in_game_ = false;
            flow_->game_finished();
            if (!script_.empty()) run_script(app);
        }
        if (frozen_) {
            taps_.clear();
            hover_.reset();
            back_click_ = false;
            return;
        }
        if (hover_) {
            if (float mx, my; ui_to_menu(app.ui(), *hover_, mx, my)) flow_->hover(mx, my);
            hover_.reset();
        }
        const bool title = on_title();
        const UiRect gt3r_button = gt3r_menu_button(app);
        for (SDL_FPoint p : taps_) {
            if (title && gt3r_button.contains(p.x, p.y)) {
                taps_.clear();
                app.pop();
                return;
            }
            if (float mx, my; ui_to_menu(app.ui(), p, mx, my)) flow_->tap(mx, my);
        }
        taps_.clear();
        if (std::exchange(back_click_, false)) {
            if (title) { // as Cancel on the title menu: our main menu
                app.pop();
                return;
            }
            flow_->back();
        }
        MenuKeys keys = keys_from_pad(app.input().menu_pad()); // player 1's (or any device's: menu_pad)
        held_over_ &= keys; // keys held since another screen was on top: ignored until released
        keys &= MenuKeys(~held_over_);
        flow_->set_player2_keys(keys_from_pad(app.input().pad(1))); // 2 Player car page
        // Select: next language available on the disc (development aid, only with the debug
        // overlay on: a stray Select / Tab must not switch a player's game to another language).
        const char* dbg_env = std::getenv("GT3R_GT3MENU_DEBUG");
        const bool debug = (dbg_env && *dbg_env && *dbg_env != '0') ||
                           to_lower(app.config().get("gt3menu", "debug").value_or("off")) == "on";
        const bool select = (keys & key_bit(MenuKey::Select)) && !(prev_keys_ & key_bit(MenuKey::Select));
        if (debug && select) cycle_language(app);
        // Cancel or Select (the GT3R menu button) on the title menu leaves the screen for our
        // main menu (the original has no way back there).
        if (title && (((keys & key_bit(MenuKey::Cancel)) && !(prev_keys_ & key_bit(MenuKey::Cancel))) || (select && !debug))) {
            app.pop();
            return;
        }
        prev_keys_ = keys;
        acc_ += dt;
        while (acc_ >= kFrame) {
            acc_ -= kFrame;
            step(app, keys);
        }
    }

    void render(App& app, Ui& ui) override {
        if (!flow_) {
            ui.header("GAME MENUS", "");
            ui.text(8, 40, error_.empty() ? "loading..." : error_, colors::kBad);
            ui.footer("Triangle: back");
            return;
        }
        draw_frame(app);
        // 4:3 picture in the middle of the game area.
        const UiRect pic = ui.picture43();
        const SDL_FRect dst{pic.x, pic.y, pic.w, pic.h};
        if (frame_) SDL_RenderTexture(app.renderer(), frame_, nullptr, &dst);
        // Our main menu (settings, mods, online, ...), in the bar beside the title menu.
        if (on_title()) {
            const UiRect b = gt3r_menu_button(app);
            auto draw = [&](float dx, float dy) {
                const UiRect d{b.x + dx, b.y + dy, b.w, b.h};
                ui.button(d, "GT3R menu", false, press_ && b.contains(press_->x, press_->y));
                if (!ui.touch() && b.bottom() + 12 <= ui.height())
                    ui.text_center_in(d.x, d.w, d.bottom() + 4, "SELECT", colors::kDim);
            };
            // Left of the game area (4:3 on a wider window): drawn across the whole window.
            if (b.x < 0) app.touch().draw_whole_window(ui, draw);
            else draw(0, 0);
        }
        // Developer overlay (language, page name, flow log lines): `[gt3menu] debug = on`
        // (or GT3R_GT3MENU_DEBUG=1).
        const char* dbg_env = std::getenv("GT3R_GT3MENU_DEBUG");
        const bool debug = (dbg_env && *dbg_env && *dbg_env != '0') ||
                           to_lower(app.config().get("gt3menu", "debug").value_or("off")) == "on";
        if (debug) {
            ui.text(4, 4, lang_, colors::kDim);
            if (const MenuController* c = flow_->controller(); c && c->has_page()) {
                std::string pg = c->page_name();
                for (size_t i = 0; i < pg.size(); i += 9) ui.text(4, 18 + float(i / 9) * 10, pg.substr(i, 9), colors::kDim);
            }
        }
        float y = ui.height() - 12;
        if (debug)
            for (auto it = messages_.rbegin(); it != messages_.rend() && y > ui.height() - 60; ++it, y -= 10)
                ui.text(dst.x + 4, y, it->text, colors::kAccent);
    }

    bool update_prompts() const override { return on_title(); } // the update dialog, on the title page only

private:
    // The "GT3R menu" button, in the left bar beside the 4:3 picture (UI units): the game
    // area's, or with a 4:3 game area the window's bar left of it (negative x); over the
    // picture's bottom left corner when the window has no bar. Above the on-screen D-pad while
    // the touch controls are up, so a finger reaches it.
    static UiRect gt3r_menu_button(App& app) {
        const Ui& ui = app.ui();
        const UiRect pic = ui.picture43();
        constexpr float kMinBar = 60;
        float x0 = 0, y0 = 0; // the bar's left edge: the window's when the game area has no bar
        if (pic.x < kMinBar && SDL_RenderCoordinatesFromWindow(app.renderer(), 0, 0, &x0, &y0)) x0 = std::min(x0, 0.0f);
        else x0 = 0;
        const UiRect b = pic.x - x0 >= kMinBar ? UiRect{x0 + 6, ui.height() - 46, std::min(pic.x - x0 - 12, 90.0f), 22}
                                               : UiRect{pic.x + 6, ui.height() - 28, 74, 22};
        const TouchRect r = app.touch().clear_above({b.x, b.y, b.w, b.h});
        return {r.x, r.y, r.w, r.h};
    }

    bool on_title() const {
        return !language_page_ && flow_ && flow_->controller() && !flow_->switching() &&
               flow_->controller()->page_name() == "root" && !flow_->controller()->busy();
    }

    std::string menu_music_; // the menu's current BGM sequence ("" = none)
    struct Message {
        std::string text;
        int frames;
    };

    void init(App& app) {
        lang_ = app.language();
        languages_ = app.languages();
        // The language page shows before a language is chosen: in the region's own (British English).
        if (language_page_) lang_ = text::language_code(app.vfs()->region().default_language());
        renderer_ = std::make_unique<MenuRenderer>(app.renderer());
        // "mpeg" widgets show the background movie.
        // GT mode: 3D car renders on top of the widget's own art.
        renderer_->set_widget_overlay_drawer([this, &app](SDL_Renderer* r, const Element& e, const SDL_FRect& box,
                                                          float alpha) {
            if (flow_ && flow_->gt_active())
                if (const GtModeWidgetDrawer& d = gt_mode_widget_drawer(); d && flow_->controller())
                    d(app, r, e, box, alpha, flow_->controller()->page_name(), element_image(renderer_->drawing_scene(), e));
            // 2 Player car page: each player's car ("ar/select_car_2p:<entry>#paint@age%player");
            // player 2's draw comes one page width to the right.
            if (flow_ && flow_->control() == "arcade" && flow_->two_player(0) && e.name == "car") {
                if (const GtModeWidgetDrawer& d = gt_mode_widget_drawer(); d) {
                    const float page_w = box.w;
                    const int p = box.x >= page_w * 0.5f ? 1 : 0;
                    const GameFlow::TwoPlayer& t = *flow_->two_player(p);
                    if (t.state >= 4 && !t.cars.empty()) {
                        SDL_FRect b = box;
                        if (p == 1) b.x -= page_w;
                        d(app, r, e, b, alpha,
                          "ar/select_car_2p:" + t.cars[size_t(t.car)] + "#" + std::to_string(t.paint) + "@" +
                              std::to_string(t.age) + "%" + std::to_string(p),
                          element_image(renderer_->drawing_scene(), e));
                    }
                }
                return;
            }
            // Arcade car pickers: the focused car in 3D ("<page>:<entry>").
            if (flow_ && flow_->control() == "arcade" && flow_->carousel() && e.name == "car")
                if (const GtModeWidgetDrawer& d = gt_mode_widget_drawer(); d && flow_->controller()) {
                    const GameFlow::Carousel& c = *flow_->carousel();
                    d(app, r, e, box, alpha,
                      c.page + ":" + c.entries[size_t(c.index)] + "#" + std::to_string(flow_->car_paint()) + "@" +
                          std::to_string(c.age),
                      element_image(renderer_->drawing_scene(), e));
                }
        });
        renderer_->set_widget_drawer([this, &app](SDL_Renderer* r, const Element& e, const SDL_FRect& box, float alpha) {
            if (!e.widget_class || e.widget_class->rfind("mpeg", 0) != 0) return;
            // Arcade course / class pickers: the entry's still (stream/course.ipic) until its
            // movie runs.
            if (*e.widget_class != "mpeg" && flow_ && flow_->carousel()) {
                const GameFlow::Carousel& c = *flow_->carousel();
                const std::string& id = c.entries[size_t(c.index)];
                if (movie_path_ != "stream/" + id + "_full.pss" || !movie_.texture()) {
                    if (SDL_Texture* t = still(app, r, id)) {
                        float tw = 0, th = 0;
                        SDL_GetTextureSize(t, &tw, &th);
                        draw_modulated(r, t, SDL_FRect{0, 0, tw, th}, box, e, alpha);
                    }
                    return;
                }
            }
            if (!movie_.texture()) return;
            SDL_FRect src, dst;
            movie_.picture_rects(box, src, dst); // handles field-split movies
            // Car page steps: 2-D blur over the background (a 3 x 3 box of offset copies).
            float blur = flow_ && flow_->carousel() ? flow_->carousel()->blur : 0;
            if (blur > 0.01f) {
                float px = blur * dst.w / 640.0f;
                for (int k = 0; k < 9; ++k) {
                    SDL_FRect d = dst;
                    d.x += float(k % 3 - 1) * px;
                    d.y += float(k / 3 - 1) * px;
                    draw_modulated(r, movie_.texture(), src, d, e, k == 0 ? alpha : alpha / float(k + 1));
                }
                return;
            }
            draw_modulated(r, movie_.texture(), src, dst, e, alpha);
        });
        if (fonts_.load(*app.vfs(), app.renderer())) renderer_->set_text_drawer(make_game_font_drawer(fonts_));
        sounds_.load(*app.vfs());
        apply_options(app);
        try {
            flow_ = std::make_unique<GameFlow>(vfs_reader(*app.vfs()), lang_);
            flow_->measure_text = font_measure(fonts_);
            connect_options(app);
            game::install_replay_theater(app.data_dir(), app.vfs()); // Replay Theater files
            install_save_dialogs(app);                                // the save page's file dialogs
            if (language_page_) flow_->start("language", "root");
            else flow_->start();
        } catch (const std::exception& e) {
            error_ = e.what();
            LOG_WARN("gt3menu: %s", e.what());
            flow_.reset();
            return;
        }
        // GT3R_GT3MENU_OPEN="<set>:<page>" (e.g. gt_mode:cd/cd_usa_ford) opens a page directly.
        if (const char* env = std::getenv("GT3R_GT3MENU_OPEN")) {
            std::string s = env;
            if (size_t c = s.find(':'); c != std::string::npos) flow_->debug_open(s.substr(0, c), s.substr(c + 1));
        }
        // Scripted input for screenshots.
        if (const char* env = std::getenv("GT3R_GT3MENU_SCRIPT")) {
            std::string s = env;
            size_t start = 0;
            for (;;) {
                size_t c = s.find(',', start);
                std::string tok = s.substr(start, c == std::string::npos ? c : c - start);
                if (tok.rfind("wait:", 0) == 0) {
                    for (int i = std::atoi(tok.c_str() + 5); i > 0; --i) script_.push_back(0);
                } else if (MenuKeys k = key_from_name(tok)) {
                    script_.push_back(k);
                    script_.push_back(0);
                } else if (!tok.empty()) {
                    LOG_WARN("gt3menu: unknown script token '%s'", tok.c_str());
                }
                if (c == std::string::npos) break;
                start = c + 1;
            }
            // Let the first page settle before the scripted keys.
            for (int i = 0; i < 30; ++i) script_.push_front(0);
            run_script(app);
            const char* fr = std::getenv("GT3R_GT3MENU_FREEZE");
            frozen_ = fr && *fr && *fr != '0';
        }
    }

    // Scripted keys, one per frame; a game request pauses the script until gameplay returns
    // (with GT3R_GTMODE_FAKE_RACE the rest then drives the result pages, e.g. the name entry).
    void run_script(App& app) {
        while (!script_.empty() && !flow_->waiting_for_game()) {
            MenuKeys k = script_.front();
            script_.pop_front();
            step(app, k);
        }
    }

    void cycle_language(App& app) {
        if (languages_.size() < 2) return;
        auto it = std::find(languages_.begin(), languages_.end(), lang_);
        size_t i = it == languages_.end() ? 0 : size_t(it - languages_.begin());
        app.set_language(languages_[(i + 1) % languages_.size()]);
        rebuild_flow(app);
    }

    // The menus again in the game's current language, from the title.
    void rebuild_flow(App& app) {
        lang_ = app.language();
        renderer_->clear_cache();
        flow_ = std::make_unique<GameFlow>(vfs_reader(*app.vfs()), lang_);
        flow_->measure_text = font_measure(fonts_);
        connect_options(app);
        flow_->start();
        transition_.stop();
    }

    // One 60 Hz frame of the menu, including its picture (transitions
    // accumulate on the previous frame, so every frame is drawn).
    void step(App& app, MenuKeys keys) {
        flow_->tick(keys);
        for (const std::string& s : flow_->take_sounds()) sounds_.play(app.audio().voices(), s);
        for (std::string& m : flow_->take_log()) messages_.push_back({std::move(m), 240});
        if (messages_.size() > 4) messages_.erase(messages_.begin());
        for (Message& m : messages_) --m.frames;
        messages_.erase(std::remove_if(messages_.begin(), messages_.end(), [](const Message& m) { return m.frames <= 0; }),
                        messages_.end());
        if (auto t = flow_->take_transition()) transition_.start(*t);
        if (auto mv = flow_->take_movie()) start_movie(app, *mv);
        if (auto m = flow_->take_music()) {
            menu_music_ = *m;
            if (m->empty() || !menu_bgm_on(app)) app.audio().stop_music();
            else if (!app.audio().play_sequence(*app.vfs(), *m, bgm::kMenuVolume)) // menu level (music §6)
                LOG_WARN("gt3menu: no sequence %s", m->c_str());
        }
        // The Options music list's preview: the focused race song's intro, looping, at level 1.0,
        // whatever the Menu BGM option (menus §10.1).
        if (auto p = flow_->take_preview()) {
            menu_music_.clear();
            if (*p < 0) app.audio().bgm().stop(app.audio());
            else app.audio().bgm().play_if_different(app.audio(), *app.vfs(), bgm::kRace, *p | bgm::kIntro, true, 1.0f);
        }
        if (auto r = flow_->take_request()) start_game(app, *r);
        if (flow_->take_controller_settings()) {
            // Our controller settings over the menus (Key Configuration): the menus wait on
            // their page underneath and go on when it closes, without the keys still held then.
            app.push(make_settings_screen(kSettingsTabControls, true));
            held_over_ = MenuKeys(~0);
            acc_ = 0;
        }
        ++pending_draws_;
        draw_frame(app);
    }

    // Options menu values ([options] in config.ini): Menu BGM / Menu SE apply at once.
    static bool menu_bgm_on(App& app) { return option_value(&app.config(), "menu_bgm") != "off"; }
    void apply_options(App& app) {
        bool se = option_value(&app.config(), "menu_se") != "off";
        if (auto v = app.config().get("audio", "menu_se")) se = se && to_lower(*v) != "off"; // older key
        sounds_.set_enabled(se);
    }
    void connect_options(App& app) {
        flow_->set_options(&app.config(), [this, &app] {
            app.save_config();
            apply_options(app);
            if (!menu_bgm_on(app)) app.audio().stop_music();
            else if (!menu_music_.empty() && !app.audio().sequence_playing())
                app.audio().play_sequence(*app.vfs(), menu_music_, bgm::kMenuVolume);
        });
    }

    void start_movie(App& app, const std::string& path) {
        movie_path_ = path;
        movie_.stop();
        if (path.empty()) return;
        try {
            movie_.set_audio_device(app.audio().output_device_id());
            movie_.start(video::open_movie(*app.vfs(), path), app.audio().music_volume());
        } catch (const std::exception& e) {
            LOG_WARN("gt3menu: movie %s: %s", path.c_str(), e.what());
            movie_path_.clear();
        }
    }

    void start_game(App& app, const GameRequest& r) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "start race: %s code=%s course=%s car=%s", r.mode.c_str(),
                      r.race_code.c_str(), r.course.c_str(), r.car.c_str());
        LOG_INFO("gt3menu: %s", buf);
        messages_.push_back({buf, 300});
        if (const menu::flow::GtModeWidgetRelease& release = menu::flow::gt_mode_widget_release()) release(app);
        // The race keeps this frame and its transition on screen while it loads (menus §6);
        // the slot is emptied again once the request has been handed on.
        menu::flow::LoadingPicture& loading = menu::flow::race_loading_picture();
        loading.frame = frame_;
        loading.transition = transition_;
        struct Clear {
            ~Clear() { menu::flow::race_loading_picture() = {}; }
        } clear;
        transition_.stop();
        if (r.kind == GameRequest::Kind::PlayMovie) {
            // GT-mode endings: the movie screen pops itself; the menu then resumes.
            play_movie(app, r.course);
            in_game_ = true;
            return;
        }
        if (const GameRequestHandler& h = game_request_handler(); h && h(r, app)) {
            in_game_ = true;
            return;
        }
        for (const ScreenEntry& e : screen_registry())
            if (std::string_view(e.id) == "race") {
                app.push(e.factory());
                in_game_ = true;
                return;
            }
        flow_->game_finished(); // nothing to run: straight back to the menu
    }

    void draw_frame(App& app) {
        if (pending_draws_ == 0) return;
        SDL_Renderer* r = app.renderer();
        if (!frame_) {
            frame_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, int(kMenuWidth),
                                       int(kMenuHeight));
            if (!frame_) return;
            SDL_SetTextureScaleMode(frame_, SDL_SCALEMODE_LINEAR);
        }
        if (!movie_path_.empty()) {
            if (movie_.finished()) start_movie(app, movie_path_); // loops
            movie_.update(r);
        }
        SDL_Texture* prev = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, frame_);
        const SDL_FRect full{0, 0, kMenuWidth, kMenuHeight};
        if (flow_->switching() || flow_->waiting_for_game()) {
            // Loading transition: drawn over the last menu picture, once per frame.
            for (; pending_draws_ > 0; --pending_draws_) transition_.draw_frame(r, full);
        } else {
            transition_.stop();
            pending_draws_ = 0;
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            RenderOptions ro;
            ro.widget_labels = false;
            renderer_->draw_elements(flow_->draw_list(), ro, full);
        }
        SDL_SetRenderTarget(r, prev);
    }

    text::GameFonts fonts_; // before renderer_: the text drawer refers to it
    std::unique_ptr<MenuRenderer> renderer_;
    std::unique_ptr<GameFlow> flow_;
    MenuSounds sounds_;
    ScreenTransition transition_;
    video::MoviePlayer movie_;
    std::string movie_path_;
    // Course / class preview stills (stream/course.ipic), as textures.
    video::CoursePreviews previews_;
    bool previews_loaded_ = false;
    std::map<std::string, SDL_Texture*> stills_;

    // A movie / still in a movie widget, modulated by the widget's corner colours (GS
    // units: 0x80 = 1.0; e.g. the car page's movie fades out towards its bottom edge).
    static void draw_modulated(SDL_Renderer* r, SDL_Texture* t, const SDL_FRect& src, const SDL_FRect& dst,
                               const Element& e, float alpha) {
        float tw = 0, th = 0;
        SDL_GetTextureSize(t, &tw, &th);
        if (tw <= 0 || th <= 0) return;
        const SDL_FPoint pos[4] = {{dst.x, dst.y}, {dst.x + dst.w, dst.y}, {dst.x + dst.w, dst.y + dst.h},
                                   {dst.x, dst.y + dst.h}};
        const SDL_FPoint uv[4] = {{src.x / tw, src.y / th},
                                  {(src.x + src.w) / tw, src.y / th},
                                  {(src.x + src.w) / tw, (src.y + src.h) / th},
                                  {src.x / tw, (src.y + src.h) / th}};
        SDL_Vertex v[4];
        for (int i = 0; i < 4; ++i) {
            SDL_FColor c{1, 1, 1, 1};
            if (e.colors) {
                const auto& k = (*e.colors)[size_t(3 - i)]; // listed from the bottom-left (y up)
                c = {std::min(1.0f, k.r / 128.0f), std::min(1.0f, k.g / 128.0f), std::min(1.0f, k.b / 128.0f),
                     std::min(1.0f, k.a / 128.0f)};
            }
            c.a *= alpha;
            v[i] = {pos[i], c, uv[i]};
        }
        static const int idx[6] = {0, 1, 2, 0, 2, 3};
        // The movie's YUV texture has no alpha of its own and so no blending by default: the
        // corner alphas fade it into the page.
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(r, t, v, 4, idx, 6);
    }

    SDL_Texture* still(App& app, SDL_Renderer* r, const std::string& name) {
        if (!previews_loaded_) {
            previews_loaded_ = true;
            if (app.vfs()) previews_.load(*app.vfs());
        }
        auto it = stills_.find(name);
        if (it != stills_.end()) return it->second;
        SDL_Texture* t = nullptr;
        if (const video::RgbaImage* img = previews_.image(name)) {
            t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img->width, img->height);
            if (t) SDL_UpdateTexture(t, nullptr, img->rgba.data(), img->width * 4);
        }
        stills_[name] = t;
        return t;
    }
    SDL_Texture* frame_ = nullptr;
    int pending_draws_ = 1;
    double acc_ = 0;
    MenuKeys prev_keys_ = 0;
    MenuKeys held_over_ = 0; // see update()
    std::optional<SDL_FPoint> press_; // left button / finger down, UI units
    std::vector<SDL_FPoint> taps_;    // taps since the last update()
    std::optional<SDL_FPoint> hover_; // the hovering pointer's last move since the last update()
    bool back_click_ = false;         // a right click since the last update(): Cancel
    bool in_game_ = false;
    bool frozen_ = false;
    std::deque<MenuKeys> script_;
    std::vector<Message> messages_;
    std::string lang_ = "US";
    std::vector<std::string> languages_;
    bool language_page_ = false;
    std::string error_;
    bool init_tried_ = false;
};

} // namespace

GT3R_REGISTER_SCREEN(gt3menu, "gt3menu", "Game menus (interactive)",
                     "Navigate GT3's own menus: title, arcade, GT mode and options.", 15,
                     [] { return std::make_unique<Gt3MenuScreen>(); });
GT3R_REGISTER_SCREEN(language, "language", "Language page",
                     "PAL discs: the game's language page, then the opening movie and title menu.", 16,
                     [] { return std::make_unique<Gt3MenuScreen>(true); });

} // namespace gt3r
