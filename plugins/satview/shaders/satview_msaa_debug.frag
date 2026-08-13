#version 450

layout(set = 0, binding = 0) uniform sampler2DMS msaa_scene;

layout(push_constant) uniform MsaaDebugParams
{
    ivec4 values;
} params;

layout(location = 0) out vec4 out_frag_color;

vec3 heat_map(float value)
{
    float t = clamp(value, 0.0, 1.0);
    return clamp(vec3(
        1.5 * t,
        1.5 * (1.0 - abs(2.0 * t - 1.0)),
        1.5 * (1.0 - t)),
        0.0,
        1.0);
}

void main()
{
    int sample_count = clamp(params.values.x, 1, 4);
    ivec2 size = textureSize(msaa_scene);
    ivec2 coord = clamp(ivec2(gl_FragCoord.xy), ivec2(0), size - ivec2(1));
    vec3 minimum_color = texelFetch(msaa_scene, coord, 0).rgb;
    vec3 maximum_color = minimum_color;
    for (int sample_index = 1; sample_index < sample_count; ++sample_index)
    {
        vec3 color = texelFetch(msaa_scene, coord, sample_index).rgb;
        minimum_color = min(minimum_color, color);
        maximum_color = max(maximum_color, color);
    }
    float difference = max(maximum_color.r - minimum_color.r,
        max(maximum_color.g - minimum_color.g, maximum_color.b - minimum_color.b));
    float intensity = sample_count > 1 ? 1.0 - exp(-difference * 6.0) : 0.0;
    out_frag_color = vec4(heat_map(intensity) * intensity, 1.0);
}
