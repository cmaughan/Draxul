#pragma once

#include <cstddef>
#include <draxul/satview/satview_filter.h>

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

enum class SatViewCameraPov
{
    Earth,
    Moon
};

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
    float time_speed = 60.0f;
    bool clouds_enabled = true;
    bool realistic_clouds_enabled = false;
    bool atmosphere_enabled = true;
    bool moon_enabled = true;
    bool moon_track_enabled = true;
    float ground_fov_degrees = 60.0f;
    float ground_marker_scale = 0.1f;
    double ground_longitude_radians = 0.0;
    double ground_latitude_radians = 0.0;

    bool operator==(const SatViewConfig&) const = default;
};

[[nodiscard]] SatViewConfig load_satview_config(const draxul::ConfigDocument& document);
void store_satview_config(draxul::ConfigDocument& document, const SatViewConfig& config);

} // namespace draxul::satview
