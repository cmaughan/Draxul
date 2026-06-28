#include "satview_moon_ephemeris.h"

#include <cmath>
#include <draxul/satview/satview_propagation.h>
#include <numbers>

namespace draxul::satview
{

namespace
{

double radians(double degrees)
{
    return std::remainder(degrees, 360.0) * std::numbers::pi_v<double> / 180.0;
}

} // namespace

SatViewMoonPosition satview_moon_position(double unix_seconds)
{
    const double julian_centuries =
        (julian_date_from_unix_seconds(unix_seconds).value() - 2451545.0) / 36525.0;
    const double t2 = julian_centuries * julian_centuries;
    const double t3 = t2 * julian_centuries;
    const double t4 = t3 * julian_centuries;

    const double mean_longitude = radians(
        218.3164477 + 481267.88123421 * julian_centuries - 0.0015786 * t2
        + t3 / 538841.0 - t4 / 65194000.0);
    const double elongation = radians(
        297.8501921 + 445267.1114034 * julian_centuries - 0.0018819 * t2
        + t3 / 545868.0 - t4 / 113065000.0);
    const double solar_anomaly = radians(
        357.5291092 + 35999.0502909 * julian_centuries - 0.0001536 * t2
        + t3 / 24490000.0);
    const double lunar_anomaly = radians(
        134.9633964 + 477198.8675055 * julian_centuries + 0.0087414 * t2
        + t3 / 69699.0 - t4 / 14712000.0);
    const double argument_of_latitude = radians(
        93.2720950 + 483202.0175233 * julian_centuries - 0.0036539 * t2
        - t3 / 3526000.0 + t4 / 863310000.0);
    const double eccentricity = 1.0 - 0.002516 * julian_centuries - 0.0000074 * t2;

    const double longitude_correction_degrees =
        6.288774 * std::sin(lunar_anomaly)
        + 1.274027 * std::sin(2.0 * elongation - lunar_anomaly)
        + 0.658314 * std::sin(2.0 * elongation)
        + 0.213618 * std::sin(2.0 * lunar_anomaly)
        - 0.185116 * eccentricity * std::sin(solar_anomaly)
        - 0.114332 * std::sin(2.0 * argument_of_latitude)
        + 0.058793 * std::sin(2.0 * elongation - 2.0 * lunar_anomaly)
        + 0.057066 * eccentricity
            * std::sin(2.0 * elongation - solar_anomaly - lunar_anomaly)
        + 0.053322 * std::sin(2.0 * elongation + lunar_anomaly)
        + 0.045758 * eccentricity * std::sin(2.0 * elongation - solar_anomaly)
        - 0.040923 * eccentricity * std::sin(solar_anomaly - lunar_anomaly)
        - 0.034720 * std::sin(elongation)
        - 0.030383 * eccentricity * std::sin(solar_anomaly + lunar_anomaly)
        + 0.015327 * std::sin(2.0 * elongation - 2.0 * argument_of_latitude)
        - 0.012528 * std::sin(lunar_anomaly + 2.0 * argument_of_latitude)
        + 0.010980 * std::sin(lunar_anomaly - 2.0 * argument_of_latitude)
        + 0.010675 * std::sin(4.0 * elongation - lunar_anomaly)
        + 0.010034 * std::sin(3.0 * lunar_anomaly)
        + 0.008548 * std::sin(4.0 * elongation - 2.0 * lunar_anomaly);

    const double latitude_degrees =
        5.128122 * std::sin(argument_of_latitude)
        + 0.280602 * std::sin(lunar_anomaly + argument_of_latitude)
        + 0.277693 * std::sin(lunar_anomaly - argument_of_latitude)
        + 0.173237 * std::sin(2.0 * elongation - argument_of_latitude)
        + 0.055413 * std::sin(2.0 * elongation - lunar_anomaly + argument_of_latitude)
        + 0.046271 * std::sin(2.0 * elongation - lunar_anomaly - argument_of_latitude)
        + 0.032573 * std::sin(2.0 * elongation + argument_of_latitude)
        + 0.017198 * std::sin(2.0 * lunar_anomaly + argument_of_latitude)
        + 0.009267 * std::sin(2.0 * elongation + lunar_anomaly - argument_of_latitude)
        + 0.008823 * std::sin(2.0 * lunar_anomaly - argument_of_latitude);

    const double distance_km =
        385000.56
        - 20905.355 * std::cos(lunar_anomaly)
        - 3699.111 * std::cos(2.0 * elongation - lunar_anomaly)
        - 2955.968 * std::cos(2.0 * elongation)
        - 569.925 * std::cos(2.0 * lunar_anomaly)
        + 246.158 * std::cos(2.0 * lunar_anomaly - 2.0 * elongation)
        - 204.586 * eccentricity * std::cos(solar_anomaly - 2.0 * elongation)
        - 170.733 * std::cos(lunar_anomaly + 2.0 * elongation)
        - 152.137 * eccentricity
            * std::cos(lunar_anomaly + solar_anomaly - 2.0 * elongation)
        - 129.620 * std::cos(lunar_anomaly - 2.0 * elongation)
        + 108.743 * std::cos(elongation);

    const double longitude = mean_longitude + radians(longitude_correction_degrees);
    const double latitude = radians(latitude_degrees);
    const double cos_latitude = std::cos(latitude);
    const glm::dvec3 ecliptic_position_km = distance_km * glm::dvec3(
        cos_latitude * std::cos(longitude),
        cos_latitude * std::sin(longitude),
        std::sin(latitude));

    const double obliquity = radians(
        23.439291111 - 0.013004167 * julian_centuries
        - 0.000000164 * t2 + 0.000000504 * t3);
    const glm::dvec3 equatorial_position_km(
        ecliptic_position_km.x,
        ecliptic_position_km.y * std::cos(obliquity)
            - ecliptic_position_km.z * std::sin(obliquity),
        ecliptic_position_km.y * std::sin(obliquity)
            + ecliptic_position_km.z * std::cos(obliquity));

    return {
        equatorial_position_km,
        teme_position_to_render_earth_radii(equatorial_position_km),
    };
}

} // namespace draxul::satview
