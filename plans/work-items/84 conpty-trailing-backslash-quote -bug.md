# WI 84 — conpty-trailing-backslash-quote

**Type:** bug  
**Priority:** 0 (crash / argument corruption — Windows only)  
**Source:** review-bugs-consensus.md §C3 [Gemini]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`quote_windows_arg()` in `libs/draxul-host/src/conpty_process.cpp:15–39` tracks a `backslashes` counter inside the loop but does not double those backslashes before appending the closing `"`. If an argument ends in one or more backslashes (e.g., a Windows directory path `C:\foo\`), the resulting string is `"C:\foo\"`. Windows command-line parsing interprets the final `\"` as an escaped literal quote, consuming the rest of the command line into one corrupted argument or causing `CreateProcess` to fail.

The equivalent fix already exists in `nvim_process.cpp`'s `quote_windows_arg` but was not ported to `conpty_process.cpp`.

---

## Investigation

- [ ] Read `libs/draxul-host/src/conpty_process.cpp:15–39` — confirm `backslashes` is not doubled before the closing quote.
- [ ] Read the corresponding function in `libs/draxul-nvim/src/nvim_process.cpp` (Windows path) — use it as the reference for the correct implementation.
- [ ] Check whether any other files in `libs/draxul-host/src/` contain a local copy of this quoting logic.

---

## Fix Strategy

- [ ] Before `quoted.push_back('"');` at line 38, add `quoted.append(backslashes, '\\');` to double the trailing backslashes.
- [ ] Ensure the fix matches the `nvim_process.cpp` version exactly.
- [ ] Add a unit test in the appropriate test file (or `tests/`) covering: no-quote needed, spaces, embedded quotes, single trailing backslash, multiple trailing backslashes.
- [ ] Build (Windows): `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] `quote_windows_arg("C:\\foo\\")` → `"\"C:\\foo\\\\\""` (two backslashes before closing quote).
- [ ] `CreateProcess` succeeds when all arguments are directories ending in `\`.
- [ ] Smoke test passes.
- [ ] Unit test added and passing.
