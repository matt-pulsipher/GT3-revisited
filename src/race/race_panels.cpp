#include "race/race_panels.h"

#include "base/file.h"
#include "base/log.h"
#include "menu/menu_text.h"
#include "race/replay.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace gt3r::race {

namespace {

constexpr float kMenuW = 640, kMenuH = 448;

struct Frame {
    SDL_FRect dst;
    float x(float mx) const { return dst.x + mx * dst.w / kMenuW; }
    float y(float my) const { return dst.y + my * dst.h / kMenuH; }
    float sx() const { return dst.w / kMenuW; }
    float sy() const { return dst.h / kMenuH; }
};

// `game`: `s` is game text (a database name: escapes and JIS pairs, drawn as the game draws them).
constexpr bool kGameText = true;

void text(text::GameFonts& fonts, const Frame& f, float mx, float my, const std::string& s, const char* font,
          float scale, SDL_Color c, text::HAlign a = text::HAlign::Left, text::VAlign v = text::VAlign::Middle,
          bool game = false) {
    text::TextStyle st;
    text::GameFont* g = fonts.resolve(font, st);
    if (!g) return;
    st.scale_x *= scale * f.sx();
    st.scale_y *= scale * f.sy();
    st.color = c;
    st.align = a;
    st.valign = v;
    if (game) g->draw_game(f.x(mx), f.y(my), s, st);
    else g->draw(f.x(mx), f.y(my), s, st);
}

void fill(SDL_Renderer* r, const Frame& f, float x, float y, float w, float h, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect q{f.x(x), f.y(y), w * f.sx(), h * f.sy()};
    SDL_RenderFillRect(r, &q);
}

std::string fmt_time(double s) {
    int ms = int(std::lround(s * 1000.0));
    char b[32];
    std::snprintf(b, sizeof(b), "%d'%02d.%03d", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return b;
}

constexpr SDL_Color kAmber{255, 200, 0, 255}, kGrey{204, 204, 204, 255}, kDim{140, 140, 140, 255};

} // namespace

// --- LapLogger ----------------------------------------------------------------------------

void LapLogger::sample(float dist_m, float speed_kmh, float throttle, float brake, float x, float z) {
    cur_.x.push_back(x);
    cur_.z.push_back(z);
    cur_.dist.push_back(dist_m);
    cur_.speed_kmh.push_back(speed_kmh);
    cur_.throttle.push_back(throttle);
    cur_.brake.push_back(brake);
}

void LapLogger::finish_lap(double time) {
    cur_.time = time;
    if (!cur_.empty()) {
        last_ = cur_;
        if (best_.time < 0 || time < best_.time) best_ = cur_;
    }
    cur_ = {};
}

// --- Records --------------------------------------------------------------------------

std::string record_date(const std::string& file_name) {
    std::string n = file_name;
    size_t dot = n.rfind('.');
    if (dot != std::string::npos) n.resize(dot);
    if (n.size() < 15) return {};
    std::string st = n.substr(n.size() - 15); // YYYYMMDD-HHMMSS
    if (st[8] != '-') return {};
    for (size_t i = 0; i < st.size(); ++i)
        if (i != 8 && !std::isdigit(static_cast<unsigned char>(st[i]))) return {};
    return st.substr(0, 4) + "/" + st.substr(4, 2) + "/" + st.substr(6, 2);
}

std::vector<RecordEntry> course_records(const std::string& dir, const std::string& course, size_t max_rows,
                                        size_t max_files) {
    std::vector<RecordEntry> out;
    size_t read = 0;
    for (const ReplayFile& f : list_replays(dir)) {
        if (f.name.rfind(course + "_", 0) != 0) continue;
        if (++read > max_files) break;
        std::optional<Bytes> b = read_host_file(f.path);
        if (!b) continue;
        try {
            Replay r = Replay::parse(*b);
            if (r.course != course || r.best_lap <= 0) continue;
            out.push_back({r.car, r.best_lap, record_date(f.name)});
        } catch (const std::exception& e) {
            LOG_WARN("Records: %s: %s", f.name.c_str(), e.what());
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const RecordEntry& a, const RecordEntry& b) { return a.lap < b.lap; });
    if (out.size() > max_rows) out.resize(max_rows);
    return out;
}

bool RecordsPanel::load(const Vfs& vfs, SDL_Renderer* r, text::GameFonts& fonts, const std::string& lang) {
    try {
        scene_ = menu::MenuScene::load(vfs, "menu/" + lang + "/records1.pmb");
    } catch (const std::exception& e) {
        LOG_WARN("Records: %s", e.what());
        scene_.reset();
        return false;
    }
    renderer_ = std::make_unique<menu::MenuRenderer>(r);
    renderer_->set_scene(&*scene_);
    if (fonts.loaded()) renderer_->set_text_drawer(menu::make_game_font_drawer(fonts)); // the layout's own text
    page_ = scene_->file().find_page("records_free_tt");
    return page_ >= 0;
}

void RecordsPanel::draw(SDL_Renderer* r, text::GameFonts& fonts, const std::vector<RecordEntry>& rows,
                        const std::vector<std::string>& names, const std::string& course_name, const SDL_FRect& dst) {
    Frame f{dst};
    if (scene_ && page_ >= 0) {
        const menu::Page& p = scene_->file().pages.at(size_t(page_));
        menu::SceneOptions so;
        so.show_items = false;
        so.show_widgets = false;
        so.layer_visible.assign(p.layers.size(), -1); // defaults (the column heads)
        renderer_->draw(page_, so, menu::RenderOptions{false, false, true, true}, dst);
    } else {
        fill(r, f, 0, 0, kMenuW, kMenuH, SDL_Color{20, 20, 20, 255});
    }
    // The list widget (menu_records, 640x356 under the column heads): rows of 28.
    if (!course_name.empty()) text(fonts, f, 320, 56, course_name, "sw_16#1", 0.6f, kDim, text::HAlign::Center);
    if (rows.empty()) {
        text(fonts, f, 320, 200, "No records yet (save a replay after a lap)", "sw_16#1", 0.8f, kGrey,
             text::HAlign::Center);
        return;
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        float y = 108 + 28 * float(i);
        if (i & 1) fill(r, f, 20, y - 13, 600, 26, SDL_Color{255, 255, 255, 18});
        SDL_Color c = i == 0 ? kAmber : kGrey;
        text(fonts, f, 34, y, std::to_string(i + 1), "sw_16#1", 0.8f, c, text::HAlign::Right);
        text(fonts, f, 50, y, i < names.size() ? names[i] : rows[i].car, "sw_16#1", 0.8f, c, text::HAlign::Left,
             text::VAlign::Middle, kGameText);
        text(fonts, f, 470, y, fmt_time(rows[i].lap), "sw_16#1", 0.8f, c, text::HAlign::Right);
        text(fonts, f, 590, y, rows[i].date, "sw_16#1", 0.7f, c, text::HAlign::Right);
    }
}

// --- Option list ----------------------------------------------------------------------

void draw_option_list(SDL_Renderer* r, text::GameFonts& fonts, const std::string& title,
                      const std::vector<OptionRow>& rows, int selected, const std::string& help, const SDL_FRect& dst) {
    Frame f{dst};
    fill(r, f, 0, 0, kMenuW, kMenuH, SDL_Color{12, 12, 14, 255});
    text(fonts, f, 320, 40, title, "sw_20", 1.0f, SDL_Color{255, 120, 0, 255}, text::HAlign::Center);
    float y0 = 110;
    for (size_t i = 0; i < rows.size(); ++i) {
        float y = y0 + 34 * float(i);
        bool sel = int(i) == selected;
        fill(r, f, 120, y - 14, 400, 28, sel ? SDL_Color{255, 120, 0, 70} : SDL_Color{40, 40, 46, 255});
        SDL_Color c = sel ? kAmber : kGrey;
        text(fonts, f, 136, y, rows[i].label, "sw_16#1", 0.75f, c);
        text(fonts, f, 504, y, sel ? "< " + rows[i].value + " >" : rows[i].value, "sw_16#1", 0.75f, c,
             text::HAlign::Right);
    }
    if (!help.empty()) text(fonts, f, 320, 410, help, "sw_16#1", 0.55f, kDim, text::HAlign::Center);
}

} // namespace gt3r::race
