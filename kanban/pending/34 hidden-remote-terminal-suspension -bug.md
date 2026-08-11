# Suspend hidden remote-terminal presentation

## Outcome

Inactive tabs and Spaces stop consuming per-terminal client polling, JSON projection,
grid-flush, and render work while their server-owned processes, terminal cores,
scrollback, topology, and agent observations continue running headlessly. Returning to
a hidden tab converges through one authoritative snapshot without changing controller
ownership or losing durable terminal state.

## Problem

A live Windows Session with twelve server terminals, including ten one-frame-per-second
dashboard panes in an inactive tab, showed:

- `draxul.exe` using about 117% CPU and `draxul-server.exe` about 57% CPU over a
  five-second sample;
- terminal control requests taking 25--322 ms, making queued input visibly coalesce;
- every `RemoteTerminalHost` attempting a poll every 25 ms regardless of visibility;
- inactive hosts being pumped, flushing hidden grids, and requesting frames for the
  visible tab; and
- the ten hidden dashboards accumulating 27,371 delta frames, roughly 23 million dirty
  cells, and about 618 MiB of encoded terminal deltas during the observed server run.

The server must continue draining PTY/ConPTY output. The defect is that presentation
delivery and rendering remain active for terminals that no attached UI can currently
see.

## Safety contract

- [ ] Server terminal runtimes continue pumping output at their normal cadence with
      zero active presentation subscribers. Headless `pane read`, `pane wait-output`,
      agent observation, process-exit cleanup, scrollback retention, and checkpointed
      topology remain current.
- [ ] Topology and agent projection polling remains independent and active for an
      attached UI, so remote pane/tab mutations and agent attention still converge
      while terminal presentation is suspended.
- [ ] Suspending presentation does not disconnect the UI client, release its terminal
      controller claim, restart the process, change terminal identity/generation, or
      clear server scrollback.
- [ ] Resuming is atomic from the client's perspective: it reactivates delivery and
      returns one current snapshot containing cells, cursor, modes, title, cwd, shell
      marks, process state, controller identity, generation, and sequence.
- [ ] A client that actually disappears still expires through the existing client
      activity lease and releases subscriptions and controller claims normally.
- [ ] Pending input, paste, resize, restart, scrollback, and take-control work is never
      stranded behind suspension. A command targeting a suspended host resumes it
      first or uses a server command path that does not depend on presentation polling.

## Protocol and server slice

- [ ] Add explicit idempotent terminal presentation suspend/resume operations rather
      than overloading `disconnect`. Advertise the capability during hello negotiation
      so older servers retain today's always-active behavior.
- [ ] Model subscriber delivery as active or suspended. A suspended subscriber retains
      its client/controller identity but owns no delta queue and is marked as requiring
      a complete snapshot when resumed.
- [ ] Do not encode a terminal delta when a terminal has no active presentation
      subscribers. With mixed active and suspended clients, encode once and queue only
      for active subscribers.
- [ ] Resume and snapshot publication happen on the server state thread so output cannot
      slip between reactivation and the returned authoritative version.
- [ ] Keep the existing 32-event/2-MiB slow-subscriber resync behavior for active
      subscribers; suspension must not consume those queue limits.
- [ ] Extend sanitized `terminal.metrics` with active/suspended subscriber counts,
      suspension/resume totals, avoided delta encodes, and intentionally suppressed
      ephemeral events without exposing terminal content.

## Ephemeral-event policy

- [ ] OSC 52 clipboard writes produced while the controlling UI is suspended do not
      mutate that UI's system clipboard later during resume. Count them as suppressed
      and document this as an intentional background-safety policy.
- [ ] Process exit, controller identity, title, cwd, cursor, and mode state converge via
      the resume snapshot. If immediate hidden-pane exit/activity badges are desired,
      carry only a compact semantic summary on the Session projection rather than
      re-enabling cell delivery.
- [ ] Preserve the last rendered client grid while suspended, but clear or revalidate
      stale selection, copy-mode, and client-local scrollback presentation when the
      resumed snapshot changes dimensions or generation.

