#include "control_metadata.h"

#include <draxul/control_plane.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <mutex>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace draxul::control_detail
{

bool read_metadata(const std::filesystem::path& path,
    std::string& endpoint, std::string& token, std::string& error)
{
    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error || size == 0 || size > 16 * 1024)
    {
        error = "No usable control endpoint metadata for this Session.";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    const auto metadata = nlohmann::json::parse(bytes, nullptr, false);
    if (metadata.is_discarded() || !metadata.is_object()
        || metadata.value("version", 0) != kControlProtocolVersion
        || !metadata.contains("endpoint") || !metadata["endpoint"].is_string()
        || !metadata.contains("token") || !metadata["token"].is_string())
    {
        error = "Control endpoint metadata is invalid.";
        return false;
    }
    endpoint = metadata["endpoint"].get<std::string>();
    token = metadata["token"].get<std::string>();
    if (endpoint.empty() || token.size() != 64)
    {
        error = "Control endpoint metadata is invalid.";
        return false;
    }
    return true;
}

namespace
{

struct CachedMetadata
{
    std::string endpoint;
    std::string token;
    std::chrono::steady_clock::time_point expires_at;
};

std::mutex& metadata_cache_mutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, CachedMetadata>& metadata_cache()
{
    static std::unordered_map<std::string, CachedMetadata> cache;
    return cache;
}

} // namespace

bool read_cached_metadata(const std::filesystem::path& path,
    std::string& endpoint, std::string& token, std::string& error)
{
    const std::string key = path.lexically_normal().generic_string();
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard guard(metadata_cache_mutex());
        const auto found = metadata_cache().find(key);
        if (found != metadata_cache().end()
            && found->second.expires_at > now)
        {
            endpoint = found->second.endpoint;
            token = found->second.token;
            return true;
        }
    }
    if (!read_metadata(path, endpoint, token, error))
        return false;
    {
        std::lock_guard guard(metadata_cache_mutex());
        metadata_cache()[key] = {
            .endpoint = endpoint,
            .token = token,
            .expires_at = now + std::chrono::seconds(1),
        };
    }
    return true;
}

void invalidate_cached_metadata(const std::filesystem::path& path)
{
    const std::string key = path.lexically_normal().generic_string();
    std::lock_guard guard(metadata_cache_mutex());
    metadata_cache().erase(key);
}

} // namespace draxul::control_detail
