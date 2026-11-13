// The car as the menus describe it (sim/car_spec.h: build_car_spec, the garage settings, the computed gearbox's slider, the
// showroom top speed) and the engine condition's rules (sim/engine_condition.h). The spec tests need the car database from
// the disc (GT3R_DISC).

#include "test_framework.h"

#include "disc/volume.h"
#include "game/data/car_database.h"
#include "sim/car_spec.h"
#include "sim/engine_condition.h"
#include "sim/exact/params_build.h"

#include <cmath>
#include <cstdio>

using namespace gt3r;
using namespace gt3r::sim;

namespace {

data::CarDatabase* database() {
    static data::CarDatabase db;
    static bool tried = false, ok = false;
    if (!tried) {
        tried = true;
        if (Disc* d = test::test_disc()) ok = db.load(d->volume());
    }
    return ok ? &db : nullptr;
}

} // namespace

TEST(car_spec_r34_gtr) {
    data::CarDatabase* db = database();
    if (!db) SKIP("needs GT3R_DISC");
    std::string err;
    auto spec = build_car_spec(*db, "r34_gtr_v_99", &err);
    CHECK(spec.has_value());
    if (!spec) {
        std::printf("  %s\n", err.c_str());
        return;
    }
    std::printf("  %s: %.0f kg, fw %.2f, wb %.3f, peak %.0f Nm, %.0f kW @ %.0f rpm, limit %.0f, %zu gears final %.3f, "
                "tyres r %.3f/%.3f, ride height %.3f, drag %.3f\n",
                spec->name.c_str(), spec->mass, spec->front_weight, spec->wheelbase, spec->peak_torque(), spec->peak_power_kw(),
                spec->peak_power_rpm(), spec->engine.rev_limit_rpm, spec->gearbox.ratios.size(), spec->gearbox.final_drive,
                spec->axle[0].tire_radius, spec->axle[1].tire_radius, spec->axle[0].ride_height, spec->aero.c_drag);
    CHECK(spec->drive == Drive::AWD);
    CHECK(spec->mass > 1570 && spec->mass < 1770); // 1560 kg + 120 kg [physics §6.4]
    CHECK(spec->gearbox.ratios.size() == 6);
    CHECK(spec->engine.turbo.on);
    // physics §4.7 R34: drag 0.511, downforce 0.348 / 0.564
    CHECK(std::fabs(spec->aero.c_drag - 0.511f) < 0.002f);
    CHECK(std::fabs(spec->aero.c_front - 0.348f) < 0.002f && std::fabs(spec->aero.c_rear - 0.564f) < 0.002f);
    // physics §4.2: the stock ride height (130 mm), both axles
    CHECK(std::fabs(spec->axle[0].ride_height - 0.130f) < 1e-6f && spec->axle[1].ride_height == spec->axle[0].ride_height);
    CHECK(spec->axle[0].tire_radius > 0.3f && spec->axle[0].tire_radius < 0.4f);
}

TEST(car_spec_settings) {
    // User settings (physics §6.6) feed the spec with the spec's conversions; the stock settings reproduce the default build
    // exactly.
    data::CarDatabase* db = database();
    if (!db) SKIP("needs GT3R_DISC");
    const data::Car* car = db->car("r34_gtr_v_99");
    CHECK(car != nullptr);
    if (!car) return;
    CarSettings st = stock_settings(*db, car->label, car->parts);
    CHECK(st.spring[0] == 40 && st.spring[1] == 50 && st.motion_ratio[0] == 95); // physics §6.6 R34 values
    CHECK(st.ride_height[0] == 130 && st.downforce_front == 29 && st.downforce_rear == 47 && st.final_drive == 3545);
    auto a = build_car_spec(*db, car->label, car->parts);
    auto b = build_car_spec(*db, car->label, car->parts, st);
    CHECK(a && b);
    if (!a || !b) return;
    CHECK(a->gearbox.ratios == b->gearbox.ratios && a->aero.c_drag == b->aero.c_drag &&
          a->axle[0].ride_height == b->axle[0].ride_height && a->engine.curve_torque == b->engine.curve_torque);

    CarSettings t = st;
    t.ride_height = {110, 120};
    t.downforce_front = 50;
    t.downforce_rear = 0;
    t.final_drive = 4000;
    t.gears[1] = 3000;
    auto c = build_car_spec(*db, car->label, car->parts, t);
    CHECK(c.has_value());
    if (!c) return;
    // [physics §4.2] both axles read the front ride height (the rear ride-height byte is ignored)
    CHECK(std::fabs(c->axle[0].ride_height - 0.110f) < 1e-6f && std::fabs(c->axle[1].ride_height - 0.110f) < 1e-6f);
    float af = 0.5f * 1.2f * 2.0f; // R34 frontal area 2.0 m^2
    CHECK(std::fabs(c->aero.c_front - af * 0.5f) < 1e-4f && c->aero.c_rear == 0);
    CHECK(std::fabs(c->aero.c_drag - (a->aero.c_drag - 0.1f * (a->aero.c_front + a->aero.c_rear) + 0.1f * af * 0.5f)) < 1e-4f);
    CHECK(std::fabs(c->gearbox.final_drive - 4.0f) < 1e-6f && std::fabs(c->gearbox.ratios[0] - 3.0f) < 1e-6f);
    // The turbo's stock boost settings shape the curve: none of the stages, no cap.
    CarSettings u = st;
    u.turbo = {};
    auto d = build_car_spec(*db, car->label, car->parts, u);
    CHECK(d && !d->engine.turbo.on);
}

