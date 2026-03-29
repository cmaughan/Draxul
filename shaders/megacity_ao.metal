#include <metal_stdlib>

using namespace metal;

struct FrameUniforms
{
    float4x4 view;
    float4x4 proj;
    float4x4 inv_view_proj;
    float4 camera_pos;
    float4 light_dir;
    float4 point_light_pos;
    float4 label_fade_px;
    float4 render_tuning;
    float4 perf_tuning;
    float4 screen_params;
    float4 ao_params; // x = radius_world, y = radius_pixels, z = bias, w = power
    float4 debug_view;
    float4 world_debug_bounds;
    float4x4 shadow_view_proj[3];
    float4x4 shadow_texture_matrix[3];
    float4 shadow_split_depths;
    float4 shadow_params;
    float4x4 point_shadow_view_proj[6];
    float4x4 point_shadow_texture_matrix[6];
    float4 point_shadow_params;
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

float2 ndc_to_uv(constant FrameUniforms& frame, float2 ndc)
{
    const float uv_y = (frame.proj[1][1] >= 0.0)
        ? (1.0 - ndc.y) * 0.5
        : (ndc.y + 1.0) * 0.5;
    return float2(ndc.x * 0.5 + 0.5, uv_y);
}

float hash1(float n)
{
    return fract(sin(n) * 43758.5453123);
}

float spatial_weight(int2 offset)
{
    constexpr float sigma = 1.35;
    const float r2 = float(offset.x * offset.x + offset.y * offset.y);
    return exp(-0.5 * r2 / (sigma * sigma));
}

float2 tiled_noise(float2 pixel)
{
    const float2 cell = fmod(floor(pixel), 4.0);
    const float seed = cell.x + cell.y * 4.0;
    return normalize(float2(hash1(seed * 17.0 + 0.13), hash1(seed * 31.0 + 0.71)) * 2.0 - 1.0);
}

float3 kernel_sample(int index, int count)
{
    const float i = float(index);
    const float u = hash1(i * 12.9898 + 0.17);
    const float v = hash1(i * 78.233 + 0.53);
    const float phi = 6.28318530718 * u;
    const float z = v;
    const float r = sqrt(max(1.0 - z * z, 0.0));
    const float3 sample = float3(cos(phi) * r, sin(phi) * r, z);
    const float t = (i + 0.5) / float(count);
    const float scale = mix(0.1, 1.0, t * t);
    return sample * scale;
}

int kernel_size(constant FrameUniforms& frame)
{
    return int(clamp(floor(frame.debug_view.z + 0.5), 1.0, 64.0));
}

bool ao_denoise_enabled(constant FrameUniforms& frame)
{
    return frame.debug_view.y > 0.5;
}

fragment float4 ao_fragment(
    VertexOut in [[stage_in]],
    constant FrameUniforms& frame [[buffer(0)]],
    texture2d<float> normalTexture [[texture(0)]],
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

    const float4 normal_data = normalTexture.sample(gbufferSampler, screen_uv);
    const float3 normal_ws = oct_decode(normal_data.rg);
    const float3 world_pos = reconstruct_world(frame, screen_uv, depth);
    const float3 normal_vs = normalize(float3x3(frame.view[0].xyz, frame.view[1].xyz, frame.view[2].xyz) * normal_ws);
    const float3 frag_pos_vs = float3(frame.view * float4(world_pos, 1.0));
    const float2 random_vec_2d = tiled_noise(in.position.xy);
    const float3 random_vec = float3(random_vec_2d, 0.0);
    const float3 tangent = normalize(random_vec - normal_vs * dot(random_vec, normal_vs));
    const float3 bitangent = cross(normal_vs, tangent);
    const float3x3 tbn = float3x3(tangent, bitangent, normal_vs);

    const float radius_world = max(frame.ao_params.x, 1e-3);
    const float bias = clamp(frame.ao_params.z, 0.0, 0.95);
    const int sample_count = kernel_size(frame);
    float occlusion = 0.0;
    for (int sample_index = 0; sample_index < sample_count; ++sample_index)
    {
        const float3 sample_pos_vs = frag_pos_vs + (tbn * kernel_sample(sample_index, sample_count)) * radius_world;
        const float4 offset = frame.proj * float4(sample_pos_vs, 1.0);
        if (abs(offset.w) <= 1e-6)
            continue;
        const float3 ndc = offset.xyz / offset.w;
        const float2 sample_uv = ndc_to_uv(frame, ndc.xy);
        if (any(sample_uv < 0.0) || any(sample_uv > 1.0))
            continue;

        const float sample_depth = depthTexture.sample(gbufferSampler, sample_uv);
        if (sample_depth >= 0.99999)
            continue;

        const float3 sample_world = reconstruct_world(frame, sample_uv, sample_depth);
        const float3 sample_depth_vs = float3(frame.view * float4(sample_world, 1.0));
        const float range_check = smoothstep(0.0, 1.0, radius_world / max(abs(frag_pos_vs.z - sample_depth_vs.z), 1e-4));
        occlusion += (sample_depth_vs.z >= sample_pos_vs.z + bias ? 1.0 : 0.0) * range_check;
    }

    float visibility = 1.0 - occlusion / float(sample_count);
    visibility = pow(clamp(visibility, 0.0, 1.0), max(frame.ao_params.w, 1e-3));
    return float4(visibility, 0.0, 0.0, 1.0);
}

fragment float4 ao_blur_fragment(
    VertexOut in [[stage_in]],
    constant FrameUniforms& frame [[buffer(0)]],
    texture2d<float> rawAoTexture [[texture(0)]],
    texture2d<float> normalTexture [[texture(1)]],
    depth2d<float> depthTexture [[texture(2)]],
    sampler pointSampler [[sampler(0)]])
{
    const float2 center_uv = clamp(
        (in.position.xy - frame.screen_params.xy) * frame.screen_params.zw,
        float2(0.0),
        float2(1.0));

    const float center_depth = depthTexture.sample(pointSampler, center_uv);
    if (center_depth >= 0.99999)
        return float4(1.0, 1.0, 1.0, 1.0);

    const float4 center_normal_data = normalTexture.sample(pointSampler, center_uv);
    const float3 center_normal = oct_decode(center_normal_data.rg);
    const float3 center_world = reconstruct_world(frame, center_uv, center_depth);
    const float center_ao = rawAoTexture.sample(pointSampler, center_uv).r;
    if (!ao_denoise_enabled(frame))
        return float4(center_ao, center_ao, center_ao, 1.0);

    const float radius_world = max(frame.ao_params.x, 1e-3);

    float weighted_sum = center_ao;
    float total_weight = 1.0;

    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            if (x == 0 && y == 0)
                continue;

            const float2 sample_uv = center_uv + float2(float(x), float(y)) * frame.screen_params.zw;
            if (any(sample_uv < 0.0) || any(sample_uv > 1.0))
                continue;

            const float sample_depth = depthTexture.sample(pointSampler, sample_uv);
            if (sample_depth >= 0.99999)
                continue;

            const float4 sample_normal_data = normalTexture.sample(pointSampler, sample_uv);
            const float3 sample_normal = oct_decode(sample_normal_data.rg);
            const float3 sample_world = reconstruct_world(frame, sample_uv, sample_depth);
            const float sample_ao = rawAoTexture.sample(pointSampler, sample_uv).r;

            const float normal_weight = pow(max(dot(center_normal, sample_normal), 0.0), 12.0);
            const float distance_weight = exp(-length(sample_world - center_world) / max(radius_world * 0.35, 1e-3));
            const float weight = spatial_weight(int2(x, y)) * normal_weight * distance_weight;
            if (weight <= 1e-4)
                continue;

            weighted_sum += sample_ao * weight;
            total_weight += weight;
        }
    }

    const float visibility = weighted_sum / max(total_weight, 1e-4);
    return float4(visibility, visibility, visibility, 1.0);
}
