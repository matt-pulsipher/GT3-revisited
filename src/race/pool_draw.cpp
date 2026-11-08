#include "race/pool_draw.h"

#include "base/ps2_float.h"
#include "formats/db/gt_db.h"
#include "game/data/car_database.h"

namespace gt3r::race {

std::vector<std::string> event_pool(const data::CarDatabase& db, const data::Event& ev) {
    std::vector<std::string> out;
    const u64 end = db::id_hash("-");
    for (size_t k = 0; k < 32 && ev.bytes.size() >= 0x18 + 8 * k; ++k) {
        u64 id = rd64(ev.bytes, 0x10 + 8 * k);
        if (id == end) break;
        std::string s = db.label(id);
        out.push_back(s.empty() ? "?" : s);
    }
    return out;
}

bool pool_entry_available(const data::CarDatabase& db, const std::string& label) {
    u64 id = db::id_hash(label);
    if (const ByteSpan* r = db.record(data::TableId::ArcadeCar, id); r && r->size() >= 0x10) id = rd64(*r, 8);
    if (const ByteSpan* r = db.record(data::TableId::CarVariant, id); r && r->size() >= 0x10) id = rd64(*r, 8);
    const ByteSpan* car = db.record(data::TableId::Car, id);
    if (!car || car->size() <= 0xF7) return false;
    return data::car_region_offered((*car)[0xF7], db.language());
}

std::vector<int> draw_pool(GameRandom& rng, const std::vector<bool>& unavailable, int draws) {
    std::vector<int> out;
    const int n = int(unavailable.size());
    std::vector<bool> taken = unavailable;
    int left = n; // counts down per draw, not reduced by the unavailable entries
    const float fn = float(n);
    for (int d = 0; d < draws; ++d) {
        int k = ps2::to_int(ps2::mul(rng.rand01(), fn));
        if (left > 0) {
            // The original probes forever when every entry is taken while `left` is still
            // positive (more draws than available entries); ours then repeats like an
            // exhausted pool.
            int j = k, probes = 0;
            while (probes < n && taken[size_t(j)]) j = (j + 1) % n, ++probes;
            if (probes < n) {
                k = j;
                taken[size_t(k)] = true;
                --left;
            } else {
                left = 0;
            }
        }
        out.push_back(k);
    }
    return out;
}

u32 pool_color_id(const data::Event& ev, int k) {
    const size_t o = 0x110 + 4 * size_t(k);
    return k >= 0 && k < 32 && ev.bytes.size() >= o + 4 ? rd32(ev.bytes, o) : 0xFFFFFFFFu;
}

std::vector<std::string> draw_opponents(const data::CarDatabase& db, const data::Event& ev, int draws,
                                        GameRandom& rng, std::vector<u32>* color_ids) {
    std::vector<std::string> pool = event_pool(db, ev), out;
    if (color_ids) color_ids->clear();
    if (pool.empty()) return out;
    std::vector<bool> unavailable(pool.size());
    for (size_t i = 0; i < pool.size(); ++i) unavailable[i] = !pool_entry_available(db, pool[i]);
    for (int k : draw_pool(rng, unavailable, draws)) {
        out.push_back(pool[size_t(k)]);
        if (color_ids) color_ids->push_back(pool_color_id(ev, k));
    }
    return out;
}

} // namespace gt3r::race
