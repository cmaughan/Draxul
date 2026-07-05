# Split session attach by protocol and platform

**Type:** refactor
**Priority:** 25
**Raised by:** GPT/Codex, Claude

## Goal

Split the 1,385-line `session_attach.cpp` into shared framing/protocol, Windows named-pipe security/transport, POSIX socket transport, and small client/server orchestration without changing the wire protocol.

## Implementation plan

- [ ] Restore macOS coverage in item 15 and record protocol bytes/behavior in tests.
- [ ] Define internal `SessionTransport` read/write/accept/close primitives and shared deadline/error types.
- [ ] Move serialization, framing, command parsing, and response validation to `session_attach_protocol.cpp`.
- [ ] Move SID/DACL/integrity/named-pipe code to `session_attach_win.cpp`.
- [ ] Move Unix-socket path, permissions, accept/connect, and cleanup to `session_attach_posix.cpp`.
- [ ] Keep public `SessionAttachServer`/client functions source-compatible; orchestration selects the private backend in CMake.
- [ ] Preserve cancellation, wakeup, bounded shutdown, and security checks.
- [ ] Land protocol extraction before platform moves to keep diffs reviewable.

## Tests and acceptance

- [ ] Shared protocol tests run on both platforms with fake transports and malformed/partial frames.
- [ ] Platform integration suites cover permissions/security, stale endpoint cleanup, timeouts, and shutdown.
- [ ] Existing clients interoperate with the unchanged protocol.
- [ ] No interleaved platform `#ifdef` blocks remain in the shared protocol implementation.
- [ ] Full session tests, `ctest`, and smoke pass on Windows and macOS.

## Dependencies and parallelism

Depends on item 15. Protocol, Windows, and POSIX moves can use separate sub-agents only after the internal interface and tests are committed.

<model>GPT-5 Codex</model>
