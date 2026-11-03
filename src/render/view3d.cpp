#include "render/view3d.h"

#include "base/file.h"
#include "base/log.h"
#include "formats/png_writer.h"
#include "platform/render_scale.h"
#include "render/guard_band.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace gt3r::render {

namespace {

// The depth targets' clear: the far plane (the depth is reversed, see render::perspective).
constexpr float kFarDepth = 0.0f;

// Uniform blocks; layouts match model.vert / model.frag (std140).
struct VertexUniforms {
    float mvp[16];
    float model[16];
    float normal_mat[16];
    float view_z[4]; // the view matrix's z row (eye-space z of a world point)
    float fog[4];    // GT3 fog: x distance A, y 1 - minimum visibility, z = 1 on; w > 0: GT3's
                     // guard band (DrawItem::guard_band), its near plane in the item's eye space
    float far_clip[4]; // x > 0: GT3's far plane in the item's eye space (DrawOptions::far_clip)
    float spin_axis[4];  // DrawItem::spin_turn's copies (model space): the axle, w the turn between copies
    float spin_pivot[4]; // a point on the axle
};
// model.vert's VertexLighting (slot 1): GT3's VU lighting per vertex.
struct VertexLighting {
    float eye_dir[4]; // towards the viewer (world), w = 1: on; w = 2: the viewer's position (local
                      // viewer); w = 3 / 4: environment pass kind 2 / 3, xyz = the camera's position
    float light_dir[4][4];
    float light_diffuse[4][4];
    float light_specular[4][4];
    float light_ambient[4];
    float mat_ambient[4];
    float mat_diffuse[4];
    float mat_specular[4];
    float mat_emission[4];
    // Environment passes: the VU's rows (kind 2: the view rotation, x the modelview scale; kind 3:
    // the panorama's yaw), the weight rows C0 / C1, and kind 3's constants (x = 0.875 - (g + 1) / 8,
    // y = EC0 / 6, z = 0.5 + EC4).
    float env_rows[3][4];
    float env_c0[4];
    float env_c1[4];
    float env_pano[4];
    float vertex_color[4]; // x = 1: the RGB are GS bytes, scaled by 255/128 (scene::kVertexColor2x)
};
struct FragmentUniforms {
    float light_dir[4];
    float camera_pos[4];
    float params[4]; // alpha ref, unlit, env strength, flat colour
    float flat_color[4]; // wireframe colour; mode 6: the taps' Gaussian spreads; mode 5: w = 1 the haze at the field's size
    float fog_color[4];  // rgb, w = enabled
    float fog_params[4]; // start, 1 / (end - start), max amount
    float tint[4];       // final colour multiplier
    float view_row0[4];  // rows 0 and 1 of the view matrix (sphere-mapped primitives)
    float view_row1[4];
    float grade[4];      // colour-grade curve g (rgb), saturation (w; 0 = off)
    float light_ambient[4]; // rgb, w: 1 = GT3 car lighting
    float light_diffuse[4]; // rgb
    float env_sky[4];       // rgb, w: reflection Fresnel C1
    float env_horizon[4];
    float env_ground[4];
    float light_specular[4]; // rgb
    float mat_ambient[4];    // rgb, w: 1 = material valid
    float mat_diffuse[4];    // rgb, w: scale
    float mat_specular[4];   // rgb, w: exponent (0 = none)
    float mat_emission[4];
    float light2_dir[4]; // w: 1 = enabled
    float light2_diffuse[4];
    float light2_specular[4];
    float light3_dir[4];
    float light3_diffuse[4];
    float light3_specular[4];
    float light4_dir[4];
    float light4_diffuse[4];
    float light4_specular[4];
    float grade2[4]; // x black, y white, z 1 = saturation before the curve, w 1 = alpha = coverage
    float env_c0[4]; // reflection weight rows (scene::Primitive::env_c0/c1); c0.w = 1: valid
    float env_c1[4];
    float output[4]; // x = 1: quantise the colour to 5 bits per channel (DrawOptions::quantize_rgb5);
                     // y = 1: the vertex colour is linear in screen space (DrawItem::guard_band);
                     // z = 1: the bound texture filtered as the GS does (DrawItem::gs_bilinear);
                     // w = 1: a car reflection drawn only below full reflectivity (kDestAlphaTest)
    float wheel_blur[4];  // DrawItem::wheel_blur: x mode, y the rim's mip level; w spin copies' cells
    float wheel_steps[4]; // DrawItem::wheel_steps
};

// Frustum planes of `vp` (clip depth 0..w, far..near): w+x, w-x, w+y, w-y, z, w-z, normalised.
void frustum_planes(const Mat4& vp, float planes[6][4]) {
    for (int i = 0; i < 4; ++i) {
        float r0 = vp.at(0, i), r1 = vp.at(1, i), r2 = vp.at(2, i), r3 = vp.at(3, i);
        planes[0][i] = r3 + r0, planes[1][i] = r3 - r0, planes[2][i] = r3 + r1;
        planes[3][i] = r3 - r1, planes[4][i] = r2, planes[5][i] = r3 - r2;
    }
    for (int k = 0; k < 6; ++k) {
        float* p = planes[k];
        float l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (l > 0)
            for (int i = 0; i < 4; ++i) p[i] /= l;
    }
}

// A mesh where an item puts it: its world transform, bounding sphere and distance from the eye.
struct Placed {
    Mat4 world;
    Vec3 center;
    float radius = 0, dist = 0;
    float scale = 1; // the world transform's largest axis scale
};
Placed place(const DrawItem& item, const GpuModel::MeshInfo& mesh, const Vec3& eye) {
    Placed m;
    m.world = item.transform * mesh.transform;
    m.center = m.world.transform_point(mesh.center);
    float sc = 0;
    for (int col = 0; col < 3; ++col)
        sc = std::max(sc, length(Vec3{m.world.at(0, col), m.world.at(1, col), m.world.at(2, col)}));
    m.radius = mesh.radius * sc;
    m.scale = sc;
    m.dist = length(m.center - eye);
    return m;
}

int item_lod(const DrawItem& item, const DrawOptions& opt) { return item.lod == -2 ? opt.lod : item.lod; }

// A point's depth in front of the eye (the view's -z).
float eye_depth(const Mat4& view, const Vec3& p) {
    return -(view.at(2, 0) * p.x + view.at(2, 1) * p.y + view.at(2, 2) * p.z + view.at(2, 3));
}

// Whether an item resets the depth after it (DrawItem::depth_reset: the pass needs the quad).
bool has_depth_reset(const std::vector<DrawItem>& items) {
    return std::any_of(items.begin(), items.end(), [](const DrawItem& i) { return i.depth_reset; });
}

// GT3's far plane as the item's depth from the eye (DrawOptions::far_clip; the backdrop's 0.01
// modelview scale puts it 100 times as far), 0 for none.
float item_far(const DrawItem& item, const DrawOptions& opt) {
    if (opt.far_clip <= 0 || item.background || item.helper) return 0;
    return opt.far_clip / std::max(item.modelview_scale, 1e-6f);
}

// The draw distance and frustum culling of encode, and GT3's far plane: its VU culls what lies
// wholly beyond.
bool culled(const DrawItem& item, const Placed& m, const DrawOptions& opt, const float planes[6][4], const Mat4& view) {
    if (item.background || item.helper) return false;
    const float max_dist = item.draw_distance >= 0 ? item.draw_distance : opt.draw_distance;
    if (max_dist > 0 && m.dist - m.radius > max_dist) return true;
    const float far = item_far(item, opt);
    if (far > 0 && eye_depth(view, m.center) - m.radius > far) return true;
    if (opt.frustum_cull)
        for (int k = 0; k < 6; ++k) {
            const float* p = planes[k];
            if (p[0] * m.center.x + p[1] * m.center.y + p[2] * m.center.z + p[3] < -m.radius) return true;
        }
    return false;
}

// DrawItem::far_whole: whether any of the item's meshes reaches past GT3's far plane.
bool reaches_past_far(const DrawItem& item, const DrawOptions& opt, const Mat4& view, const Vec3& eye) {
    const float far = item_far(item, opt);
    if (!item.far_whole || far <= 0) return false;
    const int lod = item_lod(item, opt);
    for (const GpuModel::MeshInfo& mesh : item.model->meshes()) {
        if (lod >= 0 && mesh.lod != lod) continue;
        const Placed m = place(item, mesh, eye);
        if (eye_depth(view, m.center) + m.radius > far) return true;
    }
    return false;
}

// Whether a sphere is past the near plane, before the far plane (far_w > 0) and inside the guard
// band (|x|, |y| <= 2 w) of `vp`: none of its triangles is clipped.
bool inside_guard_band(const Mat4& vp, const Vec3& c, float r, float near_w, float far_w) {
    // The planes w - near, 2 w - x, 2 w + x, 2 w - y, 2 w + y, far - w (positive inside).
    const float kw[6] = {1, 2, 2, 2, 2, -1}, kx[6] = {0, -1, 1, 0, 0, 0}, ky[6] = {0, 0, 0, -1, 1, 0};
    for (int k = 0; k < (far_w > 0 ? 6 : 5); ++k) {
        float p[4];
        for (int i = 0; i < 4; ++i) p[i] = kw[k] * vp.at(3, i) + kx[k] * vp.at(0, i) + ky[k] * vp.at(1, i);
        if (k == 0) p[3] -= near_w;
        if (k == 5) p[3] += far_w;
        const float l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (p[0] * c.x + p[1] * c.y + p[2] * c.z + p[3] < r * l) return false;
    }
    return true;
}

} // namespace

void View3D::Streamed::release(SDL_GPUDevice* dev) {
    if (vb) SDL_ReleaseGPUBuffer(dev, vb);
    if (ib) SDL_ReleaseGPUBuffer(dev, ib);
    if (tb) SDL_ReleaseGPUTransferBuffer(dev, tb);
}

bool View3D::Streamed::upload(SDL_GPUDevice* dev, SDL_GPUCommandBuffer* cmd, const void* verts, u32 vsize,
                              const void* idx, u32 isize) {
    auto grow = [&](SDL_GPUBuffer*& buf, u32& cap, u32 size, SDL_GPUBufferUsageFlags usage) {
        if (buf && cap >= size) return true;
        if (buf) SDL_ReleaseGPUBuffer(dev, buf);
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = usage;
        bi.size = std::max(size, cap * 2);
        buf = SDL_CreateGPUBuffer(dev, &bi);
        cap = buf ? bi.size : 0;
        return buf != nullptr;
    };
    const u32 ioff = (vsize + 15u) & ~15u, total = ioff + isize;
    if (!grow(vb, vb_size, vsize, SDL_GPU_BUFFERUSAGE_VERTEX) || !grow(ib, ib_size, isize, SDL_GPU_BUFFERUSAGE_INDEX))
        return false;
    if (!tb || tb_size < total) {
        if (tb) SDL_ReleaseGPUTransferBuffer(dev, tb);
        SDL_GPUTransferBufferCreateInfo ti{};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = std::max(total, tb_size * 2);
        tb = SDL_CreateGPUTransferBuffer(dev, &ti);
        tb_size = tb ? ti.size : 0;
        if (!tb) return false;
    }
    auto* p = static_cast<u8*>(SDL_MapGPUTransferBuffer(dev, tb, true));
    if (!p) return false;
    std::memcpy(p, verts, vsize);
    std::memcpy(p + ioff, idx, isize);
    SDL_UnmapGPUTransferBuffer(dev, tb);
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{tb, 0};
    SDL_GPUBufferRegion dst{vb, 0, vsize};
    SDL_UploadToGPUBuffer(cp, &src, &dst, true); // (the last frame's draws may still read them)
    src.offset = ioff;
    dst = {ib, 0, isize};
    SDL_UploadToGPUBuffer(cp, &src, &dst, true);
    SDL_EndGPUCopyPass(cp);
    return true;
}

std::array<u8, 3> post_grade_pixel(const DrawOptions::PostGrade& g, u8 r, u8 gr, u8 b) {
    const float d = g.white - g.black, inv = d == 0 ? 0.0f : 1.0f / d;
    auto table = [&](float i, int c) {
        float t = (i / 255.0f - g.black) * inv;
        if (i > 0.5f) t = std::clamp(t, 0.0f, 1.0f); // entry 0 unclamped
        const float k = std::clamp(g.curve[c], 0.0f, 1.0f);
        return std::floor(std::clamp(255.0f * (t * t + 2.0f * k * t * (1.0f - t)), 0.0f, 255.0f));
    };
    const float in[3] = {float(r), float(gr), float(b)}, w[3] = {0.30f, 0.59f, 0.11f};
    const float sp = std::min(128.0f * g.saturation, 255.0f), m = std::max(128.0f - sp, 0.0f);
    std::array<u8, 3> out{};
    for (int c = 0; c < 3; ++c) {
        float sum = 0;
        for (int k = 0; k < 3; ++k) {
            const float mk = c == k ? std::floor(std::clamp(sp + m * w[k], 0.0f, 255.0f)) : std::floor(m * w[k]);
            sum += std::floor(table(in[k], c) * mk / 128.0f);
        }
        out[size_t(c)] = u8(std::min(sum, 255.0f));
    }
    return out;
}

std::array<u8, 3> gs_pixel(const float texel[3], const float color[3], const u8 fog[3], float f) {
    std::array<u8, 3> out{};
    for (int c = 0; c < 3; ++c) {
        const float t = std::floor(texel[c] + 0.05f); // the filtered texel
        const float m = std::min(std::floor(t * color[c] / 128.0f + 1e-3f), 255.0f); // MODULATE
        const float fc = fog[c];
        out[size_t(c)] = u8(std::floor(fc + (m - fc) * (f / 256.0f) + 1e-3f));
    }
    return out;
}

u8 gs_bilinear_texel(const u8* px, int w, int h, float u, float v) {
    const float tu = u - 0.5f, tv = v - 0.5f, iu = std::floor(tu), iv = std::floor(tv);
    const int fu = std::min(int(std::floor((tu - iu) * 16.0f + 1e-3f)), 15);
    const int fv = std::min(int(std::floor((tv - iv) * 16.0f + 1e-3f)), 15);
    auto at = [&](int x, int y) { return int(px[size_t(std::clamp(y, 0, h - 1)) * size_t(w) + size_t(std::clamp(x, 0, w - 1))]); };
    auto lerp = [](int a, int b, int f) { return a + (((b - a) * f) >> 4); }; // (an arithmetic shift: floor)
    const int x = int(iu), y = int(iv);
    return u8(lerp(lerp(at(x, y), at(x + 1, y), fu), lerp(at(x, y + 1), at(x + 1, y + 1), fu), fv));
}

