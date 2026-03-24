#pragma once

#include <draxul/events.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct ImDrawData;

namespace draxul
{

struct StartupStep
{
    std::string label;
    double ms = 0.0;
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

    bool contains_panel_point(int x, int y) const
    {
        const auto px = static_cast<int>(static_cast<float>(x) * pixel_scale);
        const auto py = static_cast<int>(static_cast<float>(y) * pixel_scale);
        return visible && px >= 0 && px < window_size.x && py >= panel_y && py < panel_y + panel_height;
    }
};

PanelLayout compute_panel_layout(int pixel_w, int pixel_h, int cell_w, int cell_h, int padding, bool visible, float pixel_scale = 1.0f);

class UiPanel
{
public:
    UiPanel();
    ~UiPanel();

    bool initialize();
    void set_font(const std::string& font_path, float size_pixels);
    void activate_imgui_context(); // make this panel's ImGui context current (for backend shutdown)
    void shutdown();

    void set_visible(bool visible);
    void toggle_visible();
    bool visible() const;

    void set_window_metrics(int pixel_w, int pixel_h, int cell_w, int cell_h, int padding, float pixel_scale = 1.0f);
    const PanelLayout& layout() const;

    void update_diagnostic_state(const DiagnosticPanelState& state);

    void begin_frame(float delta_seconds);
    const ImDrawData* render();
    const ImDrawData* end_frame() const;
    void render_into_current_context() const;

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
