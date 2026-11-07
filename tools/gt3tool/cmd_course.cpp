// Course commands: inspect course files, export OBJ, render offline previews.
//
//   course-info   <vol-path>                       sections, road and AI line stats
//   course-obj    <vol-path> <outdir> [opts]       OBJ + MTL + PNG textures (+ road / lines; glb: binary glTF too)
//   course-shot   <vol-path> <out.png> [opts]      software-rendered preview
//   course-survey [prefix]                         decode every course file, report coverage
//
// Options: top | persp (shot view), road, lines, sky, scenery2, reflections, noscenery, courseline, size=N, glb.

#include "tool.h"

#include "formats/course/course_model.h"
#include "formats/model/gltf_export.h"
#include "formats/png_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>

namespace gt3r::tool {

namespace {

using namespace gt3r::course;

bool has_opt(const Args& a, size_t from, const std::string& o) {
    for (size_t i = from; i < a.size(); ++i)
        if (a[i] == o) return true;
    return false;
}

int int_opt(const Args& a, size_t from, const std::string& key, int def) {
    for (size_t i = from; i < a.size(); ++i)
        if (a[i].rfind(key + "=", 0) == 0) return std::atoi(a[i].c_str() + key.size() + 1);
    return def;
}

CourseModelOptions model_opts(const Args& a, size_t from) {
    CourseModelOptions o;
    o.road_mesh = has_opt(a, from, "road");
    o.ai_lines = has_opt(a, from, "lines");
    o.sky = has_opt(a, from, "sky");
    o.scenery2 = has_opt(a, from, "scenery2");
    o.reflections = has_opt(a, from, "reflections");
    if (has_opt(a, from, "noscenery")) o.scenery = o.backdrop = o.course_line = false;
    if (has_opt(a, from, "courseline")) o.course_line = true; // with noscenery: the licence line alone
    return o;
}

int cmd_course_info(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    Course c = load_course(data);
    std::printf("%zu bytes, %zu sections\n", data.size(), c.file.sections().size());
    for (const CourseSection& s : c.file.sections())
        std::printf("  slot %2d @%08x %9u  %s\n", s.slot, s.offset, s.size, s.tag.c_str());
    const Road& r = c.road;
    std::printf("road: id %08x length %.1f m, %zu segments, %zu triangles, %zu walls (%zu points), %zu checkpoints, "
                "%zu grid slots, %zu markers\n",
                r.course_id, r.length, r.segments.size(), r.triangle_count(), r.walls.size(), r.wall_points.size(),
                r.checkpoints.size(), r.grid.size(), r.markers.size());
    std::map<u32, size_t> surf;
    for (const RoadSegment& s : r.segments)
        for (const RoadTriangle& t : s.triangles) surf[t.surface]++;
    std::printf("surfaces:");
    for (auto& [k, n] : surf) std::printf(" %u:%zu", k, n);
    std::printf("\nai lines (build \"%s\"):", c.ai.build.c_str());
    for (const AiLine& l : c.ai.lines) std::printf(" %zu%s", l.points.size(), l.closed ? "c" : "");
    std::printf("\n");
    for (size_t i = 0; i < r.grid.size(); ++i)
        std::printf("  grid %zu: %.2f %.2f %.2f yaw %.3f\n", i, r.grid[i].pos.x, r.grid[i].pos.y, r.grid[i].pos.z,
                    r.grid[i].yaw);
    return 0;
}

// --- OBJ export ------------------------------------------------------------

int cmd_course_obj(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    std::string name = a[0].substr(a[0].find_last_of('/') + 1);
    scene::Model m = build_course_model(data, model_opts(a, 2), name);
    std::string dir = a[1];
    std::string obj, mtl;
    obj += "mtllib " + name + ".mtl\n";
    for (size_t t = 0; t < m.textures.size(); ++t) {
        const scene::Texture& tex = m.textures[t];
        char fn[64];
        std::snprintf(fn, sizeof(fn), "tex%04zu.png", t);
        if (!save(dir + "/" + fn, encode_png(tex.width, tex.height, tex.rgba))) return 1;
        mtl += "newmtl t" + std::to_string(t) + "\nKd 1 1 1\nmap_Kd " + fn + "\n";
    }
    mtl += "newmtl untextured\nKd 1 1 1\n";
    size_t vbase = 1;
    for (const scene::Mesh& mesh : m.meshes) {
        obj += "o " + mesh.name + "\n";
        char line[256];
        for (const scene::Vertex& v : mesh.vertices) {
            std::snprintf(line, sizeof(line), "v %.4f %.4f %.4f %.3f %.3f %.3f\nvt %.5f %.5f\n", v.pos[0], v.pos[1],
                          v.pos[2], (v.color & 0xFF) / 255.0, ((v.color >> 8) & 0xFF) / 255.0,
                          ((v.color >> 16) & 0xFF) / 255.0, v.uv[0], 1.0 - v.uv[1]);
            obj += line;
        }
        for (const scene::Primitive& p : mesh.primitives) {
            obj += p.texture >= 0 ? "usemtl t" + std::to_string(p.texture) + "\n" : "usemtl untextured\n";
            for (u32 i = p.first_index; i + 3 <= p.first_index + p.index_count; i += 3) {
                size_t i0 = vbase + mesh.indices[i], i1 = vbase + mesh.indices[i + 1], i2 = vbase + mesh.indices[i + 2];
                std::snprintf(line, sizeof(line), "f %zu/%zu %zu/%zu %zu/%zu\n", i0, i0, i1, i1, i2, i2);
                obj += line;
            }
        }
        vbase += mesh.vertices.size();
    }
    bool ok = save(dir + "/" + name + ".obj", ByteSpan(reinterpret_cast<const u8*>(obj.data()), obj.size())) &&
              save(dir + "/" + name + ".mtl", ByteSpan(reinterpret_cast<const u8*>(mtl.data()), mtl.size()));
    if (ok && has_opt(a, 2, "glb")) {
        GlbExportOptions o;
        o.lod = -1;
        o.reflections = has_opt(a, 2, "reflections");
        ok = save(dir + "/" + name + ".glb", export_glb(m, o));
    }
    std::printf("%zu meshes, %zu textures -> %s\n", m.meshes.size(), m.textures.size(), dir.c_str());
    return ok ? 0 : 1;
}

// --- Software preview renderer ---------------------------------------------

struct Raster {
    int w, h;
    std::vector<u32> color;
    std::vector<float> depth;
    Raster(int w_, int h_) : w(w_), h(h_), color(size_t(w_) * h_, 0xFF302820u), depth(size_t(w_) * h_, 1e30f) {}
};

struct SV {
    float x, y, z; // screen x, y, depth (smaller = nearer)
    float w;       // 1 / view depth for perspective-correct interpolation (1 for ortho)
    float u, v, r, g, b;
};

u32 shade(const scene::Model& m, int tex, float u, float v, float r, float g, float b, bool& discard) {
    float tr = 1, tg = 1, tb = 1;
    discard = false;
    if (tex >= 0 && tex < int(m.textures.size())) {
        const scene::Texture& t = m.textures[size_t(tex)];
        if (t.width && t.height) {
            float fu = u - std::floor(u), fv = v - std::floor(v);
            u32 x = std::min(t.width - 1, u32(fu * float(t.width)));
            u32 y = std::min(t.height - 1, u32(fv * float(t.height)));
            u32 c = t.rgba[size_t(y) * t.width + x];
            if ((c >> 24) < 40) {
                discard = true;
                return 0;
            }
            tr = (c & 0xFF) / 255.f, tg = ((c >> 8) & 0xFF) / 255.f, tb = ((c >> 16) & 0xFF) / 255.f;
        }
    }
    auto cl = [](float f) { return u32(std::clamp(f, 0.f, 1.f) * 255.f + 0.5f); };
    return 0xFF000000u | cl(tr * r) | cl(tg * g) << 8 | cl(tb * b) << 16;
}

void draw_tri(Raster& R, const scene::Model& m, int tex, SV a, SV b, SV c) {
    float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::fabs(area) < 1e-9f) return;
    int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))));
    int x1 = std::min(R.w - 1, int(std::ceil(std::max({a.x, b.x, c.x}))));
    int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))));
    int y1 = std::min(R.h - 1, int(std::ceil(std::max({a.y, b.y, c.y}))));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
            float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * a.z + w1 * b.z + w2 * c.z;
            size_t i = size_t(y) * R.w + x;
            if (z >= R.depth[i]) continue;
            float iw = w0 * a.w + w1 * b.w + w2 * c.w;
            auto I = [&](float SV::*f) { return (w0 * a.*f * a.w + w1 * b.*f * b.w + w2 * c.*f * c.w) / iw; };
            bool discard;
            u32 col = shade(m, tex, I(&SV::u), I(&SV::v), I(&SV::r), I(&SV::g), I(&SV::b), discard);
            if (discard) continue;
            R.depth[i] = z;
            R.color[i] = col;
        }
}

