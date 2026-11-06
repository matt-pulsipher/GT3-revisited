#include "game/gt_parts.h"

namespace gt3r::game {

namespace {

// Goods ranges: first id, table, stage of the first id (consecutive stages).
struct Range {
    int first, last, table;
};
constexpr Range kRanges[] = {
    {0, 1, 0},     {2, 3, 1},     {4, 7, 15},    {8, 9, 10},    {10, 12, 9},   {13, 14, 8},
    {15, 18, 14},  {19, 22, 17},  {23, 25, 19},  {26, 29, 4},   {30, 35, 21},  {36, 39, 20},
    {40, 43, 11},  {44, 45, 7},   {46, 47, 16},  {48, 49, 5},   {50, 53, 18},  {54, 55, 22},
    {56, 66, 28},  {67, 77, 29},  {78, 82, 12},  {83, 84, 23},  {85, 86, 13},
};

} // namespace

Goods goods_info(int g) {
    for (const Range& r : kRanges)
        if (g >= r.first && g <= r.last) {
            int stage = g - r.first;
            // Tires: stages 0..9, then dirt (the 11th id) is stage 12.
            if ((r.table == 28 || r.table == 29) && stage == 10) stage = 12;
            return {r.table, stage};
        }
    return {};
}

int goods_id(int table, int stage) {
    for (const Range& r : kRanges)
        if (r.table == table) {
            int s = stage;
            if ((table == 28 || table == 29) && stage == 12) s = 10;
            if (s >= 0 && r.first + s <= r.last) return r.first + s;
            return -1;
        }
    return -1;
}

int rear_tire_goods(int g) { return g >= 56 && g <= 65 ? g + 11 : -1; }

const std::vector<ShopItem>& shop_items() {
    using F = ShopItem::Flow;
    static const std::vector<ShopItem> items = {
        {"bkt_racing", 1, F::BuyThenAsk},       {"bbl_on", 3, F::BuyThenAsk},
        {"sus_sports", 51, F::BuyThenAsk},      {"sus_semi_racing", 52, F::BuyThenAsk},
        {"sus_racing", 53, F::BuyThenAsk},      {"muf_sports", 37, F::BuyThenAsk},
        {"muf_semi_racing", 38, F::BuyThenAsk}, {"muf_racing", 39, F::BuyThenAsk},
        {"nat_1", 41, F::BuyThenAsk},           {"nat_2", 42, F::BuyThenAsk},
        {"nat_3", 43, F::BuyThenAsk},           {"ptp_on", 45, F::BuyAndFit},
        {"egb_on", 14, F::BuyAndFit},           {"dsp_on", 11, F::BuyAndFit},
        {"cpu_on", 9, F::BuyThenAsk},           {"clt_high", 5, F::BuyThenAsk},
        {"clt_twin", 6, F::BuyThenAsk},         {"clt_triple", 7, F::BuyThenAsk},
        {"fly_light", 16, F::BuyThenAsk},       {"fly_cr_mo", 17, F::BuyThenAsk},
        {"fly_light_cr_mo", 18, F::BuyThenAsk}, {"prp_on", 47, F::BuyThenAsk},
        {"tms_close", 20, F::BuyThenAsk},       {"tms_super_close", 21, F::BuyThenAsk},
        {"tms_variable", 22, F::BuyThenAsk},    {"lsd_1", 31, F::BuyThenAsk},
        {"lsd_15", 33, F::BuyThenAsk},          {"lsd_2", 32, F::BuyThenAsk},
        {"lsd_variable", 34, F::BuyThenAsk},    {"ayc_on", 35, F::BuyThenAsk},
        {"vcd_on", 86, F::BuyThenAsk},          {"wtr_1", 27, F::BuyAndFit},
        {"wtr_2", 28, F::BuyAndFit},            {"wtr_3", 29, F::BuyAndFit},
        {"tbk_1", 79, F::BuyThenAsk},           {"tbk_2", 80, F::BuyThenAsk},
        {"tbk_3", 81, F::BuyThenAsk},           {"tbk_4", 82, F::BuyThenAsk},
        {"icl_sports", 24, F::BuyThenAsk},      {"icl_racing_big", 25, F::BuyThenAsk},
        {"tir_sports", 57, F::BuyThenAsk},      {"tir_super_hard", 58, F::BuyThenAsk},
        {"tir_hard", 59, F::BuyThenAsk},        {"tir_medium_hard", 60, F::BuyThenAsk},
        {"tir_medium", 61, F::BuyThenAsk},      {"tir_medium_soft", 62, F::BuyThenAsk},
        {"tir_soft", 63, F::BuyThenAsk},        {"tir_super_soft", 64, F::BuyThenAsk},
        {"tir_control", 65, F::BuyThenAsk},     {"tir_dirt", 66, F::BuyOnly},
        // In the code's name table but not offered by the US menu.
        {"tcs_on", 55, F::BuyThenAsk},          {"sms_on", 84, F::BuyThenAsk},
    };
    return items;
}

const ShopItem* shop_item(const std::string& item) {
    for (const ShopItem& s : shop_items())
        if (item == s.item) return &s;
    return nullptr;
}

int part_slot(int table) {
    for (size_t i = 0; i < data::kCarPartTables.size(); ++i)
        if (data::kCarPartTables[i] == table) return int(i);
    return -1;
}

std::optional<PartOffer> find_part(const data::CarDatabase& db, const std::string& car_label, int goods) {
    Goods g = goods_info(goods);
    if (g.table < 0) return std::nullopt;
    const u64 car = db::id_hash(car_label);
    for (ByteSpan r : db.table(data::TableId(g.table)).records) {
        if (r.size() < 0x18 || rd64(r, 8) != car || r[0x10] != g.stage) continue;
        PartOffer o;
        o.id = rd64(r, 0);
        o.label = db.label(o.id);
        if (std::optional<data::PartInfo> p = db.part(data::TableId(g.table), o.id)) o.price = p->price;
        if (g.table == 29) o.price = 0; // a tire set costs the front record's price
        return o;
    }
    return std::nullopt;
}

namespace {

// The record of the car's own part in a table, null for an empty slot.
const ByteSpan* stock_record(const data::CarDatabase& db, const data::Car& car, data::TableId t) {
    const int slot = part_slot(int(t));
    const u64 id = slot < 0 ? 0 : car.parts[size_t(slot)];
    return id ? db.record(t, id) : nullptr;
}

} // namespace

bool stock_turbo(const data::CarDatabase& db, const data::Car& car) {
    const ByteSpan* r = stock_record(db, car, data::TableId::TurbineKit);
    return r && r->size() > 0x11 && (*r)[0x11] != 0;
}

int stock_goods(const data::CarDatabase& db, const data::Car& car, size_t slot) {
    const int table = data::kCarPartTables[slot];
    if (table < 0) return -1;
    const data::TableId t = data::TableId(table);
    if (t == data::TableId::Intercooler && !stock_turbo(db, car)) return -1;
    if (t == data::TableId::PropellerShaft) {
        // No driveshaft on FF, MR and RR cars (drivetrain +0x14 = 1, 3, 4).
        const ByteSpan* d = stock_record(db, car, data::TableId::Drivetrain);
        const int drive = d && d->size() > 0x14 ? (*d)[0x14] : 0;
        if (drive == 1 || drive == 3 || drive == 4) return -1;
    }
    const ByteSpan* r = stock_record(db, car, t);
    if (r && r->size() > 0x10) return goods_id(table, (*r)[0x10]);
    return t == data::TableId::RacingModify ? -1 : goods_id(table, 0);
}

} // namespace gt3r::game
