#pragma once

// What the builds of a car (BuildCache), and of the cars built together (SharedBuilds), would otherwise work out again.

#include "formats/model/gtm1.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>

namespace gt3r::gtm1 {

// What the cars built together (a race's field) would otherwise each work out again: what their bytes alone decide,
// whichever car asks (wheel models, small texture sets, coloured cut-outs), so none of it depends on where a file was
// read to. The texture pack must stay the same. Any thread's: the first to ask for a piece makes it, the others wait.
class SharedBuilds {
public:
    template <class Key, class Value>
    class Once {
    public:
        // The value under `key`, `make()`'s the first time (again if that one threw). `make()` runs under the key's lock:
        // it may ask another Once (wheels: small_sets, cuts), never this one.
        template <class Make>
        const Value& get(const Key& key, const Make& make) {
            Slot* slot;
            {
                std::lock_guard lock(mutex_);
                slot = &slots_[key]; // (stays where it is as others are added)
            }
            std::lock_guard lock(slot->mutex);
            if (!slot->value) slot->value.emplace(make());
            return *slot->value;
        }
        void clear() { slots_.clear(); } // (none being asked for)

    private:
        struct Slot {
            std::mutex mutex;
            std::optional<Value> value;
        };
        std::mutex mutex_;
        std::map<Key, Slot> slots_;
    };
    struct Set {
        std::vector<scene::Texture> textures; // (named by their place in the set)
        ClutTextures clut;
    };
    // By the bytes' hash (and the LOD, reflections).
    Once<std::tuple<u64, u64, int, bool>, scene::Model> wheels;
    // By the bytes' hash (and the paint, the texture pack's use, the mips).
    Once<std::tuple<u64, u64, int, bool, bool>, Set> small_sets;
    // By the hash of the texels before colouring (and the size, the wrapping).
    Once<std::tuple<u64, u64, u32, u32, bool, bool>, std::vector<u32>> cuts;

    void clear() { wheels.clear(), small_sets.clear(), cuts.clear(); }
};

// What the builds of one car would otherwise work out again: parsed models, decoded packets and texture sets and the
// stand-in rim's file; with the other cars', through `shared`, what SharedBuilds holds. The bytes they come from must
// live as long as the cache. One thread's.
class BuildCache {
public:
    // `shared`: the field's (none: this car's own).
    explicit BuildCache(SharedBuilds* shared = nullptr) : shared_(shared) {
        if (!shared_) shared_ = (own_ = std::make_unique<SharedBuilds>()).get();
    }

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
    std::unique_ptr<SharedBuilds> own_;
    SharedBuilds* shared_;
    std::map<std::pair<const u8*, size_t>, std::unique_ptr<Gtm1>> models_;
    std::map<std::pair<const u8*, size_t>, std::array<u64, 2>> wheel_hashes_; // (each wheel's bytes hashed once)
    std::map<std::tuple<u64, u64, int, bool, bool>, SharedBuilds::Set> sets_; // the larger sets, by where their bytes are
    std::map<std::tuple<const u8*, u32, bool>, DecodedPacket> packets_;
    std::optional<std::optional<Bytes>> stand_in_;
};

} // namespace gt3r::gtm1
