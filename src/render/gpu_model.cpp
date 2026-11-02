#include "render/gpu_model.h"

#include "base/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace gt3r::render {

ModelStats model_stats(const scene::Model& model, int lod) {
    ModelStats s;
    s.textures = model.textures.size();
    for (const scene::Mesh& m : model.meshes) {
        s.max_lod = std::max(s.max_lod, m.lod);
        if (lod >= 0 && m.lod != lod) continue;
        ++s.meshes;
        s.vertices += m.vertices.size();
        s.primitives += m.primitives.size();
        for (const scene::Primitive& p : m.primitives) s.triangles += p.index_count / 3;
    }
    return s;
}

namespace {

// Meshes -> one vertex and one index list, per-mesh draw info; `lines`: wireframe edge lists.
void assemble(const std::vector<scene::Mesh>& meshes, size_t texture_count, bool lines,
              std::vector<scene::Vertex>& vertices, std::vector<u32>& indices, std::vector<GpuModel::MeshInfo>& out) {
    for (const scene::Mesh& m : meshes) {
        GpuModel::MeshInfo info;
        info.transform = Mat4::from(m.transform);
        info.lod = m.lod;
        info.vertex_offset = s32(vertices.size());
        info.index_offset = u32(indices.size());
        vertices.insert(vertices.end(), m.vertices.begin(), m.vertices.end());
        if (!m.vertices.empty()) {
            float lo[3], hi[3];
            for (int k = 0; k < 3; ++k) lo[k] = hi[k] = m.vertices[0].pos[k];
            for (const scene::Vertex& v : m.vertices)
                for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], v.pos[k]), hi[k] = std::max(hi[k], v.pos[k]);
            info.center = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
            float r2 = 0;
            for (const scene::Vertex& v : m.vertices) {
                Vec3 d = Vec3{v.pos[0], v.pos[1], v.pos[2]} - info.center;
                r2 = std::max(r2, dot(d, d));
            }
            info.radius = std::sqrt(r2);
        }
        u32 nv = u32(m.vertices.size());
        // Drop out-of-range indices instead of letting the GPU read garbage.
        for (const scene::Primitive& p : m.primitives) {
            scene::Primitive q = p;
            q.first_index = u32(indices.size()) - info.index_offset;
            u32 end = std::min<u32>(p.first_index + p.index_count, u32(m.indices.size()));
            u32 tri_end = p.first_index + (end > p.first_index ? (end - p.first_index) / 3 * 3 : 0);
            for (u32 i = p.first_index; i < tri_end; i += 3) {
                u32 a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
                if (a >= nv || b >= nv || c >= nv) continue;
                indices.insert(indices.end(), {a, b, c});
            }
            q.index_count = u32(indices.size()) - info.index_offset - q.first_index;
            if (q.texture >= int(texture_count)) q.texture = -1;
            if (!q.index_count) continue;
            info.primitives.push_back(q);
            // Its sphere: the box centre, the farthest vertex.
            const u32* idx = indices.data() + info.index_offset + q.first_index;
            float lo[3], hi[3];
            for (int k = 0; k < 3; ++k) lo[k] = hi[k] = m.vertices[idx[0]].pos[k];
            for (u32 i = 0; i < q.index_count; ++i)
                for (int k = 0; k < 3; ++k)
                    lo[k] = std::min(lo[k], m.vertices[idx[i]].pos[k]), hi[k] = std::max(hi[k], m.vertices[idx[i]].pos[k]);
            GpuModel::MeshInfo::Sphere sp;
            sp.center = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
            float r2 = 0;
            for (u32 i = 0; i < q.index_count; ++i) {
                const float* v = m.vertices[idx[i]].pos;
                const Vec3 d = Vec3{v[0], v[1], v[2]} - sp.center;
                r2 = std::max(r2, dot(d, d));
            }
            sp.radius = std::sqrt(r2);
            info.primitive_spheres.push_back(sp);
        }
        // Unique edges for the wireframe view.
        info.line_first = u32(indices.size()) - info.index_offset;
        if (lines) {
            std::unordered_set<u64> edges;
            size_t tri_count = indices.size() - info.index_offset;
            for (size_t i = info.index_offset; i < info.index_offset + tri_count; i += 3)
                for (int e = 0; e < 3; ++e) {
                    u32 a = indices[i + e], b = indices[i + (e + 1) % 3];
                    if (a > b) std::swap(a, b);
                    if (edges.insert((u64(a) << 32) | b).second) indices.insert(indices.end(), {a, b});
                }
        }
        info.line_count = u32(indices.size()) - info.index_offset - info.line_first;
        out.push_back(std::move(info));
    }
}

