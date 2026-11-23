// The race start in the original's own arithmetic (src/sim/exact/start.h) against the original's race initialisation, every
// byte of every car's record (race_start_exact).
//
// 1. Races set up by race_setup.h (the conductor's virtual 23, 0x1F3628, on the user's disc): after the initialisation every
//    car's whole record (0x16DC bytes) must equal ours from our data (start_setup.h): the database's equipment, the course
//    file's road, AI lines and driver sets, the event's rules, the race's setup inputs. The pointer words (the race info,
//    conductor, course, the entry's human object, the driver object's links) hold the original's objects' addresses and must
//    point at the corresponding objects. The AI course object (conductor + 0xBBA8) the initialisation loads is compared with
//    ours (ai_course_setup), every point of every line. Kinds: CPU races and rallies as race_setup builds them; "+pad" car 0
//    a player's entry (its input device set; pedal style, wheel type, aids off from the entry); "/rolling" the rolling start
//    (start type 3: every car at 80 km/h, car 0 in mode 8); "/cfg2", "/cfg3" the initialisation's configuration byte 2 / 3
//    (the CPU drivers varied by grid position; a player's car driven by the AI).
// 2. The moving start 0x1DD8A8 on every car of the database (table 30) at 80 and 120 km/h and at the idle and rev-limit
//    clamps (1 and 400 km/h): a start record with the car's parameter block (race_params), the original's 0x1DD8A8 against
//    moving_start, every byte.
//
// Options: GT3R_START_EVENTS=<event[+pad][/rolling][/cfg2|/cfg3][/noseg],...> ("-" none; "all": every event race_setup can
// build, as CPU races; "default": those in every kind and each course once with its AI line segments cleared), GT3R_START_LIST=1 (every differing record word of every car), GT3R_START_MOVING=0 (no part 2).

#include "harness.h"
#include "race_hits.h"
#include "race_setup.h"
#include "car_exact_probe.h"
#include "start_setup.h"

#include "disc/disc.h"
#include "disc/volume.h"
#include "formats/db/gt_db.h"
#include "formats/menu/mbl.h"
#include "game/gt_catalog.h"
#include "game/data/car_database.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <type_traits>
#include <string>
#include <vector>

using namespace gt3r;
using namespace gt3r::diff;

namespace {

namespace ex = sim::exact;
namespace rs = race_setup;
namespace ss = start_setup;

std::string fmt(const char* f, auto... a) {
    char b[512];
    std::snprintf(b, sizeof b, f, a...);
    return b;
}

// The field table's name of each record byte ("" outside the table).
const std::vector<std::string>& field_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n(ex::kCarSize);
        static const ex::Car probe{};
        ex::fields(probe, [&](const char* name, int i, u32 off, const auto& m) {
            const u32 w = std::is_same_v<std::remove_cvref_t<decltype(m)>, ee::F> ? 4 : u32(sizeof(m));
            std::string s = name;
            if (i >= 0) s += "[" + std::to_string(i) + "]";
            for (u32 k = 0; k < w && off + k < ex::kCarSize; ++k) n[off + k] = s;
        });
        return n;
    }();
    return names;
}

// The first differing record word, "" when none.
std::string record_diff(const u8* ours, const u8* theirs) {
    for (u32 off = 0; off < ex::kCarSize; ++off)
        if (ours[off] != theirs[off]) {
            const u32 w = off & ~3u;
            u32 a = 0, b = 0;
            for (u32 k = 0; k < 4 && w + k < ex::kCarSize; ++k) a |= u32(ours[w + k]) << (8 * k), b |= u32(theirs[w + k]) << (8 * k);
            const std::string& n = field_names()[off];
            return fmt("+0x%X (%s): ours %08X, original %08X", off, n.empty() ? "outside the field table" : n.c_str(), a, b);
        }
    return {};
}

struct Count {
    long n = 0, bad = 0;
    std::string first;
    void add(const std::string& what, const std::string& d) {
        ++n;
        if (!d.empty() && ++bad == 1) first = what + ": " + d;
    }
    std::string line(const char* what) const {
        return fmt("%s: %ld of %ld differ", what, bad, n) + (first.empty() ? std::string() : "; first: " + first);
    }
};

