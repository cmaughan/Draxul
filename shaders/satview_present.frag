#version 450

layout(set = 0, binding = 0) uniform sampler2D final_scene;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_frag_color;

void main()
{
    out_frag_color = texture(final_scene, in_uv);
}
