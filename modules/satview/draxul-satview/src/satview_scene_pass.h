#pragma once

#include <draxul/base_renderer.h>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>

namespace draxul::satview
{

inline constexpr uint32_t kSatViewSphereLatitudeBands = 64;
inline constexpr uint32_t kSatViewSphereLongitudeBands = 128;
inline constexpr uint32_t kSatViewSphereVertexCount =
    kSatViewSphereLatitudeBands * kSatViewSphereLongitudeBands * 6;
inline constexpr uint32_t kSatViewOrbitTrackCount = 24;
inline constexpr uint32_t kSatViewOrbitSegments = 192;
inline constexpr uint32_t kSatViewOrbitVertexCount =
    kSatViewOrbitTrackCount * kSatViewOrbitSegments * 2;

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

    void record_prepass(draxul::IRenderContext& ctx) override;
    void record(draxul::IRenderContext& ctx) override;

    struct State;

private:
    SatViewFrameUniforms frame_;
    std::unique_ptr<State> state_;
};

} // namespace draxul::satview