// The original's AI course object (conductor + 0xBBA8) against ours: the line slots and every point's fields.
std::string ai_course_diff(Ee& ee, u32 k, const ex::AiCourse& ours) {
    const u32 table = ee.r32(k + 4);
    if ((table != 0) != ours.table) return "table presence";
    if ((ee.r32(k + 8) != 0) != ours.pit_lines) return "pit lines flag";
    if (!table) return {};
    for (u32 g = 0; g < 8; ++g) {
        const u32 line = ee.r32(table + 8 + 4 * g);
        const u32 n = line ? ee.r32(line) : 0;
        const std::vector<ex::AiPoint>& p = ours.lines[g];
        if (n != p.size()) return fmt("line %u: %u points vs our %zu", g, n, p.size());
        for (u32 i = 0; i < n; ++i) {
            const u32 a = line + 4 + 0x28 * i;
            const ex::AiPoint& q = p[i];
            auto f = [&](u32 off, ee::F v, const char* what) -> std::string {
                const u32 t = ee.r32(a + off);
                return t == v.bits ? std::string() : fmt("line %u point %u %s: original %08X, ours %08X", g, i, what, t, v.bits);
            };
            auto b = [&](u32 off, u32 v, u32 size, const char* what) -> std::string {
                const u32 t = size == 1 ? ee.r8(a + off) : ee.r16(a + off);
                return t == v ? std::string() : fmt("line %u point %u %s: original %u, ours %u", g, i, what, t, v);
            };
            for (const std::string& d :
                 {b(0, q.kind, 1, "kind"), b(1, u8(q.brake_adjust), 1, "brake adjust"), b(2, u8(q.speed_adjust), 1, "speed adjust"),
                  b(3, q.x03, 1, "+0x03"), f(4, q.x, "x"), f(8, q.y, "y"), f(0xC, q.height, "height"), b(0x10, u16(q.seg), 2, "segment"),
                  b(0x12, u8(q.surface), 1, "surface"), b(0x13, q.x13, 1, "+0x13"), f(0x14, q.dist, "distance"), f(0x18, q.radius, "radius"),
                  f(0x1C, q.slope, "slope"), f(0x20, q.bank, "bank"), f(0x24, q.heading, "heading")})
                if (!d.empty()) return d;
        }
    }
    return {};
}

// A race kind: the event and what the set-up changes.
struct Kind {
    std::string event;
    std::string course; // "~<course>": the event's cars on another course file (more cars than its grid has slots)
    bool pad = false, rolling = false, noseg = false, overrides = false;
    int config = 0;
    ex::Conductor conductor = ex::Conductor::SinglePlayer;
    s32 handicap = 0;
};
Kind parse_kind(std::string s) {
    Kind k;
    if (const size_t at = s.find('~'); at != std::string::npos) {
        k.course = s.substr(at + 1);
        s.resize(at);
        if (const size_t m = k.course.find('@'); m != std::string::npos) s += k.course.substr(m), k.course.resize(m);
    }
    if (const size_t at = s.find('@'); at != std::string::npos) {
        static const std::pair<const char*, ex::Conductor> kModes[] = {{"licence", ex::Conductor::License},
                                                                       {"machine", ex::Conductor::MachineTest},
                                                                       {"tt", ex::Conductor::TimeTrial},
                                                                       {"freerun", ex::Conductor::FreeRun},
                                                                       {"practice", ex::Conductor::FreePractice},
                                                                       {"battle2p", ex::Conductor::Battle2P},
                                                                       {"battlemp", ex::Conductor::BattleMP}};
        std::string m = s.substr(at + 1);
        s.resize(at);
        if (const size_t h = m.find(':'); h != std::string::npos) k.handicap = std::atoi(m.c_str() + h + 1), m.resize(h);
        k.conductor = ex::Conductor(255);
        for (const auto& [name, c] : kModes)
            if (m == name) k.conductor = c;
    }
    for (;;) {
        if (s.size() > 8 && s.compare(s.size() - 8, 8, "/rolling") == 0) k.rolling = true, s.resize(s.size() - 8);
        else if (s.size() > 6 && s.compare(s.size() - 6, 6, "/noseg") == 0) k.noseg = true, s.resize(s.size() - 6);
        else if (s.size() > 9 && s.compare(s.size() - 9, 9, "/override") == 0) k.overrides = true, s.resize(s.size() - 9);
        else if (s.size() > 5 && s.compare(s.size() - 5, 5, "/cfg2") == 0) k.config = 2, s.resize(s.size() - 5);
        else if (s.size() > 5 && s.compare(s.size() - 5, 5, "/cfg3") == 0) k.config = 3, s.resize(s.size() - 5);
        else if (s.size() > 4 && s.compare(s.size() - 4, 4, "+pad") == 0) k.pad = true, s.resize(s.size() - 4);
        else break;
    }
    k.event = s;
    return k;
}

} // namespace

