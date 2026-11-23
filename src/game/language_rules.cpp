#include "game/language_rules.h"

#include "game/data/car_database.h"

namespace gt3r::game {

namespace {

// The prize-car label table (docs/dev/GT_MODE.md "Language"): from, to, language mask (bit = 1 << language index).
// Program constant: 0x21D8C0 fills it on its first use from immediates and looks a label up
// (via 0x21DB60); PAL 0x21E8E0 the same; the NTSC-J program has none (Japanese keeps every
// label, as the masks do here).
struct LabelSwap {
    const char* from;
    const char* to;
    u32 mask;
};
constexpr LabelSwap kPrizeLabels[] = {
    {"opel_astra_race_00", "vauxhall_astra_race_00", 0x04},
    {"opel_speedster_00", "vauxhall_vx220_00", 0x04},
    {"opel_calibra_race_95", "vauxhall_calibra_race_95", 0x04},
    {"viper_gtsr_oreca_00", "dodge_viper_gtsr_oreca_00", 0x02},
    {"s2000_99", "s2000_a_99", 0x02},
    {"s2000_99_ext", "s2000_a_99_ext", 0x02},
    {"s2000_99", "s2000_e_99", 0x7C},
    {"s2000_99_ext", "s2000_e_99_ext", 0x7C},
};

} // namespace

std::string prize_car_label(std::string_view label, Language lang, bool* swapped) {
    if (swapped) *swapped = false;
    for (const LabelSwap& s : kPrizeLabels)
        if ((s.mask >> unsigned(lang)) & 1u && label == s.from) {
            if (swapped) *swapped = true;
            return s.to;
        }
    return std::string(label);
}

bool dealer_item_listed(std::string_view item, Language lang) {
    // DealerButtonList::vf7 0x11B138 (PAL 0x11B220): language 2 leaves out "opel", every other
    // "vauxhall" (the NTSC-J program has no such list).
    return item != (lang == Language::BR ? "opel" : "vauxhall");
}

LicenseCar license_car(const data::CarDatabase& db, const data::Event& ev, Language lang) {
    LicenseCar out;
    auto slot_entry = [&](int slot) -> u64 {
        return ev.bytes.size() >= 0x18 + 8 * size_t(slot) ? rd64(ev.bytes, 0x10 + 8 * size_t(slot)) : 0;
    };
    auto slot_color = [&](int slot) {
        return ev.bytes.size() >= 0x114 + 4 * size_t(slot) ? rd32(ev.bytes, 0x110 + 4 * size_t(slot)) : 0xFFFFFFFFu;
    };
    const u64 e0 = slot_entry(0);
    out.entry = e0 == gt3r::db::id_hash("-") ? std::string() : db.label(e0);
    if (out.entry.empty() && !ev.entries.empty()) out.entry = ev.entries[0];
    out.color = slot_color(0);
    // The licence driver 0x132030 (PAL 0x132518): only with language 2.
    if (lang != Language::BR) return out;
    // Entry 0's base car: the variant's car (+8), else the entry itself.
    u64 base = e0;
    if (const ByteSpan* v = db.record(data::TableId::CarVariant, e0); v && v->size() >= 16) base = rd64(*v, 8);
    const ByteSpan* car = db.record(data::TableId::Car, base);
    if (!car || car->size() <= 0xF7 || data::car_region_offered((*car)[0xF7], lang)) return out;
    // Not offered: the slot-1 car variant in the slot-1 colour; without one entry 0 stays, in
    // the slot-1 colour when that is set.
    const u64 e1 = slot_entry(1);
    const u32 c1 = slot_color(1);
    if (db.record(data::TableId::CarVariant, e1)) {
        out.entry = db.label(e1);
        out.color = c1;
        out.slot = 1;
    } else if (c1 != 0xFFFFFFFFu) {
        out.color = c1;
    }
    return out;
}

std::string demo_replay_name(std::string_view code, Language lang) {
    // The PAL program's 0x15CCB0: "_uk" when language 2 and the code is "L0S0001" (program
    // constants; the NTSC-U and NTSC-J programs build "demo/%s" alone, and only ever run in
    // languages 1 / 0).
    std::string name(code);
    if (lang == Language::BR && code == "L0S0001") name += "_uk";
    return name;
}

std::string demo_replay_path(std::string_view code, Language lang,
                             const std::function<bool(const std::string&)>& exists) {
    const std::string name = demo_replay_name(code, lang);
    if (name != code && exists && !exists("demo/" + name)) return "demo/" + std::string(code);
    return "demo/" + name;
}

// 0x239338 (PAL 0x23A3B8, NTSC-J 0x2362D8): +8 for language 0 (and any index outside 1..6),
// +4 for 1..6.
size_t color_name_field(Language lang) { return lang == Language::JP ? 8 : 4; }

} // namespace gt3r::game
