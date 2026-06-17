#version 450
#extension GL_GOOGLE_include_directive : require

#include "decoration_constants.glsl"

layout(location = 0) in vec4 frag_bg;
layout(location = 1) in vec4 frag_fg;
layout(location = 2) in vec4 frag_sp;
layout(location = 3) in vec2 frag_local_uv;
layout(location = 4) flat in uint frag_style_flags;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 color = frag_bg;
    bool underline = (frag_style_flags & STYLE_FLAG_UNDERLINE) != 0u;
    bool hyperlink = (frag_style_flags & STYLE_FLAG_HYPERLINK) != 0u;
    bool strikethrough = (frag_style_flags & STYLE_FLAG_STRIKETHROUGH) != 0u;
    bool undercurl = (frag_style_flags & STYLE_FLAG_UNDERCURL) != 0u;
    vec4 accent = frag_sp.a > 0.0 ? frag_sp : frag_fg;

    if ((underline || hyperlink) && frag_local_uv.y >= UNDERLINE_TOP && frag_local_uv.y <= UNDERLINE_BOTTOM)
        color = accent;
    else if (strikethrough && frag_local_uv.y >= STRIKETHROUGH_TOP && frag_local_uv.y <= STRIKETHROUGH_BOTTOM)
        color = frag_fg;
    else if (undercurl) {
        float baseline = UNDERCURL_BASELINE + UNDERCURL_AMPLITUDE * sin(frag_local_uv.x * UNDERCURL_FREQ);
        if (abs(frag_local_uv.y - baseline) <= UNDERCURL_THICKNESS)
            color = accent;
    }

    out_color = color;
}
