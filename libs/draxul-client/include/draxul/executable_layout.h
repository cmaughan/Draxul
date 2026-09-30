#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace draxul
{
std::filesystem::path resolve_client_executable(const std::vector<std::string>& args);
std::filesystem::path default_server_log_path(const std::filesystem::path& runtime_directory);
// Pure bundle layout; available on all platforms for cross-platform policy tests.
std::filesystem::path macos_server_helper_executable(const std::filesystem::path& client_executable);
std::filesystem::path macos_client_executable(const std::filesystem::path& current_executable);

#ifdef _WIN32
// Windows locks a running executable against linker replacement. The shared
// server therefore runs from a sibling copy while draxul.exe remains free for
// normal UI rebuilds. These helpers also let the server's tray surface route
// UI launches back to the client executable.
std::filesystem::path windows_server_helper_executable(
    const std::filesystem::path& client_executable);
std::filesystem::path windows_client_executable(
    const std::filesystem::path& current_executable);
#endif

} // namespace draxul
