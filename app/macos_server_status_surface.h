#pragma once

#include <string>

namespace draxul
{

// The server shares the client app bundle, whose normal activation policy
// gives it a Dock icon. Switch only the --server process to an accessory
// before SDL initializes its tray support.
bool configure_macos_server_status_application(std::string& error);

} // namespace draxul
