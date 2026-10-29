#version 450
// Model vertex shader. SDL_GPU binding rules for SPIR-V vertex shaders:
// uniform buffers live in descriptor set 1.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_color;

layout(set = 1, binding = 0) uniform VertexUniforms {
    mat4 mvp;
    mat4 model;      // object -> world
    mat4 normal_mat; // inverse-transpose of model (upper 3x3 used)
    vec4 view_z;     // the view matrix's z row: eye-space z = dot(xyz, world) + w
    vec4 fog;        // GT3 fog (z = 1): x distance A, y 1 - minimum visibility b
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
} l;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec4 v_color;
layout(location = 3) out vec3 v_world;
// GT3's lighting factor (texel or 1 x this) and fog amount, per vertex. The GS interpolates both
// linearly in screen space, not perspective-correct: they go out times the clip w, and
// model.frag multiplies them by gl_FragCoord.w (1 / w), which turns the GPU's perspective-correct
// interpolation into a linear one in screen space, clipped triangles included. That is what
// noperspective does, but the shader compiler of a Mali-G52 driver (r25p1) crashed on it.
layout(location = 4) out vec3 v_lit_w;
layout(location = 5) out float v_fog_w;

vec3 v_lit;
// 1 - F / 255, from the view depth W like the VU's transform: F = trunc(b' + 0.1 +
// (255 - b') min(W, A) / W), b' = 255 b.
float v_fog;

void main() {
    gl_Position = u.mvp * vec4(a_pos, 1.0);
    v_world = (u.model * vec4(a_pos, 1.0)).xyz;
    v_normal = mat3(u.normal_mat) * a_normal;
    v_uv = a_uv;
    v_color = a_color;
    v_fog = 0.0;
    if (u.fog.z > 0.5) {
        float W = max(-(dot(u.view_z.xyz, v_world) + u.view_z.w), 1e-4);
        float b = clamp(255.0 * (1.0 - u.fog.y), 0.0, 255.0);
        float F = floor(b + 0.1 + (255.0 - b) * min(W, u.fog.x) / W);
        v_fog = 1.0 - min(F, 255.0) / 255.0;
    }
    // GT3: Me + La Ma + sum Ld Md max(N.L, 0) + sum Ls Ms max(N.H, 0)^p (H with the infinite
    // viewer), times the packet scale, clamped to 255/128 per vertex; the GS interpolates
    // the result across the triangle (Gouraud) and modulates the texel with it.
    v_lit = vec3(1.0);
    if (l.eye_dir.w > 2.5) {
        // GT3's environment programs (course chunk kinds 2 and 3): from the camera E,
        // D = vertex - E, R = D - 2 (D.N) N with the normal as stored (the UV slot's).
        vec3 n = v_normal;
        vec3 D = v_world - l.eye_dir.xyz;
        float p = 1.0 / max(length(D), 1e-6);
        vec3 R = D - 2.0 * dot(D, n) * n;
        if (l.eye_dir.w < 3.5) {
            // Kind 2: a sphere map in view space, R_eye = the view rotation x R / |D|,
            // m = |R_eye + (0, 0, 1)|, s = 0.5 + 0.5 Rx / m, t = 0.5 - 0.5 Ry / m.
            vec3 e = vec3(dot(l.env_rows[0].xyz, R), dot(l.env_rows[1].xyz, R), dot(l.env_rows[2].xyz, R)) * p;
            float m = max(length(e + vec3(0.0, 0.0, 1.0)), 1e-6);
            v_uv = vec2(0.5 + 0.5 * e.x / m, 0.5 - 0.5 * e.y / m);
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
            v_uv = vec2(l.env_pano.x - u / 8.0, l.env_pano.z - l.env_pano.y * (y * y * y + 2.0 * y));
        }
        // The colour, alpha too: c_v x (C0 + C1 min(D.N / |D|, 0)).
        v_color = a_color * (l.env_c0 + l.env_c1 * min(dot(D, n) * p, 0.0));
    } else if (l.eye_dir.w > 1.5) {
        // The road sheen's VU program (course chunk kind 4): the first light's specular term
        // only, Phong with the reflected ray from the viewer: D = vertex - viewer,
        // R = D - 2 (D.N) N, c = max(0, L.R / |D|)^p (the normal as stored). The byte
        // s Ls Ms c (s = 128 or 255) is not clamped: the GS takes its low 8 bits.
        vec3 n = v_normal;
        vec3 D = v_world - l.eye_dir.xyz;
        vec3 R = D - 2.0 * dot(D, n) * n;
        float c = max(dot(normalize(l.light_dir[0].xyz), R) / max(length(D), 1e-6), 0.0);
        vec3 byte_ = floor(l.light_specular[0].rgb * l.mat_specular.rgb * (128.0 * l.mat_diffuse.w) *
                           pow(c, l.mat_specular.w));
        v_lit = mod(byte_, 256.0) / 128.0 * l.mat_emission.w;
    } else if (l.eye_dir.w > 0.5) {
        vec3 n = normalize(v_normal);
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
        v_lit = clamp(lit * l.mat_diffuse.w, 0.0, 255.0 / 128.0) * l.mat_emission.w;
    }
    v_lit_w = v_lit * gl_Position.w;
    v_fog_w = v_fog * gl_Position.w;
}
