# Move a pane between tabs

**Type:** feature
**Priority:** 50
**Raised by:** GPT/Codex

## User need

Move a running pane to another tab without restarting its Nvim, shell, or product host.

## Implementation plan

- [ ] Add a `TabController::move_pane(source_tab, LeafId, target_tab, placement)` operation after the `TabController` refactor (`ice-box/22 app-tab-session-controllers -refactor.md`, currently deferred to the ice box) establishes controller ownership.
- [ ] Extend `PaneManager`/`SplitTree` with an ownership-safe leaf extraction/insertion transaction that moves the existing `unique_ptr<IHost>` and pane metadata rather than relaunching it.
- [ ] Define target placement (`focused split`, `new tab root`, or explicit side) and a deterministic fallback when the destination is empty.
- [ ] Preserve host owner lifetime tokens, renderer/pass attachment, zoom, title override, working directory, focus, viewport, and session launch descriptor.
- [ ] Rebind renderer/context callbacks only if their ownership is tab-scoped; do not shut down the moved host.
- [ ] Expose “Move pane to tab…” through the palette with destination completion and prevent moving the final required pane if invariants forbid an empty source.
- [ ] Persist the new topology through the normal session checkpoint path and request one relayout/frame.

## Tests and acceptance

- [ ] Move focused/background panes across empty/non-empty tabs and every split position.
- [ ] Verify the host pointer/identity and process remain unchanged, callbacks stay valid, and focus/input route to the destination.
- [ ] Inject insertion/persistence failure and prove the source topology rolls back intact.
- [ ] Cover each host kind and session round-trip; run tab invariant stress.
- [ ] No process restart, output loss, duplicate shutdown, or renderer-pass leak occurs.

## Dependencies and parallelism

Depends on the `TabController` refactor (`ice-box/22 app-tab-session-controllers -refactor.md`) and its host/session-state prerequisites — all currently deferred to the ice box, so this card is blocked until they are scheduled. Coordinate with detachable windows (43), which can later reuse the same extraction transaction. Good isolated feature-agent task after `TabController` is stable.

<model>GPT-5 Codex</model>
