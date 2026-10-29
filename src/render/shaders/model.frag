#version 450
// Model fragment shader. SDL_GPU binding rules for SPIR-V fragment shaders:
// sampled textures in set 2, uniform buffers in set 3.

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec4 v_color;
layout(location = 3) in vec3 v_world;
// GT3's lighting factor and fog amount per vertex, times the clip w (model.vert): with
// gl_FragCoord.w (1 / w), interpolated linearly in screen space like the GS.
layout(location = 4) in vec3 v_lit_w;
layout(location = 5) in float v_fog_w;
vec3 v_lit;
float v_fog;

layout(set = 2, binding = 0) uniform sampler2D u_tex;
layout(set = 2, binding = 1) uniform samplerCube u_env; // car reflections (env_ground.w = 1: valid)
layout(set = 2, binding = 2) uniform sampler2D u_sphere; // car reflections, eye-space sphere map (env_ground.w = 3)

layout(set = 3, binding = 0) uniform FragmentUniforms {
    vec4 light_dir;   // xyz: direction towards the light (world), w: ambient
    vec4 camera_pos;  // xyz: eye position (world)
    vec4 params;      // x: alpha test ref (<0 = off, >= 2: inverted, ref + 2), y: unlit, z: env map / sheen strength,
                      // w: 1 = flat colour override, 2 = specular sheen layer (additive),
                      //    3 = environment reflection layer (additive, untextured: car paint/glass)
                      //    4 = sphere map from the bound texture (prize scenes)
                      //    5 = the post pass (View3D: a picture over the frame, gt3_grade)
    vec4 flat_color;  // used when params.w > 0 (wireframe overlay)
    vec4 fog_color;   // rgb: fog colour, w: 1 = fog enabled for this draw
    vec4 fog_params;  // x: start distance, y: 1 / (end - start), z: max fog amount
    vec4 tint;        // multiplies the final colour (DrawItem::alpha for ghosts)
    vec4 view_row0;   // view matrix rows 0 and 1 (world -> view x / y), for params.w = 4
    vec4 view_row1;
    vec4 grade;       // the post pass's grade (params.w = 5): rgb curve g per channel, w saturation
    vec4 light_ambient; // rgb; w = 1: GT3 car lighting (texel x min(ambient + diffuse N.L, 2))
    vec4 light_diffuse; // rgb
    vec4 env_sky;       // environment for car reflections: rgb above the horizon; w = Fresnel C1
    vec4 env_horizon;
    vec4 env_ground;    // w = 1: sample u_env instead of the gradient (2: GT3's lookup), 3: u_sphere
    vec4 light_specular; // rgb
    vec4 mat_ambient;    // scene::Material (w = 1: valid; then the GT3 fixed-function formula)
    vec4 mat_diffuse;    // w: scale (1, or 255/128)
    vec4 mat_specular;   // w: exponent (0 = no specular term)
    vec4 mat_emission;   // w: colour factor after the clamp (1, or 128/255 untextured)
    // Extra directional lights for the GT3 lighting (showroom fills): xyz towards the
    // light (world), w = 1 enabled; their diffuse / specular colours.
    vec4 light2_dir;
    vec4 light2_diffuse;
    vec4 light2_specular;
    vec4 light3_dir;
    vec4 light3_diffuse;
    vec4 light3_specular;
    vec4 light4_dir;
    vec4 light4_diffuse;
    vec4 light4_specular;
    vec4 grade2;         // the post pass: x black, y white (levels), z 1 = grade on;
                         // w 1 = write alpha 1 (coverage) for this opaque primitive
    vec4 env_c0;         // GT3 reflection weight rows: c0 + c1 min(V.N, 0) per channel;
    vec4 env_c1;         // env_c0.w = 1: valid (else the scalar env_sky.w term)
    vec4 output_mode;    // x = 1: store 5 bits per channel (GT3's PSMCT16 reflection map)
} u;

