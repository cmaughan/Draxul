#version 450

layout(push_constant) uniform SceneState
{
    float time;
    float aspect;
    int paused;
} scene;

layout(location = 0) in vec2 local_uv;
layout(location = 0) out vec4 out_color;

float hash21(vec2 p)
{
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

void main()
{
    vec2 p = local_uv * 2.0 - 1.0;
    p.x *= scene.aspect;
    vec3 color = vec3(0.008, 0.014, 0.035);

    vec2 star_cell = floor((p + vec2(scene.time * 0.002, 0.0)) * 95.0);
    float star = step(0.993, hash21(star_cell));
    color += star * vec3(0.7, 0.82, 1.0);

    float radius = length(p);
    float earth = 1.0 - smoothstep(0.405, 0.415, radius);
    vec3 normal = vec3(p / 0.415,
        sqrt(max(0.0, 1.0 - dot(p / 0.415, p / 0.415))));
    vec3 light_dir = normalize(vec3(0.75, -0.3, 0.8));
    float lighting = 0.16 + 0.84 * max(0.0, dot(normal, light_dir));
    float bands = 0.5 + 0.5 * sin(18.0 * p.y + 2.0 * sin(9.0 * p.x));
    vec3 ocean = mix(vec3(0.015, 0.08, 0.24), vec3(0.03, 0.28, 0.52), lighting);
    vec3 land = vec3(0.08, 0.34, 0.14) * (0.55 + lighting);
    color = mix(color, mix(ocean, land, smoothstep(0.70, 0.86, bands)), earth);

    for (int i = 0; i < 4; ++i)
    {
        float orbit_radius = 0.56 + float(i) * 0.105;
        float ring = 1.0 - smoothstep(0.002, 0.008,
            abs(length(vec2(p.x, p.y * (1.35 + float(i) * 0.09))) - orbit_radius));
        color += ring * vec3(0.06, 0.18, 0.32);
        float angle = scene.time * (0.42 + float(i) * 0.11) + float(i) * 1.7;
        vec2 satellite = vec2(cos(angle) * orbit_radius,
            sin(angle) * orbit_radius / (1.35 + float(i) * 0.09));
        float marker = 1.0 - smoothstep(0.010, 0.022, length(p - satellite));
        color += marker * mix(vec3(0.3, 0.7, 1.0), vec3(1.0, 0.5, 0.15), float(i) / 3.0);
    }

    float atmosphere = 1.0 - smoothstep(0.414, 0.455, radius);
    atmosphere *= smoothstep(0.405, 0.423, radius);
    color += atmosphere * vec3(0.08, 0.35, 0.8);
    out_color = vec4(color, 1.0);
}
