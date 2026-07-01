#pragma once

#include <chrono>
#include <draxul/host.h>
#include <draxul/satview/satview_catalog_service.h>
#include <draxul/satview/satview_config.h>
#include <draxul/satview/satview_filter.h>
#include <draxul/satview/satview_propagation.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct ImGuiContext;

namespace draxul
{
class HostProviderRegistry;
}

namespace draxul::satview
{

class SatViewScenePass;
class SatViewSimulationWorker;
class SatViewCameraKeyState;
class SatViewCloudService;
class Camera;
class Manipulator;
struct SatViewSimulationSnapshot;

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
    struct ObjectTreeEntry
    {
        SatellitePopulation population = SatellitePopulation::Unknown;
        OrbitClass orbit_class = OrbitClass::Other;
        std::string prefix;
        std::string label;
        std::string object_name;
        std::int64_t norad_catalog_id = 0;
        std::size_t state_index = 0;
    };

    void request_redraw();
    void invalidate_track_buffer();
    void invalidate_marker_buffer();
    void invalidate_visual_buffers();
    [[nodiscard]] SatViewConfig current_config() const;
    void apply_config(const SatViewConfig& config);
    void persist_config();
    void sync_simulation_controls();
    void sync_simulation_render_settings();
    void set_real_time();
    void set_camera_pov(SatViewCameraPov pov, double simulation_seconds);
    void reset_camera();
    void pan_map(glm::vec2 delta_radians);
    void rebuild_object_tree(const SatViewSimulationSnapshot* snapshot);
    void render_object_tree(const SatViewSimulationSnapshot* snapshot, bool& changed);
    void render_host_imgui(float dt, const SatViewSimulationSnapshot* snapshot);
    void render_control_panel(const SatViewSimulationSnapshot* snapshot);
    std::size_t visible_track_count(const SatViewSimulationSnapshot* snapshot) const;
    const SatellitePropagatedState* selected_satellite(const SatViewSimulationSnapshot* snapshot) const;
    void clear_selection_if_missing(const SatViewSimulationSnapshot* snapshot);
    void select_nearest_satellite(const glm::ivec2& screen_pos);

    draxul::IHostCallbacks* callbacks_ = nullptr;
    draxul::ConfigDocument* config_document_ = nullptr;
    draxul::HostViewport viewport_;
    std::shared_ptr<SatViewScenePass> scene_pass_;
    SatViewCatalogService catalog_service_;
    std::unique_ptr<SatViewCloudService> cloud_service_;
    std::unique_ptr<SatViewSimulationWorker> simulation_worker_;
    std::uint64_t simulation_catalog_generation_ = 0;
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
    SatViewColorMode color_mode_ = SatViewColorMode::Population;
    SatViewTrackDisplayMode track_display_mode_ = SatViewTrackDisplayMode::AllSampled;
    SatViewSatelliteDisplayMode satellite_display_mode_ = SatViewSatelliteDisplayMode::TracksAndMarkers;
    SatViewProjectionMode projection_mode_ = SatViewProjectionMode::Globe;
    SatViewCameraPov camera_pov_ = SatViewCameraPov::Earth;
    std::size_t track_satellite_limit_ = kDefaultTrackSatelliteLimit;
    std::size_t track_sample_count_ = kDefaultTrackSampleCount;
    bool refresh_tracks_each_step_ = false;
    std::size_t marker_satellite_limit_ = 0;
    bool running_ = false;
    bool paused_ = false;
    bool dragging_ = false;
    bool pending_click_ = false;
    bool show_ui_panel_ = true;
    bool config_dirty_ = false;
    bool simulation_settings_dirty_ = false;
    bool continuous_refresh_enabled_ = false;
    bool track_buffer_dirty_ = true;
    bool marker_buffer_dirty_ = true;
    bool clouds_enabled_ = true;
    bool realistic_clouds_enabled_ = false;
    bool atmosphere_enabled_ = true;
    bool moon_enabled_ = true;
    bool moon_track_enabled_ = true;
    const void* uploaded_track_source_ = nullptr;
    std::optional<double> moon_track_center_seconds_;
    std::uint64_t uploaded_marker_generation_ = 0;
    std::uint64_t object_tree_catalog_generation_ = 0;
    std::size_t object_tree_state_count_ = 0;
    std::vector<ObjectTreeEntry> object_tree_entries_;
    std::vector<std::size_t> filtered_object_tree_indices_;
    std::shared_ptr<Camera> camera_;
    std::unique_ptr<Manipulator> camera_manipulator_;
    std::unique_ptr<SatViewCameraKeyState> camera_keys_;
    float time_speed_ = 60.0f;
    double simulated_seconds_ = 0.0;
    double last_draw_simulation_seconds_ = 0.0;
    std::chrono::steady_clock::time_point last_pump_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_activity_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point next_frame_time_ = std::chrono::steady_clock::now();
    glm::ivec2 click_start_pos_{ 0, 0 };
    glm::ivec2 last_map_drag_pos_{ 0, 0 };
    glm::vec2 map_center_radians_{ 0.0f };
    float last_imgui_delta_seconds_ = 1.0f / 60.0f;
};

std::unique_ptr<draxul::IHost> create_satview_host();

void register_satview_host_provider(draxul::HostProviderRegistry& registry);

} // namespace draxul::satview
