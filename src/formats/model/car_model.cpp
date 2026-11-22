#include "formats/model/car_model.h"
#include "formats/model/gltf.h"

#include "formats/model/rim_light.h"
#include "formats/model/tyre_model.h"
#include "formats/model/wheel_model.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace gt3r {

namespace {

const char* const kSectionMagic[4] = {"GTCI", "GTM1", "GTTR", "GTTW"};

// Body-based guess used when no database spec is available.
WheelSpec estimate_wheels(const scene::Model& car) {
    WheelSpec w;
    float len = car.bounds_max[2] - car.bounds_min[2];
    float half_w = std::max(std::fabs(car.bounds_min[0]), std::fabs(car.bounds_max[0]));
    w.wheelbase = 0.6f * len;
    for (int a = 0; a < 2; ++a) {
        w.width[a] = 0.22f;
        w.sidewall[a] = 0.1f;
        w.radius[a] = 0.32f;
        w.track[a] = 2 * (half_w - 0.04f) - w.width[a];
    }
    return w;
}

// Wheel i: axle i >> 1 (front, rear), side i & 1 (left = -X, right = +X).
// The wheel GTM1 is unit-sized with its outer face at x = 0 pointing -X (a
// left wheel). The game scales it by R - 0.75 * sidewall and puts the face at
// the tyre's outer sidewall; right wheels are rotated 180 degrees about Y.
// Each wheel gets its tyre (tyre_model.h): "wheel_<x>_tyre" and, at the highest
// detail, "wheel_<x>_band", the arch shade, which does not turn with the wheel.
// `brakes` (menu cars): the front and rear axles' brake disc and caliper, meshes of
// `car` itself, drawn in each wheel's frame like its rim: "wheel_<x>_brake".
void add_wheels(scene::Model& car, const scene::Model& wheel, int tyre_texture, const TyreShape& tyre,
                const TyreDetail& detail, const WheelSpec& ws, const std::array<std::vector<scene::Mesh>, 2>& brakes) {
    static const char* names[4] = {"wheel_fl", "wheel_fr", "wheel_rl", "wheel_rr"};
    int tex_base = int(car.textures.size());
    car.textures.insert(car.textures.end(), wheel.textures.begin(), wheel.textures.end());
    for (int i = 0; i < 4; ++i) {
        int axle = i >> 1;
        bool right = i & 1;
        float r = ws.radius[axle], w = ws.width[axle], sw = ws.sidewall[axle];
        float rim = r - 0.75f * sw;
        float cx = (right ? 0.5f : -0.5f) * ws.track[axle];
        float cy = r;
        float cz = (axle == 0 ? -0.5f : 0.5f) * ws.wheelbase;
        auto place = [&](scene::Vertex& v, float scale, float dx) {
            float px = v.pos[0] * scale + dx, py = v.pos[1] * scale, pz = v.pos[2] * scale;
            if (right) px = -px, pz = -pz, v.normal[0] = -v.normal[0], v.normal[2] = -v.normal[2];
            v.pos[0] = px + cx, v.pos[1] = py + cy, v.pos[2] = pz + cz;
        };
        for (const scene::Mesh& src : wheel.meshes) {
            scene::Mesh m = src;
            m.name = names[i];
            for (scene::Vertex& v : m.vertices) place(v, rim, -0.5f * w);
            for (scene::Primitive& p : m.primitives)
                if (p.texture >= 0) p.texture += tex_base;
            car.meshes.push_back(std::move(m));
        }
        // Modelled for the left wheels. A right wheel's are mirrored in z before the half
        // turn, i.e. mirrored in x about the hub: the caliper sits at the same end of the car
        // on both sides. The mirror reverses the triangles, so their corners are swapped back.
        for (const scene::Mesh& src : brakes[size_t(axle)]) {
            scene::Mesh m = src;
            m.name = std::string(names[i]) + "_brake";
            for (scene::Vertex& v : m.vertices) {
                if (right) v.pos[2] = -v.pos[2], v.normal[2] = -v.normal[2];
                place(v, rim, -0.5f * w);
            }
            if (right)
                for (size_t k = 0; k + 2 < m.indices.size(); k += 3) std::swap(m.indices[k + 1], m.indices[k + 2]);
            car.meshes.push_back(std::move(m));
        }
        TyreShape shape = tyre;
        shape.radius = r, shape.width = w, shape.sidewall = sw;
        // Where the texture starts round each tyre: the game draws it at random; a fixed
        // spread here (fractions of the golden ratio).
        const float phase = std::fmod(0.618034f * float(i + 1), 1.0f);
        for (scene::Mesh& t : build_tyre(shape, detail, tyre_texture, right, phase)) {
            t.name = std::string(names[i]) + (t.name == "band" ? "_band" : "_tyre");
            for (scene::Vertex& v : t.vertices) place(v, 1.0f, 0.0f);
            car.meshes.push_back(std::move(t));
        }
    }
}

} // namespace

