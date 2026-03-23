#pragma once

// This header re-exports app_config_types.h and keybinding_parser.h and declares the
// I/O and parse functions implemented in app_config_io.cpp. All definitions (AppConfig
// constructor, parse, serialize, load, save, apply_overrides) live in that TU.
//
// Most consumers only need app_config_types.h (for struct layouts) or
// keybinding_parser.h (for chord/match helpers). Include this header when you need
// the full I/O surface (file load/save, TOML parse/serialize, override merging).

#include <draxul/app_config_types.h>
#include <draxul/keybinding_parser.h>
