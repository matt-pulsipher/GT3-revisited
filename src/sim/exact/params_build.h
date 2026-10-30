#pragma once

// The car's parameter block P (car + 0x10 .. car + 0xEC0) built the way the race's initialisation builds it [physics §6]:
// the packed integer spec from the race entry's equipment (with the builder), then the converter
// that turns it into P, both in GT3's own arithmetic (EE singles, the program's soft doubles), so P comes out byte
// for byte as the program's.
//
// P is a byte image of the record (offsets from P): the converter reads its own earlier writes back and also reads a few words
// before it writes them (the record is cleared before), and its curve tables hold absolute addresses into P, so the image is
// built in place from the record's previous content with P's address given.

#include "base/ee_float.h"
#include "sim/exact/program_data.h"

#include <array>
#include <string>

namespace gt3r::data {
class CarDatabase;
struct CarVariant;
}
namespace gt3r::sim {
struct CarSettings;
}

namespace gt3r::sim::exact {

using ee::F;

// A race entry's car equipment (EQ = entry + 0x20): the car, one part per parts table and the user settings.
struct Equipment {
    u64 car = 0;                     // EQ +0x00 the car's id (table 30)
    std::array<u64, 30> part{};      // by data::TableId (EQ +0x18 + 8 k in the slot order of race_setup's eq_slot_for); 0 = none
    std::array<s32, 0x37> setting{}; // EQ +0xF0 + 4 k: the settings words [physics §6.6]
    s32 word(u32 eq_offset) const { return setting[(eq_offset - 0xF0) / 4]; }
};

// The equipment of a car (table 30 label) with these parts (Car::parts order) and settings (our CarSettings; EQ +0x1B4 and
// +0x1B8 = 0).
Equipment make_equipment(const data::CarDatabase& db, const std::string& car_label, const std::array<u64, 27>& parts,
                         const CarSettings& settings);
// The equipment set-car gives a car with these parts: the defaults of sim::stock_settings clamped to the records'
// ranges as its part setters clamp them [physics §6.6].
Equipment stock_equipment(const data::CarDatabase& db, const std::string& car_label, const std::array<u64, 27>& parts);
// The equipment set-car gives an arcade car (a table 35 record, which the program looks up first): its parts as they are and the
// part setters, the base car's id from the record's +0x08, then the record's own settings bytes +0xE8 .. +0x10D over
// the setters'.
Equipment arcade_equipment(const data::CarDatabase& db, const data::CarVariant& v);
// The equipment a car equipment image holds as it is (the memory card's 0x1D0-byte car layout: a saved car, a replay's car
// record, the disc's demo runs'): the car id EQ +0x00, the part ids EQ +0x18 + 8 k by slot, the settings words EQ +0xF0 ...
constexpr u32 kEquipmentImageSize = 0x1D0;
Equipment equipment_image(ByteSpan eq);

constexpr u32 kSpecSize = 0x3E2;
using Spec = std::array<u8, kSpecSize>;

// The spec from the equipment [physics §6.2-6.3], with the program's default curves and surface classes from `d`. `mode` is the conductor's game mode byte (2 = the mode without
// stability / traction aids); with `tire_override` (the program's t0) the tyre parts are replaced by the car's stage 11 (`flag`
// set) or stage 12 (mode 2) tyres when the database has them, written back into `eq` as the program does. The race passes
// tire_override = false.
void build_spec(const data::CarDatabase& db, const ProgramData& d, Equipment& eq, int flag, int mode, bool tire_override, Spec& spec);

constexpr u32 kParamsSize = 0xEB0; // P + 0x000 .. P + 0xEAF (car + 0x10 .. car + 0xEBF)
using ParamImage = std::array<u8, kParamsSize>;

// What the converter reads besides the spec and P.
struct ConvertInputs {
    // The program's static physics block (the mass every car gets, the pitch-tyre byte, the power cap's soft share, the load
    // curve knees, the wet surfaces' grip caps), the engine-condition limits (the table from it) and the
    // differential type letters.
    const ProgramData* data = nullptr;
    u32 base = 0;                 // P's address (car + 0x10): the curve tables store base + offset
    s32 control = 0;              // a2: the car's control source F+0x3E9 (2 = CPU)
    std::array<u8, 12> rules{};   // t0: the race-rule block K+0xE340 bytes 0..0xB (event record +0x198..)
    // t1: the entry's 0x20-byte block at entry + 0x2104 (null = zeros); the top-speed walk scales the engine torque by its +4 /
    // +8 words against the limits (an engine-condition state, by its use).
    std::array<u32, 8> condition{};
    // Physics globals: the air density and the tarmac rolling-resistance curve (n, x, y) and
    // scale, as the program last left them. The race's initialisation recomputes them before it builds the cars
    // (the initialisation), so in a race they are what physics_globals() gives.
    F air_density{};
    s16 roll_n = 0;
    std::array<F, 8> roll_x{}, roll_y{};
    F roll_scale{};
    // The physics globals as the program's routines compute them from data's static block (the program image starts with other
    // curve values and no scale).
    void physics_globals();
};

// P from the spec (which it also edits: drive type >= 5 reads as 0, the tyre blocks' curve origins), in place over
// the record's previous content `p` (the race clears the record first). Returns the program's return value (0). Its a3 (0 in
// the race) is read only by the computed-gearbox generator, which runs when spec 0xCA is set; the spec builder never
// sets it, so that path is not ported.
s32 convert_params(Spec& spec, const ConvertInputs& in, ParamImage& p);

// What the race's initialisation does for one car up to P (per car): the spec from the equipment (with the
// conductor's mode byte, no tyre override), the aid switches (the entry's ASM word +0x20F0 and TCS word +0x20F4: 0 clears
// spec 0x109 / 0x110), then the converter over the cleared record with the race's physics globals. `in` gives the control
// source, the rule block, the condition block and P's address; its globals are replaced by physics_globals().
ParamImage race_params(const data::CarDatabase& db, const ProgramData& d, Equipment eq, int mode, bool asm_on, bool tcs_on, ConvertInputs in);

// The peak power and torque of P's engine (E = P + 0x138) as the menus' figures: from the curve's first point
// (rounded to 100 rpm) every 100 rpm up to the higher of the rev limit and the last point, the full-throttle torque x the
// condition factor of in.condition under the power cap, kgf m = T / 9.8 and PS = kgf m x rpm / 716.2;
// only speeds up to the rev limit count, the first strictly greater value wins. The program writes {ps, kgfm, power rpm,
// torque rpm} at its out pointer (the showroom's + 0x1294).
struct EnginePeak {
    F ps, kgfm;
    s32 power_rpm = 0, torque_rpm = 0;
};
EnginePeak engine_peak(const ParamImage& p, const ConvertInputs& in);

// The showroom build (the menus' car, and GT Mode's grid: race/field.h): the spec (with flag, mode and
// tyre override 0), P from it (control source 0, the program's showroom rule record (no power cap), the
// condition words `condition`; zeros: a fresh engine) and the peak. The spec's mass (+0x2A, s16 kg) is the
// catalogue weight with the weight parts, before the converter's added mass.
struct Showroom {
    Spec spec{};
    ParamImage p{};
    EnginePeak peak;
    s16 mass() const { return s16(spec[0x2A] | (spec[0x2B] << 8)); }
};
Showroom showroom_build(const data::CarDatabase& db, const ProgramData& d, Equipment eq, const std::array<u32, 8>& condition = {});

// The whole power the menus show for a built car's peak of `ps` PS: converted to the language's unit
// (HP, BHP and CV: x 0.98619, `hp`; PS as is), truncated; the NTSC-J program has no conversion (its language shows PS).
s32 shown_power(F ps, bool hp);

} // namespace gt3r::sim::exact
