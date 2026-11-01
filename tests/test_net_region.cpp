// Online cars across regional discs (docs/dev/ONLINE.md "Cars"): the parameter block digest a
// descriptor carries, and whether another player's car races on this machine as its owner races
// it. The databases and the program here are INVENTED (car_db_synth.h, program_synth.h); "another
// region's disc" is a second database whose records differ where the test says. The disc tests
// at the end build every car of a disc, and compare two discs (GT3R_COMPARE_DISC).

#include "test_framework.h"
#include "car_db_synth.h"
#include "program_synth.h"

#include "disc/volume.h"
#include "game/data/car_database.h"
#include "game/gt_mode.h"
#include "game/gt_parts.h"
#include "game/gt_setting.h"
#include "game/gt_state.h"
#include "game/net_car.h"
#include "base/sha256.h"
#include "disc/core_image.h"
#include "disc/disc.h"
#include "sim/car_spec.h"
#include "sim/exact/program_data.h"
#include "vfs/vfs.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

using namespace gt3r;
using game::CarHere;

namespace {

// The descriptor as another machine receives it.
game::OnlineCar wire(const game::OnlineCar& c) {
    std::optional<game::OnlineCar> d = game::decode_online_car(game::encode_online_car(c));
    return d ? *d : game::OnlineCar{};
}

const sim::exact::ProgramData& program() {
    static const sim::exact::ProgramData d = test::synthetic_program();
    return d;
}

// The car its owner announces: the descriptor with the digest of its parameter block.
game::OnlineCar announced(const data::CarDatabase& db, game::OnlineCar c) {
    c.digest = game::online_car_digest(db, program(), c);
    return wire(c);
}

game::CarHere here_on(const data::CarDatabase& db, const game::OnlineCar& c, const std::function<bool(const std::string&)>& m) {
    return game::online_car_here(db, &program(), c, m);
}

// A disc whose files hold exactly these car models (cars/day/<model>).
std::function<bool(const std::string&)> models(std::set<std::string> m) {
    return [m](const std::string& model) { return m.count(model) > 0; };
}

} // namespace

TEST(net_car_digest_wire) {
    game::OnlineCar g;
    g.kind = game::OnlineCar::Kind::Garage;
    g.car = "syn_a";
    g.parts[3] = 0x1122334455667788ull;
    g.engine_wear_m = 1000;
    game::OnlineCar a;
    a.kind = game::OnlineCar::Kind::Arcade;
    a.car = "syn_a";
    a.arcade = "syn_a_100";
    game::OnlineCar m;
    m.kind = game::OnlineCar::Kind::Mod;
    m.car = "mod_x";
    m.package = std::string(64, 'c');
    game::OnlineCar d;
    d.car = "syn_a";
    for (game::OnlineCar c : {g, a, m, d}) {
        const Bytes old = game::encode_online_car(c); // as an older game sends it
        std::optional<game::OnlineCar> od = game::decode_online_car(old);
        CHECK(od && !od->digest); // no digest: absent
        c.digest.emplace();
        for (size_t i = 0; i < 32; ++i) (*c.digest)[i] = u8(i * 7 + 1);
        const Bytes now = game::encode_online_car(c);
        // The digest trails the fields older games read, which they read unchanged.
        CHECK(now.size() == old.size() + 32 && std::equal(old.begin(), old.end(), now.begin()));
        CHECK(now.size() < game::kMaxOnlineCarBytes);
        std::optional<game::OnlineCar> nd = game::decode_online_car(now);
        CHECK(nd && nd->digest && *nd->digest == *c.digest && nd->kind == c.kind && nd->car == c.car);
        CHECK(nd && game::encode_online_car(*nd) == now);
        // A truncated digest is no digest.
        std::optional<game::OnlineCar> td = game::decode_online_car(ByteSpan(now.data(), now.size() - 1));
        CHECK(td && !td->digest);
    }
}

TEST(net_car_params_digest) {
    // The digest is the parameter block's: what the race builds from the car's records. The label and the model are not in
    // it (a regional twin under another label builds the same block); a record value that reaches P is, to the last bit.
    namespace cardb = test::cardb;
    auto a = cardb::empty_database(), b = cardb::empty_database(), c = cardb::empty_database();
    if (!a || !b || !c) SKIP("in-memory database did not load");
    cardb::add_car(*a, "syn_a", 300, 75, "zz0001");
    cardb::add_car(*b, "syn_twin", 300, 75, "zz0009"); // the same records, another label and model
    cardb::add_car(*c, "syn_a", 301, 75, "zz0001");    // one torque step more
    a->rebuild(), b->rebuild(), c->rebuild();
    const auto da = game::online_car_digest(*a, program(), game::online_car_from_db(*a, "syn_a", 0));
    const auto db_ = game::online_car_digest(*b, program(), game::online_car_from_db(*b, "syn_twin", 0));
    const auto dc = game::online_car_digest(*c, program(), game::online_car_from_db(*c, "syn_a", 0));
    CHECK(da && db_ && dc);
    if (!da || !db_ || !dc) return;
    CHECK(*da == *db_);
    CHECK(*da != *dc);
    // The block itself: the same bytes for the twin, built from this program's tables at NTSC-U's rules.
    const auto pa = game::online_car_params(*a, program(), game::online_car_from_db(*a, "syn_a", 0));
    CHECK(pa && sha256(ByteSpan(pa->data(), pa->size())) == *da);
    // Not on this disc: no digest.
    CHECK(!game::online_car_digest(*a, program(), game::online_car_from_db(*b, "syn_twin", 0)));
}

