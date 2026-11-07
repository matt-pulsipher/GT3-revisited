#include "formats/model/gltf_export.h"

#include "formats/png_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace gt3r {

namespace {

std::string quoted(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (u8(c) < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", unsigned(u8(c)));
            out += b;
        } else {
            out += c;
        }
    }
    return out + "\"";
}

std::string num(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.9g", std::isfinite(v) ? double(v) : 0.0);
    return b;
}

// Passes drawn over other geometry are not surfaces of their own.
bool exported(const scene::Primitive& p, bool reflections) {
    if (p.index_count < 3) return false;
    if (p.flags & (scene::kAlphaOnly | scene::kDepthOnly | scene::kMultiplyDest)) return false;
    if (!reflections && ((p.flags & scene::kEnvironmentMap) || p.env_kind != 0)) return false;
    return true;
}

// The name load_glb gives a role by (gltf.h).
const char* role_of(const scene::Primitive& p) {
    if (p.flags & scene::kTintable) return "paint";
    const scene::Material& m = p.material;
    if (m.valid) {
        if (std::max({m.lamp_emission[0], m.lamp_emission[1], m.lamp_emission[2]}) > 0) return "brake";
        if (std::max({m.emission[0], m.emission[1], m.emission[2]}) > 0.5f) return "lamp";
        if ((p.flags & scene::kAlphaBlend) && !(p.flags & scene::kUnlit)) return "glass";
    }
    return "mat";
}

enum class Alpha : u8 { Opaque, Mask, Blend };

Alpha alpha_of(const scene::Primitive& p) {
    const bool test = p.flags & scene::kAlphaTest, blend = p.flags & (scene::kAlphaBlend | scene::kAdditive);
    if (test && (!blend || (p.flags & scene::kAlphaFailBlend))) return Alpha::Mask;
    return blend ? Alpha::Blend : Alpha::Opaque;
}

struct MaterialKey {
    int texture = -1; // exported texture
    Alpha alpha = Alpha::Opaque;
    float cutoff = 0.5f;
    bool double_sided = false, unlit = false;
    std::string role;
    bool operator<(const MaterialKey& o) const {
        return std::tie(texture, alpha, cutoff, double_sided, unlit, role) <
               std::tie(o.texture, o.alpha, o.cutoff, o.double_sided, o.unlit, o.role);
    }
};

// glTF faces +Z, GT3 -Z: a half turn about Y, r = (-1, 1, -1).
constexpr float kTurn[4] = {-1, 1, -1, 1};

class Writer {
public:
    Bytes bin;
    std::string views, accessors;
    int view_count = 0, accessor_count = 0;

    int view(const void* data, size_t bytes, int target) {
        const size_t offset = bin.size();
        const u8* p = static_cast<const u8*>(data);
        bin.insert(bin.end(), p, p + bytes);
        while (bin.size() % 4) bin.push_back(0);
        views += std::string(view_count ? "," : "") + "{\"buffer\":0,\"byteOffset\":" + std::to_string(offset) +
                 ",\"byteLength\":" + std::to_string(bytes) + (target ? ",\"target\":" + std::to_string(target) : "") + "}";
        return view_count++;
    }
    int accessor(int view, int component, size_t count, const char* type, const std::string& extra = {}) {
        accessors += std::string(accessor_count ? "," : "") + "{\"bufferView\":" + std::to_string(view) +
                     ",\"componentType\":" + std::to_string(component) + ",\"count\":" + std::to_string(count) +
                     ",\"type\":\"" + type + "\"" + extra + "}";
        return accessor_count++;
    }
};

void append_chunk(Bytes& out, u32 type, const u8* data, size_t size, u8 pad) {
    const size_t padded = (size + 3) & ~size_t(3);
    const u32 head[2] = {u32(padded), type};
    const u8* h = reinterpret_cast<const u8*>(head);
    out.insert(out.end(), h, h + 8);
    out.insert(out.end(), data, data + size);
    out.insert(out.end(), padded - size, pad);
}

} // namespace

