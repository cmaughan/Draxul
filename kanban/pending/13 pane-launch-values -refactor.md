# Narrow pane launch dependencies

**Summary:** Give pane creation only the settings it needs so changes to unrelated application or graphics options do not spread into pane-management code.

**Priority:** P2 — PaneManager and render tests inherit the whole graphics-heavy `AppOptions`.  
**Source:** `app/pane_manager.h`  
**Proposed by:** Claude 27. **Owner:** one app/pane agent. **Coordinates with:** cards 04 and 30.  
**Evidence:** PaneManager receives `AppOptions` and resolves three host-factory paths; render-test header also returns `AppOptions`.

**Boundary verification**
- [ ] Record launch defaults, environment and test-override precedence.
**Implementation and migration**
- [ ] Add app-owned `PaneLaunchDefaults`, resolved factory and registry reference; move neutral render scenario values out of `AppOptions` callers.
**Unit tests**
- [ ] Verify overrides, unavailable hosts and projected pane creation.
**Cross-platform validation**
- [ ] Check default shell/environment on Windows/macOS, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Clarify AppOptions ownership in module map.
**Acceptance criteria**
- [ ] PaneManager’s public contract no longer requires the whole app option bundle.
