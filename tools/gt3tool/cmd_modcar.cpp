// gt3tool mod-car commands: check car definitions (mods/<id>/cars/*.ini, see
// docs/MODDING.md "Adding cars") against the disc and test-drive them.
//
//   gt3tool <iso> modcar <file.ini | mod dir | mods dir> ...
//
// For each car: load errors and warnings, the resulting catalogue figures and
// spec, then test drives on the game's own physics (the exact core) on the
// machine tests' course: a standing start at full throttle down the straight
// (0-60 mph, 0-100 km/h, quarter mile), a 60-0 mph stop and a lift-off from
// 100 km/h, and GT3's top-speed test (the race's AI at the wheel), next to the
// [reference] figures in the file.

#include "tool.h"

#include "base/config.h"
#include "base/file.h"
#include "game/data/car_database.h"
#include "game/data/mod_cars.h"
#include "game/gt_mode.h"
#include "disc/core_image.h"
#include "race/backend.h"
#include "race/exact_field.h"
#include "sim/car_spec.h"
#include "sim/exact/race.h"
#include "vfs/vfs.h"

#include <SDL3/SDL_filesystem.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace gt3r::tool {

namespace {

namespace ex = sim::exact;

std::vector<std::string> list_dir(const std::string& dir) {
    std::vector<std::string> out;
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* user, const char*, const char* name) {
            static_cast<std::vector<std::string>*>(user)->push_back(name);
            return SDL_ENUM_CONTINUE;
        },
        &out);
    std::sort(out.begin(), out.end());
    return out;
}

bool is_ini(const std::string& n) { return n.size() > 4 && to_lower(n.substr(n.size() - 4)) == ".ini"; }

// A .ini file, a mod (has cars/), or a mods folder (holds mods).
void collect(const std::string& path, std::vector<std::string>& out) {
    if (path.rfind("disc:", 0) == 0) {
        out.push_back(path);
        return;
    }
    if (!host_is_directory(path)) {
        out.push_back(path);
        return;
    }
    if (host_is_directory(path + "/cars")) {
        for (const std::string& n : list_dir(path + "/cars"))
            if (is_ini(n)) out.push_back(path + "/cars/" + n);
        return;
    }
    for (const std::string& n : list_dir(path)) {
        if (is_ini(n)) out.push_back(path + "/" + n);
        else if (host_is_directory(path + "/" + n + "/cars")) collect(path + "/" + n, out);
    }
}

// The machine tests' course (event M1000's) and the race set-up the drives share.
struct TestCourse {
    data::CarDatabase* db = nullptr;
    std::shared_ptr<const ex::ProgramData> data;
    std::unique_ptr<Vfs> vfs;
    std::string course;
    Bytes file;
    std::unique_ptr<race::RaceTrack> track;
    std::string error;

    bool load(Disc& disc, data::CarDatabase& database) {
        db = &database;
        try {
            std::optional<Bytes> core = disc.volume().read_program();
            if (!core) throw FormatError("no game program on the disc");
            data = std::make_shared<const ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(*core)));
            const data::Event* ev = db->event("M1000");
            if (!ev) throw FormatError("no machine test event M1000");
            course = ev->course;
            vfs = std::make_unique<Vfs>(&disc.volume());
            const std::string path = course_file_path(*vfs, course);
            std::optional<Bytes> f = path.empty() ? std::nullopt : vfs->read(path);
            if (!f) throw FormatError("no course file for " + course);
            file = *f;
            track = race::make_track(*vfs, file, course);
        } catch (const std::exception& e) {
            error = e.what();
            return false;
        }
        return true;
    }

    // A run of machine test `event` with `car`: the pad's frames from the screen-style inputs given each step, or the race's
    // AI at the wheel (config 3).
    std::unique_ptr<race::RaceField> run(const std::string& event, const std::string& car, bool ai, std::string& err) const {
        race::FieldConfig cfg;
        cfg.event = event;
        cfg.max_opponents = 0;
        cfg.exact.data = data;
        cfg.exact.course_file = file;
        cfg.exact.course = course;
        cfg.exact.kind = ex::Conductor::MachineTest;
        cfg.exact.config = ai ? 3 : 0;
        cfg.exact.player.car = {car};
        return race::make_exact_field(*track, *db, cfg, &err);
    }
};

