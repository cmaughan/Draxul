# WI 14 — HostManager split/close stress test

**Type:** test  
**Source:** review-latest.claude.md (critical gap), review-latest.gemini.md  
**Consensus:** review-consensus.md Phase 5

---

## Goal

Verify that HostManager's split-tree remains structurally valid and all pane descriptors are consistent after many rapid split/close cycles. This gap was flagged unanimously — rapid split/close is a real user workflow (muscle-memory `<C-w>s` / `<C-w>q` chains) and currently has zero coverage.

---

## What to test

- [ ] 1000 rapid split/close cycles on a fake host tree (no real nvim required — use `FakeHost` stubs).
- [ ] After each cycle, assert:
  - The split tree has no dangling leaf/branch pointers.
  - All registered pane descriptors have a corresponding live node in the tree.
  - No pane descriptor IDs are duplicated.
- [ ] Zoom during a pending split: open a split, zoom a pane, close another, un-zoom — assert tree is valid.
- [ ] Rename during shutdown: rename a pane, then immediately close it — assert no use-after-free (run under ASan).
- [ ] Verify that splitting produces correct initial viewport sizes (no zero-dimension panes).

---

## Implementation notes

- Use `HostManager` with `FakeHost` instances (see WI 25 for centralised fixtures).
- No GPU renderer needed — pass a `NullRenderer` or `FakeRenderer`.
- This test should be runnable with `ctest -R hostmanager-split-close-stress` on both macOS and Windows.
- Run the stress portion under ASan to catch any use-after-free in the tree mutation paths.

---

## Interdependencies

- WI 25 (centralised test fixtures) provides the `FakeHost` and `NullRenderer` helpers — do WI 25 first or in parallel.
- WI 16 (host lifecycle state machine) is a companion covering single-host lifecycle; this test covers the multi-host tree.
- WI 04 (NvimHost RAII rollback) must be in place before this test can reliably exercise host-creation failure paths.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
