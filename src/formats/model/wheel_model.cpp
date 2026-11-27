#include "formats/model/wheel_model.h"

#include "formats/model/gltf.h"

namespace gt3r {

bool WheelFile::is_wheel(ByteSpan data) { return has_magic(data, "GTTW") && data.size() >= 0x20; }

WheelFile WheelFile::parse(ByteSpan data) {
    if (!is_wheel(data)) throw FormatError("not a GTTW wheel model");
    WheelFile w;
    u32 size = rd32(data, 0x0C);
    if (size > data.size()) size = u32(data.size()); // (a modified game's file stating more than it has: what it has)
    w.flags = rd32(data, 0x10);
    u32 off = rd32(data, 0x1C);
    if (off >= size) throw FormatError("GTTW model offset out of range");
    w.gtm1 = data.subspan(off, size - off);
    return w;
}

bool is_glb_rim(ByteSpan data) { return has_magic(data, "glTF") && data.size() >= 12; }

bool is_rim(ByteSpan data) { return WheelFile::is_wheel(data) || is_glb_rim(data); }

scene::Model load_wheel_model(ByteSpan data, const gtm1::Options& opt, const std::string& name) {
    if (is_glb_rim(data)) {
        GlbOptions g;
        g.reflections = opt.reflection;
        scene::Model m = load_glb(data, g, name);
        for (scene::Mesh& mesh : m.meshes) mesh.name = name;
        return m;
    }
    return gtm1::decode_gtm1(WheelFile::parse(data).gtm1, opt, name);
}

} // namespace gt3r
