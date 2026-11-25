#include "race/custom_paints.h"

#include "base/config.h"
#include "base/file.h"
#include "base/log.h"
#include "formats/model/car_model.h"
#include "formats/texture_pack.h"
#include "game/data/car_database.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace gt3r::race {

namespace {

// Value text without a trailing " ; comment" / " # comment" (a value may start with '#').
std::string value_text(std::string_view v) {
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i] == ';' || v[i] == '#') && (v[i - 1] == ' ' || v[i - 1] == '\t')) {
            v = v.substr(0, i);
            break;
        }
    return std::string(trim(v));
}

constexpr size_t kMaxText = 64; // a name or author, in bytes

} // namespace

std::optional<u32> parse_rgb(std::string_view text) {
    std::string_view t = trim(text);
    if (!t.empty() && t[0] == '#') t.remove_prefix(1);
    if (t.size() != 6) return std::nullopt;
    u32 v = 0;
    for (char c : t) {
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return std::nullopt;
        v = v << 4 | u32(d);
    }
    // Text is R, G, B from the left; the value keeps bytes R, G, B from the lowest (as carcolor.db).
    return (v >> 16 & 0xFF) | (v & 0xFF00) | (v & 0xFF) << 16;
}

std::string rgb_text(u32 rgb) {
    char b[8];
    std::snprintf(b, sizeof(b), "#%02X%02X%02X", rgb & 0xFF, rgb >> 8 & 0xFF, rgb >> 16 & 0xFF);
    return b;
}

std::optional<CustomPaint> parse_custom_paint(std::string_view ini_text, std::string* error) {
    auto fail = [&](const std::string& m) -> std::optional<CustomPaint> {
        if (error) *error = m;
        return std::nullopt;
    };
    IniDocument ini = IniDocument::parse(ini_text);
    CustomPaint p;
    bool has_rgb = false;
    for (const auto& [key_raw, val_raw] : ini.find("livery") ? ini.find("livery")->entries
                                                              : std::vector<std::pair<std::string, std::string>>{}) {
        const std::string key = to_lower(key_raw), v = value_text(val_raw);
        if (key == "name") p.name = v.substr(0, kMaxText);
        else if (key == "author") p.author = v.substr(0, kMaxText);
        else if (key == "model") p.model = to_lower(v);
        else if (key == "color" || key == "colour") {
            std::optional<u32> c = parse_rgb(v);
            if (!c) return fail("color: not #RRGGBB: " + v);
            p.rgb = *c, has_rgb = true;
        } else if (key == "finish" || key == "paint") {
            char* end = nullptr;
            long f = std::strtol(v.c_str(), &end, 10);
            if (v.empty() || !end || *end || f < 0 || f > 255) return fail("finish: not a paint number: " + v);
            p.finish = int(f);
        }
    }
    // A mod car's: "mod:" and its label (game/data/mod_cars.cpp valid_label).
    const std::string prefix = kModPaintKey;
    const bool mod = p.model.rfind(prefix, 0) == 0;
    const std::string body = mod ? p.model.substr(prefix.size()) : p.model;
    if (body.empty()) return fail("[livery] model: missing");
    if (body.size() > (mod ? 40u : 16u)) return fail("model: not a model name: " + p.model);
    for (char c : body)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return fail("model: not a model name: " + p.model);
    p.colour = has_rgb;
    if (p.name.empty() && has_rgb) p.name = rgb_text(p.rgb);
    return p;
}

int nearest_paint(const std::vector<u32>& paint_rgb, u32 rgb) {
    int best = 0;
    long best_d = -1;
    for (size_t i = 0; i < paint_rgb.size(); ++i) {
        long d = 0;
        for (int k = 0; k < 3; ++k) {
            long a = long(paint_rgb[i] >> (8 * k) & 0xFF), b = long(rgb >> (8 * k) & 0xFF);
            d += (a - b) * (a - b);
        }
        if (best_d < 0 || d < best_d) best = int(i), best_d = d;
    }
    return best;
}

std::vector<u32> CustomPaints::paint_rgb(const std::string& model) const {
    std::vector<u32> out;
    if (const std::vector<u32>* ids = colors_.ids(model))
        for (u32 id : *ids) out.push_back(colors_.rgb(id));
    return out;
}

int CustomPaints::disc_count(const std::string& model) const {
    const std::vector<u32>* ids = colors_.ids(model);
    return ids ? int(ids->size()) : 0;
}

bool CustomPaints::add(CustomPaint p, std::string* error, int* index) {
    const std::vector<u32> rgb = paint_rgb(p.model);
    if (rgb.empty()) {
        if (error)
            *error = p.model.rfind(kModPaintKey, 0) == 0 ? "mod car " + p.model.substr(std::string(kModPaintKey).size()) + ": not installed"
                                                         : "model " + p.model + ": no paints on this disc";
        return false;
    }
    if (p.finish >= int(rgb.size())) {
        if (error) *error = "finish " + std::to_string(p.finish) + ": " + p.model + " has paints 0 to " + std::to_string(rgb.size() - 1);
        return false;
    }
    if (!p.colour && !p.pictures) {
        if (error) *error = "[livery] color: missing";
        return false;
    }
    if (p.finish < 0) p.finish = p.colour ? nearest_paint(rgb, p.rgb) : 0;
    if (!p.colour) p.rgb = rgb[size_t(p.finish)];
    if (p.name.empty()) p.name = p.id.empty() ? rgb_text(p.rgb) : p.id.substr(p.id.find('/') + 1);
    std::vector<CustomPaint>& list = by_model_[p.model];
    if (index) *index = int(rgb.size() + list.size());
    list.push_back(std::move(p));
    return true;
}

