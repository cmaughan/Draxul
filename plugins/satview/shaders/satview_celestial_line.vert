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

layout(location = 0) in vec4 in_start_direction_width;
layout(location = 1) in vec4 in_end_direction_dash;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec4 in_style;

layout(location = 0) out vec4 out_color;
layout(location = 1) noperspective out float out_line_distance;
layout(location = 2) noperspective out float out_line_across;
layout(location = 3) flat out vec2 out_dash_gap;

const float LINE_DISTANCE_EARTH_RADII = 48.0;

void main()
{
    const float endpoint[6] = float[6](0.0, 0.0, 1.0, 0.0, 1.0, 1.0);
    const float side[6] = float[6](-1.0, 1.0, -1.0, 1.0, 1.0, -1.0);
    vec3 start_world = push.camera_pos.xyz
        + normalize(in_start_direction_width.xyz) * LINE_DISTANCE_EARTH_RADII;
    vec3 end_world = push.camera_pos.xyz
        + normalize(in_end_direction_dash.xyz) * LINE_DISTANCE_EARTH_RADII;
    vec4 start_clip = satview_project_world_position(start_world);
    vec4 end_clip = satview_project_world_position(end_world);
    bool visible = satview_ground_world_position_visible(start_world)
        && satview_ground_world_position_visible(end_world)
        && start_clip.w > 0.0
        && end_clip.w > 0.0;

    vec2 start_ndc = start_clip.xy / max(start_clip.w, 0.000001);
    vec2 end_ndc = end_clip.xy / max(end_clip.w, 0.000001);
    vec2 viewport = max(push.render_params.xy, vec2(1.0));
    vec2 screen_delta = (end_ndc - start_ndc) * viewport * 0.5;
    float segment_pixels = length(screen_delta);
    visible = visible
        && !any(isnan(vec4(start_ndc, end_ndc)))
        && !any(isinf(vec4(start_ndc, end_ndc)))
        && segment_pixels > 0.0001
        && length(end_ndc - start_ndc) < 8.0;

    float t = endpoint[gl_VertexIndex];
    vec4 clip = mix(start_clip, end_clip, t);
    vec2 perpendicular = segment_pixels > 0.0001
        ? normalize(vec2(-screen_delta.y, screen_delta.x))
        : vec2(0.0, 1.0);
    float half_width = max(in_start_direction_width.w, 0.5) * 0.5;
    vec2 offset_ndc = perpendicular * side[gl_VertexIndex] * half_width * 2.0 / viewport;
    clip.xy = (mix(start_ndc, end_ndc, t) + offset_ndc) * clip.w;

    out_color = visible ? in_color : vec4(0.0);
    float segment_angle = acos(clamp(dot(
        normalize(in_start_direction_width.xyz),
        normalize(in_end_direction_dash.xyz)), -1.0, 1.0));
    float phase_pixels = in_style.y * segment_pixels / max(segment_angle, 0.0001);
    out_line_distance = phase_pixels + t * segment_pixels;
    out_line_across = side[gl_VertexIndex];
    out_dash_gap = vec2(max(in_end_direction_dash.w, 0.0), max(in_style.x, 0.0));
    gl_Position = visible ? clip : vec4(2.0, 2.0, 0.0, 1.0);
}
