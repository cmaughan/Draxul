# 20 smooth-scrolling-tuning -feature

**Priority:** LOW (feature, quality of life)
**Type:** Feature
**Raised by:** GPT ("Smooth scrolling exists but lacks tuning and visibility")
**Model:** claude-sonnet-4-6

---

## Problem

Smooth scrolling is implemented but not user-tunable. Users cannot configure scroll speed, easing curve, or disable it entirely. There is no indication in `config.toml` documentation that the feature exists or how to adjust it. GPT notes this as a rough edge in the user-facing experience.

---

## Implementation Plan

- [x] Read the current smooth scrolling implementation (likely in `app/app.cpp`, `GridHostBase`, or a dedicated scroll animator) to understand what parameters are already in play.
- [x] Read `libs/draxul-app-support/include/draxul/app_config.h` to see what scroll-related config fields exist.
- [x] Expose `scroll_speed = 1.0` (top-level, matching `smooth_scroll` pattern) in `AppConfig` and `config.toml`:
  - `smooth_scroll = true/false` already existed
  - `scroll_speed = 1.0` (multiplier, 0.5 = slower, 2.0 = faster) — added
  - `scroll_easing` — deferred/out of scope for this implementation
- [x] Implement the config reading and apply `scroll_speed` multiplier in `InputDispatcher::on_mouse_wheel_event`.
- [x] Add validation: `scroll_speed` clamped to (0.1, 10.0]; logs WARN and falls back to 1.0 if out of range.
- [x] Update `CLAUDE.md` config notes section with the new keys.
- [x] Build succeeded (`draxul` and `draxul-tests` targets).
- [ ] Manually verify: set `smooth_scroll = false` → scrolling snaps; set high `scroll_speed` → scrolling finishes faster.

---

## Acceptance

- `smooth_scroll = false` in `config.toml` disables animation and snaps scroll immediately.
- `scroll_speed` multiplier perceptibly changes scroll duration.
- Invalid `scroll_easing` value logs a WARN and falls back to default.
- All existing scroll tests still pass.

---

## Interdependencies

- No upstream dependencies from this wave.
- Does not interact with the icebox items.