## UI and host slice

- [ ] Add an explicit presentation-visibility lifecycle hook to `IHost`, defaulting to
      a no-op. `RemoteTerminalHost` implements it without platform-specific casts in
      `App` or `PaneManager`.
- [ ] Mark all panes in the active tab of the active Space visible; mark panes in other
      tabs and Spaces hidden. Keep zoom/minimize policy unchanged in the first slice so
      the visibility boundary is deterministic.
- [ ] Implement a nonblocking remote-host worker state machine:
      `Active -> Suspending -> Suspended -> Resuming -> Active`.
- [ ] Drain or safely retain already queued commands before entering `Suspended`. A
      visibility or command wake transitions immediately to `Resuming`.
- [ ] While suspended, schedule no terminal polls or 25-ms host deadline, publish no
      grid state, wake no window for terminal output, flush no hidden grid, and request
      no frame.
- [ ] On resume, asynchronously obtain the authoritative snapshot, reconcile controller
      role and process state, send the current viewport resize when this client is the
      controller, then resume normal foreground polling.
- [ ] Ignore stale worker publications from a pre-suspend visibility generation so an
      in-flight poll cannot wake or repaint the newly hidden tab.
- [ ] Keep client-local Nvim and other hosts on their existing background-pump policy;
      this change applies only to server-owned remote-terminal presentation.

## Integration-first validation

- [ ] Server/client integration: attach as controller, suspend, produce more than 32
      frames and 2 MiB, resume, and prove exact convergence to the authoritative grid
      without controller or terminal identity changes.
- [ ] Two-client integration: suspend the controller while an observer remains active;
      prove the observer continues receiving deltas and explicit takeover still works.
- [ ] Lease integration: suspend a client, stop its Session heartbeat, advance beyond
      the activity timeout, and prove normal detachment/controller release.
- [ ] Recovery integration: restart the server or replace the terminal generation while
      hidden; activation reattaches to the new epoch/generation without reviving stale
      cells or commands.
- [ ] Ephemeral integration: clipboard writes while suspended follow the documented
      suppression policy; title/cwd/process/controller state appears after resume.
- [ ] App vertical slice: run an active interactive terminal beside an inactive tab of
      ten high-output terminals. Deterministic metrics show zero hidden terminal polls,
      zero hidden frame requests, and no hidden delta encoding once every subscriber is
      suspended; active typing remains immediate and ordered.
- [ ] Resize/scrollback integration: resize the window while a tab is hidden, then
      activate it and prove current dimensions, live viewport, and retained server
      scrollback are coherent.
- [ ] Prefer the existing fake/isolated server integration fixtures and smoke path.
      Add narrow unit tests only for state-machine transitions that cannot be exercised
      deterministically through a vertical slice.

## Acceptance and manual smoke

- [ ] With the ten-pane dashboard tab hidden for at least ten seconds, its terminal
      metrics show ten suspended and zero active presentation subscriptions for that
      UI, with no increasing delta-byte delivery to it.
- [ ] The visible Codex or PowerShell pane accepts and echoes sustained typing without
      bursty coalescing attributable to hidden-pane control traffic.
- [ ] Switching to the dashboard tab shows one current frame promptly, then live updates
      continue with no missing terminal identity, scrollback, title, or controller role.
- [ ] Switching repeatedly during active output does not deadlock, leak worker threads,
      duplicate subscribers, or produce stale frame flashes.
- [ ] Windows Release build, full CTest, smoke, and relevant render snapshots pass.
      macOS compiles and its Unix-domain-socket integration coverage passes in CI.
- [ ] Update `docs/features.md` with the delivered visibility/suspension behavior and
      clipboard policy when implementation is complete.

## Rollback

Capability negotiation preserves the existing always-active polling path for older
servers. The new UI state machine can temporarily keep presentation active if suspend
or resume returns an unsupported or recoverable error; it must never stop the
server-owned terminal runtime as a fallback.
