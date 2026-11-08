#include "race/replay_library.h"

#include "base/file.h"
#include "base/log.h"
#include "race/race_panels.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace gt3r::race {

std::string replay_dir(const std::string& save_dir, std::string_view disc_key) {
    std::string d = save_dir;
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d + (disc_key == region_key(Region::America) ? std::string("replays") : "replays/" + std::string(disc_key));
}

std::string replay_dir(const std::string& save_dir, Region region) { return replay_dir(save_dir, region_key(region)); }

int seed_replay_dir(const std::string& save_dir, std::string_view disc_key, Region region) {
    if (disc_key.empty() || disc_key == region_key(region)) return 0;
    const std::string to = replay_dir(save_dir, disc_key), from = replay_dir(save_dir, region);
    if (host_is_directory(to) || !host_is_directory(from) || !host_make_dir(to)) return 0;
    int n = 0;
    for (const ReplayFile& f : list_replays(from))
        if (host_copy_file(f.path, to + "/" + f.name)) ++n;
    if (n) LOG_INFO("Replays: %d copied from %s into %s", n, from.c_str(), to.c_str());
    return n;
}

std::optional<Replay> read_replay_file(const std::string& path, std::string* error) {
    std::optional<Bytes> b = read_host_file(path);
    if (!b) {
        if (error) *error = "cannot read " + path;
        return std::nullopt;
    }
    try {
        return Replay::parse(*b);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

std::vector<ReplayEntry> list_replay_entries(const std::string& dir) {
    std::vector<ReplayEntry> out;
    for (const ReplayFile& f : list_replays(dir)) {
        std::optional<Bytes> b = read_host_file(f.path);
        if (!b) continue;
        try {
            ReplayEntry e;
            e.header = Replay::parse(*b);
            e.path = f.path;
            e.name = f.name;
            e.size = b->size();
            e.ticks = e.header.ticks();
            e.header.inputs.clear(), e.header.inputs.shrink_to_fit();
            e.header.inputs2.clear(), e.header.inputs2.shrink_to_fit();
            e.header.driver_kind.clear(), e.header.driver_kind.shrink_to_fit();
            e.header.reverse.clear(), e.header.reverse.shrink_to_fit();
            e.header.reverse2.clear(), e.header.reverse2.shrink_to_fit();
            e.header.exact.reset(); // (the race record: loaded with the replay)
            out.push_back(std::move(e));
        } catch (const ReplayTooOld&) {
            ReplayEntry e;
            e.path = f.path, e.name = f.name, e.size = b->size(), e.too_old = true;
            out.push_back(std::move(e));
        } catch (const std::exception& ex) {
            LOG_WARN("Replays: %s: %s", f.name.c_str(), ex.what());
        }
    }
    // Newest first: the saved time, else the file name's stamp (list_replays' order).
    std::stable_sort(out.begin(), out.end(), [](const ReplayEntry& a, const ReplayEntry& b) {
        if (a.header.saved && b.header.saved) return a.header.saved > b.header.saved;
        return false;
    });
    return out;
}

bool delete_replay_file(const std::string& path) {
    bool ok = host_remove(path);
    if (!ok) LOG_WARN("Replays: could not delete %s: %s", path.c_str(), SDL_GetError());
    return ok;
}

bool rename_replay_file(const std::string& path, const std::string& comment) {
    std::optional<Replay> r = read_replay_file(path);
    if (!r) return false;
    r->comment = comment;
    return write_host_file(path, r->serialize());
}

std::string replay_date(const ReplayEntry& e) {
    if (e.header.saved > 0) {
        std::time_t t = std::time_t(e.header.saved);
        char buf[16];
        if (std::strftime(buf, sizeof(buf), "%Y/%m/%d", std::localtime(&t))) return buf;
    }
    return record_date(e.name);
}

std::string replay_size_text(u64 bytes) { return std::to_string((bytes + 1023) / 1024) + "KB"; }

std::string replay_time_text(double s) {
    if (!(s >= 0)) return "-:--.---";
    long long ms = std::llround(s * 1000.0);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld:%02lld.%03lld", ms / 60000, (ms / 1000) % 60, ms % 1000);
    return buf;
}

std::string replay_display_name(const ReplayEntry& e) {
    if (!e.header.comment.empty()) return e.header.comment;
    // Unnamed: the save time from the file name's stamp ("2026/10/11 11:33").
    std::string n = e.name;
    if (size_t dot = n.rfind('.'); dot != std::string::npos) n.resize(dot);
    if (std::string d = record_date(e.name); !d.empty() && n.size() >= 15) {
        std::string t = n.substr(n.size() - 6);
        return d + " " + t.substr(0, 2) + ":" + t.substr(2, 2);
    }
    return n;
}

} // namespace gt3r::race
