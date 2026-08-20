#include <draxul/plugin_manager.h>

#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/runtime_path.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <toml++/toml.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace draxul
{
namespace
{

std::string next_runtime_generation();

void close_module(void* module)
{
    if (!module)
        return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(module));
#else
    dlclose(module);
#endif
}

void* open_module(const std::filesystem::path& path, std::string& error)
{
#ifdef _WIN32
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module)
        error = "LoadLibrary failed for " + path.string() + " (error " + std::to_string(GetLastError()) + ")";
    return module;
#else
    void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!module)
    {
        const char* detail = dlerror();
        error = detail ? detail : "dlopen failed";
    }
    return module;
#endif
}

void* module_symbol(void* module, const char* name)
{
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
#else
    return dlsym(module, name);
#endif
}

std::filesystem::path user_plugin_directory()
{
    return user_config_dir() / "draxul" / "plugins";
}

std::filesystem::path bundled_plugin_directory()
{
    const auto executable = executable_directory();
#ifdef __APPLE__
    if (executable.filename() == "MacOS")
        return executable.parent_path() / "PlugIns";
#endif
    return executable / "plugins";
}

std::optional<std::string> table_string(const toml::table& table, std::string_view key)
{
    if (const auto value = table[key].value<std::string>())
        return *value;
    return std::nullopt;
}

PluginManifest parse_manifest(const std::filesystem::path& path, bool user_installed)
{
    PluginManifest result;
    result.directory = path.parent_path();
    result.user_installed = user_installed;
    try
    {
        const toml::table document = toml::parse_file(path.string());
        const auto schema = document["schema_version"].value<int64_t>();
        const auto id = table_string(document, "id");
        const auto name = table_string(document, "name");
        const auto version = table_string(document, "version");
        const auto abi = document["abi_version"].value<int64_t>();
        if (!schema || *schema != 1 || !id || !name || !version || !abi
            || *abi != DRAXUL_PLUGIN_ABI_VERSION || !PluginManager::valid_plugin_id(*id))
        {
            result.error = "Invalid plugin manifest metadata";
            result.id = id.value_or(path.parent_path().filename().string());
            return result;
        }
        result.id = *id;
        result.name = *name;
        result.version = *version;
        result.abi_version = static_cast<uint32_t>(*abi);
#ifdef _WIN32
        const toml::table* platform = document["platform"]["windows"].as_table();
#elif defined(__APPLE__)
        const toml::table* platform = document["platform"]["macos"].as_table();
#else
        const toml::table* platform = document["platform"]["linux"].as_table();
#endif
        const auto library = platform ? table_string(*platform, "library") : std::nullopt;
        if (!library || library->empty())
        {
            result.error = "Plugin has no library for this platform";
            return result;
        }
        const auto candidate = result.directory / *library;
        result.library_path = candidate.lexically_normal();
        if (!std::filesystem::exists(result.library_path))
            result.error = "Plugin library is missing: " + result.library_path.string();
    }
    catch (const std::exception& exception)
    {
        result.id = path.parent_path().filename().string();
        result.error = exception.what();
    }
    return result;
}

std::filesystem::path process_plugin_runtime_directory()
{
#ifdef _WIN32
    const uint64_t process_id = GetCurrentProcessId();
#else
    const uint64_t process_id = static_cast<uint64_t>(getpid());
#endif
    return user_cache_dir() / "draxul" / "plugin-runtime"
        / ("process-" + std::to_string(process_id) + "-"
            + next_runtime_generation());
}

std::optional<std::filesystem::path> published_manifest(
    const std::filesystem::path& plugin_directory)
{
    const auto legacy = plugin_directory / "plugin.toml";
    std::error_code error;
    const auto pointer = plugin_directory / "current.json";
    if (std::filesystem::is_regular_file(pointer, error))
    {
        try
        {
            std::ifstream input(pointer, std::ios::binary);
            const auto document = nlohmann::json::parse(input);
            const std::string generation = document.value("generation", "");
            if (!generation.empty() && generation != "." && generation != ".."
                && generation.find('/') == std::string::npos
                && generation.find('\\') == std::string::npos)
            {
                const auto manifest = plugin_directory / "generations"
                    / generation / "plugin.toml";
                if (std::filesystem::is_regular_file(manifest, error))
                    return manifest;
            }
        }
        catch (...)
        {
        }
        // Once a publisher marker exists it is authoritative. Returning it
        // makes discovery surface an invalid manifest instead of silently
        // falling back to an older, potentially unrelated legacy package.
        return pointer;
    }
    if (std::filesystem::is_regular_file(legacy, error))
        return legacy;
    return std::nullopt;
}

