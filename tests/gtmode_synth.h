#pragma once

// A small invented GT Mode world for tests that run without a disc: the car database
// (database/paramdb_us.db: a GTAR of 36 GTDT tables, the STDB string tables and the
// IDDB label index), gameconf/product.gcf, database/carcolor.db / .sdb and the menu sets
// menu::flow::GameFlow and game/gt_mode_menu/ look for (gt_mode, arcade, option,
// memorycard, logger, setting, records and the presentation sets). Every car, maker,
// course, event, price, time, colour and layout here is made up; only the file formats
// follow docs/formats/ (database.md, menu.md) and the gcf / carcolor layouts in
// src/game/gcf.h and src/race/paint.cpp. Nothing is read from or copied from a disc.
//
// The world: three cars ("Synthetic Coupe" 12,000 Cr., "Synthetic Wagon" 30,000 Cr. and
// the prize-only "Synthetic Racer"), one muffler upgrade, a two-race beginner series
// with a series prize car, a two-round amateur championship, a locked pro series, the
// eight B licence tests (+ one A test), two attract demos, three arcade courses / two
// arcade cars, and a new game of 25,000 Cr.

#include "menu_flow_synth.h"

#include "formats/db/gt_db.h"
#include "menu/flow/menu_set.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gt3r::test::gtsynth {

using namespace gt3r::test::menusynth;

// --- little-endian writers -------------------------------------------------------------

inline void put_bytes(Bytes& b, size_t o, const void* p, size_t n) {
    if (b.size() < o + n) b.resize(o + n, 0);
    std::memcpy(b.data() + o, p, n);
}
inline void p8(Bytes& b, size_t o, u8 v) { put_bytes(b, o, &v, 1); }
inline void p16(Bytes& b, size_t o, u16 v) { put_bytes(b, o, &v, 2); }
inline void p32(Bytes& b, size_t o, u32 v) { put_bytes(b, o, &v, 4); }
inline void p64(Bytes& b, size_t o, u64 v) { put_bytes(b, o, &v, 8); }
inline void pad4(Bytes& b) { b.resize((b.size() + 3) & ~size_t(3), 0); }
// Appends a NUL-terminated string; returns its offset.
inline size_t append_cstr(Bytes& b, const std::string& s) {
    size_t o = b.size();
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
    return o;
}

// STDB: u32 count, u16 flags, u16 pad, u32 total size, u32 offsets[count]; u16 length + bytes.
inline Bytes write_stdb(const std::vector<std::string>& strings) {
    Bytes b(16 + 4 * strings.size(), 0);
    std::memcpy(b.data(), "STDB", 4);
    p32(b, 4, u32(strings.size()));
    p32(b, 8, 1);
    for (size_t i = 0; i < strings.size(); ++i) {
        p32(b, 16 + 4 * i, u32(b.size()));
        size_t o = b.size();
        p16(b, o, u16(strings[i].size()));
        b.insert(b.end(), strings[i].begin(), strings[i].end());
        b.push_back(0);
        if (b.size() & 1) b.push_back(0);
    }
    p32(b, 12, u32(b.size()));
    return b;
}

// --- the car database ---------------------------------------------------------------------

class DbWriter {
public:
    DbWriter() {
        sizes_.fill(0x20);
        sizes_[3] = 0x20;    // chassis
        sizes_[5] = 0x48;    // racing modify (body)
        sizes_[6] = 0x58;    // engine
        sizes_[12] = 0x24;   // turbine kit
        sizes_[13] = 0x24;   // drivetrain
        sizes_[17] = 0x2C;   // gear
        sizes_[18] = 0x80;   // suspension
        sizes_[28] = sizes_[29] = 0x60; // tires
        sizes_[30] = 0x108;  // car
        sizes_[31] = sizes_[35] = 0xF0; // car / arcade variants
        sizes_[32] = 0x1F8;  // event
        sizes_[33] = 0x28;   // regulation
        sizes_[34] = 0x10;   // course
        uni_.push_back("");
        str_.push_back("-");
        id("-");
    }

    u64 id(const std::string& label) {
        labels_.insert(label);
        return db::id_hash(label);
    }
    u16 uni(const std::string& s) {
        for (size_t i = 0; i < uni_.size(); ++i)
            if (uni_[i] == s) return u16(i);
        uni_.push_back(s);
        return u16(uni_.size() - 1);
    }
    // A new zeroed record of table t whose first u64 is the id of `label`.
    Bytes& record(int t, const std::string& label) {
        tables_[size_t(t)].emplace_back(sizes_[size_t(t)], 0);
        Bytes& r = tables_[size_t(t)].back();
        p64(r, 0, id(label));
        return r;
    }
    // A purchasable part record: car at +8, stage at +0x10, price in the last four bytes.
    Bytes& part(int t, const std::string& label, const std::string& car, u8 stage, u32 price) {
        Bytes& r = record(t, label);
        p64(r, 8, id(car));
        p8(r, 0x10, stage);
        p32(r, r.size() - 4, price);
        return r;
    }

    // database/paramdb_us.db: GTAR of the 36 tables.
    Bytes paramdb() const {
        const u32 n = 36, data = 176;
        Bytes ar(data, 0);
        std::memcpy(ar.data(), "GTAR", 4);
        p32(ar, 4, n);
        p32(ar, 8, data);
        p32(ar, 12, 7);
        u32 at = 0;
        for (u32 t = 0; t < n; ++t) {
            p32(ar, 16 + 4 * t, at);
            Bytes g(16, 0);
            std::memcpy(g.data(), "GTDT", 4);
            p16(g, 4, 0x0827);
            p16(g, 6, u16(t));
            p16(g, 8, u16(tables_[t].size()));
            p16(g, 10, sizes_[t]);
            for (const Bytes& r : tables_[t]) g.insert(g.end(), r.begin(), r.end());
            p32(g, 12, u32(g.size()));
            ar.insert(ar.end(), g.begin(), g.end());
            at += u32(g.size());
        }
        p32(ar, 16 + 4 * n, at);
        return ar;
    }
    Bytes paramstr() const { return write_stdb(str_); }
    Bytes paramunistr() const { return write_stdb(uni_); }
    // .id_db_idx_us.db (IDDB: code -> string index, sorted by code) and .id_db_str_us.db.
    Bytes id_index() const {
        std::vector<std::pair<u64, u64>> v;
        u64 i = 0;
        for (const std::string& l : labels_) v.push_back({db::id_hash(l), i++});
        std::sort(v.begin(), v.end());
        Bytes b(8, 0);
        std::memcpy(b.data(), "IDDB", 4);
        p32(b, 4, u32(v.size()));
        for (const auto& [code, index] : v) {
            p64(b, b.size(), code);
            p64(b, b.size(), index);
        }
        return b;
    }
    Bytes id_strings() const { return write_stdb(std::vector<std::string>(labels_.begin(), labels_.end())); }

private:
    std::array<u16, 36> sizes_{};
    std::array<std::vector<Bytes>, 36> tables_;
    std::set<std::string> labels_;
    std::vector<std::string> uni_, str_;
};

// --- product.gcf --------------------------------------------------------------------------

// Sections: u32 count, u32 8, u32 entry offsets (relative to the section); each entry
// starts with the offset of its name (relative to the entry).
class GcfWriter {
public:
    // A raw (non entry-list) section, e.g. tag 0.
    void raw(u32 tag, Bytes body) { secs_.push_back({tag, std::move(body)}); }
    void section(u32 tag, std::vector<Bytes> entries) {
        Bytes s(8 + 4 * entries.size(), 0);
        p32(s, 0, u32(entries.size()));
        p32(s, 4, 8);
        for (size_t i = 0; i < entries.size(); ++i) {
            pad4(s);
            p32(s, 8 + 4 * i, u32(s.size()));
            s.insert(s.end(), entries[i].begin(), entries[i].end());
        }
        pad4(s);
        secs_.push_back({tag, std::move(s)});
    }
    Bytes write() const {
        Bytes b(8 + 8 * secs_.size(), 0);
        p32(b, 0, u32(secs_.size()));
        p32(b, 4, 8);
        for (size_t i = 0; i < secs_.size(); ++i) {
            pad4(b);
            p32(b, 8 + 8 * i, secs_[i].first);
            p32(b, 12 + 8 * i, u32(b.size()));
            b.insert(b.end(), secs_[i].second.begin(), secs_[i].second.end());
        }
        return b;
    }

