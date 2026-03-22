#version 450

layout(push_constant) uniform PushConstants {
    float screen_w;
    float screen_h;
    float cell_w;
    float cell_h;
    float scroll_offset_px;
    float viewport_x;
    float viewport_y;
} pc;

struct Cell {
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

layout(set = 0, binding = 0) readonly buffer CellBuffer {
    Cell cells[];
};

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_fg;
layout(location = 2) flat out uint frag_style_flags;

void main() {
    Cell cell = cells[gl_InstanceIndex];

    // Generate quad vertices for glyph
    vec2 offsets[6] = vec2[](
        vec2(0, 0), vec2(1, 0), vec2(0, 1),
        vec2(1, 0), vec2(1, 1), vec2(0, 1)
    );

    vec2 offset = offsets[gl_VertexIndex];

    // Position the glyph within the cell using bearing/size
    vec2 glyph_pos = vec2(cell.pos_x + cell.glyph_offset_x,
                          cell.pos_y + cell.size_y - cell.glyph_offset_y);
    glyph_pos.y -= pc.scroll_offset_px;
    vec2 glyph_size = vec2(cell.glyph_size_x, cell.glyph_size_y);

    vec2 pos = glyph_pos + offset * glyph_size;
    pos += vec2(pc.viewport_x, pc.viewport_y);

    // Convert to NDC
    vec2 ndc = (pos / vec2(pc.screen_w, pc.screen_h)) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);

    // Interpolate UVs
    frag_uv = mix(vec2(cell.uv_x0, cell.uv_y0), vec2(cell.uv_x1, cell.uv_y1), offset);
    frag_fg = vec4(cell.fg_r, cell.fg_g, cell.fg_b, cell.fg_a);
    frag_style_flags = cell.style_flags;
}
