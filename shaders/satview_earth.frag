#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_world;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

float hash_wave(vec3 p)
{
    return sin(p.x * 2.0 + sin(p.z * 1.8))
        + 0.7 * sin(p.y * 3.2 + p.x * 1.1)
        + 0.45 * sin((p.x + p.z) * 3.4 + p.y * 1.5);
}

void main()
{
    vec3 n = normalize(in_normal);
    vec3 light = normalize(push.sun_dir_time.xyz);
    vec3 view = normalize(push.camera_pos.xyz - in_world);

    float latitude = abs(n.y);
    float continent_noise = hash_wave(n);
    float land_mask = smoothstep(0.20, 0.60, continent_noise * 0.25 + 0.48 - latitude * 0.08);
    float ice_mask = smoothstep(0.74, 0.92, latitude);

    vec3 ocean = mix(vec3(0.015, 0.075, 0.17), vec3(0.01, 0.20, 0.34), 1.0 - latitude);
    vec3 land = mix(vec3(0.13, 0.32, 0.15), vec3(0.55, 0.47, 0.25), smoothstep(0.25, 0.65, latitude));
    vec3 surface = mix(ocean, land, land_mask);
    surface = mix(surface, vec3(0.84, 0.90, 0.88), ice_mask);

    float ndl = dot(n, light);
    float day = smoothstep(-0.10, 0.12, ndl);
    float diffuse = max(ndl, 0.0);
    vec3 lit = surface * (0.34 + diffuse * 0.90);
    vec3 night = vec3(0.006, 0.013, 0.030);

    float city_mask = land_mask
        * smoothstep(-0.40, 0.02, n.y)
        * smoothstep(-0.35, 0.05, -ndl)
        * smoothstep(0.45, 0.85, sin(in_uv.x * 18.0) * sin(in_uv.y * 13.0));
    night += vec3(1.0, 0.62, 0.24) * city_mask * 0.08;

    float rim = pow(1.0 - max(dot(view, n), 0.0), 3.0);
    vec3 atmosphere = vec3(0.12, 0.36, 0.66) * rim * (0.45 + day * 0.65);
    vec3 color = mix(night, lit, day) + atmosphere;
    out_color = vec4(color, 1.0);
}
