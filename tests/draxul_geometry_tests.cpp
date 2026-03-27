#include <catch2/catch_all.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

#include <draxul/primitive_meshes.h>
#include <draxul/tree_generator.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace draxul;

TEST_CASE("tree params from age stay bounded", "[geometry]")
{
    const DraxulTreeParams sapling = make_tree_params_from_age(1.0f);
    const DraxulTreeParams mature = make_tree_params_from_age(18.0f);
    const DraxulTreeParams ancient = make_tree_params_from_age(100.0f);

    CHECK(sapling.trunk_length < mature.trunk_length);
    CHECK(sapling.trunk_base_radius < mature.trunk_base_radius);
    CHECK(ancient.trunk_length == Catch::Approx(make_tree_params_from_age(40.0f).trunk_length));
    CHECK(ancient.max_height == Catch::Approx(make_tree_params_from_age(40.0f).max_height));
    CHECK(mature.radial_segments >= 6);
}

TEST_CASE("tree generator emits a valid trunk scaffold mesh", "[geometry]")
{
    DraxulTreeParams params = make_tree_params_from_age(8.0f);
    params.seed = 42;

    const GeometryMesh mesh = generate_draxul_tree(params);

    REQUIRE_FALSE(mesh.vertices.empty());
    REQUIRE_FALSE(mesh.indices.empty());
    REQUIRE(mesh.indices.size() % 3 == 0);

    for (const GeometryVertex& vertex : mesh.vertices)
    {
        CHECK(glm::length(vertex.normal) == Catch::Approx(1.0f).margin(0.001f));
        CHECK(std::abs(glm::dot(vertex.normal, glm::vec3(vertex.tangent))) <= Catch::Approx(0.001f));
        CHECK(vertex.uv.x >= Catch::Approx(0.0f));
        CHECK(vertex.uv.x <= Catch::Approx(1.0f));
        CHECK(vertex.uv.y >= Catch::Approx(0.0f));
        CHECK(vertex.uv.y <= Catch::Approx(1.0f));
        CHECK(vertex.color.r >= Catch::Approx(0.0f));
        CHECK(vertex.color.r <= Catch::Approx(1.0f));
        CHECK(vertex.color.g >= Catch::Approx(0.0f));
        CHECK(vertex.color.g <= Catch::Approx(1.0f));
        CHECK(vertex.color.b >= Catch::Approx(0.0f));
        CHECK(vertex.color.b <= Catch::Approx(1.0f));
    }
}

TEST_CASE("tree generator is deterministic for a fixed seed", "[geometry]")
{
    DraxulTreeParams params = make_tree_params_from_age(12.0f);
    params.seed = 99;

    const GeometryMesh a = generate_draxul_tree(params);
    const GeometryMesh b = generate_draxul_tree(params);

    REQUIRE(a.vertices.size() == b.vertices.size());
    REQUIRE(a.indices.size() == b.indices.size());
    REQUIRE(a.vertices.size() > 8);

    CHECK(a.vertices[0].position == b.vertices[0].position);
    CHECK(a.vertices[7].normal == b.vertices[7].normal);
    CHECK(a.vertices.back().color == b.vertices.back().color);
    CHECK(a.indices == b.indices);
}

TEST_CASE("tree generator emits branching canopy beyond the trunk radius", "[geometry]")
{
    DraxulTreeParams params = make_tree_params_from_age(24.0f);
    params.seed = 11;
    params.max_branch_depth = 3;
    params.child_branches_min = 2;
    params.child_branches_max = 4;

    const GeometryMesh mesh = generate_draxul_tree(params);

    float max_horizontal_radius = 0.0f;
    for (const GeometryVertex& vertex : mesh.vertices)
        max_horizontal_radius = std::max(max_horizontal_radius, glm::length(glm::vec2(vertex.position.x, vertex.position.z)));

    CHECK(max_horizontal_radius > params.trunk_base_radius * params.overall_scale * 1.5f);
}

TEST_CASE("tree generator emits separate bark and leaf meshes", "[geometry]")
{
    DraxulTreeParams params = make_tree_params_from_age(24.0f);
    params.seed = 17;
    params.leaf_density = 1.5f;

    const DraxulTreeMeshes meshes = generate_draxul_tree_meshes(params);

    CHECK_FALSE(meshes.bark_mesh.vertices.empty());
    CHECK_FALSE(meshes.bark_mesh.indices.empty());
    CHECK_FALSE(meshes.leaf_mesh.vertices.empty());
    CHECK_FALSE(meshes.leaf_mesh.indices.empty());
}

TEST_CASE("tree generator leaf start depth suppresses trunk leaves", "[geometry]")
{
    DraxulTreeParams trunk_leaves = make_tree_params_from_age(24.0f);
    trunk_leaves.seed = 19;
    trunk_leaves.max_branch_depth = 0;
    trunk_leaves.child_branches_min = 0;
    trunk_leaves.child_branches_max = 0;
    trunk_leaves.leaf_density = 2.0f;
    trunk_leaves.leaf_start_depth = 0;

    DraxulTreeParams no_trunk_leaves = trunk_leaves;
    no_trunk_leaves.leaf_start_depth = 1;

    const DraxulTreeMeshes with_trunk_leaves = generate_draxul_tree_meshes(trunk_leaves);
    const DraxulTreeMeshes without_trunk_leaves = generate_draxul_tree_meshes(no_trunk_leaves);

    CHECK_FALSE(with_trunk_leaves.leaf_mesh.vertices.empty());
    CHECK(without_trunk_leaves.leaf_mesh.vertices.empty());
    CHECK(without_trunk_leaves.leaf_mesh.indices.empty());
}

