#include "platform/recent_discs.h"

#include "base/sha256.h"
#include "disc/region.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace gt3r::platform {

namespace {

bool is_sep(char c) { return c == '/' || c == '\\'; }

bool has_drive_letter(std::string_view p) {
    return p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':';
}

bool is_uri(std::string_view p) { return p.find("://") != std::string_view::npos; }

std::string normalized(std::string_view p) {
    bool windows = has_drive_letter(p) || (p.size() >= 2 && p[0] == '\\' && p[1] == '\\');
    std::string out;
    out.reserve(p.size());
    for (char c : p) {
        if (!is_uri(p) && is_sep(c)) c = '/';
        else if (windows) c = char(std::tolower(static_cast<unsigned char>(c)));
        out += c;
    }
    // A trailing separator never names a file; ignore it.
    while (out.size() > 1 && out.back() == '/' && !is_uri(p)) out.pop_back();
    return out;
}

std::string recent_key(int i) { return "recent" + std::to_string(i + 1); }

constexpr const char* kRecordSection = "disc_library";

// A record's key: the first 16 hex digits of the SHA-256 of the normalized path (a path may hold '=').
std::string record_key(std::string_view path) {
    const std::string n = normalized(path);
    return sha256_hex(ByteSpan(reinterpret_cast<const u8*>(n.data()), n.size())).substr(0, 16);
}

// The iOS app folder part of `p`, "/Containers/Data/Application/<UUID>", as [begin, end); both
// npos when `p` is in no app folder. What comes before it differs for the same folder: the
// system reports /private/var/... and /var/... alike.
struct AppContainer {
    size_t begin = std::string_view::npos, end = std::string_view::npos;
};
AppContainer app_container(std::string_view p) {
    static constexpr std::string_view kMarker = "/Containers/Data/Application/";
    const size_t at = p.find(kMarker);
    if (at == std::string_view::npos) return {};
    const size_t end = p.find('/', at + kMarker.size());
    if (end == std::string_view::npos || end == at + kMarker.size()) return {};
    return {at, end};
}

} // namespace

bool is_absolute_disc_path(std::string_view p) {
    if (p.empty()) return false;
    if (is_uri(p)) return true;
    if (is_sep(p[0])) return true; // POSIX root, or \\server\share
    return has_drive_letter(p) && p.size() >= 3 && is_sep(p[2]);
}

bool same_disc_path(std::string_view a, std::string_view b) { return normalized(a) == normalized(b); }

std::vector<std::string> load_recent_discs(const IniDocument& cfg) {
    std::vector<std::string> out;
    for (int i = 0; i < kMaxLibraryDiscs; ++i) {
        auto v = cfg.get("game", recent_key(i));
        if (!v || v->empty()) continue;
        if (std::none_of(out.begin(), out.end(), [&](const std::string& p) { return same_disc_path(p, *v); }))
            out.push_back(*v);
    }
    return out;
}

void save_recent_discs(IniDocument& cfg, const std::vector<std::string>& list) {
    IniDocument::Section& s = cfg.get_or_add("game");
    std::erase_if(s.entries, [](const auto& kv) {
        return kv.first.size() > 6 && kv.first.compare(0, 6, "recent") == 0 &&
               std::all_of(kv.first.begin() + 6, kv.first.end(), [](char c) { return c >= '0' && c <= '9'; });
    });
    std::set<std::string> kept;
    for (size_t i = 0; i < list.size() && i < size_t(kMaxLibraryDiscs); ++i) {
        cfg.set("game", recent_key(int(i)), list[i]);
        kept.insert(record_key(list[i]));
    }
    IniDocument::Section& r = cfg.get_or_add(kRecordSection);
    std::erase_if(r.entries, [&](const auto& kv) { return !kept.count(kv.first); });
}

