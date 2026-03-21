# 08 vtparser-buffer-overflow -test

**Priority:** HIGH
**Type:** Test
**Raised by:** Claude (validates 02-bug fix)
**Model:** claude-sonnet-4-6

---

## Problem

Even though `vt_parser_fuzz_tests.cpp` exists, there is no deterministic unit test that drives `csi_buffer_`, `osc_buffer_`, or `plain_text_` past their expected maximum and asserts that the parser handles this gracefully (truncation, WARN log, no OOM, no crash).

---

## Implementation Plan

- [x] Read `libs/draxul-host/src/vt_parser.cpp` and `vt_parser_fuzz_tests.cpp` to understand the parser API and existing fuzz entry point.
- [x] Write a deterministic test in `tests/vtparser_overflow_tests.cpp`:
  - Construct a synthetic stream of `>= kMaxCsiBuffer` bytes of CSI content (e.g., a single CSI sequence with a very long parameter string).
  - Feed to the VtParser.
  - Assert: the `csi_buffer_` size never exceeds `kMaxCsiBuffer`, no OOM, parser state is reset to a known baseline after the overlong sequence.
  - Repeat for OSC: `>= kMaxOscBuffer` bytes in an OSC payload.
  - Repeat for `plain_text_` accumulation.
- [x] The test must not OOM itself — generate the stream in a streaming fashion or use a fixed-size buffer.
- [x] Add to `tests/CMakeLists.txt`.
- [x] Run under ASan: `cmake --preset mac-asan && cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`.

---

## Acceptance

- Feeding oversized sequences: parser does not OOM, buffers stay within defined caps, WARN logged.
- Normal VT sequences still parse correctly (run existing vt parser tests to confirm no regression).

---

## Interdependencies

- Best written after **02-bug** fix establishes the cap constants to test against.
