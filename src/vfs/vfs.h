#pragma once

#include "disc/volume.h"
#include "formats/texture_pack.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {

class IniDocument;

// A mod is a directory under <user data>/mods/<id>/ containing:
//   mod.ini   [mod] name, version, author, description, priority, discs
//   files/    files mirroring GT3.VOL paths; each overrides or adds a file
//   cars/     new car definitions (*.ini, see game/data/mod_cars.h)
//   textures/ replacement textures (PNGs named by key, formats/texture_pack.h)
// See docs/MODDING.md.
struct ModInfo {
    std::string id; // directory name
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    int priority = 0; // higher wins
    bool enabled = true; // the player's choice for this disc ([mods.<disc key>], else [mods])
    // mod.ini's discs: the discs the mod is for (disc keys or serials: "eu, SCEF-50294"); empty: every disc.
    std::vector<std::string> discs;
    bool for_disc = true; // the open disc is one of `discs`
    bool active() const { return enabled && for_disc; }
    std::string root; // absolute path to the mod directory
    std::vector<std::string> files; // VOL-relative paths provided
    std::vector<std::string> cars;  // host paths of cars/*.ini (game/data/mod_cars.h)
    std::vector<std::string> makers; // host paths of makers/*.ini
    std::vector<std::pair<u64, std::string>> textures; // textures/: key and host path of each picture
};

// Layered read-only filesystem: enabled mods (by priority) over GT3.VOL.
// All engine asset loads go through here, so every asset is moddable.
class Vfs {
public:
    explicit Vfs(Volume* base);

    // Scans `mods_dir` for mods; the player's choices come from `config`: [mods.<disc key>] for
    // this disc, else [mods] (the choices before discs had their own).
    void scan_mods(const std::string& mods_dir, const IniDocument* config);
    const std::vector<ModInfo>& mods() const { return mods_; }
    void set_mod_enabled(const std::string& id, bool enabled);

    bool exists(const std::string& path) const;
    std::optional<Bytes> read(const std::string& path) const;

    // Random-access stream over a file, for large assets (movies, music) that
    // should not be loaded whole. Disc entries stored uncompressed and mod
    // files are read in place; compressed disc entries are inflated once.
    struct Stream {
        u64 size = 0;
        std::function<void(u64 offset, void* out, size_t len)> read; // throws on error
    };
    std::optional<Stream> open_stream(const std::string& path) const;
    // "disc" or the providing mod's id; empty if missing.
    std::string source_of(const std::string& path) const;
    // The enabled mods' replacement textures, the higher priority's picture winning a key; null
    // when there are none.
    std::shared_ptr<const texture_pack::Pack> textures() const { return textures_; }
    // Merged listing (mod-added files included). Dirs end with '/'.
    std::vector<std::string> list_dir(const std::string& dir) const;

    Volume* base() const { return base_; }
    // The disc's regional data set (NTSC-U when there is no disc).
    RegionData region() const { return {base_ ? base_->region() : Region::America}; }
    // Where the disc keeps its saves, replays and mod choices (DiscInfo::key; "us" when there is no disc).
    std::string disc_key() const { return base_ ? base_->disc_key() : region_key(Region::America); }
    // The config section of this disc's mod choices: "mods.<disc key>".
    std::string mods_section() const { return "mods." + disc_key(); }
    // The game's language (disc/region.h), which the app sets from its config; the region's
    // own (RegionData::default_language) until then.
    Language language() const { return language_ ? *language_ : region().default_language(); }
    void set_language(Language l) { language_ = l; }

    // Virtual file listing the enabled mods' car definitions, one "<mod id>\t<host
    // path>" line each (ascending priority), so loaders that only get a reader
    // (game/data/mod_cars.h) still see mod cars.
    static constexpr const char* kModCarsList = "mods/.cars";
    // Virtual file: the disc's packed program (CORE.GT3, Volume::read_program), for loaders
    // that read a reference table from it. Not moddable.
    static constexpr const char* kProgramFile = "disc/.core.gt3";
    // Virtual file: the disc's region key ("jp", "us", "eu"; region_key), for loaders that
    // only get a reader. Not moddable.
    static constexpr const char* kRegionFile = "disc/.region";
    // Virtual file: the game's language code ("US", "BR", ...; language()). Not moddable.
    static constexpr const char* kLanguageFile = "disc/.language";
    // Virtual file: the disc key (disc_key()). Not moddable.
    static constexpr const char* kDiscKeyFile = "disc/.key";

private:
    void rebuild_index();
    Volume* base_;
    std::optional<Language> language_;
    std::vector<ModInfo> mods_;
    std::map<std::string, std::string> overrides_; // vol path -> host path
    std::map<std::string, std::string> override_source_; // vol path -> mod id
    std::shared_ptr<const texture_pack::Pack> textures_;
};

std::string normalize_vfs_path(std::string_view path);

// The region of a reader's disc (its Vfs::kRegionFile); NTSC-U when it has none.
RegionData region_data(const std::function<std::optional<Bytes>(const std::string&)>& read);
// The game's language through a reader (Vfs::kLanguageFile), else the region's own.
Language language_of(const std::function<std::optional<Bytes>(const std::string&)>& read);
// The disc key through a reader (Vfs::kDiscKeyFile), else its region's key.
std::string disc_key_of(const std::function<std::optional<Bytes>(const std::string&)>& read);
// A mod.ini discs list names the disc (its key or serial, either way written; any case).
bool mod_for_disc(const std::vector<std::string>& discs, std::string_view key, std::string_view serial);

// A course file's path: the one the game reads (RegionData::course_path in the language),
// else another folder's that has it; "" when none does.
std::string course_file_path(const Vfs& vfs, std::string_view label);

} // namespace gt3r
