# WI 36 — process-arg-quoting-dedup

**Type:** refactor  
**Priority:** Low (Windows-only, cosmetic risk)  
**Platform:** Windows only  
**Source:** review-consensus.md §6d — Claude (bug review follow-up to WI 84)  
**Produced by:** claude-sonnet-4-6

---

## Problem

`quote_windows_arg` is independently implemented in two files:

- `libs/draxul-nvim/src/nvim_process.cpp` (Windows path, ~lines 74–100): the reference correct implementation
- `libs/draxul-host/src/conpty_process.cpp` (Windows path, ~lines 15–39): the copy that WI 84 patched

These two functions now implement the same logic but are maintained separately. If either is extended (e.g., to handle Unicode `LPWSTR` or add new escape rules), the other will drift again — the exact failure mode that produced WI 84.

---

## Investigation

- [ ] Read `libs/draxul-nvim/src/nvim_process.cpp` Windows section — find `quote_windows_arg`, note its signature and exact logic.
- [ ] Read `libs/draxul-host/src/conpty_process.cpp` Windows section — find the local copy and confirm it is identical after WI 84's fix.
- [ ] Check if any other files in `libs/` contain a third copy of this function.

---

## Fix Strategy

### Step 1: Extract to a shared Windows utility header

- [ ] Create `libs/draxul-host/include/draxul/windows_process_util.h` (or a `src/` private header if it's only used internally) with:
  ```cpp
  #ifdef _WIN32
  // Returns the argument wrapped in double-quotes with internal quotes and trailing backslashes escaped.
  std::string quote_windows_arg(std::string_view arg);
  #endif
  ```
- [ ] Move the implementation to a corresponding `.cpp` (or keep it `inline` in the header for a single-TU shared internal).

### Step 2: Replace both call sites

- [ ] In `nvim_process.cpp`: replace the local definition with a `#include` of the shared header.
- [ ] In `conpty_process.cpp`: same.

### Step 3: Update unit tests

- [ ] The unit tests added in WI 84 (covering: no-quote-needed, spaces, embedded quotes, single trailing `\`, multiple trailing `\`) should move to test the shared function directly, not the per-file copy.

---

## Acceptance Criteria

- [ ] `quote_windows_arg` exists in exactly one location in the codebase.
- [ ] All WI 84 unit tests still pass after the refactor.
- [ ] Windows build succeeds: `cmake --preset release && cmake --build build --config Release --target draxul`
- [ ] Smoke test passes on Windows: `py do.py smoke`

---

## Notes

- This is a pure refactor; zero functional change.
- Low risk: the logic is not changed, only where it lives.
- Windows-only change; macOS build is unaffected.
- A sub-agent can do this independently — no other WIs block it.