const std::vector<CustomPaint>& CustomPaints::of(const std::string& model) const {
    static const std::vector<CustomPaint> none;
    auto it = by_model_.find(model);
    return it == by_model_.end() ? none : it->second;
}

const CustomPaint* CustomPaints::at(const std::string& model, int paint) const {
    const std::vector<CustomPaint>& l = of(model);
    const int k = paint - disc_count(model);
    return l.empty() || k < 0 || size_t(k) >= l.size() ? nullptr : &l[size_t(k)];
}

std::optional<ResolvedPaint> CustomPaints::resolved(const std::string& model, int paint) const {
    const CustomPaint* p = at(model, paint);
    if (!p) return std::nullopt;
    ResolvedPaint r;
    r.paint = p->finish;
    r.computed = computed(model, paint);
    r.livery = p->pictures;
    return r;
}

int CustomPaints::index_of_livery(const std::string& model, const std::string& id) const {
    const std::vector<CustomPaint>& l = of(model);
    for (size_t k = 0; k < l.size(); ++k)
        if (l[k].pictures && l[k].id == id) return disc_count(model) + int(k);
    return -1;
}

std::optional<gtm1::ComputedPaint> CustomPaints::computed(const std::string& model, int paint) const {
    const CustomPaint* p = at(model, paint);
    if (!p || !p->colour) return std::nullopt;
    gtm1::ComputedPaint c;
    c.rgb = p->rgb;
    c.finish = p->finish;
    c.paint_rgb = paint_rgb(model);
    return c;
}

int CustomPaints::index_of(const std::string& model, u32 rgb, int finish) const {
    const std::vector<CustomPaint>& l = of(model);
    for (size_t k = 0; k < l.size(); ++k)
        if (l[k].colour && !l[k].pictures && l[k].rgb == rgb && l[k].finish == finish) return disc_count(model) + int(k);
    return -1;
}

size_t CustomPaints::size() const {
    size_t n = 0;
    for (const auto& [m, l] : by_model_) n += l.size();
    return n;
}

std::shared_ptr<CustomPaints> load_custom_paints(const Vfs& vfs) {
    CarColors colors;
    colors.load(vfs);
    // Mod cars' paints (colours and liveries for "mod:<label>").
    if (std::any_of(vfs.mods().begin(), vfs.mods().end(), [](const ModInfo& m) { return m.active() && !m.cars.empty(); })) {
        data::CarDatabase db;
        if (db.load(vfs)) colors.add_mod_cars(db);
    }
    auto out = std::make_shared<CustomPaints>(std::move(colors));
    for (const ModInfo& m : vfs.mods()) {
        if (!m.active()) continue;
        for (const std::string& f : m.liveries) {
            std::optional<Bytes> b = read_host_file(f);
            std::string err;
            std::optional<CustomPaint> p =
                b ? parse_custom_paint(std::string_view(reinterpret_cast<const char*>(b->data()), b->size()), &err) : std::nullopt;
            if (!b) err = "cannot read";
            if (p) {
                p->mod = m.id, p->file = f;
                // A livery folder: its pictures, named by key as a texture pack's.
                const size_t slash = f.rfind('/');
                if (slash != std::string::npos && f.compare(slash + 1, std::string::npos, "livery.ini") == 0) {
                    const std::string dir = f.substr(0, slash);
                    auto pack = std::make_shared<texture_pack::Pack>();
                    pack->add_folder(dir, m.id);
                    if (pack->size()) p->pictures = std::move(pack);
                    p->id = m.id + "/" + dir.substr(dir.rfind('/') + 1);
                }
            }
            if (!p || !out->add(std::move(*p), &err)) {
                LOG_WARN("Custom paint %s: %s", f.c_str(), err.c_str());
                out->add_error(f + ": " + err);
            }
        }
    }
    if (out->size()) LOG_INFO("Custom paints: %zu colours and liveries", out->size());
    return out;
}

namespace {
std::mutex g_mutex;
std::shared_ptr<const CustomPaints> g_paints;
} // namespace

std::shared_ptr<const CustomPaints> custom_paints() {
    std::lock_guard lock(g_mutex);
    if (!g_paints) g_paints = std::make_shared<CustomPaints>();
    return g_paints;
}

void set_custom_paints(std::shared_ptr<const CustomPaints> paints) {
    {
        std::lock_guard lock(g_mutex);
        g_paints = std::move(paints);
    }
    // (the set of the time a model loads: a received colour swaps in a new set)
    set_paint_resolver([](const std::string& model, int paint) { return custom_paints()->resolved(model, paint); });
}

namespace {
std::mutex g_hash_mutex;
std::map<std::string, std::string> g_livery_hashes;
} // namespace

void note_livery_hash(const std::string& hash, const std::string& id) {
    std::lock_guard lock(g_hash_mutex);
    g_livery_hashes[hash] = id;
}

std::string livery_id_for_hash(const std::string& hash) {
    std::lock_guard lock(g_hash_mutex);
    auto it = g_livery_hashes.find(hash);
    return it == g_livery_hashes.end() ? std::string() : it->second;
}

int received_paint(const std::string& model, u32 rgb, int finish, const std::string& from) {
    std::lock_guard lock(g_mutex);
    std::shared_ptr<const CustomPaints> cur = g_paints ? g_paints : std::make_shared<CustomPaints>();
    if (int i = cur->index_of(model, rgb, finish); i >= 0) return i;
    auto next = std::make_shared<CustomPaints>(*cur);
    CustomPaint p;
    p.model = model, p.rgb = rgb, p.finish = finish, p.from = from;
    p.name = rgb_text(rgb);
    int index = -1;
    if (finish < 0 || !next->add(std::move(p), nullptr, &index)) return -1;
    g_paints = std::move(next);
    return index;
}

} // namespace gt3r::race