bool CarFile::is_car(ByteSpan data) {
    if (data.size() < 0x40 || rd32(data, 0) != 0) return false;
    for (int i = 0; i < 4; ++i) {
        u32 o = rd32(data, 4 + 4 * i);
        if (o + 4 > data.size() || std::memcmp(data.data() + o, kSectionMagic[i], 4) != 0) return false;
    }
    return true;
}

CarFile CarFile::parse(ByteSpan data) {
    if (!is_car(data)) throw FormatError("not a car model file");
    u32 off[6];
    for (int i = 0; i < 6; ++i) off[i] = rd32(data, 4 * i);
    // The end (+0x14, the file size on the discs) is not needed to find the sections; a modified game's files leave it 0
    // or state more than they have.
    if (off[5] == 0 || off[5] > data.size()) off[5] = u32(data.size());
    for (int i = 1; i < 5; ++i)
        if (off[i + 1] < off[i]) throw FormatError("car model sections out of order");
    CarFile c;
    c.info = data.subspan(off[1], off[2] - off[1]);
    c.body = data.subspan(off[2], off[3] - off[2]);
    c.tire = data.subspan(off[3], off[4] - off[3]);
    c.wheel = data.subspan(off[4], off[5] - off[4]);
    return c;
}

std::vector<std::array<u32, 3>> car_spark_points(ByteSpan g) {
    std::vector<std::array<u32, 3>> out;
    if (g.size() < 0x60 || std::memcmp(g.data(), "GTCI", 4) != 0) return out;
    const u32 n = rd32(g, 0x58), off = rd32(g, 0x5C);
    if (n == 0 || n > 64 || u64(off) + 16ull * n > g.size()) return out;
    for (u32 i = 0; i < n; ++i) out.push_back({rd32(g, off + 16 * i), rd32(g, off + 16 * i + 4), rd32(g, off + 16 * i + 8)});
    return out;
}

