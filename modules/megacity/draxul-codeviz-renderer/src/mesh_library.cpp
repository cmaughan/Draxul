#include "mesh_library.h"

#include <draxul/perf_timing.h>
#include <draxul/primitive_meshes.h>
#include <draxul/tree_generator.h>

#include <array>

namespace draxul
{

namespace
{

void append_quad(MeshData& mesh, const std::array<glm::vec3, 4>& positions, const glm::vec3& normal,
    const glm::vec3& color, const std::array<glm::vec2, 4>& uvs = { {
                                { 0.0f, 0.0f },
                                { 1.0f, 0.0f },
                                { 1.0f, 1.0f },
                                { 0.0f, 1.0f },
                            } },
    float tex_blend = 0.0f)
{
    PERF_MEASURE();
    const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
    for (size_t i = 0; i < positions.size(); ++i)
    {
        SceneVertex vertex;
        vertex.position = positions[i];
        vertex.normal = normal;
        vertex.color = color;
        vertex.uv = uvs[i];
        vertex.tex_blend = tex_blend;
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
    return build_unit_cube_geometry();
}

MeshData build_floor_box_mesh()
{
    return build_unit_cube_mesh();
}

MeshData build_tree_bark_mesh()
{
    PERF_MEASURE();
    DraxulTreeParams params = make_tree_params_from_age(40.0f);
    params.seed = 7;
    params.radial_segments = 12;
    params.trunk_length = 7.0f;
    params.trunk_base_radius = 0.55f;
    params.trunk_tip_radius = 0.22f;
    params.bark_color_root = { 0.18f, 0.12f, 0.08f };
    params.bark_color_tip = { 0.42f, 0.31f, 0.21f };
    params.bark_color_noise = 0.05f;
    return generate_draxul_tree_meshes(params).bark_mesh;
}

MeshData build_tree_leaf_mesh()
{
    PERF_MEASURE();
    DraxulTreeParams params = make_tree_params_from_age(40.0f);
    params.seed = 7;
    params.radial_segments = 12;
    params.trunk_length = 7.0f;
    params.trunk_base_radius = 0.55f;
    params.trunk_tip_radius = 0.22f;
    return generate_draxul_tree_meshes(params).leaf_mesh;
}

MeshData build_road_surface_mesh()
{
    return build_unit_cube_geometry();
}

MeshData build_roof_sign_mesh()
{
    PERF_MEASURE();
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    const float hx = 0.5f;
    const float hy = 0.5f;
    const float hz = 0.5f;
    const glm::vec3 color{ 1.0f, 1.0f, 1.0f };

    append_quad(mesh, { {
                          { -hx, -hy, hz },
                          { hx, -hy, hz },
                          { hx, hy, hz },
                          { -hx, hy, hz },
                      } },
        { 0.0f, 0.0f, 1.0f }, color);
    append_quad(mesh, { {
                          { hx, -hy, -hz },
                          { -hx, -hy, -hz },
                          { -hx, hy, -hz },
                          { hx, hy, -hz },
                      } },
        { 0.0f, 0.0f, -1.0f }, color);
    append_quad(mesh, { {
                          { -hx, -hy, -hz },
                          { -hx, -hy, hz },
                          { -hx, hy, hz },
                          { -hx, hy, -hz },
                      } },
        { -1.0f, 0.0f, 0.0f }, color);
    append_quad(mesh, { {
                          { hx, -hy, hz },
                          { hx, -hy, -hz },
                          { hx, hy, -hz },
                          { hx, hy, hz },
                      } },
        { 1.0f, 0.0f, 0.0f }, color);
    append_quad(mesh, { {
                          { -hx, hy, hz },
                          { hx, hy, hz },
                          { hx, hy, -hz },
                          { -hx, hy, -hz },
                      } },
        { 0.0f, 1.0f, 0.0f }, color,
        { {
            { 0.0f, 1.0f },
            { 1.0f, 1.0f },
            { 1.0f, 0.0f },
            { 0.0f, 0.0f },
        } },
        1.0f);
    append_quad(mesh, { {
                          { -hx, -hy, -hz },
                          { hx, -hy, -hz },
                          { hx, -hy, hz },
                          { -hx, -hy, hz },
                      } },
        { 0.0f, -1.0f, 0.0f }, color);

    return mesh;
}

MeshData build_wall_sign_mesh()
{
    PERF_MEASURE();
    MeshData mesh;
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);

    const float hx = 0.5f;
    const float hy = 0.5f;
    const float hz = 0.5f;
    const glm::vec3 color{ 1.0f, 1.0f, 1.0f };

    append_quad(mesh, { {
                          { -hx, -hy, hz },
                          { hx, -hy, hz },
                          { hx, hy, hz },
                          { -hx, hy, hz },
                      } },
        { 0.0f, 0.0f, 1.0f }, color,
        { {
            { 0.0f, 1.0f },
            { 1.0f, 1.0f },
            { 1.0f, 0.0f },
            { 0.0f, 0.0f },
        } },
        1.0f);
    append_quad(mesh, { {
                          { hx, -hy, -hz },
                          { -hx, -hy, -hz },
                          { -hx, hy, -hz },
                          { hx, hy, -hz },
                      } },
        { 0.0f, 0.0f, -1.0f }, color);
    append_quad(mesh, { {
                          { -hx, -hy, -hz },
                          { -hx, -hy, hz },
                          { -hx, hy, hz },
                          { -hx, hy, -hz },
                      } },
        { -1.0f, 0.0f, 0.0f }, color);
    append_quad(mesh, { {
                          { hx, -hy, hz },
                          { hx, -hy, -hz },
                          { hx, hy, -hz },
                          { hx, hy, hz },
                      } },
        { 1.0f, 0.0f, 0.0f }, color);
    append_quad(mesh, { {
                          { -hx, hy, hz },
                          { hx, hy, hz },
                          { hx, hy, -hz },
                          { -hx, hy, -hz },
                      } },
        { 0.0f, 1.0f, 0.0f }, color);
    append_quad(mesh, { {
                          { -hx, -hy, -hz },
                          { hx, -hy, -hz },
                          { hx, -hy, hz },
                          { -hx, -hy, hz },
                      } },
        { 0.0f, -1.0f, 0.0f }, color);

    return mesh;
}

MeshData build_grid_mesh(int width, int height, float tile_size)
{
    PERF_MEASURE();
    MeshData mesh;
    mesh.vertices.reserve(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    mesh.indices.reserve(static_cast<size_t>(width) * static_cast<size_t>(height) * 6u);

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
                                  { x0, 0.0f, z1 },
                                  { x1, 0.0f, z1 },
                                  { x1, 0.0f, z0 },
                              } },
                { 0.0f, 1.0f, 0.0f }, color);
        }
    }

    return mesh;
}

