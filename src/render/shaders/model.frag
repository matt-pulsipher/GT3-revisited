#version 450
// Model fragment shader. SDL_GPU binding rules for SPIR-V fragment shaders:
// sampled textures in set 2, uniform buffers in set 3.

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec4 v_color_in;
layout(location = 3) in vec3 v_world;
// GT3's lighting factor and fog amount per vertex, times the clip w (model.vert): with
// gl_FragCoord.w (1 / w), interpolated linearly in screen space like the GS. v_fog_w.y > 0: the
// triangle is drawn by the other draw (GT3's guard band: model.vert), or the point lies past GT3's
// far plane.
layout(location = 4) in vec3 v_lit_w;
layout(location = 5) in vec2 v_fog_w;
vec4 v_color; // the vertex colour (u.output_mode.y = 1: it came times the clip w, as v_lit_w)
#ifdef SPIN_COPIES
layout(location = 6) flat in int v_copy; // the copy (model.vert's instance)
#endif
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
                      //    6 = the depth of field's resampling (View3D::encode_dof, dof_sample)
    vec4 flat_color;  // used when params.w > 0 (wireframe overlay); params.w = 6: xy the taps' Gaussian
                      // spreads; params.w = 5: w = 1 the haze lens at the field's size (view_row0/1)
    vec4 fog_color;   // rgb: fog colour, w: 1 = fog enabled for this draw
    vec4 fog_params;  // x: start distance, y: 1 / (end - start), z: max fog amount
    vec4 tint;        // multiplies the final colour (DrawItem::alpha for ghosts)
    vec4 view_row0;   // view matrix rows 0 and 1 (world -> view x / y), for params.w = 4;
    vec4 view_row1;   // params.w = 6: the texture coordinates v_uv x row0.xy + row0.zw, clamped
                      // to the rectangle row1 (min xy, max zw)
    vec4 grade;       // the post pass's grade (params.w = 5): rgb curve g per channel, w saturation;
                      // params.w = 6: xy taps across / down, zw their spacing (texture units)
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
    vec4 output_mode;    // x = 1: store 5 bits per channel (GT3's PSMCT16 reflection map);
                         // y = 1: the vertex colour is linear in screen space (GT3's guard band);
                         // z = 1: the texture filtered as the GS does (gs_bilinear below);
                         // w = 1: a car reflection pass with GS TEST.DATE (drawn where Ad < 0x80)
    // GT3's wheel spin blurs (DrawItem::wheel_blur; wheel_texture below): x 1 = the tyre's tread,
    // 2 = the rim; y the rim's mip level. wheel_steps: the tread's three S shifts / the rim's
    // four angles. The SPIN_COPIES variant: w the copies' cells (DrawItem::spin_turn).
    vec4 wheel_blur;
    vec4 wheel_steps;
} u;

// GT3's per-vertex reflection weight (VU kernel): C0 + C1 min(V.N, 0), V the unit vector
// from the eye. Without decoded rows: 1 + C1 min(V.N, 0) with the scalar C1 in env_sky.w.
vec3 env_weight(vec3 view, vec3 n) {
    float vn = min(dot(view, n), 0.0);
    if (u.env_c0.w > 0.5) return u.env_c0.rgb + u.env_c1.rgb * vn;
    return vec3(1.0 + u.env_sky.w * vn);
}

layout(location = 0) out vec4 o_color;

// ---- GT3's wheel spin blurs (race/wheel_fx.h) ----
// GT3 redraws a spinning wheel's texture in a work buffer as the mean of copies of itself, each
// pass blending the buffer at 50 % with a copy shifted (the tyre) or turned (the rim) by half the
// last pass's amount. The result is the mean over every sum of a subset of the passes' amounts,
// which these taps read directly. Tread: 8 copies shifted along S. Rim: 16 copies turned about the
// image centre (the GS draws copy k turned by a_k, so it shows the image at R(-a_k)), read at the
// mip level of GT3's square of min(width, 64) texels.
vec2 rim_turn(vec2 r, int k, int bit, float a) {
    if ((k & bit) == 0) return r;
    float c = cos(a), s = sin(a);
    return vec2(r.x * c - r.y * s, r.x * s + r.y * c);
}

// The texture as the GS filters it (DrawItem::gs_bilinear): the four texels around uv (clamped)
// weighted by the 4-bit fractions of its position, across then down, each lerp truncated to a
// whole 8-bit level, a + ((b - a) f >> 4). The texture holds whole levels (an offscreen buffer).
vec4 gs_bilinear(vec2 uv) {
    ivec2 size = textureSize(u_tex, 0);
    vec2 t = uv * vec2(size) - 0.5, i = floor(t);
    vec2 f = min(floor((t - i) * 16.0 + 1e-3), vec2(15.0));
    ivec2 p0 = clamp(ivec2(i), ivec2(0), size - 1), p1 = clamp(ivec2(i) + 1, ivec2(0), size - 1);
    vec4 a = floor(texelFetch(u_tex, p0, 0) * 255.0 + 0.5), b = floor(texelFetch(u_tex, ivec2(p1.x, p0.y), 0) * 255.0 + 0.5);
    vec4 c = floor(texelFetch(u_tex, ivec2(p0.x, p1.y), 0) * 255.0 + 0.5), d = floor(texelFetch(u_tex, p1, 0) * 255.0 + 0.5);
    vec4 top = a + floor((b - a) * f.x / 16.0), bottom = c + floor((d - c) * f.x / 16.0);
    return (top + floor((bottom - top) * f.y / 16.0)) / 255.0;
}