namespace {

// Appends `src` to `dst`, remapping texture indices.
void merge(scene::Model& dst, scene::Model&& src) {
    int base = int(dst.textures.size());
    for (scene::Texture& t : src.textures) dst.textures.push_back(std::move(t));
    for (scene::Mesh& m : src.meshes) {
        for (scene::Primitive& p : m.primitives)
            if (p.texture >= 0) p.texture += base;
        dst.meshes.push_back(std::move(m));
    }
}

scene::Model car_lod(const CarFile& f, const gtm1::Gtm1& g, const CarModelOptions& opt, int lod,
                     const std::string& name, gtm1::MorphDraws* morphs, std::vector<gtm1::ClutSet>* clut_sets) {
    gtm1::Options o;
    o.run.lod = lod;
    o.run.brake = opt.brake;
    o.run.callback = &gtm1::kCarCallbackDefaults;
    o.run.every_branch = morphs != nullptr; // (the draws under the callback's branches go with the morphs)
    o.morphs = morphs;
    o.clut_sets = clut_sets;
    o.reflection = opt.reflection;
    // The menus' brake discs and calipers: nodes 2 (front) and 3 (rear) of a menu car's
    // body, drawn with the wheels. Race car files leave both empty.
    const bool brakes = opt.wheels && opt.menu_tyres;
    o.node = opt.effects || brakes || opt.floor_shadow ? -1 : 0;
    o.paint = opt.paint;
    o.program_alpha_test = true;  // wheel-arch / grille cut-outs
    o.weighted_reflection = true; // reflection passes weighted by the body's texel alpha
    scene::Model m = gtm1::build_model(g, o, name);
    std::array<std::vector<scene::Mesh>, 2> brake_parts;
    if (brakes || (opt.floor_shadow && !opt.effects)) {
        std::vector<scene::Mesh> kept;
        for (scene::Mesh& mesh : m.meshes) {
            if (brakes && (mesh.name == "node2" || mesh.name == "node3"))
                brake_parts[mesh.name == "node3"].push_back(std::move(mesh));
            else if (opt.effects || mesh.name == "node0" || (opt.floor_shadow && mesh.name == "node6"))
                kept.push_back(std::move(mesh));
        }
        m.meshes = std::move(kept);
    }
    for (scene::Mesh& mesh : m.meshes)
        if (mesh.name == "node0") mesh.name = "body";
    m.compute_bounds();
    if (opt.wheels) {
        gtm1::Options wo;
        wo.reflection = opt.reflection;
        wo.run.lod = lod;
        wo.program_alpha_test = true;
        wo.weighted_reflection = true;
        wo.runtime_set = 1; // the rim image
        // Showroom wheels: LOD 0's rim and tyre texture from the menu car file.
        const bool menu_car = CarFile::is_car(opt.showroom_car);
        const bool showroom = lod == 0 && menu_car;
        const CarFile src = showroom ? CarFile::parse(opt.showroom_car) : f;
        const ByteSpan rim = showroom && !opt.showroom_wheel.empty() ? opt.showroom_wheel
                             : !opt.wheel_override.empty()           ? opt.wheel_override
                                                                     : src.wheel;
        // Whether the rim is a menus' one (menu/cars, menu/wheel): the showroom's, or below, one standing in.
        bool from_menus = showroom && (!opt.showroom_wheel.empty() || opt.wheel_override.empty());
        scene::Model wheel;
        std::string rim_error;
        try {
            wheel = load_wheel_model(rim, wo, "wheel");
        } catch (const std::exception& e) {
            rim_error = e.what();
        }
        // A rim with nothing to draw at its full detail (a modified game's wheel that only its other kind of model has):
        // that one stands in at its full detail at every LOD, whatever the showroom wheels setting: the showroom one
        // (a fitted wheel's own menu rim, else the car's), else stand_in_rim. GT3's own race rims draw nothing at the
        // lowest LOD; those stay empty.
        auto draws_at_full_detail = [&](ByteSpan r) {
            gtm1::Options o = wo;
            o.run.lod = 0;
            try {
                return !load_wheel_model(r, o, "wheel").meshes.empty();
            } catch (const std::exception&) {
                return false;
            }
        };
        const bool no_rim = wheel.meshes.empty() && (lod == 0 || !draws_at_full_detail(rim));
        if (!showroom && menu_car && no_rim) {
            const ByteSpan menu_rim = !opt.showroom_wheel.empty() ? opt.showroom_wheel
                                      : opt.wheel_override.empty() ? CarFile::parse(opt.showroom_car).wheel
                                                                   : ByteSpan{};
            if (!menu_rim.empty()) {
                gtm1::Options mo = wo;
                mo.run.lod = 0;
                wheel = load_wheel_model(menu_rim, mo, "wheel");
                from_menus = true;
            }
        }
        if (no_rim && wheel.meshes.empty() && opt.stand_in_rim)
            if (std::optional<Bytes> other = opt.stand_in_rim(); other && WheelFile::is_wheel(*other)) {
                gtm1::Options so = wo;
                so.run.lod = 0;
                wheel = load_wheel_model(*other, so, "wheel");
                from_menus = opt.even_menu_rim_light; // (races stand the menus' rim in, the menus the race's)
            }
        if (wheel.meshes.empty() && !rim_error.empty()) throw FormatError(rim_error);
        if (opt.even_menu_rim_light && from_menus) even_rim_light(wheel);
        int tyre_tex = -1;
        TyreShape tyre;
        if (src.tire.size() > 0x20 && rd32(src.tire, 0x1C) < src.tire.size()) {
            size_t first = m.textures.size();
            tyre_tex = gtm1::decode_texture_set(src.tire.subspan(rd32(src.tire, 0x1C)), m.textures, "tyre_");
            // The game samples it repeating round the tyre, clamped across, without mipmaps.
            for (size_t t = first; t < m.textures.size(); ++t) {
                m.textures[t].clamp_u = false, m.textures[t].clamp_v = true;
                m.textures[t].no_mips = true;
            }
            tyre.repeats = float(std::clamp<u32>(rd32(src.tire, 0x10), 1, 8));
            tyre.flags = rd32(src.tire, 0x14);
            u32 s = rd32(src.tire, 0x18);
            std::memcpy(&tyre.shininess, &s, 4);
        }
        const TyreDetail detail = opt.menu_tyres || showroom ? menu_tyre_detail() : race_tyre_detail(lod);
        const WheelSpec ws = opt.wheel_spec ? *opt.wheel_spec : estimate_wheels(m);
        add_wheels(m, wheel, tyre_tex, tyre, detail, ws, brake_parts);
    }
    for (scene::Mesh& mesh : m.meshes) mesh.lod = lod;
    return m;
}

} // namespace

