#version 450

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 proj;
    vec4 light_dir;
    vec4 point_light_pos;
    vec4 label_fade_px;
    vec4 render_tuning;
} frame;
layout(set = 0, binding = 1) uniform sampler2D sign_atlas;

layout(location = 0) in vec3 in_normal_ws;
layout(location = 1) in vec3 in_base_color;
layout(location = 2) in vec3 in_world_position;
layout(location = 3) in vec2 in_atlas_uv;
layout(location = 4) in float in_tex_blend;
layout(location = 5) in vec2 in_label_ink_pixel_size;
layout(location = 0) out vec4 out_frag_color;

void main()
{
    vec3 normal_ws = normalize(in_normal_ws);
    vec3 light_dir = normalize(-frame.light_dir.xyz);
    float ndotl = max(dot(normal_ws, light_dir), 0.0);
    float ambient = max(frame.render_tuning.z, 0.0);
    float directional = 0.52 * ndotl;
    float hemi_factor = normal_ws.y * 0.5 + 0.5;
    vec3 hemi = mix(vec3(0.84, 0.82, 0.78), vec3(1.04, 1.03, 1.01), hemi_factor);
    vec3 point_vec = frame.point_light_pos.xyz - in_world_position;
    float point_dist = length(point_vec);
    vec3 point_dir = point_dist > 1e-4 ? point_vec / point_dist : vec3(0.0, 1.0, 0.0);
    float point_ndotl = max(dot(normal_ws, point_dir), 0.0);
    float point_radius = max(frame.point_light_pos.w, 1.0);
    float point_atten = clamp(1.0 - point_dist / point_radius, 0.0, 1.0);
    float point_light = max(frame.render_tuning.y, 0.0) * point_ndotl * point_atten * point_atten;
    vec3 point_color = vec3(1.05, 0.98, 0.90);

    vec3 shaded = in_base_color * (hemi * (ambient + directional) + point_color * point_light);
    if (in_tex_blend > 0.5)
    {
        vec4 label = texture(sign_atlas, in_atlas_uv);
        vec2 atlas_texels_per_pixel = max(
            fwidth(in_atlas_uv) * vec2(textureSize(sign_atlas, 0)),
            vec2(1e-5));
        vec2 projected_ink_pixels = in_label_ink_pixel_size / atlas_texels_per_pixel;
        float projected_text_pixels = min(projected_ink_pixels.x, projected_ink_pixels.y);
        float fade_start_px = max(min(frame.label_fade_px.x, frame.label_fade_px.y), 0.0);
        float fade_end_px = max(max(frame.label_fade_px.x, frame.label_fade_px.y), fade_start_px + 1e-3);
        float visibility = smoothstep(fade_start_px, fade_end_px, projected_text_pixels);
        float label_alpha = smoothstep(0.18, 0.55, label.a) * visibility;
        shaded = mix(shaded, vec3(0.0), label_alpha);
    }

    float output_gamma = max(frame.render_tuning.x, 1.0);
    vec3 encoded = pow(max(shaded, vec3(0.0)), vec3(1.0 / output_gamma));
    out_frag_color = vec4(encoded, 1.0);
}
