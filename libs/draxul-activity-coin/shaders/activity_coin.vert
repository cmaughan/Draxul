#version 450

// Procedural port of TokenFu's coin mesh (tokenfu src/coin_mesh.h and
// shaders/coin.vert). Each of the 32 segments emits 12 vertices: a front-cap
// triangle, a back-cap triangle, and two rim triangles. Keep this in sync with
// the Metal source in activity_coin_pass_metal.mm.

layout(push_constant) uniform CoinUniforms {
    vec4 viewport_radius; // {viewport width, viewport height, radius, brightness}
    vec4 coin;            // {x, y, angle, style}
} u;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec2 uv;
layout(location = 2) out float face;
layout(location = 3) flat out int style;
layout(location = 4) out float brightness;

const int kSegments = 32;
const float kHalfThickness = 0.14;
const float kTau = 6.28318530718;

vec3 rotate_y(vec3 v, float c, float s)
{
    return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

void main()
{
    int segment = gl_VertexIndex / 12;
    int corner = gl_VertexIndex % 12;
    float a0 = kTau * float(segment) / float(kSegments);
    float a1 = kTau * float(segment + 1) / float(kSegments);
    vec2 p0 = vec2(cos(a0), sin(a0));
    vec2 p1 = vec2(cos(a1), sin(a1));

    vec3 position;
    vec3 n;
    vec3 facet;
    if (corner < 3)
    {
        vec2 p = corner == 0 ? vec2(0.0) : corner == 1 ? p0 : p1;
        position = vec3(p, kHalfThickness);
        n = vec3(0.0, 0.0, 1.0);
        facet = n;
        uv = 0.5 + p * 0.5;
        face = 1.0;
    }
    else if (corner < 6)
    {
        vec2 p = corner == 3 ? vec2(0.0) : corner == 4 ? p1 : p0;
        position = vec3(p, -kHalfThickness);
        n = vec3(0.0, 0.0, -1.0);
        facet = n;
        uv = 0.5 + p * 0.5;
        face = -1.0;
    }
    else
    {
        // Rim quad as front0, back0, back1, front0, back1, front1.
        int r = corner - 6;
        bool second = r == 2 || r == 4 || r == 5;
        bool front = r == 0 || r == 3 || r == 5;
        vec2 p = second ? p1 : p0;
        position = vec3(p, front ? kHalfThickness : -kHalfThickness);
        n = vec3(p, 0.0);
        float mid = 0.5 * (a0 + a1);
        facet = vec3(cos(mid), sin(mid), 0.0);
        uv = vec2(float(second ? segment + 1 : segment) / float(kSegments),
            front ? 1.0 : 0.0);
        face = 0.0;
    }

    float c = cos(u.coin.z);
    float s = sin(u.coin.z);
    position = rotate_y(position, c, s);
    normal = normalize(rotate_y(n, c, s));
    style = int(u.coin.w);
    brightness = u.viewport_radius.w;

    // There is no depth buffer. The coin is convex, so discarding every facet
    // that faces away from the viewer (+z) leaves exactly the visible surface.
    // All three vertices of a triangle share the facet, so the whole triangle
    // collapses to a point outside the clip volume.
    if (rotate_y(facet, c, s).z <= 0.0)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    vec2 pixel = u.coin.xy + position.xy * u.viewport_radius.z;
    gl_Position = vec4(
        2.0 * pixel.x / u.viewport_radius.x - 1.0,
        2.0 * pixel.y / u.viewport_radius.y - 1.0,
        0.5,
        1.0);
}
