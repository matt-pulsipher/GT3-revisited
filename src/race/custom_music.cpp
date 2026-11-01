#include "race/custom_music.h"

#include "audio/bgm.h"
#include "audio/music_file.h"
#include "base/file.h"
#include "base/log.h"

#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <random>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace gt3r::race {

namespace fs = std::filesystem;

namespace {

fs::path fs_path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

bool hidden(const fs::path& p) {
    const std::string n = utf8(p.filename());
    return !n.empty() && n[0] == '.';
}

bool less_folded(const std::string& a, const std::string& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
}

} // namespace

MusicSource parse_music_source(std::string_view s) {
    if (s == "custom") return MusicSource::Custom;
    if (s == "both") return MusicSource::Both;
    return MusicSource::Original;
}

std::string_view music_source_name(MusicSource s) {
    switch (s) {
    case MusicSource::Custom: return "custom";
    case MusicSource::Both: return "both";
    case MusicSource::Original: break;
    }
    return "original";
}

u64 custom_song_id(std::string_view key, u64 size) {
    u64 h = 0xCBF29CE484222325ull;
    auto add = [&h](char c) { h = (h ^ u8(c)) * 0x100000001B3ull; };
    for (char c : key) add(char(std::tolower(static_cast<unsigned char>(c))));
    add('\0');
    for (char c : std::to_string(size)) add(c);
    return h;
}

std::string song_label(const std::string& title, const std::string& artist) {
    return artist.empty() ? title : title + " / " + artist;
}

std::string menu_ascii(std::string_view in) {
    // U+00C0..U+00FF and U+0100..U+017F (Latin-1 and Latin Extended-A) without their accents.
    static const char* const kLatin1[64] = {"A", "A", "A",  "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
                                            "D", "N", "O",  "O", "O", "O", "O",  "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
                                            "a", "a", "a",  "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
                                            "d", "n", "o",  "o", "o", "o", "o",  "/", "o", "u", "u", "u", "u", "y", "th", "y"};
    static const char kExtA[] = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOo"
                                "OoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";
    std::string out;
    for (size_t i = 0; i < in.size();) {
        const u8 c = u8(in[i]);
        if (c < 0x80) {
            out += char(c), ++i;
            continue;
        }
        const size_t n = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        u32 cp = n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 0x07;
        bool ok = n > 1 && i + n <= in.size();
        for (size_t k = 1; ok && k < n; ++k) {
            ok = (u8(in[i + k]) & 0xC0) == 0x80;
            cp = cp << 6 | (u8(in[i + k]) & 0x3F);
        }
        i += ok ? n : 1;
        if (!ok) out += '?';
        else if (cp >= 0xC0 && cp <= 0xFF) out += kLatin1[cp - 0xC0];
        else if (cp >= 0x100 && cp <= 0x17F) out += kExtA[cp - 0x100];
        else if (cp == 0x2018 || cp == 0x2019 || cp == 0xB4) out += '\'';
        else if (cp == 0x201C || cp == 0x201D) out += '"';
        else if (cp == 0x2013 || cp == 0x2014) out += '-';
        else if (cp == 0x2026) out += "...";
        else if (cp == 0xA0) out += ' ';
        else out += '?';
    }
    return out;
}

bool pick_custom(bool shuffle, size_t stock_left, size_t custom_left, bool stock_listed, bool custom_listed,
                 bool last_custom, double unit) {
    if (!stock_listed || !custom_listed) return custom_listed;
    if (!shuffle) return !last_custom;
    const size_t total = stock_left + custom_left;
    if (total == 0) return !last_custom;
    return unit * double(total) < double(custom_left);
}

MusicLibrary::~MusicLibrary() {
    join();
    stop_measuring();
}

void MusicLibrary::join() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    stop_ = false;
}

void MusicLibrary::cancel() {
    join();
    stop_measuring();
    take_measured();
    if (list_dirty_) save_song_list();
    std::lock_guard lock(mutex_);
    result_.reset();
    scanning_ = again_ = false;
}

