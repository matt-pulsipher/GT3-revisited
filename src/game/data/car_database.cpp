#include "game/data/car_database.h"

#include "base/config.h"
#include "base/log.h"
#include "game/data/mod_cars.h"
#include "disc/volume.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>

namespace gt3r::data {

namespace {

constexpr const char* kTableNames[] = {
    "brake",      "brake_controller", "steer",       "chassis",      "lightweight",  "racing_modify",
    "engine",     "port_polish",      "engine_balance", "displacement", "computer",   "na_tune",
    "turbine_kit", "drivetrain",      "flywheel",    "clutch",       "propeller_shaft", "gear",
    "suspension", "intercooler",      "muffler",     "lsd",          "tcsc",         "ascc",
    "wheel",      "tire_size",        "tire_force_vol", "tire_compound", "front_tire", "rear_tire",
    "car",        "car_variant",      "event",       "regulation",   "course",       "arcade_car",
};
static_assert(std::size(kTableNames) == size_t(TableId::Count));

// Tables whose records are purchasable parts with the common prefix.
bool is_part_table(TableId t) {
    int i = int(t);
    return i == 0 || i == 1 || i == 4 || (i >= 7 && i <= 21) || i == 28 || i == 29;
}

} // namespace

const char* table_name(TableId t) {
    size_t i = size_t(t);
    return i < std::size(kTableNames) ? kTableNames[i] : "?";
}

const char* drive_type_name(u8 t) {
    switch (t) {
    case 0: return "FR";
    case 1: return "FF";
    case 2: return "4WD";
    case 3: return "MR";
    case 4: return "RR";
    default: return "?";
    }
}

const std::array<int, 27> kCarPartTables = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, // br bc steer ch lw rm en pp eb ds co nt tk dt
    14, 15, 16, 21, 17, 18, 19, 20, 28, 29, 23, 22,         // fw cl ps ls ge su ic mu ft rt ascc tcsc
    24,                                                      // always 0 on disc; probably the wheel slot
};

float Engine::curve_peak_ps() const {
    float best = 0;
    for (size_t i = 0; i < rpm.size() && i < torque.size(); ++i) best = std::max(best, torque[i] * rpm[i] / 716.2f);
    return best;
}

bool CarDatabase::load(const Vfs& vfs, const std::string& suffix) {
    return load([&](const std::string& p) { return vfs.read(p); }, suffix); // the reader serves mods/.cars
}

bool CarDatabase::load(const Volume& vol, const std::string& suffix) {
    return load(
        [&](const std::string& p) -> std::optional<Bytes> {
            const Volume::Entry* e = vol.find(p);
            if (!e) return std::nullopt;
            return vol.read(*e);
        },
        suffix);
}

