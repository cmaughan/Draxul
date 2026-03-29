#include "city_picking.h"
#include "city_builder.h"
#include "city_helpers.h"
#include "isometric_camera.h"
#include "semantic_city_layout.h"
#include <cmath>
#include <draxul/megacity_code_config.h>
#include <draxul/perf_timing.h>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <unordered_map>

namespace draxul
{

namespace
{

struct PickingLevel
{
    float lower_y = 0.0f;
    float upper_y = 0.0f;
    uint32_t layer_id = 0u;
};

struct RayBuildingHit
{
    float t = std::numeric_limits<float>::max();
    float hit_y = 0.0f;
    uint32_t layer_index = 0u;
};

bool ray_triangle_intersect(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_dir,
    const glm::vec3& v0,
    const glm::vec3& v1,
    const glm::vec3& v2,
    float& out_t)
{
    PERF_MEASURE();
    constexpr float kEpsilon = 1e-7f;
    const glm::vec3 edge1 = v1 - v0;
    const glm::vec3 edge2 = v2 - v0;
    const glm::vec3 pvec = glm::cross(ray_dir, edge2);
    const float det = glm::dot(edge1, pvec);
    if (std::abs(det) < kEpsilon)
        return false;

    const float inv_det = 1.0f / det;
    const glm::vec3 tvec = ray_origin - v0;
    const float u = glm::dot(tvec, pvec) * inv_det;
    if (u < 0.0f || u > 1.0f)
        return false;

    const glm::vec3 qvec = glm::cross(tvec, edge1);
    const float v = glm::dot(ray_dir, qvec) * inv_det;
    if (v < 0.0f || (u + v) > 1.0f)
        return false;

    const float t = glm::dot(edge2, qvec) * inv_det;
    if (t < 0.0f)
        return false;

    out_t = t;
    return true;
}

std::vector<glm::vec2> make_building_contour_points(const SemanticCityBuilding& building, int sides, float scale)
{
    PERF_MEASURE();
    std::vector<glm::vec2> points;
    const int clamped_sides = std::max(sides, 3);
    const float half = std::max(building.metrics.footprint, 0.1f) * 0.5f * std::max(scale, 0.05f);
    points.reserve(static_cast<size_t>(clamped_sides));
    if (clamped_sides == 4)
    {
        points.push_back(building.center + glm::vec2(-half, half));
        points.push_back(building.center + glm::vec2(half, half));
        points.push_back(building.center + glm::vec2(half, -half));
        points.push_back(building.center + glm::vec2(-half, -half));
    }
    else
    {
        for (int i = 0; i < clamped_sides; ++i)
        {
            const float angle
                = glm::half_pi<float>() - (glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(clamped_sides));
            points.push_back(
                building.center + glm::vec2(std::cos(angle), std::sin(angle)) * half);
        }
    }
    return points;
}

std::vector<PickingLevel> make_picking_levels(const SemanticCityBuilding& building, float base_elevation)
{
    PERF_MEASURE();
    std::vector<PickingLevel> levels;
    if (building.layers.empty())
    {
        levels.push_back(PickingLevel{
            .lower_y = base_elevation,
            .upper_y = base_elevation + std::max(building.metrics.height, 0.1f),
            .layer_id = 0u,
        });
        return levels;
    }

    float current_y = base_elevation;
    levels.reserve(building.layers.size());
    for (size_t layer_index = 0; layer_index < building.layers.size(); ++layer_index)
    {
        const SemanticBuildingLayer& layer = building.layers[layer_index];
        if (layer.height <= 0.0f)
            continue;
        levels.push_back(PickingLevel{
            .lower_y = current_y,
            .upper_y = current_y + layer.height,
            .layer_id = static_cast<uint32_t>(layer_index),
        });
        current_y += layer.height;
    }

    if (levels.empty())
    {
        levels.push_back(PickingLevel{
            .lower_y = base_elevation,
            .upper_y = base_elevation + std::max(building.metrics.height, 0.1f),
            .layer_id = 0u,
        });
    }
    return levels;
}

std::unordered_map<std::string, int> build_incident_connection_counts(const SemanticMegacityModel& model)
{
    PERF_MEASURE();
    std::unordered_map<std::string, int> connection_counts;
    connection_counts.reserve(model.dependencies.size() * 2);
    auto key_for = [](std::string_view source_file_path, std::string_view module_path, std::string_view qualified_name) {
        std::string key;
        key.reserve(source_file_path.size() + module_path.size() + qualified_name.size() + 2);
        key.append(source_file_path);
        key.push_back('\n');
        key.append(module_path);
        key.push_back('\n');
        key.append(qualified_name);
        return key;
    };
    for (const SemanticCityDependency& dependency : model.dependencies)
    {
        ++connection_counts[key_for(
            dependency.source_file_path,
            dependency.source_module_path,
            dependency.source_qualified_name)];
        ++connection_counts[key_for(
            dependency.target_file_path,
            dependency.target_module_path,
            dependency.target_qualified_name)];
    }
    return connection_counts;
}

std::string building_connection_key(
    std::string_view source_file_path,
    std::string_view module_path,
    std::string_view qualified_name)
{
    std::string key;
    key.reserve(source_file_path.size() + module_path.size() + qualified_name.size() + 2);
    key.append(source_file_path);
    key.push_back('\n');
    key.append(module_path);
    key.push_back('\n');
    key.append(qualified_name);
    return key;
}

int building_side_count(
    const SemanticCityBuilding& building,
    const std::unordered_map<std::string, int>* incident_connection_counts,
    const MegaCityCodeConfig* config)
{
    PERF_MEASURE();
    if (!incident_connection_counts || !config)
        return 4;

    const auto it = incident_connection_counts->find(
        building_connection_key(
            building.source_file_path,
            building.module_path,
            building.qualified_name));
    const int incident_connection_count = it != incident_connection_counts->end() ? it->second : 0;
    return procedural_building_side_count(
        incident_connection_count,
        config->connected_hex_building_threshold,
        config->connected_oct_building_threshold);
}

void consider_triangle_hit(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_dir,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    uint32_t layer_index,
    RayBuildingHit& best_hit)
{
    PERF_MEASURE();
    float t = 0.0f;
    if (!ray_triangle_intersect(ray_origin, ray_dir, a, b, c, t))
        return;

    if (t < 0.0f || t >= best_hit.t)
        return;

    const glm::vec3 hit_point = ray_origin + t * ray_dir;
    best_hit.t = t;
    best_hit.hit_y = hit_point.y;
    best_hit.layer_index = layer_index;
}

void consider_strip_hit(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_dir,
    const std::vector<glm::vec2>& lower_contour,
    const std::vector<glm::vec2>& upper_contour,
    float lower_y,
    float upper_y,
    uint32_t layer_index,
    RayBuildingHit& best_hit)
{
    PERF_MEASURE();
    if (lower_contour.size() != upper_contour.size() || lower_contour.size() < 3)
        return;

    for (size_t i = 0; i < lower_contour.size(); ++i)
    {
        const size_t next = (i + 1) % lower_contour.size();
        const glm::vec3 p0(lower_contour[i].x, lower_y, lower_contour[i].y);
        const glm::vec3 p1(lower_contour[next].x, lower_y, lower_contour[next].y);
        const glm::vec3 p2(upper_contour[next].x, upper_y, upper_contour[next].y);
        const glm::vec3 p3(upper_contour[i].x, upper_y, upper_contour[i].y);
        consider_triangle_hit(ray_origin, ray_dir, p0, p1, p2, layer_index, best_hit);
        consider_triangle_hit(ray_origin, ray_dir, p0, p2, p3, layer_index, best_hit);
    }
}

void consider_top_cap_hit(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_dir,
    const std::vector<glm::vec2>& contour,
    float y,
    uint32_t layer_index,
    RayBuildingHit& best_hit)
{
    PERF_MEASURE();
    if (contour.size() < 3)
        return;

    glm::vec2 centroid(0.0f);
    for (const auto& p : contour)
        centroid += p;
    centroid /= static_cast<float>(contour.size());
    const glm::vec3 center(centroid.x, y, centroid.y);
    for (size_t i = 0; i < contour.size(); ++i)
    {
        const size_t next = (i + 1) % contour.size();
        const glm::vec3 a(contour[i].x, y, contour[i].y);
        const glm::vec3 b(contour[next].x, y, contour[next].y);
        consider_triangle_hit(ray_origin, ray_dir, center, a, b, layer_index, best_hit);
    }
}

std::optional<RayBuildingHit> ray_building_intersect(
    const glm::vec3& ray_origin,
    const glm::vec3& ray_dir,
    const SemanticCityBuilding& building,
    int sides,
    float middle_strip_scale,
    float base_elevation,
    float sign_extension)
{
    PERF_MEASURE();
    const std::vector<glm::vec2> outer_contour = make_building_contour_points(building, sides, 1.0f);
    const std::vector<glm::vec2> middle_contour = make_building_contour_points(building, sides, middle_strip_scale);
    if (outer_contour.size() < 3 || middle_contour.size() != outer_contour.size())
        return std::nullopt;

    const std::vector<PickingLevel> levels = make_picking_levels(building, base_elevation);
    if (levels.empty())
        return std::nullopt;

    RayBuildingHit best_hit;
    for (const PickingLevel& level : levels)
    {
        const float strip_height = (level.upper_y - level.lower_y) / 3.0f;
        consider_strip_hit(
            ray_origin,
            ray_dir,
            outer_contour,
            middle_contour,
            level.lower_y,
            level.lower_y + strip_height,
            level.layer_id,
            best_hit);
        consider_strip_hit(
            ray_origin,
            ray_dir,
            middle_contour,
            middle_contour,
            level.lower_y + strip_height,
            level.lower_y + strip_height * 2.0f,
            level.layer_id,
            best_hit);
        consider_strip_hit(
            ray_origin,
            ray_dir,
            middle_contour,
            outer_contour,
            level.lower_y + strip_height * 2.0f,
            level.upper_y,
            level.layer_id,
            best_hit);
    }

    const float top_y = levels.back().upper_y;
    const uint32_t top_layer = levels.back().layer_id;

    // Extend picking through the cap + sign region above the building body.
    if (sign_extension > 0.0f)
    {
        consider_strip_hit(
            ray_origin, ray_dir,
            outer_contour, outer_contour,
            top_y, top_y + sign_extension,
            top_layer, best_hit);
    }

    consider_top_cap_hit(
        ray_origin,
        ray_dir,
        outer_contour,
        top_y + sign_extension,
        top_layer,
        best_hit);

    if (best_hit.t == std::numeric_limits<float>::max())
        return std::nullopt;
    return best_hit;
}

} // namespace

std::optional<PickResult> pick_building(
    const glm::ivec2& screen_pos,
    int viewport_width, int viewport_height,
    const IsometricCamera& camera,
    const SemanticMegacityLayout& layout,
    const std::function<bool(const std::string&, const std::string&, const std::string&)>& filter,
    const SemanticMegacityModel* model,
    const MegaCityCodeConfig* config)
{
    PERF_MEASURE();
    if (viewport_width <= 0 || viewport_height <= 0)
        return std::nullopt;

    // Convert screen pixel to NDC [-1, 1]
    const float ndc_x = 2.0f * static_cast<float>(screen_pos.x) / static_cast<float>(viewport_width) - 1.0f;
    const float ndc_y = 1.0f - 2.0f * static_cast<float>(screen_pos.y) / static_cast<float>(viewport_height);

    const glm::mat4 inv_vp = glm::inverse(camera.proj_matrix() * camera.view_matrix());

    // Unproject near and far points
    glm::vec4 near_h = inv_vp * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    glm::vec4 far_h = inv_vp * glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f);
    near_h /= near_h.w;
    far_h /= far_h.w;