int spin_copy_at(int x, int y, int sample, int cells) {
    static constexpr int kCells[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return kCells[(y & 3) * 4 + (x & 3)] * cells / 16 + cells * sample;
}

int spin_copy_count(float spread_px, float radius_px, int samples) {
    int n = std::max(samples, 1);
    const float most = std::min(64.0f, radius_px / 1.5f); // (copies 1.5 pixels apart over a radian)
    while (2 * n <= most && n < 16 * samples && spread_px > 1.5f * float(n)) n *= 2;
    return n;
}

int spin_copy_period(int cells) { return cells <= 1 ? 1 : cells <= 4 ? 2 : 4; }

std::array<u8, 3> sheen_vertex_bytes(const scene::Material& m, const Vec3& light, const float light_spec[3],
                                     const Vec3& viewer, const Vec3& pos, const Vec3& normal) {
    const Vec3 d = pos - viewer;
    const Vec3 r = d - normal * (2.0f * dot(d, normal));
    const float len = std::max(length(d), 1e-6f);
    const float c = std::max(dot(normalize(light), r) / len, 0.0f);
    // The program (exponent) comes from the shininess alone; Ms = 0 just adds nothing.
    const float p = m.shininess < 6 ? 4.0f : m.shininess < 12 ? 8.0f : 16.0f;
    std::array<u8, 3> out{};
    for (int i = 0; i < 3; ++i) {
        float b = std::floor(light_spec[i] * m.specular[i] * 128.0f * m.scale * std::pow(c, p));
        out[size_t(i)] = u8(int(std::fmod(b, 256.0f)));
    }
    return out;
}

// presentation §G17. GT3's lens (its race +0x97CC object): a film 1.333 times as wide
// as high whose diagonal is the focal length, 0.001 m per mm; the focal length f at
// the view's zoom from the projection's vertical scale; the focus s; one line of its 448-line frame
// on the film as the unit of blur. The near side: blurs 2^k lines, k from trunc(log2(4 + 1)) (the
// largest, 4) down to 0, each at d = f^2 s / (f^2 + N r c (s - f)) and drawn from the last one's
// distance on (the first from the eye); a distance before the near plane is skipped. Then the
// sharp picture from the last. The far side: 2^(k/2) lines, k = 0, 1, ... up to 8 lines, level
// k / 2, at weight 1/2 for even k (a mix of two levels) and 1 for odd k, each from
// d = f^2 s / (f^2 - N r c (s - f)) on, until a distance passes the far plane or turns negative
// (past the hyperfocal distance: everything beyond is in focus). With ln 2.
std::vector<DofLayer> dof_layers(const DrawOptions::DepthOfField& d, float proj_y, float near) {
    constexpr float kLn2 = 0.6931472f;
    constexpr int kNearSteps = 1, kFarSteps = 2;     // steps per doubling (+0x28, +0x24)
    constexpr float kNearMax = 4.0f, kFarMax = 8.0f; // the largest blurs (+0x20, +0x1C)
    // The program's expf / logf (exactly enough in double, rounded to float).
    auto exp_f = [](float x) { return float(std::exp(double(x))); };
    auto log_f = [](float x) { return float(std::log(double(x))); };
    const float film = 0.001f * d.focal_mm / std::sqrt(1.0f + 1.333f * 1.333f);
    const float f = film * 0.5f * proj_y;
    const float c = film / 448.0f;
    const float s = d.focus, f2 = f * f, sf = s - f;
    std::vector<DofLayer> out;
    float from = 0;
    for (int k = kNearSteps * int(log_f(kNearMax + 1.0f) / kLn2); k >= 0; --k) {
        const float r = exp_f(float(k) * kLn2 / float(kNearSteps));
        const float dist = f2 * s / (f2 + sf * r * c * d.f_number);
        if (dist < near) continue;
        out.push_back({k / kNearSteps, float(kNearSteps - k % kNearSteps) / float(kNearSteps), from});
        from = dist;
    }
    out.push_back({-1, 1.0f, from});
    for (int k = 0;; ++k) {
        const float r = exp_f(float(k) * kLn2 / float(kFarSteps));
        if (kFarMax < r) break;
        const float dist = f2 * s / (f2 - sf * r * c * d.f_number);
        if (d.far < dist || dist < 0) break;
        const int m = (k + 1) % kFarSteps;
        out.push_back({k / kFarSteps, m ? float(m) / float(kFarSteps) : 1.0f, dist});
    }
    return out;
}

// GT3's pyramid (table, in one 640 x 224 buffer) at its sizes; wider pictures widen it.
std::array<int, 2> dof_level_size(int level, float aspect) {
    static constexpr int kSize[5][2] = {{320, 224}, {320, 112}, {160, 112}, {160, 56}, {160, 56}};
    const int k = std::clamp(level, 0, 4);
    const float wide = std::max(aspect, 0.1f) * 0.75f; // over 4:3
    return {std::max(1, int(std::lround(float(kSize[k][0]) * wide))), kSize[k][1]};
}

// Per axis, from the frame's pixels (spacing 1): each level is the mean of two texels of the one
// before, `s` apart (a halved axis: the pair under the texel; a kept one: a texel and the next,
// which moves the picture s / 2), adding s^2 / 4 to the variance; the layers then enlarge it
// bilinearly (a tent as wide as its texels: + s^2 / 6) half a frame pixel up and left.
DofBlur dof_level_blur(int level) {
    static constexpr int kSize[5][2] = {{320, 224}, {320, 112}, {160, 112}, {160, 56}, {160, 56}};
    constexpr float kFrame[2] = {640, 448};
    DofBlur b;
    const int k = std::clamp(level, 0, 4);
    for (int a = 0; a < 2; ++a) {
        float var = 0, shift = 0, prev = kFrame[a];
        for (int j = 0; j <= k; ++j) {
            const float s = kFrame[a] / prev; // the spacing of the level before (frame pixels)
            var += s * s / 4.0f;
            if (float(kSize[j][a]) == prev) shift += s / 2.0f;
            prev = float(kSize[j][a]);
        }
        const float spacing = kFrame[a] / prev;
        b.sigma[a] = std::sqrt(var + spacing * spacing / 6.0f);
        b.shift[a] = shift + 0.5f;
    }
    return b;
}

View3D::~View3D() {
    release_dof_levels();
    release_target();
    if (quad_vb_) SDL_ReleaseGPUBuffer(ctx_.device(), quad_vb_);
    if (quad_ib_) SDL_ReleaseGPUBuffer(ctx_.device(), quad_ib_);
    screen_.release(ctx_.device());
    clip_.release(ctx_.device());
    release_cube();
    release_offscreen();
    if (placeholder_cube_) SDL_ReleaseGPUTexture(ctx_.device(), placeholder_cube_);
}

void View3D::release_target() {
    for (SDL_Texture** w : {&wrapper_, &half_wrapper_}) {
        if (*w) SDL_DestroyTexture(*w);
        *w = nullptr;
    }
    SDL_GPUDevice* dev = ctx_.device();
    for (SDL_GPUTexture** t : {&color_, &msaa_, &depth_, &scene_, &last_, &snap_, &half_, &haze_src_, &dof_}) {
        if (*t) SDL_ReleaseGPUTexture(dev, *t);
        *t = nullptr;
    }
    last_valid_ = snap_valid_ = false;
    width_ = height_ = 0;
}

bool View3D::ensure_target(SDL_Renderer* renderer, int w, int h) {
    if (w == width_ && h == height_ && samples_ == ctx_.msaa() && wrapper_) return true;
    release_target();
    SDL_GPUDevice* dev = ctx_.device();
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.width = u32(w);
    ti.height = u32(h);
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;

    ti.format = GpuContext::kColorFormat;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    color_ = SDL_CreateGPUTexture(dev, &ti);
    if (ctx_.sample_count() != SDL_GPU_SAMPLECOUNT_1) {
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        ti.sample_count = ctx_.sample_count();
        msaa_ = SDL_CreateGPUTexture(dev, &ti);
    }
    ti.format = ctx_.depth_format();
    ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    ti.sample_count = ctx_.sample_count();
    depth_ = SDL_CreateGPUTexture(dev, &ti);
    if (!color_ || !depth_ || (ctx_.sample_count() != SDL_GPU_SAMPLECOUNT_1 && !msaa_)) {
        LOG_WARN("3D target %dx%d creation failed: %s", w, h, SDL_GetError());
        release_target();
        return false;
    }
    wrapper_ = wrap_texture(renderer, color_, w, h); // render_scale < 1 upscales, > 1 scales down
    if (!wrapper_) {
        release_target();
        return false;
    }

    width_ = w;
    height_ = h;
    samples_ = ctx_.msaa();
    return true;
}

// The supersampled picture at half size (created on first use), for SDL_BlitGPUTexture to halve
// into and the 2D renderer to composite.
bool View3D::ensure_half(SDL_Renderer* renderer) {
    if (half_wrapper_) return true;
    if (width_ <= 0 || height_ <= 0) return false;
    const int w = (width_ + 1) / 2, h = (height_ + 1) / 2;
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.width = u32(w), ti.height = u32(h);
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    ti.format = GpuContext::kColorFormat;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    half_ = SDL_CreateGPUTexture(ctx_.device(), &ti);
    if (!half_) {
        LOG_WARN("3D half-size target %dx%d: %s", w, h, SDL_GetError());
        return false;
    }
    half_wrapper_ = wrap_texture(renderer, half_, w, h);
    if (!half_wrapper_) {
        SDL_ReleaseGPUTexture(ctx_.device(), half_);
        half_ = nullptr;
        return false;
    }
    half_w_ = w, half_h_ = h;
    return true;
}

void View3D::draw(SDL_Renderer* renderer, const SDL_FRect& rect, const Mat4& view, const Mat4& proj, const Vec3& eye,
                  const std::vector<DrawItem>& items, const DrawOptions& opt) {
    // Logical (UI) rectangle -> physical pixels.
    float sx = 1, sy = 1;
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode;
    SDL_FRect lr;
    if (SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode) && lw > 0 && lh > 0 &&
        SDL_GetRenderLogicalPresentationRect(renderer, &lr)) {
        sx = lr.w / float(lw);
        sy = lr.h / float(lh);
    }
    // The picture: render_scale < 1 renders smaller and the 2D renderer upscales it; above 1
    // (supersampling) larger, within platform::supersample_limit, and it is scaled down. The
    // targets are sized for the largest scale (render_ceiling), so the dynamic scale only changes
    // how much of them a frame uses.
    const float out_w = rect.w * sx, out_h = rect.h * sy; // on screen
    const float limit = std::min(scale_cap_, platform::supersample_limit(out_w, out_h));
    const float scale = std::min(ctx_.render_scale(), limit);
    const float alloc = std::clamp(std::max(scale, ctx_.render_ceiling()), 0.5f, limit);
    auto px = [](float v) { return std::max(1, int(std::lround(v))); };
    int pw = px(out_w * scale), ph = px(out_h * scale);
    int aw = px(out_w * alloc), ah = px(out_h * alloc);
    if (opt.fixed_width > 0 && opt.fixed_height > 0) aw = pw = opt.fixed_width, ah = ph = opt.fixed_height;
    if (!ensure_target(renderer, std::max(aw, pw), std::max(ah, ph))) {
        // Supersampled targets the GPU could not create (memory, size): smaller from the next frame.
        if (alloc > 1.0f && opt.fixed_width <= 0) {
            scale_cap_ = std::max(1.0f, alloc * 0.75f);
            LOG_WARN("3D view: supersampling at most %d%% from now on", int(std::lround(scale_cap_ * 100)));
        }
        return;
    }
    used_w_ = pw, used_h_ = ph;
    // Supersampled targets are not cycled: SDL keeps a cycled target once for every frame in flight
    // (about three copies), and 400% of 3840x2160 with 4x MSAA then needed over 20 GB, spilled out
    // of a 10 GB RTX 3080's memory into system memory and drew a frame a second. Uncycled, SDL
    // orders the frames' uses of each target with barriers (the GPU is the limit at that size).
    const bool cycle = !(alloc > 1.0f && opt.fixed_width <= 0);
    SDL_SetTextureScaleMode(wrapper_, opt.nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    const SDL_GPUViewport used_viewport{0, 0, float(pw), float(ph), 0, 1};
    const SDL_Rect used_rect{0, 0, pw, ph};

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx_.device());
    if (!cmd) return;

    // The dissolve's frozen picture: the last one shown.
    if (opt.snapshot) {
        snap_valid_ = last_valid_ && ensure_post(&snap_);
        if (snap_valid_) {
            snap_w_ = last_w_, snap_h_ = last_h_;
            copy_picture(cmd, last_, snap_, last_w_, last_h_, cycle);
        }
    }
    // The screen draws of the passes after the scene (presentation §L6), uploaded first.
    const ScreenUpload screen = upload_screen(cmd, opt, pw, ph);
    // So are the pieces of the triangles GT3 clips at the guard band.
    guard_on_ = opt.guard_near > 0 && upload_clipped(cmd, view, proj, eye, items, opt);
    if (has_depth_reset(items)) ensure_quad();
    const bool vignette = screen.ok && opt.vignette && !opt.vignette->empty();
    const bool post = (post_wanted(opt) || vignette) && ensure_post(&scene_);
    // The depth of field reads the picture and its depth, then writes the post passes' picture
    // (dof_) or, without them, the finished one.
    const float aspect = out_w / std::max(out_h, 1.0f);
    const bool dof = dof_wanted(opt) && ensure_post(&scene_) && ensure_dof_levels(aspect, pw, ph) && ensure_quad() &&
                     (!post || ensure_post(&dof_));
    SDL_GPUTexture* scene_target = post || dof ? scene_ : color_;
    // The picture after the scene and the depth of field: the passes after them draw into it, the
    // post-process passes start from it.
    SDL_GPUTexture* staged = !dof ? scene_target : post ? dof_ : color_;
    // The heat haze samples the picture as the scene (or the depth of field) left it: that pass
    // keeps its picture and depth, a copy is taken, and a second pass draws the lenses, the late
    // items and the overlay. Depth-of-field frames get the second pass too (the smoke and the glare
    // come after its composite); other frames end the scene pass with them.
    const bool lenses = screen.ok && opt.haze && !opt.haze->empty() && ensure_post(&haze_src_);
    // Above the original's 448 lines the lenses displace the picture at the size of the original's
    // display field and keep this picture's finer detail (presentation §L6): its seams then are no
    // sharper than the original's were, so the lenses show as there, plainly in telephoto shots
    // and hardly in wide ones.
    haze_low_valid_ = lenses && ph > 448 && ensure_quad() &&
                      ensure_dof_texture(haze_low_, std::max(1, int(std::lround(480.0f * aspect))), 224);
    const bool after_pass = lenses || dof;

    SDL_GPUColorTargetInfo ct{};
    ct.clear_color = {opt.clear[0], opt.clear[1], opt.clear[2], opt.clear[3]};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.cycle = cycle;
    if (msaa_) {
        ct.texture = msaa_;
        // (The second pass goes on in it, unless the depth of field composites into it afresh.)
        ct.store_op = lenses && !dof ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
        ct.resolve_texture = scene_target;
        ct.cycle_resolve_texture = cycle;
    } else {
        ct.texture = scene_target;
        ct.store_op = SDL_GPU_STOREOP_STORE;
    }
    SDL_GPUDepthStencilTargetInfo dt{};
    dt.texture = depth_;
    dt.clear_depth = kFarDepth;
    dt.load_op = SDL_GPU_LOADOP_CLEAR;
    dt.store_op = after_pass ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.cycle = cycle;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    SDL_SetGPUViewport(pass, &used_viewport);
    SDL_SetGPUScissor(pass, &used_rect);
    frame_alpha_written_ = false;
    spin_rects_.clear();
    spin_px_ = 0.5f * float(pw);
    encode(cmd, pass, view, proj, eye, items, opt, Items::Early);
    spin_px_ = 0;
    if (!after_pass) encode_after_scene(cmd, pass, view, proj, eye, items, opt, screen);
    SDL_EndGPURenderPass(pass);
    if (dof) encode_dof(cmd, opt, proj, aspect, staged, cycle);
    if (after_pass) {
        if (lenses) copy_picture(cmd, staged, haze_src_, pw, ph, cycle);
        if (haze_low_valid_) { // the copy at the field's size: the mean of the texels under each texel
            SDL_GPUColorTargetInfo lt{};
            lt.texture = haze_low_.tex;
            lt.load_op = SDL_GPU_LOADOP_DONT_CARE;
            lt.store_op = SDL_GPU_STOREOP_STORE;
            lt.cycle = cycle;
            SDL_GPURenderPass* lp = SDL_BeginGPURenderPass(cmd, &lt, 1, nullptr);
            const SDL_GPUViewport lvp{0, 0, float(haze_low_.w), float(haze_low_.h), 0, 1};
            const SDL_Rect lsc{0, 0, haze_low_.w, haze_low_.h};
            SDL_SetGPUViewport(lp, &lvp);
            SDL_SetGPUScissor(lp, &lsc);
            const float W = float(width_), H = float(height_);
            const float fx = float(pw) / float(haze_low_.w), fy = float(ph) / float(haze_low_.h);
            Resample r;
            r.scale[0] = float(pw) / W, r.scale[1] = float(ph) / H;
            r.clamp[0] = 0.5f / W, r.clamp[1] = 0.5f / H;
            r.clamp[2] = (float(pw) - 0.5f) / W, r.clamp[3] = (float(ph) - 0.5f) / H;
            r.taps[0] = std::clamp(std::ceil(fx * 0.5f), 1.0f, 8.0f), r.taps[1] = std::clamp(std::ceil(fy * 0.5f), 1.0f, 8.0f);
            r.step[0] = fx / r.taps[0] / W, r.step[1] = fy / r.taps[1] / H;
            encode_resample(cmd, lp, haze_src_, r, 1.0f, 0.0f, true);
            SDL_EndGPURenderPass(lp);
        }
        SDL_GPUColorTargetInfo ht{};
        ht.load_op = SDL_GPU_LOADOP_LOAD;
        if (msaa_) {
            ht.texture = msaa_;
            ht.store_op = SDL_GPU_STOREOP_RESOLVE;
            ht.resolve_texture = staged;
        } else {
            ht.texture = staged;
            ht.store_op = SDL_GPU_STOREOP_STORE;
        }
        SDL_GPUDepthStencilTargetInfo hd = dt;
        hd.load_op = SDL_GPU_LOADOP_LOAD;
        hd.store_op = SDL_GPU_STOREOP_DONT_CARE;
        hd.cycle = false;
        pass = SDL_BeginGPURenderPass(cmd, &ht, 1, &hd);
        if (lenses) encode_screen(cmd, pass, *opt.haze, screen.haze, true);
        encode_after_scene(cmd, pass, view, proj, eye, items, opt, screen);
        SDL_EndGPURenderPass(pass);
    }
    guard_on_ = false; // (the other entry points draw every triangle themselves)
    if (!spin_rects_.empty()) spin_resolve(cmd, staged, pw, ph, cycle);
    if (post) {
        // The finished picture (graded), then the frozen one and the last one over it.
        SDL_GPUColorTargetInfo pt{};
        pt.load_op = SDL_GPU_LOADOP_DONT_CARE;
        if (msaa_) {
            pt.texture = msaa_;
            pt.store_op = SDL_GPU_STOREOP_RESOLVE;
            pt.resolve_texture = color_;
            pt.cycle_resolve_texture = cycle;
        } else {
            pt.texture = color_;
            pt.store_op = SDL_GPU_STOREOP_STORE;
        }
        pt.cycle = cycle;
        SDL_GPUDepthStencilTargetInfo pd = dt;
        pd.store_op = SDL_GPU_STOREOP_DONT_CARE;
        pass = SDL_BeginGPURenderPass(cmd, &pt, 1, &pd);
        encode_post(cmd, pass, staged, pw, ph, 1.0f, opt.post_grade.on ? &opt.post_grade : nullptr);
        if (vignette) encode_screen(cmd, pass, *opt.vignette, screen.vignette, false);
        if (opt.dissolve > 0 && snap_valid_)
            encode_post(cmd, pass, snap_, snap_w_, snap_h_, std::min(opt.dissolve, 1.0f), nullptr);
        if (opt.trail > 0 && last_valid_)
            encode_post(cmd, pass, last_, last_w_, last_h_, std::min(opt.trail, 1.0f), nullptr);
        SDL_EndGPURenderPass(pass);
    }
    if (opt.keep_last && ensure_post(&last_)) {
        copy_picture(cmd, color_, last_, pw, ph, cycle);
        last_valid_ = true;
        last_w_ = pw, last_h_ = ph;
    } else if (!opt.keep_last) {
        last_valid_ = false;
    }
    if (capture_.count > 0) capture_frame(cmd);
    // Supersampled past twice the on-screen size: halved first by a linear blit (each pixel the
    // mean of 2 x 2), so the 2D renderer's linear scaling, which reads 2 x 2 texels per pixel,
    // still takes in every one (4x: a 4 x 4 box in all).
    SDL_Texture* shown = wrapper_;
    int sw = pw, sh = ph, tw = width_, th = height_;
    if (opt.fixed_width <= 0 && (float(pw) > 2.0f * out_w + 0.5f || float(ph) > 2.0f * out_h + 0.5f) &&
        ensure_half(renderer)) {
        sw = (pw + 1) / 2, sh = (ph + 1) / 2;
        SDL_GPUBlitInfo bi{};
        bi.source.texture = color_, bi.source.w = u32(pw), bi.source.h = u32(ph);
        bi.destination.texture = half_, bi.destination.w = u32(sw), bi.destination.h = u32(sh);
        bi.load_op = SDL_GPU_LOADOP_DONT_CARE;
        bi.filter = SDL_GPU_FILTER_LINEAR;
        bi.cycle = cycle;
        SDL_BlitGPUTexture(cmd, &bi);
        shown = half_wrapper_, tw = half_w_, th = half_h_;
    }
    SDL_SubmitGPUCommandBuffer(cmd);
    if (capture_.count > 0 && capture_.frame == capture_.first + capture_.count) write_capture();

    // Opaque pictures replace what is behind (texel alpha is reflectivity, not coverage);
    // a transparent clear or a partial composite blends over it.
    bool over = opt.clear[3] < 1.0f || opt.composite_alpha < 1.0f;
    SDL_SetTextureBlendMode(shown, over ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    // DrawItem::frame_alpha left the PS2-style frame alpha in the target: copy the colour only
    // (the 2D frame keeps its own opaque alpha).
    if (!over && frame_alpha_written_) {
        static const SDL_BlendMode kColourOnly =
            SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ZERO, SDL_BLENDOPERATION_ADD,
                                       SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        SDL_SetTextureBlendMode(shown, kColourOnly);
    }
    SDL_SetTextureAlphaModFloat(shown, std::clamp(opt.composite_alpha, 0.0f, 1.0f));
    // The used corner, scaled. Linear filtering would blend the texel beyond its right and
    // bottom edges (unused, the clear colour) into the last pixels: stop half a texel short.
    const float inset = opt.nearest ? 0.0f : 0.5f;
    const SDL_FRect src{0, 0, float(sw) - (sw < tw ? inset : 0.0f), float(sh) - (sh < th ? inset : 0.0f)};
    SDL_RenderTexture(renderer, shown, &src, &rect);
    SDL_SetTextureAlphaModFloat(shown, 1.0f);
}

bool View3D::post_wanted(const DrawOptions& o) const {
    return o.post_grade.on || (o.dissolve > 0 && snap_valid_) || (o.trail > 0 && last_valid_);
}

// A picture-sized texture of the post-process passes (created on first use).
bool View3D::ensure_post(SDL_GPUTexture** t) {
    if (*t) return true;
    if (width_ <= 0 || height_ <= 0) return false;
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.width = u32(width_), ti.height = u32(height_);
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    ti.format = GpuContext::kColorFormat;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    *t = SDL_CreateGPUTexture(ctx_.device(), &ti);
    if (!*t) LOG_WARN("3D post-process target %dx%d: %s", width_, height_, SDL_GetError());
    return *t != nullptr;
}

void View3D::copy_picture(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* from, SDL_GPUTexture* to, int w, int h,
                          bool cycle) {
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureLocation src{}, dst{};
    src.texture = from, dst.texture = to;
    SDL_CopyGPUTextureToTexture(cp, &src, &dst, u32(w), u32(h), 1, cycle); // the picture's corner
    SDL_EndGPUCopyPass(cp);
}

// One picture (its picture_w x picture_h corner of a target-sized texture) over this frame's
// picture through the model shader's post mode (params.w 5): sampled 1:1 when the sizes match
// (scaled when the dynamic scale changed since it was taken), graded when `grade` is given,
// blended at `alpha` (1: replaces).
void View3D::encode_post(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, SDL_GPUTexture* picture, int picture_w,
                         int picture_h, float alpha, const DrawOptions::PostGrade* grade) {
    if (picture_w <= 0 || picture_h <= 0) return;
    // The quad's texture coordinates span the whole texture: a viewport that many target-sizes
    // across puts the picture's corner over this frame's (the whole target when the sizes match);
    // the scissor keeps to this frame's.
    SDL_GPUViewport vp{0, 0, float(width_), float(height_), 0, 1};
    if (picture_w != used_w_ || picture_h != used_h_) {
        vp.w = float(used_w_) * float(width_) / float(picture_w);
        vp.h = float(used_h_) * float(height_) / float(picture_h);
        // Past the smallest viewport limit Vulkan allows: an old picture from a much smaller scale.
        if (vp.w > std::max(4096.0f, float(width_)) || vp.h > std::max(4096.0f, float(height_))) return;
    }
    const SDL_Rect sc{0, 0, used_w_, used_h_};
    SDL_SetGPUViewport(pass, &vp);
    SDL_SetGPUScissor(pass, &sc);
    if (!ensure_quad()) return;
    PipelineKey key;
    key.blend = alpha >= 1.0f ? BlendMode::Opaque : BlendMode::Alpha;
    key.cull = CullMode::None;
    key.depth_write = false;
    SDL_GPUGraphicsPipeline* p = ctx_.pipeline(key);
    if (!p) return;
    SDL_BindGPUGraphicsPipeline(pass, p);
    SDL_GPUBufferBinding vb{quad_vb_, 0}, ib{quad_ib_, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    VertexUniforms vu{};
    const Mat4 id;
    std::copy(id.m.begin(), id.m.end(), vu.mvp);
    std::copy(id.m.begin(), id.m.end(), vu.model);
    std::copy(id.m.begin(), id.m.end(), vu.normal_mat);
    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));
    VertexLighting off{};
    SDL_PushGPUVertexUniformData(cmd, 1, &off, sizeof(off));
    SDL_GPUTextureSamplerBinding sbs[3] = {{picture, ctx_.sampler(true, true)},
                                           {placeholder_cube(), ctx_.sampler(true, true)},
                                           {ctx_.white_texture(), ctx_.sampler(true, true)}};
    SDL_BindGPUFragmentSamplers(pass, 0, sbs, 3);
    FragmentUniforms fu{};
    fu.params[0] = -1, fu.params[1] = 1, fu.params[3] = 5;
    fu.tint[0] = fu.tint[1] = fu.tint[2] = 1, fu.tint[3] = alpha;
    fu.light_dir[1] = 1;
    if (grade) {
        for (int i = 0; i < 3; ++i) fu.grade[i] = grade->curve[i];
        fu.grade[3] = grade->saturation;
        fu.grade2[0] = grade->black, fu.grade2[1] = grade->white, fu.grade2[2] = 1;
    }
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof(fu));
    SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
    ++stats_.draw_calls;
}

