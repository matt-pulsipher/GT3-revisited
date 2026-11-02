#include "formats/course/course_model.h"

#include "formats/model/gtm1.h"

#include <cmath>

namespace gt3r::course {

Course load_course(ByteSpan data) {
    Course c;
    c.file = CourseFile::parse(data);
    ByteSpan road = c.file.section(data, kSlotRoad);
    if (!road.empty()) c.road = Road::parse(road);
    ByteSpan ai = c.file.section(data, kSlotAiLines);
    if (!ai.empty()) {
        c.ai = AiLines::parse(ai);
        c.ai.project(c.road);
    }
    return c;
}

void append_model(scene::Model& dst, scene::Model&& src) {
    int base = int(dst.textures.size());
    for (scene::Texture& t : src.textures) dst.textures.push_back(std::move(t));
    for (scene::Mesh& m : src.meshes) {
        for (scene::Primitive& p : m.primitives)
            if (p.texture >= 0) p.texture += base;
        dst.meshes.push_back(std::move(m));
    }
}

namespace {

scene::Model gtm1_model(ByteSpan block, const CourseModelOptions& opt, const std::string& name,
                        std::vector<gtm1::ClutSet>* clut_sets, gtm1::AnimatedNodes* animated,
                        bool carry_state = false) {
    gtm1::Gtm1 g = gtm1::Gtm1::parse(block);
    if (!opt.reflections)
        for (gtm1::Chunk& c : g.chunks)
            if ((c.flags & 0x1F) == 4) c.packets.clear(); // the road sheen pass over the same vertices
    gtm1::Options o;
    o.run.lod = opt.lod;
    o.textures = opt.textures;
    o.reflection = opt.reflections;
    o.road_sheen = opt.reflections;
    o.course_env_maps = true;
    o.frame_alpha = opt.frame_alpha;
    o.alpha_test = true;
    o.gs_state = true;
    o.carry_state = carry_state;
    o.clut_sets = clut_sets;
    o.animated = animated;
    scene::Model m = gtm1::build_model(g, o, name);
    for (scene::Mesh& mesh : m.meshes) mesh.name = name + "/" + mesh.name;
    return m;
}

// Vertex colour is memory-order RGBA8 (R,G,B,A). On little-endian the u32 value is
// therefore 0xAABBGGRR. Alpha ~0x90 keeps the overlay readable while still letting
// the real road texture show through.
u32 surface_color(u32 surface) {
    // Prefer the material byte (low 8 of the GTRW triangle surface / material field).
    const u8 mat = u8(surface);
    switch (mat) {
    case 0: return 0x90808080u; // Tarmac / pit lane – neutral grey
    case 1: return 0x904040E0u; // Kerb / rumble strip – red
    case 2: return 0x9040C040u; // Grass – green
    case 3: return 0x9060A0C0u; // Gravel – sandy brown
    case 4: return 0x9080D0E0u; // Sand – pale yellow
    case 5: return 0x9013458Bu; // Dirt – sienna
    case 7: return 0x908090A0u; // Paving / cobbles – warm grey
    case 8: return 0x90C0A060u; // Loose – light blue-grey
    case 9: return 0x90506070u; // Rough – dark brown-grey
    default: return 0x90C000C0u; // Unknown – magenta
    }
}

} // namespace

scene::Mesh make_road_mesh(const Road& road) {
    scene::Mesh mesh;
    mesh.name = "road";
    for (const RoadSegment& s : road.segments) {
        for (const RoadTriangle& t : s.triangles) {
            const Vec3* v[3] = {&s.vertices[t.a], &s.vertices[t.b], &s.vertices[t.c]};
            u32 base = u32(mesh.vertices.size());
            for (const Vec3* p : v) {
                scene::Vertex sv;
                sv.pos[0] = p->x;
                sv.pos[1] = p->y + 0.05f;
                sv.pos[2] = p->z;
                sv.normal[1] = 1;
                sv.color = surface_color(t.material); // material byte (0=tarmac, 1=kerb, ...)
                mesh.vertices.push_back(sv);
            }
            mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
        }
    }
    // Semi-transparent overlay: blend, no depth write, unlit, double-sided.
    mesh.primitives.push_back({0, u32(mesh.indices.size()), -1,
                               scene::kUnlit | scene::kDoubleSided | scene::kAlphaBlend | scene::kNoDepthWrite});
    return mesh;
}

scene::Mesh make_ribbon(const std::vector<Vec3>& pts, float width, u32 color, float lift, const std::string& name) {
    scene::Mesh mesh;
    mesh.name = name;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec3& a = pts[i > 0 ? i - 1 : i];
        const Vec3& b = pts[i + 1 < pts.size() ? i + 1 : i];
        float dx = b.x - a.x, dz = b.z - a.z, len = std::hypot(dx, dz);
        if (len < 1e-6f) dx = 1, dz = 0, len = 1;
        float nx = -dz / len * width * 0.5f, nz = dx / len * width * 0.5f;
        for (int side = -1; side <= 1; side += 2) {
            scene::Vertex v;
            v.pos[0] = pts[i].x + side * nx;
            v.pos[1] = pts[i].y + lift;
            v.pos[2] = pts[i].z + side * nz;
            v.normal[1] = 1;
            v.color = color;
            mesh.vertices.push_back(v);
        }
        if (i > 0) {
            u32 k = u32(i * 2);
            mesh.indices.insert(mesh.indices.end(), {k - 2, k - 1, k, k - 1, k + 1, k});
        }
    }
    mesh.primitives.push_back({0, u32(mesh.indices.size()), -1, scene::kUnlit | scene::kDoubleSided});
    return mesh;
}

