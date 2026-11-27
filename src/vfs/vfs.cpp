#include "vfs/vfs.h"

#include "base/config.h"
#include "base/log.h"
#include "disc/disc.h"

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <set>

namespace gt3r {

std::string normalize_vfs_path(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        if (c == '\\') c = '/';
        if (c == '/' && (out.empty() || out.back() == '/')) continue;
        out.push_back(c);
    }
    return out;
}

Vfs::Vfs(Volume* base) : base_(base) {}

std::vector<std::string> list_host_dir(const std::string& dir) {
    std::vector<std::string> out;
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* user, const char*, const char* name) {
            static_cast<std::vector<std::string>*>(user)->push_back(name);
            return SDL_ENUM_CONTINUE;
        },
        &out);
    std::sort(out.begin(), out.end());
    return out;
}

namespace {

void collect_files(const std::string& root, const std::string& rel, std::vector<std::string>& out, int depth) {
    if (depth > 16) return;
    for (const std::string& name : list_host_dir(rel.empty() ? root : root + "/" + rel)) {
        std::string child = rel.empty() ? name : rel + "/" + name;
        if (host_is_directory(root + "/" + child))
            collect_files(root, child, out, depth + 1);
        else
            out.push_back(child);
    }
}

} // namespace

bool mod_for_disc(const std::vector<std::string>& discs, std::string_view key, std::string_view serial) {
    const std::string k = to_lower(key), s = to_lower(serial), ss = to_lower(short_serial(serial));
    bool listed = false, any_listed = false;
    for (const std::string& d : discs) {
        std::string n = to_lower(trim(d));
        const bool exclude = !n.empty() && n[0] == '!';
        if (exclude) n = to_lower(trim(std::string_view(n).substr(1)));
        const bool match = n == k || (!s.empty() && (n == s || n == ss));
        if (exclude && match) return false;
        if (!exclude) any_listed = true, listed = listed || match;
    }
    return !any_listed || listed;
}

void Vfs::scan_mods(const std::string& mods_dir, const IniDocument* config) {
    mods_.clear();
    const std::string key = disc_key(), section = mods_section(), serial = base_ ? base_->serial() : std::string();
    for (const std::string& id : list_host_dir(mods_dir)) {
        std::string root = mods_dir + "/" + id;
        if (!host_is_directory(root)) continue;
        ModInfo mod;
        mod.id = id;
        mod.root = root;
        mod.name = mod.id;
        if (auto ini = IniDocument::load(root + "/mod.ini")) {
            mod.name = ini->get("mod", "name").value_or(mod.id);
            mod.version = ini->get("mod", "version").value_or("");
            mod.author = ini->get("mod", "author").value_or("");
            mod.description = ini->get("mod", "description").value_or("");
            mod.priority = std::atoi(ini->get("mod", "priority").value_or("0").c_str());
            const std::string discs = ini->get("mod", "discs").value_or("");
            for (std::string_view d : split(discs, ','))
                if (!trim(d).empty()) mod.discs.emplace_back(trim(d));
        }
        mod.for_disc = mod_for_disc(mod.discs, key, serial);
        if (config) {
            std::optional<std::string> on = config->get(section, mod.id);
            if (!on) on = config->get("mods", mod.id);
            if (on) mod.enabled = to_lower(*on) != "off" && *on != "0";
        }
        std::vector<std::string> files;
        collect_files(root + "/files", "", files, 0);
        for (auto& f : files) mod.files.push_back(normalize_vfs_path(f));
        for (const std::string& name : list_host_dir(root + "/cars"))
            if (name.size() > 4 && to_lower(name.substr(name.size() - 4)) == ".ini") mod.cars.push_back(root + "/cars/" + name);
        for (const std::string& name : list_host_dir(root + "/makers"))
            if (name.size() > 4 && to_lower(name.substr(name.size() - 4)) == ".ini") mod.makers.push_back(root + "/makers/" + name);
        for (const std::string& name : list_host_dir(root + "/events"))
            if (name.size() > 4 && to_lower(name.substr(name.size() - 4)) == ".ini") mod.events.push_back(root + "/events/" + name);
        for (const std::string& name : list_host_dir(root + "/liveries")) {
            const std::string path = root + "/liveries/" + name;
            if (name.size() > 4 && to_lower(name.substr(name.size() - 4)) == ".ini") mod.liveries.push_back(path);
            else if (host_is_directory(path) && host_path_exists(path + "/livery.ini")) mod.liveries.push_back(path + "/livery.ini");
        }
        std::sort(mod.liveries.begin(), mod.liveries.end());
        for (const std::string& name : list_host_dir(root + "/wheels"))
            if (name.size() > 4 && to_lower(name.substr(name.size() - 4)) == ".ini") mod.wheels.push_back(root + "/wheels/" + name);
        mod.textures = texture_pack::pictures_in(root + "/textures");
        LOG_INFO("Mod '%s' (%s): %zu files, %zu cars, %zu events, %zu wheels, %zu textures, priority %d, %s",
                 mod.name.c_str(), mod.id.c_str(), mod.files.size(), mod.cars.size(), mod.events.size(),
                 mod.wheels.size(), mod.textures.size(), mod.priority,
                 !mod.for_disc ? "for other discs" : mod.enabled ? "enabled" : "disabled");
        mods_.push_back(std::move(mod));
    }
    // Stable order: priority, then id, so load order is deterministic.
    std::sort(mods_.begin(), mods_.end(), [](const ModInfo& a, const ModInfo& b) {
        return a.priority != b.priority ? a.priority < b.priority : a.id < b.id;
    });
    rebuild_index();
}

