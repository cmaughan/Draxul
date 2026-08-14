#include <draxul/plugin_gpu_imgui.h>

#include <cstring>

namespace draxul::plugin_support
{

bool UiStyleClient::discover(const DraxulPluginHostApiV2& host)
{
    service_ = {};
    applied_generation_ = 0;
    if (!host.query_service)
        return false;
    service_.struct_size = sizeof(service_);
    service_.service_version = DRAXUL_PLUGIN_UI_STYLE_SERVICE_VERSION;
    return host.query_service(host.host_context,
        DRAXUL_PLUGIN_UI_STYLE_SERVICE_ID,
        std::strlen(DRAXUL_PLUGIN_UI_STYLE_SERVICE_ID),
        DRAXUL_PLUGIN_UI_STYLE_SERVICE_VERSION,
        &service_, sizeof(service_)) != 0
        && service_.struct_size >= sizeof(service_)
        && service_.service_version == DRAXUL_PLUGIN_UI_STYLE_SERVICE_VERSION
        && service_.get_recommended_font;
}

std::optional<RecommendedUiFont> UiStyleClient::poll()
{
    if (!service_.get_recommended_font)
        return std::nullopt;
    size_t required = 0;
    RecommendedUiFont result;
    if (!service_.get_recommended_font(service_.service_context,
            nullptr, &required, &result.size_pixels, &result.display_scale,
            &result.generation)
        || required == 0 || result.generation == applied_generation_)
        return std::nullopt;
    result.path.resize(required);
    if (!service_.get_recommended_font(service_.service_context,
            result.path.data(), &required, &result.size_pixels,
            &result.display_scale, &result.generation))
        return std::nullopt;
    result.path.resize(required > 0 ? required - 1 : 0);
    applied_generation_ = result.generation;
    return result;
}

} // namespace draxul::plugin_support