bool same_folder(const std::string& a, const std::string& b) {
    std::error_code ec;
    return !a.empty() && !b.empty() && fs::equivalent(fs_path(a), fs_path(b), ec) && !ec;
}

bool MusicLibrary::set_folder(std::string folder) {
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') folder += '/';
    if (folder == folder_ && (scanning_ || generation_ > 0)) return false;
    folder_ = std::move(folder);
    // The first folder: its songs as the last run left them, until this run's scan lands.
    read_song_list();
    if (!std::exchange(list_tried_, true) && remembered_ && !folder_.empty() && remembered_->folder == folder_ &&
        !remembered_->songs.empty()) {
        ScanInfo info;
        info.found = true;
        take(std::move(remembered_->songs), info, true);
        from_list_ = true;
    }
    start_scan();
    return true;
}

void MusicLibrary::rescan() { start_scan(); }

void MusicLibrary::start_scan() {
    // A scan under way is told to stop and the new one starts when it has (poll): the game's
    // thread never waits on a slow folder (a network share).
    if (scanning_) {
        again_ = true;
        stop_ = true;
        return;
    }
    join();
    {
        std::lock_guard lock(mutex_);
        result_.reset();
    }
    stop_ = false;
    scanning_ = true;
    const std::string folder = folder_;
    thread_ = std::thread([this, folder] {
        ScanInfo info;
        std::vector<CustomSong> songs = scan(folder, info, &stop_);
        std::lock_guard lock(mutex_);
        result_ = std::move(songs);
        result_info_ = info;
    });
}

bool MusicLibrary::poll() {
    bool changed = std::exchange(from_list_, false);
    take_measured();
    if (!scanning_) return changed;
    std::vector<CustomSong> songs;
    ScanInfo info;
    {
        std::lock_guard lock(mutex_);
        if (!result_) return changed;
        songs = std::move(*result_);
        result_.reset();
        info = result_info_;
    }
    if (thread_.joinable()) thread_.join(); // (it has finished)
    scanning_ = false;
    if (std::exchange(again_, false)) { // asked for again meanwhile: that scan's result instead
        start_scan();
        return changed;
    }
    return take(std::move(songs), info, false) || changed;
}

bool MusicLibrary::take(std::vector<CustomSong> songs, const ScanInfo& info, bool remembered) {
    info_ = info;
    // What was measured of a song stays with it (the same file: id and path).
    if (!remembered) {
        std::unordered_map<u64, const CustomSong*> before;
        for (const CustomSong& s : songs_) before.emplace(s.id, &s);
        for (CustomSong& s : songs)
            if (auto it = before.find(s.id); it != before.end() && it->second->path == s.path && it->second->measured)
                s.measured = true, s.lufs = it->second->lufs, s.peak = it->second->peak;
    }
    const bool changed = songs.size() != songs_.size() ||
                         !std::equal(songs.begin(), songs.end(), songs_.begin(), [](const CustomSong& a, const CustomSong& b) {
                             return a.id == b.id && a.title == b.title && a.artist == b.artist && a.path == b.path;
                         });
    songs_ = std::move(songs);
    std::vector<u64> ids;
    ids.reserve(songs_.size());
    for (const CustomSong& s : songs_) ids.push_back(s.id);
    bool save = false;
    if (!playlist_loaded_) {
        playlist_loaded_ = true;
        std::optional<RacePlaylist> stored;
        if (!playlist_file_.empty())
            if (std::optional<Bytes> b = read_host_file(playlist_file_))
                stored = RacePlaylist::from_text(std::string(b->begin(), b->end()), RacePlaylist::kMaxCustomSongs);
        if (stored) {
            playlist_ = std::move(*stored);
        } else {
            // A new list as the disc's starts (shuffle on), on a seed of its own: the game's
            // shared generator is the original's and stays untouched.
            playlist_ = RacePlaylist::with_songs({}, u16(std::random_device{}() & 0x7FFF), RacePlaylist::kMaxCustomSongs);
            save = true;
        }
    }
    // A folder that can't be read (a drive unplugged) leaves the playlist as it is: its songs
    // keep their places and marks for when it is back.
    if (info_.found) save |= playlist_.sync(ids);
    if (save && info_.found) save_playlist();
    if (changed) {
        ++generation_;
        if (!remembered)
            LOG_INFO("Music folder %s: %s%zu songs%s, %zu files in formats that don't play, %zu that don't open",
                     folder_.c_str(), info_.found ? "" : "not found, ", songs_.size(), info_.capped ? " (the most kept)" : "",
                     info_.unplayable, info_.broken);
    }
    // A scan of a folder that could be read: remembered for the next start.
    if (!remembered && info_.found && (changed || list_dirty_ || !remembered_ || remembered_->folder != folder_))
        save_song_list();
    return changed;
}