TEST_CASE("tree generator leaf size range changes card extent", "[geometry]")
{
    DraxulTreeParams small_leaves = make_tree_params_from_age(20.0f);
    small_leaves.seed = 23;
    small_leaves.max_branch_depth = 0;
    small_leaves.child_branches_min = 0;
    small_leaves.child_branches_max = 0;
    small_leaves.leaf_density = 4.0f;
    small_leaves.leaf_size_range = glm::vec2(1.2f, 1.2f);
    small_leaves.leaf_start_depth = 0;

    DraxulTreeParams large_leaves = small_leaves;
    large_leaves.leaf_size_range = glm::vec2(5.5f, 5.5f);

    const DraxulTreeMeshes small_meshes = generate_draxul_tree_meshes(small_leaves);
    const DraxulTreeMeshes large_meshes = generate_draxul_tree_meshes(large_leaves);

    auto leaf_extent = [](const GeometryMesh& mesh) {
        REQUIRE_FALSE(mesh.vertices.empty());
        glm::vec3 min_pos(std::numeric_limits<float>::max());
        glm::vec3 max_pos(std::numeric_limits<float>::lowest());
        for (const GeometryVertex& vertex : mesh.vertices)
        {
            min_pos = glm::min(min_pos, vertex.position);
            max_pos = glm::max(max_pos, vertex.position);
        }
        return glm::length(max_pos - min_pos);
    };

    CHECK(leaf_extent(large_meshes.leaf_mesh) > leaf_extent(small_meshes.leaf_mesh) + 0.1f);
}

TEST_CASE("tree generator supports lateral branch and trunk wander", "[geometry]")
{
    DraxulTreeParams straight = make_tree_params_from_age(16.0f);
    straight.seed = 21;
    straight.max_branch_depth = 0;
    straight.curvature = 0.0f;
    straight.trunk_wander = 0.0f;
    straight.branch_wander = 0.0f;
    straight.wander_frequency = 0.0f;
    straight.wander_deviation = 0.0f;

    DraxulTreeParams wandering = straight;
    wandering.trunk_wander = 0.9f;
    wandering.wander_frequency = 1.0f;
    wandering.wander_deviation = 1.0f;

    const DraxulTreeMeshes straight_meshes = generate_draxul_tree_meshes(straight);
    const DraxulTreeMeshes wandering_meshes = generate_draxul_tree_meshes(wandering);

    auto top_center_radius = [](const GeometryMesh& mesh) {
        float max_y = std::numeric_limits<float>::lowest();
        for (const GeometryVertex& vertex : mesh.vertices)
            max_y = std::max(max_y, vertex.position.y);

        glm::vec2 average_xz(0.0f);
        int count = 0;
        for (const GeometryVertex& vertex : mesh.vertices)
        {
            if (std::abs(vertex.position.y - max_y) <= 1e-4f)
            {
                average_xz += glm::vec2(vertex.position.x, vertex.position.z);
                ++count;
            }
        }
        REQUIRE(count > 0);
        average_xz /= static_cast<float>(count);
        return glm::length(average_xz);
    };

    CHECK(
        top_center_radius(wandering_meshes.bark_mesh)
        > top_center_radius(straight_meshes.bark_mesh) + 0.02f);
}

TEST_CASE("dense tree bark stays within index budget and avoids stretched triangles", "[geometry]")
{
    DraxulTreeParams params = make_tree_params_from_age(40.0f);
    params.seed = 29;
    params.max_branch_depth = 4;
    params.child_branches_min = 3;
    params.child_branches_max = 5;
    params.branch_wander = 0.5f;
    params.wander_frequency = 0.55f;
    params.wander_deviation = 0.75f;

    const DraxulTreeMeshes meshes = generate_draxul_tree_meshes(params);

    REQUIRE(meshes.bark_mesh.vertices.size() <= static_cast<size_t>(std::numeric_limits<uint16_t>::max()));
    REQUIRE(meshes.bark_mesh.indices.size() % 3 == 0);

    float max_edge_length = 0.0f;
    for (size_t base = 0; base + 2 < meshes.bark_mesh.indices.size(); base += 3)
    {
        const glm::vec3 p0 = meshes.bark_mesh.vertices[meshes.bark_mesh.indices[base + 0]].position;
        const glm::vec3 p1 = meshes.bark_mesh.vertices[meshes.bark_mesh.indices[base + 1]].position;
        const glm::vec3 p2 = meshes.bark_mesh.vertices[meshes.bark_mesh.indices[base + 2]].position;
        max_edge_length = std::max(max_edge_length, glm::distance(p0, p1));
        max_edge_length = std::max(max_edge_length, glm::distance(p1, p2));
        max_edge_length = std::max(max_edge_length, glm::distance(p2, p0));
    }

    CHECK(max_edge_length < params.max_height * params.overall_scale * 0.45f);
}

TEST_CASE("unit cube geometry uses the shared vertex format", "[geometry]")
{
    const GeometryMesh mesh = build_unit_cube_geometry();

    REQUIRE(mesh.vertices.size() == 24);
    REQUIRE(mesh.indices.size() == 36);

    for (const GeometryVertex& vertex : mesh.vertices)
    {
        CHECK(glm::length(vertex.normal) == Catch::Approx(1.0f).margin(0.001f));
        CHECK(std::abs(glm::dot(vertex.normal, glm::vec3(vertex.tangent))) <= Catch::Approx(0.001f));
        CHECK(vertex.uv.x >= Catch::Approx(0.0f));
        CHECK(vertex.uv.x <= Catch::Approx(1.0f));
        CHECK(vertex.uv.y >= Catch::Approx(0.0f));
        CHECK(vertex.uv.y <= Catch::Approx(1.0f));
    }
}

#endif
