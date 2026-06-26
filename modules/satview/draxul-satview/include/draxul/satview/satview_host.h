#pragma once

#include <chrono>
#include <draxul/host.h>
#include <draxul/satview/satview_catalog_service.h>
#include <draxul/satview/satview_filter.h>
#include <draxul/satview/satview_propagation.h>
#include <memory>
#include <optional>
#include <string>

struct ImGuiContext;

namespace draxul
{
class HostProviderRegistry;
}

namespace draxul::satview
{

class SatViewScenePass;

enum class SatViewColorMode
{
    OrbitClass,
    ObjectType
};

enum class SatViewTrackDisplayMode
{
    AllSampled,
    SelectedOnly
};

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
    void on_text_input(const draxul::TextInputEvent& event) override;
    void on_focus_lost() override;

    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    std::string status_text() const override;
    draxul::Color default_background() const override;
    draxul::HostRuntimeState runtime_state() const override;
    draxul::HostDebugState debug_state() const override;

    void attach_imgui_host(draxul::IImGuiHost& host) override;
    void set_imgui_font(const std::string& path, float size_pixels) override;

private:
    void request_redraw();
    void clamp_camera();
    void reset_camera();
    bool rebuild_propagation_model_if_needed();
    void update_propagation_if_needed(bool force);
    void render_host_imgui(float dt);
    void render_control_panel();
    std::size_t visible_state_count() const;
    std::size_t visible_track_count() const;
    const SatellitePropagatedState* selected_satellite() const;
    void clear_selection_if_hidden();
    void select_nearest_satellite(const glm::ivec2& screen_pos);

    draxul::IHostCallbacks* callbacks_ = nullptr;
    draxul::HostViewport viewport_;
    std::shared_ptr<SatViewScenePass> scene_pass_;
    SatViewCatalogService catalog_service_;
    SatellitePropagationModel propagation_model_;
    SatellitePropagationResult propagation_snapshot_;
    std::uint64_t propagation_catalog_generation_ = 0;
    std::string propagation_status_;
    std::string init_error_;
    SatViewFilterState filter_;
    std::optional<std::int64_t> selected_norad_catalog_id_;
    ImGuiContext* imgui_context_ = nullptr;
    draxul::IImGuiHost* imgui_backend_ = nullptr;
    std::string imgui_font_path_;
    float imgui_font_size_pixels_ = 13.0f;
    char search_buffer_[128]{};
    char object_type_buffer_[64]{};
    char source_buffer_[96]{};
    SatViewColorMode color_mode_ = SatViewColorMode::OrbitClass;
    SatViewTrackDisplayMode track_display_mode_ = SatViewTrackDisplayMode::AllSampled;
    std::size_t track_satellite_limit_ = 256;
    std::size_t track_sample_count_ = 48;
    std::size_t marker_satellite_limit_ = 0;
    bool running_ = false;
    bool paused_ = false;
    bool dragging_ = false;
    bool pending_click_ = false;
    bool show_ui_panel_ = true;
    bool propagation_settings_dirty_ = false;
    float yaw_ = 0.45f;
    float pitch_ = 0.35f;
    float distance_ = 3.6f;
    float time_speed_ = 60.0f;
    double simulated_seconds_ = 0.0;
    std::chrono::steady_clock::time_point last_pump_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point next_propagation_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_activity_time_ = std::chrono::steady_clock::now();
    double last_propagation_simulated_seconds_ = 0.0;
    glm::ivec2 click_start_pos_{ 0, 0 };
    float last_imgui_delta_seconds_ = 1.0f / 60.0f;
};

std::unique_ptr<draxul::IHost> create_satview_host();

void register_satview_host_provider(draxul::HostProviderRegistry& registry);

} // namespace draxul::satview
