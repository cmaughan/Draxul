#pragma once

#include <draxul/satview/satview_surface_catalog.h>

#include <string_view>

namespace draxul::satview
{

using SatViewLunarSurfaceKind = SatViewSurfaceKind;
using SatViewLunarLocationQuality = SatViewSurfaceLocationQuality;
using SatViewLunarSurfaceObject = SatViewSurfaceObject;
using SatViewLunarSurfaceCatalog = SatViewSurfaceCatalog;

[[nodiscard]] std::string_view satview_lunar_surface_kind_name(
    SatViewLunarSurfaceKind kind);
[[nodiscard]] std::string_view satview_lunar_location_quality_name(
    SatViewLunarLocationQuality quality);
[[nodiscard]] SatViewLunarSurfaceCatalog parse_satview_lunar_surface_catalog(
    std::string_view csv);
[[nodiscard]] SatViewLunarSurfaceCatalog load_satview_lunar_surface_catalog();

} // namespace draxul::satview