bool CarDatabase::load(const Reader& read, const std::string& requested) {
    // A data set without the requested regional files (synthetic test worlds) uses the US one.
    const std::string suffix =
        requested != "_us" && !read("database/paramdb" + requested + ".db") ? std::string("_us") : requested;
    auto need = [&](const std::string& path) {
        auto b = read(path);
        if (!b) throw FormatError(path + " not found");
        return std::move(*b);
    };
    region_ = suffix == "_eu" ? 2 : suffix.empty() ? 0 : 1;
    extra_records_.clear();
    mod_cars_.clear();
    mod_makers_.clear();
    mod_car_errors_.clear();
    try {
        paramdb_ = need("database/paramdb" + suffix + ".db");
        Bytes str = need("database/paramstr" + suffix + ".db");
        Bytes uni = need("database/paramunistr" + suffix + ".db");
        paramstr_ = db::StringTable::parse(str);
        unistr_ = db::StringTable::parse(uni);
        // The id name index is a development aid (CORE.GT3 never loads it);
        // ids still work without it, only labels go missing.
        auto idx = read("database/.id_db_idx" + suffix + ".db");
        auto ids = read("database/.id_db_str" + suffix + ".db");
        if (idx && ids) names_.load(*idx, *ids);
        auto tables = db::parse_archive(paramdb_);
        if (tables.size() != size_t(TableId::Count))
            throw FormatError("paramdb: expected 36 tables, found " + std::to_string(tables.size()));
        for (size_t i = 0; i < tables.size(); ++i) {
            tables_[i] = db::Table::parse(tables[i]);
            by_id_[i].clear();
            for (size_t r = 0; r < tables_[i].size(); ++r) by_id_[i].emplace(tables_[i].id_of(r), r);
        }
        // racemode.db labels (event race modes) are plain strings in the game.
        for (const char* m : {"RACE_MODE_SINGLE", "RACE_MODE_ENDULANCE", "RACE_MODE_LICENSE", "RACE_MODE_FREE_TIME_TRIAL",
                              "RACE_MODE_0_400", "RACE_MODE_0_1000", "RACE_MODE_TIME_ATTACK", "RACE_MODE_2P_BATTLE",
                              "RACE_MODE_MAX_SPEED_CHALLENGE", "RACE_MODE_DEMO", "RACE_MODE_TIME_TRIAL",
                              "RACE_MODE_FREE_PRACTICE", "RACE_MODE_REPLAY", "RACE_MODE_LOGGER"})
            names_.add(m);
        decode();
        // Mod cars (game/data/mod_cars.h), listed by the VFS: "<mod id>\t<host path>" lines.
        if (auto list = read(Vfs::kModCarsList)) {
            std::map<std::string, std::vector<std::string>> by_mod;
            std::vector<std::string> order;
            std::string text(list->begin(), list->end());
            for (std::string_view line : split(text, '\n')) {
                size_t tab = line.find('\t');
                if (tab == std::string_view::npos) continue;
                std::string mod(line.substr(0, tab));
                if (!by_mod.count(mod)) order.push_back(mod);
                by_mod[mod].emplace_back(line.substr(tab + 1));
            }
            for (const std::string& mod : order) {
                std::vector<std::string> cars;
                for (const std::string& f : by_mod[mod]) {
                    if (f.find("/makers/") != std::string::npos) load_mod_maker_file(*this, f, mod);
                    else cars.push_back(f);
                }
                load_mod_car_files(*this, cars, mod);
            }
        }
    } catch (const std::exception& e) {
        error_ = e.what();
        LOG_ERROR("CarDatabase: %s", error_.c_str());
        return false;
    }
    error_.clear();
    return true;
}

const ByteSpan* CarDatabase::record(TableId t, u64 id) const {
    const auto& m = by_id_[size_t(t)];
    auto it = m.find(id);
    return it == m.end() ? nullptr : &tables_[size_t(t)].records[it->second];
}

void CarDatabase::put_record(TableId t, Bytes record) {
    db::Table& tab = tables_[size_t(t)];
    if (tab.record_size) record.resize(tab.record_size, 0);
    if (record.size() < 8) return;
    extra_records_.push_back(std::move(record));
    ByteSpan r = extra_records_.back();
    u64 id = rd64(r, 0);
    auto& index = by_id_[size_t(t)];
    if (auto it = index.find(id); it != index.end()) {
        tab.records[it->second] = r;
    } else {
        index.emplace(id, tab.records.size());
        tab.records.push_back(r);
    }
}

void CarDatabase::add_mod_car(ModCar m) {
    for (ModCar& x : mod_cars_)
        if (x.label == m.label) {
            x = std::move(m);
            return;
        }
    mod_cars_.push_back(std::move(m));
}

void CarDatabase::add_mod_maker(ModMaker m) {
    for (ModMaker& x : mod_makers_)
        if (x.id == m.id) {
            x = std::move(m);
            return;
        }
    mod_makers_.push_back(std::move(m));
}

const ModCar* CarDatabase::mod_car(const std::string& l) const {
    for (const ModCar& m : mod_cars_)
        if (m.label == l) return &m;
    return nullptr;
}

PartInfo CarDatabase::part_info(ByteSpan r, bool tire) const {
    PartInfo p;
    p.label = label(rd64(r, 0));
    p.car = label(rd64(r, 8));
    p.stage = r[16];
    // Tire records end with id slots; their price sits at +0x14.
    p.price = rd32(r, tire ? 0x14 : r.size() - 4);
    return p;
}

