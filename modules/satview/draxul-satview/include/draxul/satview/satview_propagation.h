#pragma once

#include <cstddef>
#include <cstdint>
#include <draxul/satview/satview_catalog.h>
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

struct SatellitePropagationEntry
{
    std::int64_t norad_catalog_id = 0;
    std::string object_name;
    OrbitClass orbit_class = OrbitClass::Other;
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
    std::string object_name;
    OrbitClass orbit_class = OrbitClass::Other;
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
    std::string object_name;
    OrbitClass orbit_class = OrbitClass::Other;
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

[[nodiscard]] std::optional<double> parse_celestrak_epoch_utc(std::string_view epoch_utc);
[[nodiscard]] SatViewJulianDate julian_date_from_unix_seconds(double unix_seconds);
[[nodiscard]] SatellitePropagationBuildResult build_satellite_propagation_model(
    const SatelliteCatalog& catalog);
[[nodiscard]] SatellitePropagationResult propagate_satellites(
    const SatellitePropagationModel& model,
    double simulation_unix_seconds,
    const SatellitePropagationSettings& settings = {});

} // namespace draxul::satview
