#version 450

layout(set = 0, binding = 0) uniform FrameUniforms
{
    mat4 view;
    mat4 proj;
    mat4 inv_view_proj;
    vec4 camera_pos;
    vec4 light_dir;
    vec4 point_light_pos;
    vec4 label_fade_px;
    vec4 render_tuning;
    vec4 perf_tuning;
    vec4 screen_params;
    vec4 ao_params;
    vec4 debug_view;
    vec4 world_debug_bounds;
    mat4 shadow_view_proj[3];
    mat4 shadow_texture_matrix[3];
    vec4 shadow_split_depths;
    vec4 shadow_params;
    mat4 point_shadow_view_proj[6];
    mat4 point_shadow_texture_matrix[6];
    vec4 point_shadow_params;
}
frame;

layout(location = 0) in vec3 in_world_position;
layout(location = 0) out float out_depth_distance;

void main()
{
    float radius = max(frame.point_light_pos.w, 1.0);
    float distance_to_light = length(in_world_position - frame.point_light_pos.xyz);
    out_depth_distance = clamp(distance_to_light / radius, 0.0, 1.0);
}
