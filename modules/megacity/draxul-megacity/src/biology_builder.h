#pragma once

#include <draxul/code_semantic_model.h>
#include <draxul/megacity_code_config.h>

#include <cstddef>

namespace draxul
{

class SceneWorld;

struct BiologyBuildStats
{
    size_t tissue_count = 0;
    size_t file_cell_count = 0;
    size_t symbol_body_count = 0;
    size_t organelle_count = 0;
    size_t fibre_count = 0;
};

struct BiologyBuildResult
{
    BiologyBuildStats stats;
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
    SceneWorld& world,
    const CodeSemanticSnapshot& semantics,
    const MegaCityCodeConfig& config);

} // namespace draxul
