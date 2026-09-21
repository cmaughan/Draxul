#include "plugin_storage.h"

#include <draxul/runtime_path.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace draxul
{
namespace
{

std::error_code io_error()
{
    return std::make_error_code(std::errc::io_error);
}

std::string path_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()),
        encoded.size());
}

std::filesystem::path path_from_utf8(std::string_view value)
{
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char ch : value)
        encoded.push_back(static_cast<char8_t>(ch));
    return std::filesystem::path(encoded);
}

class NativePluginStorageOutputFile final
    : public PluginStorageOutputFile
{
public:
    explicit NativePluginStorageOutputFile(
        const std::filesystem::path& path)
        : output_(path, std::ios::binary | std::ios::trunc)
    {
    }

    bool is_open() const noexcept { return output_.is_open(); }

    bool write(std::string_view value,
        std::error_code& error) override
    {
        output_.write(value.data(),
            static_cast<std::streamsize>(value.size()));
        if (output_)
            return true;
        error = io_error();
        return false;
    }

    bool flush(std::error_code& error) override
    {
        output_.flush();
        if (output_)
            return true;
        error = io_error();
        return false;
    }

private:
    std::ofstream output_;
};

class NativePluginStorageFileOperations final
    : public PluginStorageFileOperations
{
public:
    bool create_directories(const std::filesystem::path& path,
        std::error_code& error) override
    {
        std::filesystem::create_directories(path, error);
        return !error;
    }

    bool file_size(const std::filesystem::path& path,
        std::uintmax_t& size, std::error_code& error) override
    {
        size = std::filesystem::file_size(path, error);
        return !error;
    }

    bool read_all(const std::filesystem::path& path, std::string& value,
        std::error_code& error) override
    {
        std::ifstream input(path, std::ios::binary);
        value.assign(std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
        if (input.good() || input.eof())
            return true;
        error = io_error();
        return false;
    }

    std::unique_ptr<PluginStorageOutputFile> open_output(
        const std::filesystem::path& path,
        std::error_code& error) override
    {
        auto output
            = std::make_unique<NativePluginStorageOutputFile>(path);
        if (output->is_open())
            return output;
        error = io_error();
        return {};
    }

    bool replace(const std::filesystem::path& temporary,
        const std::filesystem::path& target,
        std::error_code& error) override
    {
#ifdef _WIN32
        if (MoveFileExW(temporary.c_str(), target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        error = std::error_code(static_cast<int>(GetLastError()),
            std::system_category());
        return false;
#else
        std::filesystem::rename(temporary, target, error);
        return !error;
#endif
    }

    bool remove(const std::filesystem::path& path,
        std::error_code& error) override
    {
        return std::filesystem::remove(path, error);
    }

    std::filesystem::path temporary_directory(
        std::error_code& error) override
    {
        return std::filesystem::temp_directory_path(error);
    }
};

} // namespace

std::shared_ptr<PluginStorageFileOperations>
native_plugin_storage_file_operations()
{
    static auto files
        = std::make_shared<NativePluginStorageFileOperations>();
    return files;
}

PluginStorage::PluginStorage(std::filesystem::path storage_root,
    std::shared_ptr<PluginStorageFileOperations> files)
    : storage_root_(std::move(storage_root))
    , files_(files ? std::move(files)
                   : native_plugin_storage_file_operations())
{
}

void PluginStorage::initialize(std::string_view plugin_id,
    std::string_view pane_id,
    const std::filesystem::path& plugin_directory)
{
    plugin_id_.assign(plugin_id);
    pane_id_.assign(pane_id);
    resource_path_ = plugin_directory;
    if (!storage_root_.empty())
    {
        config_path_ = storage_root_ / "config" / plugin_id_;
        data_path_ = storage_root_ / "data" / plugin_id_;
        cache_path_ = storage_root_ / "cache" / plugin_id_;
        temporary_path_ = storage_root_ / "temporary" / plugin_id_;
        return;
    }

#ifdef _WIN32
    config_path_ = user_config_dir() / "draxul" / "plugin-config"
        / plugin_id_;
    data_path_ = user_data_dir() / "draxul" / "plugin-data"
        / plugin_id_;
    cache_path_ = user_cache_dir() / "draxul" / "cache" / "plugins"
        / plugin_id_;
#elif defined(__APPLE__)
    const std::filesystem::path support = user_config_dir() / "draxul";
    config_path_ = support / "Plugin Config" / plugin_id_;
    data_path_ = support / "Plugins" / plugin_id_;
    cache_path_ = user_cache_dir() / "draxul" / "plugins" / plugin_id_;
#else
    config_path_ = user_config_dir() / "draxul" / "plugins" / plugin_id_;
    data_path_ = user_data_dir() / "draxul" / "plugins" / plugin_id_;
    cache_path_ = user_cache_dir() / "draxul" / "plugins" / plugin_id_;
#endif
    std::error_code temp_error;
    temporary_path_ = files_->temporary_directory(temp_error)
        / "draxul" / "plugins" / plugin_id_;
    if (temp_error)
        temporary_path_ = data_path_ / "temporary";
}

const std::filesystem::path& PluginStorage::service_path(
    uint32_t path_kind) const
{
    static const std::filesystem::path empty;
    switch (path_kind)
    {
    case DRAXUL_PLUGIN_PATH_RESOURCES:
        return resource_path_;
    case DRAXUL_PLUGIN_PATH_CONFIG:
        return config_path_;
    case DRAXUL_PLUGIN_PATH_DATA:
        return data_path_;
    case DRAXUL_PLUGIN_PATH_CACHE:
        return cache_path_;
    case DRAXUL_PLUGIN_PATH_TEMPORARY:
        return temporary_path_;
    default:
        return empty;
    }
}

int32_t PluginStorage::get_service_path(uint32_t path_kind, char* buffer,
    size_t* in_out_size)
{
    if (!in_out_size)
        return 0;
    switch (path_kind)
    {
    case DRAXUL_PLUGIN_PATH_RESOURCES:
    case DRAXUL_PLUGIN_PATH_CONFIG:
    case DRAXUL_PLUGIN_PATH_DATA:
    case DRAXUL_PLUGIN_PATH_CACHE:
    case DRAXUL_PLUGIN_PATH_TEMPORARY:
        break;
    default:
        return 0;
    }

    const auto& selected = service_path(path_kind);
    if (path_kind != DRAXUL_PLUGIN_PATH_RESOURCES)
    {
        std::error_code error;
        if (!files_->create_directories(selected, error))
            return 0;
    }
    const std::string value = path_utf8(selected);
    const size_t required = value.size() + 1;
    if (!buffer)
    {
        *in_out_size = required;
        return 1;
    }
    if (*in_out_size < required)
    {
        *in_out_size = required;
        return 0;
    }
    std::memcpy(buffer, value.c_str(), required);
    *in_out_size = required;
    return 1;
}

std::filesystem::path PluginStorage::storage_path(uint32_t scope,
    std::string_view key) const
{
    if (!valid_key(key))
        return {};
    if (scope == DRAXUL_PLUGIN_STORAGE_PLUGIN)
        return config_path_ / "state" / (std::string(key) + ".json");
    if (scope == DRAXUL_PLUGIN_STORAGE_PANE && valid_key(pane_id_))
    {
        return config_path_ / "panes" / pane_id_
            / (std::string(key) + ".json");
    }
    return {};
}

uint32_t PluginStorage::read_json(uint32_t scope, const char* key,
    size_t key_length, char* buffer, size_t* in_out_size)
{
    if (!key || !in_out_size
        || !valid_key(std::string_view(key, key_length)))
        return DRAXUL_PLUGIN_STORAGE_INVALID_KEY;
    const auto path = storage_path(scope,
        std::string_view(key, key_length));
    if (path.empty())
        return DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE;
    if (overlay_active_)
    {
        const auto found = overlay_.find(path_utf8(path));
        if (found != overlay_.end())
        {
            if (!found->second)
                return DRAXUL_PLUGIN_STORAGE_NOT_FOUND;
            return copy_value(*found->second, buffer, in_out_size);
        }
    }

    std::error_code error;
    std::uintmax_t size = 0;
    if (!files_->file_size(path, size, error))
    {
        return error == std::errc::no_such_file_or_directory
            ? DRAXUL_PLUGIN_STORAGE_NOT_FOUND
            : DRAXUL_PLUGIN_STORAGE_IO_ERROR;
    }
    if (size > DRAXUL_PLUGIN_MAX_STORAGE_JSON_BYTES)
        return DRAXUL_PLUGIN_STORAGE_TOO_LARGE;
    std::string value;
    if (!files_->read_all(path, value, error))
        return DRAXUL_PLUGIN_STORAGE_IO_ERROR;
    try
    {
        (void)nlohmann::json::parse(value);
    }
    catch (...)
    {
        return DRAXUL_PLUGIN_STORAGE_INVALID_JSON;
    }
    return copy_value(value, buffer, in_out_size);
}

uint32_t PluginStorage::write_json(uint32_t scope, const char* key,
    size_t key_length, const char* json, size_t json_length)
{
    if (!key || !valid_key(std::string_view(key, key_length)))
        return DRAXUL_PLUGIN_STORAGE_INVALID_KEY;
    if (!json)
        return DRAXUL_PLUGIN_STORAGE_INVALID_JSON;
    if (json_length > DRAXUL_PLUGIN_MAX_STORAGE_JSON_BYTES)
        return DRAXUL_PLUGIN_STORAGE_TOO_LARGE;
    try
    {
        (void)nlohmann::json::parse(json, json + json_length);
    }
    catch (...)
    {
        return DRAXUL_PLUGIN_STORAGE_INVALID_JSON;
    }
    const auto path = storage_path(scope,
        std::string_view(key, key_length));
    if (path.empty())
        return DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE;
    if (overlay_active_)
    {
        overlay_[path_utf8(path)] = std::string(json, json_length);
        return DRAXUL_PLUGIN_STORAGE_OK;
    }
    std::error_code error;
    return write_file(path, std::string_view(json, json_length), error)
        ? DRAXUL_PLUGIN_STORAGE_OK
        : DRAXUL_PLUGIN_STORAGE_IO_ERROR;
}

uint32_t PluginStorage::remove(uint32_t scope, const char* key,
    size_t key_length)
{
    if (!key || !valid_key(std::string_view(key, key_length)))
        return DRAXUL_PLUGIN_STORAGE_INVALID_KEY;
    const auto path = storage_path(scope,
        std::string_view(key, key_length));
    if (path.empty())
        return DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE;
    if (overlay_active_)
    {
        overlay_[path_utf8(path)] = std::nullopt;
        return DRAXUL_PLUGIN_STORAGE_OK;
    }
    std::error_code error;
    const bool removed = files_->remove(path, error);
    if (error)
        return DRAXUL_PLUGIN_STORAGE_IO_ERROR;
    return removed ? DRAXUL_PLUGIN_STORAGE_OK
                   : DRAXUL_PLUGIN_STORAGE_NOT_FOUND;
}

void PluginStorage::begin_overlay()
{
    overlay_.clear();
    overlay_active_ = true;
}

bool PluginStorage::commit_overlay(std::string& error)
{
    overlay_active_ = false;
    for (const auto& [name, value] : overlay_)
    {
        const std::filesystem::path path = path_from_utf8(name);
        std::error_code io_error;
        if (value)
        {
            if (!write_file(path, *value, io_error))
            {
                error = "Plugin reload activated, but a storage write failed: "
                    + io_error.message();
                overlay_.clear();
                return false;
            }
        }
        else
        {
            (void)files_->remove(path, io_error);
            if (io_error)
            {
                error = "Plugin reload activated, but a storage remove failed: "
                    + io_error.message();
                overlay_.clear();
                return false;
            }
        }
    }
    overlay_.clear();
    return true;
}

void PluginStorage::discard_overlay()
{
    overlay_active_ = false;
    overlay_.clear();
}

bool PluginStorage::valid_key(std::string_view key)
{
    if (key.empty() || key.size() > 128 || key == "." || key == "..")
        return false;
    return std::all_of(key.begin(), key.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
    });
}

uint32_t PluginStorage::copy_value(std::string_view value, char* buffer,
    size_t* in_out_size)
{
    const size_t required = value.size() + 1;
    if (!buffer)
    {
        *in_out_size = required;
        return DRAXUL_PLUGIN_STORAGE_OK;
    }
    if (*in_out_size < required)
    {
        *in_out_size = required;
        return DRAXUL_PLUGIN_STORAGE_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, value.data(), value.size());
    buffer[value.size()] = '\0';
    *in_out_size = required;
    return DRAXUL_PLUGIN_STORAGE_OK;
}

bool PluginStorage::write_file(const std::filesystem::path& path,
    std::string_view json, std::error_code& error)
{
    if (!files_->create_directories(path.parent_path(), error))
        return false;
    static std::atomic<uint64_t> serial{ 0 };
    const auto temporary = path.parent_path()
        / (path.filename().string() + ".tmp-"
            + std::to_string(serial.fetch_add(1)));

    auto output = files_->open_output(temporary, error);
    if (!output)
    {
        const auto primary_error = error ? error : io_error();
        cleanup_temporary(temporary, primary_error, error);
        return false;
    }
    if (!output->write(json, error))
    {
        const auto primary_error = error ? error : io_error();
        output.reset();
        cleanup_temporary(temporary, primary_error, error);
        return false;
    }
    if (!output->flush(error))
    {
        const auto primary_error = error ? error : io_error();
        output.reset();
        cleanup_temporary(temporary, primary_error, error);
        return false;
    }
    output.reset();

    if (!files_->replace(temporary, path, error))
    {
        const auto primary_error = error ? error : io_error();
        cleanup_temporary(temporary, primary_error, error);
        return false;
    }
    return true;
}

void PluginStorage::cleanup_temporary(
    const std::filesystem::path& temporary,
    const std::error_code& primary_error,
    std::error_code& reported_error)
{
    std::error_code cleanup_error;
    (void)files_->remove(temporary, cleanup_error);
    reported_error = primary_error;
}

} // namespace draxul
