# WI 137: ChromeHost Hardcoded Theme Colors

**Type:** feature
**Priority:** 03
**Raised by:** Gemini (`review-latest.gemini.md`, "Worst Features #3")
**Depends on:** WI 128 (`workspace-tab-name-editing`) for UI consistency — coordinate, but not a hard blocker

---

## Problem

`ChromeHost` hard-codes Catppuccin Mocha palette values for tab bar backgrounds, active/inactive tab indicators, resource pills, divider lines, and the pane status bar. These are embedded as hex color literals in `app/chrome_host.cpp`.

Users cannot change the UI chrome colors without recompiling. This is inconsistent with the configurable terminal ANSI palette (WI 33 in icebox) and the general philosophy of `config.toml` being the single customisation point.

Specific hard-coded elements (from Gemini's review):
- Tab bar background
- Active tab highlight color
- Inactive tab text color
- Resource pill fill and border
- Pane status bar background and text
- Divider line color

---

## Goal

Expose a `[chrome]` (or `[theme.chrome]`) section in `config.toml` that allows users to override individual UI chrome colors. Sensible Catppuccin Mocha defaults are kept so existing users see no change without opting in.

---

## Implementation Plan

### 1. Define a `ChromeTheme` struct in `draxul-config`

```cpp
// libs/draxul-config/include/draxul/chrome_theme.h
struct ChromeTheme {
    Color tab_bar_bg          = Color{0x1e, 0x1e, 0x2e, 0xff}; // Catppuccin Mocha base
    Color tab_active_fg       = Color{0xcd, 0xd6, 0xf4, 0xff}; // text
    Color tab_inactive_fg     = Color{0x58, 0x5b, 0x70, 0xff}; // surface2
    Color tab_active_bg       = Color{0x31, 0x32, 0x44, 0xff}; // surface0
    Color tab_inactive_bg     = Color{0x1e, 0x1e, 0x2e, 0xff}; // base
    Color divider             = Color{0x45, 0x47, 0x5a, 0xff}; // surface1
    Color status_bar_bg       = Color{0x18, 0x18, 0x25, 0xff}; // mantle
    Color status_bar_fg       = Color{0x58, 0x5b, 0x70, 0xff}; // surface2
    Color resource_pill_bg    = Color{0x31, 0x32, 0x44, 0xff}; // surface0
    Color resource_pill_fg    = Color{0xcd, 0xd6, 0xf4, 0xff}; // text
};
```

### 2. Add `[chrome]` parsing to `AppConfig`

In `libs/draxul-config/src/app_config.cpp`, add a section reader that populates `ChromeTheme` from `config.toml`. Use the same hex-string or RGB-triplet format already used by `[terminal]` fg/bg colors.

```toml
[chrome]
tab_bar_bg       = "#1e1e2e"
tab_active_fg    = "#cdd6f4"
# ... etc.
```

Unknown keys should produce a WARN (consistent with existing config validation).

### 3. Thread `ChromeTheme` into `ChromeHost`

- `ChromeHost::Deps` should include a `const ChromeTheme&` or a copy of `ChromeTheme`.
- Replace all literal color values in `chrome_host.cpp` with references to the theme struct.
- Do **not** add a runtime theme-change path in this WI — config reloads already trigger a redraw; a full reinit of ChromeHost is acceptable on config reload.

### 4. Update `docs/features.md`

Add the `[chrome]` config section and all its keys to the configuration reference section.

### 5. Update Config Schema Docs / Warnings

If the project has a config validation list, add all new keys so unknown-key warnings work correctly.

---

## Files Likely Involved

- `libs/draxul-config/include/draxul/chrome_theme.h` *(new file)*
- `libs/draxul-config/include/draxul/app_config.h` (add `ChromeTheme chrome_theme` field)
- `libs/draxul-config/src/app_config.cpp` (add `[chrome]` section parser)
- `app/chrome_host.h` / `app/chrome_host.cpp` (replace literals with `deps.chrome_theme.*`)
- `docs/features.md` (document new config keys)

---

## Out of Scope for This WI

- Runtime theme switching without config reload.
- Named themes (Solarized, Dracula, etc.) — that is a separate feature layered on top.
- Terminal ANSI palette — covered by WI 33 (`configurable-ansi-palette`) in icebox.
- NanoVG-specific color handling — do not refactor the NanoVG drawing path, only replace literal values.

---

## Acceptance Criteria

- [ ] A `[chrome]` table in `config.toml` with any subset of keys overrides ChromeHost colors.
- [ ] Omitting `[chrome]` entirely preserves current Catppuccin Mocha defaults.
- [ ] Unknown keys under `[chrome]` produce a WARN log.
- [ ] `docs/features.md` documents all new config keys.
- [ ] Build and smoke test pass on macOS: `cmake --build build --target draxul draxul-tests && py do.py smoke`.

---

*Authored by `claude-sonnet-4-6`*