TEST(car_spec_computed_gearbox_slider) {
    // Computed gearboxes regenerate from the slider (physics §6.7): a higher design speed gives a taller (smaller) top gear.
    data::CarDatabase* db = database();
    if (!db) SKIP("needs GT3R_DISC");
    const data::Car* car = db->car("zz2_00");
    if (!car) SKIP("zz2_00 not in this database");
    CarSettings st = stock_settings(*db, car->label, car->parts);
    auto a = build_car_spec(*db, car->label, car->parts, st);
    st.gear_slider += 3;
    CHECK(apply_gear_slider(*db, car->parts, st));
    auto b = build_car_spec(*db, car->label, car->parts, st);
    CHECK(a && b);
    if (!a || !b) return;
    std::printf("  zz2 slider: top gear %.3f -> %.3f, first %.3f -> %.3f\n", a->gearbox.ratios.back(), b->gearbox.ratios.back(),
                a->gearbox.ratios[0], b->gearbox.ratios[0]);
    CHECK(std::fabs(a->gearbox.ratios[0] - 3.194f) < 1e-4f); // physics §6.7 check value
    CHECK(b->gearbox.ratios.back() < a->gearbox.ratios.back());
}

TEST(car_gear_generator_values) {
    // The ratio generator in the program's own arithmetic: a six-speed box's words fall gear by gear, reverse
    // is (3 first + second) / 4, a taller design speed a taller top gear.
    s16 a[8] = {}, b[8] = {};
    generate_gear_ratios(6, 80, 3545, 18, 49, 8, 25, a, 0, 0xFE);
    generate_gear_ratios(6, 80, 3545, 18, 49, 8, 30, b, 0, 0xFE);
    for (int i = 2; i <= 6; ++i) CHECK(a[i] < a[i - 1]);
    CHECK(b[6] < a[6]);
    CHECK(a[0] == s16((3 * int(a[1]) + int(a[2])) / 4));
    CHECK(a[6] > 0 && a[1] > 2000);
}

TEST(car_gear_chart) {
    // The settings menu's gear chart (sim::exact::gear_chart, gt_mode §14): a line per forward gear from 1000 rpm (1st) or
    // the rpm after the shift into it up to the upshift rpm, the top gear's up to the rev limit, at road speeds from the
    // gear's ratio x final. A taller final moves every line; a gear's ratio moves its own line alone (its start rpm
    // follows the gear below).
    data::CarDatabase* db = database();
    if (!db) SKIP("needs GT3R_DISC");
    const data::Car* car = db->car("r34_gtr_v_99");
    CHECK(car != nullptr);
    if (!car) return;
    const CarSettings st = stock_settings(*db, car->label, car->parts);
    auto spec = build_car_spec(*db, car->label, car->parts, st);
    CHECK(spec.has_value());
    if (!spec) return;
    auto chart = [&](const CarSettings& s) {
        return exact::gear_chart(*db, exact::make_equipment(*db, car->label, car->parts, s));
    };
    const exact::GearChart a = chart(st);
    CHECK(a.n == 6);
    CHECK(a.rpm_lo[1] == 1000);
    CHECK(float(a.rpm_hi[6]) == spec->engine.rev_limit_rpm);
    for (int i = 1; i < 6; ++i) CHECK(float(a.rpm_hi[size_t(i)]) == spec->engine.upshift_rpm);
    for (int i = 2; i <= 6; ++i) CHECK(a.rpm_lo[size_t(i)] > 1000 && a.rpm_lo[size_t(i)] < a.rpm_hi[size_t(i)]);
    // Top gear at the rev limit: rpm x 2 pi / 60 x driven tyre radius / (ratio x final), m/s.
    const float top = spec->engine.rev_limit_rpm * 0.10471976f * spec->axle[0].tire_radius /
                      (spec->gearbox.ratios.back() * spec->gearbox.final_drive);
    std::printf("  r34 chart: 6th %.1f .. %.1f km/h, %u .. %u rpm (check %.1f km/h)\n", a.speed_lo[6].host() * 3.6f,
                a.speed_hi[6].host() * 3.6f, unsigned(a.rpm_lo[6]), unsigned(a.rpm_hi[6]), top * 3.6f);
    CHECK(std::fabs(a.speed_hi[6].host() - top) < top * 0.01f);
    for (int i = 1; i <= 6; ++i) CHECK(a.speed_lo[size_t(i)].host() < a.speed_hi[size_t(i)].host());

    CarSettings f = st;
    f.final_drive = st.final_drive * 5 / 4;
    const exact::GearChart b = chart(f);
    for (int i = 1; i <= 6; ++i) {
        const float k = b.speed_hi[size_t(i)].host() / a.speed_hi[size_t(i)].host();
        CHECK(std::fabs(k - float(st.final_drive) / float(f.final_drive)) < 1e-3f);
    }

    CarSettings g = st;
    g.gears[6] = st.gears[6] * 9 / 10;
    const exact::GearChart c = chart(g);
    for (int i = 1; i <= 5; ++i)
        CHECK(c.rpm_lo[size_t(i)] == a.rpm_lo[size_t(i)] && c.speed_lo[size_t(i)].bits == a.speed_lo[size_t(i)].bits &&
              c.speed_hi[size_t(i)].bits == a.speed_hi[size_t(i)].bits);
    CHECK(c.speed_hi[6].host() > a.speed_hi[6].host() && c.rpm_lo[6] < a.rpm_lo[6]);
}

