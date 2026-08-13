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

layout(set = 0, binding = 1) uniform sampler2D hdr_scene;

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_frag_color;

vec3 tone_map_aces(vec3 hdr, float exposure, float white_point)
{
    vec3 color = max(hdr, vec3(0.0)) * max(exposure, 0.0);
    color /= max(white_point, 1e-3);
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), vec3(0.0), vec3(1.0));
}

void main()
{
    vec4 hdr = texture(hdr_scene, out_uv);
    vec3 mapped = tone_map_aces(hdr.rgb, frame.render_tuning.x, frame.render_tuning.w);
    out_frag_color = vec4(mapped, clamp(hdr.a, 0.0, 1.0));
}
