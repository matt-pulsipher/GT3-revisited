#pragma once

// A 3D viewport composited into the 2D UI.
//
//   View3D view(*app.gpu());
//   view.draw(ui.renderer(), {x, y, w, h}, camera, {{&gpu_model}}, options);
//
// The rectangle is in the renderer's logical (UI) coordinates; the
// offscreen target is sized to the matching number of physical pixels, so
// the 3D image is sharp on HiDPI screens. Rendering happens immediately in
// a separate GPU command buffer; the result is drawn as an SDL_Texture, so
// UI drawn afterwards appears on top.

#include "platform/render_scale.h"
#include "render/camera.h"
#include "render/gpu_context.h"
#include "render/gpu_model.h"
#include "render/screen_batch.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace gt3r::render {

enum class WireMode : u8 { Off, Overlay, Only };

struct DrawOptions {
    WireMode wire = WireMode::Off;
    int lod = 0;                       // draw only meshes of this LOD; -1 = all
    std::optional<CullMode> cull;      // override per-primitive culling (debugging decoders)
    float clear[4] = {0.10f, 0.11f, 0.14f, 1.0f};
    // Opacity of the finished picture over what the 2D renderer already holds (with a clear
    // alpha of 0, the background shows where nothing was drawn).
    float composite_alpha = 1.0f;
    Vec3 light_dir{0.45f, 0.8f, 0.4f}; // towards the light, world space
    float ambient = 0.35f;
    float alpha_ref = 0.5f;            // alpha test threshold for kAlphaTest (opaque primitives)
    float blend_alpha_ref = 0.02f;     // alpha test threshold for blended kAlphaTest primitives
    float env_strength = 0.35f;        // kEnvironmentMap placeholder reflection
    float reflection_strength = 0.4f;  // kAdditive | kEnvironmentMap, untextured: car env reflection
                                       // (placeholder env is brighter than the game's dark reflection maps)
    // GT3 car lighting (course Env0 light 0): lit primitives get texel x min(ambient + diffuse x
    // max(N.L, 0), 2) per channel (PS2 0x80 = 1.0, so up to 2x the texel). Off: the generic
    // ambient + (1 - ambient) x diffuse above. Primitives with a valid scene::Material always
    // use these light colours with their material (see scene::Material).
    bool gt3_lighting = false;
    float light_ambient[3] = {0.3f, 0.3f, 0.3f};
    float light_diffuse[3] = {1.0f, 1.0f, 1.0f};
    float light_specular[3] = {0.7f, 0.7f, 0.7f}; // with scene::Material specular
    // Up to three more directional lights for the GT3 lighting (the menu showroom's fills):
    // world directions towards the light, diffuse and specular colours.
    int fill_count = 0;
    float fill_dir[3][3] = {};
    float fill_diffuse[3][3] = {};
    float fill_specular[3][3] = {};
    // Environment seen by the car reflection passes (stand-in for GT3's per-frame sphere map of
    // the course): colour above, at and below the horizon; `reflection_fresnel` = GT3's C1 term,
    // weight 1 - C1 cos(view, normal) (0 = constant weight).
    float env_sky[3] = {0.35f, 0.55f, 0.85f};
    float env_horizon[3] = {0.75f, 0.78f, 0.82f};
    float env_ground[3] = {0.18f, 0.17f, 0.16f};
    float reflection_fresnel = 0;
    // Environment cube for the car reflection passes (View3D::render_env_cube); null = the
    // gradient above. Sampled with the world-space reflection vector.
    SDL_GPUTexture* env_cube = nullptr;
    // Look the cube up the way GT3 reads its view-dependent sphere map: the angle from the
    // toward-viewer direction compressed (theta' = 2 asin(0.9 sin(theta / 2))) and the result
    // tilted 15 degrees up about the camera's x axis.
    bool env_gt3_lookup = false;
    // GT3's panoramic environment lookup (course chunk kind 3): Env0 +0xEC0 / +0xEC4, the
    // lookup's vertical scale and offset (4 / 0.25 on most courses).
    float panorama[2] = {4.0f, 0.25f};
    // A sphere map in eye space for the car reflection passes, used instead of the cube /
    // gradient: GT3's menu showrooms reflect a fixed image (the car_render_* widget's), looked
    // up with the reflection kernel s = 0.5 + 0.5 Rx / m, t = 0.5 - 0.5 Ry / m,
    // m = |R + (0, 0, 1)|, R the reflected view vector in view space.
    SDL_GPUTexture* env_sphere = nullptr;
    // The view whose rotation orients the view-dependent lookups (the sphere maps, the course's
    // environment programs, the specular viewer); unset = the draw's own view. A VR eye sets the
    // race camera's view: GT3's reflections follow the camera, and the head turning within the
    // seat should not turn them.
    std::optional<Mat4> reflection_view;
    // Store every fragment at 5 bits per channel (c8 >> 3 << 3): GT3's reflection map is a
    // PSMCT16 frame buffer. Set when rendering that map into the cube, so lookups filter
    // between quantised texels as the GS does, instead of quantising the filtered colour
    // (which draws hard contour bands that sweep over the car as it moves).
    bool quantize_rgb5 = false;
    // The view or projection mirrors the image (rear-view mirror, cube faces): front faces
    // wind the other way, so back-face culling is swapped.
    bool mirrored = false;
    // GT3's guard band (presentation §M6), > 0: DrawItem::guard_band items' triangles with a vertex
    // beyond |x| = 2 w or |y| = 2 w of clip space, or nearer than this (metres: GT3's near plane,
    // 0.07 x the projection's zoom), are drawn from the pieces GT3's VU clips them into
    // (render/guard_band.h). draw() only.
    float guard_near = 0;
    // GT3's far plane (presentation §G, §M6: metres of eye depth, Env0 +0x20), > 0: what lies
    // beyond it is not drawn. A mesh wholly beyond is culled, the guard band clips its items'
    // triangles there, and the other items' are cut at it like the GPU's own far plane. Background
    // items are not cut; an item's modelview scale moves its plane (the backdrop: 100 x as far).
    float far_clip = 0;
    float wire_color[4] = {0.55f, 0.95f, 0.55f, 1.0f};
    // Large scenes (race): skip meshes whose bounding sphere is outside the
    // view frustum or further than `draw_distance` (0 = unlimited) from the eye.
    bool frustum_cull = false;
    float draw_distance = 0;
    // Draw blended primitives back to front (by mesh bounding-sphere centre)
    // instead of in model order.
    bool sort_blended = false;
    // Linear distance fog; background items are never fogged.
    bool fog = false;
    float fog_color[3] = {0.7f, 0.75f, 0.8f};
    float fog_start = 300, fog_end = 2000, fog_max = 1.0f;
    // GT3 fog curve (course Env0 +0x70 / +0x74): visibility = b + (1 - b) min(1, A / z);
    // fog_gt3 = true uses fog_start = A and fog_max = 1 - b.
    bool fog_gt3 = false;
    // The post-process passes over the finished picture (GT3's full-screen passes before the
    // 2D layer; presentation §G6, §G16), in this order:
    // the grade: levels (black / white stretched to 0 / 1), a curve t^2 + 2 g t (1 - t) per
    // output channel, then the saturation mix out_c = s L_c(in_c) + (1 - s) sum_k w_k L_c(in_k)
    // (w = 0.30 0.59 0.11), in GT3's 8-bit table arithmetic (model.frag gt3_grade);
    struct PostGrade {
        bool on = false;
        float black = 0, white = 1;
        float curve[3] = {0.5f, 0.5f, 0.5f}; // g per channel (0.5 = identity)
        float saturation = 1;
    } post_grade;
    // the dissolve: the picture frozen by `snapshot` (the last one shown before this draw)
    // over it at weight `dissolve`; the trail: the last picture shown at weight `trail`
    // (recursive: it holds its own trail). `keep_last` keeps every picture for them.
    bool snapshot = false;
    float dissolve = 0, trail = 0;
    bool keep_last = false;
    // The replay depth of field (presentation §G17), before the passes above and below: the picture
    // rebuilt from itself and blurred copies of it by the scene's depth (dof_layers), for a lens
    // of `focal_mm` (a film 1.333 times as wide as high whose diagonal is the focal length; its
    // focal length at this view's zoom from the projection's vertical scale), the
    // f-number `f_number`, focused `focus` metres from the eye. Nothing beyond `far` metres
    // (GT3's far plane) gets more blur than at it. `pyramid`: GT3's own downsampled
    // pictures at any size, a fraction of the work on a phone, where the picture is otherwise
    // over 448 lines tall. Not drawn by draw_to.
    struct DepthOfField {
        bool on = false, pyramid = false;
        float f_number = 10, focal_mm = 35, focus = 10, far = 1000;
    } dof;
    // --- The passes between the 3D scene and the grade, and the vignette (GT3's frame order,
    // presentation §L6): the heat haze lenses, the DrawItem::late items (the tyre smoke), the
    // overlay; then the grade above, the vignette, the dissolve and the trail.
    // `haze`: triangles (positions and texture coordinates in its rect's units, e.g. the 640 x 448
    // frame) sampling one copy of the picture as it is after the scene, blended at 50 % (GS ALPHA
    // FIX 64), in front of everything (the original's lens depth is past its depth buffer's range,
    // so nothing hides a lens: presentation §L6).
    const ScreenBatch* haze = nullptr;
    // Screen draws over the picture before the grade (the sun glare, the lamp glows; in the
    // draw rectangle's units, ScreenBatch::rect).
    const ScreenBatch* overlay = nullptr;
    // Drawn over the graded picture, under the dissolve and the trail (the replay vignette).
    const ScreenBatch* vignette = nullptr;
    // Fixed internal resolution (e.g. the PS2's 640x448), upscaled to the rectangle;
    // 0 = the rectangle's own pixel size. `nearest` upscales without filtering.
    int fixed_width = 0, fixed_height = 0;
    bool nearest = false;
    // One of several pictures a frame from the same targets (a menu model at the screen's
    // resolution): not supersampled (it would multiply every pass), and only the finished picture
    // is cycled, which the 2D renderer reads when it submits (after every pass of the frame). The
    // multisampled colour and the depth hold nothing between passes, and each pass would keep
    // its own large copies of them, per frame in flight.
    bool repeated = false;
};

