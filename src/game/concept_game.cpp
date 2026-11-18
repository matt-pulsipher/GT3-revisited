#include "game/concept_game.h"

#include "formats/db/gt_db.h"
#include "game/gt_parts.h"
#include "sim/car_spec.h"
#include "sim/exact/params_build.h"
#include "sim/exact/race_inputs.h"

namespace gt3r::game {

ConceptRules concept_rules(const GtData& d) {
    ConceptRules r;
    if (auto it = d.arcade_courses.find("SINGLE_RACE"); it != d.arcade_courses.end())
        for (const GtData::ArcadeCourse& c : it->second) r.courses.push_back(c.course);
    if (auto it = d.arcade_classes.find("CLASS_LICENSE"); it != d.arcade_classes.end())
        for (const GtData::ArcadeCar& c : it->second) r.license_codes.push_back(c.race_code);
    for (const PrizeSet& p : d.prizes) {
        std::optional<ConceptRules::Key> k = ConceptRules::parse_key(p.name);
        if (!k || p.cars.empty()) continue;
        k->car_class = p.cars.front().arcade_class;
        k->index = p.cars.front().arcade_index;
        r.keys.push_back(std::move(*k));
    }
    return r;
}

std::string concept_category_class(const GtData& d, int category) {
    static const char* const kClass[kConceptCategories] = {"CLASS_C", "CLASS_B", "CLASS_A", "CLASS_S", "CLASS_DIRT"};
    if (category < 0 || category >= kConceptCategories) return {};
    const std::string plain = kClass[category];
    // Language index 2 (UK English) with the list present: the "_UK" twin (concept §5.6).
    if (d.language == Language::BR && d.arcade_classes.count(plain + "_UK")) return plain + "_UK";
    return plain;
}

std::string concept_base_class(std::string_view car_class) {
    if (car_class.size() > 3 && car_class.substr(car_class.size() - 3) == "_UK") car_class.remove_suffix(3);
    return std::string(car_class);
}

std::string concept_car_image(std::string_view label) {
    if (label.substr(0, 3) != "gtc") return std::string(label);
    const size_t u = label.find('_');
    return u == std::string_view::npos ? std::string(label) : std::string(label.substr(u + 1));
}

bool concept_dirt_entry(std::string_view label) { return label.substr(0, 6) == "gtcmd_"; }

std::string concept_car_race(const GtData& d, const std::string& arcade, const std::string& car, int level, bool dirt) {
    auto base = [&](const std::string& label) {
        for (const data::CarVariant& v : d.db.arcade_cars())
            if (v.label == label) return v.car;
        return std::string();
    };
    const bool any = arcade.empty() && car.empty();
    auto find = [&](bool want_dirt) -> const GtData::ArcadeCar* {
        for (const bool uk : {false, true})
            for (const char* cls : {"CLASS_C", "CLASS_B", "CLASS_A", "CLASS_S", "CLASS_DIRT"}) {
                const auto it = d.arcade_classes.find(std::string(cls) + (uk ? "_UK" : ""));
                if (it == d.arcade_classes.end()) continue;
                for (const GtData::ArcadeCar& c : it->second)
                    if (!c.race_code.empty() && concept_dirt_entry(c.label) == want_dirt &&
                        (any || (!arcade.empty() && c.label == arcade) || (!car.empty() && base(c.label) == car)))
                        return &c;
            }
        return nullptr;
    };
    const int lv = std::clamp(level, 0, kConceptLevels - 1);
    const GtData::ArcadeCar* entry = find(dirt);
    if (!entry) entry = find(!dirt); // (a car with entries of the other kind only)
    if (entry) {
        const std::string code = concept_race_code(entry->race_code, lv);
        return d.db.event(code) ? code : std::string();
    }
    // A car no class lists, whose races the disc still holds under Tokyo-Geneva's names (A_<car>_000n, dirt AD_<car>_000n).
    const std::string name = !car.empty() ? car : concept_car_image(arcade);
    if (any || name.empty()) return {};
    for (const bool want_dirt : {dirt, !dirt}) {
        const std::string code = concept_race_code(std::string(want_dirt ? "AD_" : "A_") + name + "_0001", lv);
        if (d.db.event(code)) return code;
    }
    return {};
}

std::optional<std::array<u8, 16>> concept_cpu_driver(const GtData& d, const std::string& arcade, const std::string& car, int level,
                                                     bool dirt) {
    const data::Event* ev = d.db.event(concept_car_race(d, arcade, car, level, dirt));
    if (!ev || ev->bytes.size() < 0x1F4) return std::nullopt;
    return sim::exact::driver_overrides(sim::exact::race_config(ev->bytes));
}

std::vector<ConceptEntry> concept_open_cars(const GtData& d, const ConceptRules& rules, const ConceptState& state,
                                            const std::string& car_class, bool dirt) {
    std::vector<ConceptEntry> out;
    const auto it = d.arcade_classes.find(car_class);
    if (it == d.arcade_classes.end()) return out;
    const std::string base = concept_base_class(car_class);
    for (size_t i = 0; i < it->second.size(); ++i) {
        const GtData::ArcadeCar& c = it->second[i];
        if (!c.enabled || concept_dirt_entry(c.label) != dirt) continue;
        if (concept_slot_locked(rules, state, base, int(i))) continue;
        out.push_back({&c, int(i)});
    }
    return out;
}

const GtData::ArcadeCar* concept_key_car(const GtData& d, const ConceptRules::Key& key) {
    const auto it = d.arcade_classes.find(key.car_class);
    if (it == d.arcade_classes.end() || key.index < 0 || key.index >= int(it->second.size())) return nullptr;
    return &it->second[size_t(key.index)];
}

// --- The cars' settings ---

const int kConceptNormalSet[kConceptSetParts] = {2, 50, 19, 56, 67};
const int kConceptCustomSet[kConceptSetParts] = {3, 51, 22, 56, 67};

namespace {

// Slider position of each tire stage (indexed by the tire goods less the first: stages 0 .. 9, then dirt).
constexpr int kTirePosition[11] = {1, -1, -1, 2, -1, -1, -1, 0, -1, -1, -1};

const data::CarVariant* arcade_entry(const data::CarDatabase& db, const std::string& entry) {
    for (const data::CarVariant& v : db.arcade_cars())
        if (v.label == entry || v.label == "_" + entry) return &v;
    return nullptr;
}

int tire_slot(bool rear) { return part_slot(rear ? 29 : 28); }

// A car's settings from its equipment record's words (the inverse of sim::exact::make_equipment).
sim::CarSettings settings_of(const sim::exact::Equipment& eq) {
    sim::CarSettings c;
    auto w = [&](u32 off) { return int(eq.word(off)); };
    for (u32 i = 0; i < 8; ++i) c.gears[i] = s16(w(0xF0 + 4 * i));
    c.final_drive = w(0x110), c.gear_slider = w(0x114), c.front_torque = w(0x118);
    c.brake_front = w(0x11C), c.brake_rear = w(0x120);
    c.downforce_front = w(0x124), c.downforce_rear = w(0x128);
    for (u32 i = 0; i < 6; ++i) c.turbo[i] = w(0x12C + 4 * i);
    for (u32 i = 0; i < 2; ++i) {
        c.camber[i] = w(0x144 + 4 * i), c.ride_height[i] = w(0x14C + 4 * i), c.toe[i] = w(0x154 + 4 * i);
        c.spring[i] = w(0x15C + 4 * i), c.motion_ratio[i] = w(0x164 + 4 * i), c.arb[i] = w(0x18C + 4 * i);
    }
    for (u32 i = 0; i < 8; ++i) c.damper[i] = w(0x16C + 4 * i);
    for (u32 i = 0; i < 6; ++i) c.lsd[i] = w(0x194 + 4 * i);
    c.tcs = w(0x1B0);
    for (u32 i = 0; i < 4; ++i) c.ascc[i] = w(0x1BC + 4 * i);
    return c;
}

} // namespace

std::optional<GarageCar> concept_new_car(const GtMode& gt, const std::string& entry) {
    const data::CarDatabase& db = gt.db();
    const data::CarVariant* v = arcade_entry(db, entry);
    if (!v || !db.car(v->car)) return std::nullopt;
    // The entry's car as an arcade race builds it (the row's parts with their own settings, then
    // the row's own tune over them).
    GarageCar g;
    g.car = v->car;
    g.origin = v->label;
    g.parts = v->parts;
    g.parts[26] = 0;
    g.settings = settings_of(sim::exact::variant_equipment(db, *v));
    g.ref_final = g.settings.final_drive;
    // It owns the goods of the parts it wears (the garage record's set-up) and of every part the
    // database has for it. A garage car of this game has no running-in to do.
    data::Car fitted = *db.car(v->car);
    fitted.parts = g.parts;
    g.owned = gt.fitted_ownership(fitted);
    for (int goods = 0; goods < kGoodsCount; ++goods)
        if (find_part(db, g.car, goods)) {
            g.owned.set(size_t(goods));
            if (int rear = rear_tire_goods(goods); rear >= 0) g.owned.set(size_t(rear)); // (a front tire's goods: its set)
        }
    g.cond.engine_wear_m = 0;
    concept_fit_normal(gt, g);
    return g;
}

GarageCar* concept_garage_car(GtMode& gt, const std::string& entry, bool ride) {
    const data::CarVariant* v = arcade_entry(gt.db(), entry);
    if (!v) return nullptr;
    GameState& s = gt.state();
    auto find = [&]() -> GarageCar* {
        for (GarageCar& c : s.garage)
            if (c.origin == v->label) return &c;
        return nullptr;
    };
    GarageCar* c = find();
    if (!c) {
        std::optional<GarageCar> g = concept_new_car(gt, entry);
        if (!g || gt.add_garage_car(std::move(*g)) < 0) return nullptr;
        c = find();
    }
    if (c && ride) s.ride = int(c - s.garage.data());
    return c;
}

std::vector<std::string> concept_garage_entries(const GtMode& gt) {
    std::vector<std::string> out;
    for (int category = 0; category < kConceptCategories; ++category) {
        const auto it = gt.data().arcade_classes.find(concept_category_class(gt.data(), category));
        if (it == gt.data().arcade_classes.end()) continue;
        for (const GtData::ArcadeCar& c : it->second) {
            if (out.size() >= size_t(kMaxGarageCars)) return out;
            const data::CarVariant* v = arcade_entry(gt.db(), c.label);
            if (v && gt.db().car(v->car)) out.push_back(v->label);
        }
    }
    return out;
}

bool concept_car_normal(const GtMode& gt, const GarageCar& c) {
    const ByteSpan* r = gt.db().record(data::TableId::BrakeController, c.parts[size_t(part_slot(1))]);
    return !r || r->size() <= 0x10 || (*r)[0x10] == 0;
}

namespace {

// The settings a car's parts start with, as the parts' setters leave them (each value put into
// its record's range; this game's records hold starting values outside their ranges).
sim::CarSettings own_settings(const GtMode& gt, const GarageCar& c) {
    return settings_of(sim::exact::stock_equipment(gt.db(), c.car, c.parts));
}

// Fitting a goods: the part, and its settings back to its own.
void fit(const GtMode& gt, GarageCar& c, int goods) {
    if (!gt.fit_goods(c, goods)) return;
    const int table = goods_info(goods).table;
    refresh_settings(c.settings, own_settings(gt, c), table);
    if (table == 17) c.ref_final = c.settings.final_drive;
}

} // namespace

void concept_fit_normal(const GtMode& gt, GarageCar& c) {
    for (int goods : kConceptNormalSet) fit(gt, c, goods);
}

void concept_fit_custom(const GtMode& gt, GarageCar& c) {
    for (int goods : kConceptCustomSet)
        if (find_part(gt.db(), c.car, goods)) fit(gt, c, goods);
}

void concept_reset_custom(const GtMode& gt, GarageCar& c) {
    c.settings = own_settings(gt, c);
    c.ref_final = c.settings.final_drive;
    concept_fit_custom(gt, c);
}

bool concept_cvt(const GtMode& gt, const GarageCar& c) {
    const ByteSpan* r = gt.db().record(data::TableId::Gear, c.parts[size_t(part_slot(17))]);
    return r && r->size() > 0x11 && (*r)[0x11] == 2;
}

int concept_tire_position(const GtMode& gt, const GarageCar& c, bool rear) {
    // The fitted tire's goods: the first tire goods without a record.
    const ByteSpan* r = gt.db().record(rear ? data::TableId::RearTire : data::TableId::FrontTire, c.parts[size_t(tire_slot(rear))]);
    if (!r || r->size() <= 0x10) return kTirePosition[0];
    const int stage = (*r)[0x10];
    const int index = stage == 12 ? 10 : stage;
    return index >= 0 && index < 11 ? kTirePosition[index] : -1;
}

bool concept_set_tire(const GtMode& gt, GarageCar& c, bool rear, int position) {
    // The first stage of that position, fitted on this axle alone.
    for (int index = 0; index < 11; ++index) {
        if (kTirePosition[index] != position) continue;
        const std::optional<PartOffer> o = find_part(gt.db(), c.car, (rear ? 67 : 56) + index);
        const data::Car* car = gt.db().car(c.car);
        if (!o && (index != 0 || !car)) return false;
        c.parts[size_t(tire_slot(rear))] = o ? o->id : car->parts[size_t(tire_slot(rear))];
        return true;
    }
    return false;
}

std::array<u64, 27> concept_race_parts(const GtMode& gt, const GarageCar& c, const std::string& course, bool drift) {
    std::array<u64, 27> parts = c.parts;
    int surface = 0;
    for (const data::Course& k : gt.db().courses())
        if (k.label == course) surface = k.surface;
    const int stage = surface == 2 ? 12 : surface == 1 ? -1 : drift ? 11 : -1;
    if (stage < 0) return parts;
    for (bool rear : {false, true}) {
        const data::TableId t = rear ? data::TableId::RearTire : data::TableId::FrontTire;
        const u64 car = db::id_hash(c.car);
        for (ByteSpan r : gt.db().table(t).records)
            if (r.size() >= 0x18 && rd64(r, 8) == car && r[0x10] == stage) {
                parts[size_t(tire_slot(rear))] = rd64(r, 0);
                break;
            }
    }
    return parts;
}

std::vector<std::string> concept_attract_steps(const RegionData& release) {
    const std::string gcf = release.program_gcf.value_or(std::string());
    const std::string opening = "stream/opening.pss";
    if (gcf == "gameconf/concept_eu.gcf" || gcf == "gameconf/concept_tw.gcf")
        return {"", "stream/tvcm_GTCJP1.pss", "", "stream/tvcm_GTCJP2.pss", "", opening};
    // (Tokyo-Seoul v1.02 names v2.00's config but runs the 2001 program's loop: three demo races.)
    if (gcf == "gameconf/concept_kr.gcf" && !release.gcf_per_language) return {"", "", "", "", opening};
    return {"", "", "", opening};
}

ConceptSaveRule concept_save_rule(std::string_view game_config) {
    // (The releases are told apart by the game config their program names; 2001 Tokyo's program names the plain one.)
    if (game_config == "gameconf/concept_eu.gcf") return {"BESCES-50858GAMEDATA", 2, "ICON_GTC_concept_GNV.ico"};
    if (game_config == "gameconf/concept_tw.gcf") return {"BISCPS-55903GAMEDATA", 5, "ICON_GTC_concept_GNV.ico"};
    if (game_config == "gameconf/concept_kr.gcf") return {"BKSCPS-56005GAMEDATA", 3, "ICON_GTC_concept_KR.ico"};
    return {"BISCPS-15010GAMEDATA", 0, "ICON_GTC_GT_concept.ico"};
}

ConceptTransferRule concept_transfer_rule(const RegionData& release) {
    // (The releases are told apart by the game config their program names, as their languages are.)
    if (release.program_gcf && *release.program_gcf == "gameconf/concept_eu.gcf") return {10'000'000, Region::Europe};
    return {1'000'000'000, Region::Japan};
}

std::vector<Region> concept_transfer_regions(const RegionData& release) {
    std::vector<Region> out{concept_transfer_rule(release).gt3};
    for (Region r : {Region::America, Region::Europe, Region::Japan})
        if (r != out.front()) out.push_back(r);
    return out;
}

} // namespace gt3r::game
