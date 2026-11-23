// Differential tests for format-level helpers that M0/M1 already
// reimplemented (database id hash, part-setting interpolation, sound pitch).
// Function addresses are for PBPX-95503 v1.10.

#include "harness.h"

#include "formats/db/gt_db.h"
#include "formats/sound/spu.h"

#include <climits>

using namespace gt3r;
using namespace gt3r::diff;

namespace {
constexpr u32 kStringHash = 0x00231190;   // u64 (const char*)
constexpr u32 kStrlen = 0x00269A68;       // called by string_hash
constexpr u32 kSettingLerp = 0x00233F60;  // int (?, min, max, steps, setting)
constexpr u32 kNoteToPitch = 0x002687B0;  // u16 (note_x256, center_x256)

std::string quote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') o.push_back(char(c));
        else {
            char b[8];
            std::snprintf(b, sizeof b, "\\x%02X", c);
            o += b;
        }
    }
    return o + "\"";
}

void run_hash(DiffContext& ctx, bool emulate_strlen) {
    ctx.ee.allow_calls({kStrlen});
    if (!emulate_strlen) ctx.ee.hle_libc_defaults();
    // Edge cases first: empty, one char, high bytes, long strings where the
    // byte sum no longer fits 16 bits (the original masks the seed).
    std::vector<std::string> inputs = {"", "a", "tk_r34_gtr_v_99_c", std::string(300, 'z'), std::string(600, '\xFF')};
    for (int i = 0; i < ctx.iterations; ++i) {
        int kind = int(ctx.rng.range(0, 9));
        if (kind < 6) inputs.push_back(ctx.rng.label(1, 40));
        else if (kind < 9) inputs.push_back(ctx.rng.bytes_no_nul(1, 64));
        else inputs.push_back(ctx.rng.bytes_no_nul(200, 1200));
    }
    for (const std::string& s : inputs) {
        ctx.ee.free_all();
        // Vary the alignment: the original strlen reads 16-byte blocks.
        u32 p = ctx.ee.alloc(u32(s.size()) + 32, 16) + u32(ctx.rng.range(0, 15));
        ctx.ee.write_bytes(p, ByteSpan(reinterpret_cast<const u8*>(s.data()), s.size()));
        ctx.ee.w8(p + u32(s.size()), 0);
        u64 orig = ctx.ee.call(kStringHash, {Arg::ptr(p)}).v0.lo;
        ctx.expect_eq(orig, db::id_hash(s), [&] { return "label=" + quote(s.size() > 48 ? s.substr(0, 48) + "..." : s) +
                                                     " len=" + std::to_string(s.size()); });
    }
}

} // namespace

DIFF_TEST(db_id_hash, "string_hash 0x231190 (+strlen 0x269A68 emulated) vs db::id_hash") { run_hash(ctx, true); }

DIFF_TEST(db_id_hash_hle, "string_hash 0x231190 with HLE strlen vs db::id_hash") { run_hash(ctx, false); }

DIFF_TEST(db_setting_lerp, "part setting interpolation 0x233F60 vs database.md formula") {
    // There is no shared C++ helper yet (the spec builder is not reimplemented);
    // compare with the formula documented in docs/formats/database.md.
    auto ours = [](int min, int max, int steps, int setting) {
        return steps < 2 ? min : min + (max - min) * (setting - 1) / (steps - 1);
    };
    for (int i = 0; i < ctx.iterations * 4; ++i) {
        // Byte triplets as stored in the tables, setting 1..steps (plus some out-of-range values).
        int min = int(ctx.rng.range(0, 255)), max = int(ctx.rng.range(0, 255)), steps = int(ctx.rng.range(0, 40));
        int setting = int(ctx.rng.range(steps > 0 ? 1 : 0, steps > 0 ? steps : 1));
        if (ctx.rng.chance(0.05)) setting = int(ctx.rng.range(-5, 60));
        CallResult r = ctx.ee.call(kSettingLerp, {Arg::integer(0), Arg::integer(min), Arg::integer(max),
                                                  Arg::integer(steps), Arg::integer(setting)});
        ctx.expect_eq(u64(s64(r.v0_s32())), u64(s64(ours(min, max, steps, setting))), [&] {
            return "min=" + std::to_string(min) + " max=" + std::to_string(max) + " steps=" + std::to_string(steps) +
                   " setting=" + std::to_string(setting);
        });
    }
}

DIFF_TEST(sound_note_to_pitch, "note-to-pitch 0x2687B0 vs sound::note_to_pitch") {
    // Exhaustive over the musical range: notes 0..127 with fine offsets,
    // centres over the instrument range (center_note << 8 | fine).
    auto one = [&](int note, int center) {
        u32 orig = u32(ctx.ee.call(kNoteToPitch, {Arg::integer(note), Arg::integer(center)}).v0.lo);
        u32 ours = sound::note_to_pitch(note, center);
        ctx.expect_eq(orig, ours, [&] { return "note=" + std::to_string(note) + " center=" + std::to_string(center); });
    };
    for (int n = 0; n < 128; ++n)
        for (int c = 0; c < 128; c += 3) one(n << 8, (c << 8) | ((n * 37 + c) & 0xFF));
    for (int i = 0; i < ctx.iterations * 4; ++i) one(int(ctx.rng.range(0, 0x7FFF)), int(ctx.rng.range(0, 0x7FFF)));
}
