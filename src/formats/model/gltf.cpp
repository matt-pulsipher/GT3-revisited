#include "formats/model/gltf.h"

#include "formats/json.h"
#include "formats/model/gtm1.h"
#include "formats/png_reader.h"
#include "formats/png_writer.h"
#include "formats/texture_pack.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <optional>

namespace gt3r {

namespace {

using json::Value;

struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // column-major
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + rr] * b.m[c * 4 + k];
                r.m[c * 4 + rr] = s;
            }
        return r;
    }
    void point(const float* p, float* out) const {
        for (int r = 0; r < 3; ++r) out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
    }
    void dir(const float* p, float* out) const {
        for (int r = 0; r < 3; ++r) out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2];
    }
};

Mat4 node_matrix(const Value& n) {
    Mat4 out;
    if (n.has("matrix") && n["matrix"].size() == 16) {
        for (int i = 0; i < 16; ++i) out.m[i] = float(n["matrix"][size_t(i)].number());
        return out;
    }
    float t[3] = {0, 0, 0}, s[3] = {1, 1, 1}, q[4] = {0, 0, 0, 1};
    for (int i = 0; i < 3; ++i) {
        if (n["translation"].size() == 3) t[i] = float(n["translation"][size_t(i)].number());
        if (n["scale"].size() == 3) s[i] = float(n["scale"][size_t(i)].number());
    }
    if (n["rotation"].size() == 4)
        for (int i = 0; i < 4; ++i) q[i] = float(n["rotation"][size_t(i)].number());
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
                  2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                  2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
    for (int c = 0; c < 3; ++c)
        for (int rr = 0; rr < 3; ++rr) out.m[c * 4 + rr] = r[c * 3 + rr] * s[c];
    out.m[12] = t[0], out.m[13] = t[1], out.m[14] = t[2];
    return out;
}

struct Glb {
    Value doc;
    ByteSpan bin;

    // Element `i` component `c` of an accessor, as float (normalised integers scaled to 0..1).
    struct Accessor {
        const u8* base = nullptr;
        size_t count = 0, stride = 0, comps = 0;
        int ctype = 0;
        bool normalized = false;
        float get(size_t i, size_t c) const {
            const u8* p = base + i * stride;
            switch (ctype) {
            case 5126: { float f; std::memcpy(&f, p + c * 4, 4); return f; }
            case 5125: { u32 v; std::memcpy(&v, p + c * 4, 4); return float(v); }
            case 5123: { u16 v; std::memcpy(&v, p + c * 2, 2); return normalized ? v / 65535.0f : float(v); }
            case 5121: { u8 v = p[c]; return normalized ? v / 255.0f : float(v); }
            case 5122: { s16 v; std::memcpy(&v, p + c * 2, 2); return normalized ? std::max(v / 32767.0f, -1.0f) : float(v); }
            case 5120: { s8 v = s8(p[c]); return normalized ? std::max(v / 127.0f, -1.0f) : float(v); }
            }
            return 0;
        }
        u32 index(size_t i) const { return u32(get(i, 0)); }
    };

    // A size or offset from the document: whole, non-negative and at most `max` (the file
    // is untrusted: a mod, or a car downloaded from another player).
    static size_t size_of(const Value& v, size_t max, const char* what) {
        double d = v.number();
        if (!std::isfinite(d) || d < 0 || d > double(max) || d != std::floor(d))
            throw FormatError(std::string("glTF: bad ") + what);
        return size_t(d);
    }

    ByteSpan view(int v) const {
        if (v < 0) throw FormatError("glTF: missing buffer view");
        const Value& bv = doc["bufferViews"][size_t(v)];
        if (!bv.is_object()) throw FormatError("glTF: missing buffer view");
        if (bv["buffer"].integer() != 0) throw FormatError("glTF: only the GLB binary buffer is supported");
        size_t off = size_of(bv["byteOffset"], bin.size(), "buffer view offset");
        size_t len = size_of(bv["byteLength"], bin.size() - off, "buffer view length");
        return bin.subspan(off, len);
    }

