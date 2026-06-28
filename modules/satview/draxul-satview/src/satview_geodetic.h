#pragma once

#include <glm/glm.hpp>

namespace draxul::satview
{

struct SatViewGeodeticPosition
{
    double latitude_degrees = 0.0;
    double longitude_degrees = 0.0;
    double altitude_km = 0.0;
};

[[nodiscard]] SatViewGeodeticPosition satview_geodetic_from_ecef(
    const glm::dvec3& ecef_position_km);

} // namespace draxul::satview
