#include "game/replay_theater.h"

#include "base/log.h"
#include "game/gt_mode.h"
#include "game/gt_mode_menu.h"
#include "game/net_car.h"
#include "race/demo_replay.h"
#include "race/replay_library.h"
#include "vfs/vfs.h"

#include <cmath>
#include <ctime>

namespace gt3r::game {

namespace {

using menu::flow::TheaterEntry;

const data::CarDatabase* database() {
    GtModeSession* s = gt_mode_session();
    return s ? &s->gt().db() : nullptr;
}

bool imperial() {
    GtModeSession* s = gt_mode_session();
    return s ? s->gt().data().imperial : Disc::current_db_suffix() == "_us";
}

std::string course_name(const data::CarDatabase* db, const std::string& label) {
    if (db)
        for (const data::Course& c : db->courses())
            if (c.label == label && !c.name.empty()) return c.name;
    return label;
}

std::string car_name(const data::CarDatabase* db, const std::string& label) {
    if (db) {
        std::string l = label;
        if (const data::CarVariant* v = db->variant(l)) l = v->car;
        if (const data::Car* c = db->car(l)) return c->name.empty() ? l : c->name;
    }
    return label;
}

// "112HP" (the NTSC-U data set) or "112PS".
std::string power_text(float ps) {
    if (!(ps > 0)) return "---";
    return std::to_string(display_power(ps, imperial())) + power_unit(imperial());
}

std::string record_text(double s) { return s >= 0 ? format_time_ms(u32(std::llround(s * 1000.0))) : "-'--.---"; }

// MM/DD/YYYY as the US menus show dates.
std::string date_text(const race::ReplayEntry& e) {
    std::string d = race::replay_date(e); // YYYY/MM/DD
    if (d.size() != 10) return "--/--/----";
    return d.substr(5, 2) + "/" + d.substr(8, 2) + "/" + d.substr(0, 4);
}

} // namespace

std::vector<std::string> demonstration_codes() {
    std::vector<std::string> out;
    for (const char* cls : {"0B", "0A", "IB", "IA", "0S", "0R"})
        for (int i = 1; i <= 8; ++i) out.push_back(std::string("L") + cls + "000" + std::to_string(i));
    for (int i = 1; i <= 10; ++i) {
        char b[16];
        std::snprintf(b, sizeof b, "AT%02d0001", i);
        out.push_back(b);
    }
    return out;
}

std::string demonstration_mode(const std::string& code) {
    if (code.size() == 8 && code.rfind("AT", 0) == 0) return "Time Trial " + std::to_string(std::atoi(code.substr(2, 2).c_str()));
    if (code.size() == 7 && code[0] == 'L') {
        std::string cls = code.substr(1, 2);
        if (cls[0] == '0') cls = cls.substr(1);
        return "License " + cls + "-" + std::to_string(std::atoi(code.substr(3).c_str()));
    }
    return code;
}

std::string demonstration_file(const std::string& code) {
    if (code.size() == 8 && code.rfind("AT", 0) == 0) return "TT" + std::to_string(std::atoi(code.substr(2, 2).c_str())) + "_demo";
    if (code.size() == 7 && code[0] == 'L') {
        std::string cls = code.substr(1, 2);
        if (cls[0] == '0') cls = cls.substr(1);
        return cls + std::to_string(std::atoi(code.substr(3).c_str())) + "_demo";
    }
    return code;
}

std::vector<TheaterEntry> replay_theater_entries(int kind, const std::string& replay_dir, const Vfs* vfs) {
    std::vector<TheaterEntry> out;
    const data::CarDatabase* db = database();
    if (kind == 0) {
        for (const race::ReplayEntry& e : race::list_replay_entries(replay_dir)) {
            const race::Replay& r = e.header;
            TheaterEntry t;
            t.icon = TheaterEntry::Icon::Replay;
            t.id = e.path;
            t.deletable = true;
            float ps = r.power_ps;
            if (!(ps > 0) && db)
                if (std::optional<sim::CarSpec> sp = sim::build_car_spec(*db, r.car)) ps = spec_power_ps(*sp);
            const double record = r.best_lap >= 0 ? r.best_lap : r.total_time;
            t.texts = {{"course", course_name(db, r.course)},
                       {"car", car_name(db, r.car) + (r.car2.empty() ? "" : " / " + car_name(db, r.car2))},
                       {"power", power_text(ps)},
                       {"time", record_text(record)},
                       {"laps", r.laps > 0 ? std::to_string(r.laps) : "-"},
                       {"mode", race::replay_kind_name(r.kind)},
                       {"date", date_text(e)},
                       {"size", race::replay_size_text(e.size)},
                       {"comment", race::replay_display_name(e)},
                       {"course_id", r.course}};
            out.push_back(std::move(t));
        }
        return out;
    }
    // The disc's demonstrations (licence tests and arcade time trials), as the original's list.
    for (const std::string& code : demonstration_codes()) {
        if (!vfs || !vfs->exists("demo/" + code)) continue;
        const data::Event* ev = db ? db->event(code) : nullptr;
        if (db && !ev) continue;
        TheaterEntry t;
        t.icon = code[0] == 'L' ? TheaterEntry::Icon::License : TheaterEntry::Icon::Best;
        t.id = code;
        double rec = -1;
        if (std::optional<Bytes> b = vfs->read("demo/" + code))
            if (std::optional<race::DemoReplay> d = race::DemoReplay::parse(*b)) rec = d->time_ms / 1000.0;
        std::string car = ev && !ev->entries.empty() ? ev->entries[0] : std::string();
        float ps = 0;
        if (db && !car.empty())
            if (std::optional<sim::CarSpec> sp = sim::build_car_spec(*db, car)) ps = spec_power_ps(*sp);
        t.texts = {{"course", ev ? course_name(db, ev->course) : std::string()},
                   {"car", car.empty() ? std::string() : car_name(db, car)},
                   {"power", power_text(ps)},
                   {"time", record_text(rec)},
                   {"mode", demonstration_mode(code)},
                   {"date", "--/--/----"},
                   {"size", "----"},
                   {"comment", demonstration_file(code)},
                   {"course_id", ev ? ev->course : std::string()}};
        out.push_back(std::move(t));
    }
    return out;
}

void install_replay_theater(std::string data_dir, const Vfs* vfs) {
    menu::flow::TheaterBackend b;
    const std::string dir = race::replay_dir(data_dir);
    b.list = [dir, vfs](int kind) { return replay_theater_entries(kind, dir, vfs); };
    b.remove = [](const TheaterEntry& e) { return e.deletable && race::delete_replay_file(e.id); };
    menu::flow::set_theater_backend(std::move(b));
}

} // namespace gt3r::game
