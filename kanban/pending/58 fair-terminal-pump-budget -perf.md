# Bound terminal pumping ahead of server commands

**Summary:** Limit how much terminal output the server processes at once so busy panes cannot keep keyboard input and other commands waiting indefinitely.

**Source:** `libs/draxul-server/src/server_kernel_lifecycle.cpp`  
**Priority/evidence:** P2; static, medium confidence. **Reported by:** Claude, narrowed in verification. Lines 418–437 pump terminal output before pending control and stream commands at 554; `server_terminal_runtime.cpp:406–424` feeds returned chunks without a processing-time budget. Multiple sustained-output panes can delay interactive commands.

- [ ] **Baseline:** Measure keypress and delivery p95/p99, terminal throughput, and retained bytes with several busy panes.
- [ ] **Implement:** Bound each terminal’s work and dispatch commands fairly while retaining unprocessed data and VT order.
- [ ] **Functional safety:** Preserve synchronized-output atomicity, PTY backpressure, shutdown, and per-terminal fairness.
- [ ] **Compare:** Require bounded command latency without an unbounded queue or major throughput regression.
- [ ] **Platforms:** Run server/client integration on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** A busy output pump cannot indefinitely defer ready commands.
