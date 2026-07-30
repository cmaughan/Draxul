# Atomic session persistence

**Type:** bug
**Priority:** 02
**Raised by:** GPT/Codex, Gemini

## Problem

Server checkpoints already use the shared session-state codec's sibling
temporary file, durable flush, and atomic replacement path. The remaining work
is to turn that implementation into a reusable runtime-support primitive and
strengthen interrupted/corrupt-file recovery without changing server ownership.

## Implementation plan

- [ ] Add a reusable atomic-file writer in `draxul-runtime-support` with a sibling temporary file, write/flush/close checks, and platform-correct replace semantics.
- [ ] Flush file contents before replacement and, where available, flush the containing directory; document the durability guarantee on Windows and macOS.
- [ ] Preserve file permissions and remove abandoned temporary files on the next load.
- [x] Server Session topology checkpoints avoid in-place `std::ios::trunc`
  replacement and preserve the prior file on pre-replace failure.
- [ ] Convert remaining checkpoint/metadata writers to the reusable helper.
- [ ] On load, distinguish missing, corrupt, and interrupted files; quarantine corrupt files and prefer a verified backup only when one exists.
- [ ] Keep serialization pure so fault tests can inject failures between stages.

## Tests

- [ ] Inject failure at create, write, flush, close, replace, and directory-flush stages.
- [ ] Prove the previous valid file survives every pre-replace failure.
- [ ] Prove a successful replace yields complete parseable TOML for topology and metadata.
- [ ] Cover abandoned temp/backup recovery. Concurrent writes for one Session
  are serialized by the single server owner; test that invariant rather than
  recreating multiple file writers.

## Acceptance criteria

- [ ] Final session files are never opened with `std::ios::trunc` for in-place replacement.
- [ ] A failed save returns a useful error and leaves a loadable prior state.
- [ ] Session-state tests, `ctest`, and smoke pass on Windows; the implementation is inspected/tested on macOS.

## Dependencies and parallelism

Blocks `40 crash-recovery-session-journal -feature.md` and contributes to item 48. Can proceed independently of the attach protocol work.

<model>GPT-5 Codex</model>