namespace {

// The digests of a few INVENTED cars (a disc car, a garage car with tuned parts, settings and its engine condition, an
// arcade car), folded into one value.
u64 params_digest_hash() {
    namespace cardb = test::cardb;
    auto db = cardb::empty_database();
    if (!db) return 0;
    cardb::add_car(*db, "syn_a", 300, 75, "zz0001");
    cardb::add_car(*db, "syn_b", 280, 82, "zz0002");
    cardb::add_arcade_car(*db, "syn_a_100", "syn_a");
    db->rebuild();
    std::vector<game::OnlineCar> cars = {game::online_car_from_db(*db, "syn_a", 0), game::online_car_from_db(*db, "syn_b", 0),
                                         game::online_car_from_arcade(*db, "syn_a_100", 0)};
    game::GarageCar g;
    g.car = "syn_b";
    if (const data::Car* car = db->car("syn_b")) g.parts = car->parts;
    g.settings = sim::stock_settings(*db, g.car, g.parts);
    g.settings.final_drive += 200;      // a longer final drive
    g.cond.engine_wear_m = -120000, g.cond.oil_m = 3000;
    cars.push_back(game::online_car_from_garage(*db, g));
    u64 h = 1469598103934665603ull;
    for (const game::OnlineCar& c : cars) {
        const auto d = game::online_car_digest(*db, program(), c);
        if (!d) return 1;
        for (u8 x : *d) h = (h ^ x) * 1099511628211ull;
    }
    return h;
}

} // namespace

TEST(net_car_params_digest_platform_hash) {
    // The same bytes on every platform (arm64, x86-64; fp contraction on or off): the lobby compares digests exactly.
    const u64 h = params_digest_hash();
    std::printf("  parameter block digests: %016llx\n", (unsigned long long)h);
    CHECK(h == 0xb2bd4ee5d67b3977ull);
}

