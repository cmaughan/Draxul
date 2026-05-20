---
# WI 66 — Config Reload with Multiple Active Panes Tests

**Type:** test  
**Priority:** high (multi-pane config reload is a gap; rollback path untested)  
**Raised by:** [C] Claude, [P] GPT  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`App::reload_config()` calls `for_each_host()` to propagate new font metrics. This path has no test with multiple live panes. Three untested sub-scenarios:

1. Font size change propagates to **all** panes (not just the focused one)
2. A failed font reload leaves **all** pane state coherent (no partial updates)
3. Atlas reset after font change does not lose glyphs from a concurrent `CommandPaletteHost`

---

## Findings

- WI 107 already covered the **cross-workspace** propagation gap (inactive workspaces also receive reloads).
- This work item closes the **within-workspace, multi-pane** gap by exercising a workspace that has been split via `Ctrl+Alt+V` and verifying both panes receive the reloaded config.
- Scenarios B (failed-reload rollback) and C (atlas coherence with `CommandPaletteHost`) are intentionally **deferred**: the existing app/host fakes do not provide a clean seam for forcing a font-load failure mid-reload, and the palette atlas-coherence path is better tested as a rendering snapshot than as a host-level unit test. Filed as follow-up if/when the rollback path lands.

---

## Investigation Steps

- [x] Read `app/app.cpp` around `reload_config()` and `for_each_host()` call
- [x] Read `CommandPaletteHost::flush_atlas_if_dirty()` to understand atlas lifecycle
- [x] Identify the test seam for instantiating multiple hosts in `HostManager`
- [x] Look for existing `config_reload` tests in `tests/`

---

## Test Cases

### Scenario A — Font metrics propagate to all panes
- [x] Create app with split workspace (2 panes via `Ctrl+Alt+V`)
- [x] Call `reload_config()` via the configured `Ctrl+Alt+R` chord with a new font size
- [x] Verify both panes received `apply_font_metrics()` and the new `AppConfig`

### Scenario B — Failed reload rollback (deferred — see Findings)
- [ ] Configure reload to fail (e.g. font file path made invalid)
- [ ] Call `reload_config()`
- [ ] Verify all hosts retain original font metrics
- [ ] Verify `AppConfig` state is unchanged

### Scenario C — Atlas coherence after font change (deferred — see Findings)
- [ ] Open `CommandPaletteHost`; flush atlas to a known dirty state
- [ ] Trigger font reload (atlas reset expected)
- [ ] Verify `CommandPaletteHost` rebuilds its glyphs correctly on next render frame

---

## Implementation

- [x] Added test case `"app smoke: reload_config propagates to all split panes in the active workspace"` in `tests/app_smoke_tests.cpp` (`[app_smoke][config][splits]`)
- [x] Reuses the existing `g_all_reload_hosts` capture vector from WI 107

---

## Acceptance Criteria

- [x] Scenario A has a passing test
- [x] Scenarios B and C documented as deferred with rationale
- [x] No new test infrastructure required beyond existing fakes
