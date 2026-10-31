// SDL_GPU on OpenGL ES 3.0; see gpu_gles.h.
//
// Resources map directly onto GL objects. Copy passes run at once. Render passes are
// recorded and run at SDL_EndGPURenderPass: the uniform data pushed for the pass's draws
// goes to the GPU in one upload, and the draws replay with only the state changes between
// them. Every entry point that touches GL first flushes SDL's 2D renderer, which shares the
// context (SDL_FlushRenderer also drops its cached GL state), and leaves the state SDL
// assumes (no depth test or culling, default masks, vertex array 0 and no array buffer, texture
// unit 0, its frame buffer). While the OpenGL ES device is not the one in use, every entry
// point hands the call on to SDL_GPU.

#define GT3R_GLES_IMPL // SDL_GPU by its own names in this file
#include "render/gpu_gles.h"

#if GT3R_GLES

#include "base/log.h"

#define GL_GLES_PROTOTYPES 0 // the functions come from the context (load_gl)
#include <GLES3/gl3.h>       // third_party/khronos

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace {

// The OpenGL ES 3.0 functions this file calls, looked up in the context once it exists
// (SDL_GL_GetProcAddress: the driver's, or ANGLE's libGLESv2).
#define GT3R_GL_FUNCTIONS(X) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBINDBUFFERRANGEPROC, glBindBufferRange) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer) \
    X(PFNGLBINDSAMPLERPROC, glBindSampler) \
    X(PFNGLBINDTEXTUREPROC, glBindTexture) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) \
    X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLCLEARBUFFERFIPROC, glClearBufferfi) \
    X(PFNGLCLEARBUFFERFVPROC, glClearBufferfv) \
    X(PFNGLCOLORMASKPROC, glColorMask) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLCULLFACEPROC, glCullFace) \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers) \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures) \
    X(PFNGLDEPTHFUNCPROC, glDepthFunc) \
    X(PFNGLDEPTHMASKPROC, glDepthMask) \
    X(PFNGLDISABLEPROC, glDisable) \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) \
    X(PFNGLDRAWELEMENTSPROC, glDrawElements) \
    X(PFNGLDRAWELEMENTSINSTANCEDPROC, glDrawElementsInstanced) \
    X(PFNGLENABLEPROC, glEnable) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLFINISHPROC, glFinish) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLFRONTFACEPROC, glFrontFace) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers) \
    X(PFNGLGENSAMPLERSPROC, glGenSamplers) \
    X(PFNGLGENTEXTURESPROC, glGenTextures) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap) \
    X(PFNGLGETERRORPROC, glGetError) \
    X(PFNGLGETFLOATVPROC, glGetFloatv) \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv) \
    X(PFNGLGETINTERNALFORMATIVPROC, glGetInternalformativ) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSTRINGPROC, glGetString) \
    X(PFNGLGETSTRINGIPROC, glGetStringi) \
    X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLINVALIDATEFRAMEBUFFERPROC, glInvalidateFramebuffer) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei) \
    X(PFNGLREADPIXELSPROC, glReadPixels) \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage) \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
    X(PFNGLSAMPLERPARAMETERFPROC, glSamplerParameterf) \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri) \
    X(PFNGLSCISSORPROC, glScissor) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLTEXIMAGE2DPROC, glTexImage2D) \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri) \
    X(PFNGLTEXSTORAGE2DPROC, glTexStorage2D) \
    X(PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
    X(PFNGLVIEWPORTPROC, glViewport)
#define X(type, name) type name = nullptr;
GT3R_GL_FUNCTIONS(X)
#undef X

bool load_gl() {
#define X(type, name)                                                         \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));             \
    if (!name) return SDL_SetError("OpenGL ES: the driver has no %s", #name);
    GT3R_GL_FUNCTIONS(X)
#undef X
    return true;
}

} // namespace

namespace {

constexpr int kSlots = 4;              // uniform buffers per stage; fragment samplers
constexpr GLuint kFragmentUboBase = 4; // uniform binding points: vertex 0..3, fragment 4..7

std::vector<std::string> split_names(const char* s) {
    std::vector<std::string> out;
    if (!s) return out;
    std::string cur;
    for (const char* p = s;; ++p) {
        if (*p == ',' || *p == 0) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            if (*p == 0) break;
        } else if (*p != ' ') {
            cur += *p;
        }
    }
    return out;
}

struct TexFormat {
    GLenum internal = 0, format = 0, type = 0;
    bool depth = false, stencil = false;
};

bool tex_format(SDL_GPUTextureFormat f, TexFormat* out) {
    switch (f) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: *out = {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE}; return true;
    case SDL_GPU_TEXTUREFORMAT_D16_UNORM: *out = {GL_DEPTH_COMPONENT16, 0, 0, true}; return true;
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM: *out = {GL_DEPTH_COMPONENT24, 0, 0, true}; return true;
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT: *out = {GL_DEPTH_COMPONENT32F, 0, 0, true}; return true;
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT: *out = {GL_DEPTH24_STENCIL8, 0, 0, true, true}; return true;
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT: *out = {GL_DEPTH32F_STENCIL8, 0, 0, true, true}; return true;
    default: return false;
    }
}

GLenum blend_factor(SDL_GPUBlendFactor f) {
    switch (f) {
    case SDL_GPU_BLENDFACTOR_ZERO: return GL_ZERO;
    case SDL_GPU_BLENDFACTOR_ONE: return GL_ONE;
    case SDL_GPU_BLENDFACTOR_SRC_COLOR: return GL_SRC_COLOR;
    case SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR: return GL_ONE_MINUS_SRC_COLOR;
    case SDL_GPU_BLENDFACTOR_DST_COLOR: return GL_DST_COLOR;
    case SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR: return GL_ONE_MINUS_DST_COLOR;
    case SDL_GPU_BLENDFACTOR_SRC_ALPHA: return GL_SRC_ALPHA;
    case SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case SDL_GPU_BLENDFACTOR_DST_ALPHA: return GL_DST_ALPHA;
    case SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case SDL_GPU_BLENDFACTOR_CONSTANT_COLOR: return GL_CONSTANT_COLOR;
    case SDL_GPU_BLENDFACTOR_ONE_MINUS_CONSTANT_COLOR: return GL_ONE_MINUS_CONSTANT_COLOR;
    case SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE: return GL_SRC_ALPHA_SATURATE;
    default: return GL_ONE;
    }
}

GLenum blend_op(SDL_GPUBlendOp op) {
    switch (op) {
    case SDL_GPU_BLENDOP_SUBTRACT: return GL_FUNC_SUBTRACT;
    case SDL_GPU_BLENDOP_REVERSE_SUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
    case SDL_GPU_BLENDOP_MIN: return GL_MIN;
    case SDL_GPU_BLENDOP_MAX: return GL_MAX;
    default: return GL_FUNC_ADD;
    }
}

GLenum compare_op(SDL_GPUCompareOp op) {
    switch (op) {
    case SDL_GPU_COMPAREOP_NEVER: return GL_NEVER;
    case SDL_GPU_COMPAREOP_LESS: return GL_LESS;
    case SDL_GPU_COMPAREOP_EQUAL: return GL_EQUAL;
    case SDL_GPU_COMPAREOP_LESS_OR_EQUAL: return GL_LEQUAL;
    case SDL_GPU_COMPAREOP_GREATER: return GL_GREATER;
    case SDL_GPU_COMPAREOP_NOT_EQUAL: return GL_NOTEQUAL;
    case SDL_GPU_COMPAREOP_GREATER_OR_EQUAL: return GL_GEQUAL;
    default: return GL_ALWAYS;
    }
}

GLenum wrap_mode(SDL_GPUSamplerAddressMode m) {
    switch (m) {
    case SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT: return GL_MIRRORED_REPEAT;
    case SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE: return GL_CLAMP_TO_EDGE;
    default: return GL_REPEAT;
    }
}

struct VertexFormat {
    GLint size;
    GLenum type;
    GLboolean normalized;
};

bool vertex_format(SDL_GPUVertexElementFormat f, VertexFormat* out) {
    switch (f) {
    case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT: *out = {1, GL_FLOAT, GL_FALSE}; return true;
    case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2: *out = {2, GL_FLOAT, GL_FALSE}; return true;
    case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3: *out = {3, GL_FLOAT, GL_FALSE}; return true;
    case SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4: *out = {4, GL_FLOAT, GL_FALSE}; return true;
    case SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM: *out = {4, GL_UNSIGNED_BYTE, GL_TRUE}; return true;
    case SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4: *out = {4, GL_UNSIGNED_BYTE, GL_FALSE}; return true;
    case SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM: *out = {4, GL_BYTE, GL_TRUE}; return true;
    case SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM: *out = {2, GL_SHORT, GL_TRUE}; return true;
    case SDL_GPU_VERTEXELEMENTFORMAT_SHORT4_NORM: *out = {4, GL_SHORT, GL_TRUE}; return true;
    case SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM: *out = {2, GL_UNSIGNED_SHORT, GL_TRUE}; return true;
    case SDL_GPU_VERTEXELEMENTFORMAT_USHORT4_NORM: *out = {4, GL_UNSIGNED_SHORT, GL_TRUE}; return true;
    default: return false;
    }
}

