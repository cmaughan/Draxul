#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace draxul::satview
{

enum class OrbitClass
{
    LowEarth,
    MediumEarth,
    Geosynchronous,
    HighlyElliptical,
    Other
};

struct SatelliteRecord
{
    std::int64_t norad_catalog_id = 0;
    std::string object_name;
    std::string object_id;
    std::string object_type;
    std::string epoch_utc;
    std::string classification_type;

    double mean_motion_rev_per_day = 0.0;
    double eccentricity = 0.0;
    double inclination_deg = 0.0;
    double right_ascension_ascending_node_deg = 0.0;
    double argument_of_pericenter_deg = 0.0;
    double mean_anomaly_deg = 0.0;
    double bstar = 0.0;
    double mean_motion_dot = 0.0;
    double mean_motion_ddot = 0.0;
    int ephemeris_type = 0;
    int element_set_no = 0;
    int revolution_at_epoch = 0;

    double period_minutes = 0.0;
    double perigee_km = 0.0;
    double apogee_km = 0.0;
    OrbitClass orbit_class = OrbitClass::Other;
};

struct SatelliteCatalog
{
    std::string source_label;
    std::string source_url;
    std::vector<SatelliteRecord> objects;
    std::size_t skipped_records = 0;
};

struct CatalogParseResult
{
    SatelliteCatalog catalog;
    std::string error;

    [[nodiscard]] explicit operator bool() const
    {
        return error.empty();
    }
};

[[nodiscard]] std::string_view orbit_class_name(OrbitClass orbit_class);

[[nodiscard]] CatalogParseResult parse_celestrak_gp_json(
    std::string_view json,
    std::string_view source_label = {},
    std::string_view source_url = {});

[[nodiscard]] CatalogParseResult load_sample_satellite_catalog();

} // namespace draxul::satview