MeshData build_outline_grid_mesh(const FloorGridSpec& spec)
{
    PERF_MEASURE();
    MeshData mesh;
    if (!spec.enabled || spec.max_x < spec.min_x || spec.max_z < spec.min_z || spec.tile_size <= 0.0f)
        return mesh;

    const int x_line_count = spec.max_x - spec.min_x + 1;
    const int z_line_count = spec.max_z - spec.min_z + 1;
    mesh.vertices.reserve(static_cast<size_t>((x_line_count + z_line_count) * 4));
    mesh.indices.reserve(static_cast<size_t>((x_line_count + z_line_count) * 6));

    const float half_width = spec.line_width * 0.5f;
    const float xmin = static_cast<float>(spec.min_x) * spec.tile_size;
    const float xmax = static_cast<float>(spec.max_x) * spec.tile_size;
    const float zmin = static_cast<float>(spec.min_z) * spec.tile_size;
    const float zmax = static_cast<float>(spec.max_z) * spec.tile_size;
    const glm::vec3 color{ spec.color.x, spec.color.y, spec.color.z };
    const float y = spec.y;

    for (int x = spec.min_x; x <= spec.max_x; ++x)
    {
        const float world_x = static_cast<float>(x) * spec.tile_size;
        append_quad(mesh, { {
                              { world_x - half_width, y, zmin },
                              { world_x - half_width, y, zmax },
                              { world_x + half_width, y, zmax },
                              { world_x + half_width, y, zmin },
                          } },
            { 0.0f, 1.0f, 0.0f }, color);
    }

    for (int z = spec.min_z; z <= spec.max_z; ++z)
    {
        const float world_z = static_cast<float>(z) * spec.tile_size;
        append_quad(mesh, { {
                              { xmin, y, world_z - half_width },
                              { xmin, y, world_z + half_width },
                              { xmax, y, world_z + half_width },
                              { xmax, y, world_z - half_width },
                          } },
            { 0.0f, 1.0f, 0.0f }, color);
    }

    return mesh;
}

} // namespace draxul
