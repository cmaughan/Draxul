#include <draxul/ui_panel.h>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
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

constexpr ImGuiTableFlags kMetricTableFlags = ImGuiTableFlags_SizingStretchProp
    | ImGuiTableFlags_BordersInnerV
    | ImGuiTableFlags_RowBg;

void apply_panel_style()
{
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.CellPadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
}

void help_marker(const char* text)
{
    if (!text || !text[0])
        return;

    ImGui::TextDisabled("(?)");
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        return;

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool begin_metric_table(const char* id)
{
    if (!ImGui::BeginTable(id, 2, kMetricTableFlags))
        return false;

    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthStretch, 1.35f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    return true;
}

void metric_label(const char* label, const char* help = nullptr)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help)
    {
        ImGui::SameLine();
        help_marker(help);
    }
    ImGui::TableSetColumnIndex(1);
}

void render_window_sections(const PanelLayout& layout, const DiagnosticPanelState& state)
{
    if (ImGui::CollapsingHeader("Help", ImGuiTreeNodeFlags_None))
    {
        ImGui::TextWrapped(
            "This inspector uses dockable windows inside the bottom panel. "
            "Drag tabs to rearrange panes or undock them temporarily while "
            "you inspect rendering and layout state.");
    }

    ImGui::SeparatorText("Dimensions");
    if (begin_metric_table("window_dimensions"))
    {
        metric_label("Window Size", "Current pixel size of the full Draxul window.");
        ImGui::Text("%d x %d px", layout.window_width, layout.window_height);

        metric_label("Terminal Region", "Height reserved for terminal content after the panel takes its share.");
        ImGui::Text("%d px", layout.terminal_height);

        metric_label("Panel Height", "Height reserved for the diagnostics panel.");
        ImGui::Text("%d px", layout.panel_height);

        metric_label("Panel Origin", "Y coordinate where the panel begins.");
        ImGui::Text("%d px", layout.panel_y);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Grid");
    if (begin_metric_table("window_grid"))
    {
        metric_label("Display PPI", "Detected pixel density for the current monitor.");
        ImGui::Text("%.0f ppi", state.display_ppi);

        metric_label("Cell Size", "Current terminal cell size after font metrics and DPI scaling.");
        ImGui::Text("%d x %d px", state.cell_width, state.cell_height);

        metric_label("Grid Size", "Active terminal grid in columns and rows.");
        ImGui::Text("%d x %d", state.grid_cols, state.grid_rows);
        ImGui::EndTable();
    }
}

void render_renderer_sections(const DiagnosticPanelState& state)
{
    ImGui::SeparatorText("Frame");
    if (begin_metric_table("renderer_frame"))
    {
        metric_label("Last Frame", "Duration of the most recently rendered frame.");
        ImGui::Text("%.2f ms", state.frame_ms);

        metric_label("Rolling Average", "Smoothed frame time across recent frames.");
        ImGui::Text("%.2f ms", state.average_frame_ms);

        metric_label("Dirty Cells", "Cells touched by the last redraw flush.");
        ImGui::Text("%zu", state.dirty_cells);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Atlas");
    ImGui::TextUnformatted("Occupancy");
    ImGui::SameLine();
    help_marker("How full the glyph atlas is right now. Higher values mean Draxul is closer to a rebuild.");
    {
        char overlay[32] = {};
        const float usage = std::clamp(state.atlas_usage_ratio, 0.0f, 1.0f);
        std::snprintf(overlay, sizeof(overlay), "%.1f%%", usage * 100.0f);
        ImGui::ProgressBar(usage, ImVec2(-1.0f, 0.0f), overlay);
    }

    if (begin_metric_table("renderer_atlas"))
    {
        metric_label("Glyphs", "Number of glyphs currently packed into the atlas.");
        ImGui::Text("%zu", state.atlas_glyph_count);

        metric_label("Resets", "Number of atlas rebuilds since startup.");
        ImGui::Text("%d", state.atlas_reset_count);
        ImGui::EndTable();
    }
}

void render_startup_sections(const DiagnosticPanelState& state)
{
#ifndef NDEBUG
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
    ImGui::TextUnformatted("DEBUG BUILD — timings are not representative of release performance");
    ImGui::PopStyleColor();
    ImGui::Spacing();
#endif

    if (state.startup_steps.empty())
    {
        ImGui::TextDisabled("No startup timing recorded.");
        return;
    }

    ImGui::SeparatorText("Phases");
    if (ImGui::BeginTable("startup_steps", 2, kMetricTableFlags))
    {
        ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        for (const auto& step : state.startup_steps)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(step.label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1f ms", step.ms);
        }

        // Total row
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Total");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.1f ms", state.startup_total_ms);

        ImGui::EndTable();
    }
}

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

ImGuiKey sdl_scancode_to_imgui_key(int scancode)
{
    switch (scancode)
    {
    case SDL_SCANCODE_TAB:
        return ImGuiKey_Tab;
    case SDL_SCANCODE_LEFT:
        return ImGuiKey_LeftArrow;
    case SDL_SCANCODE_RIGHT:
        return ImGuiKey_RightArrow;
    case SDL_SCANCODE_UP:
        return ImGuiKey_UpArrow;
    case SDL_SCANCODE_DOWN:
        return ImGuiKey_DownArrow;
    case SDL_SCANCODE_PAGEUP:
        return ImGuiKey_PageUp;
    case SDL_SCANCODE_PAGEDOWN:
        return ImGuiKey_PageDown;
    case SDL_SCANCODE_HOME:
        return ImGuiKey_Home;
    case SDL_SCANCODE_END:
        return ImGuiKey_End;
    case SDL_SCANCODE_INSERT:
        return ImGuiKey_Insert;
    case SDL_SCANCODE_DELETE:
        return ImGuiKey_Delete;
    case SDL_SCANCODE_BACKSPACE:
        return ImGuiKey_Backspace;
    case SDL_SCANCODE_SPACE:
        return ImGuiKey_Space;
    case SDL_SCANCODE_RETURN:
        return ImGuiKey_Enter;
    case SDL_SCANCODE_KP_ENTER:
        return ImGuiKey_KeypadEnter;
    case SDL_SCANCODE_ESCAPE:
        return ImGuiKey_Escape;
    case SDL_SCANCODE_APOSTROPHE:
        return ImGuiKey_Apostrophe;
    case SDL_SCANCODE_COMMA:
        return ImGuiKey_Comma;
    case SDL_SCANCODE_MINUS:
        return ImGuiKey_Minus;
    case SDL_SCANCODE_PERIOD:
        return ImGuiKey_Period;
    case SDL_SCANCODE_SLASH:
        return ImGuiKey_Slash;
    case SDL_SCANCODE_SEMICOLON:
        return ImGuiKey_Semicolon;
    case SDL_SCANCODE_EQUALS:
        return ImGuiKey_Equal;
    case SDL_SCANCODE_LEFTBRACKET:
        return ImGuiKey_LeftBracket;
    case SDL_SCANCODE_BACKSLASH:
        return ImGuiKey_Backslash;
    case SDL_SCANCODE_RIGHTBRACKET:
        return ImGuiKey_RightBracket;
    case SDL_SCANCODE_GRAVE:
        return ImGuiKey_GraveAccent;
    case SDL_SCANCODE_CAPSLOCK:
        return ImGuiKey_CapsLock;
    case SDL_SCANCODE_SCROLLLOCK:
        return ImGuiKey_ScrollLock;
    case SDL_SCANCODE_NUMLOCKCLEAR:
        return ImGuiKey_NumLock;
    case SDL_SCANCODE_PRINTSCREEN:
        return ImGuiKey_PrintScreen;
    case SDL_SCANCODE_PAUSE:
        return ImGuiKey_Pause;
    case SDL_SCANCODE_KP_0:
        return ImGuiKey_Keypad0;
    case SDL_SCANCODE_KP_1:
        return ImGuiKey_Keypad1;
    case SDL_SCANCODE_KP_2:
        return ImGuiKey_Keypad2;
    case SDL_SCANCODE_KP_3:
        return ImGuiKey_Keypad3;
    case SDL_SCANCODE_KP_4:
        return ImGuiKey_Keypad4;
    case SDL_SCANCODE_KP_5:
        return ImGuiKey_Keypad5;
    case SDL_SCANCODE_KP_6:
        return ImGuiKey_Keypad6;
    case SDL_SCANCODE_KP_7:
        return ImGuiKey_Keypad7;
    case SDL_SCANCODE_KP_8:
        return ImGuiKey_Keypad8;
    case SDL_SCANCODE_KP_9:
        return ImGuiKey_Keypad9;
    case SDL_SCANCODE_KP_PERIOD:
        return ImGuiKey_KeypadDecimal;
    case SDL_SCANCODE_KP_DIVIDE:
        return ImGuiKey_KeypadDivide;
    case SDL_SCANCODE_KP_MULTIPLY:
        return ImGuiKey_KeypadMultiply;
    case SDL_SCANCODE_KP_MINUS:
        return ImGuiKey_KeypadSubtract;
    case SDL_SCANCODE_KP_PLUS:
        return ImGuiKey_KeypadAdd;
    case SDL_SCANCODE_KP_EQUALS:
        return ImGuiKey_KeypadEqual;
    case SDL_SCANCODE_LCTRL:
        return ImGuiKey_LeftCtrl;
    case SDL_SCANCODE_LSHIFT:
        return ImGuiKey_LeftShift;
    case SDL_SCANCODE_LALT:
        return ImGuiKey_LeftAlt;
    case SDL_SCANCODE_LGUI:
        return ImGuiKey_LeftSuper;
    case SDL_SCANCODE_RCTRL:
        return ImGuiKey_RightCtrl;
    case SDL_SCANCODE_RSHIFT:
        return ImGuiKey_RightShift;
    case SDL_SCANCODE_RALT:
        return ImGuiKey_RightAlt;
    case SDL_SCANCODE_RGUI:
        return ImGuiKey_RightSuper;
    case SDL_SCANCODE_F1:
        return ImGuiKey_F1;
    case SDL_SCANCODE_F2:
        return ImGuiKey_F2;
    case SDL_SCANCODE_F3:
        return ImGuiKey_F3;
    case SDL_SCANCODE_F4:
        return ImGuiKey_F4;
    case SDL_SCANCODE_F5:
        return ImGuiKey_F5;
    case SDL_SCANCODE_F6:
        return ImGuiKey_F6;
    case SDL_SCANCODE_F7:
        return ImGuiKey_F7;
    case SDL_SCANCODE_F8:
        return ImGuiKey_F8;
    case SDL_SCANCODE_F9:
        return ImGuiKey_F9;
    case SDL_SCANCODE_F10:
        return ImGuiKey_F10;
    case SDL_SCANCODE_F11:
        return ImGuiKey_F11;
    case SDL_SCANCODE_F12:
        return ImGuiKey_F12;
    case SDL_SCANCODE_A:
        return ImGuiKey_A;
    case SDL_SCANCODE_B:
        return ImGuiKey_B;
    case SDL_SCANCODE_C:
        return ImGuiKey_C;
    case SDL_SCANCODE_D:
        return ImGuiKey_D;
    case SDL_SCANCODE_E:
        return ImGuiKey_E;
    case SDL_SCANCODE_F:
        return ImGuiKey_F;
    case SDL_SCANCODE_G:
        return ImGuiKey_G;
    case SDL_SCANCODE_H:
        return ImGuiKey_H;
    case SDL_SCANCODE_I:
        return ImGuiKey_I;
    case SDL_SCANCODE_J:
        return ImGuiKey_J;
    case SDL_SCANCODE_K:
        return ImGuiKey_K;
    case SDL_SCANCODE_L:
        return ImGuiKey_L;
    case SDL_SCANCODE_M:
        return ImGuiKey_M;
    case SDL_SCANCODE_N:
        return ImGuiKey_N;
    case SDL_SCANCODE_O:
        return ImGuiKey_O;
    case SDL_SCANCODE_P:
        return ImGuiKey_P;
    case SDL_SCANCODE_Q:
        return ImGuiKey_Q;
    case SDL_SCANCODE_R:
        return ImGuiKey_R;
    case SDL_SCANCODE_S:
        return ImGuiKey_S;
    case SDL_SCANCODE_T:
        return ImGuiKey_T;
    case SDL_SCANCODE_U:
        return ImGuiKey_U;
    case SDL_SCANCODE_V:
        return ImGuiKey_V;
    case SDL_SCANCODE_W:
        return ImGuiKey_W;
    case SDL_SCANCODE_X:
        return ImGuiKey_X;
    case SDL_SCANCODE_Y:
        return ImGuiKey_Y;
    case SDL_SCANCODE_Z:
        return ImGuiKey_Z;
    case SDL_SCANCODE_0:
        return ImGuiKey_0;
    case SDL_SCANCODE_1:
        return ImGuiKey_1;
    case SDL_SCANCODE_2:
        return ImGuiKey_2;
    case SDL_SCANCODE_3:
        return ImGuiKey_3;
    case SDL_SCANCODE_4:
        return ImGuiKey_4;
    case SDL_SCANCODE_5:
        return ImGuiKey_5;
    case SDL_SCANCODE_6:
        return ImGuiKey_6;
    case SDL_SCANCODE_7:
        return ImGuiKey_7;
    case SDL_SCANCODE_8:
        return ImGuiKey_8;
    case SDL_SCANCODE_9:
        return ImGuiKey_9;
    default:
        return ImGuiKey_None;
    }
}

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
