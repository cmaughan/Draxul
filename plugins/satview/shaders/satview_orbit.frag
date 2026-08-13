#version 450

layout(location = 0) in vec4 in_color;

layout(location = 0) out vec4 out_color;

vec3 srgb_to_linear(vec3 color)
{
    bvec3 cutoff = lessThanEqual(color, vec3(0.04045));
    vec3 low = color / 12.92;
    vec3 high = pow((color + 0.055) / 1.055, vec3(2.4));
    return mix(high, low, cutoff);
}

void main()
{
    out_color = vec4(srgb_to_linear(in_color.rgb), in_color.a);
}