int cmd_course_shot(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    CourseModelOptions opt = model_opts(a, 2);
    bool persp = has_opt(a, 2, "persp");
    if (persp) opt.sky = true;
    scene::Model m = build_course_model(data, opt);
    Course c = load_course(data);
    int size = int_opt(a, 2, "size", 1600);

    // Bounds for the top-down view: the road (scenery extends far beyond it).
    float bx0 = 1e30f, bx1 = -1e30f, bz0 = 1e30f, bz1 = -1e30f;
    for (const RoadSegment& s : c.road.segments) {
        bx0 = std::min(bx0, s.bounds_min.x), bx1 = std::max(bx1, s.bounds_max.x);
        bz0 = std::min(bz0, s.bounds_min.z), bz1 = std::max(bz1, s.bounds_max.z);
    }
    float margin = 0.06f * std::max(bx1 - bx0, bz1 - bz0);
    bx0 -= margin, bx1 += margin, bz0 -= margin, bz1 += margin;
    int W = size, H = size;
    if (!persp) H = std::max(16, int(float(size) * (bz1 - bz0) / (bx1 - bx0)));
    Raster R(W, H);

    // Perspective camera: behind grid slot 0, looking along the road.
    float cx = 0, cy = 0, cz = 0, fx = 0, fz = 1;
    if (persp && !c.road.grid.empty()) {
        const GridSlot& g = c.road.grid[0];
        // Direction: towards the nearest checkpoint's travel direction.
        const Checkpoint* best = nullptr;
        float bd = 1e30f;
        for (const Checkpoint& k : c.road.checkpoints) {
            float d = std::hypot((k.x0 + k.x1) / 2 - g.pos.x, (k.z0 + k.z1) / 2 - g.pos.z);
            if (d < bd) bd = d, best = &k;
        }
        if (best) fx = best->dir_x, fz = best->dir_z;
        if (has_opt(a, 2, "back")) fx = -fx, fz = -fz;
        float y = c.road.height_at(g.pos.x, g.pos.z).value_or(g.pos.y);
        cx = g.pos.x - fx * 14, cz = g.pos.z - fz * 14, cy = y + 5;
    }
    float rx = -fz, rz = fx; // right vector (right-handed, Y up)
    float focal = float(W) * 0.6f;

    auto project = [&](const scene::Vertex& v, SV& o) -> bool {
        o.u = v.uv[0], o.v = v.uv[1];
        o.r = (v.color & 0xFF) / 128.f, o.g = ((v.color >> 8) & 0xFF) / 128.f, o.b = ((v.color >> 16) & 0xFF) / 128.f;
        o.r = std::min(o.r, 2.f), o.g = std::min(o.g, 2.f), o.b = std::min(o.b, 2.f);
        if (!persp) {
            o.x = (v.pos[0] - bx0) / (bx1 - bx0) * float(W);
            o.y = (bz1 - v.pos[2]) / (bz1 - bz0) * float(H);
            o.z = -v.pos[1];
            o.w = 1;
            return true;
        }
        float dx = v.pos[0] - cx, dy = v.pos[1] - cy, dz = v.pos[2] - cz;
        float depth = dx * fx + dz * fz - dy * 0.12f;
        if (depth < 0.5f) return false;
        float sx = dx * rx + dz * rz;
        float sy = dy + (dx * fx + dz * fz) * 0.12f;
        o.x = float(W) / 2 + sx / depth * focal;
        o.y = float(H) / 2 - sy / depth * focal;
        o.z = depth;
        o.w = 1 / depth;
        return true;
    };
    // Prelit colours use 0x80 = 1.0 like the GS; overlays use 0xFF, so treat unlit untextured as-is.
    for (const scene::Mesh& mesh : m.meshes) {
        bool overlay = mesh.name == "road" || mesh.name == "centre_line" || mesh.name.rfind("ai_line", 0) == 0;
        for (const scene::Primitive& p : mesh.primitives) {
            for (u32 i = p.first_index; i + 3 <= p.first_index + p.index_count; i += 3) {
                SV s[3];
                bool ok = true;
                for (int k = 0; k < 3; ++k) ok &= project(mesh.vertices[mesh.indices[i + k]], s[k]);
                if (!ok) continue;
                if (overlay)
                    for (SV& q : s) q.r *= 0.5f, q.g *= 0.5f, q.b *= 0.5f, q.z -= persp ? 0.05f : 1000.f;
                draw_tri(R, m, p.texture, s[0], s[1], s[2]);
            }
        }
    }
    std::printf("%zu meshes, %zu textures, %dx%d\n", m.meshes.size(), m.textures.size(), W, H);
    return save(a[1], encode_png(u32(W), u32(H), R.color)) ? 0 : 1;
}

