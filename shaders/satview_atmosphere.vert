#version 450

layout(push_constant) uniform SatViewFrame
{
    mat4 view_proj;
    vec4 camera_pos;
    vec4 camera_orientation;
    vec4 sun_dir_time;
    vec4 render_params;
} push;

layout(location = 0) out vec3 out_world;

const float PI = 3.14159265358979323846;
const float ATMOSPHERE_RADIUS = 1.02;

vec2 quad_corner(int vertex)
{
    if (vertex == 0)
        return vec2(0.0, 0.0);
    if (vertex == 1)
        return vec2(1.0, 1.0);
    if (vertex == 2)
        return vec2(1.0, 0.0);
    if (vertex == 3)
        return vec2(0.0, 0.0);
    if (vertex == 4)
        return vec2(0.0, 1.0);
    return vec2(1.0, 1.0);
}

void main()
{
    int lat_bands = max(1, int(push.render_params.x + 0.5));
    int lon_bands = max(1, int(push.render_params.y + 0.5));
    int tri_vertex = gl_VertexIndex % 6;
    int quad = gl_VertexIndex / 6;
    int lon = quad % lon_bands;
    int lat = quad / lon_bands;

    vec2 corner = quad_corner(tri_vertex);
    float u = (float(lon) + corner.x) / float(lon_bands);
    float v = (float(lat) + corner.y) / float(lat_bands);
    float theta = u * 2.0 * PI + push.render_params.z;
    float phi = mix(-0.5 * PI, 0.5 * PI, v);
    float cp = cos(phi);
    vec3 world = ATMOSPHERE_RADIUS * vec3(cp * sin(theta), sin(phi), cp * cos(theta));

    out_world = world;
    gl_Position = push.view_proj * vec4(world, 1.0);
}