// The grade pass's arithmetic on one 8-bit pixel: model.frag's gt3_grade on the CPU (tests).
std::array<u8, 3> post_grade_pixel(const DrawOptions::PostGrade& g, u8 r, u8 gr, u8 b);

// The GS's 8-bit arithmetic on one textured, fogged pixel: model.frag's gs_texel, gs_color and
// gs_fog on the CPU (tests). `texel` as the texture filter returns it (0..255, fractional between
// texels), `color` the vertex colour in GS units (0x80 = 1.0, fractional where interpolated),
// `fog` FOGCOL, `f` the fog coefficient F (255: no fog; 256 here: a pass without fog).
std::array<u8, 3> gs_pixel(const float texel[3], const float color[3], const u8 fog[3], float f);

// One layer of the depth of field (presentation §G17), in drawing order: level `level` (0..4) of
// the downsampled pictures, or the sharp picture itself (-1), drawn at weight `alpha` wherever the
// scene is `distance` metres or more from the eye (0: everywhere; the first layer covers all).
struct DofLayer {
    int level = -1;
    float alpha = 1;
    float distance = 0;
};
// The layers for a lens on a projection whose vertical scale (row 1, column 1) is `proj_y` and
// whose near plane is `near` metres away.
std::vector<DofLayer> dof_layers(const DrawOptions::DepthOfField& d, float proj_y, float near);
// The downsampled picture `level` (0..4) for a picture `aspect` times as wide as high: GT3's
// 320 x 224, 320 x 112, 160 x 112, 160 x 56 and 160 x 56 of its 640 x 448 frame, as wide as the
// picture is at the frame's line spacing (4:3: the frame itself).
std::array<int, 2> dof_level_size(int level, float aspect);
// How the original's level `level` (0..4) blurs the frame as the layers show it (presentation
// §G17), across and down in frame pixels: the spread (standard deviation) of its 2-texel means
// and of the bilinear filter that enlarges it, and how far its picture moves right and down (the
// kept axes' half-texel steps and the layers' half pixel). Pictures taller than the frame build
// their levels at their own resolution with this blur (View3D::encode_dof), not GT3's
// 160-320 texel pyramid stretched over them.
struct DofBlur {
    float sigma[2] = {0, 0};
    float shift[2] = {0, 0};
};
DofBlur dof_level_blur(int level);

