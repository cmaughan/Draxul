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

layout(set = 0, binding = 4) uniform sampler2D moon_tex;

const float PI = 3.14159265358979323846;
const vec3 LUNAR_NORTH_POLE_RENDER = vec3(
    0.39812155,
    0.91733267,
    0.00003544);

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
    bool map_projection = push.camera_pos.w < 0.0;
    vec2 uv = vec2(fract(in_uv.x), 1.0 - clamp(in_uv.y, 0.0, 1.0));
    vec3 normal = normalize(in_normal);
    vec3 moon_position = map_projection
        ? push.camera_pos.xyz
        : push.camera_orientation.xyz;
    if (map_projection)
    {
        float map_longitude = (in_uv.x - 0.5) * 2.0 * PI;
        float map_latitude = mix(-0.5 * PI, 0.5 * PI, in_uv.y);
        float cp = cos(map_latitude);
        vec3 map_local = vec3(
            cp * cos(map_longitude),
            cp * sin(map_longitude),
            sin(map_latitude));
        vec3 body = map_local_to_body(map_local, push.camera_orientation.xy);
        vec3 far_axis = normalize(moon_position);
        vec3 north_axis = normalize(
            LUNAR_NORTH_POLE_RENDER
            - far_axis * dot(LUNAR_NORTH_POLE_RENDER, far_axis));
        vec3 local_x_axis = normalize(cross(north_axis, far_axis));
        normal = normalize(
            -far_axis * body.x
            - local_x_axis * body.y
            + north_axis * body.z);
        float longitude = atan(body.y, body.x);
        float latitude = asin(clamp(body.z, -1.0, 1.0));
        uv = vec2(
            fract(longitude / (2.0 * PI) + 0.5),
            0.5 - latitude / PI);
    }
    vec3 surface = texture(moon_tex, uv).rgb;
    vec3 sunlight_direction = normalize(push.sun_dir_time.xyz);
    vec3 earth_direction = -normalize(moon_position);
    float diffuse = max(dot(normal, sunlight_direction), 0.0);
    float earth_phase = 0.5 * (1.0 - dot(earth_direction, sunlight_direction));
    float earthshine = max(dot(normal, earth_direction), 0.0) * earth_phase * 0.055;
    float illumination = 0.006 + diffuse * 1.12 + earthshine;
    out_color = vec4(surface * illumination, 1.0);
}