// GT3's per-vertex reflection weight (VU kernel): C0 + C1 min(V.N, 0), V the unit vector
// from the eye. Without decoded rows: 1 + C1 min(V.N, 0) with the scalar C1 in env_sky.w.
vec3 env_weight(vec3 view, vec3 n) {
    float vn = min(dot(view, n), 0.0);
    if (u.env_c0.w > 0.5) return u.env_c0.rgb + u.env_c1.rgb * vn;
    return vec3(1.0 + u.env_sky.w * vn);
}

layout(location = 0) out vec4 o_color;

// Environment for the car reflection passes: a sky / horizon / ground gradient (the
// renderer's stand-in for GT3's per-frame sphere map of the course around the car).
vec3 env_color(vec3 dir) {
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    return t < 0.5 ? mix(u.env_ground.rgb, u.env_horizon.rgb, t * 2.0)
                   : mix(u.env_horizon.rgb, u.env_sky.rgb, t * 2.0 - 1.0);
}

// Fog amount at distance d. fog_params.w < 0.5: linear (x start, y 1/(end-start), z max);
// otherwise GT3's curve (course Env0), v_fog from model.vert: visibility = b + (1 - b) min(1, A / W)
// per vertex, W the view depth, i.e. no fog up to A, then a 1/W approach to 1 - b.
// GT3's grade pass over the finished picture (presentation §G6), in its 8-bit arithmetic. A
// 256-entry table per output channel c: t = clamp((i / 255 - black) / (white - black), 0, 1)
// (entry 0 unclamped), L_c[i] = trunc(255 (t^2 + 2 g_c t (1 - t))) clamped; a saturation
// matrix M[c][k] = trunc(s' + m w_k) for c = k, trunc(m w_k) otherwise (s' = min(128 s, 255),
// m = max(128 - s', 0), w = 0.30 0.59 0.11); out_c = min(255, sum_k (L_c[in_k] M[c][k]) >> 7).
vec3 grade_table(float i, float inv) {
    float t = (i / 255.0 - u.grade2.x) * inv;
    if (i > 0.5) t = clamp(t, 0.0, 1.0);
    vec3 g = clamp(u.grade.rgb, 0.0, 1.0);
    return floor(clamp(255.0 * (t * t + 2.0 * g * t * (1.0 - t)), 0.0, 255.0));
}

vec3 gt3_grade(vec3 c) {
    vec3 b = floor(clamp(c, 0.0, 1.0) * 255.0 + 0.5);
    float d = u.grade2.y - u.grade2.x;
    float inv = d == 0.0 ? 0.0 : 1.0 / d;
    vec3 lr = grade_table(b.r, inv), lg = grade_table(b.g, inv), lb = grade_table(b.b, inv);
    float sp = min(128.0 * u.grade.w, 255.0);
    float m = max(128.0 - sp, 0.0);
    vec3 w = vec3(0.30, 0.59, 0.11);
    vec3 off = floor(m * w), diag = floor(clamp(sp + m * w, 0.0, 255.0));
    // Lane k (input channel k) weights output c by diag_k when c == k, else off_k.
    vec3 r = floor(lr * vec3(diag.r, off.r, off.r) / 128.0);
    vec3 gg = floor(lg * vec3(off.g, diag.g, off.g) / 128.0);
    vec3 bb = floor(lb * vec3(off.b, off.b, diag.b) / 128.0);
    return min(r + gg + bb, vec3(255.0)) / 255.0;
}

float fog_amount(float d) {
    if (u.fog_params.w > 0.5) return v_fog; // GT3: per vertex from the view depth (model.vert)
    return clamp((d - u.fog_params.x) * u.fog_params.y, 0.0, 1.0) * u.fog_params.z;
}

