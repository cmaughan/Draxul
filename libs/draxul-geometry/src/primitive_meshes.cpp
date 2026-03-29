#include <draxul/perf_timing.h>
#include <draxul/primitive_meshes.h>

#include <array>

namespace draxul
{

namespace
{

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

} // namespace draxul