// Makes `buffer` hold at least `size` bytes, creating it or replacing it with one twice the size
// (contents lost). True when it is now a new buffer.
bool ensure_buffer(SDL_GPUDevice* device, SDL_GPUBufferUsageFlags usage, SDL_GPUBuffer*& buffer, u32& capacity,
                   u32 size, bool& ok) {
    ok = true;
    if (buffer && capacity >= size) return false;
    if (buffer) SDL_ReleaseGPUBuffer(device, buffer);
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = usage;
    bi.size = std::max(size, capacity * 2);
    buffer = SDL_CreateGPUBuffer(device, &bi);
    capacity = buffer ? bi.size : 0;
    ok = buffer != nullptr;
    return true;
}

} // namespace

GpuModel::GpuModel(GpuContext& ctx, const scene::Model& model, bool keep_geometry)
    : ctx_(&ctx), device_(ctx.device()), keep_(keep_geometry) {
    std::vector<scene::Vertex> vertices;
    std::vector<u32> indices;
    assemble(model.meshes, model.textures.size(), true, vertices, indices, meshes_);
    vertex_count_ = vertices.size();
    Uploader up(device_);
    if (!vertices.empty() && !indices.empty()) {
        vertices_ = up.buffer(SDL_GPU_BUFFERUSAGE_VERTEX, vertices.data(), u32(vertices.size() * sizeof(scene::Vertex)));
        indices_ = up.buffer(SDL_GPU_BUFFERUSAGE_INDEX, indices.data(), u32(indices.size() * sizeof(u32)));
    }
    if (keep_) geometry_ = {std::move(vertices), std::move(indices)};
    for (const scene::Texture& t : model.textures) {
        TextureInfo ti;
        ti.clamp_u = t.clamp_u;
        ti.clamp_v = t.clamp_v;
        ti.no_mips = t.no_mips;
        if (t.width && t.height && t.rgba.size() >= size_t(t.width) * t.height) {
            ti.texture = ctx.acquire_texture(up, t.width, t.height, t.rgba.data());
            ti.width = t.width, ti.height = t.height;
        }
        if (!ti.texture) LOG_WARN("Texture '%s' (%ux%u) not uploaded", t.name.c_str(), t.width, t.height);
        textures_.push_back(ti);
    }
    if (!up.submit()) LOG_WARN("Model upload failed: %s", SDL_GetError());
}

// Each upload goes through staging_ (grown when too small), at 16-byte aligned offsets, into its
// buffer from offset 0 with cycling: frames still drawing keep the old contents.
bool GpuModel::stage(const Upload* uploads, int count) {
    auto aligned = [](u32 v) { return (v + 15u) & ~15u; };
    u32 total = 0;
    for (int i = 0; i < count; ++i) total += aligned(uploads[i].size);
    if (total == 0) return true;
    if (!staging_ || staging_capacity_ < total) {
        if (staging_) SDL_ReleaseGPUTransferBuffer(device_, staging_);
        SDL_GPUTransferBufferCreateInfo ti{};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = std::max(total, staging_capacity_ * 2);
        staging_ = SDL_CreateGPUTransferBuffer(device_, &ti);
        staging_capacity_ = staging_ ? ti.size : 0;
        if (!staging_) return false;
    }
    auto* p = static_cast<u8*>(SDL_MapGPUTransferBuffer(device_, staging_, true));
    if (!p) return false;
    for (int i = 0, at = 0; i < count; at += int(aligned(uploads[i].size)), ++i)
        std::memcpy(p + at, uploads[i].data, uploads[i].size);
    SDL_UnmapGPUTransferBuffer(device_, staging_);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd) return false;
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    for (int i = 0, at = 0; i < count; at += int(aligned(uploads[i].size)), ++i) {
        if (!uploads[i].size) continue;
        SDL_GPUTransferBufferLocation src{staging_, u32(at)};
        SDL_GPUBufferRegion dst{uploads[i].buffer, 0, uploads[i].size};
        SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    }
    SDL_EndGPUCopyPass(cp);
    if (!SDL_SubmitGPUCommandBuffer(cmd)) return false;
    note_gpu_submits(device_, 1); // staging_ is this model's own: no new transfer buffer
    return true;
}

