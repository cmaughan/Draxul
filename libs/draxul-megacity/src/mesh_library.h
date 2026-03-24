#pragma once

#include "isometric_scene_types.h"

namespace draxul
{

MeshData build_unit_cube_mesh();
MeshData build_grid_mesh(int width, int height, float tile_size);

} // namespace draxul