// --- The passes after the scene (presentation §L6) -------------------------------------------

View3D::ScreenUpload View3D::upload_screen(SDL_GPUCommandBuffer* cmd, const DrawOptions& o, int picture_w,
                                           int picture_h) {
    ScreenUpload up;
    const ScreenBatch* batches[3] = {o.haze, o.overlay, o.vignette};
    u32* firsts[3] = {&up.haze, &up.overlay, &up.vignette};
    std::vector<scene::Vertex> verts;
    std::vector<u32> idx;
    for (int k = 0; k < 3; ++k) {
        const ScreenBatch* b = batches[k];
        *firsts[k] = u32(idx.size());
        if (!b || b->empty()) continue;
        const u32 base = u32(verts.size());
        const float rw = std::max(b->rect.w, 1e-6f), rh = std::max(b->rect.h, 1e-6f);
        for (const ScreenBatch::Vertex& s : b->verts) {
            scene::Vertex v{};
            const float nx = (s.x - b->rect.x) / rw, ny = (s.y - b->rect.y) / rh;
            v.pos[0] = 2.0f * nx - 1.0f, v.pos[1] = 1.0f - 2.0f * ny, v.pos[2] = s.depth;
            if (k == 0) v.pos[2] = 1.0f; // the haze: in front of everything
            v.normal[2] = 1;
            if (k == 0) { // the haze: texture coordinates in the rect's units, on the copy's picture corner
                v.uv[0] = (s.u - b->rect.x) / rw * float(picture_w) / float(std::max(width_, 1));
                v.uv[1] = (s.v - b->rect.y) / rh * float(picture_h) / float(std::max(height_, 1));
            } else {
                v.uv[0] = s.u, v.uv[1] = s.v;
            }
            u32 c = 0;
            for (int i = 0; i < 4; ++i) c |= u32(std::lround(std::clamp(s.rgba[i], 0.0f, 1.0f) * 255.0f)) << (8 * i);
            v.color = c;
            verts.push_back(v);
        }
        for (u32 i : b->idx) idx.push_back(base + i);
    }
    if (verts.empty() || idx.empty()) return up;
    up.ok = screen_.upload(ctx_.device(), cmd, verts.data(), u32(verts.size() * sizeof(scene::Vertex)), idx.data(),
                           u32(idx.size() * sizeof(u32)));
    return up;
}

