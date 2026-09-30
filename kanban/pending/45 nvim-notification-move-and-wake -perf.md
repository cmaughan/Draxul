# Move decoded Neovim notifications and coalesce wakes

**Source:** `libs/draxul-nvim/src/rpc.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 356–384 copy the decoded notification parameter tree into a queue and invoke the wake callback for each notification. Full-screen redraw strings and vectors are therefore deep-copied before GUI processing; burst wakes are not coalesced.

- [ ] **Baseline:** Count copied bytes, allocations, and SDL wakes for a fixed burst of redraw notifications.
- [ ] **Implement:** Move validated notification payloads and use an empty-to-nonempty wake flag acknowledged on drain.
- [ ] **Functional safety:** Preserve request/response routing, queue bounds, EOF wake, and races around drain and requeue.
- [ ] **Compare:** Require no decoded-tree deep copy and bounded wakes per burst.
- [ ] **Platforms:** Run RPC and Neovim integration on Windows/macOS, aggregate and smoke.
- [ ] **Acceptance:** Bursty notifications reach the GUI with fewer copies and wake events.
