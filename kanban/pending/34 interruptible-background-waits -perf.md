# Replace avoidable timed cancellation polls

**Summary:** Wake background tasks only for work, real deadlines, or shutdown so they stop repeatedly checking for cancellation while idle.

**Source:** `libs/draxul-control/src/async_frame_stream_posix.cpp`  
**Priority:** P2; static, high confidence. **Reported by:** Claude. Stream and listener paths poll at 50–100 ms in POSIX and Windows implementations; weather’s worker wakes every 100 ms between fetches (`weather_service.cpp:129–131`). These are idle cancellation checks, distinct from real heartbeats or write deadlines.

- [ ] **Baseline:** Measure idle wakeups by thread/process and shutdown latency with no payload, active connections, and configured weather.
- [ ] **Implement:** Use interruptible waits with owned stop signalling; retain finite write deadlines and a reliable POSIX listener wake mechanism.
- [ ] **Functional safety:** Check handle lifetime, concurrent close, blocked read/accept, stream-shutdown regression, and weather disable.
- [ ] **Compare:** Report avoidable wakes and cancellation time before/after, excluding heartbeats and fetches.
- [ ] **Platforms:** Verify socket/pipe cancellation on macOS and Windows; run aggregate and same-cache smoke.
- [ ] **Acceptance:** Idle threads sleep until work, a real deadline, or cancellation.
