#version 450
// Model vertex shader. SDL_GPU binding rules for SPIR-V vertex shaders:
// uniform buffers live in descriptor set 1.

#ifdef CLIPPED
// A corner of a triangle GT3's VU clips at the guard band (render/guard_band.h, View3D): the
// triangle's three vertices and the corner's weights of them (clip-space interpolation).
layout(location = 0) in vec3 a_pos0;
layout(location = 1) in vec3 a_normal0;
layout(location = 2) in vec2 a_uv0;
layout(location = 3) in vec4 a_color0;
layout(location = 4) in vec3 a_pos1;
layout(location = 5) in vec3 a_normal1;
layout(location = 6) in vec2 a_uv1;
layout(location = 7) in vec4 a_color1;
layout(location = 8) in vec3 a_pos2;
layout(location = 9) in vec3 a_normal2;
layout(location = 10) in vec2 a_uv2;
layout(location = 11) in vec4 a_color2;
layout(location = 12) in vec3 a_weights;
#else
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;
#endif

layout(set = 1, binding = 0) uniform VertexUniforms {
    mat4 mvp;
    mat4 model;      // object -> world
    mat4 normal_mat; // inverse-transpose of model (upper 3x3 used)
    vec4 view_z;     // the view matrix's z row: eye-space z = dot(xyz, world) + w
    vec4 fog;        // GT3 fog (z = 1): x distance A, y 1 - minimum visibility b; w > 0: GT3's
                     // guard band (DrawItem::guard_band), w its near plane in eye depth
    vec4 far_clip;   // x > 0: GT3's far plane in eye depth (DrawOptions::far_clip)
    // The SPIN_COPIES variant (a DrawItem::spin_turn rim's copies, one an instance): instance k
    // turned by -k x spin_axis.w about the axis spin_axis.xyz through spin_pivot (model space).
    vec4 spin_axis;
    vec4 spin_pivot;
} u;

// GT3's VU lighting, evaluated per vertex (set when the primitive has a scene::Material):
// the same rows as the fragment uniforms' GT3 lighting.
layout(set = 1, binding = 1) uniform VertexLighting {
    vec4 eye_dir;        // xyz: towards the viewer (world; the view's z axis: GT3's infinite
                         // viewer), w = 1: light this primitive per vertex; w = 2: the road
                         // sheen's program, xyz = its viewer's position (world)
    vec4 light_dir[4];   // xyz: towards the light (world), w = 1 enabled
    vec4 light_diffuse[4];
    vec4 light_specular[4];
    vec4 light_ambient;  // rgb
    vec4 mat_ambient;
    vec4 mat_diffuse;    // w: scale (1, or 255/128)
    vec4 mat_specular;   // w: exponent (0 = no specular term)
    vec4 mat_emission;   // w: colour factor after the clamp (1, or 128/255 untextured)
    // Environment passes (eye_dir.w = 3: chunk kind 2, 4: kind 3; eye_dir.xyz = the camera's
    // position): the VU's rows (kind 2: the view rotation; kind 3: the panorama's yaw), the
    // weight rows C0 / C1 and kind 3's constants (x 0.875 - (g + 1) / 8, y EC0 / 6, z 0.5 + EC4).
    vec4 env_rows[3];
    vec4 env_c0;
    vec4 env_c1;
    vec4 env_pano;
    vec4 vertex_color;   // x = 1: a_color's RGB are GS bytes (0x80 = 128), a factor up to 255/128
} l;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec2 v_uv;
// The vertex colour; times the clip w with the guard band (u.fog.w > 0): linear in screen space,
// as the GS interpolates it (model.frag).
layout(location = 2) out vec4 v_color_in;
layout(location = 3) out vec3 v_world;
// GT3's lighting factor (texel or 1 x this) and fog amount, per vertex. The GS interpolates both
// linearly in screen space, not perspective-correct: they go out times the clip w, and
// model.frag multiplies them by gl_FragCoord.w (1 / w), which turns the GPU's perspective-correct
// interpolation into a linear one in screen space, clipped triangles included. That is what
// noperspective does, but the shader compiler of a Mali-G52 driver (r25p1) crashed on it.
layout(location = 4) out vec3 v_lit_w;
// x: the fog amount times the clip w; y > 0: not drawn here (model.frag discards it), a triangle
// GT3 clips at the guard band in the static draw, or one it does not in the clipped draw; for the
// other items the depth past GT3's far plane (cut there, as the GPU cuts at its own).
layout(location = 5) out vec2 v_fog_w;
#ifdef SPIN_COPIES
layout(location = 6) flat out int v_copy; // the copy (model.frag's spin_copy_samples)
#endif

