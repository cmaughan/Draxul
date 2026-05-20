# 19 per-monitor-dpi-font-scaling -feature

**Priority:** LOW (feature, quality of life)
**Type:** Feature
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

When the user drags the Draxul window from a 1x DPI monitor to a 2x (HiDPI/Retina) monitor or vice versa, font size is not automatically adjusted to maintain the configured physical pixel size. The user must manually resize the font or restart the application. The `on_display_scale_changed()` path exists but may not fully handle the automatic rescaling case — the `dpi-scaling-test` is complete so some of this infrastructure is in place.

Note: This is different from the completed DPI hotplug bug fix. This item is about auto-adjusting the *logical font size* to compensate for a display scale change so the rendered text appears at the same physical size.

---

## Implementation Plan

- [ ] Read `app/app.cpp` `on_display_scale_changed()` to understand what currently happens.
- [ ] Read `AppConfig` and `TextService` to understand how font size is stored and applied.
- [ ] Determine the desired behaviour:
  - When display scale changes from 1.0 to 2.0, the logical font size should halve (so the physical pixel size is unchanged).
  - Or: keep the same logical font size but signal Neovim with the new DPI so it adjusts line heights.
  - Document which approach is chosen and why.
- [ ] Implement the scale-compensated font size adjustment in `on_display_scale_changed()`.
- [ ] Ensure the adjusted size is not persisted to `config.toml` (the config should store the user-configured size, not the DPI-adjusted effective size).
- [ ] Add a test or extend the existing DPI scaling test to cover the auto-adjust behaviour.
- [ ] Build and run smoke test. Manually verify by dragging window between monitors with different DPIs.

---

## Acceptance

- Dragging the window to a 2x monitor: text renders at the same physical size as on the 1x monitor.
- `config.toml` font_size is unchanged after the scale change.
- No glyph atlas corruption or use-after-free during the transition.

---

## Interdependencies

- Clean config-layer plumbing benefits from **15-refactor** (appconfig-sdl-decoupling) being done first.
- No other dependencies.