scene::Model build_course_model(ByteSpan data, const CourseModelOptions& opt, const std::string& name,
                                std::vector<gtm1::ClutSet>* clut_sets, std::vector<gtm1::AnimatedNodes>* animated) {
    CourseFile file = CourseFile::parse(data);
    scene::Model model;
    model.name = name;
    auto add = [&](int slot, const char* label) {
        ByteSpan s = file.section(data, slot);
        if (s.empty() || !gtm1::Gtm1::is_gtm1(s)) return;
        size_t sets = clut_sets ? clut_sets->size() : 0;
        int base = int(model.textures.size());
        gtm1::AnimatedNodes nodes;
        append_model(model, gtm1_model(s, opt, label, clut_sets, animated ? &nodes : nullptr, slot == kSlotCourseLine));
        if (clut_sets)
            for (size_t i = sets; i < clut_sets->size(); ++i) (*clut_sets)[i].first += base;
        if (animated && !nodes.empty()) {
            nodes.rebase_textures(base);
            animated->push_back(std::move(nodes));
        }
    };
    if (opt.scenery) add(kSlotScenery, "scenery");
    if (opt.scenery2) add(kSlotScenery2, "scenery2");
    if (opt.course_line) add(kSlotCourseLine, "course_line");
    if (opt.road_reflection) add(9, "reflection");
    if (opt.road_reflection_mask) add(13, "reflection_mask");
    if (opt.backdrop) add(kSlotBackdrop, "backdrop");
    if (opt.sky_object) add(kSlotSkyObject, "sky_object");
    if (opt.sky) add(kSlotSky, "sky");
    if (opt.sky_low) add(kSlotSky2, "sky2");
    if (opt.road_mesh || opt.ai_lines) {
        Course c = load_course(data);
        if (opt.road_mesh) model.meshes.push_back(make_road_mesh(c.road));
        if (opt.ai_lines) {
            model.meshes.push_back(make_ribbon(c.road.centre_line(), 0.6f, 0xFFFFFF00u, 0.2f, "centre_line"));
            if (!c.ai.lines.empty() && !c.ai.lines[0].points.empty())
                model.meshes.push_back(make_ribbon(c.ai.lines[0].sample(c.road, 2.0f), 1.2f, 0xFF0000FFu, 0.3f, "ai_line0"));
        }
    }
    model.compute_bounds();
    return model;
}

scene::Model course_line_model(ByteSpan data, const std::string& name) {
    scene::Model model;
    model.name = name;
    ByteSpan s = CourseFile::parse(data).section(data, kSlotCourseLine);
    if (s.empty() || !gtm1::Gtm1::is_gtm1(s)) return model;
    CourseModelOptions o;
    o.frame_alpha = true; // its FBMSK (colour only) as scene::kKeepAlpha
    append_model(model, gtm1_model(s, o, name.empty() ? "course_line" : name, nullptr, nullptr, true));
    model.compute_bounds();
    return model;
}

} // namespace gt3r::course