bool GpuModel::update_vertices(const std::vector<scene::Vertex>& all) {
    if (!vertices_ || all.size() != vertex_count_) return false;
    const Upload u{vertices_, all.data(), u32(all.size() * sizeof(scene::Vertex))};
    if (keep_) geometry_.vertices = all;
    return stage(&u, 1);
}

bool GpuModel::update_textures(const std::vector<TextureUpdate>& updates) {
    bool ok = true;
    Uploader up(device_);
    for (const TextureUpdate& u : updates) {
        TextureInfo* t = u.index < textures_.size() ? &textures_[u.index] : nullptr;
        if (!t || !t->texture || !u.rgba || u.rgba->size() != size_t(t->width) * t->height) {
            ok = false;
            continue;
        }
        if (ctx_->take_texture(t->texture)) {
            up.update_rgba(t->texture, t->width, t->height, u.rgba->data(), true, true);
        } else if (SDL_GPUTexture* own = up.texture_rgba(t->width, t->height, u.rgba->data(), true)) {
            ctx_->release_texture(t->texture); // other models keep drawing the shared texels
            t->texture = own;
        } else {
            ok = false;
        }
    }
    return up.submit() && ok;
}

GpuModel::GpuModel(GpuContext& ctx, const GpuModel& textures)
    : ctx_(&ctx), device_(ctx.device()), texture_source_(&textures) {}

bool GpuModel::update_geometry(const std::vector<scene::Mesh>& meshes) {
    std::vector<scene::Vertex> vertices;
    std::vector<u32> indices;
    meshes_.clear();
    assemble(meshes, textures().size(), false, vertices, indices, meshes_);
    vertex_count_ = vertices.size();
    const u32 vsize = u32(vertices.size() * sizeof(scene::Vertex)), isize = u32(indices.size() * sizeof(u32));
    if (vsize == 0 || isize == 0) return true; // nothing drawn (meshes_ is empty too)
    bool vok = true, iok = true;
    ensure_buffer(device_, SDL_GPU_BUFFERUSAGE_VERTEX, vertices_, vertex_capacity_, vsize, vok);
    // The index buffer only when its contents change (the animated nodes' usually don't).
    if (ensure_buffer(device_, SDL_GPU_BUFFERUSAGE_INDEX, indices_, index_capacity_, isize, iok))
        uploaded_indices_.clear();
    Upload uploads[2] = {{vertices_, vertices.data(), vsize}, {indices_, indices.data(), isize}};
    const bool same_indices = indices == uploaded_indices_;
    bool ok = vok && iok && stage(uploads, same_indices ? 1 : 2);
    if (ok && keep_) geometry_ = {std::move(vertices), indices};
    if (ok && !same_indices) uploaded_indices_ = std::move(indices);
    if (!ok) meshes_.clear(), uploaded_indices_.clear(), geometry_ = {};
    return ok;
}

GpuModel::~GpuModel() {
    if (staging_) SDL_ReleaseGPUTransferBuffer(device_, staging_);
    if (vertices_) SDL_ReleaseGPUBuffer(device_, vertices_);
    if (indices_) SDL_ReleaseGPUBuffer(device_, indices_);
    for (TextureInfo& t : textures_) ctx_->release_texture(t.texture);
}

} // namespace gt3r::render
