#version 450
// The gamma pass on OpenGL ES (render/gamma_pass.h): a triangle over the whole target from the
// vertex index alone, with what SDL's GPU renderer hands gamma.frag (a white vertex colour,
// texture coordinates 0..1 from the top left).

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;

void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_color = vec4(1.0);
    v_uv = p;
    gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