bool View3D::upload_clipped(SDL_GPUCommandBuffer* cmd, const Mat4& view, const Mat4& proj, const Vec3& eye,
                            const std::vector<DrawItem>& items, const DrawOptions& opt) {
    clipped_.clear();
    const Mat4 view_proj = proj * view;
    float planes[6][4];
    frustum_planes(view_proj, planes);
    std::vector<ClipVertex> corners;
    std::vector<u32> tris;
    std::vector<ClipPos> clip;
    std::vector<u8> outside;
    for (const DrawItem& item : items) {
        const GpuModel* model = item.model;
        const GpuModel::Geometry* g = model ? model->geometry() : nullptr;
        if (!item.guard_band || !g || !model->valid() || item.late) continue;
        const float near_w = opt.guard_near / std::max(item.modelview_scale, 1e-6f);
        const float far_w = item_far(item, opt);
        const int lod = item_lod(item, opt);
        const std::vector<GpuModel::MeshInfo>& meshes = model->meshes();
        for (size_t mi = 0; mi < meshes.size(); ++mi) {
            const GpuModel::MeshInfo& mesh = meshes[mi];
            if (lod >= 0 && mesh.lod != lod) continue;
            const Placed m = place(item, mesh, eye);
            if (culled(item, m, opt, planes, view) || inside_guard_band(view_proj, m.center, m.radius, near_w, far_w))
                continue;
            // The mesh's vertices in clip space, and whether GT3's VU clips them: with a margin, so
            // every vertex model.vert finds outside is (a triangle the static draw leaves out is
            // always here; the clipped draw drops what the GPU keeps itself).
            const size_t first = size_t(mesh.vertex_offset);
            const size_t end = mi + 1 < meshes.size() ? size_t(meshes[mi + 1].vertex_offset) : g->vertices.size();
            if (end < first || end > g->vertices.size()) continue;
            const Mat4 mvp = view_proj * m.world;
            clip.resize(end - first);
            outside.assign(end - first, 2); // 2: not transformed yet
            auto vertex = [&](u32 k) {
                if (outside[k] != 2) return;
                const float* q = g->vertices[first + k].pos;
                ClipPos& c = clip[k];
                c.x = mvp.at(0, 0) * q[0] + mvp.at(0, 1) * q[1] + mvp.at(0, 2) * q[2] + mvp.at(0, 3);
                c.y = mvp.at(1, 0) * q[0] + mvp.at(1, 1) * q[1] + mvp.at(1, 2) * q[2] + mvp.at(1, 3);
                c.w = mvp.at(3, 0) * q[0] + mvp.at(3, 1) * q[1] + mvp.at(3, 2) * q[2] + mvp.at(3, 3);
                const float w = 2 * c.w * (1.0f - 1e-4f);
                outside[k] = c.w < near_w * (1.0f + 1e-4f) + 1e-6f || std::fabs(c.x) > w || std::fabs(c.y) > w ||
                             (far_w > 0 && c.w > far_w * (1.0f - 1e-4f));
            };
            for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
                const scene::Primitive& p = mesh.primitives[pi];
                // A primitive wholly inside (none of its triangles clipped) or wholly past the far
                // plane (all culled; the primitive's own draw leaves them out too) needs nothing.
                if (pi < mesh.primitive_spheres.size()) {
                    const GpuModel::MeshInfo::Sphere& ps = mesh.primitive_spheres[pi];
                    const Vec3 c = m.world.transform_point(ps.center);
                    const float r = ps.radius * m.scale;
                    if (inside_guard_band(view_proj, c, r, near_w, far_w)) continue;
                    if (far_w > 0 && eye_depth(view, c) - r > far_w) continue;
                }
                const u32 tri_first = u32(tris.size());
                const size_t base = size_t(mesh.index_offset) + p.first_index;
                for (u32 t = 0; t + 2 < p.index_count && base + t + 2 < g->indices.size(); t += 3) {
                    const u32* abc = &g->indices[base + t];
                    if (abc[0] >= clip.size() || abc[1] >= clip.size() || abc[2] >= clip.size()) continue;
                    vertex(abc[0]), vertex(abc[1]), vertex(abc[2]);
                    if (!outside[abc[0]] && !outside[abc[1]] && !outside[abc[2]]) continue;
                    // GT3's strip order: a strip's vertices follow each other (gtm1), so the
                    // indices ascending; the stored order is a rotation of it (even) or not.
                    u32 s[3] = {abc[0], abc[1], abc[2]};
                    std::sort(s, s + 3);
                    const bool even = (abc[0] == s[0] && abc[1] == s[1]) || (abc[0] == s[1] && abc[1] == s[2]) ||
                                      (abc[0] == s[2] && abc[1] == s[0]);
                    const ClipPos v[3] = {clip[s[0]], clip[s[1]], clip[s[2]]};
                    ClipCorner out[kMaxClipCorners];
                    const int n = guard_clip(v, near_w, far_w, out);
                    if (n < 3) continue;
                    const u32 at = u32(corners.size());
                    for (int i = 0; i < n; ++i) {
                        ClipVertex cv;
                        for (int k = 0; k < 3; ++k) cv.v[k] = g->vertices[first + s[k]], cv.b[k] = out[i].b[k];
                        corners.push_back(cv);
                    }
                    // GT3's strip, each piece wound as the stored triangle: the sign of the
                    // determinant of its corners' weights is the sign of its turn about the
                    // triangle's.
                    for (int i = 0; i + 2 < n; ++i) {
                        const float* a = out[i].b;
                        const float* b = out[i + 1].b;
                        const float* c = out[i + 2].b;
                        const float det = a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) +
                                          a[2] * (b[0] * c[1] - b[1] * c[0]);
                        if (det == 0) continue;
                        const u32 j = (det > 0) == even ? 1 : 2; // the second corner (else swapped)
                        tris.insert(tris.end(), {at + u32(i), at + u32(i) + j, at + u32(i) + 3 - j});
                    }
                }
                if (tris.size() > tri_first) clipped_[{&item, &mesh, pi}] = {tri_first, u32(tris.size()) - tri_first};
            }
        }
    }
    return tris.empty() || clip_.upload(ctx_.device(), cmd, corners.data(), u32(corners.size() * sizeof(ClipVertex)),
                                        tris.data(), u32(tris.size() * sizeof(u32)));
}

void View3D::encode_screen(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const ScreenBatch& b, u32 first_index,
                           bool frame) {
    if (b.empty() || !screen_.vb || !screen_.ib) return;
    const SDL_GPUViewport vp{0, 0, float(used_w_), float(used_h_), 0, 1};
    const SDL_Rect sc{0, 0, used_w_, used_h_};
    SDL_SetGPUViewport(pass, &vp);
    SDL_SetGPUScissor(pass, &sc);
    SDL_GPUBufferBinding vb{screen_.vb, 0}, ib{screen_.ib, 0};
    VertexUniforms vu{};
    const Mat4 id;
    std::copy(id.m.begin(), id.m.end(), vu.mvp);
    std::copy(id.m.begin(), id.m.end(), vu.model);
    std::copy(id.m.begin(), id.m.end(), vu.normal_mat);
    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));
    VertexLighting off{};
    SDL_PushGPUVertexUniformData(cmd, 1, &off, sizeof(off));
    for (const ScreenBatch::Draw& d : b.draws) {
        PipelineKey key;
        key.blend = d.blend;
        key.cull = CullMode::None;
        key.depth_write = false;
        SDL_GPUGraphicsPipeline* p = ctx_.pipeline(key);
        if (!p || d.count == 0) continue;
        SDL_BindGPUGraphicsPipeline(pass, p);
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_GPUTexture* tex = frame ? haze_src_ : d.texture && d.texture->gpu() ? d.texture->gpu() : ctx_.white_texture();
        const bool low = frame && haze_low_valid_;
        SDL_GPUTextureSamplerBinding sbs[3] = {{tex, ctx_.sampler(true, true, true)},
                                               {placeholder_cube(), ctx_.sampler(true, true)},
                                               {low ? haze_low_.tex : ctx_.white_texture(), ctx_.sampler(true, true, true)}};
        SDL_BindGPUFragmentSamplers(pass, 0, sbs, 3);
        FragmentUniforms fu{};
        fu.light_dir[1] = 1;
        fu.params[0] = -1; // no alpha test
        fu.params[1] = 1;  // unlit: texture x vertex colour
        for (float& t : fu.tint) t = 1;
        if (frame) {
            // The post mode: the picture's copy, its alpha ignored; GS ALPHA FIX 64 = 0.5.
            fu.params[3] = 5;
            fu.tint[3] = 0.5f;
            if (low) { // the field-sized copy displaced, plus this pixel's finer detail (model.frag)
                const float px = float(used_w_) / float(width_), py = float(used_h_) / float(height_);
                const float lx = float(haze_low_.w) / float(haze_low_.cap_w), ly = float(haze_low_.h) / float(haze_low_.cap_h);
                fu.view_row0[0] = px, fu.view_row0[1] = py, fu.view_row0[2] = lx, fu.view_row0[3] = ly;
                fu.view_row1[0] = lx / px, fu.view_row1[1] = ly / py;
                fu.flat_color[3] = 1;
            }
        }
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof(fu));
        for (int k = 0; k < d.passes; ++k) {
            SDL_DrawGPUIndexedPrimitives(pass, d.count, 1, first_index + d.first, 0, 0);
            ++stats_.draw_calls;
        }
    }
}

void View3D::encode_after_scene(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const Mat4& view, const Mat4& proj,
                                const Vec3& eye, const std::vector<DrawItem>& items, const DrawOptions& o,
                                const ScreenUpload& up) {
    const SDL_GPUViewport vp{0, 0, float(used_w_), float(used_h_), 0, 1};
    const SDL_Rect sc{0, 0, used_w_, used_h_};
    SDL_SetGPUViewport(pass, &vp);
    SDL_SetGPUScissor(pass, &sc);
    // The late items (the tyre smoke), with the scene's depth; their counts add to the scene's.
    const DrawStats early = stats_;
    encode(cmd, pass, view, proj, eye, items, o, Items::Late);
    stats_.meshes_drawn += early.meshes_drawn, stats_.meshes_culled += early.meshes_culled;
    stats_.draw_calls += early.draw_calls, stats_.triangles += early.triangles;
    if (up.ok && o.overlay && !o.overlay->empty()) encode_screen(cmd, pass, *o.overlay, up.overlay, false);
}

// A quad over the viewport: clip-space corners (an identity transform), texture rows from the
// top (SDL_GPU), v_uv 0..1 across.
bool View3D::ensure_quad() {
    if (quad_vb_ && quad_ib_) return true;
    scene::Vertex v[4] = {};
    const float xy[4][2] = {{-1, 1}, {1, 1}, {-1, -1}, {1, -1}}, uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    for (int i = 0; i < 4; ++i) {
        v[i].pos[0] = xy[i][0], v[i].pos[1] = xy[i][1], v[i].pos[2] = 0;
        v[i].normal[2] = 1;
        v[i].uv[0] = uv[i][0], v[i].uv[1] = uv[i][1];
    }
    const u32 idx[6] = {0, 2, 1, 1, 2, 3};
    Uploader up(ctx_.device());
    if (!quad_vb_) quad_vb_ = up.buffer(SDL_GPU_BUFFERUSAGE_VERTEX, v, sizeof(v));
    if (!quad_ib_) quad_ib_ = up.buffer(SDL_GPU_BUFFERUSAGE_INDEX, idx, sizeof(idx));
    up.submit();
    return quad_vb_ && quad_ib_;
}

void View3D::release_dof_levels() {
    for (DofLevel* set : {dof_level_, dof_tmp_, &haze_low_})
        for (int k = 0; k < (set == dof_level_ ? 5 : set == dof_tmp_ ? 2 : 1); ++k) {
            if (set[k].tex) SDL_ReleaseGPUTexture(ctx_.device(), set[k].tex);
            set[k] = {};
        }
}

// A w x h picture in the corner of `l`'s texture (created on first use; again only when a larger
// picture needs more).
bool View3D::ensure_dof_texture(DofLevel& l, int w, int h) {
    w = std::max(w, 1), h = std::max(h, 1);
    if (!l.tex || l.cap_w < w || l.cap_h < h) {
        if (l.tex) SDL_ReleaseGPUTexture(ctx_.device(), l.tex);
        const int cw = std::max(w, l.cap_w), ch = std::max(h, l.cap_h);
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.width = u32(cw), ti.height = u32(ch);
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        ti.format = GpuContext::kColorFormat;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
        l.tex = SDL_CreateGPUTexture(ctx_.device(), &ti);
        l.cap_w = cw, l.cap_h = ch;
        if (!l.tex) {
            LOG_WARN("Depth of field target %dx%d: %s", cw, ch, SDL_GetError());
            release_dof_levels();
            return false;
        }
    }
    l.w = w, l.h = h;
    return true;
}

// The downsampled pictures for a pw x ph picture `aspect` times as wide as high. Up to the frame's
// 448 lines GT3's pyramid at its sizes; taller pictures get each level at their own resolution:
// `dx` x `dy` pixels a texel, as many as keep its blur (dof_level_blur) over 1.2 texels, and the
// two intermediate pictures of its blur.
bool View3D::ensure_dof_levels(float aspect, int pw, int ph) {
    const bool frame = ph <= 448;
    const float px[2] = {float(pw) / (480.0f * aspect), float(ph) / 448.0f}; // picture pixels a frame pixel
    int tmp_w = 1, tmp_h = 1;
    for (int k = 0; k < 5; ++k) {
        DofLevel& l = dof_level_[k];
        std::array<int, 2> size = dof_level_size(k, aspect);
        l.dx = l.dy = 1;
        if (!frame) {
            const DofBlur b = dof_level_blur(k);
            int d[2] = {1, 1};
            for (int a = 0; a < 2; ++a)
                while (d[a] * 2 <= 16 && float(d[a] * 2) * 1.2f <= b.sigma[a] * px[a]) d[a] *= 2;
            l.dx = d[0], l.dy = d[1];
            size = {(pw + d[0] - 1) / d[0], (ph + d[1] - 1) / d[1]};
            tmp_w = std::max(tmp_w, size[0]), tmp_h = std::max(tmp_h, size[1]);
        }
        if (!ensure_dof_texture(l, size[0], size[1])) return false;
    }
    if (!frame)
        for (DofLevel& t : dof_tmp_)
            if (!ensure_dof_texture(t, tmp_w, tmp_h)) return false;
    return true;
}

void View3D::encode_resample(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, SDL_GPUTexture* src,
                             const Resample& r, float alpha, float depth, bool single) {
    PipelineKey key;
    key.blend = alpha >= 1.0f ? BlendMode::Opaque : BlendMode::Alpha;
    key.cull = CullMode::None;
    key.depth_write = false;
    key.single = single;
    SDL_GPUGraphicsPipeline* p = ctx_.pipeline(key);
    if (!p) return;
    SDL_BindGPUGraphicsPipeline(pass, p);
    SDL_GPUBufferBinding vb{quad_vb_, 0}, ib{quad_ib_, 0};
    SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
    SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    VertexUniforms vu{};
    const Mat4 at_depth = translate({0, 0, depth}), id;
    std::copy(at_depth.m.begin(), at_depth.m.end(), vu.mvp);
    std::copy(id.m.begin(), id.m.end(), vu.model);
    std::copy(id.m.begin(), id.m.end(), vu.normal_mat);
    SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));
    VertexLighting off{};
    SDL_PushGPUVertexUniformData(cmd, 1, &off, sizeof(off));
    SDL_GPUTextureSamplerBinding sbs[3] = {{src, ctx_.sampler(true, true, true)},
                                           {placeholder_cube(), ctx_.sampler(true, true)},
                                           {ctx_.white_texture(), ctx_.sampler(true, true)}};
    SDL_BindGPUFragmentSamplers(pass, 0, sbs, 3);
    FragmentUniforms fu{};
    fu.params[0] = -1, fu.params[1] = 1, fu.params[3] = 6;
    fu.tint[0] = fu.tint[1] = fu.tint[2] = 1, fu.tint[3] = alpha;
    fu.light_dir[1] = 1;
    fu.view_row0[0] = r.scale[0], fu.view_row0[1] = r.scale[1];
    fu.view_row0[2] = r.offset[0], fu.view_row0[3] = r.offset[1];
    std::copy(r.clamp, r.clamp + 4, fu.view_row1);
    fu.grade[0] = r.taps[0], fu.grade[1] = r.taps[1], fu.grade[2] = r.step[0], fu.grade[3] = r.step[1];
    fu.flat_color[0] = r.sigma[0], fu.flat_color[1] = r.sigma[1];
    SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof(fu));
    SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
    ++stats_.draw_calls;
}

