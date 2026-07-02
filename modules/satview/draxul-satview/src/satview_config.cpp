#include <draxul/satview/satview_config.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <draxul/config_document.h>
#include <draxul/toml_support.h>
#include <limits>
#include <numbers>
#include <string_view>
#include <toml++/toml.hpp>

namespace draxul::satview
{

namespace
{

constexpr std::size_t kMinimumTrackSatelliteLimit = 1;
constexpr std::size_t kMinimumTrackSampleCount = 12;
constexpr std::size_t kMaximumSearchLength = 127;
constexpr std::size_t kMaximumObjectTypeLength = 63;
constexpr std::size_t kMaximumSourceLength = 95;
constexpr std::array<std::size_t, 6> kMarkerLimits = { 0, 512, 1024, 2048, 4096, 8192 };
constexpr double kPi = std::numbers::pi_v<double>;
constexpr double kLatitudeLimitRadians = 0.5 * kPi - 0.001;

std::string truncate(std::string value, std::size_t maximum_length)
{
    if (value.size() > maximum_length)
        value.resize(maximum_length);
    return value;
}

std::size_t clamped_size(const toml::table& table, std::string_view key, std::size_t fallback,
    std::size_t minimum, std::size_t maximum)
{
    const auto parsed = toml_support::get_int(table, key);
    if (!parsed.has_value() || *parsed < 0)
        return fallback;
    const auto value = static_cast<std::uint64_t>(*parsed);
    return static_cast<std::size_t>(std::clamp<std::uint64_t>(value, minimum, maximum));
}

std::string_view format_color_mode(SatViewColorMode mode)
{
    switch (mode)
    {
    case SatViewColorMode::Population:
        return "population";
    case SatViewColorMode::NamePrefix:
        return "name_prefix";
    case SatViewColorMode::OrbitClass:
        return "orbit_class";
    case SatViewColorMode::ObjectType:
        return "object_type";
    }
    return "population";
}

std::string_view format_track_display_mode(SatViewTrackDisplayMode mode)
{
    return mode == SatViewTrackDisplayMode::SelectedOnly ? "selected_only" : "all_sampled";
}

std::string_view format_satellite_display_mode(SatViewSatelliteDisplayMode mode)
{
    switch (mode)
    {
    case SatViewSatelliteDisplayMode::TracksAndMarkers:
        return "tracks_and_markers";
    case SatViewSatelliteDisplayMode::TracksOnly:
        return "tracks_only";
    case SatViewSatelliteDisplayMode::MarkersOnly:
        return "markers_only";
    }
    return "tracks_and_markers";
}

std::string_view format_projection_mode(SatViewProjectionMode mode)
{
    switch (mode)
    {
    case SatViewProjectionMode::Globe:
        return "globe";
    case SatViewProjectionMode::Map:
        return "map";
    case SatViewProjectionMode::Ground:
        return "ground";
    }
    return "globe";
}

std::string_view format_camera_pov(SatViewCameraPov pov)
{
    return pov == SatViewCameraPov::Moon ? "moon" : "earth";
}

void apply_satview_table(SatViewConfig& config, const toml::table& table)
{
    if (auto value = toml_support::get_string(table, "color_mode"))
    {
        if (*value == "population")
            config.color_mode = SatViewColorMode::Population;
        else if (*value == "name_prefix")
            config.color_mode = SatViewColorMode::NamePrefix;
        else if (*value == "orbit_class")
            config.color_mode = SatViewColorMode::OrbitClass;
        else if (*value == "object_type")
            config.color_mode = SatViewColorMode::ObjectType;
    }
    if (auto value = toml_support::get_string(table, "track_display_mode"))
    {
        if (*value == "all_sampled")
            config.track_display_mode = SatViewTrackDisplayMode::AllSampled;
        else if (*value == "selected_only")
            config.track_display_mode = SatViewTrackDisplayMode::SelectedOnly;
    }
    if (auto value = toml_support::get_string(table, "satellite_display_mode"))
    {
        if (*value == "tracks_and_markers")
            config.satellite_display_mode = SatViewSatelliteDisplayMode::TracksAndMarkers;
        else if (*value == "tracks_only")
            config.satellite_display_mode = SatViewSatelliteDisplayMode::TracksOnly;
        else if (*value == "markers_only")
            config.satellite_display_mode = SatViewSatelliteDisplayMode::MarkersOnly;
    }
    if (auto value = toml_support::get_string(table, "projection_mode"))
    {
        if (*value == "globe")
            config.projection_mode = SatViewProjectionMode::Globe;
        else if (*value == "map")
            config.projection_mode = SatViewProjectionMode::Map;
        else if (*value == "ground")
            config.projection_mode = SatViewProjectionMode::Ground;
    }
    if (auto value = toml_support::get_string(table, "camera_pov"))
    {
        if (*value == "earth")
            config.camera_pov = SatViewCameraPov::Earth;
        else if (*value == "moon")
            config.camera_pov = SatViewCameraPov::Moon;
    }

    config.track_satellite_limit = clamped_size(table, "track_count",
        config.track_satellite_limit, kMinimumTrackSatelliteLimit,
        static_cast<std::size_t>(std::numeric_limits<int>::max()));
    config.track_sample_count = clamped_size(table, "track_samples",
        config.track_sample_count, kMinimumTrackSampleCount, kMaximumTrackSampleCount);
    const std::size_t marker_limit = clamped_size(table, "marker_cap",
        config.marker_satellite_limit, 0, kMarkerLimits.back());
    if (std::ranges::find(kMarkerLimits, marker_limit) != kMarkerLimits.end())
        config.marker_satellite_limit = marker_limit;

    if (auto value = toml_support::get_double(table, "time_speed"))
        config.time_speed = static_cast<float>(std::clamp(*value, 1.0, 3600.0));
    else if (auto value = toml_support::get_int(table, "time_speed"))
        config.time_speed = static_cast<float>(std::clamp<std::int64_t>(*value, 1, 3600));
    if (auto value = toml_support::get_double(table, "ground_fov_degrees"))
        config.ground_fov_degrees = static_cast<float>(std::clamp(*value, 20.0, 120.0));
    else if (auto value = toml_support::get_int(table, "ground_fov_degrees"))
        config.ground_fov_degrees = static_cast<float>(std::clamp<std::int64_t>(*value, 20, 120));
    if (auto value = toml_support::get_double(table, "ground_marker_scale"))
        config.ground_marker_scale = static_cast<float>(std::clamp(*value, 0.05, 2.0));
    else if (auto value = toml_support::get_int(table, "ground_marker_scale"))
        config.ground_marker_scale = static_cast<float>(
            std::clamp(static_cast<double>(*value), 0.05, 2.0));
    if (auto value = toml_support::get_double(table, "ground_longitude_radians"))
        config.ground_longitude_radians = std::remainder(*value, 2.0 * kPi);
    else if (auto value = toml_support::get_int(table, "ground_longitude_radians"))
        config.ground_longitude_radians = std::remainder(static_cast<double>(*value), 2.0 * kPi);
    if (auto value = toml_support::get_double(table, "ground_latitude_radians"))
        config.ground_latitude_radians = std::clamp(*value, -kLatitudeLimitRadians, kLatitudeLimitRadians);
    else if (auto value = toml_support::get_int(table, "ground_latitude_radians"))
        config.ground_latitude_radians = std::clamp(static_cast<double>(*value), -kLatitudeLimitRadians, kLatitudeLimitRadians);

    auto assign_bool = [&](std::string_view key, bool& target) {
        if (auto value = toml_support::get_bool(table, key))
            target = *value;
    };
    assign_bool("show_low_earth", config.filter.show_low_earth);
    assign_bool("show_medium_earth", config.filter.show_medium_earth);
    assign_bool("show_geosynchronous", config.filter.show_geosynchronous);
    assign_bool("show_highly_elliptical", config.filter.show_highly_elliptical);
    assign_bool("show_other", config.filter.show_other);
    assign_bool("sun_synchronous_only", config.filter.sun_synchronous_only);
    assign_bool("show_active_payloads", config.filter.show_active_payloads);
    assign_bool("show_inactive_payloads", config.filter.show_inactive_payloads);
    assign_bool("show_rocket_bodies", config.filter.show_rocket_bodies);
    assign_bool("show_debris", config.filter.show_debris);
    assign_bool("show_unknown_population", config.filter.show_unknown_population);
    assign_bool("show_summary_estimates", config.filter.show_summary_estimates);
    assign_bool("clouds", config.clouds_enabled);
    assign_bool("realistic_clouds", config.realistic_clouds_enabled);
    assign_bool("atmosphere", config.atmosphere_enabled);
    assign_bool("moon", config.moon_enabled);
    assign_bool("moon_track", config.moon_track_enabled);
    assign_bool("refresh_tracks_each_step", config.refresh_tracks_each_step);

    if (auto value = toml_support::get_string(table, "search"))
        config.filter.search_text = truncate(std::move(*value), kMaximumSearchLength);
    if (auto value = toml_support::get_string(table, "object_type"))
        config.filter.object_type_text = truncate(std::move(*value), kMaximumObjectTypeLength);
    if (auto value = toml_support::get_string(table, "source"))
        config.filter.source_text = truncate(std::move(*value), kMaximumSourceLength);
    if (auto value = toml_support::get_double(table, "max_epoch_age_days"))
        config.filter.max_epoch_age_days = std::clamp(*value, 0.0, 30.0);
    else if (auto value = toml_support::get_int(table, "max_epoch_age_days"))
        config.filter.max_epoch_age_days = static_cast<double>(std::clamp<std::int64_t>(*value, 0, 30));

    if (config.camera_pov == SatViewCameraPov::Moon)
        config.moon_enabled = true;
}

toml::table serialize_satview_table(const SatViewConfig& config)
{
    toml::table table;
    table.insert_or_assign("color_mode", std::string(format_color_mode(config.color_mode)));
    table.insert_or_assign("track_display_mode", std::string(format_track_display_mode(config.track_display_mode)));
    table.insert_or_assign("satellite_display_mode",
        std::string(format_satellite_display_mode(config.satellite_display_mode)));
    table.insert_or_assign("projection_mode", std::string(format_projection_mode(config.projection_mode)));
    table.insert_or_assign("camera_pov", std::string(format_camera_pov(config.camera_pov)));
    table.insert_or_assign("track_count", static_cast<std::int64_t>(config.track_satellite_limit));
    table.insert_or_assign("track_samples", static_cast<std::int64_t>(config.track_sample_count));
    table.insert_or_assign("refresh_tracks_each_step", config.refresh_tracks_each_step);
    table.insert_or_assign("marker_cap", static_cast<std::int64_t>(config.marker_satellite_limit));
    table.insert_or_assign("time_speed", static_cast<double>(config.time_speed));
    table.insert_or_assign("search", config.filter.search_text);
    table.insert_or_assign("object_type", config.filter.object_type_text);
    table.insert_or_assign("source", config.filter.source_text);
    table.insert_or_assign("max_epoch_age_days", config.filter.max_epoch_age_days);
    table.insert_or_assign("show_low_earth", config.filter.show_low_earth);
    table.insert_or_assign("show_medium_earth", config.filter.show_medium_earth);
    table.insert_or_assign("show_geosynchronous", config.filter.show_geosynchronous);
    table.insert_or_assign("show_highly_elliptical", config.filter.show_highly_elliptical);
    table.insert_or_assign("show_other", config.filter.show_other);
    table.insert_or_assign("sun_synchronous_only", config.filter.sun_synchronous_only);
    table.insert_or_assign("show_active_payloads", config.filter.show_active_payloads);
    table.insert_or_assign("show_inactive_payloads", config.filter.show_inactive_payloads);
    table.insert_or_assign("show_rocket_bodies", config.filter.show_rocket_bodies);
    table.insert_or_assign("show_debris", config.filter.show_debris);
    table.insert_or_assign("show_unknown_population", config.filter.show_unknown_population);
    table.insert_or_assign("show_summary_estimates", config.filter.show_summary_estimates);
    table.insert_or_assign("clouds", config.clouds_enabled);
    table.insert_or_assign("realistic_clouds", config.realistic_clouds_enabled);
    table.insert_or_assign("atmosphere", config.atmosphere_enabled);
    table.insert_or_assign("moon", config.moon_enabled);
    table.insert_or_assign("moon_track", config.moon_track_enabled);
    table.insert_or_assign("ground_fov_degrees", static_cast<double>(config.ground_fov_degrees));
    table.insert_or_assign("ground_marker_scale", static_cast<double>(config.ground_marker_scale));
    table.insert_or_assign("ground_longitude_radians", config.ground_longitude_radians);
    table.insert_or_assign("ground_latitude_radians", config.ground_latitude_radians);
    return table;
}

} // namespace

SatViewConfig load_satview_config(const ConfigDocument& document)
{
    SatViewConfig config;
    if (const toml::table* table = document.find_table("satview"))
        apply_satview_table(config, *table);
    return config;
}

void store_satview_config(ConfigDocument& document, const SatViewConfig& config)
{
    document.ensure_table("satview") = serialize_satview_table(config);
}

} // namespace draxul::satview
