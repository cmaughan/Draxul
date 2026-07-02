#pragma once

#include <draxul/codeviz_scene_types.h>

#include <cstdint>

namespace draxul
{

enum class CityMeshId : uint32_t
{
    RoadSurface,
    TreeBark,
    TreeLeaves,
    RoofSign,
    WallSign,
};

constexpr CodeVizMeshId codeviz_mesh_id(CityMeshId id)
{
    switch (id)
    {
    case CityMeshId::RoadSurface:
        return CodeVizMeshId::TexturedSurface;
    case CityMeshId::TreeBark:
        return CodeVizMeshId::FoliageStem;
    case CityMeshId::TreeLeaves:
        return CodeVizMeshId::FoliageCards;
    case CityMeshId::RoofSign:
        return CodeVizMeshId::TopLabelPanel;
    case CityMeshId::WallSign:
        return CodeVizMeshId::FrontLabelPanel;
    }
    return CodeVizMeshId::Cube;
}

constexpr CodeVizMeshId kCityRoadSurfaceMesh = codeviz_mesh_id(CityMeshId::RoadSurface);
constexpr CodeVizMeshId kCityTreeBarkMesh = codeviz_mesh_id(CityMeshId::TreeBark);
constexpr CodeVizMeshId kCityTreeLeavesMesh = codeviz_mesh_id(CityMeshId::TreeLeaves);
constexpr CodeVizMeshId kCityRoofSignMesh = codeviz_mesh_id(CityMeshId::RoofSign);
constexpr CodeVizMeshId kCityWallSignMesh = codeviz_mesh_id(CityMeshId::WallSign);

} // namespace draxul