// The road sheen's per-vertex colour (scene::Material::specular_local): model.vert's program on
// the CPU (tests). `light` towards the light (normalised here), `light_spec` its specular colour,
// `viewer` the viewer's position (the camera + 2 m along its up axis), `normal` as stored. Returns
// the GS colour bytes (low 8 bits of s Ls Ms c, s = 128 x scale).
std::array<u8, 3> sheen_vertex_bytes(const scene::Material& m, const Vec3& light, const float light_spec[3],
                                     const Vec3& viewer, const Vec3& pos, const Vec3& normal);

// The GS's bilinear filter on one channel of an 8-bit image (DrawItem::gs_bilinear): model.frag's
// gs_bilinear on the CPU (tests). `u`, `v` in texels (texel centres at + 0.5), clamped addressing.
u8 gs_bilinear_texel(const u8* px, int w, int h, float u, float v);
// A modelled rim's turned copies (DrawItem::spin_turn) share out the picture's samples: a pixel's
// samples hold that many copies, and `cells` (1..16, a power of two) cells of a 4 x 4 ordered
// pattern of pixels the rest, cells x samples copies in all. spin_copy_at is the copy drawing sample
// `sample` of pixel (x, y): model.frag's spin_copy_samples on the CPU (tests).
int spin_copy_at(int x, int y, int sample, int cells);
// How many copies a rim `radius_px` pixels across (its radius) is drawn as whose copies spread
// `spread_px` pixels at its edge, at `samples` a pixel (1 without sample masks): powers of two from
// `samples` up while they would lie more than 1.5 pixels apart, at most 16 cells and as many as
// lie 1.5 pixels apart over a radian of its edge (a small rim's blur hides its spokes sooner; 64
// from 96 pixels).
int spin_copy_count(float spread_px, float radius_px, int samples);
// The pixels the cells' pattern repeats over (1: one cell; 2 x 2 up to 4 cells, else 4 x 4), and so
// the taps of View3D's filter weighing every cell alike: two bilinear taps a pixel apart each way
// (a 3 x 3 tent, 1 2 1) or four (1 2 2 2 1).
int spin_copy_period(int cells);

