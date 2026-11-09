#pragma once

// Format-independent, CPU-side 3D model. This is the contract between the
// decoders (cars, wheels, courses: src/formats/...) and the renderer
// (src/render/...): decoders fill a Model, the renderer uploads and draws it.
// Keep it plain data so tools (OBJ/glTF export) and tests can use it too.

#include "base/bytes.h"

#include <array>
#include <string>
#include <vector>

namespace gt3r::scene {

struct Vertex {
    float pos[3] = {0, 0, 0};
    float normal[3] = {0, 0, 0};
    float uv[2] = {0, 0}; // origin at the texture's top-left (GS convention)
    u32 color = 0xFFFFFFFFu; // RGBA8 (memory order R,G,B,A), alpha already expanded to 0..255
};

// Render state flags per primitive group. Extend as PS2 states are mapped.
enum PrimitiveFlags : u32 {
    kAlphaBlend = 1u << 0,   // GS ALPHA enabled (standard blend)
    kAdditive = 1u << 1,     // additive blend
    kAlphaTest = 1u << 2,    // GS TEST alpha test
    kDoubleSided = 1u << 3,  // no backface culling
    kEnvironmentMap = 1u << 4, // reflection pass (cars)
    kUnlit = 1u << 5,        // vertex colors only (pre-lit, e.g. courses)
    kNoDepthWrite = 1u << 6,
    kTintable = 1u << 7,     // material takes the car's body colour
    // With kAlphaBlend | kAlphaTest: GS TEST.AFAIL = RGB_ONLY. Fragments passing the alpha
    // test are drawn normally (depth write unless kNoDepthWrite); failing ones are still
    // blended, without depth write (course foliage: solid texels occlude, soft edges blend).
    kAlphaFailBlend = 1u << 8,
    // GS ALPHA 0x89 ((Cd - 0) * As + 0): the frame buffer is multiplied by the fragment alpha
    // (texel alpha x vertex alpha, 0x80 = 1.0); the fragment colour is not used. Prize cups
    // darken the inside of their bowls this way.
    kMultiplyDest = 1u << 9,
    // GS FBMSK 0x00FFFFFF: the pass writes only the frame-buffer alpha (course shadow masks
    // that switch the road sheen off). Drawn only for DrawItem::frame_alpha items.
    kAlphaOnly = 1u << 10,
    // GS FBMSK 0xFF000000: colour only, the frame-buffer alpha is kept (DrawItem::frame_alpha).
    kKeepAlpha = 1u << 11,
    // With kAdditive: GS ALPHA 0x58 (Cs * Ad + Cd), the colour added weighted by the frame-buffer
    // alpha earlier passes left (course road sheen). DrawItem::frame_alpha items only; elsewhere
    // the pass is plain additive.
    kDestAlphaAdd = 1u << 12,
    // The vertex colours' RGB are the GS bytes as they are (0x80 = 128), a texel factor up to
    // 255/128 that the renderer applies: GTM1 packets with a colour past 0x80 (Trial Mountain's
    // scenery). Other packets store 0x80 as 255 (gtm1.cpp expand_color).
    kVertexColor2x = 1u << 13,
    // GS FBMSK 0xFFFFFFFF with depth writes on: the pass writes only the depth. Cars whose glass
    // is drawn without depth writes leave its depth this way, after its reflection.
    kDepthOnly = 1u << 14,
    // With a car reflection pass: GS TEST.DATE with DATM 0, drawn only where the frame alpha
    // is below 0x80. Every reflection pass leaves 0x80 where it draws, so these skip what an
    // earlier one covered and the texels at full reflectivity (texel x vertex alpha = 0x80).
    kDestAlphaTest = 1u << 15,
};

// Fixed-function lighting material of a lit primitive (GT3 cars: GTM1 material
// records, one set per paint colour). Lit colour = emission + light ambient x ambient
// + sum of light diffuse x diffuse x max(N.L, 0) + light specular x specular x
// max(N.H, 0)^exponent, times `scale`, clamped to 2.0 (PS2 0x80 = 1.0), then
// multiplied with the texel. Invalid = the renderer's generic lighting.
struct Material {
    float ambient[3] = {0.2f, 0.2f, 0.2f};
    float diffuse[3] = {0.8f, 0.8f, 0.8f};
    float specular[3] = {0, 0, 0};
    float emission[3] = {0, 0, 0};
    // GT3 lamp materials (flag bit 0: brake-lamp lenses): this emission is scaled at draw
    // time by the car's lamp factor (DrawItem::lamp, k/4 of the last 4 frames braking).
    float lamp_emission[3] = {0, 0, 0};
    float shininess = 0; // specular exponent
    float scale = 1.0f;  // 255/128 for GT3 packets lit at the 255 scale (chunk flag 0x100)
    bool valid = false;
    // GT3 chunk kind 4 (course road sheen): the VU program lights the vertex with the first
    // light's specular term only, Phong with the reflected ray from a viewer 2 m above the
    // camera: Ls x Ms x max(0, L.R / |D|)^exponent (D = vertex - viewer, R = D reflected about
    // the normal), not clamped (the GS takes the low 8 bits of the scaled byte). See
    // docs/formats/course.md "Lighting".
    bool specular_local = false;
};

struct Primitive {
    u32 first_index = 0;
    u32 index_count = 0; // triangle list
    int texture = -1;    // index into Model::textures, -1 = untextured
    u32 flags = 0;
    float alpha_ref = -1; // kAlphaTest: discard alpha <= this (0..1); < 0 = the renderer's default
    Material material{};
    // GT3 reflection (environment-map) packets: the vertex colour is scaled per channel by
    // env_c0 + env_c1 x min(V.N, 0) (V the unit vector from the eye, N the normal), then
    // clamped to 255/128. env_rows = false: the renderer's DrawOptions::reflection_fresnel.
    bool env_rows = false;
    float env_c0[3] = {1, 1, 1};
    float env_c1[3] = {0, 0, 0};
    // Course environment passes (GTM1 chunk kinds 2 and 3, gtm1::Options::course_env_maps): the VU
    // makes the UVs per vertex from the reflected view vector (2: a sphere map in view space;
    // 3: GT3's panoramic lookup, yawed by the node's op 0x31 point or else the camera's forward)
    // and weights the vertex colour by env_c0 + env_c1 x min(V.N, 0) per vertex.
    u8 env_kind = 0;
    bool env_point = false;              // kind 3: an op 0x31 point was set before the draw
    float env_point_pos[3] = {0, 0, 0}; // that point (model space)
    float env_alpha[2] = {1, 0};         // the weight rows' alpha lanes (C0.w, C1.w), env_kind passes
};

struct Mesh {
    std::string name;
    int lod = 0;  // 0 = highest detail
    // The GTM1 node it draws (courses: the visibility lists pick nodes, render::DrawItem::nodes),
    // -1 = none.
    int node = -1;
    std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // column-major
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
    std::vector<Primitive> primitives;
};

struct Texture {
    std::string name;
    u32 width = 0, height = 0;
    std::vector<u32> rgba; // RGBA8 row-major
    bool clamp_u = false, clamp_v = false;
    bool no_mips = false; // sampled from the full-size image only (GS MXL 0), e.g. the tyres
};

struct Model {
    std::string name;
    std::vector<Mesh> meshes;
    std::vector<Texture> textures;
    float bounds_min[3] = {0, 0, 0};
    float bounds_max[3] = {0, 0, 0};

