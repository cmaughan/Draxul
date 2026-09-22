# Move client-local panes between tabs

**Type:** feature
**Priority:** 50
**Raised by:** GPT/Codex

## User need

Move a running client-local Nvim or plugin pane to another tab without
restarting its host or losing renderer, input, or presentation state.

## Delivered boundary

Server-owned terminals and managed agents already move live across tabs and
Spaces through the authoritative topology operation tracked by
`kanban/pending/33 cross-tab-server-pane-movement -feature.md`. That path
preserves pane, terminal, process, scrollback, and agent identity, and rejects
client-local panes explicitly. This card owns only the remaining UI-local host
transfer contract.

## Implementation plan

- [ ] Add a client-local `TabController` operation that transfers an existing
      pane between tabs without routing the mutation through server topology.
- [ ] Extend `PaneManager`/`SplitTree` with an ownership-safe leaf
      extraction/insertion transaction that moves the existing
      `unique_ptr<IHost>` and pane metadata rather than relaunching it.
- [ ] Define target placement (`focused split`, `new tab root`, or explicit
      side) and a deterministic fallback when the destination is empty.
- [ ] Preserve host owner lifetime tokens, renderer/pass attachment, zoom,
      title override, source configuration, focus, viewport, and session launch
      descriptor.
- [ ] Rebind renderer, window, input, and context callbacks whose ownership is
      tab-scoped; do not shut down the moved host.
- [ ] Expose “Move pane to tab…” through the palette with destination
      completion and prevent moving the final required pane when an empty
      source tab is not permitted.
- [ ] Persist the resulting client projection through the normal session
      checkpoint path and request one relayout/frame.
- [ ] Keep server-owned terminal and managed-agent panes on the authoritative
      topology path; do not duplicate pending card 33 in this operation.

## Tests and acceptance

- [ ] Move focused and background Nvim/plugin panes across empty and non-empty
      tabs in every split position.
- [ ] Verify the host pointer and runtime identity remain unchanged, callbacks
      stay valid, and focus/input route to the destination.
- [ ] Inject insertion, callback-rebind, and persistence failures and prove the
      source topology rolls back intact.
- [ ] Cover Nvim and representative native-plugin hosts plus session
      round-trip and tab-invariant stress.
- [ ] No host restart, duplicated shutdown, renderer-pass leak, lost plugin
      device state, or accidental server topology mutation occurs.

## Dependencies and parallelism

`TabController`, session descriptors, and the server-owned move boundary are
available. Coordinate with detachable windows (43), which can later reuse the
same client-local ownership-transfer transaction. One UI ownership agent
should implement the transfer and callback rebind as a single slice.

<model>GPT-5 Codex</model>
