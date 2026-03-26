#version 450

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    mat4 inv_view_proj;
    vec4 camera_pos;
    vec4 light_dir;
    vec4 point_light_pos;
    vec4 label_fade_px;
    vec4 render_tuning;
    vec4 screen_params;
    vec4 ao_params;
    vec4 debug_view;
    vec4 world_debug_bounds;
} frame;

layout(push_constant) uniform ObjectUniforms {
    mat4 world;
    vec4 color;
    vec4 material_info;
    vec4 uv_rect;
    vec4 label_metrics;
} object_data;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in float in_tex_blend;

layout(location = 0) out vec3 out_normal_ws;
layout(location = 1) out vec3 out_base_color;
layout(location = 2) out vec3 out_world_position;
layout(location = 3) flat out vec4 out_material_info;

void main()
{
    vec4 world_position = object_data.world * vec4(in_position, 1.0);
    out_normal_ws = normalize(mat3(object_data.world) * in_normal);
    out_base_color = in_color * object_data.color.rgb;
    out_world_position = world_position.xyz;
    out_material_info = object_data.material_info;
    gl_Position = frame.proj * frame.view * world_position;
}
