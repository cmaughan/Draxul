# Drag to reorder workspace tabs

**Type:** feature
**Priority:** 61
**Raised by:** Gemini

## User need

Reorder workspace tabs directly with the pointer instead of invoking move-left/right actions repeatedly.

## Implementation plan

- [ ] Build on `ChromeLayout` tab rectangles from pending `23 chrome-layout-render-editing -refactor.md` and the existing `App::move_workspace()` operation moved into `WorkspaceController` (the `ice-box/22 app-workspace-session-controllers -refactor.md` refactor, currently deferred).
- [ ] Add a tab-drag state machine with press threshold, captured workspace ID, insertion index, cancellation, and focus-loss handling.
- [ ] Render an insertion marker/ghost without mutating order on every mouse move; commit one controller reorder on release.
- [ ] Keep click-to-activate and inline rename behavior distinct from drag through distance/time thresholds.
- [ ] Auto-scroll or otherwise handle an overflowing tab strip only if the current Chrome layout exposes such behavior; do not mix pane drag/reorder from the ice box into this item.
- [ ] Persist the new order through normal session checkpointing and retain active workspace identity.

## Tests and acceptance

- [ ] Test left/right/multi-position drag, no-op release, cancellation, rename interaction, close button, 1x/2x DPI, narrow tabs, and stale workspace deletion during drag.
- [ ] Reorder preserves workspace IDs, active/focused host, pane processes, and session round-trip.
- [ ] Existing keyboard move-left/right actions use the same controller operation.
- [ ] Chrome render/hit-test snapshots remain correct on both backends.

## Dependencies and parallelism

Depends on the `WorkspaceController` refactor (`ice-box/22 app-workspace-session-controllers -refactor.md`, currently deferred to the ice box) and pending `23 chrome-layout-render-editing -refactor.md`. Distinct from ice-boxed pane drag/reorder. Best owned by a Chrome-focused agent after layout geometry is pure/tested.

<model>GPT-5 Codex</model>
