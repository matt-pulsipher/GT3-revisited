#include "game/data/mod_events.h"

#include "base/file.h"
#include "base/log.h"
#include "formats/db/gt_db.h"
#include "game/gt_parts.h"
#include "sim/exact/params_build.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace gt3r::data {

namespace {

// Event record fields (docs/formats/database.md "Event (table 32)").
constexpr size_t kLabel = 0x000, kCourse = 0x008, kEntries = 0x010, kPaints = 0x110, kRegulation = 0x190;
constexpr size_t kLaps = 0x199, kLicense = 0x19B, kCars = 0x1B5, kSpecial = 0x1D2, kPrize = 0x1D4, kBonus = 0x1E2;
constexpr size_t kTimeLimit = 0x1E4, kLeague = 0x1E5, kClass = 0x1F4, kAspiration = 0x1F5, kDrive = 0x1F6;
constexpr size_t kMinSize = 0x1F8;
constexpr size_t kDriving = 0x19C, kDrivingEnd = 0x1B4; // CPU cars' grip and driver percentages per driveline class (ai §9.2)
constexpr size_t kPoolSize = 32;
constexpr u8 kDirtTires = 9; // +0x1D2: dirt tyres required
constexpr u8 kSurfaceDirt = 2; // course +0x08

void put8(Bytes& b, size_t o, u8 v) {
    if (o < b.size()) b[o] = v;
}
void put16(Bytes& b, size_t o, u16 v) {
    if (o + 2 <= b.size()) std::memcpy(b.data() + o, &v, 2);
}
void put32(Bytes& b, size_t o, u32 v) {
    if (o + 4 <= b.size()) std::memcpy(b.data() + o, &v, 4);
}
void put64(Bytes& b, size_t o, u64 v) {
    if (o + 8 <= b.size()) std::memcpy(b.data() + o, &v, 8);
}

// Value text without a trailing " ; comment" / " # comment".
std::string value_text(std::string_view v) {
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) {
            v = v.substr(0, i);
            break;
        }
    return std::string(trim(v));
}

std::optional<long> integer(const std::string& s) {
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end) return std::nullopt;
    return v;
}

template <size_t N>
int keyword(const std::string& v, const char* const (&names)[N], const char* what, const std::string& file_key,
            std::vector<std::string>* warnings) {
    std::string l = to_lower(v);
    if (l.empty()) return 0;
    for (size_t i = 0; i < N; ++i)
        if (l == names[i]) return int(i);
    if (warnings) warnings->push_back(file_key + ": unknown " + what + " '" + v + "'");
    return 0;
}

const char* const kLicenses[] = {"none", "b", "a", "ib", "ia", "s", "r"};
const char* const kDrives[] = {"any", "ff", "fr", "4wd", "mr", "rr"};
const char* const kAspirations[] = {"any", "na", "turbo"};
const char* const kClasses[] = {"any", "race", "tuned", "stock"};
const char* const kSkills[] = {"league", "easy", "normal", "hard", "professional"};

// "car:color, car, tune:<name>:color" (a carcolor id; none = drawn at the race start).
std::vector<ModEventCar> parse_lineup(const std::string& text) {
    std::vector<ModEventCar> out;
    for (std::string_view part : split(text, ',')) {
        std::string_view p = trim(part);
        if (p.empty()) continue;
        std::vector<std::string_view> f = split(p, ':');
        ModEventCar c;
        c.label = std::string(trim(f[0]));
        if (c.label == "tune" && f.size() > 1) { // "tune:<name>[:color]"
            c.label += ":" + std::string(trim(f[1]));
            f.erase(f.begin());
        }
        if (f.size() > 1 && !trim(f[1]).empty()) c.color = u32(std::strtoul(std::string(trim(f[1])).c_str(), nullptr, 0));
        if (!c.label.empty()) out.push_back(std::move(c));
    }
    return out;
}

