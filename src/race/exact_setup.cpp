// The exact core's race setup from the game's; see exact_setup.h.

#include "race/exact_setup.h"

#include "base/log.h"
#include "disc/core_image.h"
#include "formats/db/gt_db.h"
#include "game/data/car_database.h"
#include "game/net_car.h"
#include "race/arcade_car.h"
#include "vfs/vfs.h"

#include <map>
#include <cmath>
#include <cstring>
#include <future>
#include <mutex>
#include <system_error>

namespace gt3r::race {

namespace {

u64 hash_bytes(ByteSpan b) {
    u64 h = 1469598103934665603ull;
    for (u8 c : b) h = (h ^ c) * 1099511628211ull;
    return h;
}

// 8 bytes a step, for the course cache's check that a file is the one it holds. Never stored, so free to differ from
// hash_bytes (which the race records' data fingerprint keeps).
u64 quick_hash(ByteSpan b) {
    u64 h = 0x9E3779B97F4A7C15ull ^ u64(b.size());
    auto mix = [&h](u64 w) {
        w *= 0x87C37B91114253D5ull;
        w = (w << 31) | (w >> 33);
        h ^= w * 0x4CF5AD432745937Full;
        h = ((h << 27) | (h >> 37)) * 5 + 0x52DCE729;
    };
    size_t i = 0;
    for (; i + 8 <= b.size(); i += 8) {
        u64 w;
        std::memcpy(&w, b.data() + i, 8);
        mix(w);
    }
    u64 tail = 0;
    if (i < b.size()) std::memcpy(&tail, b.data() + i, b.size() - i);
    mix(tail);
    return h;
}

std::mutex g_mutex;

struct Decoded {
    std::shared_ptr<const ex::ProgramData> data;
    std::string error;
};

Decoded decode_program(const Bytes& core) {
    Decoded d;
    try {
        d.data = std::make_shared<const ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(core)));
        const RacePhysics how = race_physics(*d.data);
        LOG_INFO("Race: %s physics%s", race_core_name(how.core), how.own ? "" : " (this disc's own physics isn't supported yet)");
    } catch (const std::exception& e) {
        d.error = e.what();
    }
    if (!d.data) LOG_WARN("Race: exact physics unavailable: %s", d.error.c_str());
    return d;
}

// The open disc's program data (prepare_program_data), keyed by its volume. CORE.GT3 is read from the volume, never
// from a mod (Vfs::read), so changing mods keeps it.
const Volume* g_prepared_volume = nullptr;
std::shared_future<Decoded> g_prepared;

} // namespace

void prepare_program_data(const Vfs& vfs) {
    std::optional<Bytes> core = vfs.read(Vfs::kProgramFile);
    if (!core || core->empty()) return; // exact_program_data says why
    std::shared_future<Decoded> work;
    try {
        work = std::async(std::launch::async, [b = std::move(*core)]() mutable {
                   Decoded d = decode_program(b);
                   Bytes().swap(b); // (the result is kept as long as the disc: not the program)
                   return d;
               }).share();
    } catch (const std::system_error&) {
        return; // no thread: the first race works it out
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_prepared_volume = vfs.base();
    g_prepared = std::move(work);
}

void forget_program_data() {
    std::shared_future<Decoded> work;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        work = std::move(g_prepared);
        g_prepared_volume = nullptr;
    }
    if (work.valid()) work.wait();
}

std::shared_ptr<const ex::ProgramData> exact_program_data(const Vfs& vfs, std::string* error) {
    std::shared_future<Decoded> prepared;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_prepared.valid() && g_prepared_volume == vfs.base()) prepared = g_prepared;
    }
    if (prepared.valid()) {
        const Decoded& d = prepared.get();
        if (!d.data && error) *error = d.error;
        return d.data;
    }
    // Without one (a Vfs of no opened disc: the tests), or with no thread: worked out here, once per program.
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
        Decoded d = decode_program(*core);
        data = std::move(d.data);
        err = std::move(d.error);
    }
    if (!data && error) *error = err;
    return data;
}

std::shared_ptr<const ex::ProgramData> record_program_data(const std::shared_ptr<const ex::ProgramData>& d, u64 program) {
    if (!d) return nullptr;
    if (program == d->fingerprint()) return d;
    if (!d->changed()) return nullptr;
    auto shared = std::make_shared<const ex::ProgramData>(d->gt3_tables());
    return program == shared->fingerprint() ? shared : nullptr;
}

RacePhysics race_physics(const ex::ProgramData& d) {
    // GT3's releases and builds of its code race on GT3's core; any other program (another game on the engine) too, with
    // GT3's tables and its nearest release's rules where its own are not recognised, until it has a core of its own
    // (chosen here).
    return {RaceCore::Gt3, d.known()};
}

std::optional<sim::Program> online_rules(const ex::ProgramData& d) {
    if (sim::concept_program(d.program())) return std::nullopt;
    return sim::Program::NtscU;
}

ex::ProgramData online_program_data(const ex::ProgramData& d) {
    const std::optional<sim::Program> rules = online_rules(d);
    if (!rules) return d;
    ex::ProgramData out = d.gt3_tables();
    out.set_behaviour({*rules, 60});
    return out;
}

