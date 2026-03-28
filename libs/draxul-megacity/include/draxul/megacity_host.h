#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <draxul/citydb.h>
#include <draxul/host.h>
#include <draxul/megacity_code_config.h>
#include <draxul/treesitter.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace draxul
{

class CityInputState;
struct GeometryMesh;
class IsometricCamera;
class IsometricScenePass;
struct SignLabelAtlas;
struct SemanticMegacityModel;
struct SemanticMegacityLayout;
struct CityGrid;
class SceneWorld;

// MegaCityHost is a non-terminal I3DHost that renders a small 3D scene directly
// into the GPU render pass. It does not use the grid path, but it does use the
// font pipeline for semantic-city rooftop labels.
//
// The I3DHost contract means HostManager calls attach_3d_renderer() after
// initialize() succeeds, and detach_3d_renderer() before shutdown(). The scene
// render pass is registered with the renderer in attach_3d_renderer() rather
// than inside initialize(), keeping HostContext free of renderer capability details.
class MegaCityHost final : public I3DHost
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
    void on_key(const KeyEvent& event) override;
    void on_font_metrics_changed() override
    {
        // MegaCityHost ignores font metrics changes; it renders 3D geometry, not a text grid.
    }
    void pump() override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;

    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_move(const MouseMoveEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;

    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    Color default_background() const override;
    HostRuntimeState runtime_state() const override;
    HostDebugState debug_state() const override;

    // I3DHost
    void attach_3d_renderer(I3DRenderer& renderer) override;
    void detach_3d_renderer() override;
    bool has_imgui() const override
    {
        return true;
    }
    void render_imgui(float dt) override;
    void set_imgui_font(const std::string& path, float size_pixels) override;

private:
    void mark_scene_dirty();
    void mark_world_rebuild_pending();
    void handle_click(const glm::ivec2& screen_pos);
    bool update_hidden_hover_blend(float dt, std::chrono::steady_clock::time_point now);
    void apply_selection_opacity();
    void clear_selection();
    void route_worker_loop();
    void request_routes_for_focus(std::string focus_module_path, std::string focus_qualified_name);
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
    I3DRenderer* renderer_3d_ = nullptr;
    CodebaseScanner scanner_;
    CityDatabase city_db_;
    std::unique_ptr<TextService> sign_text_service_;
    std::unique_ptr<TextService> tooltip_text_service_;
    std::shared_ptr<SignLabelAtlas> sign_label_atlas_;
    std::shared_ptr<const GeometryMesh> tree_bark_mesh_;
    std::shared_ptr<const GeometryMesh> tree_leaf_mesh_;
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
    std::string imgui_ini_path_;
    bool continuous_refresh_enabled_ = false;
    std::string selected_building_name_;
    std::string selected_building_module_path_;
    bool hidden_hover_active_ = false;
    float hidden_hover_blend_ = 0.0f;
    std::chrono::steady_clock::time_point last_activity_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_pump_time_ = std::chrono::steady_clock::now();

    // Hover tooltip state
    glm::ivec2 hover_mouse_pos_{ -1, -1 };
    std::chrono::steady_clock::time_point hover_start_time_;
    bool hover_tooltip_visible_ = false;
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
        std::string focus_module_path;
        std::string focus_qualified_name;
        std::shared_ptr<const SemanticMegacityLayout> layout;
        std::shared_ptr<const SemanticMegacityModel> model;
        std::shared_ptr<const CityGrid> grid;
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
