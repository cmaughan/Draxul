#include "satview_constellation_catalog.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/geometric.hpp>

using namespace draxul::satview;

TEST_CASE("SatView loads the pinned constellation line catalog", "[satview][constellations]")
{
    const std::vector<SatViewSceneVertex> vertices = load_satview_constellation_catalog();

    REQUIRE(vertices.size() == 656u * 2u);
    for (std::size_t index = 0; index < vertices.size(); index += 2u)
    {
        const SatViewSceneVertex& start = vertices[index];
        const SatViewSceneVertex& end = vertices[index + 1u];
        CHECK(glm::length(glm::vec3(start.position)) == Catch::Approx(1.0f).margin(0.0001f));
        CHECK(glm::length(glm::vec3(end.position)) == Catch::Approx(1.0f).margin(0.0001f));
        CHECK(glm::vec3(start.paired_position) == glm::vec3(end.position));
        CHECK(glm::vec3(end.paired_position) == glm::vec3(start.position));
        CHECK(start.color.a > 0.0f);
        CHECK(start.color.a < 1.0f);
    }
}
