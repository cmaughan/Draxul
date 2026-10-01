# Built in Personal Assistant host and server orchestration

**Summary:** Host persistent, folder-backed personal agents in Draxul with editable
instructions, provider/model choice, visible activity, bounded scheduled runs and
single-executor ownership. The host is built in like Kanban; the server owns work.

**Design:** [Personal Assistant initial implementation plan](../../plans/personal-assistant.md)  
**Status:** Slice 1 implemented and locally exercised on 2026-10-01; ready for user review with unrelated aggregate failures noted below.  
**Scope:** Core repository; Windows and macOS.

## Decisions and constraints

- A definition is independent of a run, pane, tab and Session. Closing a view must
  not stop orchestration. Active runs also appear in the existing Agents section.
- Use a user-selected collection root, potentially in Dropbox. Synced files hold
  durable definitions and results, not credentials or distributed execution locks.
- Preserve provider choice through supported adapters; no implicit model fallback.
  This is hosting/orchestration, not a new reasoning framework or Outlook connector.
- One executor owns the collection. Coordinate cross-machine claims through an
  atomic lease service, not a heartbeat file. Graceful handoff verifies process
  exit and checkpoint availability; expiry is not proof an old worker stopped.
- Coordinator backend/authentication remain an implementation design gate. Pinned
  single-machine execution is only an intermediate milestone. No unsafe forced
  takeover or claims of exactly-once external actions.
- Existing launch is pane/topology-backed and agent projection Session-scoped;
  extract only the narrow run seam needed, preserving current interactive agents.

## Delivery checklist

- [x] Inspect current feature inventory, agent/server boundaries and Kanban host pattern.
- [x] Write and index the initial plan with ownership risks and vertical slices.
- [x] Slice 1: strict store, server collection projection, read-only host and persistent rail entries.
- [ ] Slice 2: creation preview, revision-safe instruction edits, runtime/model selection, manual runs and active Agents projection.
- [ ] Slice 3: intervals, durable messages, pause/cancel, budgets, retries and crash/sleep recovery.
- [ ] Resolve and record the real coordinator backend, provisioning and authentication decision.
- [ ] Slice 4: automatic unowned acquisition, owner display, graceful handoff and fail-closed uncertainty.
- [ ] Slice 5: finish host/CLI UX, render coverage and shipped feature/module-map documentation.

## Acceptance and validation

- [ ] Two local clients in different Sessions see consistent definitions and live run status; closing all GUIs leaves server work running.
- [ ] Instructions/model changes survive server restart and affect only the intended run revision; invalid/conflicted files cannot silently launch work.
- [ ] A supported live provider completes a harmless local task with bounded inspectable output; absent authentication is actionable.
- [ ] Scheduling and messaging integration tests cover duplicates, overlap, limits, timeouts, loops and ambiguous interrupted runs without blind replay.
- [ ] Ownership tests cover simultaneous claims, stale generations, renewal loss, delayed sync, interrupted handoff and resume from sleep.
- [ ] Perform a real two-machine Windows/macOS graceful handoff; verify no overlapping runs and correct checkpoint transfer using the selected coordinator.
- [ ] Record one core aggregate and same-cache smoke for each completed slice, with relevant UI render checks and normal cross-platform CI.
- [ ] Complete final Release startup confirmation and record remaining limitations.

## Evidence and next step

Planning inspected `docs/features.md`, `docs/module-map.md`, the server's agent
runtime projection, topology-backed managed launch, and the built-in Kanban module.
No equivalent personal collection feature was found in the inspected feature
inventory. The first implementation review point is a read-only collection visible
in two clients. Store, protocol, server scan service, asynchronous client polling,
read-only grid host, built-in registration and persistent sidebar integration are
implemented. Execution remains disabled. A partially applied patch initially left
the host outside the build; this wiring is now complete. See
`docs/personal-assistant.md` and `examples/personal-assistant/` for setup.

### Slice 1 validation and review boundary

- Windows Ninja Debug, `py do.py test debug`: compilation passed; latest aggregate
  passed 53/56 CTest entries in 57.28 seconds. New server/store/protocol,
  personal-host, configuration/CLI and sidebar coverage passed. The real-server
  test uses two authenticated clients with different Session parameters, verifies
  shared updates, rejection of execution and no additional terminals, and checks
  retained client data after disconnect. The host test covers selection and
  closing/reopening a view. These are not a substitute for a manual two-GUI review.
- Remaining aggregate failures: joined family emoji (existing card 63), Windows
  test-scope fixture (existing card 64), and the concurrently developed flashcards
  product-scope expectation in `tests/do_py_tests.py`. The flashcards chat was
  notified; unrelated code was not changed to force a green aggregate.
- Latest configure/generate: 75.4s / 2.2s. Combined configure/build: 93.70s for
  the core aggregate target (20 Ninja steps); compilation/other build overhead
  accounts for the remaining approximately 16.1s. No separate focused test pass.
- Earlier aggregate: 51/56 in 66.22s; fixed two new fixture assumptions (a sidebar
  needs a Space, and the server already has its bootstrap terminal). This repeat
  is intentional. Earlier attempts also included an incomplete include dependency,
  a concurrent missing flashcard source at configure (~74s), and one lease refusal
  (~0.7s). A separate workflow's successful shared build is not counted as our test.
- `py do.py smoke --skip-build`: passed against the same executable; no rebuild.
  It briefly overlapped the end of the aggregate; separate elapsed timing was
  not captured. The wrapper bounds execution to 30s.
- `ctest --test-dir build-ninja-debug -C Debug --parallel 4 -R
  '^draxul-render-personal-assistant-smoke$' --output-on-failure`: 1/1 passed in
  2.79s. Inspected the exported Windows bitmap: title, controls and disconnected
  waiting state render legibly. Populated content is covered by host tests, not
  by this disconnected render scenario.
- Scoped `git diff --check` passed. No macOS/remote CI run or final Release build
  was performed in this slice. Normal cross-platform CI remains applicable.
- The existing default Release server was left running and user configuration
  was not changed. A new client may require that server to be deliberately
  restarted with the new executable after setting `[agents].personal_root`.
- Next review: configure a collection, open Personal Assistant in two real GUIs,
  edit instructions externally and verify convergence/diagnostics. Then review
  before starting Slice 2 (creation/editing/manual execution).

Keep this card pending until every delivery and validation box is checked. Record
slice decisions, commands, costs and results here as work proceeds.
