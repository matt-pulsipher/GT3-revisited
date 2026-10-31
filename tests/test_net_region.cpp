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
#include "game/gt_state.h"
#include "game/net_car.h"
#include "base/sha256.h"
#include "disc/core_image.h"
#include "disc/disc.h"
#include "sim/car_spec.h"
#include "sim/exact/program_data.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>

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