DIFF_TEST_ALL(race_start_exact, "The race start (0x1F3628 per car) vs sim::exact::start_car: every byte of every car's record") {
    struct {
        data::CarDatabase db;
    } hs;
    Volume& vol = ctx.disc->volume();
    if (!hs.db.load(vol)) {
        ctx.check(false, "database load: " + hs.db.error());
        return;
    }
    const Volume::Entry* dbe = vol.find(db_path(vol));
    if (!dbe) {
        ctx.check(false, db_path(ctx.disc->volume()) + " not found");
        return;
    }
    const Bytes gtar = vol.read(*dbe);
    std::string err;
    const ex::ProgramData* tables = ss::program_data(ctx, err);
    if (!tables) {
        ctx.check(false, err);
        return;
    }
    // The default: every event race_setup can build (its entries are cars, variants or arcade cars) as a CPU race, with car 0 a player's
    // in the rolling start, in configuration 2 and as a player under the AI (configuration 3); and each course once with the
    // AI line segments cleared.
    std::string events = std::getenv("GT3R_START_EVENTS") ? std::getenv("GT3R_START_EVENTS") : "default";
    const bool list_diffs = race_hits::env_num("GT3R_START_LIST", 0) != 0;
    const bool list_values = race_hits::env_num("GT3R_START_LIST", 0) > 1; // 2: with both values
    long unbuilt = 0;      // events with an entry that is not a car, a variant or an arcade car (race_setup builds only those)
    long courseless = 0;   // events whose course file the disc does not have (2002 Tokyo-Geneva's demo_gtc_ca)
    // Free practice on a course without car 0's pit box (fewer than 4 stop-line points: only car 0 starts in its box): the
    // program reads the box outside the line (from address 4 on with no stop line, whose slot the line loader 0x1F92F8 leaves
    // null), and ours refuses the race. Checked here: such a course has no stop line at all (1 to 3 points would put the box
    // before the line's points in the course data, which is not ported); the pre-race menus' FREE RUN switch (0x132BA0: off
    // when the course record's +8, 0x2389A0, is 2, a dirt course; the item's switch 0x12C430) is QuickMenuItems::visit's
    // rule; and every course of GT mode's races (the only menus with free practice) without the box is a dirt course.
    Count no_box;
    std::set<std::string> offered;    // courses without the box where GT3's GT race menus offer FREE RUN
    std::set<std::string> gt_courses; // the courses of GT mode's races (menu/gt_mode.mbl's series)
    std::set<std::string> gt_no_box;  // those of them without car 0's box
    if (const Volume::Entry* me = vol.find("menu/gt_mode.mbl")) {
        try {
            const Bytes mbl = vol.read(*me);
            for (const game::RaceSeries& s : game::Catalog::from_mbl(menu::MblFile::parse(mbl)).series)
                for (const std::string& r : s.races)
                    if (const data::Event* e = hs.db.event(r)) gt_courses.insert(e->course);
        } catch (const std::exception& x) {
            ctx.note(std::string("menu/gt_mode.mbl: ") + x.what());
        }
    }
    if (events == "all" || events == "default") {
        const bool kinds = events == "default";
        events.clear();
        std::set<std::string> seen, arcade;
        for (const data::CarVariant& a : hs.db.arcade_cars()) arcade.insert(a.label);
        for (const data::Event& e : hs.db.events()) {
            bool ok = !e.entries.empty();
            for (const std::string& l : e.entries)
                if (!l.empty() && l != "-" && !hs.db.variant(l) && !hs.db.car(l) && !arcade.count(l)) ok = false;
            if (!ok) {
                if (!e.entries.empty()) ++unbuilt;
                continue;
            }
            if (!vol.find(course_path(vol, e.course))) {
                ++courseless;
                continue;
            }
            for (const char* k : {"", "+pad", "+pad/rolling", "/cfg2", "+pad/cfg3"}) {
                events += (events.empty() ? "" : ",") + e.label + k;
                if (!kinds) break;
            }
            if (kinds && seen.insert(e.course).second)
                for (const char* k : {"/noseg", "/override", "@tt", "+pad@tt", "+pad/cfg2@tt", "@freerun", "+pad@freerun", "@practice", "+pad@practice",
                                      "@battle2p:15", "@battle2p:-30", "@battlemp", "+pad@licence", "+pad@machine"})
                    events += "," + e.label + k;
        }
        // A field larger than the course's grid (no GT3 menu makes one): the slots past the grid table read what follows it.
        if (kinds)
            for (const char* r : {"Gsunday_e_0001~L_seattle1", "Gsunday_e_0001+pad~L_seattle1", "Gsunday_e_0001~smtnorth",
                                  "Gsunday_e_0001~r_smtnorth", "Gsunday_e_0001~L_circle60_dry1"})
                if (hs.db.event("Gsunday_e_0001")) events += std::string(",") + r;
    }
    std::map<std::string, long> cover; // paths the races reached (from the records and the inputs)
    auto count = [&](const std::string& k, bool c = true) {
        if (c) ++cover[k];
    };
    Count cars, words, k_course, links;
    long races = 0;
    std::map<std::string, std::unique_ptr<ss::Course>> courses;
    for (size_t pos = 0; events != "-" && pos < events.size();) {
        size_t end = events.find(',', pos);
        if (end == std::string::npos) end = events.size();
        const std::string spec = events.substr(pos, end - pos);
        pos = end + 1;
        const Kind kind = parse_kind(spec);
        if (u8(kind.conductor) == 255) {
            ctx.check(false, spec + ": no such race mode");
            continue;
        }
        if (!rs::has_conductor(size_t(kind.conductor))) { // not in the disc's program
            count("races left out: no such conductor in the program");
            continue;
        }
        const data::Event* ev = hs.db.event(kind.event);
        const std::string cname = !ev ? std::string() : kind.course.empty() ? ev->course : kind.course;
        const Volume::Entry* ce = ev ? vol.find(course_path(vol, cname)) : nullptr;
        if (!ce) {
            ctx.check(false, spec + ": no such event or course");
            continue;
        }
        const Bytes course = vol.read(*ce);
        std::unique_ptr<ss::Course>& oc = courses[cname + (kind.noseg ? "/noseg" : "")];
        if (!oc) {
            oc = std::make_unique<ss::Course>();
            if (!ss::load_course(course, *tables, *oc, err, kind.noseg)) {
                ctx.check(false, spec + ": " + err);
                oc.reset();
                continue;
            }
        }
        // A time trial or free run with a second car (its ghost), a licence test or machine test alone, the others with the
        // event's cars.
        const ex::Conductor kc = kind.conductor;
        const size_t most = kc == ex::Conductor::TimeTrial || kc == ex::Conductor::FreeRun ? 2
                            : kc == ex::Conductor::License || kc == ex::Conductor::MachineTest ? 1
                                                                                                : 6;
        std::vector<std::string> labels;
        for (const std::string& e : ev->entries)
            if (!e.empty() && e != "-" && labels.size() < most) labels.push_back(e);
        if (labels.empty()) continue;
        if (kind.conductor == ex::Conductor::FreePractice && oc->ai.lines[4].size() < 4) {
            const ByteSpan* cr = hs.db.record(data::TableId::Course, cname);
            const bool dirt = cr && cr->size() > 8 && (*cr)[8] == 2;
            std::string d = oc->ai.lines[4].empty() ? "" : fmt("a stop line of %zu points", oc->ai.lines[4].size());
            if (!dirt && gt_courses.count(cname)) offered.insert(cname);
            if (gt_courses.count(cname)) gt_no_box.insert(cname + (dirt ? " (dirt)" : ""));
            if (d.empty() && ntsc_u()) { // the program's own answers (NTSC-U addresses)
                Ee& ee = ctx.ee;
                ee.reset(), ee.clear_hle(), ee.clear_probes(), ee.hle_libc_defaults();
                const u32 holder = ee.alloc(0x40);
                ee.w32(holder, ee.alloc_bytes(gtar));
                const u32 cat = u32(ee.call(0x002389A0, {Arg::ptr(holder), Arg::integer(s64(db::id_hash(cname)))}, 10'000'000).v0.lo);
                const u32 menu = ee.alloc(0x40); // the quick menu's switches: +0xC set on a dirt course (0x132BA0)
                ee.w32(menu + 0xC, cat == 2 ? 1 : 0);
                ee.w32(0x003514F0, menu);
                static const char kItem[] = "free_session";
                const u32 item = ee.alloc_bytes(ByteSpan(reinterpret_cast<const u8*>(kItem), sizeof kItem));
                const u32 on = u32(ee.call(0x0012C430, {Arg::ptr(0), Arg::ptr(item)}, 10'000'000).v0.lo);
                if ((cat == 2) != dirt || (on != 0) == dirt) d = fmt("the program's course category %u, FREE RUN %s", cat, on ? "on" : "off");
            }
            no_box.add(spec + " (" + cname + ")", d);
            continue;
        }
        rs::OrigRace race;
        auto before = [&](Ee& ee, const rs::OrigRace& o) {
            // The battles' race info words (+0xC tyre wear, +0x10 the handicap, +0x14 catch-up) and the free run's race object
            // (race info + 0x3C, as race_tick_exact gives it).
            if (kind.conductor == ex::Conductor::Battle2P || kind.conductor == ex::Conductor::BattleMP)
                ee.w32(o.info + 0xC, 1), ee.w32(o.info + 0x10, u32(kind.handicap)), ee.w32(o.info + 0x14, 1);
            if (kind.conductor == ex::Conductor::FreeRun) {
                // (as race_tick_exact's: room for PAL's ghost switch at + 0x21BA0)
                const u32 obj = ee.alloc(0x22000, 64), vt = ee.alloc(0x100), b = ee.alloc(0x60), c = ee.alloc(0x20);
                ee.w32(obj, vt), ee.w32(obj + 4, o.info);
                ee.w32(vt + 8 * 27 + 4, pa(0x00187548));
                ee.w32(obj + 0xC8C0 + 0x124, b), ee.w32(b + 4, 1), ee.w32(b + 0x50, c);
                ee.w32(o.info + 0x3C, obj);
            }
            if (kind.rolling) ee.w32(o.info + 0x54, 3);
            if (kind.noseg) ss::clear_line_segments(ee, o);
            if (kind.overrides) // every class's event overrides set, corner offsets of both signs
                for (u32 c = 0; c < 4; ++c) {
                    ee.w8(o.cond + cond_off(0xE340) + 0xC + c, u8(70 + 10 * c)), ee.w8(o.cond + cond_off(0xE340) + 0x10 + c, u8(60 + 30 * c));
                    ee.w8(o.cond + cond_off(0xE340) + 0x14 + c, u8(c == 3 ? 0 : 90 + 15 * c)), ee.w8(o.cond + cond_off(0xE340) + 0x18 + c, u8(s8(c * 9 - 12)));
                }
            if (kind.pad) { // entry 0 a player's: its own human object with an input device
                const u32 entry = ee.r32(ee.r32(ee.r32(o.info + 0x18) + 8));
                const u32 human = ee.alloc(0x200);
                ee.w32(human + 0x13C, 1);
                ee.w32(entry + entry_off(0x20DC), human);
                ee.w32(entry + entry_off(0x20E4), 1); // pedal style
                ee.w32(entry + entry_off(0x20E8), 1); // the spec builder's flag
                ee.w32(entry + entry_off(0x20EC), 2); // wheel type
                ee.w32(entry + entry_off(0x20F0), 0), ee.w32(entry + entry_off(0x20F4), 0); // ASM, TCS off
                for (u32 k = 0; k < 4; ++k) ee.w32(entry + entry_off(0x2104) + 4 * k, 0x01000000 * (k + 1) + k); // condition words
            }
        };
        if (!rs::setup_original_race(ctx.ee, hs.db, gtar, *ev, course, labels, 2, 0, race, err, before,
                                     rs::kConductors[size_t(kind.conductor)], kind.config)) {
            ctx.check(false, spec + ": " + err);
            continue;
        }
        ++races;
        std::vector<ex::Record> ours;
        try {
            ours = ss::start_records(ctx.ee, race, hs.db, *ev, labels, *oc, *tables, kind.config, kind.conductor, [&](ex::StartRace& r) {
                if (kind.overrides)
                    for (u32 c = 0; c < 4; ++c)
                        r.rules[0xC + c] = u8(70 + 10 * c), r.rules[0x10 + c] = u8(60 + 30 * c), r.rules[0x14 + c] = u8(c == 3 ? 0 : 90 + 15 * c),
                                  r.rules[0x18 + c] = u8(s8(c * 9 - 12));
            });
        } catch (const std::exception& e) {
            ctx.check(false, spec + ": " + e.what());
            continue;
        }
        k_course.add(spec, ai_course_diff(ctx.ee, race.cond + cond_off(0xBBA8), oc->ai));
        {
            bool file_segments = false, arcs_right = false, past_half = false;
            for (const course::AiLine& l : oc->lines.lines)
                for (const course::AiPoint& p : l.points) file_segments = file_segments || s16(p.segment) != 0;
            for (size_t g = 0; g < 8; ++g)
                for (const ex::AiPoint& p : oc->ai.lines[g]) {
                    arcs_right = arcs_right || p.kind == 2;
                    past_half = past_half || (g == 4 && oc->ai.lap_length < p.dist);
                }
            count(file_segments ? "AI lines with the file's segments" : "AI lines traced (no segment words)");
            count("AI lines with right-hand arcs marked (kind 2)", arcs_right);
            count("stop lines with a lap added", past_half);
            const ex::StartRace r = ss::start_race(ctx.ee, race, hs.db, *ev, *oc, *tables, kind.config, kind.conductor);
            count("races with tyre wear", !(r.wear.max == ee::F::raw(0)));
            count("races in configuration 2 / 3", kind.config != 0);
        }
        for (int i = 0; i < race.ncars; ++i) {
            const u32 car = rs::car_addr(race.cond, i);
            const std::vector<u8> theirs = car_exact::snapshot(ctx.ee, car); // NTSC-U's layout
            { // a start off the road (a grid slot past the course's table): the missed ground records' stack words as the original's
                ex::Car o, t;
                o.behaviour = t.behaviour = tables->behaviour();
                ex::load_image(o, ours[size_t(i)].data()), ex::load_image(t, theirs.data());
                if (const int k = car_exact::take_missed_ground(o, t)) cover["wheels started off the road (their stack words taken)"] += k, ex::store_image(o, ours[size_t(i)].data());
            }
            const std::string d = record_diff(ours[size_t(i)].data(), theirs.data());
            cars.add(fmt("%s car %d (%s)", spec.c_str(), i, labels[size_t(i)].c_str()), d);
            if (!d.empty() && list_diffs) {
                std::string all;
                for (u32 w = 0; w < ex::kCarSize; w += 4)
                    if (std::memcmp(ours[size_t(i)].data() + w, theirs.data() + w, std::min(4u, ex::kCarSize - w)))
                        all += list_values ? fmt(" +0x%X(%s: ours %08X, original %08X)", w, field_names()[w].c_str(), rd32(ByteSpan(ours[size_t(i)].data(), ex::kCarSize), w & ~3u),
                                                 rd32(ByteSpan(theirs.data(), ex::kCarSize), w & ~3u))
                                           : fmt(" +0x%X(%s)", w, field_names()[w].c_str());
                ctx.note(fmt("%s car %d:", spec.c_str(), i) + all);
            }
            for (u32 w = 0; w < ex::kCarSize; w += 4)
                words.add(fmt("%s car %d", spec.c_str(), i),
                          std::memcmp(ours[size_t(i)].data() + w, theirs.data() + w, std::min(4u, ex::kCarSize - w)) ? "differs" : "");
            // The pointer words point at the corresponding objects.
            const u32 entry = ctx.ee.r32(ctx.ee.r32(ctx.ee.r32(race.info + 0x18) + 8) + 4 * u32(i));
            std::string l;
            if (ctx.ee.r32(car) != race.info) l = "car +0 is not the race info";
            else if (ctx.ee.r32(car + 4) != race.cond) l = "car +4 is not the conductor";
            else if (ctx.ee.r32(car + 8) != race.course) l = "car +8 is not the course";
            else if (ctx.ee.r32(car + 0xC) != ctx.ee.r32(entry + entry_off(0x20DC))) l = "car +0xC is not the entry's human object";
            else if (ctx.ee.r32(car + 0xEC8) != race.cond || ctx.ee.r32(car + 0xECC) != car || ctx.ee.r32(car + 0xED0) != race.cond + cond_off(0xBBA8))
                l = "the driver object's links";
            links.add(fmt("%s car %d", spec.c_str(), i), l);
            ctx.sample();
            // Paths, from the original's record (ours is the same).
            ex::Car c;
            ex::load_image(c, theirs.data());
            count(fmt("cars of control source %d", c.control));
            count("ghost cars", c.ghost != 0);
            count("arcade cars (table 35)", !hs.db.variant(labels[size_t(i)]) && !hs.db.car(labels[size_t(i)]));
            for (const ex::Ground& g : c.g) count("wheels off the road at the start", g.miss != 0);
            count("cars on the pit-lane surface", (c.x535 & 0x10) != 0);
            count("cars ranked off their index", c.rank != i + 1);
            count(fmt("cars starting lap %d", c.laps));
            count("cars on an initial line other than 6", c.ai.line != 6);
            count("gear hints", c.ai.gear_hint != -1);
            count("gear hints while braking", c.ai.gear_braking != 0);
            count("clutch-pack differentials locked", c.diff_lock[0] || c.diff_lock[1]);
            count("centre clutches engaged", c.centre_locked != 0);
            count(fmt("shift lock-out %d", c.shift_lockout));
            count("moving starts", !(c.speed == ee::F::raw(0)));
            count("turbos spooled at the start", !(c.turbo_load == ee::F::raw(0)));
            count("players driven by the AI after the start", c.ai_hold != 0);
            count("gearbox parts of kind 0x13", c.x16d0 == 0);
            count("tyres started worn", c.w[0].wear_level != 0);
            const int cls = c.p.drive_class;
            const u32 cfg = race.cond + cond_off(0xE340);
            if (c.control != 0) {
                const u8 tp = ctx.ee.r8(cfg + 0xC + u32(cls)), bp = ctx.ee.r8(cfg + 0x10 + u32(cls));
                const u8 cp = ctx.ee.r8(cfg + 0x14 + u32(cls)), op = ctx.ee.r8(cfg + 0x18 + u32(cls));
                count("event overrides: throttle", tp != 0 && tp != 100);
                count("event overrides: brake", bp != 0 && bp != 100);
                count("event overrides: corner speed", cp != 0 && cp != 100);
                count("event overrides: corner offset", op != 0);
            }
            count(fmt("driveline class %d", cls));
        }
    }
    ctx.note(fmt("%ld races", races) + (unbuilt ? fmt(" (%ld events left out: an entry race_setup cannot build)", unbuilt) : "") +
             (courseless ? fmt(" (%ld left out: no course file on the disc)", courseless) : ""));
    ctx.note(no_box.line("free practices on courses without car 0's pit box (no stop line; the menus' FREE RUN switch)"));
    ctx.check(no_box.bad == 0, no_box.first);
    auto names = [](const std::set<std::string>& set) {
        std::string l;
        for (const std::string& c : set) l += (l.empty() ? "" : ", ") + c;
        return l.empty() ? std::string("none") : l;
    };
    ctx.note(fmt("GT mode races' courses: %zu; without car 0's pit box: ", gt_courses.size()) + names(gt_no_box));
    // A course of GT mode's races without the box that is not a dirt course: GT3 offers its free practice, and puts the car
    // where the console's memory says; ours refuses it.
    ctx.check(offered.empty(), "GT3 offers free practice on courses without a stop line: " + names(offered));
    ctx.note(cars.line("car records after the initialisation"));
    ctx.note(words.line("record words"));
    ctx.note(k_course.line("AI course objects (0x1F9B98 / 0x1F92F8 vs ai_course_setup)"));
    ctx.note(links.line("pointer words"));
    std::string cov;
    for (const auto& [k, n] : cover) cov += fmt("%s%s %ld", cov.empty() ? "" : "; ", k.c_str(), n);
    ctx.note("coverage: " + cov);
    for (const Count* c : {&cars, &k_course, &links}) ctx.check(c->bad == 0, c->first);
    if (events != "-") ctx.check(cars.n > 0, "no cars compared");

    // ---- 2. the moving start on every car ----------------------------------------------------------------------------------
    if (race_hits::env_num("GT3R_START_MOVING", 1) == 0) return;
    const data::Event* ev = hs.db.event("Gturbo_e_0002");
    if (!ev && !hs.db.events().empty()) ev = &hs.db.events().front(); // another game's database: any event's course
    const Volume::Entry* ce = ev ? vol.find(course_path(vol, ev->course)) : nullptr;
    if (!ce) {
        ctx.check(false, "moving start: no event Gturbo_e_0002 or its course");
        return;
    }
    const Bytes course = vol.read(*ce);
    rs::OrigRace race;
    if (!rs::setup_original_race(ctx.ee, hs.db, gtar, *ev, course, {"r34_gtr_v_99"}, 2, 0, race, err)) {
        ctx.check(false, "moving start: " + err);
        return;
    }
    const std::vector<u8> base = car_exact::snapshot(ctx.ee, rs::car_addr(race.cond, 0));
    const u32 scratch = ctx.ee.alloc(car_stride(), 64);
    const ByteSpan* cr = hs.db.record(data::TableId::Course, ev->course);
    const int mode = cr && cr->size() > 8 && (*cr)[8] != 0xFF ? (*cr)[8] : 0;
    Count moving;
    for (const data::Car& car : hs.db.cars()) {
        ex::ConvertInputs in;
        in.base = scratch + ex::kParams;
        in.control = 2;
        for (u32 k = 0; k < 12; ++k) in.rules[k] = ctx.ee.r8(race.cond + cond_off(0xE340) + k);
        const ex::ParamImage p = ex::race_params(hs.db, *tables, ex::stock_equipment(hs.db, car.label, car.parts), mode, true, true, in);
        ex::Record img{};
        std::copy(base.begin(), base.end(), img.begin());
        std::copy(p.begin(), p.end(), img.begin() + ex::kParams);
        for (const int kmh : {80, 120, 1, 400}) {
            const ee::F v = ee::int_to_f(kmh) * ee::F::raw(0x3E8E38E4);
            car_exact::write_image(ctx.ee, scratch, img.data());
            ctx.ee.call(pa(0x001DD8A8), {Arg::ptr(scratch), Arg::fbits(v.bits)}, 20'000'000);
            const std::vector<u8> theirs = car_exact::snapshot(ctx.ee, scratch);
            ex::Car c;
            c.behaviour = disc_behaviour();
            ex::load_image(c, img.data());
            ex::moving_start(c, v);
            ex::Record mine = img;
            ex::store_image(c, mine.data());
            moving.add(fmt("%s at %d km/h", car.label.c_str(), kmh), record_diff(mine.data(), theirs.data()));
            ctx.sample();
        }
    }
    ctx.note(moving.line("moving starts (0x1DD8A8 vs moving_start)"));
    ctx.check(moving.bad == 0, moving.first);
    ctx.check(moving.n > 0, "no moving starts");
}
