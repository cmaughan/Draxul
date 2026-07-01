#pragma once

#include "codeviz_scene_types.h"

namespace draxul
{

MeshData build_unit_cube_mesh();
MeshData build_floor_box_mesh();
MeshData build_tree_bark_mesh();
MeshData build_tree_leaf_mesh();
MeshData build_road_surface_mesh();
MeshData build_roof_sign_mesh();
MeshData build_wall_sign_mesh();
MeshData build_grid_mesh(int width, int height, float tile_size);
MeshData build_outline_grid_mesh(const FloorGridSpec& spec);

} // namespace draxul