// Per-draw counters (last View3D::draw call).
struct DrawStats {
    int meshes_drawn = 0, meshes_culled = 0, draw_calls = 0;
    size_t triangles = 0;
};

// A per-item light set for the GT3 lighting (GT3 point-light courses light each car with
// its own up to four nearby lamps, as directional lights): replaces the DrawOptions lights.
struct ItemLights {
    int count = 0;                 // 1..4
    float ambient[3] = {0, 0, 0};  // global ambient + the lights' ambients
    float dir[4][3] = {};          // towards the light (world)
    float diffuse[4][3] = {};
    float specular[4][3] = {};
};

struct DrawItem {
    const GpuModel* model = nullptr;
    Mat4 transform;
    int lod = -2; // -2 = use DrawOptions::lod
    bool helper = false; // grids/gizmos: always drawn solid, never wireframed
    // Sky domes and backdrops: drawn first, without depth writes, culling or
    // fog, so everything else covers them.
    bool background = false;
    float draw_distance = -1; // per-item override of DrawOptions::draw_distance (< 0 = use the option)
    // < 1: the whole item is drawn translucent (alpha-blended after the opaque
    // pass, depth writes kept), e.g. a ghost car.
    float alpha = 1.0f;
    // Multiplies the item's final colour (its lit colour; the car reflection passes take
    // reflection_scale): GT3's car colour scale ((0, s)), e.g. 0 for a black
    // silhouette.
    float tint = 1.0f;
    // Prize scenes (.prm loaded with gtm1::Options::static_env_maps) and course environment
    // passes (chunk kind 2, gtm1::Options::course_env_maps): textured kEnvironmentMap
    // primitives sample their own texture as a sphere map with GT3's reflection kernel
    // (s = 0.5 + 0.5 Rx / m, t = 0.5 - 0.5 Ry / m, m = |R + (0, 0, 1)|, R the reflected view
    // vector in view space), texture x vertex colour x the reflection rows; the primitive's
    // blend mode applies. No CPU apply_sphere_map / re-upload needed.
    bool sphere_map = false;
    // Per-item lighting (GT3 cars: the light colours scaled by the road shade under the car)
    // multipliers for the ambient, diffuse and specular light colours, and a factor on the car
    // reflection passes (GT3: 1 - dirt).
    float light_scale[3] = {1, 1, 1};
    float reflection_scale = 1;
    const ItemLights* lights = nullptr; // own light set (must outlive the draw call)
    // Drawn whole at its place in the item list, as the PS2 sends a model: its opaque meshes,
    // then its blended ones in model order, before the opaque meshes of later items (and never
    // in the DrawOptions::sort_blended pass). Course scenery: the PS2 draws its packets in file
    // order, all before the cars, so e.g. translucent light-shaft sheets come after the scenery
    // behind them, and passes without depth writes (ground overlays) never cover a car
    // (presentation §I7).
    bool in_order = false;
    // With in_order: part of the in-order item just before it in the list, as if one model: its
    // opaque meshes follow that item's opaque ones, its blended ones that item's blended ones
    // (the course's moving nodes, gtm1::AnimatedNodes, with the scenery they were taken from).
    bool in_order_join = false;
    // With in_order: once the item (with the items joined to it) is drawn, the depth buffer is
    // reset to the far plane's, as GT3 resets its Z after the course backdrop (slot 22): the
    // items after it cover it wherever its planes lie (presentation §M2, frame order).
    bool depth_reset = false;
    // The PS2's frame-buffer alpha (course scenery): every pass writes its fragment alpha (texel x
    // vertex alpha) unless flagged kKeepAlpha, kAlphaOnly passes write only it, and
    // kAdditive | kDestAlphaAdd passes (the road sheen) are weighted by it (GS ALPHA 0x58,
    // Cs * Ad + Cd) instead of by their own vertex alpha.
    bool frame_alpha = false;
    // GT3's modelview scale in eye space for this item (the course backdrop, slot 22: 0.01, drawn
    // with Scale(0.01) x view so its depth and fog shrink together). Nothing changes on screen, but
    // the road sheen's viewer comes from a rigid inverse of that modelview and collapses towards
    // the model origin: viewer = s^2 x eye + 2 s x the camera's up axis.
    float modelview_scale = 1.0f;
    // GT3 car lamp factor (0..1): lamp materials add Material::lamp_emission x this.
    float lamp = 0;
    // Samples this texture (bilinear, clamped) instead of the primitives' own, e.g. an
    // offscreen buffer from View3D::render_offscreen.
    SDL_GPUTexture* texture = nullptr;
    // With texture: filtered as the GS does it (bilinear with 4-bit weights, each lerp truncated
    // to a whole 8-bit level), for a buffer that holds whole levels: GT3's wet-road blur copies
    // and composite (race/wet_road.h). wheel_steps[0..1] hold the buffer's size.
    bool gs_bilinear = false;
    // Blended primitives weighted by the frame-buffer alpha instead (PS2 ALPHA): 1 = 0x54,
    // (Cs - Cd) Ad + Cd; 2 = 0x58, Cs Ad + Cd. The frame alpha is kept.
    u8 dest_alpha_blend = 0;
    // Double-sided primitives culled like the rest (back faces): a car body seen from inside
    // (VR's in-car view) shows its bonnet, not the inside of its roof and windows.
    bool single_sided = false;
    // Drawn after every other item, with their depth: GT3's tyre smoke pass comes after the
    // whole scene and the heat haze (DrawOptions::haze), before the glare (presentation §L6).
    bool late = false;
    // GT3's wheel spin blurs (race/wheel_fx.h): the item's textured primitives sample their
    // texture as the mean of copies of it: 1 = shifted along S by every sum of a subset of
    // wheel_steps[0..2] (the tyre's tread), 2 = turned about the image centre by every sum of a
    // subset of wheel_steps' four angles (radians), read from mip level wheel_blur_lod (the rim).
    // wheel_blur_alpha_ref >= 0: the alpha test's threshold for its alpha-tested primitives
    // without one of their own (GT3 tests the blurred image's own alpha).
    u8 wheel_blur = 0;
    float wheel_blur_lod = 0;
    float wheel_blur_alpha_ref = -1;
    std::array<float, 4> wheel_steps{};
    // Not 0: a modelled rim's spin blur (race::CarVisual). The item is drawn as copies of itself
    // turned about its own x axis (the axle) by k / n of this turn (radians), k = 0..n - 1, n from
    // its size on screen (spin_copy_count), each into its own share of the picture's samples
    // (spin_copy_at): each pixel resolves to the mean of the copies it holds, and each copy's own
    // depth keeps what lies behind it sharp. The main view then evens out the cells over the rim
    // (spin_resolve). Without the shader variant (GpuContext::spin_copies) it is drawn once, plain.
    float spin_turn = 0;
    // Not fogged (a GS pass without FGE: the brake rotor's glow).
    bool no_fog = false;
    // GT3's cars pass (DrawOptions::far_clip): the model is dropped whole once any of it reaches
    // past the far plane (its node box test), not cut at the plane.
    bool far_whole = false;
    // Clipped as GT3 does at the guard band (DrawOptions::guard_near; the model keeps its
    // geometry: GpuModel's keep_geometry), the vertex colour linear in screen space as the GS
    // interpolates it. The triangles are taken in strip order: their indices ascending (gtm1).
    bool guard_band = false;
    // A course visibility list (presentation §I7): only the meshes of these GTM1 nodes
    // (MeshInfo::node), in the list's order, as GT3 runs the listed nodes' programs; the others
    // are not drawn. Null = every mesh, in model order. Must outlive the draw call.
    const std::vector<u16>* nodes = nullptr;
    // < 1: only this share of the item's pixels is drawn, by a 4 x 4 ordered dither (opaque
    // passes keep their depth, blended ones their blend): course nodes fading in or out as a
    // view's visibility list changes (race::SceneryFade).
    float fade = 1.0f;
};