int gtm1_lod_count(const gtm1::Gtm1& g, u32 node) {
    std::vector<gtm1::Draw> prev = g.run(node, {0, 0, 0});
    for (int lod = 1; lod < 8; ++lod) {
        std::vector<gtm1::Draw> cur = g.run(node, {lod, 0, 0});
        bool same = cur.size() == prev.size();
        for (size_t i = 0; same && i < cur.size(); ++i) same = cur[i].chunk == prev[i].chunk;
        if (same) return lod;
        prev = std::move(cur);
    }
    return 8;
}

void fit_body(scene::Model& m, const BodyFit& f) {
    if (f.from_wheelbase <= 0 || f.to_wheelbase <= 0 || f.from_width <= 0 || f.from_height <= 0) return;
    const float hw0 = f.from_wheelbase * 0.0005f, hw1 = f.to_wheelbase * 0.0005f; // half wheelbase, m
    const float oh0 = f.from_length - f.from_wheelbase, oh1 = f.to_length - f.to_wheelbase;
    const float kz = hw1 / hw0, ko = oh0 > 1 && oh1 > 1 ? oh1 / oh0 : kz;
    const float kx = f.to_width / f.from_width, ky = f.to_height / f.from_height;
    for (scene::Mesh& mesh : m.meshes) {
        if (mesh.name.rfind("wheel_", 0) == 0) continue;
        for (scene::Vertex& v : mesh.vertices) {
            float z = v.pos[2], a = std::fabs(z);
            float dz = a <= hw0 ? kz : ko; // slope of the map at z (for the normals)
            v.pos[2] = a <= hw0 ? z * kz : std::copysign(hw1 + (a - hw0) * ko, z);
            v.pos[0] *= kx;
            v.pos[1] *= ky;
            float n[3] = {v.normal[0] / kx, v.normal[1] / ky, v.normal[2] / dz};
            float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (l > 0)
                for (int i = 0; i < 3; ++i) v.normal[i] = n[i] / l;
        }
    }
    m.compute_bounds();
}

namespace {

// Mod cars: stretch the borrowed body, then (with a glTF body) put the mod's
// body in place of the car file's, at every LOD the file has.
void apply_body_mod(scene::Model& m, const CarBodyMod& mod, const std::string& name) {
    if (mod.fit) fit_body(m, *mod.fit);
    if (!mod.glb) return;
    GlbOptions go;
    go.paint = mod.paint;
    scene::Model own = load_glb(*mod.glb, go, name);
    std::vector<int> lods;
    std::vector<scene::Mesh> kept;
    for (scene::Mesh& mesh : m.meshes) {
        bool body = mesh.name.rfind("wheel_", 0) != 0 && mesh.name.rfind("node", 0) != 0;
        if (body) {
            if (std::find(lods.begin(), lods.end(), mesh.lod) == lods.end()) lods.push_back(mesh.lod);
        } else {
            kept.push_back(std::move(mesh));
        }
    }
    if (lods.empty()) lods.push_back(0);
    const int tex0 = int(m.textures.size());
    for (scene::Texture& t : own.textures) m.textures.push_back(t);
    m.meshes = std::move(kept);
    for (scene::Mesh& mesh : own.meshes)
        for (scene::Vertex& v : mesh.vertices) v.pos[1] -= mod.glb_ground;
    for (int lod : lods)
        for (const scene::Mesh& src : own.meshes) {
            scene::Mesh mesh = src;
            mesh.lod = lod;
            for (scene::Primitive& p : mesh.primitives)
                if (p.texture >= 0) p.texture += tex0;
            m.meshes.push_back(std::move(mesh));
        }
}

} // namespace

