#include "render/view3d.h"

#include "base/file.h"
#include "base/log.h"
#include "formats/png_writer.h"
#include "platform/render_scale.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#define GT3R_HAVE_GPU_RENDERER SDL_VERSION_ATLEAST(3, 4, 0)

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
    float fog[4];    // GT3 fog: x distance A, y 1 - minimum visibility, z = 1 on
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
};
struct FragmentUniforms {
    float light_dir[4];
    float camera_pos[4];
    float params[4]; // alpha ref, unlit, env strength, flat colour
    float flat_color[4];
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
    float output[4]; // x = 1: quantise the colour to 5 bits per channel (DrawOptions::quantize_rgb5)
};

} // namespace

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

View3D::~View3D() {
    release_target();
    if (quad_vb_) SDL_ReleaseGPUBuffer(ctx_.device(), quad_vb_);
    if (quad_ib_) SDL_ReleaseGPUBuffer(ctx_.device(), quad_ib_);
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
    for (SDL_GPUTexture** t : {&color_, &msaa_, &depth_, &scene_, &last_, &snap_, &half_}) {
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
    wrapper_ = wrap(renderer, color_, w, h);
    if (!wrapper_) {
        release_target();
        return false;
    }

    width_ = w;
    height_ = h;
    samples_ = ctx_.msaa();
    return true;
}

// `texture` (w x h) as an SDL_Renderer texture: replaces what is behind, scaled linearly.
SDL_Texture* View3D::wrap(SDL_Renderer* renderer, SDL_GPUTexture* texture, int w, int h) {
    SDL_Texture* t = nullptr;
#if GT3R_HAVE_GPU_RENDERER
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_ABGR8888);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, h);
    SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, texture);
    t = SDL_CreateTextureWithProperties(renderer, props);
    SDL_DestroyProperties(props);
#else
    (void)renderer, (void)texture, (void)w, (void)h;
#endif
    if (!t) {
        LOG_WARN("Could not wrap the 3D target for the 2D renderer: %s", SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR); // render_scale < 1 upscales, > 1 scales down
    return t;
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
    half_wrapper_ = wrap(renderer, half_, w, h);
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
    const bool post = post_wanted(opt) && ensure_post(&scene_);
    SDL_GPUTexture* scene_target = post ? scene_ : color_;

    SDL_GPUColorTargetInfo ct{};
    ct.clear_color = {opt.clear[0], opt.clear[1], opt.clear[2], opt.clear[3]};
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.cycle = cycle;
    if (msaa_) {
        ct.texture = msaa_;
        ct.store_op = SDL_GPU_STOREOP_RESOLVE;
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
    dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    dt.cycle = cycle;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &dt);
    SDL_SetGPUViewport(pass, &used_viewport);
    SDL_SetGPUScissor(pass, &used_rect);
    frame_alpha_written_ = false;
    encode(cmd, pass, view, proj, eye, items, opt);
    SDL_EndGPURenderPass(pass);
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
        pass = SDL_BeginGPURenderPass(cmd, &pt, 1, &pd);
        encode_post(cmd, pass, scene_, pw, ph, 1.0f, opt.post_grade.on ? &opt.post_grade : nullptr);
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
    if (!quad_vb_) {
        // Clip-space corners (an identity transform), texture rows from the top (SDL_GPU).
        scene::Vertex v[4] = {};
        const float xy[4][2] = {{-1, 1}, {1, 1}, {-1, -1}, {1, -1}}, uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        for (int i = 0; i < 4; ++i) {
            v[i].pos[0] = xy[i][0], v[i].pos[1] = xy[i][1], v[i].pos[2] = 0;
            v[i].normal[2] = 1;
            v[i].uv[0] = uv[i][0], v[i].uv[1] = uv[i][1];
        }
        const u32 idx[6] = {0, 2, 1, 1, 2, 3};
        Uploader up(ctx_.device());
        quad_vb_ = up.buffer(SDL_GPU_BUFFERUSAGE_VERTEX, v, sizeof(v));
        quad_ib_ = up.buffer(SDL_GPU_BUFFERUSAGE_INDEX, idx, sizeof(idx));
        up.submit();
        if (!quad_vb_ || !quad_ib_) return;
    }
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
    encode(cmd, pass, view, proj, eye, items, opt);
    SDL_EndGPURenderPass(pass);
    return SDL_SubmitGPUCommandBuffer(cmd);
}

