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
    const bool map_projection = push.camera_pos.w < 0.0;
    const int lat_bands = max(1, int(push.render_params.x + 0.5));
    const int lon_bands = max(1, int(push.render_params.y + 0.5));
    const int tri_vertex = gl_VertexIndex % 6;
    const int quad = gl_VertexIndex / 6;
    const int lon = quad % lon_bands;
    const int lat = quad / lon_bands;

    const vec2 corner = quad_corner(tri_vertex);
    const float u = map_projection
        ? corner.x
        : (float(lon) + corner.x) / float(lon_bands);
    const float v = map_projection
        ? corner.y
        : (float(lat) + corner.y) / float(lat_bands);
    const float theta = u * 2.0 * PI + (map_projection ? 0.0 : push.render_params.z);
    const float phi = mix(-0.5 * PI, 0.5 * PI, v);
    const float cp = cos(phi);
    const float polar_ratio = max(push.render_params.w, 0.05);
    const vec3 sphere = vec3(cp * sin(theta), sin(phi), cp * cos(theta));
    const vec3 world = vec3(sphere.x, sphere.y * polar_ratio, sphere.z);
    const vec3 normal = normalize(vec3(sphere.x, sphere.y / polar_ratio, sphere.z));

    out_normal = normal;
    out_world = world;
    out_uv = vec2(u, v);
    gl_Position = map_projection
        ? push.view_proj * vec4(u * 2.0 - 1.0, v * 2.0 - 1.0, 0.2, 1.0)
        : satview_project_world_position(world);
}
