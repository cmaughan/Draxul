#include <metal_stdlib>
using namespace metal;

#include "decoration_constants_shared.h"
#include "quad_offsets_shared.h"

struct MarkdownRectInstance
{
    float4 rect;
    float4 color;
};

struct MarkdownGlyphInstance
{
    float4 rect;
    float4 uv;
    float4 color;
    uint flags;
    uint atlas_id;
    uint atlas_generation;
    uint _pad;
};

struct MarkdownPushConstants
{
    float4 viewport;
};

struct RectVertexOut
{
    float4 position [[position]];
    float4 color;
};

struct GlyphVertexOut
{
    float4 position [[position]];
    float2 uv;
    float4 color;
    uint flags;
};

constant float2 kQuadOffsets[6] = {
    float2(QUAD_OFFSET_0), float2(QUAD_OFFSET_1), float2(QUAD_OFFSET_2),
    float2(QUAD_OFFSET_3), float2(QUAD_OFFSET_4), float2(QUAD_OFFSET_5)
};

float4 markdown_position(float4 rect, float2 offset, float2 viewport_size)
{
    float2 pixel_pos = rect.xy + offset * rect.zw;
    float2 ndc = pixel_pos / viewport_size * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    return float4(ndc, 0.0f, 1.0f);
}

vertex RectVertexOut markdown_rect_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    device const MarkdownRectInstance* rects [[buffer(0)]],
    constant MarkdownPushConstants& push [[buffer(1)]])
{
    const MarkdownRectInstance instance = rects[instance_id];
    const float2 offset = kQuadOffsets[vertex_id];

    RectVertexOut out;
    out.position = markdown_position(instance.rect, offset, push.viewport.xy);
    out.color = instance.color;
    return out;
}

fragment float4 markdown_rect_fragment(RectVertexOut in [[stage_in]])
{
    return in.color;
}

vertex GlyphVertexOut markdown_glyph_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    device const MarkdownGlyphInstance* glyphs [[buffer(0)]],
    constant MarkdownPushConstants& push [[buffer(1)]])
{
    const MarkdownGlyphInstance instance = glyphs[instance_id];
    const float2 offset = kQuadOffsets[vertex_id];

    GlyphVertexOut out;
    out.position = markdown_position(instance.rect, offset, push.viewport.xy);
    out.uv = mix(instance.uv.xy, instance.uv.zw, offset);
    out.color = instance.color;
    out.flags = instance.flags;
    return out;
}

fragment float4 markdown_glyph_fragment(
    GlyphVertexOut in [[stage_in]],
    texture2d<float> atlas [[texture(0)]],
    sampler atlas_sampler [[sampler(0)]])
{
    const float4 atlas_sample = atlas.sample(atlas_sampler, in.uv);
    const float alpha = atlas_sample.a;
    if (alpha < 0.01f)
        discard_fragment();

    const bool color_glyph = (in.flags & STYLE_FLAG_COLOR_GLYPH) != 0u;
    return color_glyph ? atlas_sample : float4(in.color.rgb, in.color.a * alpha);
}
