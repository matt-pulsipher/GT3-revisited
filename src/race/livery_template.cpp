#include "race/livery_template.h"

#include "base/file.h"
#include "formats/model/car_model.h"
#include "formats/model/gltf.h"
#include "formats/model/gtm1.h"
#include "formats/texture_pack.h"
#include "game/data/car_database.h"
#include "race/paint.h"
#include "vfs/vfs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace gt3r::race {

namespace {

// "set<k>_<i>" (a cut-out copy adds "_cut"): the set and index, or false.
bool parse_texture_name(const std::string& name, u32& set, u32& index) {
    unsigned k = 0, i = 0;
    int used = 0;
    if (std::sscanf(name.c_str(), "set%u_%u%n", &k, &i, &used) != 2) return false;
    if (size_t(used) != name.size() && name.compare(size_t(used), std::string::npos, "_cut") != 0) return false;
    set = k, index = i;
    return true;
}

void blend(std::vector<u32>& px, u32 w, u32 h, int x, int y, u32 c) {
    if (x < 0 || y < 0 || u32(x) >= w || u32(y) >= h) return;
    u32& d = px[size_t(y) * w + size_t(x)];
    const u32 a = c >> 24, da = d >> 24;
    const u32 oa = a + da * (255 - a) / 255;
    if (oa == 0) return;
    u32 v = oa << 24;
    for (int k = 0; k < 3; ++k) {
        const u32 s = (c >> (8 * k)) & 0xFF, t = (d >> (8 * k)) & 0xFF;
        v |= ((s * a + t * da * (255 - a) / 255) / oa) << (8 * k);
    }
    d = v;
}

void fill_triangle(std::vector<u32>& px, u32 w, u32 h, const float (&p)[3][2], u32 c) {
    const float x0 = std::min({p[0][0], p[1][0], p[2][0]}), x1 = std::max({p[0][0], p[1][0], p[2][0]});
    const float y0 = std::min({p[0][1], p[1][1], p[2][1]}), y1 = std::max({p[0][1], p[1][1], p[2][1]});
    const float area = (p[1][0] - p[0][0]) * (p[2][1] - p[0][1]) - (p[2][0] - p[0][0]) * (p[1][1] - p[0][1]);
    if (std::fabs(area) < 1e-6f) return;
    for (int y = std::max(0, int(std::floor(y0))); y <= std::min(int(h) - 1, int(std::ceil(y1))); ++y)
        for (int x = std::max(0, int(std::floor(x0))); x <= std::min(int(w) - 1, int(std::ceil(x1))); ++x) {
            const float cx = float(x) + 0.5f, cy = float(y) + 0.5f;
            float e[3];
            for (int k = 0; k < 3; ++k) {
                const float* a = p[k];
                const float* b = p[(k + 1) % 3];
                e[k] = ((b[0] - a[0]) * (cy - a[1]) - (b[1] - a[1]) * (cx - a[0])) / area;
            }
            if (e[0] >= 0 && e[1] >= 0 && e[2] >= 0) blend(px, w, h, x, y, c);
        }
}

void line(std::vector<u32>& px, u32 w, u32 h, float ax, float ay, float bx, float by, u32 c) {
    const int n = int(std::ceil(std::max(std::fabs(bx - ax), std::fabs(by - ay)))) + 1;
    for (int i = 0; i <= n; ++i) {
        const float t = float(i) / float(n);
        blend(px, w, h, int(std::floor(ax + (bx - ax) * t)), int(std::floor(ay + (by - ay) * t)), c);
    }
}

// The side of the car a normal faces most: 0 left, 1 right, 2 top, 3 bottom, 4 front, 5 rear (car models:
// nose towards -Z, +X the car's right, +Y up; formats/model/car_model.h).
int side_of(const float* n) {
    const float ax = std::fabs(n[0]), ay = std::fabs(n[1]), az = std::fabs(n[2]);
    if (ax >= ay && ax >= az) return n[0] > 0 ? 1 : 0;
    if (ay >= az) return n[1] > 0 ? 2 : 3;
    return n[2] > 0 ? 5 : 4;
}

// A triangle's corners on a canvas of w x h. glTF textures repeat and a triangle may lie anywhere in UV space: `wrap` moves
// it by whole repeats so that its middle is on the picture.
void guide_points(const float (&uv)[3][2], u32 w, u32 h, bool wrap, float (&p)[3][2]) {
    float off[2] = {0, 0};
    if (wrap)
        for (int a = 0; a < 2; ++a) {
            const float f = std::floor((uv[0][a] + uv[1][a] + uv[2][a]) / 3);
            off[a] = std::isfinite(f) ? f : 0;
        }
    for (int k = 0; k < 3; ++k) {
        p[k][0] = (uv[k][0] - off[0]) * float(w);
        p[k][1] = (uv[k][1] - off[1]) * float(h);
        // (a tiling texture's far corners, and bad values: within a canvas of the picture)
        for (int a = 0; a < 2; ++a) p[k][a] = std::isfinite(p[k][a]) ? std::clamp(p[k][a], -float(a ? h : w), 2 * float(a ? h : w)) : 0;
    }
}

std::string template_ini(const std::string& model, int paint) {
    return "[livery]\nname = My livery\nauthor = \nmodel = " + model + "\npaint = " + std::to_string(paint) +
           "            ; the paint the pictures go over (they were made from it: keep it)\n"
           "; color = #1E3A8A     ; or a colour of your own under them\n";
}

void body_first(LiveryTemplate& t) {
    std::stable_sort(t.canvases.begin(), t.canvases.end(), [](const auto& a, const auto& b) { return a.body > b.body; });
    std::stable_sort(t.guides.begin(), t.guides.end(), [](const auto& a, const auto& b) { return a.body > b.body; });
}

// A mod car's distinct glTF models, the races' first.
std::vector<Bytes> glb_files(const data::ModCar& m) {
    std::vector<std::string> paths;
    for (data::CarScene s : {data::CarScene::Day, data::CarScene::Eve, data::CarScene::Night, data::CarScene::Menu})
        if (const std::string& f = m.model_file(s); !f.empty() && std::find(paths.begin(), paths.end(), f) == paths.end()) paths.push_back(f);
    std::vector<Bytes> out;
    for (const std::string& p : paths)
        if (std::optional<Bytes> b = read_host_file(p)) out.push_back(std::move(*b));
    return out;
}

std::optional<std::array<float, 3>> own_paint(const data::ModCar& m, int paint) {
    if (m.paints.empty()) return std::nullopt;
    return m.paints[size_t(std::max(paint, 0)) % m.paints.size()].second;
}

// The body file (cars/day/<model>) a livery's car draws.
std::string body_of(const data::CarDatabase* db, const std::string& model) {
    const data::ModCar* m = db ? mod_car_of_key(*db, model) : nullptr;
    const data::Car* c = m ? db->car(m->label) : nullptr;
    return c ? db->model_of(*c) : model;
}

} // namespace

