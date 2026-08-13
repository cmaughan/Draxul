#include <metal_stdlib>
using namespace metal;

struct TooltipUniforms
{
    float4 rect;
    float4 viewport;
};

struct TooltipVertexOut
{
    float4 position [[position]];
    float2 uv;
};

vertex TooltipVertexOut tooltip_vertex(
    uint vid [[vertex_id]],
    constant TooltipUniforms& tooltip [[buffer(0)]])
{
    const float2 corners[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    constexpr uint indices[6] = { 0, 1, 2, 0, 2, 3 };
    const float2 corner = corners[indices[vid]];
    const float2 pixel_pos = tooltip.rect.xy + corner * tooltip.rect.zw;
    float2 ndc = pixel_pos / tooltip.viewport.xy * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    return { float4(ndc, 0.0f, 1.0f), corner };
}

fragment float4 tooltip_fragment(
    TooltipVertexOut in [[stage_in]],
    texture2d<float> texture [[texture(0)]],
    sampler texture_sampler [[sampler(0)]])
{
    return texture.sample(texture_sampler, in.uv);
}
