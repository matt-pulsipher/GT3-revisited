// GT3's car settings menu (menu/setting.mbl) as a screen: the original pages
// and layouts run by menu::flow::GameFlow (control "setting"), with the
// GT-mode bindings of game/gt_mode_menu/ (sliders, value texts, locks,
// part changes). Opened from a race's pre-race quick menu ("Setting") via
// make_gt_setting_screen(), or `gt3r --screen gtsetting`.
//
// Scripted screenshots: GT3R_GTSETTING_SCRIPT="cross,down,wait:30" (same
// syntax as GT3R_GT3MENU_SCRIPT), GT3R_GTSETTING_EVENT=<race code>.

#include "app/app.h"
#include "app/screen_gtmode.h"
#include "app/screen_registry.h"
#include "base/log.h"
#include "game/gt_mode_menu.h"
#include "menu/flow/game_flow.h"
#include "menu/flow/gt_mode_hooks.h"
#include "menu/flow/menu_sound.h"
#include "menu/menu_renderer.h"
#include "menu/menu_text.h"
#include "text/game_font.h"

#include <cstdlib>
#include <deque>
#include <optional>

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

MenuKeys setting_keys(const PadState& pad) {
    MenuKeys k = 0;
    auto set = [&](MenuKey key, bool on) {
        if (on) k |= key_bit(key);
    };
    set(MenuKey::Up, pad.down(PadButton::Up) || pad.axis(PadAxis::LY) < 0x40);
    set(MenuKey::Down, pad.down(PadButton::Down) || pad.axis(PadAxis::LY) > 0xC0);
    set(MenuKey::Left, pad.down(PadButton::Left) || pad.axis(PadAxis::LX) < 0x40);
    set(MenuKey::Right, pad.down(PadButton::Right) || pad.axis(PadAxis::LX) > 0xC0);
    set(MenuKey::Confirm, pad.down(PadButton::Circle) || pad.down(PadButton::Cross));
    set(MenuKey::Cancel, pad.down(PadButton::Triangle) || pad.down(PadButton::Square));
    set(MenuKey::Start, pad.down(PadButton::Start));
    set(MenuKey::L1, pad.down(PadButton::L1));
    set(MenuKey::R1, pad.down(PadButton::R1));
    return k;
}

MenuKeys script_key(const std::string& s) {
    if (s == "up") return key_bit(MenuKey::Up);
    if (s == "down") return key_bit(MenuKey::Down);
    if (s == "left") return key_bit(MenuKey::Left);
    if (s == "right") return key_bit(MenuKey::Right);
    if (s == "cross" || s == "ok") return key_bit(MenuKey::Confirm);
    if (s == "triangle" || s == "back") return key_bit(MenuKey::Cancel);
    if (s == "l1") return key_bit(MenuKey::L1);
    if (s == "r1") return key_bit(MenuKey::R1);
    return 0;
}

// Hosts a GT-mode menu set started on its own (settings, records).
class GtSettingScreen : public Screen {
public:
    GtSettingScreen(std::string set, std::string page, std::string arg)
        : set_(std::move(set)), page_(std::move(page)), event_(std::move(arg)) {}
    // The save page ("Save game") for a race's result: the game with the round saved as a
    // new or existing save, the running game untouched.
    explicit GtSettingScreen(const GameResult& round) : set_("memorycard"), page_("file_operation"), round_(round) {}
    ~GtSettingScreen() override {
        if (round_) game::end_race_game_save();
        renderer_.reset();
        if (frame_) SDL_DestroyTexture(frame_);
    }

    void update(App& app, double dt) override {
        if (!init_) init(app);
        if (!flow_) {
            if (app.menu().pressed(PadButton::Triangle)) app.pop();
            return;
        }
        if (flow_->exited()) {
            if (set_.empty()) app.audio().stop_music(); // the presentations' music ends with them
            app.pop();
            return;
        }
        MenuKeys keys = setting_keys(app.input().pad(0));
        acc_ += dt;
        while (acc_ >= kFrame) {
            acc_ -= kFrame;
            step(app, keys);
        }
    }

    void render(App& app, Ui& ui) override {
        if (!flow_) {
            ui.header("CAR SETTINGS", "");
            ui.text(8, 40, error_.empty() ? "loading..." : error_, colors::kBad);
            ui.footer("Triangle: back");
            return;
        }
        draw_frame(app);
        const UiRect pic = ui.picture43();
        const SDL_FRect dst{pic.x, pic.y, pic.w, pic.h};
        if (frame_) SDL_RenderTexture(app.renderer(), frame_, nullptr, &dst);
    }