vec4 wheel_texture(vec2 uv) {
    if (u.output_mode.z > 0.5) return gs_bilinear(uv);
    if (u.wheel_blur.x < 0.5) return texture(u_tex, uv);
    vec4 sum = vec4(0.0);
    if (u.wheel_blur.x < 1.5) {
        for (int k = 0; k < 8; ++k) {
            float s = ((k & 1) != 0 ? u.wheel_steps.x : 0.0) + ((k & 2) != 0 ? u.wheel_steps.y : 0.0) +
                      ((k & 4) != 0 ? u.wheel_steps.z : 0.0);
            sum += texture(u_tex, vec2(uv.x + s, uv.y));
        }
        return sum * 0.125;
    }
    vec2 d = uv - vec2(0.5);
    for (int k = 0; k < 16; ++k) {
        vec2 r = vec2(1.0, 0.0); // (cos, sin) of the copy's angle
        r = rim_turn(r, k, 1, u.wheel_steps.x);
        r = rim_turn(r, k, 2, u.wheel_steps.y);
        r = rim_turn(r, k, 4, u.wheel_steps.z);
        r = rim_turn(r, k, 8, u.wheel_steps.w);
        sum += textureLod(u_tex, vec2(0.5) + vec2(d.x * r.x + d.y * r.y, d.y * r.x - d.x * r.y), u.wheel_blur.y);
    }
    return sum * 0.0625;
}
// ---- end of the wheel spin blurs ----

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

// The depth of field (presentation §G17): one texel of a downsampled picture, or one pixel of a
// blurred picture laid over the frame. The mean of grade.x x grade.y bilinear taps spaced
// grade.zw apart around v_uv x view_row0.xy + view_row0.zw (one tap: the plain bilinear sample),
// every tap clamped to view_row1 (the used part of the texture, half a texel in from its edges: a
// clamp to its edge), at the weight tint.a.
vec4 dof_sample() {
    vec2 uv = v_uv * u.view_row0.xy + u.view_row0.zw;
    vec2 n = max(u.grade.xy, vec2(1.0));
    // flat_color.xy > 0: the taps weighted by a Gaussian of that spread (in taps) per axis, else
    // equally (a box mean).
    vec2 s = u.flat_color.xy;
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (float j = 0.0; j < n.y; j += 1.0)
        for (float i = 0.0; i < n.x; i += 1.0) {
            vec2 k = vec2(i, j) - 0.5 * (n - 1.0);
            vec2 g = vec2(s.x > 0.0 ? exp(-0.5 * k.x * k.x / (s.x * s.x)) : 1.0,
                          s.y > 0.0 ? exp(-0.5 * k.y * k.y / (s.y * s.y)) : 1.0);
            float w = g.x * g.y;
            sum += w * texture(u_tex, clamp(uv + k * u.grade.zw, u.view_row1.xy, u.view_row1.zw)).rgb;
            total += w;
        }
    return vec4(sum / total, u.tint.a);
}

float fog_amount(float d) {
    if (u.fog_params.w > 0.5) return v_fog; // GT3: per vertex from the view depth (model.vert)
    return clamp((d - u.fog_params.x) * u.fog_params.y, 0.0, 1.0) * u.fog_params.z;
}

// The GS's 8-bit colour arithmetic (presentation §M7), which truncates where a GPU rounds: the
// texel as the GS filters it (the bilinear result truncated to 8 bits), MODULATE
// (texel x colour) >> 7 clamped to 255, and fog (F C + (256 - F) FOGCOL) >> 8 with the fog
// coefficient F = 255 (1 - amount) (255: no fog, which still takes C past FOGCOL down a level).
// Each leaves a whole 8-bit level, which the frame buffer stores as it is.
vec3 gs_texel(vec3 t) { return floor(t * 255.0 + 0.05) / 255.0; }
vec3 gs_color(vec3 c) { return min(floor(c * 255.0 + 1e-3), 255.0) / 255.0; }
vec3 gs_fog(vec3 c, vec3 fog, float amount) {
    vec3 f = fog * 255.0;
    return floor(f + (c * 255.0 - f) * ((255.0 / 256.0) * (1.0 - amount)) + 1e-3) / 255.0;
}

