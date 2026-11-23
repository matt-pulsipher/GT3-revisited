// GT-mode display figures (docs/specs/gt_mode.md §5 "Power figures"): the max power the menus
// show for a built car. The original builds the car (CarEquipments 0x231DB0, showroom build
// 0x1D93C0 with the car's condition words) and its peak search 0x1E14E8 writes {float PS,
// float kgf*m, u16 power rpm, u16 torque rpm} at show+0x1294; the menu text 0x1D9680 (computed
// branch, a3 = 0) converts the unit (0x245A58) and truncates. Compared with
// game::spec_power_peak / display_power: stock cars, random tune-shop parts, random condition.

#include "harness.h"

#include "disc/disc.h"
#include "disc/volume.h"
#include "formats/db/gt_db.h"
#include "game/data/car_database.h"
#include "game/gt_mode.h"
#include "game/gt_parts.h"
#include "game/net_car.h"
#include "sim/car_spec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

constexpr u32 kEqCtor = 0x002312B0, kEqInit = 0x00231BC0, kEqSetCar = 0x00231DB0, kClassCheck = 0x00235F88;
constexpr u32 kShowroom = 0x001D93C0, kPowerText = 0x001D9680, kUnits = 0x002F59D8;
constexpr u32 kEqSize = 0x400, kShowSize = 0x3000, kPeak = 0x1294;
constexpr u32 kAirDensityGlobal = 0x00352920;
// CarEquipments part words EQ+0x18+8k hold the part of table kEqSlotTables[k] (as the save's car record).
constexpr int kEqSlotTables[26] = {0, 1, 3, 6, 13, 17, 18, 21, 28, 29, 2, 4, 5, 7, 8, 9, 10, 11, 12, 14, 15, 16, 20, 19, 23, 22};
// CarEquipments settings words (CarSettings field order, as gt_save.cpp).
constexpr u32 kEqSettingOffsets[] = {0xF0,  0xF4,  0xF8,  0xFC,  0x100, 0x104, 0x108, 0x10C, 0x110, 0x114, 0x118, 0x11C,
                                     0x120, 0x124, 0x128, 0x12C, 0x130, 0x134, 0x138, 0x13C, 0x140, 0x144, 0x148, 0x14C,
                                     0x150, 0x154, 0x158, 0x15C, 0x160, 0x164, 0x168, 0x16C, 0x170, 0x174, 0x178, 0x17C,
                                     0x180, 0x184, 0x188, 0x18C, 0x190, 0x194, 0x198, 0x19C, 0x1A0, 0x1A4, 0x1A8, 0x1B0,
                                     0x1BC, 0x1C0, 0x1C4, 0x1C8};

struct OrigPower {
    float ps = 0;
    int rpm = 0;
    int shown_hp = 0, shown_ps = 0; // 0x1D9680 with the units object's power word 1 (HP) / 0 (PS)
    std::vector<u32> settings;
};

// Set-car, then `parts` (table, id) written into the EQ, then the build with the condition
// words {odometer, E, O}; reads the peak back. False on a trap.
bool run_original(DiffContext& ctx, const Bytes& gtar_bytes, const std::string& label, const std::vector<std::pair<int, u64>>& parts,
                  float wear, float oil, OrigPower& out, std::string& err) {
    Ee& ee = ctx.ee;
    try {
        ee.reset();
        ee.hle_libc_defaults();
        ee.hle(kClassCheck, [](Ee& e) { e.set_v0(1); }, "class check");
        ee.wf(kAirDensityGlobal, 1.2f);
        u32 gtar = ee.alloc_bytes(gtar_bytes);
        u32 eq = ee.alloc(kEqSize), show = ee.alloc(kShowSize), env = ee.alloc(0x20), rpm = ee.alloc(4);
        ee.call(kEqCtor, {Arg::ptr(eq + 0x1D0)}, 100000);
        ee.w32(eq + 0x1D0, gtar);
        ee.call(kEqInit, {Arg::ptr(eq)}, 1000000);
        auto r = ee.call(kEqSetCar, {Arg::ptr(eq), Arg::integer(s64(db::id_hash(label)))}, 10000000);
        if (r.v0_s32() != 1) {
            err = "set car returned " + std::to_string(r.v0_s32());
            return false;
        }
        for (auto [table, id] : parts)
            for (int k = 0; k < 26; ++k)
                if (kEqSlotTables[k] == table) {
                    ee.w32(eq + 0x18 + 8 * u32(k), u32(id));
                    ee.w32(eq + 0x1C + 8 * u32(k), u32(id >> 32));
                }
        for (u32 i = 0; i < 8; ++i) ee.w32(env + 4 * i, 0);
        ee.wf(env + 4, wear);
        ee.wf(env + 8, oil);
        ee.call(kShowroom, {Arg::ptr(show), Arg::ptr(eq), Arg::ptr(env)}, 200000000);
        out.ps = ee.rf(show + kPeak);
        out.rpm = int(ee.r32(show + kPeak + 8) & 0xFFFF);
        out.settings.clear();
        for (u32 o : kEqSettingOffsets) out.settings.push_back(ee.r32(eq + o));
        for (int u = 0; u < 2; ++u) {
            ee.w32(kUnits + 4, u32(u));
            auto t = ee.call(kPowerText, {Arg::ptr(show), Arg::ptr(rpm), Arg::integer(0x7073), Arg::integer(0)}, 100000);
            (u ? out.shown_hp : out.shown_ps) = t.v0_s32();
        }
        return true;
    } catch (const EeTrap& t) {
        char b[160];
        std::snprintf(b, sizeof b, "trap at pc=%08X: %s", t.pc, t.what());
        err = b;
        return false;
    }
}

