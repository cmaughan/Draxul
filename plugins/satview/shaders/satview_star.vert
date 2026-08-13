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

layout(location = 0) in vec4 in_direction_magnitude;
layout(location = 1) in vec4 in_color_size;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec2 out_uv;

const float STAR_DISTANCE_EARTH_RADII = 48.0;

vec2 quad_corner(uint corner_index)
{
    switch (corner_index)
    {
    case 0:
        return vec2(-1.0, -1.0);
    case 1:
        return vec2(-1.0, 1.0);
    case 2:
        return vec2(1.0, -1.0);
    case 3:
        return vec2(1.0, -1.0);
    case 4:
        return vec2(-1.0, 1.0);
    default:
        return vec2(1.0, 1.0);
    }
}

void main()
{
    vec3 direction = normalize(in_direction_magnitude.xyz);
    vec3 center = push.camera_pos.xyz + direction * STAR_DISTANCE_EARTH_RADII;
    vec4 center_clip = satview_project_world_position(center);
    float min_magnitude = push.render_params.x;
    float max_magnitude = max(push.render_params.y, min_magnitude + 0.001);
    if (in_direction_magnitude.w < min_magnitude || in_direction_magnitude.w > max_magnitude)
    {
        out_color = vec4(0.0);
        out_uv = vec2(2.0);
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    float magnitude_t = clamp(
        (in_direction_magnitude.w - min_magnitude) / (max_magnitude - min_magnitude),
        0.0,
        1.0);
    float visibility = 1.0 - magnitude_t;
    float max_channel = max(in_color_size.r, max(in_color_size.g, in_color_size.b));
    vec3 chroma = max_channel > 0.00001 ? in_color_size.rgb / max_channel : vec3(1.0);
    float brightness = mix(0.035, 1.0, pow(visibility, 1.85)) * max(push.render_params.w, 0.0);
    float star_size = mix(0.0009, 0.0090, pow(visibility, 0.72));
    out_color = vec4(chroma * brightness, 1.0);

    if (!satview_ground_world_position_visible(center) || center_clip.w <= 0.0)
    {
        out_color.a = 0.0;
        out_uv = vec2(2.0);
        gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
        return;
    }

    vec2 corner = quad_corner(uint(gl_VertexIndex));
    float x_scale = max(push.render_params.z, 0.0001);
    center_clip.xy += corner * vec2(x_scale, 1.0) * star_size * center_clip.w;
    gl_Position = center_clip;
    out_uv = corner;
}
