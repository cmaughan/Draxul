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
    vec4 ao_params;
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
struct MaterialInstance
{
    vec4 scalar_params;
    uvec4 texture_indices;
    uvec4 metadata;
};

layout(set = 0, binding = 1) uniform MaterialUniforms
{
    MaterialInstance materials[64];
}
material_table;
layout(set = 0, binding = 2) uniform sampler2D sign_atlas;
layout(set = 0, binding = 3) uniform sampler2D ao_buffer;
layout(set = 0, binding = 4) uniform sampler2D material_textures[25];
layout(set = 0, binding = 5) uniform sampler2D shadow_maps[3];
layout(set = 0, binding = 6) uniform sampler2D point_shadow_maps[6];
layout(set = 0, binding = 7, std430) readonly buffer PerformanceHeatBuffer
{
    float heat_values[];
}
performance_heat;

layout(location = 0) in vec3 in_normal_ws;
layout(location = 1) in vec3 in_base_color;
layout(location = 2) in vec3 in_world_position;
layout(location = 3) in vec2 in_atlas_uv;
layout(location = 4) in float in_tex_blend;
layout(location = 5) in vec4 in_label_metrics;
layout(location = 6) flat in uint in_material_index;
layout(location = 7) in vec2 in_material_uv;
layout(location = 8) in vec4 in_tangent_ws;
layout(location = 9) in float in_opacity;
layout(location = 10) in float in_layer_id;
layout(location = 0) out vec4 out_frag_color;

const float kPi = 3.14159265359;
const uint kShadingFlatColor = 0u;
const uint kShadingTexturedTintedPbr = 1u;
const uint kShadingVertexTintPbr = 2u;
const uint kShadingLeafCutoutPbr = 3u;
const float kLeafAlphaCutoff = 0.35;

float distribution_ggx(vec3 n, vec3 h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = max(dot(n, h), 0.0);
    float ndoth2 = ndoth * ndoth;
    float denom = ndoth2 * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * denom * denom, 1e-4);
}

float geometry_schlick_ggx(float ndotv, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return ndotv / max(ndotv * (1.0 - k) + k, 1e-4);
}

float geometry_smith(vec3 n, vec3 v, vec3 l, float roughness)
{
    return geometry_schlick_ggx(max(dot(n, v), 0.0), roughness)
        * geometry_schlick_ggx(max(dot(n, l), 0.0), roughness);
}

vec3 fresnel_schlick(float cos_theta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

mat3 tangent_basis(vec3 normal_ws, vec4 tangent_ws)
{
    vec3 tangent = normalize(tangent_ws.xyz);
    vec3 bitangent = normalize(cross(normal_ws, tangent) * tangent_ws.w);
    return mat3(tangent, bitangent, normal_ws);
}

int shadow_cascade_index(float clip_depth)
{
    if (clip_depth <= frame.shadow_split_depths.x)
        return 0;
    if (clip_depth <= frame.shadow_split_depths.y)
        return 1;
    return 2;
}

float sample_shadow_map(int cascade_index, vec3 shadow_coord)
{
    if (shadow_coord.x <= 0.0 || shadow_coord.x >= 1.0 || shadow_coord.y <= 0.0 || shadow_coord.y >= 1.0)
        return 1.0;
    if (shadow_coord.z <= 0.0 || shadow_coord.z >= 1.0)
        return 1.0;

    float visibility = 0.0;
    float texel = max(frame.shadow_params.w, 1e-6);
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 uv = shadow_coord.xy + vec2(x, y) * texel;
            float stored_depth = texture(shadow_maps[cascade_index], uv).r;
            visibility += shadow_coord.z <= stored_depth ? 1.0 : 0.0;
        }
    }
    return visibility / 9.0;
}

float directional_shadow_visibility(vec3 world_position, vec3 normal_ws, float ndotl, float clip_depth)
{
    int cascade_index = shadow_cascade_index(clip_depth);
    float normal_bias = max(frame.shadow_params.z, 0.0);
    vec3 biased_world = world_position + normal_ws * (normal_bias * (1.0 - ndotl));
    vec4 shadow_position = frame.shadow_texture_matrix[cascade_index] * vec4(biased_world, 1.0);
    vec3 shadow_coord = shadow_position.xyz / max(shadow_position.w, 1e-6);
    shadow_coord.z -= max(frame.shadow_params.y, 0.0);
    return sample_shadow_map(cascade_index, shadow_coord);
}

