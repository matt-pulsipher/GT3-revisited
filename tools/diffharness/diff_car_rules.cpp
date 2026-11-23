// Functions of the program the game keeps beside the race step, against ours on the same inputs: the computed gearbox's
// ratio generator (the garage settings and the parameter builder's stock settings: sim/car_spec.h), the surface index
// (sounds and smoke: sim_types.h) and a wheel's road roar (race/car_sfx.h).

#include "harness.h"

#include "disc/disc.h"
#include "disc/volume.h"
#include "formats/db/gt_db.h"
#include "game/data/car_database.h"
#include "race/car_sfx.h"
#include "sim/car_spec.h"
#include "sim/sim_types.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

constexpr u32 kEqCtor = 0x002312B0, kEqInit = 0x00231BC0, kEqSetCar = 0x00231DB0, kClassCheck = 0x00235F88;
constexpr u32 kShowroom = 0x001D93C0;
constexpr u32 kEqSize = 0x400, kShowSize = 0x3000, kSpecSize = 0x3E2;
constexpr u32 kAirDensityGlobal = 0x00352920;
constexpr u32 kConverter = 0x001DBFD8, kConvScratchInit = 0x001D9B10, kModeRecord = 0x002D2988;
constexpr u32 kP = 0x10, kW = 0xFE4, kWStride = 0xA4;
constexpr u32 kRecord = 0x1800, kCondSize = 0x20000;

std::string fs(std::initializer_list<std::pair<const char*, double>> kv) {
    std::string s;
    for (auto& [k, v] : kv) {
        char b[64];
        std::snprintf(b, sizeof b, "%s%s=%.7g", s.empty() ? "" : " ", k, v);
        s += b;
    }
    return s;
}

// A car's record built by the original's own pipeline (equipment set-car 0x231DB0, showroom 0x1D93C0, converter 0x1DBFD8).
struct Rec {
    explicit Rec(Ee& e) : ee(e) {}
    Ee& ee;
    u32 car = 0, cond = 0;
    float rf(u32 o) { return ee.rf(car + o); }
    void set_wheel(int i, u32 o, float v) { ee.wf(car + kW + kWStride * u32(i) + o, v); }
};

bool build_original_car(DiffContext& ctx, const Bytes& gtar_bytes, const std::string& label, Rec& rec, std::string& err) {
    Ee& ee = ctx.ee;
    try {
        ee.reset();
        ee.hle_libc_defaults();
        ee.hle(kClassCheck, [](Ee& e) { e.set_v0(1); }, "class check");
        ee.wf(kAirDensityGlobal, 1.2f);
        ee.call(0x001DAE70, {}, 20'000'000); // race init of the per-surface tables (rolling resistance, bump): the BSS copy is zero otherwise
        u32 gtar = ee.alloc_bytes(gtar_bytes);
        u32 eq = ee.alloc(kEqSize), show = ee.alloc(kShowSize);
        ee.call(kEqCtor, {Arg::ptr(eq + 0x1D0)}, 100000);
        ee.w32(eq + 0x1D0, gtar);
        ee.call(kEqInit, {Arg::ptr(eq)}, 1000000);
        auto r = ee.call(kEqSetCar, {Arg::ptr(eq), Arg::integer(s64(db::id_hash(label)))}, 10000000);
        if (r.v0_s32() != 1) {
            err = "set car returned " + std::to_string(r.v0_s32());
            return false;
        }
        ee.call(kShowroom, {Arg::ptr(show), Arg::ptr(eq), Arg::integer(0)}, 200000000);
        std::vector<u8> spec(0x400, 0);
        ee.read_bytes(show, spec.data(), kSpecSize);
        rec.car = ee.alloc(kRecord);
        rec.cond = ee.alloc(kCondSize);
        std::vector<u8> z(kRecord, 0);
        ee.write_bytes(rec.car, ByteSpan(z.data(), z.size()));
        std::vector<u8> zc(kCondSize, 0);
        ee.write_bytes(rec.cond, ByteSpan(zc.data(), zc.size()));
        ee.w32(rec.car + 4, rec.cond);
        u32 sp = ee.alloc_bytes(ByteSpan(spec.data(), spec.size())), t1 = ee.alloc(0x100);
        ee.call(kConvScratchInit, {Arg::ptr(t1)});
        ee.call(kConverter, {Arg::ptr(rec.car + kP), Arg::ptr(sp), Arg::integer(0), Arg::integer(0), Arg::ptr(kModeRecord), Arg::ptr(t1)},
                400000000);
        return true;
    } catch (const EeTrap& t) {
        char b[160];
        std::snprintf(b, sizeof b, "trap at pc=%08X: %s", t.pc, t.what());
        err = b;
        return false;
    }
}