std::optional<LiveryTemplate> make_livery_template(ByteSpan car_file, const std::string& model, int paint, int scale,
                                                   std::string* error) {
    auto fail = [&](const std::string& m) -> std::optional<LiveryTemplate> {
        if (error) *error = m;
        return std::nullopt;
    };
    if (!CarFile::is_car(car_file)) return fail("not a car file");
    scale = std::clamp(scale, 1, 8);
    const CarFile f = CarFile::parse(car_file);
    const gtm1::Gtm1 g = gtm1::Gtm1::parse(f.body);
    // The full-detail body, as the race draws it, in that paint (the disc's own textures: no pack, no custom paint).
    CarModelOptions opt;
    opt.lod = 0;
    opt.wheels = false;
    opt.reflection = false;
    opt.paint = paint;
    opt.resolve_paint = false;
    const scene::Model m = load_car_model(car_file, opt, model);
    // Every texture its triangles use, with them.
    std::map<std::pair<u32, u32>, std::vector<std::pair<const scene::Mesh*, size_t>>> tris; // (set, index) -> (mesh, first index)
    for (const scene::Mesh& mesh : m.meshes)
        for (const scene::Primitive& pr : mesh.primitives) {
            if (pr.texture < 0 || size_t(pr.texture) >= m.textures.size()) continue;
            u32 set = 0, index = 0;
            if (!parse_texture_name(m.textures[size_t(pr.texture)].name, set, index)) continue;
            auto& l = tris[{set, index}];
            for (u32 k = 0; k + 2 < pr.index_count; k += 3) l.emplace_back(&mesh, size_t(pr.first_index + k));
        }
    if (tris.empty()) return fail(model + ": no textured triangles at full detail");
    LiveryTemplate out;
    std::map<u32, std::vector<scene::Texture>> sets;      // the set's textures in `paint`
    std::map<u32, std::vector<bool>> recoloured;          // ... which the paints change
    // One canvas per key: a texture used under two indices (the same picture twice in a set) is painted once.
    struct Canvas {
        u32 set = 0, index = 0;
        std::vector<std::pair<const scene::Mesh*, size_t>> tris;
    };
    std::vector<std::pair<u64, Canvas>> canvases;
    for (const auto& [ti, list] : tris) {
        const u32 set = ti.first, index = ti.second;
        if (set >= g.texsets.size()) continue;
        if (!sets.count(set)) {
            ByteSpan tex1 = f.body.subspan(g.texsets[set]);
            gtm1::decode_texture_set(tex1, sets[set], "", paint, nullptr, false);
            const size_t n = gtm1::paint_variants(tex1).size();
            std::vector<bool>& rc = recoloured[set];
            rc.assign(sets[set].size(), false);
            for (size_t v = 0; v < n; ++v) {
                if (int(v) == paint) continue;
                std::vector<scene::Texture> other;
                gtm1::decode_texture_set(tex1, other, "", int(v), nullptr, false);
                for (size_t i = 0; i < other.size() && i < rc.size(); ++i) rc[i] = rc[i] || other[i].rgba != sets[set][i].rgba;
            }
        }
        if (index >= sets[set].size()) continue;
        const scene::Texture& t = sets[set][index];
        const u64 key = texture_pack::key_of(t.width, t.height, t.rgba.data());
        auto it = std::find_if(canvases.begin(), canvases.end(), [&](const auto& c) { return c.first == key; });
        if (it == canvases.end()) canvases.push_back({key, Canvas{set, index, {}}}), it = canvases.end() - 1;
        it->second.tris.insert(it->second.tris.end(), list.begin(), list.end());
    }
    for (const auto& [key, cv] : canvases) {
        const u32 set = cv.set, index = cv.index;
        const auto& list = cv.tris;
        const std::vector<scene::Texture>& ts = sets[set];
        if (index >= ts.size()) continue;
        const scene::Texture& t = ts[index];
        const u32 w = t.width * u32(scale), h = t.height * u32(scale);
        const bool body = recoloured[set][index];
        char name[96];
        std::snprintf(name, sizeof(name), "%s-set%u-%u-%s.png", body ? "body" : "part", set, index,
                      texture_pack::key_name(key).c_str());
        LiveryTemplate::Picture c;
        c.file = name, c.width = w, c.height = h, c.body = body;
        c.rgba.resize(size_t(w) * h);
        for (u32 y = 0; y < h; ++y)
            for (u32 x = 0; x < w; ++x) c.rgba[size_t(y) * w + x] = t.rgba[size_t(y / u32(scale)) * t.width + x / u32(scale)] | 0xFF000000u;
        LiveryTemplate::Picture gd;
        std::snprintf(name, sizeof(name), "guide-set%u-%u.png", set, index);
        gd.file = name, gd.width = w, gd.height = h, gd.body = body;
        gd.rgba.assign(size_t(w) * h, 0);
        for (const auto& [mesh, i0] : list) {
            float p[3][2];
            float nrm[3] = {0, 0, 0};
            for (int k = 0; k < 3; ++k) {
                const scene::Vertex& v = mesh->vertices[mesh->indices[i0 + size_t(k)]];
                p[k][0] = v.uv[0] * float(w), p[k][1] = v.uv[1] * float(h);
                for (int a = 0; a < 3; ++a) nrm[a] += v.normal[a];
            }
            fill_triangle(gd.rgba, w, h, p, kGuideSide[side_of(nrm)]);
        }
        for (const auto& [mesh, i0] : list) {
            float p[3][2];
            for (int k = 0; k < 3; ++k) {
                const scene::Vertex& v = mesh->vertices[mesh->indices[i0 + size_t(k)]];
                p[k][0] = v.uv[0] * float(w), p[k][1] = v.uv[1] * float(h);
            }
            for (int k = 0; k < 3; ++k) line(gd.rgba, w, h, p[k][0], p[k][1], p[(k + 1) % 3][0], p[(k + 1) % 3][1], 0xE0FFFFFFu);
        }
        out.canvases.push_back(std::move(c));
        out.guides.push_back(std::move(gd));
    }
    // A car of one paint (a race car, its livery in its textures): every canvas is its body's.
    if (std::none_of(out.canvases.begin(), out.canvases.end(), [](const auto& c) { return c.body; })) {
        for (auto* l : {&out.canvases, &out.guides})
            for (LiveryTemplate::Picture& p : *l) {
                p.body = true;
                if (p.file.rfind("part-", 0) == 0) p.file.replace(0, 5, "body-");
            }
    }
    body_first(out); // canvases the paints recolour first
    out.ini = template_ini(model, paint);
    return out;
}