const CustomSong* MusicLibrary::find(u64 id) const {
    for (const CustomSong& s : songs_)
        if (s.id == id) return &s;
    return nullptr;
}

const CustomSong* MusicLibrary::find_path(const std::string& path) const {
    for (const CustomSong& s : songs_)
        if (s.path == path) return &s;
    return nullptr;
}

void MusicLibrary::save_playlist() const {
    if (playlist_file_.empty()) return;
    const std::string text = playlist_.to_text() + "\n";
    if (!write_host_file(playlist_file_, ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size())))
        LOG_WARN("Could not save %s", playlist_file_.c_str());
}

// The song list file: a header line, the folder, the disc's level, then a song a line. Fields
// are tab-separated, with \\, \t, \n and \r escaped. A song's loudness is "-" until measured
// and "x" when it has none.
//   GT3R music songs 1
//   folder <TAB> <folder>
//   disc <TAB> <key> <TAB> <lufs or ->
//   <id, hex> <TAB> <lufs> <TAB> <peak> <TAB> <path> <TAB> <rel> <TAB> <title> <TAB> <artist>
namespace {

constexpr std::string_view kSongListHeader = "GT3R music songs 1";

void put_field(std::string& out, std::string_view s) {
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default: out += c; break;
        }
    }
}

std::string field_text(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out += s[i];
            continue;
        }
        const char c = s[++i];
        out += c == 't' ? '\t' : c == 'n' ? '\n' : c == 'r' ? '\r' : c;
    }
    return out;
}

std::vector<std::string_view> split_tabs(std::string_view line) {
    std::vector<std::string_view> out;
    for (size_t at = 0;;) {
        const size_t tab = line.find('\t', at);
        out.push_back(line.substr(at, tab == std::string_view::npos ? std::string_view::npos : tab - at));
        if (tab == std::string_view::npos) return out;
        at = tab + 1;
    }
}

std::optional<float> number(std::string_view s) {
    const std::string t(s);
    char* end = nullptr;
    const float v = std::strtof(t.c_str(), &end);
    if (t.empty() || end != t.c_str() + t.size() || !std::isfinite(v)) return std::nullopt;
    return v;
}

} // namespace

std::string MusicLibrary::song_list_text(const std::string& folder, const std::vector<CustomSong>& songs,
                                         const std::string& disc_key, std::optional<float> disc_lufs) {
    std::string out(kSongListHeader);
    out += "\nfolder\t";
    put_field(out, folder);
    out += "\ndisc\t";
    put_field(out, disc_key);
    char num[64];
    if (disc_lufs) std::snprintf(num, sizeof(num), "\t%.2f\n", double(*disc_lufs));
    out += disc_lufs ? num : "\t-\n";
    for (const CustomSong& s : songs) {
        std::snprintf(num, sizeof(num), "%016llx\t", static_cast<unsigned long long>(s.id));
        out += num;
        if (!s.measured) out += "-\t-\t";
        else if (s.peak <= 0) out += "x\tx\t";
        else std::snprintf(num, sizeof(num), "%.2f\t%.4f\t", double(s.lufs), double(s.peak)), out += num;
        put_field(out, s.path);
        out += '\t';
        put_field(out, s.rel);
        out += '\t';
        put_field(out, s.title);
        out += '\t';
        put_field(out, s.artist);
        out += '\n';
    }
    return out;
}