class View3D {
public:
    explicit View3D(GpuContext& ctx) : ctx_(ctx) {}
    ~View3D();
    View3D(const View3D&) = delete;
    View3D& operator=(const View3D&) = delete;

    void draw(SDL_Renderer* renderer, const SDL_FRect& rect, const Mat4& view, const Mat4& proj, const Vec3& eye,
              const std::vector<DrawItem>& items, const DrawOptions& options);
    void draw(SDL_Renderer* renderer, const SDL_FRect& rect, const OrbitCamera& camera,
              const std::vector<DrawItem>& items, const DrawOptions& options) {
        draw(renderer, rect, camera.view(), camera.projection(rect.w / std::max(rect.h, 1.0f)), camera.eye(), items,
             options);
    }

    // Renders straight into `target` (`w` x `h`, the context's colour format: a VR eye's canvas),
    // the multisampled picture resolved into it; no composite pass. False on failure.
    bool draw_to(SDL_GPUTexture* target, int w, int h, const Mat4& view, const Mat4& proj, const Vec3& eye,
                 const std::vector<DrawItem>& items, const DrawOptions& options);

    // Renders `items` into a cube map (six 90-degree views from `pos`, `size` pixels per face)
    // for the car reflection passes (DrawOptions::env_cube). `faces` = bit mask of the faces to
    // refresh (+X, -X, +Y, -Y, +Z, -Z). Returns the cube (null on failure).
    SDL_GPUTexture* render_env_cube(const Vec3& pos, const std::vector<DrawItem>& items, const DrawOptions& options,
                                    int size = 128, unsigned faces = 0x3F);
    SDL_GPUTexture* env_cube() const { return cube_; }