GLsizei sample_count(SDL_GPUSampleCount c) { return GLsizei(1) << int(c); }

} // namespace

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

struct SDL_GPUTexture {
    SDL_GPUTextureCreateInfo info{};
    TexFormat fmt;
    GLuint tex = 0;    // texture: sampled, or a single-sample colour target
    GLuint rb = 0;     // renderbuffer: multisampled, or a depth target that is never sampled
    GLenum target = 0; // GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP (textures)
};

struct SDL_GPUBuffer {
    GLuint buf = 0;
    GLenum target = GL_ARRAY_BUFFER; // index buffers are only ever bound as index buffers
    Uint32 size = 0;
};

struct SDL_GPUTransferBuffer {
    std::vector<Uint8> data;
};

struct SDL_GPUSampler {
    GLuint smp = 0;
};

struct SDL_GPUShader {
    GLuint shader = 0;
    SDL_GPUShaderStage stage = SDL_GPU_SHADERSTAGE_VERTEX;
    std::vector<std::string> blocks, samplers; // names in slot order
    Uint32 num_uniform_buffers = 0, num_samplers = 0;
};

struct SDL_GPUFence {
    int unused = 0;
};

namespace {

struct VertexAttr {
    GLuint location;
    VertexFormat fmt;
    Uint32 offset;
    bool operator==(const VertexAttr& o) const {
        return location == o.location && fmt.size == o.fmt.size && fmt.type == o.fmt.type &&
               fmt.normalized == o.fmt.normalized && offset == o.offset;
    }
};

} // namespace

struct SDL_GPUGraphicsPipeline {
    GLuint program = 0;
    GLenum mode = GL_TRIANGLES;
    bool cull = false;
    GLenum cull_face = GL_BACK, front_face = GL_CW;
    bool depth_test = false, depth_write = false;
    GLenum depth_func = GL_LESS;
    bool blend = false;
    GLenum src_rgb = GL_ONE, dst_rgb = GL_ZERO, src_a = GL_ONE, dst_a = GL_ZERO;
    GLenum eq_rgb = GL_FUNC_ADD, eq_a = GL_FUNC_ADD;
    GLboolean mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    Uint32 pitch = 0;
    std::vector<VertexAttr> attrs;
    Uint32 vs_ubos = 0, fs_ubos = 0, fs_samplers = 0;
};

struct SDL_GPUDevice {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr; // SDL's GLES2 renderer: owns the OpenGL ES context
    bool debug = false;               // GL error checks (off: a no-error context where offered)
    bool angle = true;                // Windows: ANGLE when bundled (off: the driver's OpenGL ES)
    std::string description;          // gles::description()
    GLint ubo_align = 256;
    bool aniso = false;
    float max_aniso = 1;
    // glDrawElementsInstancedBaseVertex (OpenGL ES 3.2, or OES / EXT_draw_elements_base_vertex):
    // draws keep their attribute pointers. Without it (OpenGL ES 3.0) they move the attribute
    // offsets by the draw's vertex offset instead.
    void(GL_APIENTRYP draw_base_vertex)(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                        GLsizei instances, GLint base_vertex) = nullptr;
    GLuint vao = 0, ubo = 0;
    GLuint scratch_read = 0, scratch_draw = 0; // frame buffers for resolves and copies
    std::map<std::pair<GLuint, GLuint>, GLuint> programs;
    // Render pass frame buffers: (colour object, colour layer, colour mip, depth object);
    // objects are GL names with bit 32 set for renderbuffers.
    std::map<std::tuple<Uint64, Uint32, Uint32, Uint64>, GLuint> fbos;
    std::map<std::pair<GLenum, int>, bool> sample_support;
};

namespace {

SDL_GPUDevice* g_device = nullptr; // while the OpenGL ES device is in use (gles::create_device)

} // namespace

namespace {

// Hands the context over from SDL's 2D renderer (see the file comment).
void begin_gl(SDL_GPUDevice* dev) {
    if (dev && dev->renderer) SDL_FlushRenderer(dev->renderer);
}

// [video] gpu_debug: the GL errors this file's calls left, logged where they showed up.
void check_gl(const SDL_GPUDevice* dev, const char* where) {
    if (!dev || !dev->debug) return;
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) LOG_WARN("OpenGL ES error 0x%x in %s", e, where);
}

Uint64 object_key(const SDL_GPUTexture* t) {
    if (!t) return 0;
    return t->rb ? (Uint64(1) << 32) | t->rb : Uint64(t->tex);
}

void attach(GLenum fb_target, GLenum attachment, const SDL_GPUTexture* t, Uint32 layer, Uint32 mip) {
    if (!t) {
        glFramebufferRenderbuffer(fb_target, attachment, GL_RENDERBUFFER, 0);
    } else if (t->rb) {
        glFramebufferRenderbuffer(fb_target, attachment, GL_RENDERBUFFER, t->rb);
    } else if (t->target == GL_TEXTURE_CUBE_MAP) {
        glFramebufferTexture2D(fb_target, attachment, GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer, t->tex, GLint(mip));
    } else {
        glFramebufferTexture2D(fb_target, attachment, GL_TEXTURE_2D, t->tex, GLint(mip));
    }
}

GLenum depth_attachment(const SDL_GPUTexture* t) {
    return t->fmt.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
}

GLuint pass_fbo(SDL_GPUDevice* dev, const SDL_GPUTexture* color, Uint32 layer, Uint32 mip,
                const SDL_GPUTexture* depth) {
    auto key = std::make_tuple(object_key(color), layer, mip, object_key(depth));
    auto it = dev->fbos.find(key);
    if (it != dev->fbos.end()) {
        glBindFramebuffer(GL_FRAMEBUFFER, it->second);
        return it->second;
    }
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    if (color) attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, color, layer, mip);
    if (depth) attach(GL_FRAMEBUFFER, depth_attachment(depth), depth, 0, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) LOG_WARN("OpenGL ES frame buffer incomplete (0x%x)", status);
    dev->fbos[key] = fbo;
    return fbo;
}

void forget_fbos(SDL_GPUDevice* dev, const SDL_GPUTexture* t) {
    const Uint64 k = object_key(t);
    for (auto it = dev->fbos.begin(); it != dev->fbos.end();) {
        if (std::get<0>(it->first) == k || std::get<3>(it->first) == k) {
            glDeleteFramebuffers(1, &it->second);
            it = dev->fbos.erase(it);
        } else {
            ++it;
        }
    }
}

GLint current_framebuffer() {
    GLint fb = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
    return fb;
}

} // namespace

// ---------------------------------------------------------------------------
// Command buffers and passes
// ---------------------------------------------------------------------------

namespace {

struct DrawRec {
    SDL_GPUGraphicsPipeline* pipe;
    GLuint vb;
    Uint32 vb_offset;
    GLuint ib;
    Uint32 ib_offset;
    GLenum ib_type;
    GLuint tex[kSlots];
    GLenum tex_target[kSlots];
    GLuint smp[kSlots];
    Uint32 ubo_offset[2][kSlots];
    Uint32 ubo_size[2][kSlots];
    Uint32 count, first_index, instances;
    // Viewport (w = 0: the whole target) and scissor (w < 0: off), in target pixels from the top
    // row, which is GL's first row too: the shaders flip y.
    GLint vp[4], sc[4];
    Sint32 base_vertex;
};

} // namespace

struct SDL_GPURenderPass {
    SDL_GPUCommandBuffer* cmd = nullptr;
    bool open = false;
    bool has_color = false, has_depth = false;
    SDL_GPUColorTargetInfo color{};
    SDL_GPUDepthStencilTargetInfo depth{};
    // Bindings while recording.
    SDL_GPUGraphicsPipeline* pipe = nullptr;
    GLuint vb = 0;
    Uint32 vb_offset = 0;
    GLuint ib = 0;
    Uint32 ib_offset = 0;
    GLenum ib_type = GL_UNSIGNED_INT;
    GLuint tex[kSlots] = {};
    GLenum tex_target[kSlots] = {};
    GLuint smp[kSlots] = {};
    std::vector<DrawRec> draws;
    GLint vp[4] = {0, 0, 0, 0}, sc[4] = {0, 0, -1, -1}; // SDL_SetGPUViewport / SDL_SetGPUScissor
};

struct SDL_GPUCopyPass {
    SDL_GPUCommandBuffer* cmd = nullptr;
};

struct SDL_GPUCommandBuffer {
    SDL_GPUDevice* dev = nullptr;
    SDL_GPURenderPass pass;
    SDL_GPUCopyPass copy;
    // The latest data pushed to each uniform slot ([0] vertex, [1] fragment) and where it
    // sits in the open render pass's arena (once a draw used it).
    struct Uniform {
        std::vector<Uint8> data;
        Uint32 offset = 0;
        bool in_arena = false;
    } uniforms[2][kSlots];
    std::vector<Uint8> arena;
};