void CarDatabase::decode() {
    cars_.clear();
    variants_.clear();
    arcade_.clear();
    courses_.clear();
    regulations_.clear();
    events_.clear();
    car_index_.clear();
    variant_index_.clear();
    event_index_.clear();
    model_index_.clear();

    for (ByteSpan r : table(TableId::Car).records) {
        if (r.size() < 264) throw FormatError("car record too small");
        Car c;
        c.label = label(rd64(r, 0));
        for (size_t i = 0; i < c.parts.size(); ++i) c.parts[i] = rd64(r, 8 + i * 8);
        c.maker_id = rd16(r, 0xE0);
        c.name = pu(r, 0xE4);
        c.name_alt = pu(r, 0xE6);
        if (c.label.empty()) c.label = ps(r, 0xE8);
        c.year = rd16(r, 0xEA);
        c.price = rd32(r, 0xEC);
        c.flags = rd16(r, 0xF0);
        c.maker = pu(r, 0x104);
        c.bytes = r;
        car_index_[c.label] = cars_.size();
        cars_.push_back(std::move(c));
    }
    for (size_t i = 0; i < cars_.size(); ++i) {
        std::string m = model_of(cars_[i]);
        if (!m.empty()) model_index_.emplace(m, i);
    }

    auto variants = [&](TableId t, std::vector<CarVariant>& out) {
        for (ByteSpan r : table(t).records) {
            CarVariant v;
            v.label = label(rd64(r, 0));
            v.car = label(rd64(r, 8));
            for (size_t i = 0; i < v.parts.size(); ++i) v.parts[i] = rd64(r, 16 + i * 8);
            v.settings = r.subspan(0xE8);
            v.bytes = r;
            out.push_back(std::move(v));
        }
    };
    variants(TableId::CarVariant, variants_);
    variants(TableId::ArcadeCar, arcade_);
    for (size_t i = 0; i < variants_.size(); ++i) variant_index_[variants_[i].label] = i;

    for (ByteSpan r : table(TableId::Course).records) {
        Course c;
        c.label = label(rd64(r, 0));
        if (c.label.empty()) c.label = ps(r, 10);
        c.surface = r[8];
        c.name = pu(r, 12);
        if (c.name == "-") c.name.clear();
        courses_.push_back(std::move(c));
    }

    for (ByteSpan r : table(TableId::Regulation).records) {
        Regulation g;
        g.label = label(rd64(r, 0));
        for (size_t o = 8; o + 8 <= r.size(); o += 8) {
            std::string s = label(rd64(r, o));
            if (!s.empty() && s != "-----") g.cars.push_back(std::move(s));
        }
        regulations_.push_back(std::move(g));
    }

    for (ByteSpan r : table(TableId::Event).records) {
        if (r.size() < 504) throw FormatError("event record too small");
        Event e;
        e.label = label(rd64(r, 0));
        e.course = label(rd64(r, 8));
        for (size_t k = 0; k < 32; ++k) {
            u64 id = rd64(r, 16 + k * 8);
            std::string s = label(id);
            if (s == "-" || id == db::id_hash("-")) continue;
            e.entries.push_back(s.empty() ? "?" : s);
        }
        u64 reg = rd64(r, 0x190);
        if (reg != db::id_hash("-")) e.regulation = label(reg);
        e.race_mode = label(rd64(r, 0x1E8));
        e.laps = r[0x199];
        e.cars_in_race = r[0x1B5];
        for (size_t k = 0; k < e.prize.size(); ++k) e.prize[k] = rd16(r, 0x1D0 + k * 2);
        e.bytes = r;
        event_index_[e.label] = events_.size();
        events_.push_back(std::move(e));
    }
}

const Car* CarDatabase::car(const std::string& l) const {
    auto it = car_index_.find(l);
    return it == car_index_.end() ? nullptr : &cars_[it->second];
}

const Car* CarDatabase::car_by_model(const std::string& m) const {
    auto it = model_index_.find(m);
    return it == model_index_.end() ? nullptr : &cars_[it->second];
}

std::string CarDatabase::model_of(const Car& c) const {
    auto b = body(c.parts[5]);
    return b ? b->model : std::string();
}

const CarVariant* CarDatabase::variant(const std::string& l) const {
    auto it = variant_index_.find(l);
    return it == variant_index_.end() ? nullptr : &variants_[it->second];
}