namespace {

std::optional<Bytes> car_rim(const FileReader& read, const std::string& path) {
    std::optional<Bytes> car = read(path);
    if (!car || !CarFile::is_car(*car)) return std::nullopt;
    const ByteSpan w = CarFile::parse(*car).wheel;
    return Bytes(w.begin(), w.end());
}

} // namespace

std::optional<Bytes> race_rim(const FileReader& read, const std::string& model, const std::string& wheel) {
    return wheel.empty() ? car_rim(read, "cars/day/" + model) : read("wheel/" + wheel);
}

std::optional<Bytes> menu_rim(const FileReader& read, const std::string& model, const std::string& wheel) {
    return wheel.empty() ? car_rim(read, "menu/cars/" + model) : read("menu/wheel/" + wheel);
}

void mark_car_glass(scene::Model& model) {
    using V = std::array<float, 3>;
    constexpr u32 kSkip = scene::kAdditive | scene::kEnvironmentMap | scene::kMultiplyDest | scene::kAlphaOnly |
                          scene::kDepthOnly;
    constexpr u32 kOver = scene::kEnvironmentMap | scene::kAlphaOnly | scene::kDepthOnly;
    constexpr double kGlassDark = 100; // glass: the brightest channel's mean (0..255) at most ...
    constexpr double kGlassGrey = 40;  // ... the channel means this close (grey: not a lamp lens) ...
    constexpr float kGlassLow = 0.45f; // ... and its lowest corner above this share of the body's height
    constexpr double kBackingDark = 48; // the opaque dark panels behind it: that mean at most, ...
    constexpr double kBackingFlat = 0.35; // ... and this share of shaded texels at most: flat black
                                          // panes, not the painted interiors (dash, seats) GT3 shows
                                          // through the glass
    constexpr float kBackingGap = 0.15f; // a backing triangle's centre this close to the glass (metres)
    constexpr float kBackingArea = 0.05f; // and a part's such triangles this large together (m2): not a mirror // a backing triangle's centre this close to the glass (metres)
    constexpr float kBelowTop = 0.03f;   // and this far below the glass's top (the roof stays), ...
    constexpr float kBelowBottom = 0.05f; // ... its corners no further below the glass's bottom
    struct Shade {
        bool known = false;
        double brightest = 0, chroma = 0;
        double shaded = 0; // share of the texels left by the alpha test that are dark grey, not black
    };
    std::vector<Shade> shades(model.textures.size());
    auto shade = [&](int t) -> const Shade* {
        if (t < 0 || size_t(t) >= model.textures.size() || model.textures[size_t(t)].rgba.empty()) return nullptr;
        Shade& s = shades[size_t(t)];
        if (!s.known) {
            const std::vector<u32>& px = model.textures[size_t(t)].rgba;
            double sum[3] = {0, 0, 0};
            for (u32 c : px)
                for (int k = 0; k < 3; ++k) sum[k] += double((c >> (8 * k)) & 255);
            for (double& v : sum) v /= double(px.size());
            s.known = true, s.brightest = std::max({sum[0], sum[1], sum[2]});
            s.chroma = s.brightest - std::min({sum[0], sum[1], sum[2]});
            size_t kept = 0, grey = 0;
            for (u32 c : px) {
                if ((c >> 24) <= 0x20) continue; // (cut away by the alpha test)
                const u32 m = std::max({c & 255, (c >> 8) & 255, (c >> 16) & 255});
                ++kept, grey += m > 6 && m <= 60;
            }
            s.shaded = kept ? double(grey) / double(kept) : 0;
        }
        return &s;
    };
    auto sub = [](V a, V b) { return V{a[0] - b[0], a[1] - b[1], a[2] - b[2]}; };
    auto dot = [](V a, V b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    auto along = [](V a, V d, float t) { return V{a[0] + d[0] * t, a[1] + d[1] * t, a[2] + d[2] * t}; };
    // The point of triangle abc closest to p (Ericson, Real-Time Collision Detection 5.1.5).
    auto closest = [&](V p, V a, V b, V c) -> V {
        const V ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
        const float d1 = dot(ab, ap), d2 = dot(ac, ap);
        if (d1 <= 0 && d2 <= 0) return a;
        const V bp = sub(p, b);
        const float d3 = dot(ab, bp), d4 = dot(ac, bp);
        if (d3 >= 0 && d4 <= d3) return b;
        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0 && d1 >= 0 && d3 <= 0) return along(a, ab, d1 / (d1 - d3));
        const V cp = sub(p, c);
        const float d5 = dot(ab, cp), d6 = dot(ac, cp);
        if (d6 >= 0 && d5 <= d6) return c;
        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0 && d2 >= 0 && d6 <= 0) return along(a, ac, d2 / (d2 - d6));
        const float va = d3 * d6 - d5 * d4;
        if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) return along(b, sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6)));
        const float den = 1 / (va + vb + vc);
        return along(along(a, ab, vb * den), ac, vc * den);
    };
    for (scene::Mesh& mesh : model.meshes) {
        if (mesh.name != "body") continue;
        const auto at = [&](u32 index) {
            const float* p = mesh.vertices[index].pos;
            return V{p[0], p[1], p[2]};
        };
        const auto valid = [&](const scene::Primitive& p) {
            return size_t(p.first_index) + p.index_count <= mesh.indices.size() && p.index_count % 3 == 0 &&
                   std::all_of(mesh.indices.begin() + p.first_index, mesh.indices.begin() + p.first_index + p.index_count,
                               [&](u32 i) { return i < mesh.vertices.size(); });
        };
        float lo = INFINITY, hi = -INFINITY;
        for (const scene::Vertex& v : mesh.vertices) lo = std::min(lo, v.pos[1]), hi = std::max(hi, v.pos[1]);
        const float low = lo + kGlassLow * (hi - lo);

        // The glass: plain blended passes with a dark grey texture, high on the body.
        std::vector<std::array<V, 3>> glass;
        std::vector<V> corners;
        float glass_top = -INFINITY, glass_bottom = INFINITY;
        for (scene::Primitive& p : mesh.primitives) {
            if (!(p.flags & scene::kAlphaBlend) || (p.flags & kSkip) || !valid(p)) continue;
            const Shade* sh = shade(p.texture);
            if (!sh || sh->brightest > kGlassDark || sh->chroma > kGlassGrey) continue;
            float bottom = INFINITY, top = -INFINITY;
            for (u32 i = p.first_index; i < p.first_index + p.index_count; ++i) {
                const float y = at(mesh.indices[i])[1];
                bottom = std::min(bottom, y), top = std::max(top, y);
            }
            if (!(bottom > low)) continue;
            p.flags |= scene::kGlass;
            glass_top = std::max(glass_top, top), glass_bottom = std::min(glass_bottom, bottom);
            for (u32 i = p.first_index; i + 2 < p.first_index + p.index_count; i += 3)
                glass.push_back({at(mesh.indices[i]), at(mesh.indices[i + 1]), at(mesh.indices[i + 2])});
            for (u32 i = p.first_index; i < p.first_index + p.index_count; ++i) corners.push_back(at(mesh.indices[i]));
        }
        if (glass.empty()) continue;
        std::sort(corners.begin(), corners.end());

        // The passes drawn over it (reflection, alpha, depth) go with it.
        for (scene::Primitive& p : mesh.primitives) {
            if (!(p.flags & kOver) || (p.flags & scene::kGlass) || !p.index_count || !valid(p)) continue;
            if (std::all_of(mesh.indices.begin() + p.first_index, mesh.indices.begin() + p.first_index + p.index_count,
                            [&](u32 i) { return std::binary_search(corners.begin(), corners.end(), at(i)); }))
                p.flags |= scene::kGlass;
        }

        // The dark panels set just behind it (GT3 blacks the windows out from outside with them):
        // their triangles close to the glass and below its top. A part with some of them is split.
        auto backing = [&](u32 i) {
            const V a = at(mesh.indices[i]), b = at(mesh.indices[i + 1]), c = at(mesh.indices[i + 2]);
            const V m{(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3};
            if (m[1] > glass_top - kBelowTop || std::min({a[1], b[1], c[1]}) < glass_bottom - kBelowBottom) return false;
            for (const auto& t : glass) {
                const V d = sub(m, closest(m, t[0], t[1], t[2]));
                if (dot(d, d) <= kBackingGap * kBackingGap) return true;
            }
            return false;
        };
        std::vector<scene::Primitive> out;
        out.reserve(mesh.primitives.size());
        for (const scene::Primitive& p : mesh.primitives) {
            const Shade* sh = shade(p.texture);
            if (!(p.flags & scene::kAlphaTest) || (p.flags & (kSkip | scene::kAlphaBlend | scene::kGlass)) || !sh ||
                sh->brightest > kBackingDark || sh->shaded > kBackingFlat || !valid(p)) {
                out.push_back(p);
                continue;
            }
            std::vector<u32> keep, hide;
            for (u32 i = p.first_index; i + 2 < p.first_index + p.index_count; i += 3) {
                std::vector<u32>& to = backing(i) ? hide : keep;
                to.insert(to.end(), mesh.indices.begin() + i, mesh.indices.begin() + i + 3);
            }
            float area = 0;
            for (size_t i = 0; i + 2 < hide.size(); i += 3) {
                const V u = sub(at(hide[i + 1]), at(hide[i])), w = sub(at(hide[i + 2]), at(hide[i]));
                const V n{u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
                area += 0.5f * std::sqrt(dot(n, n));
            }
            if (hide.empty() || area < kBackingArea) {
                out.push_back(p);
                continue;
            }
            if (keep.empty()) {
                out.push_back(p);
                out.back().flags |= scene::kGlass;
                continue;
            }
            // (new index ranges at the end: other primitives may share this one's)
            scene::Primitive k = p, h = p;
            k.first_index = u32(mesh.indices.size()), k.index_count = u32(keep.size());
            mesh.indices.insert(mesh.indices.end(), keep.begin(), keep.end());
            h.first_index = u32(mesh.indices.size()), h.index_count = u32(hide.size());
            mesh.indices.insert(mesh.indices.end(), hide.begin(), hide.end());
            h.flags |= scene::kGlass;
            out.push_back(k);
            out.push_back(h);
        }
        mesh.primitives = std::move(out);
    }
}

scene::Model load_car_model(ByteSpan data, const CarModelOptions& opt, const std::string& name) {
    CarFile f = CarFile::parse(data);
    gtm1::Gtm1 g = gtm1::Gtm1::parse(f.body);
    // (Mod bodies keep the morph draws in place: their parts would not follow the new body.)
    const bool morphs = opt.morphs && !opt.body_mod.active();
    if (morphs) opt.morphs->clear();
    if (opt.clut_sets) opt.clut_sets->clear();
    if (opt.lod >= 0) {
        gtm1::MorphDraws md;
        scene::Model m = car_lod(f, g, opt, opt.lod, name, morphs ? &md : nullptr, opt.clut_sets);
        mark_car_glass(m);
        if (opt.body_mod.active()) apply_body_mod(m, opt.body_mod, name);
        m.compute_bounds();
        if (!md.empty()) opt.morphs->push_back(std::move(md));
        return m;
    }
    scene::Model all;
    all.name = name;
    int n = g.nodes.empty() ? 0 : gtm1_lod_count(g, 0);
    for (int lod = 0; lod < n; ++lod) {
        gtm1::MorphDraws md;
        const int base = int(all.textures.size());
        std::vector<gtm1::ClutSet> cluts;
        scene::Model m = car_lod(f, g, opt, lod, name, morphs ? &md : nullptr, opt.clut_sets ? &cluts : nullptr);
        mark_car_glass(m);
        merge(all, std::move(m));
        for (gtm1::ClutSet& c : cluts) {
            c.first += base;
            opt.clut_sets->push_back(std::move(c));
        }
        if (!md.empty()) {
            md.rebase_textures(base);
            opt.morphs->push_back(std::move(md));
        }
    }
    if (opt.body_mod.active()) apply_body_mod(all, opt.body_mod, name);
    all.compute_bounds();
    return all;
}

} // namespace gt3r
