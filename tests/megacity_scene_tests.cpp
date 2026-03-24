#include <catch2/catch_all.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

#include "isometric_camera.h"
#include "isometric_world.h"
#include "mesh_library.h"

using namespace draxul;

namespace
{

float triangle_up_normal_y(const MeshData& mesh, size_t triangle_index)
{
    const size_t base = triangle_index * 3;
    const glm::vec3 p0 = mesh.vertices[mesh.indices[base + 0]].position;
    const glm::vec3 p1 = mesh.vertices[mesh.indices[base + 1]].position;
    const glm::vec3 p2 = mesh.vertices[mesh.indices[base + 2]].position;
    return glm::cross(p1 - p0, p2 - p0).y;
}

} // namespace

TEST_CASE("megacity world maps grid coordinates to tile centers", "[megacity]")
{
    IsometricWorld world;

    const glm::vec3 origin = world.grid_to_world(0, 0);
    const glm::vec3 corner = world.grid_to_world(4, 4);
    const glm::vec3 elevated = world.grid_to_world(2, 3, 2.0f);

    CHECK(origin.x == Catch::Approx(0.5f));
    CHECK(origin.y == Catch::Approx(0.0f));
    CHECK(origin.z == Catch::Approx(0.5f));

    CHECK(corner.x == Catch::Approx(4.5f));
    CHECK(corner.z == Catch::Approx(4.5f));

    CHECK(elevated.x == Catch::Approx(2.5f));
    CHECK(elevated.y == Catch::Approx(2.0f));
    CHECK(elevated.z == Catch::Approx(3.5f));
}

TEST_CASE("megacity camera projection responds to viewport aspect", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);

    camera.set_viewport(100, 100);
    const glm::mat4 square = camera.proj_matrix();

    camera.set_viewport(200, 100);
    const glm::mat4 wide = camera.proj_matrix();

    CHECK(wide[0][0] < square[0][0]);
    CHECK(wide[1][1] == Catch::Approx(square[1][1]));
}

TEST_CASE("megacity camera footprint covers the centered world", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);
    camera.set_viewport(160, 100);

    const GroundFootprint footprint = camera.visible_ground_footprint();

    CHECK(footprint.min_x < 0.5f);
    CHECK(footprint.max_x > 4.5f);
    CHECK(footprint.min_z < 0.5f);
    CHECK(footprint.max_z > 4.5f);
}

TEST_CASE("megacity mesh library builds expected primitive counts", "[megacity]")
{
    const MeshData cube = build_unit_cube_mesh();
    const MeshData filled = build_grid_mesh(2, 2, 1.0f);

    FloorGridSpec grid;
    grid.enabled = true;
    grid.min_x = 0;
    grid.max_x = 2;
    grid.min_z = 0;
    grid.max_z = 2;
    grid.tile_size = 1.0f;
    grid.line_width = 0.04f;

    const MeshData outline = build_outline_grid_mesh(grid);

    CHECK(cube.vertices.size() == 24);
    CHECK(cube.indices.size() == 36);

    CHECK(filled.vertices.size() == 16);
    CHECK(filled.indices.size() == 24);
    CHECK(triangle_up_normal_y(filled, 0) > 0.0f);

    CHECK(outline.vertices.size() == 24);
    CHECK(outline.indices.size() == 36);
    CHECK(triangle_up_normal_y(outline, 0) > 0.0f);
}

#endif
