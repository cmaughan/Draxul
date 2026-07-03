#version 450

layout(set = 0, binding = 0) uniform sampler2D hdr_scene;

layout(push_constant) uniform ToneMapParams
{
    vec4 tone_map;
} params;

layout(location = 0) in vec2 in_uv;
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
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

void main()
{
    vec4 hdr = texture(hdr_scene, in_uv);
    vec3 mapped = tone_map_aces(hdr.rgb, params.tone_map.x, params.tone_map.y);
    out_frag_color = vec4(mapped, clamp(hdr.a, 0.0, 1.0));
}
