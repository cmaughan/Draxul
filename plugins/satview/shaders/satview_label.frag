#version 450

layout(binding = 7) uniform sampler2D label_atlas;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 out_color;

void main()
{
    vec2 texel = 1.0 / vec2(textureSize(label_atlas, 0));
    float coverage = texture(label_atlas, in_uv).a;
    float nearby = coverage;
    nearby = max(nearby, texture(label_atlas, in_uv + vec2(texel.x, 0.0)).a);
    nearby = max(nearby, texture(label_atlas, in_uv - vec2(texel.x, 0.0)).a);
    nearby = max(nearby, texture(label_atlas, in_uv + vec2(0.0, texel.y)).a);
    nearby = max(nearby, texture(label_atlas, in_uv - vec2(0.0, texel.y)).a);
    float glyph_alpha = coverage * in_color.a;
    float halo_alpha = max(nearby - coverage, 0.0) * 0.72 * in_color.a;
    float alpha = glyph_alpha + halo_alpha * (1.0 - glyph_alpha);
    vec3 premultiplied = in_color.rgb * glyph_alpha
        + vec3(0.004, 0.006, 0.010) * halo_alpha * (1.0 - glyph_alpha);
    out_color = vec4(premultiplied, alpha);
}
