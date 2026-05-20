# 12 file-drop-special-chars -test

**Priority:** MEDIUM
**Type:** Test
**Raised by:** Claude, Gemini
**Model:** claude-sonnet-4-6

---

## Problem

`app/input_dispatcher.cpp:111–114` constructs an `open_file:` action string by concatenating the dropped path. No test exercises paths containing:
- Colons (`:` — meaningful in the action string format)
- Spaces
- Unicode characters
- Shell-sensitive characters (`$`, `!`, backtick, etc.)

A path like `/foo:bar/file.txt` could be misparse as action `open_file` with parameter `bar/file.txt`, losing the `/foo` prefix.

Gemini similarly notes drag-drop/open-file tests with Unicode and special characters are missing.

---

## Implementation Plan

- [x] Read `app/input_dispatcher.cpp` around the file-drop path and the `open_file:` action format.
- [x] Understand how the path is extracted from the action string on the receiving end.
  - Finding: `NvimHost::dispatch_action` uses `action.substr(10)` (fixed-length prefix extraction), which is safe for colon-containing paths. No encoding fix needed.
- [x] Write `tests/file_drop_tests.cpp`:
  - Simulate the encode/decode round-trip for paths:
    - `/normal/path/file.txt` (baseline)
    - `/path with spaces/file.txt`
    - `/path:with:colons/file.txt`
    - `/path/with/ünïcödé/file.txt` (UTF-8)
    - `/path/$SPECIAL/file.txt`
    - CJK, shell-sensitive chars, very long paths, etc.
  - For each: verify the host receives the full, unmodified path.
- [x] No encoding fix needed: the `substr(10)` approach is already correct for all special characters including colons.
- [x] Add to `tests/CMakeLists.txt`.
- [x] Run ctest.

---

## Acceptance

- File-drop with colon-containing path: host receives the complete original path, not a truncated/misparse version.
- All special-character variants pass.

---

## Interdependencies

- Relates to **05-bug** (Windows command-line quoting) — similar theme of path-handling fragility but on a different codepath.
