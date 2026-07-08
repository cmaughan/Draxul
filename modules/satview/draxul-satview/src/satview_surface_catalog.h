#pragma once

#include <cstddef>
#include <draxul/satview/satview_catalog.h>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul::satview
{

enum class SatViewSurfaceKind
{
    Lander,
    Rover,
    DeployedInstrument,
    Retroreflector,
    ImpactSite,
    RocketStage,
    CrewedArtifact,
    Unknown
};

enum class SatViewSurfaceLocationQuality
{
    Confirmed,
    Reported,
    Approximate,
    SiteOnly,
    Unknown
};

struct SatViewSurfaceObject
{
    CentralBody body = CentralBody::Moon;
    std::string id;
    std::string mission_id;
    std::string mission_name;
    std::string parent_site_id;
    std::string display_name;
    std::string vehicle_name;
    SatViewSurfaceKind kind = SatViewSurfaceKind::Unknown;
    std::string source_kind;
    std::string status;
    double latitude_degrees = std::numeric_limits<double>::quiet_NaN();
    double longitude_east_degrees = std::numeric_limits<double>::quiet_NaN();
    std::optional<double> coordinate_uncertainty_m;
    std::optional<double> radial_distance_m;
    std::string arrival_date;
    SatViewSurfaceLocationQuality location_quality = SatViewSurfaceLocationQuality::Unknown;
    std::string coordinate_source;
    std::string radial_source;
    std::string cospar_id;
    std::string gcat_id;
    std::string owner;
    std::string country;
    std::vector<std::string> references;
    int display_rank = 0;
    bool site_representative = false;
    bool crewed_mission = false;

    [[nodiscard]] bool renderable() const;
};

struct SatViewSurfaceCatalog
{
    CentralBody body = CentralBody::Moon;
    std::vector<SatViewSurfaceObject> objects;
    std::size_t site_count = 0;
    std::string error;
};

[[nodiscard]] std::string_view satview_surface_kind_name(SatViewSurfaceKind kind);
[[nodiscard]] std::string_view satview_surface_location_quality_name(
    SatViewSurfaceLocationQuality quality);
[[nodiscard]] SatViewSurfaceCatalog parse_satview_surface_catalog(
    std::string_view csv,
    CentralBody body);
[[nodiscard]] SatViewSurfaceCatalog load_satview_surface_catalog(
    std::string_view asset_path,
    CentralBody body);
[[nodiscard]] SatViewSurfaceCatalog load_satview_mars_surface_catalog();

} // namespace draxul::satview