namespace {

void push_uniform(SDL_GPUCommandBuffer* cmd, int stage, Uint32 slot, const void* data, Uint32 length) {
    if (!cmd || slot >= kSlots || !data) return;
    auto& u = cmd->uniforms[stage][slot];
    if (u.in_arena && u.data.size() == length && std::memcmp(u.data.data(), data, length) == 0) return;
    u.data.assign(static_cast<const Uint8*>(data), static_cast<const Uint8*>(data) + length);
    u.in_arena = false;
}

// Places a slot's data in the arena (once per change) and returns its offset.
Uint32 arena_place(SDL_GPUCommandBuffer* cmd, int stage, Uint32 slot) {
    auto& u = cmd->uniforms[stage][slot];
    if (!u.in_arena) {
        const size_t align = size_t(std::max(cmd->dev->ubo_align, 16));
        size_t off = (cmd->arena.size() + align - 1) / align * align;
        // Whole 16-byte rows: std140 blocks are bound by their full size.
        size_t len = (u.data.size() + 15) / 16 * 16;
        cmd->arena.resize(off + std::max<size_t>(len, 16), 0);
        if (!u.data.empty()) std::memcpy(cmd->arena.data() + off, u.data.data(), u.data.size());
        u.offset = Uint32(off);
        u.in_arena = true;
    }
    return u.offset;
}

// GL state while a pass replays; ~0u = unknown (forces the first set).
struct ReplayState {
    GLuint program = ~0u;
    int cull = -1;
    GLenum cull_face = 0, front_face = 0;
    int depth_test = -1, depth_write = -1;
    GLenum depth_func = 0;
    int blend = -1;
    GLenum blend_f[4] = {}, blend_eq[2] = {};
    int mask = -1;
    GLuint vb = ~0u;
    Uint64 vb_base = ~Uint64(0);
    const std::vector<VertexAttr>* attrs = nullptr;
    Uint32 pitch = 0;
    Uint32 enabled_attribs = 0;
    GLuint ib = ~0u;
    GLuint tex[kSlots] = {~0u, ~0u, ~0u, ~0u};
    GLuint smp[kSlots] = {~0u, ~0u, ~0u, ~0u};
    Uint32 ubo_offset[2][kSlots], ubo_size[2][kSlots];
    GLint vp[4] = {-1, -1, -1, -1}, sc[4] = {-1, -1, -1, -1};
    int scissor = 0; // run_pass turned it off
    ReplayState() {
        for (auto& a : ubo_offset)
            for (Uint32& v : a) v = ~0u;
        for (auto& a : ubo_size)
            for (Uint32& v : a) v = 0;
    }
};

void set_cap(GLenum cap, bool on, int& cur) {
    if (cur == int(on)) return;
    if (on) glEnable(cap);
    else glDisable(cap);
    cur = int(on);
}

void replay(SDL_GPUDevice* dev, const std::vector<DrawRec>& draws, GLsizei w, GLsizei h) {
    ReplayState s;
    glBindVertexArray(dev->vao);
    for (const DrawRec& d : draws) {
        const SDL_GPUGraphicsPipeline* p = d.pipe;
        if (p->program != s.program) glUseProgram(s.program = p->program);
        set_cap(GL_CULL_FACE, p->cull, s.cull);
        if (p->cull && s.cull_face != p->cull_face) glCullFace(s.cull_face = p->cull_face);
        if (s.front_face != p->front_face) glFrontFace(s.front_face = p->front_face);
        set_cap(GL_DEPTH_TEST, p->depth_test, s.depth_test);
        if (p->depth_test && s.depth_func != p->depth_func) glDepthFunc(s.depth_func = p->depth_func);
        if (s.depth_write != int(p->depth_write)) glDepthMask((s.depth_write = int(p->depth_write)) ? GL_TRUE : GL_FALSE);
        set_cap(GL_BLEND, p->blend, s.blend);
        if (p->blend) {
            if (s.blend_f[0] != p->src_rgb || s.blend_f[1] != p->dst_rgb || s.blend_f[2] != p->src_a ||
                s.blend_f[3] != p->dst_a) {
                glBlendFuncSeparate(p->src_rgb, p->dst_rgb, p->src_a, p->dst_a);
                s.blend_f[0] = p->src_rgb, s.blend_f[1] = p->dst_rgb, s.blend_f[2] = p->src_a, s.blend_f[3] = p->dst_a;
            }
            if (s.blend_eq[0] != p->eq_rgb || s.blend_eq[1] != p->eq_a) {
                glBlendEquationSeparate(p->eq_rgb, p->eq_a);
                s.blend_eq[0] = p->eq_rgb, s.blend_eq[1] = p->eq_a;
            }
        }
        const int mask = p->mask[0] | p->mask[1] << 1 | p->mask[2] << 2 | p->mask[3] << 3;
        if (s.mask != mask) {
            glColorMask(p->mask[0], p->mask[1], p->mask[2], p->mask[3]);
            s.mask = mask;
        }

        // Vertex attributes (moved by the draw's vertex offset without base vertex draws).
        const Sint64 moved = dev->draw_base_vertex ? 0 : Sint64(d.base_vertex) * Sint64(p->pitch);
        const Uint64 base = Uint64(d.vb_offset) + Uint64(moved);
        if (s.vb != d.vb || s.vb_base != base || s.attrs != &p->attrs || s.pitch != p->pitch) {
            if (s.vb != d.vb) glBindBuffer(GL_ARRAY_BUFFER, d.vb);
            Uint32 want = 0;
            for (const VertexAttr& a : p->attrs) {
                want |= 1u << a.location;
                glVertexAttribPointer(a.location, a.fmt.size, a.fmt.type, a.fmt.normalized, GLsizei(p->pitch),
                                      reinterpret_cast<const void*>(uintptr_t(base + a.offset)));
            }
            for (GLuint i = 0; i < 16; ++i) {
                const Uint32 bit = 1u << i;
                if ((want & bit) && !(s.enabled_attribs & bit)) glEnableVertexAttribArray(i);
                if (!(want & bit) && (s.enabled_attribs & bit)) glDisableVertexAttribArray(i);
            }
            s.enabled_attribs = want;
            s.vb = d.vb, s.vb_base = base, s.attrs = &p->attrs, s.pitch = p->pitch;
        }
        if (s.ib != d.ib) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s.ib = d.ib);

        for (Uint32 i = 0; i < p->fs_samplers && i < kSlots; ++i) {
            if (s.tex[i] != d.tex[i]) {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(d.tex_target[i] ? d.tex_target[i] : GL_TEXTURE_2D, s.tex[i] = d.tex[i]);
            }
            if (s.smp[i] != d.smp[i]) glBindSampler(i, s.smp[i] = d.smp[i]);
        }
        for (int st = 0; st < 2; ++st) {
            const Uint32 n = st == 0 ? p->vs_ubos : p->fs_ubos;
            for (Uint32 k = 0; k < n && k < kSlots; ++k) {
                if (s.ubo_offset[st][k] == d.ubo_offset[st][k] && s.ubo_size[st][k] == d.ubo_size[st][k]) continue;
                glBindBufferRange(GL_UNIFORM_BUFFER, (st == 0 ? 0 : kFragmentUboBase) + k, dev->ubo,
                                  GLintptr(d.ubo_offset[st][k]), GLsizeiptr(d.ubo_size[st][k]));
                s.ubo_offset[st][k] = d.ubo_offset[st][k];
                s.ubo_size[st][k] = d.ubo_size[st][k];
            }
        }
        const GLint vp[4] = {d.vp[0], d.vp[1], d.vp[2] > 0 ? d.vp[2] : w, d.vp[2] > 0 ? d.vp[3] : h};
        if (vp[0] != s.vp[0] || vp[1] != s.vp[1] || vp[2] != s.vp[2] || vp[3] != s.vp[3]) {
            glViewport(vp[0], vp[1], vp[2], vp[3]);
            for (int i = 0; i < 4; ++i) s.vp[i] = vp[i];
        }
        set_cap(GL_SCISSOR_TEST, d.sc[2] >= 0, s.scissor);
        if (d.sc[2] >= 0 && (d.sc[0] != s.sc[0] || d.sc[1] != s.sc[1] || d.sc[2] != s.sc[2] || d.sc[3] != s.sc[3])) {
            glScissor(d.sc[0], d.sc[1], d.sc[2], d.sc[3]);
            for (int i = 0; i < 4; ++i) s.sc[i] = d.sc[i];
        }
        const Uint32 index_size = d.ib_type == GL_UNSIGNED_INT ? 4 : 2;
        const void* first = reinterpret_cast<const void*>(uintptr_t(d.ib_offset + d.first_index * index_size));
        if (dev->draw_base_vertex)
            dev->draw_base_vertex(p->mode, GLsizei(d.count), d.ib_type, first, GLsizei(std::max<Uint32>(1, d.instances)),
                                  d.base_vertex);
        else if (d.instances > 1)
            glDrawElementsInstanced(p->mode, GLsizei(d.count), d.ib_type, first, GLsizei(d.instances));
        else glDrawElements(p->mode, GLsizei(d.count), d.ib_type, first);
    }
    // Hand back the state SDL's renderer assumes (and the resolve blit after this: the scissor
    // test applies to blits).
    set_cap(GL_SCISSOR_TEST, false, s.scissor);
    if (s.vp[0] != 0 || s.vp[1] != 0 || s.vp[2] != w || s.vp[3] != h) glViewport(0, 0, w, h);
    for (GLuint i = 0; i < 16; ++i)
        if (s.enabled_attribs & (1u << i)) glDisableVertexAttribArray(i);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0); // SDL's GLES2 renderer draws its vertices from client memory
    for (GLuint i = 0; i < kSlots; ++i) {
        if (s.smp[i] != ~0u && s.smp[i] != 0) glBindSampler(i, 0);
        if (s.tex[i] != ~0u && i > 0) { // unit 0: SDL rebinds its own texture
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        }
    }
    glActiveTexture(GL_TEXTURE0);
}

