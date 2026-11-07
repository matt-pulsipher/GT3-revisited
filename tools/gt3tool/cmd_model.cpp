// Car / wheel model commands: export to OBJ+MTL+PNG, survey every model.

#include "tool.h"

#include "formats/model/car_model.h"
#include "formats/model/gltf_export.h"
#include "formats/model/prm_model.h"
#include "formats/model/wheel_model.h"
#include "formats/model/wheel_spec.h"
#include "game/data/car_database.h"
#include "formats/png_writer.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace gt3r::tool {

namespace {

struct ModelArgs {
    CarModelOptions car;
    int node = -1;
    bool gltf = false;
};

ModelArgs parse_flags(const Args& a, size_t first) {
    ModelArgs m;
    for (size_t i = first; i < a.size(); ++i) {
        const std::string& s = a[i];
        if (s.rfind("--lod=", 0) == 0) m.car.lod = std::stoi(s.substr(6));
        else if (s == "--effects") m.car.effects = true;
        else if (s == "--no-wheels") m.car.wheels = false;
        else if (s == "--no-reflection") m.car.reflection = false;
        else if (s.rfind("--node=", 0) == 0) m.node = std::stoi(s.substr(7));
        else if (s == "--gltf") m.gltf = true;
        else if (s.rfind("--paint=", 0) == 0) m.car.paint = std::stoi(s.substr(8));
        else throw FormatError("unknown option " + s);
    }
    return m;
}

std::string base_name(const std::string& path) {
    size_t s = path.find_last_of('/');
    return s == std::string::npos ? path : path.substr(s + 1);
}

scene::Model load_any(ByteSpan data, const ModelArgs& m, const std::string& name) {
    if (CarFile::is_car(data)) return load_car_model(data, m.car, name);
    gtm1::Options o;
    o.run.lod = m.car.lod;
    o.reflection = m.car.reflection;
    o.node = m.node;
    if (WheelFile::is_wheel(data)) return load_wheel_model(data, o, name);
    if (prm::PrmFile::is_prm(data)) return prm::load_prm_model(data, o, name);
    if (gtm1::Gtm1::is_gtm1(data)) return gtm1::decode_gtm1(data, o, name);
    throw FormatError("not a car, wheel, prize (.prm) or GTM1 model");
}

// OBJ uses a bottom-left UV origin; scene::Model uses the GS top-left one.
bool write_obj(const scene::Model& model, const std::string& dir, const std::string& name) {
    std::string mtl, obj = "# " + name + " (gt3tool model)\nmtllib " + name + ".mtl\n";
    for (size_t t = 0; t < model.textures.size(); ++t) {
        const scene::Texture& tx = model.textures[t];
        std::string png = name + "_tex" + std::to_string(t) + ".png";
        if (!save(dir + "/" + png, encode_png(tx.width, tx.height, tx.rgba))) return false;
        mtl += "newmtl tex" + std::to_string(t) + "\nKd 1 1 1\nmap_Kd " + png + "\n\n";
    }
    mtl += "newmtl untextured\nKd 0.7 0.7 0.7\n\nnewmtl envmap\nKd 0.8 0.8 0.8\nd 0.3\n\n";
    char buf[256];
    size_t base = 1;
    for (const scene::Mesh& mesh : model.meshes) {
        for (const scene::Vertex& v : mesh.vertices) {
            float r = (v.color & 0xFF) / 255.f, g = ((v.color >> 8) & 0xFF) / 255.f, b = ((v.color >> 16) & 0xFF) / 255.f;
            std::snprintf(buf, sizeof(buf), "v %.6f %.6f %.6f %.3f %.3f %.3f\nvt %.6f %.6f\nvn %.5f %.5f %.5f\n",
                          v.pos[0], v.pos[1], v.pos[2], r, g, b, v.uv[0], 1.0f - v.uv[1], v.normal[0], v.normal[1],
                          v.normal[2]);
            obj += buf;
        }
        std::string va = "#va"; // vertex alphas (OBJ has no slot for them), one line per mesh
        for (const scene::Vertex& v : mesh.vertices) {
            std::snprintf(buf, sizeof(buf), " %u", unsigned(v.color >> 24));
            va += buf;
        }
        obj += va + "\n";
        for (size_t p = 0; p < mesh.primitives.size(); ++p) {
            const scene::Primitive& pr = mesh.primitives[p];
            std::snprintf(buf, sizeof(buf), "# flags 0x%x\n", pr.flags);
            obj += buf;
            obj += "g " + mesh.name + "_p" + std::to_string(p) + "\nusemtl " +
                   (pr.texture >= 0                          ? "tex" + std::to_string(pr.texture)
                    : (pr.flags & scene::kEnvironmentMap) ? std::string("envmap")
                                                          : std::string("untextured")) +
                   "\n";
            for (u32 i = 0; i + 2 < pr.index_count; i += 3) {
                size_t a = base + mesh.indices[pr.first_index + i], b = base + mesh.indices[pr.first_index + i + 1],
                       c = base + mesh.indices[pr.first_index + i + 2];
                std::snprintf(buf, sizeof(buf), "f %zu/%zu/%zu %zu/%zu/%zu %zu/%zu/%zu\n", a, a, a, b, b, b, c, c, c);
                obj += buf;
            }
        }
        base += mesh.vertices.size();
    }
    auto bytes = [](const std::string& s) { return ByteSpan(reinterpret_cast<const u8*>(s.data()), s.size()); };
    return save(dir + "/" + name + ".mtl", bytes(mtl)) && save(dir + "/" + name + ".obj", bytes(obj));
}

void print_summary(const scene::Model& m) {
    size_t v = 0, t = 0, p = 0;
    for (const scene::Mesh& mesh : m.meshes)
        v += mesh.vertices.size(), t += mesh.indices.size() / 3, p += mesh.primitives.size();
    std::printf("%s: %zu meshes, %zu primitives, %zu vertices, %zu triangles, %zu textures, "
                "bounds (%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n",
                m.name.c_str(), m.meshes.size(), p, v, t, m.textures.size(), m.bounds_min[0], m.bounds_min[1],
                m.bounds_min[2], m.bounds_max[0], m.bounds_max[1], m.bounds_max[2]);
}

int cmd_model(Disc& disc, const Args& a) {
    ModelArgs m = parse_flags(a, 2);
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    std::string name = base_name(a[0]);
    if (CarFile::is_car(data)) {
        data::CarDatabase db;
        if (db.load(disc.volume())) m.car.wheel_spec = wheel_spec_from_database(db, name);
        if (const WheelSpec* w = m.car.wheel_spec ? &*m.car.wheel_spec : nullptr)
            std::printf("wheels: wheelbase %.3f, track %.3f/%.3f, tyre R %.4f/%.4f W %.3f/%.3f\n", w->wheelbase,
                        w->track[0], w->track[1], w->radius[0], w->radius[1], w->width[0], w->width[1]);
        else
            std::printf("wheels: no database spec, estimated from the body\n");
    }
    scene::Model model = load_any(data, m, name);
    print_summary(model);
    for (const scene::Mesh& mesh : model.meshes)
        std::printf("  %-10s %6zu vertices %6zu triangles %3zu primitives\n", mesh.name.c_str(), mesh.vertices.size(),
                    mesh.indices.size() / 3, mesh.primitives.size());
    if (!write_obj(model, a[1], name)) return 1;
    if (!m.gltf) return 0;
    GlbExportOptions o;
    o.lod = m.car.lod;
    return save(a[1] + "/" + name + ".glb", export_glb(model, o)) ? 0 : 1;
}

int cmd_model_survey(Disc& disc, const Args& a) {
    std::string prefix = a.empty() ? "" : a[0];
    size_t ok = 0, fail = 0, skipped = 0;
    for (const auto& e : disc.volume().entries()) {
        bool car = (e.path.rfind("cars/", 0) == 0 || e.path.rfind("menu/cars/", 0) == 0) &&
                   e.path.find(".list") == std::string::npos;
        bool wheel = e.path.rfind("wheel/", 0) == 0 || e.path.rfind("menu/wheel/", 0) == 0;
        bool prize = e.path.size() > 4 && e.path.compare(e.path.size() - 4, 4, ".prm") == 0;
        if (!(car || wheel || prize) || e.path.rfind(prefix, 0) != 0) continue;
        if (e.size == 0) { ++skipped; continue; } // cars/*/ho0100 are empty placeholders
        try {
            Bytes data = disc.volume().read(e);
            for (int lod = 0; lod < (car ? 3 : 1); ++lod) {
                ModelArgs m;
                m.car.lod = lod;
                m.car.effects = true;
                scene::Model model = load_any(data, m, e.path);
                if (model.meshes.empty()) throw FormatError("no geometry");
                if (prize) {
                    prm::PrmFile p = prm::PrmFile::parse(data);
                    if (p.view_keys.empty() && p.model_keys.empty()) throw FormatError("no animation");
                }
            }
            ++ok;
        } catch (const std::exception& ex) {
            ++fail;
            std::printf("FAIL %s: %s\n", e.path.c_str(), ex.what());
        }
    }
    std::printf("decoded %zu, failed %zu, empty %zu\n", ok, fail, skipped);
    return fail ? 1 : 0;
}

// Prize scene: header, lights, key tracks; optional OBJ export posed at a frame.
int cmd_prm(Disc& disc, const Args& a) {
    Bytes data = disc.volume().read(need(disc.volume(), a[0]));
    prm::PrmFile p = prm::PrmFile::parse(data);
    float frame = -1;
    bool packets = false;
    std::string outdir;
    for (size_t i = 1; i < a.size(); ++i) {
        if (a[i].rfind("--frame=", 0) == 0) frame = std::stof(a[i].substr(8));
        else if (a[i] == "--packets") packets = true;
        else outdir = a[i];
    }
    std::printf("%s: %s, frames %.0f..%.0f loop %.0f at %.0f fps, flags %u, display %.3f %.3f\n", a[0].c_str(),
                p.prm0 ? "PRM0" : "PRZ0", p.start_frame, p.last_frame, p.loop_frame, p.fps, p.flags, p.display[0],
                p.display[1]);
    std::printf("view keys %zu, model keys %zu, lights %zu, GTM1 at 0x%zx\n", p.view_keys.size(), p.model_keys.size(),
                p.lights.size(), size_t(p.gtm1.data() - data.data()));
    for (size_t i = 0; i < p.lights.size(); ++i) {
        const prm::Light& l = p.lights[i];
        std::printf("  light %zu: dir (%.4f %.4f %.4f %.2f) diffuse (%.3f %.3f %.3f %.2f) ambient (%.3f %.3f %.3f %.2f) "
                    "specular (%.3f %.3f %.3f %.2f)\n",
                    i, l.direction[0], l.direction[1], l.direction[2], l.direction[3], l.diffuse[0], l.diffuse[1],
                    l.diffuse[2], l.diffuse[3], l.ambient[0], l.ambient[1], l.ambient[2], l.ambient[3], l.specular[0],
                    l.specular[1], l.specular[2], l.specular[3]);
    }
    for (float t : {p.start_frame, p.loop_frame, p.last_frame}) {
        prm::Matrix m = p.model_at(t), v = p.view_at(t);
        std::printf("  t=%5.0f model T (%.2f %.2f %.2f)  view T (%.2f %.2f %.2f)\n", t, m[12], m[13], m[14], v[12],
                    v[13], v[14]);
    }
    if (packets) {
        gtm1::Gtm1 g = gtm1::Gtm1::parse(p.gtm1);
        std::printf("GTM1: %zu nodes, %zu chunks, %zu materials, %zu texture sets\n", g.nodes.size(), g.chunks.size(),
                    g.materials.size(), g.texsets.size());
        for (size_t m = 0; m < g.materials.size(); ++m) {
            const gtm1::Material& mt = g.materials[m];
            std::printf("  material %zu: flags %u shininess %.2f", m, mt.flags, mt.shininess);
            for (const auto& r : mt.rows) std::printf(" (%.2f %.2f %.2f %.2f)", r[0], r[1], r[2], r[3]);
            std::printf("\n");
        }
        for (u32 n = 0; n < g.nodes.size(); ++n)
            for (const gtm1::Draw& d : g.run(n)) {
                std::printf("  node %u chunk %u flags 0x%04x texset %u alpha 0x%02x fbmsk %08x C0 %.2f %.2f %.2f %.2f C1 %.2f %.2f %.2f %.2f\n",
                            n, d.chunk, g.chunks[d.chunk].flags, d.texset, d.alpha_mode, d.fb_mask, d.env_c0[0],
                            d.env_c0[1], d.env_c0[2], d.env_c0[3], d.env_c1[0], d.env_c1[1], d.env_c1[2], d.env_c1[3]);
                for (const gtm1::Packet& pk : g.chunks[d.chunk].packets) {
                    gtm1::DecodedPacket dp = g.decode_packet(pk);
                    std::printf("    packet tex %3u mat %3u: %3zu verts%s%s%s%s abe %d tme %d\n", pk.mat & 0x1FF,
                                pk.mat >> 9, dp.vertices.size(), dp.has_normals ? " normals" : "",
                                dp.has_colors ? " colours" : "", dp.has_uv ? " uv" : "",
                                dp.env_normals ? " env-normals" : "", dp.prim.abe, dp.prim.tme);
                    if (dp.has_colors && !dp.vertices.empty()) { // vertex RGBA range (decoded, 0x80 -> 255)
                        u32 lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
                        for (const scene::Vertex& v : dp.vertices)
                            for (int k = 0; k < 4; ++k) {
                                u32 c = (v.color >> (8 * k)) & 0xFF;
                                lo[k] = std::min(lo[k], c), hi[k] = std::max(hi[k], c);
                            }
                        std::printf("      rgba %u-%u %u-%u %u-%u %u-%u (raw rgb max 0x%02x)\n", lo[0], hi[0], lo[1], hi[1],
                                    lo[2], hi[2], lo[3], hi[3], dp.raw_rgb_max);
                    }
                }
            }
    }
    if (outdir.empty()) return 0;
    std::string name = base_name(a[0]);
    if (name.size() > 4) name.resize(name.size() - 4);
    scene::Model model = prm::load_prm_model(data, {}, name);
    if (frame >= 0) { // bake model * view of that frame (camera space, looking down -Z) + sphere-map UVs
        prm::Matrix m = prm::multiply(p.model_at(frame), p.view_at(frame));
        prm::apply_sphere_map(model, m);
        for (scene::Mesh& mesh : model.meshes)
            for (scene::Vertex& v : mesh.vertices) {
                float x = v.pos[0], y = v.pos[1], z = v.pos[2], nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
                for (int k = 0; k < 3; ++k) {
                    v.pos[k] = x * m[k] + y * m[4 + k] + z * m[8 + k] + m[12 + k];
                    v.normal[k] = nx * m[k] + ny * m[4 + k] + nz * m[8 + k];
                }
            }
        model.compute_bounds();
    }
    print_summary(model);
    return write_obj(model, outdir, name) ? 0 : 1;
}

} // namespace

GT3TOOL_COMMAND(prm, "prm", "<vol-path> [outdir] [--frame=N] [--packets]",
                "describe a prize scene (.prm: medal, trophy, license card); export its model posed at a frame", 1,
                cmd_prm);
GT3TOOL_COMMAND(model, "model",
                "<vol-path> <outdir> [--lod=N|-1] [--paint=N] [--effects] [--no-wheels] [--no-reflection] [--node=N] "
                "[--gltf]",
                "export a car / wheel / prize (.prm) / GTM1 model to OBJ+MTL+PNG (+ binary glTF)", 2, cmd_model);
GT3TOOL_COMMAND(model_survey, "model-survey", "[prefix]", "decode every car, wheel and prize (.prm) model (all LODs)", 0,
                cmd_model_survey);

} // namespace gt3r::tool
