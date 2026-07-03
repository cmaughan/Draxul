#pragma once

#include "satview_texture_assets.h"

#include <draxul/base_renderer.h>
#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <span>
#include <vector>

namespace draxul::satview
{

inline constexpr uint32_t kSatViewSphereLatitudeBands = 64;
inline constexpr uint32_t kSatViewSphereLongitudeBands = 128;
inline constexpr uint32_t kSatViewSphereVertexCount =
    kSatViewSphereLatitudeBands * kSatViewSphereLongitudeBands * 6;
struct alignas(16) SatViewFrameUniforms
{
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
};

struct SatViewStarInstance
{
    glm::vec4 direction_magnitude{ 0.0f, 1.0f, 0.0f, 0.0f };
    glm::vec4 color_size{ 1.0f, 1.0f, 1.0f, 0.002f };
};

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

    void set_sun(glm::vec4 position_radius, glm::quat body_to_render, bool enabled)
    {
        sun_position_radius_ = position_radius;
        sun_body_to_render_ = body_to_render;
        sun_enabled_ = enabled;
    }

    void set_stars(std::span<const SatViewStarInstance> stars)
    {
        if (stars.empty() && stars_.empty())
            return;
        stars_.assign(stars.begin(), stars.end());
        ++star_revision_;
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
    std::vector<SatViewStarInstance> stars_;
    uint64_t track_revision_ = 0;
    uint64_t earth_track_revision_ = 0;
    uint64_t marker_revision_ = 0;
    uint64_t star_revision_ = 0;
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
    bool atmosphere_enabled_ = true;
    bool moon_enabled_ = true;
    bool sun_enabled_ = true;
    bool map_projection_ = false;
    bool moon_map_projection_ = false;
    bool sun_map_projection_ = false;
    bool ground_projection_ = false;
    bool hdr_debug_enabled_ = false;
    std::unique_ptr<State> state_;
};

} // namespace draxul::satview
