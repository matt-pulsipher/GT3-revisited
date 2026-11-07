#pragma once

// PCSX2 texture packs (docs/MODDING.md "PCSX2 texture packs"): the pictures of a pack made for
// PCSX2's texture replacement, converted into a mod's textures/ folder (formats/texture_pack.h).
//
// PCSX2 names a picture by hashes of the console's video memory, which say nothing about the
// game's files. Its dumps (textures/<serial>/dumps) hold each picture's original under the same
// name, though, so each is recognised by content: the game's textures as PCSX2 cuts them (the
// REGION_CLAMP part, Tex1::region; PCSX2's names carry its size only), with PCSX2's alpha
// (0x80 = opaque) widened the two ways the game's decode does, or opaque for textures whose
// alpha the game ignores. A recognised picture is saved under the texture's key; one that covers
// part of a texture is put into a picture of the whole texture, the rest from the game's own.

#include "formats/texture_pack.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gt3r::texture_pack {

class Pcsx2Index {
public:
    // The textures of game file `path` (contents `data`), as for_each_texture reads them.
    void add_file(const std::string& path, ByteSpan data);
    size_t size() const { return by_part_.size(); }

    struct Match {
        u64 key = 0;
        std::string path, name;    // the game file and FileTexture::name
        u32 width = 0, height = 0; // the whole texture
        Tex1::Region part;         // what PCSX2 dumps of it
        int alpha = 0;             // how PCSX2's alpha a reads: 0 min(2a, 255), 1 (255a + 64) / 128, 2 opaque
    };
    // The texture a PCSX2 dump shows (its pixels as PCSX2 saved them), or nullopt.
    std::optional<Match> find(u32 width, u32 height, const std::vector<u32>& rgba) const;

private:
    std::unordered_map<u64, Match> by_part_; // key_of(the part's pixels) -> texture
};

// A disc serial as PCSX2 names its folders: "PBPX_955.03" -> "PBPX-95503".
std::string pcsx2_serial(const std::string& serial);
// Where the game saves a disc's textures, in the user data folder: "texture-dump/<that serial>".
std::string dump_folder(const std::string& serial);

// PCSX2's alpha as the game's decode widens it (Match::alpha).
u32 widen_pcsx2_alpha(u32 rgba, int alpha);

struct Pcsx2Import {
    size_t pictures = 0; // PNGs in replacements/ (mipmap levels left out)
    size_t converted = 0;
    std::vector<std::string> no_dump;    // no dump of the same name
    std::vector<std::string> unmatched;  // the dump is none of this disc's textures (as decoded here)
    std::vector<std::string> failed;     // could not be read or written; not PNG (PCSX2 also takes DDS)
};

// Converts the pictures in `pack`/replacements (any depth; `pack` is PCSX2's textures/<serial>
// folder, with dumps/ beside it) into `textures_dir`, "<PCSX2 name>-<key>.png" each. `read` gives a
// game file's contents (for pictures of part of a texture). `progress` gets (done, total).
Pcsx2Import import_pcsx2_pack(const std::string& pack, const Pcsx2Index& index, const std::string& textures_dir,
                              const std::function<std::optional<Bytes>(const std::string&)>& read,
                              const std::function<void(size_t, size_t)>& progress = {});

} // namespace gt3r::texture_pack