std::string next_runtime_generation()
{
    static std::atomic<uint64_t> serial{ 0 };
    const auto ticks = std::chrono::steady_clock::now()
                           .time_since_epoch().count();
    return std::to_string(ticks) + "-"
        + std::to_string(serial.fetch_add(1));
}

} // namespace

LoadedPlugin::~LoadedPlugin()
{
    close_module(module_);
}

PluginManager::~PluginManager()
{
    std::scoped_lock lock(mutex_);
    loaded_.clear();
    resident_.clear();
    std::error_code error;
    std::filesystem::remove_all(runtime_directory_, error);
}

std::shared_ptr<PluginManager> PluginManager::discover_default()
{
    return discover(bundled_plugin_directory(), user_plugin_directory(),
        process_plugin_runtime_directory());
}

std::shared_ptr<PluginManager> PluginManager::discover(
    const std::filesystem::path& bundled_directory,
    const std::filesystem::path& user_directory,
    const std::filesystem::path& runtime_directory)
{
    PERF_MEASURE();
    auto manager = std::make_shared<PluginManager>();
    manager->bundled_directory_ = bundled_directory;
    manager->user_directory_ = user_directory;
    manager->runtime_directory_ = runtime_directory.empty()
        ? user_directory.parent_path() / ".draxul-plugin-runtime"
        : runtime_directory;
    std::error_code cleanup_error;
    std::filesystem::remove_all(manager->runtime_directory_, cleanup_error);
    std::filesystem::create_directories(
        manager->runtime_directory_, cleanup_error);
    std::string refresh_error;
    manager->refresh(refresh_error);
    return manager;
}

bool PluginManager::refresh(std::string& error)
{
    std::scoped_lock lock(mutex_);
    try
    {
        manifests_.clear();
        index_.clear();
        merge_tier(scan_tier(bundled_directory_, false));
        merge_tier(scan_tier(user_directory_, true));
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

bool PluginManager::valid_plugin_id(std::string_view id)
{
    if (id.empty() || id.size() > 128)
        return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char ch) {
        return std::islower(ch) || std::isdigit(ch) || ch == '.' || ch == '_' || ch == '-';
    });
}

std::vector<PluginManifest> PluginManager::scan_tier(
    const std::filesystem::path& directory, bool user_installed)
{
    std::vector<PluginManifest> result;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
        return result;
    std::vector<std::filesystem::path> manifests;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error))
    {
        if (!entry.is_directory(error))
            continue;
        if (const auto manifest = published_manifest(entry.path()))
            manifests.push_back(*manifest);
    }
    std::sort(manifests.begin(), manifests.end());
    std::unordered_map<std::string, size_t> ids;
    for (const auto& path : manifests)
    {
        PluginManifest manifest = parse_manifest(path, user_installed);
        if (path.parent_path().parent_path().filename() == "generations")
            manifest.package_generation = path.parent_path().filename().string();
        else
            manifest.package_generation = "legacy";
        const auto existing = ids.find(manifest.id);
        if (!manifest.id.empty() && existing != ids.end())
        {
            const std::string detail
                = "Duplicate plugin id in discovery tier: "
                + manifest.id;
            result[existing->second].error = detail;
            manifest.error = detail;
        }
        else if (!manifest.id.empty())
        {
            ids.emplace(manifest.id, result.size());
        }
        result.push_back(std::move(manifest));
    }
    return result;
}

void PluginManager::merge_tier(std::vector<PluginManifest> tier)
{
    for (auto& manifest : tier)
    {
        if (manifest.id.empty())
            continue;
        if (const auto existing = index_.find(manifest.id); existing != index_.end())
        {
            if (manifest.user_installed)
            {
                DRAXUL_LOG_WARN(LogCategory::App, "User plugin '%s' overrides bundled plugin", manifest.id.c_str());
                manifests_[existing->second] = std::move(manifest);
            }
            continue;
        }
        index_[manifest.id] = manifests_.size();
        manifests_.push_back(std::move(manifest));
    }
}

const PluginManifest* PluginManager::find(std::string_view id) const
{
    const auto found = index_.find(std::string(id));
    return found == index_.end() ? nullptr : &manifests_[found->second];
}

