#include "satview_moon_ephemeris.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <cmath>
#include <draxul/satview/satview_propagation.h>
#include <glm/geometric.hpp>
#include <numbers>

namespace
{

using Catch::Matchers::WithinAbs;
using draxul::satview::kSatViewEarthEquatorialRadiusKm;
using draxul::satview::satview_moon_position;

TEST_CASE("SatView Moon ephemeris agrees with a JPL Horizons reference vector", "[satview][moon]")
{
    constexpr double kMarchEquinoxNoonUtc = 1710936000.0;
    const glm::dvec3 horizons_icrf_km(
        -246450.8602314188,
        276460.3671206749,
        157023.9968924827);

    const auto moon = satview_moon_position(kMarchEquinoxNoonUtc);
    const double angular_error_radians = std::acos(std::clamp(
        glm::dot(
            glm::normalize(moon.equatorial_position_km),
            glm::normalize(horizons_icrf_km)),
        -1.0,
        1.0));
    CHECK(angular_error_radians * 180.0 / std::numbers::pi_v<double> < 1.0);
    CHECK_THAT(
        glm::length(moon.equatorial_position_km),
        WithinAbs(glm::length(horizons_icrf_km), 1500.0));
}

TEST_CASE("SatView Moon render position uses Earth-radius scene units", "[satview][moon]")
{
    const auto moon = satview_moon_position(1710936000.0);
    CHECK_THAT(
        glm::length(moon.render_position_earth_radii),
        WithinAbs(
            glm::length(moon.equatorial_position_km) / kSatViewEarthEquatorialRadiusKm,
            1.0e-10));
    CHECK(glm::length(moon.render_position_earth_radii) > 55.0);
    CHECK(glm::length(moon.render_position_earth_radii) < 65.0);
}

} // namespace