std::optional<MusicLibrary::SongList> MusicLibrary::parse_song_list(std::string_view text) {
    SongList list;
    bool header = false;
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!header) {
            if (line != kSongListHeader) return std::nullopt;
            header = true;
            continue;
        }
        const std::vector<std::string_view> f = split_tabs(line);
        if (f.size() == 2 && f[0] == "folder") {
            list.folder = field_text(f[1]);
        } else if (f.size() == 3 && f[0] == "disc") {
            list.disc_key = field_text(f[1]);
            list.disc_lufs = number(f[2]);
        } else if (f.size() == 7 && f[0].size() == 16 && list.songs.size() < RacePlaylist::kMaxCustomSongs) {
            CustomSong s;
            const std::string id(f[0]);
            char* id_end = nullptr;
            s.id = std::strtoull(id.c_str(), &id_end, 16);
            if (id_end != id.c_str() + id.size()) continue;
            if (f[1] == "x") {
                s.measured = true;
            } else if (std::optional<float> lufs = number(f[1]), peak = number(f[2]); lufs && peak && *peak > 0) {
                s.measured = true, s.lufs = *lufs, s.peak = *peak;
            }
            s.path = field_text(f[3]);
            s.rel = field_text(f[4]);
            s.title = field_text(f[5]);
            s.artist = field_text(f[6]);
            if (!s.path.empty()) list.songs.push_back(std::move(s));
        }
    }
    if (!header) return std::nullopt;
    return list;
}

void MusicLibrary::read_song_list() {
    if (std::exchange(list_read_, true) || song_list_file_.empty()) return;
    if (std::optional<Bytes> b = read_host_file(song_list_file_)) {
        remembered_ = parse_song_list(std::string_view(reinterpret_cast<const char*>(b->data()), b->size()));
        if (!remembered_) LOG_WARN("%s is not a song list; the folder is scanned anew", song_list_file_.c_str());
    }
}

void MusicLibrary::save_song_list() {
    // Only songs of a folder that was read: a folder that is gone keeps the list as it was.
    if (song_list_file_.empty() || !info_.found) return;
    const std::string text = song_list_text(folder_, songs_, disc_key_, disc_lufs_);
    if (!write_host_file(song_list_file_, ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size())))
        LOG_WARN("Could not save %s", song_list_file_.c_str());
    remembered_ = SongList{folder_, {}, disc_key_, disc_lufs_};
    list_dirty_ = false;
}

void MusicLibrary::set_disc_songs(std::string key, std::vector<StreamOpener> songs) {
    if (key == disc_key_ && (disc_lufs_ || measuring())) return;
    read_song_list();
    stop_measuring(); // its openers read the disc this replaces
    disc_key_ = std::move(key);
    disc_songs_ = std::move(songs);
    disc_measured_.assign(disc_songs_.size(), std::nullopt);
    disc_lufs_.reset();
    if (remembered_ && remembered_->disc_key == disc_key_ && remembered_->disc_lufs) disc_lufs_ = remembered_->disc_lufs;
    {
        std::lock_guard lock(measured_mutex_);
        disc_work_.clear();
        if (!disc_lufs_)
            for (size_t i = disc_songs_.size(); i-- > 0;) disc_work_.push_back(i); // (taken from the back)
    }
    ensure_measuring();
}

void MusicLibrary::want_measured(u64 id) {
    const CustomSong* s = find(id);
    if (!s || s->measured) return;
    {
        std::lock_guard lock(measured_mutex_);
        if (measuring_id_ == id) return;
        for (auto it = wanted_.begin(); it != wanted_.end(); ++it)
            if (it->first == id) {
                wanted_.erase(it);
                break;
            }
        wanted_.emplace_front(id, s->path);
        constexpr size_t kKept = 4; // the songs about to play; older asks are dropped
        if (wanted_.size() > kKept) wanted_.pop_back();
    }
    ensure_measuring();
}

float MusicLibrary::gain(const CustomSong& s) const {
    if (!disc_lufs_ || !s.measured || s.peak <= 0) return 1.0f;
    const float g = std::clamp(std::pow(10.0f, (*disc_lufs_ - s.lufs) / 20.0f), 0.1f, 4.0f);
    return std::min(g, 1.0f / (s.peak * bgm::kReplaySongVolume));
}

