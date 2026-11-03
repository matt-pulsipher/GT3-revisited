#include "race/replay_options.h"

#include "base/config.h"
#include "base/log.h"
#include "menu/flow/menu_controller.h"
#include "menu/menu_text.h"
#include "text/game_font.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace gt3r::race {

namespace {

using menu::flow::key_bit;
using menu::flow::MenuKey;

// Per view mode: allowed targets (bit per target), as in the game (presentation §G).
constexpr int kTargetMask[4] = {1, 4, 7, 1};
constexpr int kDefaultTarget[4] = {0, 2, 2, 0};

const menu::Element* find_element(const menu::MenuScene& s, const std::string& name) {
    if (s.file().pages.empty()) return nullptr;
    for (const menu::Layer& l : s.file().pages[0].layers) {
        for (const menu::Element& e : l.elements)
            if (e.name == name) return &e;
        for (const menu::Element& e : l.widgets)
            if (e.name == name) return &e;
    }
    return nullptr;
}

const char* const kModeNames[4] = {"mode_traditional", "mode_avantgarde", "mode_broadcast", "mode_crs"};
const char* const kTargetNames[3] = {"target_manual", "target_top", "target_auto"};
const char* const kItem1Names[3] = {"item_easymeter", "item_carname", "item_time"};
const char* const kItem2Names[4] = {"item_map", "item_steering", "item_tire", "item_accel"};
const char* const kSoundNames[2] = {"sound_bgm", "sound_se"};

u8 item_bit(int row, int col) {
    return u8(1u << unsigned(row == ReplayOptions::RowItem1 ? col : col + 3));
}

} // namespace

bool ReplaySettings::target_allowed(int mode, int target) {
    return mode >= 0 && mode < 4 && target >= 0 && target < 3 && (kTargetMask[mode] >> target) & 1;
}

int ReplaySettings::default_target(int mode) { return mode >= 0 && mode < 4 ? kDefaultTarget[mode] : 0; }

int ReplaySettings::current_target() const {
    int m = std::clamp(mode, 0, 3);
    return target_allowed(m, target[size_t(m)]) ? target[size_t(m)] : default_target(m);
}

AttractView pick_attract_view(float r, int& since_avant_garde) {
    AttractView v;
    v.mode = r < 0.33f ? 0 : r < 0.66f ? 2 : 1;
    if (since_avant_garde + 1 >= 4) v.mode = 1;
    if (v.mode == 2) v.target = int(r * 100.0f) % 3;
    if (v.mode == 1) v.target = 2, since_avant_garde = 0;
    ++since_avant_garde;
    return v;
}

ReplaySettings ReplaySettings::load(const IniDocument& cfg) {
    ReplaySettings s;
    auto get = [&](const char* k) { return cfg.get("options", k); };
    if (auto v = get("replay_mode")) s.mode = std::clamp(std::atoi(v->c_str()), 0, 3);
    if (auto v = get("replay_targets")) {
        std::vector<std::string_view> parts = gt3r::split(*v, ',');
        for (size_t i = 0; i < parts.size() && i < 4; ++i)
            s.target[i] = std::clamp(std::atoi(std::string(trim(parts[i])).c_str()), 0, 2);
    }
    if (auto v = get("replay_items")) s.items = u8(std::clamp(std::atoi(v->c_str()), 0, 127));
    if (auto v = get("replay_bgm")) s.bgm = to_lower(*v) != "off";
    if (auto v = get("replay_se")) s.se = to_lower(*v) != "off";
    if (auto v = get("replay_screen")) s.split = to_lower(*v) != "single";
    if (auto v = get("replay_level")) s.level = std::clamp(float(std::atof(v->c_str())), 0.0f, 1.0f);
    if (auto v = get("replay_music_cuts")) s.music_cuts = to_lower(*v) == "on" ? 1.0f : 0.0f;
    return s;
}

void ReplaySettings::save(IniDocument& cfg) const {
    cfg.set("options", "replay_mode", std::to_string(mode));
    cfg.set("options", "replay_targets", std::to_string(target[0]) + "," + std::to_string(target[1]) + "," +
                                             std::to_string(target[2]) + "," + std::to_string(target[3]));
    cfg.set("options", "replay_items", std::to_string(items));
    cfg.set("options", "replay_bgm", bgm ? "on" : "off");
    cfg.set("options", "replay_se", se ? "on" : "off");
    cfg.set("options", "replay_screen", split ? "split" : "single");
    char lv[16];
    std::snprintf(lv, sizeof(lv), "%.1f", double(level));
    cfg.set("options", "replay_level", lv);
    cfg.set("options", "replay_music_cuts", music_cuts >= 0.5f ? "on" : "off");
}

