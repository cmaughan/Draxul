#pragma once

#include <draxul/imgui_host.h>
#include <draxul/legacy_product_plugin_services.h>
#include <draxul/megacity_host.h>
#include <draxul/plugin_api.h>

#include <imgui.h>

#include <cstring>
#include <string>

namespace draxul::megacity::plugin
{

class ImGuiOverlayAdapter final : public IImGuiHost
{
public:
    bool discover(const DraxulPluginHostApiV2& host)
    {
        if (!host.query_service)
            return false;
        service_ = {};
        service_.struct_size = sizeof(service_);
        service_.service_version = DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_VERSION;
        available_ = host.query_service(host.host_context,
            DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_ID,
            std::strlen(DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_ID),
            DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_VERSION,
            &service_, sizeof(service_)) != 0
            && service_.imgui_version_num == IMGUI_VERSION_NUM
            && service_.draw_vert_size == sizeof(ImDrawVert)
            && service_.draw_idx_size == sizeof(ImDrawIdx)
            && service_.initialize && service_.shutdown
            && service_.rebuild_font_texture && service_.begin_frame
            && service_.render_draw_data && service_.get_font;
        return available_;
    }

    bool initialize_imgui_backend() override
    {
        ImGuiContext* context = ImGui::GetCurrentContext();
        initialized_ = available_ && context
            && service_.initialize(service_.service_context, context) != 0;
        initialized_context_ = initialized_ ? context : nullptr;
        return initialized_;
    }

    void shutdown_imgui_backend() override
    {
        if (initialized_ && initialized_context_)
            service_.shutdown(service_.service_context, initialized_context_);
        initialized_ = false;
        initialized_context_ = nullptr;
    }

    void rebuild_imgui_font_texture() override
    {
        if (initialized_ && initialized_context_)
            service_.rebuild_font_texture(service_.service_context, initialized_context_);
    }

    void begin_imgui_frame() override
    {
        if (!initialized_ && !initialize_imgui_backend())
            return;
        service_.begin_frame(service_.service_context, initialized_context_);
    }

    bool render_imgui_draw_data(const ImDrawData* draw_data,
        ImGuiContext* context) override
    {
        return initialized_ && service_.render_draw_data(
            service_.service_context, const_cast<ImDrawData*>(draw_data), context) != 0;
    }

    void synchronize_font(MegaCityHost& host)
    {
        if (!available_)
            return;
        size_t required = 0;
        float size_pixels = 0.0f;
        if (!service_.get_font(service_.service_context, nullptr,
                &required, &size_pixels) || required == 0)
            return;
        std::string path(required, '\0');
        if (!service_.get_font(service_.service_context, path.data(),
                &required, &size_pixels))
            return;
        path.resize(required > 0 ? required - 1 : 0);
        if (path == font_path_ && size_pixels == font_size_pixels_)
            return;
        font_path_ = std::move(path);
        font_size_pixels_ = size_pixels;
        host.set_imgui_font(font_path_, font_size_pixels_);
    }

    bool available() const { return available_; }

private:
    DraxulPluginImGuiOverlayServiceV2 service_{};
    ImGuiContext* initialized_context_ = nullptr;
    std::string font_path_;
    float font_size_pixels_ = 0.0f;
    bool available_ = false;
    bool initialized_ = false;
};

} // namespace draxul::megacity::plugin