    const glm::vec3 ray_origin(near_h);
    const glm::vec3 ray_dir = glm::normalize(glm::vec3(far_h) - glm::vec3(near_h));
    const std::optional<std::unordered_map<std::string, int>> incident_connection_counts
        = model && config ? std::optional(build_incident_connection_counts(*model)) : std::nullopt;
    const float middle_strip_scale = 1.0f + std::max(config ? config->building_middle_strip_push : 0.0f, 0.0f);
    const float base_elevation = config ? building_base_elevation(*config) : 0.0f;

    float best_t = std::numeric_limits<float>::max();
    std::optional<PickResult> best;

    for (const auto& module_layout : layout.modules)
    {
        for (const auto& building : module_layout.buildings)
        {
            if (filter
                && !filter(
                    building.source_file_path,
                    building.module_path,
                    building.qualified_name))
            {
                continue;
            }

            // Extend picking upward through the cap + sign band above the building body.
            // Cap height is clamped to at most 15% of building height (see compute_building_sign_height),
            // and the sign band has the same height, so the total extension is 2*cap.
            const float cap_height = std::clamp(building.metrics.height * 0.15f, 0.24f, 2.0f);
            const float sign_extension = cap_height * 2.0f;

            const int sides = building_side_count(
                building,
                incident_connection_counts ? &*incident_connection_counts : nullptr,
                config);
            const std::optional<RayBuildingHit> hit = ray_building_intersect(
                ray_origin,
                ray_dir,
                building,
                sides,
                middle_strip_scale,
                base_elevation,
                sign_extension);
            if (hit && hit->t < best_t)
            {
                best_t = hit->t;
                const float body_top = base_elevation + building.metrics.height;
                const bool hit_on_body = hit->hit_y <= body_top + 1e-3f;
                best = PickResult{
                    building.qualified_name,
                    building.module_path,
                    building.source_file_path,
                    building.center,
                    hit->hit_y,
                    hit->layer_index,
                    hit_on_body,
                };
            }
        }
    }

    return best;
}

} // namespace draxul
