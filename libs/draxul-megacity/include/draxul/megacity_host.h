#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <draxul/citydb.h>
#include <draxul/host.h>
#include <draxul/megacity_code_config.h>
#include <draxul/perf_timing.h>
#include <draxul/treesitter.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

struct ImGuiContext;

namespace draxul
{

class CityInputState;
struct GeometryMesh;
class IsometricCamera;
class IsometricScenePass;
struct LiveCityMetricsSnapshot;
struct LcovFunctionLookup;
struct SignLabelAtlas;
struct SemanticMegacityModel;
struct SemanticMegacityLayout;
struct CityGrid;
class SceneWorld;

// MegaCityHost is a non-terminal host that renders a small 3D scene directly
// into the GPU render pass. It does not use the grid path, but it does use the
// font pipeline for semantic-city rooftop labels.
class MegaCityHost final : public IHost
{
public:
    MegaCityHost();
    ~MegaCityHost() override;

    void set_continuous_refresh_enabled(bool enabled)
    {
        continuous_refresh_enabled_ = enabled;
    }
    void set_ui_panels_visible(bool visible)
    {
        show_ui_panels_ = visible;
    }

    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;

    void set_viewport(const HostViewport& viewport) override;
    void on_focus_lost() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    // on_font_metrics_changed() — inherited no-op from IHost is correct;
    // MegaCityHost renders 3D geometry, not a text grid.
    void pump() override;
    void draw(IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;

    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_move(const MouseMoveEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;

    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    Color default_background() const override;
    HostRuntimeState runtime_state() const override;
    HostDebugState debug_state() const override;

    void attach_imgui_host(IImGuiHost& host) override;
    void set_imgui_font(const std::string& path, float size_pixels) override;

private:
    void render_host_imgui(float dt);
    void mark_scene_dirty();
    void mark_world_rebuild_pending();
    void handle_click(const glm::ivec2& screen_pos);
    void handle_double_click(const glm::ivec2& screen_pos);
    bool update_hidden_hover_blend(float dt, std::chrono::steady_clock::time_point now);
    void apply_selection_opacity();
    void clear_selection();
    void route_worker_loop();
    void request_routes_for_focus(
        std::string focus_source_file_path,
        std::string focus_module_path,
        std::string focus_qualified_name);
    void consume_completed_routes();
    void clear_active_routes(bool request_frame = true);
    void refresh_available_modules();
    void rebuild_semantic_city();
    void launch_grid_build(const SemanticMegacityLayout& layout, const SemanticMegacityModel& model);
    void refresh_sign_text_service();
    void sync_camera_state_to_configs();
    void reset_camera_to_default_frame();

    std::unique_ptr<CityInputState> input_;
    IHostCallbacks* callbacks_ = nullptr;
    HostViewport viewport_;
    std::shared_ptr<IsometricScenePass> scene_pass_;
    std::unique_ptr<SceneWorld> world_;
    std::unique_ptr<IsometricCamera> camera_;
    CodebaseScanner scanner_;
    std::filesystem::path scan_root_;
    CityDatabase city_db_;
    std::unique_ptr<TextService> sign_text_service_;
    std::unique_ptr<TextService> tooltip_text_service_;
    std::shared_ptr<SignLabelAtlas> sign_label_atlas_;
    std::shared_ptr<const GeometryMesh> tree_bark_mesh_;
    std::shared_ptr<const GeometryMesh> tree_leaf_mesh_;
    std::shared_ptr<const LiveCityMetricsSnapshot> live_metrics_;
    std::shared_ptr<const SemanticMegacityModel> semantic_model_;
    std::shared_ptr<const SemanticMegacityLayout> semantic_layout_;
    std::vector<std::string> available_modules_;
    ConfigDocument* config_document_ = nullptr;
    MegaCityCodeConfig renderer_config_;
    MegaCityCodeConfig pending_renderer_config_;
    MegaCityCodeConfig renderer_defaults_;
    uint64_t sign_label_revision_ = 1;
    std::string sign_font_path_;
    float display_ppi_ = 96.0f;
    int pixel_w_ = 800;
    int pixel_h_ = 600;
    bool running_ = false;
    float world_span_ = 5.0f;
    bool scene_dirty_ = true;
    bool world_rebuild_pending_ = false;
    bool city_db_reconciled_ = false;
    bool restore_camera_after_initial_build_ = false;
    bool city_bounds_valid_ = false;
    float city_min_x_ = -2.5f;
    float city_max_x_ = 2.5f;
    float city_min_z_ = -2.5f;
    float city_max_z_ = 2.5f;
    bool show_ui_panels_ = true;
    int imgui_settle_frames_ = 0;
    std::string imgui_ini_path_;
    ImGuiContext* imgui_context_ = nullptr;
    IImGuiHost* imgui_backend_ = nullptr;
    bool continuous_refresh_enabled_ = false;
    std::string selected_building_name_;
    std::string selected_building_module_path_;
    std::string selected_building_source_file_;
    bool hidden_hover_active_ = false;
    float hidden_hover_blend_ = 0.0f;
    uint64_t last_live_perf_generation_ = 0;
    RuntimePerfSnapshot coverage_perf_snapshot_;
    std::shared_ptr<const LcovFunctionLookup> lcov_lookup_;
    std::chrono::steady_clock::time_point last_activity_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_pump_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_live_perf_refresh_time_ = std::chrono::steady_clock::now();
    float last_imgui_delta_seconds_ = 1.0f / 60.0f;

    // Hover tooltip state
    glm::ivec2 hover_mouse_pos_{ -1, -1 };
    glm::ivec2 hover_anchor_pos_{ -1, -1 };
    std::chrono::steady_clock::time_point hover_start_time_;
    bool hover_tooltip_visible_ = false;
    bool hover_shift_held_ = false;
    std::string hover_building_name_;
    uint64_t tooltip_revision_ = 0;

    // City grid (occupancy map for overview and pathfinding)
    mutable std::mutex grid_mutex_;
    std::shared_ptr<const CityGrid> city_grid_;
    std::thread grid_thread_;
    std::atomic<bool> grid_build_in_progress_{ false };

    struct RouteBuildRequest
    {
        uint64_t generation = 0;
        std::string focus_source_file_path;
        std::string focus_module_path;
        std::string focus_qualified_name;
        std::shared_ptr<const SemanticMegacityLayout> layout;
        std::shared_ptr<const SemanticMegacityModel> model;
        std::shared_ptr<const CityGrid> grid;
        MegaCityCodeConfig config; // Snapshot of renderer_config_ taken under lock
    };

    struct RouteBuildResult
    {
        uint64_t generation = 0;
        std::shared_ptr<const CityGrid> grid;
    };

    mutable std::mutex route_mutex_;
    std::condition_variable route_cv_;
    std::thread route_thread_;
    bool route_worker_stop_ = false;
    uint64_t route_request_generation_ = 0;
    std::optional<RouteBuildRequest> pending_route_request_;
    std::optional<RouteBuildResult> completed_route_result_;
    std::atomic<bool> route_build_in_progress_{ false };
};

// Factory function — called from host_factory.cpp
std::unique_ptr<IHost> create_megacity_host();

} // namespace draxul