// A modelled rim's turned copies (DrawItem::spin_turn) leave each pixel the mean of the copies its
// samples hold, a different set from its neighbours' over a 2 x 2 or 4 x 4 cell pattern. Over each
// rim's place on screen the picture is then filtered by a kernel that weighs every cell alike
// (spin_copy_period's taps, from a copy of the picture): the mean of all the copies, at the weight
// of how far apart a pixel's own copies lie at the rim's edge (1 from a pixel on), so a rim hardly
// turning keeps its sharpness. The heat haze's copy of the picture is free by then and is reused.
void View3D::spin_resolve(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* picture, int pw, int ph, bool cycle) {
    const int samples = ctx_.gles() ? 1 : ctx_.msaa(); // (the OpenGL ES variant has no sample masks)
    auto weight_of = [&](const SpinRect& s) { return s.period < 2 ? 0.0f : std::min(s.spread_px / float(samples), 1.0f); };
    if (std::none_of(spin_rects_.begin(), spin_rects_.end(), [&](const SpinRect& s) { return weight_of(s) >= 0.02f; }))
        return; // (a copy a sample: nothing to even out)
    if (!ensure_post(&haze_src_) || !ensure_quad()) return;
    copy_picture(cmd, picture, haze_src_, pw, ph, cycle);
    SDL_GPUColorTargetInfo t{};
    t.texture = picture;
    t.load_op = SDL_GPU_LOADOP_LOAD;
    t.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &t, 1, nullptr);
    if (!pass) return;
    const SDL_GPUViewport vp{0, 0, float(pw), float(ph), 0, 1};
    SDL_SetGPUViewport(pass, &vp);
    const float W = float(width_), H = float(height_);
    Resample r;
    r.scale[0] = float(pw) / W, r.scale[1] = float(ph) / H;
    r.clamp[0] = 0.5f / W, r.clamp[1] = 0.5f / H;
    r.clamp[2] = (float(pw) - 0.5f) / W, r.clamp[3] = (float(ph) - 0.5f) / H;
    r.step[0] = 1.0f / W, r.step[1] = 1.0f / H;
    for (const SpinRect& s : spin_rects_) {
        const float weight = weight_of(s);
        r.taps[0] = r.taps[1] = float(s.period);
        const int x0 = std::clamp(int(std::floor((s.x0 * 0.5f + 0.5f) * float(pw))), 0, pw);
        const int x1 = std::clamp(int(std::ceil((s.x1 * 0.5f + 0.5f) * float(pw))), 0, pw);
        const int y0 = std::clamp(int(std::floor((0.5f - s.y1 * 0.5f) * float(ph))), 0, ph);
        const int y1 = std::clamp(int(std::ceil((0.5f - s.y0 * 0.5f) * float(ph))), 0, ph);
        if (weight < 0.02f || x1 <= x0 || y1 <= y0) continue;
        const SDL_Rect sc{x0, y0, x1 - x0, y1 - y0};
        SDL_SetGPUScissor(pass, &sc);
        encode_resample(cmd, pass, haze_src_, r, weight, 0.0f, true);
    }
    SDL_EndGPURenderPass(pass);
}

// presentation §G17. The GS samples a pixel at its corner, the bilinear filter at the
// coordinate - 0.5: GT3's coordinates (+0.5 +0.5 in the pyramid, -0.5 +0.5 in the layers) make
// each level the mean of 2 x 2 texels of the one before, or of 2 x 1 / 1 x 2 where an axis keeps
// its size (a texel and the next one: that picture moves half a texel right / down), and show
// each pixel its level half a frame pixel up and left of its centre (the blurred picture moves
// half a pixel right and down). Ours keeps those offsets; its texel centres sit at +0.5.
void View3D::encode_dof(SDL_GPUCommandBuffer* cmd, const DrawOptions& o, const Mat4& proj, float aspect,
                        SDL_GPUTexture* out, bool cycle) {
    const int pw = used_w_, ph = used_h_;
    const float W = float(width_), H = float(height_);
    // The picture's texels in scene_ (the target's corner), clamped half a texel in.
    Resample pic;
    pic.scale[0] = float(pw) / W, pic.scale[1] = float(ph) / H;
    pic.clamp[0] = 0.5f / W, pic.clamp[1] = 0.5f / H;
    pic.clamp[2] = (float(pw) - 0.5f) / W, pic.clamp[3] = (float(ph) - 0.5f) / H;
    auto pass_into = [&](const DofLevel& l) {
        SDL_GPUColorTargetInfo ct{};
        ct.texture = l.tex;
        ct.load_op = SDL_GPU_LOADOP_DONT_CARE;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        ct.cycle = cycle;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);
        const SDL_GPUViewport vp{0, 0, float(l.w), float(l.h), 0, 1};
        const SDL_Rect sc{0, 0, l.w, l.h};
        SDL_SetGPUViewport(pass, &vp);
        SDL_SetGPUScissor(pass, &sc);
        return pass;
    };
    auto texels = [&](const DofLevel& l) { // a picture's texels in its texture, clamped half a texel in
        Resample r;
        r.scale[0] = float(l.w) / float(l.cap_w), r.scale[1] = float(l.h) / float(l.cap_h);
        r.clamp[0] = 0.5f / float(l.cap_w), r.clamp[1] = 0.5f / float(l.cap_h);
        r.clamp[2] = (float(l.w) - 0.5f) / float(l.cap_w), r.clamp[3] = (float(l.h) - 0.5f) / float(l.cap_h);
        return r;
    };
    // Mean of the picture's texels under each of `l`'s texels (bilinear taps 2 texels apart: exact
    // for an even number of texels).
    auto downsample = [&](const DofLevel& l, float fx, float fy) {
        SDL_GPURenderPass* pass = pass_into(l);
        Resample r = pic;
        r.scale[0] = float(l.w) * fx / W, r.scale[1] = float(l.h) * fy / H;
        r.taps[0] = std::clamp(std::ceil(fx * 0.5f), 1.0f, 8.0f);
        r.taps[1] = std::clamp(std::ceil(fy * 0.5f), 1.0f, 8.0f);
        r.step[0] = fx / r.taps[0] / W, r.step[1] = fy / r.taps[1] / H;
        encode_resample(cmd, pass, scene_, r, 1.0f, 0.0f, true);
        SDL_EndGPURenderPass(pass);
    };
    const bool frame = ph <= 448;
    const float frame_w = 480.0f * aspect; // the frame's pixels across (its 448 lines at their spacing)
    if (frame) {
        // Level 0: GT3's 2 x 2 mean of its 640 x 448 frame; levels 1-4 from the one before: a
        // halved axis samples between two texels (their mean), a kept one half a texel on (a
        // texel and the next).
        downsample(dof_level_[0], float(pw) / float(dof_level_[0].w), float(ph) / float(dof_level_[0].h));
        for (int k = 1; k < 5; ++k) {
            const DofLevel &src = dof_level_[k - 1], &dst = dof_level_[k];
            SDL_GPURenderPass* pass = pass_into(dst);
            Resample r = texels(src);
            if (dst.w == src.w) r.offset[0] = 0.5f / float(src.cap_w);
            if (dst.h == src.h) r.offset[1] = 0.5f / float(src.cap_h);
            encode_resample(cmd, pass, src.tex, r, 1.0f, 0.0f, true);
            SDL_EndGPURenderPass(pass);
        }
    } else {
        // A taller picture: each level at its own resolution (dx x dy pixels a texel: their mean),
        // then a Gaussian across and down that brings the spread to the original level's
        // (dof_level_blur, in this picture's pixels; the layers' bilinear filter adds its share).
        const float px[2] = {float(pw) / frame_w, float(ph) / 448.0f};
        for (int k = 0; k < 5; ++k) {
            DofLevel& l = dof_level_[k];
            const DofBlur b = dof_level_blur(k);
            const int d[2] = {l.dx, l.dy};
            float sigma[2];
            for (int a = 0; a < 2; ++a) {
                const float s = b.sigma[a] * px[a], dd = float(d[a]);
                const float rest = s * s - (dd * dd - 1.0f) / 12.0f - dd * dd / 6.0f;
                sigma[a] = std::sqrt(std::max(rest, 0.0625f)) / dd;
            }
            DofLevel &t0 = dof_tmp_[0], &t1 = dof_tmp_[1];
            t0.w = t1.w = l.w, t0.h = t1.h = l.h;
            downsample(t0, float(d[0]), float(d[1]));
            for (int a = 0; a < 2; ++a) {
                const DofLevel& src = a == 0 ? t0 : t1;
                SDL_GPURenderPass* pass = pass_into(a == 0 ? t1 : l);
                Resample r = texels(src);
                const float n = std::min(2.0f * std::ceil(2.5f * sigma[a]) + 1.0f, 15.0f);
                r.taps[a] = n, r.sigma[a] = sigma[a];
                r.step[a] = 1.0f / float(a == 0 ? src.cap_w : src.cap_h);
                encode_resample(cmd, pass, src.tex, r, 1.0f, 0.0f, true);
                SDL_EndGPURenderPass(pass);
            }
        }
    }
    // The layers, each tested (GEQUAL on the reversed depth) at its distance against the scene's
    // depth: the multisampled one with MSAA, so the layers' edges are smooth. The picture and the
    // depth stay for the passes after it (the haze, the smoke, the glare: View3D::draw).
    SDL_GPUColorTargetInfo ct{};
    ct.clear_color = {0, 0, 0, 1};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.cycle = cycle;
    if (msaa_) {
        ct.texture = msaa_;
        ct.store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
        ct.resolve_texture = out;
        ct.cycle_resolve_texture = cycle;
    } else {
        ct.texture = out;
        ct.store_op = SDL_GPU_STOREOP_STORE;
    }
    SDL_GPUDepthStencilTargetInfo dt{};
    dt.texture = depth_;
    dt.load_op = SDL_GPU_LOADOP_LOAD;
    dt.store_op = SDL_GPU_STOREOP_STORE;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    const SDL_GPUViewport vp{0, 0, float(pw), float(ph), 0, 1};
    const SDL_Rect sc{0, 0, pw, ph};
    SDL_SetGPUViewport(pass, &vp);
    SDL_SetGPUScissor(pass, &sc);
    // A distance as this projection's depth (column vectors: clip z = m22 z + m23, w = -z); the
    // near plane's is 1.
    const float m22 = proj.at(2, 2), m23 = proj.at(2, 3);
    const float near = m23 / std::max(1.0f + m22, 1e-6f);
    auto depth_at = [&](float dist) { return dist <= 0 ? 1.0f : std::clamp((m23 - m22 * dist) / dist, 0.0f, 1.0f); };
    for (const DofLayer& layer : dof_layers(o.dof, proj.at(1, 1), near)) {
        if (layer.level < 0) {
            encode_resample(cmd, pass, scene_, pic, 1.0f, depth_at(layer.distance), false);
            continue;
        }
        const DofLevel& l = dof_level_[layer.level];
        Resample r = texels(l);
        if (frame) { // half a frame pixel up and left (the pyramid made the rest of the shift)
            r.offset[0] = -0.5f / frame_w * r.scale[0], r.offset[1] = -0.5f / 448.0f * r.scale[1];
        } else { // the level's texels over the picture's pixels, the original level's whole shift
            const DofBlur b = dof_level_blur(layer.level);
            r.scale[0] = float(pw) / float(l.dx * l.cap_w), r.scale[1] = float(ph) / float(l.dy * l.cap_h);
            r.offset[0] = -b.shift[0] / frame_w * r.scale[0], r.offset[1] = -b.shift[1] / 448.0f * r.scale[1];
        }
        encode_resample(cmd, pass, l.tex, r, layer.alpha, depth_at(layer.distance), false);
    }
    SDL_EndGPURenderPass(pass);
}

bool View3D::draw_to(SDL_GPUTexture* target, int w, int h, const Mat4& view, const Mat4& proj, const Vec3& eye,
                     const std::vector<DrawItem>& items, const DrawOptions& opt) {
    if (!target || w <= 0 || h <= 0) return false;
    SDL_GPUDevice* dev = ctx_.device();
    Offscreen& o = direct_;
    if (!o.depth || o.w != w || o.h != h || o.samples != ctx_.msaa()) {
        for (SDL_GPUTexture** t : {&o.msaa, &o.depth}) {
            if (*t) SDL_ReleaseGPUTexture(dev, *t);
            *t = nullptr;
        }
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.width = u32(w), ti.height = u32(h);
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        ti.sample_count = ctx_.sample_count();
        if (ctx_.sample_count() != SDL_GPU_SAMPLECOUNT_1) {
            ti.format = GpuContext::kColorFormat;
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            o.msaa = SDL_CreateGPUTexture(dev, &ti);
        }
        ti.format = ctx_.depth_format();
        ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        o.depth = SDL_CreateGPUTexture(dev, &ti);
        o.w = w, o.h = h, o.samples = ctx_.msaa();
        if (!o.depth || (ctx_.sample_count() != SDL_GPU_SAMPLECOUNT_1 && !o.msaa)) {
            LOG_WARN("3D target %dx%d: %s", w, h, SDL_GetError());
            return false;
        }
    }
    if (has_depth_reset(items)) ensure_quad();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) return false;
    SDL_GPUColorTargetInfo ct{};
    ct.clear_color = {opt.clear[0], opt.clear[1], opt.clear[2], opt.clear[3]};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    if (o.msaa) {
        ct.texture = o.msaa;
        ct.store_op = SDL_GPU_STOREOP_RESOLVE;
        ct.resolve_texture = target;
        ct.cycle = true;
        ct.cycle_resolve_texture = true;
    } else {
        ct.texture = target;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        ct.cycle = true;
    }
    SDL_GPUDepthStencilTargetInfo dt{};
    dt.texture = o.depth;
    dt.clear_depth = kFarDepth;
    dt.load_op = SDL_GPU_LOADOP_CLEAR;
    dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.cycle = true;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    frame_alpha_written_ = false;
    encode(cmd, pass, view, proj, eye, items, opt, Items::Early);
    const DrawStats early = stats_;
    encode(cmd, pass, view, proj, eye, items, opt, Items::Late);
    stats_.meshes_drawn += early.meshes_drawn, stats_.meshes_culled += early.meshes_culled;
    stats_.draw_calls += early.draw_calls, stats_.triangles += early.triangles;
    SDL_EndGPURenderPass(pass);
    return SDL_SubmitGPUCommandBuffer(cmd);
}

