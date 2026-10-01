# Separate Kanban timing and selection policy from host effects

**Summary:** Separate Kanban's timing and selection rules from drawing and file access so keyboard repeat, focus changes, and previews can be tested without unreliable waits.

**Priority:** P2 — watcher, repeat, focus and preview timing share a render host.  
**Source:** `modules/kanban/draxul-kanban/src/kanban_host.cpp`  
**Proposed by:** Claude 40; Codex 8. **Owner:** one built-in Kanban agent.  
**Evidence:** host pump mixes deadlines and effects; host tests initialize fonts and use timed waits; completed focus/preview cards show interference risk.

**Boundary verification**
- [ ] Characterize focus loss, deferred preview, held-key repeat and watcher invalidation.
**Implementation and migration**
- [ ] Add value-oriented interaction/session owner in `draxul-kanban` taking timestamps/events and returning due commands; migrate timing first, pure selection transitions next. Keep monitor, disk, callbacks and drawing in host.
**Unit tests**
- [ ] Deterministic timeline tests in Kanban core; retain native monitor/host integration.
**Cross-platform validation**
- [ ] Verify native watchers on Windows/macOS, aggregate and smoke.
**Agent documentation and tooling**
- [ ] State pure policy versus host-effect ownership.
**Acceptance criteria**
- [ ] Policy tests use explicit time and preserve deferred preview after focus returns.