TEST(net_car_here_regions) {
    namespace cardb = test::cardb;
    // This machine's disc: car syn_a (model zz0001) and the arcade car syn_a_100 on it.
    auto here = cardb::empty_database();
    // Another region's disc: syn_a with another rev limit, its twin syn_b (syn_a's records under
    // another label and model) carrying the arcade car, and a car this disc lacks.
    auto other = cardb::empty_database();
    if (!here || !other) SKIP("in-memory database did not load");
    cardb::add_car(*here, "syn_a", 300, 75, "zz0001");
    cardb::add_arcade_car(*here, "syn_a_100", "syn_a");
    cardb::add_car(*other, "syn_a", 300, 80, "zz0001");
    cardb::add_car(*other, "syn_b", 300, 75, "zz0002");
    cardb::add_car(*other, "syn_only", 300, 75, "zz0003");
    cardb::add_arcade_car(*other, "syn_a_100", "syn_b");
    here->rebuild(), other->rebuild();
    const auto all = models({"zz0001", "zz0002", "zz0003"});

    // A disc car built alike on both discs races; one built differently does not.
    game::OnlineCar a = announced(*other, game::online_car_from_db(*other, "syn_a", 0));
    CHECK(a.digest && game::online_car_digest(*other, program(), a) == a.digest);
    CHECK(here_on(*other, a, all) == CarHere::Ok);
    CHECK(here_on(*here, a, all) == CarHere::Differs);
    CHECK(game::car_here_blocks(CarHere::Differs) && std::string(game::car_here_text(CarHere::Differs)) == "is different on this disc");
    // An older game's descriptor carries no digest: not compared.
    game::OnlineCar old = a;
    old.digest.reset();
    CHECK(here_on(*here, wire(old), all) == CarHere::Ok);
    // Not on this disc.
    game::OnlineCar only = announced(*other, game::online_car_from_db(*other, "syn_only", 0));
    CHECK(here_on(*here, only, all) == CarHere::Unknown);
    CHECK(std::string(game::car_here_text(CarHere::Unknown)) == "is not on this disc");

    // The arcade car on its regional twin: the other disc names base car syn_b, this one syn_a;
    // both build the same spec, so it races (as this disc's syn_a).
    game::OnlineCar arc = announced(*other, game::online_car_from_arcade(*other, "syn_a_100", 0));
    CHECK(arc.kind == game::OnlineCar::Kind::Arcade && arc.car == "syn_b" && arc.digest);
    CHECK(game::online_car_spec(*here, arc).has_value());
    CHECK(game::online_car_base(*here, arc) == "syn_a");
    CHECK(here_on(*here, arc, all) == CarHere::Ok);
    CHECK(game::online_car_facts(*here, nullptr, arc).known);
    // Without a digest the other base car cannot be checked: refused.
    game::OnlineCar arc_old = arc;
    arc_old.digest.reset();
    std::string why;
    CHECK(!game::online_car_spec(*here, wire(arc_old), &why) && why == "arcade car syn_a_100 is not a syn_b");
    CHECK(here_on(*here, wire(arc_old), all) == CarHere::Differs);
    // A twin that builds differently is refused too: it builds there, but the lobby's digest check says it differs.
    auto third = cardb::empty_database();
    if (!third) SKIP("in-memory database did not load");
    cardb::add_car(*third, "syn_c", 300, 80, "zz0001");
    cardb::add_arcade_car(*third, "syn_a_100", "syn_c");
    third->rebuild();
    CHECK(game::online_car_spec(*third, arc).has_value());
    CHECK(here_on(*third, arc, all) == CarHere::Differs);
    // An arcade car this disc lacks.
    game::OnlineCar unknown_arc = arc;
    unknown_arc.arcade = "syn_z_999";
    CHECK(here_on(*here, unknown_arc, all) == CarHere::Unknown);

    // A garage car: its digest includes its engine condition.
    const data::Car* car = here->car("syn_a");
    CHECK(car != nullptr);
    if (!car) return;
    game::GarageCar g;
    g.car = "syn_a";
    g.parts = car->parts;
    g.settings = sim::stock_settings(*here, g.car, g.parts);
    g.cond.engine_wear_m = -250000; // still running in: below full power
    CHECK(game::GtMode::engine_factor(g.cond) < 1.0f);
    game::OnlineCar gc = announced(*here, game::online_car_from_garage(*here, g));
    game::OnlineCar disc_a = announced(*here, game::online_car_from_db(*here, "syn_a", 0));
    CHECK(gc.digest && disc_a.digest && *gc.digest != *disc_a.digest);
    CHECK(here_on(*here, gc, all) == CarHere::Ok);
    CHECK(here_on(*other, gc, all) == CarHere::Differs);

    // A car whose body model this disc does not have (the race could not draw it).
    CHECK(here_on(*here, disc_a, models({"zz0002"})) == CarHere::NoModel);
    CHECK(std::string(game::car_here_text(CarHere::NoModel)) == "has no model on this disc");
    // The arcade twin is drawn as this disc's base car: its model counts.
    CHECK(here_on(*here, arc, models({"zz0002"})) == CarHere::NoModel);
    CHECK(here_on(*other, arc, models({"zz0002"})) == CarHere::Ok);

    // A mod car not installed here: offered for download, else a stand-in; not refused.
    game::OnlineCar mod;
    mod.kind = game::OnlineCar::Kind::Mod;
    mod.car = "mod_x";
    mod.package = std::string(64, 'd');
    CHECK(here_on(*here, wire(mod), all) == CarHere::ModMissing);
    CHECK(!game::car_here_blocks(CarHere::ModMissing) && !game::car_here_blocks(CarHere::Ok));
}

