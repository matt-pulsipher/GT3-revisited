#pragma once

// GT3's game data tables (database/paramdb*.db and friends), decoded into
// typed records. Layouts, units and confidence levels are documented in
// docs/formats/database.md; this header only names the fields.
//
// paramdb is a GTAR archive of 36 GTDT tables. Every record starts with a
// u64 id code (db::id_hash of its label). Purchasable part records share a
// common prefix: u64 id, u64 car (label of the car the part fits),
// u8 stage at +0x10 (0 = stock), and a u32 price in credits in the last
// four bytes of the record.

#include "disc/region.h"
#include "formats/db/gt_db.h"

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gt3r {
class Vfs;
class Volume;
}

namespace gt3r::data {

// Table indices inside paramdb (= GTDT header index). Names follow the
// part prefixes in the record labels and the game's PTEXT_* strings.
enum class TableId : int {
    Brake = 0,           // br_
    BrakeController = 1, // bc_
    Steer = 2,           // "n", "n1"
    Chassis = 3,         // ch_
    Lightweight = 4,     // lw_
    RacingModify = 5,    // rm_  (body: model id, tread, inertia; every car has one)
    Engine = 6,          // en_
    PortPolish = 7,      // pp_
    EngineBalance = 8,   // eb_
    Displacement = 9,    // ds_
    Computer = 10,       // co_
    NaTune = 11,         // nt_
    TurbineKit = 12,     // tk_  ("na" = no turbo)
    Drivetrain = 13,     // dt_
    Flywheel = 14,       // fw_
    Clutch = 15,         // cl_
    PropellerShaft = 16, // ps_
    Gear = 17,           // ge_
    Suspension = 18,     // su_
    Intercooler = 19,    // ic_
    Muffler = 20,        // mu_
    Lsd = 21,            // ls_
    Tcsc = 22,           // traction control: "a", "off", "evo"
    Ascc = 23,           // stability control: "a", "ne", "off"
    Wheel = 24,          // rims, e.g. bbw004_g
    TireSize = 25,       // sz001...
    TireForceVol = 26,   // "n" (name guessed)
    TireCompound = 27,   // f1, hard, soft, dirt, normal, sports, ...
    FrontTire = 28,      // ft_
    RearTire = 29,       // rt_
    Car = 30,            // default parts + name/maker/year/price of each car
    CarVariant = 31,     // opponent / prize / arcade cars: a base car + parts + settings
    Event = 32,          // races: course, opponents, regulation, laps, prizes
    Regulation = 33,     // allowed car lists (reg_*)
    Course = 34,         // course labels, names, surface
    ArcadeCar = 35,      // same layout as CarVariant (arcade cars)
    Count = 36,
};

const char* table_name(TableId t);

// Whether a car is offered in the game's language, by its region byte (car +0xF7): 0 in every
// language; 1 / 2 only / not in British English; 3 / 4 only / not in US English; 5 / 6 only /
// not in the European languages (BR, FR, GR, IT, SP); 7 / 8 only / not in Japanese; larger
// values in every language. The Opel / Vauxhall and S2000 / Viper twins use it.
bool car_region_offered(u8 region_byte, Language lang);

// The car record's name and its second string in a language (database.md "Car"): +0xF2 / +0xF4
// in Japanese, +0x100 / +0x102 in British English, +0xE4 / +0xE6 in the other languages.
struct CarNameFields {
    size_t name = 0xE4, second = 0xE6;
};
CarNameFields car_name_fields(Language lang);

// The side of a car its driver sits on. GT3's tables keep no steering side, and every car's
// camera points are on its centre line; the car's body shows it (most hold a driver in one
// seat: race::find_driver_seat). For a body that doesn't, the tables' best word is the country
// GT Mode's dealer map puts the car's maker in ("jp", "us", "gr", "fr", "it", "uk", "bel",
// "aus"; game::maker_countries): the right for a country whose traffic keeps left, else the
// left. That matches the bodies that do show their driver on about nine cars in ten, and is
// the same in every language and on every release. (The names' marks say less: the US and
// European releases end the names of models sold only in Japan with a J, but every such car
// is a Japanese maker's, and the unmarked Japanese cars have their drivers on the right too:
// the MX-5 the US release calls a Miata is one model on every disc.) A maker with no dealer on
// the map (the cars with no maker, a few prize cars) has no country: Unknown.
enum class DriverSide : u8 { Left, Right, Unknown };
// A dealer map country whose traffic keeps left: "jp", "uk", "aus".
bool country_drives_on_left(std::string_view country);
// The side for a maker's country; Unknown for none.
DriverSide country_driver_side(std::string_view maker_country);

// One record of any table, plus the ids resolved for convenience.
struct RawRecord {
    u64 id = 0;
    std::string label;
    ByteSpan bytes;
};

struct PartInfo {
    std::string label; // e.g. "tk_r34_gtr_v_99_c"
    std::string car;   // e.g. "r34_gtr_v_99"
    u8 stage = 0;      // 0 = stock, 1.. = sports/semi-racing/racing... (+0x10)
    u32 price = 0;     // last u32 of the record (+0x14 for tires), in the disc's money units (GtMode::part_price)
};

// Engine (table 6, 88 bytes). Physics confirmed by the spec builder in
// CORE.GT3: the curve, axis, point count and the scale byte are copied into
// the runtime car spec.
struct Engine {
    PartInfo part;
    std::string layout;       // "L6", "V6", "Rotary" ... (paramunistr)
    std::string valvetrain;   // "DOHC" ...
    std::string aspiration;   // "NA", "TURBO", "SuperCharger"
    std::string displacement; // "2568cc" (display string)
    u16 power_ps = 0;         // catalogue max power (PS)
    std::string power_rpm;    // "6800rpm"
    float torque_kgfm = 0;    // catalogue max torque
    std::string torque_rpm;   // "4400rpm"
    u8 torque_scale_pct = 100; // +0x42: x10 -> runtime torque factor (per mille); see docs
    u8 unknown_43 = 0;
    u8 unknown_44 = 0;        // +0x44 (100 / 80 ...), copied to spec
    u8 rpm_limit_x100 = 0;    // +0x45 (x100 rpm), raised by engine-balance/NA-tune/turbo parts (inferred: rev limit)
    u8 unknown_46 = 0;        // +0x46 (x100 rpm?), raised together with +0x45 by turbo kits
    std::vector<u16> rpm;     // torque curve x axis (rpm)
    std::vector<float> torque; // torque curve (kgf*m), same length as rpm
    u16 sound_id = 0;         // +0x16 engine sound bank: sound/engine/%05d and sound/<normalN|turboN>/%05d

