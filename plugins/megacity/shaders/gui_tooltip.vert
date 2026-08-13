#version 450

layout(push_constant) uniform TooltipUniforms
{
    vec4 rect;     // x, y, width, height in pixels
    vec4 viewport; // viewport_width, viewport_height, 0, 0
}
tooltip;

layout(location = 0) out vec2 out_uv;

void main()
{
    const vec2 corners[4] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1));
    const uint indices[6] = uint[](0, 1, 2, 0, 2, 3);
    const vec2 corner = corners[indices[gl_VertexIndex]];

    const vec2 pixel_pos = tooltip.rect.xy + corner * tooltip.rect.zw;
    vec2 ndc = pixel_pos / tooltip.viewport.xy * 2.0 - 1.0;

    out_uv = corner;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
