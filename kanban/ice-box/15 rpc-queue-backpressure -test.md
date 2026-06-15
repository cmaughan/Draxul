# WI 15 — RPC notification queue backpressure test

**Type:** test  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 5

---

## Goal

Verify that the notification queue correctly handles burst traffic, that no notifications are silently lost, and that the queue's warn/drop thresholds behave as documented. Currently there is no test for the `kMaxNotificationQueueDepth = 4096` / `kNotificationQueueWarnDepth = 512` behaviour.

---

## What to test

- [ ] Enqueue 600 notifications rapidly — assert the warn-depth warning fires at 512 and the queue still delivers all 600.
- [ ] Enqueue 4096+ notifications — determine and assert the documented behaviour (block? drop? both are acceptable but must be explicit and not UB).
- [ ] After a burst, verify the drain rate: with a synthetic consumer calling `drain_notifications()`, assert all in-flight notifications are processed in FIFO order.
- [ ] Thread-safety check: producer on a background thread, consumer on main thread — run under TSan and assert no races.
- [ ] Assert that a warning log is emitted at the warn threshold (use a fake log sink or check the structured log output).

---

## Implementation notes

- Drive the test entirely through the `NvimRpc` notification API using `replay_fixture.h` helper builders — no live `nvim` process needed.
- The test exercises `libs/draxul-nvim/src/rpc.cpp`'s queue implementation.
- Run under TSan: `cmake --preset mac-tsan && cmake --build build --target draxul-tests && ctest -R rpc-queue-backpressure`.

---

## Interdependencies

- Depends on WI 05 (reader exception safety) being in place — the reader thread must not crash during the burst.
- WI 07 (callback race) should also be fixed first so the TSan run is clean.
- This test can be written before the WI 05/07 fixes as a characterisation test to confirm the bugs exist.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