std::optional<LiveryTemplate> make_glb_livery_template(const std::vector<Bytes>& files, const std::string& model,
                                                       const std::optional<std::array<float, 3>>& paint, int paint_index, int scale,
                                                       std::string* error) {
    auto fail = [&](const std::string& m) -> std::optional<LiveryTemplate> {
        if (error) *error = m;
        return std::nullopt;
    };
    scale = std::clamp(scale, 1, 8);
    std::vector<GlbLiveryTexture> textures; // one per key, in file order
    try {
        for (const Bytes& f : files)
            for (GlbLiveryTexture& t : glb_livery_textures(f, paint))
                if (std::none_of(textures.begin(), textures.end(), [&](const GlbLiveryTexture& x) { return x.key == t.key; }))
                    textures.push_back(std::move(t));
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    if (textures.empty()) return fail("its model has no textures or paint with texture coordinates to paint on");
    LiveryTemplate out;
    // A car whose picture is no paint material's (its look all in its textures): every canvas is its body's.
    const bool any_paint = std::any_of(textures.begin(), textures.end(), [](const GlbLiveryTexture& t) { return t.paint; });
    for (const GlbLiveryTexture& t : textures) {
        u32 w = kGlbCanvas, h = kGlbCanvas;
        if (!t.blank) {
            const u32 longest = std::max({t.width, t.height, 1u});
            const u32 k = std::clamp(kGlbCanvas / longest, 1u, u32(scale));
            w = std::min(t.width * k, texture_pack::Pack::kMaxSide), h = std::min(t.height * k, texture_pack::Pack::kMaxSide);
        }
        std::string n; // the texture's name, as far as it makes a file name
        for (char c : t.name)
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') n += c;
            else if (!n.empty() && n.back() != '-') n += '-';
        n = n.substr(0, 40);
        while (!n.empty() && n.back() == '-') n.pop_back();
        if (n.empty()) n = "texture";
        const std::string key = texture_pack::key_name(t.key);
        LiveryTemplate::Picture c;
        c.body = t.paint || !any_paint;
        c.file = std::string(c.body ? "body-" : "part-") + n + "-" + key + ".png";
        c.width = w, c.height = h;
        c.rgba.resize(size_t(w) * h);
        for (u32 y = 0; y < h; ++y) // (the texel livery_coverage compares with)
            for (u32 x = 0; x < w; ++x)
                c.rgba[size_t(y) * w + x] =
                    t.rgba[size_t(u64(y) * t.height / h) * t.width + size_t(u64(x) * t.width / w)] | 0xFF000000u;
        LiveryTemplate::Picture gd;
        gd.file = "guide-" + n + "-" + std::to_string(out.guides.size()) + ".png";
        gd.width = w, gd.height = h, gd.body = c.body;
        gd.rgba.assign(size_t(w) * h, 0);
        float p[3][2];
        for (const GlbLiveryTexture::Triangle& tri : t.triangles) {
            guide_points(tri.uv, w, h, true, p);
            fill_triangle(gd.rgba, w, h, p, kGuideSide[side_of(tri.normal)]);
        }
        for (const GlbLiveryTexture::Triangle& tri : t.triangles) {
            guide_points(tri.uv, w, h, true, p);
            for (int k = 0; k < 3; ++k) line(gd.rgba, w, h, p[k][0], p[k][1], p[(k + 1) % 3][0], p[(k + 1) % 3][1], 0xE0FFFFFFu);
        }
        out.canvases.push_back(std::move(c));
        out.guides.push_back(std::move(gd));
    }
    body_first(out);
    out.ini = template_ini(model, paint_index);
    return out;
}

std::optional<LiveryTemplate> make_livery_template(const Vfs& vfs, const data::CarDatabase& db, const std::string& model, int paint,
                                                   int scale, std::string* error) {
    auto fail = [&](const std::string& m) -> std::optional<LiveryTemplate> {
        if (error) *error = m;
        return std::nullopt;
    };
    const data::ModCar* m = mod_car_of_key(db, model);
    if (!m && model.rfind(kModPaintKey, 0) == 0) return fail("no mod car " + model.substr(std::string(kModPaintKey).size()));
    if (m && !m->model_files[0].empty()) {
        const std::vector<Bytes> files = glb_files(*m);
        return files.empty() ? fail(m->label + ": its model cannot be read")
                             : make_glb_livery_template(files, model, own_paint(*m, paint), paint, scale, error);
    }
    const std::string body = body_of(&db, model);
    const std::optional<Bytes> file = vfs.read("cars/day/" + body);
    if (!file) return fail("cars/day/" + body + " cannot be read");
    std::optional<LiveryTemplate> t = make_livery_template(*file, body, paint, scale, error);
    if (t) t->ini = template_ini(model, paint);
    return t;
}

std::map<u64, scene::Texture> livery_textures(const Vfs& vfs, const data::CarDatabase* db, const std::string& model, int paint) {
    std::map<u64, scene::Texture> out;
    const data::ModCar* mod = db ? mod_car_of_key(*db, model) : nullptr;
    if (!mod && model.rfind(kModPaintKey, 0) == 0) return out;
    if (mod && !mod->model_files[0].empty()) {
        for (const Bytes& f : glb_files(*mod)) try {
                for (GlbLiveryTexture& g : glb_livery_textures(f, own_paint(*mod, paint))) {
                    scene::Texture t;
                    t.name = g.name;
                    t.width = g.width, t.height = g.height;
                    t.rgba = std::move(g.rgba);
                    out.emplace(g.key, std::move(t));
                }
            } catch (const std::exception&) { // (a model the game cannot draw either)
            }
        return out;
    }
    std::optional<Bytes> data = vfs.read("cars/day/" + body_of(db, model));
    if (!data || !CarFile::is_car(*data)) return out;
    const CarFile f = CarFile::parse(*data);
    const gtm1::Gtm1 g = gtm1::Gtm1::parse(f.body);
    for (u32 off : g.texsets) {
        ByteSpan set = f.body.subspan(off);
        if (!Tex1::is_tex1(set)) continue;
        std::vector<scene::Texture> ts;
        gtm1::decode_texture_set(set, ts, "", paint, nullptr, false);
        for (scene::Texture& t : ts) out.emplace(texture_pack::key_of(t.width, t.height, t.rgba.data()), std::move(t));
    }
    return out;
}

} // namespace gt3r::race
