#include "control_metadata.h"

#include <draxul/control_plane.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <mutex>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <utility>

namespace draxul::control_detail
{

namespace
{

TransportError metadata_error(TransportStage stage, uint32_t native_code,
    std::string message)
{
    return {
        .stage = stage,
#ifdef _WIN32
        .domain = native_code == 0 ? NativeDomain::None : NativeDomain::Win32,
#else
        .domain = native_code == 0 ? NativeDomain::None : NativeDomain::Posix,
#endif
        .native_code = native_code,
        .classification = FailureClass::EndpointUnavailable,
        .message = std::move(message),
    };
}

} // namespace

TransportStatus read_metadata(const std::filesystem::path& path,
    std::string& endpoint, std::string& token, std::string& error)
{
    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error || size == 0 || size > 16 * 1024)
    {
        error = "No usable control endpoint metadata for this Session.";
        return TransportStatus::failure(metadata_error(
            TransportStage::MetadataRead,
            size_error ? static_cast<uint32_t>(size_error.value()) : 0,
            error));
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        error = "Unable to read control endpoint metadata.";
        return TransportStatus::failure(metadata_error(
            TransportStage::MetadataRead, 0, error));
    }
    std::string bytes((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    const auto metadata = nlohmann::json::parse(bytes, nullptr, false);
    if (metadata.is_discarded() || !metadata.is_object()
        || metadata.value("version", 0) != kControlProtocolVersion
        || !metadata.contains("endpoint") || !metadata["endpoint"].is_string()
        || !metadata.contains("token") || !metadata["token"].is_string())
    {
        error = "Control endpoint metadata is invalid.";
        return TransportStatus::failure(metadata_error(
            TransportStage::MetadataParse, 0, error));
    }
    endpoint = metadata["endpoint"].get<std::string>();
    token = metadata["token"].get<std::string>();
    if (endpoint.empty() || token.size() != 64)
    {
        error = "Control endpoint metadata is invalid.";
        return TransportStatus::failure(metadata_error(
            TransportStage::MetadataParse, 0, error));
    }
    return TransportStatus::success();
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

TransportStatus read_cached_metadata(const std::filesystem::path& path,
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
            return TransportStatus::success();
        }
    }
    auto status = read_metadata(path, endpoint, token, error);
    if (!status.ok)
        return status;
    {
        std::lock_guard guard(metadata_cache_mutex());
        metadata_cache()[key] = {
            .endpoint = endpoint,
            .token = token,
            .expires_at = now + std::chrono::seconds(1),
        };
    }
    return TransportStatus::success();
}

void invalidate_cached_metadata(const std::filesystem::path& path)
{
    const std::string key = path.lexically_normal().generic_string();
    std::lock_guard guard(metadata_cache_mutex());
    metadata_cache().erase(key);
}

} // namespace draxul::control_detail
