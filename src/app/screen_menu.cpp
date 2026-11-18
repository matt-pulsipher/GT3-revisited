// Menu viewer: composes original menu pages (.pmb) statically, and browses
// IMGS image sets. First consumer of src/formats/menu and src/menu.
//
// For automated screenshots, GT3R_MENU="<vol path>[:page[:focus]]" opens a
// file directly, e.g. GT3R_MENU=menu/US/gt_mode2.pmb:0 gt3r --screen menu --shot x.png

#include "app/app.h"
#include "app/screen_registry.h"
#include "base/log.h"
#include "formats/menu/imgs.h"
#include "menu/menu_renderer.h"
#include "menu/menu_scene.h"
#include "menu/menu_text.h"
#include "text/game_font.h"

#include <algorithm>
#include <cstdlib>

namespace gt3r {

namespace {

using namespace gt3r::menu;

bool ends_with(const std::string& s, std::string_view suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

class MenuScreen : public Screen {
public:
    ~MenuScreen() override { clear_view(); }

    void update(App& app, double) override {
        if (!renderer_) {
            renderer_ = std::make_unique<MenuRenderer>(app.renderer());
            if (fonts_.load(*app.vfs(), app.renderer())) renderer_->set_text_drawer(make_game_font_drawer(fonts_));
        }
        if (files_dirty_) refresh(app);
        if (const char* env = std::getenv("GT3R_MENU"); env && !env_done_) {
            env_done_ = true;
            open_from_env(app, env);
        }
        const MenuInput& m = app.menu();
        if (viewing_) {
            update_view(app, m);
            return;
        }
        int n = int(files_.size());
        if (n) {
            int page = m.pressed(PadButton::L2) ? -kRows : m.pressed(PadButton::R2) ? kRows : 0;
            sel_ = std::clamp(sel_ + m.vertical() + page, 0, n - 1);
        }
        int dl = m.pressed(PadButton::L1) ? -1 : m.pressed(PadButton::R1) ? 1 : 0;
        if (dl) {
            int count = 0;
            while (kMenuLanguages[count]) ++count;
            lang_ = (lang_ + dl + count) % count;
            files_dirty_ = true;
        }
        if (m.pressed(PadButton::Cross) && n) open(app, files_[sel_]);
        if (m.pressed(PadButton::Triangle)) app.pop();
    }

    void render(App& app, Ui& ui) override {
        if (viewing_) {
            render_view(app, ui);
            return;
        }
        ui.header("MENU VIEWER", std::string("Language: ") + kMenuLanguages[lang_]);
        int n = int(files_.size());
        int first = ui.list_window(n, sel_, kRows);
        for (int i = 0; i < kRows && first + i < n; ++i) {
            int idx = first + i;
            float y = 26 + i * 12.0f;
            if (idx == sel_) ui.fill(4, y - 2, 300, 12, colors::kHighlight);
            bool imgs = ends_with(files_[idx], ".imgs");
            ui.text(8, y, files_[idx], imgs ? colors::kAccent : colors::kText);
        }
        if (n > kRows)
            ui.text_right(300, Ui::kHeight - 30, std::to_string(sel_ + 1) + "/" + std::to_string(n), colors::kDim);
        float px = 312;
        ui.fill(px, 26, Ui::kWidth - px - 6, Ui::kHeight - 76, colors::kPanel);
        ui.text(px + 8, 34, "Layouts (.pmb) compose a menu page:", colors::kDim);
        ui.text(px + 8, 46, "rectangles, images and text in a", colors::kDim);
        ui.text(px + 8, 58, "640x448 space. Image sets (.imgs,", colors::kDim);
        ui.text(px + 8, 70, "yellow) are named texture lists.", colors::kDim);
        if (!error_.empty()) ui.text(px + 8, 94, error_, colors::kBad);
        ui.footer("{Cross}: open  {L1}/{R1}: language  {L2}/{R2}: page  {Triangle}: back");
    }

private:
    static constexpr int kRows = 25;

    void refresh(App& app) {
        files_dirty_ = false;
        files_.clear();
        std::string lang = kMenuLanguages[lang_];
        for (auto& f : list_menu_files(*app.vfs(), lang, ".pmb")) files_.push_back(f);
        for (auto& f : list_menu_files(*app.vfs(), lang, ".imgs")) files_.push_back(f);
        for (auto& f : list_menu_files(*app.vfs(), "", ".imgs")) files_.push_back(f);
        for (const std::string& name : app.vfs()->list_dir("race/" + lang + "/"))
            if (ends_with(name, ".imgs")) files_.push_back("race/" + lang + "/" + name);
        for (const std::string& name : app.vfs()->list_dir("race/"))
            if (ends_with(name, ".imgs")) files_.push_back("race/" + name);
        sel_ = std::clamp(sel_, 0, std::max(0, int(files_.size()) - 1));
    }

    void open_from_env(App& app, std::string spec) {
        std::vector<std::string> parts;
        size_t start = 0;
        for (;;) {
            size_t c = spec.find(':', start);
            parts.push_back(spec.substr(start, c == std::string::npos ? c : c - start));
            if (c == std::string::npos) break;
            start = c + 1;
        }
        open(app, parts[0]);
        if (!viewing_) return;
        if (parts.size() > 1) {
            int p = scene_ ? scene_->file().find_page(parts[1]) : -1;
            page_ = p >= 0 ? p : std::atoi(parts[1].c_str());
            image_ = page_;
        }
        if (parts.size() > 2) focus_ = std::atoi(parts[2].c_str());
        clamp_view();
    }

    void clear_view() {
        if (renderer_) renderer_->set_scene(nullptr);
        scene_.reset();
        for (SDL_Texture* t : imgs_textures_)
            if (t) SDL_DestroyTexture(t);
        imgs_textures_.clear();
        imgs_.reset();
        if (target_) SDL_DestroyTexture(target_);
        target_ = nullptr;
        viewing_ = false;
    }

    void open(App& app, const std::string& path) {
        clear_view();
        error_.clear();
        path_ = path;
        page_ = 0;
        image_ = 0;
        focus_ = -1;
        layers_.clear();
        try {
            if (ends_with(path, ".imgs")) {
                auto data = app.vfs()->read(path);
                if (!data) throw FormatError("cannot read " + path);
                imgs_ = std::make_unique<Imgs>(Imgs::parse(*data));
                for (size_t i = 0; i < imgs_->entries.size(); ++i) {
                    Tex1::Image img = imgs_->decode(*data, i);
                    SDL_Texture* t = nullptr;
                    if (img.width && img.height) {
                        t = SDL_CreateTexture(app.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                              int(img.width), int(img.height));
                        if (t) {
                            SDL_UpdateTexture(t, nullptr, img.rgba.data(), int(img.width * 4));
                            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                        }
                    }
                    imgs_textures_.push_back(t);
                }
            } else {
                scene_ = std::make_unique<MenuScene>(MenuScene::load(*app.vfs(), path));
                renderer_->set_scene(scene_.get());
            }
            viewing_ = true;
        } catch (const std::exception& e) {
            error_ = e.what();
            LOG_WARN("Menu viewer: %s: %s", path.c_str(), e.what());
            clear_view();
        }
    }

    int page_count() const { return scene_ ? int(scene_->file().pages.size()) : 0; }

    void clamp_view() {
        if (scene_) {
            page_ = std::clamp(page_, 0, std::max(0, page_count() - 1));
            int items = page_count() ? int(scene_->file().pages[page_].items.size()) : 0;
            focus_ = std::clamp(focus_, -1, items - 1);
        }
        if (imgs_) image_ = std::clamp(image_, 0, std::max(0, int(imgs_->entries.size()) - 1));
    }

    void update_view(App&, const MenuInput& m) {
        if (m.pressed(PadButton::Triangle)) {
            clear_view();
            return;
        }
        if (imgs_) {
            int n = int(imgs_->entries.size());
            if (n) image_ = (image_ + m.horizontal() + m.vertical() * 8 + n) % n;
            if (m.pressed(PadButton::Square)) imgs_grid_ = !imgs_grid_;
            return;
        }
        if (!scene_) return;
        int pc = page_count();
        if (pc && m.horizontal()) {
            page_ = (page_ + m.horizontal() + pc) % pc;
            focus_ = -1;
            layers_.clear();
            layer_sel_ = 0;
        }
        if (m.vertical()) focus_ += m.vertical();
        if (pc) {
            const Page& pg = scene_->file().pages[page_];
            int nl = int(pg.layers.size());
            if (nl) {
                if (m.pressed(PadButton::L1)) layer_sel_ = (layer_sel_ + nl - 1) % nl;
                if (m.pressed(PadButton::R1)) layer_sel_ = (layer_sel_ + 1) % nl;
                if (m.pressed(PadButton::Square)) {
                    layers_.resize(nl, -1);
                    bool vis = scene_->layer_visible(page_, layer_sel_, options());
                    layers_[layer_sel_] = vis ? 0 : 1;
                }
            }
        }
        if (m.pressed(PadButton::Circle)) render_opts_.widget_labels = !render_opts_.widget_labels;
        if (m.pressed(PadButton::Cross)) render_opts_.outlines = !render_opts_.outlines;
        if (m.pressed(PadButton::Select)) aspect43_ = !aspect43_;
        if (m.pressed(PadButton::Start) && renderer_) {
            game_font_ = !game_font_ && fonts_.loaded();
            if (game_font_) renderer_->set_text_drawer(make_game_font_drawer(fonts_));
            else renderer_->set_text_drawer(draw_text_debug_font);
        }
        clamp_view();
    }

    SceneOptions options() const {
        SceneOptions o;
        o.layer_visible = layers_;
        o.focus = focus_;
        return o;
    }

    void draw_checker(Ui& ui, const SDL_FRect& r) {
        const float cell = 8;
        for (float y = 0; y < r.h; y += cell)
            for (float x = 0; x < r.w; x += cell) {
                bool odd = (int(x / cell) + int(y / cell)) & 1;
                ui.fill(r.x + x, r.y + y, std::min(cell, r.w - x), std::min(cell, r.h - y),
                        odd ? Color{60, 60, 66} : Color{44, 44, 50});
            }
    }

    void render_view(App& app, Ui& ui) {
        if (imgs_) {
            render_imgs(ui);
            return;
        }
        const PmbFile& f = scene_->file();
        const Page* pg = f.pages.empty() ? nullptr : &f.pages[page_];
        ui.header("MENU VIEWER", path_);

        // Side panel: page, layers, focus.
        const float pw = 196;
        ui.fill(4, 24, pw, Ui::kHeight - 48, colors::kPanel);
        float y = 30;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Page %d/%zu", page_ + 1, f.pages.size());
        ui.text(10, y, buf, colors::kDim);
        y += 12;
        if (pg) {
            std::string name = pg->name.size() > 23 ? pg->name.substr(0, 22) + "~" : pg->name;
            ui.text(10, y, name, colors::kAccent);
            y += 16;
            ui.text(10, y, "Layers ({L1}/{R1}, {Square}):", colors::kDim);
            y += 12;
            SceneOptions so = options();
            for (int l = 0; l < int(pg->layers.size()) && y < Ui::kHeight - 90; ++l) {
                if (l == layer_sel_) ui.fill(8, y - 2, pw - 8, 12, colors::kHighlight);
                bool vis = scene_->layer_visible(page_, l, so);
                std::snprintf(buf, sizeof(buf), "%s %.16s (%zu)", vis ? "[x]" : "[ ]", pg->layers[l].name.c_str(),
                              pg->layers[l].elements.size() + pg->layers[l].widgets.size());
                ui.text(10, y, buf, vis ? colors::kText : colors::kDim);
                y += 12;
            }
            y += 6;
            if (focus_ >= 0 && focus_ < int(pg->items.size()))
                std::snprintf(buf, sizeof(buf), "Focus: %.15s", pg->items[focus_].name.c_str());
            else
                std::snprintf(buf, sizeof(buf), "Focus: none (%zu items)", pg->items.size());
            ui.text(10, y, buf, colors::kDim);
            y += 12;
            std::snprintf(buf, sizeof(buf), "%zu images%s", f.images.size(),
                          scene_->image_failures() ? " (some failed)" : "");
            ui.text(10, y, buf, colors::kDim);
        }

        // Compose at native 640x448 into a texture, then show it at 4:3.
        if (!target_) {
            target_ = SDL_CreateTexture(app.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                                        int(kMenuWidth), int(kMenuHeight));
            if (target_) SDL_SetTextureScaleMode(target_, SDL_SCALEMODE_LINEAR);
        }
        float ax = pw + 10, ay = 24, aw = Ui::kWidth - ax - 4, ah = Ui::kHeight - 48;
        float aspect = aspect43_ ? 4.0f / 3.0f : kMenuWidth / kMenuHeight;
        float w = std::min(aw, ah * aspect), h = w / aspect;
        SDL_FRect dst{ax + (aw - w) / 2, ay + (ah - h) / 2, w, h};
        if (target_ && pg) {
            SDL_Renderer* r = app.renderer();
            SDL_Texture* prev = SDL_GetRenderTarget(r);
            SDL_SetRenderTarget(r, target_);
            SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
            SDL_RenderClear(r);
            renderer_->draw(page_, options(), render_opts_, SDL_FRect{0, 0, kMenuWidth, kMenuHeight});
            SDL_SetRenderTarget(r, prev);
            SDL_RenderTexture(r, target_, nullptr, &dst);
        }
        ui.footer("{Left/Right} page  {Up/Down} focus  {Cross} outline  {Circle} widget  {Select} 4:3  {Start} font  {Triangle} back");
    }

    void render_imgs(Ui& ui) {
        ui.header("MENU VIEWER", path_);
        int n = int(imgs_->entries.size());
        char buf[160];
        if (imgs_grid_) {
            // Thumbnail grid around the selection.
            const int cols = 8, rows = 4;
            const float cw = (Ui::kWidth - 8) / float(cols), ch = (Ui::kHeight - 60) / float(rows);
            int first = std::max(0, (image_ / cols - rows / 2) * cols);
            first = std::min(first, std::max(0, ((n + cols - 1) / cols - rows) * cols));
            for (int i = 0; i < cols * rows && first + i < n; ++i) {
                int idx = first + i;
                float x = 4 + (i % cols) * cw, y = 24 + (i / cols) * ch;
                if (idx == image_) ui.fill(x, y, cw - 2, ch - 2, colors::kHighlight);
                if (SDL_Texture* t = imgs_textures_[idx]) {
                    float tw, th;
                    SDL_GetTextureSize(t, &tw, &th);
                    float bw = cw - 8, bh = ch - 18;
                    float s = std::min({bw / tw, bh / th, 1.0f});
                    SDL_FRect d{x + 3 + (bw - tw * s) / 2, y + 3 + (bh - th * s) / 2, tw * s, th * s};
                    SDL_RenderTexture(ui.renderer(), t, nullptr, &d);
                }
                std::string nm = imgs_->entries[idx].name.substr(0, size_t(cw / 8) - 1);
                ui.text(x + 3, y + ch - 13, nm, idx == image_ ? colors::kAccent : colors::kDim);
            }
        } else if (n) {
            SDL_Texture* t = imgs_textures_[image_];
            SDL_FRect box{8, 40, Ui::kWidth - 16, Ui::kHeight - 90};
            if (t) {
                float tw, th;
                SDL_GetTextureSize(t, &tw, &th);
                float s = std::min({box.w / tw, box.h / th, 2.0f});
                SDL_FRect d{box.x + (box.w - tw * s) / 2, box.y + (box.h - th * s) / 2, tw * s, th * s};
                draw_checker(ui, d);
                SDL_RenderTexture(ui.renderer(), t, nullptr, &d);
                std::snprintf(buf, sizeof(buf), "%d/%d  %s  %gx%g", image_ + 1, n,
                              imgs_->entries[image_].name.c_str(), tw, th);
            } else {
                std::snprintf(buf, sizeof(buf), "%d/%d  %s  (decode failed)", image_ + 1, n,
                              imgs_->entries[image_].name.c_str());
            }
            ui.text(8, 26, buf, colors::kAccent);
        }
        ui.footer("{Arrows}: select  {Square}: grid/single  {Triangle}: back");
    }

    text::GameFonts fonts_; // declared before renderer_: the text drawer refers to it
    bool game_font_ = true;
    std::unique_ptr<MenuRenderer> renderer_;
    std::vector<std::string> files_;
    bool files_dirty_ = true;
    bool env_done_ = false;
    int lang_ = 0;
    int sel_ = 0;
    std::string error_;

    bool viewing_ = false;
    std::string path_;
    std::unique_ptr<MenuScene> scene_;
    int page_ = 0, focus_ = -1, layer_sel_ = 0;
    std::vector<int> layers_;
    RenderOptions render_opts_;
    bool aspect43_ = true;
    SDL_Texture* target_ = nullptr;

    std::unique_ptr<Imgs> imgs_;
    std::vector<SDL_Texture*> imgs_textures_;
    int image_ = 0;
    bool imgs_grid_ = true;
};

} // namespace

GT3R_REGISTER_SCREEN(menu_viewer, "menu", "Menu viewer",
                     "Compose original menu screens (.pmb) and browse IMGS image sets.", 60,
                     [] { return std::make_unique<MenuScreen>(); });

} // namespace gt3r
