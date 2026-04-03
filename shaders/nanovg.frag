#version 450

layout(location = 0) in vec2 ftcoord;
layout(location = 1) in vec2 fpos;

layout(location = 0) out vec4 outColor;

// Must match VkNVGfragUniforms layout (std140)
layout(set = 0, binding = 0) uniform FragUniforms {
    // mat3 stored as 3 x vec4 (column-major, padded)
    vec4 scissorMat_col0;
    vec4 scissorMat_col1;
    vec4 scissorMat_col2;
    vec4 paintMat_col0;
    vec4 paintMat_col1;
    vec4 paintMat_col2;
    vec4 innerCol;
    vec4 outerCol;
    vec2 scissorExt;
    vec2 scissorScale;
    vec2 extent;
    float radius;
    float feather;
    float strokeMult;
    float strokeThr;
    int texType;
    int type;
} frag;

layout(set = 0, binding = 1) uniform sampler2D tex;

mat3 buildScissorMat()
{
    return mat3(
        frag.scissorMat_col0.xyz,
        frag.scissorMat_col1.xyz,
        frag.scissorMat_col2.xyz);
}

mat3 buildPaintMat()
{
    return mat3(
        frag.paintMat_col0.xyz,
        frag.paintMat_col1.xyz,
        frag.paintMat_col2.xyz);
}

float sdroundrect(vec2 pt, vec2 ext, float rad)
{
    vec2 ext2 = ext - vec2(rad);
    vec2 d = abs(pt) - ext2;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - rad;
}

float scissorMask(vec2 p)
{
    mat3 sm = buildScissorMat();
    vec2 sc = abs((sm * vec3(p, 1.0)).xy) - frag.scissorExt;
    sc = vec2(0.5) - sc * frag.scissorScale;
    return clamp(sc.x, 0.0, 1.0) * clamp(sc.y, 0.0, 1.0);
}

float strokeMask()
{
    return min(1.0, (1.0 - abs(ftcoord.x * 2.0 - 1.0)) * frag.strokeMult) * min(1.0, ftcoord.y);
}

void main()
{
    float scissor = scissorMask(fpos);
    float strokeAlpha = strokeMask();
    if (strokeAlpha < frag.strokeThr) discard;

    vec4 result;
    if (frag.type == 0) {
        // Gradient fill
        mat3 pm = buildPaintMat();
        vec2 pt = (pm * vec3(fpos, 1.0)).xy;
        float d = clamp((sdroundrect(pt, frag.extent, frag.radius) + frag.feather * 0.5) / frag.feather, 0.0, 1.0);
        vec4 color = mix(frag.innerCol, frag.outerCol, d);
        color *= strokeAlpha * scissor;
        result = color;
    } else if (frag.type == 1) {
        // Image fill
        mat3 pm = buildPaintMat();
        vec2 pt = (pm * vec3(fpos, 1.0)).xy / frag.extent;
        vec4 color = texture(tex, pt);
        if (frag.texType == 1) color = vec4(color.xyz * color.w, color.w);
        if (frag.texType == 2) color = vec4(color.x);
        color *= frag.innerCol;
        color *= strokeAlpha * scissor;
        result = color;
    } else if (frag.type == 2) {
        // Stencil fill
        result = vec4(1.0);
    } else if (frag.type == 3) {
        // Textured triangles
        vec4 color = texture(tex, ftcoord);
        if (frag.texType == 1) color = vec4(color.xyz * color.w, color.w);
        if (frag.texType == 2) color = vec4(color.x);
        color *= scissor;
        result = color * frag.innerCol;
    }
    outColor = result;
}
