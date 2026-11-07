#pragma once

// Binary glTF 2.0 (.glb) export of a decoded model, for 3D software such as Blender (car and
// course viewers, `gt3tool model --gltf`).
//
// The file follows glTF's conventions: +Y up, the model facing +Z, metres. That is GT3's frame
// turned half a turn about Y, the turn load_glb undoes, so an exported car body loads back as a
// mod car's body (docs/MODDING.md "Car models"). Each mesh is a node with its transform; each
// texture is an embedded PNG; vertex colours are COLOR_0 (prelit course scenery is lit by them,
// with KHR_materials_unlit). Materials are named for load_glb's recognition: "paint_..." for
// primitives that take the car colour, "brake_..." and "lamp_..." for lamps, "glass_..." for
// a car's blended lit surfaces.
//
// Passes drawn over other geometry are left out: reflection (environment-map) passes unless
// asked for, and the frame-alpha, depth-only and multiply passes.

#include "scene/model.h"

namespace gt3r {

struct GlbExportOptions {
    int lod = 0;              // the meshes of this LOD; -1: every LOD, each under a node "LOD <n>"
    bool reflections = false; // keep the reflection passes (they repeat the surfaces they shine on)
};

Bytes export_glb(const scene::Model& model, const GlbExportOptions& opt = {});

} // namespace gt3r