    Accessor accessor(int a) const {
        const Value& ac = doc["accessors"][size_t(a)];
        if (!ac.is_object()) throw FormatError("glTF: missing accessor");
        if (ac.has("sparse")) throw FormatError("glTF: sparse accessors are not supported");
        static const std::map<std::string, size_t> kComps = {{"SCALAR", 1}, {"VEC2", 2}, {"VEC3", 3}, {"VEC4", 4}};
        Accessor r;
        auto it = kComps.find(ac["type"].string());
        if (it == kComps.end()) throw FormatError("glTF: unsupported accessor type " + ac["type"].string());
        r.comps = it->second;
        r.ctype = ac["componentType"].integer();
        r.normalized = ac["normalized"].boolean();
        size_t csize = r.ctype == 5126 || r.ctype == 5125 ? 4 : r.ctype == 5123 || r.ctype == 5122 ? 2 : 1;
        ByteSpan v = view(ac["bufferView"].integer());
        // Each element takes at least a byte, so a count beyond the view's size is malformed (and
        // would overflow the range check below).
        r.count = size_of(ac["count"], v.size(), "accessor count");
        size_t stride = size_of(doc["bufferViews"][size_t(ac["bufferView"].integer())]["byteStride"], 252, "byte stride");
        r.stride = stride ? stride : csize * r.comps;
        size_t off = size_of(ac["byteOffset"], v.size(), "accessor offset");
        if (r.count && (u64(r.count) - 1) * r.stride + csize * r.comps > u64(v.size() - off))
            throw FormatError("glTF: accessor out of range");
        r.base = v.data() + off;
        return r;
    }
};

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

enum class Role { Plain, Paint, Glass, Metal, BrakeLamp, Lamp };

Role role_of(const std::string& name) {
    std::string n = lower(name);
    auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
    if (has("paint") || has("body")) return Role::Paint;
    if (has("glass") || has("window")) return Role::Glass;
    if (has("chrome") || has("metal")) return Role::Metal;
    if (has("brake") || has("tail")) return Role::BrakeLamp;
    if (has("head") || has("lamp") || has("light")) return Role::Lamp;
    return Role::Plain;
}

