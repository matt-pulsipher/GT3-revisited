// gt3tool database commands: dump the paramdb tables, print car specs.
//
//   gt3tool <iso> db <outdir> [suffix]   CSV per table + typed cars/engines/events CSVs + cars.json
//   gt3tool <iso> car <label|model> [suffix]
//
// Output is for study only; never commit it (it is game data).

#include "tool.h"

#include "formats/db/gt_db.h"
#include "formats/text/charset.h"
#include "game/data/car_database.h"

#include <cstdio>
#include <set>
#include <sstream>

namespace gt3r::tool {

namespace {

// Names are stored in the game's text encoding; convert for output.
std::optional<text::JisTable> load_jis(Volume& vol) {
    if (const Volume::Entry* e = vol.find("font/jis2uni.dat")) {
        try {
            return text::JisTable::parse(vol.read(*e));
        } catch (const std::exception&) {
        }
    }
    return std::nullopt;
}

std::string utf8(const std::string& s, const std::optional<text::JisTable>& jis) {
    return text::game_text_to_utf8(s, jis ? &*jis : nullptr);
}


using data::CarDatabase;
using data::TableId;

std::string csv_escape(const std::string& s) {
    bool quote = s.find_first_of(",\"\n") != std::string::npos;
    std::string out;
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return quote ? "\"" + out + "\"" : out;
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out += '\\', out += char(c);
        else if (c < 0x20) out += ' ';
        else if (c >= 0x80) out += '?'; // non-ASCII (EUC-JP in the Japanese set)
        else out += char(c);
    }
    return out;
}

bool write_text(const std::string& path, const std::string& text) {
    return save(path, ByteSpan(reinterpret_cast<const u8*>(text.data()), text.size()));
}

// Generic dump: u64 slots that mostly resolve to labels become label
// columns; every other byte becomes a decimal u8 column named b<offset>.
std::string dump_table(const CarDatabase& d, const db::Table& t) {
    size_t rs = t.record_size;
    std::vector<bool> is_id(rs, false);
    for (size_t o = 0; o + 8 <= rs; o += 8) {
        size_t nonzero = 0, named = 0;
        for (ByteSpan r : t.records) {
            u64 v = rd64(r, o);
            if (!v) continue;
            ++nonzero;
            if (!d.label(v).empty()) ++named;
        }
        if (o == 0 || (nonzero && named * 2 >= nonzero))
            for (size_t k = 0; k < 8; ++k) is_id[o + k] = true;
    }
    std::ostringstream out;
    out << "index";
    for (size_t o = 0; o < rs;) {
        if (is_id[o]) {
            out << (o == 0 ? ",label" : ",id" + std::to_string(o));
            o += 8;
        } else {
            out << ",b" << o++;
        }
    }
    out << "\n";
    for (size_t i = 0; i < t.size(); ++i) {
        ByteSpan r = t.records[i];
        out << i;
        for (size_t o = 0; o < rs;) {
            if (is_id[o]) {
                u64 v = rd64(r, o);
                std::string l = d.label(v);
                char hex[24];
                std::snprintf(hex, sizeof(hex), "#%016llx", (unsigned long long)v);
                out << "," << (v == 0 ? std::string("0") : l.empty() ? std::string(hex) : csv_escape(l));
                o += 8;
            } else {
                out << "," << int(r[o++]);
            }
        }
        out << "\n";
    }
    return out.str();
}

std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof(b), f, v);
    return b;
}