    void compute_bounds();
};

inline void Model::compute_bounds() {
    bool first = true;
    for (const Mesh& m : meshes)
        for (const Vertex& v : m.vertices)
            for (int i = 0; i < 3; ++i) {
                if (first || v.pos[i] < bounds_min[i]) bounds_min[i] = v.pos[i];
                if (first || v.pos[i] > bounds_max[i]) bounds_max[i] = v.pos[i];
                if (i == 2) first = false;
            }
}

// Copies the meshes accepted by `keep` (and only the textures they use) into a new model.
template <class Pred>
Model extract(const Model& src, Pred keep) {
    Model out;
    out.name = src.name;
    std::vector<int> remap(src.textures.size(), -1);
    for (const Mesh& m : src.meshes) {
        if (!keep(m)) continue;
        Mesh copy = m;
        for (Primitive& p : copy.primitives) {
            if (p.texture < 0 || size_t(p.texture) >= src.textures.size()) continue;
            int& r = remap[size_t(p.texture)];
            if (r < 0) {
                r = int(out.textures.size());
                out.textures.push_back(src.textures[size_t(p.texture)]);
            }
            p.texture = r;
        }
        out.meshes.push_back(std::move(copy));
    }
    out.compute_bounds();
    return out;
}

} // namespace gt3r::scene
