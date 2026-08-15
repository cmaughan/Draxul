#include <draxul/plugin_host_services.h>

#include <cstring>
#include <vector>

namespace draxul::plugin_support
{
namespace
{

constexpr size_t kMaximumPathBytes = 32u * 1024u;

template <typename Service>
bool query_service(const DraxulPluginHostApiV2* host,
    const char* id, uint32_t version, Service& service)
{
    if (!host || !host->query_service)
        return false;
    service.struct_size = sizeof(Service);
    service.service_version = version;
    return host->query_service(host->host_context, id, std::strlen(id), version,
               &service, sizeof(Service))
        != 0;
}

std::filesystem::path utf8_path(const std::vector<char>& bytes)
{
    if (bytes.empty() || bytes.back() != '\0')
        return {};
    return std::filesystem::u8path(bytes.data());
}

} // namespace

HostServices::HostServices(const DraxulPluginCreateInfoV2& create_info)
    : host_(create_info.host)
    , plugin_directory_(create_info.plugin_directory_utf8
              ? std::filesystem::u8path(create_info.plugin_directory_utf8)
              : std::filesystem::path{})
{
    has_paths_ = query_service(host_, DRAXUL_PLUGIN_PATH_SERVICE_ID,
        DRAXUL_PLUGIN_PATH_SERVICE_VERSION, paths_);
    has_storage_ = query_service(host_, DRAXUL_PLUGIN_STORAGE_SERVICE_ID,
        DRAXUL_PLUGIN_STORAGE_SERVICE_VERSION, storage_);
    has_ui_style_ = query_service(host_, DRAXUL_PLUGIN_UI_STYLE_SERVICE_ID,
        DRAXUL_PLUGIN_UI_STYLE_SERVICE_VERSION, ui_style_);
}

bool HostServices::has_paths() const
{
    return has_paths_;
}

bool HostServices::has_storage() const
{
    return has_storage_;
}

bool HostServices::has_ui_style() const
{
    return has_ui_style_;
}

std::filesystem::path HostServices::plugin_directory() const
{
    return plugin_directory_;
}

std::filesystem::path HostServices::path(uint32_t kind) const
{
    if (!has_paths_ || !paths_.get_path)
        return kind == DRAXUL_PLUGIN_PATH_RESOURCES ? plugin_directory_ : std::filesystem::path{};

    size_t required = 0;
    if (!paths_.get_path(paths_.service_context, kind, nullptr, &required)
        || required == 0 || required > kMaximumPathBytes)
    {
        return {};
    }
    std::vector<char> bytes(required);
    size_t capacity = bytes.size();
    if (!paths_.get_path(paths_.service_context, kind, bytes.data(), &capacity)
        || capacity == 0 || capacity > bytes.size())
    {
        return {};
    }
    bytes.resize(capacity);
    return utf8_path(bytes);
}

StorageReadResult HostServices::read_json(
    uint32_t scope, std::string_view key) const
{
    StorageReadResult result;
    if (!has_storage_ || !storage_.read_json)
    {
        result.result = DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE;
        return result;
    }

    size_t required = 0;
    result.result = storage_.read_json(storage_.service_context, scope,
        key.data(), key.size(), nullptr, &required);
    if (result.result != DRAXUL_PLUGIN_STORAGE_OK)
        return result;
    if (required == 0 || required > DRAXUL_PLUGIN_MAX_STORAGE_JSON_BYTES + 1u)
    {
        result.result = DRAXUL_PLUGIN_STORAGE_TOO_LARGE;
        return result;
    }

    std::vector<char> bytes(required);
    size_t capacity = bytes.size();
    result.result = storage_.read_json(storage_.service_context, scope,
        key.data(), key.size(), bytes.data(), &capacity);
    if (result.result == DRAXUL_PLUGIN_STORAGE_OK
        && capacity > 0 && capacity <= bytes.size()
        && bytes[capacity - 1] == '\0')
    {
        result.json.assign(bytes.data(), capacity - 1);
    }
    else if (result.result == DRAXUL_PLUGIN_STORAGE_OK)
    {
        result.result = DRAXUL_PLUGIN_STORAGE_IO_ERROR;
    }
    return result;
}

uint32_t HostServices::write_json(
    uint32_t scope, std::string_view key, std::string_view json) const
{
    if (!has_storage_ || !storage_.write_json)
        return DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE;
    return storage_.write_json(storage_.service_context, scope,
        key.data(), key.size(), json.data(), json.size());
}

uint32_t HostServices::remove(uint32_t scope, std::string_view key) const
{
    if (!has_storage_ || !storage_.remove)
        return DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE;
    return storage_.remove(storage_.service_context, scope, key.data(), key.size());
}

std::optional<UiStyle> HostServices::ui_style() const
{
    if (!has_ui_style_ || !ui_style_.get_recommended_font)
        return std::nullopt;

    UiStyle result;
    size_t required = 0;
    if (!ui_style_.get_recommended_font(ui_style_.service_context, nullptr,
            &required, &result.size_pixels, &result.display_scale,
            &result.generation)
        || required == 0 || required > kMaximumPathBytes)
    {
        return std::nullopt;
    }
    std::vector<char> bytes(required);
    size_t capacity = bytes.size();
    if (!ui_style_.get_recommended_font(ui_style_.service_context, bytes.data(),
            &capacity, &result.size_pixels, &result.display_scale,
            &result.generation)
        || capacity == 0 || capacity > bytes.size())
    {
        return std::nullopt;
    }
    bytes.resize(capacity);
    result.font_path = utf8_path(bytes);
    return result;
}

void HostServices::request_redraw() const
{
    if (host_ && host_->request_redraw)
        host_->request_redraw(host_->host_context);
}

void HostServices::request_tick() const
{
    if (host_ && host_->request_tick)
        host_->request_tick(host_->host_context);
}

void HostServices::notify_presentation_changed() const
{
    if (host_ && host_->notify_presentation_changed)
        host_->notify_presentation_changed(host_->host_context);
}

void HostServices::log(uint32_t level, std::string_view message) const
{
    if (host_ && host_->log)
        host_->log(host_->host_context, level, message.data(), message.size());
}

} // namespace draxul::plugin_support