int cmd_db(Disc& disc, const Args& a) {
    std::string out = a[0], suffix = a.size() > 1 ? (a[1] == "jp" ? "" : a[1]) : disc.info().data.db_suffix();
    CarDatabase d;
    if (!d.load(disc.volume(), suffix)) {
        std::fprintf(stderr, "error: %s\n", d.error().c_str());
        return 1;
    }
    for (int i = 0; i < int(TableId::Count); ++i) {
        const db::Table& t = d.table(TableId(i));
        char name[64];
        std::snprintf(name, sizeof(name), "/%02d_%s.csv", i, data::table_name(TableId(i)));
        if (!write_text(out + name, dump_table(d, t))) return 1;
        std::printf("%2d %-18s %4zu records x %3u bytes\n", i, data::table_name(TableId(i)), t.size(), t.record_size);
    }
    // Standalone GTDT files.
    for (const char* f : {"racemode", "racedetail"}) {
        const Volume::Entry* e = disc.volume().find(std::string("database/") + f + ".db");
        if (!e) continue;
        Bytes b = disc.volume().read(*e);
        db::Table t = db::Table::parse(b);
        if (!write_text(out + "/" + f + ".csv", dump_table(d, t))) return 1;
        std::printf("   %-18s %4zu records x %3u bytes\n", f, t.size(), t.record_size);
    }

    std::ostringstream cars, json, engines, events;
    cars << "label,name,maker,year,price,model,drive,layout,aspiration,displacement,power_ps,power_rpm,torque_kgfm,"
            "torque_rpm,weight_kg,length_mm,width_mm,height_mm,wheelbase_mm,front_weight_pct,gears,final_drive,"
            "ratios,front_tire,rear_tire\n";
    json << "[\n";
    bool first = true;
    auto jis = load_jis(disc.volume());
    for (const data::Car& c : d.cars()) {
        auto s = d.summarize(c);
        s.name = utf8(s.name, jis);
        s.maker = utf8(s.maker, jis);
        std::string ratios;
        for (float r : s.ratios) ratios += (ratios.empty() ? "" : " ") + fmt("%.3f", r);
        cars << csv_escape(c.label) << "," << csv_escape(s.name) << "," << csv_escape(s.maker) << "," << s.year << ","
             << s.price << "," << s.model << "," << s.drive << "," << csv_escape(s.layout) << "," << s.aspiration << ","
             << s.displacement << "," << s.power_ps << "," << s.power_rpm << "," << fmt("%.2f", s.torque_kgfm) << ","
             << s.torque_rpm << "," << s.weight_kg << "," << s.length_mm << "," << s.width_mm << "," << s.height_mm
             << "," << s.wheelbase_mm << "," << int(s.front_weight_pct) << "," << int(s.gears) << ","
             << fmt("%.3f", s.final_drive) << "," << ratios << "," << s.front_tire << "," << s.rear_tire << "\n";
        json << (first ? "" : ",\n") << "  {\"label\":\"" << json_escape(c.label) << "\",\"name\":\""
             << json_escape(s.name) << "\",\"maker\":\"" << json_escape(s.maker) << "\",\"year\":" << s.year
             << ",\"price\":" << s.price << ",\"model\":\"" << s.model << "\",\"drive\":\"" << s.drive
             << "\",\"power_ps\":" << s.power_ps << ",\"torque_kgfm\":" << fmt("%.2f", s.torque_kgfm)
             << ",\"weight_kg\":" << s.weight_kg << "}";
        first = false;
    }
    json << "\n]\n";

    engines << "label,car,layout,valvetrain,aspiration,displacement,power_ps,power_rpm,torque_kgfm,torque_rpm,"
               "b42_scale,b43,b44,b45,b46,sound_id,curve_peak_ps,curve\n";
    for (ByteSpan r : d.table(TableId::Engine).records) {
        auto e = d.engine(rd64(r, 0));
        std::string curve;
        for (size_t i = 0; i < e->rpm.size(); ++i)
            curve += (i ? " " : "") + std::to_string(e->rpm[i]) + ":" + fmt("%.2f", e->torque[i]);
        engines << e->part.label << "," << e->part.car << "," << e->layout << "," << e->valvetrain << ","
                << e->aspiration << "," << e->displacement << "," << e->power_ps << "," << e->power_rpm << ","
                << fmt("%.2f", e->torque_kgfm) << "," << e->torque_rpm << "," << int(e->torque_scale_pct) << ","
                << int(e->unknown_43) << "," << int(e->unknown_44) << "," << int(e->rpm_limit_x100) << ","
                << int(e->unknown_46) << "," << e->sound_id << "," << fmt("%.1f", e->curve_peak_ps()) << "," << curve << "\n";
    }

    // Prizes as stored: hundreds of credits for places 1..6 (+0x1D4; the game also scales them
    // x100 in Japanese).
    events << "label,course,regulation,race_mode,laps,cars,prize1,prize2,prize3,prize4,prize5,prize6,entries\n";
    for (const data::Event& e : d.events()) {
        std::string entries;
        for (const auto& x : e.entries) entries += (entries.empty() ? "" : " ") + x;
        events << e.label << "," << e.course << "," << e.regulation << "," << e.race_mode << "," << int(e.laps)
               << "," << int(e.cars_in_race);
        for (size_t k = 0; k < 6; ++k) events << "," << rd16(e.bytes, 0x1D4 + 2 * k);
        events << "," << entries << "\n";
    }
    std::ostringstream courses;
    courses << "label,name,surface\n";
    for (const auto& c : d.courses()) courses << c.label << "," << csv_escape(c.name) << "," << int(c.surface) << "\n";

    if (!write_text(out + "/cars.csv", cars.str()) || !write_text(out + "/cars.json", json.str()) ||
        !write_text(out + "/engines.csv", engines.str()) || !write_text(out + "/events.csv", events.str()) ||
        !write_text(out + "/courses.csv", courses.str()))
        return 1;
    std::printf("%zu cars, %zu variants, %zu events, %zu courses, %zu regulations -> %s\n", d.cars().size(),
                d.variants().size(), d.events().size(), d.courses().size(), d.regulations().size(), out.c_str());
    return 0;
}