namespace {

namespace cardb = test::cardb;

// A part record of `car`: id `label`, stage +0x10.
Bytes part_record(data::CarDatabase& db, const std::string& label, const std::string& car, u8 stage, size_t size) {
    db.add_label(label);
    Bytes r(size);
    cardb::put64(r, 0, db::id_hash(label));
    cardb::put64(r, 8, db::id_hash(car));
    r[0x10] = stage;
    return r;
}

// Car syn_t (cardb::add_car) and its INVENTED tune shop: a stock gearbox with stored ratios and an adjustable one with
// generated ratios, a racing suspension with setting ranges, an NA tune (+500 rpm), a turbo kit, stock and medium tyres
// of two sizes; and a racing suspension made for another car (syn_o).
std::unique_ptr<data::CarDatabase> tuning_db() {
    auto db = cardb::empty_database();
    if (!db) return db;
    cardb::add_car(*db, "syn_t", 300, 75, "zz0001");
    const ByteSpan* stock_su = db->record(data::TableId::Suspension, "su_syn_t");
    if (!stock_su) return nullptr;
    const Bytes stock(stock_su->begin(), stock_su->end()); // (put_record moves the table's records)
    for (const char* car : {"syn_t", "syn_o"}) {
        Bytes su = stock;
        const std::string label = std::string("su_") + car + "_racing";
        db->add_label(label);
        cardb::put64(su, 0, db::id_hash(label));
        cardb::put64(su, 8, db::id_hash(car));
        su[0x10] = 3;                                    // racing kit: bound and rebound
        su[0x11] = 0, su[0x12] = 30, su[0x13] = 10;      // front camber 0..30, stock 10
        su[0x14] = 0, su[0x15] = 30, su[0x16] = 10;      // rear camber
        su[0x17] = 120, su[0x18] = 136, su[0x19] = 128;  // front toe
        su[0x1A] = 120, su[0x1B] = 136, su[0x1C] = 128;  // rear toe
        su[0x21] = 40, su[0x22] = 120, su[0x23] = 60;    // front springs 4.0..12.0 kgf/mm, stock 6.0
        su[0x24] = 45, su[0x25] = 130, su[0x26] = 70;    // rear springs
        for (size_t o : {0x2B, 0x32, 0x39, 0x40}) su[o] = 8; // damper steps
        for (size_t o : {0x2E, 0x31, 0x35, 0x38, 0x3C, 0x3F, 0x43, 0x46}) su[o] = 4;
        su[0x47] = su[0x4B] = 7, su[0x4A] = su[0x4E] = 4; // stabilizers
        cardb::put16(su, 0x50, 100), cardb::put16(su, 0x52, 140); // front ride height 100..140 mm (stock 120)
        cardb::put16(su, 0x56, 100), cardb::put16(su, 0x58, 140);
        db->put_record(data::TableId::Suspension, std::move(su));
    }
    Bytes ge = part_record(*db, "ge_syn_t", "syn_t", 0, 0x30); // stored ratios, final 4.000 (no final range)
    ge[0x11] = 5;
    cardb::put16(ge, 0x12, 3300);
    const u16 ratios[5] = {3200, 2000, 1400, 1050, 820};
    for (size_t i = 0; i < 5; ++i) cardb::put16(ge, 0x14 + 2 * i, ratios[i]);
    cardb::put16(ge, 0x22, 4000), cardb::put16(ge, 0x24, 4000), cardb::put16(ge, 0x26, 4000);
    db->put_record(data::TableId::Gear, std::move(ge));
    Bytes full = part_record(*db, "ge_syn_t_full", "syn_t", 3, 0x30); // generated: final 2.500..5.500, auto level 18..42
    full[0x11] = 6;
    cardb::put16(full, 0x22, 4111), cardb::put16(full, 0x24, 5500), cardb::put16(full, 0x26, 2500);
    full[0x28] = 1, full[0x29] = 25, full[0x2A] = 18, full[0x2B] = 42;
    db->put_record(data::TableId::Gear, std::move(full));
    for (auto [label, rim, aspect, width] : {std::tuple{"sz_syn_16", 16, 9, 45}, std::tuple{"sz_syn_17", 17, 8, 49}}) {
        db->add_label(label);
        Bytes sz(0x10);
        cardb::put64(sz, 0, db::id_hash(label));
        sz[0x08] = u8(rim), sz[0x09] = u8(aspect), sz[0x0A] = u8(width);
        db->put_record(data::TableId::TireSize, std::move(sz));
    }
    for (auto [table, prefix] : {std::pair{data::TableId::FrontTire, "ft_syn_t"}, std::pair{data::TableId::RearTire, "rt_syn_t"}})
        for (auto [suffix, stage, size] : {std::tuple{"", 0, "sz_syn_16"}, std::tuple{"_m", 5, "sz_syn_17"}}) {
            Bytes t = part_record(*db, std::string(prefix) + suffix, "syn_t", u8(stage), 0x60);
            cardb::put64(t, 0x18, db::id_hash(size));
            db->put_record(table, std::move(t));
        }
    Bytes nt = part_record(*db, "nt_syn_t_1", "syn_t", 1, 0x20);
    nt[0x12] = 5; // rev limit +500 rpm
    db->put_record(data::TableId::NaTune, std::move(nt));
    Bytes tk = part_record(*db, "tk_syn_t_1", "syn_t", 1, 0x20);
    tk[0x11] = 1, tk[0x12] = 12, tk[0x13] = 30, tk[0x14] = 20; // turbo, stage-0 boost words
    db->put_record(data::TableId::TurbineKit, std::move(tk));
    // The car's stock gearbox and tyres.
    const ByteSpan* car = db->record(data::TableId::Car, "syn_t");
    Bytes c(car->begin(), car->end());
    cardb::put64(c, 8 + 8 * 18, db::id_hash("ge_syn_t"));
    cardb::put64(c, 8 + 8 * 22, db::id_hash("ft_syn_t"));
    cardb::put64(c, 8 + 8 * 23, db::id_hash("rt_syn_t"));
    db->put_record(data::TableId::Car, std::move(c));
    db->rebuild();
    return db;
}

} // namespace

