// The original's race step set up from the user's disc; see race_setup.h.

#include "race_setup.h"

#include "formats/course/course_file.h"
#include "formats/db/gt_db.h"
#include "game/data/car_database.h"

#include <cstdio>
#include <cstring>
#include <utility>

namespace gt3r::diff::race_setup {

u32 eq_slot_for(size_t part_index) {
    switch (0x08 + 8 * part_index) {
    case 0x08: return 0x18; case 0x10: return 0x20; case 0x20: return 0x28; case 0x38: return 0x30; case 0x70: return 0x38;
    case 0x98: return 0x40; case 0xA0: return 0x48; case 0x90: return 0x50; case 0xB8: return 0x58; case 0xC0: return 0x60;
    case 0x18: return 0x68; case 0x28: return 0x70; case 0x30: return 0x78; case 0x40: return 0x80; case 0x48: return 0x88;
    case 0x50: return 0x90; case 0x58: return 0x98; case 0x60: return 0xA0; case 0x68: return 0xA8; case 0x78: return 0xB0;
    case 0x80: return 0xB8; case 0x88: return 0xC0; case 0xB0: return 0xC8; case 0xA8: return 0xD0; case 0xC8: return 0xD8;
    case 0xD0: return 0xE0;
    default: return 0;
    }
}

// Each block is self-relative; slot table offsets = file offsets of the copy.
u32 load_course(Ee& ee, ByteSpan file, std::string& err) {
    course::CourseFile cf = course::CourseFile::parse(file);
    std::vector<u8> out(0xC0, 0);
    for (int slot : {28, 31, 32, 35, 37, 38, 39, 40}) { // road, AI lines, environment, area, driver sets per driveline class
        ByteSpan s = cf.section(file, slot);
        if (s.empty()) continue;
        while (out.size() % 0x40) out.push_back(0);
        u32 off = u32(out.size());
        out.insert(out.end(), s.begin(), s.end());
        std::memcpy(out.data() + 4 * slot, &off, 4);
    }
    while (out.size() % 0x40) out.push_back(0);
    if (cf.section(file, 28).empty() || cf.section(file, 31).empty()) {
        err = "course has no road or AI-line block";
        return 0;
    }
    u32 c = ee.alloc_bytes(ByteSpan(out.data(), out.size()), 64);
    ee.call(kCourseRelocate, {Arg::ptr(c)}, 200'000'000);
    return c;
}

void set_database(Ee& ee, u32 holder, u32 gtar) {
    ee.w32(holder, gtar);
    if (program() < Program::ConceptSeoul2) return;
    // One index per database copy in memory (found again while its words are as they were left: a reset clears them).
    struct Made {
        u32 gtar, index, first, last;
    };
    static std::vector<Made> made;
    for (const Made& m : made)
        if (m.gtar == gtar && ee.r32(m.index) == m.first && ee.r32(m.index + 4 * 29) == m.last) {
            ee.w32(holder + 4, m.index);
            return;
        }
    const u32 index = ee.alloc(4 * 30);
    ee.w32(holder + 4, index);
    for (u32 t = 0; t < 30; ++t) {
        // (the table's place as the program's table accessor gives it: the header's offset, its flag bits masked)
        const u32 tab = gtar + ee.r32(gtar + 8) + (ee.r32(gtar + 0x10 + 4 * t) & ~ee.r32(gtar + 0xC));
        const u32 count = ee.r16(tab + 8), size = ee.r16(tab + 0xA);
        const u32 block = ee.alloc(0x200 + 2 * count, 4);
        for (u32 o = 0; o < 0x200 + 2 * count; o += 2) ee.w16(block + o, 0xFFFF);
        ee.w32(index + 4 * t, block);
        for (u32 i = count; i-- > 0;) {
            const u32 rec = tab + 0x10 + size * i;
            u64 key = ee.r64(rec + 8) + ee.r8(rec + 0x10);
            u32 h = 0;
            for (int b = 0; b < 8; ++b) h += u32(key >> (8 * b)) & 0xFF;
            h &= 0xFF;
            ee.w16(block + 0x200 + 2 * i, ee.r16(block + 2 * h));
            ee.w16(block + 2 * h, u16(i));
        }
    }
    std::erase_if(made, [&](const Made& m) { return m.gtar == gtar || m.index == index; });
    made.push_back({gtar, index, ee.r32(index), ee.r32(index + 4 * 29)});
}

bool build_entry(Ee& ee, u32 gtar, const data::CarDatabase& db, const std::string& label, u32 entry, std::string& err) {
    u32 eq = entry + kEntryEq;
    ee.call(kEqCtor, {Arg::ptr(eq + 0x1D0)}, 100000);
    set_database(ee, eq + 0x1D0, gtar);
    ee.call(kEqInit, {Arg::ptr(eq)}, 1000000);
    // An event's entry (table 31) as the race information builds it (0x238550): its record's parts and the part setters
    // (0x231C50), then the record's settings (0x234150).
    if (db.variant(label)) {
        const u32 rec =
            u32(ee.call(kDbRecord, {Arg::ptr(eq + 0x1D0), Arg::integer(s64(db.place_of(data::TableId::CarVariant))), Arg::integer(s64(db::id_hash(label)))}).v0.lo);
        if (!rec) {
            err = label + ": variant record not found by 0x2315D8";
            return false;
        }
        ee.call(kVariantParts, {Arg::ptr(eq), Arg::ptr(rec)}, 100000000);
        ee.call(kVariantSettings, {Arg::ptr(eq), Arg::ptr(rec)}, 1000000);
        return true;
    }
    auto r = ee.call(kEqSetCar, {Arg::ptr(eq), Arg::integer(s64(db::id_hash(label)))}, 10000000);
    if (r.v0_s32() != 1) {
        err = label + ": set car returned " + std::to_string(r.v0_s32());
        return false;
    }
    return true;
}

void set_entry_aids(Ee& ee, const OrigRace& o, bool asm_on, bool tcs_on) {
    u32 arr = ee.r32(ee.r32(o.info + 0x18) + 8);
    for (int i = 0; i < o.ncars; ++i) {
        u32 entry = ee.r32(arr + 4 * u32(i));
        ee.w32(entry + entry_off(0x20F0), asm_on ? 1 : 0);
        ee.w32(entry + entry_off(0x20F4), tcs_on ? 1 : 0);
    }
}

bool setup_original_race(Ee& ee, const data::CarDatabase& db, const Bytes& gtar_bytes, const data::Event& ev, ByteSpan course_file,
                         const std::vector<std::string>& labels, int laps, int hold, OrigRace& o, std::string& err,
                         const std::function<void(Ee&, const OrigRace&)>& before_init, const ConductorType& conductor, int config) {
    o = OrigRace{};
    o.ncars = int(labels.size());
    const int n = o.ncars;
    ee.reset();
    ee.clear_hle();
    ee.clear_probes();
    ee.hle_libc_defaults();
    ee.hle(kClassCheck, [](Ee& e) { e.set_v0(1); }, "class check");
    ee.wf(kAirDensityGlobal, 1.2f);
    try {
        // database holder (+0 = paramdb), course, race info, entries
        o.gtar = ee.alloc_bytes(gtar_bytes);
        o.holder = ee.alloc(0x40);
        set_database(ee, o.holder, o.gtar);
        u32 evrec = u32(ee.call(kDbRecord, {Arg::ptr(o.holder), Arg::integer(s64(db.place_of(data::TableId::Event))), Arg::integer(s64(db::id_hash(ev.label)))}).v0.lo);
        if (!evrec) {
            err = "event record not found by 0x2315D8";
            return false;
        }
        ee.w32(o.holder + holder_off(0x18), evrec);
        u32 crec = u32(ee.call(kDbRecord, {Arg::ptr(o.holder), Arg::integer(s64(db.place_of(data::TableId::Course))), Arg::integer(s64(db::id_hash(ev.course)))}).v0.lo);
        if (!crec) {
            err = "course record not found by 0x2315D8";
            return false;
        }
        o.container = load_course(ee, course_file, err);
        if (!o.container) return false;
        o.course = ee.alloc(0x40);
        ee.w32(o.course, o.container);
        ee.w32(o.course + 0x10, pa(0x0032DD50)); // RaceCourse vtable
        o.info = ee.alloc(0x1000); // with the race event queue at +0x84 .. +0x8A0 (0x1796C0)
        u32 list = ee.alloc(0x20), arr = ee.alloc(4 * 8), human = ee.alloc(0x200);
        ee.w32(list + 0, u32(n));
        ee.w32(list + 8, arr);
        for (int i = 0; i < n; ++i) {
            u32 entry = ee.alloc(kEntrySize, 64);
            if (!build_entry(ee, o.gtar, db, labels[size_t(i)], entry, err)) return false;
            ee.w32(entry + entry_off(0x20DC), human); // +0x13C = 0: not a human entry -> control source 2 (CPU)
            ee.w32(entry + entry_off(0x20E0), u32(i)); // grid slot
            ee.w32(arr + 4 * u32(i), entry);
        }
        ee.w32(o.info + 0x18, list);
        set_entry_aids(ee, o, true, true); // the entry constructor's ASM / TCS switches
        // The time records as the entries' constructor (0x1771B8) and the race information's (0x17A088) leave them and a run's
        // start resets them (RaceBase::vf9: the lap lists 0x17B4E8, the result words 0x17B030): no times.
        for (int i = 0; i < n; ++i) {
            const u32 entry = ee.r32(arr + 4 * u32(i));
            ee.call(pa(0x0017B498), {Arg::ptr(entry + 0x210)});
            ee.call(pa(0x0017B778), {Arg::ptr(entry + 0x11C8)}), ee.call(pa(0x0017B778), {Arg::ptr(entry + 0x11DC)});
            ee.call(pa(0x0017B6B8), {Arg::ptr(entry + 0x11F0)});
            ee.w32(entry + entry_off(0x2100), 0);
            ee.call(pa(0x0017B4E8), {Arg::ptr(entry + 0x210), Arg::integer(0)});
        }
        ee.call(pa(0x0017B708), {Arg::ptr(o.info + info_off(0x8A4))}), ee.call(pa(0x0017B708), {Arg::ptr(o.info + info_off(0x8A8))});
        ee.call(pa(0x0017B778), {Arg::ptr(o.info + info_off(0x8AC))});
        ee.call(pa(0x0017B030), {Arg::ptr(o.info + 0x40)});
        ee.w32(o.info + 0x28, o.holder);
        ee.w64(o.info + 0x30, db::id_hash(ev.course));
        ee.w32(o.info + 0x38, o.course);
        ee.w32(o.info + 0x54, 2); // start type 2: standing grid start
        ee.w32(o.info + 0x5C, u32(hold));
        // conductor
        o.cond = ee.alloc(kCondSize, 64);
        // A later build's conductor constructor leaves the collision bodies of the car slots a race does not use as the
        // memory was (GT3's clears each), and its collision step reads their in-use word: the block starts cleared.
        if (sim::concept_program(program())) ee.write_bytes(o.cond, std::vector<u8>(kCondSize, 0));
        ee.call(conductor.ctor, {Arg::ptr(o.cond)});
        if (ee.r32(o.cond + kK8 + k_off(0x63A8)) != conductor.vtable) {
            char b[80];
            std::snprintf(b, sizeof b, "conductor vtable %08X after the ctor", ee.r32(o.cond + kK8 + k_off(0x63A8)));
            err = b;
            return false;
        }
        ee.call(kRaceSetup, {Arg::ptr(o.cond), Arg::ptr(o.info)});
        ee.w8(o.cond + cond_off(0xE340) + 1, u8(laps)); // race config +1 = laps
        ee.w32(o.cond + kK8 + k_off(0x6384), u32(n));
        if (before_init) before_init(ee, o);
        // virtual 23 through the vtable (the licence conductor's runs the base one first)
        const u32 vt = ee.r32(o.cond + kK8 + k_off(0x63A8)), e23 = vt + 8 * u32(conductor_slot(23));
        ee.call(ee.r32(e23 + 4), {Arg::ptr(o.cond + u32(s32(s16(ee.r16(e23))))), Arg::integer(config)}, 2'000'000'000);
    } catch (const EeTrap& t) {
        char b[200];
        std::snprintf(b, sizeof b, "setup trap at pc=%08X: %s", t.pc, t.what());
        err = b;
        return false;
    }
    return true;
}

} // namespace gt3r::diff::race_setup