constexpr double kStep = 1.0 / sim::kDefaultTickHz;

struct Run {
    double t_roll = -1, t60 = -1, t100 = -1, t400 = -1, trap_kmh = 0;
    double spin_kmh = 0, spin_at_kmh = 0; // peak driven-wheel rim speed over road speed before 60 mph
    std::string error;
};

// A standing start at full throttle down the straight (automatic gearbox, wheel straight), timed from GO.
Run straight_line(const TestCourse& tc, const std::string& car) {
    Run r;
    std::unique_ptr<race::RaceField> f = tc.run("M1000", car, false, r.error);
    if (!f) return r;
    race::RaceVehicle& v = f->player();
    sim::ControlInputs in;
    in.throttle = 1;
    in.controller_kind = 2;
    std::optional<sim::Vec3> start;
    for (int i = 0; i < int(90 / kStep) && r.t400 < 0; ++i) {
        v.step(in, kStep);
        const double t = f->race_time();
        if (t <= 0) continue; // (the start's hold)
        if (!start) start = v.state().position;
        const float kmh = v.state().speed_kmh;
        const float dist = (v.state().position - *start).length();
        if (r.t_roll < 0 && dist >= 0.3048f) r.t_roll = t;
        if (r.t60 < 0)
            for (int w = 0; w < 4; ++w)
                if (double d = v.state().wheels[size_t(w)].angular_velocity * v.setup().wheels[size_t(w)].radius * 3.6f - kmh;
                    d > r.spin_kmh)
                    r.spin_kmh = d, r.spin_at_kmh = kmh;
        if (r.t60 < 0 && kmh >= 96.56f) r.t60 = t;
        if (r.t100 < 0 && kmh >= 100) r.t100 = t;
        if (r.t400 < 0 && dist >= 402.34f) r.t400 = t, r.trap_kmh = kmh;
    }
    return r;
}

// GT3's top-speed test (MMAX) with the race's AI at the wheel: the run's highest speed (km/h) up to its split line.
double top_speed_kmh(const TestCourse& tc, const std::string& car, std::string& err) {
    std::unique_ptr<race::RaceField> f = tc.run("MMAX", car, true, err);
    if (!f) return -1;
    const ex::ExactRace* race = race::exact_race_of(*f);
    for (int i = 0; i < int(240 / kStep) && race->cars()[0].finished == 0; ++i) f->player().step({}, kStep);
    return race->cars()[0].max_speed.host() * 3.6;
}

// Full throttle to 100 km/h down the straight, then `then` (the brake, or a lift-off) until the car stops or 25 s pass.
template <class Each>
void from_100(const TestCourse& tc, const std::string& car, float brake, Each&& each, std::string& err) {
    std::unique_ptr<race::RaceField> f = tc.run("M1000", car, false, err);
    if (!f) return;
    race::RaceVehicle& v = f->player();
    sim::ControlInputs in;
    in.throttle = 1;
    in.controller_kind = 2;
    for (int i = 0; i < int(60 / kStep) && (f->race_time() <= 0 || v.state().speed_kmh < 100); ++i) v.step(in, kStep);
    in.throttle = 0, in.brake = brake;
    double t = 0;
    for (int i = 0; i < int(25 / kStep); ++i) {
        const sim::Vec3 p0 = v.state().position;
        v.step(in, kStep);
        t += kStep;
        if (!each(t, v.state().speed_kmh, (v.state().position - p0).length())) break;
    }
}

// 60-0 mph stop with full brakes, feet.
double brake_60_0_ft(const TestCourse& tc, const std::string& car, std::string& err) {
    double dist = 0;
    bool started = false;
    from_100(tc, car, 1.0f, [&](double, float kmh, float step_m) {
        if (started) dist += step_m;
        if (!started && kmh <= 96.56f) started = true;
        return kmh >= 0.5f;
    }, err);
    return dist * 3.28084;
}

// Lift-off from 100 km/h: average deceleration 100->50 and 50->10 km/h (g), and the speed after 25 s.
struct Coast {
    double g_high = 0, g_low = 0, end_kmh = 0;
};

