#include <draxul/satview/satview_constellation_catalog.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/geometric.hpp>

using namespace draxul::satview;

TEST_CASE("SatView loads the pinned constellation line catalog", "[satview][constellations]")
{
    const std::vector<SatViewCelestialLineInstance> lines = load_satview_constellation_catalog();

    REQUIRE(lines.size() == 656u);
    for (const SatViewCelestialLineInstance& line : lines)
    {
        CHECK(glm::length(glm::vec3(line.start_direction_width))
            == Catch::Approx(1.0f).margin(0.0001f));
        CHECK(glm::length(glm::vec3(line.end_direction_dash))
            == Catch::Approx(1.0f).margin(0.0001f));
        CHECK(line.start_direction_width.w > 0.0f);
        CHECK(line.end_direction_dash.w == 0.0f);
        CHECK(line.style.x == 0.0f);
        CHECK(line.color.a > 0.0f);
        CHECK(line.color.a < 1.0f);
    }
}
