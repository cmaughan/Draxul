# Remove overlay-driven idle app polling

**Summary:** Let the application sleep when panels have nothing to update so an idle window does less background work while still responding promptly to new output.

**Source:** `app/app.cpp`  
**Priority:** P1; static, high confidence. **Reported by:** Claude. `wait_timeout_ms()` at lines 3201–3250 caps waits at 50 ms when any visible host requests periodic wake. Event-driven chrome, palette, toast, and diagnostics hosts inherit or report that condition, so an otherwise idle window repeatedly executes `pump_once()`.

- [ ] **Baseline:** Count app-loop passes and per-process wakeups over 60 seconds with remote-only panes, then with Neovim and active deadlines.
- [ ] **Implement:** Mark event/deadline-driven overlays explicitly, schedule monitor/weather work, and avoid duplicate agent projection per pass.
- [ ] **Functional safety:** Retain prompt remote and Neovim output through the documented macOS SDL lost-wake race; checking a ready flag before sleep alone is insufficient.
- [ ] **Compare:** Report avoidable idle passes and output latency before/after, excluding legitimate deadlines.
- [ ] **Platforms:** Check SDL wake behavior on macOS and Windows, aggregate tests, and same-cache smoke.
- [ ] **Acceptance:** Inert overlays no longer impose a 50 ms loop while background output remains prompt.
