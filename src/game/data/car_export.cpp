#include "game/data/car_export.h"

#include "base/config.h"
#include "base/file.h"
#include "formats/db/gt_db.h"
#include "formats/text/charset.h"
#include "game/data/mod_cars.h"
#include "race/paint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace gt3r::data {

namespace {

// GT3's record size of each table (docs/formats/database.md): Gran Turismo Concept's chassis,
// engine and gear records are these with bytes appended, which GT3 does not read.
constexpr std::array<u16, 31> kGt3RecordSize = {24, 32, 64, 32, 24, 72, 88, 24, 32, 24, 24, 32, 40, 40, 32, 32,
                                                32, 48, 128, 24, 24, 48, 32, 48, 16, 16, 24, 88, 96, 96, 264};

// The tables whose records belong to one car (its label at +8).
void put64(Bytes& b, size_t o, u64 v) {
    for (int i = 0; i < 8; ++i) b[o + size_t(i)] = u8(v >> (8 * i));
}

bool part_table(int t) { return (t <= 21 && t != 2) || t == 28 || t == 29; }

std::string utf8(const std::string& game_text, const text::JisTable* jis) {
    std::string s = text::game_text_to_utf8(game_text, jis);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    return s;
}

// An ini key or value of our own making: no line breaks, and none of the characters that start a
// section, a value or a comment.
std::string ini_safe(std::string s) {
    for (char& c : s)
        if (c == '=' || c == ';' || c == '[' || c == ']' || c == '\n' || c == '\r') c = '-';
    return s;
}

bool write_text(const std::string& path, const std::string& text) {
    return write_host_file(path, ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
}

bool write_file(const std::string& root, const std::string& rel, ByteSpan data, std::vector<std::string>& files) {
    const std::string path = root + "/" + rel;
    host_make_dir(path.substr(0, path.find_last_of('/')));
    if (!write_host_file(path, data)) return false;
    files.push_back(rel);
    return true;
}

// --- tuning parts for a car its disc sells none for (ExportOptions) ---

void put16(Bytes& b, size_t o, u16 v) { b[o] = u8(v), b[o + 1] = u8(v >> 8); }
void put32(Bytes& b, size_t o, u32 v) {
    for (int i = 0; i < 4; ++i) b[o + size_t(i)] = u8(v >> (8 * i));
}
u8 clamp8(double v) { return u8(std::clamp(std::lround(v), 0L, 255L)); }

// The tune shop's tables (game/gt_parts.cpp, its goods): all but the racing body (another model), the drivetrain (an
// AWD car's coupling, which holds the car's own inertias) and the driving aids (records every car shares).
bool shop_table(int t) { return t == 0 || t == 1 || t == 4 || (t >= 7 && t <= 12) || (t >= 14 && t <= 21) || t == 28 || t == 29; }
bool tire_table(int t) { return t == 28 || t == 29; }
// A part's price (docs/formats/database.md): the last word of GT3's record, a gearbox's at +0x2C, a tyre's at +0x14.
u32 price_of(int t, ByteSpan r) {
    const size_t o = t == 17 ? 0x2C : tire_table(t) ? 0x14 : kGt3RecordSize[size_t(t)] - 4u;
    return o + 4 <= r.size() ? rd32(r, o) : 0;
}
void set_price(int t, Bytes& r, u32 price) {
    const size_t o = t == 17 ? 0x2C : tire_table(t) ? 0x14 : kGt3RecordSize[size_t(t)] - 4u;
    if (o + 4 <= r.size()) put32(r, o, price);
}
u8 stage_of(ByteSpan r) { return r.size() > 0x10 ? r[0x10] : 0; }

// Every shop part of the disc by the car it fits.
using ShopIndex = std::map<u64, std::vector<std::pair<int, ByteSpan>>>;
ShopIndex shop_index(const CarDatabase& db) {
    ShopIndex out;
    for (int t = 0; t <= 29; ++t)
        if (shop_table(t))
            for (ByteSpan r : db.table(TableId(t)).records)
                if (r.size() > 0x10) out[rd64(r, 8)].push_back({t, r});
    return out;
}
// Whether the disc sells the car tuning: an upgrade with a price in a table other than the brake controller's and
// the tyres' (Gran Turismo Concept prices a few of its cars' brake controllers and nothing else).
bool sells_tuning(const ShopIndex& index, u64 car) {
    auto it = index.find(car);
    if (it != index.end())
        for (const auto& [t, r] : it->second)
            if (t != 1 && !tire_table(t) && stage_of(r) && price_of(t, r)) return true;
    return false;
}

// What the template choice compares.
struct CarFacts {
    int drive = -1, gears = 0;
    bool turbo = false;
    u8 category = 0;
    float ps = 0, kg = 0;
    const ByteSpan *engine = nullptr, *chassis = nullptr;
};
const ByteSpan* stock_part(const CarDatabase& db, const Car& c, size_t slot) {
    return c.parts[slot] ? db.record(TableId(kCarPartTables[slot]), c.parts[slot]) : nullptr;
}
CarFacts facts_of(const CarDatabase& db, const Car& c) {
    CarFacts f;
    if (const ByteSpan* d = stock_part(db, c, 13); d && d->size() > 0x14) f.drive = (*d)[0x14];
    if (const ByteSpan* g = stock_part(db, c, 18); g && g->size() > 0x11) f.gears = (*g)[0x11];
    if (const ByteSpan* t = stock_part(db, c, 12); t && t->size() > 0x11) f.turbo = (*t)[0x11] != 0;
    f.category = c.bytes.size() > 0xF1 ? c.bytes[0xF1] : 0;
    f.chassis = stock_part(db, c, 3);
    if (f.chassis && f.chassis->size() > 0x1B) f.kg = rd16(*f.chassis, 0x1A);
    f.engine = stock_part(db, c, 6);
    if (auto e = db.engine(c.parts[6])) f.ps = e->curve_peak_ps() * float(e->torque_scale_pct) / 100.0f;
    return f;
}
bool same_bytes(const ByteSpan* a, const ByteSpan* b, size_t from, size_t to) {
    return a && b && a->size() >= to && b->size() >= to && std::equal(a->begin() + std::ptrdiff_t(from), a->begin() + std::ptrdiff_t(to), b->begin() + std::ptrdiff_t(from));
}

// The most alike car the disc sells tuning for: the same category, drive layout and aspiration (given up in that order
// when no car has them), then the nearest power and weight, the same gear count; a car with the car's engine (torque
// curve and its rpm axis) and chassis comes first.
const Car* shop_template(const CarDatabase& db, const Car& car, const ShopIndex& index) {
    const CarFacts f = facts_of(db, car);
    for (int relax = 0; relax < 4; ++relax) {
        const Car* best = nullptr;
        double best_score = 0;
        for (const Car& c : db.cars()) {
            if (&c == &car || db.mod_car(c.label) || !sells_tuning(index, db::id_hash(c.label))) continue;
            const CarFacts g = facts_of(db, c);
            if (relax < 1 && g.category != f.category) continue;
            if (relax < 2 && g.drive != f.drive) continue;
            if (relax < 3 && g.turbo != f.turbo) continue;
            double s = 2 * std::fabs(std::log(std::max(f.ps, 1.0f) / std::max(g.ps, 1.0f))) +
                       std::fabs(std::log(std::max(f.kg, 1.0f) / std::max(g.kg, 1.0f))) + (g.gears != f.gears ? 0.5 : 0);
            if (same_bytes(f.engine, g.engine, 0x18, 0x38) && same_bytes(f.engine, g.engine, 0x45, 0x58)) s -= 10;
            if (same_bytes(f.chassis, g.chassis, 0x10, 0x20)) s -= 5;
            if (!best || s < best_score) best = &c, best_score = s;
        }
        if (best) return best;
    }
    return nullptr;
}

// One of the template's parts (`b`, a copy) fitted to the car (ExportOptions), from the two cars' stock records of
// the table; left as it is when either car has none.
void fit_part(int t, Bytes& b, const CarDatabase& db, const Car& car, const Car& tmpl) {
    const int slot = [&] {
        for (size_t i = 0; i < kCarPartTables.size(); ++i)
            if (kCarPartTables[i] == t) return int(i);
        return -1;
    }();
    if (slot < 0) return;
    const ByteSpan* cs = stock_part(db, car, size_t(slot));
    const ByteSpan* ts = stock_part(db, tmpl, size_t(slot));
    if (!cs || !ts) return;
    if (t == int(TableId::Brake) && b.size() > 0x13 && cs->size() > 0x13 && ts->size() > 0x13) {
        // Brake torques in the ratio of the two cars' stock brakes.
        for (size_t o : {0x11, 0x12, 0x13})
            if ((*ts)[o]) b[o] = clamp8(b[o] * double((*cs)[o]) / (*ts)[o]);
    } else if (t == int(TableId::Gear) && b.size() >= 0x28 && cs->size() >= 0x28 && ts->size() >= 0x28) {
        // The car's gear count, reverse and final drive; an adjustable box's range widened to hold it (the shop's
        // boxes share the stock final drive, as every gearbox on the disc does). The ratios are generated for the car.
        b[0x11] = (*cs)[0x11];
        if (rd16(b, 0x12) == rd16(*ts, 0x12)) put16(b, 0x12, rd16(*cs, 0x12));
        const s16 tf = s16(rd16(*ts, 0x22)), f = s16(rd16(*cs, 0x22));
        for (size_t o : {0x24, 0x26})
            if (s16(rd16(b, o)) == tf) put16(b, o, u16(f));
        put16(b, 0x22, u16(f));
        if (s16(rd16(b, 0x24)) < f) put16(b, 0x24, u16(f));
        if (s16(rd16(b, 0x26)) > f) put16(b, 0x26, u16(f));
        // Fixed ratios equal to the template's stock ones are the car's stock ones.
        for (size_t i = 0; i < 7; ++i)
            if (rd16(b, 0x14 + i * 2) == rd16(*ts, 0x14 + i * 2)) put16(b, 0x14 + i * 2, rd16(*cs, 0x14 + i * 2));
    } else if (t == int(TableId::Suspension) && b.size() >= 0x5C && cs->size() >= 0x5C && ts->size() >= 0x5C) {
        // Springs, dampers and anti-roll bars in the ratio of the two cars' axle loads; ride heights (range and default)
        // moved by the difference of the stock ones.
        const ByteSpan* cc = stock_part(db, car, 3);
        const ByteSpan* tc = stock_part(db, tmpl, 3);
        if (cc && tc && cc->size() > 0x1B && tc->size() > 0x1B && rd16(*tc, 0x1A)) {
            const double cw = rd16(*cc, 0x1A), tw = rd16(*tc, 0x1A), cf = (*cc)[0x10], tf = (*tc)[0x10];
            const double rf = cw * cf / std::max(1.0, tw * tf), rr = cw * (100 - cf) / std::max(1.0, tw * (100 - tf));
            for (size_t o : {0x21, 0x22, 0x23, 0x2C, 0x2D, 0x2F, 0x30, 0x33, 0x34, 0x36, 0x37, 0x48, 0x49}) b[o] = clamp8(b[o] * rf);
            for (size_t o : {0x24, 0x25, 0x26, 0x3A, 0x3B, 0x3D, 0x3E, 0x41, 0x42, 0x44, 0x45, 0x4C, 0x4D}) b[o] = clamp8(b[o] * rr);
        }
        const int df = int(rd16(*cs, 0x54)) - int(rd16(*ts, 0x54)), dr = int(rd16(*cs, 0x5A)) - int(rd16(*ts, 0x5A));
        for (size_t o : {0x50, 0x52, 0x54}) put16(b, o, u16(std::clamp(int(rd16(b, o)) + df, 0, 0xFFFF)));
        for (size_t o : {0x56, 0x58, 0x5A}) put16(b, o, u16(std::clamp(int(rd16(b, o)) + dr, 0, 0xFFFF)));
    } else if (tire_table(t) && b.size() >= 0x20 && cs->size() >= 0x20) {
        put64(b, 0x18, rd64(*cs, 0x18)); // the car's own tyre size
    }
}

} // namespace

std::string export_suffix(std::string_view disc_key) {
    std::string s;
    for (char c : to_lower(disc_key))
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) s += c;
    return s.empty() ? "disc" : s;
}

