// GT-mode menu bindings: the session (GtModeSession: the disc data and the game being played),
// the save library, the snapshot a save page saves, and the saves the race screens write before
// the menus record a run from its GameResult (a copy of the game with the run recorded).

#include "game/gt_mode_menu/internal.h"

#include "base/file.h"
#include "base/log.h"
#include "game/gt_save.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstdlib>

namespace gt3r::game {

using namespace gt_menu;

GtModeSession::GtModeSession(std::shared_ptr<const GtData> data, std::string path)
    : gt_(std::move(data)), save_path_(std::move(path)) {}

bool GtModeSession::save(std::string* error) { return write_save(save_path_, gt_.state(), error); }

bool GtModeSession::load(std::string* error) {
    std::string err;
    std::optional<GameState> s = read_save(save_path_, &err);
    if (error) *error = err;
    if (!s) {
        // A save that is there but cannot be read (damaged, or from a newer version) is set
        // aside before the new game that starts in its place autosaves over it.
        if (saving_enabled() && host_path_exists(save_path_)) {
            std::string why;
            if (std::string kept = set_aside_save(save_path_, &why); !kept.empty())
                LOG_WARN("gt mode: cannot read %s (%s); kept as %s", save_path_.c_str(), err.c_str(), kept.c_str());
            else
                LOG_WARN("gt mode: cannot read %s (%s) nor keep it: %s", save_path_.c_str(), err.c_str(), why.c_str());
        }
        return false;
    }
    gt_.state() = std::move(*s);
    return true;
}

GtModeSession* gt_mode_session() { return S(); }

bool gt_menu::start_session(const GtData::Reader& read, std::string* data_error, bool* loaded) {
    MenuState& ms = menu_state();
    if (!ms.data) {
        auto d = std::make_shared<GtData>();
        if (!d->load(read)) {
            if (data_error) *data_error = d->error();
            return false;
        }
        ms.data = d;
    }
    ms.session = std::make_unique<GtModeSession>(ms.data, current_save_path());
    std::string err;
    const bool ok = ms.session->load(&err);
    if (!ok) LOG_INFO("gt mode: new game (%s)", err.c_str());
    if (loaded) *loaded = ok;
    return true;
}

bool ensure_gt_mode_session(const std::function<std::optional<Bytes>(const std::string&)>& read) {
    MenuState& ms = menu_state();
    if (!ms.reader) ms.reader = read;
    if (ms.session) return true;
    return start_session(read, nullptr);
}

SaveLibrary& save_library() {
    MenuState& ms = menu_state();
    // The disc region's library; made again when GT3R_GTMODE_SAVE changes (tests).
    const char* env = std::getenv("GT3R_GTMODE_SAVE");
    const Region region = region_data(ms.reader).region;
    const std::string want = std::string(region_key(region)) + "|" + (env ? env : "");
    if (ms.library && want != ms.library_env) ms.library.reset();
    ms.library_env = want;
    if (!ms.library) {
        if (const char* p = env; p && *p) {
            std::string dir = p;
            size_t sl = dir.find_last_of("/\\");
            dir = sl == std::string::npos ? std::string(".") : dir.substr(0, sl);
            ms.library = std::make_unique<SaveLibrary>(dir, false);
        } else {
            ms.library = std::make_unique<SaveLibrary>(SaveLibrary::user_dir(region));
            if (saving_enabled() && region == Region::America) // earlier versions played NTSC-U data only
                if (int n = ms.library->migrate(SaveLibrary::legacy_path()); n > 0)
                    LOG_INFO("gt mode: %d save(s) copied into %s", n, ms.library->dir().c_str());
            // Versions up to 0.3 kept a PAL disc's games in saves/ too (on the NTSC-U data set,
            // which the PAL set extends): the PAL library starts with copies of them, once.
            if (saving_enabled() && region == Region::Europe)
                if (int n = ms.library->copy_library(SaveLibrary::user_dir(Region::America)); n > 0)
                    LOG_INFO("gt mode: %d save(s) copied into %s", n, ms.library->dir().c_str());
        }
    }
    return *ms.library;
}

std::string current_save_path() {
    if (const char* p = std::getenv("GT3R_GTMODE_SAVE"); p && *p) return p;
    SaveLibrary& lib = save_library();
    if (std::string c = lib.current(); !c.empty()) return c;
    for (const SaveInfo& f : lib.list())
        if (f.state) return f.path;
    // Nothing in the library yet: an earlier version's save when it was not migrated
    // (writes disabled), else the new game's file.
    if (!saving_enabled() && region_data(menu_state().reader).region == Region::America &&
        host_path_exists(SaveLibrary::legacy_path()))
        return SaveLibrary::legacy_path();
    return lib.dir() + "gtmode.sav";
}

void begin_snapshot_save(GameState s) { menu_state().snapshot = std::move(s); }

void end_race_game_save() { menu_state().snapshot.reset(); }

const GameState* snapshot_save() {
    const std::optional<GameState>& snapshot = menu_state().snapshot;
    return snapshot ? &*snapshot : nullptr;
}

void save_race_run(const GameResult& r) {
    GtModeSession* s = S();
    if (!s || !saving_enabled() || r.mode != "go_race" || !r.finished || r.position <= 0) return;
    if (s->gt().state().championship.active()) return;
    GtMode copy(s->gt().data_ptr());
    copy.state() = s->gt().state();
    copy.record_race(r.event.empty() ? r.race_code : r.event, r.position - 1);
    std::string err;
    if (!write_save(s->save_path(), copy.state(), &err)) LOG_WARN("gt mode: save failed: %s", err.c_str());
    else LOG_INFO("gt mode: race %s place %d saved", r.race_code.c_str(), r.position);
}

bool begin_race_game_save(const GameResult& r) {
    GtModeSession* s = S();
    if (!s) return false;
    GtMode copy(s->gt().data_ptr());
    copy.state() = s->gt().state();
    // What on_result() will record for the round: the engine condition, the car's dirt, the place.
    apply_condition(copy, r);
    if (r.car_dirt >= 0)
        if (GarageCar* ride = copy.state().ride_car()) ride->cond.dirt = float(std::clamp(r.car_dirt, 0.0, 0.8));
    if (r.mode == "go_race" && copy.state().championship.active() && r.started) {
        std::vector<int> order;
        for (int o : r.order) order.push_back(o + 1);
        copy.record_round(r.finished && r.position > 0 ? r.position - 1 : -1, order);
    }
    begin_snapshot_save(std::move(copy.state()));
    return true;
}

void save_license_run(const std::string& code, int medal, double seconds) {
    GtModeSession* s = S();
    if (!s || medal < 0 || !saving_enabled()) return;
    // A copy: the live state is recorded when the result reaches the menus.
    GtMode copy(s->gt().data_ptr());
    copy.state() = s->gt().state();
    copy.record_license_medal(code, Medal(std::min(medal, 2)), seconds);
    std::string err;
    if (!write_save(s->save_path(), copy.state(), &err)) LOG_WARN("gt mode: save failed: %s", err.c_str());
    else LOG_INFO("gt mode: license %s %s saved", code.c_str(), medal_name(Medal(std::min(medal, 2))));
}

bool begin_records(const std::string& key) {
    GtModeSession* s = S();
    if (!s) return false;
    s->records_key = key;
    s->records_cursor = 0; // (the first place)
    return true;
}

bool begin_setting(const std::string& event) {
    GtModeSession* s = S();
    if (!s || !s->gt().state().ride_car()) return false;
    s->setting_work = *s->gt().state().ride_car();
    s->setting_event = event;
    s->setting_entry.clear();
    s->setting_committed = false;
    return true;
}

} // namespace gt3r::game