u32 rgba8(float r, float g, float b, float a) {
    auto c = [](float v) { return u32(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return c(r) | c(g) << 8 | c(b) << 16 | c(a) << 24;
}

// What a livery needs of a body primitive (apply_livery, glb_livery_textures).
struct LiveryPart {
    size_t primitive = 0;       // in the body mesh
    u32 first = 0, count = 0;   // its vertices
    Role role = Role::Plain;
    std::string material;
    float own[3] = {1, 1, 1};  // the material's base colour without GlbOptions::paint
    float base[3] = {1, 1, 1}; // ... as drawn
    bool uv = false;
    std::vector<u32> plain; // its vertex colours without the base colour (with a livery)
};

u32 tinted(u32 c, const float* f) {
    u32 v = c & 0xFF000000u;
    for (int k = 0; k < 3; ++k)
        v |= u32(std::lround(std::clamp(float((c >> (8 * k)) & 0xFF) * f[k], 0.0f, 255.0f))) << (8 * k);
    return v;
}

// The body's primitives by the key of their texture, in file order: those a livery can cover.
std::vector<std::pair<u64, std::vector<const LiveryPart*>>> livery_groups(const scene::Model& m, const scene::Mesh& body,
                                                                          const std::vector<LiveryPart>& parts) {
    std::map<int, u64> keys; // by texture
    std::vector<std::pair<u64, std::vector<const LiveryPart*>>> out;
    for (const LiveryPart& p : parts) {
        const int tex = body.primitives[p.primitive].texture;
        // (untextured materials all share the white texture: a picture on it is the paint's)
        if (!p.uv || tex < 0 || (tex == 0 && p.role != Role::Paint)) continue;
        const scene::Texture& t = m.textures[size_t(tex)];
        auto it = keys.find(tex);
        if (it == keys.end()) it = keys.emplace(tex, texture_pack::key_of(t.width, t.height, t.rgba.data())).first;
        auto g = std::find_if(out.begin(), out.end(), [&](const auto& e) { return e.first == it->second; });
        if (g == out.end()) out.push_back({it->second, {}}), g = out.end() - 1;
        g->second.push_back(&p);
    }
    return out;
}

// The part whose base colour the group's template shows: the first paint material's, else the first.
const LiveryPart& shown_part(const std::vector<const LiveryPart*>& g) {
    for (const LiveryPart* p : g)
        if (p->role == Role::Paint) return *p;
    return *g.front();
}

void apply_livery(scene::Model& m, scene::Mesh& body, const std::vector<LiveryPart>& parts, const GlbOptions& opt) {
    for (const auto& [key, group] : livery_groups(m, body, parts)) {
        std::shared_ptr<const texture_pack::Image> pic = opt.livery->find(key);
        if (!pic) continue;
        // The picture's template: the texture in the paint it was painted over.
        const LiveryPart& shown = shown_part(group);
        const float* under = shown.role == Role::Paint && opt.livery_paint ? opt.livery_paint->data() : shown.own;
        const scene::Texture raw = m.textures[size_t(body.primitives[shown.primitive].texture)];
        std::vector<u32> templ(raw.rgba.size());
        for (size_t i = 0; i < templ.size(); ++i) templ[i] = tinted(raw.rgba[i], under);
        // One copy per base colour the texture is drawn in, the colour in its texels.
        std::map<std::array<float, 3>, int> copies; // -1: the picture covers nothing
        for (const LiveryPart* p : group) {
            const std::array<float, 3> base{p->base[0], p->base[1], p->base[2]};
            auto it = copies.find(base);
            if (it == copies.end()) {
                scene::Texture t = raw;
                for (u32& c : t.rgba) c = tinted(c, p->base);
                gtm1::overlay_livery(t, *pic, {templ.data(), raw.width, raw.height});
                it = copies.emplace(base, t.livery_cover ? int(m.textures.size()) : -1).first;
                if (t.livery_cover) m.textures.push_back(std::move(t));
            }
            if (it->second < 0) continue;
            body.primitives[p->primitive].texture = it->second;
            for (u32 i = 0; i < p->count; ++i) body.vertices[p->first + i].color = p->plain[i];
        }
    }
}

scene::Model load(ByteSpan d, const GlbOptions& opt, const std::string& name, std::vector<LiveryPart>* parts) {
    if (d.size() < 20 || std::memcmp(d.data(), "glTF", 4) != 0) throw FormatError("not a binary glTF (.glb) file");
    if (rd32(d, 4) != 2) throw FormatError("glTF: only version 2 is supported");
    Glb g;
    size_t o = 12;
    while (o + 8 <= d.size()) {
        u32 len = rd32(d, o), type = rd32(d, o + 4);
        if (o + 8 + len > d.size()) throw FormatError("glTF: truncated chunk");
        ByteSpan body = d.subspan(o + 8, len);
        if (type == 0x4E4F534A) g.doc = json::Value::parse(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()));
        else if (type == 0x004E4942) g.bin = body;
        o += 8 + ((len + 3) & ~3u);
    }
    if (!g.doc.is_object()) throw FormatError("glTF: no JSON chunk");

    scene::Model model;
    model.name = name;
    // Textures: index 0 = plain white (untextured materials), then the glTF images.
    model.textures.push_back({"white", 1, 1, {0xFFFFFFFFu}});
    std::map<int, int> image_tex; // glTF image -> model texture
    auto texture_for = [&](int gltf_texture) -> int {
        const Value& t = g.doc["textures"][size_t(gltf_texture)];
        int img = t["source"].integer(-1);
        if (img < 0) return 0;
        if (auto it = image_tex.find(img); it != image_tex.end()) return it->second;
        const Value& im = g.doc["images"][size_t(img)];
        int idx = 0;
        if (im.has("bufferView") && (im["mimeType"].string() == "image/png" || im["mimeType"].string().empty())) {
            PngImage png = decode_png(g.view(im["bufferView"].integer()), opt.max_texture_dim);
            scene::Texture tex;
            tex.name = im["name"].string();
            tex.width = png.width, tex.height = png.height;
            tex.rgba = std::move(png.rgba);
            model.textures.push_back(std::move(tex));
            idx = int(model.textures.size()) - 1;
        } // JPEG and external images: white (the base colour factor still applies)
        image_tex[img] = idx;
        return idx;
    };

    scene::Mesh body, env;
    body.name = "body";
    env.name = "body_env";
    // One primitive per (material) run; vertices transformed into GT3 axes.
    auto add_primitive = [&](const Value& prim, const Mat4& world) {
        int mode = prim.has("mode") ? prim["mode"].integer() : 4;
        if (mode != 4) return; // triangles only
        const Value& attrs = prim["attributes"];
        if (!attrs.has("POSITION")) return;
        Glb::Accessor pos = g.accessor(attrs["POSITION"].integer());
        std::optional<Glb::Accessor> nrm, uv, col;
        if (attrs.has("NORMAL")) nrm = g.accessor(attrs["NORMAL"].integer());
        if (attrs.has("TEXCOORD_0")) uv = g.accessor(attrs["TEXCOORD_0"].integer());
        if (attrs.has("COLOR_0")) col = g.accessor(attrs["COLOR_0"].integer());
        std::vector<u32> idx;
        if (prim.has("indices")) {
            Glb::Accessor ia = g.accessor(prim["indices"].integer());
            for (size_t i = 0; i < ia.count; ++i) idx.push_back(ia.index(i));
        } else {
            for (size_t i = 0; i < pos.count; ++i) idx.push_back(u32(i));
        }
        for (u32 i : idx)
            if (i >= pos.count) throw FormatError("glTF: index out of range");

        const Value& mat = g.doc["materials"][size_t(prim["material"].integer(-1))];
        Role role = role_of(mat["name"].string());
        const Value& pbr = mat["pbrMetallicRoughness"];
        float base[4] = {1, 1, 1, 1};
        for (int k = 0; k < 4; ++k)
            if (pbr["baseColorFactor"].size() == 4) base[k] = float(pbr["baseColorFactor"][size_t(k)].number());
        LiveryPart part;
        std::copy(base, base + 3, part.own);
        if (role == Role::Paint && opt.paint)
            for (int k = 0; k < 3; ++k) base[k] = (*opt.paint)[size_t(k)];
        int tex = pbr["baseColorTexture"].is_object() ? texture_for(pbr["baseColorTexture"]["index"].integer()) : 0;
        float emissive[3] = {0, 0, 0};
        for (int k = 0; k < 3; ++k)
            if (mat["emissiveFactor"].size() == 3) emissive[k] = float(mat["emissiveFactor"][size_t(k)].number());
        float metallic = float(pbr["metallicFactor"].number(1)), rough = float(pbr["roughnessFactor"].number(1));
        bool blend = mat["alphaMode"].string() == "BLEND" || (role == Role::Glass && base[3] < 0.999f);
        // MASK: cut out where the alpha is under the cutoff (alpha-cut spokes, grilles).
        const bool mask = !blend && mat["alphaMode"].string() == "MASK";
        const float cutoff = float(mat["alphaCutoff"].number(0.5));

        // Normal matrix: inverse transpose of the upper 3x3 (uniform scale: the matrix itself).
        Mat4 nm = world;
        nm.m[12] = nm.m[13] = nm.m[14] = 0;
        u32 first = u32(body.vertices.size());
        for (size_t i = 0; i < pos.count; ++i) {
            scene::Vertex v;
            float p[3] = {pos.get(i, 0), pos.get(i, 1), pos.get(i, 2)}, q[3];
            world.point(p, q);
            v.pos[0] = -q[0], v.pos[1] = q[1], v.pos[2] = -q[2]; // glTF faces +Z; GT3 noses point to -Z
            if (nrm) {
                float n[3] = {nrm->get(i, 0), nrm->get(i, 1), nrm->get(i, 2)}, m[3];
                nm.dir(n, m);
                float l = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
                if (l > 0) v.normal[0] = -m[0] / l, v.normal[1] = m[1] / l, v.normal[2] = -m[2] / l;
            }
            if (uv) v.uv[0] = uv->get(i, 0), v.uv[1] = uv->get(i, 1);
            float c[4] = {base[0], base[1], base[2], base[3]};
            if (col)
                for (size_t k = 0; k < std::min<size_t>(col->comps, 4); ++k) c[k] *= col->get(i, k);
            v.color = rgba8(c[0], c[1], c[2], c[3]);
            body.vertices.push_back(v);
            if (parts) {
                float w[3] = {1, 1, 1};
                if (col)
                    for (size_t k = 0; k < std::min<size_t>(col->comps, 3); ++k) w[k] = col->get(i, k);
                part.plain.push_back(rgba8(w[0], w[1], w[2], c[3]));
            }
        }
        if (!nrm) { // smooth normals from the faces
            for (size_t t = 0; t + 2 < idx.size(); t += 3) {
                float* a = body.vertices[first + idx[t]].pos;
                float* b = body.vertices[first + idx[t + 1]].pos;
                float* c = body.vertices[first + idx[t + 2]].pos;
                float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                for (int k = 0; k < 3; ++k)
                    for (u32 j : {idx[t], idx[t + 1], idx[t + 2]}) body.vertices[first + j].normal[k] += n[k];
            }
            for (u32 i = first; i < body.vertices.size(); ++i) {
                float* n = body.vertices[i].normal;
                float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (l > 0) n[0] /= l, n[1] /= l, n[2] /= l;
            }
        }
        // The half turn about Y keeps the winding (determinant +1); a mirrored node flips it.
        float det = world.m[0] * (world.m[5] * world.m[10] - world.m[9] * world.m[6]) -
                    world.m[4] * (world.m[1] * world.m[10] - world.m[9] * world.m[2]) +
                    world.m[8] * (world.m[1] * world.m[6] - world.m[5] * world.m[2]);
        scene::Primitive p;
        p.first_index = u32(body.indices.size());
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            u32 a = first + idx[t], b = first + idx[t + 1], c = first + idx[t + 2];
            if (det < 0) std::swap(b, c);
            body.indices.insert(body.indices.end(), {a, b, c});
        }
        p.index_count = u32(body.indices.size()) - p.first_index;
        p.texture = tex;
        if (blend) p.flags |= scene::kAlphaBlend | scene::kNoDepthWrite;
        if (mask) p.flags |= scene::kAlphaTest, p.alpha_ref = std::clamp(cutoff, 0.0f, 1.0f);
        if (mat["doubleSided"].boolean()) p.flags |= scene::kDoubleSided;
        if (role == Role::Paint) p.flags |= scene::kTintable;
        if (role == Role::Glass) p.flags |= scene::kGlass;
        // GT3-style fixed-function material: the base colour is in the vertex colours.
        p.material.valid = true;
        float spec = role == Role::Paint   ? 0.55f
                     : role == Role::Glass ? 0.8f
                     : role == Role::Metal ? 0.7f
                                           : 0.15f + 0.4f * metallic * (1 - rough);
        for (int k = 0; k < 3; ++k) {
            p.material.ambient[k] = 0.42f;
            p.material.diffuse[k] = 0.72f;
            p.material.specular[k] = spec;
            p.material.emission[k] = role == Role::Lamp ? 0.6f : emissive[k];
        }
        if (role == Role::BrakeLamp) p.material.lamp_emission[0] = 1.2f, p.material.lamp_emission[1] = 0.25f;
        p.material.shininess = role == Role::Paint ? 24 : role == Role::Glass || role == Role::Metal ? 48 : 8 + 40 * (1 - rough);
        if (p.index_count) body.primitives.push_back(p);
        if (parts && p.index_count) {
            part.primitive = body.primitives.size() - 1;
            part.first = first, part.count = u32(body.vertices.size()) - first;
            part.role = role;
            part.material = mat["name"].string();
            std::copy(base, base + 3, part.base);
            part.uv = uv.has_value();
            parts->push_back(std::move(part));
        }

        // Reflection pass (paint, glass and metal), like GT3's car env-map packets.
        // (Not over a cut-out: the pass would fill its holes.)
        if (opt.reflections && (role == Role::Paint || role == Role::Glass || role == Role::Metal) && !mask &&
            p.index_count) {
            u32 efirst = u32(env.vertices.size());
            float w = role == Role::Glass ? 0.55f : role == Role::Metal ? 0.45f : 0.32f;
            for (u32 i = first; i < body.vertices.size(); ++i) {
                scene::Vertex v = body.vertices[i];
                v.color = rgba8(w, w, w, 1);
                env.vertices.push_back(v);
            }
            scene::Primitive e;
            e.first_index = u32(env.indices.size());
            for (u32 k = p.first_index; k < p.first_index + p.index_count; ++k) env.indices.push_back(body.indices[k] - first + efirst);
            e.index_count = p.index_count;
            e.flags = scene::kAdditive | scene::kEnvironmentMap | scene::kUnlit | scene::kNoDepthWrite |
                      (role == Role::Glass ? scene::kGlass : 0u);
            e.env_rows = true;
            for (int k = 0; k < 3; ++k) e.env_c0[k] = 1.0f, e.env_c1[k] = 0.8f; // 1 - 0.8 cos: Fresnel-like
            env.primitives.push_back(e);
        }
    };

    std::function<void(int, const Mat4&, int)> visit = [&](int n, const Mat4& parent, int depth) {
        if (depth > 64) throw FormatError("glTF: node hierarchy too deep");
        const Value& node = g.doc["nodes"][size_t(n)];
        Mat4 world = parent * node_matrix(node);
        if (node.has("mesh"))
            for (const Value& prim : g.doc["meshes"][size_t(node["mesh"].integer())]["primitives"].items()) add_primitive(prim, world);
        for (const Value& c : node["children"].items()) visit(c.integer(), world, depth + 1);
    };
    const Value& scenes = g.doc["scenes"];
    if (scenes.size()) {
        for (const Value& n : scenes[size_t(g.doc["scene"].integer(0))]["nodes"].items()) visit(n.integer(), Mat4{}, 0);
    } else {
        for (size_t i = 0; i < g.doc["nodes"].size(); ++i) visit(int(i), Mat4{}, 0);
    }
    if (body.indices.empty()) throw FormatError("glTF: no triangles");
    if (opt.livery && parts) apply_livery(model, body, *parts, opt);
    model.meshes.push_back(std::move(body));
    if (!env.indices.empty()) model.meshes.push_back(std::move(env));
    model.compute_bounds();
    return model;
}

} // namespace

