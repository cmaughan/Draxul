#include <catch2/catch_all.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

#include "isometric_camera.h"
#include "isometric_world.h"
#include "mesh_library.h"

using namespace draxul;

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

TEST_CASE("megacity mesh library builds expected primitive counts", "[megacity]")
{
    const MeshData cube = build_unit_cube_mesh();
    const MeshData grid = build_grid_mesh(5, 5, 1.0f);

    CHECK(cube.vertices.size() == 24);
    CHECK(cube.indices.size() == 36);

    CHECK(grid.vertices.size() == 100);
    CHECK(grid.indices.size() == 150);
}

#endif