    // Renders `items` into offscreen buffer `slot` (0..3; `w` x `h`, cleared to `options.clear`,
    // own depth) and returns it for sampling (DrawItem::texture); null on failure.
    SDL_GPUTexture* render_offscreen(int slot, int w, int h, const Mat4& view, const Mat4& proj, const Vec3& eye,
                                     const std::vector<DrawItem>& items, const DrawOptions& options);

    // Physical size of the last picture (the part of the target drawn), for stats overlays.
    int pixel_width() const { return used_w_; }
    int pixel_height() const { return used_h_; }
    const DrawStats& stats() const { return stats_; }

    // GT3R_CAPTURE3D=<first>,<count>,<path.png>: copies this view's frames <first>.. on the GPU
    // and writes them as <path>-<k>.png after the last one. Unlike --shot (SDL_RenderReadPixels
    // waits for the GPU every frame) the frames stay in flight as in play: for flicker checks.
    void capture_from_env();

private:
    bool frame_alpha_written_ = false; // a DrawItem::frame_alpha pass wrote the target alpha this draw
    void capture_frame(SDL_GPUCommandBuffer* cmd);
    void write_capture();
    bool ensure_target(SDL_Renderer* renderer, int w, int h);
    void release_target();
    bool ensure_half(SDL_Renderer* renderer);
    bool ensure_cube(int size);
    void release_cube();
    SDL_GPUTexture* placeholder_cube();
    // Records the draws of one view into an open render pass: every item, those without
    // DrawItem::late, or only those.
    enum class Items : u8 { All, Early, Late };
    void encode(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const Mat4& view, const Mat4& proj, const Vec3& eye,
                const std::vector<DrawItem>& items, const DrawOptions& options, Items which = Items::All);
    // --- The passes after the scene (presentation §L6): DrawOptions::haze, late items, overlay,
    // vignette. Screen geometry for the frame goes up in one copy pass; each batch's first index.
    struct ScreenUpload {
        u32 haze = 0, overlay = 0, vignette = 0;
        bool ok = false;
    };
    ScreenUpload upload_screen(SDL_GPUCommandBuffer* cmd, const DrawOptions& o, int picture_w, int picture_h);
    // One batch into an open pass, over the picture's corner: `frame` samples haze_src_ (the
    // picture's copy) at 50 % (with haze_low_valid_ its field-sized copy displaced, plus this
    // picture's finer detail at each pixel), else each draw's texture times its vertex colour.
    void encode_screen(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const ScreenBatch& b, u32 first_index,
                       bool frame);
    // The late items and the overlay at the end of a pass (the scene's, or the haze's).
    void encode_after_scene(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const Mat4& view, const Mat4& proj,
                            const Vec3& eye, const std::vector<DrawItem>& items, const DrawOptions& o,
                            const ScreenUpload& up);
    // GT3's guard band (DrawOptions::guard_near): the corners of the clipped triangles of this
    // draw's DrawItem::guard_band items (ClipVertex) and their triangles per (item, mesh,
    // primitive), uploaded before the scene pass; encode draws each after the primitive's own draw.
    // False when nothing could be uploaded (the static draws then keep every triangle).
    bool upload_clipped(SDL_GPUCommandBuffer* cmd, const Mat4& view, const Mat4& proj, const Vec3& eye,
                        const std::vector<DrawItem>& items, const DrawOptions& o);
    struct ClipRange {
        u32 first = 0, count = 0;
    };
    std::map<std::tuple<const DrawItem*, const GpuModel::MeshInfo*, size_t>, ClipRange> clipped_;
    bool guard_on_ = false; // this draw's guard-band items are clipped (encode)
    // A vertex and an index buffer refilled every draw, through one transfer buffer (each grown to
    // twice its size when too small).
    struct Streamed {
        SDL_GPUBuffer *vb = nullptr, *ib = nullptr;
        SDL_GPUTransferBuffer* tb = nullptr;
        u32 vb_size = 0, ib_size = 0, tb_size = 0;
        void release(SDL_GPUDevice* dev);
        // Copies `vsize` bytes of vertices and `isize` of indices into them, in a copy pass on `cmd`.
        bool upload(SDL_GPUDevice* dev, SDL_GPUCommandBuffer* cmd, const void* verts, u32 vsize, const void* idx,
                    u32 isize);
    };
    Streamed clip_; // the clipped corners (ClipVertex) and their triangles
    // The post-process passes (DrawOptions::post_grade, dissolve, trail).
    bool post_wanted(const DrawOptions& o) const;
    bool ensure_post(SDL_GPUTexture** t);
    void encode_post(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, SDL_GPUTexture* picture, int picture_w,
                     int picture_h, float alpha, const DrawOptions::PostGrade* grade);
    // Copies the w x h corner (a picture) of `from` to `to` (cycling `to`: see draw).
    void copy_picture(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* from, SDL_GPUTexture* to, int w, int h, bool cycle);
    bool ensure_quad();
    // The depth of field (DrawOptions::dof): from scene_ (the picture) and depth_ (its depth, kept
    // by the scene pass), the downsampled pictures, then the layers into `out` (the picture's size).
    bool dof_wanted(const DrawOptions& o) const { return o.dof.on && o.dof.focus > 0; }
    // The modelled rims the scene pass drew as turned copies (DrawItem::spin_turn), in normalised
    // device coordinates, with how far the copies spread at the rim's edge (pixels) and their cells'
    // spin_copy_period. After the scene, spin_resolve lays the cell-evening filter over each.
    struct SpinRect {
        const DrawItem* item = nullptr;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        float spread_px = 0;
        int period = 1;
    };
    std::vector<SpinRect> spin_rects_;
    // The main view's scene pass: pixels per unit of x in normalised device coordinates (half the
    // picture's width), for spin_copy_count and spin_rects_. 0 elsewhere: a copy a sample, no rects.
    float spin_px_ = 0;
    void spin_resolve(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* picture, int pw, int ph, bool cycle);
    bool ensure_dof_levels(float aspect, int pw, int ph, bool pyramid);
    void encode_dof(SDL_GPUCommandBuffer* cmd, const DrawOptions& o, const Mat4& proj, float aspect,
                    SDL_GPUTexture* out, bool cycle);
    // One quad over the pass's viewport sampling `src` (model.frag's mode 6, see dof_sample) at
    // `depth`, weight `alpha` (1: replaces); `single`: a single-sample pass without depth.
    struct Resample {
        float scale[2] = {1, 1}, offset[2] = {0, 0}; // texture coordinates = v_uv x scale + offset
        float clamp[4] = {0, 0, 1, 1};               // min u, min v, max u, max v
        float taps[2] = {1, 1}, step[2] = {0, 0};    // taps across / down, their spacing
        float sigma[2] = {0, 0};                     // > 0: the taps weighted by a Gaussian (in taps), else equal
    };
    void encode_resample(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, SDL_GPUTexture* src, const Resample& r,
                         float alpha, float depth, bool single);