void View3D::encode(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const Mat4& view, const Mat4& proj,
                    const Vec3& eye, const std::vector<DrawItem>& items, const DrawOptions& opt, Items which) {
    const Mat4 view_proj = proj * view;
    // Wireframe lines are pulled slightly towards the camera (to a larger depth) to avoid z-fighting.
    Mat4 line_proj = proj;
    for (int c = 0; c < 4; ++c) line_proj.at(2, c) += 1e-4f * proj.at(3, c);
    const Mat4 line_view_proj = line_proj * view;

    float planes[6][4];
    frustum_planes(view_proj, planes);

    Vec3 l = normalize(opt.light_dir);
    FragmentUniforms fu{};
    fu.light_dir[0] = l.x, fu.light_dir[1] = l.y, fu.light_dir[2] = l.z, fu.light_dir[3] = opt.ambient;
    fu.camera_pos[0] = eye.x, fu.camera_pos[1] = eye.y, fu.camera_pos[2] = eye.z;
    for (int i = 0; i < 4; ++i) fu.flat_color[i] = opt.wire_color[i];
    for (int i = 0; i < 3; ++i) fu.fog_color[i] = opt.fog_color[i];
    fu.fog_params[0] = opt.fog_start;
    fu.fog_params[1] = 1.0f / std::max(opt.fog_end - opt.fog_start, 1e-3f);
    fu.fog_params[2] = opt.fog_max;
    fu.fog_params[3] = opt.fog_gt3 ? 1.0f : 0.0f;
    for (int i = 0; i < 3; ++i) {
        fu.light_ambient[i] = opt.light_ambient[i], fu.light_diffuse[i] = opt.light_diffuse[i];
        fu.env_sky[i] = opt.env_sky[i], fu.env_horizon[i] = opt.env_horizon[i], fu.env_ground[i] = opt.env_ground[i];
    }
    for (int i = 0; i < 3; ++i) fu.light_specular[i] = opt.light_specular[i];
    for (int k = 0; k < 3 && k < opt.fill_count; ++k) {
        float* d = k == 0 ? fu.light2_dir : k == 1 ? fu.light3_dir : fu.light4_dir;
        float* df = k == 0 ? fu.light2_diffuse : k == 1 ? fu.light3_diffuse : fu.light4_diffuse;
        float* sp = k == 0 ? fu.light2_specular : k == 1 ? fu.light3_specular : fu.light4_specular;
        for (int i = 0; i < 3; ++i) d[i] = opt.fill_dir[k][i], df[i] = opt.fill_diffuse[k][i], sp[i] = opt.fill_specular[k][i];
        d[3] = 1;
    }
    fu.light_ambient[3] = opt.gt3_lighting ? 1.0f : 0.0f;
    fu.env_sky[3] = opt.reflection_fresnel;
    SDL_GPUTexture* env_tex = opt.env_cube ? opt.env_cube : placeholder_cube();
    fu.env_ground[3] = opt.env_sphere ? 3.0f : opt.env_cube ? (opt.env_gt3_lookup ? 2.0f : 1.0f) : 0.0f;
    SDL_GPUTextureSamplerBinding sphere_sb{opt.env_sphere ? opt.env_sphere : ctx_.white_texture(),
                                           ctx_.sampler(true, true)};
    fu.output[0] = opt.quantize_rgb5 ? 1.0f : 0.0f;
    for (float& t : fu.tint) t = 1.0f;
    for (int i = 0; i < 4; ++i) fu.view_row0[i] = view.at(0, i), fu.view_row1[i] = view.at(1, i);

    // Visibility: one entry per mesh that passes LOD selection and culling.
    struct Visible {
        const DrawItem* item;
        const GpuModel::MeshInfo* mesh;
        Mat4 world;
        float dist;
        int copies = 0, cells = 0; // a DrawItem::spin_turn item's turned copies (instances)
    };
    std::vector<Visible> vis;
    stats_ = {};
    for (const DrawItem& item : items) {
        const GpuModel* model = item.model;
        if (!model || !model->valid()) continue;
        if ((which == Items::Early && item.late) || (which == Items::Late && !item.late)) continue;
        if (reaches_past_far(item, opt, view, eye)) {
            stats_.meshes_culled += int(model->meshes().size());
            continue;
        }
        const int lod = item_lod(item, opt);
        for (const GpuModel::MeshInfo& mesh : model->meshes()) {
            if (lod >= 0 && mesh.lod != lod) continue;
            const Placed m = place(item, mesh, eye);
            if (culled(item, m, opt, planes, view)) {
                ++stats_.meshes_culled;
                continue;
            }
            ++stats_.meshes_drawn;
            if (item.spin_turn == 0 || !ctx_.spin_copies()) {
                vis.push_back({&item, &mesh, m.world, m.dist});
                continue;
            }
            // A spinning modelled rim: copies turned about the item's x axis, as many as its size on
            // screen asks (the mesh's bounding sphere, widened by its nearest depth). Views without
            // spin_resolve (VR eyes, offscreen buffers, the reflection cube) draw one a sample.
            const int samples = ctx_.gles() ? 1 : ctx_.msaa(); // (the OpenGL ES variant has no sample masks)
            const Vec3 c = m.center;
            const float r = m.radius;
            const float cw = view_proj.at(3, 0) * c.x + view_proj.at(3, 1) * c.y + view_proj.at(3, 2) * c.z +
                             view_proj.at(3, 3);
            const float sx = std::fabs(proj.at(0, 0)), sy = std::fabs(proj.at(1, 1));
            const bool in_front = cw - r > 1e-3f;
            const float radius_px = in_front ? r * sx / cw * spin_px_ : 0.0f;
            const float spread_px = radius_px * std::fabs(item.spin_turn);
            const int n = spin_px_ > 0 ? spin_copy_count(spread_px, radius_px, samples) : samples;
            const int cells = std::max(n / samples, 1);
            vis.push_back({&item, &mesh, m.world, m.dist, n, cells});
            if (spin_px_ > 0 && in_front) {
                const Vec3 cc = view_proj.transform_point(c);
                const float rx = 1.1f * r * sx / (cw - r), ry = 1.1f * r * sy / (cw - r);
                SpinRect s{&item, cc.x / cw - rx, cc.y / cw - ry, cc.x / cw + rx, cc.y / cw + ry, spread_px,
                           spin_copy_period(cells)};
                if (!spin_rects_.empty() && spin_rects_.back().item == &item) {
                    SpinRect& u = spin_rects_.back(); // another mesh of the same rim
                    u.x0 = std::min(u.x0, s.x0), u.y0 = std::min(u.y0, s.y0);
                    u.x1 = std::max(u.x1, s.x1), u.y1 = std::max(u.y1, s.y1);
                    u.spread_px = std::max(u.spread_px, s.spread_px), u.period = std::max(u.period, s.period);
                } else {
                    spin_rects_.push_back(s);
                }
            }
        }
    }

    // A draw waits until the next one: while nothing changes in between (pipeline, buffers,
    // uniforms, textures) and the next primitive's indices continue its own in the same mesh,
    // the two are one draw. State is only set when it differs from what the pass has, and any
    // change sends the waiting draw first.
    struct PendingDraw {
        u32 first = 0, count = 0;
        s32 vertex_offset = 0;
        u32 instances = 1;
    } pending;
    auto flush = [&] {
        if (pending.count == 0) return;
        SDL_DrawGPUIndexedPrimitives(pass, pending.count, pending.instances, pending.first, pending.vertex_offset, 0);
        ++stats_.draw_calls;
        pending.count = 0;
    };
    SDL_GPUGraphicsPipeline* bound = nullptr;
    auto bind = [&](const PipelineKey& key) {
        SDL_GPUGraphicsPipeline* p = ctx_.pipeline(key);
        if (p && p != bound) flush(), SDL_BindGPUGraphicsPipeline(pass, p);
        if (p) bound = p;
        return p != nullptr;
    };
    SDL_GPUTextureSamplerBinding bound_sbs[3]{};
    bool sbs_bound = false;
    auto bind_samplers = [&](const SDL_GPUTextureSamplerBinding (&sbs)[3]) {
        bool same = sbs_bound;
        for (int i = 0; i < 3 && same; ++i)
            same = sbs[i].texture == bound_sbs[i].texture && sbs[i].sampler == bound_sbs[i].sampler;
        if (same) return;
        flush();
        SDL_BindGPUFragmentSamplers(pass, 0, sbs, 3);
        std::copy(std::begin(sbs), std::end(sbs), bound_sbs);
        sbs_bound = true;
    };
    VertexLighting pushed_vl{};
    bool vl_pushed = false;
    auto push_lighting = [&](const VertexLighting& vl) {
        if (vl_pushed && std::memcmp(&vl, &pushed_vl, sizeof(vl)) == 0) return;
        flush();
        SDL_PushGPUVertexUniformData(cmd, 1, &vl, sizeof(vl));
        pushed_vl = vl, vl_pushed = true;
    };
    FragmentUniforms pushed_fu{};
    bool fu_pushed = false;
    auto push_fragment = [&](const FragmentUniforms& u) {
        if (fu_pushed && std::memcmp(&u, &pushed_fu, sizeof(u)) == 0) return;
        flush();
        SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof(u));
        pushed_fu = u, fu_pushed = true;
    };
    const GpuModel* bound_model = nullptr;
    const Visible* pushed = nullptr;
    bool pushed_lines = false;
    auto use_mesh = [&](const Visible& v, bool lines) {
        const GpuModel* model = v.item->model;
        if (model != bound_model) {
            flush();
            SDL_GPUBufferBinding vb{model->vertex_buffer(), 0};
            SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
            SDL_GPUBufferBinding ib{model->index_buffer(), 0};
            SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            bound_model = model;
        }
        if (pushed == &v && pushed_lines == lines) return;
        Mat4 mvp = (lines ? line_view_proj : view_proj) * v.world;
        Mat4 nm = transpose(inverse(v.world));
        VertexUniforms vu{};
        std::copy(mvp.m.begin(), mvp.m.end(), vu.mvp);
        std::copy(v.world.m.begin(), v.world.m.end(), vu.model);
        std::copy(nm.m.begin(), nm.m.end(), vu.normal_mat);
        for (int i = 0; i < 4; ++i) vu.view_z[i] = view.at(2, i);
        vu.fog[0] = opt.fog_start, vu.fog[1] = opt.fog_max, vu.fog[2] = opt.fog_gt3 ? 1.0f : 0.0f;
        if (v.copies > 0) { // the item's x axis through its origin, in the mesh's frame
            const Mat4 to_mesh = inverse(v.mesh->transform);
            const Vec3 o = to_mesh.transform_point({0, 0, 0}), a = normalize(to_mesh.transform_point({1, 0, 0}) - o);
            vu.spin_axis[0] = a.x, vu.spin_axis[1] = a.y, vu.spin_axis[2] = a.z;
            vu.spin_axis[3] = v.item->spin_turn / float(v.copies);
            vu.spin_pivot[0] = o.x, vu.spin_pivot[1] = o.y, vu.spin_pivot[2] = o.z;
        }
        // GT3's near plane in the item's eye space (the backdrop's modelview scale: s x the depth).
        if (guard_on_ && v.item->guard_band && !lines)
            vu.fog[3] = opt.guard_near / std::max(v.item->modelview_scale, 1e-6f);
        if (!lines) vu.far_clip[0] = item_far(*v.item, opt); // (GT3's far plane)
        flush();
        SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));
        pushed = &v;
        pushed_lines = lines;
    };
    // half: kAlphaFailBlend primitives are drawn in two halves: 1 = fragments passing the
    // alpha test (with depth write, in the opaque phase), 2 = the failing ones (blended,
    // no depth write, in the blended phase). 0 = whole primitive.
    // Per-vertex lighting off until a primitive needs it (slot 1 of the vertex uniforms).
    push_lighting(VertexLighting{});
    bool vl_on = false;
    auto draw_prim = [&](const Visible& v, const scene::Primitive& p, int half = 0) {
        bool background = v.item->background;
        PipelineKey key;
        key.blend = (p.flags & scene::kMultiplyDest) ? BlendMode::MultiplyDest
                    : (p.flags & scene::kAdditive)   ? BlendMode::Additive
                    : (p.flags & scene::kAlphaBlend) ? BlendMode::Alpha
                                                     : BlendMode::Opaque;
        key.cull = opt.cull                                                  ? *opt.cull
                   : (p.flags & scene::kDoubleSided) && !v.item->single_sided ? CullMode::None
                                                                               : CullMode::Back;
        if (opt.mirrored && key.cull != CullMode::None)
            key.cull = key.cull == CullMode::Back ? CullMode::Front : CullMode::Back;
        key.depth_write = !(p.flags & scene::kNoDepthWrite) && !background && half != 2;
        key.coverage = opt.clear[3] < 1.0f;
        key.spin_copy = v.copies > 0;
        bool faded = v.item->alpha < 1.0f;
        if (faded && key.blend == BlendMode::Opaque) key.blend = BlendMode::Alpha;
        if (v.item->dest_alpha_blend && key.blend != BlendMode::Opaque)
            key.blend = v.item->dest_alpha_blend == 1 ? BlendMode::DestAlphaLerp : BlendMode::DestAlphaAdd;
        if (p.flags & scene::kDepthOnly) {
            key.blend = BlendMode::Opaque, key.depth_only = true;
        } else if (p.flags & scene::kAlphaOnly) {
            if (!v.item->frame_alpha) return;
            key.blend = BlendMode::Opaque, key.alpha = AlphaWrite::Only;
        } else if (v.item->frame_alpha && !key.coverage) {
            if ((p.flags & scene::kAdditive) && (p.flags & scene::kDestAlphaAdd))
                key.blend = BlendMode::DestAlphaAdd; // GS ALPHA 0x58: Cs * Ad + Cd, alpha kept
            else if (!(p.flags & scene::kKeepAlpha)) key.alpha = AlphaWrite::Write;
            frame_alpha_written_ = frame_alpha_written_ || key.alpha != AlphaWrite::Keep;
        }
        if (!bind(key)) return;
        use_mesh(v, false);
        const GpuModel* model = v.item->model;
        SDL_GPUTextureSamplerBinding sb{ctx_.white_texture(), ctx_.sampler(false, false)};
        bool textured = false;
        if (v.item->texture) {
            sb = {v.item->texture, ctx_.sampler(true, true)};
            textured = true;
        } else if (p.texture >= 0) {
            const GpuModel::TextureInfo& t = model->textures()[size_t(p.texture)];
            if (t.texture) sb = {t.texture, ctx_.sampler(t.clamp_u, t.clamp_v, t.no_mips)};
            textured = t.texture != nullptr;
        }
        SDL_GPUTextureSamplerBinding sbs[3] = {sb, {env_tex, ctx_.sampler(true, true)}, sphere_sb};
        bind_samplers(sbs);
        bool blended_prim = key.blend != BlendMode::Opaque;
        // Transparent pictures (composited over the 2D frame): opaque primitives cover fully,
        // whatever their texel alpha (which is reflectivity there).
        fu.grade2[3] = (opt.clear[3] < 1.0f && !blended_prim) ? 1.0f : 0.0f;
        fu.params[0] = !(p.flags & scene::kAlphaTest) ? -1.0f
                       : p.alpha_ref >= 0                ? p.alpha_ref
                       : blended_prim                    ? opt.blend_alpha_ref
                                                         : opt.alpha_ref;
        if (textured && v.item->wheel_blur && v.item->wheel_blur_alpha_ref >= 0 && (p.flags & scene::kAlphaTest) &&
            p.alpha_ref < 0)
            fu.params[0] = v.item->wheel_blur_alpha_ref;
        if (half == 2 && fu.params[0] >= 0) fu.params[0] += 2.0f; // keep only the failing fragments
        fu.params[1] = (p.flags & scene::kUnlit) ? 1.0f : 0.0f;
        // kAdditive | kEnvironmentMap: car reflection pass when kUnlit is set or nothing is
        // bound (the PS2 binds the env map at run time; a bound texture only supplies the
        // per-pixel reflectivity in its alpha).
        bool env_add = (p.flags & scene::kAdditive) && (p.flags & scene::kEnvironmentMap);
        bool reflection = env_add && ((p.flags & scene::kUnlit) || !textured);
        // The course road sheen: GT3's specular-only VU program (scene::Material::specular_local).
        bool sheen = p.material.valid && p.material.specular_local && !(p.flags & scene::kUnlit);
        fu.params[2] = reflection ? opt.reflection_strength
                       : (p.flags & scene::kEnvironmentMap) ? opt.env_strength
                                                            : 0.0f;
        fu.params[3] = reflection ? 3.0f : sheen ? 2.0f : 0.0f;
        // Prize scenes (.prm, DrawItem::sphere_map): a textured kEnvironmentMap primitive
        // carries its own sphere map, sampled from the view-space normal.
        // Course environment passes (kinds 2 and 3): UVs and colour weight per vertex
        // (model.vert), then texture x vertex colour like any unlit packet.
        const bool vertex_env = v.item->sphere_map && (p.flags & scene::kEnvironmentMap) && textured && p.env_kind != 0;
        if (vertex_env) {
            fu.params[1] = 1.0f, fu.params[2] = 0.0f, fu.params[3] = 0.0f;
        } else if (v.item->sphere_map && (p.flags & scene::kEnvironmentMap) && textured) {
            fu.params[3] = 4.0f;
            // Additive layers (GS ALPHA 0x58: Cs * Ad + Cd) are weighted by the frame
            // buffer alpha the base pass wrote (1.0 on these models), not by the texel alpha.
            fu.params[2] = (p.flags & scene::kAdditive) ? 1.0f : 0.0f;
        }
        if (const ItemLights* il = v.item->lights) {
            float* dirs[4] = {fu.light_dir, fu.light2_dir, fu.light3_dir, fu.light4_dir};
            float* difs[4] = {fu.light_diffuse, fu.light2_diffuse, fu.light3_diffuse, fu.light4_diffuse};
            float* spes[4] = {fu.light_specular, fu.light2_specular, fu.light3_specular, fu.light4_specular};
            for (int k = 0; k < 4; ++k) {
                bool on = k < il->count;
                for (int i = 0; i < 3; ++i) {
                    dirs[k][i] = on ? il->dir[k][i] : (k == 0 ? 0.0f : 0.0f);
                    difs[k][i] = on ? il->diffuse[k][i] * v.item->light_scale[1] : 0.0f;
                    spes[k][i] = on ? il->specular[k][i] * v.item->light_scale[2] : 0.0f;
                }
                if (k == 0 && !on) dirs[0][1] = 1; // keep a valid direction
                if (k > 0) dirs[k][3] = on ? 1.0f : 0.0f;
            }
            for (int i = 0; i < 3; ++i) fu.light_ambient[i] = il->ambient[i] * v.item->light_scale[0];
        } else {
            for (int i = 0; i < 3; ++i) {
                fu.light_ambient[i] = opt.light_ambient[i] * v.item->light_scale[0];
                fu.light_diffuse[i] = opt.light_diffuse[i] * v.item->light_scale[1];
                fu.light_specular[i] = opt.light_specular[i] * v.item->light_scale[2];
            }
            Vec3 ld = normalize(opt.light_dir);
            fu.light_dir[0] = ld.x, fu.light_dir[1] = ld.y, fu.light_dir[2] = ld.z;
            float* dirs[3] = {fu.light2_dir, fu.light3_dir, fu.light4_dir};
            float* difs[3] = {fu.light2_diffuse, fu.light3_diffuse, fu.light4_diffuse};
            float* spes[3] = {fu.light2_specular, fu.light3_specular, fu.light4_specular};
            for (int k = 0; k < 3; ++k) {
                bool on = k < opt.fill_count;
                for (int i = 0; i < 3; ++i) { // GT3 road shade scales every car light
                    dirs[k][i] = opt.fill_dir[k][i];
                    difs[k][i] = opt.fill_diffuse[k][i] * v.item->light_scale[1];
                    spes[k][i] = opt.fill_specular[k][i] * v.item->light_scale[2];
                }
                dirs[k][3] = on ? 1.0f : 0.0f;
            }
        }
        if (reflection && !vertex_env) fu.params[2] *= v.item->reflection_scale;
        fu.env_c0[3] = p.env_rows ? 1.0f : 0.0f;
        for (int i = 0; i < 3; ++i) fu.env_c0[i] = p.env_c0[i], fu.env_c1[i] = p.env_c1[i];
        const scene::Material& mt = p.material;
        fu.mat_ambient[3] = mt.valid ? 1.0f : 0.0f;
        if (mt.valid) {
            for (int i = 0; i < 3; ++i) {
                fu.mat_ambient[i] = mt.ambient[i], fu.mat_diffuse[i] = mt.diffuse[i];
                fu.mat_specular[i] = mt.specular[i], fu.mat_emission[i] = mt.emission[i] + mt.lamp_emission[i] * v.item->lamp;
            }
            fu.mat_diffuse[3] = mt.scale;
            // The GS uses the lit colour as is without a texture (255 = 1.0) and as a MODULATE
            // factor with one (0x80 = 1.0): untextured packets show it at 128/255.
            fu.mat_emission[3] = textured ? 1.0f : 128.0f / 255.0f;
            // GT3's VU kernels quantise the exponent: shininess < 6 -> 4, < 12 -> 8, else 16. The
            // sheen's program is picked from the shininess alone (some course materials have
            // Ms = 0 and a shininess of ~0: p 4, adding nothing).
            bool spec = mt.specular[0] > 0 || mt.specular[1] > 0 || mt.specular[2] > 0;
            fu.mat_specular[3] = !spec && !sheen ? 0.0f : mt.shininess < 6 ? 4.0f : mt.shininess < 12 ? 8.0f : 16.0f;
        }
        fu.fog_color[3] = opt.fog && !background && !v.item->helper && !v.item->no_fog ? 1.0f : 0.0f;
        // GT3's wheel spin blurs: the item's textured primitives only.
        fu.wheel_blur[0] = textured ? float(v.item->wheel_blur) : 0.0f;
        fu.wheel_blur[1] = v.item->wheel_blur_lod;
        for (int i = 0; i < 4; ++i) fu.wheel_steps[i] = v.item->wheel_steps[size_t(i)];
        fu.wheel_blur[3] = key.spin_copy ? float(v.cells) : 0.0f;
        fu.tint[3] = std::max(v.item->alpha, 0.0f); // > 1 (blended items): the blend clamps
        fu.tint[0] = fu.tint[1] = fu.tint[2] = v.item->tint;
        const bool guard = guard_on_ && v.item->guard_band;
        fu.output[1] = guard ? 1.0f : 0.0f; // model.vert sends the colour times w
        fu.output[2] = v.item->texture && v.item->gs_bilinear ? 1.0f : 0.0f;
        fu.output[3] = (p.flags & scene::kDestAlphaTest) ? 1.0f : 0.0f;
        // GT3's lighting runs per vertex (the VU), with the infinite viewer: the lights and
        // material go to the vertex shader. The sheen's program has a local viewer instead:
        // GT3's light model viewer (0, 2, 0) in eye space, 2 m along the camera's up axis.
        const bool vertex_lit = mt.valid && fu.params[1] < 0.5f && (fu.params[3] < 0.5f || sheen);
        const bool color_2x = (p.flags & scene::kVertexColor2x) != 0;
        if (vertex_lit || vertex_env || color_2x || vl_on) {
            VertexLighting vl{};
            vl.vertex_color[0] = color_2x ? 1.0f : 0.0f;
            if (vertex_env) {
                // The VU's environment programs read the camera's position in object space
                // through a rigid inverse of the modelview (the backdrop: scale s, so s^2 x camera).
                const float s = v.item->modelview_scale;
                for (int i = 0; i < 3; ++i) vl.eye_dir[i] = s * s * fu.camera_pos[i];
                vl.eye_dir[3] = p.env_kind == 3 ? 4.0f : 3.0f;
                for (int i = 0; i < 3; ++i) vl.env_c0[i] = p.env_c0[i], vl.env_c1[i] = p.env_c1[i];
                vl.env_c0[3] = p.env_alpha[0], vl.env_c1[3] = p.env_alpha[1];
                if (p.env_kind == 2) {
                    for (int r = 0; r < 3; ++r)
                        for (int i = 0; i < 3; ++i) vl.env_rows[r][i] = s * view.at(r, i);
                } else {
                    // Kind 3: yawed by 45 (g + 1) degrees about y, g from the sign of the summed
                    // components of the op 0x31 point minus the camera, or of the camera's forward
                    // (GT3 sums them instead of rotating them; 0 when the sum is positive, else 4).
                    Vec3 d = p.env_point ? Vec3{p.env_point_pos[0] - fu.camera_pos[0], p.env_point_pos[1] - fu.camera_pos[1],
                                                p.env_point_pos[2] - fu.camera_pos[2]}
                                         : Vec3{-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)};
                    const float g = d.x + d.y + d.z > 0 ? 0.0f : 4.0f;
                    const float phi = 0.78539816f * (g + 1.0f), c = std::cos(phi), sn = std::sin(phi);
                    const float rows[3][3] = {{c, 0, -sn}, {0, 1, 0}, {sn, 0, c}};
                    for (int r = 0; r < 3; ++r)
                        for (int i = 0; i < 3; ++i) vl.env_rows[r][i] = rows[r][i];
                    vl.env_pano[0] = 0.875f - (g + 1.0f) / 8.0f;
                    vl.env_pano[1] = opt.panorama[0] / 6.0f;
                    vl.env_pano[2] = 0.5f + opt.panorama[1];
                }
            } else if (vertex_lit) {
                for (int i = 0; i < 3; ++i) vl.eye_dir[i] = view.at(2, i);
                vl.eye_dir[3] = 1;
                if (sheen) {
                    constexpr float kViewerUp = 2.0f; // GT3's light model viewer (0, 2, 0) in eye space
                    const float s = v.item->modelview_scale;
                    for (int i = 0; i < 3; ++i) vl.eye_dir[i] = s * s * fu.camera_pos[i] + kViewerUp * s * view.at(1, i);
                    vl.eye_dir[3] = 2;
                }
                const float* dirs[4] = {fu.light_dir, fu.light2_dir, fu.light3_dir, fu.light4_dir};
                const float* difs[4] = {fu.light_diffuse, fu.light2_diffuse, fu.light3_diffuse, fu.light4_diffuse};
                const float* spes[4] = {fu.light_specular, fu.light2_specular, fu.light3_specular, fu.light4_specular};
                for (int k = 0; k < 4; ++k) {
                    for (int i = 0; i < 3; ++i) {
                        vl.light_dir[k][i] = dirs[k][i];
                        vl.light_diffuse[k][i] = difs[k][i];
                        vl.light_specular[k][i] = spes[k][i];
                    }
                    vl.light_dir[k][3] = k == 0 ? 1.0f : dirs[k][3]; // light 0 is always on
                }
                std::copy(std::begin(fu.light_ambient), std::end(fu.light_ambient), vl.light_ambient);
                std::copy(std::begin(fu.mat_ambient), std::end(fu.mat_ambient), vl.mat_ambient);
                std::copy(std::begin(fu.mat_diffuse), std::end(fu.mat_diffuse), vl.mat_diffuse);
                std::copy(std::begin(fu.mat_specular), std::end(fu.mat_specular), vl.mat_specular);
                std::copy(std::begin(fu.mat_emission), std::end(fu.mat_emission), vl.mat_emission);
            }
            push_lighting(vl);
            vl_on = vertex_lit || vertex_env || color_2x;
        }
        push_fragment(fu);
        const u32 first = v.mesh->index_offset + p.first_index;
        const u32 instances = u32(std::max(v.copies, 1));
        if (pending.count > 0 && pending.first + pending.count == first &&
            pending.vertex_offset == v.mesh->vertex_offset && pending.instances == instances)
            pending.count += p.index_count;
        else {
            flush();
            pending = {first, p.index_count, v.mesh->vertex_offset, instances};
        }
        stats_.triangles += p.index_count / 3 * instances;
        // The triangles GT3 clips at the guard band: their pieces (upload_clipped), in place of the
        // ones the draw above leaves out (model.vert).
        if (!guard) return;
        const auto it = clipped_.find({v.item, v.mesh, size_t(&p - v.mesh->primitives.data())});
        if (it == clipped_.end() || it->second.count == 0) return;
        PipelineKey clip_key = key;
        clip_key.clipped = true;
        if (!bind(clip_key)) return;
        flush();
        SDL_GPUBufferBinding vb{clip_.vb, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_GPUBufferBinding ib{clip_.ib, 0};
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        bound_model = nullptr; // the next mesh binds its own buffers again
        SDL_DrawGPUIndexedPrimitives(pass, it->second.count, 1, it->second.first, 0, 0);
        ++stats_.draw_calls;
    };
    // (Depth-only passes too: they follow the blended passes before them in model order.)
    auto prim_blended = [](const scene::Primitive& p) {
        return (p.flags & (scene::kAlphaBlend | scene::kAdditive | scene::kMultiplyDest | scene::kAlphaOnly |
                           scene::kDepthOnly)) != 0;
    };
    auto split = [](const scene::Primitive& p) {
        return (p.flags & (scene::kAlphaFailBlend | scene::kAlphaTest)) == (scene::kAlphaFailBlend | scene::kAlphaTest);
    };
    bool solid = opt.wire != WireMode::Only;

    // Background (sky, backdrop): everything in model order, no depth writes.
    for (const Visible& v : vis)
        if (v.item->background && solid)
            for (const scene::Primitive& p : v.mesh->primitives) draw_prim(v, p);
    auto draw_blended = [&](const Visible& v) {
        for (const scene::Primitive& p : v.mesh->primitives)
            if (prim_blended(p) || v.item->alpha < 1.0f) draw_prim(v, p, split(p) && v.item->alpha >= 1.0f ? 2 : 0);
    };
    // Opaque and alpha-tested, item by item; an in-order item's blended primitives follow its
    // opaque ones, in model order, before the next item's opaque ones (DrawItem::in_order_join
    // items count as part of the in-order item before them).
    // DrawItem::depth_reset: the far plane's depth over the viewport (GT3's Z-only sprite after
    // its backdrop), the colour and alpha untouched.
    auto reset_depth = [&] {
        if (!quad_vb_ || !quad_ib_) return;
        PipelineKey key;
        key.cull = CullMode::None;
        key.depth_write = false;
        key.depth_reset = true;
        if (!bind(key)) return;
        flush();
        SDL_GPUBufferBinding vb{quad_vb_, 0}, ib{quad_ib_, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
        SDL_BindGPUIndexBuffer(pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        bound_model = nullptr; // the next mesh binds its own buffers again
        VertexUniforms vu{};
        const Mat4 id;
        std::copy(id.m.begin(), id.m.end(), vu.mvp);
        std::copy(id.m.begin(), id.m.end(), vu.model);
        std::copy(id.m.begin(), id.m.end(), vu.normal_mat);
        SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));
        pushed = nullptr; // (the next mesh pushes its own)
        const SDL_GPUTextureSamplerBinding sbs[3] = {{ctx_.white_texture(), ctx_.sampler(true, true)},
                                                     {placeholder_cube(), ctx_.sampler(true, true)},
                                                     {ctx_.white_texture(), ctx_.sampler(true, true)}};
        bind_samplers(sbs);
        FragmentUniforms fu{};
        fu.params[0] = -1; // no alpha test
        push_fragment(fu);
        SDL_DrawGPUIndexedPrimitives(pass, 6, 1, 0, 0, 0);
        ++stats_.draw_calls;
    };
    auto same_run = [&](size_t i) { // vis[i] belongs to the run of vis[i - 1]
        const DrawItem* a = vis[i - 1].item;
        const DrawItem* b = vis[i].item;
        return a == b || (a->in_order && b->in_order && b->in_order_join);
    };
    size_t run = 0; // first entry of the current item (or joined items)
    for (size_t i = 0; i < vis.size(); ++i) {
        const Visible& v = vis[i];
        if (i > 0 && !same_run(i)) run = i;
        if (v.item->background || !(solid || v.item->helper)) continue;
        for (const scene::Primitive& p : v.mesh->primitives) {
            if (v.item->alpha < 1.0f) continue;
            if (!prim_blended(p)) draw_prim(v, p);
            else if (split(p)) draw_prim(v, p, 1);
        }
        if (v.item->in_order && (i + 1 == vis.size() || !same_run(i + 1))) {
            for (size_t k = run; k <= i; ++k) draw_blended(vis[k]);
            if (v.item->depth_reset) reset_depth();
        }
    }
    // The other blended primitives: model order, or back to front by mesh distance.
    std::vector<const Visible*> blended;
    for (const Visible& v : vis)
        if (!v.item->background && !v.item->in_order && (solid || v.item->helper))
            for (const scene::Primitive& p : v.mesh->primitives)
                if (prim_blended(p) || v.item->alpha < 1.0f) {
                    blended.push_back(&v);
                    break;
                }
    if (opt.sort_blended)
        std::stable_sort(blended.begin(), blended.end(),
                         [](const Visible* a, const Visible* b) { return a->dist > b->dist; });
    for (const Visible* v : blended) draw_blended(*v);
    flush();
    // Wireframe.
    if (opt.wire != WireMode::Off)
        for (const Visible& v : vis) {
            if (v.item->helper || v.item->background || !v.mesh->line_count) continue;
            if (!bind({BlendMode::Opaque, CullMode::None, true, true})) continue;
            use_mesh(v, true);
            SDL_GPUTextureSamplerBinding sbs[3] = {{ctx_.white_texture(), ctx_.sampler(false, false)},
                                                   {env_tex, ctx_.sampler(true, true)}, sphere_sb};
            bind_samplers(sbs);
            fu.params[0] = -1, fu.params[1] = 1, fu.params[2] = 0, fu.params[3] = 1;
            fu.fog_color[3] = 0;
            push_fragment(fu);
            SDL_DrawGPUIndexedPrimitives(pass, v.mesh->line_count, 1, v.mesh->index_offset + v.mesh->line_first,
                                         v.mesh->vertex_offset, 0);
            ++stats_.draw_calls;
        }
}

