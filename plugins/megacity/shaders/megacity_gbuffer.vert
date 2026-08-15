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
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in float in_tex_blend;
layout(location = 5) in vec4 in_tangent;

layout(location = 0) out vec3 out_normal_ws;
layout(location = 1) flat out uint out_material_index;
layout(location = 2) out vec2 out_material_uv;
void main()
{
    vec4 world_position = object_data.world * vec4(in_position, 1.0);
    out_normal_ws = normalize(mat3(object_data.world) * in_normal);
    out_material_index = object_data.material_data.x;
    out_material_uv = in_uv * object_data.uv_rect.zw;
    gl_Position = frame.proj * frame.view * world_position;
}
