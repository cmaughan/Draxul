#pragma once

#include <filesystem>
#include <string>

namespace draxul
{

using MacosServerApplicationCallback = void (*)(void* userdata);

// The server runs from a nested LSUIElement helper bundle with its own bundle
// identifier. Keep the activation policy defensive for direct development
// launches, and route native reopen and quit events to the status surface.
bool configure_macos_server_status_application(
    MacosServerApplicationCallback reopen_callback,
    MacosServerApplicationCallback quit_callback,
    void* userdata, std::string& error);

std::filesystem::path macos_server_helper_executable(
    const std::filesystem::path& client_executable);
std::filesystem::path macos_client_executable(
    const std::filesystem::path& current_executable);

} // namespace draxul
