# Extract residual App topology application glue

**Type:** refactor
**Priority:** 22
**Raised by:** GPT/Codex, Claude, Gemini
**Model:** GPT-5 Codex

## Goal

Reduce the remaining server-topology revision and UI-application logic in
`App` without recreating owners that have already been extracted. Authoritative
Session mutation, persistence, transport, recovery, projection identity, tab
ownership, and Space ownership stay with their current components.

## Delivered ownership

- Commit `3787ba7` introduced `app/tab_controller.*`. `TabController` owns tab
  records, active identity, creation, activation, closure, per-tab
  `PaneManager` state, focus transitions, layout access, and restore behavior.
  `tests/active_tab_invariant_tests.cpp` and the focused tab tests protect that
  boundary.
- `app/space_controller.*` owns live Space identity, navigation, metadata,
  active focus transfer, snapshot/restore, and shutdown.
  `tests/space_controller_tests.cpp` covers those responsibilities.
- `draxul::TopologyProjection` owns stable server-to-local Space, tab, and pane
  identity, projected layouts, command activations, and projection errors.
  `tests/topology_projection_tests.cpp` exercises that code without `App`.
- `RemoteSessionClient` and `RemoteSessionCoordinator` own negotiated Session
  polling/stream transport, recovery, revisions/cursors, command delivery, and
  publication mailboxes. Their focused client/coordinator suites plus
  `kanban/done/19 client-recovery-state-machine -refactor.md` and
  `kanban/done/40 multiplexed-session-event-stream -feature.md` protect those
  owners.
- The server remains authoritative for Session topology mutation, terminal
  processes, and persistence.

The earlier proposal for a broad `SessionProjectionController` is superseded by
these delivered owners. Do not introduce another controller for registry,
transport, recovery, Session mutation, tabs, or Spaces.

## Remaining scope

The residual boundary is the UI-thread glue in `App` that accepts a published
topology revision, applies projected Spaces/tabs/panes to live host trees,
acknowledges or reports the result, and refreshes dependent UI state. It is
currently centered around `accept_next_remote_topology_revision_`,
`apply_remote_topology_spaces`, `apply_remote_topology_tabs`, and their command
result/application path.

- [ ] Catalogue only those remaining fields and methods, and identify which
      decisions are pure projection/revision policy versus UI host-tree effects.
- [ ] Move pure identity, revision, activation, and projection-plan logic into
      the existing `TopologyProjection` or client policy owners where it fits
      their current contract.
- [ ] Consolidate the App-side application sequence into one operation that
      applies a projection result, updates live host-tree presentation, and
      returns the acknowledgements/errors and refresh effects App must perform.
- [ ] Remove repeated layout/visibility/input refresh rituals from individual
      topology branches after the consolidated operation is covered.
- [ ] Keep `App` responsible for subsystem composition, the UI thread, live
      host-tree mutation, renderer/window effects, and the main frame loop.
- [ ] Preserve `TabController`, `SpaceController`, `TopologyProjection`,
      `RemoteSessionClient`, and `RemoteSessionCoordinator` as the existing
      owners; do not create parallel state or a second recovery/transport path.

## Tests

- [ ] Extend `tests/topology_projection_tests.cpp` for any pure revision or
      projection-plan behavior moved out of `App`.
- [ ] Retain controller-level tab/Space focus and transactional restore tests.
- [ ] Add narrow App integration coverage for authoritative revision
      replacement, rejected topology application, reconnect/resync,
      acknowledgement ordering, dependent UI refresh, and shutdown ordering.
- [ ] Prove stale or duplicate publications cannot replace a newer live
      projection and a failed application leaves the last coherent UI usable.

## Acceptance criteria

- [ ] `App` no longer contains the detailed server-topology revision and
      Space/tab/pane reconciliation algorithm; it invokes one bounded
      UI-application operation and performs the returned effects.
- [ ] No new component duplicates tab, Space, projection identity, transport,
      recovery, authoritative mutation, or persistence ownership.
- [ ] The existing controller and client boundaries remain independently
      testable without constructing `App`.
- [ ] No behavior, keybinding, topology protocol, Session format, or ownership
      change occurs.
- [ ] Focused projection/controller/App tests, aggregate tests, render suite,
      and same-cache smoke pass.

## Coordination

This work owns a concentrated `app/app.cpp` boundary and should not overlap
another App-wide refactor. Changes to client transport/recovery or server
topology are outside this card unless a proven defect prevents the extraction.
