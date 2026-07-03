#version 450

layout(location = 0) in vec4 in_color;
layout(location = 1) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main()
{
    float radius2 = dot(in_uv, in_uv);
    float alpha = (1.0 - smoothstep(0.0, 1.0, radius2)) * in_color.a;
    alpha *= alpha;
    out_color = vec4(in_color.rgb * alpha, 0.0);
}