std::optional<DiscRecord> load_disc_record(const IniDocument& cfg, std::string_view path) {
    const std::optional<std::string> v = cfg.get(kRecordSection, record_key(path));
    if (!v) return std::nullopt;
    std::vector<std::string_view> f;
    size_t at = 0;
    while (f.size() < 6) { // the description, last, may hold '|'
        const size_t bar = v->find('|', at);
        if (bar == std::string::npos) return std::nullopt;
        f.push_back(std::string_view(*v).substr(at, bar - at));
        at = bar + 1;
    }
    DiscRecord r;
    r.serial = f[0], r.version = f[1], r.region = f[2], r.digest = f[3], r.key = f[4];
    r.known = f[5].find('k') != std::string_view::npos;
    r.modified = f[5].find('m') != std::string_view::npos;
    r.description = v->substr(at);
    return r;
}

void save_disc_record(IniDocument& cfg, std::string_view path, const DiscRecord& r) {
    std::string flags = std::string(r.known ? "k" : "") + (r.modified ? "m" : "");
    cfg.set(kRecordSection, record_key(path),
            r.serial + "|" + r.version + "|" + r.region + "|" + r.digest + "|" + r.key + "|" + flags + "|" + r.description);
}

std::string disc_record_label(const DiscRecord& r) {
    const std::optional<Region> region = region_from_key(r.region);
    std::string out = r.known && region ? region_name(*region) : r.key;
    if (!r.version.empty()) out += " v" + r.version;
    if (r.modified) out += ", modified";
    return out;
}

std::vector<std::string> push_recent_disc(std::vector<std::string> list, const std::string& path) {
    if (path.empty()) return list;
    list = remove_recent_disc(std::move(list), path);
    list.insert(list.begin(), path);
    if (list.size() > size_t(kMaxLibraryDiscs)) list.resize(kMaxLibraryDiscs);
    return list;
}

std::vector<std::string> remove_recent_disc(std::vector<std::string> list, std::string_view path) {
    std::erase_if(list, [&](const std::string& p) { return same_disc_path(p, path); });
    return list;
}

std::vector<std::string> disc_path_candidates(const std::string& saved, const std::string& base_dir) {
    if (saved.empty()) return {};
    std::vector<std::string> out{saved};
    if (is_absolute_disc_path(saved) || base_dir.empty()) return out;
    std::string joined = base_dir;
    if (!is_sep(joined.back())) joined += '/';
    std::string_view rel = saved;
    while (rel.size() >= 2 && rel[0] == '.' && is_sep(rel[1])) rel.remove_prefix(2);
    joined += rel;
    out.push_back(std::move(joined));
    return out;
}

std::string disc_file_name(std::string_view path) {
    while (!path.empty() && is_sep(path.back())) path.remove_suffix(1);
    size_t cut = path.find_last_of("/\\");
    std::string_view name = cut == std::string_view::npos ? path : path.substr(cut + 1);
    if (is_uri(path)) {
        // content://.../document/primary%3AGames%2FGT3.iso: keep the last decoded segment.
        size_t enc = name.rfind("%2F");
        if (enc != std::string_view::npos) name = name.substr(enc + 3);
    }
    return std::string(name);
}

std::string rebase_app_container_path(std::string_view path, std::string_view data_dir) {
    // Up to the end of the <UUID> part; the current folder is written the way data_dir has it.
    const AppContainer p = app_container(path), d = app_container(data_dir);
    if (p.end == std::string_view::npos || d.end == std::string_view::npos) return std::string(path);
    std::string out(data_dir.substr(0, d.end));
    out += path.substr(p.end);
    return out;
}

bool same_app_container(std::string_view a, std::string_view b) {
    const AppContainer ca = app_container(a), cb = app_container(b);
    return ca.end != std::string_view::npos && cb.end != std::string_view::npos &&
           a.substr(ca.begin, ca.end - ca.begin) == b.substr(cb.begin, cb.end - cb.begin);
}

} // namespace gt3r::platform
