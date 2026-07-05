#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
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

enum class SatelliteObjectKind
{
    Payload,
    RocketBody,
    Debris,
    Unknown
};

enum class SatellitePopulation
{
    ActivePayload,
    InactivePayload,
    RocketBody,
    Debris,
    Unknown
};

enum class CentralBody
{
    Earth,
    Moon,
    Other
};

enum class OrbitSolutionKind
{
    GeneralPerturbations,
    SatcatSummaryEstimate,
    CatalogOnly,
    SampledEphemeris,
    Sample
};

struct SatellitePopulationCounts
{
    std::size_t active_payloads = 0;
    std::size_t inactive_payloads = 0;
    std::size_t rocket_bodies = 0;
    std::size_t debris = 0;
    std::size_t unknown = 0;

    [[nodiscard]] std::size_t total() const
    {
        return active_payloads + inactive_payloads + rocket_bodies + debris + unknown;
    }
};

struct SampledEphemerisPoint
{
    double unix_seconds = 0.0;
    double x_km = 0.0;
    double y_km = 0.0;
    double z_km = 0.0;
    double vx_km_per_s = 0.0;
    double vy_km_per_s = 0.0;
    double vz_km_per_s = 0.0;
};

struct SatelliteRecord
{
    std::int64_t norad_catalog_id = 0;
    std::string object_name;
    std::string object_id;
    std::string object_type;
    SatelliteObjectKind object_kind = SatelliteObjectKind::Unknown;
    SatellitePopulation population = SatellitePopulation::Unknown;
    OrbitSolutionKind solution_kind = OrbitSolutionKind::GeneralPerturbations;
    std::string epoch_utc;
    std::string classification_type;
    std::string owner;
    std::string operational_status_code;
    std::string data_status_code;
    std::string orbit_center;
    std::string orbit_type;
    CentralBody central_body = CentralBody::Earth;
    std::optional<double> radar_cross_section_m2;

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
    bool sun_synchronous_candidate = false;

    // SATCAT summary fields remain optional so a blank source field is not
    // confused with a real zero-valued measurement.
    std::optional<double> satcat_period_minutes;
    std::optional<double> satcat_inclination_deg;
    std::optional<double> satcat_perigee_km;
    std::optional<double> satcat_apogee_km;
    bool renderable = true;
    std::string ephemeris_source;
    std::string ephemeris_frame;
    double ephemeris_track_horizon_minutes = 0.0;
    std::vector<SampledEphemerisPoint> ephemeris_samples;
};

struct SatelliteCatalog
{
    std::string source_label;
    std::string source_url;
    std::vector<SatelliteRecord> objects;
    std::size_t skipped_records = 0;
    std::size_t malformed_records = 0;
    std::size_t excluded_records = 0;
    std::size_t non_renderable_records = 0;
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
[[nodiscard]] std::string_view satellite_object_kind_name(SatelliteObjectKind kind);
[[nodiscard]] std::string_view satellite_population_name(SatellitePopulation population);
[[nodiscard]] std::string_view central_body_name(CentralBody central_body);
[[nodiscard]] std::string_view orbit_solution_kind_name(OrbitSolutionKind kind);
[[nodiscard]] std::string_view orbit_solution_source_name(OrbitSolutionKind kind);
[[nodiscard]] bool satcat_status_is_active(std::string_view status_code);
[[nodiscard]] bool satellite_record_has_summary_orbit(const SatelliteRecord& record);
[[nodiscard]] SatellitePopulationCounts satellite_population_counts(const SatelliteCatalog& catalog);
[[nodiscard]] std::size_t renderable_satellite_count(const SatelliteCatalog& catalog);

[[nodiscard]] CatalogParseResult parse_celestrak_gp_json(
    std::string_view json,
    std::string_view source_label = {},
    std::string_view source_url = {});

[[nodiscard]] CatalogParseResult parse_celestrak_satcat_csv(
    std::string_view csv,
    std::string_view source_label = "SATCAT",
    std::string_view source_url = {});

// Applies Moon-relative equatorial sampled vectors to matching SATCAT records.
// Rows outside their sample range are deliberately not extrapolated.
[[nodiscard]] std::size_t apply_sampled_ephemeris_csv(
    SatelliteCatalog& catalog,
    std::string_view csv,
    std::string* error = nullptr);

[[nodiscard]] std::size_t load_bundled_sampled_ephemeris(
    SatelliteCatalog& catalog,
    std::string* error = nullptr);

// Removes records which a curated source confirms are no longer orbiting the
// Moon (for example, landed or impacted objects still lacking a SATCAT decay
// date). Returns the number of removed records.
[[nodiscard]] std::size_t apply_lunar_disposition_csv(
    SatelliteCatalog& catalog,
    std::string_view csv,
    std::string* error = nullptr);

[[nodiscard]] std::size_t load_bundled_lunar_dispositions(
    SatelliteCatalog& catalog,
    std::string* error = nullptr);

[[nodiscard]] SatelliteCatalog merge_satellite_catalogs(
    const SatelliteCatalog& active_gp,
    const SatelliteCatalog& satcat);

[[nodiscard]] CatalogParseResult load_sample_satellite_catalog();

} // namespace draxul::satview