void ReplayOptions::open(const ReplaySettings& s, bool two_player) {
    s_ = s;
    two_player_ = two_player;
    changed_ = false;
    row_ = 0;
    col_ = {};
    col_[RowMode] = std::clamp(s_.mode, 0, 3);
    col_[RowTarget] = s_.current_target();
    col_[RowScreen] = s_.split ? 1 : 0;
    held_ = 0;
    ignore_ = 0xFFFF;
    repeat_ = 0;
    frames_ = 0;
    sounds_.clear();
}

std::vector<std::string> ReplayOptions::take_sounds() { return std::exchange(sounds_, {}); }

int ReplayOptions::cell(int row, int col) const {
    switch (row) {
    case RowMode: return col == s_.mode ? 1 : 0;
    case RowTarget:
        if (!ReplaySettings::target_allowed(s_.mode, col)) return 2;
        return col == s_.current_target() ? 1 : 0;
    case RowItem1:
    case RowItem2: return (s_.items & item_bit(row, col)) ? 1 : 0;
    case RowSound:
        if (col == 0 && ReplaySettings::bgm_forced(s_.mode)) return 2;
        return (col == 0 ? s_.bgm : s_.se) ? 1 : 0;
    case RowScreen: return (col == 1) == s_.split ? 1 : 0;
    }
    return 0;
}

bool ReplayOptions::tick(u16 held) {
    ++frames_;
    ignore_ &= held;
    held &= u16(~ignore_);
    const u16 pressed = held & u16(~held_);
    // Directions repeat (the press, 30 frames later, then every 3 frames).
    const u16 dirs = key_bit(MenuKey::Up) | key_bit(MenuKey::Down) | key_bit(MenuKey::Left) | key_bit(MenuKey::Right);
    u16 fired = pressed;
    if (held & dirs) {
        if (pressed & dirs) repeat_ = 0;
        else if (++repeat_ >= 30 && (repeat_ - 30) % 3 == 0) fired |= held & dirs;
    }
    held_ = held;
    auto key = [&](MenuKey k) { return (fired & key_bit(k)) != 0; };
    if (key(MenuKey::Cancel)) {
        sounds_.push_back("cancel");
        return false;
    }
    if (key(MenuKey::Up) || key(MenuKey::Down)) {
        int r = std::clamp(row_ + (key(MenuKey::Down) ? 1 : -1), 0, rows() - 1);
        if (r != row_) {
            // Between the item rows the cursor keeps its column (3 vs 4 cells).
            if ((row_ == RowItem1 && r == RowItem2) || (row_ == RowItem2 && r == RowItem1))
                col_[size_t(r)] = std::min(col_[size_t(row_)], columns(r) - 1);
            row_ = r;
            sounds_.push_back("cursor");
        }
        return true;
    }
    if (key(MenuKey::Left) || key(MenuKey::Right)) {
        const int d = key(MenuKey::Right) ? 1 : -1;
        int c = col_[size_t(row_)];
        int n = c + d;
        // TARGET skips the targets the mode does not allow; SOUND skips a forced BGM.
        while (n >= 0 && n < columns(row_) && cell(row_, n) == 2) n += d;
        if (n >= 0 && n < columns(row_) && n != c) {
            col_[size_t(row_)] = n;
            sounds_.push_back("cursor");
        }
        return true;
    }
    if (key(MenuKey::Confirm)) apply();
    return true;
}

void ReplayOptions::apply() {
    const int c = col_[size_t(row_)];
    if (cell(row_, c) == 2) {
        sounds_.push_back("disable");
        return;
    }
    switch (row_) {
    case RowMode: // (the avant-garde mode stores BGM on)
        s_.mode = c;
        if (ReplaySettings::bgm_forced(c)) s_.bgm = true;
        col_[RowTarget] = s_.current_target();
        break;
    case RowTarget: s_.target[size_t(s_.mode)] = c; break;
    case RowItem1:
    case RowItem2: s_.items ^= item_bit(row_, c); break;
    case RowSound:
        if (c == 0) s_.bgm = !s_.bgm;
        else s_.se = !s_.se;
        break;
    case RowScreen: s_.split = c == 1; break;
    }
    changed_ = true;
    sounds_.push_back("ok");
}

bool ReplayOptions::cell_at(float x, float y, bool vertical_split, int& row, int& col) const {
    const std::optional<menu::MenuScene>& sc = scenes_[two_player_ ? 1 : 0];
    if (!sc) return false;
    auto hit = [&](const char* name, int r, int c) {
        const menu::Element* e = find_element(*sc, name);
        if (!e || x < e->x || x > e->x + e->w || y < e->y || y > e->y + e->h) return false;
        row = r, col = c;
        return true;
    };
    for (int c = 0; c < 4; ++c)
        if (hit(kModeNames[c], RowMode, c)) return true;
    for (int c = 0; c < 3; ++c)
        if (hit(kTargetNames[c], RowTarget, c)) return true;
    for (int c = 0; c < 3; ++c)
        if (hit(kItem1Names[c], RowItem1, c)) return true;
    for (int c = 0; c < 4; ++c)
        if (hit(kItem2Names[c], RowItem2, c)) return true;
    for (int c = 0; c < 2; ++c)
        if (hit(kSoundNames[c], RowSound, c)) return true;
    return two_player_ && (hit("screen_single", RowScreen, 0) ||
                           hit(vertical_split ? "screen_v_split" : "screen_h_split", RowScreen, 1));
}

