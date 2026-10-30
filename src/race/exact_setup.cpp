// The exact core's race setup from the game's; see exact_setup.h.

#include "race/exact_setup.h"

#include "base/log.h"
#include "disc/core_image.h"
#include "formats/db/gt_db.h"
#include "game/data/car_database.h"
#include "game/net_car.h"
#include "race/arcade_car.h"
#include "vfs/vfs.h"

#include <cmath>
#include <mutex>

namespace gt3r::race {

namespace {

u64 hash_bytes(ByteSpan b) {
    u64 h = 1469598103934665603ull;
    for (u8 c : b) h = (h ^ c) * 1099511628211ull;
    return h;
}

std::mutex g_mutex;

} // namespace

std::shared_ptr<const ex::ProgramData> exact_program_data(const Vfs& vfs, std::string* error) {
    static u64 key = 0;
    static std::shared_ptr<const ex::ProgramData> data;
    static std::string err;
    std::lock_guard<std::mutex> lock(g_mutex);
    std::optional<Bytes> core = vfs.read(Vfs::kProgramFile);
    if (!core || core->empty()) {
        if (error) *error = "the disc's program (CORE.GT3) cannot be read";
        return nullptr;
    }
    const u64 k = hash_bytes(*core);
    if (k != key) {
        key = k;
        data.reset();
        err.clear();
        try {
            data = std::make_shared<const ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(*core)));
            const RacePhysics how = race_physics(*data);
            LOG_INFO("Race: %s physics%s", race_core_name(how.core), how.own ? "" : " (this disc's own physics isn't supported yet)");
        } catch (const std::exception& e) {
            err = e.what();
        }
        if (!data) LOG_WARN("Race: exact physics unavailable: %s", err.c_str());
    }
    if (!data && error) *error = err;
    return data;
}

RacePhysics race_physics(const ex::ProgramData& d) {
    // GT3's releases race on GT3's core; any other program (another build, another game on the engine) too, with GT3's
    // tables and NTSC-U's rules where its own are not recognised, until it has a core of its own (chosen here).
    return {RaceCore::Gt3, d.known()};
}

const char* race_core_name(RaceCore c) {
    switch (c) {
    case RaceCore::Gt3: break;
    }
    return "Gran Turismo 3";
}

std::shared_ptr<const ex::RaceCourse> exact_course(ByteSpan file, const std::string& label, const ex::ProgramData& d) {
    static u64 key = 0;
    static const ex::ProgramData* key_data = nullptr;
    static std::shared_ptr<const ex::RaceCourse> course;
    std::lock_guard<std::mutex> lock(g_mutex);
    const u64 k = hash_bytes(file) ^ hash_bytes(ByteSpan(reinterpret_cast<const u8*>(label.data()), label.size()));
    if (k != key || key_data != &d || !course) {
        course = ex::RaceCourse::load(file, d);
        key = k, key_data = &d;
    }
    return course;
}

int exact_grid_figure(const data::CarDatabase& db, const ex::ProgramData& d, const ex::Equipment& eq, bool hp) {
    const ex::Showroom s = ex::showroom_build(db, d, eq);
    return gt_grid_figure(s.mass(), ex::shown_power(s.peak.ps, hp));
}

std::string exact_base_car(const data::CarDatabase& db, const std::string& label) {
    if (db.car(label)) return label;
    if (const data::CarVariant* v = db.variant(label)) return v->car;
    for (const data::CarVariant& v : db.arcade_cars())
        if (v.label == label || v.label == "_" + label) return v.car;
    return {};
}

namespace {

// An electric mod car on the exact core: its donor car's engine (the exact core has no EV drive yet): the donor's stock
// engine, engine parts, drivetrain and gearbox records, and their settings words (the gear ratios, final drive and slider,
// the front torque split, the turbo's).
ex::Equipment donor_powertrain(const data::CarDatabase& db, const data::ModCar& m, ex::Equipment eq) {
    const data::Car* donor = db.car(m.donor);
    if (!donor) return eq;
    const ex::Equipment d = ex::stock_equipment(db, m.donor, donor->parts);
    using T = data::TableId;
    for (T t : {T::Engine, T::PortPolish, T::EngineBalance, T::Displacement, T::Computer, T::NaTune, T::TurbineKit, T::Drivetrain,
                T::Flywheel, T::Clutch, T::PropellerShaft, T::Gear, T::Intercooler, T::Muffler})
        eq.part[size_t(t)] = d.part[size_t(t)];
    auto word = [&](u32 off) { eq.setting[(off - 0xF0) / 4] = d.setting[(off - 0xF0) / 4]; };
    for (u32 off = 0xF0; off <= 0x118; off += 4) word(off); // the gears, final drive, slider and front torque
    for (u32 off = 0x12C; off < 0x144; off += 4) word(off); // the turbo's
    return eq;
}

} // namespace

