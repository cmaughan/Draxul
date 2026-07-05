#include <draxul/satview/satview_config.h>

#include "satview_sky_projection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <draxul/config_document.h>
#include <draxul/toml_support.h>
#include <limits>
#include <numbers>
#include <string_view>
#include <toml++/toml.hpp>
#include <utility>

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

float clamped_float(const toml::table& table, std::string_view key, float fallback,
    float minimum, float maximum)
{
    if (auto value = toml_support::get_double(table, key))
        return static_cast<float>(std::clamp(*value,
            static_cast<double>(minimum),
            static_cast<double>(maximum)));
    if (auto value = toml_support::get_int(table, key))
        return static_cast<float>(std::clamp(
            static_cast<double>(*value),
            static_cast<double>(minimum),
            static_cast<double>(maximum)));
    return fallback;
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

std::string_view format_ground_projection(SatViewGroundProjection projection)
{
    return projection == SatViewGroundProjection::Perspective
        ? "perspective"
        : "stereographic";
}

std::string_view format_camera_pov(SatViewCameraPov pov)
{
    switch (pov)
    {
    case SatViewCameraPov::Earth:
        return "earth";
    case SatViewCameraPov::Moon:
        return "moon";
    case SatViewCameraPov::Sun:
        return "sun";
    }
    return "earth";
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
        else if (*value == "sun")
            config.camera_pov = SatViewCameraPov::Sun;
    }
    if (auto value = toml_support::get_string(table, "ground_projection"))
    {
        if (*value == "stereographic")
            config.ground_projection = SatViewGroundProjection::Stereographic;
        else if (*value == "perspective")
            config.ground_projection = SatViewGroundProjection::Perspective;
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
    config.star_min_magnitude = clamped_float(table, "star_min_magnitude",
        config.star_min_magnitude, kMinimumStarMagnitude, kMaximumStarMagnitude);
    config.star_max_magnitude = clamped_float(table, "star_max_magnitude",
        config.star_max_magnitude, kMinimumStarMagnitude, kMaximumStarMagnitude);
    if (config.star_min_magnitude > config.star_max_magnitude)
        std::swap(config.star_min_magnitude, config.star_max_magnitude);
    config.star_brightness_scale = clamped_float(table, "star_brightness",
        config.star_brightness_scale, kMinimumStarBrightnessScale, kMaximumStarBrightnessScale);
    config.constellation_figure_width = clamped_float(table, "constellation_figure_width",
        config.constellation_figure_width,
        kMinimumConstellationLineWidth,
        kMaximumConstellationLineWidth);
    config.constellation_boundary_width = clamped_float(table, "constellation_boundary_width",
        config.constellation_boundary_width,
        kMinimumConstellationLineWidth,
        kMaximumConstellationLineWidth);
    config.milky_way_brightness = clamped_float(table, "milky_way_brightness",
        config.milky_way_brightness, kMinimumMilkyWayBrightness, kMaximumMilkyWayBrightness);
    config.tone_map_exposure = clamped_float(table, "tone_map_exposure",
        config.tone_map_exposure, kMinimumToneMapExposure, kMaximumToneMapExposure);
    config.tone_map_white_point = clamped_float(table, "tone_map_white_point",
        config.tone_map_white_point, kMinimumToneMapWhitePoint, kMaximumToneMapWhitePoint);

    if (auto value = toml_support::get_double(table, "time_speed"))
        config.time_speed = static_cast<float>(std::clamp(*value, 1.0, 3600.0));
    else if (auto value = toml_support::get_int(table, "time_speed"))
        config.time_speed = static_cast<float>(std::clamp<std::int64_t>(*value, 1, 3600));
    config.ground_fov_degrees = satview_clamp_ground_fov_degrees(
        config.ground_projection,
        clamped_float(
            table,
            "ground_fov_degrees",
            config.ground_fov_degrees,
            20.0f,
            satview_maximum_ground_fov_degrees(config.ground_projection)));
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
    assign_bool("show_catalog_only", config.filter.show_catalog_only);
    assign_bool("show_earth_objects", config.filter.show_earth);
    assign_bool("show_moon_objects", config.filter.show_moon);
    assign_bool("constellation_lines", config.constellation_lines_enabled);
    assign_bool("constellation_boundaries", config.constellation_boundaries_enabled);
    assign_bool("constellation_labels", config.constellation_labels_enabled);
    assign_bool("milky_way", config.milky_way_enabled);
    assign_bool("clouds", config.clouds_enabled);
    assign_bool("realistic_clouds", config.realistic_clouds_enabled);
    assign_bool("atmosphere", config.atmosphere_enabled);
    assign_bool("moon", config.moon_enabled);
    assign_bool("moon_track", config.moon_track_enabled);
    assign_bool("earth_track", config.earth_track_enabled);
    assign_bool("sun", config.sun_enabled);
    assign_bool("ground_visible", config.ground_visible);
    assign_bool("ground_horizon_occlusion", config.ground_horizon_occlusion);
    assign_bool("observatory_horizon", config.observatory_horizon_enabled);
    assign_bool("cardinal_labels", config.cardinal_labels_enabled);
    assign_bool("refresh_tracks_each_step", config.refresh_tracks_each_step);
    assign_bool("show_hdr_debug_panel", config.show_hdr_debug_panel);

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
    if (config.camera_pov == SatViewCameraPov::Sun)
        config.sun_enabled = true;
}

toml::table serialize_satview_table(const SatViewConfig& config)
{
    toml::table table;
    table.insert_or_assign("color_mode", std::string(format_color_mode(config.color_mode)));
    table.insert_or_assign("track_display_mode", std::string(format_track_display_mode(config.track_display_mode)));
    table.insert_or_assign("satellite_display_mode",
        std::string(format_satellite_display_mode(config.satellite_display_mode)));
    table.insert_or_assign("projection_mode", std::string(format_projection_mode(config.projection_mode)));
    table.insert_or_assign("ground_projection", std::string(format_ground_projection(config.ground_projection)));
    table.insert_or_assign("camera_pov", std::string(format_camera_pov(config.camera_pov)));
    table.insert_or_assign("track_count", static_cast<std::int64_t>(config.track_satellite_limit));
    table.insert_or_assign("track_samples", static_cast<std::int64_t>(config.track_sample_count));
    table.insert_or_assign("refresh_tracks_each_step", config.refresh_tracks_each_step);
    table.insert_or_assign("marker_cap", static_cast<std::int64_t>(config.marker_satellite_limit));
    table.insert_or_assign("star_min_magnitude", static_cast<double>(config.star_min_magnitude));
    table.insert_or_assign("star_max_magnitude", static_cast<double>(config.star_max_magnitude));
    table.insert_or_assign("star_brightness", static_cast<double>(config.star_brightness_scale));
    table.insert_or_assign(
        "constellation_figure_width", static_cast<double>(config.constellation_figure_width));
    table.insert_or_assign(
        "constellation_boundary_width", static_cast<double>(config.constellation_boundary_width));
    table.insert_or_assign("milky_way_brightness", static_cast<double>(config.milky_way_brightness));
    table.insert_or_assign("tone_map_exposure", static_cast<double>(config.tone_map_exposure));
    table.insert_or_assign("tone_map_white_point", static_cast<double>(config.tone_map_white_point));
    table.insert_or_assign("show_hdr_debug_panel", config.show_hdr_debug_panel);
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
    table.insert_or_assign("show_catalog_only", config.filter.show_catalog_only);
    table.insert_or_assign("show_earth_objects", config.filter.show_earth);
    table.insert_or_assign("show_moon_objects", config.filter.show_moon);
    table.insert_or_assign("constellation_lines", config.constellation_lines_enabled);
    table.insert_or_assign("constellation_boundaries", config.constellation_boundaries_enabled);
    table.insert_or_assign("constellation_labels", config.constellation_labels_enabled);
    table.insert_or_assign("milky_way", config.milky_way_enabled);
    table.insert_or_assign("clouds", config.clouds_enabled);
    table.insert_or_assign("realistic_clouds", config.realistic_clouds_enabled);
    table.insert_or_assign("atmosphere", config.atmosphere_enabled);
    table.insert_or_assign("moon", config.moon_enabled);
    table.insert_or_assign("moon_track", config.moon_track_enabled);
    table.insert_or_assign("earth_track", config.earth_track_enabled);
    table.insert_or_assign("sun", config.sun_enabled);
    table.insert_or_assign("ground_visible", config.ground_visible);
    table.insert_or_assign("ground_horizon_occlusion", config.ground_horizon_occlusion);
    table.insert_or_assign("observatory_horizon", config.observatory_horizon_enabled);
    table.insert_or_assign("cardinal_labels", config.cardinal_labels_enabled);
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