    // Entry: name offset, then `words`, then the name.
    static Bytes entry(const std::string& name, const std::vector<u32>& words) {
        Bytes e(4 + 4 * words.size(), 0);
        for (size_t i = 0; i < words.size(); ++i) p32(e, 4 + 4 * i, words[i]);
        p32(e, 0, u32(append_cstr(e, name)));
        pad4(e);
        return e;
    }
    // Group entry {name, count, list}: the list holds the sub-entries' offsets (relative to
    // the entry); each sub-entry is 16 bytes written by `fill(sub, index, strings)`, where
    // `strings(s)` returns the offset of a string relative to the sub-entry.
    template <class Fill>
    static Bytes group(const std::string& name, size_t n, Fill fill) {
        Bytes e(12, 0);
        p32(e, 4, u32(n));
        p32(e, 0, u32(append_cstr(e, name)));
        pad4(e);
        const size_t list = e.size();
        p32(e, 8, u32(list));
        e.resize(list + 4 * n, 0);
        std::vector<size_t> subs;
        for (size_t i = 0; i < n; ++i) {
            subs.push_back(e.size());
            p32(e, list + 4 * i, u32(e.size()));
            e.resize(e.size() + 16, 0);
        }
        for (size_t i = 0; i < n; ++i) {
            const size_t sub = subs[i];
            auto strings = [&](const std::string& s) { return u32(append_cstr(e, s) - sub); };
            fill(e, sub, i, strings);
        }
        pad4(e);
        return e;
    }

private:
    std::vector<std::pair<u32, Bytes>> secs_;
};

// --- menus --------------------------------------------------------------------------------

// One page: its flow (.mbl) and its layout (.pmb), built side by side so that every flow
// item has its page item.
struct MenuPage {
    SynFlowPage flow;
    SynPage layout;
    float next_y = 400;

    SynLayer& layer(const std::string& name) {
        for (SynLayer& l : layout.layers)
            if (l.name == name) return l;
        layout.layers.push_back(SynLayer{name, {}});
        return layout.layers.back();
    }
    // An item (flow + page button). kind: 0 go to link, 1 popup, 2 finish, 3 back,
    // 0x14 horizontal multi. x < 0: stacked down the left side.
    MenuPage& item(const std::string& name, const std::string& action = {}, const std::string& link = {},
                   u16 kind = 0, float x = -1, float y = -1, float w = 120, float h = 20) {
        SynItem it;
        it.name = name;
        it.action = action;
        it.link = link;
        it.kind = kind;
        flow.items.push_back(it);
        SynElement e;
        e.name = name;
        e.x = x < 0 ? 40 : x;
        e.y = y < 0 ? next_y : y;
        e.w = w;
        e.h = h;
        e.z = 5;
        e.text = name;
        e.has_alt = true;
        if (x < 0) next_y -= 24;
        layout.items.push_back(e);
        return *this;
    }
    // The flow item's focus layer (str80) of the last item.
    MenuPage& focus_layer(const std::string& l) {
        flow.items.back().str80 = l;
        layer(l);
        return *this;
    }
    // A child of the last (multi) item and its page item.
    MenuPage& child(const std::string& name, const std::string& action, u16 kind, const std::string& text) {
        SynItem c;
        c.name = name;
        c.action = action;
        c.kind = kind;
        flow.items.back().children.push_back(c);
        SynElement e;
        e.name = name;
        e.x = 200 + 30 * float(flow.items.back().children.size());
        e.y = layout.items.back().y;
        e.w = 24;
        e.h = 20;
        e.z = 6;
        e.text = text;
        e.has_alt = true;
        layout.items.push_back(e);
        return *this;
    }
    // A code-bound widget (layer widget with a class) carrying a text.
    MenuPage& widget(const std::string& name, const std::string& cls, const std::string& text = "-", float x = 300,
                     float y = 200, float w = 120, float h = 20, const std::string& in = "defaultLayer") {
        SynElement e;
        e.name = name;
        e.widget_class = cls;
        e.widget = true;
        e.text = text;
        e.x = x, e.y = y, e.w = w, e.h = h, e.z = 10;
        layer(in).elements.push_back(e);
        return *this;
    }
    // A plain element of a layer ("hidden" art, texts).
    MenuPage& el(const std::string& in, const std::string& name, const std::string& text = {}, float x = 0,
                 float y = 0, float w = 40, float h = 20, bool image = false, u16 align = 4) {
        SynElement e;
        e.name = name;
        e.text = text;
        e.x = x, e.y = y, e.w = w, e.h = h, e.z = 3;
        e.image = image;
        e.align = align;
        layer(in).elements.push_back(e);
        return *this;
    }
    MenuPage& hidden(const std::string& name, const std::string& text = {}, float x = 0, float y = 0, float w = 40,
                     float h = 20) {
        return el("hidden", name, text, x, y, w, h);
    }
};

