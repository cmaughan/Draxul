#pragma once

#include "control_transport.h"

#include <filesystem>
#include <string>

namespace draxul::control_detail
{

TransportStatus read_metadata(const std::filesystem::path& path,
    std::string& endpoint, std::string& token, std::string& error);
TransportStatus read_cached_metadata(const std::filesystem::path& path,
    std::string& endpoint, std::string& token, std::string& error);
void invalidate_cached_metadata(const std::filesystem::path& path);

} // namespace draxul::control_detail