TEST(net_car_setup_rules) {
    // A garage car's parts and settings must be ones GT Mode can give it (docs/dev/ONLINE.md "Cars"): its own parts, the
    // ranges its parts' Settings pages allow, the words no page sets left as the parts give them.
    auto dbp = tuning_db();
    if (!dbp) SKIP("in-memory database did not load");
    const data::CarDatabase& db = *dbp;
    const data::Car* car = db.car("syn_t");
    CHECK(car != nullptr);
    if (!car) return;
    const int su = game::part_slot(18), ge = game::part_slot(17), na = game::part_slot(11), tk = game::part_slot(12);
    const int rt = game::part_slot(29);
    auto problem = [&](const std::array<u64, 27>& p, const sim::CarSettings& s) { return game::garage_setup_problem(db, "syn_t", p, s); };
    std::array<u64, 27> parts = car->parts;
    parts[26] = 0; // make_car's wheel slot
    const sim::CarSettings stock = sim::stock_settings(db, "syn_t", parts);
    CHECK(problem(parts, stock).empty());

    // Parts: the car's own (the shop's record for each goods), never another car's.
    const std::vector<u64> sus = game::fittable_parts(db, "syn_t", su);
    CHECK(sus.size() == 2 && sus[0] == car->parts[size_t(su)] && sus[1] == db::id_hash("su_syn_t_racing"));
    std::array<u64, 27> p = parts;
    p[size_t(su)] = db::id_hash("su_syn_o_racing");
    CHECK(problem(p, sim::stock_settings(db, "syn_t", p)) == "a part GT Mode does not fit on this car (suspension)");
    p[size_t(su)] = 0;
    CHECK(problem(p, stock) == "a part GT Mode does not fit on this car (suspension)");
    // An NA tune or a turbo kit, not both (fitting one restores the other's stock part).
    p = parts;
    p[size_t(na)] = db::id_hash("nt_syn_t_1");
    CHECK(problem(p, sim::stock_settings(db, "syn_t", p)).empty());
    p[size_t(tk)] = db::id_hash("tk_syn_t_1");
    CHECK(problem(p, sim::stock_settings(db, "syn_t", p)) == "an NA tune and a turbo kit together");
    p[size_t(na)] = parts[size_t(na)];
    sim::CarSettings s = sim::stock_settings(db, "syn_t", p);
    CHECK(s.turbo[0] == 12 && problem(p, s).empty());
    s.turbo[0] = 30; // no page sets the boost
    CHECK(problem(p, s) == "turbo out of range");

    // Settings: the stock suspension's page is locked (one value per spring): its own values only.
    s = stock;
    s.spring[0] += 1;
    CHECK(problem(parts, s) == "front spring rate out of range");
    // The racing suspension's sliders: their ranges from its record.
    p = parts;
    p[size_t(su)] = db::id_hash("su_syn_t_racing");
    const sim::CarSettings racing = sim::stock_settings(db, "syn_t", p);
    CHECK(racing.spring[0] == 60 && problem(p, racing).empty());
    s = racing;
    s.spring = {40, 130}, s.ride_height = {100, 140}, s.camber = {0, 30}, s.toe = {120, 136}, s.arb = {1, 7};
    s.damper = {1, 1, 8, 8, 8, 8, 1, 1};
    CHECK(problem(p, s).empty());
    s.spring[0] = 121;
    CHECK(problem(p, s) == "front spring rate out of range");
    s.spring[0] = 120, s.damper[6] = 9; // past the steps: the spec builder's interpolation would extrapolate
    CHECK(problem(p, s) == "rear dampers out of range");
    s.damper[6] = 8, s.ride_height[1] = 99;
    CHECK(problem(p, s) == "rear ride height out of range");
    s.ride_height[1] = 100, s.motion_ratio[0] = 150; // no page sets it
    CHECK(problem(p, s) == "front motion ratio out of range");

    // A stored-ratio gearbox keeps its ratios and final.
    s = stock;
    s.gears[2] -= 10;
    CHECK(problem(parts, s) == "gear 2 out of range");
    s = stock;
    s.final_drive = 3900;
    CHECK(problem(parts, s) == "final gear out of range");
    // The adjustable gearbox: the final slider's range (its top end exclusive) and the auto level's.
    p = parts;
    p[size_t(ge)] = db::id_hash("ge_syn_t_full");
    const sim::CarSettings full = sim::stock_settings(db, "syn_t", p);
    CHECK(full.final_drive == 4111 && full.gear_slider == 25 && full.gears[1] > 0 && problem(p, full).empty());
    s = full;
    for (int f : {2500, 5499}) {
        s.final_drive = f;
        CHECK(problem(p, s).empty());
    }
    s.final_drive = 5500;
    CHECK(problem(p, s) == "final gear out of range");
    s.final_drive = 4111, s.gear_slider = 43;
    CHECK(problem(p, s) == "gear auto setting out of range");
    // As the pages leave it: the auto level moved at the shortest final (gears 1-6 regenerate there; the reverse stays),
    // first gear raised to its slider's end (9/8 of the reference), then the final moved to its other end.
    s = full;
    s.final_drive = 2500, s.gear_slider = 18;
    CHECK(sim::apply_gear_slider(db, p, s));
    s.gears[0] = full.gears[0];
    s.gears[1] = s.gears[1] * 9 / 8;
    s.final_drive = 5499;
    CHECK(problem(p, s).empty());
    const int first = s.gears[1];
    s.gears[1] = 2 * first; // past the top of the bounds (the most revs, the largest tyre, the shortest final)
    CHECK(problem(p, s) == "gear 1 out of range");
    s.gears[1] = first, s.gears[6] = 100;
    CHECK(problem(p, s) == "gear 6 out of range");
    // Parts fitted after the gearbox leave its ratios (generated with the old tyres and rev limit): still legal.
    std::array<u64, 27> later = p;
    later[size_t(rt)] = db::id_hash("rt_syn_t_m");
    later[size_t(na)] = db::id_hash("nt_syn_t_1");
    sim::CarSettings kept = full; // (refresh_settings resets only the fitted part's group)
    CHECK(sim::stock_settings(db, "syn_t", later).gears[1] != full.gears[1]);
    CHECK(problem(later, kept).empty());
    kept.gears[0] = 9000;
    CHECK(problem(later, kept) == "reverse gear out of range");

    // On receipt: the lobby refuses it, with the reason; the owner's own lobby says so too.
    game::GarageCar g;
    g.car = "syn_t";
    g.parts = p;
    g.settings = full;
    game::OnlineCar ok = wire(game::online_car_from_garage(db, g));
    const auto all = models({"zz0001"});
    std::string why = "x";
    CHECK(game::online_car_here(db, nullptr, ok, all, &why) == CarHere::Ok && why == "x");
    g.settings.final_drive = 6000;
    game::OnlineCar bad = wire(game::online_car_from_garage(db, g));
    CHECK(game::online_car_here(db, nullptr, bad, all, &why) == CarHere::Illegal && why == "final gear out of range");
    CHECK(game::car_here_blocks(CarHere::Illegal) && std::string(game::car_here_text(CarHere::Illegal)) == "is not a legal setup");
    const game::CarFacts f = game::online_car_facts(db, nullptr, bad);
    CHECK(f.setup == "final gear out of range" && game::online_car_facts(db, nullptr, ok).setup.empty());
    game::CarRules any;
    CHECK(!game::car_meets_rules(bad, f, any, &why) && why == "not a legal setup: final gear out of range");
}

