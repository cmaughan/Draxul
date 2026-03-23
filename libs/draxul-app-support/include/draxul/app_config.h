#pragma once

// Facade header -- includes the full app-config surface for backward compatibility.
// New code should prefer including the specific header it needs:
//   - <draxul/app_config_types.h>  -- struct definitions only (no TOML, no I/O)
//   - <draxul/keybinding_parser.h> -- chord parsing and keybinding matching
//   - <draxul/app_config_io.h>     -- I/O (load/save) and TOML parse/serialize

#include <draxul/app_config_io.h>
