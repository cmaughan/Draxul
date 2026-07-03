#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec3 in_world;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 5) uniform sampler2D sun_tex;

const float PI = 3.14159265358979323846;

vec3 map_local_to_body(vec3 local, vec2 center)
{
    float cos_longitude = cos(center.x);
    float sin_longitude = sin(center.x);
    float cos_latitude = cos(center.y);
    float sin_latitude = sin(center.y);
    vec3 center_axis = vec3(
        cos_latitude * cos_longitude,
        cos_latitude * sin_longitude,
        sin_latitude);
    vec3 east_axis = vec3(-sin_longitude, cos_longitude, 0.0);
    vec3 north_axis = vec3(
        -sin_latitude * cos_longitude,
        -sin_latitude * sin_longitude,
        cos_latitude);
    return local.x * center_axis + local.y * east_axis + local.z * north_axis;
}

void main()
{
    const bool map_projection = push.camera_pos.w < 0.0;
    vec2 uv = vec2(fract(in_uv.x), 1.0 - clamp(in_uv.y, 0.0, 1.0));
    if (map_projection)
    {
        float map_longitude = (in_uv.x - 0.5) * 2.0 * PI;
        float map_latitude = mix(-0.5 * PI, 0.5 * PI, in_uv.y);
        float cp = cos(map_latitude);
        vec3 body = map_local_to_body(
            vec3(cp * cos(map_longitude), cp * sin(map_longitude), sin(map_latitude)),
            push.camera_orientation.xy);
        float longitude = atan(body.y, body.x);
        float latitude = asin(clamp(body.z, -1.0, 1.0));
        uv = vec2(
            fract(longitude / (2.0 * PI) + 0.5),
            0.5 - latitude / PI);
    }

    vec3 surface = texture(sun_tex, uv).rgb;
    vec3 emission = surface * vec3(1.16, 0.94, 0.72)
        * (map_projection ? 0.65 : 2.15);
    if (!map_projection)
    {
        vec3 view_direction = normalize(push.camera_pos.xyz - in_world);
        float mu = max(dot(normalize(in_normal), view_direction), 0.0);
        float limb_darkening = mix(0.48, 1.0, pow(mu, 0.42));
        float limb_glow = pow(1.0 - mu, 3.0);
        emission = emission * limb_darkening
            + vec3(1.25, 0.30, 0.035) * limb_glow * 0.55;
    }
    out_color = vec4(emission, 1.0);
}
