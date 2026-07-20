#include <draxul/satview/satview_geodetic.h>

#include <cmath>
#include <numbers>

namespace draxul::satview
{

SatViewGeodeticPosition satview_geodetic_from_ecef(const glm::dvec3& ecef_position_km)
{
    constexpr double kEquatorialRadiusKm = 6378.137;
    constexpr double kFlattening = 1.0 / 298.257223563;
    constexpr double kPolarRadiusKm = kEquatorialRadiusKm * (1.0 - kFlattening);
    constexpr double kFirstEccentricitySquared = kFlattening * (2.0 - kFlattening);
    constexpr double kSecondEccentricitySquared = (kEquatorialRadiusKm * kEquatorialRadiusKm - kPolarRadiusKm * kPolarRadiusKm)
        / (kPolarRadiusKm * kPolarRadiusKm);
    constexpr double kRadiansToDegrees = 180.0 / std::numbers::pi_v<double>;

    const double distance_from_axis_km = std::hypot(ecef_position_km.x, ecef_position_km.y);
    const double longitude_radians = std::atan2(ecef_position_km.y, ecef_position_km.x);

    if (distance_from_axis_km < 1.0e-12)
    {
        const double latitude_degrees = ecef_position_km.z < 0.0 ? -90.0 : 90.0;
        return {
            latitude_degrees,
            0.0,
            std::abs(ecef_position_km.z) - kPolarRadiusKm,
        };
    }

    const double auxiliary_angle = std::atan2(
        ecef_position_km.z * kEquatorialRadiusKm,
        distance_from_axis_km * kPolarRadiusKm);
    const double sin_auxiliary = std::sin(auxiliary_angle);
    const double cos_auxiliary = std::cos(auxiliary_angle);
    double latitude_radians = std::atan2(
        ecef_position_km.z
            + kSecondEccentricitySquared * kPolarRadiusKm * sin_auxiliary
                * sin_auxiliary * sin_auxiliary,
        distance_from_axis_km
            - kFirstEccentricitySquared * kEquatorialRadiusKm * cos_auxiliary
                * cos_auxiliary * cos_auxiliary);

    for (int iteration = 0; iteration < 3; ++iteration)
    {
        const double sin_latitude = std::sin(latitude_radians);
        const double prime_vertical_radius_km = kEquatorialRadiusKm
            / std::sqrt(1.0 - kFirstEccentricitySquared * sin_latitude * sin_latitude);
        latitude_radians = std::atan2(
            ecef_position_km.z
                + kFirstEccentricitySquared * prime_vertical_radius_km * sin_latitude,
            distance_from_axis_km);
    }

    const double sin_latitude = std::sin(latitude_radians);
    const double prime_vertical_radius_km = kEquatorialRadiusKm
        / std::sqrt(1.0 - kFirstEccentricitySquared * sin_latitude * sin_latitude);
    const double altitude_km = distance_from_axis_km / std::cos(latitude_radians) - prime_vertical_radius_km;

    return {
        latitude_radians * kRadiansToDegrees,
        longitude_radians * kRadiansToDegrees,
        altitude_km,
    };
}

} // namespace draxul::satview