void View3D::capture_from_env() {
    const char* e = SDL_getenv("GT3R_CAPTURE3D");
    if (!e || capture_.count > 0) return;
    std::string s = e;
    size_t a = s.find(','), b = a == std::string::npos ? a : s.find(',', a + 1);
    if (b == std::string::npos) return;
    capture_.first = std::atoi(s.substr(0, a).c_str());
    capture_.count = std::max(0, std::atoi(s.substr(a + 1, b - a - 1).c_str()));
    capture_.path = s.substr(b + 1);
}

void View3D::capture_frame(SDL_GPUCommandBuffer* cmd) {
    int k = capture_.frame++ - capture_.first;
    if (k < 0 || k >= capture_.count) return;
    if (k == 0) capture_.w = used_w_, capture_.h = used_h_;
    if (used_w_ != capture_.w || used_h_ != capture_.h) return; // one size per capture
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = GpuContext::kColorFormat;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = u32(used_w_);
    ti.height = u32(used_h_);
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    SDL_GPUTexture* t = SDL_CreateGPUTexture(ctx_.device(), &ti);
    capture_.frames.push_back(t);
    if (!t) return;
    SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureLocation src{}, dst{};
    src.texture = color_;
    dst.texture = t;
    SDL_CopyGPUTextureToTexture(cp, &src, &dst, u32(used_w_), u32(used_h_), 1, false);
    SDL_EndGPUCopyPass(cp);
}

