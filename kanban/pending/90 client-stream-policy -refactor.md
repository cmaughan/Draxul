# Isolate client Session-stream policy

**Priority:** P1 — deterministic protocol testing has high leverage in concurrency-sensitive code.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 1.  
**Owner:** One client agent; delegate test cases only after the private value contract stabilizes.  
**Dependencies:** No dependency on another newly accepted refactor. Coordinate `kanban/pending/40 multiplexed-session-event-stream -feature.md` and `kanban/pending/51 session-client-worker-wakeup -bug.md`.

**Evidence:** `libs/draxul-client/src/remote_session_coordinator.cpp:2437–2781` combines frame ordering, acknowledgement, command correlation, retry, fairness, publication, and transport writes. Policy state shares storage with threads/connections at `:3033–3077`. Existing coordinator tests construct listeners and workers at `tests/remote_session_coordinator_tests.cpp:552–590,884–926`.

**Boundary:** A private worker-owned `SessionStreamPolicy` inside existing `draxul-client`. Inputs are decoded protocol values, command/registration identities, observed revisions, timestamps, and transport outcomes. Outputs are typed admission/send/completion/retry/acknowledgement/fallback decisions. Coordinator retains connections, synchronization, registrations, projection cursors, waiter notification, and UI publication. Public APIs and existing control/protocol/session-model dependencies remain unchanged.

#### Boundary verification

- [ ] Inventory command IDs versus mutation IDs, queue limits, visibility generations, acknowledgement state, fairness, and fallback order.
- [ ] Preserve the separate recovery/projection owners and stream → poll → legacy negotiation.
- [ ] Characterize transport-success-dependent transitions; acknowledgement must clear only after a successful Update write.

#### Implementation and migration

- [ ] Extract command bookkeeping, correlation, one-retry policy, and fallback decisions first.
- [ ] Extract frame admission, acknowledgement requirements, fairness, and heartbeat decisions next.
- [ ] Keep side effects in coordinator adapters and preserve their order after each migration.
- [ ] Use opaque identities rather than owning `Entry`, connection, host, or renderer pointers.
- [ ] Preserve send-order fallback and reverse terminal requeue behavior.

#### Unit tests

- [ ] Add direct private-policy cases for stale epochs, skipped/duplicate serials, invalid response IDs, explicit-time heartbeat expiry, and obsolete registrations.
- [ ] Cover every Events batch requiring acknowledgement, failed acknowledgement writes, external/terminal fairness, and retry only after a newer revision.
- [ ] Retain native stream/poll/fallback integration tests.
- [ ] Build `cmake --build <cache> --config Debug --target draxul-client draxul-test-core --parallel`.
- [ ] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-core-shard-' --parallel 4 --output-on-failure`; document a focused Catch tag for direct cases.

#### Cross-platform validation

- [ ] Exercise named-pipe and Unix-socket integration on Windows/macOS without changing transport ownership.
- [ ] Verify unchanged UI consumption on Vulkan/Metal and no graphics dependency enters policy.
- [ ] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [ ] Document private policy versus coordinator I/O/publication ownership and test-only access.
- [ ] Classify new tests once; keep headers private.
- [ ] Keep wakeup bug fixes in their existing card rather than silently combining them with extraction.

#### Acceptance criteria

- [ ] Ordering, fairness, retry, and acknowledgement decisions are testable without live listeners or sleeps.
- [ ] Public registration behavior, boundedness, worker ownership, and fallback ordering remain compatible.
- [ ] Each migration step builds; native integration coverage remains intact.
- [ ] No claim is made that the existing broad core test executable has become graphics-free.