scene::Model load_glb(ByteSpan d, const GlbOptions& opt, const std::string& name) {
    std::vector<LiveryPart> parts;
    return load(d, opt, name, opt.livery ? &parts : nullptr);
}

std::vector<GlbLiveryTexture> glb_livery_textures(ByteSpan d, const std::optional<std::array<float, 3>>& paint, u32 max_texture_dim) {
    GlbOptions opt;
    opt.paint = paint;
    opt.reflections = false;
    opt.max_texture_dim = max_texture_dim;
    std::vector<LiveryPart> parts;
    const scene::Model m = load(d, opt, {}, &parts);
    const scene::Mesh& body = m.meshes.front();
    std::vector<GlbLiveryTexture> out;
    for (const auto& [key, group] : livery_groups(m, body, parts)) {
        const LiveryPart& shown = shown_part(group);
        const int tex = body.primitives[shown.primitive].texture;
        const scene::Texture& t = m.textures[size_t(tex)];
        GlbLiveryTexture& g = out.emplace_back();
        g.key = key;
        g.paint = shown.role == Role::Paint;
        g.blank = tex == 0;
        g.name = g.blank || t.name.empty() ? shown.material : t.name;
        g.width = t.width, g.height = t.height;
        for (u32 c : t.rgba) g.rgba.push_back(tinted(c, shown.base));
        float area = 0; // (a textureless paint whose UVs are all one point has no layout to paint on)
        for (const LiveryPart* p : group) {
            const scene::Primitive& pr = body.primitives[p->primitive];
            for (u32 i = pr.first_index; i + 2 < pr.first_index + pr.index_count; i += 3) {
                GlbLiveryTexture::Triangle tri{};
                for (u32 k = 0; k < 3; ++k) {
                    const scene::Vertex& v = body.vertices[body.indices[i + k]];
                    tri.uv[k][0] = v.uv[0], tri.uv[k][1] = v.uv[1];
                    for (int a = 0; a < 3; ++a) tri.normal[a] += v.normal[a];
                }
                area += std::fabs((tri.uv[1][0] - tri.uv[0][0]) * (tri.uv[2][1] - tri.uv[0][1]) -
                                  (tri.uv[1][1] - tri.uv[0][1]) * (tri.uv[2][0] - tri.uv[0][0]));
                g.triangles.push_back(tri);
            }
        }
        if (g.blank && !(area > 1e-4f)) out.pop_back();
    }
    return out;
}