void MusicLibrary::racing() { racing_until_ = SDL_GetTicks() + 1500; }

size_t MusicLibrary::measured() const {
    return size_t(std::count_if(songs_.begin(), songs_.end(), [](const CustomSong& s) { return s.measured; }));
}

bool MusicLibrary::measuring() const {
    std::lock_guard lock(measured_mutex_);
    return worker_running_ || !wanted_.empty() || !disc_work_.empty();
}

void MusicLibrary::stop_measuring() {
    measure_stop_ = true;
    if (measure_thread_.joinable()) measure_thread_.join();
    measure_stop_ = false;
    std::lock_guard lock(measured_mutex_);
    worker_running_ = false;
    measuring_id_ = 0;
}

void MusicLibrary::ensure_measuring() {
    {
        std::lock_guard lock(measured_mutex_);
        if (worker_running_ || (wanted_.empty() && disc_work_.empty())) return;
        worker_running_ = true;
    }
    if (measure_thread_.joinable()) measure_thread_.join(); // (it has finished)
    measure_thread_ = std::thread([this] { measure_loop(); });
}

void MusicLibrary::measure_loop() {
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    for (;;) {
        // A song asked for first, else a disc song between races; nothing while the game is away.
        bool disc = false;
        size_t index = 0;
        StreamOpener open;
        std::pair<u64, std::string> song;
        bool wait = false;
        {
            std::lock_guard lock(measured_mutex_);
            if (measure_stop_) return;
            if (away_) {
                wait = true;
            } else if (!wanted_.empty()) {
                song = std::move(wanted_.front());
                wanted_.pop_front();
                measuring_id_ = song.first;
            } else if (!disc_work_.empty()) {
                if (SDL_GetTicks() < racing_until_.load()) {
                    wait = true;
                } else {
                    disc = true;
                    index = disc_work_.back();
                    disc_work_.pop_back();
                    open = disc_songs_[index];
                }
            } else {
                worker_running_ = false;
                return;
            }
        }
        if (wait) {
            SDL_Delay(200);
            continue;
        }
        std::optional<Loudness> l;
        try {
            std::unique_ptr<MusicStream> stream = disc ? open() : open_music_file(song.second, false);
            if (stream) l = measure_loudness(*stream, &measure_stop_);
        } catch (const std::exception& ex) {
            LOG_WARN("%s not measured: %s", disc ? "A disc song" : song.second.c_str(), ex.what());
        }
        std::lock_guard lock(measured_mutex_);
        measuring_id_ = 0;
        if (measure_stop_) { // not measured after all: asked for again
            if (disc) disc_work_.push_back(index);
            else wanted_.push_front(std::move(song));
            return;
        }
        measured_.push_back({disc, index, song.first, song.second, l});
    }
}

