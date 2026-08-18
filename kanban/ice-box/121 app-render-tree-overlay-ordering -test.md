# App render-tree overlay ordering

**Type:** test
**Priority:** 121

Current render order is Chrome, pane host tree, diagnostics, command palette, then Toast
(explicitly topmost). Generic overlay input routing already has focused coverage; this card
owns the App integration/order assertion.

- [ ] Record the ordered render tree for diagnostics/palette/toast on/off combinations.
- [ ] Verify opening and closing overlays rebuilds the tree without stale entries.
- [ ] Reuse existing input-routing tests for focus behavior and add only the App-level
      integration assertion that is missing.
- [ ] Keep `kanban/ice-box/125 overlay-registry-refactor -refactor.md` green throughout.