    // Catalogue-style peak power from the curve: max(T * rpm) / 716.2.
    float curve_peak_ps() const;
};

struct Chassis {
    PartInfo part;
    u8 front_weight_pct = 0; // +0x10 (confirmed copied to spec)
    u16 length_mm = 0;       // +0x14
    u16 height_mm = 0;       // +0x16 (display only)
    u16 wheelbase_mm = 0;    // +0x18
    u16 weight_kg = 0;       // +0x1A
    u8 unknown_1c = 0;       // scaled by lightweight parts (percent) -> probably an inertia term
    u8 unknown_1d = 0;
};

struct Body { // table 5 "racing modify": present for every car
    PartInfo part;
    std::string model;          // car model id, e.g. "ni0137" (cars/<time>/<model>)
    std::array<float, 4> inertia{}; // +0x20..0x2C, converted to int by the spec builder (meaning unconfirmed)
    u16 weight_permille = 1000; // +0x30 scales chassis weight
    u16 front_tread_mm = 0;     // +0x32
    u16 rear_tread_mm = 0;      // +0x34
    u16 width_mm = 0;           // +0x36
    u8 inertia_pct = 100;       // +0x38 scales chassis byte 0x1C
    float unknown_44 = 0;
};

enum class DriveType : u8 { FR = 0, FF = 1, AWD = 2, MR = 3, RR = 4 };
const char* drive_type_name(u8 t);

struct Drivetrain {
    PartInfo part;
    u8 drive_type = 0; // +0x14, DriveType (confirmed by survey of all cars)
    u8 awd_system = 0xFE; // +0x15, 0xFE = none; 3 = Skyline GT-R, 4 = Evo VII ...
    std::array<u8, 3> front_torque{}; // +0x11..0x13 (inferred: min/max/default front torque share)
    std::array<u16, 6> params{}; // +0x16..0x20, copied to spec, scaled by flywheel/clutch/shaft parts
};

struct Gearbox {
    PartInfo part;
    u8 gears = 0;              // +0x11 forward gear count (confirmed copied to spec)
    float reverse = 0;         // +0x12, ratio
    std::vector<float> ratios; // +0x14.., ratio x1000 per gear; -0.999 marks "computed"/unused
    float final_drive = 0;     // +0x22
    float final_max = 0;       // +0x24 (adjustable range for racing gearboxes)
    float final_min = 0;       // +0x26
    bool adjustable = false;   // +0x28
    std::array<u8, 3> auto_setting{}; // +0x29..0x2B
};

struct Turbo {
    PartInfo part;
    std::array<u8, 16> bytes{}; // +0x11..0x20, see docs (boost/response parameters)
    u16 power_pct = 100;        // +0x1C: scales the runtime power factor (percent)
    u8 response_pct = 100;      // +0x1B
};

struct Tire {
    PartInfo part;
    std::string size;          // tire size label (table 25), e.g. sz075
    std::array<std::string, 4> compound; // table 27 labels (first three used by physics)
    std::array<std::string, 4> force_vol; // table 26 labels
};

struct TireCompound { std::string label; ByteSpan bytes; };

// Car (table 30): default parts + catalogue data.
struct Car {
    std::string label; // e.g. "r34_gtr_v_99"
    // The names in the game's language (CarDatabase::language(), car_name_fields), as game
    // text (paramunistr; Japanese names in kana), e.g. "SKYLINE GT-R V-spec(R34)"; and the
    // second string (mostly blank).
    std::string name;
    std::string name_alt;
    // The names for text drawn without the game's fonts (the app's own screens, online car
    // descriptors): the same as above in every language but Japanese, where they are the
    // record's +0xE4 / +0xE6 names in Latin letters.
    std::string latin_name, latin_name_alt;
    std::string maker; // e.g. "NISSAN"
    u16 maker_id = 0;
    u16 year = 0;
    u32 price = 0; // +0xEC, in the disc's money units (the game shows x100 in Japanese: game::GtMode::car_price)
    u8 flags = 0;  // +0xF0: bit 0 not for sale at the dealer, 1 cannot be sold / traded, 2 wheels cannot be changed
    std::array<u64, 27> parts{}; // default part ids at +0x08.., in record order (see kCarPartTables)
    ByteSpan bytes;
};

// The table each u64 slot at Car + 8 + 8*i refers to. -1 = unknown slot.
extern const std::array<int, 27> kCarPartTables;

struct CarVariant { // tables 31 and 35
    std::string label; // e.g. "ar_s14_ks_aero_96", "corvette_z06_00_385"
    std::string car;   // base car label
    std::array<u64, 27> parts{}; // at +0x10.., same order as Car::parts
    ByteSpan settings; // +0xE8..end: tuning settings (raw)
    ByteSpan bytes;
};

struct Course {
    std::string label; // e.g. "rome", "r_rome" (reverse), "L_rome1" (license section)
    std::string name;  // e.g. "Rome Circuit"
    u8 surface = 0;    // 0 tarmac, 2 dirt (1 = ? on 6 courses)
};

struct Regulation {
    std::string label;             // e.g. "reg_80s"
    std::vector<std::string> cars; // allowed car labels
};

struct Event {
    std::string label;   // e.g. "Gpolyphony_h_1002"
    std::string course;  // course label
    std::vector<std::string> entries; // opponent car variants (table 31)
    std::string regulation; // empty = none
    std::string race_mode;  // e.g. RACE_MODE_SINGLE (racemode.db labels)
    u8 laps = 0;            // +0x199 (the race's lap count; a machine test's type, race::MachineTest)
    u8 cars_in_race = 0;    // +0x1B5 (inferred)
    // The prizes (+0x1D4) and the GT Mode rules: game::GtMode::rules().
    ByteSpan bytes;
};

// A car added by a mod (see game/data/mod_cars.h). Its records live in the
// ordinary tables, so everything that reads the database sees it; this holds
// what the disc format has no field for.
struct ModCar {
    std::string label;  // table 30 label
    std::string mod;    // providing mod id (empty when loaded directly)
    std::string file;   // host path of the definition file
    std::string donor;  // disc car whose records fill everything not defined
    bool electric = false;
    float regen_torque_nm = 0; // electric: lift-off regenerative torque at the motor shaft
    float regen_kw = 0;        // electric: regenerative power limit
    // Electric motor sound (synthesised): whine order (tone cycles per motor
    // revolution, scaled to an audible pitch), and the mix of the inverter
    // hiss under load.
    float whine_order = 0;
    float whine_level = 1, inverter_level = 1;
    // A body of its own: host path of a binary glTF (empty = the donor's
    // shell), and its paint colours (name, RGB 0..1) in menu order.
    std::string model_file;
    std::string wheel; // rim (wheel/<label> on the disc); empty = the donor's
    std::optional<DriverSide> driver_side; // the side its file gives (CarDatabase::driver_side)
    float model_lower = 0; // metres to move a glTF body down from its own ground line
    std::vector<std::pair<std::string, std::array<float, 3>>> paints;
    // GT Mode dealer: sold at its maker's dealer (a disc dealer, or a mod maker's) for its
    // price. Pictures are host paths of PNGs the mod bundles (empty = none): the full-screen
    // backdrop (640x448), the strip button (51x27) and the name artwork (256x80; without
    // it the name is drawn in the menu font).
    bool for_sale = true;
    std::string description;
    std::string picture, thumb, logo;
    // Arcade mode: class tag ("CLASS_C".."CLASS_S", "CLASS_DIRT"; empty = not offered),
    // the class race variant (0 / 1, added to the race code's last character) and the
    // unlock level.
    std::string arcade_class;
    int arcade_variant = 0;
    int arcade_level = 0;
    // Stand-in body shell fitted to this car's dimensions (mm): the donor's
    // shell is stretched so its wheel arches land on this car's wheelbase.
    struct BodyFit {
        float donor_length = 0, donor_wheelbase = 0, donor_width = 0, donor_height = 0;
        float length = 0, wheelbase = 0, width = 0, height = 0;
        bool active() const { return donor_wheelbase > 0 && wheelbase > 0; }
    } fit;
};

// A car maker added by a mod (mods/<id>/makers/<id>.ini): a dealer of its own on its
// region's page in GT Mode. Pictures are host paths of the PNGs it bundles.
struct ModMaker {
    std::string id;     // file name: the dealer page is cd/cd_<region page>_<id>
    std::string name;   // as the cars' maker strings, e.g. "TESLA"
    std::string region; // dealer map region: jp, us, gr, fr, it, uk, bel, aus
    std::string description;
    std::string icon, background, logo; // 51x27 strip button, 640x448 backdrop, 475x190 logo
    std::string mod, file;
};

// The whole database. `suffix` selects the regional set: "_us" (NTSC-U), "_eu" (PAL) or
// "" (NTSC-J); by default the set of the disc's region (disc/region.h; through a reader:
// the region file Vfs::kRegionFile, NTSC-U when the reader has none).
class CarDatabase {
public:
    using Reader = std::function<std::optional<Bytes>(const std::string&)>;
    bool load(const Reader& read, std::optional<std::string> suffix = std::nullopt);
    bool load(const Vfs& vfs, std::optional<std::string> suffix = std::nullopt);
    bool load(const Volume& vol, std::optional<std::string> suffix = std::nullopt);
    const std::string& error() const { return error_; }
    // The player's message for a failed load: "This disc's car database isn't supported yet" when the database is there but
    // laid out otherwise than GT3's (another game on the engine), else error().
    std::string player_error() const { return unsupported_ ? "This disc's car database isn't supported yet" : error_; }
    // The game's language (disc/region.h) through the reader's Vfs::kLanguageFile, else the
    // set's own (JP for "", US for "_us", BR for "_eu"): the region byte filters compare its index.
    Language language() const { return language_; }