const Event* CarDatabase::event(const std::string& l) const {
    auto it = event_index_.find(l);
    return it == event_index_.end() ? nullptr : &events_[it->second];
}

std::optional<PartInfo> CarDatabase::part(TableId t, u64 id) const {
    const ByteSpan* r = record(t, id);
    if (!r || !is_part_table(t)) return std::nullopt;
    return part_info(*r, t == TableId::FrontTire || t == TableId::RearTire);
}

std::vector<PartInfo> CarDatabase::parts_for(TableId t, const std::string& car_label) const {
    std::vector<PartInfo> out;
    if (!is_part_table(t)) return out;
    u64 code = db::id_hash(car_label);
    for (ByteSpan r : table(t).records)
        if (rd64(r, 8) == code) out.push_back(part_info(r, t == TableId::FrontTire || t == TableId::RearTire));
    std::stable_sort(out.begin(), out.end(), [](const PartInfo& a, const PartInfo& b) { return a.stage < b.stage; });
    return out;
}

std::optional<Engine> CarDatabase::engine(u64 id) const {
    const ByteSpan* p = record(TableId::Engine, id);
    if (!p) return std::nullopt;
    ByteSpan r = *p;
    Engine e;
    e.part.label = label(rd64(r, 0));
    e.part.car = label(rd64(r, 8));
    e.layout = pu(r, 0x10);
    e.valvetrain = pu(r, 0x12);
    e.aspiration = pu(r, 0x14);
    e.sound_id = rd16(r, 0x16);
    e.displacement = ps(r, 0x38);
    e.power_ps = rd16(r, 0x3A);
    e.power_rpm = pu(r, 0x3C);
    e.torque_kgfm = rd16(r, 0x3E) / 100.0f;
    e.torque_rpm = pu(r, 0x40);
    e.torque_scale_pct = r[0x42];
    e.unknown_43 = r[0x43];
    e.unknown_44 = r[0x44];
    e.rpm_limit_x100 = r[0x45];
    e.unknown_46 = r[0x46];
    size_t n = std::min<size_t>(r[0x57], 16);
    for (size_t i = 0; i < n; ++i) {
        e.rpm.push_back(u16(r[0x47 + i] * 100));
        e.torque.push_back(rd16(r, 0x18 + i * 2) / 100.0f);
    }
    return e;
}

std::optional<Chassis> CarDatabase::chassis(u64 id) const {
    const ByteSpan* p = record(TableId::Chassis, id);
    if (!p) return std::nullopt;
    ByteSpan r = *p;
    Chassis c;
    c.part.label = label(rd64(r, 0));
    c.part.car = label(rd64(r, 8));
    c.front_weight_pct = r[0x10];
    c.length_mm = rd16(r, 0x14);
    c.height_mm = rd16(r, 0x16);
    c.wheelbase_mm = rd16(r, 0x18);
    c.weight_kg = rd16(r, 0x1A);
    c.unknown_1c = r[0x1C];
    c.unknown_1d = r[0x1D];
    return c;
}

std::optional<Body> CarDatabase::body(u64 id) const {
    const ByteSpan* p = record(TableId::RacingModify, id);
    if (!p) return std::nullopt;
    ByteSpan r = *p;
    Body b;
    b.part.label = label(rd64(r, 0));
    b.part.car = label(rd64(r, 8));
    b.part.stage = r[0x10];
    b.model = label(rd64(r, 0x18));
    for (size_t i = 0; i < 4; ++i) b.inertia[i] = read_le<float>(r, 0x20 + i * 4);
    b.weight_permille = rd16(r, 0x30);
    b.front_tread_mm = rd16(r, 0x32);
    b.rear_tread_mm = rd16(r, 0x34);
    b.width_mm = rd16(r, 0x36);
    b.inertia_pct = r[0x38];
    b.unknown_44 = read_le<float>(r, 0x44);
    return b;
}

std::optional<Drivetrain> CarDatabase::drivetrain(u64 id) const {
    const ByteSpan* p = record(TableId::Drivetrain, id);
    if (!p) return std::nullopt;
    ByteSpan r = *p;
    Drivetrain d;
    d.part = part_info(r);
    for (size_t i = 0; i < 3; ++i) d.front_torque[i] = r[0x11 + i];
    d.drive_type = r[0x14];
    d.awd_system = r[0x15];
    for (size_t i = 0; i < d.params.size(); ++i) d.params[i] = rd16(r, 0x16 + i * 2);
    return d;
}

