#include "game/save_library.h"

#include "base/file.h"
#include "game/gt_save.h"
#include "game/save_folder.h"

#include <algorithm>
#include <cstring>

namespace gt3r::game {

namespace {

constexpr const char* kCurrentFile = "current.txt";

std::string with_sep(std::string d) {
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d;
}

std::string base_name(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}

std::string stem_of(const std::string& path) {
    std::string b = base_name(path);
    if (b.size() > 4) {
        std::string ext = b.substr(b.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (ext == ".sav") b.resize(b.size() - 4);
    }
    return b;
}

// A save file of the library's folder (*.sav, any case).
bool is_save_file(const HostDirEntry& e) {
    if (e.dir || e.name.size() < 4) return false;
    std::string ext = e.name.substr(e.name.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext == ".sav";
}

// Cuts a UTF-8 string to at most `n` bytes without splitting a character.
std::string cut(std::string s, size_t n) {
    if (s.size() <= n) return s;
    s.resize(n);
    while (!s.empty() && (u8(s.back()) & 0xC0) == 0x80) s.pop_back();
    if (!s.empty() && u8(s.back()) >= 0xC0) s.pop_back();
    return s;
}

} // namespace

bool same_save_file(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    std::string x = a, y = b;
    std::replace(x.begin(), x.end(), '\\', '/');
    std::replace(y.begin(), y.end(), '\\', '/');
    return x == y;
}

SaveLibrary::SaveLibrary(std::string dir, bool tracked) : dir_(with_sep(std::move(dir))), tracked_(tracked) {}

std::string SaveLibrary::user_dir(std::string_view disc_key) {
    std::string dir = save_folder() + "saves/";
    return disc_key == region_key(Region::America) ? dir : dir + std::string(disc_key) + "/";
}
std::string SaveLibrary::user_dir(Region region) { return user_dir(region_key(region)); }
std::string SaveLibrary::legacy_path() { return user_data_dir() + "gtmode.sav"; }

int SaveLibrary::migrate(const std::string& legacy) {
    if (host_is_directory(dir_)) return 0;
    if (!host_make_dir(dir_)) return 0;
    int n = 0;
    std::string second = legacy;
    if (size_t dot = second.rfind('.'); dot != std::string::npos && second.find_first_of("/\\", dot) == std::string::npos)
        second = second.substr(0, dot) + "_2" + second.substr(dot);
    else
        second += "_2";
    for (const std::string& from : {legacy, second}) {
        if (!host_path_exists(from) || host_is_directory(from)) continue;
        std::string to = dir_ + base_name(from);
        if (!host_copy_file(from, to)) continue;
        if (n++ == 0 && tracked_) set_current(to);
    }
    return n;
}

int SaveLibrary::copy_library(const std::string& from) {
    if (host_is_directory(dir_)) return 0;
    if (!host_make_dir(dir_)) return 0;
    const std::string src = with_sep(from);
    int n = 0;
    std::string copied_current;
    const std::string current = SaveLibrary(src).current();
    for (const HostDirEntry& e : host_dir_entries(src)) {
        const std::string f = src + e.name;
        if (!is_save_file(e) || !host_copy_file(f, dir_ + e.name)) continue;
        ++n;
        if (same_save_file(f, current)) copied_current = dir_ + e.name;
    }
    if (!copied_current.empty() && tracked_) set_current(copied_current);
    return n;
}

std::vector<SaveInfo> SaveLibrary::list() const {
    std::vector<SaveInfo> out;
    for (const HostDirEntry& e : host_dir_entries(dir_)) {
        if (!is_save_file(e)) continue;
        SaveInfo f;
        f.path = dir_ + e.name;
        f.mtime = e.mtime;
        f.state = read_save(f.path, &f.error);
        f.name = f.state ? display_name(*f.state, f.path) : stem_of(f.path);
        out.push_back(std::move(f));
    }
    std::sort(out.begin(), out.end(), [](const SaveInfo& a, const SaveInfo& b) {
        return a.mtime != b.mtime ? a.mtime > b.mtime : a.path < b.path;
    });
    return out;
}

std::string SaveLibrary::current() const {
    if (!tracked_) return {};
    std::optional<Bytes> b = read_host_file(dir_ + kCurrentFile);
    if (!b) return {};
    std::string name(b->begin(), b->end());
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' ')) name.pop_back();
    if (name.empty() || name.find_first_of("/\\") != std::string::npos) return {};
    std::string path = dir_ + name;
    return host_path_exists(path) ? path : std::string();
}

bool SaveLibrary::set_current(const std::string& path) {
    if (!tracked_) return true;
    host_make_dir(dir_);
    std::string name = base_name(path) + "\n";
    return write_host_file(dir_ + kCurrentFile, ByteSpan(reinterpret_cast<const u8*>(name.data()), name.size()));
}

std::string SaveLibrary::display_name(const GameState& s, const std::string& path) {
    return s.file_comment.empty() ? stem_of(path) : s.file_comment;
}

std::string SaveLibrary::file_stem(const std::string& name) {
    std::string o;
    for (char c : name) {
        if (u8(c) < 0x20 || std::strchr("/\\:*?\"<>|", c)) o += '_';
        else o += c;
    }
    // No leading / trailing spaces or dots (Windows drops them; "." / ".." are not names).
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    size_t b = o.find_first_not_of(" .");
    o = b == std::string::npos ? std::string() : o.substr(b);
    return o.empty() ? "save" : o;
}

std::string SaveLibrary::unique_name(const std::string& base, size_t max, const std::string& except) const {
    std::vector<std::string> taken;
    for (const SaveInfo& f : list())
        if (!same_save_file(f.path, except)) taken.push_back(f.name);
    auto free = [&](const std::string& n) { return std::find(taken.begin(), taken.end(), n) == taken.end(); };
    std::string b = cut(base.empty() ? std::string("GAME") : base, max);
    if (free(b)) return b;
    for (int i = 2; i < 1000; ++i) {
        std::string suffix = " " + std::to_string(i);
        std::string n = cut(b, max > suffix.size() ? max - suffix.size() : 0) + suffix;
        if (free(n)) return n;
    }
    return b;
}

std::string SaveLibrary::path_for(const std::string& name, const std::string& except) const {
    const std::string stem = file_stem(name);
    for (int i = 1; i < 1000; ++i) {
        std::string p = dir_ + stem + (i == 1 ? std::string() : " " + std::to_string(i)) + ".sav";
        if (same_save_file(p, except) || !host_path_exists(p)) return p;
    }
    return dir_ + stem + ".sav";
}

bool SaveLibrary::create(GameState s, const std::string& name, std::string* path, std::string* error) {
    host_make_dir(dir_);
    s.file_comment = name;
    std::string p = path_for(name);
    if (!write_save(p, s, error)) return false;
    if (path) *path = p;
    return true;
}

bool SaveLibrary::rename(const std::string& path, const std::string& name, std::string* new_path,
                         const GameState* live, std::string* error) {
    GameState s;
    if (live) {
        s = *live;
    } else {
        std::optional<GameState> r = read_save(path, error);
        if (!r) return false;
        s = std::move(*r);
    }
    s.file_comment = name;
    const bool was_current = same_save_file(path, current());
    std::string p = path_for(name, path);
    if (!write_save(p, s, error)) return false;
    if (!same_save_file(p, path)) remove(path);
    if (was_current) set_current(p);
    if (new_path) *new_path = p;
    return true;
}

bool SaveLibrary::copy(const std::string& path, std::string* new_path, std::string* error) {
    std::optional<GameState> s = read_save(path, error);
    if (!s) return false;
    std::string name = unique_name(display_name(*s, path));
    return create(std::move(*s), name, new_path, error);
}

bool SaveLibrary::remove(const std::string& path) { return host_remove(path); }

GameState read_any_save(const std::string& path, const GtData& data) {
    if (!host_is_directory(path)) {
        std::optional<Bytes> b = read_host_file(path);
        if (!b) throw FormatError("cannot read " + path);
        static const char kMagic[] = "GT3R-GTMODE";
        if (b->size() >= sizeof kMagic - 1 && std::memcmp(b->data(), kMagic, sizeof kMagic - 1) == 0)
            return load_from_text(std::string(b->begin(), b->end()));
    }
    return import_ps2_save_path(path, data);
}

} // namespace gt3r::game
