#include <draxul/plugin_manager.h>

#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/runtime_path.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <toml++/toml.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace draxul
{
namespace
{

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
    HMODULE module = LoadLibraryW(path.c_str());
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
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    return std::filesystem::path(appdata && *appdata ? appdata : ".") / "draxul" / "plugins";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home && *home ? home : ".") / "Library" / "Application Support" / "draxul" / "plugins";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::filesystem::path base = xdg && *xdg ? xdg : (home && *home ? std::filesystem::path(home) / ".config" : std::filesystem::path("."));
    return base / "draxul" / "plugins";
#endif
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

} // namespace

LoadedPlugin::~LoadedPlugin()
{
    close_module(module_);
}

std::shared_ptr<PluginManager> PluginManager::discover_default()
{
    return discover(bundled_plugin_directory(), user_plugin_directory());
}

std::shared_ptr<PluginManager> PluginManager::discover(
    const std::filesystem::path& bundled_directory,
    const std::filesystem::path& user_directory)
{
    PERF_MEASURE();
    auto manager = std::make_shared<PluginManager>();
    manager->merge_tier(scan_tier(bundled_directory, false));
    manager->merge_tier(scan_tier(user_directory, true));
    return manager;
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
        if (entry.is_directory(error) && std::filesystem::exists(entry.path() / "plugin.toml", error))
            manifests.push_back(entry.path() / "plugin.toml");
    }
    std::sort(manifests.begin(), manifests.end());
    std::unordered_map<std::string, size_t> ids;
    for (const auto& path : manifests)
    {
        PluginManifest manifest = parse_manifest(path, user_installed);
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
    std::scoped_lock lock(mutex_);
    const std::string key(id);
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
    void* module = open_module(manifest->library_path, error);
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
        || manifest->id != api->plugin_id
        || manifest->name != api->display_name
        || manifest->version != api->plugin_version
        || !api->create_instance
        || !api->destroy_instance)
    {
        error = "Plugin ABI or identity validation failed: " + manifest->id;
        close_module(module);
        return {};
    }
    auto plugin = std::shared_ptr<LoadedPlugin>(new LoadedPlugin(*manifest, module, api));
    loaded_[key] = plugin;
    return plugin;
}

} // namespace draxul
