#version 450

layout(set = 0, binding = 2) uniform sampler2D present_texture;

layout(location = 0) in vec2 out_uv;
layout(location = 0) out vec4 out_frag_color;

void main()
{
    out_frag_color = texture(present_texture, out_uv);
}
