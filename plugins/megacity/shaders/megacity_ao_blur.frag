#version 450

layout(set = 0, binding = 0) uniform FrameUniforms
{
    mat4 view;
    mat4 proj;
    mat4 inv_view_proj;
    vec4 camera_pos;
    vec4 light_dir;
    vec4 point_light_pos;
    vec4 label_fade_px;
    vec4 render_tuning;
    vec4 perf_tuning;
    vec4 screen_params;
    vec4 ao_params; // x = radius_world, y = radius_pixels, z = bias, w = power
    vec4 debug_view;
    vec4 world_debug_bounds;
    mat4 shadow_view_proj[3];
    mat4 shadow_texture_matrix[3];
    vec4 shadow_split_depths;
    vec4 shadow_params;
    mat4 point_shadow_view_proj[6];
    mat4 point_shadow_texture_matrix[6];
    vec4 point_shadow_params;
}
frame;
layout(set = 0, binding = 1) uniform sampler2D ao_input;
layout(set = 0, binding = 2) uniform sampler2D gbuffer_normal;
layout(set = 0, binding = 3) uniform sampler2D gbuffer_depth;

layout(location = 0) out vec4 out_ao;

vec3 oct_decode(vec2 e)
{
    vec2 f = e * 2.0 - 1.0;
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * mix(vec2(-1.0), vec2(1.0), greaterThanEqual(n.xy, vec2(0.0)));
    return normalize(n);
}

vec2 uv_to_ndc(vec2 uv)
{
    float ndc_y = (frame.proj[1][1] >= 0.0)
        ? (1.0 - 2.0 * uv.y)
        : (2.0 * uv.y - 1.0);
    return vec2(uv.x * 2.0 - 1.0, ndc_y);
}

vec3 reconstruct_world(vec2 uv, float depth)
{
    vec2 ndc_xy = uv_to_ndc(uv);
    vec4 ndc = vec4(ndc_xy, depth, 1.0);
    vec4 world = frame.inv_view_proj * ndc;
    return world.xyz / max(world.w, 1e-6);
}

float spatial_weight(ivec2 offset)
{
    const float sigma = 1.35;
    float r2 = float(offset.x * offset.x + offset.y * offset.y);
    return exp(-0.5 * r2 / (sigma * sigma));
}

bool ao_denoise_enabled()
{
    return frame.debug_view.y > 0.5;
}

void main()
{
    vec2 center_uv = clamp(
        (gl_FragCoord.xy - frame.screen_params.xy) * frame.screen_params.zw,
        vec2(0.0),
        vec2(1.0));

    float center_depth = texture(gbuffer_depth, center_uv).r;
    if (center_depth >= 0.99999)
    {
        out_ao = vec4(vec3(1.0), 1.0);
        return;
    }

    vec4 center_normal_data = texture(gbuffer_normal, center_uv);
    vec3 center_normal = oct_decode(center_normal_data.rg);
    vec3 center_world = reconstruct_world(center_uv, center_depth);
    float center_ao = texture(ao_input, center_uv).r;
    if (!ao_denoise_enabled())
    {
        out_ao = vec4(vec3(center_ao), 1.0);
        return;
    }

    float radius_world = max(frame.ao_params.x, 1e-3);

    float weighted_sum = center_ao;
    float total_weight = 1.0;

    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            if (x == 0 && y == 0)
                continue;

            vec2 sample_uv = center_uv + vec2(float(x), float(y)) * frame.screen_params.zw;
            if (any(lessThan(sample_uv, vec2(0.0))) || any(greaterThan(sample_uv, vec2(1.0))))
                continue;

            float sample_depth = texture(gbuffer_depth, sample_uv).r;
            if (sample_depth >= 0.99999)
                continue;

            vec4 sample_normal_data = texture(gbuffer_normal, sample_uv);
            vec3 sample_normal = oct_decode(sample_normal_data.rg);
            vec3 sample_world = reconstruct_world(sample_uv, sample_depth);
            float sample_ao = texture(ao_input, sample_uv).r;

            float normal_weight = pow(max(dot(center_normal, sample_normal), 0.0), 12.0);
            float distance_weight = exp(-length(sample_world - center_world) / max(radius_world * 0.35, 1e-3));
            float weight = spatial_weight(ivec2(x, y)) * normal_weight * distance_weight;
            if (weight <= 1e-4)
                continue;

            weighted_sum += sample_ao * weight;
            total_weight += weight;
        }
    }

    float visibility = weighted_sum / max(total_weight, 1e-4);
    out_ao = vec4(vec3(visibility), 1.0);
}
