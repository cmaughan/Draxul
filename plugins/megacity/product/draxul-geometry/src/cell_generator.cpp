#include <draxul/cell_generator.h>

#include <draxul/perf_timing.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>
#include <limits>

namespace draxul
{

namespace
{

// --- Hashing / value noise ---------------------------------------------------

uint32_t hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float lattice_value(int ix, int iy, int iz, uint32_t seed)
{
    uint32_t h = seed * 0x9e3779b1u;
    h = hash_u32(h ^ static_cast<uint32_t>(ix * 73856093));
    h = hash_u32(h ^ static_cast<uint32_t>(iy * 19349663));
    h = hash_u32(h ^ static_cast<uint32_t>(iz * 83492791));
    return static_cast<float>(h) / 2147483647.5f - 1.0f; // [-1, 1)
}

float smootherstep(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

uint16_t clamp_index(size_t index)
{
    return static_cast<uint16_t>(std::min(index, static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
}

void push_triangle(GeometryMesh& mesh, size_t a, size_t b, size_t c)
{
    mesh.indices.push_back(clamp_index(a));
    mesh.indices.push_back(clamp_index(b));
    mesh.indices.push_back(clamp_index(c));
}

GeometryVertex make_vertex(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& color)
{
    GeometryVertex vertex;
    vertex.position = position;
    vertex.normal = normal;
    vertex.color = color;
    vertex.uv = glm::vec2(0.0f);
    vertex.tex_blend = 0.0f;
    vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    vertex.layer_id = 0.0f;
    return vertex;
}

glm::vec3 rotate_around_axis(const glm::vec3& v, const glm::vec3& axis, float sin_a, float cos_a)
{
    // Rodrigues' rotation (axis assumed unit length).
    return v * cos_a + glm::cross(axis, v) * sin_a + axis * glm::dot(axis, v) * (1.0f - cos_a);
}

glm::vec3 any_perpendicular(const glm::vec3& t)
{
    const glm::vec3 up = std::abs(t.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::normalize(glm::cross(up, t));
}

} // namespace

// --- Public noise ------------------------------------------------------------

float value_noise_3d(const glm::vec3& p, uint32_t seed)
{
    const glm::vec3 base = glm::floor(p);
    const glm::vec3 frac = p - base;
    const int ix = static_cast<int>(base.x);
    const int iy = static_cast<int>(base.y);
    const int iz = static_cast<int>(base.z);

    const float fx = smootherstep(frac.x);
    const float fy = smootherstep(frac.y);
    const float fz = smootherstep(frac.z);

    auto corner = [&](int dx, int dy, int dz) {
        return lattice_value(ix + dx, iy + dy, iz + dz, seed);
    };

    const float x00 = glm::mix(corner(0, 0, 0), corner(1, 0, 0), fx);
    const float x10 = glm::mix(corner(0, 1, 0), corner(1, 1, 0), fx);
    const float x01 = glm::mix(corner(0, 0, 1), corner(1, 0, 1), fx);
    const float x11 = glm::mix(corner(0, 1, 1), corner(1, 1, 1), fx);
    const float y0 = glm::mix(x00, x10, fy);
    const float y1 = glm::mix(x01, x11, fy);
    return glm::mix(y0, y1, fz);
}

float fbm_noise_3d(const glm::vec3& p, int octaves, uint32_t seed)
{
    octaves = std::clamp(octaves, 1, 8);
    float sum = 0.0f;
    float amplitude = 0.5f;
    float total = 0.0f;
    glm::vec3 sample = p;
    for (int octave = 0; octave < octaves; ++octave)
    {
        sum += amplitude * value_noise_3d(sample, seed + static_cast<uint32_t>(octave) * 1013u);
        total += amplitude;
        amplitude *= 0.5f;
        sample *= 2.0f;
    }
    return sum / std::max(total, 1e-5f);
}

// --- Mesh operations ---------------------------------------------------------

void append_mesh(GeometryMesh& dst, const GeometryMesh& src)
{
    const size_t base = dst.vertices.size();
    dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
    dst.indices.reserve(dst.indices.size() + src.indices.size());
    for (const uint16_t index : src.indices)
        dst.indices.push_back(clamp_index(base + index));
}

GeometryMesh transform_mesh(const GeometryMesh& src, const glm::mat4& transform)
{
    GeometryMesh out = src;
    const glm::mat3 normal_matrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    for (GeometryVertex& vertex : out.vertices)
    {
        const glm::vec4 world = transform * glm::vec4(vertex.position, 1.0f);
        vertex.position = glm::vec3(world);
        vertex.normal = glm::normalize(normal_matrix * vertex.normal);
    }
    return out;
}

void set_mesh_color(GeometryMesh& mesh, const glm::vec3& color)
{
    for (GeometryVertex& vertex : mesh.vertices)
        vertex.color = color;
}

void recompute_smooth_normals(GeometryMesh& mesh)
{
    for (GeometryVertex& vertex : mesh.vertices)
        vertex.normal = glm::vec3(0.0f);

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const uint16_t ia = mesh.indices[i];
        const uint16_t ib = mesh.indices[i + 1];
        const uint16_t ic = mesh.indices[i + 2];
        const glm::vec3& a = mesh.vertices[ia].position;
        const glm::vec3& b = mesh.vertices[ib].position;
        const glm::vec3& c = mesh.vertices[ic].position;
        const glm::vec3 face = glm::cross(b - a, c - a); // area-weighted
        mesh.vertices[ia].normal += face;
        mesh.vertices[ib].normal += face;
        mesh.vertices[ic].normal += face;
    }

    for (GeometryVertex& vertex : mesh.vertices)
    {
        const float length = glm::length(vertex.normal);
        vertex.normal = length > 1e-6f ? vertex.normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

// --- Blobby membranes --------------------------------------------------------

GeometryMesh build_blob_mesh(const BlobParams& params)
{
    PERF_MEASURE();
    const int lat = std::clamp(params.latitude_segments, 6, 128);
    const int lon = std::clamp(params.longitude_segments, 8, 256);
    const uint32_t seed = static_cast<uint32_t>(params.seed) ^ 0xB10B0001u;
    const glm::vec3 offset(
        static_cast<float>(hash_u32(seed) & 0xffff) * 0.001f,
        static_cast<float>(hash_u32(seed + 7u) & 0xffff) * 0.001f,
        static_cast<float>(hash_u32(seed + 13u) & 0xffff) * 0.001f);

    GeometryMesh mesh;

    auto displaced = [&](const glm::vec3& dir) {
        const float n = fbm_noise_3d(dir * params.noise_frequency + offset, params.noise_octaves, seed);
        const float scale = 1.0f + params.noise_amplitude * n;
        return glm::vec3(dir.x * params.radius.x, dir.y * params.radius.y, dir.z * params.radius.z) * scale;
    };
    auto vertex_color = [&](const glm::vec3& dir) {
        const float t = glm::clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
        glm::vec3 color = glm::mix(params.color_bottom, params.color_top, t);
        const float tint = fbm_noise_3d(dir * (params.noise_frequency * 1.7f) + offset * 3.0f, 3, seed + 91u);
        return glm::clamp(color + params.color_noise * tint, glm::vec3(0.0f), glm::vec3(1.0f));
    };

    // Top pole.
    const glm::vec3 top_dir(0.0f, 1.0f, 0.0f);
    mesh.vertices.push_back(make_vertex(displaced(top_dir), top_dir, vertex_color(top_dir)));

    const int ring_stride = lon + 1;
    auto ring_index = [&](int ring, int col) {
        return 1 + ring * ring_stride + col;
    };

    for (int ring = 0; ring < lat - 1; ++ring)
    {
        const float v = static_cast<float>(ring + 1) / static_cast<float>(lat);
        const float theta = glm::pi<float>() * v;
        const float cos_theta = std::cos(theta);
        const float sin_theta = std::sin(theta);
        for (int col = 0; col <= lon; ++col)
        {
            const float u = static_cast<float>(col) / static_cast<float>(lon);
            const float phi = glm::two_pi<float>() * u;
            const glm::vec3 dir(std::cos(phi) * sin_theta, cos_theta, std::sin(phi) * sin_theta);
            mesh.vertices.push_back(make_vertex(displaced(dir), dir, vertex_color(dir)));
        }
    }

    const glm::vec3 bottom_dir(0.0f, -1.0f, 0.0f);
    const size_t bottom_index = mesh.vertices.size();
    mesh.vertices.push_back(make_vertex(displaced(bottom_dir), bottom_dir, vertex_color(bottom_dir)));

    for (int col = 0; col < lon; ++col)
        push_triangle(mesh, 0, ring_index(0, col + 1), ring_index(0, col));

    for (int ring = 0; ring < lat - 2; ++ring)
    {
        for (int col = 0; col < lon; ++col)
        {
            const size_t a = ring_index(ring, col);
            const size_t b = ring_index(ring, col + 1);
            const size_t c = ring_index(ring + 1, col);
            const size_t d = ring_index(ring + 1, col + 1);
            push_triangle(mesh, a, b, c);
            push_triangle(mesh, c, b, d);
        }
    }

    const int last_ring = lat - 2;
    for (int col = 0; col < lon; ++col)
        push_triangle(mesh, bottom_index, ring_index(last_ring, col), ring_index(last_ring, col + 1));

    recompute_smooth_normals(mesh);
    return mesh;
}

// --- Swept tubes -------------------------------------------------------------

GeometryMesh build_tube(
    const std::vector<glm::vec3>& path,
    float radius,
    int sides,
    const std::function<glm::vec3(float)>& color_along)
{
    GeometryMesh mesh;
    if (path.size() < 2)
        return mesh;
    sides = std::clamp(sides, 3, 32);

    const size_t count = path.size();
    std::vector<glm::vec3> tangents(count);
    for (size_t i = 0; i < count; ++i)
    {
        glm::vec3 t;
        if (i == 0)
            t = path[1] - path[0];
        else if (i + 1 == count)
            t = path[count - 1] - path[count - 2];
        else
            t = path[i + 1] - path[i - 1];
        const float len = glm::length(t);
        tangents[i] = len > 1e-6f ? t / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    glm::vec3 normal = any_perpendicular(tangents[0]);
    const int ring_stride = sides + 1;

    for (size_t i = 0; i < count; ++i)
    {
        if (i > 0)
        {
            const glm::vec3 axis = glm::cross(tangents[i - 1], tangents[i]);
            const float sin_a = glm::length(axis);
            const float cos_a = glm::clamp(glm::dot(tangents[i - 1], tangents[i]), -1.0f, 1.0f);
            if (sin_a > 1e-5f)
                normal = glm::normalize(rotate_around_axis(normal, axis / sin_a, sin_a, cos_a));
            // Re-orthogonalize against the tangent to avoid drift.
            normal = glm::normalize(normal - tangents[i] * glm::dot(normal, tangents[i]));
        }
        const glm::vec3 binormal = glm::normalize(glm::cross(tangents[i], normal));
        const float t = static_cast<float>(i) / static_cast<float>(count - 1);
        const glm::vec3 color = color_along ? color_along(t) : glm::vec3(1.0f);
        for (int s = 0; s <= sides; ++s)
        {
            const float ang = glm::two_pi<float>() * static_cast<float>(s) / static_cast<float>(sides);
            const glm::vec3 dir = std::cos(ang) * normal + std::sin(ang) * binormal;
            mesh.vertices.push_back(make_vertex(path[i] + dir * radius, dir, color));
        }
    }

    for (size_t i = 0; i + 1 < count; ++i)
    {
        for (int s = 0; s < sides; ++s)
        {
            const size_t a = i * ring_stride + s;
            const size_t b = i * ring_stride + s + 1;
            const size_t c = (i + 1) * ring_stride + s;
            const size_t d = (i + 1) * ring_stride + s + 1;
            push_triangle(mesh, a, c, b);
            push_triangle(mesh, b, c, d);
        }
    }

    // End caps.
    auto add_cap = [&](size_t ring_start, const glm::vec3& center, const glm::vec3& cap_normal, const glm::vec3& color, bool flip) {
        const size_t center_index = mesh.vertices.size();
        mesh.vertices.push_back(make_vertex(center, cap_normal, color));
        for (int s = 0; s < sides; ++s)
        {
            const size_t a = ring_start + s;
            const size_t b = ring_start + s + 1;
            if (flip)
                push_triangle(mesh, center_index, b, a);
            else
                push_triangle(mesh, center_index, a, b);
        }
    };
    add_cap(0, path.front(), -tangents.front(), color_along ? color_along(0.0f) : glm::vec3(1.0f), false);
    add_cap((count - 1) * ring_stride, path.back(), tangents.back(), color_along ? color_along(1.0f) : glm::vec3(1.0f), true);

    return mesh;
}

// --- DNA double helix --------------------------------------------------------

GeometryMesh build_dna_double_helix(const DnaHelixParams& params)
{
    PERF_MEASURE();
    GeometryMesh mesh;
    const int segments = std::max(8, static_cast<int>(params.turns * static_cast<float>(params.segments_per_turn)));
    const float half = params.length * 0.5f;

    auto helix_point = [&](float t, float phase) {
        const float angle = t * params.turns * glm::two_pi<float>() + phase;
        const float y = -half + t * params.length;
        return glm::vec3(params.helix_radius * std::cos(angle), y, params.helix_radius * std::sin(angle));
    };

    for (int strand = 0; strand < 2; ++strand)
    {
        const float phase = strand == 0 ? 0.0f : glm::pi<float>();
        const glm::vec3 base_color = strand == 0 ? params.backbone_a_color : params.backbone_b_color;
        std::vector<glm::vec3> path;
        path.reserve(segments + 1);
        for (int i = 0; i <= segments; ++i)
            path.push_back(helix_point(static_cast<float>(i) / static_cast<float>(segments), phase));
        append_mesh(mesh, build_tube(path, params.strand_radius, params.tube_sides, [&](float t) { return base_color * (0.82f + 0.18f * std::cos(t * 6.0f)); }));
    }

    // Base-pair rungs, colored by the four canonical bases.
    const std::array<glm::vec3, 4> base_colors = { {
        glm::vec3(0.98f, 0.66f, 0.22f), // A
        glm::vec3(0.42f, 0.86f, 0.38f), // T
        glm::vec3(0.34f, 0.56f, 0.98f), // C
        glm::vec3(0.98f, 0.90f, 0.32f), // G
    } };
    const bool use_categories = !params.rung_categories.empty();
    const int rungs = use_categories
        ? static_cast<int>(params.rung_categories.size())
        : std::max(2, params.rungs);
    for (int r = 0; r < rungs; ++r)
    {
        const float t = rungs > 1 ? static_cast<float>(r) / static_cast<float>(rungs - 1) : 0.5f;
        const glm::vec3 p0 = helix_point(t, 0.0f);
        const glm::vec3 p1 = helix_point(t, glm::pi<float>());
        const glm::vec3 mid = (p0 + p1) * 0.5f;
        const size_t base_index = use_categories
            ? static_cast<size_t>(std::max(0, params.rung_categories[static_cast<size_t>(r)])) % base_colors.size()
            : static_cast<size_t>(r) % base_colors.size();
        const glm::vec3 color0 = base_colors[base_index];
        const glm::vec3 color1 = base_colors[(base_index + 2u) % base_colors.size()];
        append_mesh(mesh, build_tube({ p0, glm::mix(p0, mid, 0.5f), mid }, params.rung_radius, 5, [&](float) { return color0; }));
        append_mesh(mesh, build_tube({ mid, glm::mix(mid, p1, 0.5f), p1 }, params.rung_radius, 5, [&](float) { return color1; }));
    }

    return mesh;
}

// --- Mitochondrion -----------------------------------------------------------

GeometryMesh build_mitochondrion(const MitochondrionParams& params)
{
    PERF_MEASURE();
    const int lat = std::clamp(params.latitude_segments, 8, 64);
    const int lon = std::clamp(params.longitude_segments, 12, 128);
    const uint32_t seed = static_cast<uint32_t>(params.seed) ^ 0x31337u;
    const float half = params.length * 0.5f;
    const float cristae = static_cast<float>(std::max(1, params.cristae));

    GeometryMesh mesh;

    // Bean-shaped capsule with transverse cristae ridges around its short axis.
    auto surface = [&](const glm::vec3& dir) {
        // u runs along the long (X) axis in [0,1]; the ridge term wrinkles the
        // membrane into evenly spaced folds (the cristae read-through).
        const float u = glm::clamp(dir.x * 0.5f + 0.5f, 0.0f, 1.0f);
        const float bend = 0.18f * params.radius * std::sin(u * glm::pi<float>());
        const float ridge = 0.11f * std::cos(u * glm::pi<float>() * (cristae * 2.0f));
        const float wobble = 0.05f * fbm_noise_3d(dir * 2.4f, 3, seed);
        const float radial = params.radius * (1.0f + ridge + wobble);
        glm::vec3 p(dir.x * half, dir.y * radial, dir.z * radial);
        p.z += bend; // gentle kidney-bean curve
        return p;
    };
    auto surface_color = [&](const glm::vec3& dir) {
        const float u = glm::clamp(dir.x * 0.5f + 0.5f, 0.0f, 1.0f);
        const float ridge = std::cos(u * glm::pi<float>() * (cristae * 2.0f)) * 0.5f + 0.5f;
        glm::vec3 base = glm::mix(params.outer_color_a, params.outer_color_b, u);
        return glm::clamp(glm::mix(base, params.cristae_color, ridge * 0.45f), glm::vec3(0.0f), glm::vec3(1.0f));
    };

    const glm::vec3 top_dir(0.0f, 1.0f, 0.0f);
    mesh.vertices.push_back(make_vertex(surface(top_dir), top_dir, surface_color(top_dir)));
    const int ring_stride = lon + 1;
    auto ring_index = [&](int ring, int col) { return 1 + ring * ring_stride + col; };

    for (int ring = 0; ring < lat - 1; ++ring)
    {
        const float v = static_cast<float>(ring + 1) / static_cast<float>(lat);
        const float theta = glm::pi<float>() * v;
        const float cos_theta = std::cos(theta);
        const float sin_theta = std::sin(theta);
        for (int col = 0; col <= lon; ++col)
        {
            const float u = static_cast<float>(col) / static_cast<float>(lon);
            const float phi = glm::two_pi<float>() * u;
            const glm::vec3 dir(std::cos(phi) * sin_theta, cos_theta, std::sin(phi) * sin_theta);
            mesh.vertices.push_back(make_vertex(surface(dir), dir, surface_color(dir)));
        }
    }

    const glm::vec3 bottom_dir(0.0f, -1.0f, 0.0f);
    const size_t bottom_index = mesh.vertices.size();
    mesh.vertices.push_back(make_vertex(surface(bottom_dir), bottom_dir, surface_color(bottom_dir)));

    for (int col = 0; col < lon; ++col)
        push_triangle(mesh, 0, ring_index(0, col + 1), ring_index(0, col));
    for (int ring = 0; ring < lat - 2; ++ring)
    {
        for (int col = 0; col < lon; ++col)
        {
            const size_t a = ring_index(ring, col);
            const size_t b = ring_index(ring, col + 1);
            const size_t c = ring_index(ring + 1, col);
            const size_t d = ring_index(ring + 1, col + 1);
            push_triangle(mesh, a, b, c);
            push_triangle(mesh, c, b, d);
        }
    }
    const int last_ring = lat - 2;
    for (int col = 0; col < lon; ++col)
        push_triangle(mesh, bottom_index, ring_index(last_ring, col), ring_index(last_ring, col + 1));

    recompute_smooth_normals(mesh);
    return mesh;
}

// --- Golgi apparatus ---------------------------------------------------------

namespace
{

GeometryMesh make_bowed_disc(float radius, float thickness, float curvature, int radial_segments, const glm::vec3& color)
{
    GeometryMesh mesh;
    const int rings = 5;
    const int cols = std::clamp(radial_segments, 12, 96);
    auto bow = [&](float r_norm) { return curvature * r_norm * r_norm; };

    // Two surfaces (top and bottom) plus a rim.
    for (int surface = 0; surface < 2; ++surface)
    {
        const float sign = surface == 0 ? 1.0f : -1.0f;
        const glm::vec3 surface_color = color * (surface == 0 ? 1.0f : 0.82f);
        // Center vertex.
        mesh.vertices.push_back(make_vertex(
            glm::vec3(0.0f, sign * thickness * 0.5f, 0.0f), glm::vec3(0.0f, sign, 0.0f), surface_color));
        const size_t center = mesh.vertices.size() - 1;
        const size_t ring_base = mesh.vertices.size();
        for (int ring = 1; ring <= rings; ++ring)
        {
            const float r_norm = static_cast<float>(ring) / static_cast<float>(rings);
            const float r = radius * r_norm;
            for (int col = 0; col < cols; ++col)
            {
                const float ang = glm::two_pi<float>() * static_cast<float>(col) / static_cast<float>(cols);
                const glm::vec3 pos(r * std::cos(ang), sign * thickness * 0.5f + bow(r_norm), r * std::sin(ang));
                mesh.vertices.push_back(make_vertex(pos, glm::vec3(0.0f, sign, 0.0f), surface_color));
            }
        }
        auto idx = [&](int ring, int col) { return ring_base + static_cast<size_t>(ring - 1) * cols + (col % cols); };
        for (int col = 0; col < cols; ++col)
        {
            if (surface == 0)
                push_triangle(mesh, center, idx(1, col), idx(1, col + 1));
            else
                push_triangle(mesh, center, idx(1, col + 1), idx(1, col));
        }
        for (int ring = 1; ring < rings; ++ring)
        {
            for (int col = 0; col < cols; ++col)
            {
                const size_t a = idx(ring, col);
                const size_t b = idx(ring, col + 1);
                const size_t c = idx(ring + 1, col);
                const size_t d = idx(ring + 1, col + 1);
                if (surface == 0)
                {
                    push_triangle(mesh, a, c, b);
                    push_triangle(mesh, b, c, d);
                }
                else
                {
                    push_triangle(mesh, a, b, c);
                    push_triangle(mesh, b, d, c);
                }
            }
        }
    }
    recompute_smooth_normals(mesh);
    return mesh;
}

} // namespace

GeometryMesh build_golgi(const GolgiParams& params)
{
    PERF_MEASURE();
    GeometryMesh mesh;
    const int count = std::max(1, params.cisternae);
    const uint32_t seed = static_cast<uint32_t>(params.seed) ^ 0x901D1u;
    for (int c = 0; c < count; ++c)
    {
        const float t = count > 1 ? static_cast<float>(c) / static_cast<float>(count - 1) : 0.5f;
        // Cis face is wider and flatter, trans face narrower (classic taper).
        const float disc_radius = params.radius * (0.72f + 0.28f * std::sin(t * glm::pi<float>()));
        const glm::vec3 color = glm::mix(params.color_a, params.color_b, t);
        GeometryMesh disc = make_bowed_disc(disc_radius, params.thickness, params.curvature, params.radial_segments, color);
        const float y = (static_cast<float>(c) - static_cast<float>(count - 1) * 0.5f) * params.spacing;
        const float wobble = 0.06f * value_noise_3d(glm::vec3(static_cast<float>(c) * 1.3f, 0.0f, 0.0f), seed);
        glm::mat4 transform(1.0f);
        transform[3] = glm::vec4(wobble, y, wobble * 0.7f, 1.0f);
        append_mesh(mesh, transform_mesh(disc, transform));
    }
    return mesh;
}

// --- Endoplasmic reticulum ---------------------------------------------------

ErResult build_endoplasmic_reticulum(const ErParams& params)
{
    PERF_MEASURE();
    ErResult result;
    const uint32_t seed = static_cast<uint32_t>(params.seed) ^ 0xE21Au;
    const int strands = std::max(1, params.strands);
    for (int s = 0; s < strands; ++s)
    {
        const float base_angle = glm::two_pi<float>() * static_cast<float>(s) / static_cast<float>(strands);
        const float radius0 = params.extent * (0.45f + 0.2f * static_cast<float>(s) / static_cast<float>(strands));
        std::vector<glm::vec3> path;
        const int steps = 34;
        for (int i = 0; i <= steps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float angle = base_angle + t * glm::two_pi<float>() * 1.4f;
            const float wobble = 1.0f + 0.32f * std::sin(t * 9.0f + static_cast<float>(s));
            const float r = radius0 * wobble;
            const float y = 0.55f * std::sin(t * 6.2831f * 1.5f + static_cast<float>(s) * 1.1f)
                + 0.25f * fbm_noise_3d(glm::vec3(t * 4.0f, static_cast<float>(s), 0.0f), 3, seed);
            path.push_back(glm::vec3(r * std::cos(angle), y, r * std::sin(angle)));
        }
        append_mesh(result.mesh, build_tube(path, params.tube_radius, params.tube_sides, [&](float t) { return params.color * (0.8f + 0.2f * std::sin(t * 8.0f)); }));

        for (int i = 2; i < static_cast<int>(path.size()) - 2; i += 3)
            result.ribosome_sites.push_back(path[i]);
    }
    return result;
}

} // namespace draxul