std::string format_lineup(const std::vector<ModEventCar>& cars) {
    std::string s;
    for (const ModEventCar& c : cars) {
        if (!s.empty()) s += ", ";
        s += c.label;
        if (c.color != 0xFFFFFFFFu) s += ":" + std::to_string(c.color);
    }
    return s;
}

bool yes(const std::string& v) {
    std::string l = to_lower(v);
    return l == "yes" || l == "on" || l == "true" || l == "1";
}

// A path in the file: relative to the mod folder (the parent of events/), else as it is.
std::string mod_path(const std::string& def_file, const std::string& p) {
    if (p.empty() || p[0] == '/' || p.find("://") != std::string::npos) return p;
    std::string dir = def_file.substr(0, def_file.find_last_of('/') + 1);
    const std::string sub = "/events/";
    if (dir.size() >= sub.size() && dir.compare(dir.size() - sub.size(), sub.size(), sub) == 0)
        dir.resize(dir.size() - sub.size() + 1);
    return dir + p;
}

u64 fnv(std::string_view s) {
    u64 h = 0xcbf29ce484222325ull;
    for (char c : s) h = (h ^ u8(c)) * 0x100000001b3ull;
    return h;
}

// The disc event a race is cloned from: the definition's base, else a GT Mode race of the disc (a
// championship's round for a championship).
const Event* base_event(const CarDatabase& db, const ModEventDef& def) {
    auto usable = [](const Event* e) { return e && e->bytes.size() >= kMinSize && e->race_mode == "RACE_MODE_SINGLE"; };
    if (const Event* e = db.event(def.base); !def.base.empty() && usable(e)) return e;
    auto ends = [](const std::string& s, const char* t) {
        size_t n = std::strlen(t);
        return s.size() >= n && s.compare(s.size() - n, n, t) == 0;
    };
    const Event* any = nullptr;
    for (const Event& e : db.events()) {
        if (e.label.empty() || e.label[0] != 'G' || !usable(&e) || e.cars_in_race < 2) continue;
        if (!any) any = &e;
        if (ends(e.label, def.championship ? "_0501" : "_0001")) return &e;
    }
    return any;
}

bool known_car(const CarDatabase& db, const std::string& label) { return db.car(label) || db.variant(label); }

} // namespace

std::optional<std::array<u64, 27>> mod_event_tune_parts(const CarDatabase& db, const ModEventTune& t,
                                                        std::vector<std::string>* left_out) {
    const Car* car = db.car(t.car);
    if (!car) return std::nullopt;
    std::array<u64, 27> parts = car->parts;
    for (const std::string& item : t.parts) {
        const game::ShopItem* si = game::shop_item(item);
        std::optional<game::PartOffer> p = si ? game::find_part(db, t.car, si->goods) : std::nullopt;
        const int slot = si ? game::part_slot(game::goods_info(si->goods).table) : -1;
        if (!p || slot < 0) {
            if (left_out) left_out->push_back(item);
            continue;
        }
        parts[size_t(slot)] = p->id;
        if (const int rear = game::rear_tire_goods(si->goods); rear >= 0) // (tyres come as a pair)
            if (std::optional<game::PartOffer> rp = game::find_part(db, t.car, rear))
                if (const int rs = game::part_slot(game::goods_info(rear).table); rs >= 0) parts[size_t(rs)] = rp->id;
    }
    return parts;
}

std::string mod_event_skill_event(const CarDatabase& db, int skill, bool dirt) {
    static const char kLevel[] = "ENHP";
    std::string code = std::string("A") + kLevel[std::clamp(skill, 0, 3)] + (dirt ? "AD" : "ST") + "0001";
    return db.event(code) ? code : std::string();
}

const char* license_key(int v) { return kLicenses[std::clamp(v, 0, 6)]; }
const char* drivetrain_key(int v) { return kDrives[std::clamp(v, 0, 5)]; }
const char* aspiration_key(int v) { return kAspirations[std::clamp(v, 0, 2)]; }
const char* car_class_key(int v) { return kClasses[std::clamp(v, 0, 3)]; }