// One vertex as GT3's VU program leaves it: the clip position, the colour and the lighting factor.
struct Vert {
    vec4 clip;
    vec3 world;
    vec3 normal;
    vec2 uv;
    vec4 color;
    vec3 lit;
};

Vert vertex(vec3 pos, vec3 nrm, vec2 uv, vec4 a_col) {
    Vert o;
    o.clip = u.mvp * vec4(pos, 1.0);
    o.world = (u.model * vec4(pos, 1.0)).xyz;
    o.normal = mat3(u.normal_mat) * nrm;
    o.uv = uv;
    // The GS's texel x colour >> 7: 0x80 = 1.0 and past it up to 255/128 (scene::kVertexColor2x);
    // other packets store 0x80 as 1.0.
    vec4 color = l.vertex_color.x > 0.5 ? vec4(a_col.rgb * (255.0 / 128.0), a_col.a) : a_col;
    o.color = color;
    // GT3: Me + La Ma + sum Ld Md max(N.L, 0) + sum Ls Ms max(N.H, 0)^p (H with the infinite
    // viewer), times the packet scale, clamped to 255/128 per vertex; the GS interpolates
    // the result across the triangle (Gouraud) and modulates the texel with it.
    o.lit = vec3(1.0);
    if (l.eye_dir.w > 2.5) {
        // GT3's environment programs (course chunk kinds 2 and 3): from the camera E,
        // D = vertex - E, R = D - 2 (D.N) N with the normal as stored (the UV slot's).
        vec3 n = o.normal;
        vec3 D = o.world - l.eye_dir.xyz;
        float p = 1.0 / max(length(D), 1e-6);
        vec3 R = D - 2.0 * dot(D, n) * n;
        if (l.eye_dir.w < 3.5) {
            // Kind 2: a sphere map in view space, R_eye = the view rotation x R / |D|,
            // m = |R_eye + (0, 0, 1)|, s = 0.5 + 0.5 Rx / m, t = 0.5 - 0.5 Ry / m.
            vec3 e = vec3(dot(l.env_rows[0].xyz, R), dot(l.env_rows[1].xyz, R), dot(l.env_rows[2].xyz, R)) * p;
            float m = max(length(e + vec3(0.0, 0.0, 1.0)), 1e-6);
            o.uv = vec2(0.5 + 0.5 * e.x / m, 0.5 - 0.5 * e.y / m);
        } else {
            // Kind 3: the panoramic lookup. r = R in the yawed frame; u from the quadrant of
            // (r.x - r.z, r.x + r.z), continuous mod 8; y = r.y / |D|, v = y^3 + 2 y.
            vec3 r = R.x * l.env_rows[0].xyz + R.y * l.env_rows[1].xyz + R.z * l.env_rows[2].xyz;
            float a = r.x - r.z, b = r.x + r.z, u;
            if (a >= 0.0 && b >= 0.0) u = r.z / r.x;
            else if (a < 0.0 && b >= 0.0) u = 2.0 - r.x / r.z;
            else if (a < 0.0) u = 4.0 + r.z / r.x;
            else u = 6.0 - r.x / r.z;
            float y = r.y * p;
            o.uv = vec2(l.env_pano.x - u / 8.0, l.env_pano.z - l.env_pano.y * (y * y * y + 2.0 * y));
        }
        // The colour, alpha too: c_v x (C0 + C1 min(D.N / |D|, 0)).
        o.color = color * (l.env_c0 + l.env_c1 * min(dot(D, n) * p, 0.0));
    } else if (l.eye_dir.w > 1.5) {
        // The road sheen's VU program (course chunk kind 4): the first light's specular term
        // only, Phong with the reflected ray from the viewer: D = vertex - viewer,
        // R = D - 2 (D.N) N, c = max(0, L.R / |D|)^p (the normal as stored). The byte
        // s Ls Ms c (s = 128 or 255) is not clamped: the GS takes its low 8 bits.
        vec3 n = o.normal;
        vec3 D = o.world - l.eye_dir.xyz;
        vec3 R = D - 2.0 * dot(D, n) * n;
        float c = max(dot(normalize(l.light_dir[0].xyz), R) / max(length(D), 1e-6), 0.0);
        vec3 byte_ = floor(l.light_specular[0].rgb * l.mat_specular.rgb * (128.0 * l.mat_diffuse.w) *
                           pow(c, l.mat_specular.w));
        o.lit = mod(byte_, 256.0) / 128.0 * l.mat_emission.w;
    } else if (l.eye_dir.w > 0.5) {
        vec3 n = normalize(o.normal);
        vec3 E = normalize(l.eye_dir.xyz);
        vec3 lit = l.mat_emission.rgb + l.light_ambient.rgb * l.mat_ambient.rgb;
        for (int i = 0; i < 4; ++i) {
            if (l.light_dir[i].w < 0.5) continue;
            vec3 L = normalize(l.light_dir[i].xyz);
            lit += l.light_diffuse[i].rgb * l.mat_diffuse.rgb * max(dot(n, L), 0.0);
            if (l.mat_specular.w > 0.0)
                lit += l.light_specular[i].rgb * l.mat_specular.rgb *
                       pow(max(dot(n, normalize(L + E)), 0.0), l.mat_specular.w);
        }
        o.lit = clamp(lit * l.mat_diffuse.w, 0.0, 255.0 / 128.0) * l.mat_emission.w;
    }
    return o;
}

