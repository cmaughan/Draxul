# WI 119 — ChromeHost Tab-Bar Hit-Testing and Viewport Test

**Type:** Test  
**Severity:** Medium (missing acceptance coverage for chrome behaviour)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`ChromeHost` manages the tab bar, including hit-testing for tab clicks and viewport calculations. There are no dedicated unit tests for:
- Which tab is hit for a given mouse coordinate at various window widths
- What happens to viewport geometry when the tab bar is shown/hidden
- How hit-testing and viewport react to DPI scale changes
- Correct behaviour when the tab bar has 1, many, or overflow-width tabs

Without these, any refactor touching `ChromeHost` (including **WI 125** overlay registry) has no regression safety net.

From Gemini: "A `ChromeHost` test for tab-bar hit-testing and viewport updates across resize and DPI changes."

---

## What to Test

```
ChromeHost tab-bar hit-test matrix:
  - Single tab: click anywhere in tab area → tab 0 selected
  - Multiple tabs: click in second tab region → tab 1 selected
  - Click below tab bar → no tab change
  - Click outside window → no crash

ChromeHost viewport geometry:
  - With tab bar: content viewport top = tab bar height
  - Without tab bar (single tab, chrome hidden): content viewport top = 0
  - After window resize: viewport dimensions recalculated correctly
  - After DPI change: tab bar height scales, viewport adjusts

ChromeHost DPI:
  - DPI 1.0 vs 2.0: tab bar pixel height doubles, hit-test regions scale
```

---

## Implementation Notes

- Use `ChromeHost::Deps` with a fake `IWindow` that returns controllable `ppi()` / `display_scale()`
- Use a `FakeRenderer` or headless renderer stub (see `tests/support/`)
- Test files: add to `tests/chrome_host_tests.cpp` (create if absent)
- No Neovim process required; this is pure layout logic

---

## Acceptance Criteria

- [x] `tests/chrome_host_tabbar_tests.cpp` exists and covers hit-test matrix above
- [x] Viewport geometry is asserted after resize and DPI change
- [x] Tests run under `ctest --test-dir build -R draxul-tests`
- [x] CI green

Notes:
- Test file lives at `tests/chrome_host_tabbar_tests.cpp` so its scope
  (tab-bar hit-testing and DPI) stays clearly separated from any future
  chrome_host test files.
- "Viewport geometry after resize/DPI" is asserted through `tab_bar_height()`,
  which is ChromeHost's only public viewport-related layout number. The
  cell_size-driven scaling covers the DPI axis; `hit_test_tab` assertions
  cover the per-tab column span that changes with resize.
- "Chrome hidden" is modelled by passing `grid_renderer = nullptr`, which
  matches the `tab_bar_height() == 0` branch in production code.

---

## Interdependencies

- **WI 125** (overlay registry refactor) should not land until these tests exist and pass.
