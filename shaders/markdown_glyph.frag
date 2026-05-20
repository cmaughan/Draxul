#version 450
#extension GL_GOOGLE_include_directive : require

#include "decoration_constants.glsl"

layout(set = 0, binding = 1) uniform sampler2D atlas;

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;
layout(location = 2) flat in uint frag_flags;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 atlas_sample = texture(atlas, frag_uv);
    float alpha = atlas_sample.a;
    if (alpha < 0.01) {
        discard;
    }

    bool color_glyph = (frag_flags & STYLE_FLAG_COLOR_GLYPH) != 0u;
    out_color = color_glyph ? atlas_sample : vec4(frag_color.rgb, frag_color.a * alpha);
}
