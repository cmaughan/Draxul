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
    vec4 screen_params;
    vec4 ao_params;
    vec4 debug_view;
    vec4 world_debug_bounds;
}
frame;

layout(set = 0, binding = 1) uniform sampler2D hdr_scene;

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_frag_color;

void main()
{
    vec4 hdr = texture(hdr_scene, out_uv);
    out_frag_color = vec4(max(hdr.rgb, vec3(0.0)), clamp(hdr.a, 0.0, 1.0));
}
