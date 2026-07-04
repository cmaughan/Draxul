#include <catch2/catch_test_macros.hpp>
#include <draxul/config_document.h>
#include <draxul/satview/satview_config.h>
#include <numbers>

using namespace draxul;
using namespace draxul::satview;

TEST_CASE("SatView config uses the shared default track count", "[satview][config]")
{
    const ConfigDocument document;
    const SatViewConfig config = load_satview_config(document);

    CHECK(config.color_mode == SatViewColorMode::Population);
    CHECK(config.satellite_display_mode == SatViewSatelliteDisplayMode::TracksAndMarkers);
    CHECK(config.track_satellite_limit == kDefaultTrackSatelliteLimit);
    CHECK(config.track_sample_count == 48);
    CHECK_FALSE(config.refresh_tracks_each_step);
    CHECK(config.star_min_magnitude == kDefaultStarMinMagnitude);
    CHECK(config.star_max_magnitude == kDefaultStarMaxMagnitude);
    CHECK(config.star_brightness_scale == kDefaultStarBrightnessScale);
    CHECK(config.tone_map_exposure == kDefaultToneMapExposure);
    CHECK(config.tone_map_white_point == kDefaultToneMapWhitePoint);
    CHECK_FALSE(config.show_hdr_debug_panel);
    CHECK(config.sun_enabled);
    CHECK(config.earth_track_enabled);
    CHECK(config.ground_projection == SatViewGroundProjection::Stereographic);
    CHECK(config.ground_fov_degrees == 60.0f);
    CHECK(config.ground_marker_scale == 0.1f);
    CHECK(config.ground_visible);
    CHECK(config.ground_horizon_occlusion);
    CHECK(config.filter.show_active_payloads);
    CHECK(config.filter.show_inactive_payloads);
    CHECK(config.filter.show_rocket_bodies);
    CHECK(config.filter.show_debris);
    CHECK(config.filter.show_unknown_population);
    CHECK(config.filter.show_summary_estimates);
    CHECK_FALSE(config.filter.sun_synchronous_only);
}

TEST_CASE("SatView config round trips durable panel controls", "[satview][config]")
{
    ConfigDocument document;
    SatViewConfig expected;
    expected.filter.search_text = "STARLINK";
    expected.filter.object_type_text = "PAYLOAD";
    expected.filter.source_text = "active";
    expected.filter.show_other = false;
    expected.filter.show_inactive_payloads = false;
    expected.filter.show_debris = false;
    expected.filter.show_summary_estimates = false;
    expected.filter.sun_synchronous_only = true;
    expected.filter.max_epoch_age_days = 2.5;
    expected.color_mode = SatViewColorMode::OrbitClass;
    expected.track_display_mode = SatViewTrackDisplayMode::SelectedOnly;
    expected.satellite_display_mode = SatViewSatelliteDisplayMode::MarkersOnly;
    expected.projection_mode = SatViewProjectionMode::Ground;
    expected.camera_pov = SatViewCameraPov::Sun;
    expected.track_satellite_limit = 4096;
    expected.track_sample_count = 256;
    expected.refresh_tracks_each_step = true;
    expected.marker_satellite_limit = 2048;
    expected.star_min_magnitude = -0.5f;
    expected.star_max_magnitude = 7.5f;
    expected.star_brightness_scale = 2.5f;
    expected.tone_map_exposure = 2.25f;
    expected.tone_map_white_point = 4.0f;
    expected.show_hdr_debug_panel = true;
    expected.time_speed = 120.0f;
    expected.clouds_enabled = false;
    expected.realistic_clouds_enabled = true;
    expected.atmosphere_enabled = false;
    expected.moon_enabled = false;
    expected.moon_track_enabled = false;
    expected.earth_track_enabled = false;
    expected.sun_enabled = true;
    expected.ground_projection = SatViewGroundProjection::Perspective;
    expected.ground_fov_degrees = 75.0f;
    expected.ground_marker_scale = 0.45f;
    expected.ground_visible = false;
    expected.ground_horizon_occlusion = false;
    expected.ground_longitude_radians = -1.25;
    expected.ground_latitude_radians = 0.7;

    store_satview_config(document, expected);

    CHECK(load_satview_config(document) == expected);
}

TEST_CASE("SatView config clamps unsafe persisted values", "[satview][config]")
{
    ConfigDocument document;
    toml::table& table = document.ensure_table("satview");
    table.insert_or_assign("track_count", -10);
    table.insert_or_assign("track_samples", 999);
    table.insert_or_assign("marker_cap", 1234);
    table.insert_or_assign("satellite_display_mode", "not_a_mode");
    table.insert_or_assign("star_min_magnitude", 99.0);
    table.insert_or_assign("star_max_magnitude", -99.0);
    table.insert_or_assign("star_brightness", 99.0);
    table.insert_or_assign("tone_map_exposure", 99.0);
    table.insert_or_assign("tone_map_white_point", -4.0);
    table.insert_or_assign("time_speed", 9000.0);
    table.insert_or_assign("ground_fov_degrees", 300.0);
    table.insert_or_assign("ground_marker_scale", 20.0);
    table.insert_or_assign("ground_longitude_radians", 9.0);
    table.insert_or_assign("ground_latitude_radians", 3.0);
    table.insert_or_assign("max_epoch_age_days", 45.0);

    const SatViewConfig config = load_satview_config(document);

    CHECK(config.satellite_display_mode == SatViewSatelliteDisplayMode::TracksAndMarkers);
    CHECK(config.track_satellite_limit == kDefaultTrackSatelliteLimit);
    CHECK(config.track_sample_count == kMaximumTrackSampleCount);
    CHECK(config.marker_satellite_limit == 0);
    CHECK(config.star_min_magnitude == kMinimumStarMagnitude);
    CHECK(config.star_max_magnitude == kMaximumStarMagnitude);
    CHECK(config.star_brightness_scale == kMaximumStarBrightnessScale);
    CHECK(config.tone_map_exposure == kMaximumToneMapExposure);
    CHECK(config.tone_map_white_point == kMinimumToneMapWhitePoint);
    CHECK(config.time_speed == 3600.0f);
    CHECK(config.ground_fov_degrees == 235.0f);
    CHECK(config.ground_marker_scale == 2.0f);
    CHECK(config.ground_longitude_radians >= -std::numbers::pi_v<double>);
    CHECK(config.ground_longitude_radians <= std::numbers::pi_v<double>);
    CHECK(config.ground_latitude_radians < 0.5 * std::numbers::pi_v<double>);
    CHECK(config.filter.max_epoch_age_days == 30.0);
}

TEST_CASE("SatView perspective ground projection keeps its narrower field of view limit", "[satview][config]")
{
    ConfigDocument document;
    toml::table& table = document.ensure_table("satview");
    table.insert_or_assign("ground_projection", "perspective");
    table.insert_or_assign("ground_fov_degrees", 200.0);

    const SatViewConfig config = load_satview_config(document);

    CHECK(config.ground_projection == SatViewGroundProjection::Perspective);
    CHECK(config.ground_fov_degrees == 120.0f);
}
