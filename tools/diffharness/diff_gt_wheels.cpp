// The wheel inventory and the wheel shop's purchase and fit (gt_mode §7), run on the original's
// game state. The game state (the pointer at 0x352A94) gets a new game (0x218CF0: the 200 wheel
// slots cleared, the stock-wheel placeholder in one; its record, licence and arcade blocks' set-ups
// are skipped, the game config's sums come from our data), a car in garage slot 0 as the ride car,
// then wheel after wheel bought by the shop's purchase (0x2196A8, the action buy_wheel's call) until
// no slot is free, with the shop's fit (the action fit_wheel, 0x126160) once on a wheel the
// inventory holds and once on the wheel the full inventory refused. Ours: GtMode::buy_wheel /
// fit_shop_wheel on the same sequence. Compared after every call: the credits, the inventory's
// entries (the placeholder included) and the ride car's wheel id. NTSC-U (the PAL and NTSC-J
// programs' purchase and fit are the same code).

#include "harness.h"

#include "disc/disc.h"
#include "formats/db/gt_db.h"
#include "game/gt_mode.h"
#include "vfs/vfs.h"

#include <memory>
#include <string>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

constexpr u32 kStatePtr = 0x00352A94;                    // the game state (GameStatus) pointer
constexpr u32 kNewGame = 0x00218CF0, kGcf = 0x0021DDE0, kGcfValue = 0x0021DCE0;
// The new game's set-ups of the other blocks (records, licences, arcade, time trials, ...).
constexpr u32 kOtherBlocks[] = {0x00215618, 0x00216F40, 0x00216CB0, 0x00216590, 0x002139D0,
                                0x00212E68, 0x002123F0, 0x00215D08, 0x00216010, 0x00217DF8};
constexpr u32 kFreeCarSlot = 0x00218F88, kGarageAdd = 0x00218FE0;
constexpr u32 kBuyWheel = 0x002196A8, kFitWheel = 0x00126160;
constexpr u32 kMenuTimer = 0x001B28F8, kPreviewWheel = 0x001B8A58; // the fit's page timer and car preview
// Game state words; the car record's wheel id and in-use word.
constexpr u32 kCredits = 0x18, kRide = 0x30, kWheelEntries = 0x3C, kFocusWheel = 0x30D10, kStateSize = 0x35210;
constexpr u32 kCarWheel = 0xF8, kCarInUse = 0x228;

} // namespace

DIFF_TEST(gt_wheel_inventory, "wheel purchase 0x2196A8 until full + shop fit 0x126160 vs GtMode::buy_wheel / fit_shop_wheel") {
    Vfs vfs(&ctx.disc->volume());
    auto data = std::make_shared<game::GtData>();
    if (!data->load([&](const std::string& p) { return vfs.read(p); })) {
        ctx.check(false, "GtData load: " + data->error());
        return;
    }
    game::GtMode gt(data);
    Ee& ee = ctx.ee;
    try {
        ee.reset();
        ee.hle_libc_defaults();
        // The game config: a stand-in object; values by name from our data.
        ee.hle(kGcf, [](Ee& e) { e.set_v0(0x1000); }, "gcf");
        ee.hle(kGcfValue, [&](Ee& e) {
            const std::string name = e.read_cstr(u32(e.a(1)));
            e.set_v0(u64(s64(data->gcf.value(name).value_or(0))));
        }, "gcf value");
        for (u32 a : kOtherBlocks) ee.hle(a, [](Ee&) {}, "other block");
        ee.hle(kMenuTimer, [](Ee&) {}, "menu timer");
        ee.hle(kPreviewWheel, [](Ee&) {}, "preview wheel");
        const u32 st = ee.alloc(kStateSize);
        ee.w32(kStatePtr, st);
        ee.call(kNewGame, {Arg::ptr(st)}, 50'000'000);
        ctx.expect_eq(u64(ee.r64(st + kCredits)), u64(gt.state().credits), [] { return std::string("new game credits"); });
        ctx.expect_eq(ee.r32(st + kWheelEntries), u32(gt.state().wheels.size() + 1), [] { return std::string("new game wheel entries"); });
        // A ride car: garage slot 0, as the ride car.
        const u32 car = u32(ee.call(kFreeCarSlot, {Arg::ptr(st)}).v0.lo);
        ee.w32(car + kCarInUse, 1);
        ee.call(kGarageAdd, {Arg::ptr(st), Arg::ptr(car)});
        ee.w32(st + kRide, 0);
        gt.state().credits = 100'000'000;
        ctx.check(gt.buy_car("r34_gtr_v_99") == 0, "our ride car");
        ee.w64(st + kCredits, u64(gt.state().credits));
        const u32 page = ee.alloc(0x400);
        auto compare = [&](const std::string& what) {
            ctx.expect_eq(u64(ee.r64(st + kCredits)), u64(gt.state().credits), [&] { return what + ": credits"; });
            ctx.expect_eq(ee.r32(st + kWheelEntries), u32(gt.state().wheels.size() + 1), [&] { return what + ": wheel entries"; });
            const game::GarageCar* ride = gt.state().ride_car();
            const u64 ours = ride && !ride->wheel.empty() ? db::id_hash(ride->wheel) : 0;
            ctx.expect_eq(ee.r64(car + kCarWheel), ours, [&] { return what + ": the ride car's wheel"; });
        };
        auto fit = [&](const std::string& wheel) {
            ee.write_bytes(st + kFocusWheel, ByteSpan(reinterpret_cast<const u8*>(wheel.c_str()), wheel.size() + 1));
            ee.call(kFitWheel, {Arg::ptr(page)});
            gt.fit_shop_wheel(wheel);
            compare("fit " + wheel);
        };
        // Wheels until no slot is free, and one more.
        int bought = 0;
        for (int i = 0; i < game::kMaxWheels; ++i) {
            const std::string wheel = "wheel_" + std::to_string(i);
            ee.call(kBuyWheel, {Arg::ptr(st), Arg::ptr(ee.alloc_cstr(wheel))});
            bought += gt.buy_wheel(wheel);
            compare("buy " + wheel);
            if (i == 0) fit(wheel); // a wheel the inventory holds
        }
        ctx.check(bought == game::kMaxWheels - 1, "199 wheels bought, the 200th refused");
        fit("wheel_" + std::to_string(game::kMaxWheels - 1)); // the wheel no slot was left for
        ctx.note("wheel entries " + std::to_string(ee.r32(st + kWheelEntries)) + ", credits " +
                 std::to_string(s64(ee.r64(st + kCredits))));
    } catch (const EeTrap& t) {
        char b[160];
        std::snprintf(b, sizeof b, "trap at pc=%08X: %s", t.pc, t.what());
        ctx.check(false, b);
    }
}
