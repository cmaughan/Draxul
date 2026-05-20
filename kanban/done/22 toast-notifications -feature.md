# Feature: Non-Blocking Toast Notifications for Recoverable Events

**Type:** feature
**Priority:** 22
**Source:** Gemini review

## Overview

Several recoverable error conditions in Draxul currently produce a log entry but no visible user feedback:

- Clipboard read/write failure (e.g. Wayland clipboard not available).
- Spawn failure for a secondary host (shell, agent).
- Font load warning (e.g. bold variant not found, using regular as fallback).
- Config key warning (unknown key in `config.toml`).
- Atlas overflow recovery (glyph evicted).

These silently fail or log to stderr which the user never sees. A small, auto-dismissing toast in the corner of the screen would make these visible without interrupting the workflow.

## Implementation plan

### Phase 1: Toast infrastructure

- [x] Create `ToastHost` (IHost in `app/`) with grid-cell rendering via `gui::render_toasts()`.
- [x] Create `toast_renderer.h/cpp` in `libs/draxul-gui/` producing `CellUpdate` vectors (same approach as command palette).
- [x] Toasts auto-dismiss after configurable duration with fade-out animation.
- [x] Thread-safe `push()` method for cross-thread toast delivery.
- [x] Add `push_toast()` to `IHostCallbacks` so any host can trigger toasts.

### Phase 2: Wire up existing silent failures

- [x] Clipboard write failure → `callbacks().push_toast(1, "Clipboard write failed")`.
- [x] Font variant not found → toast warning.
- [x] Config unknown key → toast info.
- [x] Spawn failure for secondary host → toast error.

### Phase 3: Config

- [x] Add `enable_toast_notifications = true` to `config.toml` (allow disabling).
- [x] Add `toast_duration_s = 4.0` to `config.toml`.

### Phase 4: Documentation

- [x] Document in `docs/features.md`.
- [x] Update `CLAUDE.md` config notes section.

## Acceptance criteria

- [x] A clipboard write failure produces a visible toast in the corner of the app window.
- [x] The toast auto-dismisses after the configured duration.
- [x] Multiple toasts stack vertically without overlapping.
- [x] `enable_toast_notifications = false` suppresses all toasts.
- [x] No crash if a toast is pushed before the renderer is initialised (queued via `pending_init_toasts_` and replayed once `ToastHost` is created).

## Interdependencies

- None — standalone feature using grid-cell rendering (same as command palette).

## Follow-up notes

- Added a `test_toast` GUI action that cycles info → warn → error so the palette can exercise the overlay.
- Fixed two latent bugs found while testing:
  - `ToastHost::pump()` reset its tick baseline when transitioning from empty → active so the first toast no longer gets `dt = idle_since_init` subtracted from its lifetime.
  - The full-window toast grid handle now uses a transparent default background; previously the unset cells outside the toast region painted opaque black over the rest of the window.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
