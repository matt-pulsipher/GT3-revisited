// A texture's texels (scene::Texels, src/scene/texels.h): copies share one buffer until one is written, a write is the
// writer's own, and the hash made for a buffer is forgotten when it is written. And the content hash that tells
// contents apart (base/texel_hash.h).

#include "test_framework.h"

#include "base/texel_hash.h"
#include "scene/texels.h"

#include <array>
#include <functional>
#include <set>
#include <utility>
#include <vector>

using namespace gt3r;
using scene::Texels;

namespace {

// Reading through a const reference leaves a shared buffer shared (a non-const access takes it).
const u32* buffer(const Texels& t) { return t.data(); }

std::array<u64, 2> hash_of(const std::vector<u32>& v) { return texel_hash(v.data(), v.size()); }

const std::vector<u32> kTexels{1, 2, 3, 4, 5};

} // namespace

TEST(texels_copies_share_one_buffer) {
    const Texels a(kTexels);
    const Texels b = a;
    CHECK(buffer(a) == buffer(b));
    CHECK(a == b);
    CHECK(a == kTexels);
    CHECK(b[4] == 5);
    CHECK(buffer(a) == buffer(b)); // (still, after reading)
}

// Each way to write: the writer gets its own buffer, the other copy keeps the texels and its hash, and each copy's
// hash is that of what it now holds.
TEST(texels_a_write_is_the_writers_own) {
    const std::vector<std::function<void(Texels&)>> writes{
        [](Texels& t) { t[1] = 9; },
        [](Texels& t) { t.data()[0] = 9; },
        [](Texels& t) { *t.begin() = 9; },
        [](Texels& t) { *(t.end() - 1) = 9; },
        [](Texels& t) { t.front() = 9; },
        [](Texels& t) { t.back() = 9; },
        [](Texels& t) { t.edit()[2] = 9; },
        [](Texels& t) { t.resize(7, 9); },
        [](Texels& t) { t.assign(3, 9); },
        [](Texels& t) {
            const u32 other[] = {9, 8};
            t.assign(other, other + 2);
        },
        [](Texels& t) { t.push_back(9); },
        [](Texels& t) { t.clear(); },
    };
    for (const auto& write : writes) {
        const Texels kept(kTexels);
        Texels written = kept;
        CHECK(written.hash() == hash_of(kTexels)); // (made while shared)
        write(written);
        CHECK(kept == kTexels);
        CHECK(kept.hash() == hash_of(kTexels));
        CHECK(!(written == kept));
        CHECK(written.hash() == hash_of(written.vec()));
    }
}

// A buffer of its own is written where it is, and the hash made before the write is not kept.
TEST(texels_a_write_in_place_forgets_the_hash) {
    Texels t(kTexels);
    const u32* own = buffer(t);
    const auto before = t.hash();
    t[0] = 9;
    CHECK(buffer(t) == own);
    CHECK(t.hash() != before);
    CHECK(t.hash() == hash_of(t.vec()));
}

TEST(texels_empty) {
    Texels t;
    CHECK(t.empty());
    CHECK(t.size() == 0);
    CHECK(t.hash() == hash_of({}));
    t.push_back(1);
    CHECK(t.size() == 1);
    CHECK(t.hash() == hash_of({1}));
}

// The same contents elsewhere (at any alignment) hash alike; another length (whole steps and every tail, zeros too) or
// one changed bit gives another hash.
TEST(content_hash_tells_contents_apart) {
    std::vector<u8> bytes(100);
    for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = u8(i * 7 + 1);
    std::vector<u8> moved(bytes.size() + 1);
    std::copy(bytes.begin(), bytes.end(), moved.begin() + 1);
    CHECK(bytes_hash(ByteSpan(moved).subspan(1)) == bytes_hash(bytes));

    const std::vector<u8> zeros(bytes.size());
    for (const std::vector<u8>* data : {static_cast<const std::vector<u8>*>(&bytes), &zeros}) {
        std::set<std::array<u64, 2>> lengths;
        for (size_t n = 0; n <= data->size(); ++n) lengths.insert(bytes_hash(ByteSpan(*data).first(n)));
        CHECK(lengths.size() == data->size() + 1);
    }

    std::set<std::array<u64, 2>> flipped{bytes_hash(bytes)};
    for (size_t bit = 0; bit < bytes.size() * 8; ++bit) {
        bytes[bit / 8] ^= u8(1 << (bit % 8));
        flipped.insert(bytes_hash(bytes));
        bytes[bit / 8] ^= u8(1 << (bit % 8));
    }
    CHECK(flipped.size() == bytes.size() * 8 + 1);

    // (texels are hashed as their bytes: a build cache's key and the GPU's are one hash)
    CHECK(hash_of(kTexels) ==
          bytes_hash(ByteSpan(reinterpret_cast<const u8*>(kTexels.data()), kTexels.size() * sizeof(u32))));
}

// The race records' data hash (base/bytes.h): saved replays keep its values, so they never change.
TEST(record_hash_keeps_its_values) {
    CHECK(record_hash({}) == 0x14650FB0739D0383ull);
    const u8 a[] = {'a'};
    CHECK(record_hash(a) == 0x44BD8AD473CD9906ull);
    const u8 foobar[] = {'f', 'o', 'o', 'b', 'a', 'r'};
    CHECK(record_hash(foobar) == 0x88FAD7C0A8FF07F2ull);
}
