#include "sim/car_spec.h"

#include "base/ee_float.h"
#include "game/data/car_database.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r::sim {

namespace {

// Slot indices into Car::parts (see data::kCarPartTables).
enum Slot : int {
    kBrake = 0, kSteer = 2, kChassis = 3, kLightweight = 4, kBody = 5, kEngine = 6, kPortPolish = 7,
    kEngineBalance = 8, kDisplacement = 9, kComputer = 10, kNaTune = 11, kTurbo = 12, kDrivetrain = 13,
    kFlywheel = 14, kClutch = 15, kPropShaft = 16, kLsd = 17, kGear = 18, kSuspension = 19, kIntercooler = 20, kMuffler = 21, kFrontTire = 22, kRearTire = 23,
};

const ByteSpan* part_record(const data::CarDatabase& db, const std::array<u64, 27>& parts, int slot) {
    u64 id = parts[size_t(slot)];
    if (id == 0) return nullptr;
    return db.record(data::TableId(data::kCarPartTables[size_t(slot)]), id);
}

u8 byte_at(const ByteSpan* r, size_t off, u8 fallback = 0) { return r && r->size() > off ? (*r)[off] : fallback; }

// Geometric gear set for gearboxes that store "computed" ratios (-0.999).
std::vector<float> fill_ratios(std::vector<float> r, int gears) {
    if (gears == 1 && !r.empty() && r[0] > 0) return {r[0]}; // single speed (mod EVs; no disc box has < 4)
    gears = std::clamp(gears, 3, 7);
    r.resize(size_t(gears), -1.0f);
    float first = r[0] > 0 ? r[0] : 3.2f;
    float top = 0.80f;
    for (int i = gears - 1; i >= 0; --i)
        if (r[size_t(i)] > 0 && i > 0) {
            top = r[size_t(i)] * std::pow(0.85f, float(gears - 1 - i));
            break;
        }
    for (int i = 0; i < gears; ++i)
        if (r[size_t(i)] <= 0) r[size_t(i)] = first * std::pow(top / first, float(i) / float(gears - 1));
    return r;
}

constexpr float kRpmToRadS = 0.10471976f; // 2*pi/60

// Piecewise-linear lookup, clamped to the first / last y outside the axis.
float table_lookup(const float* x, const float* y, int n, float v) {
    if (n <= 0) return 0;
    if (v <= x[0]) return y[0];
    if (v >= x[n - 1]) return y[n - 1];
    int i = 1;
    while (i < n - 1 && v > x[i]) ++i;
    float span = x[i] - x[i - 1];
    float t = span > 0 ? (v - x[i - 1]) / span : 1.0f;
    return y[i - 1] + (y[i] - y[i - 1]) * t;
}

// [physics §2.1] Full-throttle torque (N*m) at `rpm` on the curve.
float curve_torque(const EngineSpec& e, float rpm) {
    return table_lookup(e.curve_rpm.data(), e.curve_torque.data(), int(std::min(e.curve_rpm.size(), e.curve_torque.size())),
                        rpm);
}

// [physics §2.3] One turbo stage's boost at rpm n (shape type 0): below the knee A (1 - ((N - n)/N)^2), from N to 3N
// A - 0.25 A ((n - N)/N)^2, above 3N 0; the uncapped boost 1 + both stages.
float turbo_stage(const TurboSpec::Stage& st, float n) {
    if (!st.active || st.knee_rpm <= 0) return 0;
    float N = st.knee_rpm, A = st.peak;
    if (n < N) {
        float u = (N - n) / N;
        return A * (1 - u * u);
    }
    if (n <= 3 * N) {
        float u = (n - N) / N;
        return A - 0.25f * A * u * u;
    }
    return 0;
}
float turbo_boost_raw(const TurboSpec& t, float n0, float n1) {
    return 1 + turbo_stage(t.stage[0], n0) + turbo_stage(t.stage[1], n1);
}

// [physics §2.1] The curve from raw spec values: torque points (0.01 kgf*m), rpm axis (x100 rpm), output factor (1000 =
// x1), high-rpm factor (per mille) and low-rpm factor (percent); with a turbo, each point is multiplied by min(B, cap) / B,
// B = the uncapped boost at that rpm with both stages at the axis rpm. Writes rpm / N*m pairs; returns the point count.
int build_torque_curve(const u16* torque, const u8* rpm_x100, int n, u16 out_factor, u16 hi_permille, u8 lo_percent,
                       float* out_rpm, float* out_nm, const TurboSpec* turbo) {
    n = std::clamp(n, 0, 16);
    if (n == 0) return 0;
    float f_out = out_factor ? out_factor * 1e-5f : 0.01f;
    float s_lo = lo_percent ? lo_percent * 0.01f : 1.0f;
    float s_hi = hi_permille ? hi_permille * 0.001f : 1.0f;
    float w0 = rpm_x100[0] * 100.0f, w1 = rpm_x100[n - 1] * 100.0f;
    for (int i = 0; i < n; ++i) {
        float w = rpm_x100[i] * 100.0f;
        float t = w1 > w0 ? (w - w0) / (w1 - w0) : 0.0f;
        out_rpm[i] = w;
        out_nm[i] = torque[i] * 9.8f * f_out * (s_lo + (s_hi - s_lo) * t);
        if (turbo && turbo->on) {
            float b = turbo_boost_raw(*turbo, w, w);
            if (b > 0) out_nm[i] *= std::min(b, turbo->cap) / b;
        }
    }
    return n;
}

// Gear x final drive (-1 = reverse, 0 = neutral -> 0).
float total_ratio(const GearboxSpec& g, int gear) {
    if (gear < 0) return -g.reverse * g.final_drive;
    if (gear == 0 || gear > int(g.ratios.size())) return 0;
    return g.ratios[size_t(gear - 1)] * g.final_drive;
}

// [physics §3.5] The rolling-resistance speed curve RR(|v|); tarmac's coefficient (surface 0) is 0.1.
float rolling_curve(float speed) {
    // speed axis in exact km/h / 3.6 (0, 4, 20, 58, 116, 200, 308, 400 km/h)
    static const float x[] = {0, 4 / 3.6f, 20 / 3.6f, 58 / 3.6f, 116 / 3.6f, 200 / 3.6f, 308 / 3.6f, 400 / 3.6f};
    static const float y[] = {0, 0, 0.010f, 0.015f, 0.032f, 0.072f, 0.144f, 0.255f};
    return table_lookup(x, y, 8, std::fabs(speed));
}
constexpr float kTarmacRolling = 0.10f;

} // namespace

