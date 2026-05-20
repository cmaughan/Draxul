# Hierarchical Config Loading

**Type:** feature
**Priority:** 37
**Raised by:** Gemini

## Summary

Replace the current single-file `config.toml` loading with a three-level hierarchical config system: system-wide defaults → user `~/.config/draxul/config.toml` → project-local `./config.toml`, merging in that order so more specific files override less specific ones. This enables per-project terminal configuration without losing user defaults.

## Background

The current config system loads a single `config.toml` from the working directory (or a fixed path). This means users cannot have a global default configuration that applies everywhere while also having per-project overrides (e.g., a different font size for a high-DPI workstation, or a different ANSI palette for a specific project's colour scheme). Hierarchical config is a standard pattern in developer tools (git, editorconfig, eslint) and is expected by power users.

The three levels are:
1. **System-wide defaults** — compiled-in defaults in `AppConfig` (no file required)
2. **User config** — `~/.config/draxul/config.toml` on macOS/Linux, `%APPDATA%\draxul\config.toml` on Windows
3. **Project-local config** — `./config.toml` in the current working directory (existing behaviour)

## Implementation Plan

### Files to modify
- `libs/draxul-app-support/` — update config loading logic to implement the three-level merge
- Platform path resolution:
  - macOS/Linux: use `$XDG_CONFIG_HOME/draxul/config.toml` (defaulting to `~/.config/draxul/config.toml`)
  - Windows: use `%APPDATA%\draxul\config.toml`
- `app/app.cpp` — pass the merged config to all subsystems as before; no change to call sites

### Steps
- [ ] Define a `load_config(std::filesystem::path optional_local_path)` function that:
  - Starts with compiled-in `AppConfig` defaults
  - Loads and merges the user config file if it exists (silently skip if absent)
  - Loads and merges the project-local config file if it exists (silently skip if absent)
  - Returns the merged `AppConfig`
- [ ] Implement platform path resolution for the user config directory using `std::filesystem` and platform environment variables (`HOME`, `XDG_CONFIG_HOME`, `APPDATA`)
- [ ] Implement "merge" semantics: any key present in a later file overrides the value from an earlier file; absent keys keep their value from earlier levels
- [ ] Handle parse errors in any config file: log a warning with the file path and line number, continue with values loaded so far (do not abort startup)
- [ ] Document the three-level precedence in `config.toml` comments
- [ ] Test: user config sets `font_size = 14`; project config sets `font_size = 18`; result is 18
- [ ] Test: user config sets `font_size = 14`; project config absent; result is 14
- [ ] Test: neither config file exists; result is compiled-in defaults

## Depends On
- None (but new config keys from work items `33`, `34`, `35` should be placed in the right table structure from the start)

## Blocks
- `36 window-state-persistence -feature.md` — if per-project window state is desired, hierarchical config must exist first

## Notes
TOML does not have a built-in merge operator. The merge logic must be implemented at the `AppConfig` struct level: load each file into a temporary `AppConfig`, then copy only the explicitly-set fields over the base. This requires distinguishing "explicitly set to X" from "absent, use default" — a simple approach is to use `std::optional<T>` for all config fields during parsing, then apply a three-way merge before resolving optionals to their defaults.

> Work item produced by: claude-sonnet-4-6
