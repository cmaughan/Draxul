#include "satview_geodetic.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>

namespace
{

using Catch::Matchers::WithinAbs;
using draxul::satview::SatViewGeodeticPosition;
using draxul::satview::satview_geodetic_from_ecef;

constexpr double kEquatorialRadiusKm = 6378.137;
constexpr double kFlattening = 1.0 / 298.257223563;
constexpr double kPolarRadiusKm = kEquatorialRadiusKm * (1.0 - kFlattening);

TEST_CASE("SatView converts equatorial ECEF positions to geodetic coordinates", "[satview][geodetic]")
{
    const SatViewGeodeticPosition greenwich =
        satview_geodetic_from_ecef(glm::dvec3(kEquatorialRadiusKm, 0.0, 0.0));
    CHECK_THAT(greenwich.latitude_degrees, WithinAbs(0.0, 1.0e-10));
    CHECK_THAT(greenwich.longitude_degrees, WithinAbs(0.0, 1.0e-10));
    CHECK_THAT(greenwich.altitude_km, WithinAbs(0.0, 1.0e-10));

    const SatViewGeodeticPosition east =
        satview_geodetic_from_ecef(glm::dvec3(0.0, kEquatorialRadiusKm, 0.0));
    CHECK_THAT(east.latitude_degrees, WithinAbs(0.0, 1.0e-10));
    CHECK_THAT(east.longitude_degrees, WithinAbs(90.0, 1.0e-10));
}

TEST_CASE("SatView converts polar ECEF positions to geodetic coordinates", "[satview][geodetic]")
{
    const SatViewGeodeticPosition north =
        satview_geodetic_from_ecef(glm::dvec3(0.0, 0.0, kPolarRadiusKm));
    CHECK_THAT(north.latitude_degrees, WithinAbs(90.0, 1.0e-10));
    CHECK_THAT(north.longitude_degrees, WithinAbs(0.0, 1.0e-10));
    CHECK_THAT(north.altitude_km, WithinAbs(0.0, 1.0e-10));
}

TEST_CASE("SatView recovers an arbitrary WGS-84 location", "[satview][geodetic]")
{
    constexpr double kLatitudeDegrees = 45.0;
    constexpr double kLongitudeDegrees = -73.0;
    constexpr double kAltitudeKm = 550.0;
    constexpr double kFirstEccentricitySquared = kFlattening * (2.0 - kFlattening);
    constexpr double kDegreesToRadians = std::numbers::pi_v<double> / 180.0;
    const double latitude_radians = kLatitudeDegrees * kDegreesToRadians;
    const double longitude_radians = kLongitudeDegrees * kDegreesToRadians;
    const double sin_latitude = std::sin(latitude_radians);
    const double prime_vertical_radius_km = kEquatorialRadiusKm
        / std::sqrt(1.0 - kFirstEccentricitySquared * sin_latitude * sin_latitude);
    const glm::dvec3 ecef(
        (prime_vertical_radius_km + kAltitudeKm) * std::cos(latitude_radians)
            * std::cos(longitude_radians),
        (prime_vertical_radius_km + kAltitudeKm) * std::cos(latitude_radians)
            * std::sin(longitude_radians),
        (prime_vertical_radius_km * (1.0 - kFirstEccentricitySquared) + kAltitudeKm)
            * sin_latitude);

    const SatViewGeodeticPosition position = satview_geodetic_from_ecef(ecef);
    CHECK_THAT(position.latitude_degrees, WithinAbs(kLatitudeDegrees, 1.0e-8));
    CHECK_THAT(position.longitude_degrees, WithinAbs(kLongitudeDegrees, 1.0e-10));
    CHECK_THAT(position.altitude_km, WithinAbs(kAltitudeKm, 1.0e-6));
}

} // namespace
