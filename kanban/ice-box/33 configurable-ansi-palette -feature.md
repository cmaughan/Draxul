# Configurable ANSI Colour Palette

**Type:** feature
**Priority:** 33
**Raised by:** Claude

## Summary

The ANSI 16-colour palette in `libs/draxul-host/src/terminal_host_base.cpp` is hardcoded in `ansi_color()`. Add a `[terminal] ansi_colors = [...]` array to `config.toml` so users can customise the base palette to match their preferred colour scheme without modifying source code.

## Background

The 16-colour ANSI palette (colours 0–15) is the primary tool by which terminal applications express theme-aware colours. Virtually every terminal emulator allows users to configure this palette. Draxul's hardcoded palette forces users who prefer a specific colour scheme (Solarized, Gruvbox, Nord, Catppuccin, etc.) to accept the default colours or wait for neovim's own colour settings to override them. Exposing the palette in config.toml is a low-risk, high-value feature that enables full theme compatibility.

## Implementation Plan

### Files to modify
- `libs/draxul-host/src/terminal_host_base.cpp` — modify `ansi_color()` to use a configurable palette array instead of hardcoded values; receive the palette via constructor or setter
- `libs/draxul-host/src/terminal_host_base.h` — add a palette member (array of 16 `Color` values)
- `libs/draxul-app-support/` — extend `AppConfig` struct with `ansi_colors` field (array of 16 `Color`)
- Config parsing code — parse `[terminal] ansi_colors` from `config.toml` as an array of 16 hex colour strings or RGB triples

### Steps
- [ ] Add `std::array<Color, 16> ansi_palette` to `AppConfig` with the existing hardcoded values as defaults
- [ ] Parse `[terminal] ansi_colors` from `config.toml`: accept an array of 16 strings in `#RRGGBB` format; log a warning and use the default if the array is missing or the wrong length
- [ ] Pass the palette to `TerminalHostBase` (via constructor argument or `set_palette()` method)
- [ ] Update `ansi_color()` to index into the stored palette rather than a hardcoded switch
- [ ] Document the config option in the `config.toml` example or comments
- [ ] Test: load a custom palette from config, verify `ansi_color(1)` returns the configured value

## Depends On
- None (can be done before `37 hierarchical-config -feature.md`, but should be reviewed alongside it)

## Blocks
- None

## Notes
Colours 0–7 are the standard ANSI colours; colours 8–15 are the bright variants. The 256-colour and 24-bit colour paths are separate and do not need to be configurable (they are exact values sent by applications). Only the 16-colour base palette is relevant here.

> Work item produced by: claude-sonnet-4-6
