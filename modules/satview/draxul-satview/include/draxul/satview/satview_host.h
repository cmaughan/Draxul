#pragma once

#include <chrono>
#include <draxul/host.h>
#include <draxul/satview/satview_catalog.h>
#include <memory>
#include <string>

namespace draxul
{
class HostProviderRegistry;
}

namespace draxul::satview
{

class SatViewScenePass;

class SatViewHost final : public draxul::IHost
{
public:
    SatViewHost();
    ~SatViewHost() override;

    bool initialize(const draxul::HostContext& context, draxul::IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;

    void set_viewport(const draxul::HostViewport& viewport) override;
    void pump() override;
    void draw(draxul::IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;

    void on_mouse_button(const draxul::MouseButtonEvent& event) override;
    void on_mouse_move(const draxul::MouseMoveEvent& event) override;
    void on_mouse_wheel(const draxul::MouseWheelEvent& event) override;
    void on_key(const draxul::KeyEvent& event) override;

    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    std::string status_text() const override;
    draxul::Color default_background() const override;
    draxul::HostRuntimeState runtime_state() const override;
    draxul::HostDebugState debug_state() const override;

private:
    void request_redraw();
    void clamp_camera();

    draxul::IHostCallbacks* callbacks_ = nullptr;
    draxul::HostViewport viewport_;
    std::shared_ptr<SatViewScenePass> scene_pass_;
    SatelliteCatalog catalog_;
    std::string catalog_status_ = "catalog loading";
    std::string init_error_;
    bool running_ = false;
    bool paused_ = false;
    bool dragging_ = false;
    float yaw_ = 0.45f;
    float pitch_ = 0.35f;
    float distance_ = 3.6f;
    float time_speed_ = 60.0f;
    double simulated_seconds_ = 0.0;
    std::chrono::steady_clock::time_point last_pump_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_activity_time_ = std::chrono::steady_clock::now();
};

std::unique_ptr<draxul::IHost> create_satview_host();

void register_satview_host_provider(draxul::HostProviderRegistry& registry);

} // namespace draxul::satview
