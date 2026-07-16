# Atomic session persistence

**Type:** bug
**Priority:** 02
**Raised by:** GPT/Codex, Gemini

## Problem

`app/session_state.cpp` writes topology and runtime metadata directly to their final paths with `std::ios::trunc`. A crash, full disk, or power loss can destroy the last valid session.

## Implementation plan

- [ ] Add a reusable atomic-file writer in `draxul-runtime-support` with a sibling temporary file, write/flush/close checks, and platform-correct replace semantics.
- [ ] Flush file contents before replacement and, where available, flush the containing directory; document the durability guarantee on Windows and macOS.
- [ ] Preserve file permissions and remove abandoned temporary files on the next load.
- [ ] Convert both session topology and metadata writers to the helper.
- [ ] On load, distinguish missing, corrupt, and interrupted files; quarantine corrupt files and prefer a verified backup only when one exists.
- [ ] Keep serialization pure so fault tests can inject failures between stages.

## Tests

- [ ] Inject failure at create, write, flush, close, replace, and directory-flush stages.
- [ ] Prove the previous valid file survives every pre-replace failure.
- [ ] Prove a successful replace yields complete parseable TOML for topology and metadata.
- [ ] Cover abandoned temp/backup recovery and concurrent attempts for the same session id.

## Acceptance criteria

- [ ] Final session files are never opened with `std::ios::trunc` for in-place replacement.
- [ ] A failed save returns a useful error and leaves a loadable prior state.
- [ ] Session-state tests, `ctest`, and smoke pass on Windows; the implementation is inspected/tested on macOS.

## Dependencies and parallelism

Blocks `40 crash-recovery-session-journal -feature.md` and contributes to item 48. Can proceed independently of the attach protocol work.

<model>GPT-5 Codex</model>
