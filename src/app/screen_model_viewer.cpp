// Reusable 3D model viewer (see screen_model_viewer.h) and the `viewer`
// demo screen with procedural test models.

#include "app/screen_model_viewer.h"

#include "app/app.h"
#include "app/screen_registry.h"
#include "base/file.h"
#include "base/log.h"
#include "formats/model/gltf_export.h"
#include "render/test_models.h"
#include "render/view3d.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace gt3r {

namespace {

constexpr float kListW = 196;
constexpr float kTop = 24;
constexpr float kBottom = Ui::kHeight - 18;
constexpr float kRowH = 12;

const char* wire_name(render::WireMode m) {
    switch (m) {
    case render::WireMode::Off: return "solid";
    case render::WireMode::Overlay: return "solid+wire";
    default: return "wire";
    }
}

const char* cull_name(const std::optional<render::CullMode>& c) {
    if (!c) return "material";
    switch (*c) {
    case render::CullMode::None: return "none";
    case render::CullMode::Back: return "back";
    default: return "front";
    }
}

bool inside(const SDL_FRect& r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

class ModelViewerScreen : public Screen {
public:
    ModelViewerScreen(std::string title, std::vector<std::string> items, ModelLoader load, ModelExport exports)
        : title_(std::move(title)), items_(std::move(items)), load_(std::move(load)), exports_(std::move(exports)) {
        if (exports_.folder.empty()) exports_.folder = file_name(title_);
        apply_debug_state();
    }

    ~ModelViewerScreen() override {
        model_.reset();
        grid_.reset();
        view_.reset();
    }

    void update(App& app, double dt) override {
        const MenuInput& m = app.menu();
        int n = int(items_.size());
        PadState pad = app.input().pad(0);

        if (!fullscreen_) {
            if (n > 0) {
                int prev = sel_;
                sel_ = std::clamp(sel_ + m.vertical(), 0, n - 1);
                if (sel_ != prev) settle_ = 0;
            }
            // The list owns the d-pad and left stick; only zoom reaches the camera.
            pad.buttons = u16(pad.buttons & ~((1u << unsigned(PadButton::Up)) | (1u << unsigned(PadButton::Down)) |
                                              (1u << unsigned(PadButton::Left)) | (1u << unsigned(PadButton::Right))));
            pad.axes[size_t(PadAxis::LX)] = pad.axes[size_t(PadAxis::LY)] = 128;
            if (m.pressed(PadButton::Cross) && n > 0) {
                if (loaded_ != sel_) load(app, sel_);
                fullscreen_ = true;
            }
            if (m.pressed(PadButton::Triangle)) {
                app.pop();
                return;
            }
        } else if (m.pressed(PadButton::Triangle) || m.pressed(PadButton::Circle)) {
            fullscreen_ = false;
        } else if (m.pressed(PadButton::Cross)) {
            camera_.turntable = !camera_.turntable;
        }
        // Load the highlighted item once the selection has settled.
        settle_ += dt;
        if (n > 0 && loaded_ != sel_ && settle_ > 0.25) load(app, sel_);

        if (m.pressed(PadButton::Square)) options_.wire = render::WireMode((int(options_.wire) + 1) % 3);
        if (m.pressed(PadButton::Select)) {
            options_.lod = options_.lod >= max_lod_ ? -1 : options_.lod + 1;
            update_stats();
        }
        if (m.pressed(PadButton::Start)) {
            if (!options_.cull) options_.cull = render::CullMode::None;
            else if (*options_.cull == render::CullMode::None) options_.cull = render::CullMode::Back;
            else if (*options_.cull == render::CullMode::Back) options_.cull = render::CullMode::Front;
            else options_.cull.reset();
        }
        if (m.pressed(PadButton::L3)) show_grid_ = !show_grid_;
        if ((m.pressed(PadButton::R2) || (debug_export_ && loaded_ >= 0)) && n > 0) {
            debug_export_ = false;
            save_glb(app, loaded_ >= 0 ? loaded_ : sel_);
        }
        if (notice_time_ > 0) notice_time_ -= dt;
        camera_.update(pad, dt);
    }

    void on_event(App& app, const SDL_Event& e) override {
        float x = 0, y = 0;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) x = e.button.x, y = e.button.y;
        else if (e.type == SDL_EVENT_MOUSE_WHEEL) x = e.wheel.mouse_x, y = e.wheel.mouse_y;
        else if (e.type == SDL_EVENT_FINGER_DOWN) x = e.tfinger.x, y = e.tfinger.y;

        // Taps/clicks on the list select an item.
        if (!fullscreen_ && (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_FINGER_DOWN) && x < kListW &&
            y >= kTop && y < kBottom) {
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.which == SDL_TOUCH_MOUSEID) return;
            int row = int((y - kTop) / kRowH);
            int idx = first_row_ + row;
            if (idx >= 0 && idx < int(items_.size())) {
                if (idx == sel_ && loaded_ == sel_) fullscreen_ = true; // second tap: enlarge
                sel_ = idx;
                load(app, sel_);
            }
            return;
        }
        bool starts_here = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_WHEEL ||
                           e.type == SDL_EVENT_FINGER_DOWN;
        if (starts_here && !inside(view_rect(), x, y)) return;
        camera_.handle_event(e);
    }

