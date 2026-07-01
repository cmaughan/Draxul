#ifdef DRAXUL_ENABLE_MEGACITY

#include <draxul/isometric_camera.h>
#include <draxul/megacity_code_config.h>
#include "scene_snapshot_builder.h"
#include "scene_world.h"
#include "static_mesh_family_cache.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <glm/glm.hpp>
#include <limits>

using namespace draxul;

namespace
{

struct MeshBounds
{
    glm::vec3 min{ std::numeric_limits<float>::max() };
    glm::vec3 max{ std::numeric_limits<float>::lowest() };
};

MeshBounds bounds_of(const GeometryMesh& mesh)
{
    MeshBounds bounds;
    for (const GeometryVertex& vertex : mesh.vertices)
    {
        bounds.min.x = std::min(bounds.min.x, vertex.position.x);
        bounds.min.y = std::min(bounds.min.y, vertex.position.y);
        bounds.min.z = std::min(bounds.min.z, vertex.position.z);
        bounds.max.x = std::max(bounds.max.x, vertex.position.x);
        bounds.max.y = std::max(bounds.max.y, vertex.position.y);
        bounds.max.z = std::max(bounds.max.z, vertex.position.z);
    }
    return bounds;
}

} // namespace

TEST_CASE("static mesh family cache reuses normalized building ring meshes", "[megacity][static-mesh-family]")
{
    StaticMeshFamilyCache cache;
    StaticBuildingRingSpec spec;
    spec.sides = 4;
    spec.middle_strip_scale = 1.0f;
    spec.level_gap_fraction = 0.03f;
    spec.layers = {
        { 0.30f, 1.0f, 0u },
        { 0.67f, 0.72f, 1u },
    };

    const auto first = cache.building_ring(spec);
    const auto second = cache.building_ring(spec);

    REQUIRE(first);
    CHECK(first.get() == second.get());
    CHECK(cache.size() == 1);

    const MeshBounds bounds = bounds_of(*first);
    CHECK(bounds.min.x == Catch::Approx(-0.5f).margin(0.001f));
    CHECK(bounds.max.x == Catch::Approx(0.5f).margin(0.001f));
    CHECK(bounds.min.y == Catch::Approx(0.0f).margin(0.001f));
    CHECK(bounds.max.y == Catch::Approx(1.0f).margin(0.001f));
}

TEST_CASE("static mesh family cache separates different ring layer patterns", "[megacity][static-mesh-family]")
{
    StaticMeshFamilyCache cache;
    StaticBuildingRingSpec two_layers;
    two_layers.sides = 4;
    two_layers.layers = {
        { 0.5f, 1.0f, 0u },
        { 0.5f, 0.8f, 1u },
    };

    StaticBuildingRingSpec three_layers = two_layers;
    three_layers.layers = {
        { 0.33f, 1.0f, 0u },
        { 0.33f, 0.8f, 1u },
        { 0.34f, 1.0f, 2u },
    };

    CHECK(cache.building_ring(two_layers).get() != cache.building_ring(three_layers).get());
    CHECK(cache.size() == 2);
}

TEST_CASE("static mesh family cache reuses roof sign and sidewalk rings by normalized ratios", "[megacity][static-mesh-family]")
{
    StaticMeshFamilyCache cache;

    StaticRoofSignRingSpec roof;
    roof.sides = 8;
    roof.inner_radius_fraction = 0.42f;

    StaticSidewalkRingSpec sidewalk;
    sidewalk.sides = 8;
    sidewalk.inner_radius_fraction = 0.76f;
    sidewalk.color = glm::vec3(0.5f, 0.55f, 0.6f);

    CHECK(cache.roof_sign_ring(roof).get() == cache.roof_sign_ring(roof).get());
    CHECK(cache.sidewalk_ring(sidewalk).get() == cache.sidewalk_ring(sidewalk).get());
    CHECK(cache.size() == 2);

    const MeshBounds roof_bounds = bounds_of(*cache.roof_sign_ring(roof));
    CHECK(roof_bounds.min.y == Catch::Approx(-0.5f).margin(0.001f));
    CHECK(roof_bounds.max.y == Catch::Approx(0.5f).margin(0.001f));

    const MeshBounds sidewalk_bounds = bounds_of(*cache.sidewalk_ring(sidewalk));
    CHECK(sidewalk_bounds.max.x == Catch::Approx(0.5f).margin(0.001f));
    CHECK(sidewalk_bounds.max.y == Catch::Approx(1.0f).margin(0.001f));
}

TEST_CASE("scene snapshot scales reusable custom building meshes from building metrics", "[megacity][static-mesh-family]")
{
    StaticMeshFamilyCache cache;
    StaticBuildingRingSpec spec;
    spec.sides = 4;
    spec.layers = { { 1.0f, 1.0f, 0u } };
    const auto mesh = cache.building_ring(spec);

    CodeVizSceneWorld world;
    const BuildingMetrics metrics{
        .footprint = 8.0f,
        .height = 6.0f,
        .sidewalk_width = 1.0f,
        .road_width = 2.0f,
    };
    world.create_building(
        10.0f,
        20.0f,
        2.0f,
        metrics,
        glm::vec4(0.4f, 0.6f, 0.8f, 1.0f),
        CodeVizSemanticRef{ "src/app.cpp", "App", "src" },
        MaterialId::FlatColor,
        mesh,
        0.0f,
        CustomMeshTransformMode::ScaleByBlockMetrics);
    world.create_building(
        -10.0f,
        -20.0f,
        1.0f,
        metrics,
        glm::vec4(0.8f, 0.6f, 0.4f, 1.0f),
        CodeVizSemanticRef{ "src/app.cpp", "Other", "src" },
        MaterialId::FlatColor,
        mesh,
        0.0f,
        CustomMeshTransformMode::ScaleByBlockMetrics);

    IsometricCamera camera;
    camera.look_at_world_center(0.0f, 0.0f);
    camera.set_viewport(800, 600);
    MegaCityCodeConfig config;

    const CodeVizSceneSnapshotResult result = build_scene_snapshot(camera, world, config, {}, {}, {}, {});

    REQUIRE(result.snapshot.objects.size() == 2);
    REQUIRE(result.snapshot.custom_meshes.size() == 1);
    CHECK(result.snapshot.objects[0].custom_mesh_index == result.snapshot.objects[1].custom_mesh_index);

    const CodeVizRenderable& object = result.snapshot.objects[0].source_name == "App"
        ? result.snapshot.objects[0]
        : result.snapshot.objects[1];
    const glm::vec3 scaled_top = glm::vec3(object.world * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    const glm::vec3 scaled_edge = glm::vec3(object.world * glm::vec4(0.5f, 0.0f, 0.0f, 1.0f));

    CHECK(scaled_top.y == Catch::Approx(8.0f).margin(0.001f));
    CHECK(scaled_edge.x == Catch::Approx(14.0f).margin(0.001f));
}

#endif