float CarSpec::peak_torque() const {
    float m = 0;
    for (float t : engine.curve_torque) m = std::max(m, t);
    return m;
}

float CarSpec::peak_power_rpm() const {
    float best = 0, best_rpm = engine.rev_limit_rpm;
    for (float rpm = engine.idle_rpm; rpm <= engine.rev_limit_rpm; rpm += 50) {
        float p = curve_torque(engine, rpm) * rpm;
        if (p > best) {
            best = p;
            best_rpm = rpm;
        }
    }
    return best_rpm;
}

float CarSpec::peak_power_kw() const {
    float rpm = peak_power_rpm();
    return curve_torque(engine, rpm) * rpm * kRpmToRadS * 0.001f;
}

float showroom_top_speed(const CarSpec& c) {
    const int n = int(c.gearbox.ratios.size());
    if (n == 0 || c.engine.curve_rpm.empty()) return 0;
    float r = 0;
    int driven_axles = 0;
    for (int a = 0; a < 2; ++a)
        if (c.drive == Drive::AWD || (a == 0) == (c.drive == Drive::FF)) {
            r += c.axle[size_t(a)].tire_radius;
            ++driven_axles;
        }
    r = std::max(r / float(std::max(driven_axles, 1)), 0.05f);
    auto per_metre = [&](int gear) { return total_ratio(c.gearbox, gear) / r; }; // engine rad/s per m/s
    // The start value: the rev limit in top gear.
    float top = c.engine.rev_limit_rpm * kRpmToRadS / per_metre(n);
    const int kmh_max = int(top * 3.6f), step = kmh_max / 500 + 1;
    // The aero coefficients as built (air density 1.2; the showroom's global holds 0.8
    // until a race has been set up, but the reference screens match 1.2).
    const float drag = c.aero.c_drag, down = c.aero.c_front + c.aero.c_rear;
    const float weight = c.mass * 9.8f, rr = kTarmacRolling;
    const float up = c.engine.upshift_rpm > 0 ? c.engine.upshift_rpm : c.engine.rev_limit_rpm - 500;
    int gear = 1;
    for (int kmh = 0; kmh < kmh_max; kmh += step) {
        const float v = float(kmh) / 3.6f, w = v * per_metre(gear);
        const float drive = curve_torque(c.engine, w / kRpmToRadS) * per_metre(gear);
        const float resist = rolling_curve(v) * rr * (weight + down * v * v) + drag * v * v;
        if (resist < drive) top = v;
        if (gear < n && up < float(int(w * 60.0f / 6.2831853f))) ++gear;
    }
    return top;
}