// --- Survey ------------------------------------------------------------------

int cmd_course_survey(Disc& disc, const Args& a) {
    std::string prefix = a.empty() ? "courses/" : a[0];
    size_t files = 0, ok = 0, model_ok = 0;
    std::map<std::string, size_t> errors;
    for (const auto& e : disc.volume().entries()) {
        if (e.path.compare(0, prefix.size(), prefix) != 0 || e.path.find("/.") != std::string::npos) continue;
        if (e.path.find('/', prefix.size()) == std::string::npos && prefix == "courses/") continue;
        ++files;
        Bytes data = disc.volume().read(e);
        try {
            Course c = load_course(data);
            size_t lines = 0;
            for (const AiLine& l : c.ai.lines) lines += !l.points.empty();
            ++ok;
            CourseModelOptions o;
            o.textures = has_opt(a, 1, "textures");
            o.sky = true, o.scenery2 = true, o.reflections = true;
            scene::Model m = build_course_model(data, o);
            size_t tris = 0, verts = 0;
            for (const scene::Mesh& mesh : m.meshes) tris += mesh.indices.size() / 3, verts += mesh.vertices.size();
            ++model_ok;
            std::printf("%-32s len %7.1f seg %4zu rtri %6zu walls %zu cps %4zu grid %zu ai %zu | meshes %4zu verts %7zu "
                        "tris %7zu\n",
                        e.path.c_str(), c.road.length, c.road.segments.size(), c.road.triangle_count(),
                        c.road.walls.size(), c.road.checkpoints.size(), c.road.grid.size(), lines, m.meshes.size(),
                        verts, tris);
        } catch (const std::exception& ex) {
            errors[ex.what()]++;
            std::printf("%-32s ERROR %s\n", e.path.c_str(), ex.what());
        }
    }
    std::printf("\n%zu files: %zu course data ok, %zu models ok\n", files, ok, model_ok);
    for (auto& [k, n] : errors) std::printf("  %zu x %s\n", n, k.c_str());
    return 0;
}

} // namespace

GT3TOOL_COMMAND(course_info, "course-info", "<vol-path>", "course sections, road and AI line stats", 1,
                cmd_course_info);
GT3TOOL_COMMAND(course_obj, "course-obj", "<vol-path> <outdir> [road] [lines] [sky] [scenery2] [glb]",
                "export a course as OBJ+MTL+PNG (glb: and binary glTF)", 2, cmd_course_obj);
GT3TOOL_COMMAND(course_shot, "course-shot", "<vol-path> <out.png> [top|persp] [road] [lines] [size=N]",
                "software-render a course preview", 2, cmd_course_shot);
GT3TOOL_COMMAND(course_survey, "course-survey", "[prefix] [textures]", "decode every course file", 0,
                cmd_course_survey);

} // namespace gt3r::tool