void shade() {
    if (u.params.w > 4.5) {
        // The post pass (View3D): a finished picture drawn over the frame, graded when asked,
        // at the weight tint.a (the dissolve's frozen picture, the trail's previous one).
        vec3 c = texture(u_tex, v_uv).rgb;
        if (u.grade2.z > 0.5) c = gt3_grade(c);
        o_color = vec4(c, u.tint.a);
        return;
    }
    if (u.params.w > 0.5 && u.params.w < 1.5) {
        o_color = u.flat_color;
        return;
    }
    if (u.params.w > 3.5) {
        // Sphere map stored with the model (prize scenes), GT3's reflection kernel: the
        // reflected view vector in view space, s = 0.5 + 0.5 Rx / m, t = 0.5 - 0.5 Ry / m,
        // m = |R + (0, 0, 1)|.
        vec3 n = normalize(v_normal);
        if (!gl_FrontFacing) n = -n;
        vec3 view = normalize(v_world - u.camera_pos.xyz);
        vec3 rw = reflect(view, n);
        vec3 r = vec3(dot(u.view_row0.xyz, rw), dot(u.view_row1.xyz, rw),
                      dot(cross(u.view_row0.xyz, u.view_row1.xyz), rw));
        float m = max(length(r + vec3(0.0, 0.0, 1.0)), 1e-4);
        vec2 uv = vec2(0.5 + 0.5 * r.x / m, 0.5 - 0.5 * r.y / m);
        // Vertex colour x the reflection weight, an 8-bit GS colour (<= 255/128); then PS2
        // MODULATE: texture x colour (0x80 = 1.0, already expanded by the decoder).
        vec4 vc = vec4(min(v_color.rgb * env_weight(view, n), vec3(255.0 / 128.0)), v_color.a);
        vec4 c = texture(u_tex, uv) * vc * u.tint;
        if (u.params.z > 0.5) c.a = v_color.a * u.tint.a; // additive layer: full weight (Ad = 1)
        o_color = c;
        if (u.grade2.w > 0.5) o_color.a = 1.0;
        return;
    }
    if (u.params.w > 2.5) {
        // Car reflection pass (PS2 ALPHA Cs*Ad + Cd): environment colour times the
        // pass colour, weighted through SRC_ALPHA blending by the reflectivity Ad = the
        // base pass's texel alpha (bound texture; white when none) x vertex alpha.
        vec3 n = normalize(v_normal);
        if (!gl_FrontFacing) n = -n;
        vec3 view = normalize(v_world - u.camera_pos.xyz);
        // GT3's per-vertex weight C0 + C1 min(V.N, 0): e.g. 1 - 0.8 cos for car paint.
        vec3 w = env_weight(view, n);
        vec3 r = reflect(view, n);
        if (u.env_ground.w > 1.5 && u.env_ground.w < 2.5) {
            // GT3's sphere-map lookup in eye space: compressed angle, 15 degree tilt.
            vec3 x = u.view_row0.xyz, y = u.view_row1.xyz, z = cross(x, y);
            vec3 e = vec3(dot(x, r), dot(y, r), dot(z, r));
            float th = acos(clamp(e.z, -1.0, 1.0)), ph = atan(e.y, e.x);
            float th2 = 2.0 * asin(0.9 * sin(0.5 * th));
            vec3 d = vec3(sin(th2) * cos(ph), sin(th2) * sin(ph), cos(th2));
            d = vec3(d.x, 0.966 * d.y + 0.259 * d.z, -0.259 * d.y + 0.966 * d.z);
            r = x * d.x + y * d.y + z * d.z;
        }
        // GT3's map is PSMCT16: the cube holds 5-bit texels (output_mode, set while the map is
        // rendered) and the lookup filters between them, as the GS does.
        vec3 env = u.env_ground.w > 0.5 ? texture(u_env, r).rgb : env_color(r);
        if (u.env_ground.w > 2.5) {
            // A fixed sphere map in eye space (menu showrooms): GT3's reflection kernel.
            vec3 x = u.view_row0.xyz, y = u.view_row1.xyz;
            vec3 e = vec3(dot(x, r), dot(y, r), dot(cross(x, y), r));
            float m = max(length(e + vec3(0.0, 0.0, 1.0)), 1e-4);
            env = texture(u_sphere, vec2(0.5 + 0.5 * e.x / m, 0.5 - 0.5 * e.y / m)).rgb;
        }
        vec3 rgb = env * min(v_color.rgb * w, vec3(255.0 / 128.0)) * u.params.z;
        if (u.fog_color.w > 0.0) {
            float d = length(v_world - u.camera_pos.xyz);
            rgb *= 1.0 - fog_amount(d);
        }
        o_color = vec4(rgb, v_color.a * texture(u_tex, v_uv).a * u.tint.a);
        return;
    }
    vec4 c = texture(u_tex, v_uv) * v_color;
    if (u.params.w > 1.5) {
        // Road sheen: the texel modulated by its VU program's lit colour (model.vert), fogged
        // to black (the node program sets the fog colour to 0 for this pass); added weighted by
        // the frame-buffer alpha (Cs * Ad + Cd) or, without one, by its own alpha.
        c.rgb = texture(u_tex, v_uv).rgb * v_lit;
        if (u.fog_color.w > 0.0) {
            float d = length(v_world - u.camera_pos.xyz);
            c.rgb *= 1.0 - fog_amount(d);
        }
        o_color = c;
        return;
    }
    // Alpha test: discard alpha <= x; x >= 2: the inverse (keep only alpha <= x - 2), the
    // second half of a GS AFAIL = RGB_ONLY primitive.
    if (u.params.x >= 0.0 && (u.params.x < 2.0 ? c.a <= u.params.x : c.a > u.params.x - 2.0)) discard;
    if (u.params.y < 0.5) {
        vec3 n = normalize(v_normal);
        if (!gl_FrontFacing) n = -n;
        vec3 L = normalize(u.light_dir.xyz);
        float diffuse = max(dot(n, L), 0.0);
        if (u.mat_ambient.w > 0.5) {
            // GT3 VU lighting, computed per vertex (model.vert) and interpolated as the GS does.
            c.rgb *= v_lit;
        } else if (u.light_ambient.w > 0.5) {
            vec3 lit = u.light_ambient.rgb + u.light_diffuse.rgb * diffuse;
            if (u.light2_dir.w > 0.5) lit += u.light2_diffuse.rgb * max(dot(n, normalize(u.light2_dir.xyz)), 0.0);
            if (u.light3_dir.w > 0.5) lit += u.light3_diffuse.rgb * max(dot(n, normalize(u.light3_dir.xyz)), 0.0);
            if (u.light4_dir.w > 0.5) lit += u.light4_diffuse.rgb * max(dot(n, normalize(u.light4_dir.xyz)), 0.0);
            c.rgb *= min(lit, vec3(2.0));
        }
        else
            c.rgb *= u.light_dir.w + (1.0 - u.light_dir.w) * diffuse;
        if (u.params.z > 0.0) {
            vec3 view = normalize(v_world - u.camera_pos.xyz);
            vec3 r = reflect(view, n);
            c.rgb = mix(c.rgb, env_color(r), u.params.z);
        }
    }
    if (u.fog_color.w > 0.0) {
        float d = length(v_world - u.camera_pos.xyz);
        float f = fog_amount(d);
        c.rgb = mix(c.rgb, u.fog_color.rgb, f);
    }
    o_color = c * u.tint;
    if (u.grade2.w > 0.5) o_color.a = 1.0;
}

void main() {
    v_lit = v_lit_w * gl_FragCoord.w;
    v_fog = v_fog_w * gl_FragCoord.w;
    shade();
    // GT3's reflection map is a PSMCT16 frame buffer: each write keeps c8 >> 3 (read back as
    // c5 << 3).
    if (u.output_mode.x > 0.5) o_color.rgb = floor(clamp(o_color.rgb, 0.0, 1.0) * (255.0 / 8.0) + 1e-4) * (8.0 / 255.0);
}
