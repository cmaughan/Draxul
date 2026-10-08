# Prepare one presentation for already queued Neovim redraws

**Summary:** Combine drawing preparation for Neovim updates that are already waiting so the application does not prepare several intermediate screens before displaying only the last one.

**Source:** `libs/draxul-host/src/nvim_host.cpp`  
**Priority:** P2; static, high confidence. **Reported by:** Codex. `pump()` drains notifications at lines 194–201; each `flush` prepares cells and glyphs at 464–475, while App presents after pumping (`app.cpp:2490–2494,2536–2539`). A queued macro can prepare the same area repeatedly before one presentation.

- [ ] **Baseline:** Replay 1, 10, and 100 complete queued redraw transactions; count preparations, dirty cells, and pump time.
- [ ] **Implement:** Apply semantic events in order but coalesce GPU preparation across already available complete transactions.
- [ ] **Functional safety:** Cover first-flush startup, resize, atlas reset, cursor state, and a trailing incomplete transaction.
- [ ] **Compare:** Require one final preparation for a complete queued batch with identical final cells and cursor.
- [ ] **Platforms:** Check Neovim integration and Vulkan/Metal render output, aggregate and smoke.
- [ ] **Acceptance:** Intermediate queued flushes do not trigger redundant presentation preparation.
