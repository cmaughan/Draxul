# Draxul configuration keys

<!-- Generated from the config schema (libs/draxul-config/src/config_schema.cpp).
     Do not edit by hand. Regenerate with:
       DRAXUL_REGEN_CONFIG_DOCS=1 ./build/tests/draxul-tests "[config][docs]" -->

User settings live in `config.toml`. Every key below is owned by the core
config layer; optional product modules own their own top-level tables and are
preserved verbatim.

## Top-level keys

| Key | Type | Default | Range | Description |
|-----|------|---------|-------|-------------|
| `window_width` | integer | `1280` | 640 .. 3840 | Initial window width in pixels. Out-of-range values fall back to the default. |
| `window_height` | integer | `800` | 400 .. 2160 | Initial window height in pixels. Out-of-range values fall back to the default. |
| `font_size` | float | `11` | 6 .. 72 | Terminal/grid font point size. Clamped into range. |
| `atlas_size` | integer | `2048` | 1024 .. 8192 (power of two) | Glyph atlas edge length in texels. Clamped, then rounded down to a power of two. |
| `scrollback_lines` | integer | `10000` | 1 .. 1000000 | Terminal scrollback buffer size in lines. |
| `enable_ligatures` | boolean | `true` |  | Combine eligible two-cell programming ligatures during shaping. |
| `smooth_scroll` | boolean | `true` |  | Enable trackpad momentum-style scroll accumulation. |
| `enable_toast_notifications` | boolean | `true` |  | Master enable for the corner toast overlay. |
| `show_pane_status` | boolean | `true` |  | Show the per-pane status bar (host kind | dims | cwd) below each pane. |
| `chord_timeout_ms` | integer | `1500` | >= 100 | How long a chord prefix stays armed while waiting for the second key. |
| `chord_indicator_fade_ms` | integer | `2500` | >= 100 | How long the top-bar chord indicator remains visible while fading out. |
| `scroll_speed` | float | `1` | 0.1 .. 10 | Multiplier applied to raw scroll deltas before smooth-scroll accumulation. |
| `palette_bg_alpha` | float | `0.9` | 0 .. 1 | Command palette background opacity. |
| `focus_border_width` | float | `3` | 1 .. 10 | Pane focus indicator thickness in pixels. |
| `toast_duration_s` | float | `4` | 0.5 .. 60 | How long each toast remains visible before fading out, in seconds. |
| `font_path` | string | `(empty)` |  | Path to the primary (regular) font file. Empty uses the bundled font. |
| `bold_font_path` | string | `(empty)` |  | Path to the bold font file. Empty synthesizes bold from the regular face. |
| `italic_font_path` | string | `(empty)` |  | Path to the italic font file. Empty synthesizes italic from the regular face. |
| `bold_italic_font_path` | string | `(empty)` |  | Path to the bold-italic font file. |
| `weather_location` | string | `(empty)` |  | City name or lat,lon for the weather pill. Empty disables the pill. |
| `fallback_paths` | array of strings | `(empty)` |  | Ordered fallback font files used for glyphs the primary font lacks. |

## [markdown]

Markdown viewer layout and font options.

| Key | Type | Default | Range | Description |
|-----|------|---------|-------|-------------|
| `font_size` | float | `11` | 6 .. 72 | Markdown viewer font point size. Defaults to the global font_size when unset. |
| `margin_columns` | float | `2` | 0 .. 24 | Markdown viewer horizontal margin, in text columns. |

## [chrome]

UI chrome color theme.

| Key | Type | Default | Range | Description |
|-----|------|---------|-------|-------------|
| `tab_bar_bg` | color (#RRGGBB) | `#181825` |  | Tab bar background color. |
| `tab_active_fg` | color (#RRGGBB) | `#f5e0dc` |  | Active tab label color. |
| `tab_inactive_fg` | color (#RRGGBB) | `#cdd6f4` |  | Inactive tab label color. |
| `tab_active_bg` | color (#RRGGBB) | `#b93c3c` |  | Active tab background color. |
| `tab_inactive_bg` | color (#RRGGBB) | `#45475a` |  | Inactive tab background color. |
| `tab_editing_bg` | color (#RRGGBB) | `#8c90af` |  | Tab background while its name is being edited. |
| `divider` | color (#RRGGBB) | `#78788c` |  | Pane divider color. |
| `focus_border` | color (#RRGGBB) | `#b93c3c` |  | Focused pane border color. |
| `status_bar_bg` | color (#RRGGBB) | `#45475a` |  | Status bar background color. |
| `status_bar_fg` | color (#RRGGBB) | `#cdd6f4` |  | Status bar text color. |
| `status_focused_accent_bg` | color (#RRGGBB) | `#3ca55f` |  | Status bar accent background for the focused pane. |
| `status_inactive_accent_bg` | color (#RRGGBB) | `#6e738c` |  | Status bar accent background for inactive panes. |
| `status_editing_bg` | color (#RRGGBB) | `#8c90af` |  | Status bar background while editing. |
| `resource_pill_bg` | color (#RRGGBB) | `#f9e2af` |  | Resource pill background color. |
| `resource_pill_fg` | color (#RRGGBB) | `#1a1a1f` |  | Resource pill text color. |
| `resource_pill_warn_bg` | color (#RRGGBB) | `#f5c282` |  | Resource pill background at the warning threshold. |
| `resource_pill_hot_bg` | color (#RRGGBB) | `#f45656` |  | Resource pill background at the hot threshold. |
| `chord_pill_bg` | color (#RRGGBB) | `#45475a` |  | Chord indicator pill background color. |
| `weather_pill_bg` | color (#RRGGBB) | `#474d61` |  | Weather pill background color. |
| `editing_outline` | color (#RRGGBB) | `#ffffff` |  | Outline color drawn around an in-place editing field. |

## [terminal]

Terminal/shell pane appearance and behavior.

| Key | Type | Default | Range | Description |
|-----|------|---------|-------|-------------|
| `fg` | color (#RRGGBB) | `(empty)` |  | Terminal foreground color (#RRGGBB). Empty uses the host default. |
| `bg` | color (#RRGGBB) | `(empty)` |  | Terminal background color (#RRGGBB). Empty uses the host default. |
| `selection_max_cells` | integer | `65536` | 256 .. 1048576 | Maximum grid cells a single selection may span before truncation. |
| `copy_on_select` | boolean | `true` |  | Copy the selection to the clipboard when a click-drag completes. |
| `paste_confirm_lines` | integer | `5` | 0 .. 100000 | Minimum line count in a paste before the confirmation prompt appears (0 disables). |
| `url_detection` | boolean | `true` |  | Detect URLs in terminal output and make them clickable. |
| `enable_osc8_hyperlinks` | boolean | `true` |  | Honor OSC 8 hyperlink escape sequences. |
| `enable_shell_integration_marks` | boolean | `true` |  | Honor shell-integration prompt marks (OSC 133). |

## [keybindings]

GUI-only keyboard shortcuts (action = combo).

Compound section: `action = "combo"` pairs. See the GUI action
registry (`libs/draxul-config/include/draxul/gui_actions.h`) for the
full list of action names and `docs/features.md` for combo syntax.