std::shared_ptr<LoadedPlugin> PluginManager::load(std::string_view id, std::string& error)
{
    PERF_MEASURE();
    const std::string key(id);
    {
        std::scoped_lock lock(mutex_);
        if (const auto loaded = loaded_.find(key); loaded != loaded_.end())
            return loaded->second;
    }

    // A build can atomically publish and eventually prune plugin package
    // generations while this UI remains open. Refresh before the first load so
    // a command-palette launch does not retain a path to a pruned generation.
    if (!refresh(error))
        return {};

    std::scoped_lock lock(mutex_);
    if (const auto loaded = loaded_.find(key); loaded != loaded_.end())
        return loaded->second;
    const PluginManifest* manifest = find(id);
    if (!manifest)
    {
        error = "Plugin is not installed: " + key;
        return {};
    }
    if (!manifest->error.empty())
    {
        error = manifest->error;
        return {};
    }
    const auto plugin = load_generation(*manifest, error);
    if (!plugin)
        return {};
    loaded_[key] = plugin;
    resident_.push_back(plugin);
    return plugin;
}

std::optional<PluginManifest> PluginManager::stage_generation(
    const PluginManifest& manifest, std::string& error)
{
    const std::string runtime_generation = next_runtime_generation();
    const auto target = runtime_directory_ / manifest.id / runtime_generation;
    std::error_code copy_error;
    std::filesystem::create_directories(target.parent_path(), copy_error);
    std::filesystem::copy(manifest.directory, target,
        std::filesystem::copy_options::recursive
            | std::filesystem::copy_options::overwrite_existing,
        copy_error);
    if (copy_error)
    {
        error = "Unable to stage plugin generation: " + copy_error.message();
        return std::nullopt;
    }
    PluginManifest staged = manifest;
    const auto relative_library = std::filesystem::relative(
        manifest.library_path, manifest.directory, copy_error);
    if (copy_error || relative_library.empty()
        || relative_library.native().starts_with(
            std::filesystem::path("..").native()))
    {
        error = "Plugin library is outside its package: " + manifest.id;
        return std::nullopt;
    }
    staged.directory = target;
    staged.library_path = target / relative_library;
    staged.package_generation = runtime_generation;
    return staged;
}

std::shared_ptr<LoadedPlugin> PluginManager::load_generation(
    const PluginManifest& source_manifest, std::string& error)
{
    const auto staged = stage_generation(source_manifest, error);
    if (!staged)
        return {};
    void* module = open_module(staged->library_path, error);
    if (!module)
        return {};
    const auto query = reinterpret_cast<DraxulPluginQueryFnV2>(
        module_symbol(module, DRAXUL_PLUGIN_QUERY_SYMBOL));
    if (!query)
    {
        error = "Plugin does not export " DRAXUL_PLUGIN_QUERY_SYMBOL;
        close_module(module);
        return {};
    }
    const DraxulPluginApiV2* api = query(DRAXUL_PLUGIN_ABI_VERSION);
    if (!api || api->struct_size < sizeof(DraxulPluginApiV2)
        || api->abi_version != DRAXUL_PLUGIN_ABI_VERSION || !api->plugin_id
        || !api->display_name || !api->plugin_version
        || staged->id != api->plugin_id
        || staged->name != api->display_name
        || staged->version != api->plugin_version
        || !api->create_instance
        || !api->destroy_instance)
    {
        error = "Plugin ABI or identity validation failed: " + staged->id;
        close_module(module);
        return {};
    }
    return std::shared_ptr<LoadedPlugin>(new LoadedPlugin(
        *staged, module, api, staged->package_generation));
}

std::shared_ptr<LoadedPlugin> PluginManager::prepare_reload(
    std::string_view id, std::string& error)
{
    if (!refresh(error))
        return {};
    std::scoped_lock lock(mutex_);
    const PluginManifest* manifest = find(id);
    if (!manifest)
    {
        error = "Plugin is not installed: " + std::string(id);
        return {};
    }
    if (!manifest->error.empty())
    {
        error = manifest->error;
        return {};
    }
    auto plugin = load_generation(*manifest, error);
    if (plugin)
        resident_.push_back(plugin);
    return plugin;
}

bool PluginManager::activate(const std::shared_ptr<LoadedPlugin>& plugin)
{
    if (!plugin)
        return false;
    std::scoped_lock lock(mutex_);
    loaded_[plugin->manifest().id] = plugin;
    return true;
}

} // namespace draxul
