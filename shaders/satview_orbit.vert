#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) out vec4 out_color;

const float PI = 3.14159265358979323846;

vec3 palette(float t)
{
    return 0.58 + 0.42 * cos(6.28318 * (vec3(0.05, 0.38, 0.67) + t));
}

void main()
{
    int segment_count = 192;
    int pair = gl_VertexIndex / 2;
    int endpoint = gl_VertexIndex - pair * 2;
    int segment = pair % segment_count;
    int track = pair / segment_count;

    float track_f = float(track);
    float family = mod(track_f, 3.0);
    float radius = family < 0.5 ? 1.28 : (family < 1.5 ? 1.58 : 2.05);
    radius += 0.035 * sin(track_f * 2.37);

    float inclination = mix(0.10, 1.45, fract(track_f * 0.31831));
    if (family > 1.5)
        inclination *= 0.12;
    float raan = track_f * 0.61803398875 * 2.0 * PI + push.sun_dir_time.w * 0.000006;
    float anomaly = (float(segment + endpoint) / float(segment_count)) * 2.0 * PI;
    anomaly += push.sun_dir_time.w * (family < 0.5 ? 0.00023 : (family < 1.5 ? 0.00009 : 0.00002));

    vec3 local = vec3(cos(anomaly) * radius, 0.0, sin(anomaly) * radius);
    float ci = cos(inclination);
    float si = sin(inclination);
    vec3 inclined = vec3(local.x, -local.z * si, local.z * ci);
    float cr = cos(raan);
    float sr = sin(raan);
    vec3 world = vec3(
        inclined.x * cr + inclined.z * sr,
        inclined.y,
        -inclined.x * sr + inclined.z * cr);

    vec3 color = mix(palette(fract(track_f * 0.173)), vec3(1.0), 0.20);
    vec3 view = normalize(push.camera_pos.xyz - world);
    float frontness = max(dot(normalize(world), view), 0.0);
    float face_fade = mix(1.0, 0.68, smoothstep(0.05, 0.70, frontness));
    float alpha = (family < 0.5 ? 0.78 : (family < 1.5 ? 0.64 : 0.86)) * face_fade;
    out_color = vec4(color, alpha);
    gl_Position = push.view_proj * vec4(world, 1.0);
}
