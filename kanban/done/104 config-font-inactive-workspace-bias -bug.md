# WI 104 — config-font-inactive-workspace-bias

**Type:** bug
**Priority:** 2 (MEDIUM-HIGH — stale font and config state in inactive workspaces after reload)
**Source:** review-consensus.md §2 [GPT]
**Produced by:** claude-sonnet-4-6

---

## Problem

`App::apply_font_metrics()` and `App::reload_config()` walk only the *currently active* `HostManager`. If the user has multiple workspaces (tabs), hosts in inactive workspaces receive neither the font-metrics update nor the config change. They see stale state until the user switches to that workspace — at which point a resize or repaint may or may not trigger the correct update.

Concrete symptoms:
- Change font size → inactive tab panes keep the old font until the tab is visited.
- Change a config setting (e.g., `enable_ligatures`) → inactive panes ignore it.

The app already has all-workspace viewport recomputation helpers; `apply_font_metrics()` and `reload_config()` do not use them.

**Files:**
- `app/app.cpp:377` — `apply_font_metrics()` (active workspace only)
- `app/app.cpp:453` — `reload_config()` (active workspace only)
- `app/app.cpp:1485` — all-workspace viewport helper (used for some operations, not these)

---

## Investigation

- [ ] Read `app/app.cpp:350–500` — understand `apply_font_metrics()` and `reload_config()`; find exactly which call or loop skips inactive workspaces.
- [ ] Read `app/app.cpp:1480–1510` — understand the all-workspace viewport helper; determine if it can be reused or if a similar loop is needed.
- [ ] Check whether calling `apply_font_metrics` on an inactive workspace host is safe (no GPU calls, no layout that requires a visible window).

---

## Fix Strategy

- [ ] In `apply_font_metrics()`, iterate over **all** workspaces (not just the active one). For each workspace's `HostManager`, call the appropriate font-update method on each host.
- [ ] In `reload_config()`, do the same: fan out config changes to all workspace host managers.
- [ ] Write a test (WI 107) that verifies inactive workspace hosts receive updates.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke: `py do.py smoke`

---

## Acceptance Criteria

- [ ] After a font change, all panes in all workspaces (active and inactive) use the new font metrics when they next render.
- [ ] After a config reload, all panes in all workspaces reflect the new settings.
- [ ] WI 107 propagation test passes.

---

## Interdependencies

- **WI 107** (inactive-workspace-config-propagation -test) — acceptance test for this fix.
- **WI 66** (config-reload-multi-pane -test, existing) — this bug is the source-level cause of the gap WI 66's test is probing.
