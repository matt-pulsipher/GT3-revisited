#include "vfs/mod_archive.h"

#include "base/config.h"
#include "base/file.h"
#include "base/log.h"

#include <SDL3/SDL_iostream.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace gt3r {

namespace fs = std::filesystem;

namespace {

constexpr size_t kChunk = 256 * 1024;
constexpr u64 kMaxCentralDirectory = 64u << 20;
constexpr u64 kMaxEntries = 200000;
constexpr u64 kSpareBytes = 16u << 20; // left free on the device after unpacking
constexpr int kMaxModDepth = 4;        // folders above a mod.ini inside the archive

bool fail(std::string* error, std::string msg) {
    if (error) *error = std::move(msg);
    return false;
}

u16 le16(const u8* p) { return u16(p[0] | p[1] << 8); }
u32 le32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
u64 le64(const u8* p) { return u64(le32(p)) | u64(le32(p + 4)) << 32; }

// UTF-8 paths on every platform (Windows included).
fs::path fs_path(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string_view base_name(std::string_view path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

bool starts_with(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

// Clutter that archivers add: macOS resource forks and Finder files, Windows thumbnails.
bool ignored(std::string_view name) {
    std::string_view base = base_name(name);
    return starts_with(name, "__MACOSX/") || base == ".DS_Store" || starts_with(base, "._") || base == "Thumbs.db" ||
           base == "desktop.ini";
}

// '/' separators, without "." or empty components (a trailing '/' marks a folder).
std::string normalize_entry_name(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    std::string out;
    size_t start = 0;
    const bool absolute = !name.empty() && name[0] == '/';
    while (start <= name.size()) {
        size_t slash = name.find('/', start);
        std::string_view part = std::string_view(name).substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty() && part != ".") {
            if (!out.empty()) out += '/';
            out += part;
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    if (absolute) out.insert(out.begin(), '/'); // kept so safe_archive_path refuses it
    if (!out.empty() && !name.empty() && name.back() == '/') out += '/';
    return out;
}

std::string mb(u64 bytes) { return std::to_string((bytes + (1u << 20) - 1) >> 20) + " MB"; }

} // namespace

// --- ZipArchive ---

ZipArchive::ZipArchive(std::unique_ptr<File> f) : file_(std::move(f)) {}
ZipArchive::~ZipArchive() = default;

std::unique_ptr<ZipArchive> ZipArchive::open(const std::string& path, std::string* error) {
    std::unique_ptr<File> f = File::open(path);
    if (!f) {
        fail(error, "cannot open the file");
        return nullptr;
    }
    std::unique_ptr<ZipArchive> zip(new ZipArchive(std::move(f)));
    try {
        if (!zip->parse(error)) return nullptr;
    } catch (const std::exception& e) {
        fail(error, std::string("cannot read the file: ") + e.what());
        return nullptr;
    }
    return zip;
}

bool ZipArchive::parse(std::string* error) {
    const u64 size = file_->size();
    if (size < 22) return fail(error, "not a .zip file");
    // End of central directory record: within the last 64 KB (its comment) + 22 bytes.
    const size_t tail_len = size_t(std::min<u64>(size, 22 + 0xFFFF + 20));
    const u64 tail_at = size - tail_len;
    const Bytes tail = file_->read_at(tail_at, tail_len);
    size_t eocd = std::string::npos;
    for (size_t i = tail_len - 22 + 1; i-- > 0;)
        if (le32(&tail[i]) == 0x06054b50 && i + 22 + le16(&tail[i + 20]) <= tail_len) {
            eocd = i;
            break;
        }
    if (eocd == std::string::npos) return fail(error, "not a .zip file");
    const u8* e = &tail[eocd];
    u64 count = le16(e + 10), cd_size = le32(e + 12), cd_offset = le32(e + 16);
    bool multi_disk = le16(e + 4) != 0 || le16(e + 6) != 0;
    if (count == 0xFFFF || cd_size == 0xFFFFFFFF || cd_offset == 0xFFFFFFFF) { // ZIP64
        if (eocd < 20 || le32(&tail[eocd - 20]) != 0x07064b50) return fail(error, "damaged .zip file");
        const u64 at = le64(&tail[eocd - 20 + 8]);
        if (at > size || size - at < 56) return fail(error, "damaged .zip file");
        const Bytes z = file_->read_at(at, 56);
        if (le32(z.data()) != 0x06064b50) return fail(error, "damaged .zip file");
        multi_disk = le32(&z[16]) != 0 || le32(&z[20]) != 0;
        count = le64(&z[32]), cd_size = le64(&z[40]), cd_offset = le64(&z[48]);
    }
    if (multi_disk) return fail(error, "split (multi-part) archives are not supported");
    if (cd_offset > size || size - cd_offset < cd_size) return fail(error, "damaged .zip file");
    if (cd_size > kMaxCentralDirectory || count > kMaxEntries) return fail(error, "too many files in the archive");

    const Bytes cd = file_->read_at(cd_offset, size_t(cd_size));
    size_t p = 0;
    entries_.clear();
    entries_.reserve(size_t(count));
    for (u64 i = 0; i < count; ++i) {
        if (cd.size() - p < 46 || le32(&cd[p]) != 0x02014b50) return fail(error, "damaged .zip file");
        const u8* h = &cd[p];
        Entry en;
        const u16 made_by = le16(h + 4);
        en.flags = le16(h + 8);
        en.method = le16(h + 10);
        en.crc = le32(h + 16);
        en.compressed = le32(h + 20);
        en.size = le32(h + 24);
        const size_t name_len = le16(h + 28), extra_len = le16(h + 30), comment_len = le16(h + 32);
        const u32 attributes = le32(h + 38);
        en.local_offset = le32(h + 42);
        if (cd.size() - p - 46 < name_len + extra_len + comment_len) return fail(error, "damaged .zip file");
        en.name.assign(reinterpret_cast<const char*>(h + 46), name_len);
        // ZIP64 sizes and offset: present for each field that reads 0xFFFFFFFF, in this order.
        for (size_t x = 46 + name_len, end = x + extra_len; x + 4 <= end;) {
            const u16 id = le16(h + x), len = le16(h + x + 2);
            if (x + 4 + len > end) break;
            if (id == 0x0001) {
                size_t q = x + 4;
                for (u64* v : {&en.size, &en.compressed, &en.local_offset})
                    if (*v == 0xFFFFFFFF && q + 8 <= x + 4 + len) *v = le64(h + q), q += 8;
            }
            x += 4 + len;
        }
        // Unix (3) and macOS (19) archivers keep the file mode in the high attribute bits.
        const u8 host = u8(made_by >> 8);
        en.symlink = (host == 3 || host == 19) && ((attributes >> 16) & 0170000) == 0120000;
        en.name = normalize_entry_name(std::move(en.name));
        en.dir = !en.name.empty() && en.name.back() == '/';
        if (en.local_offset > size) return fail(error, "damaged .zip file");
        entries_.push_back(std::move(en));
        p += 46 + name_len + extra_len + comment_len;
    }
    return true;
}

bool ZipArchive::read(const Entry& e, const std::function<bool(const u8*, size_t)>& sink, std::string* error) const {
    const std::string damaged = e.name + ": the archive is damaged";
    if (e.flags & 1) return fail(error, "the archive is password-protected");
    if (e.method != 0 && e.method != 8)
        return fail(error, e.name + ": compressed with a method other than Deflate (save the .zip with standard compression)");
    try {
        u8 local[30];
        file_->read_at(e.local_offset, local, sizeof(local));
        if (le32(local) != 0x04034b50) return fail(error, damaged);
        const u64 data = e.local_offset + 30 + le16(local + 26) + le16(local + 28);
        if (data > file_->size() || file_->size() - data < e.compressed) return fail(error, damaged);
        if (e.method == 0 && e.compressed != e.size) return fail(error, damaged);

        Bytes in(kChunk), out(kChunk);
        uLong crc = crc32(0, nullptr, 0);
        u64 produced = 0, pos = data, in_left = e.compressed;
        auto emit = [&](const u8* bytes, size_t n) {
            crc = crc32(crc, bytes, uInt(n));
            produced += n;
            return sink(bytes, n);
        };
        if (e.method == 0) {
            while (in_left) {
                const size_t n = size_t(std::min<u64>(in_left, kChunk));
                file_->read_at(pos, in.data(), n);
                pos += n, in_left -= n;
                if (!emit(in.data(), n)) return fail(error, e.name + ": stopped");
            }
        } else {
            z_stream zs{};
            if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return fail(error, "out of memory");
            struct End {
                z_stream* zs;
                ~End() { inflateEnd(zs); }
            } end{&zs};
            int rc = Z_OK;
            while (rc != Z_STREAM_END) {
                if (zs.avail_in == 0) {
                    if (in_left == 0) return fail(error, damaged); // ends early
                    const size_t n = size_t(std::min<u64>(in_left, kChunk));
                    file_->read_at(pos, in.data(), n);
                    pos += n, in_left -= n;
                    zs.next_in = in.data();
                    zs.avail_in = uInt(n);
                }
                zs.next_out = out.data();
                zs.avail_out = uInt(kChunk);
                rc = inflate(&zs, Z_NO_FLUSH);
                if (rc != Z_OK && rc != Z_STREAM_END && !(rc == Z_BUF_ERROR && zs.avail_in == 0))
                    return fail(error, damaged);
                const size_t got = kChunk - zs.avail_out;
                if (got > e.size - produced) return fail(error, damaged); // more than it says
                if (got && !emit(out.data(), got)) return fail(error, e.name + ": stopped");
            }
        }
        if (produced != e.size || u32(crc) != e.crc) return fail(error, damaged);
        return true;
    } catch (const std::exception& ex) {
        return fail(error, e.name + ": " + ex.what());
    }
}

std::optional<Bytes> ZipArchive::read_all(const Entry& e, size_t max_size, std::string* error) const {
    if (e.size > max_size) {
        fail(error, e.name + " is too large");
        return std::nullopt;
    }
    Bytes out;
    out.reserve(size_t(e.size));
    if (!read(e, [&](const u8* p, size_t n) { return out.insert(out.end(), p, p + n), true; }, error)) return std::nullopt;
    return out;
}

// --- planning ---

std::string mod_id_from(std::string_view s) {
    std::string out;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
                        c == '_' || c == '-';
        if (!ok && !out.empty() && out.back() == '_') continue;
        out.push_back(ok ? c : '_');
    }
    auto trim = [&] {
        while (!out.empty() && (out.front() == '.' || out.front() == '_' || out.front() == '-')) out.erase(out.begin());
        while (!out.empty() && (out.back() == '.' || out.back() == '_')) out.pop_back();
    };
    trim();
    if (out.size() > 48) out.resize(48), trim();
    // Names Windows reserves for devices.
    std::string upper = out.substr(0, out.find('.'));
    for (char& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    static const std::set<std::string> kReserved = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                                                    "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                                                    "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (kReserved.count(upper)) out += "_mod";
    return out;
}

bool safe_archive_path(std::string_view path) {
    if (path.empty() || path.size() > 1024 || path[0] == '/') return false;
    for (char c : path)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F || c == ':' || c == '\\') return false;
    if (path.back() == '/') path.remove_suffix(1);
    size_t start = 0;
    while (start <= path.size()) {
        size_t slash = path.find('/', start);
        std::string_view part = path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (part.empty() || part == "." || part == ".." || part.size() > 255) return false;
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return true;
}

std::optional<ModArchivePlan> plan_mod_install(const ZipArchive& zip, std::string_view archive_name,
                                               const std::string& mods_dir, std::string* error) {
    using Entry = ZipArchive::Entry;
    std::vector<const Entry*> files;
    for (const Entry& e : zip.entries()) {
        if (!safe_archive_path(e.name)) {
            fail(error, "the archive has an unsafe path (" + e.name.substr(0, 60) + ")");
            return std::nullopt;
        }
        if (!e.dir && !e.symlink && !ignored(e.name)) files.push_back(&e);
    }

    // Mod folders: where a mod.ini is, the shallowest on each branch.
    std::vector<std::string> found;
    for (const Entry* e : files) {
        if (to_lower(base_name(e->name)) != "mod.ini") continue;
        std::string dir = e->name.substr(0, e->name.size() - base_name(e->name).size());
        if (std::count(dir.begin(), dir.end(), '/') <= kMaxModDepth) found.push_back(dir);
    }
    std::sort(found.begin(), found.end(),
              [](const std::string& a, const std::string& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; });
    std::vector<std::string> roots;
    for (const std::string& d : found)
        if (std::none_of(roots.begin(), roots.end(), [&](const std::string& r) { return starts_with(d, r); }))
            roots.push_back(d);
    // No mod.ini: the top level, or the only top folder, when it has a mod's folders.
    if (roots.empty()) {
        auto has_mod_folders = [&](const std::string& prefix) {
            for (const Entry* e : files)
                for (const char* sub : {"files/", "cars/", "makers/", "textures/"})
                    if (starts_with(e->name, prefix + sub)) return true;
            return false;
        };
        std::set<std::string> tops;
        for (const Entry* e : files)
            if (size_t slash = e->name.find('/'); slash != std::string::npos) tops.insert(e->name.substr(0, slash + 1));
        if (has_mod_folders("")) roots.push_back("");
        else if (tops.size() == 1 && has_mod_folders(*tops.begin())) roots.push_back(*tops.begin());
    }
    if (roots.empty()) {
        fail(error, "no mod in this archive (a mod is a folder with a mod.ini; see the modding guide)");
        return std::nullopt;
    }

    ModArchivePlan plan;
    std::set<std::string> ids;
    for (const std::string& prefix : roots) {
        ArchiveMod m;
        m.prefix = prefix;
        for (const Entry* e : files)
            if (e->name.size() > prefix.size() && starts_with(e->name, prefix)) {
                m.files.push_back(e);
                m.bytes += e->size;
                if (to_lower(e->name.substr(prefix.size())) == "mod.ini") {
                    std::optional<Bytes> data = zip.read_all(*e, 64 * 1024, error);
                    if (!data) return std::nullopt;
                    IniDocument ini = IniDocument::parse(std::string_view(reinterpret_cast<const char*>(data->data()), data->size()));
                    m.name = ini.get("mod", "name").value_or("");
                    m.version = ini.get("mod", "version").value_or("");
                }
            }
        // The id: the mod's folder, or for a mod at the top the archive's name, or its own.
        std::string id;
        if (!prefix.empty()) {
            id = mod_id_from(base_name(std::string_view(prefix).substr(0, prefix.size() - 1)));
        } else if (std::string lower = to_lower(archive_name); lower.size() > 4 && lower.ends_with(".zip")) {
            id = mod_id_from(archive_name.substr(0, archive_name.size() - 4));
        }
        if (id.empty()) id = mod_id_from(m.name);
        if (id.empty()) id = "mod";
        const std::string base = id;
        for (int k = 2; ids.count(id); ++k) id = base + "-" + std::to_string(k);
        ids.insert(id);
        m.id = id;
        if (m.name.empty()) m.name = id;
        const std::string dest = mods_dir + "/" + id;
        if (host_path_exists(dest)) {
            m.replaces = true;
            if (auto old = IniDocument::load(dest + "/mod.ini")) {
                m.installed_name = old->get("mod", "name").value_or("");
                m.installed_version = old->get("mod", "version").value_or("");
            }
        }
        plan.bytes += m.bytes;
        plan.mods.push_back(std::move(m));
    }
    return plan;
}

// --- installing and removing ---

bool install_mod_archive(const ZipArchive& zip, const ModArchivePlan& plan, const std::string& mods_dir,
                         const std::string& staging_dir, ModInstallProgress* progress, std::string* error) {
    std::error_code ec;
    const fs::path staging = fs_path(staging_dir), mods = fs_path(mods_dir);
    fs::remove_all(staging, ec); // what an earlier attempt left
    fs::create_directories(staging, ec);
    if (ec) return fail(error, "cannot create " + staging_dir + ": " + ec.message());
    struct Cleanup {
        const fs::path& dir;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(dir, e);
        }
    } cleanup{staging};
    fs::create_directories(mods, ec);
    const fs::space_info space = fs::space(staging, ec);
    if (!ec && space.available < plan.bytes + kSpareBytes)
        return fail(error, "not enough free space: the mods need " + mb(plan.bytes) + ", " + mb(space.available) + " is free");
    if (progress) progress->done = 0, progress->total = plan.bytes;

    for (const ArchiveMod& m : plan.mods) {
        const fs::path dir = staging / fs_path(m.id);
        fs::create_directories(dir, ec);
        for (const ZipArchive::Entry* e : m.files) {
            std::string rel = e->name.substr(m.prefix.size());
            if (to_lower(rel) == "mod.ini") rel = "mod.ini"; // the name the mod scan reads
            const fs::path out = dir / fs_path(rel);
            fs::create_directories(out.parent_path(), ec);
            SDL_IOStream* io = SDL_IOFromFile(utf8(out).c_str(), "wb");
            if (!io) return fail(error, "cannot write " + rel + ": " + SDL_GetError());
            bool write_failed = false;
            std::string err;
            const bool ok = zip.read(
                *e,
                [&](const u8* p, size_t n) {
                    if (SDL_WriteIO(io, p, n) != n) {
                        write_failed = true;
                        return false;
                    }
                    if (progress) progress->done += n;
                    return true;
                },
                &err);
            const bool closed = SDL_CloseIO(io);
            if (write_failed || (ok && !closed)) return fail(error, "cannot write " + rel + " (is the storage full?)");
            if (!ok) return fail(error, err);
        }
    }
    // Into place: an installed mod of the same id moves aside first, and back if the new one
    // cannot move in.
    for (const ArchiveMod& m : plan.mods) {
        const fs::path dest = mods / fs_path(m.id), fresh = staging / fs_path(m.id), old = staging / fs_path(".old-" + m.id);
        const bool had = fs::exists(fs::symlink_status(dest, ec));
        if (had) {
            fs::rename(dest, old, ec);
            if (ec) return fail(error, "cannot replace " + m.id + ": " + ec.message());
        }
        fs::rename(fresh, dest, ec);
        if (ec) {
            std::error_code back;
            if (had) fs::rename(old, dest, back);
            return fail(error, "cannot install " + m.id + ": " + ec.message());
        }
        LOG_INFO("Installed mod '%s' (%s): %zu files, %llu bytes%s", m.name.c_str(), m.id.c_str(), m.files.size(),
                 (unsigned long long)m.bytes, had ? ", replacing the installed one" : "");
    }
    return true;
}

bool remove_mod_folder(const std::string& mods_dir, const std::string& id, std::string* error) {
    if (id.empty() || id == "." || id == ".." || id.find_first_of("/\\:") != std::string::npos)
        return fail(error, "not a mod folder name: " + id);
    std::error_code ec;
    // A symbolic link (a mod kept elsewhere) is removed itself, never what it points to.
    fs::remove_all(fs_path(mods_dir) / fs_path(id), ec);
    if (ec) return fail(error, "cannot remove " + id + ": " + ec.message());
    LOG_INFO("Removed mod folder %s", id.c_str());
    return true;
}

} // namespace gt3r
