# Drag to reorder tabs

**Type:** feature
**Priority:** 61
**Raised by:** Gemini

## User need

Reorder tabs directly with the pointer instead of invoking move-left/right actions repeatedly.

## Implementation plan

- [ ] Build on `ChromeLayout` tab rectangles from
      `kanban/done/23 chrome-layout-render-editing -refactor.md` and move the existing
      reorder operation into the partially delivered
      `kanban/ice-box/22 app-tab-session-controllers -refactor.md` boundary.
- [ ] Add a tab-drag state machine with press threshold, captured tab ID, insertion index, cancellation, and focus-loss handling.
- [ ] Render an insertion marker/ghost without mutating order on every mouse move; commit one controller reorder on release.
- [ ] Keep click-to-activate and inline rename behavior distinct from drag through distance/time thresholds.
- [ ] Auto-scroll or otherwise handle an overflowing tab strip only if the current Chrome layout exposes such behavior; do not mix pane drag/reorder from the ice box into this item.
- [ ] Persist the new order through normal session checkpointing and retain active tab identity.

## Tests and acceptance

- [ ] Test left/right/multi-position drag, no-op release, cancellation, rename interaction, close button, 1x/2x DPI, narrow tabs, and stale tab deletion during drag.
- [ ] Reorder preserves tab IDs, active/focused host, pane processes, and session round-trip.
- [ ] Existing keyboard move-left/right actions use the same controller operation.
- [ ] Chrome render/hit-test snapshots remain correct on both backends.

## Dependencies and parallelism

Depends on finishing the relevant `TabController` ownership in
`kanban/ice-box/22 app-tab-session-controllers -refactor.md`; Chrome layout is already
pure/tested. Distinct from pane drag/reorder.

<model>GPT-5 Codex</model>
