#pragma once

#include <cstdint>
#include <draxul/codeviz_scene_types.h>

namespace draxul
{

enum class CityMaterialId : uint8_t
{
    FlatColor,
    AsphaltRoad,
    PavingSidewalk,
    WoodBuilding,
    LeafCards,
    TreeBark,
};

constexpr CodeVizMaterialPreset codeviz_material_preset(CityMaterialId material)
{
    switch (material)
    {
    case CityMaterialId::AsphaltRoad:
        return CodeVizMaterialPreset::TexturedPbr0;
    case CityMaterialId::PavingSidewalk:
        return CodeVizMaterialPreset::TexturedPbr1;
    case CityMaterialId::WoodBuilding:
        return CodeVizMaterialPreset::VertexTintPbr0;
    case CityMaterialId::LeafCards:
        return CodeVizMaterialPreset::AlphaMaskedPbr0;
    case CityMaterialId::TreeBark:
        return CodeVizMaterialPreset::TexturedPbr2;
    case CityMaterialId::FlatColor:
    default:
        return CodeVizMaterialPreset::FlatColor;
    }
}

constexpr CodeVizMaterialPreset kCityFlatColorMaterial = codeviz_material_preset(CityMaterialId::FlatColor);
constexpr CodeVizMaterialPreset kCityAsphaltRoadMaterial = codeviz_material_preset(CityMaterialId::AsphaltRoad);
constexpr CodeVizMaterialPreset kCityPavingSidewalkMaterial = codeviz_material_preset(CityMaterialId::PavingSidewalk);
constexpr CodeVizMaterialPreset kCityWoodBuildingMaterial = codeviz_material_preset(CityMaterialId::WoodBuilding);
constexpr CodeVizMaterialPreset kCityLeafCardMaterial = codeviz_material_preset(CityMaterialId::LeafCards);
constexpr CodeVizMaterialPreset kCityTreeBarkMaterial = codeviz_material_preset(CityMaterialId::TreeBark);

} // namespace draxul