Bytes reencode_glb_images(ByteSpan d, u32 max_dim) {
    if (d.size() < 20 || std::memcmp(d.data(), "glTF", 4) != 0) throw FormatError("not a binary glTF (.glb) file");
    if (rd32(d, 4) != 2) throw FormatError("glTF: only version 2 is supported");
    json::Value doc;
    bool have_doc = false;
    Bytes bin;
    size_t o = 12;
    while (o + 8 <= d.size()) {
        const u32 len = rd32(d, o), type = rd32(d, o + 4);
        if (o + 8 + len > d.size()) throw FormatError("glTF: truncated chunk");
        ByteSpan body = d.subspan(o + 8, len);
        if (type == 0x4E4F534A) {
            doc = json::Value::parse(std::string_view(reinterpret_cast<const char*>(body.data()), body.size()));
            have_doc = true;
        } else if (type == 0x004E4942 && bin.empty()) {
            bin.assign(body.begin(), body.end());
        }
        o += 8 + ((len + 3) & ~3u);
    }
    if (!have_doc || !doc.is_object()) throw FormatError("glTF: no JSON chunk");
    // The views accessors read (geometry): never zeroed, even if an image shares one.
    std::vector<bool> geometry(doc["bufferViews"].size(), false);
    for (const json::Value& a : doc["accessors"].items())
        if (const int v = a["bufferView"].integer(-1); v >= 0 && size_t(v) < geometry.size()) geometry[size_t(v)] = true;
    auto view_range = [&](int v, size_t& off, size_t& len) {
        const json::Value& bv = doc["bufferViews"][size_t(v)];
        if (!bv.is_object() || bv["buffer"].integer() != 0) return false;
        off = Glb::size_of(bv["byteOffset"], bin.size(), "buffer view offset");
        len = Glb::size_of(bv["byteLength"], bin.size() - off, "buffer view length");
        return true;
    };
    std::vector<std::pair<size_t, size_t>> old_images; // ranges to zero
    std::vector<Bytes> fresh;                          // each image's new PNG, in image order
    const Bytes white = encode_png(1, 1, {0xFFFFFFFFu});
    for (const json::Value& im : doc["images"].items()) {
        Bytes png = white;
        if (im.has("bufferView")) {
            const int v = im["bufferView"].integer(-1);
            size_t off = 0, len = 0;
            if (v >= 0 && size_t(v) < geometry.size() && view_range(v, off, len)) {
                if (im["mimeType"].string() == "image/png" || im["mimeType"].string().empty()) {
                    const PngImage p = decode_png(ByteSpan(bin).subspan(off, len), max_dim);
                    png = encode_png(p.width, p.height, p.rgba);
                }
                if (!geometry[size_t(v)]) old_images.emplace_back(off, len);
            }
        }
        fresh.push_back(std::move(png));
    }
    for (const auto& [off, len] : old_images) std::fill(bin.begin() + std::ptrdiff_t(off), bin.begin() + std::ptrdiff_t(off + len), u8(0));
    // The new images at the end of the buffer, each in a view of its own.
    json::Value* images = doc.member("images");
    for (size_t i = 0; images && i < fresh.size(); ++i) {
        while (bin.size() % 4) bin.push_back(0);
        json::Value view = json::Value::empty_object();
        view.set("buffer", json::Value::of_number(0));
        view.set("byteOffset", json::Value::of_number(double(bin.size())));
        view.set("byteLength", json::Value::of_number(double(fresh[i].size())));
        bin.insert(bin.end(), fresh[i].begin(), fresh[i].end());
        json::Value* views = doc.member("bufferViews");
        const size_t index = views ? views->size() : 0;
        if (views) views->push(std::move(view));
        else {
            json::Value arr;
            arr.push(std::move(view));
            doc.set("bufferViews", std::move(arr));
        }
        json::Value* im = images->item(i);
        im->erase("uri");
        im->set("bufferView", json::Value::of_number(double(index)));
        im->set("mimeType", json::Value::of_string("image/png"));
    }
    while (bin.size() % 4) bin.push_back(0);
    // One buffer: the GLB's own.
    json::Value buffer = json::Value::empty_object();
    buffer.set("byteLength", json::Value::of_number(double(bin.size())));
    if (json::Value* buffers = doc.member("buffers"); buffers && buffers->item(0)) {
        *buffers->item(0) = buffer;
    } else {
        json::Value arr;
        arr.push(std::move(buffer));
        doc.set("buffers", std::move(arr));
    }
    std::string text = doc.dump();
    while (text.size() % 4) text += ' ';
    Bytes out;
    auto put32 = [&](u32 v) {
        for (int k = 0; k < 4; ++k) out.push_back(u8(v >> (8 * k)));
    };
    out.insert(out.end(), {'g', 'l', 'T', 'F'});
    put32(2);
    put32(u32(12 + 8 + text.size() + 8 + bin.size()));
    put32(u32(text.size()));
    put32(0x4E4F534A);
    out.insert(out.end(), text.begin(), text.end());
    put32(u32(bin.size()));
    put32(0x004E4942);
    out.insert(out.end(), bin.begin(), bin.end());
    return out;
}

