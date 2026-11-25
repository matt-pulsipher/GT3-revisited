#include "race/paint.h"

#include "formats/db/gt_db.h"
#include "game/data/car_database.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r::race {

bool CarColors::load(const Vfs& vfs) {
    std::optional<Bytes> b = vfs.read("database/carcolor.db");
    return b && parse(*b);
}

bool CarColors::parse(ByteSpan d) {
    models_.clear();
    rgb_.clear();
    try {
        u32 n = rd32(d, 0x08), idoff = rd32(d, 0x0C), defoff = rd32(d, 0x10);
        for (u32 i = 0; i < n; ++i) {
            size_t r = 0x18 + 16 * size_t(i);
            u64 key = rd64(d, r);
            u32 count = rd32(d, r + 8), off = rd32(d, r + 12) & ~3u;
            std::vector<u32> ids;
            for (u32 k = 0; k < count; ++k) ids.push_back(rd32(d, idoff + off + 4 * size_t(k)));
            models_[key] = std::move(ids);
        }
        u32 nd = rd32(d, defoff);
        for (u32 i = 0; i < nd; ++i) {
            size_t o = defoff + 4 + 16 * size_t(i);
            rgb_[rd32(d, o)] = rd32(d, o + 12);
        }
    } catch (const FormatError&) {
        return false;
    }
    return !models_.empty();
}

const std::vector<u32>* CarColors::ids(const std::string& model) const {
    auto it = models_.find(db::id_hash(model));
    return it == models_.end() ? nullptr : &it->second;
}

u32 CarColors::rgb(u32 id) const {
    auto it = rgb_.find(id);
    return it == rgb_.end() ? 0 : it->second;
}

int CarColors::index_of(const std::string& model, u32 id) const {
    const std::vector<u32>* l = ids(model);
    if (!l) return -1;
    auto it = std::find(l->begin(), l->end(), id);
    return it == l->end() ? -1 : int(it - l->begin());
}

u32 CarColors::id_at(const std::string& model, int index) const {
    const std::vector<u32>* l = ids(model);
    return l && index >= 0 && size_t(index) < l->size() ? (*l)[size_t(index)] : 0xFFFFFFFFu;
}

u32 mod_paint_id(size_t mod_car, int paint) {
    return kModPaint | (u32(mod_car) & 0xFFFFu) << 12 | (u32(paint) & 0xFFFu);
}

namespace {
const data::ModCar* mod_of(const data::CarDatabase& db, const std::string& car) {
    std::string base = car;
    if (!db.car(car)) {
        if (const data::CarVariant* v = db.variant(car)) base = v->car;
        else
            for (const data::CarVariant& a : db.arcade_cars())
                if (a.label == car) base = a.car;
    }
    return db.mod_car(base);
}
} // namespace

std::string paint_key(const data::CarDatabase& db, const std::string& car, const std::string& model) {
    const data::ModCar* m = mod_of(db, car);
    return m ? kModPaintKey + m->label : model;
}

const data::ModCar* mod_car_of_key(const data::CarDatabase& db, const std::string& key) {
    const std::string prefix = kModPaintKey;
    return key.rfind(prefix, 0) == 0 ? db.mod_car(key.substr(prefix.size())) : nullptr;
}

