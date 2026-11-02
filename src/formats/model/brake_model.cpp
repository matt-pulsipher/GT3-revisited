#include "formats/model/brake_model.h"

#include "formats/model/gtm1.h"
#include "formats/model/tyre_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace gt3r {

namespace {

float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

// The game's corner order for these quads (a triangle strip): (y, z) = (1, 1), (1, -1), (-1, 1),
// (-1, -1) in units of the brake radius.
constexpr float kCorners[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

// A quad of four corners, both faces (the GS culls nothing).
void add_quad(scene::Mesh& m, const scene::Vertex (&v)[4], int texture, u32 flags, float alpha_ref,
              const scene::Material& mat) {
    const u32 base = u32(m.vertices.size());
    for (const scene::Vertex& x : v) m.vertices.push_back(x);
    scene::Primitive p;
    p.first_index = u32(m.indices.size());
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
    p.index_count = 6;
    p.texture = texture;
    p.flags = flags | scene::kDoubleSided;
    p.alpha_ref = alpha_ref;
    p.material = mat;
    m.primitives.push_back(p);
}

// Copies texture `i` of the set into `m` (once); returns its index there.
int use_texture(scene::Model& m, const BrakeParts& bin, int i) {
    m.textures.push_back(bin.textures[size_t(i)]);
    return int(m.textures.size()) - 1;
}

} // namespace

std::array<BrakeRecord, 2> parse_brake_records(ByteSpan gtci) {
    std::array<BrakeRecord, 2> out{};
    if (gtci.size() < 0x3C || !has_magic(gtci, "GTCI")) return out;
    for (size_t a = 0; a < 2; ++a) {
        const size_t o = 0x14 + 0x14 * a;
        BrakeRecord& r = out[a];
        // The game stores caliper - 2 and rotor - 1; a negative caliper draws none.
        r.caliper = int(rd32(gtci, o)) - 2;
        r.rotor = int(rd32(gtci, o + 4)) - 1;
        r.radius = rdf(gtci, o + 8);
        r.offset = rdf(gtci, o + 0xC);
        r.angle = rdf(gtci, o + 0x10) * 0.017453292f; // the program's pi / 180
        if (r.caliper < 0) r.caliper = -1;
        if (r.rotor < 0) r.rotor = -1;
    }
    return out;
}

bool BrakeParts::is_brake_bin(ByteSpan data) { return has_magic(data, "GTBR") && data.size() >= 0x28; }

BrakeParts BrakeParts::parse(ByteSpan data) {
    if (!is_brake_bin(data)) throw FormatError("not a GTBR brake file");
    BrakeParts b;
    const u32 tex1 = rd32(data, 0x10), rotors = rd32(data, 0x20), calipers = rd32(data, 0x24);
    b.first_rotor = int(rd32(data, 0x14));
    b.first_caliper = int(rd32(data, 0x18));
    b.glow = int(rd32(data, 0x1C));
    if (tex1 >= data.size() || rotors > calipers || calipers > tex1) throw FormatError("GTBR tables out of order");
    for (u32 o = rotors; o + 16 <= calipers; o += 16) b.rotor_flags.push_back(rd32(data, o));
    for (u32 o = calipers; o + 32 <= tex1; o += 32)
        b.caliper_place.push_back({rdf(data, o), rdf(data, o + 4), rdf(data, o + 8), rdf(data, o + 12)});
    gtm1::decode_texture_set(data.subspan(tex1), b.textures, "brake_");
    // REGION_CLAMP, MXL 0, bilinear: clamped, from the full-size image.
    for (scene::Texture& t : b.textures) t.clamp_u = t.clamp_v = true, t.no_mips = true;
    return b;
}

scene::Model build_brake_parts(const BrakeParts& bin, const BrakeRecord& rec, bool right, float shininess) {
    scene::Model m;
    m.name = "brake";
    if (!bin.has_rotor(rec.rotor)) return m;
    scene::Mesh mesh;
    mesh.name = "brake";
    const float side = right ? -1.0f : 1.0f; // the left wheel's axis is the car's; a right one's is turned half a turn
    const float r = rec.radius;
    const scene::Material mat = tyre_material(shininess);
    auto vertex = [&](float x, float y, float z, float s, float t) {
        scene::Vertex v;
        v.pos[0] = side * x, v.pos[1] = y, v.pos[2] = z;
        v.normal[0] = -side; // out of the wheel
        v.uv[0] = s, v.uv[1] = t;
        return v;
    };
    // The rotor: a square of side 2 r on the hub at the offset, the image upright, S along the car
    // (the right wheel's image mirrored in its own frame, i.e. the same in the car's).
    {
        scene::Vertex v[4];
        for (int k = 0; k < 4; ++k) {
            const float y = kCorners[k][0], z = kCorners[k][1];
            v[k] = vertex(rec.offset, y * r, z * r, 0.5f + 0.5f * z, 0.5f - 0.5f * y);
        }
        add_quad(mesh, v, use_texture(m, bin, bin.first_rotor + rec.rotor), scene::kAlphaTest,
                 (0x40 + 0.5f) / 128.0f, mat);
    }
    // The caliper, 5 mm outward of the rotor: its table record places it in the rotor's plane
    // (radial along the car, tangential up), turned by the angle; the image's S runs down the
    // tangential axis, T in along the radial one.
    if (bin.has_caliper(rec.caliper)) {
        const std::array<float, 4>& p = bin.caliper_place[size_t(rec.caliper)];
        const float c = std::cos(rec.angle), s = std::sin(rec.angle);
        scene::Vertex v[4];
        for (int k = 0; k < 4; ++k) {
            const float yt = kCorners[k][0], zt = kCorners[k][1];
            const float y0 = p[3] + p[1] * yt, z0 = p[2] + p[0] * zt;
            v[k] = vertex(rec.offset - 0.005f, r * (y0 * c - z0 * s), r * (y0 * s + z0 * c), 0.5f - 0.5f * yt,
                          0.5f - 0.5f * zt);
        }
        add_quad(mesh, v, use_texture(m, bin, bin.first_caliper + rec.caliper),
                 scene::kAlphaBlend | scene::kAlphaTest | scene::kAlphaFailBlend, (0x7F + 0.5f) / 128.0f, mat);
    }
    m.meshes.push_back(std::move(mesh));
    m.compute_bounds();
    return m;
}

scene::Model build_brake_glow(const BrakeParts& bin, const BrakeRecord& rec, bool right) {
    scene::Model m;
    m.name = "brake_glow";
    if (!bin.rotor_glows(rec.rotor)) return m;
    scene::Mesh mesh;
    mesh.name = "brake_glow";
    const float side = right ? -1.0f : 1.0f;
    scene::Vertex v[4];
    for (int k = 0; k < 4; ++k) {
        // In the wheel's own frame: S along its z, which a right wheel's half turn reverses.
        const float y = kCorners[k][0], z = kCorners[k][1];
        v[k].pos[0] = side * (rec.offset - 0.001f), v[k].pos[1] = y * rec.radius, v[k].pos[2] = side * z * rec.radius;
        v[k].normal[0] = -side;
        v[k].uv[0] = 0.5f + 0.5f * z, v[k].uv[1] = 0.5f - 0.5f * y;
    }
    add_quad(mesh, v, use_texture(m, bin, bin.glow), scene::kAdditive | scene::kUnlit | scene::kNoDepthWrite, -1, {});
    m.meshes.push_back(std::move(mesh));
    m.compute_bounds();
    return m;
}

float brake_glow_fix(float heat, float gain) {
    const float f = std::clamp(heat * heat * gain * 128.0f, 0.0f, 255.0f);
    return std::trunc(f) / 128.0f;
}

} // namespace gt3r
