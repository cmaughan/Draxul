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

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_world;
layout(location = 2) out vec2 out_uv;

const float PI = 3.14159265358979323846;
const vec3 LUNAR_NORTH_POLE_RENDER = vec3(
    0.39812155,
    0.91733267,
    0.00003544);

vec2 quad_corner(int vertex)
{
    if (vertex == 0)
        return vec2(0.0, 0.0);
    if (vertex == 1)
        return vec2(1.0, 1.0);
    if (vertex == 2)
        return vec2(1.0, 0.0);
    if (vertex == 3)
        return vec2(0.0, 0.0);
    if (vertex == 4)
        return vec2(0.0, 1.0);
    return vec2(1.0, 1.0);
}

void main()
{
    bool map_projection = push.camera_pos.w < 0.0;
    int lat_bands = max(1, int(push.render_params.x + 0.5));
    int lon_bands = max(1, int(push.render_params.y + 0.5));
    int tri_vertex = gl_VertexIndex % 6;
    int quad = gl_VertexIndex / 6;
    int lon = quad % lon_bands;
    int lat = quad / lon_bands;

    vec2 corner = quad_corner(tri_vertex);
    float u = map_projection
        ? corner.x
        : (float(lon) + corner.x) / float(lon_bands);
    float v = map_projection
        ? corner.y
        : (float(lat) + corner.y) / float(lat_bands);
    float theta = u * 2.0 * PI;
    float phi = mix(-0.5 * PI, 0.5 * PI, v);
    float cp = cos(phi);
    vec3 local_normal = vec3(cp * sin(theta), sin(phi), cp * cos(theta));

    vec3 moon_position = map_projection
        ? push.camera_pos.xyz
        : push.camera_orientation.xyz;
    vec3 far_axis = normalize(moon_position);
    vec3 north_axis = normalize(
        LUNAR_NORTH_POLE_RENDER
        - far_axis * dot(LUNAR_NORTH_POLE_RENDER, far_axis));
    vec3 local_x_axis = normalize(cross(north_axis, far_axis));
    vec3 normal = normalize(
        local_x_axis * local_normal.x
        + north_axis * local_normal.y
        + far_axis * local_normal.z);
    vec3 world = moon_position + normal * push.camera_orientation.w;

    out_normal = normal;
    out_world = world;
    out_uv = vec2(u, v);
    gl_Position = map_projection
        ? push.view_proj * vec4(u * 2.0 - 1.0, v * 2.0 - 1.0, 0.8, 1.0)
        : satview_project_world_position(world);
}
