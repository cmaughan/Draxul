#include <draxul/plugin_imgui_context.h>

#include <imgui.h>

#include <algorithm>
#include <filesystem>

namespace draxul::plugin_support
{

PluginImGuiContext::~PluginImGuiContext()
{
    destroy();
}

bool PluginImGuiContext::create()
{
    return create(Options{});
}

bool PluginImGuiContext::create(const Options& options)
{
    destroy();
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    if (context_ == nullptr)
        return false;
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    if (options.docking_enable)
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (options.is_srgb)
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
    io.ConfigWindowsResizeFromEdges = options.windows_resize_from_edges;
    io.ConfigWindowsMoveFromTitleBarOnly = options.windows_move_from_title_bar_only;
    // Persistence is explicit: layout state goes through Options::ini_path,
    // never an implicit imgui.ini in the working directory.
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    ini_path_ = options.ini_path;
    if (!ini_path_.empty() && std::filesystem::exists(ini_path_))
        ImGui::LoadIniSettingsFromDisk(ini_path_.c_str());
    return true;
}

void PluginImGuiContext::set_font(const std::string& path, float size_pixels)
{
    if (context_ == nullptr)
        return;
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    if (!path.empty() && size_pixels > 0.0f)
        io.Fonts->AddFontFromFileTTF(path.c_str(), size_pixels);
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();
    if (host_ != nullptr)
        host_->rebuild_imgui_font_texture();
}

void PluginImGuiContext::attach_host(IImGuiHost& host)
{
    host_ = &host;
    if (context_ == nullptr)
        return;
    ImGui::SetCurrentContext(context_);
    host.initialize_imgui_backend();
    host.rebuild_imgui_font_texture();
}

bool PluginImGuiContext::begin_frame(
    int pixel_pos_x, int pixel_pos_y, int pixel_w, int pixel_h, float dt)
{
    if (!active())
        return false;
    ImGui::SetCurrentContext(context_);
    host_->begin_imgui_frame();
    ImGuiIO& io = ImGui::GetIO();
    // Product panes render in window-global pixels: DisplaySize spans from the
    // window origin to the pane's bottom-right corner.
    io.DisplaySize = ImVec2(
        static_cast<float>(pixel_pos_x + std::max(1, pixel_w)),
        static_cast<float>(pixel_pos_y + std::max(1, pixel_h)));
    io.DeltaTime = dt > 0.0f ? dt : (1.0f / 60.0f);
    ImGui::NewFrame();
    return true;
}

void PluginImGuiContext::destroy()
{
    if (context_ == nullptr)
    {
        host_ = nullptr;
        return;
    }
    ImGui::SetCurrentContext(context_);
    if (!ini_path_.empty())
        ImGui::SaveIniSettingsToDisk(ini_path_.c_str());
    if (host_ != nullptr)
        host_->shutdown_imgui_backend();
    ImGui::DestroyContext(context_);
    context_ = nullptr;
    host_ = nullptr;
    ini_path_.clear();
}

} // namespace draxul::plugin_support
