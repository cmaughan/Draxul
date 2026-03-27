#include <catch2/catch_all.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

#include <draxul/tree_generator.h>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

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
        CHECK(vertex.color.a == Catch::Approx(1.0f));
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

#endif