void shade() {
    if (u.params.w > 5.5) {
        o_color = dof_sample();
        return;
    }
    if (u.params.w > 4.5) {
        // The post pass (View3D): a finished picture drawn over the frame, graded when asked,
        // at the weight tint.a (the dissolve's frozen picture, the trail's previous one).
        vec3 c = texture(u_tex, v_uv).rgb;
        if (u.flat_color.w > 0.5) {
            // The heat haze above the original's resolution (View3D::encode_screen): the lens's
            // texel in the picture at the original field's size (u_sphere), plus this pixel's
            // detail finer than that (the picture here minus its field-sized copy here). v_world
            // is the fragment's place in the picture (clip x, y; the screen batch's model is the
            // identity).
            vec2 s = vec2(0.5 + 0.5 * v_world.x, 0.5 - 0.5 * v_world.y);
            c = texture(u_sphere, v_uv * u.view_row1.xy).rgb + texture(u_tex, s * u.view_row0.xy).rgb -
                texture(u_sphere, s * u.view_row0.zw).rgb;
        }
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
        vec4 t = texture(u_tex, uv);
        vec4 c = vec4(gs_color(gs_texel(t.rgb) * vc.rgb), t.a * vc.a) * u.tint;
        if (u.params.z > 0.5) c.a = v_color.a * u.tint.a; // additive layer: full weight (Ad = 1)
        o_color = c;
        if (u.grade2.w > 0.5) o_color.a = 1.0;
        return;
    }
    if (u.params.w > 2.5) {
        // Car reflection pass (PS2 ALPHA Cs*Ad + Cd): environment colour times the
        // pass colour, weighted through SRC_ALPHA blending by the reflectivity Ad = the
        // base pass's texel alpha (bound texture; white when none) x vertex alpha.
        float ad = v_color.a * wheel_texture(v_uv).a;
        // GS TEST.DATE with DATM 0: only where Ad < 0x80 (0x80 = 1.0 here), not on texels at full
        // reflectivity. (The 0x80 an earlier reflection pass leaves: the decoder drops the
        // triangles such a pass repeats.)
        if (u.output_mode.w > 0.5 && ad > 254.0 / 255.0) discard;
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
        vec3 rgb = gs_color(gs_texel(env) * min(v_color.rgb * w, vec3(255.0 / 128.0)) * u.params.z);
        if (u.fog_color.w > 0.0) {
            float d = length(v_world - u.camera_pos.xyz);
            rgb = gs_fog(rgb, vec3(0.0), fog_amount(d));
        }
        o_color = vec4(rgb, ad * u.tint.a);
        return;
    }
    vec4 t = wheel_texture(v_uv);
    vec4 c = vec4(gs_texel(t.rgb), t.a) * v_color;
    if (u.params.w > 1.5) {
        // Road sheen: the texel modulated by its VU program's lit colour (model.vert), fogged
        // to black (the node program sets the fog colour to 0 for this pass); added weighted by
        // the frame-buffer alpha (Cs * Ad + Cd) or, without one, by its own alpha.
        c.rgb = gs_color(gs_texel(texture(u_tex, v_uv).rgb) * v_lit);
        if (u.fog_color.w > 0.0) {
            float d = length(v_world - u.camera_pos.xyz);
            c.rgb = gs_fog(c.rgb, vec3(0.0), fog_amount(d));
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
    c.rgb = gs_color(c.rgb);
    if (u.fog_color.w > 0.0) {
        float d = length(v_world - u.camera_pos.xyz);
        c.rgb = gs_fog(c.rgb, u.fog_color.rgb, fog_amount(d));
    }
    o_color = c * u.tint;
    if (u.grade2.w > 0.5) o_color.a = 1.0;
}

#ifdef SPIN_COPIES
// One of a modelled rim's turned copies (DrawItem::spin_turn; render::spin_copy_at is this rule on
// the CPU): each copy draws only its share of the picture's samples, so a pixel resolves to the
// mean of the copies it holds, and each copy's own depth keeps whatever lies behind its spokes (the
// brake parts) sharp. A pixel's samples hold one copy each; the cells of a 4 x 4 ordered pattern
// share out the rest, so a pixel's copies spread over the whole turn. Without sample masks (OpenGL
// ES 3.0) the copies come one to a pixel (as many as cells).
const int kSpinCells[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);

void spin_copy_samples() {
    int copy = v_copy;
    int cells = int(u.wheel_blur.w + 0.5);
    ivec2 p = ivec2(gl_FragCoord.xy) & 3;
    if (kSpinCells[p.y * 4 + p.x] * cells / 16 != copy % cells) discard;
#ifndef NO_SAMPLE_MASK
    gl_SampleMask[0] = 1 << (copy / cells);
#endif
}
#endif

void main() {
#ifdef SPIN_COPIES
    spin_copy_samples();
#endif
    if (v_fog_w.y > 0.0) discard;
    v_color = u.output_mode.y > 0.5 ? v_color_in * gl_FragCoord.w : v_color_in;
    v_lit = v_lit_w * gl_FragCoord.w;
    v_fog = v_fog_w.x * gl_FragCoord.w;
    shade();
    // GT3's reflection map is a PSMCT16 frame buffer: each write keeps c8 >> 3 (read back as
    // c5 << 3).
    if (u.output_mode.x > 0.5) o_color.rgb = floor(clamp(o_color.rgb, 0.0, 1.0) * (255.0 / 8.0) + 1e-4) * (8.0 / 255.0);
}
