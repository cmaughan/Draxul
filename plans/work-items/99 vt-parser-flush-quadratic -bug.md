# WI 99 — vt-parser-flush-quadratic

**Type:** bug  
**Priority:** 2 (O(K²) per-character flush in Ground state)  
**Source:** review-bugs-consensus.md §M6 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

In `libs/draxul-host/src/vt_parser.cpp:90–98`, the `Ground` state calls `flush_plain_text()` after every single character pushed to `plain_text_`. If `flush_plain_text()` rescans `plain_text_` from the beginning to validate UTF-8 completeness, processing a K-byte cluster (e.g., a multi-codepoint emoji) results in K calls each doing up to K bytes of work — O(K²) total. This is visible as stalling during emoji-heavy terminal output.

---

## Investigation

- [ ] Read `libs/draxul-host/src/vt_parser.cpp` fully — trace the `flush_plain_text()` implementation and confirm how much re-scanning it does.
- [ ] Measure or estimate the practical cost per flush call for typical multi-byte clusters (3–4 bytes for most Unicode; up to 20+ bytes for ZWJ emoji sequences).
- [ ] Determine the correct batching point: flush should happen on control characters, escape entry, or when `kMaxPlainTextBuffer` is reached.

---

## Fix Strategy

- [ ] Remove the `flush_plain_text()` call from inside the regular-character branch (lines 97–98).
- [ ] Call `flush_plain_text()` only when:
  - A control character is encountered (before dispatching the control),
  - An escape character starts (transitioning to `State::Escape`),
  - `plain_text_.size() >= kMaxPlainTextBuffer` (the existing cap guard).
- [ ] Ensure `flush_plain_text()` is also called at end-of-input/end-of-buffer if `plain_text_` is non-empty.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`
- [ ] Run Unicode render tests to confirm emoji output is unchanged.

---

## Acceptance Criteria

- [ ] `flush_plain_text()` is called at most once per control character or escape, not once per byte.
- [ ] Multi-codepoint emoji sequences render correctly.
- [ ] Unicode render tests pass (or snapshots are re-blessed with correct output).
- [ ] Smoke test passes.
