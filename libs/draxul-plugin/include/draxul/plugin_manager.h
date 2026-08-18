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
    std::string package_generation;
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
    const DraxulPluginApiV2& api() const noexcept { return *api_; }
    std::string_view generation() const noexcept { return generation_; }

private:
    friend class PluginManager;
    LoadedPlugin(PluginManifest manifest, void* module,
        const DraxulPluginApiV2* api, std::string generation)
        : manifest_(std::move(manifest)), module_(module), api_(api),
          generation_(std::move(generation)) {}

    PluginManifest manifest_;
    void* module_ = nullptr;
    const DraxulPluginApiV2* api_ = nullptr;
    std::string generation_;
};

class PluginManager
{
public:
    PluginManager() = default;
    ~PluginManager();

    static std::shared_ptr<PluginManager> discover_default();
    static std::shared_ptr<PluginManager> discover(
        const std::filesystem::path& bundled_directory,
        const std::filesystem::path& user_directory,
        const std::filesystem::path& runtime_directory = {});

    const std::vector<PluginManifest>& manifests() const noexcept { return manifests_; }
    const PluginManifest* find(std::string_view id) const;
    std::shared_ptr<LoadedPlugin> load(std::string_view id, std::string& error);
    std::shared_ptr<LoadedPlugin> prepare_reload(
        std::string_view id, std::string& error);
    bool activate(const std::shared_ptr<LoadedPlugin>& plugin);
    bool refresh(std::string& error);

    static bool valid_plugin_id(std::string_view id);

private:
    static std::vector<PluginManifest> scan_tier(
        const std::filesystem::path& directory, bool user_installed);
    void merge_tier(std::vector<PluginManifest> tier);
    std::shared_ptr<LoadedPlugin> load_generation(
        const PluginManifest& manifest, std::string& error);
    std::optional<PluginManifest> stage_generation(
        const PluginManifest& manifest, std::string& error);

    std::filesystem::path bundled_directory_;
    std::filesystem::path user_directory_;
    std::filesystem::path runtime_directory_;
    std::vector<PluginManifest> manifests_;
    std::unordered_map<std::string, size_t> index_;
    // Strong cache by design: native modules remain resident until the
    // manager (and therefore the UI process) shuts down.
    std::unordered_map<std::string, std::shared_ptr<LoadedPlugin>> loaded_;
    // Retired native images remain resident until manager/process shutdown.
    // Instance destruction is safe; general-purpose native module unloading is not.
    std::vector<std::shared_ptr<LoadedPlugin>> resident_;
    mutable std::mutex mutex_;
};

} // namespace draxul
