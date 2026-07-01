#include <draxul/perf_timing.h>
#include <draxul/primitive_meshes.h>

#include <algorithm>
#include <array>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <limits>

namespace draxul
{

namespace
{

uint16_t checked_index(size_t index)
{
    return static_cast<uint16_t>(std::min(index, static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
}

void append_indexed_triangle(GeometryMesh& mesh, size_t a, size_t b, size_t c)
{
    mesh.indices.push_back(checked_index(a));
    mesh.indices.push_back(checked_index(b));
    mesh.indices.push_back(checked_index(c));
}

GeometryVertex make_sphere_vertex(const glm::vec3& position, const glm::vec2& uv, float phi)
{
    GeometryVertex vertex;
    vertex.position = position;
    vertex.normal = glm::normalize(position);
    vertex.color = glm::vec3(1.0f);
    vertex.uv = uv;
    vertex.tangent = glm::vec4(glm::normalize(glm::vec3(-std::sin(phi), 0.0f, std::cos(phi))), 1.0f);
    return vertex;
}

void append_quad(GeometryMesh& mesh, const std::array<glm::vec3, 4>& positions, const glm::vec3& normal,
    const glm::vec3& tangent, const std::array<glm::vec2, 4>& uvs = { {
                                  { 0.0f, 0.0f },
                                  { 1.0f, 0.0f },
                                  { 1.0f, 1.0f },
                                  { 0.0f, 1.0f },
                              } })
{
    PERF_MEASURE();
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (size_t i = 0; i < positions.size(); ++i)
    {
        GeometryVertex vertex;
        vertex.position = positions[i];
        vertex.normal = normal;
        vertex.color = glm::vec3(1.0f);
        vertex.uv = uvs[i];
        vertex.tangent = glm::vec4(tangent, 1.0f);
        mesh.vertices.push_back(vertex);
    }

    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
}

} // namespace

GeometryMesh build_unit_cube_geometry()
{
    PERF_MEASURE();
    GeometryMesh mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    const float h = 0.5f;

    append_quad(mesh, { {
                          { -h, -h, h },
                          { h, -h, h },
                          { h, h, h },
                          { -h, h, h },
                      } },
        { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f });
    append_quad(mesh, { {
                          { h, -h, -h },
                          { -h, -h, -h },
                          { -h, h, -h },
                          { h, h, -h },
                      } },
        { 0.0f, 0.0f, -1.0f }, { -1.0f, 0.0f, 0.0f });
    append_quad(mesh, { {
                          { -h, -h, -h },
                          { -h, -h, h },
                          { -h, h, h },
                          { -h, h, -h },
                      } },
        { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });
    append_quad(mesh, { {
                          { h, -h, h },
                          { h, -h, -h },
                          { h, h, -h },
                          { h, h, h },
                      } },
        { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f });
    append_quad(mesh, { {
                          { -h, h, h },
                          { h, h, h },
                          { h, h, -h },
                          { -h, h, -h },
                      } },
        { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f });
    append_quad(mesh, { {
                          { -h, -h, -h },
                          { h, -h, -h },
                          { h, -h, h },
                          { -h, -h, h },
                      } },
        { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f });

    return mesh;
}

GeometryMesh build_unit_uv_sphere_geometry(int latitude_segments, int longitude_segments)
{
    PERF_MEASURE();
    latitude_segments = std::clamp(latitude_segments, 4, 128);
    longitude_segments = std::clamp(longitude_segments, 6, 256);

    GeometryMesh mesh;
    const size_t ring_vertex_count = static_cast<size_t>(longitude_segments) + 1u;
    const size_t latitude_ring_count = static_cast<size_t>(latitude_segments - 1);
    mesh.vertices.reserve(2u + ring_vertex_count * latitude_ring_count);
    mesh.indices.reserve(static_cast<size_t>(longitude_segments) * static_cast<size_t>(latitude_segments) * 6u);

    const float radius = 0.5f;
    mesh.vertices.push_back(make_sphere_vertex({ 0.0f, radius, 0.0f }, { 0.5f, 0.0f }, 0.0f));

    auto ring_index = [ring_vertex_count](int latitude, int longitude) {
        return 1u + static_cast<size_t>(latitude - 1) * ring_vertex_count + static_cast<size_t>(longitude);
    };

    for (int latitude = 1; latitude < latitude_segments; ++latitude)
    {
        const float v = static_cast<float>(latitude) / static_cast<float>(latitude_segments);
        const float theta = glm::pi<float>() * v;
        const float y = std::cos(theta) * radius;
        const float ring_radius = std::sin(theta) * radius;
        for (int longitude = 0; longitude <= longitude_segments; ++longitude)
        {
            const float u = static_cast<float>(longitude) / static_cast<float>(longitude_segments);
            const float phi = glm::two_pi<float>() * u;
            const glm::vec3 position{
                std::cos(phi) * ring_radius,
                y,
                std::sin(phi) * ring_radius,
            };
            mesh.vertices.push_back(make_sphere_vertex(position, { u, v }, phi));
        }
    }

    const size_t bottom_index = mesh.vertices.size();
    mesh.vertices.push_back(make_sphere_vertex({ 0.0f, -radius, 0.0f }, { 0.5f, 1.0f }, 0.0f));

    for (int longitude = 0; longitude < longitude_segments; ++longitude)
        append_indexed_triangle(mesh, 0u, ring_index(1, longitude + 1), ring_index(1, longitude));

    for (int latitude = 1; latitude < latitude_segments - 1; ++latitude)
    {
        for (int longitude = 0; longitude < longitude_segments; ++longitude)
        {
            const size_t upper_a = ring_index(latitude, longitude);
            const size_t upper_b = ring_index(latitude, longitude + 1);
            const size_t lower_a = ring_index(latitude + 1, longitude);
            const size_t lower_b = ring_index(latitude + 1, longitude + 1);
            append_indexed_triangle(mesh, upper_a, upper_b, lower_a);
            append_indexed_triangle(mesh, lower_a, upper_b, lower_b);
        }
    }

    const int last_ring = latitude_segments - 1;
    for (int longitude = 0; longitude < longitude_segments; ++longitude)
        append_indexed_triangle(mesh, bottom_index, ring_index(last_ring, longitude), ring_index(last_ring, longitude + 1));

    return mesh;
}

} // namespace draxul
