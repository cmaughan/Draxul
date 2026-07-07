#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

#include "satview_sky_projection.glsl"

layout(location = 0) out vec2 out_local;

vec2 quad_corner(int vertex)
{
    if (vertex == 0) return vec2(-1.0, -1.0);
    if (vertex == 1) return vec2(1.0, 1.0);
    if (vertex == 2) return vec2(1.0, -1.0);
    if (vertex == 3) return vec2(-1.0, -1.0);
    if (vertex == 4) return vec2(-1.0, 1.0);
    return vec2(1.0, 1.0);
}

void main()
{
    const float outer_radius = max(push.camera_orientation.y, 0.001);
    const vec2 local = quad_corner(gl_VertexIndex) * outer_radius;
    const vec3 world = vec3(local.x, 0.0, local.y);
    out_local = local;
    gl_Position = satview_project_world_position(world);
}
