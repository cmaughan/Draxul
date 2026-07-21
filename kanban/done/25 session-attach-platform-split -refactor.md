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

### Validation status (2026-07-21)

Windows Release automated coverage is green: `t.bat release` built the full
tree and passed all 22 CTest entries, including shared fake-transport protocol,
named-pipe integration/security, session CLI, and app smoke coverage. The
security test was repeated directly three times in Release and passed all three.
The user also completed a PC visual check with no behavior regression observed.

The Debug build, app smoke, and all unrelated tests passed, but
`session attach Windows pipe is owned by and grants access to the current user`
fails deterministically: the queried pipe owner does not equal the current token
user SID and the expected user access entry is not found. The exact case failed
three out of three direct Debug reruns, while passing three out of three Release
reruns, so this is a repeatable configuration-specific validation blocker rather
than a suite-order flake. Both acceptance items remain open for that Debug issue
and for macOS execution.

## Dependencies and parallelism

Depends on item 15. Protocol, Windows, and POSIX moves can use separate sub-agents only after the internal interface and tests are committed.

<model>GPT-5 Codex</model>
