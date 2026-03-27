#include "semantic_city_layout.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <numeric>
#include <string_view>
#include <vector>

namespace draxul
{

namespace
{

float snap_to_grid(float value, float grid_step)
{
    return std::round(value / grid_step) * grid_step;
}

bool path_has_prefix(std::string_view path, std::string_view prefix)
{
    return path.rfind(prefix, 0) == 0;
}

int function_mass(const CityClassRecord& row)
{
    int mass = 0;
    for (const int size : row.function_sizes)
        mass += size;
    return mass;
}

std::vector<SemanticBuildingLayer> build_function_layers(const CityClassRecord& row, const BuildingMetrics& metrics)
{
    std::vector<int> sizes;
    sizes.reserve(row.function_sizes.size());
    for (const int size : row.function_sizes)
    {
        if (size > 0)
            sizes.push_back(size);
    }

    if (sizes.empty())
        return { { 0, metrics.height } };

    const int total_size = std::accumulate(sizes.begin(), sizes.end(), 0);
    if (total_size <= 0)
        return { { 0, metrics.height } };

    std::vector<SemanticBuildingLayer> layers;
    layers.reserve(sizes.size());

    float remaining_height = metrics.height;
    for (size_t index = 0; index < sizes.size(); ++index)
    {
        const int function_size = sizes[index];
        float layer_height = metrics.height;
        if (index + 1 < sizes.size())
        {
            layer_height = metrics.height * static_cast<float>(function_size) / static_cast<float>(total_size);
            layer_height = std::min(layer_height, remaining_height);
        }
        else
        {
            layer_height = remaining_height;
        }

        layers.push_back({ function_size, std::max(layer_height, 0.0f) });
        remaining_height = std::max(0.0f, remaining_height - layer_height);
    }

    if (!layers.empty())
    {
        float total_height = 0.0f;
        for (const SemanticBuildingLayer& layer : layers)
            total_height += layer.height;
        const float error = metrics.height - total_height;
        layers.back().height += error;
    }

    return layers;
}

struct LotRect
{
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
};

LotRect centered_building_lot(const BuildingMetrics& metrics, const MegaCityCodeConfig& config)
{
    const float step = std::max(config.placement_step, 0.01f);
    const float raw_half_extent = metrics.footprint * 0.5f + metrics.sidewalk_width + metrics.road_width;
    const float half_extent = std::max(step, snap_to_grid(raw_half_extent, step));
    return {
        -half_extent,
        half_extent,
        -half_extent,
        half_extent,
    };
}

LotRect translate_lot(const LotRect& local_lot, const glm::vec2& offset)
{
    return {
        local_lot.min_x + offset.x,
        local_lot.max_x + offset.x,
        local_lot.min_z + offset.y,
        local_lot.max_z + offset.y,
    };
}

bool overlaps(const LotRect& a, const LotRect& b)
{
    return a.min_x < b.max_x && a.max_x > b.min_x && a.min_z < b.max_z && a.max_z > b.min_z;
}

float lot_center_x(const LotRect& lot)
{
    return (lot.min_x + lot.max_x) * 0.5f;
}

float lot_center_z(const LotRect& lot)
{
    return (lot.min_z + lot.max_z) * 0.5f;
}

float lot_half_extent_x(const LotRect& lot)
{
    return (lot.max_x - lot.min_x) * 0.5f;
}

float lot_half_extent_z(const LotRect& lot)
{
    return (lot.max_z - lot.min_z) * 0.5f;
}

template <typename Fn>
bool for_each_spiral_candidate(const MegaCityCodeConfig& config, Fn&& fn)
{
    const float placement_step = std::max(config.placement_step, 0.01f);
    const int max_spiral_rings = std::max(config.max_spiral_rings, 1);
    if (!fn(glm::vec2(0.0f)))
        return false;

    for (int ring = 1; ring < max_spiral_rings; ++ring)
    {
        const float radius = static_cast<float>(ring) * placement_step;
        for (int ix = -ring; ix <= ring; ++ix)
        {
            if (!fn(glm::vec2(static_cast<float>(ix) * placement_step, -radius)))
                return false;
        }
        for (int iz = -ring + 1; iz <= ring; ++iz)
        {
            if (!fn(glm::vec2(radius, static_cast<float>(iz) * placement_step)))
                return false;
        }
        for (int ix = ring - 1; ix >= -ring; --ix)
        {
            if (!fn(glm::vec2(static_cast<float>(ix) * placement_step, radius)))
                return false;
        }
        for (int iz = ring - 1; iz >= -ring + 1; --iz)
        {
            if (!fn(glm::vec2(-radius, static_cast<float>(iz) * placement_step)))
                return false;
        }
    }

    return true;
}

void push_candidate(std::vector<glm::vec2>& candidates, const glm::vec2& candidate)
{
    constexpr float kDuplicateEpsilon = 1e-4f;
    const bool duplicate = std::any_of(candidates.begin(), candidates.end(), [&](const glm::vec2& existing) {
        return std::abs(existing.x - candidate.x) <= kDuplicateEpsilon
            && std::abs(existing.y - candidate.y) <= kDuplicateEpsilon;
    });
    if (!duplicate)
        candidates.push_back(candidate);
}

void add_contact_candidates_for_side(
    std::vector<glm::vec2>& candidates, float fixed_axis, float center_axis, float overlap_limit, bool fixed_x,
    const MegaCityCodeConfig& config)
{
    constexpr int kMaxEdgeSamplesPerSide = 24;
    push_candidate(candidates, fixed_x ? glm::vec2(fixed_axis, center_axis) : glm::vec2(center_axis, fixed_axis));

    if (overlap_limit <= 0.0f)
        return;

    const float placement_step = std::max(config.placement_step, 0.01f);
    const int desired_samples = std::max(1, static_cast<int>(std::ceil(overlap_limit / placement_step)));
    const int sample_count = std::min(desired_samples, kMaxEdgeSamplesPerSide);

    for (int sample = 1; sample <= sample_count; ++sample)
    {
        const float t = static_cast<float>(sample) / static_cast<float>(sample_count + 1);
        const float offset = t * overlap_limit;
        push_candidate(candidates,
            fixed_x ? glm::vec2(fixed_axis, center_axis + offset) : glm::vec2(center_axis + offset, fixed_axis));
        push_candidate(candidates,
            fixed_x ? glm::vec2(fixed_axis, center_axis - offset) : glm::vec2(center_axis - offset, fixed_axis));
    }
}

std::vector<glm::vec2> touching_lot_candidates(
    const std::vector<LotRect>& reserved_lots, const LotRect& local_lot, const MegaCityCodeConfig& config)
{
    std::vector<glm::vec2> candidates;
    if (reserved_lots.empty())
    {
        candidates.push_back(glm::vec2(0.0f));
        return candidates;
    }

    const float local_center_x = lot_center_x(local_lot);
    const float local_center_z = lot_center_z(local_lot);
    const float local_half_extent_x = lot_half_extent_x(local_lot);
    const float local_half_extent_z = lot_half_extent_z(local_lot);
    for (const LotRect& occupied : reserved_lots)
    {
        const float center_x = lot_center_x(occupied);
        const float center_z = lot_center_z(occupied);
        const float overlap_limit_z = lot_half_extent_z(occupied) + local_half_extent_z;
        const float overlap_limit_x = lot_half_extent_x(occupied) + local_half_extent_x;

        add_contact_candidates_for_side(
            candidates, occupied.min_x - local_lot.max_x, center_z - local_center_z, overlap_limit_z, true, config);
        add_contact_candidates_for_side(
            candidates, occupied.max_x - local_lot.min_x, center_z - local_center_z, overlap_limit_z, true, config);
        add_contact_candidates_for_side(
            candidates, occupied.min_z - local_lot.max_z, center_x - local_center_x, overlap_limit_x, false, config);
        add_contact_candidates_for_side(
            candidates, occupied.max_z - local_lot.min_z, center_x - local_center_x, overlap_limit_x, false, config);
    }

    std::sort(candidates.begin(), candidates.end(), [](const glm::vec2& a, const glm::vec2& b) {
        const float radius_a = glm::dot(a, a);
        const float radius_b = glm::dot(b, b);
        if (radius_a != radius_b)
            return radius_a < radius_b;

        float angle_a = std::atan2(a.y, a.x);
        float angle_b = std::atan2(b.y, b.x);
        if (angle_a < 0.0f)
            angle_a += 2.0f * std::numbers::pi_v<float>;
        if (angle_b < 0.0f)
            angle_b += 2.0f * std::numbers::pi_v<float>;
        return angle_a < angle_b;
    });

    return candidates;
}

bool try_place_candidate(
    const std::vector<LotRect>& reserved_lots,
    const LotRect& local_lot,
    const glm::vec2& offset,
    glm::vec2& chosen_offset,
    LotRect& chosen_lot)
{
    const LotRect lot = translate_lot(local_lot, offset);
    const bool collides = std::any_of(reserved_lots.begin(), reserved_lots.end(), [&](const LotRect& occupied) {
        return overlaps(lot, occupied);
    });
    if (collides)
        return false;

    chosen_offset = offset;
    chosen_lot = lot;
    return true;
}

} // namespace

BuildingMetrics derive_building_metrics(const CityClassRecord& row, const MegaCityCodeConfig& config)
{
    const float base = static_cast<float>(std::max(row.base_size, 0));
    const float mass = static_cast<float>(std::max(function_mass(row), 0));
    const float funcs = static_cast<float>(std::max(row.building_functions, 0));
    const float road = static_cast<float>(std::max(row.road_size, 0));

    const bool clamp_metrics = config.clamp_semantic_metrics;
    const float step = std::max(config.placement_step, 0.01f);
    const float raw_footprint = clamp_metrics
        ? std::clamp(config.footprint_base + std::sqrt(base), config.footprint_min, config.footprint_max)
        : config.footprint_base + base * config.footprint_unclamped_scale;
    const float footprint = std::max(step, snap_to_grid(raw_footprint, step));
    const float height = clamp_metrics
        ? std::clamp(
              config.height_base + config.height_mass_weight * std::log1p(mass)
                  + config.height_count_weight * std::sqrt(funcs),
              config.height_min, config.height_max)
        : config.height_base + config.height_multiplier * std::log1p(mass)
            + config.height_multiplier * config.height_unclamped_count_weight * std::log1p(funcs);
    const float raw_road_width = clamp_metrics
        ? std::clamp(config.road_width_base + config.road_width_scale * std::log1p(road), config.road_width_min, config.road_width_max)
        : config.road_width_base + config.road_width_scale * std::log1p(road);
    const float road_width = std::max(step, snap_to_grid(raw_road_width, step));
    const float sidewalk_width = std::max(step, snap_to_grid(config.sidewalk_width, step));
    return { footprint, height, sidewalk_width, road_width };
}

bool is_test_semantic_source(std::string_view source_file_path)
{
    return path_has_prefix(source_file_path, "tests/");
}

std::array<RoadSegmentPlacement, 4> build_sidewalk_segments(const SemanticCityBuilding& building)
{
    const float half_footprint = building.metrics.footprint * 0.5f;
    const float sidewalk_width = building.metrics.sidewalk_width;
    const float sidewalk_outer_span = building.metrics.footprint + 2.0f * sidewalk_width;
    const glm::vec2 center = building.center;

    return {
        RoadSegmentPlacement{
            { center.x, center.y + half_footprint + sidewalk_width * 0.5f },
            { sidewalk_outer_span, sidewalk_width },
        },
        RoadSegmentPlacement{
            { center.x, center.y - half_footprint - sidewalk_width * 0.5f },
            { sidewalk_outer_span, sidewalk_width },
        },
        RoadSegmentPlacement{
            { center.x - half_footprint - sidewalk_width * 0.5f, center.y },
            { sidewalk_width, building.metrics.footprint },
        },
        RoadSegmentPlacement{
            { center.x + half_footprint + sidewalk_width * 0.5f, center.y },
            { sidewalk_width, building.metrics.footprint },
        },
    };
}

std::array<RoadSegmentPlacement, 4> build_road_segments(const SemanticCityBuilding& building)
{
    const float half_footprint = building.metrics.footprint * 0.5f;
    const float sidewalk_width = building.metrics.sidewalk_width;
    const float road_width = building.metrics.road_width;
    const float inner_span = building.metrics.footprint + 2.0f * sidewalk_width;
    const float outer_span = inner_span + 2.0f * road_width;
    const glm::vec2 center = building.center;

    return {
        RoadSegmentPlacement{
            { center.x, center.y + half_footprint + sidewalk_width + road_width * 0.5f },
            { outer_span, road_width },
        },
        RoadSegmentPlacement{
            { center.x, center.y - half_footprint - sidewalk_width - road_width * 0.5f },
            { outer_span, road_width },
        },
        RoadSegmentPlacement{
            { center.x - half_footprint - sidewalk_width - road_width * 0.5f, center.y },
            { road_width, inner_span },
        },
        RoadSegmentPlacement{
            { center.x + half_footprint + sidewalk_width + road_width * 0.5f, center.y },
            { road_width, inner_span },
        },
    };
}

CitySurfaceBounds compute_city_road_surface_bounds(const SemanticMegacityLayout& layout)
{
    CitySurfaceBounds bounds;
    bool have_bounds = false;

    auto expand = [&](float min_x, float max_x, float min_z, float max_z) {
        if (!have_bounds)
        {
            bounds.min_x = min_x;
            bounds.max_x = max_x;
            bounds.min_z = min_z;
            bounds.max_z = max_z;
            have_bounds = true;
            return;
        }
        bounds.min_x = std::min(bounds.min_x, min_x);
        bounds.max_x = std::max(bounds.max_x, max_x);
        bounds.min_z = std::min(bounds.min_z, min_z);
        bounds.max_z = std::max(bounds.max_z, max_z);
    };

    for (const auto& module_layout : layout.modules)
    {
        for (const auto& building : module_layout.buildings)
        {
            const float half_extent
                = building.metrics.footprint * 0.5f + building.metrics.sidewalk_width + building.metrics.road_width;
            expand(building.center.x - half_extent, building.center.x + half_extent,
                building.center.y - half_extent, building.center.y + half_extent);
        }

        if (module_layout.park_footprint > 0.0f)
        {
            const float half_extent = module_layout.park_footprint * 0.5f
                + module_layout.park_sidewalk_width + module_layout.park_road_width;
            expand(module_layout.park_center.x - half_extent, module_layout.park_center.x + half_extent,
                module_layout.park_center.y - half_extent, module_layout.park_center.y + half_extent);
        }
    }

    return bounds;
}

SemanticCityModuleModel build_semantic_city_model(
    std::string_view module_path, const std::vector<CityClassRecord>& rows, const MegaCityCodeConfig& config)
{
    SemanticCityModuleModel module_model;
    module_model.module_path = std::string(module_path);
    module_model.buildings.reserve(rows.size());

    for (const auto& row : rows)
    {
        if (row.entity_kind != "building" || row.is_abstract)
            continue;
        if (config.hide_test_entities && is_test_semantic_source(row.source_file_path))
            continue;

        const BuildingMetrics metrics = derive_building_metrics(row, config);
        module_model.connectivity += std::max(row.road_size, 0);
        module_model.buildings.push_back({
            row.module_path.empty() ? std::string(module_path) : row.module_path,
            row.name,
            row.qualified_name,
            row.source_file_path,
            std::max(row.base_size, 0),
            std::max(row.building_functions, 0),
            function_mass(row),
            std::max(row.road_size, 0),
            metrics,
            { 0.0f, 0.0f },
            build_function_layers(row, metrics),
        });
    }

    std::sort(module_model.buildings.begin(), module_model.buildings.end(), [](const SemanticCityBuilding& a, const SemanticCityBuilding& b) {
        if (a.metrics.height != b.metrics.height)
            return a.metrics.height > b.metrics.height;
        if (a.metrics.footprint != b.metrics.footprint)
            return a.metrics.footprint > b.metrics.footprint;
        return a.qualified_name < b.qualified_name;
    });

    return module_model;
}

SemanticMegacityModel build_semantic_megacity_model(
    const std::vector<SemanticCityModuleInput>& modules, const MegaCityCodeConfig& config)
{
    SemanticMegacityModel model;
    model.modules.reserve(modules.size());

    for (const auto& module_input : modules)
    {
        SemanticCityModuleModel module_model = build_semantic_city_model(module_input.module_path, module_input.rows, config);
        if (!module_model.empty())
        {
            module_model.quality = module_input.quality;
            module_model.health = module_input.health;
            model.modules.push_back(std::move(module_model));
        }
    }

    return model;
}

SemanticCityLayout build_semantic_city_layout(
    const SemanticCityModuleModel& module_model, const MegaCityCodeConfig& config)
{
    SemanticCityLayout layout;
    if (module_model.empty())
        return layout;

    std::vector<LotRect> reserved_lots;
    reserved_lots.reserve(module_model.buildings.size() + 1);
    layout.min_x = std::numeric_limits<float>::max();
    layout.max_x = std::numeric_limits<float>::lowest();
    layout.min_z = std::numeric_limits<float>::max();
    layout.max_z = std::numeric_limits<float>::lowest();

    // Reserve a park lot at the center so buildings spiral outward from it.
    const float step = std::max(config.placement_step, 0.01f);
    const float park_fp = std::max(step, snap_to_grid(config.park_footprint, step));
    if (park_fp > 0.0f)
    {
        const float park_margin = park_fp * 0.5f + config.park_sidewalk_width + config.park_road_width;
        const float park_lot_half = std::max(step, snap_to_grid(park_margin, step));
        layout.park_center = { 0.0f, 0.0f };
        layout.park_footprint = park_fp;
        layout.park_sidewalk_width = config.park_sidewalk_width;
        layout.park_road_width = config.park_road_width;
        reserved_lots.push_back({ -park_lot_half, park_lot_half, -park_lot_half, park_lot_half });
        layout.min_x = -park_lot_half;
        layout.max_x = park_lot_half;
        layout.min_z = -park_lot_half;
        layout.max_z = park_lot_half;
    }

    for (const SemanticCityBuilding& building : module_model.buildings)
    {
        const LotRect local_lot = centered_building_lot(building.metrics, config);
        glm::vec2 chosen_center{ 0.0f };
        LotRect chosen_lot{};
        bool placed = false;

        const std::vector<glm::vec2> contact_candidates = touching_lot_candidates(reserved_lots, local_lot, config);
        for (const glm::vec2& center : contact_candidates)
        {
            if (try_place_candidate(reserved_lots, local_lot, center, chosen_center, chosen_lot))
            {
                placed = true;
                break;
            }
        }

        if (!placed)
        {
            for_each_spiral_candidate(config, [&](const glm::vec2& center) {
                if (!try_place_candidate(reserved_lots, local_lot, center, chosen_center, chosen_lot))
                    return true;

                placed = true;
                return false;
            });
        }

        if (!placed)
            continue;

        reserved_lots.push_back(chosen_lot);
        SemanticCityBuilding placed_building = building;
        placed_building.center = chosen_center;
        layout.buildings.push_back(std::move(placed_building));
        layout.min_x = std::min(layout.min_x, chosen_lot.min_x);
        layout.max_x = std::max(layout.max_x, chosen_lot.max_x);
        layout.min_z = std::min(layout.min_z, chosen_lot.min_z);
        layout.max_z = std::max(layout.max_z, chosen_lot.max_z);
    }

    if (layout.buildings.empty())
    {
        layout.min_x = 0.0f;
        layout.max_x = 0.0f;
        layout.min_z = 0.0f;
        layout.max_z = 0.0f;
    }

    return layout;
}

SemanticCityLayout build_semantic_city_layout(
    const std::vector<CityClassRecord>& rows, const MegaCityCodeConfig& config)
{
    const SemanticCityModuleModel module_model = build_semantic_city_model(std::string_view{}, rows, config);
    return build_semantic_city_layout(module_model, config);
}

SemanticMegacityLayout build_semantic_megacity_layout(
    const SemanticMegacityModel& model, const MegaCityCodeConfig& config)
{
    struct ModuleCandidate
    {
        std::string module_path;
        int connectivity = 0;
        float quality = 0.5f;
        CodebaseHealthMetrics health;
        SemanticCityLayout layout;
        LotRect local_lot;
        float area = 0.0f;
    };

    std::vector<ModuleCandidate> candidates;
    candidates.reserve(model.modules.size());
    for (const auto& module_model : model.modules)
    {
        SemanticCityLayout layout = build_semantic_city_layout(module_model, config);
        if (layout.empty())
            continue;

        const float width = layout.max_x - layout.min_x;
        const float depth = layout.max_z - layout.min_z;
        ModuleCandidate mc;
        mc.module_path = module_model.module_path;
        mc.connectivity = module_model.connectivity;
        mc.quality = module_model.quality;
        mc.health = module_model.health;
        mc.layout = std::move(layout);
        mc.area = width * depth;
        candidates.push_back(std::move(mc));
        candidates.back().local_lot = {
            candidates.back().layout.min_x,
            candidates.back().layout.max_x,
            candidates.back().layout.min_z,
            candidates.back().layout.max_z,
        };
    }

    // Most connected module first (hub of the codebase at the center),
    // then by area, then by name.
    std::sort(candidates.begin(), candidates.end(), [](const ModuleCandidate& a, const ModuleCandidate& b) {
        if (a.connectivity != b.connectivity)
            return a.connectivity > b.connectivity;
        if (a.area != b.area)
            return a.area > b.area;
        return a.module_path < b.module_path;
    });

    SemanticMegacityLayout megacity;
    if (candidates.empty())
        return megacity;

    std::vector<LotRect> reserved_modules;
    reserved_modules.reserve(candidates.size() + 1);
    megacity.min_x = std::numeric_limits<float>::max();
    megacity.max_x = std::numeric_limits<float>::lowest();
    megacity.min_z = std::numeric_limits<float>::max();
    megacity.max_z = std::numeric_limits<float>::lowest();

    // Place the central park module at the origin — it represents the whole codebase.
    // Only shown in full city view (multiple modules), not single-module view.
    // Central park is double the size of regular module parks.
    if (candidates.size() > 1)
    {
        const float step = std::max(config.placement_step, 0.01f);
        const float area_scale = std::clamp(config.central_park_area_scale, 1.0f, 3.0f);
        const float border_scale = std::clamp(config.central_park_border_scale, 1.0f, 3.0f);
        const float park_fp = std::max(step, snap_to_grid(config.park_footprint * area_scale, step));
        const float park_sw = config.park_sidewalk_width * border_scale;
        const float park_rw = config.park_road_width * border_scale;
        const float park_margin = park_fp * 0.5f + park_sw + park_rw;
        const float park_lot_half = std::max(step, snap_to_grid(park_margin, step));

        SemanticCityModuleLayout central;
        central.module_path = "central_park";
        central.is_central_park = true;
        central.offset = { 0.0f, 0.0f };
        central.min_x = -park_lot_half;
        central.max_x = park_lot_half;
        central.min_z = -park_lot_half;
        central.max_z = park_lot_half;
        central.quality = (model.codebase_health.complexity + model.codebase_health.cohesion + model.codebase_health.coupling) / 3.0f;
        central.health = model.codebase_health;
        central.park_center = { 0.0f, 0.0f };
        central.park_footprint = park_fp;
        central.park_sidewalk_width = park_sw;
        central.park_road_width = park_rw;

        reserved_modules.push_back({ -park_lot_half, park_lot_half, -park_lot_half, park_lot_half });
        megacity.modules.push_back(std::move(central));
        megacity.min_x = -park_lot_half;
        megacity.max_x = park_lot_half;
        megacity.min_z = -park_lot_half;
        megacity.max_z = park_lot_half;
    }

    for (const ModuleCandidate& candidate : candidates)
    {
        glm::vec2 chosen_offset{ 0.0f };
        LotRect chosen_lot{};
        bool placed = false;

        const std::vector<glm::vec2> contact_candidates = touching_lot_candidates(reserved_modules, candidate.local_lot, config);
        for (const glm::vec2& offset : contact_candidates)
        {
            if (try_place_candidate(reserved_modules, candidate.local_lot, offset, chosen_offset, chosen_lot))
            {
                placed = true;
                break;
            }
        }

        if (!placed)
        {
            for_each_spiral_candidate(config, [&](const glm::vec2& offset) {
                if (!try_place_candidate(reserved_modules, candidate.local_lot, offset, chosen_offset, chosen_lot))
                    return true;

                placed = true;
                return false;
            });
        }

        if (!placed)
            continue;

        SemanticCityModuleLayout module_layout;
        module_layout.module_path = candidate.module_path;
        module_layout.offset = chosen_offset;
        module_layout.min_x = chosen_lot.min_x;
        module_layout.max_x = chosen_lot.max_x;
        module_layout.min_z = chosen_lot.min_z;
        module_layout.max_z = chosen_lot.max_z;
        module_layout.quality = candidate.quality;
        module_layout.health = candidate.health;
        module_layout.park_center = candidate.layout.park_center + chosen_offset;
        module_layout.park_footprint = candidate.layout.park_footprint;
        module_layout.park_sidewalk_width = candidate.layout.park_sidewalk_width;
        module_layout.park_road_width = candidate.layout.park_road_width;
        module_layout.buildings.reserve(candidate.layout.buildings.size());
        for (const SemanticCityBuilding& building : candidate.layout.buildings)
        {
            SemanticCityBuilding translated = building;
            translated.center += chosen_offset;
            module_layout.buildings.push_back(std::move(translated));
        }

        reserved_modules.push_back(chosen_lot);
        megacity.modules.push_back(std::move(module_layout));
        megacity.min_x = std::min(megacity.min_x, chosen_lot.min_x);
        megacity.max_x = std::max(megacity.max_x, chosen_lot.max_x);
        megacity.min_z = std::min(megacity.min_z, chosen_lot.min_z);
        megacity.max_z = std::max(megacity.max_z, chosen_lot.max_z);
    }

    if (megacity.modules.empty())
    {
        megacity.min_x = 0.0f;
        megacity.max_x = 0.0f;
        megacity.min_z = 0.0f;
        megacity.max_z = 0.0f;
    }

    return megacity;
}

SemanticMegacityLayout build_semantic_megacity_layout(
    const std::vector<SemanticCityModuleInput>& modules, const MegaCityCodeConfig& config)
{
    const SemanticMegacityModel model = build_semantic_megacity_model(modules, config);
    return build_semantic_megacity_layout(model, config);
}

CityGrid build_city_grid(const SemanticMegacityLayout& layout, const MegaCityCodeConfig& config)
{
    CityGrid grid;
    if (layout.empty())
        return grid;

    const float step = std::max(config.placement_step, 0.01f);
    grid.cell_size = step;

    // Pad bounds by one cell so buildings at the edge are fully inside.
    grid.origin_x = layout.min_x - step;
    grid.origin_z = layout.min_z - step;
    const float extent_x = (layout.max_x + step) - grid.origin_x;
    const float extent_z = (layout.max_z + step) - grid.origin_z;
    grid.cols = static_cast<int>(std::ceil(extent_x / step));
    grid.rows = static_cast<int>(std::ceil(extent_z / step));

    if (grid.cols <= 0 || grid.rows <= 0)
        return grid;

    grid.cells.assign(static_cast<size_t>(grid.cols) * grid.rows, kCityGridEmpty);

    // Helper: rasterize an axis-aligned rect into the grid with a given cell value.
    // All geometry is snapped to `step`, so edges land exactly on cell boundaries.
    // Use a small inset on max edges to avoid filling cells the geometry only touches
    // at the boundary (off-by-one), and a small inset on min edges for symmetry.
    const float eps = step * 0.01f;
    auto fill_rect = [&](float world_min_x, float world_max_x, float world_min_z, float world_max_z, uint8_t value) {
        const int c0 = std::max(0, static_cast<int>(std::floor((world_min_x - grid.origin_x + eps) / step)));
        const int c1 = std::min(grid.cols - 1, static_cast<int>(std::floor((world_max_x - grid.origin_x - eps) / step)));
        const int r0 = std::max(0, static_cast<int>(std::floor((world_min_z - grid.origin_z + eps) / step)));
        const int r1 = std::min(grid.rows - 1, static_cast<int>(std::floor((world_max_z - grid.origin_z - eps) / step)));
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
                grid.cells[static_cast<size_t>(r) * grid.cols + c] = value;
    };

    // Three separate passes so higher-priority layers always overwrite lower ones:
    // 1) roads, 2) sidewalks, 3) buildings + parks.
    auto for_each_building = [&](auto&& fn) {
        for (const auto& module_layout : layout.modules)
            for (const auto& building : module_layout.buildings)
                fn(building);
    };

    // Pass 1: shared road surface (outermost layer)
    const CitySurfaceBounds road_surface_bounds = compute_city_road_surface_bounds(layout);
    if (road_surface_bounds.valid())
    {
        fill_rect(
            road_surface_bounds.min_x,
            road_surface_bounds.max_x,
            road_surface_bounds.min_z,
            road_surface_bounds.max_z,
            kCityGridRoad);
    }

    // Pass 2: sidewalks (overwrites roads within sidewalk areas)
    for_each_building([&](const SemanticCityBuilding& building) {
        const auto sidewalks = build_sidewalk_segments(building);
        for (const auto& sw : sidewalks)
            fill_rect(
                sw.center.x - sw.extent.x * 0.5f,
                sw.center.x + sw.extent.x * 0.5f,
                sw.center.y - sw.extent.y * 0.5f,
                sw.center.y + sw.extent.y * 0.5f,
                kCityGridSidewalk);
    });
    for (const auto& module_layout : layout.modules)
    {
        if (module_layout.park_footprint > 0.0f)
        {
            SemanticCityBuilding park_building;
            park_building.center = module_layout.park_center;
            park_building.metrics.footprint = module_layout.park_footprint;
            park_building.metrics.sidewalk_width = module_layout.park_sidewalk_width;
            park_building.metrics.road_width = module_layout.park_road_width;
            const auto sidewalks = build_sidewalk_segments(park_building);
            for (const auto& sw : sidewalks)
                fill_rect(
                    sw.center.x - sw.extent.x * 0.5f,
                    sw.center.x + sw.extent.x * 0.5f,
                    sw.center.y - sw.extent.y * 0.5f,
                    sw.center.y + sw.extent.y * 0.5f,
                    kCityGridSidewalk);
        }
    }

    // Pass 3: building footprints + parks (parks never overlap buildings)
    for_each_building([&](const SemanticCityBuilding& building) {
        const float half_fp = building.metrics.footprint * 0.5f;
        const float cx = building.center.x;
        const float cz = building.center.y; // center.y is world Z
        fill_rect(cx - half_fp, cx + half_fp, cz - half_fp, cz + half_fp, kCityGridBuilding);
    });
    for (const auto& module_layout : layout.modules)
    {
        if (module_layout.park_footprint > 0.0f)
        {
            const float half = module_layout.park_footprint * 0.5f;
            fill_rect(
                module_layout.park_center.x - half,
                module_layout.park_center.x + half,
                module_layout.park_center.y - half,
                module_layout.park_center.y + half,
                kCityGridPark);
        }
    }

    return grid;
}

} // namespace draxul
