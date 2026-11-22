#include "game/data/car_export.h"

#include "base/config.h"
#include "base/file.h"
#include "formats/db/gt_db.h"
#include "formats/text/charset.h"
#include "game/data/mod_cars.h"
#include "race/paint.h"

#include <array>
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
                                    std::string* error) {
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
    auto take = [&](int t, ByteSpan r, const std::string& owner) {
        const std::string l = db.label(rd64(r, 0));
        size_t p = l.find(owner);
        std::string nl = p == std::string::npos
                             ? std::string(table_name(TableId(t))) + "_" + new_label + "_" + std::to_string(++unnamed)
                             : l.substr(0, p) + new_label + l.substr(p + owner.size());
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
