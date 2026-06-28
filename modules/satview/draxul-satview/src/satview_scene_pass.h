#pragma once

#include "satview_texture_assets.h"

#include <draxul/base_renderer.h>
#include <cstdint>
#include <glm/glm.hpp>
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

inline constexpr uint32_t kSatViewMarkerVerticesPerInstance = 8;

class SatViewScenePass final : public draxul::IRenderPass
{
public:
    SatViewScenePass();
    ~SatViewScenePass() override;

    bool requires_main_depth_attachment() const override
    {
        return true;
    }

    void set_frame(const SatViewFrameUniforms& frame)
    {
        frame_ = frame;
    }

    void set_atmosphere_enabled(bool enabled)
    {
        atmosphere_enabled_ = enabled;
    }

    void set_map_projection(bool enabled)
    {
        map_projection_ = enabled;
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

    void set_markers(std::span<const SatViewMarkerInstance> markers)
    {
        if (markers.empty() && markers_.empty())
            return;
        markers_.assign(markers.begin(), markers.end());
        ++marker_revision_;
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

    struct State;

private:
    SatViewFrameUniforms frame_;
    std::vector<SatViewSceneVertex> track_vertices_;
    std::vector<SatViewMarkerInstance> markers_;
    uint64_t track_revision_ = 0;
    uint64_t marker_revision_ = 0;
    std::shared_ptr<const LoadedTextureImage> pending_cloud_image_;
    uint64_t cloud_revision_ = 0;
    glm::vec4 moon_position_radius_{ 0.0f };
    bool atmosphere_enabled_ = true;
    bool moon_enabled_ = true;
    bool map_projection_ = false;
    std::unique_ptr<State> state_;
};

} // namespace draxul::satview
