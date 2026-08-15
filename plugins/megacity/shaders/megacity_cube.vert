#version 450

// 36 procedural vertices — no vertex buffer, geometry baked here (mirrors megacity_cube.metal)
const vec3 cube_verts[36] = vec3[36](
    // Front  (z=+0.5)
    vec3(-0.5, -0.5,  0.5), vec3( 0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5),
    vec3(-0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5), vec3(-0.5,  0.5,  0.5),
    // Back   (z=-0.5)
    vec3( 0.5, -0.5, -0.5), vec3(-0.5, -0.5, -0.5), vec3(-0.5,  0.5, -0.5),
    vec3( 0.5, -0.5, -0.5), vec3(-0.5,  0.5, -0.5), vec3( 0.5,  0.5, -0.5),
    // Left   (x=-0.5)
    vec3(-0.5, -0.5, -0.5), vec3(-0.5, -0.5,  0.5), vec3(-0.5,  0.5,  0.5),
    vec3(-0.5, -0.5, -0.5), vec3(-0.5,  0.5,  0.5), vec3(-0.5,  0.5, -0.5),
    // Right  (x=+0.5)
    vec3( 0.5, -0.5,  0.5), vec3( 0.5, -0.5, -0.5), vec3( 0.5,  0.5, -0.5),
    vec3( 0.5, -0.5,  0.5), vec3( 0.5,  0.5, -0.5), vec3( 0.5,  0.5,  0.5),
    // Top    (y=+0.5)
    vec3(-0.5,  0.5,  0.5), vec3( 0.5,  0.5,  0.5), vec3( 0.5,  0.5, -0.5),
    vec3(-0.5,  0.5,  0.5), vec3( 0.5,  0.5, -0.5), vec3(-0.5,  0.5, -0.5),
    // Bottom (y=-0.5)
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5),
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5), vec3(-0.5, -0.5,  0.5)
);

// One colour per face (vid / 6)
const vec3 face_colors[6] = vec3[6](
    vec3(1.0,  0.35, 0.35),  // Front  — red
    vec3(0.35, 1.0,  0.35),  // Back   — green
    vec3(0.35, 0.35, 1.0),   // Left   — blue
    vec3(1.0,  1.0,  0.35),  // Right  — yellow
    vec3(1.0,  0.35, 1.0),   // Top    — magenta
    vec3(0.35, 1.0,  1.0)    // Bottom — cyan
);

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} push;

layout(location = 0) out vec3 out_color;

void main()
{
    gl_Position = push.mvp * vec4(cube_verts[gl_VertexIndex], 1.0);
    out_color = face_colors[gl_VertexIndex / 6];
}