float point_shadow_visibility(vec3 world_position, vec3 normal_ws, vec3 point_dir)
{
    if (frame.point_shadow_params.w < 0.5)
        return 1.0;

    float radius = max(frame.point_light_pos.w, 1.0);
    float sample_bias = max(frame.point_shadow_params.x, 0.0);
    float normal_bias = max(frame.point_shadow_params.y, 0.0);
    vec3 light_to_surface = world_position - frame.point_light_pos.xyz;
    float current_depth = length(light_to_surface) / radius;
    vec3 abs_dir = abs(light_to_surface);
    int face_index = 0;
    if (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z)
        face_index = light_to_surface.x >= 0.0 ? 0 : 1;
    else if (abs_dir.y >= abs_dir.x && abs_dir.y >= abs_dir.z)
        face_index = light_to_surface.y >= 0.0 ? 2 : 3;
    else
        face_index = light_to_surface.z >= 0.0 ? 4 : 5;

    vec4 shadow_position = frame.point_shadow_texture_matrix[face_index] * vec4(world_position, 1.0);
    vec3 shadow_coord = shadow_position.xyz / max(shadow_position.w, 1e-6);
    if (shadow_coord.x <= 0.0 || shadow_coord.x >= 1.0 || shadow_coord.y <= 0.0 || shadow_coord.y >= 1.0)
        return 1.0;
    if (shadow_coord.z <= 0.0 || shadow_coord.z >= 1.0)
        return 1.0;

    float stored_depth = texture(point_shadow_maps[face_index], shadow_coord.xy).r;
    float slope_bias = normal_bias * (1.0 - max(dot(normal_ws, point_dir), 0.0));
    return current_depth - (sample_bias + slope_bias) <= stored_depth ? 1.0 : 0.0;
}

vec4 sample_material_texture(uint texture_index, vec2 uv)
{
    return texture(material_textures[int(texture_index)], uv);
}

vec3 performance_heat_color(float heat)
{
    heat = clamp(heat, 0.0, 1.0);
    if (heat <= 0.5)
        return mix(vec3(0.18, 0.84, 0.24), vec3(0.98, 0.84, 0.18), heat * 2.0);
    return mix(vec3(0.98, 0.84, 0.18), vec3(0.92, 0.20, 0.16), (heat - 0.5) * 2.0);
}

float performance_heat_blend(float heat)
{
    heat = clamp(heat, 0.0, 1.0);
    if (heat <= 0.0)
        return 0.0;
    return clamp(0.20 + 0.80 * sqrt(heat), 0.0, 1.0);
}

float performance_heat_display_value(float heat, float log_scale)
{
    heat = clamp(heat, 0.0, 1.0);
    log_scale = max(log_scale, 0.0);
    if (log_scale <= 0.0)
        return heat;
    float denom = log2(1.0 + log_scale);
    if (denom <= 1e-6)
        return heat;
    return clamp(log2(1.0 + heat * log_scale) / denom, 0.0, 1.0);
}

