#pragma once

// Installing mods from a .zip (the Mods screen's "Install from .zip"; docs/MODDING.md
// "Installing mods"). On Android the mods folder is in the app's private storage, where no file
// manager reaches, so a picked archive is the way in there; desktops get the same.
//
// An archive holds one mod or several. A mod is a folder with a mod.ini (the shallowest one on
// each branch); an archive without any mod.ini is one mod when its top level, or its only top
// folder, has files/, cars/, makers/ or textures/. Each mod installs as mods/<id>/: <id> is its folder's
// name, or for a mod at the top of the archive the archive's own name (else its mod.ini name).
// Anything outside the mods (READMEs beside them, __MACOSX/) is left out.
//
// Archives come from anywhere, so every entry path must be a plain relative path (no "..",
// absolute or drive paths), symbolic links are skipped, only stored and Deflate entries are
// read, sizes and CRC-32s are checked while unpacking, and a mod replaces mods/<id>/ only once
// it has unpacked completely (into a staging folder first).

#include "base/bytes.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gt3r {

class File;

// A .zip read through its central directory (ZIP64 included), from a path or an Android
// content:// URI (base/file.h).
class ZipArchive {
public:
    struct Entry {
        std::string name; // '/' separated (backslashes from Windows tools turned into '/')
        u64 local_offset = 0, compressed = 0, size = 0;
        u32 crc = 0;
        u16 method = 0, flags = 0;
        bool dir = false, symlink = false;
    };

    static std::unique_ptr<ZipArchive> open(const std::string& path, std::string* error = nullptr);
    ~ZipArchive();

    const std::vector<Entry>& entries() const { return entries_; }
    // Streams an entry's contents to `sink` in pieces (a false return stops with an error). The
    // size and CRC-32 are checked; false with *error on any problem.
    bool read(const Entry& e, const std::function<bool(const u8*, size_t)>& sink, std::string* error = nullptr) const;
    // A small entry whole (at most `max_size` bytes).
    std::optional<Bytes> read_all(const Entry& e, size_t max_size, std::string* error = nullptr) const;

private:
    explicit ZipArchive(std::unique_ptr<File> f);
    bool parse(std::string* error);

    std::unique_ptr<File> file_;
    std::vector<Entry> entries_;
};

// One mod found in an archive.
struct ArchiveMod {
    std::string id;     // installs as mods/<id>/
    std::string prefix; // its folder inside the archive: "" or "dir/"
    std::string name;   // its mod.ini's [mod] name (the id when there is none)
    std::string version;
    std::vector<const ZipArchive::Entry*> files; // with paths below `prefix`
    u64 bytes = 0;
    bool replaces = false; // mods/<id>/ is there already
    std::string installed_name, installed_version;
};

struct ModArchivePlan {
    std::vector<ArchiveMod> mods;
    u64 bytes = 0; // to unpack, all mods
};

// The mods in `zip` and where they go. `archive_name` is the archive's file name (for the id of
// a mod at its top level; may be empty). nullopt with *error when it holds no mod or an unsafe
// path.
std::optional<ModArchivePlan> plan_mod_install(const ZipArchive& zip, std::string_view archive_name,
                                               const std::string& mods_dir, std::string* error = nullptr);

struct ModInstallProgress {
    std::atomic<u64> done{0}, total{0}; // bytes unpacked / to unpack
};

// Unpacks every planned mod into `staging_dir` (wiped first and removed after), then moves each
// into `mods_dir`, replacing a mod of the same id. On failure nothing in `mods_dir` changes
// (except mods moved in before a later one failed to move). `progress` may be null.
bool install_mod_archive(const ZipArchive& zip, const ModArchivePlan& plan, const std::string& mods_dir,
                         const std::string& staging_dir, ModInstallProgress* progress, std::string* error = nullptr);

// Deletes mods/<id>/ and everything in it.
bool remove_mod_folder(const std::string& mods_dir, const std::string& id, std::string* error = nullptr);

// A folder name made from `s`: letters, digits, '.', '_' and '-' only, at most 48 characters,
// no leading dot; empty when nothing usable is left. (Exposed for tests.)
std::string mod_id_from(std::string_view s);
// A path an archive entry may have (exposed for tests).
bool safe_archive_path(std::string_view path);

} // namespace gt3r
