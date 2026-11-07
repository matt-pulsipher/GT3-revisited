#pragma once

// Reusable 3D model viewer screen (renderer workstream).
//
// Shows a list of items on the left; selecting one calls `load` and displays
// the resulting model in an orbit/turntable view with stats (meshes,
// vertices, triangles, textures) and a wireframe toggle. Car and course
// viewers build on this:
//
//   app.push(make_model_viewer("Cars", names, [](App& app, size_t i) {
//       return decode_car(app.vfs(), ...); // returns scene::Model
//   }));
//
// `load` may throw (e.g. FormatError); the message is shown instead of the
// model. When the GPU renderer is unavailable the screen still works but
// only shows the list and stats.
//
// R2 saves the item as binary glTF (formats/model/gltf_export.h) at the LOD shown, in
// <user data>/exports/<ModelExport::folder>/, for 3D software.

#include "scene/model.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gt3r {

class App;
class Screen;

using ModelLoader = std::function<scene::Model(App& app, size_t index)>;

struct ModelExport {
    std::string folder;  // under exports/; empty: the title's
    ModelLoader load;    // the model to save (e.g. without the viewer's guide lines); null: `load`'s
};

std::unique_ptr<Screen> make_model_viewer(std::string title, std::vector<std::string> items, ModelLoader load,
                                          ModelExport exports = {});

} // namespace gt3r