void generate_gear_ratios(int n, int rev_byte, int final_word, int rim, int wc, int ac, int slider, s16 out[8], int drive,
                          int awd_system) {
    using ee::F;
    static const u8 kSteps[3][6] = {{155, 165, 188, 190, 190, 200}, {165, 175, 190, 192, 190, 200},
                                    {170, 180, 192, 194, 190, 200}};
    const u8* b = kSteps[std::clamp(n, 5, 7) - 5];
    const F k_rpm = F::raw(0x3DD67750); // 0.10472
    const F k_kmh = F::raw(0x3E8E38E4); // 0.277778
    const F t = ee::int_to_f(10 * slider - 150) / F(250.0f);
    const F v = ee::int_to_f(10 * slider) * k_kmh;
    const F rpm = ee::int_to_f(100 * rev_byte);
    const F fd = F(float(double(final_word) * 0.001)); // double, then rounded to float
    // Driven tyre [physics §6.7]: front for FF and for AWD system 0, rear otherwise. A
    // width code 0 gives no sidewall on the front path and 10 mm on the rear one.
    const bool front = drive == 1 || (drive == 2 && awd_system == 0);
    const F c_mm = F::raw(0x3A83126F), c_in = F::raw(0x3CD013A9), c_sw = F::raw(0x3D4CCCCD);
    const F W = ee::int_to_f(wc ? 5 * wc : (front ? 0 : 10)) * c_mm; // rear path: width 0 -> 10 mm
    const F R = ee::int_to_f(rim) * c_in * F(0.5f) + W * ee::int_to_f(ac) * c_sw;
    F r = rpm * k_rpm * R / v / fd;
    auto q = [&](int hi, int lo) {
        const F a = F(200.0f) / ee::int_to_f(b[lo]), c = F(200.0f) / ee::int_to_f(b[hi]);
        return a + t * (c - a);
    };
    F q1 = q(0, 1), q2 = q(2, 3), q3 = q(4, 5);
    for (int i = std::min(n, 7); i >= 1; --i) {
        if (i == 1) r = r * q3;
        // The float-to-int conversion truncates and saturates; the store keeps the low 16 bits.
        out[i] = s16(u16(u32(ee::to_int(r * F(1000.0f)))));
        r = r * q1;
        q1 = q1 * q2;
    }
    out[0] = s16((3 * int(out[1]) + int(out[2])) / 4);
}

std::optional<CarSpec> build_car_spec(const data::CarDatabase& db, const std::string& label, std::string* error) {
    if (const data::Car* c = db.car(label)) return build_car_spec(db, c->label, c->parts, error);
    const data::CarVariant* v = db.variant(label);
    if (!v)
        for (const auto& a : db.arcade_cars())
            if (a.label == label) v = &a;
    if (v) {
        const data::Car* base = db.car(v->car);
        if (!base) {
            if (error) *error = "variant " + label + ": base car " + v->car + " not found";
            return std::nullopt;
        }
        std::array<u64, 27> parts = v->parts;
        for (size_t i = 0; i < parts.size(); ++i)
            if (parts[i] == 0) parts[i] = base->parts[i];
        auto s = build_car_spec(db, base->label, parts, error);
        if (s) s->label = label;
        return s;
    }
    if (error) *error = "unknown car label " + label;
    return std::nullopt;
}