void restore_defaults() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glFrontFace(GL_CCW);
    glUseProgram(0);
}

void run_pass(SDL_GPURenderPass* pass) {
    SDL_GPUCommandBuffer* cmd = pass->cmd;
    SDL_GPUDevice* dev = cmd->dev;
    begin_gl(dev);
    check_gl(dev, "SDL's 2D renderer");
    const GLint saved_fb = current_framebuffer();

    SDL_GPUTexture* ct = pass->has_color ? pass->color.texture : nullptr;
    SDL_GPUTexture* dt = pass->has_depth ? pass->depth.texture : nullptr;
    const Uint32 layer = ct ? (ct->info.type == SDL_GPU_TEXTURETYPE_CUBE ? pass->color.layer_or_depth_plane : 0) : 0;
    const Uint32 mip = ct ? pass->color.mip_level : 0;
    const SDL_GPUTexture* sized = ct ? ct : dt;
    if (!sized) return;
    const GLsizei w = GLsizei(std::max<Uint32>(1, sized->info.width >> mip));
    const GLsizei h = GLsizei(std::max<Uint32>(1, sized->info.height >> mip));
    const GLuint fbo = pass_fbo(dev, ct, layer, mip, dt);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);

    if (ct && pass->color.load_op == SDL_GPU_LOADOP_CLEAR) {
        const SDL_FColor& c = pass->color.clear_color;
        const GLfloat v[4] = {c.r, c.g, c.b, c.a};
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearBufferfv(GL_COLOR, 0, v);
    }
    if (dt && pass->depth.load_op == SDL_GPU_LOADOP_CLEAR) {
        glDepthMask(GL_TRUE);
        if (dt->fmt.stencil) glClearBufferfi(GL_DEPTH_STENCIL, 0, pass->depth.clear_depth, pass->depth.clear_stencil);
        else glClearBufferfv(GL_DEPTH, 0, &pass->depth.clear_depth);
    }

    if (!pass->draws.empty()) {
        glBindBuffer(GL_UNIFORM_BUFFER, dev->ubo);
        glBufferData(GL_UNIFORM_BUFFER, GLsizeiptr(std::max<size_t>(cmd->arena.size(), 16)),
                     cmd->arena.empty() ? nullptr : cmd->arena.data(), GL_STREAM_DRAW);
        replay(dev, pass->draws, w, h);
    }

    // Multisample resolve into the resolve texture (a cube face, for cube resolves).
    const SDL_GPUStoreOp store = pass->color.store_op;
    if (ct && pass->color.resolve_texture &&
        (store == SDL_GPU_STOREOP_RESOLVE || store == SDL_GPU_STOREOP_RESOLVE_AND_STORE)) {
        SDL_GPUTexture* rt = pass->color.resolve_texture;
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dev->scratch_draw);
        attach(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, rt,
               rt->info.type == SDL_GPU_TEXTURETYPE_CUBE ? pass->color.resolve_layer : 0, pass->color.resolve_mip_level);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        attach(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, nullptr, 0, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }
    // Contents nobody reads again need not be written back (tiled mobile GPUs).
    GLenum discard[2];
    GLsizei nd = 0;
    if (ct && (store == SDL_GPU_STOREOP_DONT_CARE || store == SDL_GPU_STOREOP_RESOLVE)) discard[nd++] = GL_COLOR_ATTACHMENT0;
    if (dt && pass->depth.store_op == SDL_GPU_STOREOP_DONT_CARE) discard[nd++] = depth_attachment(dt);
    if (nd) glInvalidateFramebuffer(GL_FRAMEBUFFER, nd, discard);

    restore_defaults();
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(saved_fb));
    check_gl(dev, "a render pass");
}

} // namespace

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

namespace gt3r::render::gles {

SDL_GPUDevice* create_device(bool debug, bool angle) {
    if (!g_device) g_device = new SDL_GPUDevice();
    g_device->debug = debug;
    g_device->angle = angle;
    return g_device;
}

bool active() { return g_device != nullptr; }

bool angle() { return g_device && g_device->angle; }

const char* description() { return g_device ? g_device->description.c_str() : ""; }

} // namespace gt3r::render::gles

SDL_WindowFlags gt3r_gl_window_flags() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    // The window only shows the UI (3D views draw into textures): no depth, stencil or alpha.
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
    return SDL_WINDOW_OPENGL;
}

void gt3r_gl_DestroyGPUDevice(SDL_GPUDevice* device) {
    if (!device || device != g_device) return SDL_DestroyGPUDevice(device);
    // The GL objects went with the context (the renderer is destroyed first).
    delete device;
    g_device = nullptr;
}

const char* gt3r_gl_GetGPUDeviceDriver(SDL_GPUDevice* device) {
    if (!g_device) return SDL_GetGPUDeviceDriver(device);
    return "opengles";
}

SDL_GPUShaderFormat gt3r_gl_GetGPUShaderFormats(SDL_GPUDevice* device) {
    if (!g_device) return SDL_GetGPUShaderFormats(device);
    return SDL_GPU_SHADERFORMAT_PRIVATE;
}

namespace {

bool has_extension(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
        if (e && std::strcmp(e, name) == 0) return true;
    }
    return false;
}

#ifdef SDL_PLATFORM_WINDOWS
// ANGLE's DLLs next to the game: OpenGL ES on Direct3D 11, which every Direct3D 10.1 GPU has
// (old GL drivers are often worse, and many offer no OpenGL ES 3.0 context at all).
bool angle_present() {
    const char* base = SDL_GetBasePath();
    if (!base) return false;
    for (const char* f : {"libEGL.dll", "libGLESv2.dll"}) {
        SDL_PathInfo info;
        if (!SDL_GetPathInfo((std::string(base) + f).c_str(), &info)) return false;
    }
    return true;
}

// ANGLE draws straight into the window's swap chain instead of into an image it copies there,
// flipped, at every present (EGL_ANGLE_experimental_present_path; it flips the frame itself).
SDL_EGLint* SDLCALL angle_surface_attribs(void*, SDL_EGLDisplay display, SDL_EGLConfig) {
    constexpr SDL_EGLint kExtensions = 0x3055, kPresentPath = 0x33A4, kPresentPathFast = 0x33A9, kNone = 0x3038;
    using QueryString = const char*(KHRONOS_APIENTRY*)(SDL_EGLDisplay, SDL_EGLint);
    auto query = reinterpret_cast<QueryString>(SDL_EGL_GetProcAddress("eglQueryString"));
    const char* ext = query ? query(display, kExtensions) : nullptr;
    auto* a = static_cast<SDL_EGLint*>(SDL_malloc(3 * sizeof(SDL_EGLint))); // SDL frees it
    if (!a) return nullptr;
    int n = 0;
    if (ext && std::strstr(ext, "EGL_ANGLE_experimental_present_path")) a[n++] = kPresentPath, a[n++] = kPresentPathFast;
    a[n] = kNone;
    return a;
}
#endif

// SDL's GLES2 renderer with an OpenGL ES 3.0 context. On a window that has no GL set up yet,
// SDL sets it up and asks for OpenGL ES 2.0 (which may well be what comes back), so then the
// renderer is made once more, with the attributes asking for 3.0 again.
SDL_Renderer* create_es3_renderer(SDL_Window* window, bool debug) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        gt3r_gl_window_flags();
        // A context that skips GL error checks where it offers that (less work per call, mostly
        // in ANGLE); [video] gpu_debug keeps them.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_NO_ERROR, debug ? 0 : 1);
        SDL_Renderer* r = SDL_CreateRenderer(window, "opengles2");
        if (!r) return nullptr;
        if (!load_gl()) {
            SDL_DestroyRenderer(r);
            return nullptr;
        }
        const char* v = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        if (v && std::strstr(v, "OpenGL ES 3")) return r;
        SDL_DestroyRenderer(r);
        if (attempt == 1) SDL_SetError("OpenGL ES 3.0 is not available (%s)", v ? v : "no context");
    }
    return nullptr;
}

} // namespace

