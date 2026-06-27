#include "satview_object_style.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <draxul/pastel_palette.h>

using draxul::kPastelAccentPalette;
using draxul::satview::kSatelliteAccentPalette;
using draxul::satview::normalized_satellite_prefix;
using draxul::satview::satellite_prefix_color;
using draxul::satview::satellite_prefix_hash;

TEST_CASE("SatView normalizes satellite names into stable tree prefixes", "[satview][style]")
{
    CHECK(normalized_satellite_prefix("STARLINK-1234") == "STARLINK");
    CHECK(normalized_satellite_prefix(" starlink 5678 ") == "STARLINK");
    CHECK(normalized_satellite_prefix("GPS BIIR-2 (PRN 13)") == "GPS BIIR");
    CHECK(normalized_satellite_prefix("ISS (ZARYA)") == "ISS");
    CHECK(normalized_satellite_prefix("12345") == "Other");
    CHECK(normalized_satellite_prefix("NAMEONLY") == "Other");
}

TEST_CASE("SatView assigns one pastel color to each normalized prefix", "[satview][style]")
{
    const std::uint32_t dashed = satellite_prefix_hash("STARLINK-1234");
    const std::uint32_t spaced = satellite_prefix_hash("starlink 5678");
    CHECK(dashed == spaced);

    const glm::vec4 dashed_color = satellite_prefix_color(dashed, 0.7f);
    const glm::vec4 spaced_color = satellite_prefix_color(spaced, 0.7f);
    CHECK(dashed_color.r == spaced_color.r);
    CHECK(dashed_color.g == spaced_color.g);
    CHECK(dashed_color.b == spaced_color.b);
    CHECK(dashed_color.a == spaced_color.a);

    CHECK(dashed_color.g > dashed_color.r);
    CHECK(dashed_color.b > dashed_color.r);
}

TEST_CASE("Shared pastel accents stay bright enough for dark scenes", "[satview][style]")
{
    for (const glm::vec4& color : kPastelAccentPalette)
    {
        CHECK(std::min({ color.r, color.g, color.b }) >= 0.45f);
        CHECK(color.a == 1.0f);
    }

    for (const glm::vec4& color : kSatelliteAccentPalette)
    {
        CHECK(std::min({ color.r, color.g, color.b }) >= 0.45f);
        CHECK(color.a == 1.0f);
    }
}
