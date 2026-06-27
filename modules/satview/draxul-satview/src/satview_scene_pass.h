#pragma once

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
    glm::vec4 sun_dir_time{ 1.0f, 0.0f, 0.0f, 0.0f };
    glm::vec4 render_params{
        static_cast<float>(kSatViewSphereLatitudeBands),
        static_cast<float>(kSatViewSphereLongitudeBands),
        0.0f,
        0.0f
    };
};

struct SatViewSceneVertex
{
    glm::vec4 position{ 0.0f, 0.0f, 0.0f, 1.0f };
    glm::vec4 color{ 1.0f };
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

    void record_prepass(draxul::IRenderContext& ctx) override;
    void record(draxul::IRenderContext& ctx) override;

    struct State;

private:
    SatViewFrameUniforms frame_;
    std::vector<SatViewSceneVertex> track_vertices_;
    std::vector<SatViewMarkerInstance> markers_;
    uint64_t track_revision_ = 0;
    uint64_t marker_revision_ = 0;
    std::unique_ptr<State> state_;
};

} // namespace draxul::satview