    // Generic access.
    const db::Table& table(TableId t) const { return tables_[size_t(t)]; }
    std::string label(u64 code) const { return names_.name(code); }
    const ByteSpan* record(TableId t, u64 id) const;
    const ByteSpan* record(TableId t, const std::string& label) const { return record(t, db::id_hash(label)); }
    const db::StringTable& strings() const { return paramstr_; }    // ASCII labels / numbers
    const db::StringTable& unistrings() const { return unistr_; }   // display names

    // Typed access.
    const std::vector<Car>& cars() const { return cars_; }
    const Car* car(const std::string& label) const;
    const Car* car_by_model(const std::string& model) const; // e.g. "ho0030"
    std::string model_of(const Car& c) const;
    // The driver's side: a mod car's own when its file gives one; else the one its body shows
    // (`body`: race::find_driver_seat, Unknown when it shows none or none was read); else its
    // maker's country's.
    DriverSide driver_side(const Car& c, DriverSide body = DriverSide::Unknown) const;
    // The dealer map's country of each maker, by Car::maker_id (game::load_maker_countries;
    // none until set), and a car's ("" for a maker with no dealer on the map).
    void set_maker_countries(std::map<u16, std::string> countries) { maker_countries_ = std::move(countries); }
    std::string_view maker_country(const Car& c) const;
    std::optional<Engine> engine(u64 id) const;
    std::optional<Chassis> chassis(u64 id) const;
    std::optional<Body> body(u64 id) const;
    std::optional<Drivetrain> drivetrain(u64 id) const;
    std::optional<Gearbox> gearbox(u64 id) const;
    std::optional<Turbo> turbo(u64 id) const;
    std::optional<Tire> tire(TableId front_or_rear, u64 id) const;
    std::optional<PartInfo> part(TableId t, u64 id) const;
    // All upgrade parts for a car in one table (stage order).
    std::vector<PartInfo> parts_for(TableId t, const std::string& car_label) const;