SDL_Renderer* gt3r_gl_CreateGPURenderer(SDL_GPUDevice* device, SDL_Window* window) {
    if (!g_device) return SDL_CreateGPURenderer(device, window);
    if (!device) return nullptr;
#ifdef SDL_PLATFORM_WINDOWS
    // ANGLE over the driver's own OpenGL ES when it is there, unless [video] gpu = gles_driver
    // (SDL_OPENGL_ES_DRIVER in the environment overrides both, for comparisons). SDL still turns
    // to EGL, so to ANGLE when it is there, for a driver without OpenGL ES 3.0 contexts.
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, device->angle && angle_present() ? "1" : "0");
    SDL_EGL_SetAttributeCallbacks(nullptr, angle_surface_attribs, nullptr, nullptr);
#endif
    SDL_Renderer* r = create_es3_renderer(window, device->debug);
    if (!r) return nullptr;
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    device->window = window;
    device->renderer = r;
    SDL_FlushRenderer(r);
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &device->ubo_align);
    device->aniso = has_extension("GL_EXT_texture_filter_anisotropic");
    GLint major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    const char* base_vertex = major > 3 || (major == 3 && minor >= 2) ? "glDrawElementsInstancedBaseVertex"
                              : has_extension("GL_OES_draw_elements_base_vertex")
                                  ? "glDrawElementsInstancedBaseVertexOES"
                              : has_extension("GL_EXT_draw_elements_base_vertex") ? "glDrawElementsInstancedBaseVertexEXT"
                                                                                  : nullptr;
    if (base_vertex)
        device->draw_base_vertex = reinterpret_cast<decltype(device->draw_base_vertex)>(SDL_GL_GetProcAddress(base_vertex));
    if (device->aniso) glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &device->max_aniso);
    glGenVertexArrays(1, &device->vao);
    glGenBuffers(1, &device->ubo);
    glGenFramebuffers(1, &device->scratch_read);
    glGenFramebuffers(1, &device->scratch_draw);
    const char* gpu = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    device->description = std::string(version) + "; " + (gpu ? gpu : "?");
    LOG_INFO("OpenGL ES: %s%s", device->description.c_str(), device->draw_base_vertex ? "; base vertex draws" : "");
    return r;
}

namespace gt3r::render::gles {

bool probe(bool angle) {
    if (!SDL_Init(SDL_INIT_VIDEO)) return false;
    bool ok = false;
    if (SDL_Window* window = SDL_CreateWindow("GT3 Revisited", 64, 64, SDL_WINDOW_HIDDEN)) {
        SDL_GPUDevice* device = create_device(false, angle);
        if (SDL_Renderer* r = gt3r_gl_CreateGPURenderer(device, window)) {
            ok = (device->description.find("(ANGLE") != std::string::npos) == angle;
            SDL_DestroyRenderer(r);
        }
        gt3r_gl_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return ok;
}

} // namespace gt3r::render::gles

bool gt3r_gl_GPUTextureSupportsFormat(SDL_GPUDevice* device, SDL_GPUTextureFormat format, SDL_GPUTextureType type,
                                      SDL_GPUTextureUsageFlags usage) {
    if (!g_device) return SDL_GPUTextureSupportsFormat(device, format, type, usage);
    TexFormat f;
    return tex_format(format, &f) && (type == SDL_GPU_TEXTURETYPE_2D || type == SDL_GPU_TEXTURETYPE_CUBE);
}

bool gt3r_gl_GPUTextureSupportsSampleCount(SDL_GPUDevice* device, SDL_GPUTextureFormat format,
                                           SDL_GPUSampleCount count) {
    if (!g_device) return SDL_GPUTextureSupportsSampleCount(device, format, count);
    TexFormat f;
    if (!device || !device->renderer || !tex_format(format, &f)) return false;
    if (count == SDL_GPU_SAMPLECOUNT_1) return true;
    const auto key = std::make_pair(f.internal, int(count));
    if (auto it = device->sample_support.find(key); it != device->sample_support.end()) return it->second;
    begin_gl(device);
    // GL_SAMPLES fills in the supported counts (without asking GL_NUM_SAMPLE_COUNTS first).
    GLint counts[16] = {};
    glGetInternalformativ(GL_RENDERBUFFER, f.internal, GL_SAMPLES, 16, counts);
    const bool ok = std::find(std::begin(counts), std::end(counts), GLint(sample_count(count))) != std::end(counts);
    device->sample_support[key] = ok;
    return ok;
}

// ---------------------------------------------------------------------------
// Shaders and pipelines
// ---------------------------------------------------------------------------

SDL_GPUShader* gt3r_gl_CreateGPUShader(SDL_GPUDevice* device, const SDL_GPUShaderCreateInfo* info) {
    if (!g_device) return SDL_CreateGPUShader(device, info);
    if (!device || !info || info->format != SDL_GPU_SHADERFORMAT_PRIVATE) {
        SDL_SetError("OpenGL ES shaders must be GLSL ES 3.00 (SDL_GPU_SHADERFORMAT_PRIVATE)");
        return nullptr;
    }
    begin_gl(device);
    const bool vertex = info->stage == SDL_GPU_SHADERSTAGE_VERTEX;
    GLuint sh = glCreateShader(vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    const GLchar* src = reinterpret_cast<const GLchar*>(info->code);
    const GLint len = GLint(info->code_size);
    glShaderSource(sh, 1, &src, &len);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        SDL_SetError("GLSL ES %s shader: %s", vertex ? "vertex" : "fragment", log);
        glDeleteShader(sh);
        return nullptr;
    }
    auto* s = new SDL_GPUShader();
    s->shader = sh;
    s->stage = info->stage;
    s->num_uniform_buffers = info->num_uniform_buffers;
    s->num_samplers = info->num_samplers;
    s->blocks = split_names(SDL_GetStringProperty(info->props, GT3R_GL_PROP_SHADER_UNIFORM_BLOCKS, ""));
    s->samplers = split_names(SDL_GetStringProperty(info->props, GT3R_GL_PROP_SHADER_SAMPLERS, ""));
    return s;
}

void gt3r_gl_ReleaseGPUShader(SDL_GPUDevice* device, SDL_GPUShader* shader) {
    if (!g_device) return SDL_ReleaseGPUShader(device, shader);
    if (!shader) return;
    begin_gl(device);
    glDeleteShader(shader->shader);
    delete shader;
}

namespace {

GLuint program_for(SDL_GPUDevice* dev, const SDL_GPUShader* vs, const SDL_GPUShader* fs) {
    const auto key = std::make_pair(vs->shader, fs->shader);
    if (auto it = dev->programs.find(key); it != dev->programs.end()) return it->second;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs->shader);
    glAttachShader(prog, fs->shader);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        SDL_SetError("GLSL ES link: %s", log);
        glDeleteProgram(prog);
        return 0;
    }
    // Uniform blocks to binding points by slot; samplers to texture units by slot.
    for (size_t i = 0; i < vs->blocks.size(); ++i) {
        GLuint idx = glGetUniformBlockIndex(prog, vs->blocks[i].c_str());
        if (idx != GL_INVALID_INDEX) glUniformBlockBinding(prog, idx, GLuint(i));
    }
    for (size_t i = 0; i < fs->blocks.size(); ++i) {
        GLuint idx = glGetUniformBlockIndex(prog, fs->blocks[i].c_str());
        if (idx != GL_INVALID_INDEX) glUniformBlockBinding(prog, idx, kFragmentUboBase + GLuint(i));
    }
    glUseProgram(prog);
    for (size_t i = 0; i < fs->samplers.size(); ++i) {
        GLint loc = glGetUniformLocation(prog, fs->samplers[i].c_str());
        if (loc >= 0) glUniform1i(loc, GLint(i));
    }
    glUseProgram(0);
    dev->programs[key] = prog;
    return prog;
}

} // namespace