struct Setup {
    data::CarDatabase db;
    Bytes gtar;
};

bool load_setup(DiffContext& ctx, Setup& s) {
    if (!s.db.load(ctx.disc->volume())) {
        ctx.check(false, "database load: " + s.db.error());
        return false;
    }
    const Volume::Entry* e = ctx.disc->volume().find("database/paramdb_us.db");
    if (!e) {
        ctx.check(false, "database/paramdb_us.db not found");
        return false;
    }
    s.gtar = ctx.disc->volume().read(*e);
    return true;
}

} // namespace

// [spec §6.7, §7 #30] Computed-gearbox ratio generator 0x1E28A8 (a0 = packed spec, a1 = 10 x slider,
// a2 = s16[8] out, a3 = 0) vs sim::generate_gear_ratios, on random spec inputs.
DIFF_TEST(car_gear_generator, "gear-ratio generator 0x1E28A8 vs sim::generate_gear_ratios") {
    u64 off_by_one = 0, width0 = 0, other = 0;
    for (int i = 0; i < ctx.iterations * 2; ++i) {
        ctx.ee.free_all();
        u32 spec = ctx.ee.alloc(0x400), out = ctx.ee.alloc(16);
        int n = int(ctx.rng.range(4, 7)), rev = int(ctx.rng.range(60, 110)), fin = int(ctx.rng.range(2500, 5500));
        int drive = int(ctx.rng.range(0, 4)), slider = int(ctx.rng.range(10, 40));
        int sys = drive == 2 ? int(ctx.rng.range(0, 3)) : 0; // AWD system (spec 0xB3)
        int tyre[2][3];
        for (auto& t : tyre) {
            t[0] = int(ctx.rng.range(13, 19));  // rim inches
            t[1] = ctx.rng.chance(0.03) ? 0 : int(ctx.rng.range(31, 66)); // width / 5 mm (0 -> 10 mm)
            t[2] = int(ctx.rng.range(5, 13));   // aspect / 5 %
        }
        ctx.ee.w8(spec + 0x6F, u8(rev));
        ctx.ee.w8(spec + 0xB2, u8(drive));
        ctx.ee.w8(spec + 0xB3, u8(sys));
        ctx.ee.w16(spec + 0xC6, u16(fin));
        ctx.ee.w8(spec + 0xC8, u8(n));
        // Tyre codes per axle a (0 front, 1 rear): width/5 mm at 0x11A+a, rim inches at 0x11C+a, aspect/5 % at 0x11E+a.
        for (int a = 0; a < 2; ++a) {
            ctx.ee.w8(spec + 0x11A + u32(a), u8(tyre[a][1]));
            ctx.ee.w8(spec + 0x11C + u32(a), u8(tyre[a][0]));
            ctx.ee.w8(spec + 0x11E + u32(a), u8(tyre[a][2]));
        }
        for (int k = 0; k < 8; ++k) ctx.ee.w16(out + 2 * u32(k), 0x7777);
        ctx.ee.call(0x001E28A8, {Arg::ptr(spec), Arg::integer(10 * slider), Arg::ptr(out), Arg::integer(0)});
        // The front tyre is driven for FF and for AWD with system byte 0xB3 == 0 (physics §6.7).
        bool front = drive == 1 || (drive == 2 && sys == 0);
        const int* t = tyre[front ? 0 : 1];
        s16 ours[8];
        for (s16& v : ours) v = 0x7777;
        sim::generate_gear_ratios(n, rev, fin, t[0], t[1], t[2], slider, ours, drive, sys);
        for (int k = 0; k <= n; ++k) {
            int o = s16(ctx.ee.r16(out + 2 * u32(k)));
            if (o != ours[k]) ++(t[1] == 0 ? width0 : std::abs(o - ours[k]) == 1 ? off_by_one : other);
            ctx.expect_eq(u64(s64(o)), u64(s64(ours[k])), [&] {
                return (k ? "gear " + std::to_string(k) : std::string("reverse")) + " " +
                       fs({{"n", n}, {"rev", rev}, {"final", fin}, {"drive", drive}, {"sys", sys}, {"slider", slider}, {"rim", t[0]},
                           {"width", t[1]}, {"aspect", t[2]}});
            });
        }
    }
    ctx.note("differing words: width code 0 " + std::to_string(width0) + ", off by one count " + std::to_string(off_by_one) +
             ", other " + std::to_string(other));
}

