# WI 121 — App Render-Tree Overlay Ordering Test

**Type:** Test  
**Severity:** Medium (no coverage for overlay z-order correctness)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem

The app assembles a render tree that includes `chrome_host_`, `palette_host_`, `toast_host_`, and `diagnostics_host_` at specific z-positions. There are no tests that verify:
- The correct ordering of overlays in the render tree
- That disabling/enabling overlays (diagnostics panel toggle, command palette open/close) updates the render tree without leaving stale entries
- That the correct host receives input focus when overlays stack

From Gemini: "An `App` render-tree test covering overlay ordering with diagnostics, palette, toast, and chrome on/off."

---

## What to Test

```
Overlay ordering (bottom to top):
  1. Grid hosts (per pane)
  2. ChromeHost (tab bar)
  3. ToastHost
  4. DiagnosticsHost (when visible)
  5. CommandPaletteHost (when open) — must be topmost

Enabling/disabling:
  - Open command palette → palette appears at top of render tree
  - Close command palette → palette removed from render tree, focus returns to grid
  - Toggle diagnostics → diagnostics host enters/leaves render tree
  - Render tree snapshot is consistent after each state change

Input routing:
  - When palette is open: keyboard events go to palette, not grid
  - When palette is closed: keyboard events go to focused grid host
```

---

## Implementation Notes

- This may require a lightweight `App` test harness or a focused `HostManager` unit test
- The render tree is assembled differently than the input tree — test both
- Consider a `RenderTreeSpy` that records the ordered host list after each frame
- Test file: `tests/app_overlay_ordering_tests.cpp` or extend `tests/host_manager_tests.cpp`

---

## Acceptance Criteria

- [ ] Overlay z-order is asserted for each on/off combination
- [ ] Input routing assertions pass for palette open and closed states
- [ ] Tests run under `ctest`
- [ ] CI green

---

## Interdependencies

- **WI 125** (overlay registry) depends on this test existing before the refactor lands.
- **WI 120** (ToastHost lifecycle) tests the component; this test verifies its integration in the render tree.
