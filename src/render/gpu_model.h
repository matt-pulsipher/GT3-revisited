#pragma once

// A scene::Model uploaded to the GPU: one vertex and one index buffer for
// all meshes, one texture per scene::Texture (with mipmaps; identical texels
// share one texture across models, GpuContext::acquire_texture), and a line
// index list per mesh for wireframe display.

#include "render/gpu_context.h"
#include "render/math3d.h"
#include "scene/model.h"

#include <vector>

namespace gt3r::render {

struct ModelStats {
    size_t meshes = 0, primitives = 0, vertices = 0, triangles = 0, textures = 0;
    int max_lod = 0;
};
// CPU-side statistics; `lod` < 0 counts every mesh, otherwise only that LOD.
ModelStats model_stats(const scene::Model& model, int lod = -1);

class GpuModel {
public:
    // `keep_geometry`: also keep the vertices and indices on the CPU (geometry()), for the
    // triangles GT3 clips at the guard band (DrawItem::guard_band).
    GpuModel(GpuContext& ctx, const scene::Model& model, bool keep_geometry = false);
    // A model whose geometry changes (update_geometry), drawn with `textures`' textures (shared:
    // that model must outlive this one; texture indices are its). Empty until the first update.
    GpuModel(GpuContext& ctx, const GpuModel& textures);
    ~GpuModel();
    GpuModel(const GpuModel&) = delete;
    GpuModel& operator=(const GpuModel&) = delete;

    bool valid() const { return vertices_ != nullptr && indices_ != nullptr; }
    // Rewrites the vertex buffer (same layout and count as at construction, meshes in
    // order), e.g. camera-facing billboards. Returns false on a size mismatch.
    bool update_vertices(const std::vector<scene::Vertex>& all);
    // Replaces textures' texels (RGBA8, the size they were uploaded with) and rebuilds their
    // mipmaps, e.g. textures recoloured by CLUT animations; a texture other models share
    // becomes this model's own first. Returns false when one is skipped (unknown index, size
    // mismatch) or the upload fails.
    struct TextureUpdate {
        size_t index = 0;
        const std::vector<u32>* rgba = nullptr;
    };
    bool update_textures(const std::vector<TextureUpdate>& updates);
    // Replaces the geometry: the meshes and the vertex and index buffers (grown when needed);
    // no wireframe lines. E.g. the course's animated nodes, every frame.
    bool update_geometry(const std::vector<scene::Mesh>& meshes);
    size_t vertex_count() const { return vertex_count_; }
    // The buffers' contents on the CPU (MeshInfo::vertex_offset / index_offset into them), when
    // kept: the constructor's keep_geometry, or keep_geometry() before update_geometry.
    struct Geometry {
        std::vector<scene::Vertex> vertices;
        std::vector<u32> indices;
    };
    const Geometry* geometry() const { return keep_ ? &geometry_ : nullptr; }
    void keep_geometry() { keep_ = true; }

    struct MeshInfo {
        Mat4 transform;
        int lod = 0;
        int node = -1;         // scene::Mesh::node
        s32 vertex_offset = 0; // added to every index
        u32 index_offset = 0;  // first triangle index in the shared buffer
        u32 line_first = 0, line_count = 0;
        std::vector<scene::Primitive> primitives; // first_index relative to index_offset
        // Bounding sphere of the mesh's vertices in mesh space (before `transform`),
        // used for frustum culling, draw distance and transparency sorting.
        Vec3 center;
        float radius = 0;
        // Each primitive's own sphere (the vertices of its triangles), in the same space: the
        // guard band's clipping skips the primitives it cannot cut (View3D::upload_clipped).
        struct Sphere {
            Vec3 center;
            float radius = 0;
        };
        std::vector<Sphere> primitive_spheres; // parallel to `primitives`
    };
    struct TextureInfo {
        SDL_GPUTexture* texture = nullptr;
        bool clamp_u = false, clamp_v = false;
        bool no_mips = false; // scene::Texture::no_mips
        u32 width = 0, height = 0;
    };

    const std::vector<MeshInfo>& meshes() const { return meshes_; }
    // The meshes of GTM1 node `node` (MeshInfo::node) as indices into meshes(), in model order
    // (DrawItem::nodes); empty when it has none.
    struct MeshRange {
        const u32* first = nullptr;
        const u32* last = nullptr;
        const u32* begin() const { return first; }
        const u32* end() const { return last; }
    };
    MeshRange node_meshes(u32 node) const {
        if (size_t(node) + 1 >= node_start_.size()) return {};
        return {node_meshes_.data() + node_start_[node], node_meshes_.data() + node_start_[size_t(node) + 1]};
    }
    const std::vector<TextureInfo>& textures() const { return texture_source_ ? texture_source_->textures_ : textures_; }
    SDL_GPUBuffer* vertex_buffer() const { return vertices_; }
    SDL_GPUBuffer* index_buffer() const { return indices_; }

private:
    // One write into a GPU buffer for stage().
    struct Upload {
        SDL_GPUBuffer* buffer;
        const void* data;
        u32 size;
    };
    // Copies the uploads into their buffers through staging_ in one command buffer.
    bool stage(const Upload* uploads, int count);
    // Fills node_start_ / node_meshes_ from meshes_.
    void index_nodes();

    GpuContext* ctx_;
    SDL_GPUDevice* device_;
    SDL_GPUBuffer* vertices_ = nullptr;
    SDL_GPUBuffer* indices_ = nullptr;
    std::vector<MeshInfo> meshes_;
    // node_meshes(n) = node_meshes_[node_start_[n], node_start_[n + 1]).
    std::vector<u32> node_start_, node_meshes_;
    std::vector<TextureInfo> textures_;
    const GpuModel* texture_source_ = nullptr; // the model whose textures this one draws with
    u32 vertex_capacity_ = 0, index_capacity_ = 0; // update_geometry's buffers (bytes)
    size_t vertex_count_ = 0;
    // The updates' transfer buffer, kept between them (mapped with cycling: one an upload still
    // reads is set aside, so after a few frames none is created). A new one per update allocated
    // device memory every frame, ~7% of a Hiby R4's frame on Trial Mountain.
    SDL_GPUTransferBuffer* staging_ = nullptr;
    u32 staging_capacity_ = 0;
    std::vector<u32> uploaded_indices_; // update_geometry: the index buffer's contents
    bool keep_ = false;
    Geometry geometry_;
};

} // namespace gt3r::render
