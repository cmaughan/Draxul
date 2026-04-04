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
        return { { std::string(), 0, metrics.height } };

    int total_size = 0;
    for (const auto& entry : entries)
        total_size += entry.size;
    if (total_size <= 0)
        return { { std::string(), 0, metrics.height } };

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

        layers.push_back({ entries[index].name, function_size, std::max(layer_height, 0.0f) });
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
    PERF_MEASURE();
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
    PERF_MEASURE();
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
    const std::vector<LotRect>& reserved_lots, const LotRect& local_lot, const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
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
    bool is_abstract_ref = false;
};

struct RouteObstacle
{
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
};

constexpr float kModuleSignObstaclePadding = 0.08f;

struct VisibilityGraph
{
    CitySurfaceBounds bounds;
    std::vector<RouteObstacle> obstacles;
    std::vector<glm::vec2> nodes;
    std::vector<std::vector<std::pair<int, float>>> adjacency;
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
    std::string_view focus_qualified_name)
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

    // Deduplicate by (source, target) — keep the first non-std::function match per pair.
    // Abstract fan-outs (same source, same field, multiple targets) are NOT deduped.
    std::unordered_map<std::string, size_t> seen_pairs; // edge_key → index in route_pairs
    std::vector<RoutePair> route_pairs;
    route_pairs.reserve(model.dependencies.size());
    for (const auto& dep : model.dependencies)
    {
        const std::string source_key = building_key(
            dep.source_file_path, dep.source_module_path, dep.source_qualified_name);
        const std::string target_key = building_key(
            dep.target_file_path, dep.target_module_path, dep.target_qualified_name);
        if (!focus_key.empty() && source_key != focus_key && target_key != focus_key)
        {
            continue;
        }

        if (source_key == target_key)
            continue;
        const auto source_it = buildings_by_key.find(source_key);
        const auto target_it = buildings_by_key.find(target_key);
        if (source_it == buildings_by_key.end() || target_it == buildings_by_key.end())
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
                existing = { source_it->second, target_it->second, dep.source_qualified_name, dep.target_qualified_name,
                    dep.field_name, dep.field_type_name, dep.is_abstract_ref };
            continue;
        }

        route_pairs.push_back({
            source_it->second,
            target_it->second,
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

std::vector<AssignedRoutePorts> assign_route_ports(
    const std::vector<RoutePair>& route_pairs, const CityGrid& grid)
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
            const std::optional<BuildingRoutePort> port
                = make_building_route_port(building, requests[index].side, tangent_offset, grid);
            if (!port.has_value())
                continue;

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

bool point_within_bounds(const glm::vec2& point, const CitySurfaceBounds& bounds)
{
    constexpr float kEpsilon = 1e-4f;
    return point.x >= bounds.min_x - kEpsilon && point.x <= bounds.max_x + kEpsilon
        && point.y >= bounds.min_z - kEpsilon && point.y <= bounds.max_z + kEpsilon;
}

bool point_strictly_inside_obstacle(const glm::vec2& point, const RouteObstacle& obstacle)
{
    constexpr float kEpsilon = 1e-4f;
    return point.x > obstacle.min_x + kEpsilon && point.x < obstacle.max_x - kEpsilon
        && point.y > obstacle.min_z + kEpsilon && point.y < obstacle.max_z - kEpsilon;
}

bool liang_barsky_clip(float p, float q, float& t0, float& t1)
{
    constexpr float kEpsilon = 1e-6f;
    if (std::abs(p) <= kEpsilon)
        return q >= 0.0f;

    const float r = q / p;
    if (p < 0.0f)
    {
        if (r > t1)
            return false;
        if (r > t0)
            t0 = r;
    }
    else
    {
        if (r < t0)
            return false;
        if (r < t1)
            t1 = r;
    }
    return true;
}

bool segment_intersects_obstacle_interior(const glm::vec2& a, const glm::vec2& b, const RouteObstacle& obstacle)
{
    if (point_strictly_inside_obstacle(a, obstacle) || point_strictly_inside_obstacle(b, obstacle))
        return true;

    float t0 = 0.0f;
    float t1 = 1.0f;
    const glm::vec2 delta = b - a;
    if (!liang_barsky_clip(-delta.x, a.x - obstacle.min_x, t0, t1))
        return false;
    if (!liang_barsky_clip(delta.x, obstacle.max_x - a.x, t0, t1))
        return false;
    if (!liang_barsky_clip(-delta.y, a.y - obstacle.min_z, t0, t1))
        return false;
    if (!liang_barsky_clip(delta.y, obstacle.max_z - a.y, t0, t1))
        return false;
    if (t1 < t0)
        return false;

    const glm::vec2 sample = glm::mix(a, b, (t0 + t1) * 0.5f);
    return point_strictly_inside_obstacle(sample, obstacle);
}

std::vector<RouteObstacle> build_route_obstacles(const SemanticMegacityLayout& layout, const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    std::vector<RouteObstacle> obstacles;
    obstacles.reserve(layout.building_count() + layout.modules.size() * 3);

    for (const auto& module_layout : layout.modules)
    {
        for (const auto& building : module_layout.buildings)
        {
            const float half_extent = building.metrics.footprint * 0.5f + building.metrics.sidewalk_width;
            obstacles.push_back({
                building.center.x - half_extent,
                building.center.x + half_extent,
                building.center.y - half_extent,
                building.center.y + half_extent,
            });
        }

        if (module_layout.park_footprint > 0.0f)
        {
            const float half_extent = module_layout.park_footprint * 0.5f + module_layout.park_sidewalk_width;
            obstacles.push_back({
                module_layout.park_center.x - half_extent,
                module_layout.park_center.x + half_extent,
                module_layout.park_center.y - half_extent,
                module_layout.park_center.y + half_extent,
            });
        }

        if (!module_layout.is_central_park && !module_layout.buildings.empty())
        {
            for (const ModuleBoundarySignPlacement& sign : build_module_boundary_sign_placements(module_layout, config))
            {
                obstacles.push_back({
                    sign.center.x - sign.width * 0.5f - kModuleSignObstaclePadding,
                    sign.center.x + sign.width * 0.5f + kModuleSignObstaclePadding,
                    sign.center.y - sign.depth * 0.5f - kModuleSignObstaclePadding,
                    sign.center.y + sign.depth * 0.5f + kModuleSignObstaclePadding,
                });
            }
        }
    }

    return obstacles;
}

bool segment_visible_in_road_space(
    const glm::vec2& a, const glm::vec2& b, const CitySurfaceBounds& bounds, const std::vector<RouteObstacle>& obstacles)
{
    if (!point_within_bounds(a, bounds) || !point_within_bounds(b, bounds))
        return false;

    for (const RouteObstacle& obstacle : obstacles)
    {
        if (segment_intersects_obstacle_interior(a, b, obstacle))
            return false;
    }

    return true;
}

std::vector<glm::vec2> build_static_visibility_nodes(
    const CitySurfaceBounds& bounds, const std::vector<RouteObstacle>& obstacles)
{
    PERF_MEASURE();
    constexpr float kPointMergeEpsilon = 1e-4f;

    std::vector<glm::vec2> nodes;
    nodes.reserve(obstacles.size() * 4 + 4);

    const auto add_unique = [&](const glm::vec2& point) {
        if (!point_within_bounds(point, bounds))
            return;
        for (const RouteObstacle& obstacle : obstacles)
        {
            if (point_strictly_inside_obstacle(point, obstacle))
                return;
        }
        for (const glm::vec2& existing : nodes)
        {
            if (glm::distance2(existing, point) <= kPointMergeEpsilon * kPointMergeEpsilon)
                return;
        }
        nodes.push_back(point);
    };

    add_unique({ bounds.min_x, bounds.min_z });
    add_unique({ bounds.min_x, bounds.max_z });
    add_unique({ bounds.max_x, bounds.min_z });
    add_unique({ bounds.max_x, bounds.max_z });

    for (const RouteObstacle& obstacle : obstacles)
    {
        add_unique({ obstacle.min_x, obstacle.min_z });
        add_unique({ obstacle.min_x, obstacle.max_z });
        add_unique({ obstacle.max_x, obstacle.min_z });
        add_unique({ obstacle.max_x, obstacle.max_z });
    }

    return nodes;
}

VisibilityGraph build_visibility_graph(
    const CitySurfaceBounds& bounds, std::vector<RouteObstacle> obstacles)
{
    PERF_MEASURE();
    VisibilityGraph graph;
    graph.bounds = bounds;
    graph.obstacles = std::move(obstacles);
    graph.nodes = build_static_visibility_nodes(graph.bounds, graph.obstacles);
    graph.adjacency.resize(graph.nodes.size());

    for (size_t i = 0; i < graph.nodes.size(); ++i)
    {
        for (size_t j = i + 1; j < graph.nodes.size(); ++j)
        {
            if (!segment_visible_in_road_space(graph.nodes[i], graph.nodes[j], graph.bounds, graph.obstacles))
                continue;
            const float length = glm::length(graph.nodes[j] - graph.nodes[i]);
            graph.adjacency[i].push_back({ static_cast<int>(j), length });
            graph.adjacency[j].push_back({ static_cast<int>(i), length });
        }
    }

    return graph;
}

int add_visibility_endpoint_node(
    const VisibilityGraph& graph,
    std::vector<glm::vec2>& nodes,
    std::vector<std::vector<std::pair<int, float>>>& adjacency,
    const glm::vec2& point)
{
    PERF_MEASURE();
    constexpr float kPointMergeEpsilon = 1e-4f;

    if (!point_within_bounds(point, graph.bounds))
        return -1;
    for (const RouteObstacle& obstacle : graph.obstacles)
    {
        if (point_strictly_inside_obstacle(point, obstacle))
            return -1;
    }

    for (size_t index = 0; index < nodes.size(); ++index)
    {
        if (glm::distance2(nodes[index], point) <= kPointMergeEpsilon * kPointMergeEpsilon)
            return static_cast<int>(index);
    }

    const int node_index = static_cast<int>(nodes.size());
    nodes.push_back(point);
    adjacency.emplace_back();
    for (int existing_index = 0; existing_index < node_index; ++existing_index)
    {
        if (!segment_visible_in_road_space(point, nodes[static_cast<size_t>(existing_index)], graph.bounds, graph.obstacles))
            continue;
        const float length = glm::length(point - nodes[static_cast<size_t>(existing_index)]);
        adjacency[static_cast<size_t>(node_index)].push_back({ existing_index, length });
        adjacency[static_cast<size_t>(existing_index)].push_back({ node_index, length });
    }

    return node_index;
}

bool find_visibility_path(
    const VisibilityGraph& graph,
    const glm::vec2& start,
    const glm::vec2& goal,
    std::vector<glm::vec2>& path_out)
{
    PERF_MEASURE();
    path_out.clear();
    if (!point_within_bounds(start, graph.bounds) || !point_within_bounds(goal, graph.bounds))
        return false;

    std::vector<glm::vec2> nodes = graph.nodes;
    std::vector<std::vector<std::pair<int, float>>> adjacency = graph.adjacency;
    const int start_index = add_visibility_endpoint_node(graph, nodes, adjacency, start);
    const int goal_index = add_visibility_endpoint_node(graph, nodes, adjacency, goal);
    if (start_index < 0 || goal_index < 0)
        return false;
    if (start_index == goal_index)
    {
        path_out.push_back(nodes[static_cast<size_t>(start_index)]);
        return true;
    }

    const size_t node_count = nodes.size();

    std::vector<float> g_score(node_count, std::numeric_limits<float>::max());
    std::vector<int> predecessor(node_count, -1);
    using QueueEntry = std::pair<float, int>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> frontier;

    g_score[start_index] = 0.0f;
    frontier.push({ glm::distance(start, goal), start_index });

    while (!frontier.empty())
    {
        const auto [f_score, current] = frontier.top();
        (void)f_score;
        frontier.pop();

        if (current == goal_index)
            break;

        for (const auto& [next, edge_cost] : adjacency[current])
        {
            const float tentative_g = g_score[current] + edge_cost;
            if (tentative_g + 1e-5f >= g_score[next])
                continue;
            g_score[next] = tentative_g;
            predecessor[next] = current;
            const float heuristic = glm::length(nodes[goal_index] - nodes[next]);
            frontier.push({ tentative_g + heuristic, next });
        }
    }

    if (g_score[goal_index] == std::numeric_limits<float>::max())
        return false;

    int current = goal_index;
    while (current >= 0)
    {
        path_out.push_back(nodes[current]);
        if (current == start_index)
            break;
        current = predecessor[current];
    }
    if (path_out.empty() || glm::distance2(path_out.back(), start) > 1e-8f)
    {
        path_out.clear();
        return false;
    }

    std::reverse(path_out.begin(), path_out.end());
    return true;
}

std::vector<CityGrid::RoutePolyline> build_city_routes_from_grid(
    const SemanticMegacityLayout& layout, const SemanticMegacityModel& model, const CityGrid& grid,
    const MegaCityCodeConfig& config,
    std::string_view focus_source_file_path,
    std::string_view focus_module_path,
    std::string_view focus_qualified_name)
{
    PERF_MEASURE();
    (void)grid;
    const std::vector<RoutePair> route_pairs = collect_route_pairs(
        layout,
        model,
        focus_source_file_path,
        focus_module_path,
        focus_qualified_name);
    const std::vector<AssignedRoutePorts> assigned_ports = assign_route_ports(route_pairs, grid);
    const CitySurfaceBounds road_bounds = compute_city_road_surface_bounds(layout);
    const VisibilityGraph visibility_graph = build_visibility_graph(road_bounds, build_route_obstacles(layout, config));

    std::vector<std::optional<CityGrid::RoutePolyline>> route_results(route_pairs.size());
    const auto solve_route = [&](size_t route_index) {
        const RoutePair& pair = route_pairs[route_index];
        const AssignedRoutePorts& ports = assigned_ports[route_index];
        if (pair.source == nullptr || pair.target == nullptr
            || !ports.source_port.has_value() || !ports.target_port.has_value()
            || !visibility_graph.bounds.valid())
        {
            return;
        }

        std::vector<glm::vec2> road_path;
        if (!find_visibility_path(
                visibility_graph,
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
    const float base_elev = route_base_elevation(config);
    const float layer_step = std::max(config.dependency_route_layer_step, 0.0f);
    std::unordered_map<std::string, int> side_layer_counters;
    for (auto& route : routes)
    {
        if (route.world_points.size() < 2)
        {
            route.elevation = base_elev;
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
        route.elevation = base_elev + static_cast<float>(side_layer) * layer_step;
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

    for (const auto& row : rows)
    {
        if ((row.entity_kind != "building" && row.entity_kind != "block") || row.is_abstract)
            continue;
        if (config.hide_test_entities && is_test_semantic_source(row.source_file_path))
            continue;
        if (config.hide_struct_entities && row.is_struct)
            continue;

        const BuildingMetrics metrics = derive_building_metrics(row, config);
        module_model.connectivity += std::max(row.road_size, 0);
        module_model.buildings.push_back({
            row.module_path.empty() ? std::string(module_path) : row.module_path,
            row.name,
            row.qualified_name,
            row.source_file_path,
            row.is_struct,
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
            for_each_spiral_candidate(config, [&reserved_lots, &local_lot, &chosen_center, &chosen_lot, &placed](const glm::vec2& center) {
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
            for_each_spiral_candidate(config, [&reserved_modules, &candidate, &chosen_offset, &chosen_lot, &placed](const glm::vec2& offset) {
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
    std::string_view focus_qualified_name)
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
        focus_qualified_name);
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