Bytes export_glb(const scene::Model& model, const GlbExportOptions& opt) {
    Writer w;
    // Textures and materials as the exported primitives use them.
    std::map<int, int> texture_of;       // model texture -> exported texture
    std::map<MaterialKey, int> material_of;
    std::string images, textures, materials, meshes, nodes;
    int samplers[4] = {-1, -1, -1, -1};
    std::string sampler_list;
    int sampler_count = 0;
    bool unlit_used = false;
    auto texture_index = [&](int t) -> int {
        if (t < 0 || size_t(t) >= model.textures.size()) return -1;
        if (auto it = texture_of.find(t); it != texture_of.end()) return it->second;
        const scene::Texture& tx = model.textures[size_t(t)];
        if (tx.width == 0 || tx.height == 0 || tx.rgba.size() < size_t(tx.width) * tx.height) return -1;
        const Bytes png = encode_png(tx.width, tx.height, tx.rgba);
        const int v = w.view(png.data(), png.size(), 0);
        const int n = int(texture_of.size());
        images += std::string(n ? "," : "") + "{\"name\":" + quoted(tx.name.empty() ? "tex" + std::to_string(t) : tx.name) +
                  ",\"bufferView\":" + std::to_string(v) + ",\"mimeType\":\"image/png\"}";
        const int s = (tx.clamp_u ? 1 : 0) | (tx.clamp_v ? 2 : 0);
        if (samplers[s] < 0) {
            sampler_list += std::string(sampler_count ? "," : "") + "{\"magFilter\":9729,\"minFilter\":9987,\"wrapS\":" +
                            (tx.clamp_u ? "33071" : "10497") + ",\"wrapT\":" + (tx.clamp_v ? "33071" : "10497") + "}";
            samplers[s] = sampler_count++;
        }
        textures += std::string(n ? "," : "") + "{\"source\":" + std::to_string(n) + ",\"sampler\":" +
                    std::to_string(samplers[s]) + "}";
        texture_of[t] = n;
        return n;
    };
    auto material_index = [&](const scene::Primitive& p) -> int {
        MaterialKey k;
        k.texture = texture_index(p.texture);
        k.alpha = alpha_of(p);
        k.cutoff = k.alpha == Alpha::Mask && p.alpha_ref >= 0 ? std::clamp(p.alpha_ref, 0.0f, 1.0f) : 0.5f;
        k.double_sided = p.flags & scene::kDoubleSided;
        k.unlit = p.flags & scene::kUnlit;
        k.role = role_of(p);
        if (auto it = material_of.find(k); it != material_of.end()) return it->second;
        const int n = int(material_of.size());
        std::string name = k.role + (k.texture >= 0 ? "_tex" + std::to_string(k.texture) : "_plain") +
                           (k.alpha == Alpha::Blend ? "_blend" : k.alpha == Alpha::Mask ? "_cutout" : "") +
                           (k.double_sided ? "_2s" : "");
        std::string m = "{\"name\":" + quoted(name) + ",\"pbrMetallicRoughness\":{" +
                        (k.texture >= 0 ? "\"baseColorTexture\":{\"index\":" + std::to_string(k.texture) + "}," : "") +
                        "\"metallicFactor\":0,\"roughnessFactor\":0.5}";
        if (k.alpha == Alpha::Blend) m += ",\"alphaMode\":\"BLEND\"";
        if (k.alpha == Alpha::Mask) m += ",\"alphaMode\":\"MASK\",\"alphaCutoff\":" + num(k.cutoff);
        if (k.double_sided) m += ",\"doubleSided\":true";
        if (k.unlit) m += ",\"extensions\":{\"KHR_materials_unlit\":{}}", unlit_used = true;
        materials += std::string(n ? "," : "") + m + "}";
        material_of[k] = n;
        return n;
    };

    std::map<int, std::vector<int>> lod_nodes; // LOD -> mesh nodes
    int mesh_count = 0, node_count = 0;
    for (const scene::Mesh& mesh : model.meshes) {
        if (opt.lod >= 0 && mesh.lod != opt.lod) continue;
        std::vector<const scene::Primitive*> prims;
        for (const scene::Primitive& p : mesh.primitives)
            if (exported(p, opt.reflections) && p.first_index + u64(p.index_count) <= mesh.indices.size()) prims.push_back(&p);
        if (prims.empty() || mesh.vertices.empty()) continue;
        // Vertex colours past 0x80 (kVertexColor2x packets) are GS bytes: 0x80 is 1.0.
        std::vector<bool> twice(mesh.vertices.size(), false);
        for (const scene::Primitive* p : prims)
            if (p->flags & scene::kVertexColor2x)
                for (u32 i = 0; i < p->index_count; ++i)
                    if (u32 v = mesh.indices[p->first_index + i]; v < twice.size()) twice[v] = true;
        const size_t n = mesh.vertices.size();
        std::vector<float> pos(n * 3), nrm(n * 3), uv(n * 2);
        std::vector<u32> col(n);
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (size_t i = 0; i < n; ++i) {
            const scene::Vertex& v = mesh.vertices[i];
            float nx = -v.normal[0], ny = v.normal[1], nz = -v.normal[2];
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f && std::isfinite(len)) nx /= len, ny /= len, nz /= len;
            else nx = 0, ny = 1, nz = 0;
            const float p[3] = {-v.pos[0], v.pos[1], -v.pos[2]};
            for (int k = 0; k < 3; ++k) {
                pos[i * 3 + k] = p[k];
                lo[k] = std::min(lo[k], p[k]), hi[k] = std::max(hi[k], p[k]);
            }
            nrm[i * 3] = nx, nrm[i * 3 + 1] = ny, nrm[i * 3 + 2] = nz;
            uv[i * 2] = v.uv[0], uv[i * 2 + 1] = v.uv[1];
            u32 c = v.color;
            if (twice[i]) {
                u32 out = c & 0xFF000000u;
                for (int k = 0; k < 3; ++k) out |= std::min<u32>(255, (((c >> (8 * k)) & 0xFF) * 255 + 64) / 128) << (8 * k);
                c = out;
            }
            col[i] = c;
        }
        char bounds[160];
        std::snprintf(bounds, sizeof bounds, ",\"min\":[%s,%s,%s],\"max\":[%s,%s,%s]", num(lo[0]).c_str(), num(lo[1]).c_str(),
                      num(lo[2]).c_str(), num(hi[0]).c_str(), num(hi[1]).c_str(), num(hi[2]).c_str());
        const int ap = w.accessor(w.view(pos.data(), pos.size() * 4, 34962), 5126, n, "VEC3", bounds);
        const int an = w.accessor(w.view(nrm.data(), nrm.size() * 4, 34962), 5126, n, "VEC3");
        const int au = w.accessor(w.view(uv.data(), uv.size() * 4, 34962), 5126, n, "VEC2");
        const int ac = w.accessor(w.view(col.data(), col.size() * 4, 34962), 5121, n, "VEC4", ",\"normalized\":true");
        std::string list;
        for (const scene::Primitive* p : prims) {
            const int ai = w.accessor(w.view(mesh.indices.data() + p->first_index, size_t(p->index_count) * 4, 34963), 5125,
                                      p->index_count, "SCALAR");
            list += std::string(list.empty() ? "" : ",") + "{\"attributes\":{\"POSITION\":" + std::to_string(ap) +
                    ",\"NORMAL\":" + std::to_string(an) + ",\"TEXCOORD_0\":" + std::to_string(au) +
                    ",\"COLOR_0\":" + std::to_string(ac) + "},\"indices\":" + std::to_string(ai) +
                    ",\"material\":" + std::to_string(material_index(*p)) + "}";
        }
        meshes += std::string(mesh_count ? "," : "") + "{\"name\":" + quoted(mesh.name) + ",\"primitives\":[" + list + "]}";
        // The node's transform, turned with the vertices: R M R (R its own inverse).
        std::string matrix;
        static const std::array<float, 16> kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        if (mesh.transform != kIdentity) {
            matrix = ",\"matrix\":[";
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                    matrix += (c || r ? "," : "") + num(mesh.transform[size_t(c * 4 + r)] * kTurn[r] * kTurn[c]);
            matrix += "]";
        }
        nodes += std::string(node_count ? "," : "") + "{\"name\":" + quoted(mesh.name) + ",\"mesh\":" +
                 std::to_string(mesh_count) + matrix + "}";
        lod_nodes[mesh.lod].push_back(node_count++);
        ++mesh_count;
    }

    std::string roots;
    for (const auto& [lod, list] : lod_nodes) {
        if (opt.lod >= 0) {
            for (int i : list) roots += std::string(roots.empty() ? "" : ",") + std::to_string(i);
            continue;
        }
        std::string children;
        for (int i : list) children += std::string(children.empty() ? "" : ",") + std::to_string(i);
        nodes += std::string(node_count ? "," : "") + "{\"name\":\"LOD " + std::to_string(lod) + "\",\"children\":[" +
                 children + "]}";
        roots += std::string(roots.empty() ? "" : ",") + std::to_string(node_count++);
    }

    std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"GT3 Revisited\"}";
    if (unlit_used) json += ",\"extensionsUsed\":[\"KHR_materials_unlit\"]";
    json += ",\"scene\":0,\"scenes\":[{\"name\":" + quoted(model.name) + ",\"nodes\":[" + roots + "]}]";
    if (node_count) json += ",\"nodes\":[" + nodes + "]";
    if (mesh_count) json += ",\"meshes\":[" + meshes + "]";
    if (!materials.empty()) json += ",\"materials\":[" + materials + "]";
    if (!images.empty())
        json += ",\"images\":[" + images + "],\"textures\":[" + textures + "],\"samplers\":[" + sampler_list + "]";
    if (w.accessor_count) json += ",\"accessors\":[" + w.accessors + "]";
    if (w.view_count) json += ",\"bufferViews\":[" + w.views + "]";
    if (!w.bin.empty()) json += ",\"buffers\":[{\"byteLength\":" + std::to_string(w.bin.size()) + "}]";
    json += "}";

    Bytes out;
    const u32 header[3] = {0x46546C67u, 2, 0};
    out.insert(out.end(), reinterpret_cast<const u8*>(header), reinterpret_cast<const u8*>(header) + 12);
    append_chunk(out, 0x4E4F534Au, reinterpret_cast<const u8*>(json.data()), json.size(), ' ');
    if (!w.bin.empty()) append_chunk(out, 0x004E4942u, w.bin.data(), w.bin.size(), 0);
    const u32 total = u32(out.size());
    std::memcpy(&out[8], &total, 4);
    return out;
}

} // namespace gt3r