std::optional<ModEventDef> parse_mod_event(std::string_view text, const std::string& id, std::string* error,
                                           std::vector<std::string>* warnings) {
    IniDocument ini = IniDocument::parse(text);
    ModEventDef d;
    d.id = id;
    auto get = [&](const char* sec, const char* key) { return value_text(ini.get(sec, key).value_or("")); };
    d.title = get("event", "title");
    d.description = get("event", "description");
    d.picture = get("event", "picture");
    d.league = get("event", "league");
    d.base = get("event", "base");
    d.championship = yes(get("event", "championship"));
    {
        size_t k = 0;
        const std::string prizes = get("event", "prizes"); // (split's views point into it)
        for (std::string_view p : split(prizes, ','))
            if (k < d.prizes.size()) d.prizes[k++] = u32(std::max(0L, integer(std::string(trim(p))).value_or(0)));
    }
    d.bonus = u32(std::max(0L, integer(get("event", "bonus")).value_or(0)));
    d.prize_car = get("event", "prize_car");
    d.prize_paint = int(integer(get("event", "prize_paint")).value_or(-1));
    d.license = keyword(get("entry", "license"), kLicenses, "licence", "[entry] license", warnings);
    d.drivetrain = keyword(get("entry", "drivetrain"), kDrives, "drivetrain", "[entry] drivetrain", warnings);
    d.aspiration = keyword(get("entry", "aspiration"), kAspirations, "aspiration", "[entry] aspiration", warnings);
    d.car_class = keyword(get("entry", "cars"), kClasses, "car type", "[entry] cars", warnings);
    const std::string allowed = get("entry", "allowed");
    for (std::string_view c : split(allowed, ','))
        if (!trim(c).empty()) d.allowed.emplace_back(trim(c));
    d.field = parse_lineup(get("field", "opponents"));
    for (int k = 1; k <= ModEventDef::kMaxRaces; ++k) {
        const std::string sec = "race" + std::to_string(k);
        if (!ini.find(sec)) break;
        ModEventRace r;
        r.course = get(sec.c_str(), "course");
        r.laps = int(std::clamp<long>(integer(get(sec.c_str(), "laps")).value_or(3), 1, 99));
        r.opponents = parse_lineup(get(sec.c_str(), "opponents"));
        r.cars = int(std::clamp<long>(integer(get(sec.c_str(), "cars")).value_or(0), 0, ModEventDef::kMaxOpponents));
        r.skill = keyword(get(sec.c_str(), "skill"), kSkills, "skill", "[" + sec + "] skill", warnings) - 1;
        d.races.push_back(std::move(r));
    }
    for (const IniDocument::Section& sec : ini.sections()) {
        if (sec.name.rfind("tune:", 0) != 0) continue;
        ModEventTune t;
        t.name = sec.name.substr(5);
        t.car = get(sec.name.c_str(), "car");
        const std::string parts = get(sec.name.c_str(), "parts");
        for (std::string_view p : split(parts, ','))
            if (!trim(p).empty()) t.parts.emplace_back(trim(p));
        if (t.name.empty() || t.car.empty()) {
            if (warnings) warnings->push_back("[" + sec.name + "] needs a name and a car");
            continue;
        }
        d.tunes.push_back(std::move(t));
    }
    if (d.title.empty()) d.title = id;
    if (d.races.empty()) {
        if (error) *error = "no races ([race1] with a course)";
        return std::nullopt;
    }
    for (size_t k = 0; k < d.races.size(); ++k)
        if (d.races[k].course.empty()) {
            if (error) *error = "[race" + std::to_string(k + 1) + "] has no course";
            return std::nullopt;
        }
    return d;
}