    const std::vector<CarVariant>& variants() const { return variants_; }
    const std::vector<CarVariant>& arcade_cars() const { return arcade_; }
    const CarVariant* variant(const std::string& label) const;
    const std::vector<Course>& courses() const { return courses_; }
    const std::vector<Regulation>& regulations() const { return regulations_; }
    const std::vector<Event>& events() const { return events_; }
    const Event* event(const std::string& label) const;

    // Display helpers for the default configuration of a car.
    struct Summary {
        std::string name, maker, model, drive, aspiration, layout, displacement;
        u16 year = 0;
        u32 price = 0;
        u16 power_ps = 0;
        float torque_kgfm = 0;
        std::string power_rpm, torque_rpm;
        u16 weight_kg = 0, length_mm = 0, height_mm = 0, width_mm = 0, wheelbase_mm = 0;
        u8 front_weight_pct = 0;
        u8 gears = 0;
        float final_drive = 0;
        std::vector<float> ratios;
        std::string front_tire, rear_tire;
    };
    Summary summarize(const Car& c) const;

    // Engine sound for a set of parts (sound workstream finding, verified:
    // all 235 engine sound ids exist under sound/engine/). The variant is the
    // muffler stage (table 20, +0x10; 0 when no muffler part) plus 4 when the
    // turbo kit record has a non-zero byte at +0x11 -> sound/normal<v>/ or
    // sound/turbo<v-4>/.
    struct EngineSound {
        u16 id = 0;
        u8 variant = 0;
    };
    EngineSound engine_sound(u64 engine, u64 muffler, u64 turbo) const;
    EngineSound engine_sound(const Car& c) const { return engine_sound(c.parts[6], c.parts[21], c.parts[12]); }

