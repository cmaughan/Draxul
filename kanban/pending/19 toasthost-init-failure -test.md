# WI 19 — ToastHost `create_grid_handle()` failure during initialize

**Type:** test  
**Source:** review-bugs-latest.gpt.md  
**Consensus:** review-consensus.md Phase 5

---

## Goal

Verify that `ToastHost::initialize()` returns gracefully (no crash, no null-deref) when `create_grid_handle()` returns null. This test directly covers the bug fixed by WI 11 (null grid handle).

---

## What to test

- [ ] Construct a `ToastHost` backed by a fake renderer that injects a `nullptr` from `create_grid_handle()`.
- [ ] Call `initialize()` and assert it returns `false` (not true, not an exception, not a crash).
- [ ] Assert no subsequent method on `handle_` is called (check with an `EXPECT_CALL`-style mock or a crash-on-deref sentinel).
- [ ] Assert that the error is logged (`DRAXUL_LOG_ERROR` with an identifiable message).
- [ ] After the failed init, verify that `ToastHost` can be safely destroyed without a double-free or crash.

---

## Implementation notes

- Use `FakeGridHandle` that returns null on request — this may already exist in the test suite; if not, create a minimal stub.
- Run under ASan to catch any null-deref or leak in the failure path.
- Place alongside WI 18 in `tests/toast_host_test.cpp`.

---

## Interdependencies

- **Requires WI 11 (null grid handle bug fix) to be fixed first** — test will fail as a crash until that fix lands.
- WI 18 (idle wake delivery test) is the companion test for the other toast bug.
- WI 25 (centralised test fixtures) provides the `FakeGridHandle` infrastructure.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
