#pragma once

// "GTTW" wheel models: wheel/<id> (464 files: one per car plus aftermarket
// wheels such as wheel/enw001_s) and section 4 of every car file.
//
//   0x00 "GTTW"   0x0C u32 size   0x10 u32 flags (0 or 8)   0x1C u32 GTM1 offset (0x20)
//
// The embedded GTM1 holds only the rim face, modelled at unit scale (radius
// about 1.08, outer face towards -X, i.e. a right-hand wheel); the game scales it to the car's wheel size and the
// tyre is not part of it. See docs/formats/car_model.md.
//
// A mod wheel's rim (vfs/mod_wheels.h) is a binary glTF model in the same frame, as
// `gt3tool model <iso> wheel/<id> <dir> --gltf` exports a disc rim: unit scale, the outer face
// on x = 0 (glTF axes: facing +X, the car's left), the axle along X. load_wheel_model reads
// either kind.

#include "formats/model/gtm1.h"
#include "scene/model.h"

#include <string>

namespace gt3r {

struct WheelFile {
    u32 flags = 0;
    ByteSpan gtm1;

    static bool is_wheel(ByteSpan data);
    static WheelFile parse(ByteSpan data);
};

// A binary glTF rim (a mod wheel's).
bool is_glb_rim(ByteSpan data);
// A rim of either kind: a GTTW wheel or a binary glTF one.
bool is_rim(ByteSpan data);

// A GTTW wheel's rim, or a glTF one's (drawn the same at every LOD; `opt.reflection` gives its
// reflection passes, formats/model/gltf.h).
scene::Model load_wheel_model(ByteSpan data, const gtm1::Options& opt = {}, const std::string& name = {});

} // namespace gt3r
