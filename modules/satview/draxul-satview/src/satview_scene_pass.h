#pragma once

#include "satview_label_layout.h"
#include "satview_landscape.h"
#include <draxul/satview/satview_solar_system.h>
#include <draxul/satview/satview_texture_assets.h>

#include <algorithm>
#include <cstdint>
#include <draxul/base_renderer.h>
#include <draxul/satview/satview_config.h>
#include <draxul/text_atlas.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <span>
#include <vector>

namespace draxul::satview
{

inline constexpr uint32_t kSatViewSphereLatitudeBands = 64;
inline constexpr uint32_t kSatViewSphereLongitudeBands = 128;
inline constexpr uint32_t kSatViewSphereVertexCount = kSatViewSphereLatitudeBands * kSatViewSphereLongitudeBands * 6;
struct alignas(16) SatViewFrameUniforms
{
    // Stereographic Ground mode stores world-to-camera rotation in the upper 3x3,
    // lens scale in [3][2], and viewport height/width in [3][3]. camera_pos.w is
    // negative for Map, zero for Globe, and 1..4 for Ground lens/occlusion modes.
    glm::mat4 view_proj{ 1.0f };
    glm::vec4 camera_pos{ 0.0f, 0.0f, 4.0f, 1.0f };
    glm::vec4 camera_orientation{ 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec4 sun_dir_time{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec4 render_params{
        static_cast<float>(kSatViewSphereLatitudeBands),
        static_cast<float>(kSatViewSphereLongitudeBands),
        0.0f,
        0.0f
    };
};
static_assert(sizeof(SatViewFrameUniforms) == 128);

struct SatViewSceneVertex
{
    // The position w sign marks the paired endpoint; magnitude 2 tags ECEF map coordinates.
    glm::vec4 position{ 0.0f, 0.0f, 0.0f, -1.0f };
    glm::vec4 color{ 1.0f };
    glm::vec4 paired_position{ 0.0f, 0.0f, 0.0f, 1.0f };
};

struct SatViewMarkerInstance
{
    glm::vec4 position0_size{ 0.0f, 0.0f, 0.0f, 0.01f };
    glm::vec4 position1_selected{ 0.0f, 0.0f, 0.0f, 0.0f };
    glm::vec4 color{ 1.0f };
    // x selects procedural shape; y offsets wrapped map copies by map widths.
    glm::vec4 style{ 0.0f };
    // xyz is the outward surface normal; w enables tangent-plane rendering.
    glm::vec4 surface_normal{ 0.0f };
};
static_assert(sizeof(SatViewMarkerInstance) == 80);

struct SatViewStarInstance
{
    glm::vec4 direction_magnitude{ 0.0f, 1.0f, 0.0f, 0.0f };
    glm::vec4 color_size{ 1.0f, 1.0f, 1.0f, 0.002f };
};

struct SatViewCelestialLineInstance
{
    glm::vec4 start_direction_width{ 0.0f, 1.0f, 0.0f, 1.0f };
    glm::vec4 end_direction_dash{ 0.0f, 1.0f, 0.0f, 0.0f };
    glm::vec4 color{ 1.0f };
    // x is dash gap in pixels; y is cumulative angular phase in radians.
    glm::vec4 style{ 0.0f };
};
static_assert(sizeof(SatViewCelestialLineInstance) == 64);

inline constexpr uint32_t kSatViewMarkerVerticesPerInstance = 8;
inline constexpr uint32_t kSatViewStarVerticesPerInstance = 6;

class SatViewScenePass final : public draxul::IRenderPass
{
public:
    SatViewScenePass();
    ~SatViewScenePass() override;

    void set_frame(const SatViewFrameUniforms& frame)
    {
        frame_ = frame;
    }

    void set_atmosphere_enabled(bool enabled)
    {
        atmosphere_enabled_ = enabled;
    }

    void set_ground_visible(bool visible)
    {
        ground_visible_ = visible;
    }

    void set_projection_mode(
        bool map_enabled,
        bool moon_map_centered,
        bool sun_map_centered,
        bool ground_enabled)
    {
        map_projection_ = map_enabled;
        moon_map_projection_ = map_enabled && moon_map_centered;
        sun_map_projection_ = map_enabled && sun_map_centered;
        ground_projection_ = ground_enabled;
    }

    void set_moon(glm::vec4 position_radius, bool enabled)
    {
        moon_position_radius_ = position_radius;
        moon_enabled_ = enabled;
    }

    void set_track_vertices(std::span<const SatViewSceneVertex> vertices)
    {
        if (vertices.empty() && track_vertices_.empty())
            return;
        track_vertices_.assign(vertices.begin(), vertices.end());
        ++track_revision_;
    }

    void set_earth_track_vertices(std::span<const SatViewSceneVertex> vertices)
    {
        if (vertices.empty() && earth_track_vertices_.empty())
            return;
        earth_track_vertices_.assign(vertices.begin(), vertices.end());
        ++earth_track_revision_;
    }

    void set_markers(std::span<const SatViewMarkerInstance> markers)
    {
        if (markers.empty() && markers_.empty())
            return;
        markers_.assign(markers.begin(), markers.end());
        ++marker_revision_;
    }

    void set_surface_markers(std::span<const SatViewMarkerInstance> markers)
    {
        if (markers.empty() && surface_markers_.empty())
            return;
        surface_markers_.assign(markers.begin(), markers.end());
        ++surface_marker_revision_;
    }

    void set_sun(glm::vec4 position_radius, glm::quat body_to_render, bool enabled)
    {
        sun_position_radius_ = position_radius;
        sun_body_to_render_ = body_to_render;
        sun_enabled_ = enabled;
    }

    void set_focus_body(
        SatViewCameraPov body,
        float rotation_radians,
        float polar_radius_ratio,
        bool emissive,
        bool enabled)
    {
        focus_body_ = body;
        focus_body_rotation_radians_ = rotation_radians;
        focus_body_polar_radius_ratio_ = polar_radius_ratio;
        focus_body_emissive_ = emissive;
        focus_body_enabled_ = enabled;
    }

    void set_context_body(
        SatViewCameraPov body,
        glm::vec4 position_radius,
        float rotation_radians,
        float polar_radius_ratio,
        bool enabled)
    {
        context_body_ = body;
        context_body_position_radius_ = position_radius;
        context_body_rotation_radians_ = rotation_radians;
        context_body_polar_radius_ratio_ = polar_radius_ratio;
        context_body_enabled_ = enabled;
    }

    void set_child_bodies(std::span<const SatViewBodyRenderInstance> bodies)
    {
        child_bodies_.assign(bodies.begin(), bodies.end());
    }

    void set_ring_bands(std::span<const SatViewRingBand> bands)
    {
        ring_bands_.assign(bands.begin(), bands.end());
    }

    void set_stars(std::span<const SatViewStarInstance> stars)
    {
        if (stars.empty() && stars_.empty())
            return;
        stars_.assign(stars.begin(), stars.end());
        ++star_revision_;
    }

    void set_constellation_lines(std::span<const SatViewCelestialLineInstance> lines)
    {
        if (lines.empty() && constellation_lines_.empty())
            return;
        constellation_lines_.assign(lines.begin(), lines.end());
        ++constellation_revision_;
    }

    void set_constellation_lines_enabled(bool enabled)
    {
        constellation_lines_enabled_ = enabled;
    }

    void set_constellation_boundary_lines(std::span<const SatViewCelestialLineInstance> lines)
    {
        if (lines.empty() && constellation_boundary_lines_.empty())
            return;
        constellation_boundary_lines_.assign(lines.begin(), lines.end());
        ++constellation_boundary_revision_;
    }

    void set_constellation_boundaries_enabled(bool enabled)
    {
        constellation_boundaries_enabled_ = enabled;
    }

    void set_label_atlas(std::shared_ptr<const TextAtlasImage> atlas)
    {
        if (!atlas || !atlas->valid())
            return;
        label_atlas_ = std::move(atlas);
        ++label_atlas_revision_;
    }

    void set_constellation_labels(std::span<const SatViewLabelInstance> labels)
    {
        constellation_labels_.assign(labels.begin(), labels.end());
        ++constellation_label_revision_;
    }

    void set_cardinal_labels(std::span<const SatViewLabelInstance> labels)
    {
        cardinal_labels_.assign(labels.begin(), labels.end());
        ++cardinal_label_revision_;
    }

    void set_observatory_landscape(const SatViewLandscapeMesh& landscape)
    {
        observatory_fill_triangles_ = landscape.fill_triangles;
        observatory_rim_lines_ = landscape.rim_lines;
        ++observatory_revision_;
    }

    void set_observatory_horizon_enabled(bool enabled)
    {
        observatory_horizon_enabled_ = enabled;
    }

    void set_milky_way_enabled(bool enabled)
    {
        milky_way_enabled_ = enabled;
    }

    void set_milky_way_brightness(float brightness)
    {
        milky_way_brightness_ = std::clamp(brightness, 0.0f, 1.0f);
    }

    void set_star_magnitude_range(float minimum_magnitude, float maximum_magnitude)
    {
        star_min_magnitude_ = minimum_magnitude;
        star_max_magnitude_ = maximum_magnitude;
    }

    void set_star_brightness_scale(float scale)
    {
        star_brightness_scale_ = scale;
    }

    void set_tone_mapping(float exposure, float white_point)
    {
        tone_map_exposure_ = exposure;
        tone_map_white_point_ = white_point;
    }

    void set_hdr_debug_enabled(bool enabled)
    {
        hdr_debug_enabled_ = enabled;
    }

    void set_star_projection_aspect_scale(float aspect_scale)
    {
        star_projection_aspect_scale_ = aspect_scale;
    }

    void set_cloud_image(std::shared_ptr<const LoadedTextureImage> image)
    {
        if (!image || !image->valid())
            return;
        pending_cloud_image_ = std::move(image);
        ++cloud_revision_;
    }

    void record_prepass(draxul::IRenderContext& ctx) override;
    void record(draxul::IRenderContext& ctx) override;
    void render_hdr_debug_ui();

    struct State;

private:
    SatViewFrameUniforms frame_;
    std::vector<SatViewSceneVertex> track_vertices_;
    std::vector<SatViewSceneVertex> earth_track_vertices_;
    std::vector<SatViewMarkerInstance> markers_;
    std::vector<SatViewMarkerInstance> surface_markers_;
    std::vector<SatViewStarInstance> stars_;
    std::vector<SatViewCelestialLineInstance> constellation_lines_;
    std::vector<SatViewCelestialLineInstance> constellation_boundary_lines_;
    std::vector<SatViewLabelInstance> constellation_labels_;
    std::vector<SatViewLabelInstance> cardinal_labels_;
    std::vector<SatViewLandscapeTriangleInstance> observatory_fill_triangles_;
    std::vector<SatViewLandscapeLineInstance> observatory_rim_lines_;
    std::vector<SatViewBodyRenderInstance> child_bodies_;
    std::vector<SatViewRingBand> ring_bands_;
    std::shared_ptr<const TextAtlasImage> label_atlas_;
    uint64_t track_revision_ = 0;
    uint64_t earth_track_revision_ = 0;
    uint64_t marker_revision_ = 0;
    uint64_t surface_marker_revision_ = 0;
    uint64_t star_revision_ = 0;
    uint64_t constellation_revision_ = 0;
    uint64_t constellation_boundary_revision_ = 0;
    uint64_t constellation_label_revision_ = 0;
    uint64_t cardinal_label_revision_ = 0;
    uint64_t observatory_revision_ = 0;
    uint64_t label_atlas_revision_ = 0;
    float star_min_magnitude_ = -1.5f;
    float star_max_magnitude_ = 6.0f;
    float star_brightness_scale_ = 1.0f;
    float star_projection_aspect_scale_ = 1.0f;
    float tone_map_exposure_ = 1.32f;
    float tone_map_white_point_ = 0.9f;
    std::shared_ptr<const LoadedTextureImage> pending_cloud_image_;
    uint64_t cloud_revision_ = 0;
    glm::vec4 moon_position_radius_{ 0.0f };
    glm::vec4 sun_position_radius_{ 0.0f };
    glm::quat sun_body_to_render_{ 1.0f, 0.0f, 0.0f, 0.0f };
    SatViewCameraPov focus_body_ = SatViewCameraPov::Sun;
    SatViewCameraPov context_body_ = SatViewCameraPov::Earth;
    glm::vec4 context_body_position_radius_{ 0.0f };
    float focus_body_rotation_radians_ = 0.0f;
    float focus_body_polar_radius_ratio_ = 1.0f;
    float context_body_rotation_radians_ = 0.0f;
    float context_body_polar_radius_ratio_ = 1.0f;
    bool focus_body_emissive_ = false;
    bool focus_body_enabled_ = false;
    bool context_body_enabled_ = false;
    bool atmosphere_enabled_ = true;
    bool moon_enabled_ = true;
    bool sun_enabled_ = true;
    bool map_projection_ = false;
    bool moon_map_projection_ = false;
    bool sun_map_projection_ = false;
    bool ground_projection_ = false;
    bool ground_visible_ = true;
    bool constellation_lines_enabled_ = false;
    bool constellation_boundaries_enabled_ = false;
    bool observatory_horizon_enabled_ = true;
    bool milky_way_enabled_ = false;
    float milky_way_brightness_ = 0.55f;
    bool hdr_debug_enabled_ = false;
    std::unique_ptr<State> state_;
};

} // namespace draxul::satview
