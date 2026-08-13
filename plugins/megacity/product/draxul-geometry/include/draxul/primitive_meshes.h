#pragma once

#include <draxul/geometry_mesh.h>

namespace draxul
{

[[nodiscard]] GeometryMesh build_unit_cube_geometry();
[[nodiscard]] GeometryMesh build_unit_uv_sphere_geometry(int latitude_segments = 16, int longitude_segments = 32);

} // namespace draxul
