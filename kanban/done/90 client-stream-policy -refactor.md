# Isolate client Session-stream policy

**Priority:** P1 — deterministic protocol testing has high leverage in concurrency-sensitive code.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 1.  
**Owner:** One client agent; delegate test cases only after the private value contract stabilizes.  
**Dependencies:** No dependency on another newly accepted refactor. Coordinate `kanban/pending/40 multiplexed-session-event-stream -feature.md` and completed `kanban/done/51 session-client-worker-wakeup -bug.md`.

**Evidence:** `libs/draxul-client/src/remote_session_coordinator.cpp:2437–2781` combines frame ordering, acknowledgement, command correlation, retry, fairness, publication, and transport writes. Policy state shares storage with threads/connections at `:3033–3077`. Existing coordinator tests construct listeners and workers at `tests/remote_session_coordinator_tests.cpp:552–590,884–926`.

**Boundary:** A private worker-owned `SessionStreamPolicy` inside existing `draxul-client`. Inputs are decoded protocol values, command/registration identities, observed revisions, timestamps, and transport outcomes. Outputs are typed admission/send/completion/retry/acknowledgement/fallback decisions. Coordinator retains connections, synchronization, registrations, projection cursors, waiter notification, and UI publication. Public APIs and existing control/protocol/session-model dependencies remain unchanged.

#### Boundary verification

- [x] Inventory command IDs versus mutation IDs, queue limits, visibility generations, acknowledgement state, fairness, and fallback order.
- [x] Preserve the separate recovery/projection owners and stream → poll → legacy negotiation.
- [x] Characterize transport-success-dependent transitions; acknowledgement must clear only after a successful Update write.

#### Implementation and migration

- [x] Extract command bookkeeping, correlation, one-retry policy, and fallback decisions first.
- [x] Extract frame admission, acknowledgement requirements, fairness, and heartbeat decisions next.
- [x] Keep side effects in coordinator adapters and preserve their order after each migration.
- [x] Use opaque identities rather than owning `Entry`, connection, host, or renderer pointers.
- [x] Preserve send-order fallback and reverse terminal requeue behavior.

#### Unit tests

- [x] Add direct private-policy cases for stale epochs, skipped/duplicate serials, invalid response IDs, explicit-time heartbeat expiry, and obsolete registrations.
- [x] Cover every Events batch requiring acknowledgement, failed acknowledgement writes, external/terminal fairness, and retry only after a newer revision.
- [x] Retain native stream/poll/fallback integration tests.
- [x] Build `cmake --build <cache> --config Debug --target draxul-client draxul-test-core --parallel`.
- [x] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-core-shard-' --parallel 4 --output-on-failure`; document a focused Catch tag for direct cases.

#### Cross-platform validation

- [x] Exercise Unix-socket integration on macOS without changing transport ownership.
- [x] Exercise named-pipe integration on Windows.
- [x] Verify unchanged Metal UI consumption and no graphics dependency enters policy.
- [x] Verify unchanged Vulkan UI consumption on Windows.
- [x] Run `python3 do.py test debug`, then `python3 do.py smoke --skip-build`.

#### Agent documentation/tooling

- [x] Document private policy versus coordinator I/O/publication ownership and test-only access.
- [x] Classify new tests once; keep headers private.
- [x] Keep wakeup bug fixes in their existing card rather than silently combining them with extraction.

#### Acceptance criteria

- [x] Ordering, fairness, retry, and acknowledgement decisions are testable without live listeners or sleeps.
- [x] Public registration behavior, boundedness, worker ownership, and fallback ordering remain compatible.
- [x] Each migration step builds; native integration coverage remains intact.
- [x] No claim is made that the existing broad core test executable has become graphics-free.

#### Delivered checkpoint (2026-09-21)

- The private header is `libs/draxul-client/src/session_stream_policy.h`; the coordinator retains transport, decoded-payload application, waiter notification, recovery, projection, and publication side effects.
- Direct policy coverage is tagged `[client][session-stream-policy]` and runs in the default core classification: 6 cases and 63 assertions.
- Native macOS coverage passed `[remote-session-coordinator][session-stream]` (3 cases, 90 assertions) and `[remote-session-coordinator]` (8 cases, 191 assertions).
- The required aggregate passed 22/22 tests (40.23 s CTest real time), and the same-cache Metal smoke passed on Apple M5.
- Windows named-pipe integration and Vulkan smoke remain remote-CI evidence before this card moves to done.

## Windows closeout (2026-09-22)

Native named-pipe/session-stream coverage passed in the final 48/48 products
aggregate, followed by a successful same-cache Vulkan smoke.
