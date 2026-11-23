// Our race start for a race_setup race; see start_setup.h.

#include "start_setup.h"

#include "disc/core_image.h"
#include "disc/disc.h"
#include "formats/course/course_file.h"
#include "game/data/car_database.h"

namespace gt3r::diff::start_setup {

void clear_line_segments(Ee& ee, const race_setup::OrigRace& o) {
    const u32 block = ee.r32(o.container + 4 * 31);
    for (u32 k = 0; block && k < 7; ++k)
        if (const u32 off = ee.r32(block + 0xC + 4 * k)) {
            const u32 line = block + off, n = ee.r32(line);
            for (u32 i = 0; i < n; ++i) ee.w32(line + 4 + 0x28 * i + 0x10, 0);
        }
}

bool load_course(ByteSpan file, const ex::ProgramData& d, Course& out, std::string& err, bool no_segments) {
    try {
        const course::CourseFile cf = course::CourseFile::parse(file);
        const ByteSpan road = cf.section(file, 28);
        if (road.empty()) {
            err = "course has no road block";
            return false;
        }
        out.road = course::Road::parse(road);
        const ByteSpan lines = cf.section(file, 31);
        out.has_lines = !lines.empty();
        if (out.has_lines) out.lines = course::AiLines::parse(lines);
        if (no_segments)
            for (course::AiLine& l : out.lines.lines)
                for (course::AiPoint& p : l.points) p.segment = 0;
        out.sets = course::DriverSets::parse(file, cf);
        out.query = std::make_unique<ex::CourseQuery>(out.road);
        out.ai = ex::ai_course_setup(*out.query, out.has_lines ? &out.lines : nullptr, d);
    } catch (const std::exception& e) {
        err = std::string("course parse: ") + e.what();
        return false;
    }
    return true;
}

const ex::ProgramData* program_data(DiffContext& ctx, std::string& err) {
    static std::unique_ptr<ex::ProgramData> d;
    static std::string d_err;
    if (!d && d_err.empty()) {
        try {
            d = std::make_unique<ex::ProgramData>(ex::ProgramData::load(CoreImage::decode(ctx.disc->read_core_gt3())));
            // the harness checks the disc's own release (a later build's with its routines' variants, disc_behaviour())
            d->set_behaviour(sim::concept_program(d->program()) ? disc_behaviour() : ex::release_behaviour(d->program()));
        } catch (const std::exception& e) {
            d_err = e.what();
        }
    }
    err = d_err;
    return d.get();
}

ex::StartRace start_race(Ee& ee, const race_setup::OrigRace& o, const data::CarDatabase& db, const data::Event& ev, const Course& c,
                         const ex::ProgramData& d, int config, ex::Conductor kind) {
    ex::StartRace r;
    r.db = &db, r.course = c.query.get(), r.ai = &c.ai, r.sets = &c.sets, r.data = &d;
    r.rules = ex::race_config(ev.bytes);
    r.rules[1] = ee.r8(o.cond + cond_off(0xE340) + 1); // the race's lap count (set after the event configuration)
    const ByteSpan* cr = db.record(data::TableId::Course, ev.course);
    r.category = cr ? ex::race_course_category(*cr) : 0;
    r.config = u8(config);
    r.kind = kind;
    // The conductor's virtuals 11 and 10 as virtual 21 asks them (the battles write their own bytes into the configuration).
    ex::rubber_band_answer(kind, r.rules, s32(ee.r32(o.info + 0x14)), d.behaviour().rule(ex::Rule::CatchupAnswer));
    r.wear = ex::wear_params(r.rules, d, ex::wear_answer(kind, r.rules, r.category, s32(ee.r32(o.info + 0xC)), d.behaviour().rule(ex::Rule::WearAnswer)));
    r.start_type = s32(ee.r32(o.info + 0x54));
    r.handicap = s32(ee.r32(o.info + 0x10));
    r.mark_pit = ex::has_pits(kind);
    r.info = o.info, r.conductor = o.cond, r.course_addr = o.course;
    r.ai_course_at = cond_off(0xBBA8);
    r.road = ee.r32(o.container + 4 * 28); // the road block as the course loader left it (a slot past the grid reads it)
    return r;
}

ex::StartEntry start_entry(Ee& ee, const race_setup::OrigRace& o, const data::CarDatabase& db, const std::string& label, int i) {
    ex::StartEntry e;
    std::array<u64, 27> parts{};
    // As race_setup builds the entry: a table 31 variant from its record (0x238550's 0x231C50 / 0x234150), else set-car
    // (0x231DB0) on the label, which takes an arcade car (table 35) first, else the car (table 30).
    const data::CarVariant* variant = db.variant(label);
    if (!variant)
        for (const data::CarVariant& a : db.arcade_cars())
            if (!variant && a.label == label) variant = &a;
    if (const data::Car* b = variant ? nullptr : db.car(label)) parts = b->parts;
    e.eq = variant ? ex::variant_equipment(db, *variant) : ex::stock_equipment(db, label, parts);
    const u32 entry = ee.r32(ee.r32(ee.r32(o.info + 0x18) + 8) + 4 * u32(i));
    e.human = ee.r32(entry + entry_off(0x20DC));
    e.player = ee.r32(e.human + 0x13C) != 0;
    e.grid_slot = s32(ee.r32(entry + entry_off(0x20E0)));
    e.pedal_style = s32(ee.r32(entry + entry_off(0x20E4)));
    e.spec_flag = s32(ee.r32(entry + entry_off(0x20E8)));
    e.wheel_type = s32(ee.r32(entry + entry_off(0x20EC)));
    e.asm_on = ee.r32(entry + entry_off(0x20F0)) != 0, e.tcs_on = ee.r32(entry + entry_off(0x20F4)) != 0;
    for (u32 k = 0; k < 8; ++k) e.condition[k] = ee.r32(entry + entry_off(0x2104) + 4 * k);
    e.best_lap = ee.r32(entry + 0x11D8);
    e.car = race_setup::car_addr(o.cond, i);
    return e;
}

std::vector<ex::Record> start_records(Ee& ee, const race_setup::OrigRace& o, const data::CarDatabase& db, const data::Event& ev,
                                      const std::vector<std::string>& labels, const Course& c, const ex::ProgramData& d,
                                      int config, ex::Conductor kind, const std::function<void(ex::StartRace&)>& adjust,
                                      std::vector<ex::Car::Later>* later) {
    ex::StartRace r = start_race(ee, o, db, ev, c, d, config, kind);
    if (adjust) adjust(r);
    std::vector<ex::StartEntry> entries;
    for (int i = 0; i < o.ncars; ++i) entries.push_back(start_entry(ee, o, db, labels[size_t(i)], i));
    ex::StartResult res = ex::start_cars(r, entries);
    if (later) *later = std::move(res.later);
    return res.records;
}

} // namespace gt3r::diff::start_setup
