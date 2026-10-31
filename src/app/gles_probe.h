#pragma once

// Which OpenGL ES renderers open on this PC (GT3R_GLES builds), for the choices of Settings →
// Display → 3D renderer. Each kind is tried in a copy of the game started with
// `--probe-3d angle|driver` (render::gles::probe), as a start with it would: one process can set
// up only one kind of OpenGL, and a driver that crashes or hangs there takes only the copy down.

#include <optional>

namespace gt3r {

namespace render {
class GpuContext;
}

enum class GlesKind {
    Angle,  // Windows: ANGLE (OpenGL ES on Direct3D 11), when its DLLs are next to the game
    Driver, // the graphics driver's own
};

// SDL_AppInit: a --probe-3d run (true: that OpenGL ES opened); nullopt: a normal start.
std::optional<bool> run_gles_probe(int argc, char** argv);

// Whether kind opens here: the renderer in use answers for its own kind, a probe for the others.
// The first call starts the probe; nullopt until it answers (about a second).
std::optional<bool> gles_opens(GlesKind kind, const render::GpuContext* gpu);

} // namespace gt3r
