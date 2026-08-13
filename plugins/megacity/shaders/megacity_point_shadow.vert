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

layout(push_constant) uniform ObjectUniforms
{
    mat4 world;
    vec4 color;
    uvec4 material_data;
    vec4 uv_rect;
    vec4 label_metrics;
}
object_data;

layout(location = 0) in vec3 in_position;
layout(location = 0) out vec3 out_world_position;

void main()
{
    vec4 world_position = object_data.world * vec4(in_position, 1.0);
    uint face_index = min(object_data.material_data.z, 5u);
    gl_Position = frame.point_shadow_view_proj[face_index] * world_position;
    out_world_position = world_position.xyz;
}
