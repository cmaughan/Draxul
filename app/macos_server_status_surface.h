#pragma once

#include <string>

namespace draxul
{

using MacosServerApplicationCallback = void (*)(void* userdata);

// The server shares the client app bundle, whose normal activation policy
// gives it a Dock icon. Switch only the --server process to an accessory
// before SDL initializes its tray support. Finder routes later bundle-open
// requests to this process because it has the same bundle identifier as the
// client, so forward reopen and quit requests to the server status surface.
bool configure_macos_server_status_application(
    MacosServerApplicationCallback reopen_callback,
    MacosServerApplicationCallback quit_callback,
    void* userdata, std::string& error);

} // namespace draxul