bool MusicLibrary::take_measured() {
    std::vector<Measured> landed;
    {
        std::lock_guard lock(measured_mutex_);
        landed.swap(measured_);
    }
    for (const Measured& m : landed) {
        if (m.disc) {
            if (m.index < disc_measured_.size())
                disc_measured_[m.index] = m.loudness ? float(m.loudness->lufs) : std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        for (CustomSong& s : songs_)
            if (s.id == m.id && s.path == m.path) {
                s.measured = true;
                s.lufs = m.loudness ? float(m.loudness->lufs) : 0.0f;
                s.peak = m.loudness ? m.loudness->peak : 0.0f;
                list_dirty_ = true;
                break;
            }
    }
    // The disc's level once all its songs are in: the mean of those that have one.
    if (!disc_lufs_ && !disc_measured_.empty() &&
        std::all_of(disc_measured_.begin(), disc_measured_.end(), [](const std::optional<float>& v) { return v.has_value(); })) {
        double sum = 0;
        int n = 0;
        for (const std::optional<float>& v : disc_measured_)
            if (!std::isnan(*v)) sum += *v, ++n;
        if (n > 0) {
            disc_lufs_ = float(sum / n);
            list_dirty_ = true;
            LOG_INFO("Disc race songs: %d measured, %.1f LUFS on average", n, double(*disc_lufs_));
        }
    }
    // Saved as songs land (one now and then); the disc's level when it is in.
    if (list_dirty_) save_song_list();
    return !landed.empty();
}

namespace {

// The scan's walk: each folder once (by its canonical path), its entries in name order.
struct Walk {
    fs::path root;
    const std::atomic<bool>* stop;
    std::vector<CustomSong>& out;
    MusicLibrary::ScanInfo& info;
    size_t limit;
    std::unordered_set<std::string> seen_folders, seen_files; // canonical paths
    std::unordered_set<u64> ids;

    bool stopped() const { return (stop && *stop) || info.capped; }

    void folder(const fs::path& dir, int depth) {
        if (depth > MusicLibrary::kMaxDepth || stopped()) return;
        std::error_code ec;
        const fs::path canon = fs::canonical(dir, ec);
        if (ec || !seen_folders.insert(utf8(canon)).second) return; // unreadable, or reached already
        // In name order, the links after the rest: a file or folder reached both ways is kept
        // under its own name.
        std::vector<std::pair<bool, fs::directory_entry>> entries;
        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code le;
            entries.emplace_back(it->is_symlink(le), *it);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return b.first;
            return less_folded(utf8(a.second.path().filename()), utf8(b.second.path().filename()));
        });
        for (const auto& entry : entries) {
            const fs::directory_entry& e = entry.second;
            if (stopped()) return;
            const fs::path& p = e.path();
            if (hidden(p)) continue;
            std::error_code fe;
            if (e.is_directory(fe)) { // (a link to a folder too)
                folder(p, depth + 1);
                continue;
            }
            if (!e.is_regular_file(fe)) continue;
            const std::string name = utf8(p.filename());
            if (music_file_type(name) == MusicFileType::None) {
                if (unplayable_music_file(name)) ++info.unplayable;
                continue;
            }
            const u64 size = e.file_size(fe);
            const fs::path real = fs::canonical(p, fe);
            if (fe || !seen_files.insert(utf8(real)).second) continue; // the same file again (a second link to it)
            if (out.size() >= limit) { // one song more than the limit
                info.capped = true;
                return;
            }
            CustomSong s;
            s.path = utf8(p);
            const std::u8string rel = p.lexically_relative(root).generic_u8string();
            s.rel = std::string(rel.begin(), rel.end());
            s.id = custom_song_id(utf8(p.parent_path().filename()) + "/" + name, size);
            if (!ids.insert(s.id).second) { // another song with that name, size and folder name
                s.id = custom_song_id(s.rel, size);
                if (!ids.insert(s.id).second) continue;
            }
            out.push_back(std::move(s));
        }
    }
};

} // namespace

std::vector<CustomSong> MusicLibrary::scan(const std::string& folder, ScanInfo& info, const std::atomic<bool>* stop,
                                           size_t limit) {
    std::vector<CustomSong> out;
    info = ScanInfo{};
    if (folder.empty()) return out;
    const fs::path root = fs_path(folder).lexically_normal(); // (Windows: one kind of separator in every path)
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    fs::directory_iterator probe(root, ec); // a folder that is there but can't be listed counts as missing
    if (ec) return out;
    info.found = true;
    Walk walk{root, stop, out, info, limit, {}, {}, {}};
    walk.folder(root, 0);
    if (stop && *stop) return info = ScanInfo{}, std::vector<CustomSong>{};
    std::sort(out.begin(), out.end(), [](const CustomSong& a, const CustomSong& b) { return less_folded(a.rel, b.rel); });
    for (CustomSong& s : out) {
        if (stop && *stop) return info = ScanInfo{}, std::vector<CustomSong>{};
        if (!music_file_decodes(s.path)) { // damaged, or another format under the name
            ++info.broken;
            s.id = 0;
            continue;
        }
        MusicTags t = read_music_tags(s.path, music_file_type(s.path));
        s.title = std::move(t.title);
        s.artist = std::move(t.artist);
        if (s.title.empty()) s.title = utf8(fs_path(s.path).stem());
    }
    std::erase_if(out, [](const CustomSong& s) { return s.id == 0; });
    return out;
}

} // namespace gt3r::race