    void render(App& app, Ui& ui) override {
        if (clean_) { // only the model, over the whole window (cut-out renders)
            if (app.gpu() && !view_) view_ = std::make_unique<render::View3D>(*app.gpu());
            if (view_ && model_) {
                std::vector<render::DrawItem> draw{{model_.get(), {}, -2}};
                view_->draw(ui.renderer(), {0, 0, float(Ui::kWidth), float(Ui::kHeight)}, camera_, draw, options_);
            }
            return;
        }
        ui.header("3D VIEWER: " + title_, items_.empty() ? "" : items_[size_t(std::max(loaded_, 0))]);

        SDL_FRect vr = view_rect();
        if (app.gpu() && !view_) view_ = std::make_unique<render::View3D>(*app.gpu());
        if (view_ && model_) {
            std::vector<render::DrawItem> draw{{model_.get(), {}, -2}};
            if (show_grid_ && grid_) draw.push_back({grid_.get(), {}, -1, true});
            view_->draw(ui.renderer(), vr, camera_, draw, options_);
        } else {
            ui.fill(vr.x, vr.y, vr.w, vr.h, colors::kPanel);
            const char* msg = !app.gpu() ? "3D renderer unavailable (2D-only mode)"
                              : items_.empty() ? "Nothing to show"
                                               : "Loading...";
            if (!error_.empty()) msg = "";
            ui.text_center_in(vr.x, vr.w, vr.y + vr.h / 2 - 4, msg, colors::kDim);
        }
        if (!error_.empty()) ui.text(vr.x + 6, vr.y + 6, Ui::clip(error_, vr.w - 12), colors::kBad);
        if (notice_time_ > 0)
            ui.text(vr.x + 6, vr.y + (error_.empty() ? 6 : 18), Ui::clip(notice_, vr.w - 12),
                    notice_bad_ ? colors::kBad : colors::kGood);

        if (!fullscreen_) draw_list(ui);
        draw_stats(app, ui, vr);
        ui.footer(fullscreen_ ? "Stick: orbit L1/R1: zoom X: spin []: wire Sel: LOD Start: cull R2: export Tri: back"
                              : "Up/Down: select  Cross: view  Square: wireframe  L1/R1: zoom  R2: export  Triangle: back");
    }

private:
    // GT3R_VIEWER="item=2,wire=1,lod=-1,cull=0,full=1,yaw=0.5,pitch=0.3,zoom=0.8,spin=0" presets the
    // viewer state, so `--shot` screenshots can show any configuration. find=<text> selects the
    // first item containing it; clean=1 draws only the model over the whole window on a flat
    // magenta (key=1) or the clear colour, for cut-out renders (mod pictures). export=1 saves the
    // first item loaded as R2 does.
    void apply_debug_state() {
        const char* env = SDL_getenv("GT3R_VIEWER");
        if (!env) return;
        std::string s = env;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t end = s.find(',', pos);
            if (end == std::string::npos) end = s.size();
            std::string kv = s.substr(pos, end - pos);
            pos = end + 1;
            size_t eq = kv.find('=');
            if (eq == std::string::npos) continue;
            std::string k = kv.substr(0, eq);
            float v = std::strtof(kv.c_str() + eq + 1, nullptr);
            if (k == "find") {
                std::string want = kv.substr(eq + 1);
                for (size_t i = 0; i < items_.size(); ++i)
                    if (items_[i].find(want) != std::string::npos) {
                        sel_ = int(i);
                        break;
                    }
            } else if (k == "clean") clean_ = v != 0, fullscreen_ = fullscreen_ || clean_;
            else if (k == "key" && v != 0) {
                options_.clear[0] = 1, options_.clear[1] = 0, options_.clear[2] = 1, options_.clear[3] = 1;
            } else if (k == "item") sel_ = std::clamp(int(v), 0, std::max(0, int(items_.size()) - 1));
            else if (k == "wire") options_.wire = render::WireMode(std::clamp(int(v), 0, 2));
            else if (k == "lod") debug_lod_ = int(v);
            else if (k == "cull") options_.cull = render::CullMode(std::clamp(int(v), 0, 2));
            else if (k == "full") fullscreen_ = v != 0;
            else if (k == "yaw") debug_yaw_ = v;
            else if (k == "pitch") debug_pitch_ = v;
            else if (k == "zoom") debug_zoom_ = v;
            else if (k == "spin") camera_.turntable = v != 0;
            else if (k == "grid") show_grid_ = v != 0;
            else if (k == "export") debug_export_ = v != 0;
        }
    }

    SDL_FRect view_rect() const {
        if (fullscreen_) return {0, 20, float(Ui::kWidth), kBottom - 20 + 2};
        return {kListW + 6, kTop - 2, Ui::kWidth - kListW - 10, kBottom - kTop};
    }

    void draw_list(Ui& ui) {
        int n = int(items_.size());
        int rows = int((kBottom - kTop) / kRowH);
        first_row_ = ui.list_window(n, sel_, rows);
        ui.fill(0, kTop - 4, kListW + 2, kBottom - kTop + 4, colors::kBackground);
        for (int i = 0; i < rows && first_row_ + i < n; ++i) {
            int idx = first_row_ + i;
            float y = kTop + i * kRowH;
            if (idx == sel_) ui.fill(4, y - 2, kListW - 4, kRowH, colors::kHighlight);
            std::string name = items_[size_t(idx)];
            name = Ui::clip(name, kListW - 12);
            ui.text(8, y, name, idx == loaded_ ? colors::kAccent : colors::kText);
        }
    }

    void draw_stats(App& app, Ui& ui, const SDL_FRect& vr) {
        char line[160];
        std::snprintf(line, sizeof(line), "meshes %zu  verts %zu  tris %zu  tex %zu", stats_.meshes, stats_.vertices,
                      stats_.triangles, stats_.textures);
        float y = vr.y + vr.h - 22;
        ui.fill(vr.x, y - 4, vr.w, 26, Color{10, 11, 14, 170});
        ui.text(vr.x + 6, y, line, colors::kText);
        char lod[16];
        if (options_.lod < 0) std::snprintf(lod, sizeof(lod), "all");
        else std::snprintf(lod, sizeof(lod), "%d/%d", options_.lod, max_lod_);
        if (view_)
            std::snprintf(line, sizeof(line), "LOD %s  %s  cull %s  %s %dxAA %dx%d", lod, wire_name(options_.wire),
                          cull_name(options_.cull), app.gpu()->driver_name(), app.gpu()->msaa(), view_->pixel_width(),
                          view_->pixel_height());
        else
            std::snprintf(line, sizeof(line), "LOD %s  %s  cull %s", lod, wire_name(options_.wire), cull_name(options_.cull));
        ui.text(vr.x + 6, y + 10, line, colors::kDim);
    }

    // A file name from an item's label: its first word ("<id>  <name>" in the car viewer).
    static std::string file_name(const std::string& label) {
        std::string out;
        for (char c : label.substr(0, label.find("  "))) {
            const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            out += keep ? char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) : '_';
        }
        return out.empty() ? std::string("model") : out.substr(0, 64);
    }

    void save_glb(App& app, int index) {
        notice_time_ = 6;
        notice_bad_ = true;
        try {
            scene::Model model = (exports_.load ? exports_.load : load_)(app, size_t(index));
            GlbExportOptions o;
            o.lod = options_.lod;
            const Bytes glb = export_glb(model, o);
            const std::string rel = "exports/" + exports_.folder + "/" + file_name(items_[size_t(index)]) +
                                    (o.lod > 0 ? "_lod" + std::to_string(o.lod) : "") + ".glb";
            const std::string path = app.data_dir() + rel;
            if (!host_make_dir(app.data_dir() + "exports/" + exports_.folder) || !write_host_file(path, glb)) {
                notice_ = "Could not save " + path;
                LOG_WARN("Viewer: %s", notice_.c_str());
                return;
            }
            notice_ = "Saved " + rel;
            notice_bad_ = false;
            LOG_INFO("Viewer: saved %s (%zu bytes)", path.c_str(), glb.size());
        } catch (const std::exception& e) {
            notice_ = std::string("Could not export: ") + e.what();
            LOG_WARN("Viewer: %s", notice_.c_str());
        }
    }

    void update_stats() {
        size_t i = size_t(options_.lod + 1);
        stats_ = i < lod_stats_.size() ? lod_stats_[i] : render::ModelStats{};
    }

    void load(App& app, int index) {
        loaded_ = index;
        settle_ = 0;
        error_.clear();
        model_.reset();
        grid_.reset();
        stats_ = {};
        lod_stats_.clear();
        try {
            scene::Model model = load_(app, size_t(index));
            if (model.bounds_min[0] == 0 && model.bounds_max[0] == 0 && model.bounds_min[1] == 0 &&
                model.bounds_max[1] == 0)
                model.compute_bounds();
            lod_stats_.clear();
            lod_stats_.push_back(render::model_stats(model, -1));
            max_lod_ = lod_stats_[0].max_lod;
            for (int l = 0; l <= max_lod_; ++l) lod_stats_.push_back(render::model_stats(model, l));
            if (options_.lod > max_lod_) options_.lod = 0;
            camera_.frame(model.bounds_min, model.bounds_max);
            if (debug_lod_ != -2) options_.lod = std::min(debug_lod_, max_lod_);
            if (debug_yaw_) camera_.yaw = *debug_yaw_;
            if (debug_pitch_) camera_.pitch = *debug_pitch_;
            if (debug_zoom_) camera_.distance *= *debug_zoom_;
            update_stats();
            if (app.gpu()) {
                model_ = std::make_unique<render::GpuModel>(*app.gpu(), model);
                scene::Model grid = render::make_ground_grid(model.bounds_min, model.bounds_max);
                grid_ = std::make_unique<render::GpuModel>(*app.gpu(), grid);
            }
            LOG_INFO("Viewer: loaded '%s' (%zu meshes, %zu vertices, %zu triangles, %zu textures)",
                     items_[size_t(index)].c_str(), stats_.meshes, stats_.vertices, stats_.triangles, stats_.textures);
        } catch (const std::exception& e) {
            error_ = e.what();
            LOG_WARN("Viewer: could not load '%s': %s", items_[size_t(index)].c_str(), e.what());
        }
    }

    std::string title_;
    std::vector<std::string> items_;
    ModelLoader load_;
    ModelExport exports_;
    std::string notice_; // the last export's result, shown for notice_time_ seconds
    double notice_time_ = 0;
    bool notice_bad_ = false;
    int sel_ = 0, loaded_ = -1, first_row_ = 0, max_lod_ = 0;
    double settle_ = 1.0; // load the first item immediately
    bool fullscreen_ = false, show_grid_ = true, clean_ = false;
    std::string error_;
    int debug_lod_ = -2;
    bool debug_export_ = false;
    std::optional<float> debug_yaw_, debug_pitch_, debug_zoom_;
    render::ModelStats stats_;
    std::vector<render::ModelStats> lod_stats_; // [0] = all LODs, [1 + n] = LOD n
    render::OrbitCamera camera_;
    render::DrawOptions options_;
    std::unique_ptr<render::View3D> view_;
    std::unique_ptr<render::GpuModel> model_, grid_;
};

} // namespace

std::unique_ptr<Screen> make_model_viewer(std::string title, std::vector<std::string> items, ModelLoader load,
                                          ModelExport exports) {
    return std::make_unique<ModelViewerScreen>(std::move(title), std::move(items), std::move(load), std::move(exports));
}

GT3R_REGISTER_SCREEN_NO_DISC(model_viewer_demo, "viewer", "3D viewer (test scene)",
                     "Renderer test: procedural models, materials, orbit camera, wireframe.", 90, [] {
                         std::vector<std::string> names;
                         for (int i = 0; i < render::kTestModelVariants; ++i) names.push_back(render::test_model_name(i));
                         return make_model_viewer("Test scene", std::move(names),
                                                  [](App&, size_t i) { return render::make_test_model(int(i)); });
                     });

} // namespace gt3r
