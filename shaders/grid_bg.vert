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

layout(location = 0) out vec4 frag_bg;
layout(location = 1) out vec4 frag_fg;
layout(location = 2) out vec4 frag_sp;
layout(location = 3) out vec2 frag_local_uv;
layout(location = 4) flat out uint frag_style_flags;

void main() {
    Cell cell = cells[gl_InstanceIndex];

    // Generate quad vertices from vertex index (2 triangles = 6 vertices)
    vec2 offsets[6] = vec2[](
        vec2(0, 0), vec2(1, 0), vec2(0, 1),
        vec2(1, 0), vec2(1, 1), vec2(0, 1)
    );

    vec2 offset = offsets[gl_VertexIndex];
    vec2 pos = vec2(cell.pos_x, cell.pos_y) + offset * vec2(cell.size_x, cell.size_y);
    pos.y -= pc.scroll_offset_px;
    pos += vec2(pc.viewport_x, pc.viewport_y);

    // Convert to NDC: [0, screen] -> [-1, 1]
    vec2 ndc = (pos / vec2(pc.screen_w, pc.screen_h)) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
    frag_bg = vec4(cell.bg_r, cell.bg_g, cell.bg_b, cell.bg_a);
    frag_fg = vec4(cell.fg_r, cell.fg_g, cell.fg_b, cell.fg_a);
    frag_sp = vec4(cell.sp_r, cell.sp_g, cell.sp_b, cell.sp_a);
    frag_local_uv = offset;
    frag_style_flags = cell.style_flags;
}