std::optional<GearGenerator> gear_generator(const data::CarDatabase& db, const std::array<u64, 27>& parts) {
    const ByteSpan* g = part_record(db, parts, kGear);
    const ByteSpan* er = part_record(db, parts, kEngine);
    if (!g || g->size() <= 0x29 || !(*g)[0x28] || !er || er->size() < 0x58) return std::nullopt;
    GearGenerator in;
    in.n = (*g)[0x11];
    // Rev byte = engine +0x45 + engine-balance / NA-tune / turbo adders.
    in.rev = (*er)[0x45];
    for (int slot : {kEngineBalance, kNaTune}) in.rev += byte_at(part_record(db, parts, slot), 0x12);
    in.rev += byte_at(part_record(db, parts, kTurbo), 0x18);
    auto dt = db.drivetrain(parts[kDrivetrain]);
    in.drive = dt ? dt->drive_type : 0, in.awd_system = dt ? dt->awd_system : 0xFE;
    int axle = (in.drive == 1 || (in.drive == 2 && in.awd_system == 0)) ? 0 : 1;
    if (auto tr = db.tire(axle ? data::TableId::RearTire : data::TableId::FrontTire, parts[size_t(axle ? kRearTire : kFrontTire)]))
        if (const ByteSpan* sz = db.record(data::TableId::TireSize, tr->size); sz && sz->size() >= 0x0B) {
            in.rim = (*sz)[0x08];
            in.aspect_code = (*sz)[0x09];
            in.width_code = (*sz)[0x0A];
        }
    return in;
}

void generate_gear_ratios(const GearGenerator& in, int final_word, int slider, s16 out[8]) {
    generate_gear_ratios(in.n, in.rev, final_word, in.rim, in.width_code, in.aspect_code, slider, out, in.drive, in.awd_system);
}

bool apply_gear_slider(const data::CarDatabase& db, const std::array<u64, 27>& parts, CarSettings& c) {
    const std::optional<GearGenerator> in = gear_generator(db, parts);
    if (!in) return false;
    s16 out[8] = {};
    generate_gear_ratios(*in, c.final_drive, c.gear_slider, out);
    for (int i = 0; i <= std::min(in->n, 7); ++i) c.gears[size_t(i)] = out[i];
    return true;
}

CarSettings stock_settings(const data::CarDatabase& db, const std::string& car_label, const std::array<u64, 27>& parts) {
    (void)car_label;
    CarSettings c;
    auto rec = [&](int slot) { return part_record(db, parts, slot); };
    if (const ByteSpan* g = rec(kGear); g && g->size() > 0x2B) {
        c.gears[0] = int(s16(rd16(*g, 0x12)));
        for (int i = 0; i < 7; ++i) c.gears[size_t(i + 1)] = int(s16(rd16(*g, size_t(0x14 + 2 * i))));
        c.final_drive = rd16(*g, 0x22);
        c.gear_slider = (*g)[0x29];
        if ((*g)[0x28]) apply_gear_slider(db, parts, c); // [physics §6.7] computed gearbox: generated default words
    }
    if (const ByteSpan* d = rec(kDrivetrain); d && d->size() > 0x13) c.front_torque = (*d)[0x13];
    if (const ByteSpan* bc = rec(1); bc && bc->size() > 0x14) c.brake_front = c.brake_rear = (*bc)[0x14];
    if (const ByteSpan* b = rec(kBody); b && b->size() > 0x3C) {
        c.downforce_front = (*b)[0x13];
        c.downforce_rear = (*b)[0x3C];
    }
    if (const ByteSpan* t = rec(kTurbo); t && t->size() > 0x17)
        for (int i = 0; i < 6; ++i) c.turbo[size_t(i)] = (*t)[size_t(0x12 + i)];
    if (const ByteSpan* su = rec(kSuspension); su && su->size() >= 0x7C) {
        auto B = [&](size_t o) -> int { return (*su)[o]; };
        c.camber = {B(0x13), B(0x16)};
        c.ride_height = {rd16(*su, 0x54), rd16(*su, 0x5A)};
        c.toe = {B(0x19), B(0x1C)};
        c.spring = {B(0x23), B(0x26)};
        c.motion_ratio = {B(0x27), B(0x28)};
        c.damper = {B(0x2E), B(0x31), B(0x35), B(0x38), B(0x3C), B(0x3F), B(0x43), B(0x46)};
        c.arb = {B(0x4A), B(0x4E)};
    }
    if (const ByteSpan* ls = rec(kLsd); ls && ls->size() > 0x22)
        c.lsd = {(*ls)[0x12], (*ls)[0x1C], (*ls)[0x15], (*ls)[0x1F], (*ls)[0x18], (*ls)[0x22]};
    if (const ByteSpan* tc = db.record(data::TableId::Tcsc, parts[25]); tc && tc->size() > 0x11) c.tcs = (*tc)[0x11];
    if (const ByteSpan* as = db.record(data::TableId::Ascc, parts[24]); as && as->size() > 0x26)
        c.ascc = {(*as)[0x12], (*as)[0x1B], (*as)[0x21], (*as)[0x26]};
    return c;
}

