# Avoid duplicate anchored scrollback retrieval and full presentation rebuild

**Summary:** Reuse unchanged terminal history while new output arrives so reading older lines does not repeatedly fetch and rebuild the same view.

**Source:** `libs/draxul-client/src/remote_session_coordinator.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 1028–1046 can request the current offset and then the anchored offset after new rows; `remote_terminal_host.cpp:1225–1228` rebuilds the whole display grid while scrolled back. Continuing output therefore repeats round trips and cell work for a fixed history view.

- [ ] **Baseline:** Count scrollback requests, copied cells, rebuild cells, and delivery latency while output continues behind a fixed anchor.
- [ ] **Implement:** Request an anchored page atomically and update only changed visible rows where its semantics allow.
- [ ] **Functional safety:** Preserve ring overflow, resize, attributes, selection, and independent multi-client viewports.
- [ ] **Compare:** Report requests/rebuilds and GUI latency before/after.
- [ ] **Platforms:** Check remote terminal integration on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Stable anchored views do not repeatedly fetch or rebuild unchanged content.
