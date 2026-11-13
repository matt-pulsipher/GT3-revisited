// Asset browser: navigates the merged (disc + mods) filesystem and previews
// textures and audio. Doubles as the first consumer of the format decoders. Each texture shows
// its texture pack key (and the mod replacing it); Square saves the file's textures as the
// texture dump does (formats/texture_pack.h).

#include "app/app.h"
#include "app/screen_registry.h"
#include "base/log.h"
#include "formats/clut_animation.h"
#include "formats/pcsx2_textures.h"
#include "formats/tex1.h"
#include "formats/texture_pack.h"

#include <algorithm>

namespace gt3r {

namespace {

struct Preview {
    std::string path;
    std::string summary;
    std::vector<SDL_Texture*> textures;
    std::vector<std::string> labels;
    Bytes head; // first bytes for a hex view
    int current = 0;

    void clear() {
        for (SDL_Texture* t : textures) SDL_DestroyTexture(t);
        textures.clear();
        labels.clear();
        head.clear();
        summary.clear();
        current = 0;
    }
};

bool ends_with(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

class BrowserScreen : public Screen {
public:
    explicit BrowserScreen(std::string open_path) : open_path_(std::move(open_path)) {}
    ~BrowserScreen() override { preview_.clear(); }

    void update(App& app, double) override {
        if (entries_dirty_) refresh(app);
        if (!open_path_.empty()) { // jump straight to a file (--browse)
            std::string path = normalize_vfs_path(open_path_);
            open_path_.clear();
            dir_ = path.substr(0, path.find_last_of('/') + 1);
            refresh(app);
            auto it = std::find(entries_.begin(), entries_.end(), path.substr(dir_.size()));
            if (it != entries_.end()) {
                sel_ = int(it - entries_.begin());
                open(app, path);
            }
        }
        const MenuInput& m = app.menu();
        int n = int(entries_.size());
        if (n > 0) {
            int page = m.pressed(PadButton::L1) ? -kRows : m.pressed(PadButton::R1) ? kRows : 0;
            sel_ = std::clamp(sel_ + m.vertical() + page, 0, n - 1);
        }
        if (!preview_.textures.empty() && m.horizontal())
            preview_.current = (preview_.current + m.horizontal() + int(preview_.textures.size())) %
                               int(preview_.textures.size());
        if (m.pressed(PadButton::Triangle)) {
            if (dir_.empty()) {
                app.audio().stop_music();
                app.pop();
                return;
            }
            std::string prev = dir_;
            dir_.pop_back();
            dir_ = dir_.substr(0, dir_.find_last_of('/') + 1);
            refresh(app);
            std::string name = prev.substr(dir_.size());
            auto it = std::find(entries_.begin(), entries_.end(), name);
            sel_ = it == entries_.end() ? 0 : int(it - entries_.begin());
            return;
        }
        if (m.pressed(PadButton::Cross) && n > 0) {
            const std::string& e = entries_[sel_];
            if (e.back() == '/') {
                dir_ += e;
                sel_ = 0;
                refresh(app);
            } else {
                open(app, dir_ + e);
            }
        }
        if (m.pressed(PadButton::Circle)) app.audio().stop_music();
        if (m.pressed(PadButton::Square) && !preview_.path.empty()) save_textures(app, preview_.path);
    }

    void render(App& app, Ui& ui) override {
        ui.header("ASSET BROWSER", "/" + dir_);
        int n = int(entries_.size());
        int first = ui.list_window(n, sel_, kRows);
        for (int i = 0; i < kRows && first + i < n; ++i) {
            int idx = first + i;
            float y = 26 + i * 12.0f;
            if (idx == sel_) ui.fill(4, y - 2, 250, 12, colors::kHighlight);
            std::string name = entries_[idx];
            if (name.size() > 30) name = name.substr(0, 27) + "...";
            bool is_dir = entries_[idx].back() == '/';
            std::string src = is_dir ? "" : app.vfs()->source_of(dir_ + entries_[idx]);
            Color c = is_dir ? colors::kAccent : (src != "disc" ? colors::kGood : colors::kText);
            ui.text(8, y, name, c);
        }
        if (n > kRows) ui.text_right(250, Ui::kHeight - 30, std::to_string(sel_ + 1) + "/" + std::to_string(n), colors::kDim);

        // Preview pane.
        float px = 262, py = 26, pw = Ui::kWidth - px - 6, ph = Ui::kHeight - 50 - py;
        ui.fill(px, py, pw, ph, colors::kPanel);
        if (!preview_.path.empty()) {
            ui.text(px + 6, py + 6, preview_.path, colors::kAccent);
            ui.text(px + 6, py + 18, preview_.summary, colors::kDim);
        }
        if (!preview_.textures.empty()) {
            SDL_Texture* t = preview_.textures[preview_.current];
            float tw, th;
            SDL_GetTextureSize(t, &tw, &th);
            float box_w = pw - 12, box_h = ph - 52;
            float s = std::min({box_w / tw, box_h / th, 2.0f});
            SDL_FRect dst{px + 6 + (box_w - tw * s) / 2, py + 32 + (box_h - th * s) / 2, tw * s, th * s};
            draw_checker(ui, dst);
            SDL_RenderTexture(ui.renderer(), t, nullptr, &dst);
            ui.text(px + 6, py + ph - 14, preview_.labels[preview_.current], colors::kText);
        } else if (!preview_.head.empty()) {
            for (size_t row = 0; row < 16 && row * 8 < preview_.head.size(); ++row) {
                char line[80];
                int o = std::snprintf(line, sizeof(line), "%04zx ", row * 8);
                for (size_t i = 0; i < 8 && row * 8 + i < preview_.head.size(); ++i)
                    o += std::snprintf(line + o, sizeof(line) - o, "%02x ", preview_.head[row * 8 + i]);
                for (size_t i = 0; i < 8 && row * 8 + i < preview_.head.size(); ++i) {
                    u8 c = preview_.head[row * 8 + i];
                    line[o++] = (c >= 32 && c < 127) ? char(c) : '.';
                }
                line[o] = 0;
                ui.text_mono(px + 6, py + 36 + row * 11.0f, line, colors::kText);
            }
        }
        if (app.audio().music_playing()) {
            double pos = app.audio().music_position(), len = app.audio().music_length();
            ui.bar(px + 6, Ui::kHeight - 40, pw - 12, 6, len > 0 ? float(pos / len) : 0.0f, colors::kAccent);
        }
        ui.footer("Cross: open  Triangle: up  L1/R1: page  Left/Right: texture  Square: save textures  Circle: stop");
    }

private:
    static constexpr int kRows = 25;

    void refresh(App& app) {
        entries_ = app.vfs()->list_dir(dir_);
        entries_dirty_ = false;
        sel_ = std::clamp(sel_, 0, std::max(0, int(entries_.size()) - 1));
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

    // The file's textures into the texture dump folder, named by key.
    void save_textures(App& app, const std::string& path) {
        std::optional<Bytes> data = app.vfs()->read(path);
        if (!data) return;
        const std::string root = app.data_dir() + texture_pack::dump_folder(app.disc() ? app.disc()->info().serial : "");
        texture_pack::Dump dump(root);
        bool ok = true;
        dump.add_file(path, *data, &ok);
        preview_.summary = ok ? "Saved " + std::to_string(dump.textures()) + " texture(s) to " + root + "/" + path
                              : "Could not save to " + root;
    }

    void open(App& app, const std::string& path) {
        preview_.clear();
        preview_.path = path;
        if (ends_with(path, ".ads")) {
            preview_.summary = app.audio().play_music(*app.vfs(), path, false) ? "Playing audio stream"
                                                                                : "Could not play this stream";
            return;
        }
        auto data = app.vfs()->read(path);
        if (!data) {
            preview_.summary = "Could not read file";
            return;
        }
        preview_.head.assign(data->begin(), data->begin() + std::min<size_t>(data->size(), 128));
        char sz[64];
        std::snprintf(sz, sizeof(sz), "%zu bytes, source: %s", data->size(), app.vfs()->source_of(path).c_str());
        preview_.summary = sz;
        try {
            const std::shared_ptr<const texture_pack::Pack> pack = app.vfs()->textures();
            for (size_t off : find_tex1_sets(*data)) {
                ByteSpan sub = ByteSpan(*data).subspan(off);
                Tex1 set = Tex1::parse(sub);
                GsMemory gs;
                set.upload(sub, gs);
                const std::vector<u64> keys = texture_pack::keys_of_set(sub);
                const std::vector<u32> animated = ClutTextures(sub).records();
                for (size_t i = 0; i < set.textures.size(); ++i) {
                    Tex1::Image img = set.decode(gs, i);
                    if (img.width == 0 || img.height == 0 || img.width > 2048 || img.height > 2048) continue;
                    SDL_Texture* t = SDL_CreateTexture(app.renderer(), SDL_PIXELFORMAT_RGBA32,
                                                       SDL_TEXTUREACCESS_STATIC, int(img.width), int(img.height));
                    if (!t) continue;
                    SDL_UpdateTexture(t, nullptr, img.rgba.data(), int(img.width * 4));
                    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                    preview_.textures.push_back(t);
                    const Tex0& t0 = set.textures[i].tex0;
                    char label[160];
                    std::snprintf(label, sizeof(label), "set @%zx tex %zu/%zu  %ux%u %s  key %s", off, i + 1,
                                  set.textures.size(), img.width, img.height, psm_name(t0.psm),
                                  i < keys.size() ? texture_pack::key_name(keys[i]).c_str() : "-");
                    std::string text = label;
                    if (std::binary_search(animated.begin(), animated.end(), u32(i))) text += "  animated";
                    if (pack && i < keys.size() && pack->contains(keys[i])) text += "  (replaced: " + pack->source_of(keys[i]) + ")";
                    preview_.labels.push_back(text);
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("Texture decode failed for %s: %s", path.c_str(), e.what());
        }
        if (!preview_.textures.empty()) preview_.summary += ", " + std::to_string(preview_.textures.size()) + " texture(s)";
    }

    std::string open_path_;
    std::string dir_;
    std::vector<std::string> entries_;
    bool entries_dirty_ = true;
    int sel_ = 0;
    Preview preview_;
};

} // namespace

std::unique_ptr<Screen> make_browser_screen(const std::string& open_path) {
    return std::make_unique<BrowserScreen>(open_path);
}
GT3R_REGISTER_SCREEN(browser, "browser", "Asset browser",
                     "Browse game files; preview textures, play audio, see mod overrides.", 10,
                     [] { return make_browser_screen(); });

} // namespace gt3r
