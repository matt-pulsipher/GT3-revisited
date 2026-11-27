#pragma once

// What the builds of a car (BuildCache) would otherwise work out again.

#include "formats/model/gtm1.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <tuple>

namespace gt3r::gtm1 {

// What the builds of one car would otherwise work out again: parsed models, decoded texture sets, coloured cut-outs and
// the stand-in rim's file. The bytes they come from must live as long as the cache. One thread's.
class BuildCache {
public:
    // Gtm1::parse(data)
    const Gtm1& model(ByteSpan data);
    // g.decode_packet(pk, prelit): the passes, a model's LODs and its brake variant draw the same packets again.
    const DecodedPacket& packet(const Gtm1& g, const Packet& pk, bool prelit);
    // A wheel's model for `lod`, made by `build` the first time its bytes are seen (a car's race rim is often its
    // menu one, from another file).
    const scene::Model& wheel(ByteSpan data, int lod, bool reflection, const std::function<scene::Model()>& build);
    // decode_texture_set with the same arguments (a small set, a tyre's, known by its bytes, wherever they are from). A
    // computed colour or a livery: decoded each time.
    int texture_set(ByteSpan tex1, std::vector<scene::Texture>& out, const std::string& prefix, int paint, ClutTextures* clut,
                    bool use_pack, const ComputedPaint* computed, const texture_pack::Pack* livery, bool gs_mip_levels);
    // colour_cut_texels
    void colour_cut(scene::Texture& t, bool wrap_u, bool wrap_v);
    // `read()`, the first time
    const std::optional<Bytes>& stand_in(const std::function<std::optional<Bytes>()>& read);

private:
    struct Set {
        std::vector<scene::Texture> textures; // (named by their place in the set)
        ClutTextures clut;
    };
    std::map<std::pair<const u8*, size_t>, std::unique_ptr<Gtm1>> models_;
    // (bytes: where they are, or for a small set what they hold)
    std::map<std::tuple<u64, u64, int, bool, bool>, Set> sets_;
    std::map<std::tuple<const u8*, u32, bool>, DecodedPacket> packets_;
    std::map<std::tuple<u64, u64, int, bool>, scene::Model> wheels_;
    std::map<std::tuple<u64, u64, u32, u32, bool, bool>, std::vector<u32>> cuts_; // by the texels before colouring
    std::optional<std::optional<Bytes>> stand_in_;
};

} // namespace gt3r::gtm1
