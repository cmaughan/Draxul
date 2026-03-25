#pragma once

#include <chrono>
#include <draxul/citydb.h>
#include <draxul/host.h>
#include <draxul/treesitter.h>
#include <memory>

namespace draxul
{

class IsometricCamera;
class IsometricScenePass;
struct SignLabelAtlas;
struct SemanticCityLayout;
class SceneWorld;
struct SceneSnapshot;

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
    void rebuild_semantic_city();
    bool movement_active() const;
    bool drag_smoothing_active() const;
    SceneSnapshot build_scene_snapshot() const;

    IHostCallbacks* callbacks_ = nullptr;
    HostViewport viewport_;
    std::shared_ptr<IsometricScenePass> scene_pass_;
    std::unique_ptr<SceneWorld> world_;
    std::unique_ptr<IsometricCamera> camera_;
    I3DRenderer* renderer_3d_ = nullptr;
    CodebaseScanner scanner_;
    CityDatabase city_db_;
    std::unique_ptr<TextService> sign_text_service_;
    std::shared_ptr<SignLabelAtlas> sign_label_atlas_;
    uint64_t sign_label_revision_ = 1;
    float sign_text_hidden_px_ = 1.5f;
    float sign_text_full_px_ = 8.0f;
    float output_gamma_ = 1.0f;
    bool clamp_semantic_metrics_ = false;
    bool hide_test_entities_ = true;
    int pixel_w_ = 800;
    int pixel_h_ = 600;
    bool running_ = false;
    mutable float world_span_ = 5.0f;
    bool scene_dirty_ = true;
    bool city_db_reconciled_ = false;
    bool continuous_refresh_enabled_ = false;
    bool move_left_ = false;
    bool move_right_ = false;
    bool move_up_ = false;
    bool move_down_ = false;
    bool orbit_left_ = false;
    bool orbit_right_ = false;
    bool zoom_in_ = false;
    bool zoom_out_ = false;
    bool pitch_up_ = false;
    bool pitch_down_ = false;
    bool dragging_scene_ = false;
    glm::vec2 pending_drag_pan_{ 0.0f };
    float pending_drag_orbit_ = 0.0f;
    glm::ivec2 last_drag_pos_{ 0 };
    std::chrono::steady_clock::time_point last_activity_time_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_pump_time_ = std::chrono::steady_clock::now();
};

// Factory function — called from host_factory.cpp
std::unique_ptr<IHost> create_megacity_host();

} // namespace draxul
