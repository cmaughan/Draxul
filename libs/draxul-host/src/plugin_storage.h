#pragma once

#include <draxul/plugin_api.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace draxul
{

// Private filesystem seam for deterministic storage failure coverage. This
// header is intentionally kept under src/ and is not part of draxul-host's
// public include surface.
class PluginStorageOutputFile
{
public:
    virtual ~PluginStorageOutputFile() = default;

    virtual bool write(std::string_view value,
        std::error_code& error) = 0;
    virtual bool flush(std::error_code& error) = 0;
};

class PluginStorageFileOperations
{
public:
    virtual ~PluginStorageFileOperations() = default;

    virtual bool create_directories(const std::filesystem::path& path,
        std::error_code& error) = 0;
    virtual bool file_size(const std::filesystem::path& path,
        std::uintmax_t& size, std::error_code& error) = 0;
    virtual bool read_all(const std::filesystem::path& path,
        std::string& value, std::error_code& error) = 0;
    virtual std::unique_ptr<PluginStorageOutputFile> open_output(
        const std::filesystem::path& path, std::error_code& error) = 0;
    virtual bool replace(const std::filesystem::path& temporary,
        const std::filesystem::path& target,
        std::error_code& error) = 0;
    virtual bool remove(const std::filesystem::path& path,
        std::error_code& error) = 0;
    virtual std::filesystem::path temporary_directory(
        std::error_code& error) = 0;
};

std::shared_ptr<PluginStorageFileOperations>
native_plugin_storage_file_operations();

class PluginStorage final
{
public:
    explicit PluginStorage(std::filesystem::path storage_root = {},
        std::shared_ptr<PluginStorageFileOperations> files = {});

    void initialize(std::string_view plugin_id, std::string_view pane_id,
        const std::filesystem::path& plugin_directory);

    const std::filesystem::path& service_path(uint32_t path_kind) const;
    int32_t get_service_path(uint32_t path_kind, char* buffer,
        size_t* in_out_size);
    std::filesystem::path storage_path(uint32_t scope,
        std::string_view key) const;

    uint32_t read_json(uint32_t scope, const char* key, size_t key_length,
        char* buffer, size_t* in_out_size);
    uint32_t write_json(uint32_t scope, const char* key, size_t key_length,
        const char* json, size_t json_length);
    uint32_t remove(uint32_t scope, const char* key, size_t key_length);

    void begin_overlay();
    bool commit_overlay(std::string& error);
    void discard_overlay();
    bool overlay_active() const noexcept { return overlay_active_; }

private:
    static bool valid_key(std::string_view key);
    static uint32_t copy_value(std::string_view value, char* buffer,
        size_t* in_out_size);
    bool write_file(const std::filesystem::path& path,
        std::string_view json, std::error_code& error);
    void cleanup_temporary(const std::filesystem::path& temporary,
        const std::error_code& primary_error,
        std::error_code& reported_error);

    std::filesystem::path storage_root_;
    std::shared_ptr<PluginStorageFileOperations> files_;
    std::string plugin_id_;
    std::string pane_id_;
    std::filesystem::path resource_path_;
    std::filesystem::path config_path_;
    std::filesystem::path data_path_;
    std::filesystem::path cache_path_;
    std::filesystem::path temporary_path_;
    bool overlay_active_ = false;
    std::unordered_map<std::string, std::optional<std::string>> overlay_;
};

} // namespace draxul