void ReplayOptions::hover(float x, float y, bool vertical_split) {
    int r = 0, c = 0;
    if (!cell_at(x, y, vertical_split, r, c) || cell(r, c) == 2 || (r == row_ && c == col_[size_t(r)])) return;
    row_ = r, col_[size_t(r)] = c;
    sounds_.push_back("cursor");
}

void ReplayOptions::tap(float x, float y, bool vertical_split) {
    int r = 0, c = 0;
    if (!cell_at(x, y, vertical_split, r, c) || cell(r, c) == 2) return;
    if (r == row_ && c == col_[size_t(r)]) apply();
    else hover(x, y, vertical_split);
}

bool ReplayOptions::load(const Vfs& vfs, SDL_Renderer* r, text::GameFonts& fonts, const std::string& lang) {
    bool any = false;
    for (int i = 0; i < 2; ++i) {
        std::string f = "menu/" + lang + "/replay_panel" + std::to_string(i + 1) + ".pmb";
        try {
            scenes_[i] = menu::MenuScene::load(vfs, f);
            any = true;
        } catch (const std::exception& e) {
            LOG_WARN("Replay options %s: %s", f.c_str(), e.what());
            scenes_[i].reset();
        }
    }
    renderer_ = std::make_unique<menu::MenuRenderer>(r);
    if (fonts.loaded()) renderer_->set_text_drawer(menu::make_game_font_drawer(fonts));
    return any;
}

void ReplayOptions::draw(const SDL_FRect& dst, bool vertical_split) {
    const std::optional<menu::MenuScene>& sc = scenes_[two_player_ ? 1 : 0];
    if (!sc || !renderer_ || sc->file().pages.empty()) return;
    renderer_->set_scene(&*sc);
    std::vector<menu::ElementDraw> list;
    for (const menu::Layer& l : sc->file().pages[0].layers)
        if (l.name == "defaultLayer")
            for (const menu::Element& e : l.elements) {
                menu::ElementDraw d;
                d.element = &e;
                list.push_back(d);
            }
    // The page's own parts in their z order (the backgrounds under the labels).
    std::stable_sort(list.begin(), list.end(),
                     [](const menu::ElementDraw& a, const menu::ElementDraw& b) { return a.element->z < b.element->z; });
    auto colour = [&](const char* name) {
        SDL_FColor c{1, 1, 1, 1};
        if (const menu::Element* e = find_element(*sc, name); e && e->colors) {
            const auto& v = (*e->colors)[0];
            c = {float(v.r) / 255.0f, float(v.g) / 255.0f, float(v.b) / 255.0f, 1};
        }
        return c;
    };
    const SDL_FColor on = colour("color_on"), off = colour("color_off"), dis = colour("color_disable");
    const float pulse = 0.36f * (0.6f + 0.4f * (1.0f - float(frames_ % 31) / 30.0f));
    auto put = [&](const std::string& name, int row, int col) {
        const menu::Element* e = find_element(*sc, name);
        if (!e) return;
        int st = cell(row, col);
        SDL_FColor t = st == 1 ? on : st == 2 ? dis : off;
        if (row == row_ && col == col_[size_t(row)]) // the cursor: lightened
            t = {std::min(1.0f, t.r + pulse), std::min(1.0f, t.g + pulse), std::min(1.0f, t.b + pulse), 1};
        menu::ElementDraw d;
        d.element = e;
        d.tint = t;
        list.push_back(d);
    };
    for (int c = 0; c < 4; ++c) put(kModeNames[c], RowMode, c);
    for (int c = 0; c < 3; ++c) put(kTargetNames[c], RowTarget, c);
    for (int c = 0; c < 3; ++c) put(kItem1Names[c], RowItem1, c);
    for (int c = 0; c < 4; ++c) put(kItem2Names[c], RowItem2, c);
    for (int c = 0; c < 2; ++c) put(kSoundNames[c], RowSound, c);
    if (two_player_) {
        put("screen_single", RowScreen, 0);
        put(vertical_split ? "screen_v_split" : "screen_h_split", RowScreen, 1);
    }
    menu::RenderOptions ro{false, false, true, true};
    renderer_->draw_elements(list, ro, dst);
}

} // namespace gt3r::race