void Vfs::set_mod_enabled(const std::string& id, bool enabled) {
    for (ModInfo& m : mods_)
        if (m.id == id) m.enabled = enabled;
    rebuild_index();
}

void Vfs::rebuild_index() {
    overrides_.clear();
    override_source_.clear();
    for (const ModInfo& m : mods_) { // ascending priority: later wins
        if (!m.active()) continue;
        for (const std::string& f : m.files) {
            overrides_[f] = m.root + "/files/" + f;
            override_source_[f] = m.id;
        }
    }
    // Mod wheels: their rims as the disc keeps its wheels. A label the disc has stays the disc's (the
    // shop would list it twice); one two mods give is the higher priority's.
    wheels_.clear();
    rims_.clear();
    for (const ModInfo& m : mods_) {
        if (!m.active()) continue;
        for (const std::string& f : m.wheels) {
            ModWheelDef d = load_mod_wheel(f, m.id, m.root);
            if (d.error.empty() && base_ && base_->find("wheel/" + d.label))
                d.error = "label " + d.label + " is a wheel of the disc";
            for (const std::string* r : {&d.model, &d.menu, &d.race})
                if (const std::string w = disc_rim(*r); d.error.empty() && !w.empty() && !exists("wheel/" + w))
                    d.error = "the disc has no wheel " + w;
            wheels_.push_back(std::move(d));
        }
    }
    for (size_t i = 0; i < wheels_.size(); ++i) {
        ModWheelDef& d = wheels_[i];
        if (!d.error.empty()) continue;
        for (size_t j = i + 1; j < wheels_.size(); ++j)
            if (wheels_[j].error.empty() && wheels_[j].label == d.label) {
                d.error = "label " + d.label + " is also " + wheels_[j].mod_id + "'s, which wins";
                break;
            }
        if (!d.error.empty()) continue;
        for (const char* dir : {"wheel/", "menu/wheel/"}) {
            const std::string p = dir + d.label, from = *dir == 'w' ? d.race_file() : d.menu_file();
            override_source_[p] = d.mod_id;
            const std::string w = disc_rim(from);
            if (w.empty() && !d.shape.any()) {
                overrides_[p] = from;
                continue;
            }
            ModRim r;
            r.shape = d.shape;
            if (w.empty()) r.host = from;
            else r.vol = exists(dir + w) ? dir + w : "wheel/" + w; // (a car's own rim has no menus' file)
            rims_[p] = r;
        }
    }
    auto pack = std::make_shared<texture_pack::Pack>();
    for (auto m = mods_.rbegin(); m != mods_.rend(); ++m) // descending priority: the first file of a key stays
        if (m->active())
            for (const auto& [key, path] : m->textures) pack->add_file(key, path, m->id);
    textures_ = pack->size() ? std::move(pack) : nullptr;
}

bool Vfs::exists(const std::string& path) const {
    std::string p = normalize_vfs_path(path);
    return overrides_.count(p) || rims_.count(p) || (base_ && base_->find(p));
}

std::optional<Bytes> Vfs::read(const std::string& path) const {
    std::string p = normalize_vfs_path(path);
    if (p == kModCarsList) { // "<mod id>\t<host path>" per line, mods in ascending priority
        std::string text;
        for (const ModInfo& m : mods_)
            if (m.active()) {
                for (const std::string& f : m.makers) text += m.id + "\t" + f + "\n"; // makers first
                for (const std::string& f : m.cars) text += m.id + "\t" + f + "\n";
                for (const std::string& f : m.events) text += m.id + "\t" + f + "\n";
            }
        return Bytes(text.begin(), text.end());
    }
    if (p == kModWheelsList) {
        std::string text;
        for (const ModWheelDef& d : wheels_) {
            std::string root;
            for (const ModInfo& m : mods_)
                if (m.id == d.mod_id) root = m.root;
            text += d.mod_id + "\t" + d.file + "\t" + root + "\t" + d.error + "\n";
        }
        return Bytes(text.begin(), text.end());
    }
    if (p == kProgramFile) return base_ ? base_->read_program() : std::nullopt;
    if (p == kRegionFile) {
        if (!base_) return std::nullopt;
        std::string key = base_->region_data().to_text();
        return Bytes(key.begin(), key.end());
    }
    if (p == kDiscKeyFile) {
        if (!base_) return std::nullopt;
        std::string key = disc_key();
        return Bytes(key.begin(), key.end());
    }
    if (p == kLanguageFile) {
        if (!base_ && !language_) return std::nullopt;
        std::string code = text::language_code(language());
        return Bytes(code.begin(), code.end());
    }
    if (auto it = overrides_.find(p); it != overrides_.end()) return read_host_file(it->second);
    if (auto it = rims_.find(p); it != rims_.end()) {
        std::optional<Bytes> rim = it->second.vol.empty() ? read_host_file(it->second.host) : read(it->second.vol);
        if (rim && it->second.shape.any()) rim = wrap_shaped_rim(*rim, it->second.shape);
        return rim;
    }
    if (base_)
        if (const Volume::Entry* e = base_->find(p)) return base_->read(*e);
    return std::nullopt;
}

