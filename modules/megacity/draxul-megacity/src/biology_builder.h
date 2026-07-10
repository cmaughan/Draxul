#pragma once

#include <draxul/code_semantic_model.h>
#include <draxul/megacity_code_config.h>

#include <cstddef>

namespace draxul
{

class CodeVizSceneWorld;

struct BiologyBuildStats
{
    size_t module_tissue_count = 0; // modules rendered as tissue territories
    size_t cell_count = 0; // types rendered as cells
    size_t full_cell_count = 0; // cells with full organelle detail
    size_t vessel_count = 0; // inter-module dependency blood vessels
    size_t mitochondria_count = 0; // methods (across full cells)
    size_t ribosome_count = 0; // fields (across full cells)
};

struct BiologyBuildResult
{
    BiologyBuildStats stats;
    // Human-readable label for the dominant cell/tissue (the most significant
    // class), or empty when no semantic subject was found.
    std::string subject_label;
    bool mapped_from_semantics = false;
    bool bounds_valid = false;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
    bool computed_default_light = false;
    float default_light_x = 0.0f;
    float default_light_y = 0.0f;
    float default_light_z = 0.0f;
    float default_light_radius = 0.0f;
};

BiologyBuildResult build_biology_view(
    CodeVizSceneWorld& world,
    const CodeSemanticSnapshot& semantics,
    const MegaCityCodeConfig& config);

} // namespace draxul
