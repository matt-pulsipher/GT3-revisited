#pragma once

// Texture packs (docs/MODDING.md "Texture packs"): PNG pictures, any size, that replace the
// game's textures as the engine decodes them, from the enabled mods' textures/ folders; and the
// texture dump, which saves the game's textures under the names a pack uses.
//
// A texture is named by its key, made from its full decode: the whole TEX0 size, the CLUT
// offset (CSA) honoured, CLUT animations at their reset state and a car paint applied, which is
// what gtm1::decode_texture_set gives. Decoders that take a texture another way (Tex1::decode
// keeps the REGION_CLAMP rectangle only) look it up by the same key, so one picture replaces a
// texture wherever the game shows it, and the copies the engine derives from it (alpha-tested
// cut-outs, masks) are made from the picture. A replaced texture keeps its place and size on
// screen: only its pixels change.

#include "base/bytes.h"
#include "formats/tex1.h"
#include "scene/model.h"

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gt3r::texture_pack {

// FNV-1a (64-bit) over 32-bit words, the width, the height, then the texels in rows (RGBA8,
// R in the low byte), finished with SplitMix64's mix.
u64 key_of(u32 width, u32 height, const u32* rgba);
// 16 lower-case hex digits.
std::string key_name(u64 key);
// The key a file name ends in: "<16 hex digits>.png", or any name ending in '-', '_', '.' or a
// space and the 16 digits ("tyre-0123456789abcdef.png"). Case is ignored.
std::optional<u64> key_in_file_name(std::string_view name);

struct Image {
    u32 width = 0, height = 0;
    std::vector<u32> rgba; // RGBA8, row-major
};

// Replacement pictures by key.
class Pack {
public:
    // Largest picture side that loads (bigger ones are skipped with a warning).
    static constexpr u32 kMaxSide = 4096;

    // `path` replaces `key` unless an earlier file already does; true when added. `source`
    // names where it came from (a mod's id).
    bool add_file(u64 key, std::string path, std::string source = {});
    // Every PNG under `dir` (any depth) named by a key; returns how many it added.
    size_t add_folder(const std::string& dir, const std::string& source = {});
    size_t size() const { return files_.size(); }
    bool contains(u64 key) const { return files_.count(key) != 0; }
    // The `source` of the file replacing `key`; empty when none does.
    std::string source_of(u64 key) const;
    // The picture that replaces `key`, decoded on first use and kept while it is among the
    // recently used; null when there is none or its file does not load.
    std::shared_ptr<const Image> find(u64 key) const;

private:
    struct File {
        std::string path, source;
    };
    std::unordered_map<u64, File> files_;
    // Decoded pictures, most recently used first, up to kCacheBytes (one always stays).
    static constexpr u64 kCacheBytes = 128ull << 20;
    struct Cached {
        std::shared_ptr<const Image> image;
        std::list<u64>::iterator use;
    };
    mutable std::mutex mutex_;
    mutable std::unordered_map<u64, Cached> cache_;
    mutable std::list<u64> uses_;
    mutable u64 cache_bytes_ = 0;
    mutable std::unordered_set<u64> broken_; // files that did not load (warned once)
};

// The PNGs under `dir` (any depth) whose names end in a key, sorted by path.
std::vector<std::pair<u64, std::string>> pictures_in(const std::string& dir);

// The pack the decoders replace textures from. None by default: tools and tests see the disc's
// own textures unless they set one.
void set_pack(std::shared_ptr<const Pack> pack);
std::shared_ptr<const Pack> current_pack();

// The replacement for a texture decoded in full (see above), or null.
std::shared_ptr<const Image> replacement(u32 width, u32 height, const u32* rgba);
// The replacement for texture `index` of Tex1 set `tex1` (data from its header on), cut to the
// part `region` of the texture that the caller decoded (in the texture's texels), or nullopt.
// While a pack is set this decodes the set in full once to find the keys.
std::optional<Image> replacement(ByteSpan tex1, size_t index, const Tex1::Region& region);
// The same, cut the way Tex1::decode cuts texture `index` (Tex1::region).
std::optional<Image> replacement(ByteSpan tex1, size_t index);
// Puts that picture in `img` (texture `index` of `tex1` from Tex1::decode); true when it did.
bool replace_decoded(ByteSpan tex1, size_t index, Tex1::Image& img);

// The keys of a Tex1 set's textures (gtm1::decode_texture_set with `paint`).
std::vector<u64> keys_of_set(ByteSpan tex1, int paint = 0);

// A texture of a game file, as the dump and PCSX2 pack imports read them (for_each_texture).
struct FileTexture {
    std::string name; // "<set>-<texture>[-paint<n>]"; a menu page's pictures are sets "img<n>"
    ByteSpan set;     // its Tex1 set
    size_t index = 0; // in the set
    int paint = 0;
    const scene::Texture* texture = nullptr; // its full decode (the key's)
    u64 key = 0;
};
// Calls `each` for every texture of game file `path` (contents `data`): a menu page's pictures
// (the first texture of each, which the page shows), or every embedded Tex1 set's textures (models,
// courses, fonts...) in every car paint, a paint only where it changes the texture.
void for_each_texture(const std::string& path, ByteSpan data, const std::function<void(const FileTexture&)>& each);

// Saves game files' textures as PNGs named by key, each key once: under the root, a folder per
// game file, "<set>-<texture>[-paint<n>]-<key>.png". A menu page's pictures are its sets; in
// other files every embedded Tex1 set counts (models, courses, fonts...), with every car paint.
class Dump {
public:
    explicit Dump(std::string root) : root_(std::move(root)) {}
    // The textures of game file `path` (contents `data`); returns how many it saved (keys new
    // to this dump). False in `*ok` when a picture could not be written.
    size_t add_file(const std::string& path, ByteSpan data, bool* ok = nullptr);
    // Writes <root>/textures.txt: per key, the files and textures that use it. False on failure.
    bool finish() const;
    size_t saved() const { return saved_; }
    size_t textures() const { return uses_.size(); }

private:
    std::string root_;
    size_t saved_ = 0;
    std::unordered_map<u64, std::vector<std::string>> uses_; // key -> "<file>\t<texture>"
    std::vector<u64> order_;                                 // keys in the order first seen
};

// Whether the dump reads a game file: not movies, music or sound streams (no textures, large).
bool dump_reads(const std::string& path);

} // namespace gt3r::texture_pack
