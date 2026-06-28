#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) in vec3 in_world;
layout(location = 0) out vec4 out_color;

const float PI = 3.14159265358979323846;
const float PLANET_RADIUS = 1.0;
const float ATMOSPHERE_RADIUS = 1.02;
const float RAYLEIGH_SCALE_HEIGHT = 0.00125;
const float MIE_SCALE_HEIGHT = 0.00019;
const vec3 RAYLEIGH_BETA = vec3(36.0, 84.0, 205.0);
const vec3 MIE_BETA = vec3(22.0, 20.0, 18.0);
const float MIE_G = 0.76;
const int VIEW_STEPS = 12;
const int LIGHT_STEPS = 6;

vec2 ray_sphere(vec3 origin, vec3 direction, float radius)
{
    float b = dot(origin, direction);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0)
        return vec2(-1.0);
    float root = sqrt(discriminant);
    return vec2(-b - root, -b + root);
}

vec2 atmosphere_density(vec3 position)
{
    float altitude = max(length(position) - PLANET_RADIUS, 0.0);
    return vec2(
        exp(-altitude / RAYLEIGH_SCALE_HEIGHT),
        exp(-altitude / MIE_SCALE_HEIGHT));
}

vec2 optical_depth_to_sun(vec3 origin, vec3 sun_direction)
{
    vec2 planet_hit = ray_sphere(origin, sun_direction, PLANET_RADIUS);
    if (planet_hit.x > 0.00001)
        return vec2(-1.0);

    vec2 atmosphere_hit = ray_sphere(origin, sun_direction, ATMOSPHERE_RADIUS);
    if (atmosphere_hit.y <= 0.0)
        return vec2(-1.0);

    vec2 optical_depth = vec2(0.0);
    for (int step_index = 0; step_index < LIGHT_STEPS; ++step_index)
    {
        float u0 = float(step_index) / float(LIGHT_STEPS);
        float u1 = float(step_index + 1) / float(LIGHT_STEPS);
        float distance0 = atmosphere_hit.y * u0 * u0;
        float distance1 = atmosphere_hit.y * u1 * u1;
        float step_length = distance1 - distance0;
        float distance = 0.5 * (distance0 + distance1);
        optical_depth += atmosphere_density(origin + sun_direction * distance) * step_length;
    }
    return optical_depth;
}

void main()
{
    vec3 ray_origin = push.camera_pos.xyz;
    vec3 ray_direction = normalize(in_world - ray_origin);
    vec3 sun_direction = normalize(push.sun_dir_time.xyz);

    vec2 atmosphere_hit = ray_sphere(ray_origin, ray_direction, ATMOSPHERE_RADIUS);
    if (atmosphere_hit.y <= 0.0)
        discard;

    float ray_start = max(atmosphere_hit.x, 0.0);
    float ray_end = atmosphere_hit.y;
    float fragment_distance = length(in_world - ray_origin);
    if (fragment_distance > 0.5 * (ray_start + ray_end))
        discard;

    vec2 planet_hit = ray_sphere(ray_origin, ray_direction, PLANET_RADIUS);
    bool hits_planet = planet_hit.x > ray_start && planet_hit.x < ray_end;
    if (hits_planet)
        ray_end = planet_hit.x;
    if (ray_end <= ray_start)
        discard;

    float segment_length = ray_end - ray_start;
    vec2 view_optical_depth = vec2(0.0);
    vec3 rayleigh_sum = vec3(0.0);
    vec3 mie_sum = vec3(0.0);
    for (int step_index = 0; step_index < VIEW_STEPS; ++step_index)
    {
        float u0 = float(step_index) / float(VIEW_STEPS);
        float u1 = float(step_index + 1) / float(VIEW_STEPS);
        float distance0 = segment_length * (hits_planet ? 1.0 - (1.0 - u0) * (1.0 - u0) : u0);
        float distance1 = segment_length * (hits_planet ? 1.0 - (1.0 - u1) * (1.0 - u1) : u1);
        float step_length = distance1 - distance0;
        float distance = ray_start + 0.5 * (distance0 + distance1);
        vec3 sample_position = ray_origin + ray_direction * distance;
        vec2 density = atmosphere_density(sample_position);
        vec2 light_optical_depth = optical_depth_to_sun(sample_position, sun_direction);
        if (light_optical_depth.x >= 0.0)
        {
            vec2 total_optical_depth = view_optical_depth + light_optical_depth;
            vec3 transmittance = exp(-(
                RAYLEIGH_BETA * total_optical_depth.x
                + MIE_BETA * total_optical_depth.y));
            rayleigh_sum += density.x * transmittance * step_length;
            mie_sum += density.y * transmittance * step_length;
        }
        view_optical_depth += density * step_length;
    }

    float cosine = dot(-ray_direction, sun_direction);
    float rayleigh_phase = 3.0 * (1.0 + cosine * cosine) / (16.0 * PI);
    float mie_denominator = pow(1.0 + MIE_G * MIE_G - 2.0 * MIE_G * cosine, 1.5);
    float mie_phase = (1.0 - MIE_G * MIE_G) / (4.0 * PI * mie_denominator);
    vec3 scattering = (
        rayleigh_sum * RAYLEIGH_BETA * rayleigh_phase
        + mie_sum * MIE_BETA * mie_phase)
        * 6.0;
    float extinction = dot(
        RAYLEIGH_BETA * view_optical_depth.x + MIE_BETA * view_optical_depth.y,
        vec3(0.2126, 0.7152, 0.0722));
    float alpha = clamp(1.0 - exp(-extinction), 0.0, 0.92);
    scattering = min(scattering, vec3(alpha));
    out_color = vec4(scattering, alpha);
}
