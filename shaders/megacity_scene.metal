#include <metal_stdlib>

using namespace metal;

struct FrameUniforms
{
    float4x4 view;
    float4x4 proj;
    float4 light_dir;
    float4 point_light_pos;
    float4 label_fade_px;
    float4 render_tuning;
};

struct ObjectUniforms
{
    float4x4 world;
    float4 color;
    float4 uv_rect;
    float4 label_metrics;
};

struct VertexIn
{
    float3 position [[attribute(0)]];
    float3 normal [[attribute(1)]];
    float3 color [[attribute(2)]];
    float2 uv [[attribute(3)]];
    float tex_blend [[attribute(4)]];
};

struct VertexOut
{
    float4 position [[position]];
    float3 normal_ws;
    float3 base_color;
    float3 world_position;
    float2 atlas_uv;
    float tex_blend;
    float2 label_ink_pixel_size;
};

vertex VertexOut scene_vertex(VertexIn in [[stage_in]],
    constant FrameUniforms& frame [[buffer(1)]],
    constant ObjectUniforms& object [[buffer(2)]])
{
    VertexOut out;
    const float4 world_position = object.world * float4(in.position, 1.0);
    out.position = frame.proj * frame.view * world_position;
    out.normal_ws = normalize(float3x3(object.world[0].xyz, object.world[1].xyz, object.world[2].xyz) * in.normal);
    out.base_color = in.color * object.color.rgb;
    out.world_position = world_position.xyz;
    out.atlas_uv = mix(object.uv_rect.xy, object.uv_rect.zw, in.uv);
    out.tex_blend = in.tex_blend;
    out.label_ink_pixel_size = object.label_metrics.xy;
    return out;
}

fragment float4 scene_fragment(
    VertexOut in [[stage_in]],
    constant FrameUniforms& frame [[buffer(1)]],
    texture2d<float> signAtlas [[texture(0)]],
    sampler signSampler [[sampler(0)]])
{
    const float3 normal_ws = normalize(in.normal_ws);
    const float3 light_dir = normalize(-frame.light_dir.xyz);
    const float ndotl = max(dot(normal_ws, light_dir), 0.0);
    const float ambient = max(frame.render_tuning.z, 0.0);
    const float directional = 0.52 * ndotl;
    const float hemi_factor = normal_ws.y * 0.5 + 0.5;
    const float3 hemi = mix(float3(0.84, 0.82, 0.78), float3(1.04, 1.03, 1.01), hemi_factor);
    const float3 point_vec = frame.point_light_pos.xyz - in.world_position;
    const float point_dist = length(point_vec);
    const float3 point_dir = point_dist > 1e-4 ? point_vec / point_dist : float3(0.0, 1.0, 0.0);
    const float point_ndotl = max(dot(normal_ws, point_dir), 0.0);
    const float point_radius = max(frame.point_light_pos.w, 1.0);
    const float point_atten = clamp(1.0 - point_dist / point_radius, 0.0, 1.0);
    const float point_light = max(frame.render_tuning.y, 0.0) * point_ndotl * point_atten * point_atten;
    const float3 point_color = float3(1.05, 0.98, 0.90);
    float3 shaded = in.base_color * (hemi * (ambient + directional) + point_color * point_light);
    if (in.tex_blend > 0.5)
    {
        const float4 label = signAtlas.sample(signSampler, in.atlas_uv);
        const float2 atlas_texels_per_pixel = max(
            fwidth(in.atlas_uv) * float2(signAtlas.get_width(), signAtlas.get_height()),
            float2(1e-5));
        const float2 projected_ink_pixels = in.label_ink_pixel_size / atlas_texels_per_pixel;
        const float projected_text_pixels = min(projected_ink_pixels.x, projected_ink_pixels.y);
        const float fade_start_px = max(min(frame.label_fade_px.x, frame.label_fade_px.y), 0.0);
        const float fade_end_px = max(max(frame.label_fade_px.x, frame.label_fade_px.y), fade_start_px + 1e-3);
        const float visibility = smoothstep(fade_start_px, fade_end_px, projected_text_pixels);
        const float label_alpha = smoothstep(0.18, 0.55, label.a) * visibility;
        shaded = mix(shaded, float3(0.0), label_alpha);
    }
    const float output_gamma = max(frame.render_tuning.x, 1.0);
    const float3 encoded = pow(max(shaded, float3(0.0)), float3(1.0 / output_gamma));
    return float4(encoded, 1.0);
}
