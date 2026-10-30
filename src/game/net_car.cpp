#include "game/net_car.h"

#include "game/data/car_database.h"
#include "game/data/weight_class.h"
#include "game/gt_mode.h"
#include "game/gt_parts.h"
#include "game/gt_state.h"
#include "base/ps2_float.h"
#include "base/sha256.h"
#include "net/protocol.h"
#include "race/arcade_car.h"
#include "race/exact_car.h"
#include "race/exact_setup.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>

namespace gt3r::game {

namespace {

constexpr u8 kVersion = 1;

// Every CarSettings value in a fixed order (encode / decode walk the same list).
template <class F>
void each_setting(sim::CarSettings& s, F&& f) {
    for (int& v : s.gears) f(v);
    f(s.final_drive), f(s.gear_slider), f(s.front_torque), f(s.brake_front), f(s.brake_rear);
    f(s.downforce_front), f(s.downforce_rear);
    for (int& v : s.turbo) f(v);
    for (int& v : s.camber) f(v);
    for (int& v : s.ride_height) f(v);
    for (int& v : s.toe) f(v);
    for (int& v : s.spring) f(v);
    for (int& v : s.motion_ratio) f(v);
    for (int& v : s.damper) f(v);
    for (int& v : s.arb) f(v);
    for (int& v : s.lsd) f(v);
    f(s.tcs);
    for (int& v : s.ascc) f(v);
}

std::string ascii(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (c >= 0x20 && c < 0x7F) out += char(c);
        else if ((c & 0xC0) == 0xC0) out += '-';
    return out;
}

} // namespace

Bytes encode_online_car(const OnlineCar& c) {
    net::Writer w;
    w.u8_(kVersion);
    w.u8_(u8(c.kind));
    w.str(c.car);
    w.str(c.name);
    w.u8_(c.paint);
    if (c.kind == OnlineCar::Kind::Garage) {
        for (u64 p : c.parts) w.u32_(u32(p)), w.u32_(u32(p >> 32));
        sim::CarSettings s = c.settings;
        each_setting(s, [&](int& v) { w.u32_(u32(s32(v))); });
        w.f32_(c.engine_wear_m);
        w.f32_(c.oil_m);
        w.str(c.wheel);
    } else if (c.kind == OnlineCar::Kind::Mod) {
        w.str(c.package);
        w.u32_(c.package_size);
    } else if (c.kind == OnlineCar::Kind::Arcade) {
        w.str(c.arcade);
    }
    if (c.digest) w.bytes(*c.digest); // trailing: older games stop reading before it
    return w.data;
}

std::optional<OnlineCar> decode_online_car(ByteSpan data) {
    if (data.size() > kMaxOnlineCarBytes) return std::nullopt;
    net::Reader r(data);
    if (r.u8_() != kVersion) return std::nullopt;
    OnlineCar c;
    u8 kind = r.u8_();
    if (kind > u8(OnlineCar::Kind::Arcade)) return std::nullopt;
    c.kind = OnlineCar::Kind(kind);
    c.car = r.str();
    c.name = ascii(r.str());
    c.paint = r.u8_();
    if (c.kind == OnlineCar::Kind::Garage) {
        for (u64& p : c.parts) {
            u64 lo = r.u32_(), hi = r.u32_();
            p = lo | hi << 32;
        }
        each_setting(c.settings, [&](int& v) { v = int(s32(r.u32_())); });
        c.engine_wear_m = r.f32_();
        c.oil_m = r.f32_();
        c.wheel = r.str();
    } else if (c.kind == OnlineCar::Kind::Mod) {
        c.package = r.str();
        c.package_size = r.u32_();
        if (c.package.size() != 64 || !std::all_of(c.package.begin(), c.package.end(), [](char ch) {
                return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            }))
            return std::nullopt;
    } else if (c.kind == OnlineCar::Kind::Arcade) {
        c.arcade = r.str();
        if (c.arcade.empty()) return std::nullopt;
    }
    if (r.remaining() >= 32) { // the spec digest (absent from older games' descriptors)
        ByteSpan d = r.bytes(32);
        c.digest.emplace();
        std::copy(d.begin(), d.end(), c.digest->begin());
    }
    if (!r.ok() || c.car.empty()) return std::nullopt;
    return c;
}

OnlineCar online_car_from_db(const data::CarDatabase& db, const std::string& label, u8 paint) {
    OnlineCar c;
    c.car = label;
    c.paint = paint;
    c.kind = db.mod_car(label) ? OnlineCar::Kind::Mod : OnlineCar::Kind::Disc;
    if (const data::Car* dc = db.car(label)) c.name = ascii(dc->latin_name.empty() ? label : dc->latin_name);
    return c;
}

OnlineCar online_car_from_garage(const data::CarDatabase& db, const GarageCar& g) {
    OnlineCar c = online_car_from_db(db, g.car, u8(std::clamp(g.color, 0, 255)));
    c.kind = OnlineCar::Kind::Garage;
    c.parts = g.parts;
    c.settings = g.settings;
    c.engine_wear_m = g.cond.engine_wear_m;
    c.oil_m = g.cond.oil_m;
    c.wheel = g.wheel;
    return c;
}

OnlineCar online_car_from_arcade(const data::CarDatabase& db, const std::string& arcade, u8 paint) {
    for (const data::CarVariant& v : db.arcade_cars())
        if (v.label == arcade && db.car(v.car)) {
            OnlineCar c = online_car_from_db(db, v.car, paint);
            c.kind = OnlineCar::Kind::Arcade;
            c.arcade = v.label;
            return c;
        }
    return {};
}

namespace {

// The arcade car `c` names, as this machine builds it for `course`: the arcade car's parts with
// its racing tyres (the car page's default), as the arcade races it. Its base car is the one
// the descriptor names, or another label when the owner sent its car's digest (online_car_here
// compares it: the lobby refuses the car where this disc builds another one).
std::optional<race::ArcadeCar> arcade_here(const data::CarDatabase& db, const OnlineCar& c, const std::string& course,
                                           std::string* error) {
    std::optional<race::ArcadeCar> ac = race::arcade_car(db, c.arcade, 0, course, error);
    if (!ac || ac->car == c.car || c.digest) return ac;
    if (error) *error = "arcade car " + c.arcade + " is not a " + c.car;
    return std::nullopt;
}

} // namespace

std::optional<sim::CarSpec> online_car_spec(const data::CarDatabase& db, const OnlineCar& c, std::string* error,
                                            const std::string& course) {
    if (c.kind == OnlineCar::Kind::Arcade) {
        std::optional<race::ArcadeCar> ac = arcade_here(db, c, course, error);
        if (!ac) return std::nullopt;
        return std::move(ac->spec);
    }
    if (!db.car(c.car)) {
        if (error) *error = "no car " + c.car + " here";
        return std::nullopt;
    }
    if (c.kind != OnlineCar::Kind::Garage) return sim::build_car_spec(db, c.car, error);
    std::optional<sim::CarSpec> s = sim::build_car_spec(db, c.car, c.parts, c.settings, error);
    if (!s) return s;
    CarCondition cond;
    cond.engine_wear_m = std::isfinite(c.engine_wear_m) ? c.engine_wear_m : 0;
    cond.oil_m = std::isfinite(c.oil_m) ? c.oil_m : 0;
    float f = GtMode::engine_factor(cond);
    for (float& t : s->engine.curve_torque) t *= f;
    return s;
}

std::string online_car_base(const data::CarDatabase& db, const OnlineCar& c) {
    if (c.kind == OnlineCar::Kind::Arcade)
        for (const data::CarVariant& v : db.arcade_cars())
            if ((v.label == c.arcade || v.label == "_" + c.arcade) && db.car(v.car)) return v.car;
    return db.car(c.car) ? c.car : std::string();
}

// --- the same car on every machine ---

std::optional<sim::exact::ParamImage> online_car_params(const data::CarDatabase& db, const sim::exact::ProgramData& d,
                                                        const OnlineCar& c) {
    namespace ex = sim::exact;
    if (c.kind == OnlineCar::Kind::Arcade ? !race::arcade_car(db, c.arcade, 0, {}) : !db.car(c.car)) return std::nullopt;
    sim::EngineCondition cond;
    const race::ExactCarSource src = race::exact_online_source(db, c, {}, &cond); // (an arcade car: its road tyres)
    std::optional<ex::Equipment> eq = race::exact_equipment(db, src);
    if (!eq) return std::nullopt;
    ex::ProgramData rules = d;
    rules.set_behaviour({sim::Program::NtscU, 60}); // every online race's (docs/dev/ONLINE.md "Model")
    ex::ConvertInputs in;
    in.base = ex::kParams; // P's address in a race's record (the record at 0)
    in.condition = race::exact_condition_words(cond);
    try {
        return ex::race_params(db, rules, *eq, 0, true, true, in);
    } catch (const std::exception&) { // (records the builder cannot read)
        return std::nullopt;
    }
}

std::optional<std::array<u8, 32>> online_car_digest(const data::CarDatabase& db, const sim::exact::ProgramData& d,
                                                    const OnlineCar& c) {
    std::optional<sim::exact::ParamImage> p = online_car_params(db, d, c);
    if (!p) return std::nullopt;
    return sha256(ByteSpan(p->data(), p->size()));
}

CarHere online_car_here(const data::CarDatabase& db, const sim::exact::ProgramData* d, const OnlineCar& c,
                        const std::function<bool(const std::string& model)>& has_model) {
    if (c.kind == OnlineCar::Kind::Mod && !db.car(c.car)) return CarHere::ModMissing;
    std::optional<sim::CarSpec> s = online_car_spec(db, c);
    if (!s) // an arcade car of this disc on a base car that builds another spec: different
        return c.kind == OnlineCar::Kind::Arcade && race::arcade_car(db, c.arcade, 0, {}) ? CarHere::Differs : CarHere::Unknown;
    if (c.digest && d && online_car_digest(db, *d, c) != c.digest) return CarHere::Differs;
    // The body the race draws (the base car's model).
    const data::Car* car = db.car(online_car_base(db, c));
    const std::string model = car ? db.model_of(*car) : std::string();
    if (model.empty() || !has_model(model)) return CarHere::NoModel;
    return CarHere::Ok;
}

const char* car_here_text(CarHere h) {
    switch (h) {
    case CarHere::Unknown: return "is not on this disc";
    case CarHere::Differs: return "is different on this disc";
    case CarHere::NoModel: return "has no model on this disc";
    default: return "";
    }
}

PowerPeak spec_power_peak(const sim::CarSpec& s, float condition) {
    const sim::EngineSpec& e = s.engine;
    const size_t n = std::min(e.curve_rpm.size(), e.curve_torque.size());
    PowerPeak best;
    if (n == 0) return best;
    // Sample from the first curve point (rpm rounded to 100) up to max(rev limit, last
    // point); only rpm up to the rev limit can set the peak; strictly greater wins.
    const int limit = int(e.rev_limit_rpm);
    const int first = (int(e.curve_rpm[0]) + 50) / 100 * 100;
    const int last = std::max(limit, (int(e.curve_rpm[n - 1]) + 50) / 100 * 100);
    // The original's curve axis is in rad/s; look up in the same domain so float ties on a
    // flat power plateau resolve the same way.
    constexpr float kRpmToRadS = 0.10471976f; // 2*pi/60
    std::vector<float> w(n);
    for (size_t i = 0; i < n; ++i) w[i] = ps2::mul(e.curve_rpm[i], kRpmToRadS);
    // Piecewise-linear lookup on the curve, clamped to its ends.
    auto lookup = [&](float v) {
        if (v <= w[0]) return e.curve_torque[0];
        if (v >= w[n - 1]) return e.curve_torque[n - 1];
        size_t i = 1;
        while (i < n - 1 && v > w[i]) ++i;
        const float span = w[i] - w[i - 1];
        const float t = span > 0 ? (v - w[i - 1]) / span : 1.0f;
        return e.curve_torque[i - 1] + (e.curve_torque[i] - e.curve_torque[i - 1]) * t;
    };
    for (int rpm = first; rpm <= last; rpm += 100) {
        if (rpm > limit) continue;
        float omega = ps2::mul(float(rpm), kRpmToRadS);
        float t = lookup(omega);
        float kgfm = ps2::mul(t, condition) / 9.8f; // the looked-up torque x the condition factor
        float ps = ps2::mul(kgfm, float(rpm)) / 716.2f; // PS2 multiply (rounds towards zero)
        if (ps > best.ps) {
            best.ps = ps;
            best.rpm = rpm;
        }
    }
    return best;
}

float spec_power_ps(const sim::CarSpec& s, float condition) { return spec_power_peak(s, condition).ps; }

int display_power(float ps, const Units& units) {
    return sim::exact::shown_power(ee::F::raw(std::bit_cast<u32>(ps)), units.power_hp);
}

const char* power_unit(const Units& units) { return units.power; }

float geared_top_speed(const sim::CarSpec& s) {
    float r = 0;
    for (const sim::AxleSpec& a : s.axle) r = std::max(r, a.tire_radius);
    float ratio = 1e9f;
    for (float g : s.gearbox.ratios) ratio = std::min(ratio, std::fabs(g * s.gearbox.final_drive));
    float rpm = s.engine.rev_limit_rpm;
    if (!(ratio > 0 && ratio < 1e8f) || r <= 0 || rpm <= 0) return 150.0f; // unknown gearing: a generous bound
    return rpm * 0.10471976f / ratio * r;
}

// --- arcade classes ---

namespace {
constexpr const char* kClassTags[kArcadeClasses + 1] = {"", "CLASS_C", "CLASS_B", "CLASS_A", "CLASS_S", "CLASS_DIRT"};
constexpr const char* kClassNames[kArcadeClasses + 1] = {"", "Class C", "Class B", "Class A", "Class S", "Rally cars"};
} // namespace

const char* arcade_class_tag(int cls) { return cls >= 1 && cls <= kArcadeClasses ? kClassTags[cls] : ""; }
const char* arcade_class_name(int cls) { return cls >= 1 && cls <= kArcadeClasses ? kClassNames[cls] : ""; }

int arcade_class_index(const std::string& tag) {
    for (int i = 1; i <= kArcadeClasses; ++i)
        if (tag == kClassTags[i]) return i;
    return 0;
}

int arcade_class_of(const GtData& gd, const std::string& label) {
    for (int i = 1; i <= kArcadeClasses; ++i)
        if (auto it = gd.arcade_classes.find(kClassTags[i]); it != gd.arcade_classes.end())
            for (const GtData::ArcadeCar& a : it->second)
                if (a.label == label) return i;
    return 0;
}

// --- rules ---

const char* drive_rule_name(int drive) {
    static const char* const n[] = {"any", "FF", "FR", "4WD", "MR", "RR"};
    return drive >= 0 && drive <= kDriveRules ? n[drive] : "?";
}

const char* category_rule_name(int category) {
    static const char* const n[] = {"any", "production cars", "race and rally cars"};
    return category >= 0 && category <= kCategoryRules ? n[category] : "?";
}

const char* aspiration_rule_name(int aspiration) {
    static const char* const n[] = {"any", "NA", "turbo"};
    return aspiration >= 0 && aspiration <= kAspirationRules ? n[aspiration] : "?";
}

std::string car_rules_summary(const CarRules& r) {
    std::string out;
    auto add = [&](const std::string& s) { out += (out.empty() ? "" : ", ") + s; };
    if (r.car_class) add(arcade_class_name(r.car_class));
    if (r.category) add(r.category == 1 ? "production" : "race and rally");
    if (r.drive) add(drive_rule_name(r.drive));
    if (r.aspiration) add(aspiration_rule_name(r.aspiration));
    if (r.stock_only) add("stock");
    if (r.max_ps) add("at most " + std::to_string(r.max_ps) + " PS");
    if (!r.allow_mods) add("no mod cars");
    return out.empty() ? "any car" : out;
}

CarFacts online_car_facts(const data::CarDatabase& db, const GtData* gd, const OnlineCar& c) {
    CarFacts f;
    const data::Car* car = db.car(online_car_base(db, c));
    if (c.kind == OnlineCar::Kind::Arcade && gd) f.arcade_class = arcade_class_of(*gd, c.arcade);
    if (c.kind == OnlineCar::Kind::Mod) // a mod car's own [arcade] class
        if (const data::ModCar* m = db.mod_car(c.car)) f.arcade_class = arcade_class_index(m->arcade_class);
    if (!car) return f;
    f.known = true;
    f.category = car->bytes.size() > 0xF1 ? car->bytes[0xF1] : 0;
    // The fitted parts: the car's own, an arcade car's variant parts over them, a garage car's.
    std::array<u64, 27> parts = car->parts;
    if (c.kind == OnlineCar::Kind::Garage) parts = c.parts;
    if (c.kind == OnlineCar::Kind::Arcade)
        for (const data::CarVariant& v : db.arcade_cars())
            if (v.label == c.arcade)
                for (size_t i = 0; i < parts.size(); ++i)
                    if (v.parts[i]) parts[i] = v.parts[i];
    if (c.kind == OnlineCar::Kind::Garage) f.tuned = GtMode::tuned(db, c.car, c.parts, c.settings);
    // Drivetrain and aspiration as GT3's event rules read them (GtMode::check_entry).
    static const int kRule[5] = {2, 1, 3, 4, 5}; // database drive type FR, FF, AWD, MR, RR -> rule
    if (auto dt = db.drivetrain(parts[size_t(part_slot(int(data::TableId::Drivetrain)))]); dt && dt->drive_type < 5)
        f.drive = kRule[dt->drive_type];
    const ByteSpan* t = db.record(data::TableId::TurbineKit, parts[size_t(part_slot(int(data::TableId::TurbineKit)))]);
    f.turbo = t && t->size() > 0x11 && (*t)[0x11] != 0;
    if (std::optional<sim::CarSpec> spec = online_car_spec(db, c)) {
        f.built = true;
        f.power_ps = spec_power_ps(*spec);
        // As Single Race weighs a garage car (arcade_prepare): mass less 120 kg over its power.
        f.kg_per_ps = std::max(1.0f, spec->mass - 120.0f) / std::max(1.0f, f.power_ps);
        f.weight_class = data::class_by_weight(f.kg_per_ps).cls;
    }
    return f;
}

int car_class(const CarFacts& f, bool dirt_course) {
    if (f.arcade_class) return f.arcade_class;
    if (!f.built) return 0;
    return dirt_course ? kArcadeClasses : f.weight_class;
}

std::string car_class_text(const CarFacts& f, bool dirt_course) {
    const int k = car_class(f, dirt_course);
    if (!k) return {};
    if (f.arcade_class) return arcade_class_name(k) + std::string(", class list");
    if (dirt_course) return arcade_class_name(k) + std::string(", dirt course");
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s, %.1f kg/PS", arcade_class_name(k), double(f.kg_per_ps));
    return buf;
}

bool car_meets_rules(const OnlineCar& c, const CarFacts& f, const CarRules& r, std::string* why) {
    auto no = [&](std::string s) {
        if (why) *why = std::move(s);
        return false;
    };
    if (!r.allow_mods && c.kind == OnlineCar::Kind::Mod) return no("mod cars are not allowed");
    if (!f.known) return true; // a mod car this machine lacks: its owner's lobby checked it
    // "Class B cars", "rally cars".
    auto cars_of = [](int cls) { return cls == kArcadeClasses ? std::string("rally cars") : arcade_class_name(cls) + std::string(" cars"); };
    if (const int k = car_class(f, r.dirt_course); r.car_class && k != r.car_class) {
        char kg[16];
        std::snprintf(kg, sizeof kg, "%.1f", double(f.kg_per_ps));
        std::string what = !k               ? " (its class is unknown here)"
                           : f.arcade_class ? " (this is one of the " + cars_of(k) + ")"
                           : r.dirt_course  ? " (on dirt every car without a class list is a rally car)"
                                            : " (this is a " + std::string(arcade_class_name(k)) + " car: " + kg + " kg/PS)";
        return no("only " + cars_of(r.car_class) + what);
    }
    // The arcade class page: the listed road classes only on road courses, the rally class only
    // on dirt. Every other car races anywhere (as GT3's garage cars: on dirt in the rally race).
    if (f.arcade_class == kArcadeClasses && !r.dirt_course) return no("rally cars are not available on road courses");
    if (f.arcade_class && f.arcade_class < kArcadeClasses && r.dirt_course)
        return no(cars_of(f.arcade_class) + " are not available on dirt courses");
    if (r.category == 1 && f.category != 0) return no("production cars only");
    if (r.category == 2 && f.category == 0) return no("race and rally cars only");
    if (r.drive && f.drive != r.drive)
        return no(std::string(drive_rule_name(r.drive)) + " cars only (this is " + drive_rule_name(f.drive) + ")");
    if (r.aspiration && f.turbo != (r.aspiration == 2))
        return no(std::string(aspiration_rule_name(r.aspiration)) + " cars only");
    if (r.stock_only && f.tuned) return no("tuned garage cars are not allowed (stock cars only)");
    if (r.max_ps > 0 && f.built && f.power_ps > float(r.max_ps) + 0.5f)
        return no(std::to_string(int(f.power_ps + 0.5f)) + " PS is over the " + std::to_string(r.max_ps) + " PS limit");
    return true;
}

bool online_car_racing_gearbox(const data::CarDatabase& db, const OnlineCar& c) {
    if (c.kind != OnlineCar::Kind::Garage) return false;
    const ByteSpan* gear = db.record(data::TableId::Gear, c.parts[size_t(part_slot(17))]);
    return gear && gear->size() > 0x10 && (*gear)[0x10] != 0;
}

} // namespace gt3r::game