std::optional<Vfs::Stream> Vfs::open_stream(const std::string& path) const {
    std::string p = normalize_vfs_path(path);
    if (auto it = overrides_.find(p); it != overrides_.end()) {
        std::shared_ptr<File> file = File::open(it->second);
        if (!file) return std::nullopt;
        Stream s;
        s.size = file->size();
        s.read = [file](u64 off, void* out, size_t len) { file->read_at(off, out, len); };
        return s;
    }
    if (!base_) return std::nullopt;
    const Volume::Entry* e = base_->find(p);
    if (!e) return std::nullopt;
    Stream s;
    s.size = e->size;
    if (!e->compressed) {
        Volume* vol = base_;
        Volume::Entry entry = *e;
        s.read = [vol, entry](u64 off, void* out, size_t len) { vol->read_range(entry, off, out, len); };
    } else {
        auto data = std::make_shared<Bytes>(base_->read(*e));
        s.read = [data](u64 off, void* out, size_t len) {
            if (off > data->size() || data->size() - off < len) throw FormatError("stream read out of range");
            std::memcpy(out, data->data() + off, len);
        };
    }
    return s;
}

std::string Vfs::source_of(const std::string& path) const {
    std::string p = normalize_vfs_path(path);
    if (auto it = override_source_.find(p); it != override_source_.end()) return it->second;
    return base_ && base_->find(p) ? "disc" : "";
}

std::vector<std::string> Vfs::list_dir(const std::string& dir) const {
    std::string prefix = normalize_vfs_path(dir);
    if (!prefix.empty() && prefix.back() != '/') prefix += '/';
    std::vector<std::string> base_list = base_ ? base_->list_dir(prefix) : std::vector<std::string>{};
    std::set<std::string> dirs, files;
    for (auto& n : base_list) (n.back() == '/' ? dirs : files).insert(n);
    auto add = [&](const std::string& path) {
        std::string rest = path.substr(prefix.size());
        if (auto slash = rest.find('/'); slash != std::string::npos)
            dirs.insert(rest.substr(0, slash + 1));
        else
            files.insert(rest);
    };
    for (auto it = overrides_.lower_bound(prefix); it != overrides_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
        add(it->first);
    for (auto it = rims_.lower_bound(prefix); it != rims_.end() && it->first.compare(0, prefix.size(), prefix) == 0; ++it)
        add(it->first);
    std::vector<std::string> out(dirs.begin(), dirs.end());
    out.insert(out.end(), files.begin(), files.end());
    return out;
}

RegionData region_data(const std::function<std::optional<Bytes>(const std::string&)>& read) {
    if (std::optional<Bytes> b = read ? read(Vfs::kRegionFile) : std::nullopt)
        if (std::optional<RegionData> r = RegionData::from_text(std::string(b->begin(), b->end()))) return *r;
    return {};
}

Language language_of(const std::function<std::optional<Bytes>(const std::string&)>& read) {
    if (std::optional<Bytes> b = read ? read(Vfs::kLanguageFile) : std::nullopt)
        if (std::optional<Language> l = text::language_from_code(std::string(b->begin(), b->end()))) return *l;
    return region_data(read).default_language();
}

std::string disc_key_of(const std::function<std::optional<Bytes>(const std::string&)>& read) {
    if (std::optional<Bytes> b = read ? read(Vfs::kDiscKeyFile) : std::nullopt; b && !b->empty())
        return std::string(b->begin(), b->end());
    return region_key(region_data(read).region);
}

std::string course_file_path(const Vfs& vfs, std::string_view label) {
    std::string own = vfs.region().course_path(label, vfs.language());
    if (vfs.exists(own)) return own;
    for (const char* dir : {"courses/", "courses/US/", "courses/UK/", "courses/EU/", "courses/JP/"}) {
        std::string p = dir + std::string(label);
        if (vfs.exists(p)) return p;
    }
    return {};
}

} // namespace gt3r