int cmd_car(Disc& disc, const Args& a) {
    CarDatabase d;
    if (!d.load(disc.volume(), a.size() > 1 ? (a[1] == "jp" ? "" : a[1]) : disc.info().data.db_suffix())) {
        std::fprintf(stderr, "error: %s\n", d.error().c_str());
        return 1;
    }
    const data::Car* c = d.car(a[0]);
    if (!c) c = d.car_by_model(a[0]);
    if (!c) {
        std::fprintf(stderr, "no car '%s'\n", a[0].c_str());
        return 1;
    }
    auto s = d.summarize(*c);
    auto jis = load_jis(disc.volume());
    std::printf("%s  [%s, model %s]\n%s %u, %u Cr\n", utf8(s.name, jis).c_str(), c->label.c_str(), s.model.c_str(),
                utf8(s.maker, jis).c_str(), s.year, s.price);
    std::printf("Engine   %s %s %s, %u PS @ %s, %.1f kgfm @ %s\n", s.layout.c_str(), s.aspiration.c_str(),
                s.displacement.c_str(), s.power_ps, s.power_rpm.c_str(), s.torque_kgfm, s.torque_rpm.c_str());
    std::printf("Body     %u kg, %u x %u x %u mm, wheelbase %u mm, front %u%%, %s\n", s.weight_kg, s.length_mm,
                s.width_mm, s.height_mm, s.wheelbase_mm, s.front_weight_pct, s.drive.c_str());
    std::printf("Gearbox  %u speed, final %.3f:", s.gears, s.final_drive);
    for (float r : s.ratios) std::printf(" %.3f", r);
    std::printf("\nTires    F %s / R %s\n", s.front_tire.c_str(), s.rear_tire.c_str());
    if (auto e = d.engine(c->parts[6])) {
        std::printf("Curve   ");
        for (size_t i = 0; i < e->rpm.size(); ++i) std::printf(" %u:%.1f", e->rpm[i], e->torque[i]);
        auto snd = d.engine_sound(*c);
        std::printf("\n         peak %.1f PS from curve, scale byte %u, sound %05u variant %u\n", e->curve_peak_ps(),
                    e->torque_scale_pct, snd.id, snd.variant);
    }
    std::printf("Parts   ");
    for (size_t i = 0; i < c->parts.size(); ++i)
        if (c->parts[i]) std::printf(" %s", d.label(c->parts[i]).c_str());
    std::printf("\n");
    for (int t : {4, 7, 8, 9, 10, 11, 12, 14, 15, 16, 17, 18, 19, 20, 21, 0, 1, 28}) {
        auto parts = d.parts_for(TableId(t), c->label);
        if (parts.empty()) continue;
        std::printf("  %-16s", data::table_name(TableId(t)));
        for (const auto& p : parts) std::printf(" [%u] %u Cr", p.stage, p.price);
        std::printf("\n");
    }
    return 0;
}

} // namespace

GT3TOOL_COMMAND(db, "db", "<outdir> [_us|_eu|jp]", "dump database tables to CSV/JSON (scratch only)", 1, cmd_db);
GT3TOOL_COMMAND(car, "car", "<label|model> [_us|_eu|jp]", "print a car's catalogue specs", 1, cmd_car);

} // namespace gt3r::tool
