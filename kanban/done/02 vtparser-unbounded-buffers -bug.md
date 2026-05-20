# 02 vtparser-unbounded-buffers -bug

**Priority:** HIGH
**Type:** Bug
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

In `libs/draxul-host/src/vt_parser.cpp:62–65`, the accumulation buffers `plain_text_`, `csi_buffer_`, and `osc_buffer_` have no maximum size. A pathological or buggy nvim/terminal stream with runaway CSI or OSC sequences causes unbounded heap growth leading to OOM.

Note: `mpack-fuzz-test` is complete (MPack fuzzing). VT parser fuzz tests also exist (`vt_parser_fuzz_tests.cpp`). But the structural cap on buffer growth is absent in the production path — fuzzing finds crashes but does not guarantee the cap is enforced.

---

## Fix Plan

- [x] Read `libs/draxul-host/src/vt_parser.cpp` in full.
- [x] Define constants for maximum buffer sizes (e.g., `kMaxPlainTextBuffer = 64 * 1024`, `kMaxCsiBuffer = 4096`, `kMaxOscBuffer = 8192` — adjust based on realistic terminal output).
- [x] Add checks at the point each buffer is appended:
  - If the buffer would exceed its max, emit a WARN log and either:
    - Flush/reset the buffer (treating it as a truncated sequence), or
    - Drop the incoming byte and mark the sequence as errored.
  - The chosen strategy must not crash or hang.
- [x] Ensure the VT parser fuzz tests still pass after the change.
- [x] Build and run smoke test + ctest.

---

## Acceptance

- Feeding 1 MB of malformed CSI sequences: `csi_buffer_` does not grow beyond its cap, no OOM, WARN logged.
- Normal terminal sequences still parse correctly.

---

## Interdependencies

- Validates via **08-test** (vtparser-buffer-overflow).
- No upstream dependencies.