TEST(car_spec_showroom_top_speed) {
    // The arcade car pages' speed gauge (menus §9.3): km/h as read off the original's gauges (reference captures, about
    // +-2 km/h).
    data::CarDatabase* db = database();
    if (!db) SKIP("needs GT3R_DISC");
    const struct {
        const char* car;
        float kmh;
    } kRef[] = {{"nsx_s_zero_97", 297}, {"rx7_rz_00", 285}, {"r32_gtr_v2_00", 277}, {"r34_gtr_v2_00", 270},
                {"lancer_evo7_gsr_01", 275}, {"xsara_wrc_99", 250}};
    for (const auto& r : kRef) {
        auto spec = build_car_spec(*db, r.car);
        CHECK(spec.has_value());
        if (!spec) continue;
        float kmh = std::floor(showroom_top_speed(*spec) * 3.6f);
        std::printf("  %s: %.0f km/h (reference %.0f)\n", r.car, kmh, r.kmh);
        CHECK(std::fabs(kmh - r.kmh) <= 4);
    }
}

TEST(car_spec_every_car_builds) {
    // Every car of the database builds, with a usable curve, gearing and tyres.
    data::CarDatabase* db = database();
    if (!db) SKIP("needs GT3R_DISC");
    int built = 0, failed = 0;
    for (const data::Car& car : db->cars()) {
        if (car.label.rfind("sample", 0) == 0) continue; // placeholder cars with empty body records
        std::string err;
        auto s = build_car_spec(*db, car.label, &err);
        if (!s) {
            if (++failed <= 5) std::printf("  %s: %s\n", car.label.c_str(), err.c_str());
            continue;
        }
        ++built;
        CHECK(!s->engine.curve_rpm.empty() && !s->gearbox.ratios.empty() && s->axle[0].tire_radius > 0.2f);
        CHECK(s->peak_power_kw() > 10 && s->mass >= 300);
    }
    std::printf("  %d cars built, %d failed\n", built, failed);
    CHECK(built > 150 && failed == 0);
}

TEST(engine_condition_rules) {
    // physics §10.3: the torque factor's running-in, wear and oil terms, the counters' advance and the odometer's step.
    CHECK(engine_condition_factor(0, 0) == 1.0f);
    CHECK(std::fabs(engine_condition_factor(-300000, 0) - 0.97f) < 1e-6f);
    CHECK(engine_condition_factor(250000, 0) == 1.0f);
    CHECK(std::fabs(engine_condition_factor(600000, 0) - 0.98f) < 1e-6f);
    CHECK(std::fabs(engine_condition_factor(0, -100000) - 1.05f) < 1e-6f);
    float e = 599990, o = -5;
    engine_condition_step(e, o, 20);
    CHECK(e == 600000 && o == 300000); // E capped; O jumps to its worn value when it reaches 0
    e = 0, o = 100;
    engine_condition_step(e, o, 50);
    CHECK(e == 50 && o == 150);
    u32 w = 10;
    CHECK(odometer_step(w, 5.5f) == 1.5f && w == 12);
    CHECK(odometer_step(w, 1.5f) == 1.5f && w == 12);
    CHECK(odometer_step(w, 0.0f) == 0.0f && odometer_step(w, -3.0f) == 0.0f && w == 12);
    w = kOdometerMax - 2;
    CHECK(odometer_step(w, 5.0f) == 0.0f && w == kOdometerMax);
    CHECK(oil_worn(300000) && !oil_worn(299999) && !oil_worn(-300000));
}