std::optional<Gearbox> CarDatabase::gearbox(u64 id) const {
    const ByteSpan* p = record(TableId::Gear, id);
    if (!p) return std::nullopt;
    ByteSpan r = *p;
    Gearbox g;
    g.part = part_info(r);
    g.gears = r[0x11];
    auto ratio = [&](size_t off) { return s16(rd16(r, off)) / 1000.0f; };
    g.reverse = ratio(0x12);
    for (size_t i = 0; i < std::min<size_t>(g.gears, 7); ++i) g.ratios.push_back(ratio(0x14 + i * 2));
    g.final_drive = ratio(0x22);
    g.final_max = ratio(0x24);
    g.final_min = ratio(0x26);
    g.adjustable = r[0x28] != 0;
    for (size_t i = 0; i < 3; ++i) g.auto_setting[i] = r[0x29 + i];
    return g;
}

std::optional<Turbo> CarDatabase::turbo(u64 id) const {
    const ByteSpan* p = record(TableId::TurbineKit, id);
    if (!p) return std::nullopt;
    ByteSpan r = *p;
    Turbo t;
    t.part = part_info(r);
    for (size_t i = 0; i < t.bytes.size(); ++i) t.bytes[i] = r[0x11 + i];
    t.power_pct = rd16(r, 0x1C);
    t.response_pct = r[0x1B];
    return t;
}

std::optional<Tire> CarDatabase::tire(TableId t, u64 id) const {
    const ByteSpan* p = record(t, id);
    if (!p) return std::nullopt;
    ByteSpan r = *p;
    Tire x;
    x.part = part_info(r, true);
    x.size = id_label(r, 0x18);
    for (size_t i = 0; i < 4; ++i) {
        x.compound[i] = id_label(r, 0x20 + i * 8);
        x.force_vol[i] = id_label(r, 0x40 + i * 8);
    }
    return x;
}

CarDatabase::EngineSound CarDatabase::engine_sound(u64 engine, u64 muffler, u64 turbo) const {
    EngineSound s;
    if (const ByteSpan* e = record(TableId::Engine, engine)) s.id = rd16(*e, 0x16);
    if (const ByteSpan* m = record(TableId::Muffler, muffler)) s.variant = (*m)[0x10];
    if (const ByteSpan* t = record(TableId::TurbineKit, turbo); t && (*t)[0x11]) s.variant += 4;
    return s;
}

CarDatabase::Summary CarDatabase::summarize(const Car& c) const {
    Summary s;
    s.name = c.name;
    s.maker = c.maker;
    s.year = c.year;
    s.price = c.price;
    if (auto e = engine(c.parts[6])) {
        s.aspiration = e->aspiration;
        s.layout = e->layout + " " + e->valvetrain;
        s.displacement = e->displacement;
        s.power_ps = e->power_ps;
        s.power_rpm = e->power_rpm;
        s.torque_kgfm = e->torque_kgfm;
        s.torque_rpm = e->torque_rpm;
    }
    if (auto ch = chassis(c.parts[3])) {
        s.weight_kg = ch->weight_kg;
        s.length_mm = ch->length_mm;
        s.height_mm = ch->height_mm;
        s.wheelbase_mm = ch->wheelbase_mm;
        s.front_weight_pct = ch->front_weight_pct;
    }
    if (auto b = body(c.parts[5])) {
        s.model = b->model;
        s.width_mm = b->width_mm;
    }
    if (auto d = drivetrain(c.parts[13])) s.drive = drive_type_name(d->drive_type);
    if (auto g = gearbox(c.parts[18])) {
        s.gears = g->gears;
        s.final_drive = g->final_drive;
        s.ratios = g->ratios;
    }
    if (auto t = tire(TableId::FrontTire, c.parts[22])) s.front_tire = t->compound[0] + " " + t->size;
    if (auto t = tire(TableId::RearTire, c.parts[23])) s.rear_tire = t->compound[0] + " " + t->size;
    return s;
}

} // namespace gt3r::data
