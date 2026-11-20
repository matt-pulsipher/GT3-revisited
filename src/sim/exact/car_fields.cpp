// The car record's field table: record images to and from the mirror; see car_fields.h.

#include "sim/exact/car_fields.h"

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace gt3r::sim::exact {

namespace {

static_assert(std::endian::native == std::endian::little, "record bytes are copied straight into the mirror's members");

template <class M> constexpr u32 width() {
    using T = std::remove_cvref_t<M>;
    if constexpr (std::is_same_v<T, ee::F>) return 4;
    else return u32(sizeof(T));
}

// One field of the table, computed once: its record offset and width, and where its member sits in Car, so reads,
// writes and compares are byte copies.
struct Slot {
    const char* name;
    int index;
    u32 off, size;
    size_t member;
    bool is_signed;
};

const std::vector<Slot>& slots() {
    static const std::vector<Slot> table = [] {
        std::vector<Slot> v;
        static const Car probe{};
        const u8* base = reinterpret_cast<const u8*>(&probe);
        fields(probe, [&](const char* n, int i, u32 off, const auto& m) {
            using T = std::remove_cvref_t<decltype(m)>;
            if (off + width<T>() > kCarSize) {
                std::fprintf(stderr, "car_fields: %s at +0x%X is outside the record\n", n, off);
                std::abort();
            }
            v.push_back({n, i, off, width<T>(), size_t(reinterpret_cast<const u8*>(&m) - base), std::is_signed_v<T>});
        });
        return v;
    }();
    return table;
}

u8* at(Car& c, const Slot& s) { return reinterpret_cast<u8*>(&c) + s.member; }
const u8* at(const Car& c, const Slot& s) { return reinterpret_cast<const u8*>(&c) + s.member; }

// Fixed-width copies and compares (the widths are 1, 2 or 4; std::memcpy with a run-time size is a library call).
void copy(u8* d, const u8* p, u32 size) {
    if (size == 4) std::memcpy(d, p, 4);
    else if (size == 2) std::memcpy(d, p, 2);
    else *d = *p;
}
bool same(const u8* a, const u8* b, u32 size) {
    if (size == 4) return std::memcmp(a, b, 4) == 0;
    if (size == 2) return std::memcmp(a, b, 2) == 0;
    return *a == *b;
}

// The field's bits as the old table printed them (signed members sign-extended).
u32 bits(const Car& c, const Slot& s) {
    const u8* p = at(c, s);
    if (s.size == 4) {
        u32 v;
        std::memcpy(&v, p, 4);
        return v;
    }
    if (s.size == 2) {
        u16 v;
        std::memcpy(&v, p, 2);
        return s.is_signed ? u32(s32(s16(v))) : u32(v);
    }
    return s.is_signed ? u32(s32(s8(*p))) : u32(*p);
}

std::string label(const Slot& w) {
    std::string n = w.name;
    if (w.index < 0) return n;
    size_t dot = n.find('.');
    return dot == std::string::npos ? n + "[" + std::to_string(w.index) + "]" : n.substr(0, dot) + "[" + std::to_string(w.index) + "]" + n.substr(dot);
}

} // namespace

void load_image(Car& c, const u8* image) {
    for (const Slot& s : slots()) copy(at(c, s), image + s.off, s.size);
}

void store_image(const Car& c, u8* image) {
    for (const Slot& s : slots()) copy(image + s.off, at(c, s), s.size);
}

std::string first_difference(const Car& ours, const Car& theirs) {
    for (const Slot& s : slots())
        if (!same(at(ours, s), at(theirs, s), s.size)) {
            const u32 a = bits(ours, s), b = bits(theirs, s);
            char buf[96];
            if (s.size == 4) std::snprintf(buf, sizeof buf, " 0x%08X vs 0x%08X (%.9g vs %.9g)", a, b, double(ee::F::raw(a).host()),
                                           double(ee::F::raw(b).host()));
            else std::snprintf(buf, sizeof buf, " %u vs %u", a, b);
            return label(s) + buf;
        }
    // the later builds' own words (zero on GT3's discs)
    char buf[96];
    if (ours.later.ratio.bits != theirs.later.ratio.bits) {
        std::snprintf(buf, sizeof buf, "later.ratio 0x%08X vs 0x%08X", ours.later.ratio.bits, theirs.later.ratio.bits);
        return buf;
    }
    for (size_t i = 0; i < ours.later.special.size(); ++i)
        if (ours.later.special[i] != theirs.later.special[i]) {
            std::snprintf(buf, sizeof buf, "later.special[%zu] %u vs %u", i, ours.later.special[i], theirs.later.special[i]);
            return buf;
        }
    return {};
}

void copy_changed(Car& dst, const Car& before, const Car& after, u32 lo, u32 hi) {
    for (const Slot& s : slots())
        if (s.off >= lo && s.off < hi && !same(at(before, s), at(after, s), s.size)) copy(at(dst, s), at(after, s), s.size);
}

const std::vector<u8>& covered_bytes() {
    static const std::vector<u8> mask = [] {
        std::vector<u8> m(kCarSize, 0);
        for (const Slot& s : slots())
            for (u32 k = 0; k < s.size; ++k) m[s.off + k] = 1;
        return m;
    }();
    return mask;
}

} // namespace gt3r::sim::exact