void main()
{
    vec3 normal_ws = normalize(in_normal_ws);
    vec3 albedo = in_base_color;
    float roughness = 0.65;
    float metallic = 0.0;
    float material_ao = 1.0;
    float leaf_scattering = 0.0;
    MaterialInstance material = material_table.materials[min(in_material_index, 63u)];
    vec2 material_uv = in_material_uv * material.scalar_params.x;
    float normal_strength = max(material.scalar_params.y, 0.0);
    float ao_strength = clamp(material.scalar_params.z, 0.0, 1.0);
    metallic = material.scalar_params.w;
    if (material.metadata.x == kShadingFlatColor)
        roughness = clamp(material.scalar_params.x, 0.04, 1.0);

    if (material.metadata.x == kShadingTexturedTintedPbr)
    {
        vec3 tangent_normal = sample_material_texture(material.texture_indices.y, material_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);
        mat3 tbn = tangent_basis(normal_ws, in_tangent_ws);

        normal_ws = normalize(tbn * tangent_normal);
        albedo = sample_material_texture(material.texture_indices.x, material_uv).rgb * in_base_color;
        roughness = clamp(sample_material_texture(material.texture_indices.z, material_uv).r, 0.04, 1.0);
        material_ao = mix(1.0, sample_material_texture(material.texture_indices.w, material_uv).r, ao_strength);
    }
    else if (material.metadata.x == kShadingVertexTintPbr)
    {
        mat3 tbn = tangent_basis(normal_ws, in_tangent_ws);
        vec3 tangent_normal = sample_material_texture(material.texture_indices.y, material_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);

        normal_ws = normalize(tbn * tangent_normal);
        albedo = sample_material_texture(material.texture_indices.x, material_uv).rgb * in_base_color;
        roughness = clamp(sample_material_texture(material.texture_indices.z, material_uv).r, 0.04, 1.0);
        material_ao = mix(1.0, sample_material_texture(material.texture_indices.w, material_uv).r, ao_strength);
        metallic = clamp(material.scalar_params.w * sample_material_texture(material.metadata.y, material_uv).r, 0.0, 1.0);
    }
    else if (material.metadata.x == kShadingLeafCutoutPbr)
    {
        float opacity = sample_material_texture(material.texture_indices.w, material_uv).r;
        if (opacity < kLeafAlphaCutoff)
            discard;

        vec3 tangent_normal = sample_material_texture(material.texture_indices.y, material_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= normal_strength;
        tangent_normal = normalize(tangent_normal);
        mat3 tbn = tangent_basis(normal_ws, in_tangent_ws);

        normal_ws = normalize(tbn * tangent_normal);
        albedo = sample_material_texture(material.texture_indices.x, material_uv).rgb * in_base_color;
        roughness = clamp(sample_material_texture(material.texture_indices.z, material_uv).r, 0.04, 1.0);
        leaf_scattering = sample_material_texture(material.metadata.y, material_uv).r * ao_strength;
    }

    if (frame.label_fade_px.z > 0.5 && in_label_metrics.w > 0.5)
    {
        uint heat_offset = uint(max(in_label_metrics.z + 0.5, 0.0));
        uint heat_count = uint(max(in_label_metrics.w + 0.5, 0.0));
        uint layer_index = min(uint(max(in_layer_id + 0.5, 0.0)), heat_count - 1u);
        float heat = performance_heat.heat_values[heat_offset + layer_index];
        bool lcov_mode = frame.perf_tuning.y > 0.5;
        if (lcov_mode)
        {
            float brightness = dot(albedo, vec3(0.299, 0.587, 0.114));
            vec3 uncovered_color = vec3(0.25, 0.45, 0.85);
            vec3 covered_color = vec3(0.95, 0.85, 0.25);
            albedo = mix(uncovered_color, covered_color, heat) * (0.6 + 0.4 * brightness);
        }
        else
        {
            float display_heat = performance_heat_display_value(heat, frame.perf_tuning.x);
            float heat_blend = clamp(frame.label_fade_px.w, 0.0, 1.0) * performance_heat_blend(display_heat);
            albedo = mix(albedo, performance_heat_color(display_heat), heat_blend);
        }
    }

    vec2 screen_uv = clamp(
        (gl_FragCoord.xy - frame.screen_params.xy) * frame.screen_params.zw,
        vec2(0.0),
        vec2(1.0));
    float ao = clamp(texture(ao_buffer, screen_uv).r, 0.0, 1.0);
    vec3 view_dir = normalize(frame.camera_pos.xyz - in_world_position);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    float ambient = max(frame.render_tuning.z, 0.0);
    float hemi_factor = normal_ws.y * 0.5 + 0.5;
    vec3 hemi = mix(vec3(0.84, 0.82, 0.78), vec3(1.04, 1.03, 1.01), hemi_factor);
    float combined_ao = material_ao * ao;
    vec3 ambient_lighting = hemi * ambient * combined_ao;

    vec3 direct_lighting = vec3(0.0);

    vec3 dir_light = normalize(-frame.light_dir.xyz);
    float ndotl = max(dot(normal_ws, dir_light), 0.0);
    if (ndotl > 0.0)
    {
        float shadow_visibility = directional_shadow_visibility(
            in_world_position,
            normal_ws,
            ndotl,
            gl_FragCoord.z);
        vec3 half_vec = normalize(view_dir + dir_light);
        vec3 fresnel = fresnel_schlick(max(dot(half_vec, view_dir), 0.0), f0);
        float ndf = distribution_ggx(normal_ws, half_vec, roughness);
        float geometry = geometry_smith(normal_ws, view_dir, dir_light, roughness);
        vec3 numerator = ndf * geometry * fresnel;
        float denom = max(4.0 * max(dot(normal_ws, view_dir), 0.0) * ndotl, 1e-4);
        vec3 specular = numerator / denom;
        vec3 kd = (1.0 - fresnel) * (1.0 - metallic);
        vec3 radiance = hemi * 0.52;
        direct_lighting += (kd * albedo / kPi + specular) * radiance * ndotl * shadow_visibility;
        if (leaf_scattering > 0.0)
        {
            float transmitted = max(dot(-normal_ws, dir_light), 0.0);
            direct_lighting += albedo * radiance * (leaf_scattering * 0.45) * transmitted * shadow_visibility;
        }
    }

    vec3 point_vec = frame.point_light_pos.xyz - in_world_position;
    float point_dist = length(point_vec);
    vec3 point_dir = point_dist > 1e-4 ? point_vec / point_dist : vec3(0.0, 1.0, 0.0);
    float point_radius = max(frame.point_light_pos.w, 1.0);
    float point_atten = clamp(1.0 - point_dist / point_radius, 0.0, 1.0);
    float point_ndotl = max(dot(normal_ws, point_dir), 0.0);
    if (point_ndotl > 0.0 && point_atten > 0.0)
    {
        float point_shadow = point_shadow_visibility(in_world_position, normal_ws, point_dir);
        vec3 half_vec = normalize(view_dir + point_dir);
        vec3 fresnel = fresnel_schlick(max(dot(half_vec, view_dir), 0.0), f0);
        float ndf = distribution_ggx(normal_ws, half_vec, roughness);
        float geometry = geometry_smith(normal_ws, view_dir, point_dir, roughness);
        vec3 numerator = ndf * geometry * fresnel;
        float denom = max(4.0 * max(dot(normal_ws, view_dir), 0.0) * point_ndotl, 1e-4);
        vec3 specular = numerator / denom;
        vec3 kd = (1.0 - fresnel) * (1.0 - metallic);
        vec3 radiance = vec3(1.05, 0.98, 0.90)
            * max(frame.render_tuning.y, 0.0) * point_atten * point_atten;
        direct_lighting += (kd * albedo / kPi + specular) * radiance * point_ndotl * point_shadow;
        if (leaf_scattering > 0.0)
        {
            float transmitted = max(dot(-normal_ws, point_dir), 0.0);
            direct_lighting += albedo * radiance * (leaf_scattering * 0.60) * transmitted * point_shadow;
        }
    }

    vec3 shaded = albedo * ambient_lighting + direct_lighting;
    if (in_tex_blend > 0.5)
    {
        vec4 label = texture(sign_atlas, in_atlas_uv);
        vec2 atlas_texels_per_pixel = max(
            fwidth(in_atlas_uv) * vec2(textureSize(sign_atlas, 0)),
            vec2(1e-5));
        vec2 projected_ink_pixels = in_label_metrics.xy / atlas_texels_per_pixel;
        float projected_text_pixels = min(projected_ink_pixels.x, projected_ink_pixels.y);
        float fade_start_px = max(min(frame.label_fade_px.x, frame.label_fade_px.y), 0.0);
        float fade_end_px = max(max(frame.label_fade_px.x, frame.label_fade_px.y), fade_start_px + 1e-3);
        float visibility = smoothstep(fade_start_px, fade_end_px, projected_text_pixels);
        float label_alpha = smoothstep(0.18, 0.55, label.a) * visibility;
        shaded = mix(shaded, label.rgb, label_alpha);
    }

    out_frag_color = vec4(max(shaded, vec3(0.0)), in_opacity);
}
