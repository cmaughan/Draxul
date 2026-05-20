# WI 39 — rightclick-context-menus

**Type:** feature  
**Priority:** Low (discoverability; core functionality already accessible via keybindings)  
**Source:** review-consensus.md §6c — GPT  
**Produced by:** claude-sonnet-4-6

---

## Feature Description

Add right-click context menus to the three primary interactive surfaces:

1. **Tab bar**: right-click a tab → menu with: rename, duplicate, close, move left/right.
2. **Pane pill** (split-pane header): right-click → menu with: rename pane, close pane, zoom pane, split horizontal, split vertical, duplicate pane.
3. **Status/cwd area**: right-click → menu with: copy cwd, open new pane at cwd.

The goal is discoverability — users who don't know the keychords can right-click to find common actions. All actions should already be implementable through the existing keybinding dispatch; context menus are just an additional input route.

---

## Investigation

- [ ] Read `app/input_dispatcher.cpp` — find where right-click (`SDL_BUTTON_RIGHT`) mouse events are currently handled. They may be forwarded to Neovim or discarded.
- [ ] Read `app/chrome_host.cpp` — find where tab hit-testing and pane-pill hit-testing happen; understand what logical region is identified from a mouse position.
- [ ] Check if an ImGui or NanoVG popup/overlay pattern already exists in the codebase (e.g., does the command palette use a similar overlay mechanism).
- [ ] Read `app/host_manager.cpp` — identify available actions for tab/pane operations.

---

## Implementation Plan

### Step 1: Intercept right-click in ChromeHost

- [ ] In `ChromeHost` (or `InputDispatcher`), intercept `SDL_BUTTON_RIGHT` events when the click lands in the tab bar or pane pill region.
- [ ] Do not forward these to Neovim (or shell) — right-click on chrome UI is a GUI action.
- [ ] Identify which tab or pane was right-clicked; store as `context_menu_target_`.

### Step 2: Context menu overlay

Choose an implementation path:

**Option A (simpler): NanoVG-rendered popup**
- Draw a floating popup menu using NanoVG, positioned at the click location.
- Handle mouse events within the popup to dispatch actions.
- Close popup on click-outside or Escape.

**Option B (if ImGui is available): ImGui popup**
- Use `ImGui::BeginPopupContextItem()` or `ImGui::OpenPopup()`.
- Simpler to implement but requires ImGui to be present in the chrome layer.

**Recommendation:** check whether ImGui is accessible in the chrome/overlay layer; prefer Option B if it is.

### Step 3: Action dispatch

- [ ] Each context menu item should dispatch an existing named action (e.g., `Action::rename_tab`, `Action::close_pane`, `Action::duplicate_pane`, `Action::split_pane_horizontal`).
- [ ] The `duplicate_pane` action (WI 37) should be included; implement WI 37 first or in the same patch.

### Step 4: Tab right-click menu

- [ ] Items: Rename tab, Duplicate pane (WI 37), Close tab, Move tab left, Move tab right.
- [ ] "Rename tab" should focus the inline rename input if one exists.

### Step 5: Pane pill right-click menu

- [ ] Items: Rename pane, Close pane, Zoom pane, Split horizontal, Split vertical, Duplicate pane (WI 37).

### Step 6: Status/cwd right-click menu

- [ ] Items: Copy current cwd to clipboard, Open new pane at this cwd.

### Step 7: Configuration

- [ ] Add `enable_context_menus = true` to `AppConfig`; defaults to `true`.
- [ ] Document in `docs/features.md`.

### Step 8: Tests

- [ ] Unit test: right-click on tab hit region → context menu opens, correct target tab is set.
- [ ] Unit test: click "Close tab" in menu → correct pane is closed.
- [ ] Unit test: click outside menu → menu closes without action.

---

## Acceptance Criteria

- [ ] Right-click on a tab shows tab context menu.
- [ ] Right-click on a pane pill shows pane context menu.
- [ ] All menu actions work correctly and match keybinding equivalents.
- [ ] Context menu is dismissed by Escape or click-outside.
- [ ] Feature is documented in `docs/features.md`.
- [ ] Smoke test passes.

---

## Dependencies

- [ ] WI 26 (inputdispatcher-routing-consolidation) — right-click routing should go through the consolidated input path; implement after WI 26 to avoid adding more Deps footguns.
- [ ] WI 125 (overlay-registry-refactor) — context menu is an overlay; cleaner registry avoids hand-wiring.
- [ ] WI 37 (duplicate-pane) — "Duplicate pane" menu item depends on this action existing.
- [ ] WI 38 (pane-activity-badges) — visual surface design should be coordinated (both touch tab bar appearance).
