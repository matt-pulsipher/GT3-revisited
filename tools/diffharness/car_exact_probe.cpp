// Car records in the interpreter through the field table; see car_exact_probe.h.

#include "car_exact_probe.h"

#include "disc/core_image.h"
#include "disc/disc.h"

#include <algorithm>
#include <cstring>
#include <memory>

namespace gt3r::diff::car_exact {

namespace {
// A later build's special car state in its own record (Car::Later::special).
constexpr u32 kLaterSpecial = 0x149C;
} // namespace

ex::Car read_car(Ee& ee, u32 car) {
    const std::vector<u8> raw = snapshot(ee, car);
    ex::Car c{};
    ex::load_image(c, raw.data());
    c.behaviour = disc_behaviour(); // the disc's release (program_map.h)
    // a later build's own words of the record (not in the snapshot's layout)
    if (const u32 x = car_extra_word()) {
        c.later.ratio = ee::F::raw(ee.r32(car + x));
        ee.read_bytes(car + kLaterSpecial, c.later.special.data(), c.later.special.size());
    }
    return c;
}

int take_missed_ground(ex::Car& ours, const ex::Car& theirs) {
    int n = 0;
    for (size_t i = 0; i < 4; ++i) {
        const ex::Ground& t = theirs.g[i];
        if (!t.miss || !ours.g[i].miss) continue;
        ex::Ground& o = ours.g[i];
        o.normal = t.normal, o.plane_d = t.plane_d, o.attr_a_raw = t.attr_a_raw, o.attr_b_raw = t.attr_b_raw;
        ++n;
    }
    return n;
}

void write_car(Ee& ee, u32 car, const ex::Car& c) {
    std::vector<u8> raw = snapshot(ee, car);
    ex::store_image(c, raw.data());
    write_image(ee, car, raw.data());
    if (const u32 x = car_extra_word()) {
        ee.w32(car + x, c.later.ratio.bits);
        ee.write_bytes(car + kLaterSpecial, ByteSpan(c.later.special.data(), c.later.special.size()));
    }
}

namespace {

// NTSC-U's record offsets of the words a later build keeps in its conductor's feedback block: the bytes +0x16D4 (x16d4,
// the pad's vibration, the steering wheel's force), the force's filter F +0x514 and its step count F +0x518 (a byte there).
constexpr u32 kFeedbackBytes = 0x16D4, kFeedbackFilter = kF + 0x514, kFeedbackCount = kF + 0x518;

// A later build's feedback block of the car at `car` (conductor + 0xB7C8 + 8 index: three bytes, the force's step count,
// the filter's float), when the record is a slot of the conductor its +4 names; else 0 (a record a test made on its own).
u32 feedback_block(Ee& ee, u32 car) {
    if (!car_extra_word()) return 0;
    const u32 cond = ee.r32(car + 4), stride = car_stride();
    if (!cond || car < cond + 0xC8 || (car - cond - 0xC8) % stride || (car - cond - 0xC8) / stride >= 8) return 0;
    return cond + 0xB7C8 + 8 * ((car - cond - 0xC8) / stride);
}

} // namespace

std::vector<u8> snapshot(Ee& ee, u32 car) {
    std::vector<u8> v(kCarSize);
    const u32 x = car_extra_word();
    if (!x) {
        ee.read_bytes(car, v.data(), kCarSize);
        return v;
    }
    ee.read_bytes(car, v.data(), x);
    ee.read_bytes(car + x + 4, v.data() + x, kCarSize - x);
    if (const u32 fb = feedback_block(ee, car)) {
        ee.read_bytes(fb, v.data() + kFeedbackBytes, 3);
        ee.read_bytes(fb + 4, v.data() + kFeedbackFilter, 4);
        const u8 count[4] = {ee.r8(fb + 3), 0, 0, 0};
        std::memcpy(v.data() + kFeedbackCount, count, 4);
    }
    return v;
}

void write_image(Ee& ee, u32 car, const u8* image) {
    const u32 x = car_extra_word();
    if (!x) {
        ee.write_bytes(car, ByteSpan(image, kCarSize));
        return;
    }
    const u32 fb = feedback_block(ee, car);
    if (!fb) {
        ee.write_bytes(car, ByteSpan(image, x));
        ee.write_bytes(car + x + 4, ByteSpan(image + x, kCarSize - x));
        return;
    }
    // The feedback words go to the conductor's block; the record's own bytes at their old places stay as they are.
    std::vector<u8> rec(image, image + kCarSize);
    const std::vector<u8> now = [&] {
        std::vector<u8> n(kCarSize);
        ee.read_bytes(car, n.data(), x);
        ee.read_bytes(car + x + 4, n.data() + x, kCarSize - x);
        return n;
    }();
    std::copy(now.begin() + kFeedbackBytes, now.begin() + kFeedbackBytes + 3, rec.begin() + kFeedbackBytes);
    std::copy(now.begin() + kFeedbackFilter, now.begin() + kFeedbackFilter + 8, rec.begin() + kFeedbackFilter); // (with the count's place)
    ee.write_bytes(car, ByteSpan(rec.data(), x));
    ee.write_bytes(car + x + 4, ByteSpan(rec.data() + x, kCarSize - x));
    ee.write_bytes(fb, ByteSpan(image + kFeedbackBytes, 3));
    ee.write_bytes(fb + 4, ByteSpan(image + kFeedbackFilter, 4));
    ee.w8(fb + 3, image[kFeedbackCount]);
}

std::vector<u32> unmirrored_changes(const std::vector<u8>& pre, const std::vector<u8>& post) {
    std::vector<u32> out;
    if (std::memcmp(pre.data(), post.data(), kCarSize) == 0) return out;
    const std::vector<u8>& cov = ex::covered_bytes();
    for (u32 off = 0; off < kCarSize; ++off)
        if (pre[off] != post[off] && !cov[off] && (out.empty() || out.back() != (off & ~3u))) out.push_back(off & ~3u);
    return out;
}

const ex::ProgramData& program_data(Disc& disc) {
    static std::unique_ptr<ex::ProgramData> data;
    if (!data) {
        data = std::make_unique<ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(disc.read_core_gt3())));
        // the harness checks the disc's own release (a later build's with its routines' variants, disc_behaviour())
        data->set_behaviour(sim::concept_program(data->program()) ? disc_behaviour() : ex::release_behaviour(data->program()));
    }
    return *data;
}

} // namespace gt3r::diff::car_exact