    // --- mod cars (game/data/mod_cars.h) ---
    // load(const Vfs&) adds the cars of every enabled mod. The calls below are
    // the building blocks: records are added to (or replace, by id) the disc
    // tables; rebuild() re-derives the typed lists afterwards.
    void put_record(TableId t, Bytes record);
    u16 add_unistring(std::string_view s) { return u16(unistr_.add(s)); }
    u16 add_string(std::string_view s) { return u16(paramstr_.add(s)); }
    void add_label(std::string_view l) { names_.add(l); }
    void add_mod_car(ModCar m);
    void rebuild() { decode(); }
    const std::vector<ModCar>& mod_cars() const { return mod_cars_; }
    const std::vector<ModMaker>& mod_makers() const { return mod_makers_; }
    void add_mod_maker(ModMaker m);
    const ModCar* mod_car(const std::string& label) const;
    // Problems found while loading mod cars (one line each, naming the file).
    const std::vector<std::string>& mod_car_errors() const { return mod_car_errors_; }
    void add_mod_car_error(std::string e) { mod_car_errors_.push_back(std::move(e)); }

private:
    Language language_ = Language::US;
    std::map<u16, std::string> maker_countries_;
    void decode();
    void us_medal_times(const Reader& read); // a PAL disc: the US database's medal times
    PartInfo part_info(ByteSpan r, bool tire = false) const;
    std::string pu(ByteSpan r, size_t off) const { return unistr_.at(rd16(r, off)); }
    std::string ps(ByteSpan r, size_t off) const { return paramstr_.at(rd16(r, off)); }
    std::string id_label(ByteSpan r, size_t off) const { return label(rd64(r, off)); }

    Bytes paramdb_;
    std::array<db::Table, size_t(TableId::Count)> tables_;
    std::array<std::map<u64, size_t>, size_t(TableId::Count)> by_id_;
    db::StringTable paramstr_, unistr_;
    db::IdNames names_;
    std::vector<Car> cars_;
    std::vector<CarVariant> variants_, arcade_;
    std::vector<Course> courses_;
    std::vector<Regulation> regulations_;
    std::vector<Event> events_;
    std::map<std::string, size_t> car_index_, variant_index_, event_index_, model_index_;
    std::deque<Bytes> extra_records_; // records added by mods (stable addresses)
    std::vector<ModCar> mod_cars_;
    std::vector<ModMaker> mod_makers_;
    std::vector<std::string> mod_car_errors_;
    std::string error_;
    bool unsupported_ = false;
};

} // namespace gt3r::data
