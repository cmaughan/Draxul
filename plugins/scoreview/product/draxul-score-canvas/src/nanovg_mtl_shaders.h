// Metal shader source for NanoVG backend — compiled at runtime.
// This is ported from the nanovg_gl.h reference implementation.

#pragma once

static const char* kNanoVGMetalShaderSource = R"metal(
#include <metal_stdlib>
using namespace metal;

struct NVGvertex_in {
    float2 pos [[attribute(0)]];
    float2 tcoord [[attribute(1)]];
};

struct FragUniforms {
    float3x3 scissorMat;
    float3x3 paintMat;
    float4 innerCol;
    float4 outerCol;
    float2 scissorExt;
    float2 scissorScale;
    float2 extent;
    float radius;
    float feather;
    float strokeMult;
    float strokeThr;
    int texType;
    int type;
};

struct VertexOut {
    float4 position [[position]];
    float2 ftcoord;
    float2 fpos;
};

struct ViewUniforms {
    float2 viewSize;
};

vertex VertexOut nanovg_vertex(
    NVGvertex_in in [[stage_in]],
    constant ViewUniforms& view [[buffer(1)]])
{
    VertexOut out;
    out.ftcoord = in.tcoord;
    out.fpos = in.pos;
    out.position = float4(
        2.0 * in.pos.x / view.viewSize.x - 1.0,
        1.0 - 2.0 * in.pos.y / view.viewSize.y,
        0.0, 1.0);
    return out;
}

float sdroundrect(float2 pt, float2 ext, float rad) {
    float2 ext2 = ext - float2(rad);
    float2 d = abs(pt) - ext2;
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - rad;
}

float scissorMask(float2 p, float3x3 scissorMat, float2 scissorExt, float2 scissorScale) {
    float2 sc = abs((scissorMat * float3(p, 1.0)).xy) - scissorExt;
    sc = float2(0.5) - sc * scissorScale;
    return clamp(sc.x, 0.0, 1.0) * clamp(sc.y, 0.0, 1.0);
}

float strokeMask(float2 ftcoord, float strokeMult) {
    return min(1.0, (1.0 - abs(ftcoord.x * 2.0 - 1.0)) * strokeMult) * min(1.0, ftcoord.y);
}

fragment float4 nanovg_fragment_aa(
    VertexOut in [[stage_in]],
    constant FragUniforms& frag [[buffer(0)]],
    texture2d<float> tex [[texture(0)]],
    sampler smp [[sampler(0)]])
{
    float scissor = scissorMask(in.fpos, frag.scissorMat, frag.scissorExt, frag.scissorScale);
    float strokeAlpha = strokeMask(in.ftcoord, frag.strokeMult);
    if (strokeAlpha < frag.strokeThr) discard_fragment();

    float4 result;
    if (frag.type == 0) {
        // Gradient fill
        float2 pt = (frag.paintMat * float3(in.fpos, 1.0)).xy;
        float d = clamp((sdroundrect(pt, frag.extent, frag.radius) + frag.feather * 0.5) / frag.feather, 0.0, 1.0);
        float4 color = mix(frag.innerCol, frag.outerCol, d);
        color *= strokeAlpha * scissor;
        result = color;
    } else if (frag.type == 1) {
        // Image fill
        float2 pt = (frag.paintMat * float3(in.fpos, 1.0)).xy / frag.extent;
        float4 color = tex.sample(smp, pt);
        if (frag.texType == 1) color = float4(color.xyz * color.w, color.w);
        if (frag.texType == 2) color = float4(color.x);
        color *= frag.innerCol;
        color *= strokeAlpha * scissor;
        result = color;
    } else if (frag.type == 2) {
        // Stencil fill
        result = float4(1.0);
    } else if (frag.type == 3) {
        // Textured triangles
        float4 color = tex.sample(smp, in.ftcoord);
        if (frag.texType == 1) color = float4(color.xyz * color.w, color.w);
        if (frag.texType == 2) color = float4(color.x);
        color *= scissor;
        result = color * frag.innerCol;
    }
    return result;
}

// Non-AA variant (identical but strokeAlpha is always 1.0)
fragment float4 nanovg_fragment(
    VertexOut in [[stage_in]],
    constant FragUniforms& frag [[buffer(0)]],
    texture2d<float> tex [[texture(0)]],
    sampler smp [[sampler(0)]])
{
    float scissor = scissorMask(in.fpos, frag.scissorMat, frag.scissorExt, frag.scissorScale);
    if (frag.strokeThr > 0.0 && 1.0 < frag.strokeThr) discard_fragment();

    float4 result;
    if (frag.type == 0) {
        float2 pt = (frag.paintMat * float3(in.fpos, 1.0)).xy;
        float d = clamp((sdroundrect(pt, frag.extent, frag.radius) + frag.feather * 0.5) / frag.feather, 0.0, 1.0);
        float4 color = mix(frag.innerCol, frag.outerCol, d);
        color *= scissor;
        result = color;
    } else if (frag.type == 1) {
        float2 pt = (frag.paintMat * float3(in.fpos, 1.0)).xy / frag.extent;
        float4 color = tex.sample(smp, pt);
        if (frag.texType == 1) color = float4(color.xyz * color.w, color.w);
        if (frag.texType == 2) color = float4(color.x);
        color *= frag.innerCol;
        color *= scissor;
        result = color;
    } else if (frag.type == 2) {
        result = float4(1.0);
    } else if (frag.type == 3) {
        float4 color = tex.sample(smp, in.ftcoord);
        if (frag.texType == 1) color = float4(color.xyz * color.w, color.w);
        if (frag.texType == 2) color = float4(color.x);
        color *= scissor;
        result = color * frag.innerCol;
    }
    return result;
}
)metal";
