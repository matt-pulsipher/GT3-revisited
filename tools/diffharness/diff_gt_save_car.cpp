// A garage car as the original keeps it and as our memory-card export writes it (gt_mode §3.1, issue 308). The dealer's
// purchase (the calls of 0x219250: 0x23AF78 the car, 0x235F88 its paint, 0x1D9F60 its condition) runs for every car of the
// database in each of its model's paints; the record the save holds (read back by 0x238048: car +0x10 for 0x1D0 bytes,
// +0x1F8 for 0x2C, +0x228 for 8) is compared with export_ps2_save's, all but the serial. Every exported paint id must also
// be one the original's color lookup (0x2391A0) finds: the race's color draw (0x176C28) reads the result without a check,
// and 0 is not one. NTSC-U.

#include "harness.h"

#include "disc/disc.h"
#include "formats/db/gt_db.h"
#include "game/gt_mode.h"
#include "game/gt_save.h"
#include "sim/car_spec.h"
#include "vfs/vfs.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

constexpr u32 kCarDb = 0x002EBD88, kColorDb = 0x00352D98;
constexpr u32 kGarageCtor = 0x0023AEA0, kNewCar = 0x0023AF78, kSetPaint = 0x00235F88, kNewCondition = 0x001D9F60;
constexpr u32 kColorById = 0x002391A0;
constexpr u32 kGarageCar = 0x230, kRecord = 0x204;

} // namespace

DIFF_TEST(gt_save_garage_car, "garage car purchase (0x23AF78, 0x235F88, 0x1D9F60) vs export_ps2_save's car records") {
    Vfs vfs(&ctx.disc->volume());
    auto data = std::make_shared<game::GtData>();
    if (!data->load([&](const std::string& p) { return vfs.read(p); })) {
        ctx.check(false, "GtData load: " + data->error());
        return;
    }
    const std::optional<Bytes> colors = vfs.read("database/carcolor.db"), db = vfs.read("database/paramdb_us.db");
    if (!colors || !db) {
        ctx.check(false, "database files");
        return;
    }
    Ee& ee = ctx.ee;
    int records = 0;
    try {
        ee.reset();
        ee.hle_libc_defaults();
        ee.w32(kCarDb, ee.alloc_bytes(*db));
        ee.w32(kColorDb, ee.alloc_bytes(*colors));
        const u32 car = ee.alloc(kGarageCar);
        ctx.expect_eq(u64(ee.call(kColorById, {Arg::ptr(kColorDb), Arg::integer(0)}).v0.lo), u64(0),
                      [] { return std::string("no color has the id 0"); });
        for (const data::Car& c : data->db.cars()) {
            const int paints = data->color_count(c.label);
            for (int paint = 0; paint < paints; ++paint) {
                ee.w32(car, 0);
                ee.call(kGarageCtor, {Arg::ptr(car)}, 1'000'000);
                ee.call(kNewCar, {Arg::ptr(car), Arg::ptr(ee.alloc_cstr(c.label))}, 50'000'000);
                ee.call(kSetPaint, {Arg::ptr(car + 0x10), Arg::integer(paint)}, 1'000'000);
                ee.call(kNewCondition, {Arg::ptr(car + 0x204)}, 1'000'000);
                game::GtMode gt(data);
                if (gt.add_car(c.label, paint) < 0) {
                    ctx.check(false, "our car " + c.label);
                    break;
                }
                const Bytes file = game::export_ps2_save(gt.state(), *data);
                // A computed gearbox's words after its own gears: whatever the original's work area held.
                const std::optional<sim::GearGenerator> gg = sim::gear_generator(data->db, gt.state().garage[0].parts);
                const u32 gears_end = gg ? 0xF0 + 4 * u32(gg->n + 1) : 0x110;
                for (u32 o = 0; o + 4 <= kRecord - 4; o += 4) {
                    if (o >= gears_end && o < 0x110) continue;
                    // One car's stock LSD setting below its range's minimum (physics §6.6).
                    if (o == 0x1A4 && c.label == "evo6_wrc_99_special") continue;
                    const u32 theirs = ee.r32(car + (o < 0x1D0 ? 0x10 + o : o < 0x1FC ? 0x1F8 + (o - 0x1D0) : 0x228 + (o - 0x1FC)));
                    const u32 ours = ~rd32(file, 0x40 + 0x130 + o);
                    ctx.expect_eq(theirs, ours, [&] {
                        char b[160];
                        std::snprintf(b, sizeof b, "%s paint %d: record +0x%03X", c.label.c_str(), paint, o);
                        return std::string(b);
                    });
                }
                const u32 id = ~rd32(file, 0x40 + 0x130 + 0x08);
                ctx.check(ee.call(kColorById, {Arg::ptr(kColorDb), Arg::integer(s64(id))}).v0.lo != 0, c.label + ": paint id found");
                ++records;
            }
        }
        ctx.note(std::to_string(records) + " car records");
    } catch (const EeTrap& t) {
        char b[160];
        std::snprintf(b, sizeof b, "trap at pc=%08X: %s", t.pc, t.what());
        ctx.check(false, b);
    }
}
