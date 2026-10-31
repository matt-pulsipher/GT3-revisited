// The car parameter block's converter (src/sim/exact/params_build.h) on synthetic specs, disc-free: a hash of the P images it
// builds, pinned so a build that changes its results (host float or std:: math creeping in, another architecture, FMA
// contraction) fails.

#include "program_synth.h"
#include "sim/exact/params_build.h"
#include "test_framework.h"

#include <random>
#include <vector>

using namespace gt3r;

namespace {

sim::exact::ProgramData program() { return test::synthetic_program(); }

// The converter's P over a cleared record for synthetic specs (all zero, then bytes 0..7: counts and selectors stay in their
// tables), with the race's physics globals and each control source, rule block and condition block in turn.
u64 params_build_hash() {
    const sim::exact::ProgramData d = program();
    std::mt19937 rng(3);
    u64 h = 1469598103934665603ull;
    for (int run = 0; run < 64; ++run) {
        sim::exact::Spec spec{};
        if (run > 0)
            for (u8& b : spec) b = u8(rng() % 8);
        sim::exact::ConvertInputs in;
        in.data = &d;
        in.base = 0x00400000;
        in.control = run % 3;
        for (u8& b : in.rules) b = u8(rng() % 8);
        for (u32& w : in.condition) w = rng() % 4 ? 0 : rng();
        in.physics_globals();
        sim::exact::ParamImage p{};
        const s32 r = sim::exact::convert_params(spec, in, p);
        h = (h ^ u32(r)) * 1099511628211ull;
        for (u8 b : p) h = (h ^ b) * 1099511628211ull;
        for (u8 b : spec) h = (h ^ b) * 1099511628211ull;
    }
    return h;
}

} // namespace

TEST(params_build_platform_hash) { CHECK(params_build_hash() == 0xd4cff275d646b663ull); }

// ---- on the disc ----

#include "disc/core_image.h"
#include "disc/volume.h"
#include "formats/db/gt_db.h"
#include "game/data/car_database.h"
#include "sim/exact/car_fields.h"

#include <cstdio>
#include <map>
#include <set>

namespace {

data::CarDatabase* disc_database() {
    static data::CarDatabase db;
    static bool tried = false, ok = false;
    if (!tried) {
        tried = true;
        if (Disc* d = test::test_disc()) ok = db.load(d->volume());
    }
    return ok ? &db : nullptr;
}

std::shared_ptr<const sim::exact::ProgramData> disc_program() {
    static std::shared_ptr<const sim::exact::ProgramData> d;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (Disc* disc = test::test_disc())
            if (std::optional<Bytes> core = disc->volume().read_program())
                d = std::make_shared<const sim::exact::ProgramData>(sim::exact::ProgramData::load(CoreImage::decode(*core)));
    }
    return d;
}

} // namespace

// The tyre records' base grip (P + 0x398 + 0x1B4 r, +0x00: 9.8 x the friction curve D at zero slip; physics §6.5) as the
// race builds it, for every car on every tyre part it has (stock, then each ft_ / rt_ stage): one value per compound (the
// tyre class's table 27 record), the same on every car; printed per compound as a multiple of 9.8.
TEST(params_tire_base_grip_disc) {
    data::CarDatabase* db = disc_database();
    std::shared_ptr<const sim::exact::ProgramData> d = disc_program();
    if (!db || !d) SKIP("needs GT3R_DISC");
    namespace ex = sim::exact;
    std::map<std::string, std::set<u32>> by_compound;
    std::map<std::string, std::string> example;
    int builds = 0;
    for (const data::Car& car : db->cars()) {
        std::vector<std::array<u64, 27>> sets{car.parts};
        for (const data::PartInfo& ft : db->parts_for(data::TableId::FrontTire, car.label)) {
            if (ft.label.rfind("ft_", 0) != 0) continue;
            std::array<u64, 27> p = car.parts;
            p[22] = db::id_hash(ft.label), p[23] = db::id_hash("rt_" + ft.label.substr(3));
            if (db->tire(data::TableId::RearTire, p[23])) sets.push_back(p);
        }
        for (const std::array<u64, 27>& parts : sets) {
            const std::optional<data::Tire> front = db->tire(data::TableId::FrontTire, parts[22]);
            const std::optional<data::Tire> rear = db->tire(data::TableId::RearTire, parts[23]);
            if (!front || !rear) continue;
            ex::ParamImage pi;
            try {
                ex::ConvertInputs in;
                in.base = ex::kParams;
                pi = ex::race_params(*db, *d, ex::stock_equipment(*db, car.label, parts), 0, true, true, in);
            } catch (const std::exception&) {
                continue; // (records the builder cannot read)
            }
            ++builds;
            for (u32 r = 0; r < 6; ++r) {
                const u32 o = 0x398 + 0x1B4 * r;
                const u32 bits = u32(pi[o]) | u32(pi[o + 1]) << 8 | u32(pi[o + 2]) << 16 | u32(pi[o + 3]) << 24;
                const std::string& c = (r < 3 ? front : rear)->compound[r % 3];
                by_compound[c].insert(bits);
                example.emplace(c, car.label);
            }
        }
    }
    CHECK(builds > 1000);
    int varies = 0;
    for (const auto& [c, values] : by_compound) {
        varies += values.size() != 1;
        std::string list;
        for (u32 b : values) {
            char t[32];
            std::snprintf(t, sizeof(t), "%.5f ", double(ex::F::raw(b).host()) / 9.8);
            list += t;
        }
        std::printf("  %-14s 9.8 x %s(%s)\n", c.c_str(), list.c_str(), example[c].c_str());
    }
    CHECK(varies == 0); // (a compound's grip is the same on every car)
    std::printf("  %d builds, %zu compounds\n", builds, by_compound.size());
}