DIFF_TEST(car_surface_index, "0x1E8A48 vs sim::surface_index (materials 0..15 x all flags)") {
    // Materials >= 16 do not occur in course data; the original passes them
    // through unchanged while ours clamps to 15 (harmless, noted).
    u32 q = ctx.ee.alloc(0x40);
    for (int m = 0; m < 16; ++m)
        for (int f = 0; f < 256; ++f) {
            ctx.ee.w8(q + 0x31, u8(m));
            ctx.ee.w8(q + 0x33, u8(f));
            u32 orig = u32(ctx.ee.call(0x001E8A48, {Arg::ptr(q)}).v0.lo);
            ctx.expect_eq(orig, u32(sim::surface_index(u32(m) | u32(f) << 8)),
                          [&] { return fs({{"material", m}, {"flags", f}}); });
        }
}

DIFF_TEST(race_road_roar, "0x1DF9D8 road roar of one wheel (volume, pitch; called by 0x1DE410 for surfaces 0 and 7-9) vs race::road_noise with the axle's tyre width (car +0x294 / +0x298); random rim speed, surface, airborne counter") {
    Setup su;
    if (!load_setup(ctx, su)) return;
    Rec rec(ctx.ee);
    std::string err;
    if (!build_original_car(ctx, su.gtar, "r34_gtr_v_99", rec, err)) {
        ctx.check(false, err);
        return;
    }
    ctx.ee.call(0x1DE830, {});
    const u32 out = ctx.ee.alloc(16);
    long n = 0, silent = 0;
    for (int it = 0; it < 2000; ++it) {
        int w = it & 3, s = int(ctx.rng.uniform(0, 16)) & 15;
        float rim = ctx.rng.uniform(-20, 80);
        bool counter = it % 5 != 0;
        rec.set_wheel(w, 0x20, rim);
        ctx.ee.w8(rec.car + kW + kWStride * u32(w) + 0x24, u8(s));
        ctx.ee.w8(rec.car + kW + kWStride * u32(w) + 0x2A, counter ? 3 : 0);
        ctx.ee.wf(out, -1);
        CallResult r = ctx.ee.call(0x1DF9D8, {Arg::ptr(rec.car), Arg::integer(w), Arg::ptr(out)}, 1'000'000);
        float vol = r.f0_host(), pitch = ctx.ee.rf(out);
        float width = rec.rf(0x10 + 0x284 + 4 * u32(w >> 1));
        bool sounds = counter && (s <= 1 || (s >= 7 && s <= 9));
        race::TyreSound ours = sounds ? race::road_noise(width, std::fabs(rim)) : race::TyreSound{0, 0.5f};
        ++n;
        silent += !sounds;
        auto what = [&] {
            char b[160];
            std::snprintf(b, sizeof b, "wheel %d surface %d rim %.3f counter %d width %.3f", w, s, rim, int(counter), width);
            return std::string(b);
        };
        ctx.expect_near(vol, ours.vol, 2e-6, 1e-5, what);
        ctx.expect_near(pitch, ours.pitch, 2e-6, 1e-5, what);
    }
    ctx.note(std::to_string(n) + " calls, " + std::to_string(silent) + " silent (airborne counter 0 or another surface)");
}