// 1 - F / 255, from the view depth W like the VU's transform: F = trunc(b' + 0.1 +
// (255 - b') min(W, A) / W), b' = 255 b. A corner GT3 clips gets its own (from its own W).
float fog_at(vec3 world) {
    if (u.fog.z < 0.5) return 0.0;
    float W = max(-(dot(u.view_z.xyz, world) + u.view_z.w), 1e-4);
    float b = clamp(255.0 * (1.0 - u.fog.y), 0.0, 255.0);
    float F = floor(b + 0.1 + (255.0 - b) * min(W, u.fog.x) / W);
    return 1.0 - min(F, 255.0) / 255.0;
}

// GT3's guard band (render/guard_band.h): beyond |x| = 2 w or |y| = 2 w, nearer than its near plane
// or farther than its far plane.
bool outside(vec4 c) {
    return c.w < u.fog.w || abs(c.x) > 2.0 * c.w || abs(c.y) > 2.0 * c.w || (u.far_clip.x > 0.0 && c.w > u.far_clip.x);
}

void main() {
#ifdef CLIPPED
    Vert a = vertex(a_pos0, a_normal0, a_uv0, a_color0);
    Vert b = vertex(a_pos1, a_normal1, a_uv1, a_color1);
    Vert c = vertex(a_pos2, a_normal2, a_uv2, a_color2);
    vec3 k = a_weights;
    gl_Position = k.x * a.clip + k.y * b.clip + k.z * c.clip;
    v_world = k.x * a.world + k.y * b.world + k.z * c.world;
    v_normal = k.x * a.normal + k.y * b.normal + k.z * c.normal;
    v_uv = k.x * a.uv + k.y * b.uv + k.z * c.uv;
    vec4 color = k.x * a.color + k.y * b.color + k.z * c.color;
    vec3 lit = k.x * a.lit + k.y * b.lit + k.z * c.lit;
    if (max(k.x, max(k.y, k.z)) < 1.0) {
        // A new corner: the VU lerps the GS bytes (0x80 = 1.0) and rounds them (+ 0.5, truncated).
        color = floor(color * 128.0 + 0.5) / 128.0;
        float e = l.mat_emission.w > 0.0 ? l.mat_emission.w : 1.0;
        lit = floor(lit / e * 128.0 + 0.5) / 128.0 * e;
    }
    // The static draw has this triangle unless the GPU sees it cross the guard band too.
    float drop = outside(a.clip) || outside(b.clip) || outside(c.clip) ? 0.0 : 1.0;
#else
    vec3 pos = a_pos, normal = a_normal;
#ifdef SPIN_COPIES
    v_copy = gl_InstanceIndex;
    float t = -u.spin_axis.w * float(gl_InstanceIndex), co = cos(t), si = sin(t);
    vec3 ax = u.spin_axis.xyz, d = pos - u.spin_pivot.xyz;
    pos = u.spin_pivot.xyz + d * co + cross(ax, d) * si + ax * dot(ax, d) * (1.0 - co);
    normal = normal * co + cross(ax, normal) * si + ax * dot(ax, normal) * (1.0 - co);
#endif
    Vert o = vertex(pos, normal, a_uv, a_color);
    gl_Position = o.clip;
    v_world = o.world;
    v_normal = o.normal;
    v_uv = o.uv;
    vec4 color = o.color;
    vec3 lit = o.lit;
    // The clipped draw has the triangles with a vertex beyond the guard band (View3D); the other
    // items are cut at the far plane.
    float drop = u.fog.w > 0.0 ? (outside(o.clip) ? 1.0 : 0.0) : u.far_clip.x > 0.0 ? o.clip.w - u.far_clip.x : 0.0;
#endif
    v_color_in = u.fog.w > 0.0 ? color * gl_Position.w : color;
    v_lit_w = lit * gl_Position.w;
    v_fog_w = vec2(fog_at(v_world) * gl_Position.w, drop);
}