std::string format_mod_event(const ModEventDef& d) {
    IniDocument ini;
    ini.set("event", "title", d.title);
    if (!d.description.empty()) ini.set("event", "description", d.description);
    if (!d.picture.empty()) ini.set("event", "picture", d.picture);
    if (!d.league.empty()) ini.set("event", "league", d.league);
    if (!d.base.empty()) ini.set("event", "base", d.base);
    ini.set("event", "championship", d.championship ? "yes" : "no");
    std::string prizes;
    for (u32 p : d.prizes) prizes += (prizes.empty() ? "" : ", ") + std::to_string(p);
    ini.set("event", "prizes", prizes);
    if (d.championship) ini.set("event", "bonus", std::to_string(d.bonus));
    if (!d.prize_car.empty()) {
        ini.set("event", "prize_car", d.prize_car);
        if (d.prize_paint >= 0) ini.set("event", "prize_paint", std::to_string(d.prize_paint));
    }
    ini.set("entry", "license", license_key(d.license));
    ini.set("entry", "drivetrain", drivetrain_key(d.drivetrain));
    ini.set("entry", "aspiration", aspiration_key(d.aspiration));
    ini.set("entry", "cars", car_class_key(d.car_class));
    if (!d.allowed.empty()) {
        std::string a;
        for (const std::string& c : d.allowed) a += (a.empty() ? "" : ", ") + c;
        ini.set("entry", "allowed", a);
    }
    ini.set("field", "opponents", format_lineup(d.field));
    for (size_t k = 0; k < d.races.size(); ++k) {
        const ModEventRace& r = d.races[k];
        const std::string sec = "race" + std::to_string(k + 1);
        ini.set(sec, "course", r.course);
        ini.set(sec, "laps", std::to_string(r.laps));
        ini.set(sec, "skill", kSkills[std::clamp(r.skill, -1, 3) + 1]);
        if (r.cars > 0) ini.set(sec, "cars", std::to_string(r.cars));
        if (!r.opponents.empty() && !d.championship) ini.set(sec, "opponents", format_lineup(r.opponents));
    }
    for (const ModEventTune& t : d.tunes) {
        const std::string sec = "tune:" + t.name;
        ini.set(sec, "car", t.car);
        std::string parts;
        for (const std::string& p : t.parts) parts += (parts.empty() ? "" : ", ") + p;
        ini.set(sec, "parts", parts);
    }
    return ini.serialize();
}

