# WI 16 — Host lifecycle state machine tests

**Type:** test  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 5

---

## Goal

Verify that the host lifecycle transitions are robust against out-of-order, double, or missing calls. Currently the happy-path lifecycle is tested but edge cases are not.

---

## What to test

- [ ] **`pump()` before `initialize()`** — must return an error code or no-op, not crash.
- [ ] **`shutdown()` twice (double shutdown)** — second call must be idempotent; no crash, no double-free (run under ASan).
- [ ] **`pump()` after `shutdown()`** — must return a terminal error or be a no-op.
- [ ] **`initialize()` → failure path → `shutdown()`** — partial init rollback (see WI 04) must leave the host in a safe state; `shutdown()` must not crash.
- [ ] **Normal lifecycle: `initialize()` → `pump()` × N → `shutdown()`** — explicit positive test with a fake host, confirming basic invariants.
- [ ] **`on_viewport_changed()` before `initialize()`** — must not crash (delayed-init race from a resize event arriving early).

---

## Implementation notes

- Use `FakeHost` (see WI 25) for the generic lifecycle contract, plus a dedicated `NvimHost` harness for the nvim-specific paths.
- For the nvim-specific cases, use `replay_fixture.h` to drive the RPC without spawning a real nvim.
- Run all lifecycle tests under ASan to catch any use-after-free in destructor paths.
- Aim for these tests to be in a `host_lifecycle_test.cpp` in `tests/`.

---

## Interdependencies

- WI 04 (NvimHost RAII rollback) must be fixed before the failure-path test passes.
- WI 25 (centralised test fixtures) provides `FakeHost` helpers.
- WI 14 (HostManager split/close stress) is the multi-host companion test.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
