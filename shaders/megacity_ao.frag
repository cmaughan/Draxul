#version 450

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    mat4 inv_view_proj;
    vec4 light_dir;
    vec4 point_light_pos;
    vec4 label_fade_px;
    vec4 render_tuning;
    vec4 screen_params;
    vec4 ao_params; // x = radius_world, y = radius_pixels, z = bias, w = power
} frame;
layout(set = 0, binding = 3) uniform sampler2D gbuffer_material;
layout(set = 0, binding = 4) uniform sampler2D gbuffer_depth;

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

float direction_occlusion(vec2 uv, vec3 world_pos, vec3 normal_ws, vec2 dir)
{
    const int kStepCount = 4;

    float max_horizon = -1.0;
    const float radius_world = max(frame.ao_params.x, 1e-3);
    const float radius_pixels = max(frame.ao_params.y, 1.0);
    const float bias = clamp(frame.ao_params.z, 0.0, 0.95);

    for (int step_index = 1; step_index <= kStepCount; ++step_index)
    {
        float t = (float(step_index) - 0.5) / float(kStepCount);
        vec2 sample_uv = uv + dir * (radius_pixels * t) * frame.screen_params.zw;
        if (any(lessThan(sample_uv, vec2(0.0))) || any(greaterThan(sample_uv, vec2(1.0))))
            continue;

        float sample_depth = texture(gbuffer_depth, sample_uv).r;
        if (sample_depth >= 0.99999)
            continue;

        vec3 sample_world = reconstruct_world(sample_uv, sample_depth);
        vec3 sample_vec = sample_world - world_pos;
        float sample_dist = length(sample_vec);
        if (sample_dist <= 1e-4 || sample_dist > radius_world)
            continue;

        vec3 sample_dir = sample_vec / sample_dist;
        float horizon = dot(normal_ws, sample_dir);
        float distance_weight = 1.0 - smoothstep(radius_world * 0.35, radius_world, sample_dist);
        max_horizon = max(max_horizon, horizon * distance_weight);
    }

    return max(max_horizon - bias, 0.0);
}

void main()
{
    vec2 screen_uv = clamp(
        (gl_FragCoord.xy - frame.screen_params.xy) * frame.screen_params.zw,
        vec2(0.0),
        vec2(1.0));
    float depth = texture(gbuffer_depth, screen_uv).r;
    if (depth >= 0.99999)
    {
        out_ao = vec4(1.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 material = texture(gbuffer_material, screen_uv);
    vec3 normal_ws = oct_decode(material.rg);
    vec3 world_pos = reconstruct_world(screen_uv, depth);

    const int kDirectionCount = 8;
    float occlusion = 0.0;
    for (int direction_index = 0; direction_index < kDirectionCount; ++direction_index)
    {
        float angle = (6.28318530718 * float(direction_index)) / float(kDirectionCount);
        vec2 dir = vec2(cos(angle), sin(angle));
        occlusion += direction_occlusion(screen_uv, world_pos, normal_ws, dir);
    }

    float visibility = 1.0 - occlusion / float(kDirectionCount);
    visibility = pow(clamp(visibility, 0.0, 1.0), max(frame.ao_params.w, 1e-3));
    out_ao = vec4(visibility, 0.0, 0.0, 1.0);
}