std::optional<CarSpec> build_car_spec(const data::CarDatabase& db, const std::string& car_label,
                                      const std::array<u64, 27>& parts, std::string* error) {
    return build_car_spec(db, car_label, parts, stock_settings(db, car_label, parts), error);
}

std::optional<CarSpec> build_car_spec(const data::CarDatabase& db, const std::string& car_label,
                                      const std::array<u64, 27>& parts, const CarSettings& set, std::string* error) {
    auto fail = [&](const std::string& msg) -> std::optional<CarSpec> {
        if (error) *error = car_label + ": " + msg;
        return std::nullopt;
    };
    const data::Car* car = db.car(car_label);
    if (!car) return fail("not in the car table");

    CarSpec s;
    s.label = car_label;
    s.name = car->latin_name; // the app's own screens draw it (Car::latin_name)

    // --- chassis / body ---
    auto ch = db.chassis(parts[kChassis]);
    auto body = db.body(parts[kBody]);
    if (!ch || ch->weight_kg == 0 || ch->wheelbase_mm == 0) return fail("missing chassis record");
    // [physics §6.2/6.4] weight x lightweight(+0x12 per mille) x body(+0x30 per mille), integer maths, + 120 kg.
    int weight = ch->weight_kg;
    const ByteSpan* lw = part_record(db, parts, kLightweight);
    if (lw && lw->size() >= 0x14 && rd16(*lw, 0x12)) weight = weight * rd16(*lw, 0x12) / 1000;
    if (body && body->weight_permille) weight = weight * body->weight_permille / 1000;
    s.mass = std::max(float(weight + 120), 300.0f);
    s.front_weight = std::clamp(ch->front_weight_pct > 0 ? ch->front_weight_pct * 0.01f : 0.55f, 0.3f, 0.7f);
    s.wheelbase = ch->wheelbase_mm * 0.001f;
    if (ch->length_mm) s.length = ch->length_mm * 0.001f;
    if (ch->height_mm) s.height = ch->height_mm * 0.001f;
    if (body) {
        if (body->width_mm) s.width = body->width_mm * 0.001f;
        const ByteSpan* br = part_record(db, parts, kBody);
        int off_f = br && br->size() > 0x3E ? int(static_cast<signed char>((*br)[0x3D])) : 0;
        int off_r = br && br->size() > 0x3E ? int(static_cast<signed char>((*br)[0x3E])) : 0;
        if (body->front_tread_mm) s.axle[0].track = (body->front_tread_mm - 2 * off_f) * 0.001f;
        if (body->rear_tread_mm) s.axle[1].track = (body->rear_tread_mm - 2 * off_r) * 0.001f;
        // [physics §6.2] collision extents (body f32 +0x20 / +0x24, mm, truncated).
        // Extents default to length / 2 in integer mm; width/height are the collision
        // extents = body f32 +0x28 / +0x2C.
        int half = int(ch->length_mm) / 2;
        int fe = int(body->inertia[0]), re = int(body->inertia[1]);
        s.front_extent = float(fe ? fe : half) * 0.001f;
        s.rear_extent = float(re ? re : half) * 0.001f;
        if (int(body->inertia[2]) > 0) s.width = float(int(body->inertia[2])) * 0.001f;
        if (int(body->inertia[3]) > 0) s.height = float(int(body->inertia[3])) * 0.001f;
    }
    if (s.front_extent <= 0.3f) s.front_extent = s.length * 0.5f;
    if (s.rear_extent <= 0.3f) s.rear_extent = s.length * 0.5f;
    // [physics §4.2] The stock ride height q: the front setting for both axles, when the suspension has a spring.
    if (const ByteSpan* su = part_record(db, parts, kSuspension); su && su->size() >= 0x7C)
        if (set.spring[0] && set.motion_ratio[0])
            for (AxleSpec& a : s.axle) a.ride_height = set.ride_height[0] * 0.001f;

    // --- tyres ---
    const int tire_slot[2] = {kFrontTire, kRearTire};
    const data::TableId tire_table[2] = {data::TableId::FrontTire, data::TableId::RearTire};
    for (int a = 0; a < 2; ++a) {
        auto t = db.tire(tire_table[a], parts[size_t(tire_slot[a])]);
        if (!t) continue;
        if (const ByteSpan* sz = db.record(data::TableId::TireSize, t->size); sz && sz->size() >= 0x0B) {
            float rim_in = (*sz)[0x08], aspect = (*sz)[0x09] * 5.0f, width_mm = (*sz)[0x0A] * 5.0f;
            if (rim_in > 0 && width_mm > 0) {
                s.axle[size_t(a)].tire_width = width_mm * 0.001f;
                s.axle[size_t(a)].tire_radius = rim_in * 0.0254f * 0.5f + width_mm * 0.001f * aspect * 0.01f;
            }
        }
    }

    // --- engine ---
    auto en = db.engine(parts[kEngine]);
    if (!en || en->rpm.empty()) return fail("missing engine record");
    s.power_ps = en->power_ps;
    const ByteSpan* er = part_record(db, parts, kEngine);
    if (!er || er->size() < 0x58) return fail("short engine record");
    // [physics §2.1 / physics §6.2] raw spec values plus the part adders/multipliers
    // (docs/formats/database.md "runtime car spec"); integer percent maths.
    u8 axis[16];
    u16 torque[16];
    int n = std::clamp(int((*er)[0x57]), 1, 16);
    for (int i = 0; i < 16; ++i) {
        axis[i] = (*er)[size_t(0x47 + i)];
        torque[i] = rd16(*er, size_t(0x18 + 2 * i));
    }
    int limit = (*er)[0x45], limit2 = (*er)[0x46], idle = (*er)[0x44];
    u32 hi = 1000, lo = 100;
    // [physics §6.2] the game runs one helper per parts table in its own order;
    // the factors are integer percent steps, so the order matters, and it runs the port
    // polish helper twice (its factors apply squared: 102 % -> 1000 * 1.02 * 1.02 = 1040).
    auto engine_part = [&](int slot) {
        const ByteSpan* r = part_record(db, parts, slot);
        if (!r) return;
        if (slot == kEngineBalance || slot == kNaTune) {
            for (int i = 0; i < n; ++i) axis[i] = u8(axis[i] + byte_at(r, 0x11));
            limit += byte_at(r, 0x12);
            if (u8 p = byte_at(r, 0x13)) hi = hi * p / 100;
            if (u8 p = byte_at(r, 0x14)) lo = lo * p / 100;
        } else if (slot == kTurbo) {
            if (r->size() < 0x1E) return;
            if (u16 p = rd16(*r, 0x1C)) hi = hi * p / 100;
            if (u8 p = (*r)[0x1B]) lo = lo * p / 100;
            limit += (*r)[0x18];
            limit2 += (*r)[0x19];
            // [physics §2.3 field map] stock boost settings = turbo +0x12..+0x17 (peak, knee,
            // rate of stage 0, then stage 1), cap = 1 + 0.1 x +0x11.
            TurboSpec& t = s.engine.turbo;
            u8 cap = (*r)[0x11];
            int p0 = set.turbo[0], k0 = set.turbo[1], r0 = set.turbo[2], p1 = set.turbo[3], k1 = set.turbo[4], r1 = set.turbo[5];
            t.on = cap && k0 && p0 && r0;
            t.cap = 1 + 0.1f * cap;
            t.stage[0] = {true, p0 * 0.1f, k0 * 100.0f, r0 * 0.1f};
            t.stage[1] = {p1 && k1 && r1, p1 * 0.1f, k1 * 100.0f, r1 * 0.1f};
        } else {
            if (u8 p = byte_at(r, 0x11)) hi = hi * p / 100;
            if (u8 p = byte_at(r, 0x12)) lo = lo * p / 100;
        }
    };
    for (int slot : {kPortPolish, kEngineBalance, kDisplacement, kComputer, kNaTune, kTurbo, kPortPolish, kMuffler, kIntercooler})
        engine_part(slot);
    s.engine.upshift_rpm = limit2 * 100.0f; // [physics §2.6] UP
    float rpm[16], nm[16];
    n = build_torque_curve(torque, axis, n, u16((*er)[0x42] * 10), u16(std::min<u32>(hi, 65535)), u8(std::min<u32>(lo, 255)),
                           rpm, nm, &s.engine.turbo);
    for (int i = 0; i < n; ++i) {
        if (!s.engine.curve_rpm.empty() && rpm[i] <= s.engine.curve_rpm.back()) continue; // padding
        s.engine.curve_rpm.push_back(rpm[i]);
        s.engine.curve_torque.push_back(nm[i]);
    }
    if (s.engine.curve_rpm.empty()) return fail("empty torque curve");
    // [physics §2.1] rev limit = +0x45 x100 (last axis point if 0), at least +0x46 x100 + 200; idle = +0x44 x10.
    float lim = limit > 0 ? limit * 100.0f : s.engine.curve_rpm.back();
    s.engine.rev_limit_rpm = std::max(lim, limit2 * 100.0f + 200.0f);
    s.engine.idle_rpm = std::clamp(idle ? idle * 10.0f : 900.0f, 500.0f, 2500.0f);
    if (const data::ModCar* mc = db.mod_car(car_label); mc && mc->electric) {
        s.engine.electric = true;
        s.engine.idle_rpm = 0;
    }
    if (s.engine.rev_limit_rpm < s.engine.idle_rpm + 1500) s.engine.rev_limit_rpm = s.engine.curve_rpm.back();

    // --- gearbox ---
    if (auto ge = db.gearbox(parts[kGear])) {
        // User gear words [physics §6.6]: reverse, gears 1..7 (x1000, -999 = none), final drive.
        std::vector<float> ratios;
        for (int i = 1; i <= 7; ++i) ratios.push_back(set.gears[size_t(i)] * 0.001f);
        s.gearbox.ratios = fill_ratios(ratios, ge->gears ? ge->gears : 5);
        if (set.final_drive > 0) s.gearbox.final_drive = set.final_drive * 0.001f;
        if (set.gears[0] != 0) s.gearbox.reverse = std::fabs(set.gears[0] * 0.001f);
        // A computed gearbox's words are the settings' as the race reads them [physics §6.7]: generated by stock_settings
        // and by the settings pages' slider (apply_gear_slider), never here.
    }
    if (s.gearbox.ratios.empty()) s.gearbox.ratios = {3.2f, 2.0f, 1.4f, 1.05f, 0.82f};
    if (s.gearbox.final_drive <= 0) s.gearbox.final_drive = 4.0f;
    if (s.gearbox.reverse <= 0) s.gearbox.reverse = 3.3f;

    // --- drivetrain ---
    if (auto dt = db.drivetrain(parts[kDrivetrain])) s.drive = dt->drive_type <= 4 ? Drive(dt->drive_type) : Drive::FR;

    // --- aero [physics §4.7]: A from body f32 +0x44 (m^2), Cd = body +0x39 %, rho = 1.2 ---
    if (body) {
        const ByteSpan* br = part_record(db, parts, kBody);
        // the stored area = int(body f32 +0x44 x 10000): the area is quantised to 1e-4 m^2.
        int a16 = int(body->unknown_44 * 10000.0f);
        float area = a16 > 1000 ? a16 / 10000.0f : s.width * s.height;
        float cd = br && br->size() > 0x39 && (*br)[0x39] ? (*br)[0x39] * 0.01f : 0.33f;
        float af = 0.5f * 1.2f * area;
        // Stock downforce settings [physics §6.6]: body +0x13 front, +0x3C rear (percent of A_f).
        s.aero.c_front = af * set.downforce_front * 0.01f; // setting 0x124
        s.aero.c_rear = af * set.downforce_rear * 0.01f;   // setting 0x128
        s.aero.c_drag = af * cd + 0.1f * (s.aero.c_front + s.aero.c_rear);
    }
    if (s.aero.c_drag <= 0) s.aero.c_drag = 0.5f * 1.225f * (0.33f * s.width * s.height * 0.85f); // no body record
    return s;
}

} // namespace gt3r::sim
