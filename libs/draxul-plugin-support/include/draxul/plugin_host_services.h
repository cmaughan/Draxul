#pragma once

#include <draxul/plugin_api.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace draxul::plugin_support
{

struct StorageReadResult
{
    uint32_t result = DRAXUL_PLUGIN_STORAGE_NOT_FOUND;
    std::string json;

    [[nodiscard]] bool ok() const
    {
        return result == DRAXUL_PLUGIN_STORAGE_OK;
    }
};

struct UiStyle
{
    std::filesystem::path font_path;
    float size_pixels = 13.0f;
    float display_scale = 1.0f;
    uint64_t generation = 0;
};

// Same-build C++ convenience around the versioned C host-service tables. It
// owns no Draxul object and stores only copied function tables and paths.
class HostServices
{
public:
    explicit HostServices(const DraxulPluginCreateInfoV2& create_info);

    [[nodiscard]] bool has_paths() const;
    [[nodiscard]] bool has_storage() const;
    [[nodiscard]] bool has_ui_style() const;
    [[nodiscard]] std::filesystem::path plugin_directory() const;
    [[nodiscard]] std::filesystem::path path(uint32_t kind) const;
    [[nodiscard]] StorageReadResult read_json(
        uint32_t scope, std::string_view key) const;
    [[nodiscard]] uint32_t write_json(
        uint32_t scope, std::string_view key, std::string_view json) const;
    [[nodiscard]] uint32_t remove(uint32_t scope, std::string_view key) const;
    [[nodiscard]] std::optional<UiStyle> ui_style() const;

    void request_redraw() const;
    void request_tick() const;
    void notify_presentation_changed() const;
    void log(uint32_t level, std::string_view message) const;

private:
    const DraxulPluginHostApiV2* host_ = nullptr;
    std::filesystem::path plugin_directory_;
    DraxulPluginPathServiceV2 paths_{};
    DraxulPluginStorageServiceV2 storage_{};
    DraxulPluginUiStyleServiceV2 ui_style_{};
    bool has_paths_ = false;
    bool has_storage_ = false;
    bool has_ui_style_ = false;
};

} // namespace draxul::plugin_support