namespace {

// A disc's program tables (its CORE.GT3); nullopt when the exact core does not know the program.
std::optional<sim::exact::ProgramData> program_of(Disc& disc) {
    std::optional<Bytes> core = disc.volume().read_program();
    if (!core) return std::nullopt;
    try {
        return sim::exact::ProgramData::load(CoreImage::decode(*core));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Every car a lobby lists from a database (its cars but mod cars, its arcade cars on a base car), by a key that names the
// same car on every disc (the arcade label for an arcade car); with `parts`, also every car with each of its upgrade parts
// alone (a garage car of the stock car with that part fitted, its stock settings).
std::vector<std::pair<std::string, game::OnlineCar>> lobby_cars(const data::CarDatabase& db, bool parts) {
    std::vector<std::pair<std::string, game::OnlineCar>> out;
    for (const data::Car& c : db.cars()) {
        if (db.mod_car(c.label)) continue;
        out.push_back({c.label, game::online_car_from_db(db, c.label, 0)});
        if (!parts) continue;
        for (int goods = 0; goods < game::kGoodsCount; ++goods) {
            const std::optional<game::PartOffer> part = game::find_part(db, c.label, goods);
            const int slot = part ? game::part_slot(game::goods_info(goods).table) : -1;
            if (slot < 0 || c.parts[size_t(slot)] == part->id) continue;
            game::GarageCar g;
            g.car = c.label;
            g.parts = c.parts;
            g.parts[size_t(slot)] = part->id;
            g.settings = sim::stock_settings(db, g.car, g.parts);
            out.push_back({c.label + "+" + part->label, game::online_car_from_garage(db, g)});
        }
    }
    for (const data::CarVariant& v : db.arcade_cars())
        if (db.car(v.car)) out.push_back({"arcade " + v.label, game::online_car_from_arcade(db, v.label, 0)});
    return out;
}

} // namespace

TEST(net_car_params_disc) {
    // Every car of a disc: its block builds, the same digest twice, and the fold of every digest (compare it between an
    // arm64 and an x86-64 build of the tests: the same bytes everywhere).
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    std::optional<sim::exact::ProgramData> d = program_of(*disc);
    CHECK(d.has_value());
    if (!d) return;
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    int built = 0, none = 0;
    u64 h = 1469598103934665603ull;
    for (const auto& [key, c] : lobby_cars(db, false)) {
        const auto x = game::online_car_digest(db, *d, c);
        if (!x) {
            ++none;
            std::printf("  no block for %s\n", key.c_str());
            continue;
        }
        ++built;
        CHECK(game::online_car_digest(db, *d, c) == x);
        for (u8 b : *x) h = (h ^ b) * 1099511628211ull;
    }
    std::printf("  %s: %d cars' blocks, %d not built; digests fold %016llx\n", region_name(disc->info().region), built, none,
                (unsigned long long)h);
    CHECK(built > 0 && none == 0);
}

TEST(net_car_params_regions) {
    // GT3R_COMPARE_DISC=<another region's disc>: which cars build another parameter block there (the lobby says "is
    // different on this disc"), every car and every car with each upgrade part, NTSC-U's rules on both.
    Disc* disc = test::any_region_disc();
    const char* other_path = std::getenv("GT3R_COMPARE_DISC");
    if (!disc || !other_path) SKIP("needs GT3R_DISC and GT3R_COMPARE_DISC");
    std::unique_ptr<Disc> other = Disc::open(other_path);
    std::optional<sim::exact::ProgramData> da = program_of(*disc), dbp = program_of(*other);
    CHECK(da && dbp);
    if (!da || !dbp) return;
    data::CarDatabase a, b;
    CHECK(a.load(disc->volume()) && b.load(other->volume()));
    std::map<std::string, game::OnlineCar> theirs;
    for (auto& [key, c] : lobby_cars(b, true)) theirs.emplace(key, c);
    int same = 0, differ = 0, missing = 0, cars_same = 0, cars_differ = 0;
    for (const auto& [key, c] : lobby_cars(a, true)) {
        auto it = theirs.find(key);
        if (it == theirs.end()) {
            ++missing;
            continue;
        }
        const auto x = game::online_car_digest(a, *da, c), y = game::online_car_digest(b, *dbp, it->second);
        const bool whole_car = key.find('+') == std::string::npos;
        if (x && y && *x == *y) ++same, cars_same += whole_car;
        else {
            ++differ, cars_differ += whole_car;
            if (whole_car || std::getenv("GT3R_COMPARE_ALL")) std::printf("  differs: %s\n", key.c_str());
        }
    }
    std::printf("  %s against %s: %d alike, %d differ (cars and arcade cars: %d alike, %d differ), %d not on the other disc\n",
                region_name(disc->info().region), region_name(other->info().region), same, differ, cars_same, cars_differ, missing);
    CHECK(same > 0);
}

TEST(net_car_here_disc) {
    // Every car of a disc races on that disc as its own descriptor says (bar cars without a
    // model there), and the arcade Viper races when its base car carries its twin's label.
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    data::CarDatabase db;
    CHECK(db.load(disc->volume()));
    std::optional<sim::exact::ProgramData> d = program_of(*disc);
    CHECK(d.has_value());
    if (!d) return;
    const sim::exact::ProgramData* disc_program = &*d;
    auto disc_announced = [&](game::OnlineCar c) {
        c.digest = game::online_car_digest(db, *d, c);
        return wire(c);
    };
    auto has_model = [&](const std::string& m) { return disc->volume().find("cars/day/" + m) != nullptr; };
    int ok = 0, no_model = 0, other = 0;
    auto count = [&](CarHere h) { (h == CarHere::Ok ? ok : h == CarHere::NoModel ? no_model : other)++; };
    for (const data::Car& c : db.cars())
        if (!db.mod_car(c.label)) count(game::online_car_here(db, disc_program, disc_announced(game::online_car_from_db(db, c.label, 0)), has_model));
    for (const data::CarVariant& v : db.arcade_cars())
        if (db.car(v.car)) count(game::online_car_here(db, disc_program, disc_announced(game::online_car_from_arcade(db, v.label, 0)), has_model));
    std::printf("  %d cars race here, %d without a model on this disc\n", ok, no_model);
    CHECK(other == 0 && ok > 0);
    game::OnlineCar viper = game::online_car_from_arcade(db, "viper_gtsr_oreca_00_597", 0);
    if (viper.car.empty()) SKIP("no arcade Viper on this disc");
    const std::string twin = viper.car == "viper_gtsr_oreca_00" ? "dodge_viper_gtsr_oreca_00" : "viper_gtsr_oreca_00";
    game::OnlineCar from_twin = disc_announced(viper); // another region's disc: the twin's label
    from_twin.car = twin;
    CHECK(game::online_car_spec(db, from_twin).has_value());
    CHECK(game::online_car_base(db, from_twin) == viper.car);
    const CarHere h = game::online_car_here(db, disc_program, from_twin, has_model);
    CHECK(!game::car_here_blocks(h));
    std::printf("  arcade Viper (%s here) named as %s: %s\n", viper.car.c_str(), twin.c_str(),
                h == CarHere::Ok ? "races" : game::car_here_text(h));
}

TEST(net_car_setup_disc) {
    // Every garage car GT Mode can build on a disc is a legal setup: each stock car, each with each upgrade the tune shop
    // fits (GtMode's fitting, settings as it leaves them), and each with the adjustable gearbox, its Settings sliders set
    // to their top ends, then every other upgrade fitted (parts fitted after a gearbox leave its ratios), then every slider
    // to its bottom end.
    Disc* disc = test::any_region_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    Vfs vfs(&disc->volume());
    auto data = std::make_shared<game::GtData>();
    CHECK(data->load([&](const std::string& path) { return vfs.read(path); }));
    game::GtMode gt(data);
    const data::CarDatabase& db = gt.db();
    std::vector<std::string> sliders = {
        "slider_gear_auto_level", "slider_gear_ratio_1", "slider_gear_ratio_2", "slider_gear_ratio_3", "slider_gear_ratio_4",
        "slider_gear_ratio_5", "slider_gear_ratio_6", "slider_gear_ratio_7", "slider_gear_ratio_final", "slider_rate_f",
        "slider_rate_r", "slider_height_f", "slider_height_r", "slider_damper_f", "slider_damper_r", "slider_bound_f",
        "slider_bound_r", "slider_rebound_f", "slider_rebound_r", "slider_camber_f", "slider_camber_r", "slider_toe_f",
        "slider_toe_r", "slider_stabilizer_f", "slider_stabilizer_r", "slider_brake_f", "slider_brake_r",
        "slider_lsd_init_f", "slider_lsd_init_r", "slider_lsd_accl_f", "slider_lsd_accl_r", "slider_lsd_decl_f",
        "slider_lsd_decl_r", "slider_elec_aycc", "slider_aero_f", "slider_aero_r", "slider_elec_ascc", "slider_elec_tcsc",
        "slider_elec_vcdc"};
    // Each slider the car's pages open, to one end, committed one by one (the gear ratios' ranges follow the auto level).
    auto to_ends = [&](game::GarageCar& c, bool top) {
        for (const std::string& item : sliders) {
            if (!game::slider_enabled(gt, c, item)) continue;
            const std::optional<game::SliderRange> r = game::slider_range(gt, c, c, item);
            if (!r || r->fixed()) continue;
            game::GarageCar work = c;
            if (game::slider_set(gt, c, work, item, top ? r->hi() : r->lo(), "")) c = work;
        }
    };
    int cars = 0, builds = 0, bad = 0, probes = 0, refusals = 0;
    u64 other_su = 0;
    auto check = [&](const game::GarageCar& c, const std::string& what) {
        ++builds;
        const std::string why = game::garage_setup_problem(db, c.car, c.parts, c.settings);
        if (!why.empty() && ++bad <= 20) std::printf("  %s: %s\n", what.c_str(), why.c_str());
    };
    constexpr int kVariableGearbox = 22; // goods: the adjustable transmission
    for (const data::Car& car : db.cars()) {
        if (db.mod_car(car.label)) continue;
        const std::optional<game::GarageCar> stock = gt.make_car(car.label);
        if (!stock) continue;
        ++cars;
        check(*stock, car.label);
        std::vector<const game::ShopItem*> upgrades;
        for (const game::ShopItem& item : game::shop_items())
            if (item.flow != game::ShopItem::Flow::BuyOnly && game::find_part(db, car.label, item.goods)) upgrades.push_back(&item);
        for (const game::ShopItem* item : upgrades) {
            game::GarageCar c = *stock;
            if (gt.fit_goods(c, item->goods)) check(c, car.label + " + " + item->item);
        }
        game::GarageCar c = *stock;
        gt.fit_goods(c, kVariableGearbox);
        to_ends(c, true);
        check(c, car.label + " top ends");
        for (const game::ShopItem* item : upgrades)
            if (item->goods != kVariableGearbox) gt.fit_goods(c, item->goods);
        check(c, car.label + " tuned after the gearbox");
        to_ends(c, false);
        check(c, car.label + " tuned, bottom ends");
        // And what no page or shop gives it: refused.
        auto refused = [&](game::GarageCar x, const std::string& expect) {
            ++probes;
            const std::string why = game::garage_setup_problem(db, x.car, x.parts, x.settings);
            if (why == expect) ++refusals;
            else std::printf("  %s: \"%s\", not \"%s\"\n", car.label.c_str(), why.c_str(), expect.c_str());
        };
        game::GarageCar x = c;
        x.settings.spring[0] = 1000;
        refused(x, "front spring rate out of range");
        x = c;
        x.settings.gears[1] = 30000;
        refused(x, "gear 1 out of range");
        x = c;
        const size_t su = size_t(game::part_slot(18));
        const std::vector<u64> own = game::fittable_parts(db, car.label, int(su));
        if (other_su && std::find(own.begin(), own.end(), other_su) == own.end()) {
            x.parts[su] = other_su;
            refused(x, "a part GT Mode does not fit on this car (suspension)");
        }
        other_su = c.parts[su]; // this car's racing suspension, for the next car
    }
    std::printf("  %s: %d cars, %d garage cars checked, %d not legal; %d of %d tampered cars refused\n",
                region_name(disc->info().region), cars, builds, bad, refusals, probes);
    CHECK(cars > 0 && bad == 0 && refusals == probes);
}