// A menu set: menu/<name>.mbl and menu/US/<name>1.pmb.
class MenuWriter {
public:
    explicit MenuWriter(std::string name) : name_(std::move(name)) {}
    // flags: page_flag bits (0x100 = cancel goes back).
    MenuPage& page(const std::string& name, u16 flags = 0x100, const std::string& back = {},
                   const std::string& back_action = {}, const std::string& import = {}) {
        auto p = std::make_unique<MenuPage>();
        p->flow.name = name;
        p->flow.pmb = 1;
        p->flow.flags = flags;
        p->flow.back = back;
        p->flow.back_action = back_action;
        p->flow.import = import;
        p->layout.name = name;
        p->layer("defaultLayer");
        pages_.push_back(std::move(p));
        return *pages_.back();
    }
    void write(std::map<std::string, Bytes>& files) const {
        std::vector<const MenuPage*> sorted;
        for (const auto& p : pages_) sorted.push_back(p.get());
        std::sort(sorted.begin(), sorted.end(),
                  [](const MenuPage* a, const MenuPage* b) { return a->flow.name < b->flow.name; });
        std::vector<SynFlowPage> flows;
        std::vector<SynPage> layouts;
        for (const MenuPage* p : sorted) {
            flows.push_back(p->flow);
            layouts.push_back(p->layout);
        }
        files["menu/" + name_ + ".mbl"] = write_mbl(flows);
        files["menu/US/" + name_ + "1.pmb"] = write_pmb(layouts);
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<MenuPage>> pages_;
};

// --- the world ----------------------------------------------------------------------------

// Labels and figures the tests check against.
namespace world {
inline const char* const kCoupe = "syn_coupe_01";  // ACME MOTORS, 12,000 Cr., 2 paints, the muffler upgrade
inline const char* const kWagon = "syn_wagon_02";  // ACME MOTORS, 30,000 Cr., 3 paints, FF
inline const char* const kRacer = "syn_racer_03";  // BETA WORKS, prize only (not for sale / buy)
constexpr s64 kStartCredits = 25000;
// Two wheels of the wheel shop (gtmode_synth_garage_wheel_list); the database has no wheel table.
inline const char* const kRimA = "syn_rim_a";
inline const char* const kRimB = "syn_rim_b";
constexpr s64 kMufflerPrice = 1500;                  // muf_sports (goods 37) for the coupe
constexpr int kTitleIdleSeconds = 2;
// The oil gauge's colourings (r in the low byte): clean green, dirty brown.
constexpr u32 kOilClean = 0xC000C000u, kOilDirty = 0xC0204080u;
} // namespace world

// The regional twins (gtmode_files(true)): two cars of the coupe's model offered by language
// (region byte +0xF7: 2 = not in British English, 1 = only in British English) with names per
// language (+0xE4 / +0xF2 / +0x100), their licence variants, and licence test S-1 driving the
// first, the second in slot 1 (colours 0x101 / 0x102).
namespace world {
inline const char* const kTwinOpel = "opel_speedster_00";
inline const char* const kTwinVauxhall = "vauxhall_vx220_00";
} // namespace world

inline void add_database(std::map<std::string, Bytes>& files, bool twins = false) {
    DbWriter d;
    enum { Body = 5, Drive = 13, Gear = 17, Susp = 18, Muffler = 20, Car = 30, Variant = 31, Event = 32,
           Regulation = 33, Course = 34, Arcade = 35 };
    // Cars: default parts (slot -> record), catalogue data.
    auto car = [&](const std::string& label, const std::string& name, const std::string& alt, const std::string& maker,
                   u32 price, u8 flags, u8 category, const std::string& model, u8 drive) -> Bytes& {
        const std::string body = "rm_" + label, dt = "dt_" + label, su = "su_" + label, mu = "mu_" + label + "_0";
        Bytes& b = d.part(Body, body, label, 0, 0);
        p64(b, 0x18, d.id(model));
        p16(b, 0x30, 1000);
        p16(b, 0x32, 1510);
        p16(b, 0x34, 1490);
        p16(b, 0x36, 1720);
        p8(b, 0x38, 100);
        Bytes& t = d.part(Drive, dt, label, 0, 0);
        p8(t, 0x14, drive);
        p8(t, 0x15, 0xFE);
        Bytes& s = d.part(Susp, su, label, 0, 0);
        // Spring rate sliders 3..9 (front / rear), stock 5; nothing else adjustable.
        p8(s, 0x21, 3), p8(s, 0x22, 9), p8(s, 0x23, 5);
        p8(s, 0x24, 3), p8(s, 0x25, 9), p8(s, 0x26, 5);
        p8(s, 0x27, 1), p8(s, 0x28, 1);
        d.part(Muffler, mu, label, 0, 0);
        Bytes& c = d.record(Car, label);
        p64(c, 8 + 5 * 8, d.id(body));
        p64(c, 8 + 13 * 8, d.id(dt));
        p64(c, 8 + 19 * 8, d.id(su));
        p64(c, 8 + 21 * 8, d.id(mu));
        p16(c, 0xE4, d.uni(name));
        p16(c, 0xE6, d.uni(alt));
        p16(c, 0xEA, 2026);
        p32(c, 0xEC, price);
        p8(c, 0xF0, flags);
        p8(c, 0xF1, category);
        p16(c, 0x104, d.uni(maker));
        return c;
    };
    car(world::kCoupe, "Synthetic Coupe", "", "ACME MOTORS", 12000, 0, 0, "zz0001", 0);
    car(world::kWagon, "Synthetic Wagon", "Touring", "ACME MOTORS", 30000, 0, 0, "zz0002", 1);
    if (twins) {
        // Names per language (+0xF2 / +0xF4 Japanese, +0x100 / +0x102 British English).
        auto names = [&](Bytes& c, const char* jp, const char* jp2, const char* uk, const char* uk2, u8 region) {
            const u16 a = d.uni(jp), b = d.uni(jp2), e = d.uni(uk), f = d.uni(uk2);
            p16(c, 0xF2, a), p16(c, 0xF4, b), p16(c, 0x100, e), p16(c, 0x102, f);
            p8(c, 0xF7, region);
        };
        names(car(world::kTwinOpel, "Speedster", "Turbo", "OPEL", 20000, 0, 0, "zz0001", 3),
              "\xA5\xB9\xA5\xD4", "", "Speedster (UK)", "Turbo (UK)", 2);
        names(car(world::kTwinVauxhall, "VX220", "", "VAUXHALL", 20000, 0, 0, "zz0001", 3), "VX220", "", "VX220",
              "", 1);
    }
    car(world::kRacer, "Synthetic Racer", "", "BETA WORKS", 90000, 3, 1, "zz0003", 3);
    // The coupe's sports muffler (goods 37 = muffler stage 1).
    d.part(Muffler, "mu_syn_coupe_01_1", world::kCoupe, 1, u32(world::kMufflerPrice));

    // Courses: label, surface (+8), name (+12).
    auto course = [&](const std::string& label, u8 surface, const std::string& name) {
        Bytes& c = d.record(Course, label);
        p8(c, 8, surface);
        p16(c, 12, d.uni(name));
    };
    course("syn_ring", 0, "Synthetic Ring");
    course("syn_dirt", 2, "Synthetic Dirt Trail");
    course("syn_hill", 0, "Synthetic Hill");
    course("syn_pad", 0, "Synthetic Test Pad");

    // Opponent / licence cars (base car at +8, parts default to the base car's).
    for (auto [v, base] : {std::pair<const char*, const char*>{"syv_coupe_a", world::kCoupe},
                           {"syv_wagon_b", world::kWagon},
                           {"syv_lic_b", world::kCoupe}})
        p64(d.record(Variant, v), 8, d.id(base));
    for (auto [v, base] : {std::pair<const char*, const char*>{"syn_coupe_01_250", world::kCoupe},
                           {"syn_wagon_02_180", world::kWagon}})
        p64(d.record(Arcade, v), 8, d.id(base));

    Bytes& reg = d.record(Regulation, "reg_syn_acme");
    p64(reg, 8, d.id(world::kCoupe));
    p64(reg, 16, d.id(world::kWagon));

    // Events (table 32): course +8, opponent pool in 32 slots from +0x10 (unused: "-"), regulation
    // +0x190, launch speed +0x198, laps +0x199, licence kind +0x19A, required licence
    // +0x19B, cars +0x1B5, gold / silver / bronze {min, sec, u16 ms} +0x1C4.., special
    // +0x1D2, trophy class +0x1D3, prizes (x100 Cr.) +0x1D4, championship bonus +0x1E2,
    // league +0x1E5.
    struct Ev {
        std::string code, course, reg = "-";
        std::vector<std::string> pool;
        u8 cars = 6, trophy = 0, league = 0, license_kind = 0, launch = 0;
        std::array<u16, 6> prize{};
        u16 bonus = 0;
        std::array<std::array<u8, 3>, 3> times{}; // {min, sec, centi -> ms x10}
    };
    auto event = [&](const Ev& e) -> Bytes& {
        Bytes& r = d.record(Event, e.code);
        p64(r, 8, d.id(e.course));
        for (size_t k = 0; k < 32; ++k) p64(r, 0x10 + 8 * k, d.id(k < e.pool.size() ? e.pool[k] : std::string("-")));
        p64(r, 0x190, d.id(e.reg));
        p8(r, 0x198, e.launch);
        p8(r, 0x199, 2);
        p8(r, 0x19A, e.license_kind);
        p8(r, 0x1B5, e.cars);
        for (size_t m = 0; m < 3; ++m) {
            p8(r, 0x1C4 + 4 * m, e.times[m][0]);
            p8(r, 0x1C5 + 4 * m, e.times[m][1]);
            p16(r, 0x1C6 + 4 * m, u16(e.times[m][2] * 10));
        }
        p8(r, 0x1D3, e.trophy);
        for (size_t p = 0; p < 6; ++p) p16(r, 0x1D4 + 2 * p, e.prize[p]);
        p16(r, 0x1E2, e.bonus);
        p8(r, 0x1E5, e.league);
        return r;
    };
    Ev g1;
    g1.code = "Gsyn_e_0001", g1.course = "syn_ring", g1.pool = {"syv_coupe_a", "syv_wagon_b"};
    g1.trophy = 5, g1.league = 1, g1.prize = {15, 9, 6, 4, 3, 2};
    event(g1);
    Ev g2 = g1;
    g2.code = "Gsyn_e_0002", g2.reg = "reg_syn_acme", g2.prize = {20, 12, 8, 5, 4, 3};
    event(g2);
    for (const char* round : {"Gsynch_n_0501", "Gsynch_n_0502"}) {
        Ev c = g1;
        c.code = round, c.trophy = 6, c.league = 2, c.prize = {30, 18, 12, 8, 6, 4}, c.bonus = 50;
        event(c);
    }
    for (int t = 1; t <= 8; ++t) {
        Ev l;
        l.code = "L0B000" + std::to_string(t), l.course = "syn_pad", l.pool = {"syv_lic_b"};
        l.cars = 1, l.license_kind = 1, l.launch = 50;
        l.times = {{{0, 30, 0}, {0, 32, 50}, {0, 35, 0}}}; // 0'30.000, 0'32.500, 0'35.000
        event(l);
        if (t == 1) {
            l.code = "L0A0001";
            event(l);
        }
    }
    if (twins) {
        // Licence S-1: the Opel in slot 0, the Vauxhall in slot 1; S-2: the Opel alone, a slot-1
        // colour all the same. Slot colours +0x110 (0xFFFFFFFF = none).
        for (auto [v, base] : {std::pair<const char*, const char*>{"s1_opel", world::kTwinOpel},
                               {"s1_vauxhall", world::kTwinVauxhall}})
            p64(d.record(Variant, v), 8, d.id(base));
        Ev s;
        s.code = "L0S0001", s.course = "syn_pad", s.pool = {"s1_opel", "s1_vauxhall"};
        s.cars = 1, s.license_kind = 1, s.launch = 50;
        s.times = {{{0, 30, 0}, {0, 32, 50}, {0, 35, 0}}};
        Bytes& s1 = event(s);
        for (size_t k = 0; k < 32; ++k) p32(s1, 0x110 + 4 * k, k == 0 ? 0x101u : k == 1 ? 0x102u : 0xFFFFFFFFu);
        s.code = "L0S0002", s.pool = {"s1_opel"};
        Bytes& s2 = event(s);
        for (size_t k = 0; k < 32; ++k) p32(s2, 0x110 + 4 * k, k == 0 ? 0x101u : k == 1 ? 0x102u : 0xFFFFFFFFu);
    }

    files["database/paramdb_us.db"] = d.paramdb();
    files["database/paramstr_us.db"] = d.paramstr();
    files["database/paramunistr_us.db"] = d.paramunistr();
    files["database/.id_db_idx_us.db"] = d.id_index();
    files["database/.id_db_str_us.db"] = d.id_strings();
}

// carcolor.db: u32 model count +8, id table +0xC, colour definitions +0x10; models at
// +0x18 {u64 model id, u32 count, u32 offset into the id table}; definitions {u32 count,
// {u32 id, u32 name, u32 second name, u32 rgb}}. carcolor.sdb: the names.
inline void add_colors(std::map<std::string, Bytes>& files) {
    struct Paint {
        u32 id;
        const char* name;
        u32 rgb;                     // bytes R, G, B
        const char* second = nullptr; // the second name (Japanese), else the same
    };
    const std::vector<std::pair<std::string, std::vector<Paint>>> models = {
        {"zz0001", {{0x101, "Sky Mist", 0x00C0A080}, {0x102, "Ember Red", 0x002020C0}}},
        {"zz0002",
         {{0x201, "Moss Green", 0x00208040},
          {0x202, "Night Blue", 0x00602010},
          {0x203, "Chalk White", 0x00F0F0F0, "\xA5\xDB\xA5\xEF\xA5\xA4\xA5\xC8"}}},
        {"zz0003", {{0x301, "Racing Orange", 0x000080FF}}},
    };
    Bytes b(0x18 + 16 * models.size(), 0);
    p32(b, 8, u32(models.size()));
    const size_t ids = b.size();
    p32(b, 0xC, u32(ids));
    std::vector<std::string> names;
    std::vector<Paint> defs;
    for (size_t m = 0; m < models.size(); ++m) {
        p64(b, 0x18 + 16 * m, db::id_hash(models[m].first));
        p32(b, 0x18 + 16 * m + 8, u32(models[m].second.size()));
        p32(b, 0x18 + 16 * m + 12, u32(b.size() - ids));
        for (const Paint& p : models[m].second) {
            p32(b, b.size(), p.id);
            defs.push_back(p);
        }
    }
    p32(b, 0x10, u32(b.size()));
    p32(b, b.size(), u32(defs.size()));
    for (const Paint& p : defs) {
        size_t o = b.size();
        p32(b, o, p.id);
        p32(b, o + 4, u32(names.size()));
        names.push_back(p.name);
        if (p.second) names.push_back(p.second);
        p32(b, o + 8, u32(names.size() - 1));
        p32(b, o + 12, p.rgb);
    }
    files["database/carcolor.db"] = b;
    files["database/carcolor.sdb"] = write_stdb(names);
}

inline void add_gcf(std::map<std::string, Bytes>& files) {
    GcfWriter g;
    Bytes idle(8, 0);
    p32(idle, 0, world::kTitleIdleSeconds);
    g.raw(0, idle);
    // Race codes (every GT-mode result slot) and their unlock level (1 = the S licence).
    g.section(4, {GcfWriter::entry("Gsyn_e_0001", {0}), GcfWriter::entry("Gsyn_e_0002", {0}),
                  GcfWriter::entry("Gsynch_n_0500", {0}), GcfWriter::entry("Gsynch_n_0501", {0}),
                  GcfWriter::entry("Gsynch_n_0502", {0}),
                  GcfWriter::entry("Gsynf_s_0001", {1})});
    // Arcade course lists: {course, -, s16 level +6, u32 length +8}.
    auto courses = [](const std::string& list, const std::vector<std::pair<std::string, int>>& cs) {
        return GcfWriter::group(list, cs.size(), [&](Bytes& e, size_t sub, size_t i, auto strings) {
            p32(e, sub, strings(cs[i].first));
            p16(e, sub + 6, u16(s16(cs[i].second)));
            p32(e, sub + 8, cs[i].first == "syn_dirt" ? 1500u : 2000u);
        });
    };
    g.section(8, {courses("SINGLE_RACE", {{"syn_ring", 0}, {"syn_dirt", 0}, {"syn_hill", 1}}),
                  courses("TIME_ATTACK", {{"syn_ring", 0}}),
                  courses("TWO_PLAYER", {{"syn_ring", 0}, {"syn_hill", 1}})});
    g.section(10, {GcfWriter::entry("car_wash", {60}), GcfWriter::entry("initial", {u32(world::kStartCredits)}),
                   GcfWriter::entry("oil_change", {300}), GcfWriter::entry("wheel", {700})});
    // Demo races: {event, course, car (0 = none), -, level}.
    auto demo = [](const std::string& ev, const std::string& course) {
        Bytes e(20, 0);
        p32(e, 0, u32(append_cstr(e, ev)));
        p32(e, 4, u32(append_cstr(e, course)));
        pad4(e);
        return e;
    };
    g.section(12, {demo("demo_syn_1", "syn_ring"), demo("demo_syn_2", "syn_hill")});
    // Arcade classes: {label, race code, u16 enabled +8, s16 level +0xA, s32 variant +0xC}.
    auto cls = [](const std::string& tag, const std::vector<std::string>& cars) {
        return GcfWriter::group(tag, cars.size(), [&](Bytes& e, size_t sub, size_t i, auto strings) {
            p32(e, sub, strings(cars[i]));
            p32(e, sub + 4, strings(""));
            p16(e, sub + 8, 1);
        });
    };
    g.section(14, {cls("CLASS_B", {"syn_wagon_02_180"}), cls("CLASS_C", {"syn_coupe_01_250", "syn_wagon_02_180"})});
    // Prize sets: {s32 id, u32 car, s32 colour id, s32 weight}.
    auto prize = [](const std::string& set, const std::string& car, s32 color) {
        return GcfWriter::group(set, 1, [&](Bytes& e, size_t sub, size_t, auto strings) {
            p32(e, sub, 7);
            p32(e, sub + 4, strings(car));
            p32(e, sub + 8, u32(color));
            p32(e, sub + 12, 1);
        });
    };
    // (The whole game's set, every GT-mode result won: the coupe in a random paint.)
    g.section(15, {prize("Gsyn_e_0000", world::kRacer, 0x301), prize("Gsynch_n_0500", world::kWagon, 0x203),
                   prize("L0B0000", world::kWagon, 0x202), prize("gtmode_complete", world::kCoupe, -1)});
    files["gameconf/product.gcf"] = g.write();
}

// The GT-mode menus (menu/gt_mode.mbl): title, map, dealers, garage, races, tune shop,
// licences, machine test, status and the popups their checks open.
inline void add_gt_mode_menus(std::map<std::string, Bytes>& files) {
    MenuWriter m("gt_mode");
    const u16 kCancel = 0x100;
    // Title: items in the original's order (arcade 0, GT mode 1, replay theater 2, option 6).
    m.page("root", 0)
        .item("arcade", "go_arcade_menu", {}, 2)
        .item("gt_mode", "go_sitemap", {}, 2)
        .item("replay", {}, "replay_theater")
        .item("load", "go_load_game_menu", {}, 2)
        .item("save", "go_save_game_menu", {}, 2)
        .item("file", "go_file_operation", {}, 2)
        .item("option", "go_option", {}, 2);
    m.page("replay_theater", kCancel, "root", "set_focus_replay_theater")
        .item("theater", "go_replay_theater", {}, 2)
        .item("demo", "go_demonstration", {}, 2)
        .item("logger", "go_logger", {}, 2);
    // The map: buildings on a grid, the status bar imported; Cancel ends GT mode (go_top).
    m.page("map", kCancel, {}, "go_top", "00cmn/status")
        .item("cd_cd", {}, "cd/cd", 0, 100, 300, 60, 40)
        .item("hm_hm", {}, "hm/hm", 0, 300, 300, 60, 40)
        .item("r_r", {}, "r/r", 0, 500, 300, 60, 40)
        .item("ts_ts", {}, "ts/ts", 0, 100, 150, 60, 40)
        .item("lc_lc", {}, "lc/lc", 0, 300, 150, 60, 40)
        .item("mt_mt", {}, "mt/mt", 0, 500, 150, 60, 40)
        .item("st_st", {}, "status", 0, 300, 40, 60, 40)
        .item("at_at", {}, "at/at", 0, 500, 40, 60, 40)
        .hidden("pointer", {}, 0, 0, 16, 16);
    // Car wash / oil change: a page each whose button opens a progress popup that takes the
    // controls away (its "ignore_control" widget) until the wash / the oil change is over; the
    // car wash's 3D car; the oil gauge and its hidden clean and dirty colourings.
    m.page("at/at", kCancel, "map", {}, "00cmn/status")
        .item("cw", {}, "at/cw/cw")
        .item("ot", {}, "at/ot/ot")
        .item("ws", {}, "at/ws/ws");
    // The wheel shop: one brand page of two wheels (tiles named after them), the purchase and
    // fit questions.
    m.page("at/ws/ws", kCancel, "at/at", {}, "00cmn/status").item("syn", {}, "at/ws/ws_syn");
    m.page("at/ws/ws_syn", kCancel | 0x001, "at/ws/ws", {}, "00cmn/status")
        .item(std::string(world::kRimA) + "_link", "set_focus_wheel_code", "00cmn/ws_buy_sure", 1, 660, 404, 52, 52)
        .item(std::string(world::kRimB) + "_link", "set_focus_wheel_code", "00cmn/ws_buy_sure", 1, 660, 404, 52, 52)
        .widget("menu_wheel_shop", "", "", 20, 124, 600, 52)
        .widget("car_render_wheel_shop", "car_render_wheel_shop", "-", 0, 0, 640, 448)
        .hidden(world::kRimA, {}, 294, 486, 52, 52)
        .hidden(world::kRimB, {}, 294, 486, 52, 52)
        .hidden("sold_out", {}, 294, 486, 52, 52);
    m.page("00cmn/ws_buy_sure", kCancel).item("yes1", "buy_wheel", "00cmn/ws_fit_sure", 1).item("no1", {}, {}, 3);
    m.page("00cmn/ws_fit_sure", kCancel).item("yes1", "fit_wheel", {}, 3).item("no1", {}, {}, 3);
    m.page("at/cw/cw", kCancel, "at/at", {}, "00cmn/status")
        .item("wash", "wash_car", "at/cw/cw_now", 1, 500, 300, 60, 60)
        .widget("car_render_car_wash", "car_render_car_wash", "-", 0, 0, 640, 448);
    m.page("at/cw/cw_now", 0, "at/cw/cw")
        .item("end", {}, "at/at", 0, 240, 300, 160, 30)
        .el("defaultLayer", "base1", "Washing", 240, 300, 160, 30)
        .widget("ignore_control", "ignore_control", "", 280, 200, 8, 8);
    MenuPage& at = m.page("at/ot/ot", kCancel, "at/at", {}, "00cmn/status")
                       .item("oil", "oil_change", "at/ot/ot_now", 1, 500, 300, 60, 60)
                       .widget("oil_cylinder", "oil_change_cylinder", "-", 500, 100, 20, 100)
                       .el("defaultLayer", "glass", {}, 500, 100, 20, 100)
                       .hidden("clean", {}, 500, 100, 20, 100)
                       .hidden("dirty", {}, 500, 100, 20, 100);
    at.layer("defaultLayer").elements.back().z = 12; // the cylinder's glass, over the oil
    at.layer("hidden").elements[0].rgba = world::kOilClean;
    at.layer("hidden").elements[1].rgba = world::kOilDirty;
    m.page("at/ot/ot_now", 0, "at/ot/ot")
        .item("end", {}, "at/at", 0, 240, 300, 160, 30)
        .el("defaultLayer", "base1", "Changing oil", 240, 300, 160, 30)
        .widget("ignore_control", "ignore_control", "", 280, 200, 8, 8);
    m.page("00cmn/status", 0)
        .widget("user_money", "user_money", "0", 500, 420, 100, 20)
        .widget("user_day_unit", "user_day_unit", "day %d", 560, 400, 24, 16)
        .widget("user_carname", "user_carname", "-", 40, 420, 200, 20);
    // Dealers: region -> maker -> car page.
    m.page("cd/cd", kCancel, "map").item("jp", {}, "cd/cd_jp");
    m.page("cd/cd_jp", kCancel, "cd/cd").item("acme", {}, "cd/cd_jp_acme").item("beta", {}, "cd/cd_jp_beta");
    m.page("cd/cd_jp_acme", kCancel, "cd/cd_jp")
        .item(world::kCoupe, "set_car_code", "cd/acme_coupe_car")
        .item(world::kWagon, "set_car_code", "cd/acme_wagon_car")
        .widget("syn_coupe_01_price", "goods_car_price", "-", 300, 400)
        .widget("syn_wagon_02_price", "goods_car_price", "-", 300, 376);
    m.page("cd/cd_jp_beta", kCancel, "cd/cd_jp")
        .item("_syn_racer_03", "set_car_code", "cd/beta_racer_car")
        .widget("_syn_racer_03_price", "goods_car_price", "-", 300, 400);
    for (const char* p : {"cd/acme_coupe_car", "cd/acme_wagon_car", "cd/beta_racer_car"})
        m.page(p, kCancel, "return", {}, "00cmn/status")
            .item("buy", {}, "00cmn/cd_buy_sure", 1)
            .widget("focused_carname", "focused_carname", "-", 300, 380)
            .widget("car_buy_price", "car_buy_price", "-", 300, 356)
            .widget("color_name_box", "color_name_box", "-", 300, 332)
            .widget("car_color_box", "car_color_box", "-", 560, 100, 16, 10)
            .widget("car_render_from", "car_render_from", "-", 0, 0, 640, 448)
            .widget("car_logo", "car_logo", "-", 40, 60, 80, 40)
            .hidden(world::kCoupe, {}, 0, 0, 80, 40)
            .hidden(world::kWagon, {}, 0, 0, 80, 40);
    m.page("00cmn/cd_buy_sure", kCancel).item("yes1", "buy_car", "00cmn/cd_bought", 1).item("no1", {}, {}, 3);
    m.page("00cmn/cd_bought", kCancel).item("yes1", "check_ride_car", {}, 3);
    m.page("00cmn/cd_ride_sure", kCancel).item("yes1", "ride_car", {}, 3).item("no1", "ride_car", {}, 3);
    for (const char* p : {"00cmn/no_money", "00cmn/not_for_buy", "00cmn/max_car", "00cmn/ride_car",
                          "00cmn/not_for_sell", "00cmn/regulation_violation", "00cmn/license_violation",
                          "ts/already_you_have", "lc/no_license_a", "lc/no_last_exam"})
        m.page(p, kCancel).item("ok", {}, {}, 3);
    // Home: the garage list (code drawn) and the garage car page.
    m.page("hm/hm", kCancel, "map").item("hm_gr", {}, "hm/hm_gr").item("save", "go_save_in_home", {}, 2);
    // (Both garage lists with their scroll bar left of the window and its hidden "pinch"; the
    // trade list shows the guest garage.)
    m.page("hm/hm_gr", kCancel, "hm/hm")
        .item("car1", "set_garage_car", "hm/hm_car", 0, 40, 380, 300, 20)
        .widget("menu_garage", "window", "-", 40, 100, 420, 300)
        .widget("scroll_bar", "scroll_bar", "", 20, 100, 8, 300)
        .hidden("maker", "MAKE", 350, 410, 80, 16)
        .hidden("price", "PRICE", 350, 410, 80, 16)
        .hidden("info", "-", 350, 0, 100, 20)
        .hidden("info_a", "-", 350, 0, 100, 20)
        .hidden("mark", {}, 0, 0, 12, 12)
        .hidden("pinch", {}, 0, 0, 8, 64);
    m.page("hm/hm_gr_trade", kCancel, "hm/hm")
        .item("car1", "set_garage_car_trade", "hm/hm_car", 0, 40, 380, 300, 20)
        .widget("menu_garage_trade", "window", "-", 40, 100, 420, 300)
        .widget("scroll_bar", "scroll_bar", "", 20, 100, 8, 300)
        .hidden("maker", "MAKE", 350, 410, 80, 16)
        .hidden("info", "-", 350, 0, 100, 20)
        .hidden("info_a", "-", 350, 0, 100, 20)
        .hidden("pinch", {}, 0, 0, 8, 64);
    m.page("hm/hm_car", kCancel, "hm/hm_gr", {}, "00cmn/status")
        .item("sell", {}, "hm/hm_sell_sure", 1)
        .item("ride", "ride_car")
        .item("wheel", {}, "hm/hm_wheel")
        .widget("focused_carname", "focused_carname", "-", 300, 380)
        .widget("car_sell_price", "car_sell_price", "-", 300, 356)
        .widget("car_kilometrage", "car_kilometrage", "-", 300, 332)
        .widget("color_name_box", "color_name_box", "-", 300, 308);
    m.page("hm/hm_sell_sure", kCancel).item("yes1", "sell_car", "hm/hm_gr").item("no1", {}, {}, 3);
    // The garage's wheel list: the page's one "wheel" item (parked above the screen; the list
    // draws a frame of it per button), its window and scroll bar, the hidden tiles (the
    // stock-wheel button's, the wheels', one named after the coupe's model), "mark" and "pinch".
    m.page("hm/hm_wheel", kCancel | 0x001, "hm/hm_car", {}, "00cmn/status")
        .item("wheel", "set_focus_wheel", "hm/hm_change_wheel_sure", 1, 0, 470, 60, 60)
        .widget("car_render_wheel", "car_render_wheel", "-", 0, 0, 640, 448)
        .widget("menu_wheel", "", "", 10, 89, 620, 72)
        .widget("scroll_bar", "scroll_bar", "", 10, 72, 620, 12)
        .hidden("default_wheel", {}, 380, 472, 52, 52)
        .hidden(world::kRimA, {}, 294, 486, 52, 52)
        .hidden(world::kRimB, {}, 294, 486, 52, 52)
        .hidden("zz0001", {}, 294, 486, 52, 52)
        .hidden("mark", {}, 140, 470, 60, 60)
        .hidden("pinch", {}, 0, 0, 40, 12);
    m.page("hm/hm_change_wheel_sure", kCancel, "hm/hm_wheel")
        .item("yes1", "change_wheel", "hm/hm_wheel_changed", 1)
        .item("no1", {}, {}, 3);
    m.page("hm/hm_wheel_changed", 0, "hm/hm_change_wheel_sure").item("buy3", {}, "hm/hm_car");
    m.page("hm/hm_gr_panel", kCancel)
        .item("sort_name", "sort_name", {}, 3)
        .item("sort_price", "sort_price", {}, 3);
    // Races: tiers -> series -> entry; the race information popup.
    m.page("r/r", kCancel, "map")
        .item("beginner", {}, "r/beginner")
        .item("amateur", {}, "r/amateur")
        .item("pro", {}, "r/pro");
    m.page("r/beginner", kCancel, "r/r")
        .item("Gsyn_e_0001", {}, "r/beginner/gsyn_e")
        .focus_layer("s_1")
        .widget("event_status1", "event_status", "-", 200, 300, 120, 30)
        .widget("need_license1", "need_license", "-", 340, 300, 40, 30)
        .hidden("frame", {}, 0, 0, 30, 30)
        .hidden("mark_1", {}, 0, 0, 30, 30)
        .hidden("mark_2", {}, 0, 0, 30, 30)
        .hidden("lc_mark_b", {}, 0, 0, 30, 30);
    // A series page as on the disc: the race information item first, then the races (the
    // focus hook's index from the stored race code counts on that order); per race button an
    // event_status_buttonNN widget (NN = the item's index; the box unused, a widget past the
    // list draws nothing) and the course logo and marks at the disc's places on a button.
    m.page("r/beginner/gsyn_e", kCancel, "r/beginner")
        .item("Gsyn_e_0002_info", "set_race_code_info", "00cmn/race_info", 1, 260, 250, 120, 20)
        .item("Gsyn_e_0001", "set_race_code", "r/beginner/gsyn_e_entry", 0, 40, 300, 200, 40)
        .item("Gsyn_e_0002", "set_race_code", "r/beginner/gsyn_e_entry", 0, 260, 300, 200, 40)
        .el("defaultLayer", "bar_text", {}, 0, 20, 640, 24)
        .widget("event_status1", "event_status_button01", "-", -72, 261, 32, 32)
        .widget("event_status2", "event_status_button02", "-", -70, 211, 32, 32)
        .widget("event_status3", "event_status_button03", "-", -70, 161, 32, 32)
        .hidden("syn_ring", {}, 4, 413, 55, 42)
        .hidden("syn_ring_doc", "Synthetic Ring", 0, 0, 200, 20)
        .hidden("mark_1", {}, 61, 412, 32, 32)
        .hidden("mark_2", {}, 61, 412, 32, 32);
    m.page("r/beginner/gsyn_e_entry", kCancel, "r/beginner/gsyn_e").item("menu1", "go_race", {}, 2);
    // A championship's league item is named after the championship itself (as on the disc).
    m.page("r/amateur", kCancel, "r/r")
        .item("Gsynch_n_0500", {}, "r/amateur/gsynch_n")
        .focus_layer("s_1")
        .widget("event_status1", "event_status", "-", 200, 300, 120, 30, "s_1")
        .hidden("frame", {}, 0, 0, 30, 30)
        .hidden("frame_arrow", {}, 0, 0, 40, 30)
        .hidden("mark_1", {}, 0, 0, 30, 30)
        .hidden("mark_2", {}, 0, 0, 30, 30);
    m.page("r/amateur/gsynch_n", kCancel, "r/amateur")
        .item("Gsynch_n_0501_info", "set_race_code_info", "00cmn/race_info", 1)
        .item("Gsynch_n_0500", "set_race_code_champ", "r/amateur/gsynch_n_entry", 0, 40, 300, 96, 40)
        .item("Gsynch_n_0501", "set_race_code", "r/amateur/gsynch_n_entry", 0, 140, 300, 96, 40)
        .item("Gsynch_n_0502", "set_race_code", "r/amateur/gsynch_n_entry", 0, 240, 300, 152, 40)
        .widget("event_status1", "event_status_button01", "-", -72, 261, 32, 32)
        .widget("event_status2", "event_status_button02", "-", -70, 211, 32, 32)
        .widget("event_status3", "event_status_button03", "-", -70, 161, 32, 32)
        .hidden("syn_ring", {}, 4, 413, 55, 42)
        .hidden("mark_1", {}, 61, 412, 32, 32);
    m.page("r/amateur/gsynch_n_entry", kCancel, "r/amateur/gsynch_n").item("menu1", "go_race", {}, 2);
    m.page("r/pro", kCancel, "r/r").item("Gsynf_s_0001", {}, "r/pro/gsynf_s");
    m.page("r/pro/gsynf_s", kCancel, "r/pro").item("Gsynf_s_0001", "set_race_code", {});
    m.page("00cmn/race_info", kCancel)
        .item("permitted", {}, "00cmn/car_reguration", 1)
        .item("ok", {}, {}, 3)
        .widget("race_prize_1", "race_prize_1", "-", 300, 380)
        .widget("race_prize_6", "race_prize_6", "-", 300, 356)
        .widget("regulation_license", "regulation_license", "-", 300, 332)
        .widget("regulation_dr_type", "regulation_dr_type", "-", 300, 308)
        .widget("regulation_car", "regulation_car", "-", 300, 284);
    m.page("00cmn/car_reguration", kCancel)
        .item("link_menu", {}, {}, 3, 60, 300, 300, 20)
        .widget("menu_regulated_car", "window", "-", 60, 120, 300, 200)
        .widget("scroll_bar", "scroll_bar", "", 370, 120, 10, 200)
        .hidden("no_regulated_car", "No Limitations", 60, 200, 300, 20)
        .hidden("pinch", {}, 0, 0, 10, 40);
    // Tune shop: one part page and the Purchase? -> Install now? -> Installed chain.
    m.page("ts/ts", kCancel, "map").item("bg2", {}, "ts/ts_eg");
    m.page("ts/ts_eg", kCancel, "ts/ts").item("item1", {}, "ts/ts_eg_muf_sp");
    m.page("ts/ts_eg_muf_sp", kCancel, "ts/ts_eg")
        .item("muf_sports", "set_focus_parts", "ts/ts_buy_sure1", 1)
        .widget("price_muf_sports", "price_muf_sports", "-", 300, 380)
        .widget("disp_ps_befor", "disp_ps_befor", "-", 300, 356)
        .widget("disp_ps_after", "disp_ps_after", "-", 300, 332)
        .widget("tuner_logo", "tuner_logo", "-", 40, 60, 80, 40)
        .hidden("acmemotors", {}, 0, 0, 80, 40);
    m.page("ts/ts_buy_sure1", kCancel).item("yes1", "buy_parts", "ts/ts_fit_sure1", 1).item("no1", {}, {}, 3);
    m.page("ts/ts_fit_sure1", kCancel).item("yes1", "fit_parts", "ts/ts_fit_end1", 1).item("no1", {}, {}, 3);
    m.page("ts/ts_fit_end1", kCancel)
        .item("yes1", {}, {}, 3)
        .widget("car_previous_torque", "car_previous_torque", "-", 300, 380)
        .widget("car_current_torque", "car_current_torque", "-", 300, 356);
    // Licences.
    m.page("lc/lc", kCancel, "map").item("lc_b", {}, "lc/lc_b").item("lc_a", {}, "lc/lc_a");
    MenuPage& lb = m.page("lc/lc_b", kCancel, "lc/lc");
    for (int t = 1; t <= 8; ++t) lb.item("L0B000" + std::to_string(t), "go_license", {}, 2);
    lb.widget("disp_license_cup_b1", "disp_license_cup_b1", "-", 200, 400, 30, 20)
        .widget("L0B0001_gold", "disp_time_gold", "-", 300, 400)
        .widget("L0B0001_bronze", "disp_time_bronze", "-", 300, 376)
        .widget("L0B0001_car", "license_car_name", "-", 300, 352)
        .widget("L0B0001_speed", "license_launch_speed", "-", 300, 328)
        .hidden("cup_gold", {}, 0, 0, 20, 20)
        .hidden("cup_silver", {}, 0, 0, 20, 20)
        .hidden("cup_bronze", {}, 0, 0, 20, 20);
    m.page("lc/lc_a", kCancel, "lc/lc").item("L0A0001", "go_license", {}, 2);
    m.page("mt/mt", kCancel, "map").item("M400", "go_machine_test", {}, 2);
    m.page("status", kCancel, "map", {}, "00cmn/status")
        .widget("status_ttl_wins", "status_ttl_wins", "-", 300, 400)
        .widget("status_ttl_races", "status_ttl_races", "-", 300, 376)
        .widget("status_avg_win", "status_avg_win", "-", 300, 352)
        .widget("status_ttl_milage", "status_ttl_milage", "-", 300, 328)
        .widget("status_prize_money", "status_prize_money", "-", 300, 304)
        .widget("status_no_cars", "status_no_cars", "-", 300, 280)
        .widget("status_ttl_car_price", "status_ttl_car_price", "-", 300, 256)
        .widget("status_no_lc_g", "status_no_lc_g", "-", 300, 232)
        .widget("status_complete", "status_complete", "-", 300, 208)
        .widget("license_mark", "license_mark", "-", 40, 300, 40, 40)
        .hidden("lc_mark1", {}, 0, 0, 40, 40)
        .hidden("lc_mark2", {}, 0, 0, 40, 40);
    m.write(files);
}

// The presentation sets (prizes, licence card, trophy, prize car, garage full, record
// name entry) and the records / settings menus.
inline void add_presentation_menus(std::map<std::string, Bytes>& files) {
    auto mini = [&](const char* set, bool money) {
        MenuWriter m(set);
        MenuPage& p = m.page(set, 0).item("ok", {}, {}, 3, 280, 40, 80, 24);
        p.widget("rank_disp", "rank_disp", "-", 100, 300).widget("model", "render_model", "-", 200, 150, 240, 200);
        if (money)
            p.widget("count", "count", "-", 300, 100, 200, 20)
                .widget("get_money_disp", "get_money_disp", "-", 300, 76, 200, 20);
        m.write(files);
    };
    mini("gtmode_mini", true);
    mini("consolation_prize", true);
    mini("arcade_mini", false);
    {
        MenuWriter m("lc_get");
        m.page("license_get", 0)
            .item("ok", {}, {}, 3, 280, 40, 80, 24)
            .widget("model", "render_model", "-", 200, 150, 240, 200)
            .el("b_mess0", "b_mess0_text", "You obtained it", 100, 380, 300, 20)
            .el("b_mess1", "b_mess1_text", "Well driven", 100, 380, 300, 20);
        m.write(files);
    }
    {
        MenuWriter m("trophy_get");
        m.page("trophy_get", 0)
            .item("ok", {}, {}, 3, 280, 40, 80, 24)
            .widget("model", "render_model", "-", 200, 150, 240, 200)
            .widget("count", "count", "-", 300, 100, 200, 20)
            .el("mess0", "mess0_text", "Champion", 100, 380, 300, 20)
            .widget("get_money_disp", "get_money_disp", "-", 300, 76, 200, 20, "get_mess")
            .el("mess1", "mess1_text", "Congratulations", 100, 360, 300, 20);
        m.write(files);
    }
    {
        MenuWriter m("car_get");
        m.page("car_get", 0)
            .item("ok", {}, {}, 3, 280, 40, 80, 24)
            .widget("model", "render_model", "-", 200, 150, 240, 200)
            .widget("event_message", "event_message", "-", 100, 400, 400, 24)
            .hidden("solo_race_clear", "Series cleared", 0, 0, 300, 20)
            .hidden("championship_win", "Championship won", 0, 0, 300, 20)
            .hidden("license_b_all_gold", "All gold", 0, 0, 300, 20)
            .hidden("gtmode_complete", "Everything won", 0, 0, 300, 20);
        m.write(files);
    }
    {
        MenuWriter m("not_get_car");
        m.page("not_car_get", 0).item("ok", {}, {}, 3, 280, 40, 80, 24);
        m.write(files);
    }
    {
        // Name entry: one key row (A B C) and OK. GtData reads the keys' characters from
        // this layout (menu/US/name_entry1.pmb).
        MenuWriter m("name_entry");
        m.page("name_entry", 0)
            .item("free_cursor", {}, {}, 0x14, 40, 300, 120, 24)
            .child("key_a", "input_text", 0, "A")
            .child("key_b", "input_text", 0, "B")
            .child("key_c", "input_text", 0, "C")
            .item("free_command", {}, {}, 3, 40, 260, 120, 24)
            .widget("text_box", "text_box", "-", 40, 380, 300, 24)
            .hidden("cursor", {}, 0, 0, 3, 16);
        m.write(files);
    }
    {
        MenuWriter m("records");
        m.page("records_free_tt", 0x100)
            .item("record_bar", {}, {}, 3, 40, 380, 560, 24)
            .widget("menu_records", "window", "-", 40, 56, 560, 332)
            .el("hidden", "record_rank", "-", 40, 384, 30, 16)
            .el("hidden", "record_rank_shadow", "-", 41, 383, 30, 16)
            .el("hidden", "record_name", "-", 80, 384, 120, 16, false, 0)
            .el("hidden", "record_car", "-", 210, 384, 160, 16, false, 0)
            .el("hidden", "record_car_pict", {}, 180, 384, 28, 16, true)
            .el("hidden", "record_data", "-", 380, 384, 100, 16)
            .el("hidden", "record_date", "-", 500, 384, 80, 16);
        m.write(files);
    }
    {
        // Car settings: root -> parts -> suspension -> spring rate (one slider; OK commits and
        // goes back, kind 3 without a link, as on every set/adjast_ slider page of the disc).
        MenuWriter m("setting");
        m.page("root", 0x100)
            .item("item2", {}, "parts_setting")
            .item("save", "go_save", {}, 2)
            .item("load", "go_load", {}, 2);
        m.page("parts_setting", 0x100, "root").item("item1", {}, "set/adjast_sus");
        m.page("set/adjast_sus", 0x100, "parts_setting")
            .item("status_box_sus1", {}, "set/adjast_sus_spring")
            .item("status_box_sus3", {}, "set/adjast_sus");
        m.page("set/adjast_sus_spring", 0x100, "set/adjast_sus", "retrieve_slider")
            .item("slider_rate_f", {}, {}, 0, 100, 300, 200, 10)
            .item("ok", "commit_slider", {}, 3, 100, 200, 80, 20)
            .hidden("pinch", {}, 0, 0, 8, 14)
            .hidden("pinch_prev", {}, 0, 0, 8, 14)
            .hidden("pinch_fix", {}, 0, 0, 8, 14);
        m.write(files);
    }
}

// The arcade menus (menu/arcade.mbl): the top page, difficulty, the code-drawn pickers
// (their entries are the "hidden" art) and Clear Status.
inline void add_arcade_menus(std::map<std::string, Bytes>& files) {
    MenuWriter m("arcade");
    m.page("ar/top", 0x100, {}, "cancel_select_game")
        .item("single_race", "single_race", {}, 2)
        .item("time_trial", "compulsory", {}, 2)
        .item("free_run", "time_attack", {}, 2)
        .item("battle", "two_player_battle", {}, 2)
        .item("goodies", "goodies", {}, 2)
        .el("hidden", "history_level_easy", {}, 0, 420, 60, 20)
        .el("hidden", "history_level_normal", {}, 0, 420, 60, 20)
        .el("hidden", "history_area_a", {}, 0, 420, 60, 20)
        .el("hidden", "history_class_c", {}, 0, 420, 60, 20);
    m.page("ar/level", 0x100).item("easy", {}, {}, 2).item("normal", {}, {}, 2).item("hard", {}, {}, 2);
    // Pickers: "<id>_logo" + "<id>_doc" (+ "<id>_mini" strip icons); cars: a picture
    // "<car>" + "<car>_mini".
    auto picker = [&](const char* page, const char* strip, const std::vector<std::string>& ids,
                      bool cars) -> MenuPage& {
        MenuPage& p = m.page(page, 0);
        for (const std::string& id : ids) {
            if (cars) p.el("hidden", id, {}, 200, 200, 200, 100, true);
            else p.el("hidden", id + "_logo", {}, 200, 300, 200, 60).hidden(id + "_doc", id, 200, 260, 240, 20);
            p.el("hidden", id + "_mini", {}, 0, 0, 40, 24, true);
        }
        p.hidden(strip, {}, 120, 40, 400, 30).hidden("cross_bar", {}, 300, 180, 40, 90);
        p.hidden("arrow_l", {}, 10, 200, 20, 20).hidden("arrow_r", {}, 610, 200, 20, 20);
        return p;
    };
    picker("ar/select_course", "mini_courses", {"syn_ring", "syn_dirt", "syn_hill"}, false)
        .hidden("area_a", {}, 100, 380, 40, 20)
        .hidden("tarmac", {}, 150, 380, 40, 20)
        .hidden("dirt", {}, 150, 380, 40, 20)
        .hidden("total_length", "Length", 200, 380, 60, 20)
        .hidden("length", "-", 260, 380, 80, 20);
    picker("ar/select_class", "mini_classes", {"class_c", "class_b", "class_dirt", "home_garage", "guest_garage"},
           false)
        .hidden("disable_dirt_doc", "Dirt cars only", 200, 260, 240, 20)
        .hidden("disable_road_doc", "Road cars only", 200, 260, 240, 20)
        .hidden("disable_no_car_doc", "No cars", 200, 260, 240, 20)
        .hidden("disabled", {}, 200, 300, 40, 20)
        .hidden("x", {}, 0, 0, 20, 20);
    for (const char* page : {"ar/select_car", "ar/select_car_2p"}) {
        MenuPage& p = picker(page, "mini_cars", {"syn_coupe_01", "syn_wagon_02"}, true);
        p.hidden("mini_cars_2p", {}, 120, 10, 400, 30).hidden("car", {}, 0, 0, 640, 448);
        for (const char* n : {"handling", "speed", "acceleration", "handling_gauge", "speed_gauge",
                              "acceleration_gauge", "color_chip", "color_name", "color_selection", "ps", "kgm", "fr",
                              "ff", "flow_spec", "arrow_up", "arrow_down", "auto_yes", "auto_no", "manu_yes",
                              "manu_no", "racing_yes", "racing_no", "dri_yes", "dri_no", "class_c_mini", "class_b_mini",
                              "class_dirt_mini", "home_garage_mini", "guest_garage_mini", "class_c_logo"})
            p.hidden(n, n, 300, 100, 30, 16);
    }
    m.page("ar/goodies", 0x100)
        .item("goal", {}, "ar/select_ending")
        .widget("course_logo_1", "course_logo_1", "-", 40, 300, 60, 30)
        .widget("mark_easy_1", "mark_easy_1", "-", 110, 300, 20, 20)
        .hidden("syn_ring_mini", {}, 0, 0, 60, 30)
        .hidden("clear_easy", {}, 0, 0, 20, 20);
    m.page("ar/cannot_ending", 0x100).item("ok", {}, {}, 3);
    m.page("ar/select_ending", 0x100, "ar/goodies").item("short", "go_ending_short", {}, 2);
    m.write(files);
}

// The race songs of bgm/ads.inf ("MADS": 17 groups {u32 records, u32 count}, 20-byte records
// {name, file, title, artist, info} as string offsets); group 2 holds the race songs (BGM id
// 0x14). Three invented songs.
namespace world {
inline const char* const kSongFiles[3] = {"race_s01.ads", "race_s02.ads", "race_s03.ads"};
inline const char* const kSongTitles[3] = {"First Lap", "Second Wind", "Third Gear"};
inline const char* const kSongArtist = "The Synthetics";
} // namespace world

inline void add_music(std::map<std::string, Bytes>& files) {
    Bytes b(0x10 + 8 * 17, 0);
    std::memcpy(b.data(), "MADS", 4);
    p32(b, 4, 1);
    p32(b, 12, 3);
    const size_t rec = b.size();
    b.resize(rec + 20 * 3, 0);
    p32(b, 0x10 + 8 * 2, u32(rec));
    p32(b, 0x14 + 8 * 2, 3);
    for (size_t i = 0; i < 3; ++i) {
        const std::string file = world::kSongFiles[i];
        p32(b, rec + 20 * i, u32(append_cstr(b, file.substr(0, file.size() - 4))));
        p32(b, rec + 20 * i + 4, u32(append_cstr(b, file)));
        p32(b, rec + 20 * i + 8, u32(append_cstr(b, world::kSongTitles[i])));
        p32(b, rec + 20 * i + 12, u32(append_cstr(b, world::kSongArtist)));
    }
    files["bgm/ads.inf"] = b;
}

// The music list's hidden art (menus §10.1 names; the sizes of a 24-unit row list).
inline void add_music_list_art(MenuPage& p, bool cursor) {
    p.hidden("label_check_box", {}, 28, 300, 580, 24);
    p.hidden("check_box", {}, 28, 300, 24, 24);
    p.hidden("label_box", "-", 56, 300, 552, 24);
    p.hidden("check_on", {}, 700, 400, 24, 24);
    p.hidden("check_off", {}, 700, 370, 24, 24);
    if (!cursor) return;
    p.hidden("check_on_focus", {}, 700, 340, 24, 24);
    p.hidden("check_off_focus", {}, 700, 310, 24, 24);
    p.hidden("label_check_focus", {}, 31, 493, 580, 24);
    p.hidden("label_focus", "-", 50, 463, 552, 24);
    p.hidden("mode_select_box", {}, 28, 218, 580, 32);
    p.hidden("button_straight", "Order", 197, 224, 102, 22);
    p.hidden("button_straight_focus", "Order", 197, 196, 102, 22);
    p.hidden("button_shuffle", "Shuffle", 345, 224, 102, 22);
    p.hidden("button_shuffle_focus", "Shuffle", 345, 196, 102, 22);
    p.hidden("pop_check_box", {}, 24, 260, 32, 32);
    p.hidden("pop_label_box", "-", 60, 260, 552, 32);
    p.hidden("pop_label_check_box", {}, 24, 260, 588, 32);
}

// Options, the save / theater pages (memorycard) and the Analyzer (logger).
inline void add_other_menus(std::map<std::string, Bytes>& files) {
    {
        MenuWriter m("option");
        MenuPage& top = m.page("option", 0x100);
        top.item("view", {}, "view/view").item("sound", {}, "sound/sound").item("music", {}, "music/music");
        top.focus_layer("m3").widget("song_list", "song_list", "-", 28, 44, 580, 308, "m3");
        add_music_list_art(top, false);
        MenuPage& music = m.page("music/music", 0x163, "option", "stop_music");
        music.widget("song_list", "song_edit", "-", 28, 44, 580, 308);
        add_music_list_art(music, true);
        m.page("view/view", 0x100, "option", "commit")
            .item("multi_wide_mode", {}, {}, 0x14, 40, 300)
            .child("wide_mode_off", "commit", 3, "OFF")
            .child("wide_mode_on", "commit", 3, "ON");
        m.page("sound/sound", 0x100, "option", "commit")
            .item("multi_menu_bgm", {}, {}, 0x14, 40, 300)
            .child("menu_bgm_on", "commit", 3, "ON")
            .child("menu_bgm_off", "commit", 3, "OFF");
        m.write(files);
    }
    {
        MenuWriter m("memorycard");
        m.page("file_operation", 0).el("defaultLayer", "title", "Files", 40, 420, 200, 20);
        m.page("demo_loader", 0).el("defaultLayer", "title", "Replays", 40, 420, 200, 20);
        m.page("name_entry", 0)
            .item("free_cursor", {}, {}, 0x14, 40, 300, 120, 24)
            .child("key_a", "input_text", 0, "A")
            .child("key_b", "input_text", 0, "B")
            .item("free_command", {}, {}, 3, 40, 260, 120, 24)
            .widget("text_box", "text_box", "-", 40, 380, 300, 24);
        m.write(files);
    }
    {
        MenuWriter m("logger");
        MenuPage& p = m.page("top", 0);
        for (const char* n : {"RawListItemFixed_Replay", "RawListItemFixed_Graph", "RawListItemFixed_TrackA",
                              "RawListItemFixed_TrackB", "RawListItemFixed_Exit"})
            p.el("defaultLayer", n, n, 40, 300, 100, 20);
        m.write(files);
    }
}

// text/realtime.dat (MLTX, docs/formats/text.md) with the prize pages' word before "Cr.", in
// the seven languages' pools (JP, US, BR, FR, GR, IT, SP; the Japanese one in the game encoding).
inline void add_realtime_text(std::map<std::string, Bytes>& files) {
    const std::string key = "PRIZE_RT_MSG_BONUS_PREFIX";
    const char* const values[7] = {"\xBE\xDE\xB6\xE2", "Prize", "Prize", "Prix", "Preis", "Premio", "Precio"};
    constexpr size_t kPool = 8;
    const size_t keys = 0x14 + 8, pool = keys + key.size() + 1;
    Bytes b(pool + 7 * kPool, 0);
    std::memcpy(b.data(), "MLTX", 4);
    p32(b, 4, 2);
    p32(b, 8, u32(pool));
    p32(b, 12, 1);
    p32(b, 16, u32(kPool));
    p32(b, 0x14, u32(keys));
    p32(b, 0x18, 0);
    std::memcpy(&b[keys], key.c_str(), key.size());
    for (size_t l = 0; l < 7; ++l) std::memcpy(&b[pool + l * kPool], values[l], std::strlen(values[l]));
    files["text/realtime.dat"] = b;
}

// Every file of the world, by VOL path (`twins`: with the regional twin cars, add_database).
inline std::map<std::string, Bytes> gtmode_files(bool twins = false) {
    std::map<std::string, Bytes> f;
    add_database(f, twins);
    add_colors(f);
    add_gcf(f);
    add_gt_mode_menus(f);
    add_presentation_menus(f);
    add_arcade_menus(f);
    add_other_menus(f);
    add_music(f);
    add_realtime_text(f);
    return f;
}

inline menu::flow::FileReader gtmode_reader() {
    auto files = std::make_shared<const std::map<std::string, Bytes>>(gtmode_files());
    return [files](const std::string& p) -> std::optional<Bytes> {
        auto it = files->find(p);
        if (it == files->end()) return std::nullopt;
        return it->second;
    };
}

} // namespace gt3r::test::gtsynth