    bool follows_aspect() const override { return true; } // the game's menus

private:
    void init(App& app) {
        init_ = true;
        if (!app.vfs()) {
            error_ = "No disc loaded";
            return;
        }
        FileReader read = vfs_reader(*app.vfs());
        if (!game::ensure_gt_mode_session(read)) {
            error_ = "GT mode data could not be loaded";
            return;
        }
        if (set_ == "setting" && !game::begin_setting(event_)) {
            error_ = "No car: buy a car in GT mode first";
            return;
        }
        if (set_ == "records") game::begin_records(event_);
        if (round_ && !game::begin_race_game_save(*round_)) {
            error_ = "GT mode data could not be loaded";
            return;
        }
        if (set_.empty() && !event_.empty()) { // gt3r --screen gtprize: "<event>:<place>"
            size_t c = event_.find(':');
            game::queue_race_prize(event_.substr(0, c), c == std::string::npos ? 0 : std::atoi(event_.c_str() + c + 1));
        }
        const bool shows = set_.empty();
        renderer_ = std::make_unique<MenuRenderer>(app.renderer());
        if (fonts_.load(*app.vfs(), app.renderer())) renderer_->set_text_drawer(make_game_font_drawer(fonts_));
        // GT-mode 3D models (prize presentations: cups, licence cards; car renders).
        renderer_->set_widget_overlay_drawer([this, &app](SDL_Renderer* r, const Element& e, const SDL_FRect& box,
                                                          float alpha) {
            if (flow_ && flow_->controller())
                if (const GtModeWidgetDrawer& d = gt_mode_widget_drawer(); d)
                    d(app, r, e, box, alpha, flow_->controller()->page_name(), element_image(renderer_->drawing_scene(), e));
        });
        sounds_.load(*app.vfs());
        try {
            flow_ = std::make_unique<GameFlow>(read, app.language());
            flow_->measure_text = font_measure(fonts_);
            if (shows) flow_->start_presentations();
            else if (round_) flow_->start_save_game();
            else flow_->start(set_, page_);
        } catch (const std::exception& e) {
            error_ = e.what();
            flow_.reset();
            return;
        }
        if (const char* env = std::getenv("GT3R_GTSETTING_SCRIPT")) {
            std::deque<MenuKeys> script;
            std::string s = env;
            size_t start = 0;
            for (;;) {
                size_t c = s.find(',', start);
                std::string tok = s.substr(start, c == std::string::npos ? c : c - start);
                if (tok.rfind("wait:", 0) == 0) {
                    for (int i = std::atoi(tok.c_str() + 5); i > 0; --i) script.push_back(0);
                } else if (MenuKeys k = script_key(tok)) {
                    script.push_back(k);
                    script.push_back(0);
                }
                if (c == std::string::npos) break;
                start = c + 1;
            }
            for (int i = 0; i < 30; ++i) script.push_front(0);
            for (MenuKeys k : script) step(app, k);
        }
    }

    void step(App& app, MenuKeys keys) {
        flow_->tick(keys);
        if (const MenuController* c = flow_->controller(); c && c->has_page() && c->page_name() != last_page_) {
            last_page_ = c->page_name();
            LOG_INFO("gt setting: page %s (level %d)", last_page_.c_str(), c->depth());
        }
        for (const std::string& s : flow_->take_sounds()) sounds_.play(app.audio().voices(), s);
        if (auto m = flow_->take_music()) { // presentation music (bgm cues); stopped when it closes
            if (m->empty()) app.audio().stop_music();
            else app.audio().play_sequence(*app.vfs(), *m, bgm::kMenuVolume);
        }
        for (std::string& m : flow_->take_log()) LOG_INFO("gt setting: %s", m.c_str());
        ++pending_draws_;
        draw_frame(app);
    }

    void draw_frame(App& app) {
        if (pending_draws_ == 0) return;
        pending_draws_ = 0;
        SDL_Renderer* r = app.renderer();
        if (!frame_) {
            frame_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, int(kMenuWidth),
                                       int(kMenuHeight));
            if (!frame_) return;
            SDL_SetTextureScaleMode(frame_, SDL_SCALEMODE_LINEAR);
        }
        SDL_Texture* prev = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, frame_);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
        SDL_RenderClear(r);
        RenderOptions ro;
        ro.widget_labels = false;
        renderer_->draw_elements(flow_->draw_list(), ro, SDL_FRect{0, 0, kMenuWidth, kMenuHeight});
        SDL_SetRenderTarget(r, prev);
    }

    std::string set_, page_, event_, last_page_;
    std::optional<GameResult> round_; // make_game_save_screen()
    bool init_ = false;
    text::GameFonts fonts_;
    std::unique_ptr<MenuRenderer> renderer_;
    std::unique_ptr<GameFlow> flow_;
    MenuSounds sounds_;
    SDL_Texture* frame_ = nullptr;
    int pending_draws_ = 1;
    double acc_ = 0;
    std::string error_;
};

} // namespace

std::unique_ptr<Screen> make_gt_setting_screen(std::string event) {
    return std::make_unique<GtSettingScreen>("setting", "root", std::move(event));
}

std::unique_ptr<Screen> make_records_screen(std::string key) {
    return std::make_unique<GtSettingScreen>("records", "records_free_tt", std::move(key));
}

std::unique_ptr<Screen> make_game_save_screen(const GameResult& r) { return std::make_unique<GtSettingScreen>(r); }

std::unique_ptr<Screen> make_presentation_screen() {
    return std::make_unique<GtSettingScreen>("", "", "");
}

GT3R_REGISTER_SCREEN(gtrecords, "gtrecords", "Records", "The records menu (rankings) for a key from GT3R_RECORDS_KEY.",
                     18, [] {
                         const char* key = std::getenv("GT3R_RECORDS_KEY");
                         return make_records_screen(key ? key : "mt:M400");
                     });

GT3R_REGISTER_SCREEN(gtprize, "gtprize", "GT Mode race prize",
                     "The race prize page (cup / consolation) for GT3R_PRIZE=<event>:<place> (screenshots).", 19, [] {
                         const char* ev = std::getenv("GT3R_PRIZE");
                         return std::make_unique<GtSettingScreen>("", "", ev ? ev : "Gsunday_e_0001:0");
                     });

GT3R_REGISTER_SCREEN(gtsetting, "gtsetting", "GT Mode car settings",
                     "The car settings menu (springs, dampers, gears, LSD, ...) for the GT-mode car.", 17, [] {
                         const char* ev = std::getenv("GT3R_GTSETTING_EVENT");
                         return make_gt_setting_screen(ev ? ev : "");
                     });

} // namespace gt3r
