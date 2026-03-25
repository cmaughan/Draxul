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

constexpr float kPlacementStep = 0.5f;

float clamp_metric(float value, float min_value, float max_value)
{
    return std::clamp(value, min_value, max_value);
}

float maybe_clamp_metric(float value, float min_value, float max_value, bool clamp_metrics)
{
    return clamp_metrics ? clamp_metric(value, min_value, max_value) : value;
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
    std::vector<int> sorted_sizes;
    sorted_sizes.reserve(row.function_sizes.size());
    for (const int size : row.function_sizes)
    {
        if (size > 0)
            sorted_sizes.push_back(size);
    }

    if (sorted_sizes.empty())
        return { { 0, metrics.height } };

    std::sort(sorted_sizes.begin(), sorted_sizes.end(), std::greater<int>());

    const int total_size = std::accumulate(sorted_sizes.begin(), sorted_sizes.end(), 0);
    if (total_size <= 0)
        return { { 0, metrics.height } };

    std::vector<SemanticBuildingLayer> layers;
    layers.reserve(sorted_sizes.size());

    float remaining_height = metrics.height;
    for (size_t index = 0; index < sorted_sizes.size(); ++index)
    {
        const int function_size = sorted_sizes[index];
        float layer_height = metrics.height;
        if (index + 1 < sorted_sizes.size())
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

LotRect centered_building_lot(const BuildingMetrics& metrics)
{
    const float half_extent = metrics.footprint * 0.5f + metrics.road_width * kRoadMarginFraction;
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
bool for_each_spiral_candidate(Fn&& fn)
{
    if (!fn(glm::vec2(0.0f)))
        return false;

    for (int ring = 1; ring < 4096; ++ring)
    {
        const float radius = static_cast<float>(ring) * kPlacementStep;
        for (int ix = -ring; ix <= ring; ++ix)
        {
            if (!fn(glm::vec2(static_cast<float>(ix) * kPlacementStep, -radius)))
                return false;
        }
        for (int iz = -ring + 1; iz <= ring; ++iz)
        {
            if (!fn(glm::vec2(radius, static_cast<float>(iz) * kPlacementStep)))
                return false;
        }
        for (int ix = ring - 1; ix >= -ring; --ix)
        {
            if (!fn(glm::vec2(static_cast<float>(ix) * kPlacementStep, radius)))
                return false;
        }
        for (int iz = ring - 1; iz >= -ring + 1; --iz)
        {
            if (!fn(glm::vec2(-radius, static_cast<float>(iz) * kPlacementStep)))
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
    std::vector<glm::vec2>& candidates, float fixed_axis, float center_axis, float overlap_limit, bool fixed_x)
{
    push_candidate(candidates, fixed_x ? glm::vec2(fixed_axis, center_axis) : glm::vec2(center_axis, fixed_axis));

    for (float offset = kPlacementStep; offset < overlap_limit; offset += kPlacementStep)
    {
        push_candidate(candidates, fixed_x ? glm::vec2(fixed_axis, center_axis + offset) : glm::vec2(center_axis + offset, fixed_axis));
        push_candidate(candidates, fixed_x ? glm::vec2(fixed_axis, center_axis - offset) : glm::vec2(center_axis - offset, fixed_axis));
    }
}

std::vector<glm::vec2> touching_lot_candidates(
    const std::vector<LotRect>& reserved_lots, const LotRect& local_lot)
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
            candidates, occupied.min_x - local_lot.max_x, center_z - local_center_z, overlap_limit_z, true);
        add_contact_candidates_for_side(
            candidates, occupied.max_x - local_lot.min_x, center_z - local_center_z, overlap_limit_z, true);
        add_contact_candidates_for_side(
            candidates, occupied.min_z - local_lot.max_z, center_x - local_center_x, overlap_limit_x, false);
        add_contact_candidates_for_side(
            candidates, occupied.max_z - local_lot.min_z, center_x - local_center_x, overlap_limit_x, false);
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

BuildingMetrics derive_building_metrics(const CityClassRecord& row, bool clamp_metrics)
{
    const float footprint = maybe_clamp_metric(
        1.0f + std::sqrt(static_cast<float>(std::max(row.base_size, 0))), 1.0f, 9.0f, clamp_metrics);
    const float height = maybe_clamp_metric(
        2.0f
            + 1.35f * std::log1p(static_cast<float>(std::max(function_mass(row), 0)))
            + 0.45f * std::sqrt(static_cast<float>(std::max(row.building_functions, 0))),
        2.0f,
        12.0f,
        clamp_metrics);
    const float raw_road = 0.6f + static_cast<float>(std::max(row.road_size, 0));
    const float road_width = clamp_metrics
        ? std::clamp(0.6f + 0.85f * std::log1p(static_cast<float>(std::max(row.road_size, 0))), 0.6f, 3.0f)
        : raw_road;
    return { footprint, height, road_width };
}

bool is_test_semantic_source(std::string_view source_file_path)
{
    return path_has_prefix(source_file_path, "tests/");
}

std::array<RoadSegmentPlacement, 4> build_road_segments(const SemanticCityBuilding& building)
{
    const float half_footprint = building.metrics.footprint * 0.5f;
    const float road_width = building.metrics.road_width;
    const float outer_span = building.metrics.footprint + 2.0f * road_width;
    const glm::vec2 center = building.center;

    return {
        RoadSegmentPlacement{
            { center.x, center.y + half_footprint + road_width * 0.5f },
            { outer_span, road_width },
        },
        RoadSegmentPlacement{
            { center.x, center.y - half_footprint - road_width * 0.5f },
            { outer_span, road_width },
        },
        RoadSegmentPlacement{
            { center.x - half_footprint - road_width * 0.5f, center.y },
            { road_width, building.metrics.footprint },
        },
        RoadSegmentPlacement{
            { center.x + half_footprint + road_width * 0.5f, center.y },
            { road_width, building.metrics.footprint },
        },
    };
}

SemanticCityLayout build_semantic_city_layout(
    const std::vector<CityClassRecord>& rows, bool clamp_metrics, bool hide_test_entities)
{
    struct Candidate
    {
        std::string module_path;
        std::string display_name;
        std::string qualified_name;
        std::string source_file_path;
        BuildingMetrics metrics;
        std::vector<SemanticBuildingLayer> layers;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(rows.size());
    for (const auto& row : rows)
    {
        if (row.entity_kind != "building" || row.is_abstract)
            continue;
        if (hide_test_entities && is_test_semantic_source(row.source_file_path))
            continue;

        const BuildingMetrics metrics = derive_building_metrics(row, clamp_metrics);
        candidates.push_back(
            { row.module_path, row.name, row.qualified_name, row.source_file_path, metrics,
                build_function_layers(row, metrics) });
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.metrics.height != b.metrics.height)
            return a.metrics.height > b.metrics.height;
        if (a.metrics.footprint != b.metrics.footprint)
            return a.metrics.footprint > b.metrics.footprint;
        return a.qualified_name < b.qualified_name;
    });

    SemanticCityLayout layout;
    if (candidates.empty())
        return layout;

    std::vector<LotRect> reserved_lots;
    reserved_lots.reserve(candidates.size());
    layout.min_x = std::numeric_limits<float>::max();
    layout.max_x = std::numeric_limits<float>::lowest();
    layout.min_z = std::numeric_limits<float>::max();
    layout.max_z = std::numeric_limits<float>::lowest();

    for (const Candidate& candidate : candidates)
    {
        const LotRect local_lot = centered_building_lot(candidate.metrics);
        glm::vec2 chosen_center{ 0.0f };
        LotRect chosen_lot{};
        bool placed = false;

        const std::vector<glm::vec2> contact_candidates = touching_lot_candidates(reserved_lots, local_lot);
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
            for_each_spiral_candidate([&](const glm::vec2& center) {
                if (!try_place_candidate(reserved_lots, local_lot, center, chosen_center, chosen_lot))
                {
                    return true;
                }

                placed = true;
                return false;
            });
        }

        if (!placed)
            continue;

        reserved_lots.push_back(chosen_lot);
        layout.buildings.push_back(
            { candidate.module_path, candidate.display_name, candidate.qualified_name, candidate.source_file_path,
                candidate.metrics, chosen_center, candidate.layers });
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

SemanticMegacityLayout build_semantic_megacity_layout(
    const std::vector<SemanticCityModuleInput>& modules, bool clamp_metrics, bool hide_test_entities)
{
    struct ModuleCandidate
    {
        std::string module_path;
        SemanticCityLayout layout;
        LotRect local_lot;
        float area = 0.0f;
    };

    std::vector<ModuleCandidate> candidates;
    candidates.reserve(modules.size());
    for (const auto& module : modules)
    {
        SemanticCityLayout layout = build_semantic_city_layout(module.rows, clamp_metrics, hide_test_entities);
        if (layout.empty())
            continue;

        const float width = layout.max_x - layout.min_x;
        const float depth = layout.max_z - layout.min_z;
        candidates.push_back({
            module.module_path,
            std::move(layout),
            { 0.0f, 0.0f, 0.0f, 0.0f },
            width * depth,
        });
        candidates.back().local_lot = {
            candidates.back().layout.min_x,
            candidates.back().layout.max_x,
            candidates.back().layout.min_z,
            candidates.back().layout.max_z,
        };
    }

    std::sort(candidates.begin(), candidates.end(), [](const ModuleCandidate& a, const ModuleCandidate& b) {
        if (a.area != b.area)
            return a.area > b.area;
        if (a.layout.buildings.size() != b.layout.buildings.size())
            return a.layout.buildings.size() > b.layout.buildings.size();
        return a.module_path < b.module_path;
    });

    SemanticMegacityLayout megacity;
    if (candidates.empty())
        return megacity;

    std::vector<LotRect> reserved_modules;
    reserved_modules.reserve(candidates.size());
    megacity.min_x = std::numeric_limits<float>::max();
    megacity.max_x = std::numeric_limits<float>::lowest();
    megacity.min_z = std::numeric_limits<float>::max();
    megacity.max_z = std::numeric_limits<float>::lowest();

    for (const ModuleCandidate& candidate : candidates)
    {
        glm::vec2 chosen_offset{ 0.0f };
        LotRect chosen_lot{};
        bool placed = false;

        const std::vector<glm::vec2> contact_candidates = touching_lot_candidates(reserved_modules, candidate.local_lot);
        for (const glm::vec2& offset : contact_candidates)
        {
            if (try_place_candidate(
                    reserved_modules, candidate.local_lot, offset, chosen_offset, chosen_lot))
            {
                placed = true;
                break;
            }
        }

        if (!placed)
        {
            for_each_spiral_candidate([&](const glm::vec2& offset) {
                if (!try_place_candidate(
                        reserved_modules, candidate.local_lot, offset, chosen_offset, chosen_lot))
                {
                    return true;
                }

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

} // namespace draxul
