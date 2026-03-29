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

vec2 ndc_to_uv(vec2 ndc)
{
    float uv_y = (frame.proj[1][1] >= 0.0)
        ? (1.0 - ndc.y) * 0.5
        : (ndc.y + 1.0) * 0.5;
    return vec2(ndc.x * 0.5 + 0.5, uv_y);
}

float hash1(float n)
{
    return fract(sin(n) * 43758.5453123);
}

vec2 tiled_noise(vec2 pixel)
{
    vec2 cell = mod(floor(pixel), 4.0);
    float seed = cell.x + cell.y * 4.0;
    return normalize(vec2(hash1(seed * 17.0 + 0.13), hash1(seed * 31.0 + 0.71)) * 2.0 - 1.0);
}

vec3 kernel_sample(int index, int count)
{
    float i = float(index);
    float u = hash1(i * 12.9898 + 0.17);
    float v = hash1(i * 78.233 + 0.53);
    float phi = 6.28318530718 * u;
    float z = v;
    float r = sqrt(max(1.0 - z * z, 0.0));
    vec3 kernel_dir = vec3(cos(phi) * r, sin(phi) * r, z);
    float t = (i + 0.5) / float(count);
    float scale = mix(0.1, 1.0, t * t);
    return kernel_dir * scale;
}

int kernel_size()
{
    return int(clamp(floor(frame.debug_view.z + 0.5), 1.0, 64.0));
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

    vec4 normal_data = texture(gbuffer_normal, screen_uv);
    vec3 normal_ws = oct_decode(normal_data.rg);
    vec3 world_pos = reconstruct_world(screen_uv, depth);
    vec3 normal_vs = normalize(mat3(frame.view) * normal_ws);
    vec3 frag_pos_vs = vec3(frame.view * vec4(world_pos, 1.0));
    vec2 random_vec_2d = tiled_noise(gl_FragCoord.xy);
    vec3 random_vec = vec3(random_vec_2d, 0.0);
    vec3 tangent = normalize(random_vec - normal_vs * dot(random_vec, normal_vs));
    vec3 bitangent = cross(normal_vs, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal_vs);

    float radius_world = max(frame.ao_params.x, 1e-3);
    float bias = clamp(frame.ao_params.z, 0.0, 0.95);
    int sample_count = kernel_size();
    float occlusion = 0.0;
    for (int sample_index = 0; sample_index < sample_count; ++sample_index)
    {
        vec3 sample_pos_vs = frag_pos_vs + (tbn * kernel_sample(sample_index, sample_count)) * radius_world;
        vec4 offset = frame.proj * vec4(sample_pos_vs, 1.0);
        if (abs(offset.w) <= 1e-6)
            continue;
        vec3 ndc = offset.xyz / offset.w;
        vec2 sample_uv = ndc_to_uv(ndc.xy);
        if (any(lessThan(sample_uv, vec2(0.0))) || any(greaterThan(sample_uv, vec2(1.0))))
            continue;

        float sample_depth = texture(gbuffer_depth, sample_uv).r;
        if (sample_depth >= 0.99999)
            continue;

        vec3 sample_world = reconstruct_world(sample_uv, sample_depth);
        vec3 sample_depth_vs = vec3(frame.view * vec4(sample_world, 1.0));
        float range_check = smoothstep(0.0, 1.0, radius_world / max(abs(frag_pos_vs.z - sample_depth_vs.z), 1e-4));
        occlusion += (sample_depth_vs.z >= sample_pos_vs.z + bias ? 1.0 : 0.0) * range_check;
    }

    float visibility = 1.0 - occlusion / float(sample_count);
    visibility = pow(clamp(visibility, 0.0, 1.0), max(frame.ao_params.w, 1e-3));
    out_ao = vec4(visibility, 0.0, 0.0, 1.0);
}
