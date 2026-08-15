#include "semantic_city_layout.h"
#include "city_helpers.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <draxul/perf_timing.h>
#include <functional>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <queue>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace draxul
{

namespace
{

constexpr glm::vec4 kOutgoingRouteColor{ 0.20f, 0.88f, 0.30f, 1.0f };
constexpr glm::vec4 kAbstractOutgoingRouteColor{ 0.25f, 0.50f, 0.95f, 1.0f };
constexpr glm::vec4 kIncomingRouteColor{ 0.92f, 0.22f, 0.18f, 1.0f };
constexpr float kModuleSurfaceBorderWidthScale = 0.5f;
constexpr float kModuleSurfaceBorderWidthMin = 0.2f;

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
    PERF_MEASURE();
    struct FunctionEntry
    {
        std::string name;
        int size = 0;
    };
    std::vector<FunctionEntry> entries;
    entries.reserve(row.function_sizes.size());
    for (size_t i = 0; i < row.function_sizes.size(); ++i)
    {
        if (row.function_sizes[i] > 0)
        {
            std::string name = (i < row.function_names.size()) ? row.function_names[i] : std::string();
            entries.push_back({ std::move(name), row.function_sizes[i] });
        }
    }

    if (entries.empty())
        return { { std::string(), std::string(), 0, metrics.height } };

    int total_size = 0;
    for (const auto& entry : entries)
        total_size += entry.size;
    if (total_size <= 0)
        return { { std::string(), std::string(), 0, metrics.height } };

    std::vector<SemanticBuildingLayer> layers;
    layers.reserve(entries.size());

    float remaining_height = metrics.height;
    for (size_t index = 0; index < entries.size(); ++index)
    {
        const int function_size = entries[index].size;
        float layer_height = metrics.height;
        if (index + 1 < entries.size())
        {
            layer_height = metrics.height * static_cast<float>(function_size) / static_cast<float>(total_size);
            layer_height = std::min(layer_height, remaining_height);
        }
        else
        {
            layer_height = remaining_height;
        }

        layers.push_back({ entries[index].name, std::string(), function_size, std::max(layer_height, 0.0f) });
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

bool overlaps(const LotRect& a, const LotRect& b)
{
    return a.min_x < b.max_x && a.max_x > b.min_x && a.min_z < b.max_z && a.max_z > b.min_z;
}

// Uniform spatial grid for fast AABB overlap queries against placed lots.
// Replaces O(n) linear scans in try_place_candidate and touching_lot_candidates.
struct SpatialLotGrid
{
    static constexpr float kCellSize = 10.0f;

    struct CellKey
    {
        int x = 0;
        int z = 0;
        bool operator==(const CellKey&) const = default;
    };

    struct CellKeyHash
    {
        size_t operator()(const CellKey& k) const
        {
            return std::hash<int>()(k.x) ^ (std::hash<int>()(k.z) << 16);
        }
    };

    std::vector<LotRect> lots;
    std::unordered_map<CellKey, std::vector<size_t>, CellKeyHash> cells;

    void clear()
    {
        lots.clear();
        cells.clear();
    }

    void reserve(size_t n)
    {
        lots.reserve(n);
    }

    size_t insert(const LotRect& lot)
    {
        const size_t idx = lots.size();
        lots.push_back(lot);
        const int min_cx = static_cast<int>(std::floor(lot.min_x / kCellSize));
        const int max_cx = static_cast<int>(std::floor(lot.max_x / kCellSize));
        const int min_cz = static_cast<int>(std::floor(lot.min_z / kCellSize));
        const int max_cz = static_cast<int>(std::floor(lot.max_z / kCellSize));
        for (int cx = min_cx; cx <= max_cx; ++cx)
            for (int cz = min_cz; cz <= max_cz; ++cz)
                cells[{ cx, cz }].push_back(idx);
        return idx;
    }

    bool any_overlap(const LotRect& query) const
    {
        const int min_cx = static_cast<int>(std::floor(query.min_x / kCellSize));
        const int max_cx = static_cast<int>(std::floor(query.max_x / kCellSize));
        const int min_cz = static_cast<int>(std::floor(query.min_z / kCellSize));
        const int max_cz = static_cast<int>(std::floor(query.max_z / kCellSize));
        for (int cx = min_cx; cx <= max_cx; ++cx)
            for (int cz = min_cz; cz <= max_cz; ++cz)
            {
                auto it = cells.find({ cx, cz });
                if (it == cells.end())
                    continue;
                for (const size_t idx : it->second)
                    if (overlaps(query, lots[idx]))
                        return true;
            }
        return false;
    }

    // Visit indices of lots whose cells overlap the expanded query AABB.
    // Visitor may see the same index multiple times; caller deduplicates if needed.
    template <typename Fn>
    void for_each_nearby(const LotRect& query, float expansion, Fn&& fn) const
    {
        const int min_cx = static_cast<int>(std::floor((query.min_x - expansion) / kCellSize));
        const int max_cx = static_cast<int>(std::floor((query.max_x + expansion) / kCellSize));
        const int min_cz = static_cast<int>(std::floor((query.min_z - expansion) / kCellSize));
        const int max_cz = static_cast<int>(std::floor((query.max_z + expansion) / kCellSize));
        for (int cx = min_cx; cx <= max_cx; ++cx)
            for (int cz = min_cz; cz <= max_cz; ++cz)
            {
                auto it = cells.find({ cx, cz });
                if (it == cells.end())
                    continue;
                for (const size_t idx : it->second)
                    fn(idx);
            }
    }
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
bool for_each_spiral_candidate(float placement_step, int max_spiral_rings, Fn&& fn, int start_ring = 0)
{
    PERF_MEASURE();
    if (start_ring <= 0)
    {
        if (!fn(glm::vec2(0.0f)))
            return false;
    }

    for (int ring = std::max(1, start_ring); ring < max_spiral_rings; ++ring)
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
    candidates.push_back(candidate);
}

void add_contact_candidates_for_side(
    std::vector<glm::vec2>& candidates, float fixed_axis, float center_axis, float overlap_limit, bool fixed_x,
    const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
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
    const SpatialLotGrid& grid, const LotRect& local_lot, const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    std::vector<glm::vec2> candidates;
    if (grid.lots.empty())
    {
        candidates.push_back(glm::vec2(0.0f));
        return candidates;
    }

    const float local_center_x = lot_center_x(local_lot);
    const float local_center_z = lot_center_z(local_lot);
    const float local_half_extent_x = lot_half_extent_x(local_lot);
    const float local_half_extent_z = lot_half_extent_z(local_lot);

    // Only check recent lots (frontier). Buildings spiral outward, so recently placed
    // lots are on the outer ring. Interior lots produce candidates that always collide.
    constexpr size_t kMaxFrontierLots = 256;
    const size_t frontier_start = grid.lots.size() > kMaxFrontierLots ? grid.lots.size() - kMaxFrontierLots : 0;
    candidates.reserve((grid.lots.size() - frontier_start) * 200);
    for (size_t i = frontier_start; i < grid.lots.size(); ++i)
    {
        const LotRect& occupied = grid.lots[i];
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

    // Cheaply partition the nearest candidates (O(n)) then sort only those (O(k log k)).
    // Far-away candidates almost never succeed — the spiral fallback handles edge cases.
    constexpr size_t kMaxSortedCandidates = 512;
    auto distance_less = [](const glm::vec2& a, const glm::vec2& b) {
        return glm::dot(a, a) < glm::dot(b, b);
    };

    if (candidates.size() > kMaxSortedCandidates)
    {
        std::nth_element(
            candidates.begin(), candidates.begin() + kMaxSortedCandidates, candidates.end(), distance_less);
        candidates.resize(kMaxSortedCandidates);
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

    constexpr float kDuplicateEpsilon = 1e-4f;
    candidates.erase(
        std::unique(candidates.begin(), candidates.end(),
            [](const glm::vec2& a, const glm::vec2& b) {
                return std::abs(a.x - b.x) <= kDuplicateEpsilon && std::abs(a.y - b.y) <= kDuplicateEpsilon;
            }),
        candidates.end());

    return candidates;
}

bool try_place_candidate(
    const SpatialLotGrid& grid,
    const LotRect& local_lot,
    const glm::vec2& offset,
    glm::vec2& chosen_offset,
    LotRect& chosen_lot)
{
    const LotRect lot = translate_lot(local_lot, offset);
    if (grid.any_overlap(lot))
        return false;

    chosen_offset = offset;
    chosen_lot = lot;
    return true;
}

std::string building_key(
    std::string_view source_file_path,
    std::string_view module_path,
    std::string_view qualified_name)
{
    return std::string(source_file_path) + "|" + std::string(module_path) + "|" + std::string(qualified_name);
}

int grid_index(const CityGrid& grid, int col, int row)
{
    return row * grid.cols + col;
}

glm::vec2 grid_cell_center_world(const CityGrid& grid, int col, int row)
{
    return {
        grid.origin_x + (static_cast<float>(col) + 0.5f) * grid.cell_size,
        grid.origin_z + (static_cast<float>(row) + 0.5f) * grid.cell_size,
    };
}

int world_to_grid_col(const CityGrid& grid, float world_x)
{
    return static_cast<int>(std::floor((world_x - grid.origin_x) / grid.cell_size));
}

int world_to_grid_row(const CityGrid& grid, float world_z)
{
    return static_cast<int>(std::floor((world_z - grid.origin_z) / grid.cell_size));
}

struct BuildingRoutePort
{
    glm::vec2 edge_world{ 0.0f };
    glm::vec2 road_entry_world{ 0.0f };
};

enum class PortSide : uint8_t
{
    North,
    South,
    West,
    East,
};

struct RoutePair
{
    const SemanticCityBuilding* source = nullptr;
    const SemanticCityBuilding* target = nullptr;
    std::string source_qualified_name;
    std::string target_qualified_name;
    std::string field_name;
    std::string field_type_name;
    bool is_abstract_ref = false;
};

struct RouteEndpointRequest
{
    size_t route_index = 0;
    bool source_endpoint = false;
    const SemanticCityBuilding* building = nullptr;
    const SemanticCityBuilding* other = nullptr;
    PortSide side = PortSide::North;
    float sort_key = 0.0f;
    std::string field_name;
    std::string individual_qualified_name; // for struct stacks: the specific struct name
    bool is_abstract_ref = false;
};

PortSide side_towards(const SemanticCityBuilding& from, const SemanticCityBuilding& to)
{
    const glm::vec2 delta = to.center - from.center;
    if (std::abs(delta.x) >= std::abs(delta.y))
        return delta.x >= 0.0f ? PortSide::East : PortSide::West;
    return delta.y >= 0.0f ? PortSide::North : PortSide::South;
}

float side_sort_key(PortSide side, const SemanticCityBuilding& other)
{
    switch (side)
    {
    case PortSide::North:
    case PortSide::South:
        return other.center.x;
    case PortSide::West:
    case PortSide::East:
        return other.center.y;
    }
    return 0.0f;
}

std::string route_port_group_key(std::string_view building_key_value, PortSide side)
{
    return std::string(building_key_value) + "#" + std::to_string(static_cast<int>(side));
}

std::optional<BuildingRoutePort> make_building_route_port(
    const SemanticCityBuilding& building, PortSide side, float tangent_offset, const CityGrid& grid)
{
    PERF_MEASURE();
    (void)grid;
    const float half_footprint = building.metrics.footprint * 0.5f;
    const float road_center_offset = half_footprint + building.metrics.sidewalk_width + building.metrics.road_width * 0.5f;
    // Start the edge anchor 1/4 inside the footprint so the rendered line
    // visually intersects non-rectangular (pentagonal) building geometry.
    const float edge_inset = half_footprint * 0.5f;

    glm::vec2 edge_world(0.0f);
    glm::vec2 road_world(0.0f);
    switch (side)
    {
    case PortSide::North:
        edge_world = { building.center.x + tangent_offset, building.center.y + edge_inset };
        road_world = { building.center.x + tangent_offset, building.center.y + road_center_offset };
        break;
    case PortSide::South:
        edge_world = { building.center.x + tangent_offset, building.center.y - edge_inset };
        road_world = { building.center.x + tangent_offset, building.center.y - road_center_offset };
        break;
    case PortSide::West:
        edge_world = { building.center.x - edge_inset, building.center.y + tangent_offset };
        road_world = { building.center.x - road_center_offset, building.center.y + tangent_offset };
        break;
    case PortSide::East:
        edge_world = { building.center.x + edge_inset, building.center.y + tangent_offset };
        road_world = { building.center.x + road_center_offset, building.center.y + tangent_offset };
        break;
    }

    return BuildingRoutePort{
        edge_world,
        road_world,
    };
}

std::vector<RoutePair> collect_route_pairs(
    const SemanticMegacityLayout& layout,
    const SemanticMegacityModel& model,
    std::string_view focus_source_file_path,
    std::string_view focus_module_path,
    std::string_view focus_qualified_name,
    std::string_view focus_function_name)
{
    PERF_MEASURE();
    std::unordered_map<std::string, const SemanticCityBuilding*> buildings_by_key;
    for (const auto& module_layout : layout.modules)
    {
        for (const auto& building : module_layout.buildings)
            buildings_by_key.emplace(
                building_key(building.source_file_path, building.module_path, building.qualified_name),
                &building);
    }

    const std::string focus_key = focus_qualified_name.empty()
        ? std::string()
        : building_key(focus_source_file_path, focus_module_path, focus_qualified_name);

    // For function bundles and struct stacks, look up the building via the remap when the direct key misses.
    auto find_building = [&](const std::string& key, const std::string& qualified_name,
                             const std::string& module_path) -> const SemanticCityBuilding* {
        auto it = buildings_by_key.find(key);
        if (it != buildings_by_key.end())
            return it->second;
        auto remap_it = model.function_bundle_remap.find(qualified_name);
        if (remap_it != model.function_bundle_remap.end())
        {
            auto bundle_it = buildings_by_key.find(building_key("", module_path, remap_it->second));
            if (bundle_it != buildings_by_key.end())
                return bundle_it->second;
        }
        auto struct_remap_it = model.struct_stack_remap.find(qualified_name);
        if (struct_remap_it != model.struct_stack_remap.end())
        {
            auto stack_it = buildings_by_key.find(building_key("", module_path, struct_remap_it->second));
            if (stack_it != buildings_by_key.end())
                return stack_it->second;
        }
        return nullptr;
    };

    // Deduplicate by (source, target) — keep the first non-std::function match per pair.
    // Abstract fan-outs (same source, same field, multiple targets) are NOT deduped.
    std::unordered_map<std::string, size_t> seen_pairs; // edge_key → index in route_pairs
    std::vector<RoutePair> route_pairs;
    route_pairs.reserve(model.dependencies.size());
    for (const auto& dep : model.dependencies)
    {
        // When a specific function/struct in a bundle/stack is focused, only show that entity's deps.
        if (!focus_function_name.empty()
            && dep.source_qualified_name != focus_function_name
            && dep.target_qualified_name != focus_function_name)
            continue;

        const std::string source_key = building_key(
            dep.source_file_path, dep.source_module_path, dep.source_qualified_name);
        const std::string target_key = building_key(
            dep.target_file_path, dep.target_module_path, dep.target_qualified_name);

        const SemanticCityBuilding* source_building = find_building(source_key, dep.source_qualified_name, dep.source_module_path);
        const SemanticCityBuilding* target_building = find_building(target_key, dep.target_qualified_name, dep.target_module_path);

        // When focusing a bundle, match by the bundle building's key.
        if (!focus_key.empty())
        {
            const std::string effective_source_key = source_building
                ? building_key(source_building->source_file_path, source_building->module_path, source_building->qualified_name)
                : source_key;
            const std::string effective_target_key = target_building
                ? building_key(target_building->source_file_path, target_building->module_path, target_building->qualified_name)
                : target_key;
            if (effective_source_key != focus_key && effective_target_key != focus_key)
                continue;
        }

        if (source_building == target_building || !source_building || !target_building)
            continue;

        const std::string edge_key = source_key + "→" + target_key;
        const bool is_function_type = dep.field_type_name.find("function") != std::string::npos;
        auto [it, inserted] = seen_pairs.emplace(edge_key, route_pairs.size());
        if (!inserted)
        {
            // Duplicate (source, target). Replace if the existing one is a function type and this one isn't.
            if (is_function_type)
                continue;
            auto& existing = route_pairs[it->second];
            if (existing.field_type_name.find("function") != std::string::npos)
                existing = { source_building, target_building, dep.source_qualified_name, dep.target_qualified_name,
                    dep.field_name, dep.field_type_name, dep.is_abstract_ref };
            continue;
        }

        route_pairs.push_back({
            source_building,
            target_building,
            dep.source_qualified_name,
            dep.target_qualified_name,
            dep.field_name,
            dep.field_type_name,
            dep.is_abstract_ref,
        });
    }

    return route_pairs;
}

struct AssignedRoutePorts
{
    std::optional<BuildingRoutePort> source_port;
    std::optional<BuildingRoutePort> target_port;
};

glm::vec2 brick_center_offset(
    const SemanticCityBuilding& building, std::string_view individual_name, const MegaCityCodeConfig& config)
{
    if (!building.is_struct_stack || individual_name.empty())
        return { 0.0f, 0.0f };
    const int grid_size = std::max(config.struct_brick_grid_size, 1);
    const int bpf = brick_slots_per_floor(grid_size);
    const float footprint = std::max(building.metrics.footprint, 0.1f);
    const float brick_gap = std::max(config.struct_brick_gap, 0.0f);
    const float total_gap = static_cast<float>(grid_size - 1) * brick_gap;
    const float brick_size = std::max((footprint - total_gap) / static_cast<float>(grid_size), 0.01f);
    const float half = footprint * 0.5f;

    for (size_t li = 0; li < building.layers.size(); ++li)
    {
        if (building.layers[li].function_name == individual_name)
        {
            const int local = static_cast<int>(li) % bpf;
            const auto [col, row] = brick_slot_position(local, grid_size);
            const float cx = -half + static_cast<float>(col) * (brick_size + brick_gap) + brick_size * 0.5f;
            const float cz = -half + static_cast<float>(row) * (brick_size + brick_gap) + brick_size * 0.5f;
            return { cx, cz };
        }
    }
    return { 0.0f, 0.0f };
}

std::vector<AssignedRoutePorts> assign_route_ports(
    const std::vector<RoutePair>& route_pairs, const CityGrid& grid, const MegaCityCodeConfig& config)
{
    PERF_MEASURE();

    // Pre-compute a shared source side for abstract fan-out clusters.
    // All routes from the same source building + field that are abstract refs
    // exit from the same side (toward the centroid of their targets).
    struct AbstractClusterKey
    {
        std::string source_key;
        std::string field_name;
        bool operator==(const AbstractClusterKey& other) const
        {
            return source_key == other.source_key && field_name == other.field_name;
        }
    };
    struct AbstractClusterKeyHash
    {
        size_t operator()(const AbstractClusterKey& k) const
        {
            size_t h1 = std::hash<std::string>{}(k.source_key);
            size_t h2 = std::hash<std::string>{}(k.field_name);
            return h1 ^ (h2 * 2654435761u);
        }
    };
    struct ClusterAccum
    {
        const SemanticCityBuilding* source = nullptr;
        glm::vec2 target_centroid_sum{ 0.0f };
        int count = 0;
    };
    std::unordered_map<AbstractClusterKey, ClusterAccum, AbstractClusterKeyHash> abstract_clusters;
    for (const auto& route : route_pairs)
    {
        if (!route.is_abstract_ref || route.source == nullptr || route.target == nullptr)
            continue;
        AbstractClusterKey key{
            building_key(route.source->source_file_path, route.source->module_path, route.source->qualified_name),
            route.field_name
        };
        auto& accum = abstract_clusters[key];
        accum.source = route.source;
        accum.target_centroid_sum += route.target->center;
        ++accum.count;
    }
    // Resolve each cluster with 2+ targets to a shared side.
    std::unordered_map<AbstractClusterKey, PortSide, AbstractClusterKeyHash> abstract_shared_side;
    for (const auto& [key, accum] : abstract_clusters)
    {
        if (accum.count < 2)
            continue;
        const glm::vec2 centroid = accum.target_centroid_sum / static_cast<float>(accum.count);
        SemanticCityBuilding centroid_building;
        centroid_building.center = centroid;
        abstract_shared_side[key] = side_towards(*accum.source, centroid_building);
    }

    std::unordered_map<std::string, std::vector<RouteEndpointRequest>> groups;
    groups.reserve(route_pairs.size() * 2);

    for (size_t route_index = 0; route_index < route_pairs.size(); ++route_index)
    {
        const RoutePair& route = route_pairs[route_index];
        if (route.source == nullptr || route.target == nullptr)
            continue;

        PortSide source_side = side_towards(*route.source, *route.target);
        // Override with shared side for abstract fan-out clusters.
        if (route.is_abstract_ref)
        {
            AbstractClusterKey key{
                building_key(route.source->source_file_path, route.source->module_path, route.source->qualified_name),
                route.field_name
            };
            const auto shared_it = abstract_shared_side.find(key);
            if (shared_it != abstract_shared_side.end())
                source_side = shared_it->second;
        }
        const PortSide target_side = side_towards(*route.target, *route.source);

        groups[route_port_group_key(
                   building_key(route.source->source_file_path, route.source->module_path, route.source->qualified_name),
                   source_side)]
            .push_back({
                route_index,
                true,
                route.source,
                route.target,
                source_side,
                side_sort_key(source_side, *route.target),
                route.field_name,
                route.source_qualified_name,
                route.is_abstract_ref,
            });
        groups[route_port_group_key(
                   building_key(route.target->source_file_path, route.target->module_path, route.target->qualified_name),
                   target_side)]
            .push_back({
                route_index,
                false,
                route.target,
                route.source,
                target_side,
                side_sort_key(target_side, *route.source),
                route.field_name,
                route.target_qualified_name,
                route.is_abstract_ref,
            });
    }

    std::vector<AssignedRoutePorts> assignments(route_pairs.size());
    for (auto& [group_key, requests] : groups)
    {
        (void)group_key;
        if (requests.empty() || requests.front().building == nullptr)
            continue;

        std::sort(requests.begin(), requests.end(), [](const RouteEndpointRequest& lhs, const RouteEndpointRequest& rhs) {
            if (lhs.sort_key != rhs.sort_key)
                return lhs.sort_key < rhs.sort_key;
            return lhs.route_index < rhs.route_index;
        });

        const SemanticCityBuilding& building = *requests.front().building;
        const float half_footprint = building.metrics.footprint * 0.5f;
        const float edge_margin = std::min(
            half_footprint * 0.65f,
            std::max(grid.cell_size * 0.75f, building.metrics.footprint * 0.08f));
        const float usable_half_span = std::max(grid.cell_size * 0.25f, half_footprint - edge_margin);
        const size_t count = requests.size();

        // Abstract source endpoints with the same field_name share a port slot.
        std::vector<size_t> slot_indices(count);
        size_t next_slot = 0;
        std::unordered_map<std::string, size_t> abstract_source_slots;
        for (size_t index = 0; index < count; ++index)
        {
            const auto& req = requests[index];
            if (req.source_endpoint && req.is_abstract_ref)
            {
                auto [it, inserted] = abstract_source_slots.emplace(req.field_name, next_slot);
                if (inserted)
                    ++next_slot;
                slot_indices[index] = it->second;
            }
            else
            {
                slot_indices[index] = next_slot++;
            }
        }
        const size_t total_slots = next_slot;

        for (size_t index = 0; index < count; ++index)
        {
            const float fraction = static_cast<float>(slot_indices[index] + 1) / static_cast<float>(total_slots + 1);
            const float tangent_offset = total_slots == 1
                ? 0.0f
                : glm::mix(-usable_half_span, usable_half_span, fraction);
            std::optional<BuildingRoutePort> port
                = make_building_route_port(building, requests[index].side, tangent_offset, grid);
            if (!port.has_value())
                continue;

            // Offset edge_world to the specific brick for struct stacks.
            if (building.is_struct_stack)
            {
                const glm::vec2 offset = brick_center_offset(building, requests[index].individual_qualified_name, config);
                port->edge_world += offset;
            }

            if (requests[index].source_endpoint)
                assignments[requests[index].route_index].source_port = port;
            else
                assignments[requests[index].route_index].target_port = port;
        }
    }

    return assignments;
}

void append_point_if_new(std::vector<glm::vec2>& points, const glm::vec2& point)
{
    constexpr float kPointEpsilon = 1e-4f;
    if (!points.empty())
    {
        const glm::vec2 delta = points.back() - point;
        if (std::abs(delta.x) <= kPointEpsilon && std::abs(delta.y) <= kPointEpsilon)
            return;
    }
    points.push_back(point);
}

void simplify_polyline(std::vector<glm::vec2>& points)
{
    PERF_MEASURE();
    if (points.size() < 3)
        return;

    std::vector<glm::vec2> simplified;
    simplified.reserve(points.size());
    simplified.push_back(points.front());
    for (size_t i = 1; i + 1 < points.size(); ++i)
    {
        const glm::vec2 a = simplified.back();
        const glm::vec2 b = points[i];
        const glm::vec2 c = points[i + 1];
        const glm::vec2 ab = b - a;
        const glm::vec2 bc = c - b;
        const float cross = ab.x * bc.y - ab.y * bc.x;
        const float dot = ab.x * bc.x + ab.y * bc.y;
        if (std::abs(cross) <= 1e-4f && dot >= 0.0f)
            continue;
        simplified.push_back(b);
    }
    simplified.push_back(points.back());
    points = std::move(simplified);
}

// ---------------------------------------------------------------------------
// Grid-based A* pathfinder — replaces the old O(n^2*m) visibility graph.
// Routes through road and sidewalk cells, avoiding buildings and parks.
// ---------------------------------------------------------------------------

bool is_walkable_cell(uint8_t cell)
{
    return cell == kCityGridRoad || cell == kCityGridSidewalk;
}

glm::ivec2 nearest_walkable_cell(const CityGrid& grid, const glm::vec2& world_pos)
{
    int col = static_cast<int>(std::floor((world_pos.x - grid.origin_x) / grid.cell_size));
    int row = static_cast<int>(std::floor((world_pos.y - grid.origin_z) / grid.cell_size));
    col = std::clamp(col, 0, grid.cols - 1);
    row = std::clamp(row, 0, grid.rows - 1);

    if (is_walkable_cell(grid.at(col, row)))
        return { col, row };

    // Spiral outward to find the nearest walkable cell.
    constexpr int kMaxSearch = 16;
    for (int radius = 1; radius <= kMaxSearch; ++radius)
    {
        for (int dc = -radius; dc <= radius; ++dc)
        {
            for (int dr = -radius; dr <= radius; ++dr)
            {
                if (std::abs(dc) != radius && std::abs(dr) != radius)
                    continue;
                const int c = col + dc;
                const int r = row + dr;
                if (c >= 0 && c < grid.cols && r >= 0 && r < grid.rows
                    && is_walkable_cell(grid.at(c, r)))
                    return { c, r };
            }
        }
    }
    return { -1, -1 };
}

bool find_grid_path(
    const CityGrid& grid,
    const glm::vec2& start_world,
    const glm::vec2& goal_world,
    std::vector<glm::vec2>& path_out)
{
    PERF_MEASURE();
    path_out.clear();
    if (grid.cols <= 0 || grid.rows <= 0 || grid.cells.empty())
        return false;

    const glm::ivec2 sc = nearest_walkable_cell(grid, start_world);
    const glm::ivec2 gc = nearest_walkable_cell(grid, goal_world);
    if (sc.x < 0 || gc.x < 0)
        return false;
    if (sc == gc)
    {
        path_out.push_back(start_world);
        return true;
    }

    const int total = grid.cols * grid.rows;
    const auto idx = [&](int c, int r) { return r * grid.cols + c; };

    std::vector<float> g_score(total, std::numeric_limits<float>::max());
    std::vector<int> predecessor(total, -1);

    const auto heuristic = [&](int c, int r) {
        const float dx = static_cast<float>(std::abs(c - gc.x));
        const float dy = static_cast<float>(std::abs(r - gc.y));
        return std::max(dx, dy) + (1.41421356f - 1.0f) * std::min(dx, dy);
    };

    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> frontier;

    const int start_idx = idx(sc.x, sc.y);
    const int goal_idx = idx(gc.x, gc.y);
    g_score[start_idx] = 0.0f;
    frontier.push({ heuristic(sc.x, sc.y), start_idx });

    static constexpr int kDx[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static constexpr int kDy[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    static constexpr float kCost[] = { 1.41421356f, 1.0f, 1.41421356f, 1.0f, 1.0f, 1.41421356f, 1.0f, 1.41421356f };

    while (!frontier.empty())
    {
        const auto [f, ci] = frontier.top();
        frontier.pop();
        if (ci == goal_idx)
            break;

        const int cc = ci % grid.cols;
        const int cr = ci / grid.cols;
        if (f > g_score[ci] + heuristic(cc, cr) + 0.01f)
            continue;

        for (int d = 0; d < 8; ++d)
        {
            const int nc = cc + kDx[d];
            const int nr = cr + kDy[d];
            if (nc < 0 || nc >= grid.cols || nr < 0 || nr >= grid.rows)
                continue;
            if (!is_walkable_cell(grid.at(nc, nr)))
                continue;
            // Diagonal: both cardinal neighbors must be walkable to prevent corner-cutting.
            if (kDx[d] != 0 && kDy[d] != 0)
            {
                if (!is_walkable_cell(grid.at(cc + kDx[d], cr))
                    || !is_walkable_cell(grid.at(cc, cr + kDy[d])))
                    continue;
            }
            const int ni = idx(nc, nr);
            const float tg = g_score[ci] + kCost[d];
            if (tg + 1e-5f >= g_score[ni])
                continue;
            g_score[ni] = tg;
            predecessor[ni] = ci;
            frontier.push({ tg + heuristic(nc, nr), ni });
        }
    }

    if (g_score[goal_idx] == std::numeric_limits<float>::max())
        return false;

    // Reconstruct cell path.
    std::vector<glm::ivec2> cells;
    for (int ci = goal_idx; ci >= 0; ci = (ci == start_idx) ? -1 : predecessor[ci])
    {
        cells.push_back({ ci % grid.cols, ci / grid.cols });
        if (ci == start_idx)
            break;
    }
    std::reverse(cells.begin(), cells.end());

    // Convert to world coordinates (cell centers), but keep the precise
    // start/goal positions so the path doesn't overshoot the port anchor.
    path_out.reserve(cells.size());
    for (size_t i = 0; i < cells.size(); ++i)
    {
        if (i == 0)
            path_out.push_back(start_world);
        else if (i == cells.size() - 1)
            path_out.push_back(goal_world);
        else
            path_out.push_back({
                grid.origin_x + (cells[i].x + 0.5f) * grid.cell_size,
                grid.origin_z + (cells[i].y + 0.5f) * grid.cell_size,
            });
    }
    return true;
}

std::vector<CityGrid::RoutePolyline> build_city_routes_from_grid(
    const SemanticMegacityLayout& layout, const SemanticMegacityModel& model, const CityGrid& grid,
    const MegaCityCodeConfig& config,
    std::string_view focus_source_file_path,
    std::string_view focus_module_path,
    std::string_view focus_qualified_name,
    std::string_view focus_function_name = {})
{
    PERF_MEASURE();
    const std::vector<RoutePair> route_pairs = collect_route_pairs(
        layout,
        model,
        focus_source_file_path,
        focus_module_path,
        focus_qualified_name,
        focus_function_name);
    const std::vector<AssignedRoutePorts> assigned_ports = assign_route_ports(route_pairs, grid, config);

    std::vector<std::optional<CityGrid::RoutePolyline>> route_results(route_pairs.size());
    const auto solve_route = [&](size_t route_index) {
        const RoutePair& pair = route_pairs[route_index];
        const AssignedRoutePorts& ports = assigned_ports[route_index];
        if (pair.source == nullptr || pair.target == nullptr
            || !ports.source_port.has_value() || !ports.target_port.has_value()
            || grid.cols <= 0 || grid.rows <= 0)
        {
            return;
        }

        std::vector<glm::vec2> road_path;
        if (!find_grid_path(
                grid,
                ports.source_port->road_entry_world,
                ports.target_port->road_entry_world,
                road_path))
        {
            return;
        }

        std::vector<glm::vec2> world_points;
        append_point_if_new(world_points, ports.source_port->edge_world);
        append_point_if_new(world_points, ports.source_port->road_entry_world);
        for (const glm::vec2& point : road_path)
            append_point_if_new(world_points, point);
        append_point_if_new(world_points, ports.target_port->road_entry_world);
        append_point_if_new(world_points, ports.target_port->edge_world);

        simplify_polyline(world_points);
        route_results[route_index] = CityGrid::RoutePolyline{
            pair.source->source_file_path,
            pair.source->module_path,
            pair.source_qualified_name,
            pair.target->source_file_path,
            pair.target->module_path,
            pair.target_qualified_name,
            pair.field_name,
            pair.field_type_name,
            pair.is_abstract_ref ? kAbstractOutgoingRouteColor : kOutgoingRouteColor,
            kIncomingRouteColor,
            std::move(world_points),
            0.0f, // source_elevation — assigned below
            0.0f, // target_elevation — assigned below
        };
    };

    const unsigned hw_threads = std::thread::hardware_concurrency();
    const size_t worker_count = std::min<size_t>(
        route_pairs.size(),
        std::max<size_t>(1, hw_threads == 0 ? 1 : hw_threads));
    if (worker_count <= 1 || route_pairs.size() < 4)
    {
        for (size_t route_index = 0; route_index < route_pairs.size(); ++route_index)
            solve_route(route_index);
    }
    else
    {
        std::atomic<size_t> next_route_index{ 0 };
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (size_t worker_index = 0; worker_index < worker_count; ++worker_index)
        {
            workers.emplace_back([&next_route_index, &route_pairs, &solve_route]() {
                while (true)
                {
                    const size_t route_index = next_route_index.fetch_add(1);
                    if (route_index >= route_pairs.size())
                        break;
                    solve_route(route_index);
                }
            });
        }
        for (std::thread& worker : workers)
            worker.join();
    }

    std::vector<CityGrid::RoutePolyline> routes;
    routes.reserve(route_pairs.size());
    for (auto& route_result : route_results)
    {
        if (route_result.has_value())
            routes.push_back(std::move(*route_result));
    }

    // Assign per-route stacked elevation so pick code can use it per-route.
    // For function bundles with a focused function, use the layer's Y position
    // so the route exits at the correct floor height.
    const float base_elev = route_base_elevation(config);
    const float bldg_base_elev = building_base_elevation(config);
    const float layer_step = std::max(config.dependency_route_layer_step, 0.0f);

    // Resolve the elevation of a specific layer within a building.  Returns -1
    // when the building has no matching layer (i.e. it is not a bundle/stack or
    // the focus name is not among its layers).
    auto resolve_layer_elev = [&](const SemanticCityBuilding* bldg,
                                  std::string_view fn_name) -> float {
        if (fn_name.empty() || bldg == nullptr
            || (!bldg->is_free_function && !bldg->is_struct_stack))
            return -1.0f;
        if (bldg->is_struct_stack)
        {
            const int gs = std::max(config.struct_brick_grid_size, 1);
            const int bpf = brick_slots_per_floor(gs);
            const float fg = std::max(config.struct_stack_gap, 0.0f);
            for (size_t li = 0; li < bldg->layers.size(); ++li)
            {
                if (bldg->layers[li].function_name != fn_name)
                    continue;
                const int floor = static_cast<int>(li) / bpf;
                float cumulative = 0.0f;
                for (int fi = 0; fi < floor; ++fi)
                {
                    const size_t fs = static_cast<size_t>(fi) * bpf;
                    const size_t fe = std::min(fs + static_cast<size_t>(bpf), bldg->layers.size());
                    float fh = 0.0f;
                    for (size_t bi = fs; bi < fe; ++bi)
                        fh = std::max(fh, bldg->layers[bi].height);
                    cumulative += fh + fg;
                }
                const size_t cfs = static_cast<size_t>(floor) * bpf;
                const size_t cfe = std::min(cfs + static_cast<size_t>(bpf), bldg->layers.size());
                float cfh = 0.0f;
                for (size_t bi = cfs; bi < cfe; ++bi)
                    cfh = std::max(cfh, bldg->layers[bi].height);
                return bldg_base_elev + cumulative + cfh * 0.5f;
            }
        }
        else
        {
            float cumulative = 0.0f;
            for (size_t li = 0; li < bldg->layers.size(); ++li)
            {
                const auto& layer = bldg->layers[li];
                cumulative += layer.height;
                if (layer.function_name == fn_name)
                    return bldg_base_elev + cumulative - layer.height * 0.5f;
            }
        }
        return -1.0f;
    };

    std::unordered_map<std::string, int> side_layer_counters;
    for (size_t ri = 0; ri < routes.size(); ++ri)
    {
        auto& route = routes[ri];
        if (route.world_points.size() < 2)
        {
            route.source_elevation = base_elev;
            route.target_elevation = base_elev;
            continue;
        }
        const glm::vec2 edge = route.world_points.front();
        const glm::vec2 road = route.world_points[1];
        const glm::vec2 dir = road - edge;
        const char side = std::abs(dir.x) > std::abs(dir.y)
            ? (dir.x > 0.0f ? 'E' : 'W')
            : (dir.y > 0.0f ? 'N' : 'S');
        const std::string key = route.source_file_path + "#" + route.source_module_path + "#"
            + route.source_qualified_name + '#' + side;
        const int side_layer = side_layer_counters[key]++;
        const float fallback_elev = base_elev + static_cast<float>(side_layer) * layer_step;

        // Resolve per-end elevations.  For the focused building, use the
        // specific layer height; for the other end use the fallback.
        const auto& rp = route_pairs[ri];
        const float src_layer = resolve_layer_elev(rp.source, focus_function_name);
        const float tgt_layer = resolve_layer_elev(rp.target, focus_function_name);

        route.source_elevation = src_layer >= 0.0f ? src_layer : fallback_elev;
        route.target_elevation = tgt_layer >= 0.0f ? tgt_layer : fallback_elev;
    }

    return routes;
}

} // namespace

BuildingMetrics derive_building_metrics(const CityClassRecord& row, const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    const float base = static_cast<float>(std::max(row.base_size, 0));
    const float mass = static_cast<float>(std::max(function_mass(row), 0));
    const float funcs = static_cast<float>(std::max(row.building_functions, 0));
    const float road = static_cast<float>(std::max(row.road_size, 0));

    const bool clamp_metrics = config.clamp_semantic_metrics;
    const float step = std::max(config.placement_step, 0.01f);
    const float raw_footprint = clamp_metrics
        ? std::clamp(config.footprint_base + std::sqrt(base), config.footprint_range.x, config.footprint_range.y)
        : config.footprint_base + base * config.footprint_unclamped_scale;
    const float footprint = std::max(step, snap_to_grid(raw_footprint, step));
    const float height = clamp_metrics
        ? std::clamp(
              config.height_base + config.height_mass_weight * std::log1p(mass)
                  + config.height_count_weight * std::sqrt(funcs),
              config.height_range.x, config.height_range.y)
        : config.height_base + config.height_multiplier * std::log1p(mass)
            + config.height_multiplier * config.height_unclamped_count_weight * std::log1p(funcs);
    const float raw_road_width = clamp_metrics
        ? std::clamp(config.road_width_base + config.road_width_scale * std::log1p(road), config.road_width_range.x, config.road_width_range.y)
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
    PERF_MEASURE();
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
    PERF_MEASURE();
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

float compute_module_border_width(const SemanticCityModuleLayout& module_layout, const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    const float extent_x = module_layout.max_x - module_layout.min_x;
    const float extent_z = module_layout.max_z - module_layout.min_z;
    if (extent_x <= 1e-4f || extent_z <= 1e-4f)
        return 0.0f;

    return std::min(
        std::min(extent_x, extent_z) * 0.5f,
        std::max(config.placement_step * kModuleSurfaceBorderWidthScale, kModuleSurfaceBorderWidthMin));
}

std::array<ModuleBoundarySignPlacement, 2> build_module_boundary_sign_placements(
    const SemanticCityModuleLayout& module_layout, const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    ModuleBoundarySignPlacement south;
    ModuleBoundarySignPlacement north;

    const float extent_x = module_layout.max_x - module_layout.min_x;
    const float border_width = compute_module_border_width(module_layout, config);
    const float usable_width = std::max(0.35f, extent_x - 2.0f * border_width);
    const float sign_width = module_layout.park_footprint > 0.0f
        ? std::max(0.35f, std::min(module_layout.park_footprint, usable_width))
        : usable_width;
    const float sign_depth = config.roof_sign_thickness * 0.5f;
    const float center_x = (module_layout.min_x + module_layout.max_x) * 0.5f;

    south.center = { center_x, module_layout.min_z + sign_depth * 0.5f };
    south.width = sign_width;
    south.depth = sign_depth;
    south.yaw_radians = std::numbers::pi_v<float>;

    north.center = { center_x, module_layout.max_z - sign_depth * 0.5f };
    north.width = sign_width;
    north.depth = sign_depth;
    north.yaw_radians = 0.0f;

    return { south, north };
}

CitySurfaceBounds compute_city_road_surface_bounds(const SemanticMegacityLayout& layout)
{
    PERF_MEASURE();
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
    PERF_MEASURE();
    SemanticCityModuleModel module_model;
    module_model.module_path = std::string(module_path);
    module_model.buildings.reserve(rows.size());

    std::vector<const CityClassRecord*> free_functions;
    for (const auto& row : rows)
    {
        if ((row.entity_kind != "building" && row.entity_kind != "block" && row.entity_kind != "tree") || row.is_abstract)
            continue;
        if (config.hide_test_entities && is_test_semantic_source(row.source_file_path))
            continue;
        if (config.hide_struct_entities && row.is_struct)
            continue;
        if (row.entity_kind == "tree")
        {
            if (!config.hide_function_entities)
                free_functions.push_back(&row);
            continue;
        }

        const BuildingMetrics metrics = derive_building_metrics(row, config);
        module_model.connectivity += std::max(row.road_size, 0);
        module_model.buildings.push_back({
            row.module_path.empty() ? std::string(module_path) : row.module_path,
            row.name,
            row.qualified_name,
            row.source_file_path,
            row.is_struct,
            false,
            false,
            std::max(row.base_size, 0),
            std::max(row.building_functions, 0),
            function_mass(row),
            std::max(row.road_size, 0),
            metrics,
            { 0.0f, 0.0f },
            build_function_layers(row, metrics),
        });
    }

    // Bundle free functions into grouped buildings (triangular, up to N per bundle).
    if (!free_functions.empty())
    {
        const int max_per_bundle = std::max(config.functions_per_building_max, 1);
        const int bundle_count = (static_cast<int>(free_functions.size()) + max_per_bundle - 1) / max_per_bundle;
        for (int bi = 0; bi < bundle_count; ++bi)
        {
            const size_t start = static_cast<size_t>(bi) * max_per_bundle;
            const size_t end = std::min(start + static_cast<size_t>(max_per_bundle), free_functions.size());

            CityClassRecord bundle_row;
            bundle_row.module_path = std::string(module_path);
            bundle_row.name = bundle_count > 1
                ? "Functions " + std::to_string(bi + 1)
                : "Functions";
            bundle_row.qualified_name = bundle_row.name;
            bundle_row.entity_kind = "tree";
            bundle_row.base_size = 0;
            bundle_row.building_functions = static_cast<int>(end - start);
            bundle_row.road_size = 0;

            struct BundledFunction
            {
                std::string name;
                std::string source_file_path;
                int size = 0;
            };
            std::vector<BundledFunction> bundled;
            bundled.reserve(end - start);
            for (size_t j = start; j < end; ++j)
            {
                const auto& fn = *free_functions[j];
                const int sz = (!fn.function_sizes.empty() && fn.function_sizes[0] > 0) ? fn.function_sizes[0] : 0;
                bundle_row.function_sizes.push_back(sz);
                bundle_row.function_names.push_back(fn.name);
                if (sz > 0)
                    bundled.push_back({ fn.name, fn.source_file_path, sz });
            }

            const BuildingMetrics metrics = derive_building_metrics(bundle_row, config);

            // Build layers with per-function source file paths.
            std::vector<SemanticBuildingLayer> layers;
            if (bundled.empty())
            {
                layers.push_back({ std::string(), std::string(), 0, metrics.height });
            }
            else
            {
                int total_size = 0;
                for (const auto& bf : bundled)
                    total_size += bf.size;
                layers.reserve(bundled.size());
                float remaining_height = metrics.height;
                for (size_t fi = 0; fi < bundled.size(); ++fi)
                {
                    float layer_height = (fi + 1 < bundled.size())
                        ? std::min(metrics.height * static_cast<float>(bundled[fi].size) / static_cast<float>(total_size), remaining_height)
                        : remaining_height;
                    layers.push_back({ bundled[fi].name, bundled[fi].source_file_path, bundled[fi].size, std::max(layer_height, 0.0f) });
                    remaining_height = std::max(0.0f, remaining_height - layer_height);
                }
            }

            module_model.buildings.push_back({
                std::string(module_path),
                bundle_row.name,
                bundle_row.qualified_name,
                "",
                false,
                false,
                true,
                0,
                bundle_row.building_functions,
                function_mass(bundle_row),
                0,
                metrics,
                { 0.0f, 0.0f },
                std::move(layers),
            });

            // Map individual function names to the bundle so dependency routing finds them.
            for (size_t j = start; j < end; ++j)
                module_model.function_bundle_remap[free_functions[j]->qualified_name] = bundle_row.qualified_name;
        }
    }

    // Stack same-footprint structs into grouped buildings (square, up to N per stack).
    if (config.enable_struct_stacking && !config.hide_struct_entities)
    {
        // Collect struct buildings and partition them out of the main list.
        std::vector<SemanticCityBuilding> structs;
        std::vector<SemanticCityBuilding> non_structs;
        for (auto& bldg : module_model.buildings)
        {
            if (bldg.is_struct && !bldg.is_struct_stack)
                structs.push_back(std::move(bldg));
            else
                non_structs.push_back(std::move(bldg));
        }
        module_model.buildings = std::move(non_structs);

        // Group structs by base_size so each stack has uniform footprint.
        std::unordered_map<int, std::vector<size_t>> by_footprint;
        for (size_t i = 0; i < structs.size(); ++i)
            by_footprint[structs[i].base_size].push_back(i);

        const float floor_gap = std::max(config.struct_stack_gap, 0.0f);
        const float brick_gap = std::max(config.struct_brick_gap, 0.0f);
        const int grid_size = std::max(config.struct_brick_grid_size, 1);
        const int bricks_per_floor = brick_slots_per_floor(grid_size);
        const int max_per_stack = std::max(config.struct_stack_max, 1) * bricks_per_floor;

        // Count total stacks across all base_size groups so names are unique.
        int total_stacks = 0;
        for (const auto& [bs, idxs] : by_footprint)
        {
            if (idxs.size() <= 1)
                continue;
            total_stacks += (static_cast<int>(idxs.size()) + max_per_stack - 1) / max_per_stack;
        }

        int stack_index = 0;
        for (auto& [base_size, indices] : by_footprint)
        {
            if (indices.size() <= 1)
            {
                for (size_t idx : indices)
                    module_model.buildings.push_back(std::move(structs[idx]));
                continue;
            }
            const int group_stack_count = (static_cast<int>(indices.size()) + max_per_stack - 1) / max_per_stack;
            for (int si = 0; si < group_stack_count; ++si)
            {
                ++stack_index;
                const size_t start = static_cast<size_t>(si) * max_per_stack;
                const size_t end = std::min(start + static_cast<size_t>(max_per_stack), indices.size());
                const size_t plate_count = end - start;

                const SemanticCityBuilding& first = structs[indices[start]];
                BuildingMetrics stack_metrics = first.metrics;

                // One brick per struct — brick size comes from the struct's
                // metrics which already scale with field count.
                std::vector<SemanticBuildingLayer> layers;
                layers.reserve(plate_count);
                for (size_t pi = start; pi < end; ++pi)
                {
                    const auto& s = structs[indices[pi]];
                    layers.push_back({
                        s.qualified_name,
                        s.source_file_path,
                        s.base_size,
                        s.metrics.height,
                    });
                }

                // Brick layout: N bricks per floor, height = sum of floor maxes + floor gaps.
                const int num_floors = (static_cast<int>(plate_count) + bricks_per_floor - 1) / bricks_per_floor;
                float stack_height = 0.0f;
                for (int fi = 0; fi < num_floors; ++fi)
                {
                    const size_t fs = static_cast<size_t>(fi) * bricks_per_floor;
                    const size_t fe = std::min(fs + static_cast<size_t>(bricks_per_floor), layers.size());
                    float floor_height = 0.0f;
                    for (size_t bi = fs; bi < fe; ++bi)
                        floor_height = std::max(floor_height, layers[bi].height);
                    stack_height += floor_height;
                }
                if (num_floors > 1)
                    stack_height += floor_gap * static_cast<float>(num_floors - 1);
                stack_metrics.height = stack_height;

                const std::string stack_name = total_stacks > 1
                    ? "Structs " + std::to_string(stack_index)
                    : "Structs";

                module_model.buildings.push_back({
                    std::string(module_path),
                    stack_name,
                    stack_name,
                    "",
                    true,
                    true,
                    false,
                    base_size,
                    static_cast<int>(plate_count),
                    0,
                    0,
                    stack_metrics,
                    { 0.0f, 0.0f },
                    std::move(layers),
                });

                // Map individual struct names to the stack for dependency routing.
                for (size_t pi = start; pi < end; ++pi)
                    module_model.struct_stack_remap[structs[indices[pi]].qualified_name] = stack_name;
            }
        }
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
    PERF_MEASURE();
    SemanticMegacityModel model;
    model.modules.reserve(modules.size());

    for (const auto& module_input : modules)
    {
        SemanticCityModuleModel module_model = build_semantic_city_model(module_input.module_path, module_input.rows, config);
        if (!module_model.empty())
        {
            module_model.quality = module_input.quality;
            module_model.health = module_input.health;
            model.function_bundle_remap.insert(
                module_model.function_bundle_remap.begin(), module_model.function_bundle_remap.end());
            model.struct_stack_remap.insert(
                module_model.struct_stack_remap.begin(), module_model.struct_stack_remap.end());
            model.modules.push_back(std::move(module_model));
        }

        for (const auto& dep : module_input.dependencies)
        {
            model.dependencies.push_back({
                dep.source_module_path,
                dep.source_qualified_name,
                dep.field_name,
                dep.field_type_name,
                dep.target_module_path,
                dep.target_qualified_name,
                dep.source_file_path,
                dep.target_file_path,
                dep.is_abstract_ref,
            });
        }
    }

    return model;
}

SemanticCityLayout build_semantic_city_layout(
    const SemanticCityModuleModel& module_model, const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    SemanticCityLayout layout;
    if (module_model.empty())
        return layout;

    SpatialLotGrid grid;
    grid.reserve(module_model.buildings.size() + 1);
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
        grid.insert({ -park_lot_half, park_lot_half, -park_lot_half, park_lot_half });
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

        const std::vector<glm::vec2> contact_candidates = touching_lot_candidates(grid, local_lot, config);
        for (const glm::vec2& center : contact_candidates)
        {
            if (try_place_candidate(grid, local_lot, center, chosen_center, chosen_lot))
            {
                placed = true;
                break;
            }
        }

        if (!placed)
        {
            const float spiral_step = std::max(config.placement_step, 0.01f);
            // Start the spiral near the city frontier — inner rings are fully occupied.
            const float city_extent = std::max(layout.max_x - layout.min_x, layout.max_z - layout.min_z) * 0.5f;
            const float local_extent = std::max(local_lot.max_x - local_lot.min_x, local_lot.max_z - local_lot.min_z) * 0.5f;
            const int frontier_ring = std::max(0, static_cast<int>((city_extent - local_extent) / spiral_step) - 2);
            for_each_spiral_candidate(spiral_step, config.max_spiral_rings, [&grid, &local_lot, &chosen_center, &chosen_lot, &placed](const glm::vec2& center) {
                if (!try_place_candidate(grid, local_lot, center, chosen_center, chosen_lot))
                    return true;

                placed = true;
                return false; }, frontier_ring);
        }

        if (!placed)
            continue;

        grid.insert(chosen_lot);
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
    PERF_MEASURE();
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

    SpatialLotGrid module_grid;
    module_grid.reserve(candidates.size() + 1);
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
        const float area_scale = std::clamp(config.central_park_scale.x, 1.0f, 3.0f);
        const float border_scale = std::clamp(config.central_park_scale.y, 1.0f, 3.0f);
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

        module_grid.insert({ -park_lot_half, park_lot_half, -park_lot_half, park_lot_half });
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

        const std::vector<glm::vec2> contact_candidates = touching_lot_candidates(module_grid, candidate.local_lot, config);
        for (const glm::vec2& offset : contact_candidates)
        {
            if (try_place_candidate(module_grid, candidate.local_lot, offset, chosen_offset, chosen_lot))
            {
                placed = true;
                break;
            }
        }

        if (!placed)
        {
            const float lot_width = candidate.local_lot.max_x - candidate.local_lot.min_x;
            const float lot_depth = candidate.local_lot.max_z - candidate.local_lot.min_z;
            const float module_step = std::max(std::min(lot_width, lot_depth) * 0.5f, std::max(config.placement_step, 0.01f));
            for_each_spiral_candidate(module_step, config.max_spiral_rings, [&module_grid, &candidate, &chosen_offset, &chosen_lot, &placed](const glm::vec2& offset) {
                if (!try_place_candidate(module_grid, candidate.local_lot, offset, chosen_offset, chosen_lot))
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

        module_grid.insert(chosen_lot);
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
    SemanticMegacityModel empty_model;
    return build_city_grid(layout, empty_model, config);
}

CityGrid build_city_grid(
    const SemanticMegacityLayout& layout, const SemanticMegacityModel& model, const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
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
    for_each_building([&fill_rect](const SemanticCityBuilding& building) {
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
    for_each_building([&fill_rect](const SemanticCityBuilding& building) {
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

std::vector<CityGrid::RoutePolyline> build_city_routes(
    const SemanticMegacityLayout& layout, const SemanticMegacityModel& model, const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    const CityGrid routing_grid = build_city_grid(layout, config);
    return build_city_routes_from_grid(layout, model, routing_grid, config, {}, {}, {});
}

std::vector<CityGrid::RoutePolyline> build_city_routes_for_selection(
    const SemanticMegacityLayout& layout, const SemanticMegacityModel& model, const CityGrid& grid,
    const MegaCityCodeConfig& config,
    std::string_view focus_source_file_path,
    std::string_view focus_module_path,
    std::string_view focus_qualified_name,
    std::string_view focus_function_name)
{
    PERF_MEASURE();
    if (focus_qualified_name.empty())
        return {};
    return build_city_routes_from_grid(
        layout,
        model,
        grid,
        config,
        focus_source_file_path,
        focus_module_path,
        focus_qualified_name,
        focus_function_name);
}

std::vector<CityGrid::RouteRenderSegment> build_city_route_render_segments(
    const std::vector<CityGrid::RoutePolyline>& routes, float lane_spacing)
{
    PERF_MEASURE();
    (void)lane_spacing;

    std::vector<CityGrid::RouteRenderSegment> segments;
    for (const auto& route : routes)
    {
        if (route.world_points.size() < 2)
            continue;

        float total_length = 0.0f;
        for (size_t point_index = 1; point_index < route.world_points.size(); ++point_index)
            total_length += glm::length(route.world_points[point_index] - route.world_points[point_index - 1]);
        if (total_length <= 1e-4f)
            continue;

        float traversed_length = 0.0f;
        for (size_t point_index = 1; point_index < route.world_points.size(); ++point_index)
        {
            const glm::vec2 a = route.world_points[point_index - 1];
            const glm::vec2 b = route.world_points[point_index];
            const float segment_length = glm::length(b - a);
            if (segment_length <= 1e-4f)
                continue;

            const float segment_mid_length = traversed_length + segment_length * 0.5f;
            const float color_t = std::clamp(segment_mid_length / total_length, 0.0f, 1.0f);
            const glm::vec4 segment_color = glm::mix(route.source_color, route.target_color, color_t);
            segments.push_back(
                {
                    a,
                    b,
                    segment_color,
                    route.source_module_path,
                    route.source_qualified_name,
                    route.target_module_path,
                    route.target_qualified_name,
                });
            traversed_length += segment_length;
        }
    }

    return segments;
}

} // namespace draxul
