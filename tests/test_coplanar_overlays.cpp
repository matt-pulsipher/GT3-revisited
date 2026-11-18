// Course faces drawn over an earlier face from other vertices (formats/model/coplanar_overlays.h,
// issue 349): on made-up quads, and Midfield's grandstand windows on a real disc (GT3R_DISC).

#include "test_framework.h"

#include "disc/disc.h"
#include "formats/course/course_model.h"
#include "formats/model/coplanar_overlays.h"

#include <array>

using namespace gt3r;

namespace {

using P = std::array<float, 3>;

// A quad (two triangles, facing +z for a, b, c, d counter-clockwise) as its own primitive.
size_t add_quad(scene::Mesh& mesh, P a, P b, P c, P d, u32 flags = 0) {
    const u32 base = u32(mesh.vertices.size());
    for (const P& p : {a, b, c, d}) {
        scene::Vertex v;
        v.pos[0] = p[0], v.pos[1] = p[1], v.pos[2] = p[2];
        mesh.vertices.push_back(v);
    }
    scene::Primitive prim;
    prim.first_index = u32(mesh.indices.size());
    for (u32 k : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(base + k);
    prim.index_count = 6;
    prim.flags = flags;
    mesh.primitives.push_back(prim);
    return mesh.primitives.size() - 1;
}

// An axis-aligned quad in the plane z, from (x0, y0) to (x1, y1).
size_t add_rect(scene::Mesh& mesh, float x0, float y0, float x1, float y1, float z, u32 flags = 0) {
    return add_quad(mesh, {x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}, flags);
}

bool marked(const scene::Mesh& mesh, size_t prim) {
    return (mesh.primitives[prim].flags & scene::kCoplanarOverlay) != 0;
}

} // namespace

TEST(coplanar_overlays_made_up_faces) {
    scene::Mesh mesh;
    // A window: the glass, then its frame on the same plane from other corners (0.1 mm off).
    const size_t glass = add_rect(mesh, 0, 0, 28, 3, 0);
    const size_t frame = add_rect(mesh, -0.07f, 0, 28.04f, 3, -0.0001f);
    // A second pass over the frame's vertices (an environment pass): drawn at the frame's depth.
    const size_t frame_pass =
        add_rect(mesh, -0.07f, 0, 28.04f, 3, -0.0001f, scene::kAdditive | scene::kNoDepthWrite);
    // A pass over the glass's own vertices: equal depth with the glass, and on the frame drawn
    // since, which it wins over in GT3 too.
    const size_t glass_pass = add_rect(mesh, 0, 0, 28, 3, 0, scene::kAdditive | scene::kNoDepthWrite);
    // Faces that are not overlays: 5 cm in front, beside the glass (touching along an edge),
    // facing the other way.
    const size_t in_front = add_rect(mesh, 2, 0.5f, 4, 1.5f, 0.05f);
    const size_t beside = add_rect(mesh, 28.04f, 0, 30, 3, 0);
    const size_t back = add_quad(mesh, {5, 0, 0}, {5, 3, 0}, {7, 3, 0}, {7, 0, 0});
    // A decal drawn without depth writes, then a face on it: nothing to win over.
    const size_t decal = add_rect(mesh, 40, 0, 42, 2, 0, scene::kNoDepthWrite);
    const size_t on_decal = add_rect(mesh, 40.5f, 0.5f, 41.5f, 1.5f, 0);
    CHECK(gtm1::mark_coplanar_overlays(mesh) == 3);
    CHECK(!marked(mesh, glass));
    CHECK(marked(mesh, frame));
    CHECK(marked(mesh, frame_pass));
    CHECK(marked(mesh, glass_pass));
    CHECK(!marked(mesh, in_front));
    CHECK(!marked(mesh, beside));
    CHECK(!marked(mesh, back));
    CHECK(!marked(mesh, decal));
    CHECK(!marked(mesh, on_decal));
}

TEST(coplanar_overlays_midfield_windows_disc) {
    Disc* disc = test::test_disc();
    if (!disc) SKIP("needs GT3R_DISC");
    const Volume::Entry* e = disc->volume().find("courses/US/midfield");
    if (!e) SKIP("no Midfield on this disc");
    Bytes data = disc->volume().read(*e);
    course::CourseModelOptions o;
    o.backdrop = o.course_line = false;
    o.reflections = o.frame_alpha = true; // (as the race builds it)
    scene::Model m = course::build_course_model(data, o, "midfield");
    // The grandstand at the start of the main straight (node 595): the window frames lie on their
    // glass and win over it, as GT3's Z buffer lets them.
    const scene::Mesh* stand = nullptr;
    for (const scene::Mesh& mesh : m.meshes)
        if (mesh.node == 595) stand = &mesh;
    CHECK(stand != nullptr);
    if (!stand) return;
    int marked_here = 0, total = 0;
    for (const scene::Primitive& p : stand->primitives)
        marked_here += (p.flags & scene::kCoplanarOverlay) != 0;
    for (const scene::Mesh& mesh : m.meshes)
        for (const scene::Primitive& p : mesh.primitives) total += (p.flags & scene::kCoplanarOverlay) != 0;
    CHECK(marked_here > 0);
    // Few in all: the course's faces mostly lie on no other.
    size_t prims = 0;
    for (const scene::Mesh& mesh : m.meshes) prims += mesh.primitives.size();
    CHECK(total > 0 && size_t(total) * 20 < prims);
}
