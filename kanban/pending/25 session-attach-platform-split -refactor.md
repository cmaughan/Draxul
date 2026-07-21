# Split session attach by protocol and platform

**Type:** refactor
**Priority:** 25
**Raised by:** GPT/Codex, Claude

## Goal

Split the 1,385-line `session_attach.cpp` into shared framing/protocol, Windows named-pipe security/transport, POSIX socket transport, and small client/server orchestration without changing the wire protocol.

## Implementation plan

- [x] Restore macOS coverage in item 15 and record protocol bytes/behavior in tests.
- [x] Define internal `SessionTransport` read/write/accept/close primitives and shared deadline/error types.
- [x] Move serialization, framing, command parsing, and response validation to `session_attach_protocol.cpp`.
- [x] Move SID/DACL/integrity/named-pipe code to `session_attach_win.cpp`.
- [x] Move Unix-socket path, permissions, accept/connect, and cleanup to `session_attach_posix.cpp`.
- [x] Keep public `SessionAttachServer`/client functions source-compatible; orchestration selects the private backend in CMake.
- [x] Preserve cancellation, wakeup, bounded shutdown, and security checks.
- [x] Land protocol extraction before platform moves to keep diffs reviewable.

## Tests and acceptance

- [ ] Shared protocol tests run on both platforms with fake transports and malformed/partial frames.
- [x] Platform integration suites cover permissions/security, stale endpoint cleanup, timeouts, and shutdown.
- [x] Existing clients interoperate with the unchanged protocol.
- [x] No interleaved platform `#ifdef` blocks remain in the shared protocol implementation.
- [ ] Full session tests, `ctest`, and smoke pass on Windows and macOS.

Build-only validation is required for this work session: the shared fake-transport
suite now covers fragmented, malformed, and truncated live-session frames without
opening a platform endpoint. Windows coverage inspects named-pipe ownership/DACLs
and endpoint exclusivity; POSIX coverage retains socket ownership, stale cleanup,
wire-byte, and unlink checks. Runtime execution remains unchecked above until the
Windows and macOS suites can be launched safely. Build-only validation passed on
Windows in Release for `draxul-runtime-support` and `draxul-test-app`; no test or
application executable was launched.

## Dependencies and parallelism

Depends on item 15. Protocol, Windows, and POSIX moves can use separate sub-agents only after the internal interface and tests are committed.

<model>GPT-5 Codex</model>
