#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float3 color;
};

struct CubeUniforms {
    float4x4 mvp;
};

// 36 vertices for a unit cube (6 faces x 2 triangles x 3 verts), CCW front-face
constant float3 cube_verts[] = {
    // Front  (z=+0.5)
    {-0.5, -0.5,  0.5}, { 0.5, -0.5,  0.5}, { 0.5,  0.5,  0.5},
    {-0.5, -0.5,  0.5}, { 0.5,  0.5,  0.5}, {-0.5,  0.5,  0.5},
    // Back   (z=-0.5)
    { 0.5, -0.5, -0.5}, {-0.5, -0.5, -0.5}, {-0.5,  0.5, -0.5},
    { 0.5, -0.5, -0.5}, {-0.5,  0.5, -0.5}, { 0.5,  0.5, -0.5},
    // Left   (x=-0.5)
    {-0.5, -0.5, -0.5}, {-0.5, -0.5,  0.5}, {-0.5,  0.5,  0.5},
    {-0.5, -0.5, -0.5}, {-0.5,  0.5,  0.5}, {-0.5,  0.5, -0.5},
    // Right  (x=+0.5)
    { 0.5, -0.5,  0.5}, { 0.5, -0.5, -0.5}, { 0.5,  0.5, -0.5},
    { 0.5, -0.5,  0.5}, { 0.5,  0.5, -0.5}, { 0.5,  0.5,  0.5},
    // Top    (y=+0.5)
    {-0.5,  0.5,  0.5}, { 0.5,  0.5,  0.5}, { 0.5,  0.5, -0.5},
    {-0.5,  0.5,  0.5}, { 0.5,  0.5, -0.5}, {-0.5,  0.5, -0.5},
    // Bottom (y=-0.5)
    {-0.5, -0.5, -0.5}, { 0.5, -0.5, -0.5}, { 0.5, -0.5,  0.5},
    {-0.5, -0.5, -0.5}, { 0.5, -0.5,  0.5}, {-0.5, -0.5,  0.5},
};

// One colour per face (6 faces x 6 verts = index vid/6)
constant float3 face_colors[] = {
    {1.0, 0.35, 0.35},  // Front  — red
    {0.35, 1.0, 0.35},  // Back   — green
    {0.35, 0.35, 1.0},  // Left   — blue
    {1.0, 1.0, 0.35},   // Right  — yellow
    {1.0, 0.35, 1.0},   // Top    — magenta
    {0.35, 1.0, 1.0},   // Bottom — cyan
};

vertex VertexOut cube_vertex(uint vid [[vertex_id]],
                             constant CubeUniforms& u [[buffer(0)]])
{
    VertexOut out;
    out.position = u.mvp * float4(cube_verts[vid], 1.0);
    out.color = face_colors[vid / 6];
    return out;
}

fragment float4 cube_fragment(VertexOut in [[stage_in]])
{
    return float4(in.color, 1.0);
}
