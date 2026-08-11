#version 450

layout(push_constant) uniform PushConstants {
    float angle;
    float aspect;
    int mode;
} pc;

layout(location = 0) out vec3 color;

const vec2 background_positions[6] = vec2[](
    vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
const vec2 triangle_positions[3] = vec2[](
    vec2(0.0, -0.62), vec2(0.54, 0.34), vec2(-0.54, 0.34));
const vec3 triangle_colors[3] = vec3[](
    vec3(1.0, 0.18, 0.18), vec3(0.18, 1.0, 0.32), vec3(0.2, 0.4, 1.0));

void main() {
    if (pc.mode == 0) {
        gl_Position = vec4(background_positions[gl_VertexIndex], 0.0, 1.0);
        color = vec3(0.025, 0.035, 0.055);
        return;
    }
    vec2 p = triangle_positions[gl_VertexIndex];
    p.x /= max(pc.aspect, 0.001);
    float c = cos(pc.angle);
    float s = sin(pc.angle);
    p = mat2(c, -s, s, c) * p;
    gl_Position = vec4(p, 0.0, 1.0);
    color = triangle_colors[gl_VertexIndex];
}