SDL_GPUGraphicsPipeline* gt3r_gl_CreateGPUGraphicsPipeline(SDL_GPUDevice* device,
                                                           const SDL_GPUGraphicsPipelineCreateInfo* info) {
    if (!g_device) return SDL_CreateGPUGraphicsPipeline(device, info);
    if (!device || !info || !info->vertex_shader || !info->fragment_shader) return nullptr;
    begin_gl(device);
    GLuint prog = program_for(device, info->vertex_shader, info->fragment_shader);
    if (!prog) return nullptr;
    auto* p = new SDL_GPUGraphicsPipeline();
    p->program = prog;
    switch (info->primitive_type) {
    case SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP: p->mode = GL_TRIANGLE_STRIP; break;
    case SDL_GPU_PRIMITIVETYPE_LINELIST: p->mode = GL_LINES; break;
    case SDL_GPU_PRIMITIVETYPE_LINESTRIP: p->mode = GL_LINE_STRIP; break;
    case SDL_GPU_PRIMITIVETYPE_POINTLIST: p->mode = GL_POINTS; break;
    default: p->mode = GL_TRIANGLES; break;
    }
    const SDL_GPURasterizerState& rs = info->rasterizer_state;
    p->cull = rs.cull_mode != SDL_GPU_CULLMODE_NONE;
    p->cull_face = rs.cull_mode == SDL_GPU_CULLMODE_FRONT ? GL_FRONT : GL_BACK;
    // The shaders flip y (see gpu_gles.h), which reverses the winding GL sees.
    p->front_face = rs.front_face == SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE ? GL_CW : GL_CCW;
    const SDL_GPUDepthStencilState& ds = info->depth_stencil_state;
    p->depth_test = ds.enable_depth_test;
    p->depth_write = ds.enable_depth_test && ds.enable_depth_write;
    p->depth_func = compare_op(ds.compare_op);
    if (info->target_info.num_color_targets > 0 && info->target_info.color_target_descriptions) {
        const SDL_GPUColorTargetBlendState& b = info->target_info.color_target_descriptions[0].blend_state;
        p->blend = b.enable_blend;
        p->src_rgb = blend_factor(b.src_color_blendfactor);
        p->dst_rgb = blend_factor(b.dst_color_blendfactor);
        p->src_a = blend_factor(b.src_alpha_blendfactor);
        p->dst_a = blend_factor(b.dst_alpha_blendfactor);
        p->eq_rgb = blend_op(b.color_blend_op);
        p->eq_a = blend_op(b.alpha_blend_op);
        if (b.enable_color_write_mask) {
            p->mask[0] = (b.color_write_mask & SDL_GPU_COLORCOMPONENT_R) ? GL_TRUE : GL_FALSE;
            p->mask[1] = (b.color_write_mask & SDL_GPU_COLORCOMPONENT_G) ? GL_TRUE : GL_FALSE;
            p->mask[2] = (b.color_write_mask & SDL_GPU_COLORCOMPONENT_B) ? GL_TRUE : GL_FALSE;
            p->mask[3] = (b.color_write_mask & SDL_GPU_COLORCOMPONENT_A) ? GL_TRUE : GL_FALSE;
        }
    }
    const SDL_GPUVertexInputState& vi = info->vertex_input_state;
    for (Uint32 i = 0; i < vi.num_vertex_buffers; ++i)
        if (vi.vertex_buffer_descriptions[i].slot == 0) p->pitch = vi.vertex_buffer_descriptions[i].pitch;
    for (Uint32 i = 0; i < vi.num_vertex_attributes; ++i) {
        const SDL_GPUVertexAttribute& a = vi.vertex_attributes[i];
        VertexFormat f;
        if (a.buffer_slot != 0 || !vertex_format(a.format, &f)) {
            SDL_SetError("OpenGL ES: unsupported vertex attribute %u", a.location);
            delete p;
            return nullptr;
        }
        p->attrs.push_back({a.location, f, a.offset});
    }
    p->vs_ubos = std::min<Uint32>(info->vertex_shader->num_uniform_buffers, kSlots);
    p->fs_ubos = std::min<Uint32>(info->fragment_shader->num_uniform_buffers, kSlots);
    p->fs_samplers = std::min<Uint32>(info->fragment_shader->num_samplers, kSlots);
    return p;
}

void gt3r_gl_ReleaseGPUGraphicsPipeline(SDL_GPUDevice* device, SDL_GPUGraphicsPipeline* pipeline) {
    if (!g_device) return SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    delete pipeline; // programs are shared and kept with the device
}

