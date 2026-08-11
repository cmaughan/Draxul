#pragma once

#include <draxul/plugin_api.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace draxul
{

struct PluginManifest
{
    std::string id;
    std::string name;
    std::string version;
    uint32_t abi_version = 0;
    std::filesystem::path directory;
    std::filesystem::path library_path;
    bool user_installed = false;
    std::string error;
};

class LoadedPlugin
{
public:
    ~LoadedPlugin();
    LoadedPlugin(const LoadedPlugin&) = delete;
    LoadedPlugin& operator=(const LoadedPlugin&) = delete;

    const PluginManifest& manifest() const noexcept { return manifest_; }
    const DraxulPluginApiV1& api() const noexcept { return *api_; }

private:
    friend class PluginManager;
    LoadedPlugin(PluginManifest manifest, void* module, const DraxulPluginApiV1* api)
        : manifest_(std::move(manifest)), module_(module), api_(api) {}

    PluginManifest manifest_;
    void* module_ = nullptr;
    const DraxulPluginApiV1* api_ = nullptr;
};

class PluginManager
{
public:
    PluginManager() = default;

    static std::shared_ptr<PluginManager> discover_default();
    static std::shared_ptr<PluginManager> discover(
        const std::filesystem::path& bundled_directory,
        const std::filesystem::path& user_directory);

    const std::vector<PluginManifest>& manifests() const noexcept { return manifests_; }
    const PluginManifest* find(std::string_view id) const;
    std::shared_ptr<LoadedPlugin> load(std::string_view id, std::string& error);

    static bool valid_plugin_id(std::string_view id);

private:
    static std::vector<PluginManifest> scan_tier(
        const std::filesystem::path& directory, bool user_installed);
    void merge_tier(std::vector<PluginManifest> tier);

    std::vector<PluginManifest> manifests_;
    std::unordered_map<std::string, size_t> index_;
    // Strong cache by design: native modules remain resident until the
    // manager (and therefore the UI process) shuts down.
    std::unordered_map<std::string, std::shared_ptr<LoadedPlugin>> loaded_;
    mutable std::mutex mutex_;
};

} // namespace draxul
