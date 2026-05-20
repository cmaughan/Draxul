# 17 clipboard-test-migration -refactor

**Priority:** MEDIUM
**Type:** Refactor
**Raised by:** Gemini
**Model:** claude-sonnet-4-6

---

## Problem

`tests/clipboard_tests.cpp:9` reimplements production clipboard logic as test helpers rather than calling the real production path in `NvimHost`. The comments in the test file still reference `App` (the old location), but the real logic is in `libs/draxul-host/src/nvim_host.cpp:341` and `:367`. A future change to `nvim_host.cpp`'s clipboard path silently bypasses these tests, giving false confidence.

Note: `clipboard-roundtrip-test` is marked complete, but that may refer to an older integration test. This item is specifically about the test quality issue.

---

## Implementation Plan

- [ ] Read `tests/clipboard_tests.cpp` in full.
- [ ] Read `libs/draxul-host/src/nvim_host.cpp:341` and `:367` (the real clipboard put/get paths).
- [ ] Determine the test seam: what does the test need to invoke the real `NvimHost` clipboard path without launching nvim?
  - Look for existing fakes (e.g., a `MockRpc` or stub that captures RPC calls).
  - If a seam doesn't exist, add one: extract a `ClipboardBridge` or `IClipboardProvider` interface that `NvimHost` delegates to, and inject a fake in tests.
- [ ] Rewrite `clipboard_tests.cpp` to:
  - Construct a `NvimHost` (or its clipboard component) with a stub RPC.
  - Exercise the real `nvim_osc52_paste` / `nvim_osc52_yank` (or equivalent) path.
  - Assert the correct RPC calls are made (not reimplemented logic).
- [ ] Delete the mirrored helper implementations from the test file.
- [ ] Run clang-format on touched files.
- [ ] Run ctest.

---

## Acceptance

- `clipboard_tests.cpp` no longer duplicates production logic.
- A change to the clipboard path in `nvim_host.cpp` causes the test to fail if the behaviour changes (test exercises the real code).
- All clipboard tests pass.

---

## Interdependencies

- No upstream dependencies.
- May require a minor refactor of `NvimHost` to expose a seam — keep it minimal.
