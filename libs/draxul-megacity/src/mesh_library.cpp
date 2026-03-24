#include "mesh_library.h"

#include <array>

namespace draxul
{

namespace
{

void append_quad(MeshData& mesh, const std::array<glm::vec3, 4>& positions, const glm::vec3& normal,
    const glm::vec3& color)
{
    const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
    for (const glm::vec3& pos : positions)
    {
        SceneVertex vertex;
        vertex.position = pos;
        vertex.normal = normal;
        vertex.color = color;
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

MeshData build_unit_cube_mesh()
{
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    const float h = 0.5f;
    const glm::vec3 color{ 1.0f, 1.0f, 1.0f };

    append_quad(mesh, { {
                          { -h, -h, h },
                          { h, -h, h },
                          { h, h, h },
                          { -h, h, h },
                      } },
        { 0.0f, 0.0f, 1.0f }, color);
    append_quad(mesh, { {
                          { h, -h, -h },
                          { -h, -h, -h },
                          { -h, h, -h },
                          { h, h, -h },
                      } },
        { 0.0f, 0.0f, -1.0f }, color);
    append_quad(mesh, { {
                          { -h, -h, -h },
                          { -h, -h, h },
                          { -h, h, h },
                          { -h, h, -h },
                      } },
        { -1.0f, 0.0f, 0.0f }, color);
    append_quad(mesh, { {
                          { h, -h, h },
                          { h, -h, -h },
                          { h, h, -h },
                          { h, h, h },
                      } },
        { 1.0f, 0.0f, 0.0f }, color);
    append_quad(mesh, { {
                          { -h, h, h },
                          { h, h, h },
                          { h, h, -h },
                          { -h, h, -h },
                      } },
        { 0.0f, 1.0f, 0.0f }, color);
    append_quad(mesh, { {
                          { -h, -h, -h },
                          { h, -h, -h },
                          { h, -h, h },
                          { -h, -h, h },
                      } },
        { 0.0f, -1.0f, 0.0f }, color);

    return mesh;
}

MeshData build_grid_mesh(int width, int height, float tile_size)
{
    MeshData mesh;
    mesh.vertices.reserve(static_cast<size_t>(width * height * 4));
    mesh.indices.reserve(static_cast<size_t>(width * height * 6));

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const float x0 = static_cast<float>(x) * tile_size;
            const float x1 = static_cast<float>(x + 1) * tile_size;
            const float z0 = static_cast<float>(y) * tile_size;
            const float z1 = static_cast<float>(y + 1) * tile_size;
            const bool even = ((x + y) % 2) == 0;
            const glm::vec3 color = even ? glm::vec3(0.32f, 0.34f, 0.38f) : glm::vec3(0.25f, 0.27f, 0.31f);

            append_quad(mesh, { {
                                  { x0, 0.0f, z0 },
                                  { x1, 0.0f, z0 },
                                  { x1, 0.0f, z1 },
                                  { x0, 0.0f, z1 },
                              } },
                { 0.0f, 1.0f, 0.0f }, color);
        }
    }

    return mesh;
}

} // namespace draxul
