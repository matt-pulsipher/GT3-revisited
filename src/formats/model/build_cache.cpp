#include "formats/model/build_cache.h"
#include "base/texel_hash.h"

namespace gt3r::gtm1 {

const Gtm1& BuildCache::model(ByteSpan data) {
    std::unique_ptr<Gtm1>& m = models_[{data.data(), data.size()}];
    if (!m) m = std::make_unique<Gtm1>(Gtm1::parse(data));
    return *m;
}

const DecodedPacket& BuildCache::packet(const Gtm1& g, const Packet& pk, bool prelit) {
    const auto [it, fresh] = packets_.try_emplace({g.data.data() + pk.offset, pk.qwc, prelit});
    if (fresh) it->second = g.decode_packet(pk, prelit);
    return it->second;
}

const scene::Model& BuildCache::wheel(ByteSpan data, int lod, bool reflection, const std::function<scene::Model()>& build) {
    const std::array<u64, 2> hash = bytes_hash(data);
    const std::tuple<u64, u64, int, bool> key{hash[0], hash[1], lod, reflection};
    auto it = wheels_.find(key);
    if (it == wheels_.end()) it = wheels_.emplace(key, build()).first;
    return it->second;
}

int BuildCache::texture_set(ByteSpan tex1, std::vector<scene::Texture>& out, const std::string& prefix, int paint,
                            ClutTextures* clut, bool use_pack, const ComputedPaint* computed, const texture_pack::Pack* livery,
                            bool gs_mip_levels) {
    if (computed || livery)
        return decode_texture_set(tex1, out, prefix, paint, clut, use_pack, computed, livery, gs_mip_levels);
    constexpr size_t kSmall = 64 * 1024; // (quick to hash: a tyre's, whose two files hold the same picture)
    const std::array<u64, 2> of = tex1.size() <= kSmall
                                      ? bytes_hash(tex1)
                                      : std::array<u64, 2>{u64(reinterpret_cast<uintptr_t>(tex1.data())), u64(tex1.size())};
    const std::tuple<u64, u64, int, bool, bool> key{of[0], of[1], paint, use_pack, gs_mip_levels};
    auto it = sets_.find(key);
    if (it == sets_.end()) {
        Set s;
        decode_texture_set(tex1, s.textures, "", paint, &s.clut, use_pack, nullptr, nullptr, gs_mip_levels);
        it = sets_.emplace(key, std::move(s)).first;
    }
    const int first = int(out.size());
    for (size_t i = 0; i < it->second.textures.size(); ++i) {
        out.push_back(it->second.textures[i]);
        out.back().name = prefix + std::to_string(i);
    }
    if (clut && !it->second.clut.empty()) *clut = it->second.clut;
    return first;
}

void BuildCache::colour_cut(scene::Texture& t, bool wrap_u, bool wrap_v) {
    const std::array<u64, 2> hash = texel_hash(t.rgba.data(), t.rgba.size());
    const auto [it, fresh] = cuts_.try_emplace({hash[0], hash[1], t.width, t.height, wrap_u, wrap_v});
    if (!fresh) {
        t.rgba = it->second;
        return;
    }
    colour_cut_texels(t, wrap_u, wrap_v);
    it->second = t.rgba;
}

const std::optional<Bytes>& BuildCache::stand_in(const std::function<std::optional<Bytes>()>& read) {
    if (!stand_in_) stand_in_ = read();
    return *stand_in_;
}

} // namespace gt3r::gtm1
