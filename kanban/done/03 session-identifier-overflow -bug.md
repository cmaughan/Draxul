# Validate restored identifiers and counters

**Summary:** Reject saved numbers that cannot support the next item safely so restoring a session cannot overflow its counters.

**Priority:** 70  
**Severity:** CRITICAL  
**Source:** `libs/draxul-session-model/src/session_state.cpp`

**Evidence and trigger:** B05; maximum signed identifiers pass validation, then overflow during server capture or standalone Space and tab restoration.

- [x] **Investigate:** Inventory identifier and next-counter validation and increment sites across both restoration paths.
- [x] **Fix:** Reject unsupported ranges and use checked increments with explicit exhaustion handling.
- [x] **Acceptance:** Boundary identifiers and allocator values cannot overflow during restore, capture, or subsequent creation.
- [x] **Validation:** Retain valid-session round trips; run core aggregate tests and same-cache smoke.

## Implementation notes (2026-10-05)

Inventory of overflow sites (all `int`):

- Decode (`session_state.cpp`): every id/counter was `static_cast<int>` of a
  64-bit TOML integer, so `4294967298` silently became `2`. Validation accepted any
  Space/leaf id except `-1` and any non-negative tab id, including `INT_MAX`, and
  never checked `next_space_id`, `next_tab_id`, or `next_leaf_id`.
- Server capture (`session_topology_bridge.cpp`): `space.id + 1` and `tab->id + 1`
  on serials parsed from topology ids; `topology_layout.cpp` `maximum_leaf + 1`.
- Standalone restore: `SpaceController::restore_spaces` (`max_space_id + 1`),
  `TabController::restore_tabs` (`max_tab_id + 1`), and `SplitTree::restore`
  (`max_leaf_id + 1`).
- Creation after restore: `next_space_id_++`, `next_tab_id_++` (four sites), and
  `SplitTree::split_leaf` `++next_id_`.

Fix:

- `session_model.h` defines `kSessionIdentifierLimit` (`INT_MAX`): ids must be in
  `[0, limit)` and counters in `[0, limit]`, where a counter at the limit means the
  identifier space is exhausted. `allocate_session_identifier` returns `nullopt` on
  exhaustion instead of incrementing.
- Decode rejects values outside `int` with "Session state contains an
  out-of-range identifier or counter." Validation enforces the id and counter
  ranges for Spaces, tabs, leaves, and all three counters.
- Server capture and topology layout conversion reject an id at the limit before
  deriving `id + 1`. Standalone restore uses the same id predicate and clamps
  counters to `[max_id + 1, limit]`. Space, tab, and leaf creation fail cleanly on
  exhaustion. Their existing callers already handle `kInvalidSpaceId`, `-1`, and
  `kInvalidLeaf`.
- Not changed: the client-local `TopologyProjection::next_leaf_id_` starts at 0 for
  each connection and is not restored from saved data. It would need 2^31 distinct
  panes in one connection.

Tests: session-state boundary round trip, rejection of out-of-range TOML values
and ids, and SplitTree exhaustion (`session_state_tests.cpp`); standalone
Space/tab restore, rejection, and creation exhaustion (`space_controller_tests.cpp`);
server restore/capture at the boundary and capture rejection of live serials at
the limit (`server_topology_terminal_tests.cpp`).

## Validation (2026-10-05, macOS arm64, Debug make cache `build/`)

- Core aggregate `python3 do.py test debug`: 55/56 CTest entries pass. The one
  failure is `draxul-do-py-tests`, which reads
  `plugins/megacity/product/AGENTS.md`; that submodule is not initialized in this
  worktree, so the failure is environmental. On two earlier aggregate runs under
  heavy machine load (load average 15 to 25 from concurrent builds),
  `app dispatch: shared Neovim split retains actions until its host exists` failed
  its 20 ms `run_smoke_test` pump deadline. That test passed 3/3 shard runs and 8/8
  direct runs both with and without this change, and passed in the final aggregate.
  `server_service_protocol_tests.cpp:567` (`backpressure_elapsed`) failed once
  under load and passed 3/3 when repeated.
- Same-cache smoke `python3 do.py smoke --skip-build`: passed.
- Focused `[session_state],[space_controller]` (draxul-test-app): 24 cases pass,
  including the existing valid-session round trips. The server boundary case
  (draxul-test-server-integration) passes with 17 assertions.
