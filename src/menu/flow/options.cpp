#include "menu/flow/options.h"

#include <algorithm>
#include <cstdlib>

namespace gt3r::menu::flow {

const std::vector<GameOption>& game_options() {
    // Values in the multi items' child order (menu/option.mbl). Defaults: the original's
    // new-game options block is not decoded; these are the values a fresh console shows
    // in the reference captures where known (4:3, sound on, vibration on), M otherwise.
    static const std::vector<GameOption> k = {
        {"wide_mode", "view/view", "multi_wide_mode", {"off", "on"}, 0},
        {"menu_bgm", "sound/sound", "multi_menu_bgm", {"on", "off"}, 0},
        {"menu_se", "sound/sound", "multi_menu_se", {"on", "off"}, 0},
        {"race_bgm", "sound/sound", "multi_race_bgm", {"on", "off"}, 0},
        {"race_se", "sound/sound", "multi_race_se", {"on", "off"}, 0},
        {"replay_bgm", "sound/sound", "multi_replay_bgm", {"on", "off"}, 0},
        {"replay_se", "sound/sound", "multi_replay_se", {"on", "off"}, 0},
        {"vib1", "vibration/vibration", "multi_vibration_1p", {"on", "off"}, 0},
        {"vib2", "vibration/vibration", "multi_vibration_2p", {"on", "off"}, 0},
        {"gear_auto", "race/race", "multi_gear_type", {"on", "off"}, 0},
        {"assist_asm", "race/race", "multi_assist_asm", {"on", "off"}, 0},
        {"assist_tcs", "race/race", "multi_assist_tcs", {"on", "off"}, 0},
        // GT Concept's race page has one assist row instead of the two above: ASM and TCS, TCS only,
        // neither (children assist_0..2). Kept in assist_asm / assist_tcs, so the race reads it the same.
        {"assist", "race/race", "multi_assist", {"0", "1", "2"}, 0},
        {"split_tire_damage", "split/split", "multi_split_tire_damage", {"on", "off"}, 1},
        {"split_boost", "split/split", "multi_split_boost", {"none", "low", "high"}, 1},
        {"ghost", "ghost/ghost", "multi_ghost_type", {"on", "off"}, 0},
        {"assist_1p", "gt_force1/gt_force", "multi_steering_assist_1p", {"type1", "type2", "type3", "type4"}, 0},
        {"force_feedback_1p", "gt_force1/gt_force", "multi_steering_force_1p", {"on", "off"}, 0},
        {"assist_2p", "gt_force2/gt_force", "multi_steering_assist_2p", {"type1", "type2", "type3", "type4"}, 0},
        {"force_feedback_2p", "gt_force2/gt_force", "multi_steering_force_2p", {"on", "off"}, 0},
    };
    return k;
}

const GameOption* find_game_option(const std::string& key) {
    for (const GameOption& o : game_options())
        if (o.key == key) return &o;
    return nullptr;
}

const GameOption* find_game_option_by_item(const std::string& page, const std::string& multi) {
    for (const GameOption& o : game_options())
        if (o.page == page && o.multi == multi) return &o;
    return nullptr;
}

namespace {
bool raw_off(const IniDocument* cfg, const char* key) {
    if (!cfg) return false;
    std::optional<std::string> v = cfg->get("options", key);
    return v && to_lower(trim(*v)) == "off";
}
} // namespace

int option_index(const IniDocument* cfg, const GameOption& o) {
    if (o.key == "assist") return raw_off(cfg, "assist_tcs") ? 2 : raw_off(cfg, "assist_asm") ? 1 : 0;
    if (cfg)
        if (std::optional<std::string> v = cfg->get("options", o.key)) {
            std::string l = to_lower(trim(*v));
            for (size_t i = 0; i < o.values.size(); ++i)
                if (o.values[i] == l) return int(i);
        }
    return o.def;
}

std::string option_value(const IniDocument* cfg, const std::string& key) {
    const GameOption* o = find_game_option(key);
    return o ? o->values[size_t(option_index(cfg, *o))] : std::string();
}

void set_option(IniDocument& cfg, const GameOption& o, int index) {
    index = std::clamp(index, 0, int(o.values.size()) - 1);
    if (o.key == "assist") {
        cfg.set("options", "assist_asm", index == 0 ? "on" : "off");
        cfg.set("options", "assist_tcs", index == 2 ? "off" : "on");
        return;
    }
    cfg.set("options", o.key, o.values[size_t(index)]);
}

bool option_on(const IniDocument* cfg, const std::string& key) { return option_value(cfg, key) == "on"; }

namespace {
int option_int(const IniDocument* cfg, const char* key, int def, int lo, int hi) {
    if (cfg)
        if (std::optional<std::string> v = cfg->get("options", key)) {
            char* end = nullptr;
            long n = std::strtol(v->c_str(), &end, 10);
            if (end != v->c_str()) return std::clamp(int(n), lo, hi);
        }
    return def;
}
} // namespace

int option_laps(const IniDocument* cfg) { return option_int(cfg, "split_laps", 2, 1, 99); }
int option_handicap(const IniDocument* cfg) { return option_int(cfg, "split_handicap", 0, -150, 150) / 5 * 5; }

bool option_preview_visible(const IniDocument* cfg, const std::string& widget_class) {
    std::string c = widget_class, suffix;
    if (c.size() > 3 && (c.compare(c.size() - 3, 3, "_1p") == 0 || c.compare(c.size() - 3, 3, "_2p") == 0)) {
        suffix = c.substr(c.size() - 3);
        c.resize(c.size() - 3);
    }
    bool on;
    if (c.size() > 3 && c.compare(c.size() - 3, 3, "_on") == 0) on = true, c.resize(c.size() - 3);
    else if (c.size() > 4 && c.compare(c.size() - 4, 4, "_off") == 0) on = false, c.resize(c.size() - 4);
    else return true;
    // "<base>_<on|off>": an on/off option in that state.
    if (const GameOption* o = find_game_option(c + suffix)) return option_value(cfg, o->key) == (on ? "on" : "off");
    // "<base>_<value>_<on|off>": the choice's lit art when the option has that value.
    size_t us = c.rfind('_');
    if (us == std::string::npos) return true;
    // (e.g. "assist_type1_on_1p": option assist_1p, value type1).
    std::string base = c.substr(0, us), value = c.substr(us + 1);
    const GameOption* o = find_game_option(base + suffix);
    if (!o) return true;
    return (option_value(cfg, o->key) == value) == on;
}

} // namespace gt3r::menu::flow