std::string mod_event_series(const std::string& mod, const std::string& id) {
    std::string s(1, kModEventPrefix);
    for (char c : to_lower(mod.empty() ? id : mod + "_" + id))
        s += (std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    // Long names: their start and a hash (codes stay short in saves and replays).
    constexpr size_t kMax = 40;
    if (s.size() > kMax) {
        char h[12];
        std::snprintf(h, sizeof h, "%08x", unsigned(fnv(s)));
        s = s.substr(0, kMax - 9) + "_" + h;
    }
    return s;
}

bool add_mod_event(CarDatabase& db, const ModEventDef& def, std::string* error) {
    auto fail = [&](std::string e) {
        if (error) *error = std::move(e);
        return false;
    };
    if (def.races.empty()) return fail("no races");
    if (def.races.size() > size_t(ModEventDef::kMaxRaces)) return fail("more than 10 races");
    const Event* base = base_event(db, def);
    if (!base) return fail("this disc has no GT Mode race to build it on");
    const Bytes tmpl(base->bytes.begin(), base->bytes.end());

    ModEvent ev;
    ev.def = def;
    ev.series = mod_event_series(def.mod, def.id);
    // The tunes: variant records of their cars with their parts, as the disc's opponents are.
    std::map<std::string, std::string> tuned; // "tune:<name>" -> its variant's label
    for (const ModEventTune& t : ev.def.tunes) {
        const Car* car = db.car(t.car);
        if (!car) {
            LOG_WARN("Mod event %s: tune %s: no car '%s' here, left out", def.id.c_str(), t.name.c_str(), t.car.c_str());
            continue;
        }
        std::vector<std::string> left_out;
        const std::array<u64, 27> parts = *mod_event_tune_parts(db, t, &left_out);
        for (const std::string& item : left_out)
            LOG_WARN("Mod event %s: tune %s: no part '%s' for %s, left out", def.id.c_str(), t.name.c_str(), item.c_str(), t.car.c_str());
        std::string label = ev.series + "_" + to_lower(t.name);
        for (char& c : label)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') c = '_';
        db.add_label(label);
        db.put_record(TableId::CarVariant, sim::exact::variant_record(db, label, t.car, parts));
        tuned["tune:" + t.name] = label;
    }
    auto to_variants = [&](std::vector<ModEventCar>& pool) {
        for (ModEventCar& c : pool)
            if (auto it = tuned.find(c.label); it != tuned.end()) c.label = it->second;
    };
    to_variants(ev.def.field);
    for (ModEventRace& r : ev.def.races) to_variants(r.opponents);
    if (!tuned.empty()) db.rebuild(); // (the pools' checks below look the variants up)

    // Cars this disc lacks drop out of the lineups (an event made with mod cars, shared without them).
    auto keep = [&](std::vector<ModEventCar>& cars, const std::string& where) {
        std::vector<ModEventCar> ok;
        for (ModEventCar& c : cars) {
            if (known_car(db, c.label)) ok.push_back(std::move(c));
            else LOG_WARN("Mod event %s: %s: no car '%s' here, left out", def.id.c_str(), where.c_str(), c.label.c_str());
        }
        if (ok.size() > size_t(ModEventDef::kMaxPool)) ok.resize(size_t(ModEventDef::kMaxPool));
        cars = std::move(ok);
    };
    keep(ev.def.field, "field");
    for (size_t k = 0; k < ev.def.races.size(); ++k) keep(ev.def.races[k].opponents, "race " + std::to_string(k + 1));
    if (!ev.def.prize_car.empty() && !db.car(ev.def.prize_car)) {
        LOG_WARN("Mod event %s: no prize car '%s' here, left out", def.id.c_str(), ev.def.prize_car.c_str());
        ev.def.prize_car.clear();
    }

    // Every race must run here before any record goes in.
    for (size_t k = 0; k < ev.def.races.size(); ++k) {
        const std::string& course = ev.def.races[k].course;
        if (std::none_of(db.courses().begin(), db.courses().end(), [&](const Course& c) { return c.label == course; }))
            return fail("race " + std::to_string(k + 1) + ": no course '" + course + "' on this disc");
        if (ev.lineup(k).empty()) return fail("race " + std::to_string(k + 1) + ": no opponents");
    }

    // The regulation: the cars it names (those this disc has).
    std::string regulation;
    std::vector<std::string> allowed;
    for (const std::string& c : ev.def.allowed)
        if (db.car(c)) allowed.push_back(c);
    if (!ev.def.allowed.empty()) {
        if (allowed.empty()) return fail("none of its allowed cars is on this disc");
        const db::Table& t = db.table(TableId::Regulation);
        if (t.records.empty()) return fail("this disc has no regulations to build one on");
        Bytes reg(t.records.front().begin(), t.records.front().end());
        const size_t slots = (reg.size() - 8) / 8;
        if (allowed.size() > slots) {
            LOG_WARN("Mod event %s: %zu allowed cars, only the first %zu fit", def.id.c_str(), allowed.size(), slots);
            allowed.resize(slots);
        }
        regulation = ev.series + "_reg";
        std::fill(reg.begin(), reg.end(), u8(0));
        put64(reg, 0, db::id_hash(regulation));
        for (size_t i = 0; i < allowed.size(); ++i) put64(reg, 8 + 8 * i, db::id_hash(allowed[i]));
        db.add_label(regulation);
        for (const std::string& c : allowed) db.add_label(c);
        db.put_record(TableId::Regulation, std::move(reg));
    }
    ev.def.allowed = allowed;

    auto hundreds = [](u32 cr) { return u16(std::min<u32>((cr + 50) / 100, 0xFFFF)); };
    const u64 none = db::id_hash("-");
    char num[8];
    if (ev.def.championship) {
        ev.champ = ev.series + "_0500";
        db.add_label(ev.champ);
    }
    for (size_t k = 0; k < ev.def.races.size(); ++k) {
        const ModEventRace& r = ev.def.races[k];
        const Course* course = nullptr;
        for (const Course& c : db.courses())
            if (c.label == r.course) course = &c;
        const std::vector<ModEventCar>& lineup = ev.lineup(k);
        std::snprintf(num, sizeof num, "_%04d", int(k) + (ev.def.championship ? 501 : 1));
        const std::string code = ev.series + num;
        Bytes b = tmpl;
        put64(b, kLabel, db::id_hash(code));
        put64(b, kCourse, db::id_hash(r.course));
        for (size_t i = 0; i < kPoolSize; ++i) {
            put64(b, kEntries + 8 * i, i < lineup.size() ? db::id_hash(lineup[i].label) : none);
            put32(b, kPaints + 4 * i, i < lineup.size() ? lineup[i].color : 0xFFFFFFFFu);
        }
        put64(b, kRegulation, regulation.empty() ? none : db::id_hash(regulation));
        put8(b, kLaps, u8(std::clamp(r.laps, 1, 99)));
        put8(b, kLicense, u8(std::clamp(ev.def.license, 0, 6)));
        // The cars it draws: the player and as many opponents as it asks for, at most the pool's and five.
        const int most = std::min<int>(int(lineup.size()), ModEventDef::kMaxOpponents);
        put8(b, kCars, u8(std::clamp(r.cars > 0 ? r.cars : most, 1, most) + 1));
        put8(b, kSpecial, course->surface == kSurfaceDirt ? kDirtTires : 0);
        for (size_t p = 0; p < 6; ++p) put16(b, kPrize + 2 * p, hundreds(ev.def.prizes[p]));
        put16(b, kBonus, ev.def.championship ? hundreds(ev.def.bonus) : 0);
        put8(b, kTimeLimit, 0);
        put8(b, kLeague, 0); // (no disc league: GtMode's league checks never see it)
        put8(b, kClass, u8(std::clamp(ev.def.car_class, 0, 3)));
        put8(b, kAspiration, u8(std::clamp(ev.def.aspiration, 0, 2)));
        put8(b, kDrive, u8(std::clamp(ev.def.drivetrain, 0, 5)));
        // The opponents' driving: an arcade race's percentages for every CPU car (a game race's own).
        if (r.skill >= 0)
            if (const Event* s = db.event(mod_event_skill_event(db, r.skill, course->surface == kSurfaceDirt)); s && s->bytes.size() >= kMinSize)
                std::copy(s->bytes.begin() + kDriving, s->bytes.begin() + kDrivingEnd, b.begin() + kDriving);
        db.add_label(code);
        db.put_record(TableId::Event, std::move(b));
        ev.races.push_back(code);
    }
    LOG_INFO("Mod event '%s' (%s): %zu race(s)%s, built on %s", ev.def.title.c_str(), ev.series.c_str(), ev.races.size(),
             ev.def.championship ? " as a championship" : "", base->label.c_str());
    db.add_mod_event(std::move(ev));
    return true;
}

void load_mod_event_files(CarDatabase& db, const std::vector<std::string>& files, const std::string& mod) {
    bool any = false;
    for (const std::string& f : files) {
        auto bytes = read_host_file(f);
        if (!bytes) {
            db.add_mod_car_error(f + ": cannot read");
            continue;
        }
        std::string name = f.substr(f.find_last_of('/') + 1);
        name = name.substr(0, name.size() - 4);
        std::string err;
        std::vector<std::string> warns;
        auto def = parse_mod_event(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), name, &err, &warns);
        for (const std::string& w : warns) LOG_WARN("Mod event %s: %s", f.c_str(), w.c_str());
        if (def) {
            def->mod = mod;
            def->file = f;
            def->picture = mod_path(f, def->picture);
        }
        if (!def || !add_mod_event(db, *def, &err)) {
            LOG_WARN("Mod event %s: %s", f.c_str(), err.c_str());
            db.add_mod_car_error(f + ": " + err);
            continue;
        }
        any = true;
    }
    if (any) db.rebuild();
}

} // namespace gt3r::data