sim::CarSettings settings_from(const std::vector<u32>& w) {
    sim::CarSettings c;
    std::vector<int*> f;
    for (int& v : c.gears) f.push_back(&v);
    for (int* v : {&c.final_drive, &c.gear_slider, &c.front_torque, &c.brake_front, &c.brake_rear, &c.downforce_front, &c.downforce_rear})
        f.push_back(v);
    for (int& v : c.turbo) f.push_back(&v);
    for (auto* a : {&c.camber, &c.ride_height, &c.toe, &c.spring, &c.motion_ratio})
        for (int& v : *a) f.push_back(&v);
    for (int& v : c.damper) f.push_back(&v);
    for (int& v : c.arb) f.push_back(&v);
    for (int& v : c.lsd) f.push_back(&v);
    f.push_back(&c.tcs);
    for (int& v : c.ascc) f.push_back(&v);
    for (size_t k = 0; k < f.size() && k < w.size(); ++k) *f[k] = s32(w[k]);
    return c;
}

} // namespace

DIFF_TEST(gt_display_power, "menu max power (0x1D93C0 + peak 0x1E14E8 + text 0x1D9680) vs game::spec_power_peak / display_power") {
    data::CarDatabase db;
    if (!db.load(ctx.disc->volume())) {
        ctx.check(false, "database load: " + db.error());
        return;
    }
    const Volume::Entry* e = ctx.disc->volume().find("database/paramdb_us.db");
    if (!e) {
        ctx.check(false, "database/paramdb_us.db not found");
        return;
    }
    Bytes gtar_bytes = ctx.disc->volume().read(*e);
    size_t n = 0, bad_ps = 0, bad_rpm = 0, bad_shown = 0, tuned = 0, ties = 0;
    double worst = 0;
    for (const data::Car& car : db.cars()) {
        if (car.label.rfind("sample", 0) == 0) continue;
        const int variants = 1 + std::max(1, ctx.iterations / 100); // stock, then random parts and condition
        for (int v = 0; v < variants; ++v) {
            std::vector<std::pair<int, u64>> parts;
            float wear = 0, oil = 0;
            std::array<u64, 27> ours_parts = car.parts;
            if (v > 0) {
                // A random stage of some tune-shop tables this car has parts for.
                for (int g = 0; g < game::kGoodsCount; ++g) {
                    game::Goods gi = game::goods_info(g);
                    if (gi.table == 28 || gi.table == 29 || !ctx.rng.chance(0.3)) continue;
                    std::optional<game::PartOffer> o = game::find_part(db, car.label, g);
                    int slot = game::part_slot(gi.table);
                    if (!o || slot < 0) continue;
                    // A turbo kit and an NA tune are exclusive in the shop (fit_part); never both.
                    if ((gi.table == 11 || gi.table == 12) &&
                        std::any_of(parts.begin(), parts.end(), [&](const auto& p) { return p.first == 23 - gi.table; }))
                        continue;
                    std::erase_if(parts, [&](const auto& p) { return p.first == gi.table; });
                    parts.push_back({gi.table, o->id});
                    ours_parts[size_t(slot)] = o->id;
                }
                if (!parts.empty()) ++tuned;
                wear = ctx.rng.uniform(-400000.0f, 700000.0f);
                oil = ctx.rng.chance(0.5) ? 0.0f : ctx.rng.uniform(-300000.0f, 300000.0f);
            }
            OrigPower o;
            std::string err;
            if (!run_original(ctx, gtar_bytes, car.label, parts, wear, oil, o, err)) {
                ctx.check(false, car.label + ": original: " + err);
                continue;
            }
            std::optional<sim::CarSpec> s = sim::build_car_spec(db, car.label, ours_parts, settings_from(o.settings));
            if (!s) {
                ctx.check(false, car.label + ": build_car_spec failed");
                continue;
            }
            game::CarCondition cond;
            cond.engine_wear_m = wear;
            cond.oil_m = oil;
            float f = game::GtMode::engine_factor(cond);
            for (float& t : s->engine.curve_torque) t *= f;
            game::PowerPeak p = game::spec_power_peak(*s);
            ++n;
            double rel = std::fabs(double(p.ps) - o.ps) / std::max(1.0f, o.ps);
            worst = std::max(worst, rel);
            auto what = [&] {
                char b[240];
                std::snprintf(b, sizeof b, "%s parts=%zu E=%.0f O=%.0f: orig %.4f PS @%d (%d HP / %d PS), ours %.4f PS @%d (%d / %d)",
                              car.label.c_str(), parts.size(), wear, oil, o.ps, o.rpm, o.shown_hp, o.shown_ps, p.ps, p.rpm,
                              game::display_power(p.ps, language_units(Language::US)), game::display_power(p.ps, language_units(Language::JP)));
                return std::string(b);
            };
            if (rel > 1e-4) {
                ++bad_ps;
                ctx.check(false, what());
            } else if (p.rpm != o.rpm) {
                // A flat power plateau: two samples within float noise (the condition factor is
                // applied to the curve points here, to the looked-up torque there).
                if (rel < 2e-6)
                    ++ties;
                else {
                    ++bad_rpm;
                    ctx.check(false, what());
                }
            }
            // The display rule on the original's own float (unit conversion + truncation).
            if (game::display_power(o.ps, language_units(Language::US)) != o.shown_hp ||
                game::display_power(o.ps, language_units(Language::JP)) != o.shown_ps) {
                ++bad_shown;
                ctx.check(false, "display rule: " + what());
            }
        }
    }
    char b[240];
    std::snprintf(b, sizeof b, "%zu builds (%zu with random parts): power mismatches %zu, rpm %zu (+%zu plateau ties within 2e-6), display rule %zu, "
                  "worst rel %.2e", n, tuned, bad_ps, bad_rpm, ties, bad_shown, worst);
    ctx.note(b);
}
