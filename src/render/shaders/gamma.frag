#version 450
// The player's gamma over the whole frame (render/gamma_pass.h): out = in ^ (1 / gamma) per
// channel, so black and white stay. SDL's GPU renderer draws the frame with this as its
// fragment shader (SDL_GPURenderState: its vertex colour at location 0, its texture coordinates
// at 1, the texture in slot 0); OpenGL ES draws it with gamma.vert. SDL_GPU binding rules:
// sampled textures in set 2, uniform buffers in set 3.

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 2, binding = 0) uniform sampler2D u_frame;

layout(set = 3, binding = 0) uniform GammaUniforms {
    vec4 gamma; // x = 1 / gamma
};

void main() {
    vec3 c = texture(u_frame, v_uv).rgb * v_color.rgb;
    o_color = vec4(pow(c, vec3(gamma.x)), 1.0);
}
