# Avoid per-host full clipboard polling

**Source:** `libs/draxul-host/src/nvim_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 186–203 and 558–576 read/copy the OS clipboard every 500 ms per Neovim host, including hidden hosts. The gate limits frequency but not copies of a large clipboard across panes.

- [ ] **Baseline:** Count clipboard reads, copied bytes, and GUI time with several hosts and a fixed 5 MiB clipboard.
- [ ] **Implement:** Use a shared main-thread cache driven by clipboard/focus events, with a bounded fallback where event delivery needs one.
- [ ] **Functional safety:** Preserve external changes, Neovim clipboard requests, `clipboard_set`, focus transitions, and reader-thread access.
- [ ] **Compare:** Require reads to follow changes rather than host count during idle.
- [ ] **Platforms:** Check SDL clipboard behavior on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Multiple idle Neovim panes do not each copy unchanged clipboard contents.
