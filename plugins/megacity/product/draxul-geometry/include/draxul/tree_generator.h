#pragma once

#include <draxul/geometry_mesh.h>

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace draxul
{

struct DraxulTreeParams
{
    uint64_t seed = 1;

    float age_years = 8.0f;
    float overall_scale = 1.0f;
    float max_height = 18.0f;
    float max_canopy_radius = 7.0f;

    int radial_segments = 10;

    float trunk_length = 5.0f;
    float trunk_base_radius = 0.35f;
    float trunk_tip_radius = 0.18f;
    float base_ring_spacing = 0.45f;

    float branch_length_scale = 0.72f;
    float branch_radius_scale = 0.68f;
    int max_branch_depth = 3;

    int child_branches_min = 1;
    int child_branches_max = 3;

    float branch_spawn_start = 0.25f;
    float branch_spawn_end = 0.9f;

    float upward_bias = 0.35f;
    float outward_bias = 0.75f;
    float droop_bias = 0.05f;

    float curvature = 0.12f;
    float trunk_wander = 0.12f;
    float branch_wander = 0.28f;
    float wander_frequency = 0.22f;
    float wander_deviation = 0.45f;
    float leaf_density = 1.0f;
    float leaf_orientation_randomness = 0.35f;
    glm::vec2 leaf_size_range{ 3.6f, 5.2f };
    int leaf_start_depth = 1;
    float taper_power = 1.25f;
    float tip_ring_spacing_scale = 0.5f;

    glm::vec3 bark_color_root{ 0.22f, 0.15f, 0.10f };
    glm::vec3 bark_color_tip{ 0.42f, 0.30f, 0.20f };
    float bark_color_noise = 0.08f;
};

struct DraxulTreeMeshes
{
    GeometryMesh bark_mesh;
    GeometryMesh leaf_mesh;
};

// High-level age dial for later presets. The current scaffold maps age to a
// bounded trunk-first parameter set.
[[nodiscard]] DraxulTreeParams make_tree_params_from_age(float age_years);

// Split bark/leaves output for renderers that want independent material or
// raster state.
[[nodiscard]] DraxulTreeMeshes generate_draxul_tree_meshes(const DraxulTreeParams& params);

// Convenience merged tree mesh. This combines bark and leaves into a single
// mesh for simpler consumers and tests.
[[nodiscard]] GeometryMesh generate_draxul_tree(const DraxulTreeParams& params);

} // namespace draxul
