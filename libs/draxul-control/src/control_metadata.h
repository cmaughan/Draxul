#pragma once

#include <filesystem>
#include <string>

namespace draxul::control_detail
{

bool read_metadata(const std::filesystem::path& path,
    std::string& endpoint, std::string& token, std::string& error);
bool read_cached_metadata(const std::filesystem::path& path,
    std::string& endpoint, std::string& token, std::string& error);
void invalidate_cached_metadata(const std::filesystem::path& path);

} // namespace draxul::control_detail
