#version 450

layout(location = 0) in vec3 in_color;
layout(location = 0) out vec4 out_frag;

void main()
{
    out_frag = vec4(in_color, 1.0);
}