std::vector<std::string> glb_image_warnings(ByteSpan d) {
    std::vector<std::string> out;
    if (d.size() < 20 || std::memcmp(d.data(), "glTF", 4) != 0) return out;
    json::Value doc;
    size_t o = 12;
    while (o + 8 <= d.size()) {
        const u32 len = rd32(d, o), type = rd32(d, o + 4);
        if (o + 8 + len > d.size()) return out;
        if (type == 0x4E4F534A) {
            try {
                doc = json::Value::parse(std::string_view(reinterpret_cast<const char*>(d.data() + o + 8), len));
            } catch (const std::exception&) {
                return out;
            }
            break;
        }
        o += 8 + ((len + 3) & ~3u);
    }
    for (size_t i = 0; i < doc["images"].size(); ++i) {
        const json::Value& im = doc["images"][i];
        const std::string mime = im["mimeType"].string();
        const std::string which = "texture " + (im["name"].string().empty() ? std::to_string(i)
                                                                            : "\"" + im["name"].string().substr(0, 40) + "\"");
        if (!im.has("bufferView")) out.push_back(which + ": separate file, shows white (embed it)");
        else if (!mime.empty() && mime != "image/png")
            out.push_back(which + ": " + (mime.find("jp") != std::string::npos ? std::string("JPEG") : mime) +
                          ", shows white (use PNG)");
    }
    return out;
}

} // namespace gt3r
