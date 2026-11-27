#include "formats/model/wheel_model.h"
#include "formats/model/build_cache.h"

#include "formats/model/gltf.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r {

bool WheelFile::is_wheel(ByteSpan data) { return has_magic(data, "GTTW") && data.size() >= 0x20; }

WheelFile WheelFile::parse(ByteSpan data) {
    if (!is_wheel(data)) throw FormatError("not a GTTW wheel model");
    WheelFile w;
    u32 size = rd32(data, 0x0C);
    if (size > data.size()) size = u32(data.size()); // (a modified game's file stating more than it has: what it has)
    w.flags = rd32(data, 0x10);
    u32 off = rd32(data, 0x1C);
    if (off >= size) throw FormatError("GTTW model offset out of range");
    w.gtm1 = data.subspan(off, size - off);
    return w;
}

namespace {

constexpr char kShapedMagic[8] = {'G', 'T', '3', 'R', 'I', 'M', '1', 0};
constexpr size_t kShapedHeader = 0x14;

float rdf(ByteSpan d, size_t o) {
    float f;
    std::memcpy(&f, d.data() + o, 4);
    return f;
}

} // namespace

bool valid_rim_shape(const RimShape& s) {
    return std::isfinite(s.depth) && std::isfinite(s.dish) && std::isfinite(s.lip) && s.depth >= kRimDepthMin &&
           s.depth <= kRimDepthMax && std::fabs(s.dish) <= kRimDishMax && s.lip >= kRimLipMin && s.lip <= kRimLipMax;
}

bool is_glb_rim(ByteSpan data) { return has_magic(data, "glTF") && data.size() >= 12; }

bool is_shaped_rim(ByteSpan data) {
    return data.size() >= kShapedHeader && std::memcmp(data.data(), kShapedMagic, 8) == 0;
}

bool is_rim(ByteSpan data) {
    const ByteSpan r = is_shaped_rim(data) ? data.subspan(kShapedHeader) : data;
    return WheelFile::is_wheel(r) || is_glb_rim(r);
}

Bytes wrap_shaped_rim(ByteSpan rim, const RimShape& shape) {
    Bytes out(kShapedMagic, kShapedMagic + 8);
    for (float f : {shape.depth, shape.dish, shape.lip}) {
        u8 b[4];
        std::memcpy(b, &f, 4);
        out.insert(out.end(), b, b + 4);
    }
    out.insert(out.end(), rim.begin(), rim.end());
    return out;
}

ByteSpan unwrap_rim(ByteSpan data, RimShape* shape) {
    if (shape) *shape = {};
    if (!is_shaped_rim(data)) {
        if (data.size() >= 8 && std::memcmp(data.data(), kShapedMagic, 7) == 0) throw FormatError("reshaped rim too short");
        return data;
    }
    const RimShape s{rdf(data, 0x08), rdf(data, 0x0C), rdf(data, 0x10)};
    if (!valid_rim_shape(s)) throw FormatError("rim shape out of range");
    if (shape) *shape = s;
    return data.subspan(kShapedHeader);
}

void apply_rim_shape(scene::Model& m, const RimShape& s) {
    if (!s.any()) return;
    // The rim's radius: the farthest vertex from the axle.
    float rmax = 0;
    for (const scene::Mesh& mesh : m.meshes)
        for (const scene::Vertex& v : mesh.vertices) rmax = std::max(rmax, std::hypot(v.pos[1], v.pos[2]));
    if (rmax <= 0) return;
    const float lip0 = 0.85f * rmax;                                  // where the lip starts
    const float lip1 = std::max(0.0f, rmax - (rmax - lip0) * s.lip); // ... after the shape
    for (scene::Mesh& mesh : m.meshes)
        for (scene::Vertex& v : mesh.vertices) {
            const float r = std::hypot(v.pos[1], v.pos[2]);
            // Radially: the face [0, lip0] onto [0, lip1], the lip [lip0, rmax] onto [lip1, rmax].
            const float r2 = r <= lip0 ? r * (lip1 / lip0) : lip1 + (r - lip0) * (rmax - lip1) / (rmax - lip0);
            if (r > 0) v.pos[1] *= r2 / r, v.pos[2] *= r2 / r;
            // Along the axle: deeper or shallower, then the middle pushed in towards the lip.
            v.pos[0] = v.pos[0] * s.depth + (lip1 > 0 ? s.dish * std::max(0.0f, 1.0f - r2 / lip1) : 0.0f);
            float n[3] = {v.normal[0] / s.depth, v.normal[1], v.normal[2]};
            const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (l > 0)
                for (int k = 0; k < 3; ++k) v.normal[k] = n[k] / l;
        }
    m.compute_bounds();
}

scene::Model load_wheel_model(ByteSpan whole, const gtm1::Options& opt, const std::string& name) {
    RimShape shape;
    const ByteSpan data = unwrap_rim(whole, &shape);
    if (shape.any()) {
        scene::Model m = load_wheel_model(data, opt, name);
        apply_rim_shape(m, shape);
        return m;
    }
    if (is_glb_rim(data)) {
        GlbOptions g;
        g.reflections = opt.reflection;
        scene::Model m = load_glb(data, g, name);
        for (scene::Mesh& mesh : m.meshes) mesh.name = name;
        return m;
    }
    const ByteSpan g = WheelFile::parse(data).gtm1;
    if (!opt.cache) return gtm1::decode_gtm1(g, opt, name);
    // (The wheels of a car, and of the cars sharing a build, differ only in their LOD and reflections: car_model.cpp
    // asks with the same other options for every car.)
    scene::Model m = opt.cache->wheel(g, opt.run.lod, opt.reflection,
                                      [&] { return gtm1::build_model(opt.cache->model(g), opt, name); });
    m.name = name;
    return m;
}

} // namespace gt3r