std::string export_mod_id(std::string_view disc_key) { return "cars_" + export_suffix(disc_key); }

std::optional<CarExport> export_car(const CarDatabase& db, const CarDatabase::Reader& read, const std::string& label,
                                    const std::string& mod_dir, std::string_view disc_key, const std::string& source,
                                    std::string* error, const ExportOptions& options) {
    auto fail = [&](const std::string& m) -> std::optional<CarExport> {
        if (error) *error = m;
        return std::nullopt;
    };
    const Car* car = db.car(label);
    if (!car || db.mod_car(label)) return fail(label + " is not a car on this disc");
    const std::string suffix = export_suffix(disc_key);
    text::JisTable jis_table;
    if (std::optional<Bytes> j = read("font/jis2uni.dat"); j && text::JisTable::is_utbl(*j)) {
        try {
            jis_table = text::JisTable::parse(*j);
        } catch (const std::exception&) {
        }
    }
    const text::JisTable* jis = jis_table.empty() ? nullptr : &jis_table;
    // The new label: the car's with the disc's suffix, within the 40 characters a mod car's label may have.
    std::string base = label;
    if (base.size() + 1 + suffix.size() > 40) base.resize(40 - 1 - std::min<size_t>(suffix.size(), 20));
    const std::string new_label = base + "_" + suffix.substr(0, std::min<size_t>(suffix.size(), 40 - 1 - base.size()));
    const u64 old_code = db::id_hash(label), new_code = db::id_hash(new_label);

    CarExport out;
    out.label = new_label;
    CarRecordFile file;
    std::map<u64, u64> remap;      // the car's own records: old id -> new id
    std::set<std::pair<int, u64>> shared; // shared records it refers to (table, id)
    std::map<std::string, std::string> models; // disc model -> exported model name
    std::set<u16> sounds;
    auto model_name = [&](const std::string& m) {
        auto [it, added] = models.emplace(m, m + "_" + suffix);
        return it->second;
    };

    // Its own records: every part record naming the car, and the stock parts it borrows from another car (a tuned
    // variant's are its base car's), relabelled.
    int unnamed = 0;
    std::set<std::string> taken; // the labels of its own records
    auto take = [&](int t, ByteSpan r, const std::string& owner) {
        const std::string l = db.label(rd64(r, 0));
        size_t p = l.find(owner);
        std::string nl = p == std::string::npos
                             ? std::string(table_name(TableId(t))) + "_" + new_label + "_" + std::to_string(++unnamed)
                             : l.substr(0, p) + new_label + l.substr(p + owner.size());
        while (taken.count(nl)) nl = std::string(table_name(TableId(t))) + "_" + new_label + "_" + std::to_string(++unnamed);
        taken.insert(nl);
        CarRecordFile::Record rec;
        rec.table = TableId(t);
        rec.label = nl;
        rec.bytes.assign(r.begin(), r.begin() + std::ptrdiff_t(std::min<size_t>(r.size(), kGt3RecordSize[size_t(t)])));
        put64(rec.bytes, 0, db::id_hash(nl));
        put64(rec.bytes, 8, new_code);
        for (auto [off, uni] : record_text_fields(TableId(t)))
            if (off + 2u <= rec.bytes.size()) {
                const u16 i = rd16(rec.bytes, off);
                rec.texts.push_back({off, uni ? CarRecordFile::Text::Unistring : CarRecordFile::Text::String,
                                     uni ? db.unistrings().at(i) : db.strings().at(i)});
            }
        if (t == int(TableId::RacingModify) && rec.bytes.size() >= 0x20) { // its models, under exported names
            const std::string m = db.label(rd64(rec.bytes, 0x18));
            if (!m.empty() && m != "-") {
                const std::string nm = model_name(m);
                put64(rec.bytes, 0x18, db::id_hash(nm));
                rec.texts.push_back({0x18, CarRecordFile::Text::Label, nm});
            }
        }
        if (t == int(TableId::Engine) && rec.bytes.size() >= 0x18 && rd16(rec.bytes, 0x16)) sounds.insert(rd16(rec.bytes, 0x16));
        if ((t == int(TableId::FrontTire) || t == int(TableId::RearTire)) && rec.bytes.size() >= 0x60) {
            shared.insert({int(TableId::TireSize), rd64(rec.bytes, 0x18)});
            for (size_t k = 0; k < 4; ++k) {
                shared.insert({int(TableId::TireCompound), rd64(rec.bytes, 0x20 + k * 8)});
                shared.insert({int(TableId::TireForceVol), rd64(rec.bytes, 0x40 + k * 8)});
            }
        }
        remap[rd64(r, 0)] = db::id_hash(nl);
        file.records.push_back(std::move(rec));
    };
    for (int t = 0; t <= 29; ++t) {
        if (!part_table(t)) continue;
        for (ByteSpan r : db.table(TableId(t)).records)
            if (r.size() >= 16 && rd64(r, 8) == old_code) take(t, r, label);
    }
    for (size_t i = 0; i < kCarPartTables.size(); ++i) {
        const int t = kCarPartTables[i];
        if (!car->parts[i] || t < 0 || !part_table(t) || remap.count(car->parts[i])) continue;
        const ByteSpan* r = db.record(TableId(t), car->parts[i]);
        if (!r || r->size() < 16) continue;
        const std::string owner = db.label(rd64(*r, 8));
        if (!owner.empty() && db.car(owner)) take(t, *r, owner); // (else a part every car shares: "no turbo")
    }
    // Tuning parts for a car its disc sells none for (ExportOptions): each shop table it sells nothing in takes the
    // template's upgrades (not its stock part), fitted to the car; a part the car holds at that stage without a price
    // keeps its values and takes the template's price.
    const bool concept_disc = db.place_of(TableId::Car) != size_t(TableId::Car); // (Gran Turismo Concept's table order)
    const ShopIndex index = options.shop == ExportOptions::Shop::Keep || (options.shop == ExportOptions::Shop::Auto && !concept_disc)
                                ? ShopIndex{}
                                : shop_index(db);
    const Car* tmpl = index.empty() || sells_tuning(index, old_code) ? nullptr : shop_template(db, *car, index);
    if (tmpl) {
        const auto& theirs = index.at(db::id_hash(tmpl->label));
        auto sold = [&](int t) { // a priced upgrade of the car's own in the table
            for (const CarRecordFile::Record& r : file.records)
                if (int(r.table) == t && rd64(r.bytes, 8) == new_code && stage_of(r.bytes) && price_of(t, r.bytes)) return true;
            return false;
        };
        std::set<u64> stock; // the car's stock parts, under their new ids
        for (u64 id : car->parts)
            if (auto it = remap.find(id); it != remap.end()) stock.insert(it->second);
        // Differentials and the propeller shaft belong to a drive layout.
        const bool same_drive = facts_of(db, *car).drive == facts_of(db, *tmpl).drive;
        for (int t = 0; t <= 29; ++t) {
            if (!shop_table(t) || sold(t) || (!same_drive && (t == 16 || t == 21))) continue;
            for (const auto& [tt, r] : theirs) {
                if (tt != t || !stage_of(r) || std::count(tmpl->parts.begin(), tmpl->parts.end(), rd64(r, 0))) continue;
                if (tire_table(t) && stage_of(r) > 9 && stage_of(r) != 12) continue; // (not a shop tyre: race and arcade compounds)
                CarRecordFile::Record* own = nullptr;
                for (CarRecordFile::Record& x : file.records)
                    if (int(x.table) == t && rd64(x.bytes, 8) == new_code && stage_of(x.bytes) == stage_of(r)) own = &x;
                if (own) {
                    if (!stock.count(rd64(own->bytes, 0)) && !price_of(t, own->bytes) && price_of(t, r)) {
                        set_price(t, own->bytes, price_of(t, r));
                        ++out.shop_parts;
                    }
                    continue;
                }
                Bytes b(r.begin(), r.end());
                fit_part(t, b, db, *car, *tmpl);
                take(t, b, tmpl->label);
                ++out.shop_parts;
            }
        }
        if (out.shop_parts) {
            out.shop_from = tmpl->label;
            std::string nm = utf8(tmpl->latin_name, jis);
            out.notes.push_back("its disc sells no tuning parts for it: the tune shop sells it those of the " +
                                ini_safe(nm.empty() ? tmpl->label : nm) + " (" + tmpl->label + "), the most alike car there, fitted to it (" +
                                std::to_string(out.shop_parts) + " parts)");
        }
    }

    // The car record: its parts under their new ids; parts it shares with other cars (steering, aids, "no turbo") go in
    // as shared records.
    CarRecordFile::Record car_rec;
    car_rec.table = TableId::Car;
    car_rec.label = new_label;
    car_rec.bytes.assign(car->bytes.begin(), car->bytes.begin() + std::ptrdiff_t(std::min<size_t>(car->bytes.size(), kCarRecordSize)));
    put64(car_rec.bytes, 0, new_code);
    for (size_t i = 0; i < kCarPartTables.size(); ++i) {
        const u64 id = car->parts[i];
        if (!id) continue;
        if (auto it = remap.find(id); it != remap.end()) put64(car_rec.bytes, 8 + i * 8, it->second);
        else if (kCarPartTables[i] >= 0) shared.insert({kCarPartTables[i], id});
    }
    for (auto [t, id] : shared) {
        if (!id) continue;
        const ByteSpan* r = db.record(TableId(t), id);
        if (!r) continue;
        CarRecordFile::Record rec;
        rec.table = TableId(t);
        rec.label = db.label(id);
        if (rec.label.empty()) continue;
        rec.bytes.assign(r->begin(), r->begin() + std::ptrdiff_t(std::min<size_t>(r->size(), kGt3RecordSize[size_t(t)])));
        file.records.push_back(std::move(rec));
    }
    file.records.push_back(std::move(car_rec));

    // What GT3's rules leave out of a Gran Turismo Concept car.
    if (auto dt = db.drivetrain(car->parts[13]); dt && dt->drive_type > 4)
        out.notes.push_back("its drive layout " + std::to_string(dt->drive_type) + " is not one of GT3's: GT3's rules race it as their own");
    if (const ByteSpan* ge = db.record(TableId::Gear, car->parts[18]); ge && ge->size() > 0x30 && (*ge)[0x30])
        out.notes.push_back("its gearbox is a stepless or automatic one on its disc; GT3's rules shift it as a manual one");

    // The files.
    host_make_dir(mod_dir);
    const std::string ini_rel = "cars/" + new_label + ".ini", rec_rel = "cars/" + new_label + ".records";
    if (!write_file(mod_dir, rec_rel, serialize_car_records(file), out.files)) return fail("cannot write " + mod_dir + "/" + rec_rel);
    for (const auto& [m, nm] : models)
        for (const char* dir : {"cars/day/", "cars/eve/", "cars/night/", "menu/cars/"})
            if (std::optional<Bytes> b = read(std::string(dir) + m); b && !b->empty())
                if (!write_file(mod_dir, "files/" + std::string(dir) + nm, *b, out.files)) return fail("cannot write the model " + nm);
    for (u16 id : sounds) {
        char n[8];
        std::snprintf(n, sizeof(n), "%05u", unsigned(id));
        for (const char* dir : {"engine", "normal0", "normal1", "normal2", "normal3", "turbo0", "turbo1", "turbo2", "turbo3"})
            if (std::optional<Bytes> b = read(std::string("sound/") + dir + "/" + n); b && !b->empty())
                if (!write_file(mod_dir, std::string("sounds/") + dir + "/" + n, *b, out.files)) return fail("cannot write its sounds");
    }

    // Its paints: the disc's colour list of its model, by name and colour, in the list's order (the model's colour variants).
    std::vector<std::pair<std::string, u32>> paints;
    if (std::optional<Bytes> cc = read("database/carcolor.db")) {
        race::CarColors colors;
        colors.parse(*cc);
        std::map<u32, std::string> names; // id -> the colour's first name (carcolor.sdb, in Latin letters)
        if (std::optional<Bytes> sdb = read("database/carcolor.sdb")) {
            try {
                db::StringTable st = db::StringTable::parse(*sdb);
                ByteSpan d(*cc);
                const u32 defs = rd32(d, 0x10), n = rd32(d, defs);
                for (u32 i = 0; i < n && defs + 4 + 16 * (i + 1) <= d.size(); ++i) {
                    const size_t o = defs + 4 + 16 * size_t(i);
                    names[rd32(d, o)] = st.at(rd32(d, o + 4));
                }
            } catch (const FormatError&) {
            }
        }
        if (const std::vector<u32>* ids = colors.ids(db.model_of(*car)))
            for (size_t i = 0; i < ids->size(); ++i) {
                std::string nm = ini_safe(utf8(names[(*ids)[i]], jis));
                if (nm.empty()) nm = "Color " + std::to_string(i + 1);
                paints.push_back({nm, colors.rgb((*ids)[i])});
            }
    }

    // The definition.
    const u8 category = car->bytes.size() > 0xF1 ? car->bytes[0xF1] : 0;
    std::string name = ini_safe(utf8(car->latin_name, jis));
    if (name.empty()) name = label; // (a car without a name on its disc)
    std::string ini;
    ini += "; " + name + ", exported from " + source + " with GT3 Revisited.\n";
    ini += "; It holds that disc's data for this car (records, models, sounds): for your own use.\n";
    for (const std::string& n : out.notes) ini += "; Note: " + n + ".\n";
    ini += "\n[car]\n";
    ini += "label = " + new_label + "\n";
    ini += "name = " + name + "\n";
    ini += "maker = " + ini_safe(utf8(car->maker, jis)) + "\n";
    if (car->year) ini += "year = " + std::to_string(car->year) + "\n";
    ini += "price = " + std::to_string(car->price) + "\n";
    ini += std::string("category = ") + (category == 1 ? "race" : category == 2 ? "rally" : "production") + "\n";
    ini += "records = " + rec_rel + "        ; every record of the car (no donor)\n";
    if (!sounds.empty()) ini += "\n[sound]\nbank = sounds                 ; its engine sounds (sounds/engine/<id>...)\n";
    ini += "\n[dealer]\n";
    if (!car->price) ini += "for_sale = no                 ; no price on its disc\n";
    else ini += "for_sale = yes\n";
    ini += "\n[arcade]\nclass = auto\n";
    if (!paints.empty()) {
        ini += "\n[paints]                      ; its model's colours, in order\n";
        for (const auto& [nm, rgb] : paints) {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "%02X%02X%02X", rgb & 0xFF, rgb >> 8 & 0xFF, rgb >> 16 & 0xFF);
            ini += nm + " = #" + hex + "\n";
        }
    }
    if (!write_text(mod_dir + "/" + ini_rel, ini)) return fail("cannot write " + mod_dir + "/" + ini_rel);
    out.files.push_back(ini_rel);

    const std::string mod_ini = mod_dir + "/mod.ini";
    if (!host_path_exists(mod_ini)) {
        std::string m = "[mod]\nname = Cars from " + ini_safe(source) + "\nversion = 1\nauthor = exported with GT3 Revisited\n";
        m += "; every disc but the one they came from\ndiscs = !" + std::string(disc_key) + "\n";
        m += "description = Cars exported from " + ini_safe(source) + ". They hold that disc's data: for your own use.\n";
        if (!write_text(mod_ini, m)) return fail("cannot write " + mod_ini);
        out.files.push_back("mod.ini");
    }
    return out;
}

} // namespace gt3r::data
