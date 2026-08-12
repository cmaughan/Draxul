#pragma once

#include <array>
#include <cstddef>
#include <draxul/satview/satview_filter.h>
#include <optional>
#include <string>
#include <string_view>

namespace draxul
{
class ConfigDocument;
}

namespace draxul::satview
{

inline constexpr std::size_t kDefaultTrackSatelliteLimit = 10;
inline constexpr std::size_t kDefaultTrackSampleCount = 48;
inline constexpr std::size_t kMaximumTrackSampleCount = 256;
inline constexpr std::size_t kMaximumStarCatalogCount = 100000;
inline constexpr float kMinimumStarMagnitude = -2.0f;
inline constexpr float kMaximumStarMagnitude = 10.0f;
inline constexpr float kDefaultStarMinMagnitude = -1.5f;
inline constexpr float kDefaultStarMaxMagnitude = 6.0f;
inline constexpr float kMinimumStarBrightnessScale = 0.0f;
inline constexpr float kMaximumStarBrightnessScale = 8.0f;
inline constexpr float kDefaultStarBrightnessScale = 1.0f;
inline constexpr float kMinimumConstellationLineWidth = 0.5f;
inline constexpr float kMaximumConstellationLineWidth = 8.0f;
inline constexpr float kDefaultConstellationFigureWidth = 2.0f;
inline constexpr float kDefaultConstellationBoundaryWidth = 2.0f;
inline constexpr float kMinimumMilkyWayBrightness = 0.0f;
inline constexpr float kMaximumMilkyWayBrightness = 1.0f;
inline constexpr float kDefaultMilkyWayBrightness = 0.55f;
inline constexpr float kMinimumToneMapExposure = 0.0f;
inline constexpr float kMaximumToneMapExposure = 8.0f;
inline constexpr float kDefaultToneMapExposure = 1.32f;
inline constexpr float kMinimumToneMapWhitePoint = 0.5f;
inline constexpr float kMaximumToneMapWhitePoint = 32.0f;
inline constexpr float kDefaultToneMapWhitePoint = 0.9f;

enum class SatViewColorMode
{
    Population,
    NamePrefix,
    OrbitClass,
    ObjectType
};

enum class SatViewTrackDisplayMode
{
    AllSampled,
    SelectedOnly
};

enum class SatViewSatelliteDisplayMode
{
    TracksAndMarkers,
    TracksOnly,
    MarkersOnly
};

enum class SatViewProjectionMode
{
    Globe,
    Map,
    Ground
};

enum class SatViewGroundProjection
{
    Stereographic,
    Perspective
};

enum class SatViewCameraPov
{
    Earth,
    Moon,
    Sun,
    Mercury,
    Venus,
    Mars,
    Phobos,
    Deimos,
    Jupiter,
    Io,
    Europa,
    Ganymede,
    Callisto,
    Saturn,
    Mimas,
    Enceladus,
    Tethys,
    Dione,
    Rhea,
    Titan,
    Iapetus,
    Uranus,
    Miranda,
    Ariel,
    Umbriel,
    Titania,
    Oberon,
    Neptune,
    Triton
};

struct SatViewPlanetTrackConfig
{
    bool mercury = true;
    bool venus = true;
    bool earth = true;
    bool mars = true;
    bool jupiter = true;
    bool saturn = true;
    bool uranus = true;
    bool neptune = true;

    bool operator==(const SatViewPlanetTrackConfig&) const = default;
};

inline constexpr std::array<SatViewCameraPov, 8> kSatViewPlanetTrackBodies = {
    SatViewCameraPov::Mercury,
    SatViewCameraPov::Venus,
    SatViewCameraPov::Earth,
    SatViewCameraPov::Mars,
    SatViewCameraPov::Jupiter,
    SatViewCameraPov::Saturn,
    SatViewCameraPov::Uranus,
    SatViewCameraPov::Neptune,
};

[[nodiscard]] bool satview_planet_track_enabled(
    const SatViewPlanetTrackConfig& tracks,
    SatViewCameraPov body);
void satview_set_planet_track_enabled(
    SatViewPlanetTrackConfig& tracks,
    SatViewCameraPov body,
    bool enabled);

struct SatViewConfig
{
    SatViewFilterState filter;
    SatViewColorMode color_mode = SatViewColorMode::Population;
    SatViewTrackDisplayMode track_display_mode = SatViewTrackDisplayMode::AllSampled;
    SatViewSatelliteDisplayMode satellite_display_mode = SatViewSatelliteDisplayMode::TracksAndMarkers;
    SatViewProjectionMode projection_mode = SatViewProjectionMode::Globe;
    SatViewCameraPov camera_pov = SatViewCameraPov::Earth;
    std::size_t track_satellite_limit = kDefaultTrackSatelliteLimit;
    std::size_t track_sample_count = kDefaultTrackSampleCount;
    bool refresh_tracks_each_step = false;
    std::size_t marker_satellite_limit = 0;
    float star_min_magnitude = kDefaultStarMinMagnitude;
    float star_max_magnitude = kDefaultStarMaxMagnitude;
    float star_brightness_scale = kDefaultStarBrightnessScale;
    float constellation_figure_width = kDefaultConstellationFigureWidth;
    float constellation_boundary_width = kDefaultConstellationBoundaryWidth;
    bool constellation_lines_enabled = false;
    bool constellation_boundaries_enabled = false;
    bool constellation_labels_enabled = false;
    bool milky_way_enabled = false;
    float milky_way_brightness = kDefaultMilkyWayBrightness;
    float tone_map_exposure = kDefaultToneMapExposure;
    float tone_map_white_point = kDefaultToneMapWhitePoint;
    bool show_hdr_debug_panel = false;
    float time_speed = 60.0f;
    bool clouds_enabled = true;
    bool realistic_clouds_enabled = false;
    bool atmosphere_enabled = true;
    bool moon_enabled = true;
    bool moon_track_enabled = true;
    bool surface_objects_enabled = true;
    bool show_surface_landers = true;
    bool show_surface_rovers = true;
    bool show_surface_instruments = true;
    bool show_surface_impacts = false;
    bool show_surface_crewed_artifacts = true;
    bool show_surface_approximate_locations = false;
    bool earth_track_enabled = true;
    SatViewPlanetTrackConfig planet_tracks;
    bool sun_enabled = true;
    SatViewGroundProjection ground_projection = SatViewGroundProjection::Stereographic;
    float ground_fov_degrees = 60.0f;
    float ground_marker_scale = 0.1f;
    bool ground_visible = true;
    bool ground_horizon_occlusion = true;
    bool observatory_horizon_enabled = true;
    bool cardinal_labels_enabled = false;
    double ground_longitude_radians = 0.0;
    double ground_latitude_radians = 0.0;

    bool operator==(const SatViewConfig&) const = default;
};

[[nodiscard]] SatViewConfig load_satview_config(const draxul::ConfigDocument& document);
void store_satview_config(draxul::ConfigDocument& document, const SatViewConfig& config);
[[nodiscard]] std::string serialize_satview_config_toml(
    const SatViewConfig& config);
[[nodiscard]] std::optional<SatViewConfig> parse_satview_config_toml(
    std::string_view text);

} // namespace draxul::satview
