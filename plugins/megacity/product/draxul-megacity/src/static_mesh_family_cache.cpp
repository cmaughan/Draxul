#include "static_mesh_family_cache.h"

#include <algorithm>
#include <cmath>
#include <draxul/building_generator.h>
#include <draxul/roof_sign_generator.h>
#include <string>

namespace draxul
{

namespace
{

int32_t quantize_key_float(float value)
{
    return static_cast<int32_t>(std::lround(static_cast<double>(value) * 1000.0));
}

void append_key(std::string& key, int64_t value)
{
    key.append(std::to_string(value));
    key.push_back('|');
}

void append_key(std::string& key, float value)
{
    append_key(key, static_cast<int64_t>(quantize_key_float(value)));
}

float clamped_fraction(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

std::string key_for(const StaticBuildingRingSpec& spec)
{
    std::string key;
    append_key(key, static_cast<int64_t>(std::max(spec.sides, 3)));
    append_key(key, std::max(spec.middle_strip_scale, 0.05f));
    append_key(key, std::max(spec.level_gap_fraction, 0.0f));
    append_key(key, static_cast<int64_t>(spec.layers.size()));
    for (const StaticBuildingLayerSpec& layer : spec.layers)
    {
        append_key(key, std::max(layer.height_fraction, 0.0f));
        append_key(key, clamped_fraction(layer.color_multiplier));
        append_key(key, static_cast<int64_t>(layer.layer_id));
    }
    return key;
}

std::string key_for(const StaticBrickStackSpec& spec)
{
    std::string key;
    append_key(key, static_cast<int64_t>(std::max(spec.grid_size, 1)));
    append_key(key, std::max(spec.brick_gap_fraction, 0.0f));
    append_key(key, std::max(spec.floor_gap_fraction, 0.0f));
    append_key(key, static_cast<int64_t>(spec.bricks.size()));
    for (const StaticBuildingLayerSpec& brick : spec.bricks)
    {
        append_key(key, std::max(brick.height_fraction, 0.0f));
        append_key(key, clamped_fraction(brick.color_multiplier));
        append_key(key, static_cast<int64_t>(brick.layer_id));
    }
    return key;
}

std::string key_for(const StaticRoofSignRingSpec& spec)
{
    std::string key;
    append_key(key, static_cast<int64_t>(std::max(spec.sides, 3)));
    append_key(key, std::clamp(spec.inner_radius_fraction, 0.01f, 0.49f));
    return key;
}

std::string key_for(const StaticSidewalkRingSpec& spec)
{
    std::string key;
    append_key(key, static_cast<int64_t>(std::max(spec.sides, 3)));
    append_key(key, std::clamp(spec.inner_radius_fraction, 0.01f, 0.49f));
    append_key(key, spec.color.x);
    append_key(key, spec.color.y);
    append_key(key, spec.color.z);
    return key;
}

} // namespace

std::shared_ptr<const GeometryMesh> StaticMeshFamilyCache::building_ring(const StaticBuildingRingSpec& spec)
{
    const std::string key = key_for(spec);
    if (const auto it = building_rings_.find(key); it != building_rings_.end())
        return it->second;

    DraxulBuildingParams params;
    params.footprint = 1.0f;
    params.sides = std::max(spec.sides, 3);
    params.middle_strip_scale = std::max(spec.middle_strip_scale, 0.05f);
    params.level_gap = std::max(spec.level_gap_fraction, 0.0f);

    if (spec.layers.empty())
    {
        params.levels.push_back({ 1.0f, glm::vec3(1.0f), 0u });
    }
    else
    {
        params.levels.reserve(spec.layers.size());
        for (const StaticBuildingLayerSpec& layer : spec.layers)
        {
            if (layer.height_fraction <= 1e-4f)
                continue;
            const float color = clamped_fraction(layer.color_multiplier);
            params.levels.push_back({ layer.height_fraction, glm::vec3(color), layer.layer_id });
        }
    }

    if (params.levels.empty())
        params.levels.push_back({ 1.0f, glm::vec3(1.0f), 0u });

    auto mesh = std::make_shared<GeometryMesh>(generate_draxul_building(params));
    return building_rings_.emplace(key, std::move(mesh)).first->second;
}

std::shared_ptr<const GeometryMesh> StaticMeshFamilyCache::brick_stack(const StaticBrickStackSpec& spec)
{
    const std::string key = key_for(spec);
    if (const auto it = brick_stacks_.find(key); it != brick_stacks_.end())
        return it->second;

    DraxulBrickBuildingParams params;
    params.footprint = 1.0f;
    params.grid_size = std::max(spec.grid_size, 1);
    params.brick_gap = std::max(spec.brick_gap_fraction, 0.0f);
    params.floor_gap = std::max(spec.floor_gap_fraction, 0.0f);
    params.bricks.reserve(spec.bricks.size());
    for (const StaticBuildingLayerSpec& brick : spec.bricks)
    {
        if (brick.height_fraction <= 1e-4f)
            continue;
        const float color = clamped_fraction(brick.color_multiplier);
        params.bricks.push_back({ brick.height_fraction, glm::vec3(color), brick.layer_id });
    }
    if (params.bricks.empty())
        params.bricks.push_back({ 1.0f, glm::vec3(1.0f), 0u });

    auto mesh = std::make_shared<GeometryMesh>(generate_draxul_brick_building(params));
    return brick_stacks_.emplace(key, std::move(mesh)).first->second;
}

std::shared_ptr<const GeometryMesh> StaticMeshFamilyCache::building_cap(
    int sides, float middle_strip_scale, uint32_t layer_id)
{
    std::string key;
    append_key(key, static_cast<int64_t>(std::max(sides, 3)));
    append_key(key, std::max(middle_strip_scale, 0.05f));
    append_key(key, static_cast<int64_t>(layer_id));
    if (const auto it = building_caps_.find(key); it != building_caps_.end())
        return it->second;

    DraxulBuildingParams params;
    params.footprint = 1.0f;
    params.sides = std::max(sides, 3);
    params.middle_strip_scale = std::max(middle_strip_scale, 0.05f);
    params.levels.push_back({ 1.0f, glm::vec3(1.0f), layer_id });

    auto mesh = std::make_shared<GeometryMesh>(generate_draxul_building(params));
    return building_caps_.emplace(key, std::move(mesh)).first->second;
}

std::shared_ptr<const GeometryMesh> StaticMeshFamilyCache::roof_sign_ring(const StaticRoofSignRingSpec& spec)
{
    const std::string key = key_for(spec);
    if (const auto it = roof_sign_rings_.find(key); it != roof_sign_rings_.end())
        return it->second;

    const float inner = std::clamp(spec.inner_radius_fraction, 0.01f, 0.49f);
    DraxulRoofSignParams params;
    params.sides = std::max(spec.sides, 3);
    params.inner_radius = inner;
    params.band_depth = 0.5f - inner;
    params.height = 1.0f;
    params.color = glm::vec3(1.0f);

    auto mesh = std::make_shared<GeometryMesh>(generate_draxul_roof_sign(params));
    return roof_sign_rings_.emplace(key, std::move(mesh)).first->second;
}

std::shared_ptr<const GeometryMesh> StaticMeshFamilyCache::sidewalk_ring(const StaticSidewalkRingSpec& spec)
{
    const std::string key = key_for(spec);
    if (const auto it = sidewalk_rings_.find(key); it != sidewalk_rings_.end())
        return it->second;

    const float inner = std::clamp(spec.inner_radius_fraction, 0.01f, 0.49f);
    auto mesh = std::make_shared<GeometryMesh>(
        generate_sidewalk_ring(std::max(spec.sides, 3), inner, 0.5f, 0.0f, 1.0f, spec.color));
    return sidewalk_rings_.emplace(key, std::move(mesh)).first->second;
}

size_t StaticMeshFamilyCache::size() const
{
    return building_rings_.size()
        + brick_stacks_.size()
        + building_caps_.size()
        + roof_sign_rings_.size()
        + sidewalk_rings_.size();
}

void StaticMeshFamilyCache::clear()
{
    building_rings_.clear();
    brick_stacks_.clear();
    building_caps_.clear();
    roof_sign_rings_.clear();
    sidewalk_rings_.clear();
}

} // namespace draxul
