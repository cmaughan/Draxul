#include "satview_constellation_boundary_catalog.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <set>

using namespace draxul::satview;

TEST_CASE("SatView loads the pinned constellation boundary and name catalog", "[satview][constellations]")
{
    const SatViewConstellationBoundaryCatalog catalog =
        load_satview_constellation_boundary_catalog();

    REQUIRE(catalog.segments.size() > 1'000);
    REQUIRE(catalog.labels.size() == 89);
    std::set<std::string> designations;
    std::vector<std::string> serpens_names;
    for (const SatViewConstellationBoundarySegment& segment : catalog.segments)
    {
        CHECK(glm::length(segment.start_direction) == Catch::Approx(1.0f).margin(0.0001f));
        CHECK(glm::length(segment.end_direction) == Catch::Approx(1.0f).margin(0.0001f));
        const float angle = std::acos(std::clamp(
            glm::dot(segment.start_direction, segment.end_direction), -1.0f, 1.0f));
        CHECK(angle <= glm::radians(1.001f));
    }
    for (const SatViewConstellationAreaLabel& label : catalog.labels)
    {
        CHECK(glm::length(label.direction) == Catch::Approx(1.0f).margin(0.0001f));
        CHECK(label.rank >= 1);
        CHECK(label.rank <= 3);
        CHECK(label.designation.size() == 3);
        CHECK_FALSE(label.name.empty());
        designations.insert(label.designation);
        if (label.designation == "Ser")
            serpens_names.push_back(label.name);
    }
    CHECK(designations.size() == 88);
    CHECK(serpens_names == std::vector<std::string>{ "Serpens Caput", "Serpens Cauda" });
}

TEST_CASE("SatView rejects a truncated constellation boundary catalog", "[satview][constellations]")
{
    const auto path = std::filesystem::temp_directory_path()
        / "draxul-satview-truncated-constellation-boundaries.dxbnd";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write("DXCBND01", 8);
    }
    const SatViewConstellationBoundaryCatalog catalog =
        load_satview_constellation_boundary_catalog(path);
    std::error_code error;
    std::filesystem::remove(path, error);

    CHECK(catalog.segments.empty());
    CHECK(catalog.labels.empty());
}
