# Suppress rendering requests from hidden Neovim grids

**Summary:** Stop hidden Neovim panes from requesting visible-window redraws so background output does not waste drawing work while their contents remain ready for reopening.

**Source:** `libs/draxul-host/src/grid_host_base.cpp`  
**Priority:** P2; static, high confidence. **Reported by:** Claude. `flush_grid()` at lines 222–240 requests a frame, and cursor updates at 397–454 can do likewise, without a presentation-visibility gate. Hidden remote terminals already suspend delivery; hidden Neovim hosts continue ingesting and can trigger visible-window renders.

- [ ] **Baseline:** Count foreground frames while a hidden Neovim pane outputs and blinks.
- [ ] **Implement:** Track presentation visibility, suppress hidden frame requests and unfocused blink work, then invalidate once on reveal.
- [ ] **Functional safety:** Keep semantic ingestion, title/atlas state, cursor correctness, and reveal convergence.
- [ ] **Compare:** Require no output-driven foreground render for a wholly hidden host.
- [ ] **Platforms:** Check tab/Space switching and render output on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Hidden Neovim activity does not render the visible tree needlessly.
