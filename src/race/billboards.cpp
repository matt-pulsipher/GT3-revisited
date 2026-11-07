#include "race/billboards.h"

#include "base/log.h"
#include "formats/clut_animation.h"
#include "formats/gs_memory.h"
#include "formats/tex1.h"
#include "formats/texture_pack.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace gt3r::race {

namespace {
float rdf(ByteSpan d, size_t o) {
    u32 v = rd32(d, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
} // namespace

bool Billboards::load(render::GpuContext& ctx, ByteSpan b, const std::string& name) {
    sprites_.clear();
    verts_.clear();
    gpu_.reset();
    cluts_ = {};
    ClutTextures clut;
    if (b.size() < 0x28 || std::memcmp(b.data(), "BLBd", 4) != 0) return false;
    u16 lists = rd16(b, 0x10), types = rd16(b, 0x14);
    u32 list_ptrs = rd32(b, 0x18), tex_off = rd32(b, 0x1C), type_off = rd32(b, 0x24);
    if (!lists || !types || list_ptrs + 4u * lists > b.size() || type_off + 16u * types > b.size()) return false;
    scene::Model model;
    model.name = name + "/billboards";
    try {
        if (tex_off < b.size() && Tex1::is_tex1(b.subspan(tex_off))) {
            ByteSpan td = b.subspan(tex_off);
            Tex1 tex = Tex1::parse(td);
            GsMemory gs;
            tex.upload(td, gs);
            // (Textures fed by CLUT animations: in the animations' state.)
            if (!tex.clut_animations.empty()) clut = ClutTextures(td, true);
            for (size_t i = 0; i < tex.textures.size(); ++i) {
                Tex1::Image img = tex.decode(gs, i);
                if (const std::vector<u32>* px = clut.rgba(u32(i)); px && px->size() == img.rgba.size()) img.rgba = *px;
                if (texture_pack::replace_decoded(td, i, img)) clut.erase(u32(i)); // a pack's picture: not animated
                scene::Texture t;
                t.name = name + "/billboard" + std::to_string(i);
                t.width = img.width, t.height = img.height;
                t.rgba = std::move(img.rgba);
                // (decode() already maps the GS alpha 0..0x80 to 0..255.)
                t.clamp_u = t.clamp_v = true;
                model.textures.push_back(std::move(t));
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN("Billboards %s: %s", name.c_str(), e.what());
        return false;
    }
    // One mesh per visibility-cell list, one primitive per texture.
    for (u16 l = 0; l < lists; ++l) {
        u32 lp = rd32(b, list_ptrs + 4u * l);
        if (!lp || lp + 4 > b.size()) continue;
        u32 n = rd32(b, lp);
        if (lp + 4 + 16ull * n > b.size()) continue;
        std::map<int, std::vector<u32>> by_tex;
        scene::Mesh mesh;
        mesh.name = "billboards" + std::to_string(l);
        for (u32 k = 0; k < n; ++k) {
            size_t o = lp + 4 + 16ull * k;
            u16 type = rd16(b, o), param = rd16(b, o + 2);
            if (type >= types) continue;
            size_t to = type_off + 16ull * type;
            float w = rdf(b, to), h = rdf(b, to + 4);
            u32 uvp = rd32(b, to + 8), colp = rd32(b, to + 12);
            if (uvp + 32 > b.size() || colp + 16 > b.size()) continue;
            Sprite s{rdf(b, o + 4), rdf(b, o + 8), rdf(b, o + 12), w, h};
            u32 base = u32(mesh.vertices.size());
            for (int c = 0; c < 4; ++c) { // corners: bottom-left, bottom-right, top-left, top-right
                scene::Vertex v;
                v.uv[0] = rdf(b, uvp + 8 * c), v.uv[1] = rdf(b, uvp + 8 * c + 4);
                u32 rgba = rd32(b, colp + 4 * c);
                auto ch = [&](int sh) { return std::min<u32>(255, ((rgba >> sh) & 0xFF) * 2); }; // 0x80 = 1.0
                v.color = ch(0) | (ch(8) << 8) | (ch(16) << 16) | (ch(24) << 24);
                v.normal[1] = 1;
                mesh.vertices.push_back(v);
            }
            sprites_.push_back(s);
            int tex = int(param) - 1;
            if (tex < 0 || tex >= int(model.textures.size())) tex = model.textures.empty() ? -1 : 0;
            by_tex[tex].insert(by_tex[tex].end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
        }
        if (mesh.vertices.empty()) continue;
        for (auto& [tex, idx] : by_tex) {
            scene::Primitive p;
            p.first_index = u32(mesh.indices.size());
            p.index_count = u32(idx.size());
            p.texture = tex;
            p.flags = scene::kAlphaTest | scene::kUnlit;
            mesh.indices.insert(mesh.indices.end(), idx.begin(), idx.end());
            mesh.primitives.push_back(p);
        }
        model.meshes.push_back(std::move(mesh));
    }
    if (sprites_.empty()) return false;
    // Initial placement (facing +z) so the per-mesh bounding spheres are right.
    for (const scene::Mesh& m : model.meshes) verts_.insert(verts_.end(), m.vertices.begin(), m.vertices.end());
    size_t vi = 0;
    for (scene::Mesh& m : model.meshes)
        for (size_t k = 0; k < m.vertices.size(); k += 4) {
            const Sprite& s = sprites_[vi / 4];
            float xs[4] = {-0.5f, 0.5f, -0.5f, 0.5f}, ys[4] = {0, 0, 1, 1};
            for (int c = 0; c < 4; ++c) {
                m.vertices[k + c].pos[0] = s.x + xs[c] * s.w;
                m.vertices[k + c].pos[1] = s.y + ys[c] * s.h;
                m.vertices[k + c].pos[2] = s.z;
            }
            vi += 4;
        }
    gpu_ = std::make_unique<render::GpuModel>(ctx, model);
    if (!clut.empty()) {
        std::vector<gtm1::ClutSet> sets;
        sets.push_back({0, std::move(clut)}); // the model's textures are the set's records in order
        cluts_.add(gpu_.get(), std::move(sets));
    }
    LOG_INFO("Billboards %s: %zu sprites, %zu textures", name.c_str(), sprites_.size(), model.textures.size());
    return true;
}

void Billboards::add_items(std::vector<render::DrawItem>& items, const render::Vec3& right) {
    if (!gpu_) return;
    render::Vec3 r{right.x, 0, right.z};
    float n = std::sqrt(r.x * r.x + r.z * r.z);
    if (n < 1e-4f) r = {1, 0, 0};
    else r = {r.x / n, 0, r.z / n};
    if (std::fabs(r.x - last_right_.x) + std::fabs(r.z - last_right_.z) > 1e-3f) { // re-face only when the camera turned
        last_right_ = r;
        float xs[4] = {-0.5f, 0.5f, -0.5f, 0.5f}, ys[4] = {0, 0, 1, 1};
        for (size_t i = 0; i < sprites_.size(); ++i) {
            const Sprite& s = sprites_[i];
            for (int c = 0; c < 4; ++c) {
                scene::Vertex& v = verts_[i * 4 + size_t(c)];
                v.pos[0] = s.x + r.x * xs[c] * s.w;
                v.pos[1] = s.y + ys[c] * s.h;
                v.pos[2] = s.z + r.z * xs[c] * s.w;
                v.normal[0] = -r.z, v.normal[1] = 0, v.normal[2] = r.x;
            }
        }
        gpu_->update_vertices(verts_);
    }
    items.push_back({gpu_.get(), {}, 0});
}

} // namespace gt3r::race