Coast coast_down(const TestCourse& tc, const std::string& car, std::string& err) {
    Coast c;
    double t50 = -1, t10 = -1;
    from_100(tc, car, 0.0f, [&](double t, float kmh, float) {
        if (t50 < 0 && kmh <= 50) t50 = t;
        if (t10 < 0 && kmh <= 10) t10 = t;
        c.end_kmh = kmh;
        return true;
    }, err);
    if (t50 > 0) c.g_high = 50 / 3.6 / t50 / 9.81;
    if (t10 > 0 && t50 > 0) c.g_low = 40 / 3.6 / (t10 - t50) / 9.81;
    return c;
}

std::string secs(double t) {
    char b[32];
    std::snprintf(b, sizeof(b), t < 0 ? "  -  " : "%5.2f", t);
    return b;
}

int cmd_modcar(Disc& disc, const Args& a) {
    std::vector<std::string> files;
    for (const std::string& p : a) {
        if (p.rfind("arcade:", 0) == 0) { // every arcade car of a class: arcade:CLASS_S
            game::GtData gd;
            Vfs vfs(&disc.volume()); // the disc release's game config (Vfs::kRegionFile)
            if (!gd.load([&](const std::string& path) { return vfs.read(path); })) {
                std::fprintf(stderr, "error: %s\n", gd.error().c_str());
                return 1;
            }
            auto it = gd.arcade_classes.find(p.substr(7));
            if (it == gd.arcade_classes.end()) continue;
            for (const auto& c : it->second) files.push_back("disc:" + c.label);
            continue;
        }
        collect(p, files);
    }
    if (files.empty()) {
        std::fprintf(stderr, "no car definitions found\n");
        return 1;
    }
    data::CarDatabase db;
    if (!db.load(disc.volume())) {
        std::fprintf(stderr, "error: %s\n", db.error().c_str());
        return 1;
    }
    TestCourse tc;
    if (!tc.load(disc, db)) std::fprintf(stderr, "warning: no test drives (%s)\n", tc.error.c_str());
    int bad = 0;
    for (const std::string& f : files) {
        std::printf("== %s\n", f.c_str());
        if (f.rfind("disc:", 0) == 0) { // a disc car, for comparison
            std::string serr;
            auto spec = sim::build_car_spec(db, f.substr(5), &serr);
            if (!spec) {
                std::printf("   error: %s\n", serr.c_str());
                continue;
            }
            std::printf("   %s: %.0f kg, %.0f kW, %.2f kg/PS\n", spec->name.c_str(), spec->mass, spec->peak_power_kw(),
                        (spec->mass - 120) / std::max(1.0f, spec->peak_power_kw() * 1.35962f));
            if (!tc.track) continue;
            Run r = straight_line(tc, f.substr(5));
            std::string err;
            std::printf("   0-60 mph  %s s, %s s with a 1 ft rollout\n", secs(r.t60).c_str(), secs(r.t60 - r.t_roll).c_str());
            std::printf("   1/4 mile  %s s @ %.0f mph\n", secs(r.t400).c_str(), r.trap_kmh / 1.609344);
            std::printf("   top speed %.0f km/h\n", top_speed_kmh(tc, f.substr(5), err));
            std::printf("   60-0 mph  %.0f ft\n", brake_60_0_ft(tc, f.substr(5), err));
            if (!r.error.empty() || !err.empty()) std::printf("   error: %s\n", (r.error.empty() ? err : r.error).c_str());
            continue;
        }
        auto bytes = read_host_file(f);
        if (!bytes) {
            std::printf("   error: cannot read\n");
            ++bad;
            continue;
        }
        std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size()), err;
        std::vector<std::string> warns;
        auto def = data::parse_mod_car(text, &err, &warns);
        for (const std::string& w : warns) std::printf("   warning: %s\n", w.c_str());
        if (def) def->file = f;
        if (!def || !data::add_mod_car(db, *def, &err)) {
            std::printf("   error: %s\n", err.c_str());
            ++bad;
            continue;
        }
        db.rebuild();
        const data::Car* c = db.car(def->label);
        auto s = db.summarize(*c);
        std::printf("   %s %s (%u) [%s], %u Cr, donor %s, shell %s\n", s.maker.c_str(), s.name.c_str(), s.year,
                    def->label.c_str(), s.price, def->donor.c_str(), s.model.c_str());
        std::printf("   %s %s, %u PS (%.0f kW), %.1f kgfm, %u kg, %u x %u x %u mm, wb %u, front %u%%\n", s.drive.c_str(),
                    s.aspiration.c_str(), s.power_ps, s.power_ps / 1.35962f, s.torque_kgfm, s.weight_kg, s.length_mm,
                    s.width_mm, s.height_mm, s.wheelbase_mm, s.front_weight_pct);
        std::string serr;
        auto spec = sim::build_car_spec(db, def->label, &serr);
        if (!spec) {
            std::printf("   error: spec: %s\n", serr.c_str());
            ++bad;
            continue;
        }
        float total = spec->gearbox.ratios.empty() ? 0 : spec->gearbox.ratios[0] * spec->gearbox.final_drive;
        std::printf("   spec: %.0f kg, %s, peak %.0f N*m, %.0f kW @ %.0f rpm, limit %.0f rpm, %zu gear(s), 1st x final %.2f, "
                    "tyres r %.3f/%.3f m, drag %.3f N/(m/s)^2\n",
                    spec->mass, spec->engine.electric ? "electric" : "engine", spec->peak_torque(), spec->peak_power_kw(),
                    spec->peak_power_rpm(), spec->engine.rev_limit_rpm, spec->gearbox.ratios.size(), total,
                    spec->axle[0].tire_radius, spec->axle[1].tire_radius, spec->aero.c_drag);
        if (spec->engine.electric)
            std::printf("   electric: the test drives run on its donor's engine (%s) until the game's physics has the EV drive\n",
                        def->donor.c_str());
        if (!tc.track) continue;
        Run r = straight_line(tc, def->label);
        if (!r.error.empty()) {
            std::printf("   error: test drive: %s\n", r.error.c_str());
            ++bad;
            continue;
        }
        IniDocument ini = IniDocument::parse(text);
        auto ref = [&](const char* k) {
            auto v = ini.get("reference", k);
            if (!v) return std::string();
            std::string t(trim(*v));
            if (size_t p = t.find(" ;"); p != std::string::npos) t = std::string(trim(t.substr(0, p)));
            return "   (real " + t + ")";
        };
        std::printf("   0-60 mph  %s s, %s s with a 1 ft rollout like US magazine tests%s\n", secs(r.t60).c_str(),
                    secs(r.t60 - r.t_roll).c_str(), ref("zero_to_60_s").c_str());
        std::printf("   launch    wheelspin peaks %.0f km/h over road speed at %.0f km/h\n", r.spin_kmh, r.spin_at_kmh);
        std::printf("   0-100 km/h%s s%s\n", secs(r.t100).c_str(), ref("zero_to_100_s").c_str());
        std::printf("   1/4 mile  %s s @ %.0f mph (rollout %s s)%s\n", secs(r.t400).c_str(), r.trap_kmh / 1.609344,
                    secs(r.t400 - r.t_roll).c_str(), ref("quarter_mile_s").c_str());
        std::string drive_err;
        const double top = top_speed_kmh(tc, def->label, drive_err);
        std::printf("   top speed %.0f km/h (%.0f mph), GT3's top-speed test%s\n", top, top / 1.609344, ref("top_speed_kmh").c_str());
        std::printf("   60-0 mph  %.0f ft%s\n", brake_60_0_ft(tc, def->label, drive_err), ref("brake_60_0_ft").c_str());
        Coast cd = coast_down(tc, def->label, drive_err);
        if (!drive_err.empty()) std::printf("   error: test drive: %s\n", drive_err.c_str());
        std::printf("   lift-off  %.2f g (100-50 km/h), %.2f g (50-10), %.1f km/h after 25 s\n", cd.g_high, cd.g_low, cd.end_kmh);
    }
    for (const std::string& e : db.mod_car_errors()) std::printf("error: %s\n", e.c_str());
    return bad ? 1 : 0;
}

} // namespace

GT3TOOL_COMMAND(modcar, "modcar", "<car.ini | mod dir | mods dir | disc:<label>>...", "check and test-drive mod car definitions (disc cars for comparison)", 1,
                cmd_modcar);

} // namespace gt3r::tool
