#pragma once

// A texture's texels (scene::Texture::rgba): copies share one buffer until one of them is written, so a texture handed
// from a build cache to every model that draws it is never copied texel by texel; and the buffer's content hash
// (texel_hash, the GPU's key for one texture of the same texels) is made once, whichever copy asks.
//
// It reads like a std::vector<u32>. Any non-const access (data(), [], begin(), edit() ...) makes the buffer this
// object's own first (copying a shared one) and forgets its hash: get what is only read through a const reference or
// vec(), and keep no pointer from a write access past a copy of the object.

#include "base/bytes.h"
#include "base/texel_hash.h"

#include <array>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace gt3r::scene {

class Texels {
public:
    Texels() = default;
    Texels(std::vector<u32> v) : b_(std::make_shared<Block>(std::move(v))) {} // (implicit: assigned as a vector)
    Texels(size_t n, u32 value) : Texels(std::vector<u32>(n, value)) {}
    Texels(std::initializer_list<u32> l) : Texels(std::vector<u32>(l)) {}

    size_t size() const { return b_ ? b_->v.size() : 0; }
    bool empty() const { return size() == 0; }
    const std::vector<u32>& vec() const {
        static const std::vector<u32> none;
        return b_ ? b_->v : none;
    }
    operator const std::vector<u32>&() const { return vec(); }
    const u32* data() const { return vec().data(); }
    const u32* begin() const { return data(); }
    const u32* end() const { return data() + size(); }
    const u32& operator[](size_t i) const { return b_->v[i]; }
    const u32& front() const { return b_->v.front(); }
    const u32& back() const { return b_->v.back(); }
    friend bool operator==(const Texels& a, const Texels& b) { return a.b_ == b.b_ || a.vec() == b.vec(); }
    friend bool operator==(const Texels& a, const std::vector<u32>& b) { return a.vec() == b; }

    // texel_hash of the texels, made once for the buffer.
    std::array<u64, 2> hash() const {
        if (!b_) return texel_hash(nullptr, 0);
        std::lock_guard lock(b_->mutex);
        if (!b_->hashed) b_->hash = texel_hash(b_->v.data(), b_->v.size()), b_->hashed = true;
        return b_->hash;
    }

    // The texels to write: this object's own. (Texels that threads share are also held by the build's caches meanwhile, so
    // the count below is never 1 for them.)
    std::vector<u32>& edit() {
        if (!b_) b_ = std::make_shared<Block>(std::vector<u32>{});
        else if (b_.use_count() > 1) b_ = std::make_shared<Block>(b_->v);
        else b_->hashed = false;
        return b_->v;
    }
    u32* data() { return edit().data(); }
    u32* begin() { return edit().data(); }
    u32* end() {
        std::vector<u32>& v = edit();
        return v.data() + v.size();
    }
    u32& operator[](size_t i) { return edit()[i]; }
    u32& front() { return edit().front(); }
    u32& back() { return edit().back(); }
    void resize(size_t n, u32 value = 0) { edit().resize(n, value); }
    void assign(size_t n, u32 value) { edit().assign(n, value); }
    template <class It>
    void assign(It first, It last) {
        edit().assign(first, last);
    }
    void reserve(size_t n) { edit().reserve(n); }
    void push_back(u32 value) { edit().push_back(value); }
    void clear() { b_.reset(); }

private:
    struct Block {
        explicit Block(std::vector<u32> texels) : v(std::move(texels)) {}
        std::vector<u32> v;
        std::mutex mutex; // (the hash, asked for from any thread)
        bool hashed = false;
        std::array<u64, 2> hash{};
    };
    std::shared_ptr<Block> b_;
};

} // namespace gt3r::scene
