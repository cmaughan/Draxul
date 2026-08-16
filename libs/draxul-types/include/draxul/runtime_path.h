#pragma once

#include <filesystem>

namespace draxul
{

std::filesystem::path executable_directory();
std::filesystem::path bundled_asset_path(const std::filesystem::path& relative_path);

// Platform base directories for per-user files. No application component is
// appended — callers compose their own subtree (e.g. `/ "draxul"`).
// Environment variables whose value is empty are treated as unset. When every
// candidate is missing, the current directory "." is returned and a WARN is
// logged.
//
//   user_config_dir: %APPDATA% | ~/Library/Application Support
//                    | $XDG_CONFIG_HOME else ~/.config
//   user_data_dir:   %APPDATA% | ~/Library/Application Support
//                    | $XDG_DATA_HOME else ~/.local/share
//   user_cache_dir:  %LOCALAPPDATA% else %APPDATA% | ~/Library/Caches
//                    | $XDG_CACHE_HOME else ~/.cache
std::filesystem::path user_config_dir();
std::filesystem::path user_data_dir();
std::filesystem::path user_cache_dir();

} // namespace draxul