SDL_GPUSampler* gt3r_gl_CreateGPUSampler(SDL_GPUDevice* device, const SDL_GPUSamplerCreateInfo* info) {
    if (!g_device) return SDL_CreateGPUSampler(device, info);
    if (!device || !info) return nullptr;
    begin_gl(device);
    auto* s = new SDL_GPUSampler();
    glGenSamplers(1, &s->smp);
    const bool lin_min = info->min_filter == SDL_GPU_FILTER_LINEAR;
    const bool lin_mip = info->mipmap_mode == SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    const GLenum min = lin_min ? (lin_mip ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST)
                               : (lin_mip ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
    glSamplerParameteri(s->smp, GL_TEXTURE_MIN_FILTER, GLint(min));
    glSamplerParameteri(s->smp, GL_TEXTURE_MAG_FILTER, info->mag_filter == SDL_GPU_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
    glSamplerParameteri(s->smp, GL_TEXTURE_WRAP_S, GLint(wrap_mode(info->address_mode_u)));
    glSamplerParameteri(s->smp, GL_TEXTURE_WRAP_T, GLint(wrap_mode(info->address_mode_v)));
    glSamplerParameteri(s->smp, GL_TEXTURE_WRAP_R, GLint(wrap_mode(info->address_mode_w)));
    glSamplerParameterf(s->smp, GL_TEXTURE_MIN_LOD, info->min_lod);
    glSamplerParameterf(s->smp, GL_TEXTURE_MAX_LOD, info->max_lod);
    if (info->enable_anisotropy && device->aniso)
        glSamplerParameterf(s->smp, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::clamp(info->max_anisotropy, 1.0f, device->max_aniso));
    return s;
}

void gt3r_gl_ReleaseGPUSampler(SDL_GPUDevice* device, SDL_GPUSampler* sampler) {
    if (!g_device) return SDL_ReleaseGPUSampler(device, sampler);
    if (!sampler) return;
    begin_gl(device);
    glDeleteSamplers(1, &sampler->smp);
    delete sampler;
}

// ---------------------------------------------------------------------------
// Textures and buffers
// ---------------------------------------------------------------------------

SDL_GPUTexture* gt3r_gl_CreateGPUTexture(SDL_GPUDevice* device, const SDL_GPUTextureCreateInfo* info) {
    if (!g_device) return SDL_CreateGPUTexture(device, info);
    TexFormat f;
    if (!device || !info || !tex_format(info->format, &f) || info->width == 0 || info->height == 0) {
        SDL_SetError("OpenGL ES: unsupported texture");
        return nullptr;
    }
    if (info->type != SDL_GPU_TEXTURETYPE_2D && info->type != SDL_GPU_TEXTURETYPE_CUBE) {
        SDL_SetError("OpenGL ES: unsupported texture type");
        return nullptr;
    }
    begin_gl(device);
    auto* t = new SDL_GPUTexture();
    t->info = *info;
    t->fmt = f;
    const bool msaa = info->sample_count != SDL_GPU_SAMPLECOUNT_1;
    const bool sampled = info->usage & SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (msaa || (f.depth && !sampled)) {
        glGenRenderbuffers(1, &t->rb);
        glBindRenderbuffer(GL_RENDERBUFFER, t->rb);
        if (msaa)
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, sample_count(info->sample_count), f.internal,
                                             GLsizei(info->width), GLsizei(info->height));
        else
            glRenderbufferStorage(GL_RENDERBUFFER, f.internal, GLsizei(info->width), GLsizei(info->height));
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    } else {
        t->target = info->type == SDL_GPU_TEXTURETYPE_CUBE ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
        const GLsizei levels = GLsizei(std::max<Uint32>(1, info->num_levels));
        glGenTextures(1, &t->tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(t->target, t->tex);
        if (t->target == GL_TEXTURE_2D && levels == 1 && !f.depth) {
            // Mutable: SDL's 2D renderer re-specifies the textures it wraps (View3D targets)
            // with glTexImage2D, which immutable storage rejects.
            glTexImage2D(GL_TEXTURE_2D, 0, GLint(f.internal), GLsizei(info->width), GLsizei(info->height), 0, f.format,
                         f.type, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        } else {
            glTexStorage2D(t->target, levels, f.internal, GLsizei(info->width), GLsizei(info->height));
        }
        // Defaults for SDL's 2D renderer when it draws the texture (3D draws use samplers).
        glTexParameteri(t->target, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(t->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(t->target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(t->target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(t->target, 0);
    }
    return t;
}

void gt3r_gl_ReleaseGPUTexture(SDL_GPUDevice* device, SDL_GPUTexture* texture) {
    if (!g_device) return SDL_ReleaseGPUTexture(device, texture);
    if (!texture) return;
    begin_gl(device);
    if (device) forget_fbos(device, texture);
    if (texture->tex) glDeleteTextures(1, &texture->tex);
    if (texture->rb) glDeleteRenderbuffers(1, &texture->rb);
    delete texture;
}

unsigned gt3r_gl_texture_name(SDL_GPUTexture* texture) { return texture ? texture->tex : 0; }

SDL_GPUBuffer* gt3r_gl_CreateGPUBuffer(SDL_GPUDevice* device, const SDL_GPUBufferCreateInfo* info) {
    if (!g_device) return SDL_CreateGPUBuffer(device, info);
    if (!device || !info || info->size == 0) return nullptr;
    begin_gl(device);
    auto* b = new SDL_GPUBuffer();
    b->target = (info->usage & SDL_GPU_BUFFERUSAGE_INDEX) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    b->size = info->size;
    glGenBuffers(1, &b->buf);
    glBindBuffer(b->target, b->buf);
    glBufferData(b->target, GLsizeiptr(info->size), nullptr, GL_STATIC_DRAW);
    glBindBuffer(b->target, 0);
    return b;
}

void gt3r_gl_ReleaseGPUBuffer(SDL_GPUDevice* device, SDL_GPUBuffer* buffer) {
    if (!g_device) return SDL_ReleaseGPUBuffer(device, buffer);
    if (!buffer) return;
    begin_gl(device);
    glDeleteBuffers(1, &buffer->buf);
    delete buffer;
}

SDL_GPUTransferBuffer* gt3r_gl_CreateGPUTransferBuffer(SDL_GPUDevice* device,
                                                       const SDL_GPUTransferBufferCreateInfo* info) {
    if (!g_device) return SDL_CreateGPUTransferBuffer(device, info);
    if (!info) return nullptr;
    auto* t = new SDL_GPUTransferBuffer();
    t->data.resize(info->size);
    return t;
}

void gt3r_gl_ReleaseGPUTransferBuffer(SDL_GPUDevice* device, SDL_GPUTransferBuffer* transfer_buffer) {
    if (!g_device) return SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);
    delete transfer_buffer;
}

void* gt3r_gl_MapGPUTransferBuffer(SDL_GPUDevice* device, SDL_GPUTransferBuffer* t, bool cycle) {
    if (!g_device) return SDL_MapGPUTransferBuffer(device, t, cycle);
    return t ? t->data.data() : nullptr;
}

void gt3r_gl_UnmapGPUTransferBuffer(SDL_GPUDevice* device, SDL_GPUTransferBuffer* transfer_buffer) {
    if (!g_device) return SDL_UnmapGPUTransferBuffer(device, transfer_buffer);
}

// ---------------------------------------------------------------------------
// Command buffers
// ---------------------------------------------------------------------------

SDL_GPUCommandBuffer* gt3r_gl_AcquireGPUCommandBuffer(SDL_GPUDevice* device) {
    if (!g_device) return SDL_AcquireGPUCommandBuffer(device);
    if (!device || !device->renderer) return nullptr;
    auto* c = new SDL_GPUCommandBuffer();
    c->dev = device;
    c->pass.cmd = c;
    c->copy.cmd = c;
    return c;
}

bool gt3r_gl_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer* cmd) {
    if (!g_device) return SDL_SubmitGPUCommandBuffer(cmd);
    if (!cmd) return false;
    if (cmd->pass.open) run_pass(&cmd->pass); // not ended: SDL_GPU would reject it; draw what was recorded
    delete cmd;
    return true;
}

SDL_GPUFence* gt3r_gl_SubmitGPUCommandBufferAndAcquireFence(SDL_GPUCommandBuffer* cmd) {
    if (!g_device) return SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    return gt3r_gl_SubmitGPUCommandBuffer(cmd) ? new SDL_GPUFence() : nullptr;
}

bool gt3r_gl_WaitForGPUFences(SDL_GPUDevice* device, bool wait_all, SDL_GPUFence* const* fences, Uint32 num_fences) {
    if (!g_device) return SDL_WaitForGPUFences(device, wait_all, fences, num_fences);
    return gt3r_gl_WaitForGPUIdle(device);
}

void gt3r_gl_ReleaseGPUFence(SDL_GPUDevice* device, SDL_GPUFence* fence) {
    if (!g_device) return SDL_ReleaseGPUFence(device, fence);
    delete fence;
}

bool gt3r_gl_WaitForGPUIdle(SDL_GPUDevice* device) {
    if (!g_device) return SDL_WaitForGPUIdle(device);
    if (!device || !device->renderer) return false;
    begin_gl(device);
    glFinish();
    return true;
}

// Copy passes run at once.
SDL_GPUCopyPass* gt3r_gl_BeginGPUCopyPass(SDL_GPUCommandBuffer* cmd) {
    if (!g_device) return SDL_BeginGPUCopyPass(cmd);
    return cmd ? &cmd->copy : nullptr;
}

void gt3r_gl_EndGPUCopyPass(SDL_GPUCopyPass* copy_pass) {
    if (!g_device) return SDL_EndGPUCopyPass(copy_pass);
}

void gt3r_gl_UploadToGPUBuffer(SDL_GPUCopyPass* copy, const SDL_GPUTransferBufferLocation* src,
                               const SDL_GPUBufferRegion* dst, bool cycle) {
    if (!g_device) return SDL_UploadToGPUBuffer(copy, src, dst, cycle);
    if (!copy || !src || !dst || !src->transfer_buffer || !dst->buffer) return;
    SDL_GPUBuffer* b = dst->buffer;
    if (size_t(src->offset) + dst->size > src->transfer_buffer->data.size() || dst->offset + dst->size > b->size) return;
    begin_gl(copy->cmd->dev);
    glBindBuffer(b->target, b->buf);
    const Uint8* data = src->transfer_buffer->data.data() + src->offset;
    if (dst->offset == 0 && dst->size == b->size) // whole buffer: new storage, no wait on draws using the old
        glBufferData(b->target, GLsizeiptr(b->size), data, GL_STATIC_DRAW);
    else
        glBufferSubData(b->target, GLintptr(dst->offset), GLsizeiptr(dst->size), data);
    glBindBuffer(b->target, 0);
}

void gt3r_gl_UploadToGPUTexture(SDL_GPUCopyPass* copy, const SDL_GPUTextureTransferInfo* src,
                                const SDL_GPUTextureRegion* dst, bool cycle) {
    if (!g_device) return SDL_UploadToGPUTexture(copy, src, dst, cycle);
    if (!copy || !src || !dst || !src->transfer_buffer || !dst->texture || !dst->texture->tex) return;
    SDL_GPUTexture* t = dst->texture;
    if (!t->fmt.format) return; // depth
    const Uint32 row = src->pixels_per_row ? src->pixels_per_row : dst->w;
    if (size_t(src->offset) + size_t(row) * (dst->h ? dst->h - 1 : 0) * 4 + size_t(dst->w) * 4 >
        src->transfer_buffer->data.size())
        return;
    begin_gl(copy->cmd->dev);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(t->target, t->tex);
    if (row != dst->w) glPixelStorei(GL_UNPACK_ROW_LENGTH, GLint(row));
    const GLenum face = t->target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + dst->layer : GL_TEXTURE_2D;
    glTexSubImage2D(face, GLint(dst->mip_level), GLint(dst->x), GLint(dst->y), GLsizei(dst->w), GLsizei(dst->h),
                    t->fmt.format, t->fmt.type, src->transfer_buffer->data.data() + src->offset);
    if (row != dst->w) glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(t->target, 0);
}

void gt3r_gl_DownloadFromGPUTexture(SDL_GPUCopyPass* copy, const SDL_GPUTextureRegion* src,
                                    const SDL_GPUTextureTransferInfo* dst) {
    if (!g_device) return SDL_DownloadFromGPUTexture(copy, src, dst);
    if (!copy || !src || !dst || !src->texture || !dst->transfer_buffer || !src->texture->fmt.format) return;
    if (size_t(dst->offset) + size_t(src->w) * src->h * 4 > dst->transfer_buffer->data.size()) return;
    SDL_GPUDevice* dev = copy->cmd->dev;
    begin_gl(dev);
    const GLint saved = current_framebuffer();
    glBindFramebuffer(GL_FRAMEBUFFER, dev->scratch_read);
    attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src->texture, src->layer, src->mip_level);
    glReadPixels(GLint(src->x), GLint(src->y), GLsizei(src->w), GLsizei(src->h), GL_RGBA, GL_UNSIGNED_BYTE,
                 dst->transfer_buffer->data.data() + dst->offset);
    attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, nullptr, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(saved));
}

void gt3r_gl_CopyGPUTextureToTexture(SDL_GPUCopyPass* copy, const SDL_GPUTextureLocation* src,
                                     const SDL_GPUTextureLocation* dst, Uint32 w, Uint32 h, Uint32 d, bool cycle) {
    if (!g_device) return SDL_CopyGPUTextureToTexture(copy, src, dst, w, h, d, cycle);
    if (!copy || !src || !dst || !src->texture || !dst->texture) return;
    SDL_GPUDevice* dev = copy->cmd->dev;
    begin_gl(dev);
    const GLint saved = current_framebuffer();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, dev->scratch_read);
    attach(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src->texture, src->layer, src->mip_level);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dev->scratch_draw);
    attach(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dst->texture, dst->layer, dst->mip_level);
    glBlitFramebuffer(GLint(src->x), GLint(src->y), GLint(src->x + w), GLint(src->y + h), GLint(dst->x),
                      GLint(dst->y), GLint(dst->x + w), GLint(dst->y + h), GL_COLOR_BUFFER_BIT, GL_NEAREST);
    attach(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, nullptr, 0, 0);
    attach(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, nullptr, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(saved));
}

// Region to region, scaled with `filter` (no flips; the load op is the blit's own: every texel of
// the destination region is written).
void gt3r_gl_BlitGPUTexture(SDL_GPUCommandBuffer* cmd, const SDL_GPUBlitInfo* info) {
    if (!g_device) return SDL_BlitGPUTexture(cmd, info);
    if (!cmd || !info || !info->source.texture || !info->destination.texture) return;
    const SDL_GPUBlitRegion &src = info->source, &dst = info->destination;
    SDL_GPUDevice* dev = cmd->dev;
    begin_gl(dev);
    const GLint saved = current_framebuffer();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, dev->scratch_read);
    attach(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, src.texture, src.layer_or_depth_plane, src.mip_level);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dev->scratch_draw);
    attach(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, dst.texture, dst.layer_or_depth_plane, dst.mip_level);
    glBlitFramebuffer(GLint(src.x), GLint(src.y), GLint(src.x + src.w), GLint(src.y + src.h), GLint(dst.x),
                      GLint(dst.y), GLint(dst.x + dst.w), GLint(dst.y + dst.h), GL_COLOR_BUFFER_BIT,
                      info->filter == SDL_GPU_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
    attach(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, nullptr, 0, 0);
    attach(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, nullptr, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(saved));
}