std::optional<ex::Equipment> exact_equipment(const data::CarDatabase& db, const ExactCarSource& src, std::string* error) {
    std::optional<ex::Equipment> eq = exact_equipment_as_built(db, src, error);
    if (eq)
        if (const data::ModCar* m = db.mod_car(exact_base_car(db, src.label)); m && m->electric) eq = donor_powertrain(db, *m, *eq);
    return eq;
}

std::optional<ex::Equipment> exact_equipment_as_built(const data::CarDatabase& db, const ExactCarSource& src, std::string* error) {
    const std::string& label = src.label;
    if (src.parts) { // a garage car / a recorded car: its own parts and settings
        const std::string base = exact_base_car(db, label);
        if (base.empty()) {
            if (error) *error = "unknown car " + label;
            return std::nullopt;
        }
        if (src.settings) return ex::make_equipment(db, base, *src.parts, *src.settings);
        return ex::stock_equipment(db, base, *src.parts);
    }
    // As the race entries are built (the set-car): a variant (table 31) as its base car with the variant's parts,
    // else an arcade car (table 35), else the car (table 30) with its own parts.
    if (const data::CarVariant* v = db.variant(label)) {
        std::array<u64, 27> parts{};
        if (const data::Car* b = db.car(v->car)) parts = b->parts;
        for (size_t k = 0; k < 27; ++k)
            if (v->parts[k]) parts[k] = v->parts[k];
        return ex::stock_equipment(db, v->car, parts);
    }
    for (const data::CarVariant& a : db.arcade_cars()) {
        if (a.label != label && a.label != "_" + label) continue;
        ex::Equipment eq = ex::arcade_equipment(db, a);
        if (src.tire_stage >= 0) { // the car page's tyres: both tyre parts to the stage's records when the car has them
            const char sfx = char('a' + src.tire_stage);
            for (data::TableId t : {data::TableId::FrontTire, data::TableId::RearTire}) {
                const std::string l = std::string(t == data::TableId::FrontTire ? "ft_" : "rt_") + a.car + "_" + sfx;
                if (db.record(t, l)) eq.part[size_t(t)] = db::id_hash(l);
            }
        }
        return eq;
    }
    if (const data::Car* c = db.car(label)) return ex::stock_equipment(db, label, c->parts);
    if (error) *error = "unknown car " + label;
    return std::nullopt;
}

ExactCarSource exact_online_source(const data::CarDatabase& db, const game::OnlineCar& c, const std::string& course,
                                   sim::EngineCondition* condition) {
    using K = game::OnlineCar::Kind;
    if (condition) *condition = {};
    if (c.kind == K::Garage) {
        if (condition)
            *condition = {0, std::isfinite(c.engine_wear_m) ? c.engine_wear_m : 0.0f, std::isfinite(c.oil_m) ? c.oil_m : 0.0f};
        return {c.car, c.parts, c.settings};
    }
    if (c.kind == K::Arcade)
        if (std::optional<ArcadeCar> ac = arcade_car(db, c.arcade, 0, course)) return {ac->label, std::nullopt, std::nullopt, ac->stage};
    ExactCarSource s;
    s.label = c.car;
    return s;
}

u8 exact_course_category(const data::CarDatabase& db, const std::string& course) {
    const ByteSpan* r = db.record(data::TableId::Course, course);
    return r && r->size() >= 9 ? ex::race_course_category(*r) : 0; // (the database's course records are 9 bytes or more)
}

u64 exact_data_fingerprint(const data::CarDatabase& db, std::span<const ex::StartEntry> entries, ByteSpan course_file) {
    u64 h = hash_bytes(course_file);
    auto mix = [&](data::TableId t, u64 id) {
        const ByteSpan* r = id ? db.record(t, id) : nullptr;
        const u64 v = r ? hash_bytes(*r) : 0;
        h = (h ^ v) * 1099511628211ull;
        return r;
    };
    for (const ex::StartEntry& e : entries) {
        mix(data::TableId::Car, e.eq.car);
        for (size_t t = 0; t < e.eq.part.size(); ++t) {
            const ByteSpan* r = mix(data::TableId(t), e.eq.part[t]);
            if (r && (t == size_t(data::TableId::FrontTire) || t == size_t(data::TableId::RearTire)) && r->size() >= 0x58) {
                for (u32 k = 0; k < 3; ++k)
                    mix(data::TableId::TireCompound, rd64(*r, 0x20 + 8 * k)), mix(data::TableId::TireForceVol, rd64(*r, 0x40 + 8 * k));
                mix(data::TableId::TireSize, rd64(*r, 0x18));
            }
        }
    }
    return h;
}

bool exact_has_pit_box(const ex::RaceCourse& c) { return c.ai.lines[4].size() >= 4; }

} // namespace gt3r::race
