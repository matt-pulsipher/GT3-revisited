#pragma once

// Part "goods" of the GT-mode tune shop. The
// original numbers every purchasable part category 0..86 (87 = none); a
// goods id names a parts table and a stage. Each garage car keeps an
// 87-bit "owned" set over these ids; the fitted parts are separate.

#include "game/data/car_database.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gt3r::game {

constexpr int kGoodsCount = 87;
constexpr int kGoodsNone = 87;

struct Goods {
    int table = -1; // paramdb table (data::TableId)
    int stage = 0;  // part record +0x10
};

// Goods id -> table and stage; {-1, 0} for ids outside 0..86.
Goods goods_info(int goods);
// Inverse: the goods id of a table/stage pair, or -1.
int goods_id(int table, int stage);

// Rear tire goods paired with a front tire goods (56..65 -> 67..76), else -1.
int rear_tire_goods(int front_goods);

// Tune-shop menu items (ts/ pages of gt_mode.mbl) and what confirming them does.
struct ShopItem {
    const char* item; // menu item name, e.g. "sus_sports"
    int goods;
    enum class Flow : u8 {
        BuyThenAsk, // buy_parts, then "fit now?" (fit_parts)
        BuyAndFit,  // buy_fit_parts: fitted at once
        BuyOnly,    // dirt tires: bought, never fitted by the shop
    } flow;
};
const std::vector<ShopItem>& shop_items();
const ShopItem* shop_item(const std::string& item);

// The car slot (index into data::Car::parts / data::kCarPartTables) holding a table, or -1.
int part_slot(int table);

// The part record of `table`/`stage` made for `car_label` (the original scans
// the table for +0x08 == car and +0x10 == stage). Price per the shop rules
// (front tire price only; the rear record costs 0).
struct PartOffer {
    u64 id = 0;
    std::string label;
    u32 price = 0;
};
std::optional<PartOffer> find_part(const data::CarDatabase& db, const std::string& car_label, int goods);

// Whether the car was built with a turbo: its own turbine record's +0x11. A turbo
// kit fitted later does not count.
bool stock_turbo(const data::CarDatabase& db, const data::Car& car);

// The goods a car comes with in part slot `slot`, -1 for none: its stock part's
// stage, stage 0 when the slot holds no part. So an upgrade can always be swapped back, except
// that a racing modification needs a record, a car built without a turbo has no intercooler
// goods and an FF, MR or RR car no driveshaft goods.
int stock_goods(const data::CarDatabase& db, const data::Car& car, size_t slot);

} // namespace gt3r::game
