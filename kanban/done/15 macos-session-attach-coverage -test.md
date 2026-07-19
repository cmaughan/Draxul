# Restore macOS session-attach coverage

**Type:** test
**Priority:** 15
**Raised by:** GPT/Codex, Claude, Gemini

## Gap

`tests/CMakeLists.txt` removes all of `session_attach_tests.cpp` on Apple to avoid a Catch2/libc++ `__int128` formatting conflict. That drops the suite on the platform using Unix-domain sockets.

## Implementation plan

- [ ] Reproduce the Apple compile error with the current pinned Catch2 and identify the exact assertion/captured chrono type.
- [ ] Prefer a local test-expression fix (explicit duration/count conversion or matcher) if it restores coverage without weakening diagnostics.
- [ ] If the issue is library-wide, evaluate a pinned Catch2 update and record the reason/version in CMake.
- [ ] Remove the Apple source filter from `tests/CMakeLists.txt`.
- [ ] Split protocol-only and OS-transport sections with tags so failures are easy to isolate.
- [ ] Ensure Unix socket paths, permissions, cleanup, stale-owner handling, detach, rename, save-as, kill, and reattach cases run on macOS.

## Verification

- [ ] Prove `ctest -N` includes the suite on Windows and macOS.
- [ ] Run focused session-attach tests repeatedly to expose path/cleanup flakiness.
- [ ] Run the macOS ASan preset and normal full suite.

## Acceptance criteria

- [ ] No platform removes the entire source file.
- [ ] Shared protocol and POSIX transport behavior are covered on macOS.
- [ ] Windows named-pipe coverage remains intact.

## Dependencies and parallelism

Required before `25 session-attach-platform-split -refactor.md`. A test-focused agent can own it independently.

<model>GPT-5 Codex</model>
