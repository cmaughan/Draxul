#pragma once

#include <draxul/codeviz_scene_types.h>

namespace draxul
{

MeshData build_unit_cube_mesh();
MeshData build_floor_box_mesh();
MeshData build_foliage_stem_mesh();
MeshData build_foliage_card_mesh();
MeshData build_textured_surface_mesh();
MeshData build_top_label_panel_mesh();
MeshData build_front_label_panel_mesh();
MeshData build_grid_mesh(int width, int height, float tile_size);
MeshData build_outline_grid_mesh(const FloorGridSpec& spec);

} // namespace draxul