void gt3r_gl_GenerateMipmapsForGPUTexture(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* t) {
    if (!g_device) return SDL_GenerateMipmapsForGPUTexture(cmd, t);
    if (!cmd || !t || !t->tex || t->info.num_levels < 2) return;
    begin_gl(cmd->dev);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(t->target, t->tex);
    glGenerateMipmap(t->target);
    glBindTexture(t->target, 0);
}

// ---------------------------------------------------------------------------
// Render passes (recorded; run at the end)
// ---------------------------------------------------------------------------

SDL_GPURenderPass* gt3r_gl_BeginGPURenderPass(SDL_GPUCommandBuffer* cmd, const SDL_GPUColorTargetInfo* colors,
                                              Uint32 num_colors, const SDL_GPUDepthStencilTargetInfo* depth) {
    if (!g_device) return SDL_BeginGPURenderPass(cmd, colors, num_colors, depth);
    if (!cmd) return nullptr;
    SDL_GPURenderPass& p = cmd->pass;
    p.open = true;
    p.has_color = num_colors > 0 && colors && colors[0].texture;
    if (p.has_color) p.color = colors[0];
    p.has_depth = depth && depth->texture;
    if (p.has_depth) p.depth = *depth;
    p.pipe = nullptr;
    p.vb = p.ib = 0;
    p.vb_offset = p.ib_offset = 0;
    for (int i = 0; i < kSlots; ++i) p.tex[i] = p.smp[i] = 0, p.tex_target[i] = 0;
    p.draws.clear();
    p.vp[2] = 0, p.sc[2] = -1; // a pass starts with the whole target
    cmd->arena.clear();
    for (auto& stage : cmd->uniforms)
        for (auto& u : stage) u.in_arena = false;
    return &p;
}

void gt3r_gl_EndGPURenderPass(SDL_GPURenderPass* pass) {
    if (!g_device) return SDL_EndGPURenderPass(pass);
    if (!pass || !pass->open) return;
    run_pass(pass);
    pass->open = false;
    pass->draws.clear();
}

void gt3r_gl_BindGPUGraphicsPipeline(SDL_GPURenderPass* pass, SDL_GPUGraphicsPipeline* pipeline) {
    if (!g_device) return SDL_BindGPUGraphicsPipeline(pass, pipeline);
    if (pass) pass->pipe = pipeline;
}

void gt3r_gl_BindGPUVertexBuffers(SDL_GPURenderPass* pass, Uint32 first_slot, const SDL_GPUBufferBinding* b, Uint32 n) {
    if (!g_device) return SDL_BindGPUVertexBuffers(pass, first_slot, b, n);
    if (!pass || !b || n == 0 || first_slot != 0) return; // one vertex buffer (slot 0)
    pass->vb = b[0].buffer ? b[0].buffer->buf : 0;
    pass->vb_offset = b[0].offset;
}

void gt3r_gl_BindGPUIndexBuffer(SDL_GPURenderPass* pass, const SDL_GPUBufferBinding* b, SDL_GPUIndexElementSize size) {
    if (!g_device) return SDL_BindGPUIndexBuffer(pass, b, size);
    if (!pass || !b) return;
    pass->ib = b->buffer ? b->buffer->buf : 0;
    pass->ib_offset = b->offset;
    pass->ib_type = size == SDL_GPU_INDEXELEMENTSIZE_32BIT ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
}

void gt3r_gl_BindGPUFragmentSamplers(SDL_GPURenderPass* pass, Uint32 first_slot, const SDL_GPUTextureSamplerBinding* b,
                                     Uint32 n) {
    if (!g_device) return SDL_BindGPUFragmentSamplers(pass, first_slot, b, n);
    if (!pass || !b) return;
    for (Uint32 i = 0; i < n && first_slot + i < kSlots; ++i) {
        const Uint32 k = first_slot + i;
        SDL_GPUTexture* t = b[i].texture;
        pass->tex[k] = t ? t->tex : 0;
        pass->tex_target[k] = t ? t->target : GL_TEXTURE_2D;
        pass->smp[k] = b[i].sampler ? b[i].sampler->smp : 0;
    }
}

void gt3r_gl_PushGPUVertexUniformData(SDL_GPUCommandBuffer* cmd, Uint32 slot, const void* data, Uint32 length) {
    if (!g_device) return SDL_PushGPUVertexUniformData(cmd, slot, data, length);
    push_uniform(cmd, 0, slot, data, length);
}

void gt3r_gl_PushGPUFragmentUniformData(SDL_GPUCommandBuffer* cmd, Uint32 slot, const void* data, Uint32 length) {
    if (!g_device) return SDL_PushGPUFragmentUniformData(cmd, slot, data, length);
    push_uniform(cmd, 1, slot, data, length);
}

void gt3r_gl_SetGPUViewport(SDL_GPURenderPass* pass, const SDL_GPUViewport* v) {
    if (!g_device) return SDL_SetGPUViewport(pass, v);
    if (!pass || !pass->open || !v) return;
    pass->vp[0] = GLint(std::lround(v->x)), pass->vp[1] = GLint(std::lround(v->y));
    pass->vp[2] = std::max(1, GLint(std::lround(v->w))), pass->vp[3] = std::max(1, GLint(std::lround(v->h)));
}

void gt3r_gl_SetGPUScissor(SDL_GPURenderPass* pass, const SDL_Rect* r) {
    if (!g_device) return SDL_SetGPUScissor(pass, r);
    if (!pass || !pass->open || !r) return;
    pass->sc[0] = r->x, pass->sc[1] = r->y, pass->sc[2] = std::max(0, r->w), pass->sc[3] = std::max(0, r->h);
}

void gt3r_gl_DrawGPUIndexedPrimitives(SDL_GPURenderPass* pass, Uint32 num_indices, Uint32 num_instances,
                                      Uint32 first_index, Sint32 vertex_offset, Uint32 first_instance) {
    if (!g_device)
        return SDL_DrawGPUIndexedPrimitives(pass, num_indices, num_instances, first_index, vertex_offset, first_instance);
    if (!pass || !pass->open || !pass->pipe || !pass->vb || !pass->ib || num_indices == 0) return;
    SDL_GPUCommandBuffer* cmd = pass->cmd;
    DrawRec d{};
    d.pipe = pass->pipe;
    d.vb = pass->vb;
    d.vb_offset = pass->vb_offset;
    d.ib = pass->ib;
    d.ib_offset = pass->ib_offset;
    d.ib_type = pass->ib_type;
    for (int i = 0; i < kSlots; ++i) d.tex[i] = pass->tex[i], d.tex_target[i] = pass->tex_target[i], d.smp[i] = pass->smp[i];
    for (int st = 0; st < 2; ++st) {
        const Uint32 n = st == 0 ? pass->pipe->vs_ubos : pass->pipe->fs_ubos;
        for (Uint32 k = 0; k < n; ++k) {
            d.ubo_offset[st][k] = arena_place(cmd, st, k);
            d.ubo_size[st][k] = Uint32(std::max<size_t>(16, (cmd->uniforms[st][k].data.size() + 15) / 16 * 16));
        }
    }
    d.count = num_indices;
    d.first_index = first_index;
    d.instances = num_instances;
    d.base_vertex = vertex_offset;
    for (int i = 0; i < 4; ++i) d.vp[i] = pass->vp[i], d.sc[i] = pass->sc[i];
    pass->draws.push_back(d);
}

#endif // GT3R_GLES