void View3D::encode(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass, const Mat4& view, const Mat4& proj,
                    const Vec3& eye, const std::vector<DrawItem>& items, const DrawOptions& opt) {
    const Mat4 view_proj = proj * view;
    // Wireframe lines are pulled slightly towards the camera (to a larger depth) to avoid z-fighting.
    Mat4 line_proj = proj;
    for (int c = 0; c < 4; ++c) line_proj.at(2, c) += 1e-4f * proj.at(3, c);
    const Mat4 line_view_proj = line_proj * view;

    // Frustum planes (clip depth 0..w, far..near): w+x, w-x, w+y, w-y, z, w-z.
    float planes[6][4];
    for (int i = 0; i < 4; ++i) {
        float r0 = view_proj.at(0, i), r1 = view_proj.at(1, i), r2 = view_proj.at(2, i), r3 = view_proj.at(3, i);
        planes[0][i] = r3 + r0, planes[1][i] = r3 - r0, planes[2][i] = r3 + r1;
        planes[3][i] = r3 - r1, planes[4][i] = r2, planes[5][i] = r3 - r2;
    }
    for (auto& p : planes) {
        float l = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (l > 0)
            for (float& v : p) v /= l;
    }

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
    };
    std::vector<Visible> vis;
    stats_ = {};
    for (const DrawItem& item : items) {
        const GpuModel* model = item.model;
        if (!model || !model->valid()) continue;
        int lod = item.lod == -2 ? opt.lod : item.lod;
        float max_dist = item.draw_distance >= 0 ? item.draw_distance : opt.draw_distance;
        for (const GpuModel::MeshInfo& mesh : model->meshes()) {
            if (lod >= 0 && mesh.lod != lod) continue;
            Mat4 world = item.transform * mesh.transform;
            Vec3 c = world.transform_point(mesh.center);
            float sc = 0;
            for (int col = 0; col < 3; ++col)
                sc = std::max(sc, length(Vec3{world.at(0, col), world.at(1, col), world.at(2, col)}));
            float r = mesh.radius * sc;
            float dist = length(c - eye);
            if (!item.background && !item.helper) {
                bool culled = max_dist > 0 && dist - r > max_dist;
                if (opt.frustum_cull && !culled)
                    for (const auto& p : planes)
                        if (p[0] * c.x + p[1] * c.y + p[2] * c.z + p[3] < -r) {
                            culled = true;
                            break;
                        }
                if (culled) {
                    ++stats_.meshes_culled;
                    continue;
                }
            }
            ++stats_.meshes_drawn;
            vis.push_back({&item, &mesh, world, dist});
        }
    }

    SDL_GPUGraphicsPipeline* bound = nullptr;
    auto bind = [&](const PipelineKey& key) {
        SDL_GPUGraphicsPipeline* p = ctx_.pipeline(key);
        if (p && p != bound) SDL_BindGPUGraphicsPipeline(pass, p);
        if (p) bound = p;
        return p != nullptr;
    };
    const GpuModel* bound_model = nullptr;
    const Visible* pushed = nullptr;
    bool pushed_lines = false;
    auto use_mesh = [&](const Visible& v, bool lines) {
        const GpuModel* model = v.item->model;
        if (model != bound_model) {
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
        SDL_PushGPUVertexUniformData(cmd, 0, &vu, sizeof(vu));
        pushed = &v;
        pushed_lines = lines;
    };
    // half: kAlphaFailBlend primitives are drawn in two halves: 1 = fragments passing the
    // alpha test (with depth write, in the opaque phase), 2 = the failing ones (blended,
    // no depth write, in the blended phase). 0 = whole primitive.
    // Per-vertex lighting off until a primitive needs it (slot 1 of the vertex uniforms).
    {
        VertexLighting off{};
        SDL_PushGPUVertexUniformData(cmd, 1, &off, sizeof(off));
    }
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
        bool faded = v.item->alpha < 1.0f;
        if (faded && key.blend == BlendMode::Opaque) key.blend = BlendMode::Alpha;
        if (v.item->dest_alpha_blend && key.blend != BlendMode::Opaque)
            key.blend = v.item->dest_alpha_blend == 1 ? BlendMode::DestAlphaLerp : BlendMode::DestAlphaAdd;
        if (p.flags & scene::kAlphaOnly) {
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
        SDL_BindGPUFragmentSamplers(pass, 0, sbs, 3);
        bool blended_prim = key.blend != BlendMode::Opaque;
        // Transparent pictures (composited over the 2D frame): opaque primitives cover fully,
        // whatever their texel alpha (which is reflectivity there).
        fu.grade2[3] = (opt.clear[3] < 1.0f && !blended_prim) ? 1.0f : 0.0f;
        fu.params[0] = !(p.flags & scene::kAlphaTest) ? -1.0f
                       : p.alpha_ref >= 0                ? p.alpha_ref
                       : blended_prim                    ? opt.blend_alpha_ref
                                                         : opt.alpha_ref;
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
        fu.fog_color[3] = opt.fog && !background && !v.item->helper ? 1.0f : 0.0f;
        fu.tint[3] = std::max(v.item->alpha, 0.0f); // > 1 (blended items): the blend clamps
        fu.tint[0] = fu.tint[1] = fu.tint[2] = v.item->tint;
        // GT3's lighting runs per vertex (the VU), with the infinite viewer: the lights and
        // material go to the vertex shader. The sheen's program has a local viewer instead:
        // GT3's light model viewer (0, 2, 0) in eye space, 2 m along the camera's up axis.
        const bool vertex_lit = mt.valid && fu.params[1] < 0.5f && (fu.params[3] < 0.5f || sheen);
        if (vertex_lit || vertex_env || vl_on) {
            VertexLighting vl{};
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
            SDL_PushGPUVertexUniformData(cmd, 1, &vl, sizeof(vl));
            vl_on = vertex_lit || vertex_env;
        }
        SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof(fu));
        SDL_DrawGPUIndexedPrimitives(pass, p.index_count, 1, v.mesh->index_offset + p.first_index,
                                     v.mesh->vertex_offset, 0);
        ++stats_.draw_calls;
        stats_.triangles += p.index_count / 3;
    };
    auto prim_blended = [](const scene::Primitive& p) {
        return (p.flags & (scene::kAlphaBlend | scene::kAdditive | scene::kMultiplyDest | scene::kAlphaOnly)) != 0;
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
        if (v.item->in_order && (i + 1 == vis.size() || !same_run(i + 1)))
            for (size_t k = run; k <= i; ++k) draw_blended(vis[k]);
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
    // Wireframe.
    if (opt.wire != WireMode::Off)
        for (const Visible& v : vis) {
            if (v.item->helper || v.item->background || !v.mesh->line_count) continue;
            if (!bind({BlendMode::Opaque, CullMode::None, true, true})) continue;
            use_mesh(v, true);
            SDL_GPUTextureSamplerBinding sbs[3] = {{ctx_.white_texture(), ctx_.sampler(false, false)},
                                                   {env_tex, ctx_.sampler(true, true)}, sphere_sb};
            SDL_BindGPUFragmentSamplers(pass, 0, sbs, 3);
            fu.params[0] = -1, fu.params[1] = 1, fu.params[2] = 0, fu.params[3] = 1;
            fu.fog_color[3] = 0;
            SDL_PushGPUFragmentUniformData(cmd, 0, &fu, sizeof(fu));
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
