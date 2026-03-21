#include <draxul/ui_panel.h>

#include "ui_metrics_panel.h"
#include "ui_panel_style.h"
#include <draxul/sdl_imgui_input.h>

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>

namespace draxul
{

namespace
{

constexpr float kDefaultPanelRatio = 1.0f / 3.0f;
constexpr int kMinimumPanelHeight = 120;
constexpr const char* kDockspaceName = "DraxulPanelDockspace";
constexpr const char* kWindowTabName = "Window";
constexpr const char* kRendererWindowName = "Renderer";
constexpr const char* kStartupWindowName = "Startup";

bool create_dock_space()
{
    const ImGuiID dockspace_id = ImGui::GetID(kDockspaceName);
    const bool first_render = ImGui::DockBuilderGetNode(dockspace_id) == nullptr;

    if (first_render)
    {
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetContentRegionAvail());

        ImGuiID dock_id_right = 0;
        ImGuiID dock_id_left = 0;
        ImGui::DockBuilderSplitNode(
            dockspace_id,
            ImGuiDir_Right,
            0.42f,
            &dock_id_right,
            &dock_id_left);
        ImGui::DockBuilderDockWindow(kWindowTabName, dock_id_left);
        ImGui::DockBuilderDockWindow(kStartupWindowName, dock_id_left);
        ImGui::DockBuilderDockWindow(kRendererWindowName, dock_id_right);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    return first_render;
}

void render_window_tab(const PanelLayout& layout, const DiagnosticPanelState& state)
{
    if (ImGui::Begin(kWindowTabName, nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
        render_window_sections(layout, state);
    ImGui::End();
}

void render_renderer_window(const DiagnosticPanelState& state)
{
    if (ImGui::Begin(kRendererWindowName, nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
        render_renderer_sections(state);
    ImGui::End();
}

void render_startup_window(const DiagnosticPanelState& state)
{
    if (ImGui::Begin(kStartupWindowName, nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
        render_startup_sections(state);
    ImGui::End();
}

} // namespace

struct UiPanel::Impl
{
    ImGuiContext* context = nullptr;
    PanelLayout layout = {};
    DiagnosticPanelState debug_state = {};
    std::string font_path;
    float font_size_pixels = 0.0f;
};

PanelLayout compute_panel_layout(int pixel_w, int pixel_h, int cell_w, int cell_h, int padding, bool visible, float pixel_scale)
{
    PanelLayout layout;
    layout.visible = visible;
    layout.pixel_scale = pixel_scale > 0.0f ? pixel_scale : 1.0f;
    layout.window_width = std::max(0, pixel_w);
    layout.window_height = std::max(0, pixel_h);

    const int safe_cell_w = std::max(1, cell_w);
    const int safe_cell_h = std::max(1, cell_h);
    const int safe_padding = std::max(0, padding);

    int panel_height = 0;
    if (visible && layout.window_height > safe_cell_h + safe_padding * 2)
    {
        const int desired = static_cast<int>(std::lround(static_cast<double>(layout.window_height) * kDefaultPanelRatio));
        const int max_panel_height = std::max(0, layout.window_height - (safe_cell_h + safe_padding * 2));
        panel_height = std::clamp(desired, kMinimumPanelHeight, max_panel_height);
    }

    layout.panel_height = panel_height;
    layout.terminal_height = std::max(0, layout.window_height - panel_height);
    layout.panel_y = layout.terminal_height;

    const int usable_width = std::max(0, layout.window_width - safe_padding * 2);
    const int usable_height = std::max(0, layout.terminal_height - safe_padding * 2);
    layout.grid_cols = std::max(1, usable_width / safe_cell_w);
    layout.grid_rows = std::max(1, usable_height / safe_cell_h);

    // Snap panel_y to exactly where terminal content ends: padding + grid_rows * cell_h.
    // Cells are drawn at row*cell_h+padding, so the last row's bottom edge is at
    // padding + grid_rows*cell_h. Without this snap, integer division leaves an
    // unfilled strip of the clear color between the last row and the panel top.
    if (panel_height > 0)
    {
        const int snapped = safe_padding + layout.grid_rows * safe_cell_h;
        layout.terminal_height = snapped;
        layout.panel_y = snapped;
        layout.panel_height = std::max(0, layout.window_height - snapped);
    }

    return layout;
}

UiPanel::UiPanel()
    : impl_(std::make_unique<Impl>())
{
}

UiPanel::~UiPanel()
{
    shutdown();
}

bool UiPanel::initialize()
{
    if (impl_->context)
        return true;

    IMGUI_CHECKVERSION();
    impl_->context = ImGui::CreateContext();
    if (!impl_->context)
        return false;

    ImGui::SetCurrentContext(impl_->context);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    apply_panel_style();
    return true;
}

void UiPanel::activate_imgui_context()
{
    if (impl_->context)
        ImGui::SetCurrentContext(impl_->context);
}

void UiPanel::shutdown()
{
    if (!impl_->context)
        return;

    ImGui::SetCurrentContext(impl_->context);
    ImGui::DestroyContext(impl_->context);
    impl_->context = nullptr;
}

void UiPanel::set_font(const std::string& font_path, float size_pixels)
{
    impl_->font_path = font_path;
    impl_->font_size_pixels = size_pixels;

    if (!impl_->context)
        return;

    ImGui::SetCurrentContext(impl_->context);
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    if (!font_path.empty() && size_pixels > 0.0f)
        io.Fonts->AddFontFromFileTTF(font_path.c_str(), size_pixels);

    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();
}

void UiPanel::set_visible(bool visible)
{
    impl_->layout.visible = visible;
}

void UiPanel::toggle_visible()
{
    impl_->layout.visible = !impl_->layout.visible;
}

bool UiPanel::visible() const
{
    return impl_->layout.visible;
}

void UiPanel::set_window_metrics(int pixel_w, int pixel_h, int cell_w, int cell_h, int padding, float pixel_scale)
{
    impl_->layout = compute_panel_layout(pixel_w, pixel_h, cell_w, cell_h, padding, impl_->layout.visible, pixel_scale);
}

const PanelLayout& UiPanel::layout() const
{
    return impl_->layout;
}

void UiPanel::update_diagnostic_state(const DiagnosticPanelState& state)
{
    impl_->debug_state = state;
    impl_->debug_state.visible = impl_->layout.visible;
}

void UiPanel::begin_frame(float delta_seconds)
{
    if (!impl_->context)
        return;

    ImGui::SetCurrentContext(impl_->context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(impl_->layout.window_width), static_cast<float>(impl_->layout.window_height));
    io.DeltaTime = delta_seconds > 0.0f ? delta_seconds : (1.0f / 60.0f);
    ImGui::NewFrame();
}

const ImDrawData* UiPanel::render()
{
    if (!impl_->context || !impl_->layout.visible || impl_->layout.panel_height <= 0)
        return nullptr;

    ImGui::SetCurrentContext(impl_->context);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoTitleBar;

    ImGui::SetNextWindowPos(ImVec2(0.0f, static_cast<float>(impl_->layout.panel_y)));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(impl_->layout.window_width), static_cast<float>(impl_->layout.panel_height)));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool open = ImGui::Begin("Draxul Diagnostics", nullptr, flags);
    ImGui::PopStyleVar();
    bool first_render = false;
    if (open)
    {
        first_render = create_dock_space();
        ImGui::End();

        render_window_tab(impl_->layout, impl_->debug_state);
        render_renderer_window(impl_->debug_state);
        render_startup_window(impl_->debug_state);

        if (first_render)
            ImGui::SetWindowFocus(kStartupWindowName);
    }
    else
    {
        ImGui::End();
    }

    ImGui::Render();
    return ImGui::GetDrawData();
}

void UiPanel::on_mouse_move(const MouseMoveEvent& event)
{
    if (!impl_->context)
        return;

    ImGui::SetCurrentContext(impl_->context);
    const float scale = impl_->layout.pixel_scale;
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(event.x) * scale, static_cast<float>(event.y) * scale);
}

void UiPanel::on_mouse_button(const MouseButtonEvent& event)
{
    if (!impl_->context)
        return;

    ImGui::SetCurrentContext(impl_->context);

    int button = -1;
    switch (event.button)
    {
    case 1:
        button = 0;
        break;
    case 2:
        button = 2;
        break;
    case 3:
        button = 1;
        break;
    default:
        break;
    }

    if (button >= 0)
        ImGui::GetIO().AddMouseButtonEvent(button, event.pressed);
}

void UiPanel::on_mouse_wheel(const MouseWheelEvent& event)
{
    if (!impl_->context)
        return;

    ImGui::SetCurrentContext(impl_->context);
    ImGui::GetIO().AddMouseWheelEvent(event.dx, event.dy);
}

void UiPanel::on_text_input(const TextInputEvent& event)
{
    if (!impl_->context || !event.text)
        return;

    ImGui::SetCurrentContext(impl_->context);
    ImGui::GetIO().AddInputCharactersUTF8(event.text);
}

void UiPanel::on_key(const KeyEvent& event)
{
    if (!impl_->context)
        return;

    ImGui::SetCurrentContext(impl_->context);
    ImGuiIO& io = ImGui::GetIO();

    const ModifierFlags mod = event.mod;
    io.AddKeyEvent(ImGuiMod_Ctrl, (mod & kModCtrl) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (mod & kModShift) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (mod & kModAlt) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (mod & kModSuper) != 0);

    const ImGuiKey key = sdl_scancode_to_imgui_key(event.scancode);
    if (key != ImGuiKey_None)
        io.AddKeyEvent(key, event.pressed);
}

bool UiPanel::wants_keyboard() const
{
    if (!impl_->context || !impl_->layout.visible)
        return false;

    ImGui::SetCurrentContext(impl_->context);
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace draxul
