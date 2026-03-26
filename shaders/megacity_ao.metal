#include <metal_stdlib>

using namespace metal;

struct FrameUniforms
{
    float4x4 view;
    float4x4 proj;
    float4x4 inv_view_proj;
    float4 light_dir;
    float4 point_light_pos;
    float4 label_fade_px;
    float4 render_tuning;
    float4 screen_params;
    float4 ao_params; // x = radius_world, y = radius_pixels, z = bias, w = power
};

struct VertexOut
{
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut ao_vertex(uint vertex_id [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0),
    };

    VertexOut out;
    const float2 position = positions[vertex_id];
    out.position = float4(position, 0.0, 1.0);
    out.uv = position * 0.5 + 0.5;
    return out;
}

float3 oct_decode(float2 encoded)
{
    const float2 f = encoded * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * select(float2(-1.0), float2(1.0), n.xy >= 0.0);
    return normalize(n);
}

float2 uv_to_ndc(constant FrameUniforms& frame, float2 uv)
{
    const float ndc_y = (frame.proj[1][1] >= 0.0)
        ? (1.0 - 2.0 * uv.y)
        : (2.0 * uv.y - 1.0);
    return float2(uv.x * 2.0 - 1.0, ndc_y);
}

float3 reconstruct_world(constant FrameUniforms& frame, float2 uv, float depth)
{
    const float2 ndc_xy = uv_to_ndc(frame, uv);
    const float4 ndc = float4(ndc_xy, depth, 1.0);
    const float4 world = frame.inv_view_proj * ndc;
    return world.xyz / max(world.w, 1e-6);
}

float direction_occlusion(
    constant FrameUniforms& frame,
    texture2d<float> materialTexture,
    depth2d<float> depthTexture,
    sampler gbufferSampler,
    float2 uv,
    float3 world_pos,
    float3 normal_ws,
    float2 dir)
{
    constexpr int kStepCount = 4;

    float max_horizon = -1.0;
    const float radius_world = max(frame.ao_params.x, 1e-3);
    const float radius_pixels = max(frame.ao_params.y, 1.0);
    const float bias = clamp(frame.ao_params.z, 0.0, 0.95);

    for (int step_index = 1; step_index <= kStepCount; ++step_index)
    {
        const float t = (float(step_index) - 0.5) / float(kStepCount);
        const float2 sample_uv = uv + dir * (radius_pixels * t) * frame.screen_params.zw;
        if (any(sample_uv < 0.0) || any(sample_uv > 1.0))
            continue;

        const float sample_depth = depthTexture.sample(gbufferSampler, sample_uv);
        if (sample_depth >= 0.99999)
            continue;

        const float3 sample_world = reconstruct_world(frame, sample_uv, sample_depth);
        const float3 sample_vec = sample_world - world_pos;
        const float sample_dist = length(sample_vec);
        if (sample_dist <= 1e-4 || sample_dist > radius_world)
            continue;

        const float3 sample_dir = sample_vec / sample_dist;
        const float horizon = dot(normal_ws, sample_dir);
        const float distance_weight = 1.0 - smoothstep(radius_world * 0.35, radius_world, sample_dist);
        max_horizon = max(max_horizon, horizon * distance_weight);
    }

    return max(max_horizon - bias, 0.0);
}

fragment float4 ao_fragment(
    VertexOut in [[stage_in]],
    constant FrameUniforms& frame [[buffer(0)]],
    texture2d<float> materialTexture [[texture(0)]],
    depth2d<float> depthTexture [[texture(1)]],
    sampler gbufferSampler [[sampler(0)]])
{
    const float2 screen_uv = clamp(
        (in.position.xy - frame.screen_params.xy) * frame.screen_params.zw,
        float2(0.0),
        float2(1.0));
    const float depth = depthTexture.sample(gbufferSampler, screen_uv);
    if (depth >= 0.99999)
        return float4(1.0, 0.0, 0.0, 1.0);

    const float4 material = materialTexture.sample(gbufferSampler, screen_uv);
    const float3 normal_ws = oct_decode(material.rg);
    const float3 world_pos = reconstruct_world(frame, screen_uv, depth);

    constexpr int kDirectionCount = 8;
    float occlusion = 0.0;
    for (int direction_index = 0; direction_index < kDirectionCount; ++direction_index)
    {
        const float angle = (6.28318530718 * float(direction_index)) / float(kDirectionCount);
        const float2 dir = float2(cos(angle), sin(angle));
        occlusion += direction_occlusion(
            frame, materialTexture, depthTexture, gbufferSampler, screen_uv, world_pos, normal_ws, dir);
    }

    float visibility = 1.0 - occlusion / float(kDirectionCount);
    visibility = pow(clamp(visibility, 0.0, 1.0), max(frame.ao_params.w, 1e-3));
    return float4(visibility, 0.0, 0.0, 1.0);
}
