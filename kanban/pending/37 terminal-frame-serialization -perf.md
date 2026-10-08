# Remove repeated Session terminal JSON materialization

**Summary:** Avoid repeatedly converting the same terminal update into and out of text when sending it to a window so delivery requires less copying and processing.

**Source:** `libs/draxul-server/src/session_stream_service.cpp`  
**Priority:** P2; static, high confidence. **Reported by:** Claude, Codex. `remote_terminal_service.cpp:383–415` builds, sizes, and retains an unused JSON tree; `session_stream_service.cpp:179–187,508–514` serializes a queued frame, reparses it, then dumps it again to assign `frame_serial`. Queue limits bound frames but not repeated full-payload work.

- [ ] **Baseline:** Count parsed/dumped bytes, allocations, server CPU, `steady_frame_bytes`, and `terminal_delivery_latency_us` under the existing multi-pane load test.
- [ ] **Implement:** Remove the dead retained tree and add the serial-bearing envelope without reparsing the complete payload.
- [ ] **Functional safety:** Assign serial at dequeue, preserving priority overtaking, schema, replacement-safe encoding, and conservative byte accounting.
- [ ] **Compare:** Require zero whole-payload writer parses and report before/after latency and throughput.
- [ ] **Platforms:** Check POSIX Session sockets and Windows named pipes, aggregate and smoke.
- [ ] **Acceptance:** Terminal frames retain protocol semantics with fewer full-payload conversions.
