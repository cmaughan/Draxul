#include <metal_stdlib>
using namespace metal;

#include "decoration_constants_shared.h"

struct Cell
{
    float pos_x, pos_y;
    float size_x, size_y;
    float bg_r, bg_g, bg_b, bg_a;
    float fg_r, fg_g, fg_b, fg_a;
    float sp_r, sp_g, sp_b, sp_a;
    float uv_x0, uv_y0, uv_x1, uv_y1;
    float glyph_offset_x, glyph_offset_y;
    float glyph_size_x, glyph_size_y;
    uint style_flags;
    uint _pad0, _pad1, _pad2;
};

struct PushConstants
{
    float screen_w;
    float screen_h;
    float cell_w;
    float cell_h;
    float scroll_offset_px;
    float viewport_x;
    float viewport_y;
};

// Background pass

struct BgVertexOut
{
    float4 position [[position]];
    float4 bg_color;
    float4 fg_color;
    float4 sp_color;
    float2 local_uv;
    uint style_flags;
};

vertex BgVertexOut bg_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    device const Cell* cells [[buffer(0)]],
    constant PushConstants& pc [[buffer(1)]])
{
    Cell cell = cells[instance_id];

    constexpr float2 offsets[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(1, 0), float2(1, 1), float2(0, 1)
    };

    float2 offset = offsets[vertex_id];
    float2 pos = float2(cell.pos_x, cell.pos_y) + offset * float2(cell.size_x, cell.size_y);
    pos.y -= pc.scroll_offset_px;
    pos += float2(pc.viewport_x, pc.viewport_y);

    // Convert to NDC: [0, screen] -> [-1, 1]
    float2 ndc = (pos / float2(pc.screen_w, pc.screen_h)) * 2.0 - 1.0;
    // Metal NDC: Y goes up, flip Y
    ndc.y = -ndc.y;

    BgVertexOut out;
    out.position = float4(ndc, 0.0, 1.0);
    out.bg_color = float4(cell.bg_r, cell.bg_g, cell.bg_b, cell.bg_a);
    out.fg_color = float4(cell.fg_r, cell.fg_g, cell.fg_b, cell.fg_a);
    out.sp_color = float4(cell.sp_r, cell.sp_g, cell.sp_b, cell.sp_a);
    out.local_uv = offset;
    out.style_flags = cell.style_flags;
    return out;
}

fragment float4 bg_fragment(BgVertexOut in [[stage_in]])
{
    float4 color = in.bg_color;
    bool underline = (in.style_flags & STYLE_FLAG_UNDERLINE) != 0u;
    bool strikethrough = (in.style_flags & STYLE_FLAG_STRIKETHROUGH) != 0u;
    bool undercurl = (in.style_flags & STYLE_FLAG_UNDERCURL) != 0u;
    float4 accent = in.sp_color.a > 0.0 ? in.sp_color : in.fg_color;

    if (underline && in.local_uv.y >= UNDERLINE_TOP && in.local_uv.y <= UNDERLINE_BOTTOM)
    {
        color = accent;
    }
    else if (strikethrough && in.local_uv.y >= STRIKETHROUGH_TOP && in.local_uv.y <= STRIKETHROUGH_BOTTOM)
    {
        color = in.fg_color;
    }
    else if (undercurl)
    {
        float baseline = UNDERCURL_BASELINE + UNDERCURL_AMPLITUDE * sin(in.local_uv.x * UNDERCURL_FREQ);
        if (fabs(in.local_uv.y - baseline) <= UNDERCURL_THICKNESS)
            color = accent;
    }

    return color;
}

// Foreground pass

struct FgVertexOut
{
    float4 position [[position]];
    float2 uv;
    float4 fg_color;
    uint style_flags;
};

vertex FgVertexOut fg_vertex(
    uint vertex_id [[vertex_id]],
    uint instance_id [[instance_id]],
    device const Cell* cells [[buffer(0)]],
    constant PushConstants& pc [[buffer(1)]])
{
    Cell cell = cells[instance_id];

    constexpr float2 offsets[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(1, 0), float2(1, 1), float2(0, 1)
    };

    float2 offset = offsets[vertex_id];

    // Position the glyph within the cell using bearing/size
    float2 glyph_pos = float2(cell.pos_x + cell.glyph_offset_x,
        cell.pos_y + cell.size_y - cell.glyph_offset_y);
    glyph_pos.y -= pc.scroll_offset_px;
    float2 glyph_size = float2(cell.glyph_size_x, cell.glyph_size_y);

    float2 pos = glyph_pos + offset * glyph_size;
    pos += float2(pc.viewport_x, pc.viewport_y);

    // Convert to NDC
    float2 ndc = (pos / float2(pc.screen_w, pc.screen_h)) * 2.0 - 1.0;
    ndc.y = -ndc.y;

    FgVertexOut out;
    out.position = float4(ndc, 0.0, 1.0);
    out.uv = mix(float2(cell.uv_x0, cell.uv_y0), float2(cell.uv_x1, cell.uv_y1), offset);
    out.fg_color = float4(cell.fg_r, cell.fg_g, cell.fg_b, cell.fg_a);
    out.style_flags = cell.style_flags;
    return out;
}

fragment float4 fg_fragment(
    FgVertexOut in [[stage_in]],
    texture2d<float> atlas [[texture(0)]],
    sampler atlas_sampler [[sampler(0)]])
{
    float4 atlas_sample = atlas.sample(atlas_sampler, in.uv);
    float alpha = atlas_sample.a;
    if (alpha < 0.01)
        discard_fragment();
    bool color_glyph = (in.style_flags & STYLE_FLAG_COLOR_GLYPH) != 0u;
    return color_glyph ? atlas_sample : float4(in.fg_color.rgb, in.fg_color.a * alpha);
}
