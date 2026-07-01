#pragma once

#include <cstddef>
#include <cstdint>
#include <draxul/satview/satview_catalog.h>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul::satview
{

inline constexpr double kSatViewEarthEquatorialRadiusKm = 6378.137;

struct SatViewJulianDate
{
    double day = 0.0;
    double fraction = 0.0;

    [[nodiscard]] double value() const
    {
        return day + fraction;
    }
};

struct SatelliteStaticMetadata
{
    std::string object_name;
    std::string object_id;
    std::string object_type;
    std::string classification_type;
    std::string owner;
    std::string operational_status_code;
    std::string data_status_code;
    std::optional<double> radar_cross_section_m2;
};

struct SatellitePropagationEntry
{
    std::int64_t norad_catalog_id = 0;
    std::uint32_t object_prefix_hash = 0;
    std::string object_name;
    std::string object_id;
    std::string object_type;
    SatelliteObjectKind object_kind = SatelliteObjectKind::Unknown;
    SatellitePopulation population = SatellitePopulation::Unknown;
    OrbitSolutionKind solution_kind = OrbitSolutionKind::GeneralPerturbations;
    std::string classification_type;
    std::string owner;
    std::string operational_status_code;
    std::string data_status_code;
    std::optional<double> radar_cross_section_m2;
    std::shared_ptr<const SatelliteStaticMetadata> metadata;
    OrbitClass orbit_class = OrbitClass::Other;
    bool sun_synchronous_candidate = false;
    double epoch_unix_seconds = 0.0;
    double period_minutes = 0.0;
};

class SatellitePropagationModel
{
public:
    SatellitePropagationModel();
    ~SatellitePropagationModel();

    SatellitePropagationModel(SatellitePropagationModel&&) noexcept;
    SatellitePropagationModel& operator=(SatellitePropagationModel&&) noexcept;

    SatellitePropagationModel(const SatellitePropagationModel&) = delete;
    SatellitePropagationModel& operator=(const SatellitePropagationModel&) = delete;

    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t skipped_records() const;
    [[nodiscard]] std::string_view source_label() const;
    [[nodiscard]] std::string_view source_url() const;
    [[nodiscard]] const std::vector<SatellitePropagationEntry>& entries() const;

private:
    struct State;
    std::unique_ptr<State> state_;
    std::vector<SatellitePropagationEntry> entries_;
    std::size_t skipped_records_ = 0;
    std::string source_label_;
    std::string source_url_;

    friend struct SatellitePropagationBuilderAccess;
    friend struct SatellitePropagationRunnerAccess;
};

struct SatellitePropagationBuildResult
{
    SatellitePropagationModel model;
    std::size_t compiled_records = 0;
    std::size_t skipped_records = 0;
    std::string error;

    [[nodiscard]] explicit operator bool() const
    {
        return error.empty();
    }
};

struct SatellitePropagatedState
{
    std::int64_t norad_catalog_id = 0;
    std::uint32_t object_prefix_hash = 0;
    std::shared_ptr<const SatelliteStaticMetadata> metadata;
    SatelliteObjectKind object_kind = SatelliteObjectKind::Unknown;
    SatellitePopulation population = SatellitePopulation::Unknown;
    OrbitSolutionKind solution_kind = OrbitSolutionKind::GeneralPerturbations;
    OrbitClass orbit_class = OrbitClass::Other;
    bool sun_synchronous_candidate = false;
    double period_minutes = 0.0;
    double minutes_since_epoch = 0.0;
    glm::dvec3 teme_position_km{ 0.0 };
    glm::dvec3 teme_velocity_km_per_s{ 0.0 };
    glm::dvec3 ecef_position_km{ 0.0 };
    glm::dvec3 render_position_earth_radii{ 0.0 };
    int sgp4_error = 0;
};

struct SatelliteOrbitTrack
{
    std::int64_t norad_catalog_id = 0;
    std::uint32_t object_prefix_hash = 0;
    std::string object_name;
    std::string object_id;
    std::string object_type;
    SatelliteObjectKind object_kind = SatelliteObjectKind::Unknown;
    SatellitePopulation population = SatellitePopulation::Unknown;
    OrbitSolutionKind solution_kind = OrbitSolutionKind::GeneralPerturbations;
    std::string classification_type;
    std::string owner;
    std::string operational_status_code;
    std::string data_status_code;
    std::optional<double> radar_cross_section_m2;
    OrbitClass orbit_class = OrbitClass::Other;
    bool sun_synchronous_candidate = false;
    double minutes_since_epoch = 0.0;
    double sample_center_unix_seconds = 0.0;
    double sample_horizon_minutes = 0.0;
    std::vector<glm::dvec3> teme_points_km;
    std::vector<glm::dvec3> ecef_points_km;
    std::vector<glm::dvec3> render_teme_points_earth_radii;
    std::vector<glm::dvec3> render_points_earth_radii;
};

struct SatellitePropagationSettings
{
    // Zero means no limit.
    std::size_t max_satellites = 0;
    std::size_t track_satellite_limit = 0;
    std::size_t track_sample_count = 0;
    std::optional<std::int64_t> selected_track_norad_catalog_id;

    // Zero or negative means sample one orbital period per satellite.
    double track_horizon_minutes = 0.0;
};

struct SatellitePropagationResult
{
    double simulation_unix_seconds = 0.0;
    SatViewJulianDate simulation_julian_date;
    std::vector<SatellitePropagatedState> states;
    std::vector<SatelliteOrbitTrack> tracks;
    std::size_t skipped_model_records = 0;
    std::size_t failed_propagations = 0;
    std::string error;

    [[nodiscard]] explicit operator bool() const
    {
        return error.empty();
    }
};

class SatellitePropagationExecutor
{
public:
    // Zero leaves two hardware threads free and caps automatic concurrency at 16.
    explicit SatellitePropagationExecutor(std::size_t concurrency = 0);
    ~SatellitePropagationExecutor();

    SatellitePropagationExecutor(const SatellitePropagationExecutor&) = delete;
    SatellitePropagationExecutor& operator=(const SatellitePropagationExecutor&) = delete;
    SatellitePropagationExecutor(SatellitePropagationExecutor&&) = delete;
    SatellitePropagationExecutor& operator=(SatellitePropagationExecutor&&) = delete;

    [[nodiscard]] std::size_t concurrency() const;

private:
    struct State;
    std::unique_ptr<State> state_;

    void parallel_for(
        std::size_t item_count,
        std::size_t minimum_chunk_size,
        const std::function<void(std::size_t, std::size_t)>& function);

    friend SatellitePropagationResult propagate_satellites(
        const SatellitePropagationModel&,
        double,
        const SatellitePropagationSettings&,
        SatellitePropagationExecutor*);
};

[[nodiscard]] std::optional<double> parse_celestrak_epoch_utc(std::string_view epoch_utc);
[[nodiscard]] SatViewJulianDate julian_date_from_unix_seconds(double unix_seconds);
[[nodiscard]] double greenwich_sidereal_angle_radians(double unix_seconds);
[[nodiscard]] glm::dvec3 solar_direction_teme(double unix_seconds);
[[nodiscard]] glm::dvec3 solar_direction_render(double unix_seconds);
[[nodiscard]] glm::dvec3 teme_position_to_render_earth_radii(const glm::dvec3& teme_position_km);
[[nodiscard]] SatellitePropagationBuildResult build_satellite_propagation_model(
    const SatelliteCatalog& catalog);
[[nodiscard]] SatellitePropagationResult propagate_satellites(
    const SatellitePropagationModel& model,
    double simulation_unix_seconds,
    const SatellitePropagationSettings& settings = {},
    SatellitePropagationExecutor* executor = nullptr);
[[nodiscard]] std::optional<SatelliteOrbitTrack> propagate_satellite_track(
    const SatellitePropagationModel& model,
    std::int64_t norad_catalog_id,
    double simulation_unix_seconds,
    std::size_t sample_count);

} // namespace draxul::satview
