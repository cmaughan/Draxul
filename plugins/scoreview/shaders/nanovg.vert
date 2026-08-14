#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inTCoord;

layout(push_constant) uniform ViewUniforms {
    vec2 viewSize;
} view;

layout(location = 0) out vec2 ftcoord;
layout(location = 1) out vec2 fpos;

void main()
{
    ftcoord = inTCoord;
    fpos = inPos;
    gl_Position = vec4(
        2.0 * inPos.x / view.viewSize.x - 1.0,
        2.0 * inPos.y / view.viewSize.y - 1.0,
        0.0, 1.0);
}