const char* race_core_name(RaceCore c) {
    switch (c) {
    case RaceCore::Gt3: break;
    }
    return "Gran Turismo 3";
}

std::shared_ptr<const ex::RaceCourse> exact_course(ByteSpan file, const std::string& label, const ex::ProgramData& d) {
    static u64 key = 0, key_program = 0;
    static const ex::ProgramData* key_data = nullptr;
    static std::shared_ptr<const ex::RaceCourse> course;
    std::lock_guard<std::mutex> lock(g_mutex);
    const u64 k = quick_hash(file) ^ hash_bytes(ByteSpan(reinterpret_cast<const u8*>(label.data()), label.size()));
    // (and the program by its fingerprint: another disc's program data may be made where a closed one's was)
    const u64 program = d.fingerprint();
    if (k != key || key_data != &d || key_program != program || !course) {
        course = ex::RaceCourse::load(file, d);
        key = k, key_data = &d, key_program = program;
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

std::optional<ex::Equipment> exact_equipment(const data::CarDatabase& db, const ExactCarSource& src, std::string* error) {
    const std::string& label = src.label;
    if (src.parts) { // a garage car / a recorded car: its own parts and settings
        const std::string base = exact_base_car(db, label);
        if (base.empty()) {
            if (error) *error = "unknown car " + label;
            return std::nullopt;
        }
        if (src.settings) {
            ex::Equipment eq = ex::make_equipment(db, base, *src.parts, *src.settings);
            // A garage car made from an arcade row (Gran Turismo Concept keeps one per class entry, concept §10) carries
            // the row's engine output factor (the row's +0x10A, EQ +0x1B8: 0x2361D8), which the settings do not hold.
            for (const data::CarVariant& a : db.arcade_cars())
                if (a.label == label && a.bytes.size() >= 0x10C) eq.setting[(0x1B8 - 0xF0) / 4] = rd16(a.bytes, 0x10A);
            return eq;
        }
        return ex::stock_equipment(db, base, *src.parts);
    }
    // As the race entries are built: an event's entry (table 31) from its record, as the race information builds it (0x238550);
    // else an arcade car (table 35, which the set-car looks up first: 0x231DB0); else the car (table 30) with its own parts.
    if (const data::CarVariant* v = db.variant(label)) return ex::variant_equipment(db, *v);
    for (const data::CarVariant& a : db.arcade_cars()) {
        if (a.label != label && a.label != "_" + label) continue;
        ex::Equipment eq = ex::variant_equipment(db, a);
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
        if (std::optional<ArcadeCar> ac = arcade_car_on(db, c.arcade, c.tires >= 0 ? c.tires : 10, course))
            return {ac->label, std::nullopt, std::nullopt, ac->stage};
    // A disc or mod car with the lobby's tires or settings: its parts with those tires, and those settings.
    if (c.tires >= 0 || c.tune)
        if (std::optional<std::array<u64, 27>> parts = game::online_car_parts(db, c))
            if (std::optional<sim::CarSettings> settings = game::online_car_settings(db, c)) return {c.car, parts, settings};
    ExactCarSource s;
    s.label = c.car;
    return s;
}

u8 exact_course_category(const data::CarDatabase& db, const std::string& course) {
    const ByteSpan* r = db.record(data::TableId::Course, course);
    return r && r->size() >= 9 ? ex::race_course_category(*r) : 0; // (the database's course records are 9 bytes or more)
}

namespace {
// Mixed into the fingerprint for an electric mod car on the electric drive. Change it when the drive changes how such a
// car races, so replays recorded with the old drive are refused rather than played differently.
constexpr u64 kElectricDriveVersion = 0xE1EC7D01;
} // namespace

u64 exact_data_fingerprint(const data::CarDatabase& db, std::span<const ex::StartEntry> entries, ByteSpan course_file,
                           const std::shared_ptr<const ex::RaceCourse>& course) {
    u64 h = course ? course->file_hash : hash_bytes(course_file);
    std::map<const ByteSpan*, u64> hashed; // (cars share most of their parts' records)
    auto mix = [&](data::TableId t, u64 id) {
        const ByteSpan* r = id ? db.record(t, id) : nullptr;
        u64 v = 0;
        if (r) {
            const auto [it, fresh] = hashed.try_emplace(r);
            if (fresh) it->second = hash_bytes(*r);
            v = it->second;
        }
        h = (h ^ v) * 1099511628211ull;
        return r;
    };
    for (const ex::StartEntry& e : entries) {
        mix(data::TableId::Car, e.eq.car);
        // An electric mod car's drive is not in its records: its regen and the drive's version, so a build without the
        // drive, or with another one, refuses the race instead of running it differently.
        if (const ex::ElectricDrive d = ex::electric_drive(&db, e.eq); d.on)
            for (u64 v : {kElectricDriveVersion, u64(d.regen_torque.bits), u64(d.regen_power.bits)})
                h = (h ^ v) * 1099511628211ull;
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
