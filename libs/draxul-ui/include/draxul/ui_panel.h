#pragma once

#include <draxul/events.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace draxul
{

class IImGuiHost;
class IFrameContext;

struct StartupStep
{
    std::string label;
    double ms = 0.0;
};

struct HostPaneDimensions
{
    std::string name;
    glm::ivec2 pixel_pos{ 0 };
    glm::ivec2 pixel_size{ 0 };
};

struct DiagnosticPanelState
{
    bool visible = false;
    float display_ppi = 0.0f;
    glm::ivec2 cell_size{ 0 };
    glm::ivec2 grid_size{ 0 };
    size_t dirty_cells = 0;
    double frame_ms = 0.0;
    double average_frame_ms = 0.0;
    float atlas_usage_ratio = 0.0f;
    size_t atlas_glyph_count = 0;
    int atlas_reset_count = 0;
    std::vector<StartupStep> startup_steps;
    double startup_total_ms = 0.0;
    std::vector<HostPaneDimensions> host_panes;
};

struct PanelLayout
{
    bool visible = false;
    glm::ivec2 window_size{ 0 };
    int terminal_height = 0;
    int panel_y = 0;
    int panel_height = 0;
    glm::ivec2 grid_size{ 1 };
    float pixel_scale = 1.0f;

    // x, y must be in physical pixels (callers convert from SDL logical via PixelScale).
    // window_size, panel_y, panel_height are already in physical pixels.
    bool contains_panel_point(int x, int y) const
    {
        return visible && x >= 0 && x < window_size.x && y >= panel_y && y < panel_y + panel_height;
    }
};

PanelLayout compute_panel_layout(int pixel_w, int pixel_h, int cell_w, int cell_h, int padding, bool visible, float pixel_scale = 1.0f);

class UiPanel
{
public:
    UiPanel();
    ~UiPanel();

    bool initialize();

    // --- ImGui backend attachment contract -------------------------------
    // The panel never owns the backend. attach_imgui_backend() makes the
    // panel's ImGui context current, stores a reference, and initializes the
    // backend for that context (IImGuiHost::initialize_imgui_backend();
    // returns its result). The caller guarantees the backend stays alive
    // until detach_imgui_backend() — or the panel's shutdown(), which
    // performs an implicit detach — whichever comes first. Call
    // detach_imgui_backend() explicitly if the backend is torn down before
    // the panel.
    //
    // While attached:
    //   - render() calls IImGuiHost::begin_imgui_frame() each frame,
    //   - set_font() calls IImGuiHost::rebuild_imgui_font_texture(),
    //   - detach (explicit or via shutdown()) calls
    //     IImGuiHost::shutdown_imgui_backend() with the panel's context
    //     current, exactly once per attachment.
    //
    // Requires initialize() to have succeeded; before that, attach stores
    // nothing and returns false. If backend initialization fails, nothing
    // stays attached and attach returns false. Attaching a new backend
    // detaches the previous one first; re-attaching the already-attached
    // backend is a no-op returning true. detach_imgui_backend() is a no-op
    // when nothing is attached.
    bool attach_imgui_backend(IImGuiHost& backend);
    void detach_imgui_backend();

    void set_font(const std::string& font_path, float size_pixels);
    void shutdown();

    void set_visible(bool visible);
    void toggle_visible();
    bool visible() const;

    void set_window_metrics(int pixel_w, int pixel_h, int cell_w, int cell_h, int padding, float pixel_scale = 1.0f);
    const PanelLayout& layout() const;

    void update_diagnostic_state(const DiagnosticPanelState& state);

    // The single frame-driving API. Runs the whole ImGui frame for this
    // panel — activate the panel's context, IImGuiHost::begin_imgui_frame(),
    // ImGui::NewFrame(), draw the diagnostics windows, ImGui::Render(), then
    // submit the draw data through frame.render_imgui(). Call once per frame
    // (typically from IHost::draw). No-op while the panel is hidden, has no
    // reserved height, or initialize() has not succeeded.
    void render(IFrameContext& frame, float delta_seconds);

    void on_key(const KeyEvent& event);
    void on_mouse_move(const MouseMoveEvent& event);
    void on_mouse_button(const MouseButtonEvent& event);
    void on_mouse_wheel(const MouseWheelEvent& event);
    void on_text_input(const TextInputEvent& event);

    bool wants_mouse() const;
    bool wants_keyboard() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace draxul
