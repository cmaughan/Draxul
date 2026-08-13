#include <draxul/satview/satview_lunar_surface_catalog.h>

namespace draxul::satview
{

std::string_view satview_lunar_surface_kind_name(SatViewLunarSurfaceKind kind)
{
    return satview_surface_kind_name(kind);
}

std::string_view satview_lunar_location_quality_name(
    SatViewLunarLocationQuality quality)
{
    return satview_surface_location_quality_name(quality);
}

SatViewLunarSurfaceCatalog parse_satview_lunar_surface_catalog(std::string_view csv)
{
    return parse_satview_surface_catalog(csv, CentralBody::Moon);
}

SatViewLunarSurfaceCatalog load_satview_lunar_surface_catalog()
{
    return load_satview_surface_catalog("catalog/lunar_surface_objects.csv", CentralBody::Moon);
}

} // namespace draxul::satview