void View3D::write_capture() {
    SDL_GPUDevice* dev = ctx_.device();
    SDL_WaitForGPUIdle(dev);
    u32 w = u32(capture_.w), h = u32(capture_.h);
    for (size_t k = 0; k < capture_.frames.size(); ++k) {
        SDL_GPUTexture* t = capture_.frames[k];
        if (!t) continue;
        SDL_GPUTransferBufferCreateInfo bi{};
        bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        bi.size = w * h * 4;
        SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(dev, &bi);
        SDL_GPUCommandBuffer* cmd = tb ? SDL_AcquireGPUCommandBuffer(dev) : nullptr;
        if (cmd) {
            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTextureRegion src{};
            src.texture = t;
            src.w = w, src.h = h, src.d = 1;
            SDL_GPUTextureTransferInfo dst{};
            dst.transfer_buffer = tb;
            SDL_DownloadFromGPUTexture(cp, &src, &dst);
            SDL_EndGPUCopyPass(cp);
            SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            if (f) {
                SDL_WaitForGPUFences(dev, true, &f, 1);
                SDL_ReleaseGPUFence(dev, f);
            }
            std::vector<u32> px(size_t(w) * h);
            if (void* p = SDL_MapGPUTransferBuffer(dev, tb, false)) {
                std::memcpy(px.data(), p, px.size() * 4);
                SDL_UnmapGPUTransferBuffer(dev, tb);
            }
            for (u32& c : px) c |= 0xFF000000u; // texel alpha is reflectivity
            std::string path = capture_.path;
            size_t dot = path.rfind('.');
            std::string suffix = "-" + std::to_string(k);
            path = dot == std::string::npos ? path + suffix : path.substr(0, dot) + suffix + path.substr(dot);
            if (write_host_file(path, encode_png(w, h, px))) LOG_INFO("3D capture saved: %s", path.c_str());
        }
        if (tb) SDL_ReleaseGPUTransferBuffer(dev, tb);
        SDL_ReleaseGPUTexture(dev, t);
    }
    capture_ = {};
}

SDL_GPUTexture* View3D::placeholder_cube() {
    if (placeholder_cube_) return placeholder_cube_;
    SDL_GPUDevice* dev = ctx_.device();
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_CUBE;
    ti.format = GpuContext::kColorFormat;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = ti.height = 1;
    ti.layer_count_or_depth = 6;
    ti.num_levels = 1;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    placeholder_cube_ = SDL_CreateGPUTexture(dev, &ti);
    if (!placeholder_cube_) return nullptr;
    if (SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev)) {
        for (u32 f = 0; f < 6; ++f) {
            SDL_GPUColorTargetInfo ct{};
            ct.texture = placeholder_cube_;
            ct.layer_or_depth_plane = f;
            ct.clear_color = {0.5f, 0.5f, 0.5f, 1.0f};
            ct.load_op = SDL_GPU_LOADOP_CLEAR;
            ct.store_op = SDL_GPU_STOREOP_STORE;
            SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr));
        }
        SDL_SubmitGPUCommandBuffer(cmd);
    }
    return placeholder_cube_;
}

void View3D::release_offscreen() {
    for (Offscreen* p : {&offscreen_[0], &offscreen_[1], &offscreen_[2], &offscreen_[3], &direct_}) {
        Offscreen& o = *p;
        for (SDL_GPUTexture** t : {&o.color, &o.msaa, &o.depth}) {
            if (*t) SDL_ReleaseGPUTexture(ctx_.device(), *t);
            *t = nullptr;
        }
        o.w = o.h = o.samples = 0;
    }
}

SDL_GPUTexture* View3D::render_offscreen(int slot, int w, int h, const Mat4& view, const Mat4& proj, const Vec3& eye,
                                         const std::vector<DrawItem>& items, const DrawOptions& opt) {
    if (slot < 0 || slot >= 4 || w <= 0 || h <= 0) return nullptr;
    Offscreen& o = offscreen_[slot];
    SDL_GPUDevice* dev = ctx_.device();
    if (!o.color || o.w != w || o.h != h || o.samples != ctx_.msaa()) {
        for (SDL_GPUTexture** t : {&o.color, &o.msaa, &o.depth}) {
            if (*t) SDL_ReleaseGPUTexture(dev, *t);
            *t = nullptr;
        }
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = GpuContext::kColorFormat;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width = u32(w), ti.height = u32(h);
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
        o.color = SDL_CreateGPUTexture(dev, &ti);
        // The scene pipelines are built for the context's sample count.
        if (ctx_.sample_count() != SDL_GPU_SAMPLECOUNT_1) {
            ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
            ti.sample_count = ctx_.sample_count();
            o.msaa = SDL_CreateGPUTexture(dev, &ti);
        }
        ti.format = ctx_.depth_format();
        ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        ti.sample_count = ctx_.sample_count();
        o.depth = SDL_CreateGPUTexture(dev, &ti);
        o.w = w, o.h = h, o.samples = ctx_.msaa();
        if (!o.color || !o.depth || (ctx_.sample_count() != SDL_GPU_SAMPLECOUNT_1 && !o.msaa)) {
            LOG_WARN("Offscreen buffer %dx%d: %s", w, h, SDL_GetError());
            return nullptr;
        }
    }
    if (has_depth_reset(items)) ensure_quad();
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) return nullptr;
    SDL_GPUColorTargetInfo ct{};
    ct.clear_color = {opt.clear[0], opt.clear[1], opt.clear[2], opt.clear[3]};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    // The previous frame's commands may still sample the buffer: cycle it.
    if (o.msaa) {
        ct.texture = o.msaa;
        ct.store_op = SDL_GPU_STOREOP_RESOLVE;
        ct.resolve_texture = o.color;
        ct.cycle = true;
        ct.cycle_resolve_texture = true;
    } else {
        ct.texture = o.color;
        ct.store_op = SDL_GPU_STOREOP_STORE;
        ct.cycle = true;
    }
    SDL_GPUDepthStencilTargetInfo dt{};
    dt.texture = o.depth;
    dt.clear_depth = kFarDepth;
    dt.load_op = SDL_GPU_LOADOP_CLEAR;
    dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.cycle = true;
    DrawStats keep = stats_;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    encode(cmd, pass, view, proj, eye, items, opt);
    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    stats_ = keep;
    return o.color;
}

void View3D::release_cube() {
    SDL_GPUDevice* dev = ctx_.device();
    for (SDL_GPUTexture** t : {&cube_, &cube_depth_, &cube_msaa_[0], &cube_msaa_[1], &cube_msaa_[2], &cube_msaa_[3],
                               &cube_msaa_[4], &cube_msaa_[5]}) {
        if (*t) SDL_ReleaseGPUTexture(dev, *t);
        *t = nullptr;
    }
}

bool View3D::ensure_cube(int size) {
    if (cube_ && size == cube_size_ && cube_samples_ == ctx_.msaa()) return true;
    SDL_GPUDevice* dev = ctx_.device();
    release_cube();
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_CUBE;
    ti.format = GpuContext::kColorFormat;
    ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = ti.height = u32(size);
    ti.layer_count_or_depth = 6;
    ti.num_levels = 1;
    ti.sample_count = SDL_GPU_SAMPLECOUNT_1;
    cube_ = SDL_CreateGPUTexture(dev, &ti);
    // The scene pipelines are built for the context's sample count: render each face at it.
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.layer_count_or_depth = 1;
    bool msaa_ok = true;
    if (ctx_.sample_count() != SDL_GPU_SAMPLECOUNT_1) {
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        ti.sample_count = ctx_.sample_count();
        for (SDL_GPUTexture*& t : cube_msaa_) msaa_ok = (t = SDL_CreateGPUTexture(dev, &ti)) && msaa_ok;
    }
    ti.format = ctx_.depth_format();
    ti.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    ti.sample_count = ctx_.sample_count();
    cube_depth_ = SDL_CreateGPUTexture(dev, &ti);
    if (!cube_ || !cube_depth_ || !msaa_ok) {
        LOG_WARN("Environment cube %d creation failed: %s", size, SDL_GetError());
        release_cube();
        return false;
    }
    cube_size_ = size;
    cube_samples_ = ctx_.msaa();
    return true;
}

SDL_GPUTexture* View3D::render_env_cube(const Vec3& pos, const std::vector<DrawItem>& items, const DrawOptions& opt,
                                        int size, unsigned faces) {
    if (!ensure_cube(size)) return nullptr;
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx_.device());
    if (!cmd) return nullptr;
    // Standard cube-map face frames (direction, up), as the sampler addresses them: texel row 0
    // of a side face is +Y, of +Y / -Y it is -Z / +Z. SDL_GPU stores a target's top row (NDC +Y)
    // first, so that direction is each face camera's up (OpenGL's table negates them for its
    // bottom-up rows; with the x mirror below it turned every face half a turn).
    static const Vec3 kDir[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const Vec3 kUp[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
    // Cube faces are seen from inside, left-handed: mirror x in clip space.
    Mat4 proj = perspective(1.5707963f, 1.0f, 0.1f, std::max(opt.draw_distance, 10.0f) * 1.5f);
    for (int c = 0; c < 4; ++c) proj.at(0, c) = -proj.at(0, c);
    DrawStats keep = stats_;
    DrawOptions o = opt;
    o.env_cube = nullptr; // no reflections of reflections
    o.mirrored = !opt.mirrored;
    // The previous frame's command buffer may still be sampling the cube: a full refresh cycles
    // it (SDL_GPU: overwriting a texture a pending command references is undefined), at its first
    // face only - cycling undefines the whole cube. A partial refresh keeps the other faces.
    bool cycle = (faces & 0x3Fu) == 0x3Fu;
    for (int f = 0; f < 6; ++f) {
        if (!(faces & (1u << f))) continue;
        SDL_GPUColorTargetInfo ct{};
        ct.clear_color = {opt.clear[0], opt.clear[1], opt.clear[2], 1.0f};
        ct.load_op = SDL_GPU_LOADOP_CLEAR;
        if (cube_msaa_[f]) {
            ct.texture = cube_msaa_[f];
            ct.store_op = SDL_GPU_STOREOP_RESOLVE;
            ct.resolve_texture = cube_;
            ct.resolve_layer = u32(f);
            ct.cycle = true;
            ct.cycle_resolve_texture = cycle;
        } else {
            ct.texture = cube_;
            ct.layer_or_depth_plane = u32(f);
            ct.store_op = SDL_GPU_STOREOP_STORE;
            ct.cycle = cycle;
        }
        cycle = false;
        SDL_GPUDepthStencilTargetInfo dt{};
        dt.texture = cube_depth_;
        dt.clear_depth = kFarDepth;
        dt.load_op = SDL_GPU_LOADOP_CLEAR;
        dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
        dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        dt.cycle = true;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
        encode(cmd, pass, look_at(pos, pos + kDir[f], kUp[f]), proj, pos, items, o);
        SDL_EndGPURenderPass(pass);
    }
    SDL_SubmitGPUCommandBuffer(cmd);
    stats_ = keep;
    return cube_;
}

} // namespace gt3r::render