void CarColors::add_mod_cars(const data::CarDatabase& db) {
    auto byte = [](float v) { return u32(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    const std::vector<data::ModCar>& mods = db.mod_cars();
    for (size_t c = 0; c < mods.size(); ++c) {
        const std::string key = kModPaintKey + mods[c].label;
        if (mods[c].paints.empty()) {
            const data::Car* car = db.car(mods[c].label);
            if (const std::vector<u32>* body = car ? this->ids(db.model_of(*car)) : nullptr) models_[db::id_hash(key)] = std::vector<u32>(*body);
            continue;
        }
        std::vector<u32>& ids = models_[db::id_hash(key)];
        ids.clear();
        for (size_t i = 0; i < mods[c].paints.size(); ++i) {
            const u32 id = mod_paint_id(c, int(i));
            const std::array<float, 3>& v = mods[c].paints[i].second;
            ids.push_back(id);
            rgb_[id] = byte(v[0]) | byte(v[1]) << 8 | byte(v[2]) << 16;
        }
    }
}

namespace {

struct Cand {
    u32 id;
    float w;
    float v[3], m, u[3];
};

Cand make_cand(const CarColors& c, u32 id, float w) {
    Cand k{id, w, {}, 1, {}};
    u32 rgb = c.rgb(id);
    for (int i = 0; i < 3; ++i) k.v[i] = float((rgb >> (8 * i)) & 0xFF) / 255.0f;
    float m = std::sqrt(k.v[0] * k.v[0] + k.v[1] * k.v[1] + k.v[2] * k.v[2]);
    k.m = m > 0 ? m : 1.0f;
    for (int i = 0; i < 3; ++i) k.u[i] = k.v[i] / k.m;
    return k;
}

float sim(const Cand& a, const Cand& b) {
    if (a.m == b.m && a.u[0] == b.u[0] && a.u[1] == b.u[1] && a.u[2] == b.u[2]) return 1.0f;
    float dv[3] = {a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2]};
    float dist = std::sqrt(dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]);
    constexpr float kRoot3 = 1.7320508f;
    float s = (kRoot3 - dist) / kRoot3 * (a.u[0] * b.u[0] + a.u[1] * b.u[1] + a.u[2] * b.u[2]);
    return std::clamp(s, 0.0f, 1.0f);
}

} // namespace

std::vector<int> assign_paints(const CarColors& colors, std::vector<PaintEntry> entries, PaintRng& rng) {
    size_t n = entries.size();
    for (size_t e = 0; e < n; ++e) {
        if (entries[e].id != 0xFFFFFFFFu) continue;
        const std::vector<u32>* own = colors.ids(entries[e].model);
        if (!own || own->empty()) continue;
        // 1. Candidate tables for every entry.
        std::vector<std::vector<Cand>> t(n);
        for (size_t k = 0; k < n; ++k) {
            if (entries[k].id != 0xFFFFFFFFu) {
                t[k].push_back(make_cand(colors, entries[k].id, 1.0f));
                continue;
            }
            const std::vector<u32>* l = colors.ids(entries[k].model);
            if (!l) continue;
            float w = 1.0f, sum = 0;
            for (u32 id : *l) t[k].push_back(make_cand(colors, id, w)), sum += w, w *= 0.984375f;
            for (Cand& c : t[k]) c.w /= sum;
        }
        // 2. Penalise similarity to every other candidate (fixed colours heavily).
        std::vector<Cand>& mine = t[e];
        for (size_t j = 0; j < mine.size(); ++j) {
            float p = 1.0f;
            for (size_t k = 0; k < n; ++k)
                for (size_t m = 0; m < t[k].size(); ++m) {
                    if (k == e && m == j) continue;
                    float s = sim(mine[j], t[k][m]);
                    p += t[k].size() > 1 ? s : 10000.0f * std::pow(s, 64.0f);
                }
            mine[j].w /= p;
        }
        float sum = 0;
        for (const Cand& c : mine) sum += c.w;
        for (Cand& c : mine) c.w /= sum > 0 ? sum : 1.0f;
        std::stable_sort(mine.begin(), mine.end(), [](const Cand& a, const Cand& b) { return a.w < b.w; });
        // 3. Pick: lowest weights first, each with its own draw; the heaviest otherwise.
        size_t pick = mine.size() - 1;
        for (size_t j = 0; j + 1 < mine.size(); ++j)
            if (rng.rand01() < mine[j].w) {
                pick = j;
                break;
            }
        entries[e].id = mine[pick].id;
    }
    std::vector<int> out;
    for (const PaintEntry& p : entries) out.push_back(p.id == 0xFFFFFFFFu ? -1 : colors.index_of(p.model, p.id));
    return out;
}

} // namespace gt3r::race
