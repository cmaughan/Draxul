# Pace plugin ticks requested inside a tick

**Summary:** Pace plugins that repeatedly request their next update so playback cannot create a busy loop when a window is minimized.

**Source:** `libs/draxul-host/src/plugin_host.cpp`  
**Priority/evidence:** P1; static, high confidence. **Reported by:** Claude. `PluginHost::pump()` at lines 492–515 consumes `tick_pending_`; `request_tick()` at 567–574 immediately wakes SDL. ScoreView requests another tick from its playing pump (`score_runtime.cpp:1741–1742`, `scoreview_plugin.cpp:36–44`), which can spin when minimized and presentation supplies no pacing.

- [ ] **Baseline:** Count ticks, wakeups, redraws, and CPU during visible, minimized, and permitted hidden ScoreView playback.
- [ ] **Implement:** Prevent a tick from immediately rearming itself; use returned deadlines while retaining external asynchronous tick requests.
- [ ] **Functional safety:** Preserve playback cadence, worker completion, visibility policy, and plugin quiescence.
- [ ] **Compare:** Require bounded minimized/hidden tick rate and prompt response to external events.
- [ ] **Platforms:** Exercise ScoreView and generic plugin-host tests on Windows and macOS, then aggregate and smoke.
- [ ] **Acceptance:** Self-requested ticks cannot form an unpaced wake loop.