    GpuContext& ctx_;
    // The targets are width_ x height_, sized for the largest 3D scale (GpuContext::render_ceiling);
    // each frame draws its used_w_ x used_h_ picture into the top-left corner, so a change of the
    // dynamic scale allocates nothing (a new size used to stall phones for 100-400 ms while SDL
    // re-created every target for each frame in flight).
    int width_ = 0, height_ = 0, samples_ = 0;
    int used_w_ = 0, used_h_ = 0;
    SDL_GPUTexture* color_ = nullptr;   // single-sample, sampled by the 2D renderer
    SDL_GPUTexture* msaa_ = nullptr;    // multisampled colour (when MSAA is on)
    SDL_GPUTexture* depth_ = nullptr;
    SDL_Texture* wrapper_ = nullptr;    // color_ as an SDL_Renderer texture
    // Supersampling past 2x: the picture halved (half_, as half_wrapper_ for the 2D renderer).
    SDL_GPUTexture* half_ = nullptr;
    SDL_Texture* half_wrapper_ = nullptr;
    int half_w_ = 0, half_h_ = 0;
    // Supersampled targets that failed (GPU memory) lower this view's most: 75% of the failed
    // size each time, down to native.
    float scale_cap_ = platform::kMaxRenderScale;
    // The post-process passes: the scene before them, the last picture shown, the frozen one;
    // a quad over the target.
    SDL_GPUTexture *scene_ = nullptr, *last_ = nullptr, *snap_ = nullptr;
    bool last_valid_ = false, snap_valid_ = false;
    int last_w_ = 0, last_h_ = 0, snap_w_ = 0, snap_h_ = 0; // their pictures' sizes
    SDL_GPUBuffer *quad_vb_ = nullptr, *quad_ib_ = nullptr;
    // The screen geometry of the passes after the scene (refilled per draw), and the picture's copy
    // the haze lenses sample.
    Streamed screen_;
    SDL_GPUTexture* haze_src_ = nullptr;
    // The depth of field's downsampled pictures (each `w` x `h` in the corner of a `cap_w` wide
    // texture) and its result when the post-process passes follow.
    struct DofLevel {
        SDL_GPUTexture* tex = nullptr;
        int cap_w = 0, cap_h = 0, w = 0, h = 0;
        int dx = 1, dy = 1; // picture pixels per texel (pictures taller than the frame)
    };
    DofLevel dof_level_[5];
    DofLevel dof_tmp_[2]; // the blur's intermediate pictures (pictures taller than the frame)
    // Pictures taller than the original's 448 lines: the picture at the size of the original's
    // display field (640 x 224, as wide as the picture at its spacing) for the haze lenses, which
    // displace that picture and keep this one's finer detail (encode_screen).
    DofLevel haze_low_;
    bool haze_low_valid_ = false;
    bool ensure_dof_texture(DofLevel& l, int w, int h);
    SDL_GPUTexture* dof_ = nullptr;
    void release_dof_levels();
    DrawStats stats_;
    SDL_GPUTexture* cube_ = nullptr;      // environment cube (6 layers)
    // Multisampled colour, one per face (resolved into cube_'s layer): SDL 3.4's Vulkan backend
    // keys its framebuffer cache by the resolve texture's layer 0 whatever resolve_layer is, so
    // faces sharing one target got another face's framebuffer and resolved into the wrong layer.
    SDL_GPUTexture* cube_msaa_[6] = {};
    SDL_GPUTexture* cube_depth_ = nullptr;
    int cube_size_ = 0, cube_samples_ = 0;
    SDL_GPUTexture* placeholder_cube_ = nullptr; // 1x1 grey, bound when no cube is given
    struct Offscreen {
        SDL_GPUTexture *color = nullptr, *msaa = nullptr, *depth = nullptr;
        int w = 0, h = 0, samples = 0;
    };
    Offscreen offscreen_[4];
    Offscreen direct_; // draw_to's multisampled colour and depth (no colour of its own)
    void release_offscreen();
    struct Capture {
        int first = 0, count = 0, frame = 0, w = 0, h = 0;
        std::string path;
        std::vector<SDL_GPUTexture*> frames;
    } capture_;
};

} // namespace gt3r::render
